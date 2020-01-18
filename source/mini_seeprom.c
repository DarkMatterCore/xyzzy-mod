/*
    mini - a Free Software replacement for the Nintendo/BroadOn IOS.
    SEEPROM support

Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
Copyright (C) 2008, 2009    Haxx Enterprises <bushing@gmail.com>
Copyright (C) 2008, 2009    Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008, 2009    John Kelley <wiidev@kelley.ca>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include <ogc/machine/processor.h>
#include <unistd.h>
#include <string.h>

#include "mini_seeprom.h"

#define HW_REG_BASE     0xd800000
#define HW_GPIO1OUT     (HW_REG_BASE + 0x0e0)
#define HW_GPIO1IN      (HW_REG_BASE + 0x0e8)

#define HW_SEEPROM_BLK_SIZE 2
#define HW_SEEPROM_BLK_CNT  (SEEPROM_SIZE / HW_SEEPROM_BLK_SIZE)

#define eeprom_delay()  usleep(5)

enum {
    GP_EEP_CS = 0x000400,
    GP_EEP_CLK = 0x000800,
    GP_EEP_MOSI = 0x001000,
    GP_EEP_MISO = 0x002000
};

static void send_bits(u16 value, u8 bits)
{
    if (!bits || bits > 16) return;
    
    while(bits--)
    {
        if (value & (1 << bits))
        {
            mask32(HW_GPIO1OUT, 0, GP_EEP_MOSI);
        } else {
            mask32(HW_GPIO1OUT, GP_EEP_MOSI, 0);
        }
        
        eeprom_delay();
        
        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();
        
        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();
    }
}

static u16 recv_bits(u8 bits)
{
    if (!bits || bits > 16) return 0;
    
    int res = 0;
    
    while(bits--)
    {
        res <<= 1;
        
        mask32(HW_GPIO1OUT, 0, GP_EEP_CLK);
        eeprom_delay();
        
        mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
        eeprom_delay();
        
        res |= !!(read32(HW_GPIO1IN) & GP_EEP_MISO);
    }
    
    return res;
}

u16 seeprom_read(void *dst, u16 offset, u16 size)
{
    if (!dst || offset >= SEEPROM_SIZE || !size || (offset + size) > SEEPROM_SIZE) return 0;
    
    u16 i, cur_offset = 0;
    
    u8 *ptr = (u8*)dst;
    u8 val[HW_SEEPROM_BLK_SIZE];
    
    u8 start_addr = ((u8)offset / HW_SEEPROM_BLK_SIZE);
    u8 start_addr_offset = ((u8)offset % HW_SEEPROM_BLK_SIZE);
    
    u8 end_addr = ((u8)(offset + size) / HW_SEEPROM_BLK_SIZE);
    u8 end_addr_size = ((u8)(offset + size) % HW_SEEPROM_BLK_SIZE);
    
    if (!end_addr_size)
    {
        end_addr--;
        end_addr_size = HW_SEEPROM_BLK_SIZE;
    }
    
    if (end_addr == start_addr) end_addr_size -= start_addr_offset;
    
    mask32(HW_GPIO1OUT, GP_EEP_CLK, 0);
    mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
    eeprom_delay();
    
    for(i = start_addr; i <= end_addr; i++)
    {
        if (cur_offset >= size) break;
        
        mask32(HW_GPIO1OUT, 0, GP_EEP_CS);
        send_bits((0x600 | (offset + i)), 11);
        
        *((u16*)val) = recv_bits(16);
        
        if (start_addr == end_addr)
        {
            memcpy(ptr + cur_offset, val + start_addr_offset, end_addr_size);
            cur_offset += end_addr_size;
        } else {
            if (i == start_addr)
            {
                memcpy(ptr + cur_offset, val + start_addr_offset, HW_SEEPROM_BLK_SIZE - start_addr_offset);
                cur_offset += (HW_SEEPROM_BLK_SIZE - start_addr_offset);
            } else
            if (i == end_addr)
            {
                memcpy(ptr + cur_offset, val, end_addr_size);
                cur_offset += end_addr_size;
            } else {
                memcpy(ptr + cur_offset, val, HW_SEEPROM_BLK_SIZE);
                cur_offset += HW_SEEPROM_BLK_SIZE;
            }
        }
        
        mask32(HW_GPIO1OUT, GP_EEP_CS, 0);
        eeprom_delay();
    }
    
    return cur_offset;
}
