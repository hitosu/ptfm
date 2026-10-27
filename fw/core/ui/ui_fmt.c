/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Numbers as the screens write them (specs/ui/frame.md#numbers): indexes and values hex, counts and
 * values with a unit decimal, constant lengths. */
#include <stdarg.h>
#include <string.h>
#include "ui_core.h"

int ui_clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
int ui_wrap(int v, int n) { return n ? ((v % n) + n) % n : 0; }

static int vfmt(char *dst, int max, const char *f, va_list ap)
{
    int n = 0;
#define PUTC(c) do { if (n < max - 1) dst[n] = (c); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUTC(*f); continue; }
        f++;
        int left = 0, zero = 0, w = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
        char tmp[16];
        const char *s = tmp;
        int len = 0;
        if (*f == 's') {
            s = va_arg(ap, const char *);
#ifdef UI_TXT_CHECK                             /* (the host tests' packed builds: a table's text number given as %s, not T()) */
            if (s && (uintptr_t)s < 65536u) __builtin_trap();
#endif
            if (!s) s = "";
            len = (int)strlen(s);
        }
        else if (*f == 'c') { tmp[0] = (char)va_arg(ap, int); len = 1; }
        else if (*f == 'd' || *f == 'u' || *f == 'x' || *f == 'X') {
            int base = *f == 'x' || *f == 'X' ? 16 : 10, neg = 0;
            uint32_t v;
            if (*f == 'd') { int x = va_arg(ap, int); neg = x < 0; v = neg ? (uint32_t)(-(int64_t)x) : (uint32_t)x; }
            else v = va_arg(ap, unsigned);
            char r[12];
            int k = 0;
            do { int d = (int)(v % (uint32_t)base); r[k++] = (char)(d < 10 ? '0' + d : (*f == 'x' ? 'a' : 'A') + d - 10); v /= (uint32_t)base; } while (v);
            if (neg) {
                if (zero) { PUTC('-'); w--; } else r[k++] = '-';
            }
            while (k) tmp[len++] = r[--k];
        } else if (*f == '%') { tmp[0] = '%'; len = 1; }
        else if (!*f) break;
        if (!left) for (int i = len; i < w; i++) PUTC(zero ? '0' : ' ');
        for (int i = 0; i < len; i++) PUTC(s[i]);
        if (left) for (int i = len; i < w; i++) PUTC(' ');
    }
    if (max > 0) dst[n < max ? n : max - 1] = 0;
#undef PUTC
    return n;
}
int ui_fmt(char *dst, int max, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vfmt(dst, max, f, ap);
    va_end(ap);
    return n;
}
void ui_toastf(const char *f, ...)
{
    char b[64];
    va_list ap;
    va_start(ap, f);
    vfmt(b, sizeof b, f, ap);
    va_end(ap);
    ui_toast(b);
}
static char rot[8][12];
static unsigned nrot;                   /* (unsigned: it wraps, never negative) */
const char *ui_hex2(int v) { char *b = rot[nrot++ % 8]; ui_fmt(b, 12, "%02X", v); return b; }
const char *ui_dec(int v) { char *b = rot[nrot++ % 8]; ui_fmt(b, 12, "%d", v); return b; }
void ui_ref_text(char *dst, uint16_t ref) { ui_fmt(dst, 8, "%02X:%02X", ref >> 8, ref & 255); }
void ui_note_name(char *dst, int m)
{
    static const char *const N[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (m < 0) { strcpy(dst, "---"); return; }
    if (m == 1) { strcpy(dst, "==="); return; }
    ui_fmt(dst, 6, "%s%d", N[m % 12], m / 12 - 1);
}
void ui_page_of(char *dst, int i, int n) { ui_fmt(dst, 12, "%d/%d", i + 1, n); }
/* N/M: the index and the last index, both hex, N padded to M's width (`00/11`) */
void ui_of(char *dst, int i, int n)
{
    char a[8], b[8];
    ui_fmt(b, sizeof b, "%X", n - 1 > 0 ? n - 1 : 0);
    ui_fmt(a, sizeof a, "%X", i);
    while (strlen(a) < strlen(b)) { memmove(a + 1, a, strlen(a) + 1); a[0] = '0'; }
    ui_fmt(dst, 16, "%s/%s", a, b);
}
void ui_fmt_time(char *dst, uint32_t s) { ui_fmt(dst, 12, "%02u:%02u", s / 60, s % 60); }
/* a count of voices (specs/ui/colours.md) against the limit in force: red at it, yellow from 4 under it, light grey */
int ui_voices_col(int n, int limit) { return n >= limit ? C_RED : n >= limit - 4 ? C_YELLOW : C_LGREY; }
