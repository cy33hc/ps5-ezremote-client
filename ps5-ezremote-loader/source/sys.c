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

#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#include "hbldr.h"
#include "sys.h"

/* ShellUI / UserService externs */
typedef struct {
    unsigned int size;
    uint32_t userId;
} SceShellUIUtilLaunchByUriParam;

int  sceUserServiceInitialize(void*);
void sceUserServiceTerminate(void);
int sceKernelLoadStartModule(const char *path, size_t args, const void *argp,
                                    uint32_t flags, const void *pOpt, int *pRes);
int sceUserServiceGetForegroundUser(int *userId);
int sceUserServiceGetInitialUser(int *userId);

void
Notify(const char *fmt, ...) {
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof req);
  va_start(args, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, args);
  va_end(args);

  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

/**
 * Navigate to PS5 Home Screen via libSceShellUIUtil.sprx URI launch.
 * Uses: pshomeui:navigateToHome?bootCondition=psButton
 * Based on the Return-to-Home mechanism by LightningMods / etaHEN.
 */
static int return_to_home_by_uri(void) {
    int (*p_sceShellUIUtilInitialize)(void) = NULL;
    int (*p_sceShellUIUtilLaunchByUri)(const char *uri, SceShellUIUtilLaunchByUriParam *param) = NULL;

    /* Initialize User Service so user queries succeed */
    int user_prio = 256;
    int u_init = sceUserServiceInitialize(&user_prio);
    printf("[autoloader] sceUserServiceInitialize returned: 0x%08x (%d)\n", u_init, u_init);

    int mod = sceKernelLoadStartModule("/system_ex/common_ex/lib/libSceShellUIUtil.sprx", 0, 0, 0, 0, 0);
    if (mod < 0) {
        printf("[autoloader] return_to_home_by_uri: sceKernelLoadStartModule failed: 0x%08x (%d)\n", mod, mod);
        fflush(stdout);
        return -1;
    }

    /* Resolve symbols directly via kernel_dynlib_dlsym as in etaHEN */
    p_sceShellUIUtilInitialize = (void *)kernel_dynlib_dlsym(-1, (uint32_t)mod, "sceShellUIUtilInitialize");
    p_sceShellUIUtilLaunchByUri = (void *)kernel_dynlib_dlsym(-1, (uint32_t)mod, "sceShellUIUtilLaunchByUri");

    if (!p_sceShellUIUtilInitialize || !p_sceShellUIUtilLaunchByUri) {
        printf("[autoloader] return_to_home_by_uri: failed to resolve libSceShellUIUtil symbols\n");
        fflush(stdout);
        return -1;
    }

    SceShellUIUtilLaunchByUriParam param;
    memset(&param, 0, sizeof(param));
    param.size = sizeof(SceShellUIUtilLaunchByUriParam);

    int ui_init = p_sceShellUIUtilInitialize();
    printf("[autoloader] sceShellUIUtilInitialize returned: 0x%08x (%d)\n", ui_init, ui_init);

    int u_fg = sceUserServiceGetForegroundUser((int *)&param.userId);
    printf("[autoloader] sceUserServiceGetForegroundUser returned: 0x%08x (%d), userId=0x%08x\n",
           u_fg, u_fg, param.userId);

    if (param.userId == 0 || param.userId == (uint32_t)-1) {
        int u_init_u = sceUserServiceGetInitialUser((int *)&param.userId);
        printf("[autoloader] sceUserServiceGetInitialUser returned: 0x%08x (%d), userId=0x%08x\n",
               u_init_u, u_init_u, param.userId);
    }

    printf("[autoloader] return_to_home_by_uri: launching pshomeui with userId=0x%08x\n", param.userId);
    fflush(stdout);

    int res = p_sceShellUIUtilLaunchByUri("pshomeui:navigateToHome?bootCondition=psButton", &param);
    printf("[autoloader] return_to_home_by_uri: sceShellUIUtilLaunchByUri returned: 0x%08x (%d)\n", res, res);
    fflush(stdout);

    return res;
}

/**
 * Write a diagnostic line to the payload log. Not shown to the user; use Notify
 * for progress the user should see.
 **/
void log_msg(const char *fmt, ...)
{
  va_list ap;

  fprintf(stderr, "[ezremote-loader] ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}



/**
 * Decode an escaped argument.
 **/
static char*
args_decode(const char* s) {
  size_t length = strlen(s);
  char *arg = malloc(length+1);
  size_t off = 0;
  int escape = 0;

  for(size_t i=0; i<length; i++) {
    if(s[i] == '\\' && !escape) {
      escape = 1;
    } else {
      arg[off++] = s[i];
      escape = 0;
    }
  }

  arg[off] = 0;
  return arg;
}


static int
args_split(const char* args, char** argv, size_t size) {
  char* buf = strdup(args);
  size_t len = strlen(buf);
  int escape = 0;
  int argc = 0;

  memset(argv, 0, size*sizeof(char*));
  for(int i=0; i<len && argc<size; i++) {
    if(escape) {
      escape = 0;
      continue;
    }

    if(buf[i] == '\\') {
      escape = 1;
      continue;
    }

    if(buf[i] == ' ') {
      buf[i] = 0;
      continue;
    }

    if(buf[i] && !i) {
      argv[argc++] = buf+i;
      continue;
    }

    if(buf[i] && !buf[i-1]) {
      argv[argc++] = buf+i;
    }
  }

  for(int i=0; i<argc; i++) {
    argv[i] = args_decode(argv[i]);
  }

  free(buf);

  return argc;
}


/**
 * Find the pid of a process with the given name.
 **/
static pid_t
sys_find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }

  if(!(buf=malloc(buf_size))) {
    perror("malloc");
    return -1;
  }

  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    perror("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && ki_pid != mypid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


int
sys_launch_homebrew(const char* cwd, const char* path, const char* args,
		    const char* env) {
  char* argv[255];
  char* envp[255];
  int stdio = -1;
  pid_t pid;

  if(!cwd) {
    cwd = "/";
  }

  if(!args) {
    args = "";
  }

  if(!env) {
    env = "";
  }

  // Notify("Launching ezRemote Client");

  // Detach the homebrew's stdout/stderr from the launcher. The launcher
  // exits right after the hijack, so a pipe or socket would give the
  // homebrew SIGPIPE/EPIPE on its first write.
  if((stdio=open("/dev/null", O_WRONLY)) < 0) {
    perror("open");
  }

  return_to_home_by_uri();

  args_split(args, argv, 255);
  args_split(env, envp, 255);
  pid = hbldr_launch(cwd, path, stdio, argv, envp);

  if(stdio >= 0) {
    close(stdio);
  }

  for(int i=0; argv[i]; i++) {
    free(argv[i]);
  }
  for(int i=0; envp[i]; i++) {
    free(envp[i]);
  }

  if(pid < 0) {
    return -1;
  }
  
  return 0;
}


__attribute__((constructor)) static void
sys_init(void) {
  pid_t pid;
  int err;

  if((err=sceUserServiceInitialize(0))) {
    perror("sceUserServiceInitialize");
    exit(err);
  }

  syscall(SYS_thr_set_name, -1, "ezremote_loader.elf");

  while((pid=sys_find_pid("ezremote_loader.elf")) > 0) {
    if((err=kill(pid, SIGKILL))) {
      perror("kill");
      exit(err);
    }
    sleep(1);
  }

  kernel_set_ucred_authid(-1, 0x4801000000000013L);
}


__attribute__((destructor)) static void
sys_fini(void) {
  sceUserServiceTerminate();
}
