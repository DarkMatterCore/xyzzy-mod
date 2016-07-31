typedef struct
{
	u8 boot1_hash[20];
	u8 common_key[16];
	u8 ng_id[4];
	union { // first two bytes of nand_hmac overlap last two bytes of ng_priv
		struct {
			u8 ng_priv[30];
			u8 _wtf1[18];
		};
		struct {
			u8 _wtf2[28];
			u8 nand_hmac[20];
		};
	};
	u8 nand_key[16];
	u8 rng_key[16];
	u32 unk1;
	u32 unk2; // 0x00000007
} __attribute__((packed)) otp_t;

typedef struct
{
	u8 boot2version;
	u8 unknown1;
	u8 unknown2;
	u8 pad;
	u32 update_tag;
	u16 checksum;
} __attribute__((packed)) eep_ctr_t;

typedef struct
{
	union {
		struct {
			u32 ms_key_id; // 0x00000002
			u32 ca_key_id; // 0x00000001
			u8 ng_key_id[4];
			u8 ng_sig[60];
			eep_ctr_t counters[2];
			u8 fill[0x18];
			u8 korean_key[16];
		};
		u8 data[256];
	};
} __attribute__((packed)) seeprom_t;

void xyzzy_get_keys();