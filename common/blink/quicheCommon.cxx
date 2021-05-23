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

static rfb::LogWriter vlog("flush_egress");

void quiche::flush_egress(int fd, conn_io *conn, bool server) {
  static uint8_t out[MAX_DATAGRAM_SIZE];

  while (1) {
    ssize_t written = quiche_conn_send(conn->q_conn, out, sizeof(out));

    if (written == QUICHE_ERR_DONE) {
      vlog.error("done writing");
      break;
    }

    if (written < 0) {
      vlog.error("failed to create packet");
      return;
    }

    ssize_t sent = server ? sendto(fd, out, written, 0,
                                   (struct sockaddr *)&conn->peer_addr,
                                   conn->peer_addr_len)
                          : send(fd, out, written, 0);
    if (sent != written) {
      vlog.error("fail to send");
      return;
    }

    vlog.error("sent %zd bytes", sent);
  }
}