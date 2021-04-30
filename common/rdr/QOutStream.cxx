/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman for Cendio AB
 * Copyright 2017 Peter Astrand <astrand@cendio.se> for Cendio AB
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
#include <rdr/QOutStream.h>
#include <rfb/LogWriter.h>

using namespace rdr;
using namespace quiche;

static rfb::LogWriter vlog("QOutStream");

QOutStream::QOutStream(int fd_, quiche_conn *q_conn_)
    : FdOutStream(fd_), q_conn{q_conn_} {}

QOutStream::~QOutStream() {}

bool QOutStream::flushBuffer() {
  size_t n = writeFd((const void *)sentUpTo, ptr - sentUpTo);
  if (n == 0) return false;

  sentUpTo += n;

  return true;
}

//
// writeFd() writes up to the given length in bytes from the given
// buffer to the file descriptor. It returns the number of bytes written.  It
// never attempts to send() unless select() indicates that the fd is writable
// - this means it can be used on an fd which has been set non-blocking.  It
// also has to cope with the annoying possibility of both select() and send()
// returning EINTR.
//

size_t QOutStream::writeFd(const void *data, size_t length) {
  size_t write = 0;
  if (quiche_conn_is_established(q_conn)) {
    write =
        quiche_conn_stream_send(q_conn, 4, (const uint8_t *)data, length, true);
    if (write < 0) {
      vlog.error("fail to send\n");
      throw SystemException("write", errno);
    }
  }

  gettimeofday(&lastWrite, NULL);

  return write;
}
