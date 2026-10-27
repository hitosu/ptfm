/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* CRC-32 four bits at a time: a 64-byte table in flash, no RAM; ~0.4 ms for 4 KB on the FM-1. */
#include "crc.h"

static const uint32_t crc_nib[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t crc32_update(uint32_t crc, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ crc_nib[c & 15];
        c = (c >> 4) ^ crc_nib[c & 15];
    }
    return ~c;
}
