#ifndef __VWII_SRAM_OTP_H__
#define __VWII_SRAM_OTP_H__

#define SRAM_OTP_SIZE 0x80

typedef struct {
    u32 ms_id; // 0x00000002
    u32 ca_id; // 0x00000001
    u32 ng_key_id;
    u8 ng_sig[60];
    /* locked out, seemingly */
    u8 korean_key[16];
    u8 nss_device_cert[32];
} vwii_sram_otp_t;

u16 vwii_sram_otp_read(void *dst, u16 offset, u16 size);

#endif /* __VWII_SRAM_OTP_H__ */
