#include <gccore.h>
#include <stdlib.h>
#include <string.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include <ogc/machine/processor.h>

#include "tools.h"

#define USB_REG_BASE		0x0D040000
#define USB_REG_OP_BASE		(USB_REG_BASE + (read32(USB_REG_BASE) & 0xff))
#define USB_PORT_CONNECTED	(read32(USB_REG_OP_BASE + 0x44) & 0x0F)

#define TITLEID_200         (u64)0x0000000100000200 // IOS512

extern DISC_INTERFACE __io_usbstorage;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

typedef enum {
    STORAGE_DEVICE_TYPE_NONE = 0,
    STORAGE_DEVICE_TYPE_SD,
    STORAGE_DEVICE_TYPE_USB,
    STORAGE_DEVICE_TYPE_CNT
} storage_device_type_t;

static storage_device_type_t device_type = STORAGE_DEVICE_TYPE_NONE;

static u64 tmd_tid ATTRIBUTE_ALIGN(32) = 0;
static u32 tmd_size ATTRIBUTE_ALIGN(32) = 0;

static s32 isfs_fd ATTRIBUTE_ALIGN(32) = 0;
static char isfs_file_path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {0};
static fstats isfs_file_stats ATTRIBUTE_ALIGN(32) = {0};

bool IsWiiU(void)
{
    s32 ret = 0;
    u32 x = 0;

    ret = ES_GetTitleContentsCount(TITLEID_200, &x);

    if (ret < 0) return false; // Title was never installed

    if (x == 0) return false; // Title was installed but deleted via Channel Management

    return true;
}

void Reboot(void)
{
    if (*(u32*)0x80001800) exit(0);
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void InitPads(void)
{
    PAD_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
}

void WaitForButtonPress(u32 *out, u32 *outGC)
{
    u32 pressed = 0;
    u32 pressedGC = 0;

    while(true)
    {
        WPAD_ScanPads();
        pressed = (WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1) | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3));

        PAD_ScanPads();
        pressedGC = (PAD_ButtonsDown(0) | PAD_ButtonsDown(1) | PAD_ButtonsDown(2) | PAD_ButtonsDown(3));

        if (pressed || pressedGC)
        {
            // Without waiting you can't select anything
            if (pressedGC) usleep(20000);

            if (out) *out = pressed;
            if (outGC) *outGC = pressedGC;

            break;
        }
    }
}

void InitConsole(void)
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
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    // Set console parameters
    int x = 24;
    int y = 32;
    int w = (rmode->fbWidth - 32);
    int h = (rmode->xfbHeight - 48);

    // Initialize the console - CON_InitEx works after VIDEO_ calls
    CON_InitEx(rmode, x, y, w, h);

    // Clear the garbage around the edges of the console
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

    printf("\x1b[%u;%um", 37, false);
}

void PrintHeadline(void)
{
    ResetScreen();

    int rows, cols;
    CON_GetMetrics(&cols, &rows);

    printf("Xyzzy v%s (unofficial).", VERSION);

    char buf[64];
    sprintf(buf, "IOS%d (v%d)", IOS_GetVersion(), IOS_GetRevision());
    printf("\x1B[%d;%dH", 0, cols - strlen(buf) - 1);
    printf(buf);

    printf("\nOriginal code by bushing (RIP). Maintained by DarkMatterCore.\n\n");
}

static void SetHighlight(bool highlight)
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

static void Con_ClearLine(void)
{
    int cols, rows, cnt;

    printf("\r");
    fflush(stdout);

    /* Get console metrics */
    CON_GetMetrics(&cols, &rows);

    /* Erase line */
    for(cnt = 1; cnt < cols; cnt++)
    {
        printf(" ");
        fflush(stdout);
    }

    printf("\r");
    fflush(stdout);
}

static void UnmountSD(void)
{
    fatUnmount("sd");
    __io_wiisd.shutdown();
}

static int MountSD(void)
{
    UnmountSD();
    if (!fatMountSimple("sd", &__io_wiisd)) return -1;
    return 0;
}

static void UnmountUSB(void)
{
    fatUnmount("usb");
    __io_usbstorage.shutdown();
}

