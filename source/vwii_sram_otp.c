#include <ogc/machine/processor.h>
#include <unistd.h>
#include <string.h>

#include "vwii_sram_otp.h"
#include "tools.h"

#define SRAM_OTP_MIRR   0xD407F00

#define HW_SRNPROT      0xD800060
#define SRAM_MASK       0x20
#define OTP_BLK_SIZE    4

u16 vwii_sram_otp_read(void *dst, u16 offset, u16 size)
{
    if (!dst || offset >= SRAM_OTP_SIZE || !size || (offset + size) > SRAM_OTP_SIZE) return 0;

    u8 *ptr = (u8*)dst;
    u8 val[OTP_BLK_SIZE] = {0};
    u16 cur_offset = 0;
    bool disable_sram_mirror = false;

    // Calculate block offsets and sizes
    u32 start_addr = (SRAM_OTP_MIRR + ALIGN_DOWN(offset, OTP_BLK_SIZE));
    u8 start_addr_offset = (offset % OTP_BLK_SIZE);

    u32 end_addr = (SRAM_OTP_MIRR + ALIGN_UP(offset + size, OTP_BLK_SIZE));
    u8 end_addr_size = ((offset + size) % OTP_BLK_SIZE);

    // Make sure the SRAM mirror is enabled (unlikely to be disabled, but let's play it safe)
    if (!(read32(HW_SRNPROT) & SRAM_MASK))
    {
        // Enable SRAM mirror
        mask32(HW_SRNPROT, 0, SRAM_MASK);
        disable_sram_mirror = true;
    }

    for(u32 addr = start_addr; addr < end_addr; addr += OTP_BLK_SIZE)
    {
        if (cur_offset >= size) break;

        // Read SRAM mirror (actually holds OTP data)
        *((u32*)val) = read32(addr);

        // Copy data to destination buffer
        if (addr == start_addr && start_addr_offset != 0)
        {
            // Handle unaligned read at start address
            memcpy(ptr + cur_offset, val + start_addr_offset, OTP_BLK_SIZE - start_addr_offset);
            cur_offset += (OTP_BLK_SIZE - start_addr_offset);
        } else
        if (addr >= (end_addr - OTP_BLK_SIZE) && end_addr_size != 0)
        {
            // Handle unaligned read at end address
            memcpy(ptr + cur_offset, val, end_addr_size);
            cur_offset += end_addr_size;
        } else {
            // Normal read
            memcpy(ptr + cur_offset, val, OTP_BLK_SIZE);
            cur_offset += OTP_BLK_SIZE;
        }
    }

    // Disable SRAM mirror, if needed
    if (disable_sram_mirror) mask32(HW_SRNPROT, SRAM_MASK, 0);

    return cur_offset;
}