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
#include <network/QSSocket.h>
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
#include <signal.h>
#include <x0vncserver/Geometry.h>
#include <x0vncserver/Image.h>
#include <x0vncserver/PollingScheduler.h>
#include <x0vncserver/XDesktop.h>

extern char buildtime[];

using namespace rfb;
using namespace network;

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
StringParameter displayname("display", "The X display", "");
StringParameter server_address("server_address", "The address of server",
                               "127.0.0.1");
IntParameter rfbport("rfbport", "UDP port to listen for RFB protocol", 814);
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
    char *addr = server_address.getData();
    QSSocket *qs_socket = new QSSocket(addr, (int)rfbport);
    delete[] addr;
    server.addSocket(qs_socket);
    vlog.info("Listening on port %d", (int)rfbport);

    PollingScheduler sched((int)pollingCycle, (int)maxProcessorUsage);

    while (!caughtSignal) {
      int wait_ms;
      struct timeval tv;
      fd_set rfds, wfds;
      std::list<Socket *> sockets;

      // Process any incoming X events
      TXWindow::handleXEvents(dpy);

      FD_ZERO(&rfds);
      FD_ZERO(&wfds);

      FD_SET(ConnectionNumber(dpy), &rfds);
      FD_SET(qs_socket->getFd(), &rfds);

      server.getSockets(&sockets);
      int clients_connected = 0;
      for (auto i = sockets.begin(); i != sockets.end(); i++) {
        if ((*i)->isShutdown()) {
          server.removeSocket(*i);
          delete (*i);
        } else {
          FD_SET((*i)->getFd(), &rfds);
          if ((*i)->outStream().hasBufferedData()) FD_SET((*i)->getFd(), &wfds);
          clients_connected++;
        }
      }

      if (!clients_connected) sched.reset();

      wait_ms = 0;

      if (sched.isRunning()) {
        wait_ms = sched.millisRemaining();
        if (wait_ms > 500) {
          wait_ms = 500;
        }
      }

      soonestTimeout(&wait_ms, Timer::checkTimeouts());

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

      Timer::checkTimeouts();

      // Client list could have been changed.
      server.getSockets(&sockets);

      // Nothing more to do if there are no client connections.
      if (sockets.empty()) continue;

      // Process events on existing VNC connections
      for (auto i = sockets.begin(); i != sockets.end(); i++) {
        if (FD_ISSET((*i)->getFd(), &rfds)) server.processSocketReadEvent(*i);
        if (FD_ISSET((*i)->getFd(), &wfds)) server.processSocketWriteEvent(*i);
      }

      if (desktop.isRunning() && sched.goodTimeToPoll()) {
        sched.newPass();
        desktop.poll();
      }
    }

  } catch (rdr::Exception &e) {
    vlog.error("%s", e.str());
    return 1;
  }

  TXWindow::handleXEvents(dpy);

  vlog.info("Terminated");
  return 0;
}
