#ifndef __SCE_APP_INST_UTIL_H__
#define __SCE_APP_INST_UTIL_H__

extern "C"
{
    int sceAppInstUtilGetTitleIdFromPkg(const char* pkgPath, char* titleId, int* isApp);
    int sceAppInstUtilAppUnInstall(const char* titleId);
}
    
#endif