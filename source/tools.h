#include <sys/unistd.h>
#include <wiiuse/wpad.h>

#define VERSION "1.2.3"
#define IsWiiU() ((*(vu32*)(0xCD8005A0) >> 16 ) == 0xCAFE)
#define resetscreen() printf("\x1b[2J")

int device;
bool vWii;

void Reboot();
void waitforbuttonpress(u32 *out, u32 *outGC);
void Init_Console();
void printheadline();
void Close_SD();
void Close_USB();
bool select_device();
void hexdump(FILE *fp, void *d, int len);
void hex_key_dump(FILE *fp, void *d, int len);
