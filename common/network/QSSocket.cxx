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

#include <ev.h>
#include <fcntl.h>
#include <network/QSSocket.h>
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

// - RFB protocol parameters

static rfb::LogWriter vlog("QSSocket");

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

// -=- QSSocket

QSSocket::QSSocket(int sock) : Socket(sock) {}

QSSocket::QSSocket(const char *host, int port)
    : config{NULL}, conns{NULL}, instream{NULL}, outstream{NULL} {
  int sock, err, result;
  struct addrinfo *ai, *current, hints;

  // - Create a UDP socket

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  if ((result = getaddrinfo(host, NULL, &hints, &ai)) != 0) {
    throw GAIException("unable to resolve host by name", result);
  }

  sock = -1;
  err = 0;
  sock = socket(ai->ai_family, SOCK_DGRAM, 0);
  if (sock == -1) {
    err = errorNumber;
    freeaddrinfo(ai);
    throw SocketException("unable to create a UDP socket", err);
  }

  // - Set the sock nonblock

  if (sock != -1 && fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
    err = errorNumber;
    freeaddrinfo(ai);
    throw SocketException("failed to make socket non-blocking", err);
  }

#ifndef WIN32
  // - By default, close the socket on exec()
  if (sock != -1 && fcntl(sock, F_SETFD, FD_CLOEXEC)) {
    err = errorNumber;
    freeaddrinfo(ai);
    throw SocketException("failed to make socket non-blocking", err);
  }
#endif

  // - Bind the socket

  if (sock != -1 && bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
    err = errorNumber;
    freeaddrinfo(ai);
    throw SocketException("failed to bind socket", err);
  }

  // - Keep the socket

  if (sock != -1) {
    qs_sock = sock;
    freeaddrinfo(ai);
    vlog.info("successfully create a UDP socket\n");
  }

  // - Configure quiche

  config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
  if (config == NULL) {
    freeaddrinfo(ai);
    throw QuicheException("failed to create quiche config", 0);
  } else {
    quiche_config_load_cert_chain_from_pem_file(config, "../quiche/cert.crt");
    quiche_config_load_priv_key_from_pem_file(config, "../quiche/cert.key");

    quiche_config_set_application_protos(
        config, (uint8_t *)"\x05hq-28\x05hq-27\x08http/0.9", 21);

    quiche_config_set_max_idle_timeout(config, 5000);
    quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);
    quiche_config_set_initial_max_streams_bidi(config, 100);
    quiche_config_set_cc_algorithm(config, QUICHE_CC_RENO);
    vlog.info("successfully configure quiche\n");
  }
}

// - Same as TcpSocket
char *QSSocket::getPeerAddress() {
  vnc_sockaddr_t sa;
  socklen_t sa_size = sizeof(sa);

  if (getpeername(getFd(), &sa.u.sa, &sa_size) != 0) {
    vlog.error("unable to get peer name for socket");
    return rfb::strDup("");
  }

  if (sa.u.sa.sa_family == AF_INET6) {
    char buffer[INET6_ADDRSTRLEN + 2];
    int ret;

    buffer[0] = '[';

    ret = getnameinfo(&sa.u.sa, sizeof(sa.u.sin6), buffer + 1,
                      sizeof(buffer) - 2, NULL, 0, NI_NUMERICHOST);
    if (ret != 0) {
      vlog.error("unable to convert peer name to a string");
      return rfb::strDup("");
    }

    strcat(buffer, "]");

    return rfb::strDup(buffer);
  }

  if (sa.u.sa.sa_family == AF_INET) {
    char *name;

    name = inet_ntoa(sa.u.sin.sin_addr);
    if (name == NULL) {
      vlog.error("unable to convert peer name to a string");
      return rfb::strDup("");
    }

    return rfb::strDup(name);
  }

  vlog.error("unknown address family for socket");
  return rfb::strDup("");
}

// - Same as TcpSocket
char *QSSocket::getPeerEndpoint() {
  rfb::CharArray address;
  address.buf = getPeerAddress();
  vnc_sockaddr_t sa;
  socklen_t sa_size = sizeof(sa);
  int port;

  getpeername(getFd(), &sa.u.sa, &sa_size);

  if (sa.u.sa.sa_family == AF_INET6)
    port = ntohs(sa.u.sin6.sin6_port);
  else if (sa.u.sa.sa_family == AF_INET)
    port = ntohs(sa.u.sin.sin_port);
  else
    port = 0;

  int buflen = strlen(address.buf) + 32;
  char *buffer = new char[buflen];
  sprintf(buffer, "%s::%d", address.buf, port);
  return buffer;
}

rdr::QSInStream &QSSocket::inStream() {
  return instream ? *(instream = new QSInStream(qs_sock, config, conns))
                  : *instream;
}

rdr::QSOutStream &QSSocket::outStream() {
  return outstream ? *(outstream = new QSOutStream(qs_sock, config, conns))
                   : *outstream;
}

QSSocket::~QSSocket() { quiche_config_free(config); }