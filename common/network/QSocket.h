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

// -=- QSocket.h - base-class for UDP sockets using Quic on server end.
//     This header also defines the Quic listener class, used
//     to listen for incoming socket connections over UDP

#ifndef __NETWORK_QSOCKET_H__
#define __NETWORK_QSOCKET_H__

#include <network/Socket.h>
#include <quiche/quicheConfig.h>
#include <rdr/Exception.h>
#include <rdr/QInStream.h>
#include <rdr/QOutStream.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h> /* for struct sockaddr_in */
#include <sys/socket.h> /* for socklen_t */
#endif

#include <list>

namespace network {

int findFreeUDPPort(void);
int createUDPSocket(const char* host, int port);

class QSocket : public Socket {
 public:
  QSocket(int sock, quiche::quiche_conn* q_conn_);

  virtual char* getPeerAddress() override;
  virtual char* getPeerEndpoint() override;

  ~QSocket();
};

struct QuicheException : public rdr::SystemException {
  QuicheException(const char* text, int err_)
      : rdr::SystemException(text, err_) {}
};

}  // namespace network

#endif  // __NETWORK_QSOCKET_H__
