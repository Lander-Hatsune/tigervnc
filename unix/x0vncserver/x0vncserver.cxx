/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2004-2008 Constantin Kaplinsky.  All Rights Reserved.
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

// FIXME: Check cases when screen width/height is not a multiply of 32.
//        e.g. 800x600.

#include <errno.h>
#include <inttypes.h>
#include <rfb/Configuration.h>
#include <rfb/LogWriter.h>
#include <rfb/Logger_stdio.h>
#include <rfb/Timer.h>
#include <rfb/VNCServerST.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
// #include <network/UnixSocket.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <blink/quicheServer.h>
#include <network/QSocket.h>
#include <signal.h>
#include <x0vncserver/Geometry.h>
#include <x0vncserver/Image.h>
#include <x0vncserver/PollingScheduler.h>
#include <x0vncserver/XDesktop.h>

extern char buildtime[];

using namespace rfb;
using namespace network;
using namespace quiche;

static LogWriter vlog("Main");

IntParameter pollingCycle("PollingCycle",
                          "Milliseconds per one polling "
                          "cycle; actual interval may be dynamically "
                          "adjusted to satisfy MaxProcessorUsage setting",
                          30);
IntParameter maxProcessorUsage("MaxProcessorUsage",
                               "Maximum percentage of "
                               "CPU time to be consumed",
                               35);
StringParameter displayname("display", "The X display", ":0");
StringParameter server_address("server_address", "The address of server",
                               "127.0.0.1");
IntParameter rfbport("rfbport", "UDP port for RFB protocol", DEFAULT_UDP_PORT);
// StringParameter rfbunixpath("rfbunixpath",
//                             "Unix socket to listen for RFB protocol", "");
// IntParameter rfbunixmode("rfbunixmode", "Unix socket access mode", 0600);
// StringParameter hostsFile("HostsFile", "File with IP access control rules",
// "");
BoolParameter localhostOnly("localhost",
                            "Only allow connections from localhost", false);

//
// Allow the main loop terminate itself gracefully on receiving a signal.
//

static bool caughtSignal = false;
static void CleanupSignalHandler(int sig) { caughtSignal = true; }

static quiche_config *config = NULL;
static conn_io *conns = NULL;
static int udp_sock = -1;

char *programName;

static void printVersion(FILE *fp) {
  fprintf(fp, "TigerVNC Server version %s, built %s\n", PACKAGE_VERSION,
          buildtime);
}

static void usage() {
  printVersion(stderr);
  fprintf(stderr, "\nUsage: %s [<parameters>]\n", programName);
  fprintf(stderr, "       %s --version\n", programName);
  fprintf(stderr,
          "\n"
          "Parameters can be turned on with -<param> or off with -<param>=0\n"
          "Parameters which take a value can be specified as "
          "-<param> <value>\n"
          "Other valid forms are <param>=<value> -<param>=<value> "
          "--<param>=<value>\n"
          "Parameter names are case-insensitive.  The parameters are:\n\n");
  Configuration::listParams(79, 14);
  exit(1);
}

static void main_loop(VNCServerST *server) {
  struct conn_io *conn = NULL;
  static uint8_t buf[65535];
  static uint8_t out[MAX_DATAGRAM_SIZE];
  static std::list<Socket *> sockets;

  while (1) {
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    memset(&peer_addr, 0, peer_addr_len);

    ssize_t read = recvfrom(udp_sock, buf, sizeof(buf), 0,
                            (struct sockaddr *)&peer_addr, &peer_addr_len);

    if (read < 0) {
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
        vlog.error("read would block");
        break;
      }

      vlog.error("fail to read");
      return;
    }

    uint8_t type;
    uint32_t version;

    uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
    size_t scid_len = sizeof(scid);

    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
    size_t dcid_len = sizeof(dcid);

    uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
    size_t odcid_len = sizeof(odcid);

    uint8_t token[MAX_TOKEN_LEN];
    size_t token_len = sizeof(token);

    int rc = quiche_header_info((uint8_t *)buf, read, LOCAL_CONN_ID_LEN,
                                &version, &type, scid, &scid_len, dcid,
                                &dcid_len, token, &token_len);
    if (rc < 0) {
      vlog.error("failed to parse header: %d", rc);
      continue;
    }

    HASH_FIND(hh, conns, dcid, dcid_len, conn);

    if (conn == NULL) {
      if (!quiche_version_is_supported(version)) {
        vlog.error("version negotiation");

        ssize_t written = quiche_negotiate_version(scid, scid_len, dcid,
                                                   dcid_len, out, sizeof(out));

        if (written < 0) {
          vlog.error("failed to create vneg packet: %zd", written);
          continue;
        }

        ssize_t sent = sendto(udp_sock, out, written, 0,
                              (struct sockaddr *)&peer_addr, peer_addr_len);
        if (sent != written) {
          vlog.error("failed to send");
          continue;
        }

        vlog.info("sent %zd bytes", sent);
        continue;
      }

      if (token_len == 0) {
        vlog.error("stateless retry");

        mint_token(dcid, dcid_len, &peer_addr, peer_addr_len, token,
                   &token_len);

        ssize_t written =
            quiche_retry(scid, scid_len, dcid, dcid_len, dcid, dcid_len, token,
                         token_len, out, sizeof(out));

        if (written < 0) {
          vlog.error("failed to create retry packet: %zd", written);
          continue;
        }

        ssize_t sent = sendto(udp_sock, out, written, 0,
                              (struct sockaddr *)&peer_addr, peer_addr_len);
        if (sent != written) {
          vlog.error("failed to send");
          continue;
        }

        vlog.info("sent %zd bytes", sent);
        continue;
      }

      if (!validate_token(token, token_len, &peer_addr, peer_addr_len, odcid,
                          &odcid_len)) {
        vlog.error("invalid address validation token");
        continue;
      }

      if (!(conn = create_conn_server(odcid, odcid_len, conns, config))) {
        continue;
      }

      memcpy(&conn->peer_addr, &peer_addr, peer_addr_len);
      conn->peer_addr_len = peer_addr_len;

      // Add new VNC connections
      server->addSocket(new QSocket(udp_sock, conn));
    }

    ssize_t done = quiche_conn_recv(conn->q_conn, buf, read);

    if (done < 0) {
      vlog.error("failed to process packet: %zd", done);
      continue;
    }

    vlog.info("recv %zd bytes", done);

    server->getSockets(&sockets);
    for (auto i = sockets.begin(); i != sockets.end(); i++) {
      QSocket *q_sock = (QSocket *)(*i);
      flush_egress(udp_sock, q_sock->conn);

      if (quiche_conn_is_closed(q_sock->conn->q_conn)) {
        quiche_stats stats;
        quiche_conn_stats(q_sock->conn->q_conn, &stats);
        vlog.error(
                   "connection[id:%s] closed, recv=%zu sent=%zu lost=%zu "
                   "rtt=%" PRIu64 "ns cwnd=%zu",
                   (char *)q_sock->conn->cid, stats.recv, stats.sent, stats.lost,
                   stats.rtt, stats.cwnd);

        HASH_DELETE(hh, conns, q_sock->conn);
        quiche_conn_free(q_sock->conn->q_conn);
        delete q_sock->conn;

        server->removeSocket(*i);
        delete (*i);
      }
    }

    server->getSockets(&sockets);
    for (auto i = sockets.begin(); i != sockets.end(); i++) {
      QSocket *q_sock = (QSocket *)(*i);
      // flush_egress(udp_sock, q_sock->conn);
      if (q_sock->conn->cid == conn->cid &&
          quiche_conn_is_established(conn->q_conn)) {
        server->processSocketReadEvent(*i);
        server->processSocketWriteEvent(*i);
        break;
      }
    }
  }
}

