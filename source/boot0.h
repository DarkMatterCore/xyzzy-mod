#ifndef __BOOT0_H__
#define __BOOT0_H__

#define BOOT0_SIZE    0x1000

u16 boot0_read(void *dst, u16 offset, u16 size);

#endif /* __BOOT0_H__ */
