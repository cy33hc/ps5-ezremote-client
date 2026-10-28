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

#pragma once

#include <stddef.h>


typedef struct notify_request
{
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

/**
 * Post a system notification. The loader runs as a payload, so progress is
 * reported this way rather than on stdout, which the user never sees.
 **/
void Notify(const char *fmt, ...);

void log_msg(const char *fmt, ...);

/**
 * Launch a homebrew ELF as a bigapp. The homebrew's stdout/stderr are
 * redirected to /dev/null so it can keep running after the launcher exits.
 * Returns 0 on success, or a negative value on failure.
 **/
int sys_launch_homebrew(const char* cwd, const char* path, const char* args,
			const char* env);
