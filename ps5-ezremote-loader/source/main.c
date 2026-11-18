/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sys.h"
#include "update.h"


/**
 * Homebrew that is launched automatically when the payload starts.
 * Change these constants to target a different homebrew, then rebuild.
 **/
#define HOMEBREW_ROOT "/data/homebrew"
#define HOMEBREW_CWD  HOMEBREW_ROOT "/ezremote-client"
#define HOMEBREW_PATH HOMEBREW_CWD "/ezremote_client.elf"
#define HOMEBREW_ARGS ""
#define HOMEBREW_ENV  ""

#define DEFAULT_HTTP_PORT 9090

struct launcher_config
{
  int headless;
  int port;
};

/**
 * Reads the client's config.ini once: the launch mode from [Global]
 * headless_mode and the web server port from [HttpServer] http_server_port.
 * Command line arguments are not used.
 **/
static struct launcher_config ReadConfig(void)
{
  struct launcher_config cfg;
  char path[PATH_MAX];
  char line[256];
  int in_global = 0;
  int in_http = 0;
  FILE *fp;

  cfg.headless = 0;
  cfg.port = DEFAULT_HTTP_PORT;

  snprintf(path, sizeof(path), "%s/config.ini", HOMEBREW_CWD);
  if(!(fp = fopen(path, "r")))
  {
    return cfg;
  }

  while(fgets(line, sizeof(line), fp))
  {
    char *key = line;

    while(*key == ' ' || *key == '\t')
    {
      key++;
    }

    if(*key == '[')
    {
      in_global = !strncmp(key, "[Global]", 8);
      in_http = !strncmp(key, "[HttpServer]", 12);
      continue;
    }

    if(in_global && !strncmp(key, "headless_mode", 13))
    {
      char *value = strchr(key, '=');
      cfg.headless = (value && atoi(value + 1)) ? 1 : 0;
    }
    else if(in_http && !strncmp(key, "http_server_port", 16))
    {
      char *value = strchr(key, '=');
      int port = value ? atoi(value + 1) : 0;

      if(port > 0 && port < 65536)
      {
        cfg.port = port;
      }
    }
  }

  fclose(fp);
  return cfg;
}


static int PortIsActive(int port)
{
  struct sockaddr_in addr;
  struct timeval timeout;
  fd_set writefds;
  socklen_t len = sizeof(int);
  int sock, rc, error = 0;

  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    return 0;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

  if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
  {
    close(sock);
    return 1;
  }

  if(errno != EINPROGRESS)
  {
    close(sock);
    return 0;
  }

  FD_ZERO(&writefds);
  FD_SET(sock, &writefds);
  timeout.tv_sec = 0;
  timeout.tv_usec = 250000;

  rc = select(sock + 1, NULL, &writefds, NULL, &timeout);
  if(rc > 0 && getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0)
  {
    close(sock);
    return 1;
  }

  close(sock);
  return 0;
}

int main(int argc, char** argv)
{
  struct launcher_config cfg = ReadConfig();
  int headless = cfg.headless;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  // Install the homebrew when it is missing, or update it when the release
  // version differs from the one recorded in version.txt.
  if(update_prepare(HOMEBREW_ROOT, HOMEBREW_CWD) < 0)
  {
    Notify("Failed to install ezRemote Client");
    return EXIT_FAILURE;
  }

  if(!headless)
  {
    if (sys_launch_homebrew(HOMEBREW_CWD, HOMEBREW_PATH, HOMEBREW_ARGS, HOMEBREW_ENV) < 0)
    {
      Notify("Failed to launch ezRemote Client");
      return EXIT_FAILURE;
    }
  }
  else
  {
    // Already serving: a second instance would only fail to bind the port.
    if(PortIsActive(cfg.port))
    {
      Notify("ezRemote Client is already running on port %d\n", cfg.port);
      launch_uri("pshomeui:navigateToHome?bootCondition=psButton");
      return EXIT_SUCCESS;
    }

    if (sys_launch_daemon(HOMEBREW_CWD, HOMEBREW_PATH, "ezremote_client", "LD_LIBRARY_PATH=" HOMEBREW_CWD) < 0)
    {
      Notify("Failed to launch ezRemote Client in headless mode");
      return EXIT_FAILURE;
    }
    launch_uri("pshomeui:navigateToHome?bootCondition=psButton");
  }

  // The homebrew is running independently, so the launcher is done.
  return EXIT_SUCCESS;
}
