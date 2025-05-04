#ifndef __SCE_SYSTEM_SERVICE_H__
#define __SCE_SYSTEM_SERVICE_H__

typedef struct {
    char padding[8];
    char s_version[10]; // e.g. " 6.720.001"
    char unk[18]; // zeros
    uint32_t i_version; // e.g. 0x06720001
} KernelSwVersion;

extern "C"
{
    void sceSystemServicePowerTick();
}
    
#endif