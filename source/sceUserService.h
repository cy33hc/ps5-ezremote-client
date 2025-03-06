#ifndef __SCE_USER_SERVICE_H__
#define __SCE_USER_SERVICE_H__

extern "C"
{
    int sceUserServiceInitialize(void *);
    int sceUserServiceGetForegroundUser(int *);
}

#endif