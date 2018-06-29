/* OTP and SEEPROM access code taken from libOTP and libSEEPROM, respectively */
/* Both libraries were made by joedj (partially using code from MINI), so big thanks to him and Team fail0verflow */

/* Additional functions, like the hexdump feature and the device.cert dumping procedure, */
/* were taken from the original Xyzzy by bushing, which evidently served as a start point */

/* Kudos to WiiPower and Arikado for additional init code */

/* DarkMatterCore / PabloACZ - 2012 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#include "xyzzy.h"
#include "tools.h"
#include "mini_seeprom.h"

#define HW_OTP_COMMAND (*(vu32*)0xcd8001ec)
#define HW_OTP_DATA (*(vu32*)0xcd8001f0)
#define OTP_SIZE 0x80

#define SEEPROM_SIZE 0x100

static u8 otp_ptr[OTP_SIZE];
static u8 seeprom_ptr[SEEPROM_SIZE];

void OTP_Unmount()
{
	memset(otp_ptr, 0, OTP_SIZE);
}

static bool OTP_Mount()
{
	OTP_Unmount();
	
	u8 addr;
	
	for (addr = 0; addr < 32; addr++)
	{
		HW_OTP_COMMAND = 0x80000000 | addr;
		*(((u32 *)otp_ptr) + addr) = HW_OTP_DATA;
	}
	
	return *otp_ptr;
}

void SEEPROM_Unmount()
{
	memset(seeprom_ptr, 0, SEEPROM_SIZE);
}

static bool SEEPROM_Mount()
{
	SEEPROM_Unmount();
	
	return seeprom_read(seeprom_ptr, 0, SEEPROM_SIZE / 2) >= 0 && *(((u32 *)seeprom_ptr) + 2) != 0;
}

char *key_names[] = {
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
	NULL};

void print_all_keys(FILE *fp, bool access_seeprom)
{
	int i;
	u8 null_key[16];
	otp_t *otp_data = memalign(32, sizeof(otp_t));
	seeprom_t *seeprom_data = memalign(32, sizeof(seeprom_t));
	
	memset(null_key, 0, sizeof(null_key)); // We'll later use this for the Korean Key check
	
	/* Read OTP data into otp_ptr pointer */
	bool read_otp = OTP_Mount();
	if (!read_otp)
	{
		OTP_Unmount();
		free(otp_data);
		if (device == 1) Close_SD();
		if (device == 2) Close_USB();
		printf("\n\nFatal error: OTP_Mount failed.");
		sleep(2);
		Reboot();
	}
	
	memcpy(otp_data, otp_ptr, sizeof(otp_t));
	OTP_Unmount();
	
	if (access_seeprom)
	{
		/* Read SEEPROM data into seeprom_ptr pointer */
		bool read_seeprom = SEEPROM_Mount();
		if (!read_seeprom)
		{
			SEEPROM_Unmount();
			free(seeprom_data);
			if (device == 1) Close_SD();
			if (device == 2) Close_USB();
			printf("\n\nFatal error: SEEPROM_Mount failed.");
			sleep(2);
			Reboot();
		}
		
		memcpy(seeprom_data, seeprom_ptr, sizeof(seeprom_t));
		SEEPROM_Unmount();
	}
	
	for (i = 0; i < 10; i++)
	{
		/* The Korean Key will be shown only if it is actually present on the SEEPROM */
		/* If it isn't, we'll skip it */
		if (access_seeprom && i == 9 && memcmp(seeprom_data->korean_key, null_key, sizeof(seeprom_data->korean_key)) == 0)
		{
			continue;
		} else
		if (!access_seeprom && (i == 7 || i == 8 || i == 9))
		{
			/* Do not print SEEPROM values if its access is disabled */
			continue;
		} else {
			fprintf(fp, "[%d] %s:\t", i, key_names[i]);
		}
		
		switch (i)
		{
			case 0: // boot1 Hash
				hex_key_dump(fp, (void *)otp_data->boot1_hash, sizeof(otp_data->boot1_hash));
				break;
			case 1: // Common Key
				hex_key_dump(fp, (void *)otp_data->common_key, sizeof(otp_data->common_key));
				break;
			case 2: // Console ID
				hex_key_dump(fp, (void *)otp_data->ng_id, sizeof(otp_data->ng_id));
				break;
			case 3: // ECC Priv Key
				hex_key_dump(fp, (void *)otp_data->ng_priv, sizeof(otp_data->ng_priv));
				break;
			case 4: // NAND HMAC
				hex_key_dump(fp, (void *)otp_data->nand_hmac, sizeof(otp_data->nand_hmac));
				break;
			case 5: // NAND AES Key
				hex_key_dump(fp, (void *)otp_data->nand_key, sizeof(otp_data->nand_key));
				break;
			case 6: // PRNG Key
				hex_key_dump(fp, (void *)otp_data->rng_key, sizeof(otp_data->rng_key));
				break;
			case 7: // NG Key ID
				if (access_seeprom) hex_key_dump(fp, (void *)seeprom_data->ng_key_id, sizeof(seeprom_data->ng_key_id));
				break;
			case 8: // NG Signature
				if (access_seeprom) hex_key_dump(fp, (void *)seeprom_data->ng_sig, sizeof(seeprom_data->ng_sig));
				break;
			case 9: // Korean Key
				if (access_seeprom && memcmp(seeprom_data->korean_key, null_key, sizeof(seeprom_data->korean_key)) != 0)
				{
					hex_key_dump(fp, (void *)seeprom_data->korean_key, sizeof(seeprom_data->korean_key));
				}
				break;
			default:
				break;
		}
		
		fprintf(fp, "\n");
	}
	
	/* Time to free these babies */
	free(otp_data);
	free(seeprom_data);
}

void xyzzy_get_keys()
{
	int i;
	FILE *fp = NULL;
	
	bool xyzzy_device = select_device();
	if (xyzzy_device)
	{
		char path[16] = {0};
		sprintf(path, "%s:/keys.txt", device == 1 ? "sd" : "usb");
		
		fp = fopen(path, "w");
		if (!fp)
		{
			printf("\n\t- Unable to open keys.txt for writing.");
			printf("\n\t- Sorry, not writing keys to %s.", device == 1 ? "card" : "USB device");
			if (device == 1) Close_SD();
			if (device == 2) Close_USB();
			sleep(2);
		}
	}
	
	resetscreen();
	printheadline();
	printf("Getting keys...\n\n");
	
	/* This is where the magic begins */
	/* Access to the SEEPROM will be disabled in we're running under vWii */
	print_all_keys(stdout, vWii);
	
	if (fp)
	{
		print_all_keys(fp, vWii);
		
		/* This will create a hexdump of the device.cert in the selected device */
		char devcert[0x200];
		memset(devcert, 42, 0x200);
		i = ES_GetDeviceCert((u8*)devcert);
		if (i)
		{
			printf("\n\nES_GetDeviceCert returned %d.\n\n", i);
		} else {
			fprintf(fp, "\nDevice cert:\n");
			hexdump(fp, devcert, 0x180);
		}
		
		fclose(fp);
		
		if (device == 1) Close_SD();
		if (device == 2) Close_USB();
	}
}