static int MountUSB(void)
{
    UnmountUSB();

    if (!USB_PORT_CONNECTED)
    {
        return -1;
    } else {
        bool started = false;

        time_t tStart = time(0);
        while((time(0) - tStart) < 10) // 10 seconds timeout
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

void UnmountStorageDevice(void)
{
    switch(device_type)
    {
        case STORAGE_DEVICE_TYPE_SD:
            UnmountSD();
            break;
        case STORAGE_DEVICE_TYPE_USB:
            UnmountUSB();
            break;
        default:
            break;
    }
}

int SelectStorageDevice(void)
{
    u32 pressed, pressedGC;
    int ret = 0, selection = 0;

    const char *options[] = { "SD card", "USB device" };
    const int options_cnt = (sizeof(options) / sizeof(options[0]));

    printf("Press HOME or Start to exit.\n\n");

    while(true)
    {
        Con_ClearLine();

        printf("Select device: ");

        SetHighlight(true);
        printf("< %s >", options[selection]);
        SetHighlight(false);

        WaitForButtonPress(&pressed, &pressedGC);

        if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
        {
            if (selection > 0)
            {
                selection--;
            } else {
                selection = (options_cnt - 1);
            }
        }

        if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
        {
            if (selection < (options_cnt - 1))
            {
                selection++;
            } else {
                selection = 0;
            }
        }

        if (pressed == WPAD_BUTTON_HOME || pressedGC == PAD_BUTTON_START)
        {
            ret = -2;
            break;
        }

        if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) break;
    }

    if (ret == -2) return ret;

    printf("\n\nMounting %s...", options[selection]);

    switch(selection)
    {
        case 0:
            ret = MountSD();
            break;
        case 1:
            ret = MountUSB();
            break;
        default:
            break;
    }

    if (ret >= 0)
    {
        printf(" OK!");
        device_type = (storage_device_type_t)(selection + 1);
    } else {
        printf("\n\t- fatMountSimple failed.");
        printf("\n\t- Sorry, not writing keys to %s.", options[selection]);
    }

    sleep(2);

    return ret;
}

const char *StorageDeviceString(void)
{
    const char *str = NULL;

    switch(device_type)
    {
        case STORAGE_DEVICE_TYPE_SD:
            str = "SD card";
            break;
        case STORAGE_DEVICE_TYPE_USB:
            str = "USB device";
            break;
        default:
            break;
    }

    return str;
}

const char *StorageDeviceMountName(void)
{
    const char *str = NULL;

    switch(device_type)
    {
        case STORAGE_DEVICE_TYPE_SD:
            str = "sd";
            break;
        case STORAGE_DEVICE_TYPE_USB:
            str = "usb";
            break;
        default:
            break;
    }

    return str;
}

void HexKeyDump(FILE *fp, const void *d, size_t len, bool add_spaces)
{
    if (!fp || !d || !len) return;

    size_t i;
    const u8 *data = (const u8*)d;

    for(i = 0; i < len; i++)
    {
        fprintf(fp, "%02X", data[i]);

        if (add_spaces && (i + 1) < len)
        {
            if (((i + 1) % 16) > 0)
            {
                fprintf(fp, " ");
            } else {
                fprintf(fp, "\r\n                   ");
            }
        }
    }
}

signed_blob *GetSignedTMDFromTitle(u64 title_id, u32 *out_size)
{
    if (!out_size) return NULL;

    s32 ret = 0;
    signed_blob *stmd = NULL;
    bool success = false;

    tmd_tid = title_id;

    ret = ES_GetStoredTMDSize(tmd_tid, &tmd_size);
    if (ret < 0)
    {
        printf("ES_GetStoredTMDSize failed! (%d) (TID %X-%X)\n", ret, TITLE_UPPER(tmd_tid), TITLE_LOWER(tmd_tid));
        return NULL;
    }

    stmd = (signed_blob*)memalign(32, ALIGN_UP(tmd_size, 32));
    if (!stmd)
    {
        printf("Failed to allocate memory for TMD! (TID %X-%X)\n", TITLE_UPPER(tmd_tid), TITLE_LOWER(tmd_tid));
        return NULL;
    }

    ret = ES_GetStoredTMD(tmd_tid, stmd, tmd_size);
    if (ret < 0)
    {
        printf("ES_GetStoredTMD failed! (%d) (TID %X-%X)\n", ret, TITLE_UPPER(tmd_tid), TITLE_LOWER(tmd_tid));
        goto out;
    }

    if (!IS_VALID_SIGNATURE(stmd))
    {
        printf("Invalid TMD signature! (TID %X-%X)\n", TITLE_UPPER(tmd_tid), TITLE_LOWER(tmd_tid));
        goto out;
    }

    *out_size = tmd_size;
    success = true;

out:
    if (!success && stmd)
    {
        free(stmd);
        stmd = NULL;
    }

    return stmd;
}

void *ReadFileFromFlashFileSystem(const char *path, u32 *out_size)
{
    if (!path || !strlen(path) || !out_size) return NULL;

    s32 ret = 0;
    u8 *buf = NULL;
    bool success = false;

    snprintf(isfs_file_path, ISFS_MAXPATH, "%s", path);

    isfs_fd = ISFS_Open(isfs_file_path, ISFS_OPEN_READ);
    if (isfs_fd < 0)
    {
        printf("ISFS_Open(\"%s\") failed! (%d)\n", isfs_file_path, isfs_fd);
        return NULL;
    }

    ret = ISFS_GetFileStats(isfs_fd, &isfs_file_stats);
    if (ret < 0)
    {
        printf("ISFS_GetFileStats(\"%s\") failed! (%d)\n", isfs_file_path, ret);
        goto out;
    }

    if (!isfs_file_stats.file_length)
    {
        printf("\"%s\" is empty!\n", isfs_file_path);
        goto out;
    }

    buf = (u8*)memalign(32, ALIGN_UP(isfs_file_stats.file_length, 32));
    if (!buf)
    {
        printf("Failed to allocate memory for \"%s\"!\n", isfs_file_path);
        goto out;
    }

    ret = ISFS_Read(isfs_fd, buf, isfs_file_stats.file_length);
    if (ret < 0)
    {
        printf("ISFS_Read(\"%s\") failed! (%d)\n", isfs_file_path, ret);
        goto out;
    }

    *out_size = isfs_file_stats.file_length;
    success = true;

out:
    if (!success && buf)
    {
        free(buf);
        buf = NULL;
    }

    ISFS_Close(isfs_fd);
    isfs_fd = 0;

    return (void*)buf;
}

bool CheckIfFlashFileSystemFileExists(const char *path)
{
    if (!path || !strlen(path)) return NULL;

    snprintf(isfs_file_path, ISFS_MAXPATH, "%s", path);

    isfs_fd = ISFS_Open(isfs_file_path, ISFS_OPEN_READ);
    if (isfs_fd < 0) return false;

    ISFS_Close(isfs_fd);
    isfs_fd = 0;

    return true;
}
