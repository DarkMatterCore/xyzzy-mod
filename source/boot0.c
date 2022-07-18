#include <ogc/machine/processor.h>
#include <unistd.h>
#include <string.h>

#include "boot0.h"
#include "tools.h"

#define SRAM_MIRROR     0xD400000

#define HW_SRNPROT      0xD800060
#define SRAM_MASK       0x20

#define HW_BOOT0        0xD80018C
#define BOOT0_MASK      0x1000

#define BOOT0_BLK_SIZE  4

u16 boot0_read(void *dst, u16 offset, u16 size)
{
    if (!dst || offset >= BOOT0_SIZE || !size || (offset + size) > BOOT0_SIZE) return 0;

    u8 *ptr = (u8*)dst;
    u8 val[BOOT0_BLK_SIZE] = {0};
    u16 cur_offset = 0;
    bool disable_sram_mirror = false;

    // Calculate block offsets and sizes
    u32 start_addr = (SRAM_MIRROR + ALIGN_DOWN(offset, BOOT0_BLK_SIZE));
    u8 start_addr_offset = (offset % BOOT0_BLK_SIZE);

    u32 end_addr = (SRAM_MIRROR + ALIGN_UP(offset + size, BOOT0_BLK_SIZE));
    u8 end_addr_size = ((offset + size) % BOOT0_BLK_SIZE);

    // Make sure the SRAM mirror is enabled (unlikely to be disabled, but let's play it safe)
    if (!(read32(HW_SRNPROT) & SRAM_MASK))
    {
        // Enable SRAM mirror
        mask32(HW_SRNPROT, 0, SRAM_MASK);
        disable_sram_mirror = true;
    }

    // Enable boot0
    mask32(HW_BOOT0, BOOT0_MASK, 0);

    for(u32 addr = start_addr; addr < end_addr; addr += BOOT0_BLK_SIZE)
    {
        if (cur_offset >= size) break;

        // Read SRAM mirror (actually holds boot0 data)
        *((u32*)val) = read32(addr);

        // Copy data to destination buffer
        if (addr == start_addr && start_addr_offset != 0)
        {
            // Handle unaligned read at start address
            memcpy(ptr + cur_offset, val + start_addr_offset, BOOT0_BLK_SIZE - start_addr_offset);
            cur_offset += (BOOT0_BLK_SIZE - start_addr_offset);
        } else
        if (addr >= (end_addr - BOOT0_BLK_SIZE) && end_addr_size != 0)
        {
            // Handle unaligned read at end address
            memcpy(ptr + cur_offset, val, end_addr_size);
            cur_offset += end_addr_size;
        } else {
            // Normal read
            memcpy(ptr + cur_offset, val, BOOT0_BLK_SIZE);
            cur_offset += BOOT0_BLK_SIZE;
        }
    }

    // Disable boot0
    mask32(HW_BOOT0, 0, BOOT0_MASK);

    // Disable SRAM mirror, if needed
    if (disable_sram_mirror) mask32(HW_SRNPROT, SRAM_MASK, 0);

    return cur_offset;
}
