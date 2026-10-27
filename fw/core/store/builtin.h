/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The built-in instruments (specs/files/instruments.md#built-in-instruments): the firmware's own
 * records, numbered F0-FF, payloads byte for byte as a record in the flash would hold them. Made by
 * tools/pack.py --builtin into build/fw/gen/builtin.c. */
#ifndef BUILTIN_H
#define BUILTIN_H
#include <stdint.h>

typedef struct {
    uint8_t kind, num, sub, ver;        /* as a record's header (specs/files/store.md#a-record) */
    uint32_t len;                       /* the payload's length */
    const uint8_t *data;                /* the payload */
} builtin_rec_t;

extern const builtin_rec_t builtin_recs[];
extern const int builtin_count;

#endif
