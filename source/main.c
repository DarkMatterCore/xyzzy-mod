#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

#include "tools.h"
#include "xyzzy.h"

#define AHBPROT_DISABLED (*(vu32*)0xcd800064 == 0xFFFFFFFF)

extern void __exception_setreload(int);

int main(int argc, char* argv[])
{
	__exception_setreload(10);
	
	Init_Console();
	printf("\x1b[%u;%um", 37, false);
	
	PAD_Init();
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
	
	printheadline();
	vWii = IsWiiU();
	
	/* HW_AHBPROT check */
	if (!AHBPROT_DISABLED)
	{
		/* HW_AHBPROT access is disabled */
		printf("The HW_AHBPROT hardware register is not enabled.\n");
		printf("Maybe you didn't load the application from a loader\n");
		printf("capable of passing arguments (you should use HBC\n");
		printf("1.1.0 or later). Or, perhaps, you don't have the\n");
		printf("\"<ahb_access/>\" node in the meta.xml file, which is\n");
		printf("very important.\n\n");
		printf("Remember that this application can't do its job\n");
		printf("without full hardware access rights.\n");
		printf("\nProcess cannot continue. Press any button to exit.");
	} else {
		xyzzy_get_keys();
		printf("\nEnjoy! Press any button to exit.");
	}
	
	waitforbuttonpress(NULL, NULL);
	
	Reboot();
	
	return 0;
}
