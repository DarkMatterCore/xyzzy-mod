/*
        mini - a Free Software replacement for the Nintendo/BroadOn IOS.
        SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#ifndef __MINI_SEEPROM_H__
#define __MINI_SEEPROM_H__

#define SEEPROM_SIZE    0x100

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

u16 seeprom_read(void *dst, u16 offset, u16 size);

#endif /* __MINI_SEEPROM_H__ */
