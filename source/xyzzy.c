/* OTP and SEEPROM access code taken from libOTP and libSEEPROM, respectively */
/* Both libraries were made by joedj (partially using code from MINI), so big thanks to him and Team fail0verflow */

/* Additional functions, like the hexdump feature and the device.cert dumping procedure, */
/* were taken from the original Xyzzy by bushing, which evidently served as a start point */

/* Kudos to WiiPower and Arikado for additional init code */

/* DarkMatterCore - 2019 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#include "tools.h"
#include "otp.h"
#include "mini_seeprom.h"

#define XYZZY_KEY_CNT   10

#define DEVCERT_SIZE    0x200

static u8 otp_ptr[OTP_SIZE];
static u8 seeprom_ptr[SEEPROM_SIZE];

static const char *key_names[] = {
    "boot1 Hash   ",
    "Common Key   ",
    "Console ID   ",
    "ECC Priv Key ",
    "NAND HMAC    ",
    "NAND AES Key ",
    "PRNG Key     ",
    "NG Key ID    ",
    "NG Signature ",
    "Korean Key   ",
    NULL
};

static void OTP_ClearData(void)
{
    memset(otp_ptr, 0, OTP_SIZE);
}

static bool OTP_ReadData(void)
{
    OTP_ClearData();
    
    u8 ret = otp_read(otp_ptr, 0, OTP_SIZE);
    
    return (ret == OTP_SIZE && *((u32*)(otp_ptr + (OTP_SIZE - 4))) != 0);
}

static void SEEPROM_ClearData(void)
{
    memset(seeprom_ptr, 0, SEEPROM_SIZE);
}

static bool SEEPROM_ReadData(void)
{
    SEEPROM_ClearData();
    
    u16 ret = seeprom_read(seeprom_ptr, 0, SEEPROM_SIZE);
    
    return (ret == SEEPROM_SIZE && *(((u32*)seeprom_ptr) + 2) != 0);
}

static bool FillOTPStruct(otp_t **out)
{
    if (!out)
    {
        printf("\n\nFatal error: invalid output OTP struct pointer.");
        return false;
    }
    
    otp_t *otp_data = memalign(32, sizeof(otp_t));
    if (!otp_data)
    {
        printf("\n\nFatal error: unable to allocate memory for OTP struct.");
        return false;
    }
    
    /* Read OTP data into otp_ptr pointer */
    if (!OTP_ReadData())
    {
        printf("\n\nFatal error: OTP_ReadData() failed.");
        OTP_ClearData();
        return false;
    }
    
    /* Copy OTP data into our allocated struct */
    memcpy(otp_data, otp_ptr, sizeof(otp_t));
    OTP_ClearData();
    
    /* Save OTP struct pointer */
    *out = otp_data;
    
    return true;
}

static bool FillSEEPROMStruct(seeprom_t **out)
{
    if (!out)
    {
        printf("\n\nFatal error: invalid output SEEPROM struct pointer.");
        return false;
    }
    
    seeprom_t *seeprom_data = memalign(32, sizeof(seeprom_t));
    if (!seeprom_data)
    {
        printf("\n\nFatal error: unable to allocate memory for SEEPROM struct.");
        return false;
    }
    
    /* Read SEEPROM data into seeprom_ptr pointer */
    if (!SEEPROM_ReadData())
    {
        printf("\n\nFatal error: SEEPROM_ReadData() failed.");
        SEEPROM_ClearData();
        return false;
    }
    
    /* Copy SEEPROM data into our allocated struct */
    memcpy(seeprom_data, seeprom_ptr, sizeof(seeprom_t));
    SEEPROM_ClearData();
    
    /* Save SEEPROM struct pointer */
    *out = seeprom_data;
    
    return true;
}

