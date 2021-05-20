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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
//#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define errorNumber WSAGetLastError()
#else
#define errorNumber errno
#define closesocket close
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#endif

// #include <ev.h>
#include <fcntl.h>
#include <network/QSocket.h>
#include <network/TcpSocket.h>
#include <rfb/Configuration.h>
#include <rfb/LogWriter.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef WIN32
#include <os/winerrno.h>
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long)-1)
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((unsigned long)0x7F000001)
#endif

#ifndef IN6_ARE_ADDR_EQUAL
#define IN6_ARE_ADDR_EQUAL(a, b) \
  (memcmp((const void *)(a), (const void *)(b), sizeof(struct in6_addr)) == 0)
#endif

// Missing on older Windows and OS X
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif

using namespace network;
using namespace rdr;
using namespace quiche;

// - RFB protocol parameters

static rfb::LogWriter vlog("QSocket");

static rfb::BoolParameter UseIPv4(
    "UseIPv4", "Use IPv4 for incoming and outgoing connections.", true);
static rfb::BoolParameter UseIPv6(
    "UseIPv6", "Use IPv6 for incoming and outgoing connections.", true);

int network::findFreeUDPPort(void) {
  int sock;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;

  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    throw SocketException("unable to create a UDP socket", errorNumber);

  addr.sin_port = 0;
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    throw SocketException("unable to find free a port for UDP", errorNumber);

  socklen_t n = sizeof(addr);
  if (getsockname(sock, (struct sockaddr *)&addr, &n) < 0)
    throw SocketException("unable to get port number", errorNumber);

  closesocket(sock);
  return ntohs(addr.sin_port);
}

int network::createUDPSocket(const char *host, int port,
                             UDPSocketParam udp_param) {
  int sock, err, result;
  struct addrinfo *ai, hints;

  // - Create a UDP socket

  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  char port_str[6];  // len(65535) + 1
  sprintf(port_str, "%d", port);
  if ((result = getaddrinfo(host, port_str, &hints, &ai)) != 0) {
    throw GAIException("unable to resolve host by name", result);
  }

  sock = -1;
  err = 0;
  sock = socket(ai->ai_family, SOCK_DGRAM, 0);

  if (sock == -1) {
    err = errorNumber;
    throw SocketException("unable to create a UDP socket", err);
  } else {
    // - Set the sock nonblock

    if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
      err = errorNumber;
      throw SocketException("failed to make socket non-blocking", err);
    }

#ifndef WIN32
    // - By default, close the socket on exec()
    if (fcntl(sock, F_SETFD, FD_CLOEXEC)) {
      err = errorNumber;
      throw SocketException("failed to make socket non-blocking", err);
    }
#endif

    switch (udp_param) {
      case BIND:
        // - Bind the socket(server end)
        if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
          throw SocketException("failed to bind socket", errno);
        }
        vlog.info("successfully bind to UDP socket at %s:%d", host, port);
        break;
      case CONNECT:
        // - connect use the socket(client end)
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
          throw SocketException("failed to connect socket", errno);
        }
        vlog.info("successfully connect to UDP socket %s:%d", host, port);
        break;
      default:
        throw SocketException("Bad UDPSocketParam", 0);
        return -1;
    }
  }

  freeaddrinfo(ai);

  return sock;
}

// -=- QSocket

QSocket::QSocket(int sock, conn_io *conn_) : Socket{false}, conn{conn_} {
  instream = new rdr::QInStream(sock, conn_->q_conn);
  outstream = new rdr::QOutStream(sock, conn_->q_conn);
  isShutdown_ = false;
}

char *QSocket::getPeerAddress() {
  struct sockaddr_in *sin = (struct sockaddr_in *)&conn->peer_addr;
  return rfb::strDup(inet_ntoa(sin->sin_addr));
}

// - Same as TcpSocket
char *QSocket::getPeerEndpoint() {
  struct sockaddr_in *sin = (struct sockaddr_in *)&conn->peer_addr;
  rfb::CharArray address;
  address.buf = getPeerAddress();
  int port = sin->sin_port;

  int buflen = strlen(address.buf) + 32;
  char *buffer = new char[buflen];
  sprintf(buffer, "%s::%d", address.buf, port);
  return buffer;
}

QSocket::~QSocket() {}
