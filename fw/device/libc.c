/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (cut: what the controller does not call) */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc.
 * memset and memcpy go a 32-bit word at a time where they can: the sound clears and copies its blocks
 * with them every 1.45 ms, and a byte at a time they cost ~15 % of the CPU on DJ (measured on the FM-1). */
typedef uint32_t __attribute__((__may_alias__)) libc_w32;   /* (words over any type's bytes) */

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
    uint32_t w = (unsigned char)c;
    w |= w << 8;
    w |= w << 16;
    while (n && ((uintptr_t)p & 3u)) {
        *p++ = (unsigned char)c;
        n--;
    }
    libc_w32 *q = (libc_w32 *)p;
    for (; n >= 16u; n -= 16u, q += 4) {
        q[0] = w;
        q[1] = w;
        q[2] = w;
        q[3] = w;
    }
    for (; n >= 4u; n -= 4u)
        *q++ = w;
    p = (unsigned char *)q;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (!(((uintptr_t)p ^ (uintptr_t)q) & 3u)) {   /* (the same alignment: words once aligned) */
        while (n && ((uintptr_t)p & 3u)) {
            *p++ = *q++;
            n--;
        }
        libc_w32 *pw = (libc_w32 *)p;
        const libc_w32 *qw = (const libc_w32 *)q;
        for (; n >= 16u; n -= 16u, pw += 4, qw += 4) {
            pw[0] = qw[0];
            pw[1] = qw[1];
            pw[2] = qw[2];
            pw[3] = qw[3];
        }
        for (; n >= 4u; n -= 4u)
            *pw++ = *qw++;
        p = (unsigned char *)pw;
        q = (const unsigned char *)qw;
    }
    while (n--)
        *p++ = *q++;
    return d;
}

#ifdef PLAT_C2
/* Core 2's own (plat.h): the same, in its room in RAM, for the code there (out of a call's reach of these above) */
PLAT_C2 void *c2_memset(void *d, int c, uint32_t n)
{
    unsigned char *p = d;
    uint32_t w = (unsigned char)c;
    w |= w << 8;
    w |= w << 16;
    while (n && ((uintptr_t)p & 3u)) {
        *p++ = (unsigned char)c;
        n--;
    }
    libc_w32 *q = (libc_w32 *)p;
    for (; n >= 16u; n -= 16u, q += 4) {
        q[0] = w;
        q[1] = w;
        q[2] = w;
        q[3] = w;
    }
    for (; n >= 4u; n -= 4u)
        *q++ = w;
    p = (unsigned char *)q;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

PLAT_C2 void *c2_memcpy(void *d, const void *s, uint32_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (!(((uintptr_t)p ^ (uintptr_t)q) & 3u)) {   /* (the same alignment: words once aligned) */
        while (n && ((uintptr_t)p & 3u)) {
            *p++ = *q++;
            n--;
        }
        libc_w32 *pw = (libc_w32 *)p;
        const libc_w32 *qw = (const libc_w32 *)q;
        for (; n >= 16u; n -= 16u, pw += 4, qw += 4) {
            pw[0] = qw[0];
            pw[1] = qw[1];
            pw[2] = qw[2];
            pw[3] = qw[3];
        }
        for (; n >= 4u; n -= 4u)
            *pw++ = *qw++;
        p = (unsigned char *)pw;
        q = (const unsigned char *)qw;
    }
    while (n--)
        *p++ = *q++;
    return d;
}
#endif

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

/* decimal with an optional fixed point: fmt_fix(buf, -63, 1) -> "-6.3" */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}

static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }
