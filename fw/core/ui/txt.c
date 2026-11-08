/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Unpacking the fixed texts (txt.h; specs/ui/texts.md#unpacking): each token through its halves with a
 * small stack, into a line buffer. */
#include <string.h>
#include "txt.h"

extern const uint8_t TXT_BITS;
extern const uint16_t TXT_BASE;
extern const char TXT_CHARS[];
extern const uint8_t TXT_PAIRS[];
extern const uint8_t TXT_DATA[];
extern const uint8_t TXT_LEN[];
extern const uint16_t TXT_START[];
#define TXT_STEP 16                     /* every 16th text's start kept (gen_texts.py's IDX_STEP) */

/* the n bits (n <= 16) from bit b: TXT_DATA and TXT_PAIRS are bit-packed, low bits first, with 2 spare bytes */
static unsigned bits_at(const uint8_t *p, uint32_t b, unsigned n)
{
    uint32_t v = (uint32_t)p[b >> 3] | (uint32_t)p[(b >> 3) + 1] << 8 | (uint32_t)p[(b >> 3) + 2] << 16;
    return v >> (b & 7) & ((1u << n) - 1u);
}

int txt_unpack(int id, char *dst, int max)
{
    int n = 0;
    if (id < 0 || id >= TXT_COUNT) { dst[0] = 0; return 0; }
    uint32_t k0 = TXT_START[id / TXT_STEP];
    for (int j = id - id % TXT_STEP; j < id; j++) k0 += TXT_LEN[j];
    for (uint32_t k = k0; k < k0 + TXT_LEN[id]; k++) {
        unsigned stack[64];
        int sp = 0;
        stack[sp++] = bits_at(TXT_DATA, k * TXT_BITS, TXT_BITS);
        while (sp) {
            unsigned t = stack[--sp];
            if (t < TXT_BASE) {
                if (n < max - 1) dst[n++] = TXT_CHARS[t];
                continue;
            }
            uint32_t h = 2u * (t - TXT_BASE) * TXT_BITS;    /* (its halves a, b) */
            if (sp + 2 > 64) break;                     /* (never: the table's depth is far less) */
            stack[sp++] = bits_at(TXT_PAIRS, h + TXT_BITS, TXT_BITS);
            stack[sp++] = bits_at(TXT_PAIRS, h, TXT_BITS);
        }
    }
    dst[n] = 0;
    return n;
}

const char *ui_txt_s(const char *s) { return s; }

/* rotating buffers: a few texts may be in use at once (one line's runs: 8 at most, with their values; the UI's RAM
 * budget leaves no room for more) */
#define NBUF 12
static char bufs[NBUF][48];                     /* (a line is 40 characters) */
static unsigned nb;                             /* (unsigned: it wraps, never negative) */
const char *ui_txt_n(int id)
{
    char *b = bufs[nb++ % NBUF];
    txt_unpack(id, b, sizeof bufs[0]);
    return b;
}
const char *ui_txt_v(const char *t, const char *v)
{
    char *b = bufs[nb++ % NBUF];
    int n = 0;
    int done = 0;
    for (; *t && n < (int)sizeof bufs[0] - 1; t++) {
        if (*t == '#' && !done) { for (const char *q = v; *q && n < (int)sizeof bufs[0] - 1; q++) b[n++] = *q; done = 1; }
        else b[n++] = *t;
    }
    b[n] = 0;
    return b;
}
