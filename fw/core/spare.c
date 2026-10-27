/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The spare zone (spare.h): its own array, marked PLAT_SPARE (on the FM-1 the data region's last object), and the
 * room the platform has right after it, plat_spare_room() bytes. */
#include "spare.h"
#include "plat.h"

static PLAT_SPARE uint32_t spare32[(SPARE_OWN + PLAT_SPARE_EXTRA) / 4];
static uint32_t lent;                   /* the bytes lent from the top; 0 none */

uint32_t spare_size(void) { return (uint32_t)sizeof spare32 + (plat_spare_room() & ~3u); }
uint8_t *spare_zone(void) { return (uint8_t *)spare32; }

void *spare_borrow(uint32_t n)
{
    uint32_t size = spare_size();
    n = (n + 3u) & ~3u;
    if (lent || !n || n > size) return 0;     /* (one at a time) */
    lent = n;
    return (uint8_t *)spare32 + size - n;
}

void spare_return(void) { lent = 0; }
uint32_t spare_lent(void) { return lent; }
