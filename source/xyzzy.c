/* OTP and SEEPROM access code taken from libOTP and libSEEPROM, respectively */
/* Both libraries were made by joedj (partially using code from MINI), so big thanks to him and Team fail0verflow */

/* Additional functions, like the hexdump feature and the device.cert dumping procedure, */
/* were taken from the original Xyzzy by bushing, which evidently served as a start point */

/* Kudos to WiiPower and Arikado for additional init code */
/* Kudos to InvoxiPlayGames for speeding up the key dumping algorithms */

/* DarkMatterCore - 2020-2022 */

#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <network.h>

#include "tools.h"
#include "otp.h"
#include "mini_seeprom.h"
#include "vwii_sram_otp.h"
#include "sha1.h"
#include "aes.h"
#include "boot0.h"
#include "xxhash.h"

#define SYSTEM_MENU_TID     (u64)0x0000000100000002

#define DEVCERT_BUF_SIZE    0x200
#define DEVCERT_SIZE        0x180

#define ANCAST_HEADER_MAGIC (u32)0xEFA282D9

typedef struct {
    char human_info[0x100];
    otp_t otp_data;
    u8 otp_padding[0x80];
    seeprom_t seeprom_data;
    u8 seeprom_padding[0x100];
} bootmii_keys_bin_t;

typedef struct {
    char content_name[8];
    sha1 content_hash;
} __attribute__((packed)) content_map_entry_t;

typedef struct {
    u8 key[64];
    u32 key_size;
    u32 xxhash;
    u8 hash[SHA1HashSize];
    bool retrieved;
} additional_keyinfo_t;

typedef struct {
    u32 magic;
    u32 unk_1;
    u32 signature_offset;
    u32 unk_2;
    u8 unk_3[0x10];
    u32 signature_type;
    u8 signature[0x38];
    u8 padding_1[0x44];
    u16 unk_4;
    u8 unk_5;
    u8 unk_6;
    u32 unk_7;
    u32 hash_type;
    u32 body_size;
    u8 body_hash[0x14];
    u8 padding_2[0x3C];
} ppc_ancast_image_header_t;

static const u8 ATTRIBUTE_ALIGN(16) vwii_ancast_key[0x10] = { 0x2E, 0xFE, 0x8A, 0xBC, 0xED, 0xBB, 0x7B, 0xAA, 0xE3, 0xC0, 0xED, 0x92, 0xFA, 0x29, 0xF8, 0x66 };
static const u8 ATTRIBUTE_ALIGN(16) vwii_ancast_iv[0x10]  = { 0x59, 0x6D, 0x5A, 0x9A, 0xD7, 0x05, 0xF9, 0x4F, 0xE1, 0x58, 0x02, 0x6F, 0xEA, 0xA7, 0xB8, 0x87 };

static u8 otp_ptr[OTP_SIZE] = {0};
static u8 seeprom_ptr[SEEPROM_SIZE] = {0};

static additional_keyinfo_t additional_keys[] = {
    {
        // SD Key. Retrieved from the ES module from the current IOS.
        .key = {0},
        .key_size = 16,
        .xxhash = 0xF655F81B,
        .hash = { 0x10, 0x37, 0xD8, 0x80, 0x10, 0x2F, 0xF0, 0x21, 0xC2, 0x2B, 0xA8, 0xF5, 0xDF, 0x53, 0xD7, 0x98, 0xCF, 0x44, 0xDD, 0x0B },
        .retrieved = false
    },
    {
        // SD IV. Retrieved from System Menu binary.
        .key = {0},
        .key_size = 16,
        .xxhash = 0xBBD8F75D,
        .hash = { 0x25, 0xAE, 0xEF, 0x2E, 0x60, 0x1E, 0xDE, 0x3E, 0x16, 0x17, 0x54, 0x3B, 0xEB, 0x2E, 0xDE, 0xB0, 0x8A, 0xF8, 0x7D, 0xA8 },
        .retrieved = false
    },
    {
        // MD5 Blanker. Retrieved from System Menu binary.
        .key = {0},
        .key_size = 16,
        .xxhash = 0xEE88846F,
        .hash = { 0x3D, 0xAB, 0xA9, 0xEF, 0x67, 0xCA, 0x94, 0xBF, 0x08, 0x28, 0xEC, 0x04, 0x39, 0x4A, 0x53, 0x13, 0x4D, 0x33, 0x1C, 0x1F },
        .retrieved = false
    },
    {
        // MAC Address. Retrieved from /dev/net virtual device. Console specific so this isn't hashed. Used to generate custom savedata.
        .key = {0},
        .key_size = 6,
        .xxhash = 0,
        .hash = {0},
        .retrieved = false
    }
};

