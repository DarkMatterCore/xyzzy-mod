#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <sys/unistd.h>
#include <wiiuse/wpad.h>
#include <malloc.h>

#define VERSION             "1.2.8"

//#define IsWiiU()          (((*(vu32*)0xCD8005A0) >> 16) == 0xCAFE)
#define ResetScreen()       printf("\x1b[2J")

#define TITLE_UPPER(x)      ((u32)((x) >> 32))
#define TITLE_LOWER(x)      ((u32)(x))
#define TITLE_ID(x, y)      (((u64)(x) << 32) | (y))

#define ALIGN_UP(x, y)      ((((y) - 1) + (x)) & ~((y) - 1))

#define MAX_ELEMENTS(x)     (sizeof((x)) / sizeof((x)[0]))

bool IsWiiU(void);

void Reboot(void);

void InitPads(void);
void WaitForButtonPress(u32 *out, u32 *outGC);

void InitConsole();
void PrintHeadline();

void UnmountStorageDevice(void);
int SelectStorageDevice(void);
char *StorageDeviceString(void);
char *StorageDeviceMountName(void);

void HexDump(FILE *fp, void *d, size_t len);
void HexKeyDump(FILE *fp, void *d, size_t len);

signed_blob *GetSignedTMDFromTitle(u64 title_id, u32 *out_size);

static inline tmd *GetTMDFromSignedBlob(signed_blob *stmd)
{
    if (!stmd || !IS_VALID_SIGNATURE(stmd)) return NULL;
    return (tmd*)((u8*)stmd + SIGNATURE_SIZE(stmd));
}

void *ReadFileFromFlashFileSystem(const char *path, u32 *out_size);

bool CheckIfFlashFileSystemFileExists(const char *path);

#endif /* __TOOLS_H__ */
