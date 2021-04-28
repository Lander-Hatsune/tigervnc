/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <rdr/QSInStream.h>
#include <rfb/LogWriter.h>
#include <unistd.h>

using namespace rdr;

static rfb::LogWriter vlog("QSInstream");

QSInStream::QSInStream(int fd_, quiche_config *config_, conn_io *conns_,
                       bool close_when_done_ = false)
    : FdInStream(fd_, close_when_done_), config{config_}, conns{conns_} {}

QSInStream::~QSInStream() {}

bool QSInStream::fillBuffer(size_t maxSize) {
  size_t n = readFd((U8 *)end, maxSize);
  if (n == 0) return false;
  end += n;

  return true;
}

size_t QSInStream::readFd(void *buf, size_t len) {
  struct conn_io *tmp, *conn = NULL;
  static uint8_t out[MAX_DATAGRAM_SIZE];

  ssize_t read, recv_len = 0;
  while (1) {
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    memset(&peer_addr, 0, peer_addr_len);

    read = recvfrom(fd, buf, 65535, 0, (struct sockaddr *)&peer_addr,
                    &peer_addr_len);

    if (read < 0) {
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
        vlog.error("read would block\n");
        break;
      }

      vlog.error("fail to read\n");
      break;
    }

    uint8_t type;
    uint32_t version;

    uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
    size_t scid_len = sizeof(scid);

    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
    size_t dcid_len = sizeof(dcid);

    uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
    size_t odcid_len = sizeof(odcid);

    uint8_t token[MAX_TOKEN_LEN];
    size_t token_len = sizeof(token);

    int rc = quiche_header_info((uint8_t *)buf, read, LOCAL_CONN_ID_LEN,
                                &version, &type, scid, &scid_len, dcid,
                                &dcid_len, token, &token_len);
    if (rc < 0) {
      vlog.error("failed to parse header: %d\n", rc);
      continue;
    }

    HASH_FIND(hh, conns, dcid, dcid_len, conn);

    if (conn == NULL) {
      if (!quiche_version_is_supported(version)) {
        vlog.error("version negotiation\n");

        ssize_t written = quiche_negotiate_version(scid, scid_len, dcid,
                                                   dcid_len, out, sizeof(out));

        if (written < 0) {
          vlog.error("failed to create vneg packet: %zd\n", written);
          continue;
        }

        ssize_t sent = sendto(fd, out, written, 0,
                              (struct sockaddr *)&peer_addr, peer_addr_len);
        if (sent != written) {
          vlog.error("failed to send\n");
          continue;
        }

        vlog.info("sent %zd bytes\n", sent);
        continue;
      }

      if (token_len == 0) {
        vlog.error("stateless retry\n");

        mint_token(dcid, dcid_len, &peer_addr, peer_addr_len, token,
                   &token_len);

        ssize_t written =
            quiche_retry(scid, scid_len, dcid, dcid_len, dcid, dcid_len, token,
                         token_len, out, sizeof(out));

        if (written < 0) {
          vlog.error("failed to create retry packet: %zd\n", written);
          continue;
        }

        ssize_t sent = sendto(fd, out, written, 0,
                              (struct sockaddr *)&peer_addr, peer_addr_len);
        if (sent != written) {
          vlog.error("failed to send");
          continue;
        }

        vlog.info("sent %zd bytes\n", sent);
        continue;
      }

      if (!validate_token(token, token_len, &peer_addr, peer_addr_len, odcid,
                          &odcid_len)) {
        vlog.error("invalid address validation token\n");
        continue;
      }

      if (!(conn = create_conn(odcid, odcid_len))) {
        continue;
      }

      memcpy(&conn->peer_addr, &peer_addr, peer_addr_len);
      conn->peer_addr_len = peer_addr_len;
    }

    ssize_t done = quiche_conn_recv(conn->q_conn, (uint8_t *)buf, read);

    if (done < 0) {
      vlog.error("failed to process packet: %zd\n", done);
      continue;
    }

    vlog.info("recv %zd bytes\n", done);

    if (quiche_conn_is_established(conn->q_conn)) {
      uint64_t s = 0;

      quiche_stream_iter *readable = quiche_conn_readable(conn->q_conn);

      while (quiche_stream_iter_next(readable, &s)) {
        vlog.info("stream %" PRIu64 " is readable\n", s);

        bool fin = false;
        ssize_t curr_recv_len =
            quiche_conn_stream_recv(conn->q_conn, s, (uint8_t *)buf, len, &fin);
        if (curr_recv_len < 0 || curr_recv_len == len) {
          break;
        }
        vlog.info("recv: length=%ld\n", curr_recv_len);

        if (fin) {
          static const char *resp = "byez\n";
          quiche_conn_stream_send_full(conn->q_conn, s, (uint8_t *)resp, 5,
                                       true, 200, 99999999999999);
          vlog.info("send: %s", resp);
        }
      }
      quiche_stream_iter_free(readable);
    }
  }

  HASH_ITER(hh, conns, conn, tmp) {
    flush_egress(conn);

    if (quiche_conn_is_closed(conn->q_conn)) {
      quiche_stats stats;
      quiche_conn_stats(conn->q_conn, &stats);
      vlog.error(
          "connection[id:%s] closed, recv=%zu sent=%zu lost=%zu "
          "rtt=%" PRIu64 "ns cwnd=%zu\n",
          (char *)conn->cid, stats.recv, stats.sent, stats.lost, stats.rtt,
          stats.cwnd);

      HASH_DELETE(hh, conns, conn);
      quiche_conn_free(conn->q_conn);
      free(conn);
    }
  }
}

