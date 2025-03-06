#ifndef __SCE_KERNEL_H__
#define __SCE_KERNEL_H__

#include <stdio.h>

typedef struct _SceNotificationRequest {
    char useless1[45];
    char message[3075];
  } SceNotificationRequest;
  
  
extern "C"
{
    void sceSystemServicePowerTick();
    int sceKernelSendNotificationRequest(int, SceNotificationRequest*, size_t, int);
}
    
#endif