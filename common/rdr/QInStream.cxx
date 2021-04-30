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
#include <inttypes.h>
#include <rdr/QInStream.h>
#include <rfb/LogWriter.h>

using namespace rdr;

static rfb::LogWriter vlog("QInStream");

QInStream::QInStream(int fd_, conn_io *conn_, bool close_when_done_)
    : FdInStream(fd_, close_when_done_), conn{conn_} {}

QInStream::~QInStream() {}

bool QInStream::fillBuffer(size_t maxSize) {
  size_t n = readFd((U8 *)end, maxSize);
  if (n == 0) return false;
  end += n;

  return true;
}

size_t QInStream::readFd(void *buf, size_t len) {
  uint8_t *buffer = (uint8_t *)buf;
  size_t recv_len = 0, curr_len = len;
  if (quiche_conn_is_established(conn->q_conn)) {
    uint64_t s = 0;

    quiche_stream_iter *readable = quiche_conn_readable(conn->q_conn);

    while (quiche_stream_iter_next(readable, &s)) {
      vlog.info("stream %" PRIu64 " is readable\n", s);

      bool fin = false;
      size_t curr_recv_len =
          quiche_conn_stream_recv(conn->q_conn, s, buffer, curr_len, &fin);
      if (curr_recv_len < 0 || curr_len == curr_recv_len) {
        vlog.info("stream %" PRIu64 " over, read %zd bytes, expect %zd bytes\n",
                  s, recv_len, len);
        break;
      }
      buffer += curr_recv_len;
      recv_len += curr_recv_len;
      curr_len -= curr_recv_len;

      if (fin) {
        static const char *resp = "byez\n";
        quiche_conn_stream_send_full(conn->q_conn, s, (uint8_t *)resp, 5, true,
                                     200, 99999999999999);
        vlog.info("send: %s", resp);
      }
    }
    quiche_stream_iter_free(readable);
  }
  return recv_len;
}