static const char *priiloader_files[] = {
    "content/title_or.tmd",
    "data/loader.ini",
    "data/hackshas.ini",
    "data/hacksh_s.ini",
    "data/password.txt",
    "data/main.nfo",
    "data/main.bin"
};

static const u32 priiloader_files_count = (u32)MAX_ELEMENTS(priiloader_files);

static const char *key_names_stdout[] = {
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
    "SD Key      ",
    "SD IV       ",
    "MD5 Blanker ",
    "MAC Address ",
    NULL
};

static const char *key_names_txt[] = {
    "boot1_hash      ",
    "wii_common_key  ",
    "console_id      ",
    "ecc_private_key ",
    "nand_hmac       ",
    "nand_aes_key    ",
    "prng_key        ",
    "ng_key_id       ",
    "ng_signature    ",
    "wii_korean_key  ",
    "sd_key          ",
    "sd_iv           ",
    "md5_blanker     ",
    "mac_address     ",
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

    return (ret == OTP_SIZE);
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
        printf("Fatal error: invalid output OTP struct pointer.\n\n");
        return false;
    }

    otp_t *otp_data = memalign(32, sizeof(otp_t));
    if (!otp_data)
    {
        printf("Fatal error: unable to allocate memory for OTP struct.\n\n");
        return false;
    }

    /* Read OTP data into otp_ptr pointer */
    if (!OTP_ReadData())
    {
        printf("Fatal error: OTP_ReadData() failed.\n\n");
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
        printf("Fatal error: invalid output SEEPROM struct pointer.\n\n");
        return false;
    }

    seeprom_t *seeprom_data = memalign(32, sizeof(seeprom_t));
    if (!seeprom_data)
    {
        printf("Fatal error: unable to allocate memory for SEEPROM struct.\n\n");
        return false;
    }

    /* Read SEEPROM data into seeprom_ptr pointer */
    if (!SEEPROM_ReadData())
    {
        printf("Fatal error: SEEPROM_ReadData() failed.\n\n");
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

static bool FillBootMiiKeysStruct(otp_t *otp_data, seeprom_t *seeprom_data, bootmii_keys_bin_t **out)
{
    if (!otp_data || !seeprom_data || !out)
    {
        printf("Fatal error: invalid OTP/SEEPROM/BootMiiKeys struct pointer(s).\n\n");
        return false;
    }

    bootmii_keys_bin_t *bootmii_keys = memalign(32, sizeof(bootmii_keys_bin_t));
    if (!bootmii_keys)
    {
        printf("Fatal error: unable to allocate memory for BootMiiKeys struct.\n\n");
        return false;
    }

    /* Fill structure with zeroes */
    memset(bootmii_keys, 0, sizeof(bootmii_keys_bin_t));

    /* Fill human info text block */
    sprintf(bootmii_keys->human_info, "BackupMii v1, ConsoleID: %08x\n", *((u32*)otp_data->ng_id));

    /* Fill OTP block */
    memcpy(&(bootmii_keys->otp_data), otp_data, sizeof(otp_t));

    /* Fill SEEPROM block */
    memcpy(&(bootmii_keys->seeprom_data), seeprom_data, sizeof(seeprom_t));

    /* Save BootMiiKeys struct pointer */
    *out = bootmii_keys;

    return true;
}

static void RetrieveSDKey(void)
{
    sha1 hash = {0};
    additional_keyinfo_t *sd_key = &(additional_keys[0]);

    /* Look for our key within the currently loaded IOS binary */
    for(u32 addr = MEM2_IOS_LOOKUP_START; addr < MEM2_IOS_LOOKUP_END; addr += 4)
    {
        /* Bail out if a read operation could exceed our memory extents */
        if ((addr + sd_key->key_size) > MEM2_IOS_LOOKUP_END) break;

        /* Calculate XXHash checksum over the current chunk */
        /* Since the collision potential in XXHash is considerably higher, we'll use software-based SHA1 calculation as a failsafe if we find a XXHash match */
        /* We will only proceed if both hashes match */
        u8 *ptr = (u8*)addr;
        if (XXH32(ptr, sd_key->key_size, 0) != sd_key->xxhash || SHA1(ptr, sd_key->key_size, hash) != shaSuccess || memcmp(hash, sd_key->hash, SHA1HashSize) != 0) continue;

        //printf("Found SD Key at 0x%X\n\n", addr);
        memcpy(sd_key->key, ptr, sd_key->key_size);
        sd_key->retrieved = true;
        break;
    }
}

static void RetrieveSystemMenuKeys(void)
{
    signed_blob *sysmenu_stmd = NULL;
    u32 sysmenu_stmd_size = 0;

    tmd *sysmenu_tmd = NULL;
    tmd_content *sysmenu_boot_content = NULL;

    char content_path[ISFS_MAXPATH] = {0};
    u8 *sysmenu_boot_content_data = NULL;
    u32 sysmenu_boot_content_size = 0;

    u8 *binary_body = NULL;

    bool priiloader = false;

    sha1 hash = {0};

    /* Get System Menu TMD */
    sysmenu_stmd = GetSignedTMDFromTitle(SYSTEM_MENU_TID, &sysmenu_stmd_size);
    if (!sysmenu_stmd)
    {
        printf("Error retrieving System Menu TMD!\n\n");
        return;
    }

    sysmenu_tmd = GetTMDFromSignedBlob(sysmenu_stmd);

    /* Get System Menu TMD boot content entry */
    sysmenu_boot_content = &(sysmenu_tmd->contents[sysmenu_tmd->boot_index]);

    /* Check for Priiloader */
    for(u32 i = 0; i < priiloader_files_count; i++)
    {
        sprintf(content_path, "/title/%08x/%08x/%s", TITLE_UPPER(SYSTEM_MENU_TID), TITLE_LOWER(SYSTEM_MENU_TID), priiloader_files[i]);
        if (CheckIfFlashFileSystemFileExists(content_path))
        {
            priiloader = true;
            break;
        }
    }

    /* Generate boot content path and read it */
    sprintf(content_path, "/title/%08x/%08x/content/%08x.app", TITLE_UPPER(SYSTEM_MENU_TID), TITLE_LOWER(SYSTEM_MENU_TID), \
            priiloader ? (0x10000000 | sysmenu_boot_content->cid) : sysmenu_boot_content->cid);
    sysmenu_boot_content_data = (u8*)ReadFileFromFlashFileSystem(content_path, &sysmenu_boot_content_size);
    if (!sysmenu_boot_content_data && priiloader)
    {
        sprintf(content_path, "/title/%08x/%08x/content/%08x.app", TITLE_UPPER(SYSTEM_MENU_TID), TITLE_LOWER(SYSTEM_MENU_TID), sysmenu_boot_content->cid);
        sysmenu_boot_content_data = (u8*)ReadFileFromFlashFileSystem(content_path, &sysmenu_boot_content_size);
    }

    if (!sysmenu_boot_content_data)
    {
        printf("Failed to read System Menu boot content data!\n\n");
        goto out;
    }

    if (g_isvWii)
    {
        /* Retrieve a pointer to the PPC Ancast Image header */
        ppc_ancast_image_header_t *ancast_image_header = (ppc_ancast_image_header_t*)(sysmenu_boot_content_data + 0x500);
        if (ancast_image_header->magic != ANCAST_HEADER_MAGIC)
        {
            printf("Invalid vWii System Menu ancast image header magic word!\n\n");
            goto out;
        }

        /* Set the binary body pointer to the end of the PPC Ancast Image header and update size */
        binary_body = (sysmenu_boot_content_data + 0x500 + sizeof(ppc_ancast_image_header_t));
        sysmenu_boot_content_size = ancast_image_header->body_size;

        /* Calculate hash */
        if (SHA1(binary_body, sysmenu_boot_content_size, hash) != shaSuccess)
        {
            printf("Failed to calculate encrypted vWii System Menu ancast image body SHA-1 hash!\n\n");
            goto out;
        }

        /* Compare hashes */
        if (memcmp(hash, ancast_image_header->body_hash, SHA1HashSize) != 0)
        {
            printf("Encrypted vWii System Menu ancast image body SHA-1 hash mismatch!\n\n");
            goto out;
        }

        /* Decrypt System Menu binary using baked in vWii Ancast Key and IV (unavoidable...) */
        if (aes_128_cbc_decrypt(vwii_ancast_key, vwii_ancast_iv, binary_body, sysmenu_boot_content_size) != 0)
        {
            printf("Failed to decrypt vWii System Menu ancast image body!\n\n");
            goto out;
        }
    } else {
        /* Set the binary body pointer to our allocated buffer */
        binary_body = sysmenu_boot_content_data;
    }

    /* Retrieve keys */
    for(u32 offset = 0; offset < sysmenu_boot_content_size; offset += 4)
    {
        /* Bail out if we have retrieved both keys, or if a read operation could exceed our buffer extents */
        if ((additional_keys[1].retrieved && additional_keys[2].retrieved) || (sysmenu_boot_content_size - offset) < 16) break;

        /* Calculate XXHash checksum over the current chunk */
        /* Since the collision potential in XXHash is considerably higher, we'll use software-based SHA1 calculation as a failsafe if we find a XXHash match */
        u32 xxhash = XXH32(binary_body + offset, 16, 0);
        if ((xxhash != additional_keys[1].xxhash && xxhash != additional_keys[2].xxhash) || SHA1(binary_body + offset, 16, hash) != shaSuccess) continue;

        /* Determine additional key index based on the calculated SHA1 checksum */
        u8 idx = 0;

        if (!additional_keys[1].retrieved && !memcmp(hash, additional_keys[1].hash, SHA1HashSize))
        {
            idx = 1;
        } else
        if (!additional_keys[2].retrieved && !memcmp(hash, additional_keys[2].hash, SHA1HashSize))
        {
            idx = 2;
        }

        if (!idx) continue;

        //printf("Found %s in \"%s\" at 0x%X.\n\n", idx == 1 ? "SD IV" : "MD5 Blanker", content_path, offset);
        memcpy(additional_keys[idx].key, binary_body + offset, 16);
        additional_keys[idx].retrieved = true;

        offset += 12;
    }

out:
    if (sysmenu_boot_content_data) free(sysmenu_boot_content_data);
    if (sysmenu_stmd) free(sysmenu_stmd);
}

static void GetMACAddress(void)
{
    s32 ret = net_get_mac_address(&(additional_keys[3].key));
    if (ret >= 0)
    {
        //printf("Got WLAN MAC address.\n\n");
        additional_keys[3].retrieved = true;
    } else {
        printf("net_get_mac_address failed! (%d)\n\n", ret);
    }
}

static void PrintAllKeys(const otp_t *otp_data, const seeprom_t *seeprom_data, const vwii_sram_otp_t *sram_otp_data, FILE *fp)
{
    if (!otp_data || !fp) return;

    u8 key_idx = 1;
    bool is_txt = (fp != stdout);
    const char **key_names = (is_txt ? key_names_txt : key_names_stdout);

    /* We'll use this for the Korean common key check */
    u8 null_key[16] = {0};

    /* We'll use these to fetch the data that may come from multiple sources */
    u32 ng_key_id = 0;
    const u8 *korean_key = NULL, *ng_sig = NULL;

    if (seeprom_data)
    {
        korean_key = seeprom_data->korean_key;
        ng_sig = seeprom_data->ng_sig;
        ng_key_id = seeprom_data->ng_key_id;
    } else
    if (sram_otp_data)
    {
        korean_key = sram_otp_data->korean_key;
        ng_sig = sram_otp_data->ng_sig;
        ng_key_id = sram_otp_data->ng_key_id;
    }

    for(u8 i = 0; key_names[i]; i++)
    {
        /* Only display these keys if they're truly available in the data we have */
        /* Otherwise, we'll just skip them */
        if ((i == 7 && !ng_key_id) || (i == 8 && !ng_sig) || (i == 9 && (!korean_key || !memcmp(korean_key, null_key, sizeof(null_key))))) continue;

        /* Only display the current additional key if we retrieved it */
        if (i >= 10 && !additional_keys[i - 10].retrieved) continue;

        if (is_txt)
        {
            fprintf(fp, "%s= ", key_names[i]);
        } else {
            fprintf(fp, "[%u] %s: ", key_idx, key_names[i]);
        }

        switch(i)
        {
            case 0: // boot1 Hash
                HexKeyDump(fp, otp_data->boot1_hash, sizeof(otp_data->boot1_hash), !is_txt);
                break;
            case 1: // Common Key
                HexKeyDump(fp, otp_data->common_key, sizeof(otp_data->common_key), !is_txt);
                break;
            case 2: // Console ID
                HexKeyDump(fp, otp_data->ng_id, sizeof(otp_data->ng_id), !is_txt);
                break;
            case 3: // ECC Priv Key
                HexKeyDump(fp, otp_data->ng_priv, sizeof(otp_data->ng_priv), !is_txt);
                break;
            case 4: // NAND HMAC
                HexKeyDump(fp, otp_data->nand_hmac, sizeof(otp_data->nand_hmac), !is_txt);
                break;
            case 5: // NAND AES Key
                HexKeyDump(fp, otp_data->nand_key, sizeof(otp_data->nand_key), !is_txt);
                break;
            case 6: // PRNG Key
                HexKeyDump(fp, otp_data->rng_key, sizeof(otp_data->rng_key), !is_txt);
                break;
            case 7: // NG Key ID
                HexKeyDump(fp, &ng_key_id, sizeof(ng_key_id), !is_txt);
                break;
            case 8: // NG Signature
                HexKeyDump(fp, ng_sig, MEMBER_SIZE(seeprom_t, ng_sig), !is_txt);
                break;
            case 9: // Korean Key
                HexKeyDump(fp, korean_key, MEMBER_SIZE(seeprom_t, korean_key), !is_txt);
                break;
            default: // Additional keys
                HexKeyDump(fp, additional_keys[i - 10].key, additional_keys[i - 10].key_size, !is_txt);
                break;
        }

        fprintf(fp, "\r\n");

        key_idx++;
    }
}

int XyzzyGetKeys(void)
{
    int ret = 0;
    FILE *fp = NULL;
    char ATTRIBUTE_ALIGN(32) path[128] = {0};
    char *pch = NULL;

    otp_t *otp_data = NULL;
    seeprom_t *seeprom_data = NULL;
    vwii_sram_otp_t *sram_otp = NULL;
    bootmii_keys_bin_t *bootmii_keys = NULL;
    u8 *devcert = NULL, *boot0 = NULL;
    u16 boot0_size = (!g_isvWii ? BOOT0_RVL_SIZE : BOOT0_WUP_SIZE);

    ret = SelectStorageDevice();
    if (ret == -2) return ret;
    ret = 0;

    PrintHeadline();
    printf("Getting keys, please wait...\n\n");

    if (!FillOTPStruct(&otp_data))
    {
        ret = -1;
        sleep(2);
        goto out;
    }

    if (!g_isvWii)
    {
        /* Access to the SEEPROM will be disabled in we're running under vWii */
        if (!FillSEEPROMStruct(&seeprom_data))
        {
            ret = -1;
            sleep(2);
            goto out;
        }

        if (!FillBootMiiKeysStruct(otp_data, seeprom_data, &bootmii_keys))
        {
            ret = -1;
            sleep(2);
            goto out;
        }
    } else {
        /* Under vWii, many once-SEEPROM values are fetched from OTP by c2w and stored in the end of IOS SRAM. */
        sram_otp = memalign(32, SRAM_OTP_SIZE);
        if (sram_otp)
        {
            u16 rd = vwii_sram_otp_read(sram_otp, 0, SRAM_OTP_SIZE);
            if (rd != SRAM_OTP_SIZE)
            {
                free(sram_otp);
                sram_otp = NULL;
                printf("vwii_sram_otp_read failed! (%u).\n\n", rd);
            }
        } else {
            printf("Error allocating memory for vWii SRAM OTP buffer.\n\n");
        }
    }

    /* Retrieve SD key from IOS */
    RetrieveSDKey();

    /* Initialize filesystem driver */
    ret = ISFS_Initialize();
    if (ret >= 0)
    {
        /* Retrieve keys from System Menu binary */
        RetrieveSystemMenuKeys();

        /* Deinitialize filesystem driver */
        ISFS_Deinitialize();
    } else {
        printf("ISFS_Initialize failed! (%d)\n\n", ret);
    }

    /* Get MAC address */
    GetMACAddress();

    /* Get device certificate */
    devcert = memalign(32, DEVCERT_BUF_SIZE);
    if (devcert)
    {
        memset(devcert, 42, DEVCERT_BUF_SIZE); // Why... ?

        ret = ES_GetDeviceCert(devcert);
        if (ret < 0)
        {
            free(devcert);
            devcert = NULL;
            printf("ES_GetDeviceCert failed! (%d)\n\n", ret);
        }
    } else {
        printf("Error allocating memory for device certificate buffer.\n\n");
    }

    /* Get boot0 dump */
    boot0 = memalign(32, boot0_size);
    if (boot0)
    {
        u16 rd = boot0_read(boot0, 0, boot0_size);
        if (rd != boot0_size)
        {
            free(boot0);
            boot0 = NULL;
            printf("boot0_read failed! (%u).\n\n", rd);
        }
    } else {
        printf("Error allocating memory for boot0 buffer.\n\n");
    }

    /* Print all keys to stdout */
    PrintAllKeys(otp_data, seeprom_data, sram_otp, stdout);

    /* Create output directory tree */
    sprintf(path, "%s:/xyzzy", StorageDeviceMountName());
    mkdir(path, 0777);

    sprintf(path + strlen(path), "/%08x", *((u32*)otp_data->ng_id));
    mkdir(path, 0777);

    strcat(path, "/");
    pch = (path + strlen(path));

    /* Print all keys to output txt */
    sprintf(pch, "keys.txt");
    fp = fopen(path, "w");
    if (fp)
    {
        PrintAllKeys(otp_data, seeprom_data, sram_otp, fp);
        fclose(fp);
        fp = NULL;
    } else {
        printf("\t- Unable to open keys.txt for writing.\n");
        printf("\t- Sorry, not writing keys to %s.\n\n", StorageDeviceString());
        sleep(2);
    }

    if (devcert)
    {
        /* Save raw device.cert */
        sprintf(pch, "device.cert");
        fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(devcert, 1, DEVCERT_SIZE, fp);
            fclose(fp);
            fp = NULL;
        } else {
            printf("\n\t- Unable to open device.cert for writing.");
            printf("\n\t- Sorry, not writing raw device.cert to %s.\n", StorageDeviceString());
            sleep(2);
        }
    }

    /* Save raw OTP data */
    sprintf(pch, "otp.bin");
    fp = fopen(path, "wb");
    if (fp)
    {
        fwrite(otp_data, 1, sizeof(otp_t), fp);
        fclose(fp);
        fp = NULL;
    } else {
        printf("\n\t- Unable to open otp.bin for writing.");
        printf("\n\t- Sorry, not writing raw OTP data to %s.\n", StorageDeviceString());
        sleep(2);
    }

    if (!g_isvWii)
    {
        /* Save raw SEEPROM data */
        sprintf(pch, "seeprom.bin");
        fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(seeprom_data, 1, sizeof(seeprom_t), fp);
            fclose(fp);
            fp = NULL;
        } else {
            printf("\n\t- Unable to open seeprom.bin for writing.");
            printf("\n\t- Sorry, not writing raw SEEPROM data to %s.\n", StorageDeviceString());
            sleep(2);
        }

        /* Save BootMii keys file */
        sprintf(pch, "bootmii_keys.bin");
        fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(bootmii_keys, 1, sizeof(bootmii_keys_bin_t), fp);
            fclose(fp);
            fp = NULL;
        } else {
            printf("\n\t- Unable to open bootmii_keys.bin for writing.");
            printf("\n\t- Sorry, not writing BootMii keys.bin data to %s.\n", StorageDeviceString());
            sleep(2);
        }
    } else {
        /* Save raw OTP bank 6 data from SRAM (vWii) */
        sprintf(pch, "vwii_sram_otp.bin");
        fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(sram_otp, 1, sizeof(vwii_sram_otp_t), fp);
            fclose(fp);
            fp = NULL;
        } else {
            printf("\n\t- Unable to open otp_sram_vwii.bin for writing.");
            printf("\n\t- Sorry, not writing vWii SRAM OTP data to %s.\n", StorageDeviceString());
            sleep(2);
        }
    }

    if (boot0)
    {
        /* Save raw boot0.bin */
        sprintf(pch, "boot0.bin");
        fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(boot0, 1, boot0_size, fp);
            fclose(fp);
            fp = NULL;
        } else {
            printf("\n\t- Unable to open boot0.bin for writing.");
            printf("\n\t- Sorry, not writing raw boot0.bin to %s.\n", StorageDeviceString());
            sleep(2);
        }
    }

out:
    if (boot0) free(boot0);

    if (devcert) free(devcert);

    if (bootmii_keys) free(bootmii_keys);

    if (sram_otp) free(sram_otp);

    if (seeprom_data) free(seeprom_data);

    if (otp_data) free(otp_data);

    if (fp) fclose(fp);

    UnmountStorageDevice();

    return ret;
}
