/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Unpacking the fixed texts (txt.h; specs/ui/texts.md#unpacking): each token's code read a bit at a time (canonical
 * Huffman: only each length's count kept), then the token through its halves with a small stack, into a line
 * buffer. */
#include <string.h>
#include "txt.h"

extern const uint8_t TXT_HBITS;
extern const uint8_t TXT_HMIN, TXT_HMAX;
extern const uint16_t TXT_HLIM[16];
extern const int16_t TXT_HOFF[16];
extern const uint8_t TXT_PAIRS[];
extern const uint8_t TXT_DATA[];
extern const uint8_t TXT_LEN[];
extern const uint16_t TXT_LSTART[];
extern const uint16_t TXT_START[];
#define TXT_STEP 16                     /* every 16th text's start kept (gen_texts.py's IDX_STEP) */
#define TXT_LBITS 3                     /* a length's bits; all ones: 8 bits more (gen_texts.py's LEN_BITS) */
#define TXT_UNIT 4                      /* a sixteen's start, in bits of 4 (gen_texts.py's START_UNIT) */

/* the n bits (n <= 16) from bit b: TXT_DATA, TXT_PAIRS and TXT_LEN are bit-packed, low bits first, with 2 spare
 * bytes */
static unsigned bits_at(const uint8_t *p, uint32_t b, unsigned n)
{
    uint32_t v = (uint32_t)p[b >> 3] | (uint32_t)p[(b >> 3) + 1] << 8 | (uint32_t)p[(b >> 3) + 2] << 16;
    return v >> (b & 7) & ((1u << n) - 1u);
}

/* the token whose code starts at bit *b (the stream from a byte's highest bit), *b past it: the next 16 bits read as a
 * number, its code's length the first whose codes end past it (canonical codes) */
static unsigned token(uint32_t *b)
{
    uint32_t p = *b, q = p >> 3;
    uint32_t v = ((uint32_t)TXT_DATA[q] << 16 | (uint32_t)TXT_DATA[q + 1] << 8 | TXT_DATA[q + 2]) << (p & 7u) >> 8 & 0xFFFFu;
    unsigned l = TXT_HMIN;
    while (l < TXT_HMAX && v >= TXT_HLIM[l - 1])
        l++;
    *b = p + l;
    return (unsigned)(TXT_HOFF[l - 1] + (int)(v >> (16u - l)));
}

int txt_unpack(int id, char *dst, int max)
{
    int n = 0;
    if (id < 0 || id >= TXT_COUNT) { dst[0] = 0; return 0; }
    uint32_t b = TXT_START[id / TXT_STEP] * (uint32_t)TXT_UNIT, lb = TXT_LSTART[id / TXT_STEP], len;
    for (int j = id - id % TXT_STEP;; j++) {            /* (the texts before it in its 16 skipped, then its own) */
        len = bits_at(TXT_LEN, lb, TXT_LBITS);
        lb += TXT_LBITS;
        if (len == (1u << TXT_LBITS) - 1u) { len = bits_at(TXT_LEN, lb, 8); lb += 8; }
        if (j == id) break;
        while (len--) token(&b);
    }
    unsigned ones = (1u << TXT_HBITS) - 1u;
    while (len--) {
        unsigned stack[64];
        int sp = 0;
        stack[sp++] = token(&b);
        while (sp) {
            uint32_t h = 2u * stack[--sp] * TXT_HBITS;  /* (its halves a, b) */
            unsigned a = bits_at(TXT_PAIRS, h, TXT_HBITS), c = bits_at(TXT_PAIRS, h + TXT_HBITS, TXT_HBITS);
            if (a == ones) {                            /* (a character: a all ones, b the character) */
                if (n < max - 1) dst[n++] = (char)c;
                continue;
            }
            if (sp + 2 > 64) break;                     /* (never: the table's depth is far less) */
            stack[sp++] = c;
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
