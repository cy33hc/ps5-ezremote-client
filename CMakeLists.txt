cmake_minimum_required(VERSION 3.5)

project(ezremote_client)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDONT_HAVE_STRUPR -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fpermissive")

add_definitions(-DCPPHTTPLIB_THREAD_POOL_COUNT=16)

add_subdirectory(ps5-ezremote-dpi)

include_directories(
  source
  source/pugixml
  source/imgui
)

add_executable(ezremote_client.elf
  source/imgui/imgui_draw.cpp
  source/imgui/imgui_impl_sdl.cpp
  source/imgui/imgui_impl_sdlrenderer.cpp
  source/imgui/imgui_tables.cpp
  source/imgui/imgui_widgets.cpp
  source/imgui/imgui.cpp
  source/pugixml/pugixml.cpp
  source/clients/baseclient.cpp
  source/clients/apache.cpp
  source/clients/archiveorg.cpp
  source/clients/ftpclient.cpp
  source/clients/github.cpp
  source/clients/myrient.cpp
  source/clients/iis.cpp
  source/clients/nginx.cpp
  source/clients/npxserve.cpp
  source/clients/nfsclient.cpp
  source/clients/smbclient.cpp
  source/clients/sftpclient.cpp
  source/clients/rclone.cpp
  source/clients/webdav.cpp
  source/filehost/1fichier.cpp
  source/filehost/alldebrid.cpp
  source/filehost/directhost.cpp
  source/filehost/filehost.cpp
  source/filehost/gdrive.cpp
  source/filehost/mediafire.cpp
  source/filehost/pixeldrain.cpp
  source/filehost/realdebrid.cpp
  source/http/httplib.cpp
  source/server/http_server.cpp
  source/actions.cpp
  source/config.cpp
  source/crypt.c
  source/dbglogger.c
  source/fs.cpp
  source/gui.cpp
  source/ime_dialog.cpp
  source/inifile.c
  source/installer.cpp
  source/lang.cpp
  source/main.cpp
  source/sfo.cpp
  source/textures.cpp
  source/windows.cpp
  source/zip_util.cpp
  source/split_file.cpp
)

target_link_libraries(ezremote_client.elf
  webp
  webpdemux
  SDL2
  SDL2_image
  c
  c++
  png
  webp
  jpeg
  iconv
  httpclient
  zstd
  z
  psl
  crypto
  ssl
  curl
  lzma
  bz2
  pthread
  samplerate
  lexbor_static
  smb2
  nfs
  minizip
  archive
  json-c
  ssh2
  SceAudioOut
  SceVideoOut
  SceImeDialog
  SceKeyboard
  ScePad
  SceNet
  SceSysCore
  SceSystemService
  SceUserService
  SceLibcInternal
  kernel_sys
  SceAppInstUtil
)

set(APPDIR "ezremote-client")
set(APP_VERSION "1.10")

add_custom_target(package
   DEPENDS ezremote-dpi.elf
   DEPENDS ezremote_client.elf
   COMMAND rm -rf ${APPDIR}
   COMMAND rm -f ezremote-client.zip
   COMMAND mkdir -p ${APPDIR}
   COMMAND cp -R ${PROJECT_SOURCE_DIR}/data/* ${APPDIR}
   COMMAND cp ezremote_client.elf ${APPDIR}
   COMMAND cp ps5-ezremote-dpi/ezremote-dpi.elf ${APPDIR}
   COMMAND sed -i "s/ezRemote Client/ezRemote Client v${APP_VERSION}/g"  ${APPDIR}/homebrew.js

   COMMAND zip -r ezremote-client.zip ${APPDIR}/


)