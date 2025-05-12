cmake_minimum_required(VERSION 3.5)

project(ezremote-dpi)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -fstack-protector-all")
#set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -O0 -pedantic -pedantic-errors")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fpermissive")

add_definitions(-DCPPHTTPLIB_THREAD_POOL_COUNT=16)
add_definitions(-DNO_GUI=1)
include_directories(
  ../source
  ../source/pugixml
  source
)

add_executable(${PROJECT_NAME}.elf
  ../source/pugixml/pugixml.cpp
  ../source/clients/baseclient.cpp
  ../source/clients/smbclient.cpp
  ../source/filehost/1fichier.cpp
  ../source/filehost/alldebrid.cpp
  ../source/filehost/directhost.cpp
  ../source/filehost/filehost.cpp
  ../source/filehost/gdrive.cpp
  ../source/filehost/mediafire.cpp
  ../source/filehost/pixeldrain.cpp
  ../source/filehost/realdebrid.cpp
  ../source/http/httplib.cpp
  ../source/config.cpp
  ../source/crypt.c
  ../source/dbglogger.c
  ../source/fs.cpp
  ../source/inifile.c
  ../source/installer.cpp
  ../source/lang.cpp
  ../source/split_file.cpp
  source/server/http_server.cpp
  source/main.cpp
)

target_link_libraries(${PROJECT_NAME}.elf
  zstd
  z
  psl
  mbedtls
  mbedcrypto
  mbedx509
  wolfssl
  curl
  lzma
  bz2
  lexbor_static
  smb2
  nfs
  minizip
  iconv
  archive
  json-c
  ssh2
  httpclient
  SceSystemService
  SceUserService
  SceSysCore
  kernel_sys
  SceAppInstUtil
)
