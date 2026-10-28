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

#include <stdio.h>
#include <stdlib.h>

#include "fs.h"


/**
 * Read a file from disk at the given path.
 **/
uint8_t*
fs_readfile(const char* path, size_t* size) {
  uint8_t* buf;
  long len;
  FILE* file;

  if(!(file=fopen(path, "rb"))) {
    return 0;
  }

  if(fseek(file, 0, SEEK_END)) {
    fclose(file);
    return 0;
  }

  if((len=ftell(file)) < 0) {
    fclose(file);
    return 0;
  }

  if(fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return 0;
  }

  if(!(buf=malloc(len))) {
    fclose(file);
    return 0;
  }

  if(fread(buf, 1, len, file) != (size_t)len) {
    free(buf);
    fclose(file);
    return 0;
  }

  if(fclose(file)) {
    free(buf);
    return 0;
  }

  if(size) {
    *size = len;
  }

  return buf;
}