static void PrintAllKeys(otp_t *otp_data, seeprom_t *seeprom_data, FILE *fp)
{
    if (!otp_data || !fp) return;
    
    /* We'll use this for the Korean common key check */
    u8 null_key[16];
    memset(null_key, 0, 16);
    
    u8 i;
    for(i = 0; i < XYZZY_KEY_CNT; i++)
    {
        /* Do not print SEEPROM keys if its access is disabled */
        if (!seeprom_data && (i == 7 || i == 8 || i == 9)) continue;
        
        /* Only display the Korean common key if it's really available in the SEEPROM data */
        /* Otherwise, we'll just skip it */
        if (seeprom_data && i == 9 && !memcmp(seeprom_data->korean_key, null_key, sizeof(seeprom_data->korean_key))) continue;
        
        fprintf(fp, "[%d] %s:\t", i, key_names[i]);
        
        switch (i)
        {
            case 0: // boot1 Hash
                HexKeyDump(fp, otp_data->boot1_hash, sizeof(otp_data->boot1_hash));
                break;
            case 1: // Common Key
                HexKeyDump(fp, otp_data->common_key, sizeof(otp_data->common_key));
                break;
            case 2: // Console ID
                HexKeyDump(fp, otp_data->ng_id, sizeof(otp_data->ng_id));
                break;
            case 3: // ECC Priv Key
                HexKeyDump(fp, otp_data->ng_priv, sizeof(otp_data->ng_priv));
                break;
            case 4: // NAND HMAC
                HexKeyDump(fp, otp_data->nand_hmac, sizeof(otp_data->nand_hmac));
                break;
            case 5: // NAND AES Key
                HexKeyDump(fp, otp_data->nand_key, sizeof(otp_data->nand_key));
                break;
            case 6: // PRNG Key
                HexKeyDump(fp, otp_data->rng_key, sizeof(otp_data->rng_key));
                break;
            case 7: // NG Key ID
                HexKeyDump(fp, seeprom_data->ng_key_id, sizeof(seeprom_data->ng_key_id));
                break;
            case 8: // NG Signature
                HexKeyDump(fp, seeprom_data->ng_sig, sizeof(seeprom_data->ng_sig));
                break;
            case 9: // Korean Key
                HexKeyDump(fp, seeprom_data->korean_key, sizeof(seeprom_data->korean_key));
                break;
            default:
                break;
        }
        
        fprintf(fp, "\r\n");
    }
}

int XyzzyGetKeys(bool vWii)
{
    int ret = 0;
    FILE *fp = NULL;
    
    otp_t *otp_data = NULL;
    seeprom_t *seeprom_data = NULL;
    
    ret = SelectStorageDevice();
    
    if (ret >= 0)
    {
        char path[16] = {0};
        sprintf(path, "%s:/keys.txt", StorageDeviceMountName());
        
        fp = fopen(path, "w");
        if (!fp)
        {
            printf("\n\t- Unable to open keys.txt for writing.");
            printf("\n\t- Sorry, not writing keys to %s.", StorageDeviceString());
            sleep(2);
        }
    } else
    if (ret == -2)
    {
        return ret;
    }
    
    ret = 0;
    
    PrintHeadline();
    printf("Getting keys...\n\n");
    
    if (!FillOTPStruct(&otp_data))
    {
        ret = -1;
        sleep(2);
        goto out;
    }
    
    /* Access to the SEEPROM will be disabled in we're running under vWii */
    if (!vWii)
    {
        if (!FillSEEPROMStruct(&seeprom_data))
        {
            ret = -1;
            sleep(2);
            goto out;
        }
    }
    
    PrintAllKeys(otp_data, seeprom_data, stdout);
    
    if (fp)
    {
        PrintAllKeys(otp_data, seeprom_data, fp);
        
        /* This will create a hexdump of the device.cert in the selected device */
        u8 *devcert = memalign(32, 0x200);
        if (devcert)
        {
            memset(devcert, 42, 0x200); // Why... ?
            
            ret = ES_GetDeviceCert(devcert);
            if (ret)
            {
                printf("\n\nES_GetDeviceCert() returned %d.\n\n", ret);
            } else {
                fprintf(fp, "\r\nDevice cert:\r\n");
                HexDump(fp, devcert, 0x180);
            }
            
            free(devcert);
        } else {
            printf("\n\nError allocating memory for device certificate buffer.\n\n");
        }
    }
    
out:
    if (seeprom_data) free(seeprom_data);
    
    if (otp_data) free(otp_data);
    
    if (fp) fclose(fp);
    
    UnmountStorageDevice();
    
    return ret;
}
