#include <gccore.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include <ogc/machine/processor.h>

#include "tools.h"

#define USB_REG_BASE		0x0D040000
#define USB_REG_OP_BASE		(USB_REG_BASE + (read32(USB_REG_BASE) & 0xff))
#define USB_PORT_CONNECTED	(read32(USB_REG_OP_BASE + 0x44) & 0x0F)

extern DISC_INTERFACE __io_usbstorage;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

bool IsWiiU(void)
{
	s32 ret;
	u32 x;
	
	ret = ES_GetTitleContentsCount(TITLEID_200, &x);
	
	if (ret < 0) return false; // title was never installed
	
	if (x <= 0) return false; // title was installed but deleted via Channel Management
	
	return true;
}

void Reboot()
{
	if (*(u32*)0x80001800) exit(0);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void waitforbuttonpress(u32 *out, u32 *outGC)
{
	u32 pressed = 0;
	u32 pressedGC = 0;

	while (true)
	{
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1) | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0) | PAD_ButtonsDown(1) | PAD_ButtonsDown(2) | PAD_ButtonsDown(3);

		if(pressed || pressedGC) 
		{
			if (pressedGC)
			{
				// Without waiting you can't select anything
				usleep (20000);
			}
			if (out) *out = pressed;
			if (outGC) *outGC = pressedGC;
			return;
		}
	}
}

void Init_Console()
{
	// Initialise the video system
	VIDEO_Init();
	
	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	
	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);
	
	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	// Set console parameters
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

    // Initialize the console - CON_InitEx works after VIDEO_ calls
	CON_InitEx(rmode, x, y, w, h);

	// Clear the garbage around the edges of the console
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
}

void printheadline()
{
	int rows, cols;
	CON_GetMetrics(&cols, &rows);
	
	printf("Xyzzy v%s (unofficial).", VERSION);
	
	char buf[64];
	sprintf(buf, "IOS%ld (v%ld)", IOS_GetVersion(), IOS_GetRevision());
	printf("\x1B[%d;%dH", 0, cols-strlen(buf)-1);
	printf(buf);
	
	printf("\nOriginal code by bushing. Mod by DarkMatterCore.\n\n");
}

void set_highlight(bool highlight)
{
	if (highlight)
	{
		printf("\x1b[%u;%um", 47, false);
		printf("\x1b[%u;%um", 30, false);
	} else {
		printf("\x1b[%u;%um", 37, false);
		printf("\x1b[%u;%um", 40, false);
	}
}

void Con_ClearLine()
{
	int cols, rows;
	u32 cnt;

	printf("\r");
	fflush(stdout);

	/* Get console metrics */
	CON_GetMetrics(&cols, &rows);

	/* Erase line */
	for (cnt = 1; cnt < cols; cnt++)
	{
		printf(" ");
		fflush(stdout);
	}

	printf("\r");
	fflush(stdout);
}

void Close_SD()
{
	fatUnmount("sd");
	__io_wiisd.shutdown();
}

s32 Init_SD()
{
	Close_SD();
	if (!fatMountSimple("sd", &__io_wiisd)) return -1;
	return 0;
}

void Close_USB()
{
	fatUnmount("usb");
	__io_usbstorage.shutdown();
}

s32 Init_USB()
{
	Close_USB();
	
	if (!USB_PORT_CONNECTED)
	{
		return -1;
	} else {
		bool started = false;
		
		time_t tStart = time(0);
		while ((time(0) - tStart) < 10) // 10 seconds timeout
		{
			started = (__io_usbstorage.startup() && __io_usbstorage.isInserted());
			if (started) break;
			usleep(50000);
		}
		
		if (started)
		{
			if (!fatMountSimple("usb", &__io_usbstorage)) return -1;
			return 0;
		}
	}
	
	return -1;
}

bool select_device()
{
	u32 pressed;
	u32 pressedGC;
	int selection = 0;
	int ret;
	char *optionsstring[2] = { "< SD Card >", "< USB device >" };
	
	printf("Press HOME or Start to exit.\n\n");
	
	device = 0;
	
	while (true)
	{
		Con_ClearLine();
		
		printf("Select device: ");
		
		set_highlight(true);
		printf(optionsstring[selection]);
		set_highlight(false);
		
		waitforbuttonpress(&pressed, &pressedGC);
		
		if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{
			if (selection > 0)
			{
				selection--;
			} else {
				selection = 1;
			}
		}
		
		if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{
			if (selection < 1)
			{
				selection++;
			} else {
				selection = 0;
			}
		}
		
		if (pressed == WPAD_BUTTON_HOME || pressedGC == PAD_BUTTON_START) Reboot();
		
		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) break;
	}
	
	// SD card was selected.
	printf("Mounting %s... ", selection == 0 ? "SD card" : "USB device");
	ret = (selection == 0 ? Init_SD() : Init_USB());
	if (ret < 0)
	{
		printf("\n\t- fatMountSimple failed. Is your %s?", selection == 0 ? "SD card properly inserted" : "USB device properly connected");
		printf("\n\t- Sorry, not writing keys to %s.", selection == 0 ? "card" : "USB device");
		sleep(2);
		return false;
	} else {
		printf("OK!");
		sleep(2);
	}
	
	// Update the 'device' pointer.
	device = (selection + 1);
	
	return true;
}

char ascii(char s)
{
	if(s < 0x20) return '.';
	if(s > 0x7E) return '.';
	return s;
}

void hexdump(FILE *fp, void *d, int len)
{
	u8 *data;
	int i, off;
	data = (u8*)d;
	for (off=0; off<len; off += 16)
	{
		fprintf(fp, "%08x  ", off);
		for(i=0; i<16; i++)
		{
			if((i+off)>=len)
			{
				fprintf(fp, "   ");
			} else {
				fprintf(fp, "%02x ",data[off+i]);
			}
		}
		
		fprintf(fp, " ");
		for(i=0; i<16; i++)
		{
			if((i+off)>=len)
			{
				fprintf(fp," ");
			} else {
				fprintf(fp, "%c", ascii(data[off+i]));
			}
		}
		
		fprintf(fp,"\n");
	}
}

void hex_key_dump(FILE *fp, void *d, int len)
{
	u8 *data;
	int i;
	data = (u8*)d;
	
	for(i = 0; i < len; i++)
	{
		fprintf(fp, "%02x ", data[i]);
		if ((i % 16 == 15) && len > 16)
		{
			fprintf(fp, "\n\t\t\t\t\t");
		}
	}
}
