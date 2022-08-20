#include <stdlib.h>
#include <string.h>
#include <gccore.h>

#include "tools.h"

bool g_isvWii = false;

extern void __exception_setreload(int t);

int XyzzyGetKeys(void);

int main(int argc, char **argv)
{
    __exception_setreload(10);

    int ret = 0;

    InitConsole();
    InitPads();

    g_isvWii = IsWiiU();

    PrintHeadline();

    /* HW_AHBPROT check */
    if (AHBPROT_DISABLED)
    {
        /* Disable memory protection */
        DisableMemoryProtection();

        /* Patch ISFS access permissions */
        if (PatchNandFsPermissions())
        {
            /* Get keys */
            ret = XyzzyGetKeys();
            if (ret != -2) printf("\nPress any button to exit.");
        } else {
            printf("Failed to patch ISFS access permissions! Press any button to exit.");
        }
    } else {
        /* HW_AHBPROT flag is enabled */
        printf("The HW_AHBPROT hardware register is not disabled.\n");
        printf("Maybe you didn't load the application from a loader\n");
        printf("capable of passing arguments (you should use HBC\n");
        printf("1.1.0 or later). Or, perhaps, you don't have the\n");
        printf("\"<ahb_access/>\" node in the meta.xml file, which is\n");
        printf("very important.\n\n");
        printf("Remember that this application can't do its job\n");
        printf("without full hardware access rights.\n");
        printf("\nProcess cannot continue. Press any button to exit.");
    }

    if (ret != -2) WaitForButtonPress(NULL, NULL);

    Reboot();

    return 0;
}
