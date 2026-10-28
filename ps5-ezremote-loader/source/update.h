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

#ifndef UPDATE_H
#define UPDATE_H

/**
 * Make sure the homebrew in HOME_DIR is installed and up to date.
 *
 * ROOT_DIR is the directory that holds the homebrew folder (the zip archive
 * contains a top-level "ezremote-client" directory, so it is extracted there).
 *
 * Returns 0 when the caller may go ahead and launch the homebrew, and -1 when
 * the homebrew is not installed and could not be downloaded/extracted.
 **/
int update_prepare(const char *root_dir, const char *home_dir);

#endif /* UPDATE_H */
