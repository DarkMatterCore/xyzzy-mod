/*
 * AES (Rijndael) cipher - encrypt
 *
 * Modifications to public domain implementation:
 * - support only 128-bit keys
 * - cleanup
 * - use C pre-processor to make it easier to change S table access
 *
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef __AES_H__
#define __AES_H__

int aes_128_cbc_encrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len);
int aes_128_cbc_decrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len);

#endif /* __AES_H__ */
