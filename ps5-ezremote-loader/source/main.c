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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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


int main(int argc, char** argv)
{
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  // Install the homebrew when it is missing, or update it when the release
  // version differs from the one recorded in version.txt.
  if(update_prepare(HOMEBREW_ROOT, HOMEBREW_CWD) < 0)
  {
    Notify("Failed to install ezRemote Client");
    return EXIT_FAILURE;
  }

  if(sys_launch_homebrew(HOMEBREW_CWD, HOMEBREW_PATH,
                         HOMEBREW_ARGS, HOMEBREW_ENV) < 0)
  {
    Notify("Failed to launch ezRemote Client");
    return EXIT_FAILURE;
  }

  // The homebrew is running independently, so the launcher is done.
  return EXIT_SUCCESS;
}
