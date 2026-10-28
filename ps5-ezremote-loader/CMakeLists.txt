#   Copyright (C) 2024 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.

#
# Builds ezremote-loader.elf, a minimal payload that launches the
# ezRemote Client homebrew. See README.md for deployment instructions.
#
#   cmake -B build -DPS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
#   cmake --build build
#

cmake_minimum_required(VERSION 3.13)

# The payload SDK can be supplied as a CMake variable or via the environment.
# The prospero toolchain file caches it, so any existing value takes priority.
if(NOT PS5_PAYLOAD_SDK)
  set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}" CACHE PATH
      "Path to the ps5-payload-sdk checkout")
endif()

if(NOT PS5_PAYLOAD_SDK)
  message(FATAL_ERROR
    "PS5_PAYLOAD_SDK is not set. Point it at your ps5-payload-sdk checkout:\n"
    "  cmake -B build -DPS5_PAYLOAD_SDK=/opt/ps5-payload-sdk\n"
    "or export PS5_PAYLOAD_SDK in your environment.")
endif()

if(NOT EXISTS "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
  message(FATAL_ERROR
    "No prospero toolchain found under PS5_PAYLOAD_SDK='${PS5_PAYLOAD_SDK}'.")
endif()

# Cross-compilation for the PS5. Must be set before the first project() call;
# an explicitly supplied toolchain file always wins.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake"
      CACHE FILEPATH "PS5 payload toolchain")
endif()

project(ezremote-loader.elf C)

set(APP_VERSION "1.00")

add_executable(ezremote-loader.elf
  source/main.c
  source/update.c
  source/fs.c
  source/sys.c
  source/pt.c
  source/elfldr.c
  source/hbldr.c
)

# The PS5 payload convention: an ELF with a .elf suffix.
set_target_properties(ezremote-loader.elf PROPERTIES
  OUTPUT_NAME ezRemote-Loader-${APP_VERSION}.elf
)

target_include_directories(ezremote-loader.elf PRIVATE
  source
  ${PS5_PAYLOAD_SDK}/target/user/homebrew/include
)

target_link_directories(ezremote-loader.elf PRIVATE
  ${PS5_PAYLOAD_SDK}/target/user/homebrew/lib
)

# Same flags as the original websrv Makefile.
target_compile_options(ezremote-loader.elf PRIVATE
  -g
  -O0
  -Wall
  -Werror
)

# libcurl is linked statically from the payload SDK sysroot.
target_compile_definitions(ezremote-loader.elf PRIVATE CURL_STATICLIB)

target_link_libraries(ezremote-loader.elf PRIVATE
  kernel_sys
  SceSystemService
  SceUserService
  SceNet
  curl
  minizip
  ssl
  crypto
  psl
  zstd
  z
  pthread
)
