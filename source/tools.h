#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <sys/unistd.h>
#include <wiiuse/wpad.h>

#define VERSION             "1.2.5"

//#define IsWiiU()          (((*(vu32*)0xCD8005A0) >> 16) == 0xCAFE)
#define ResetScreen()       printf("\x1b[2J")

#define AHBPROT_DISABLED    ((*(vu32*)0xCD800064) == 0xFFFFFFFF)

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

#endif /* __TOOLS_H__ */
