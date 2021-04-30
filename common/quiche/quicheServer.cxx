/**
 * @file quicheServer.cxx
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-04-30
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#include <fcntl.h>
#include <inttypes.h>
#include <quicheServer.h>
#include <rdr/QSInStream.h>
#include <rfb/LogWriter.h>
#include <unistd.h>

static rfb::LogWriter vlog("quicheServer");

void mint_token(const uint8_t *dcid, size_t dcid_len,
                struct sockaddr_storage *addr, socklen_t addr_len,
                uint8_t *token, size_t *token_len) {
  memcpy(token, "quiche", sizeof("quiche") - 1);
  memcpy(token + sizeof("quiche") - 1, addr, addr_len);
  memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

  *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

bool validate_token(const uint8_t *token, size_t token_len,
                    struct sockaddr_storage *addr, socklen_t addr_len,
                    uint8_t *odcid, size_t *odcid_len) {
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

conn_io *create_conn(uint8_t *odcid, size_t odcid_len, conn_io *conns,
                     quiche_config *config) {
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

void flush_egress(int fd, conn_io *conn) {
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
  }
}