void QSInStream::mint_token(const uint8_t *dcid, size_t dcid_len,
                            struct sockaddr_storage *addr, socklen_t addr_len,
                            uint8_t *token, size_t *token_len) {
  memcpy(token, "quiche", sizeof("quiche") - 1);
  memcpy(token + sizeof("quiche") - 1, addr, addr_len);
  memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

  *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

bool QSInStream::validate_token(const uint8_t *token, size_t token_len,
                                struct sockaddr_storage *addr,
                                socklen_t addr_len, uint8_t *odcid,
                                size_t *odcid_len) {
  if ((token_len < sizeof("quiche") - 1) ||
      memcmp(token, "quiche", sizeof("quiche") - 1)) {
    return false;
  }

  token += sizeof("quiche") - 1;
  token_len -= sizeof("quiche") - 1;

  if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
    return false;
  }

  token += addr_len;
  token_len -= addr_len;

  if (*odcid_len < token_len) {
    return false;
  }

  memcpy(odcid, token, token_len);
  *odcid_len = token_len;

  return true;
}

conn_io *QSInStream::create_conn(uint8_t *odcid, size_t odcid_len) {
  struct conn_io *conn = (conn_io *)malloc(sizeof(*conn));
  if (conn == NULL) {
    vlog.error("failed to allocate connection IO\n");
    return NULL;
  }

  int rng = open("/dev/urandom", O_RDONLY);
  if (rng < 0) {
    vlog.error("failed to open /dev/urandom");
    return NULL;
  }

  ssize_t rand_len = read(rng, conn->cid, LOCAL_CONN_ID_LEN);
  if (rand_len < 0) {
    vlog.error("failed to create connection ID");
    return NULL;
  }

  quiche_conn *q_conn =
      quiche_accept(conn->cid, LOCAL_CONN_ID_LEN, odcid, odcid_len, config);
  if (conn == NULL) {
    vlog.error("failed to create connection\n");
    return NULL;
  }
  conn->q_conn = q_conn;

  HASH_ADD(hh, conns, cid, LOCAL_CONN_ID_LEN, conn);

  vlog.info("new connection\n");

  return conn;
}

void QSInStream::flush_egress(conn_io *conn) {
  static uint8_t out[MAX_DATAGRAM_SIZE];

  while (1) {
    ssize_t written = quiche_conn_send(conn->q_conn, out, sizeof(out));

    if (written == QUICHE_ERR_DONE) {
      vlog.info("done writing\n");
      break;
    }

    if (written < 0) {
      vlog.error("failed to create packet: %zd\n", written);
      return;
    }

    ssize_t sent =
        sendto(fd, out, written, 0, (struct sockaddr *)&conn->peer_addr,
               conn->peer_addr_len);
    if (sent != written) {
      vlog.error("failed to send");
      return;
    }

    vlog.info("sent %zd bytes\n", sent);
    // sleep(quiche_conn_timeout_as_nanos(conn->q_conn) / 1e9f);
  }
}