/**
 * @file quicheCommon.cxx
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-05-06
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#include <blink/quicheCommon.h>
#include <rfb/LogWriter.h>

using namespace quiche;

static rfb::LogWriter vlog("quicheCommon");

void quiche::flush_egress(int fd, conn_io *conn) {
  static uint8_t out[MAX_DATAGRAM_SIZE];

  while (1) {
    ssize_t written = quiche_conn_send(conn->q_conn, out, sizeof(out));

    if (written == QUICHE_ERR_DONE) {
      vlog.info("flush_egress: done writing");
      break;
    }

    if (written < 0) {
      vlog.error("flush_egress: failed to create packet: %zd", written);
      QuicheException("flush_egress: failed to create packet", 0);
      return;
    }

    ssize_t sent =
        sendto(fd, out, written, 0, (struct sockaddr *)&conn->peer_addr,
               conn->peer_addr_len);
    if (sent != written) {
      vlog.error("flush_egress: failed to send");
      QuicheException("flush_egress: failed to send", 0);
      return;
    }

    vlog.info("flush_egress: sent %zd bytes", sent);
  }
}