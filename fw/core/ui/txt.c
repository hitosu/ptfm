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
extern const uint16_t TXT_INDEX[];

static unsigned token(uint32_t k)
{
    if (TXT_BITS == 8) return TXT_DATA[k];
    uint32_t b = k * 12u;
    unsigned v = (unsigned)TXT_DATA[b >> 3] | (unsigned)TXT_DATA[(b >> 3) + 1] << 8;
    return v >> (b & 7) & 0xFFFu;
}
static void halves(unsigned t, unsigned *a, unsigned *b)
{
    unsigned i = t - TXT_BASE;
    if (TXT_BITS == 8) { *a = TXT_PAIRS[2 * i]; *b = TXT_PAIRS[2 * i + 1]; return; }
    uint32_t v = (uint32_t)TXT_PAIRS[3 * i] | (uint32_t)TXT_PAIRS[3 * i + 1] << 8 | (uint32_t)TXT_PAIRS[3 * i + 2] << 16;
    *a = v & 0xFFFu;
    *b = v >> 12 & 0xFFFu;
}

int txt_unpack(int id, char *dst, int max)
{
    int n = 0;
    if (id < 0 || id >= TXT_COUNT) { dst[0] = 0; return 0; }
    for (uint32_t k = TXT_INDEX[id]; k < TXT_INDEX[id + 1]; k++) {
        unsigned stack[64];
        int sp = 0;
        stack[sp++] = token(k);
        while (sp) {
            unsigned t = stack[--sp];
            if (t < TXT_BASE) {
                if (n < max - 1) dst[n++] = TXT_CHARS[t];
                continue;
            }
            unsigned a, b;
            halves(t, &a, &b);
            if (sp + 2 > 64) break;                     /* (never: the table's depth is far less) */
            stack[sp++] = b;
            stack[sp++] = a;
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
