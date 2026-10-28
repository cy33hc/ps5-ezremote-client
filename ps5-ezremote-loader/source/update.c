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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <curl/curl.h>
#include <minizip/unzip.h>

#include "sys.h"
#include "update.h"

/* libnet, needed before any socket (and thus libcurl) use. */
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);

#define NET_POOL_SIZE (1024 * 1024)

/**
 * The zip archive published with every release contains a top-level
 * "ezremote-client" directory, hence archives are extracted into ROOT_DIR.
 **/
#define RELEASE_VERSION_URL \
  "https://github.com/cy33hc/ps5-ezremote-client/releases/latest/download/version.txt"
#define RELEASE_ZIP_URL \
  "https://github.com/cy33hc/ps5-ezremote-client/releases/latest/download/ezremote-client.zip"

#define VERSION_SIZE 128
#define PATH_SIZE    1024
#define COPY_SIZE    (64 * 1024)

#define ZIP_TIMEOUT     900L  /* seconds, the archive is ~66 MiB */
#define VERSION_TIMEOUT  30L

static int is_dir(const char *path)
{
  struct stat st;

  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


/**
 * Initialise libnet. A failure is not fatal here: the network may already be
 * initialised by the payload host, and curl will report a proper error if it
 * is really unusable.
 **/
static void net_init(void)
{
  int rc;

  if((rc = sceNetInit()) != 0)
  {
    log_msg("sceNetInit: 0x%08x", (unsigned)rc);
  }
  if((rc = sceNetPoolCreate("ezremote_loader", NET_POOL_SIZE, 0)) < 0)
  {
    log_msg("sceNetPoolCreate: 0x%08x", (unsigned)rc);
  }
}


/**
 * Create PATH and all missing parent directories.
 **/
static int mkdir_p(const char *path, mode_t mode)
{
  char buf[PATH_SIZE];
  size_t len;
  char *p;

  len = strlen(path);
  if(len == 0 || len >= sizeof(buf))
  {
    errno = ENAMETOOLONG;
    return -1;
  }

  memcpy(buf, path, len + 1);

  for(p = buf + 1; *p; p++)
  {
    if(*p != '/')
    {
      continue;
    }
    *p = '\0';
    if(mkdir(buf, mode) != 0 && errno != EEXIST)
    {
      return -1;
    }
    *p = '/';
  }

  if(mkdir(buf, mode) != 0 && errno != EEXIST)
  {
    return -1;
  }
  return 0;
}


/**
 * Reject absolute entry names and any ".." path component (zip-slip).
 **/
static int path_is_safe(const char *name)
{
  const char *p = name;

  if(*p == '/')
  {
    return 0;
  }

  while(*p)
  {
    const char *seg = p;
    size_t len;

    while(*p && *p != '/')
    {
      p++;
    }
    len = (size_t)(p - seg);

    if(len == 2 && seg[0] == '.' && seg[1] == '.')
    {
      return 0;
    }
    if(*p == '/')
    {
      p++;
    }
  }
  return 1;
}


/**
 * Trim leading and trailing whitespace in place.
 **/
static void trim(char *s)
{
  char *p = s;
  size_t len;

  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\v')
  {
    p++;
  }
  if(p != s)
  {
    memmove(s, p, strlen(p) + 1);
  }

  len = strlen(s);
  while(len > 0)
  {
    char c = s[len - 1];

    if(c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\v')
    {
      break;
    }
    s[--len] = '\0';
  }
}


/**
 * Normalise a version string so that "v2.15", " 2.15\n" and "2.15" match.
 **/
static void normalise_version(char *s)
{
  trim(s);
  if(s[0] == 'v' || s[0] == 'V')
  {
    memmove(s, s + 1, strlen(s));
    trim(s);
  }
}


static int read_local_version(const char *path, char *out, size_t out_size)
{
  FILE *fp;
  size_t len;

  fp = fopen(path, "r");
  if(!fp)
  {
    return -1;
  }

  if(!fgets(out, (int)out_size, fp))
  {
    fclose(fp);
    return -1;
  }
  fclose(fp);

  len = strlen(out);
  if(len == 0)
  {
    return -1;
  }
  return 0;
}


static int write_local_version(const char *path, const char *version)
{
  FILE *fp;

  fp = fopen(path, "w");
  if(!fp)
  {
    log_msg("cannot write %s: %s", path, strerror(errno));
    return -1;
  }
  if(fprintf(fp, "%s\n", version) < 0)
  {
    log_msg("cannot write %s: %s", path, strerror(errno));
    fclose(fp);
    return -1;
  }
  if(fclose(fp) != 0)
  {
    log_msg("cannot write %s: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}


struct text_buffer
{
  char *data;
  size_t size;
  size_t len;
};


static size_t text_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  struct text_buffer *buf = userdata;
  size_t len = size * nmemb;

  if(len > buf->size - 1 - buf->len)
  {
    return 0; /* too large, abort the transfer */
  }

  memcpy(buf->data + buf->len, ptr, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return len;
}


/**
 * Create a curl handle configured the way all our requests need it.
 *
 * Certificate verification is deliberately disabled: payloads do not ship a
 * CA bundle and the PS5 has no usable system trust store.
 **/
static CURL *curl_new(const char *url)
{
  CURL *curl = curl_easy_init();

  if(!curl)
  {
    return NULL;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ezremote-loader/1.0");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

  return curl;
}


/**
 * Fetch a small text document (the version file) into OUT.
 **/
static int http_get_text(const char *url, char *out, size_t out_size)
{
  struct text_buffer buf;
  CURLcode rc;
  CURL *curl;
  long code = 0;

  buf.data = out;
  buf.size = out_size;
  buf.len = 0;
  out[0] = '\0';

  curl = curl_new(url);
  if(!curl)
  {
    log_msg("cannot initialise curl");
    return -1;
  }

  curl_easy_setopt(curl, CURLOPT_TIMEOUT, VERSION_TIMEOUT);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, text_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

  rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);

  if(rc != CURLE_OK)
  {
    log_msg("cannot fetch %s: %s", url, curl_easy_strerror(rc));
    return -1;
  }
  if(code != 200)
  {
    log_msg("cannot fetch %s: HTTP %ld", url, code);
    return -1;
  }
  if(buf.len == 0)
  {
    log_msg("empty response from %s", url);
    return -1;
  }
  return 0;
}


static size_t file_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  return fwrite(ptr, size, nmemb, (FILE *)userdata);
}


/**
 * Download URL into the file at DEST.
 **/
static int http_download(const char *url, const char *dest)
{
  CURLcode rc;
  CURL *curl;
  FILE *fp;
  long code = 0;
  curl_off_t size = 0;
  int ok;

  fp = fopen(dest, "wb");
  if(!fp)
  {
    log_msg("cannot create %s: %s", dest, strerror(errno));
    return -1;
  }

  curl = curl_new(url);
  if(!curl)
  {
    log_msg("cannot initialise curl");
    fclose(fp);
    unlink(dest);
    return -1;
  }

  curl_easy_setopt(curl, CURLOPT_TIMEOUT, ZIP_TIMEOUT);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

  Notify("Downloading ezRemote Client");
  rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &size);
  curl_easy_cleanup(curl);

  ok = rc == CURLE_OK && code == 200;
  if(fclose(fp) != 0)
  {
    ok = 0;
  }

  if(!ok)
  {
    if(rc != CURLE_OK)
    {
      log_msg("Download ezRemote Client failed: %s", curl_easy_strerror(rc));
    }
    else if(code != 200)
    {
      log_msg("Download ezRemote Client failed: HTTP %ld", code);
    }
    else
    {
      log_msg("Download ezRemote Client failed: cannot write %s: %s", dest, strerror(errno));
    }
    unlink(dest);
    return -1;
  }

  log_msg("downloaded %ld bytes", (long)size);
  return 0;
}


/**
 * Extract a single file from ZIP into DEST_ROOT.
 **/
static int extract_file(unzFile zip, const char *dest_root, const char *name,
                        const unz_file_info *info)
{
  char path[PATH_SIZE];
  char buf[COPY_SIZE];
  mode_t mode;
  FILE *fp = NULL;
  int rc = -1;
  int n;

  if(snprintf(path, sizeof(path), "%s/%s", dest_root, name) >= (int)sizeof(path))
  {
    log_msg("path too long: %s", name);
    return -1;
  }

  /* Remove the old file first: overwriting a running executable fails with
     ETXTBSY, unlinking it does not. A running process keeps its inode. */
  if(unlink(path) != 0 && errno != ENOENT)
  {
    log_msg("cannot remove %s: %s", path, strerror(errno));
    return -1;
  }

  fp = fopen(path, "wb");
  if(!fp)
  {
    log_msg("cannot create %s: %s", path, strerror(errno));
    return -1;
  }

  if(unzOpenCurrentFile(zip) != UNZ_OK)
  {
    log_msg("cannot open %s in archive", name);
    goto out;
  }

  while((n = unzReadCurrentFile(zip, buf, sizeof(buf))) > 0)
  {
    if(fwrite(buf, 1, (size_t)n, fp) != (size_t)n)
    {
      log_msg("cannot write %s: %s", path, strerror(errno));
      unzCloseCurrentFile(zip);
      goto out;
    }
  }

  if(unzCloseCurrentFile(zip) != UNZ_OK)
  {
    log_msg("cannot close %s in archive", name);
    goto out;
  }

  if(n < 0)
  {
    log_msg("cannot read %s from archive", name);
    goto out;
  }

  rc = 0;

out:
  if(fclose(fp) != 0 && rc == 0)
  {
    log_msg("cannot write %s: %s", path, strerror(errno));
    rc = -1;
  }

  if(rc == 0)
  {
    mode = (mode_t)((info->external_fa >> 16) & 0777);
    if(mode != 0)
    {
      chmod(path, mode);
    }
  }
  else
  {
    unlink(path);
  }
  return rc;
}


/**
 * Extract all entries of ZIP into DEST_ROOT.
 **/
static int extract_zip(const char *zip_path, const char *dest_root)
{
  unz_global_info global;
  unz_file_info info;
  unzFile zip;
  char name[PATH_SIZE];
  size_t done = 0;
  int rc = -1;

  zip = unzOpen(zip_path);
  if(!zip)
  {
    log_msg("cannot open archive %s", zip_path);
    return -1;
  }

  if(unzGetGlobalInfo(zip, &global) != UNZ_OK)
  {
    log_msg("cannot read archive %s", zip_path);
    goto out;
  }

  if(unzGoToFirstFile(zip) != UNZ_OK)
  {
    log_msg("archive %s is empty", zip_path);
    goto out;
  }

  do
  {
    size_t len;
    int is_dir_entry;

    memset(&info, 0, sizeof(info));
    if(unzGetCurrentFileInfo(zip, &info, name, sizeof(name),
                             NULL, 0, NULL, 0) != UNZ_OK)
    {
      log_msg("cannot read entry in archive %s", zip_path);
      goto out;
    }

    if(!path_is_safe(name))
    {
      log_msg("skipping unsafe archive entry: %s", name);
      continue;
    }

    len = strlen(name);
    is_dir_entry = len > 0 && name[len - 1] == '/';
    while(len > 0 && name[len - 1] == '/')
    {
      name[--len] = '\0';
    }
    if(len == 0)
    {
      continue;
    }

    if(is_dir_entry || (info.external_fa & 0x10))
    {
      /* directory entry */
      char path[PATH_SIZE];

      if(snprintf(path, sizeof(path), "%s/%s", dest_root, name) >= (int)sizeof(path))
      {
        log_msg("path too long: %s", name);
        goto out;
      }
      if(mkdir_p(path, 0755) != 0)
      {
        log_msg("cannot create directory %s: %s", path, strerror(errno));
        goto out;
      }
    }
    else
    {
      char path[PATH_SIZE];
      char *slash;

      if(snprintf(path, sizeof(path), "%s/%s", dest_root, name) >= (int)sizeof(path))
      {
        log_msg("path too long: %s", name);
        goto out;
      }

      slash = strrchr(path, '/');
      if(slash)
      {
        *slash = '\0';
        if(mkdir_p(path, 0755) != 0)
        {
          log_msg("cannot create directory %s: %s", path, strerror(errno));
          goto out;
        }
      }

      if(extract_file(zip, dest_root, name, &info) != 0)
      {
        goto out;
      }
    }

    done++;
    if(global.number_entry && done % 25 == 0)
    {
      Notify("Extracted %zu/%lu files", done, (unsigned long)global.number_entry);
    }
  } while(unzGoToNextFile(zip) == UNZ_OK);

  rc = 0;
  log_msg("Complete extracing", done);

out:
  unzClose(zip);
  return rc;
}


/**
 * Download the release zip, extract it into ROOT_DIR and record the version.
 **/
static int download_and_extract(const char *root_dir, const char *version_path,
                                const char *version)
{
  char zip_path[PATH_SIZE];

  if(mkdir_p(root_dir, 0755) != 0)
  {
    log_msg("cannot create %s: %s", root_dir, strerror(errno));
    return -1;
  }

  if(snprintf(zip_path, sizeof(zip_path), "%s/.ezremote-client.zip.part",
              root_dir) >= (int)sizeof(zip_path))
  {
    log_msg("path too long: %s", root_dir);
    return -1;
  }

  if(http_download(RELEASE_ZIP_URL, zip_path) != 0)
  {
    return -1;
  }

  Notify("Extracting ezRemote Client to %s", root_dir);
  if(extract_zip(zip_path, root_dir) != 0)
  {
    unlink(zip_path);
    return -1;
  }
  unlink(zip_path);

  /* The release archive does not contain version.txt, so record the version
     we just installed ourselves. */
  if(write_local_version(version_path, version) != 0)
  {
    Notify("Installed ezRemote Client %s but could not record its version", version);
  }

  Notify("Installed ezRemote Client version %s", version);
  return 0;
}


int update_prepare(const char *root_dir, const char *home_dir)
{
  char version_path[PATH_SIZE];
  char local[VERSION_SIZE];
  char remote[VERSION_SIZE];
  int installed;
  int have_local = 0;
  int rc;

  if(snprintf(version_path, sizeof(version_path), "%s/version.txt",
              home_dir) >= (int)sizeof(version_path))
  {
    log_msg("path too long: %s", home_dir);
    return -1;
  }

  installed = is_dir(home_dir);

  if(installed)
  {
    have_local = read_local_version(version_path, local, sizeof(local)) == 0;
    if(have_local)
    {
      normalise_version(local);
      log_msg("installed version: %s", local);
    }
    else
    {
      log_msg("no usable %s, will refresh the installation", version_path);
    }
  }
  else
  {
    log_msg("%s is not installed", home_dir);
  }

  net_init();

  if(curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
  {
    log_msg("cannot initialise libcurl");
    return installed ? 0 : -1;
  }

  if(http_get_text(RELEASE_VERSION_URL, remote, sizeof(remote)) != 0)
  {
    Notify("Cannot determine the latest version;\n %s",
            installed ? "Launching the installed version"
                      : "The homebrew is not installed");
    curl_global_cleanup();
    return installed ? 0 : -1;
  }
  normalise_version(remote);
  log_msg("latest version: %s", remote);

  if(installed && have_local && strcmp(local, remote) == 0)
  {
    log_msg("already up to date");
    curl_global_cleanup();
    return 0;
  }

  Notify("%s version %s", installed ? "Updating ezRemote Client to" : "Installing ezRemote Client", remote);
  rc = download_and_extract(root_dir, version_path, remote);
  curl_global_cleanup();

  if(rc != 0)
  {
    if(installed)
    {
      Notify("Update failed, launching the installed version");
      return 0;
    }
    return -1;
  }
  return 0;
}
