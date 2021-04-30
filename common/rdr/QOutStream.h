/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman for Cendio AB
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
// FdOutStream streams to a file descriptor.
//

#ifndef __RDR_QOUTSTREAM_H__
#define __RDR_QOUTSTREAM_H__

#include <quiche/quicheConfig.h>
#include <rdr/FdOutStream.h>

namespace rdr {

class QOutStream : public FdOutStream {
 public:
  QOutStream(int fd_, quiche_config *config_, conn_io *conns_,
             bool close_when_done_ = false);
  virtual ~QOutStream();

  int getFd() { return fd; }

  unsigned getIdleTime();

  virtual void cork(bool enable);

 private:
  virtual bool flushBuffer() override;
  virtual size_t writeFd(const void *data, size_t length) override;
  int fd;
  struct timeval lastWrite;
};

}  // namespace rdr

#endif  // __RDR_QOUTSTREAM_H__
