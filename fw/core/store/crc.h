/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* CRC-32 (zlib's: reflected, polynomial 0xEDB88320), as every record, disk and blob carries. */
#ifndef PTFM_CRC_H
#define PTFM_CRC_H
#include <stdint.h>

/* crc32_update(0, p, n) is the CRC of n bytes; pass a CRC back in to go on with more bytes. */
uint32_t crc32_update(uint32_t crc, const void *p, uint32_t n);

#endif
