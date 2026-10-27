/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The spare zone (specs/files/ram.md#the-edit-memory-and-the-spare-zone): the data region's last part, holding no
 * records, lent to what borrows from it (DJ's memory while DJ plays). One borrow at a time, from its top; a borrow
 * never refuses while it fits the zone. */
#ifndef SPARE_H
#define SPARE_H
#include <stdint.h>

#define SPARE_OWN 24576u                /* the zone's own array, 24 KB (plat_spare_room: the rest of it) */
/* n bytes at the zone's top lent to the caller; NULL when one is lent already, or n exceeds the zone.
 * spare_return gives it back (the caller makes sure nothing uses it any more). */
void *spare_borrow(uint32_t n);
void spare_return(void);
uint32_t spare_size(void);              /* the zone's bytes: its own and the platform's room after it */
uint32_t spare_lent(void);              /* the bytes lent now (STATUS' RAM MAP: DJ's), 0 none */
uint8_t *spare_zone(void);              /* its first byte */

#endif
