/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A small table of built-in records for the store's tests (the firmware's comes from tools/pack.py). */
#include "../core/store/builtin.h"

static const uint8_t b_dx7[] = { 'D', 'X', '7', 0xF0 };
static const uint8_t b_pat0[] = { 1, 2, 3 };
static const uint8_t b_pat1[] = { 4, 5 };

const builtin_rec_t builtin_recs[] = {
    { 0x01, 0xF0, 0, 1, sizeof b_dx7, b_dx7 },
    { 0x20, 0xF0, 0, 1, sizeof b_pat0, b_pat0 },
    { 0x20, 0xF0, 1, 1, sizeof b_pat1, b_pat1 },
};
const int builtin_count = 3;