int main(int argc, char **argv) {
  initStdIOLoggers();
  LogWriter::setLogParams("*:stderr:30");

  programName = argv[0];
  Display *dpy;

  Configuration::enableServerParams();

  for (int i = 1; i < argc; i++) {
    if (Configuration::setParam(argv[i])) continue;

    if (argv[i][0] == '-') {
      if (i + 1 < argc) {
        if (Configuration::setParam(&argv[i][1], argv[i + 1])) {
          i++;
          continue;
        }
      }
      if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-version") == 0 ||
          strcmp(argv[i], "--version") == 0) {
        printVersion(stdout);
        return 0;
      }
      usage();
    }

    usage();
  }

  CharArray dpyStr(displayname.getData());
  if (!(dpy = XOpenDisplay(dpyStr.buf[0] ? dpyStr.buf : 0))) {
    vlog.error("%s: unable to open display \"%s\"\r\n", programName,
               XDisplayName(dpyStr.buf));
    exit(1);
  }

  signal(SIGHUP, CleanupSignalHandler);
  signal(SIGINT, CleanupSignalHandler);
  signal(SIGTERM, CleanupSignalHandler);

  try {
    TXWindow::init(dpy, "x0vncserver");
    Geometry geo(DisplayWidth(dpy, DefaultScreen(dpy)),
                 DisplayHeight(dpy, DefaultScreen(dpy)));
    if (geo.getRect().is_empty()) {
      vlog.error("Exiting with error");
      return 1;
    }
    XDesktop desktop(dpy, &geo);

    VNCServerST server("x0vncserver", &desktop);

    // - Create UDP socket
    char *addr = server_address.getData();
    udp_sock = createUDPSocket(addr, (int)rfbport, BIND);
    delete[] addr;

    // - Configure quiche

    config = quiche_configure_server();

    PollingScheduler sched((int)pollingCycle, (int)maxProcessorUsage);

    // - Main loop
    while (!caughtSignal) {
      fd_set rfds, wfds;
      FD_ZERO(&rfds);
      FD_ZERO(&wfds);
      // Process any incoming X events
      TXWindow::handleXEvents(dpy);

      FD_SET(ConnectionNumber(dpy), &rfds);
      FD_SET(udp_sock, &rfds);
      FD_SET(udp_sock, &wfds);

      std::list<Socket *> sockets;
      server.getSockets(&sockets);
      if (sockets.empty()) sched.reset();
      int wait_ms = 0;
      if (sched.isRunning()) {
        wait_ms = sched.millisRemaining();
        if (wait_ms > 500) {
          wait_ms = 500;
        }
      }
      soonestTimeout(&wait_ms, Timer::checkTimeouts());
      struct timeval tv;
      tv.tv_sec = wait_ms / 1000;
      tv.tv_usec = (wait_ms % 1000) * 1000;

      // Do the wait...
      sched.sleepStarted();
      int n = select(FD_SETSIZE, &rfds, &wfds, 0, wait_ms ? &tv : NULL);
      sched.sleepFinished();

      if (n < 0) {
        if (errno == EINTR) {
          vlog.debug("Interrupted select() system call");
          continue;
        } else {
          throw rdr::SystemException("select", errno);
        }
      }
      bool read_ev = FD_ISSET(udp_sock, &rfds);
      if (!read_ev) continue;

      main_loop(&server);

      Timer::checkTimeouts();
      if (desktop.isRunning() && sched.goodTimeToPoll()) {
        sched.newPass();
        desktop.poll();
      }
    }
  } catch (rdr::Exception &e) {
    vlog.error("%s", e.str());
    return 1;
  }

  quiche_config_free(config);

  TXWindow::handleXEvents(dpy);

  vlog.info("Terminated");
  return 0;
}
