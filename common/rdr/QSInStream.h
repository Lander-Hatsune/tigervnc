/* Copyright (C) 2002-2003 RealVNC Ltd.  All Rights Reserved.
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

//
// rdr::QSInstream is a stream on server end using quic and reads data from
// the given socket while modifying quic connections as the same time
//

#ifndef __RDR_QSInStream_H__
#define __RDR_QSInStream_H__

#include <quiche/quicheConfig.h>
#include <rdr/FdInStream.h>

namespace rdr {

class QSInStream : public FdInStream {
 public:
  QSInStream(int fd_, quiche_config *config_, conn_io *conns_,
             bool close_when_done_ = false);
  virtual ~QSInStream();

 private:
  quiche_config *config;
  conn_io *conns;

  virtual bool fillBuffer(size_t maxSize) override;
  virtual size_t readFd(void *buf, size_t len) override;
  void mint_token(const uint8_t *dcid, size_t dcid_len,
                         struct sockaddr_storage *addr, socklen_t addr_len,
                         uint8_t *token, size_t *token_len);

  bool validate_token(const uint8_t *token, size_t token_len,
                             struct sockaddr_storage *addr, socklen_t addr_len,
                             uint8_t *odcid, size_t *odcid_len);
  conn_io *QSInStream::create_conn(uint8_t *odcid, size_t odcid_len);

  void flush_egress(conn_io *conn_io);
};

}  // end of namespace rdr

#endif  // __RDR_QSInStream_H__
