/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FX EDIT's pictures (fx_pics.h; specs/ui/screens/fx.md, The pictures), in fixed point. Each follows the
 * DSP's own law for what it shows (the playground's pictures are the reference). What takes real work (DRIVE's
 * harmonics) is kept until a knob changes. */
#include <string.h>
#include "ui_core.h"
#include "fx_pics.h"
#include "fxdef.h"
#include "fxm.h"
#include "../fx/fx.h"

#define LOG2_20 283240                  /* log2(20) Q16 */
#define LOG2_1000 653117                /* log2(1000) Q16 */
#define DB_PER_LOG2_Q8 1541             /* 20 log10(2) x 256 */

static void dotted(int x, int y0, int h, int col) { for (int y = y0; y < y0 + h; y += 2) g_px(x, y, col); }
static int curve_py;
static void curve_px(int x, int y, int col, int first)       /* a line through columns, joined */
{
    if (!first) {
        int d = curve_py - y;
        if (d > 1 || d < -1) g_vline(x, (curve_py < y ? curve_py : y) + 1, (d < 0 ? -d : d) - 1, col);
    }
    g_px(x, y, col);
    curve_py = y;
}
static int text_right(int xr, int y, const char *s, int col) { return g_text(xr - g_tw(&FONT_T, s), y, &FONT_T, s, col); }
/* acos(c), c Q30 in [-1, 1], as a phase (2^32 a turn) */
static uint32_t acos_ph(int64_t c)
{
    uint32_t lo = 0, hi = 0x80000000u;
    for (int i = 0; i < 31; i++) {
        uint32_t m = lo + (hi - lo) / 2;
        if ((int64_t)fxm_cos(m) << 15 > c) lo = m; else hi = m;
    }
    return lo;
}

/* ---------------------------------------------------------------------- GATE --- */
static void gate(const fxpic_t *c)
{
    uint8_t st[16];
    char nb[16];
    const char *name = fxd_gate_name(c->k[0], nb);
    fx_gate_steps(c->k[0], st);
    if (c->editing) {
        g_run_t r[5] = {{name, C_WHITE}, {T("  KEY"), C_YELLOW}, {T(": ON/OFF  "), C_LGREY}, {T("KEY+ALG"), C_YELLOW},
                        {T(": X2-X4"), C_LGREY}};
        g_runs(c->x, c->y, &FONT_T, r, 5);
    } else {
        g_run_t r[3] = {{name, C_LGREY}, {T("  EDIT"), C_YELLOW}, {T(": ITS STEPS"), C_DGREY}};
        g_runs(c->x, c->y, &FONT_T, r, 3);
    }
    for (int i = 0; i < 16; i++) {
        int v = st[i], sx = c->x + i * 14 + 4, sy = c->y + 16;
        if (i == c->step) g_rect(sx - 1, sy - 1, 13, 30, C_DEEP + C_BLUE);
        g_box(sx, sy, 11, 28, v ? c->col : C_DGREY);
        if (v) for (int q = 0; q < v; q++) g_rect(sx + 2, sy + 2 + q * (24 / v), 7, 24 / v - 2, c->col);
    }
}

/* -------------------------------------------------------------------- FILTER --- */
static int x_of_log(int32_t lf, int w) { return fxm_round((int64_t)(lf - LOG2_20) * (w - 1), LOG2_1000); }
static void filter(const fxpic_t *c)
{
    int x = c->x, w = c->w, cut = c->k[0], top = c->y + 2, gh = c->h - 16;
    static const int F[3] = {100, 1000, 10000};
    static const char *const FL[3] = {"100", "1K", "10K"};
    for (int i = 0; i < 3; i++) {
        int fx_ = x_of_log(fxm_log2((uint64_t)F[i] << 16) - (16 << 16), w);
        g_vline(x + fx_, top, gh, C_DEEP + C_DGREY);
        g_text(x + fx_ - 6, top + gh + 2, &FONT_T, FL[i], C_DGREY);
    }
#define YDB(dbq8) (top + fxm_round((int64_t)(18 * 256 - ui_clamp((dbq8), -24 * 256, 18 * 256)) * (gh - 1), 42 * 256))
    g_hline(x, YDB(0), w, C_DGREY);
    if (!cut) return;
    int32_t lcut = fxm_log2((uint64_t)fxd_filter_hz(cut) << 16) - fxm_log2(10u << 16);   /* log2(fc) Q16 */
    int lp = cut < 0, env = c->k[2];
    int32_t lopen = lp ? LOG2_20 + LOG2_1000 : LOG2_20;     /* fully open: 20 kHz, 20 Hz */
    int32_t lfc = lcut + (int32_t)((int64_t)(lopen - lcut) * c->open >> 16);   /* where its envelope has it now */
    int64_t q = 45875 + (int64_t)c->k[1] * 524288 / 100;    /* Q16: 0.7 + RESO x 8 */
    int64_t iq2 = ((int64_t)1 << 62) / (q * q);             /* 1 / q^2, Q30 */
    int peak = -99 * 256, xfc = x_of_log(lfc, w);
    for (int i = 0; i < w; i++) {
        int32_t lf = LOG2_20 + (int32_t)((int64_t)LOG2_1000 * i / (w - 1));
        int32_t l2r = 2 * (lf - lfc);                       /* log2(r^2) */
        int64_t v = (int64_t)fxm_exp2(-(l2r < 0 ? -l2r : l2r)) << 14;   /* r^2 or 1 / r^2, whichever is <= 1, Q30 */
        int64_t one = (int64_t)1 << 30, a = one - v, inner = (a * a >> 30) + (v * iq2 >> 30);
        int32_t lden = (fxm_log2((uint64_t)(inner > 0 ? inner : 1)) - (30 << 16)) + (l2r > 0 ? 2 * l2r : 0);
        int32_t db = (int32_t)((-(int64_t)lden * DB_PER_LOG2_Q8 / 2 + (lp ? 0 : (int64_t)l2r * DB_PER_LOG2_Q8)) / 65536);
        if (db > peak) peak = db;
        int yy = YDB(db);
        g_vline(x + i, yy + 1, top + gh - yy - 1, C_DEEP + c->col);
        g_px(x + i, yy, c->col);
    }
#undef YDB
    if (env > 0) {                                          /* where it rests, how far a note opens it */
        int xr = x_of_log(lcut + (int32_t)((int64_t)(lopen - lcut) * env / 100), w);
        dotted(x + x_of_log(lcut, w), top, gh, C_LGREY);
        dotted(x + xr, top, gh, C_YELLOW);
        if (c->hit) g_vline(x + xr, top, 6, C_WHITE);
    }
    g_vline(x + xfc, top, gh, c->col);
    if (peak > 256) {
        char s[16];
        ui_fmt(s, sizeof s, T("PEAK +%d DB"), fxm_round(peak, 256));
        int right = xfc < w / 2, tw = g_tw(&FONT_T, s), tx = right ? x + w - 48 - tw : x + 2;   /* (clear of the
                                                                     * knob page, `ALG 1/2`; on black over the lines) */
        g_rect(tx - 1, top, tw + 2, 12, C_BLACK);
        g_text(tx, top, &FONT_T, s, C_LGREY);
    }
}

/* --------------------------------------------------------------------- LO-FI --- */
static void lofi(const fxpic_t *c)
{
    int x = c->x, y = c->y, w = c->w, h = c->h;
    int steps = 1 << (c->k[0] - 1), hold = 2 * ui_clamp(fxm_round(441, 10 * (int64_t)c->k[1]), 1, 999);
    int64_t mid2 = 2 * (y + 8) + (h - 10), amp2 = (h - 10) - 4;    /* x 2: (h - 10) / 2 may be a half */
    for (int i = 0; i < w; i++) {
        int32_t s = fxm_sin((uint32_t)((uint64_t)i * 2u * 0x100000000ULL / (uint64_t)w));
        g_px(x + i, fxm_round(mid2 * 32768 - (int64_t)s * amp2, 65536), C_DEEP + c->col);
    }
    for (int i = 0; i < w; i++) {
        int t2 = (i / hold) * hold * 2 + hold;                       /* the hold's middle, x 2 */
        int32_t s = fxm_sin((uint32_t)((uint64_t)t2 * 0x100000000ULL / (uint64_t)w));
        int lvl = fxm_round((int64_t)s * steps, 32768);              /* the level, in BITS' steps */
        curve_px(x + i, fxm_round(mid2 * steps - (int64_t)lvl * amp2, 2 * (int64_t)steps), c->col, i == 0);
    }
    uint32_t seed = 12345;
    for (int i = 0; i < 400; i++) {                     /* DUST: specks from a fixed scatter */
        seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
        int sx = (int)((uint64_t)seed * (uint64_t)w / 0x7FFFFFFFu);
        seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
        int sy = (int)((uint64_t)seed * (uint64_t)(h - 12) / 0x7FFFFFFFu);
        seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
        if ((uint64_t)seed * 100u < (uint64_t)c->k[2] * 0x7FFFFFFFu) g_px(x + sx, y + 10 + sy, C_LGREY);
    }
}

/* -------------------------------------------------------------------- CHORUS --- */
static void chorus(const fxpic_t *c)
{
    static const int HZM[3] = {500, 830, 9000}, DEP[3] = {19000, 29000, 6000}, SCALE[3] = {30, 80, 160};
    int x = c->x, y = c->y, w = c->w, h = c->h, mode = ui_clamp(c->k[0], 0, 2);
    int64_t hz_m = (int64_t)HZM[mode] * (300 + 14 * c->k[1]) / 1000;        /* the rate, mHz */
    int64_t dep7 = (int64_t)DEP[mode] * (300 + 14 * c->k[2]) / 1000;        /* the depth, x 1e7 */
    int64_t wob = dep7 * hz_m * 6746518852LL / 10000000000LL;              /* dep x 2 pi x hz, Q30 */
    int scale = SCALE[mode], top = y + 13, gh = h - 26, X0 = x + 26, GW = w - 26;
#define CENTS(v30) ((int)(((int64_t)fxm_log2((uint64_t)(v30)) - (30 << 16)) * 120000 / 65536))   /* (1/100 cent: not
                                                         * whole cents, which drew the curve in steps) */
#define YM(ct) (top + fxm_round((int64_t)(scale * 100 - ui_clamp((ct), -scale * 100, scale * 100)) * (gh - 1), 200 * (int64_t)scale))
    g_hline(X0, YM(0), GW, C_DEEP + C_DGREY);
    for (int s = 1; s <= 2; s++) g_vline(X0 + fxm_round((int64_t)s * (GW - 1), 3), top, gh, C_DEEP + C_DGREY);
    char b[24];
    ui_fmt(b, sizeof b, "+%d", scale); g_text(x, top - 2, &FONT_T, b, C_DGREY);
    g_text(x + 6, YM(0) - 6, &FONT_T, "0", C_DGREY);
    ui_fmt(b, sizeof b, "-%d", scale); g_text(x, top + gh - 10, &FONT_T, b, C_DGREY);
    for (int side = 0; side < 2; side++) {              /* right light grey (1 + wob cos), left the colour (1 - wob cos) */
        for (int i = 0; i < GW; i++) {
            uint32_t ph = (uint32_t)((uint64_t)i * 3u * (uint64_t)hz_m * 0x100000000ULL / (1000u * (uint64_t)(GW - 1)));
            int64_t v = ((int64_t)1 << 30) + (side ? -1 : 1) * (wob * fxm_cos(ph) >> 15);
            curve_px(X0 + i, YM(CENTS(v)), side ? c->col : C_LGREY, i == 0);
        }
    }
    int cents = fxm_round((int64_t)(fxm_log2((uint64_t)(((int64_t)1 << 30) + wob)) - (30 << 16)) * 1200, 65536);
    char hz[12];
    fxm_fixed(hz, hz_m, hz_m < 1000 ? 2 : 1);
    ui_fmt(b, sizeof b, "%s HZ  +-%d CENTS", hz, cents);
    text_right(x + w, y, b, c->col);
    g_text(x, y, &FONT_T, T("L"), c->col);
    g_text(x + 10, y, &FONT_T, T("R"), C_LGREY);
    g_text(X0, top + gh + 1, &FONT_T, "0", C_DGREY);
    for (int sec = 1; sec <= 3; sec++) {
        ui_fmt(b, sizeof b, "%d S", sec);
        int lx = X0 + fxm_round((int64_t)sec * (GW - 1), 3), tw = g_tw(&FONT_T, b), a = x + w - tw, bb = lx - tw / 2;
        g_text(a < bb ? a : bb, top + gh + 1, &FONT_T, b, C_DGREY);
    }
#undef YM
#undef CENTS
}

/* --------------------------------------------------------------------- DELAY --- */
static const uint8_t NV24[12] = {3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48};   /* TIME's values in 1/24 beats */
static const txt_t NV_NAME[12] = {TX("1/32"), TX("1/16T"), TX("1/16"), TX("1/8T"), TX("1/16."), TX("1/8"), TX("1/4T"), TX("1/8."),
                                  TX("1/4"), TX("1/2T"), TX("1/4."), TX("1/2")};
static int64_t one_pole_kk(int tone)    /* a repeat's lowpass coefficient, Q30: 1 - (1 - (0.1 + TONE x 0.85))^1.5 */
{
    int64_t base = (int64_t)(200 + 17 * tone) * 65536 / 2000;               /* Q16 */
    int64_t rest = fxm_pow((uint64_t)(65536 - base), 98304);               /* ^1.5, Q16 */
    return ((int64_t)65536 - rest) << 14;
}
static int one_pole_hz(int64_t kk, int rate)            /* its -3 dB frequency, Hz */
{
    int64_t one = (int64_t)1 << 30, cc = one - (kk * kk >> 30) * one / (2 * (one - kk));
    if (cc <= -one) return rate / 2;
    return (int)((uint64_t)acos_ph(cc) * (uint64_t)rate >> 32);
}
static void delay(const fxpic_t *c)
{
    int x = c->x, y = c->y, w = c->w, h = c->h, bpm10 = c->bpm10 > 0 ? c->bpm10 : 1200, pp = c->k[3] == 1;
    int nv = ui_clamp(c->k[0], 0, 11);
    while (nv > 0 && (int64_t)NV24[nv] * 25 * (pp ? 2 : 1) > bpm10) nv--;       /* the longest that fits */
    int64_t kk = one_pole_kk(c->k[2]), one = (int64_t)1 << 30, a1 = one - kk;
    uint32_t ph = (uint32_t)(5000ULL * 0x100000000ULL / 29400u);
    int64_t re = one - (a1 * fxm_cos(ph) >> 15), im = a1 * fxm_sin(ph) >> 15;
    int db_h = fxm_db((uint64_t)((kk >> 15) * (kk >> 15)), (uint64_t)((re >> 15) * (re >> 15) + (im >> 15) * (im >> 15))) / 2;
    int db_mix = fxm_db((uint64_t)c->k[4], 100), db_fb = c->k[1] ? fxm_db((uint64_t)c->k[1] * 92u, 10000) : -200 * 256;
    int fc = one_pole_hz(kk, 29400);
    int top = y + 13, gh = h - 26, mid = top + gh / 2, half = gh / 2 - 1;
#define XB(b24) (x + 3 + fxm_round((int64_t)(b24) * (w - 8), 96))
#define HD(dbq8) (ui_clamp(fxm_round((int64_t)(48 * 256 + (dbq8)) * half, 48 * 256), 0, 9999))
    for (int b = 1; b <= 3; b++) g_vline(XB(24 * b), top, gh, C_DEEP + C_DGREY);
    g_hline(x, mid, w, C_DEEP + C_DGREY);
    g_rect(XB(0) - 1, mid - half, 3, 2 * half + 1, C_WHITE);
    for (int n = 1; n * NV24[nv] <= 96 && n < 80; n++) {
        int db = db_mix + (n - 1) * db_fb, tre = db + n * db_h;
        if (db < -48 * 256) break;
        int ex = XB(n * NV24[nv]) - 1, eh = HD(db), et = HD(tre), side = pp ? (n % 2 ? -1 : 1) : 0;
        if (side <= 0) { g_rect(ex, mid - eh, 3, eh, C_DEEP + c->col); g_rect(ex, mid - et, 3, et, c->col); }
        if (side >= 0) { g_rect(ex, mid + 1, 3, eh, C_DEEP + c->col); g_rect(ex, mid + 1, 3, et, c->col); }
    }
#undef HD
    /* what plays, clear of the knob page at the right: single spaces when it would not fit, then without TONE */
    char p[4][20], s[64], hz[12];
    int np = 0;
    if (nv != c->k[0]) ui_fmt(p[np++], 20, T("PLAYS %s"), T(NV_NAME[nv]));
    ui_fmt(p[np++], 20, "%d MS", fxm_round((int64_t)NV24[nv] * 25000, bpm10));
    if (c->k[1]) ui_fmt(p[np++], 20, T("%d DB EACH"), fxm_round(db_fb, 256)); else ui_fmt(p[np++], 20, "%s", T("ONCE"));
    if (fc >= 13000) strcpy(hz, T("OPEN")); else fxd_hz_short(hz, fc * 10);
    ui_fmt(p[np++], 20, T("TONE %s"), hz);
    int room = w - 50;
    for (int pass = 0; pass < 3; pass++) {
        s[0] = 0;
        for (int i = 0; i < np - (pass == 2); i++) {
            if (i) strcat(s, pass ? " " : "  ");
            strcat(s, p[i]);
        }
        if (g_tw(&FONT_T, s) <= room) break;
    }
    text_right(x + w - 48, y, s, c->col);
    if (pp) { g_text(x + w - 6, top, &FONT_T, T("L"), C_DGREY); g_text(x + w - 6, top + gh - 11, &FONT_T, T("R"), C_DGREY); }
    for (int i = 0; i < 4; i++) {
        char l[2] = {(char)('1' + i), 0};
        g_text(XB(24 * i) + (i ? -2 : 3), top + gh + 1, &FONT_T, l, C_DGREY);
    }
#undef XB
}

/* -------------------------------------------------------------------- REVERB --- */
static void fmt_secs(char *dst, int ms)
{
    char n[12];
    if (ms < 10000) fxm_fixed(n, ms, 1); else fxm_fixed(n, ms, 0);
    ui_fmt(dst, 16, "%s S", n);
}
static void reverb(const fxpic_t *c)
{
    int x = c->x, y = c->y, w = c->w, h = c->h, damp = c->k[1], pre = c->k[2];
    int rt = (int)(1300ULL * fxm_exp2((int32_t)((int64_t)c->k[0] * 201907 / 100)) >> 16);   /* 1.3 x (11 / 1.3)^SIZE, ms */
    int rt_hi = rt * (400 - 3 * damp) / 400;
    int span = rt <= 2000 ? 2 : rt <= 4000 ? 4 : rt <= 8000 ? 8 : 12, N = 12;
    int bh = ui_clamp((h - 26) / N, 2, 3), top = y + 12, gh = N * bh;
    int start = fxm_db((uint64_t)(c->k[3] < 1 ? 1 : c->k[3]), 100);       /* dB(max(.01, MIX)) */
    for (int b = 0; b < N; b++) {
        int64_t f15 = (int64_t)fxm_pow((uint64_t)b * 65536 / 11, 98304);   /* (b / 11)^1.5, Q16 */
        int64_t rtb = (int64_t)rt * (6553600 - 3 * damp * f15 / 4) / 6553600;
        if (rtb < 1) rtb = 1;
        int y0 = top + gh - (b + 1) * bh;
        for (int i = 0; i < w; i++) {
            int64_t s_ms = (int64_t)i * span * 1000 / w - pre;
            if (s_ms < 0) continue;
            int64_t db = start - b * 1536 / 10 - 60 * 256 * s_ms / rtb;     /* Q8; 0.6 dB a band */
            for (int yy = 0; yy < bh; yy++) {
                int chk = (i + yy) & 1, cc = db > -12 * 256 ? c->col : db > -24 * 256 ? (chk ? c->col : C_DEEP + c->col)
                                             : db > -40 * 256 ? C_DEEP + c->col : db > -60 * 256 ? (chk ? C_DEEP + c->col : -1) : -1;
                if (cc >= 0) g_px(x + i, y0 + yy, cc);
            }
        }
    }
    char l[16];
    for (int s = 0; s <= span; s += span <= 4 ? 1 : 2) {
        int lx = x + fxm_round((int64_t)s * (w - 1), span), xr = x + w;
        if (s == span) ui_fmt(l, sizeof l, "%d S", s); else ui_fmt(l, sizeof l, "%d", s);
        int tx = xr - g_tw(&FONT_T, l);
        g_text(lx < tx ? lx : tx, top + gh + 1, &FONT_T, l, C_DGREY);
    }
    fmt_secs(l, rt);
    g_text(x, y, &FONT_T, l, c->col);
    char t[24];
    fmt_secs(l, rt_hi);
    ui_fmt(t, sizeof t, T("TREBLE %s"), l);
    text_right(x + w, y, t, C_DGREY);
}

/* --------------------------------------------------------------------- DRIVE --- */
static struct { int k0, k1, ok; int db[8]; int fc; int gain; } dr = {.k0 = -1, .k1 = -1};
static void drive_calc(int k0, int k1)
{
    int d = k0 * 127 / 100;
    int64_t G = 65536 + (int64_t)d * d * 32;                       /* 1 + d^2 / 2048, Q16 */
    int64_t mk = (int64_t)(30000 - 120 * d) * 2;                   /* / 32768, Q16 */
    int64_t kk = 3277 + (int64_t)k1 * 58982 / 100;                 /* 0.05 + TONE x 0.9, Q16 */
    int32_t bias = 4800, b0 = fxm_tanh(bias);                      /* 2400 / 32768 */
    static int32_t out[100];
    int64_t s = 0, hp = 0;
    for (int i = -4410; i < 100; i++) {
        int32_t xin = fxm_sin((uint32_t)((int64_t)i * 42949673)) >> 1;   /* 0.25 sin(2 pi i / 100), Q16 */
        hp += ((int64_t)xin * 1 - hp) / 128;
        int64_t u = ((int64_t)xin - hp) * G / 65536 + bias;
        if (u > 131072) u = 131072;
        if (u < -131072) u = -131072;
        int64_t y = ((int64_t)fxm_tanh((int32_t)u) - b0) * mk / 65536;
        s += (y - s) * kk / 65536;
        if (i >= 0) out[i] = (int32_t)s;
    }
    for (int j = 0; j < 8; j++) {
        int hn = 2 * j + 1;
        int64_t re = 0, im = 0;
        for (int i = 0; i < 100; i++) {
            uint32_t ph = (uint32_t)((uint64_t)hn * (uint64_t)i * 0x100000000ULL / 100u);
            re += (int64_t)out[i] * fxm_cos(ph);
            im += (int64_t)out[i] * fxm_sin(ph);
        }
        re /= 50 * 32768;                                          /* |X| / 50, Q16 */
        im /= 50 * 32768;
        uint64_t mag = fxm_sqrt((uint64_t)(re * re + im * im));
        dr.db[j] = fxm_db(mag, 65536);
    }
    int64_t one = (int64_t)1 << 30, kk30 = kk << 14, cc = one - (kk30 * kk30 >> 30) * one / (2 * (one - kk30));
    dr.fc = cc <= -one ? 22050 : (int)((uint64_t)acos_ph(cc) * 44100u >> 32);
    int64_t g = G * mk / 65536 * (65536 - (int64_t)b0 * b0 / 65536) / 65536;    /* G mk (1 - b0^2), Q16 */
    dr.gain = fxm_db((uint64_t)(g > 0 ? g : 1), 65536);
    dr.k0 = k0; dr.k1 = k1; dr.ok = 1;
}
static void drive(const fxpic_t *c)
{
    int x = c->x, y = c->y, w = c->w, h = c->h;
    if (!dr.ok || dr.k0 != c->k[0] || dr.k1 != c->k[1]) drive_calc(c->k[0], c->k[1]);
    int top = y + 13, gh = h - 26, NB = 8, sw = w / NB;
    for (int j = 0; j < NB; j++) {
        int hn = 2 * j + 1, bx = x + j * sw + 2;
        int bh = ui_clamp(fxm_round((int64_t)(72 * 256 + dr.db[j]) * gh, 72 * 256), 0, gh);
        if (bh) g_rect(bx + 4, top + gh - bh, sw - 12, bh, hn == 1 ? C_WHITE : c->col);
        else g_hline(bx + 4, top + gh - 1, sw - 12, C_DEEP + C_DGREY);
        char n[4];
        ui_fmt(n, sizeof n, "%d", hn);
        g_text(fxm_round((int64_t)(2 * bx + (sw - 4)) - g_tw(&FONT_T, n), 2), top + gh + 1, &FONT_T, n, C_DGREY);
    }
    int64_t hc_m = (int64_t)dr.fc * 1000 / 441;                    /* the cutoff in harmonics, x 1000 */
    if (hc_m < 2000 * NB) dotted(fxm_round((int64_t)2 * x * 1000 + (hc_m - 1000) * sw + (int64_t)(sw - 4) * 1000, 2000), top, gh, C_LGREY);
    char g[8], t[16], lab[32];
    fxm_fixed(g, (int64_t)dr.gain * 1000 / 256, 0);
    if (dr.fc >= 15000) strcpy(t, T("OPEN"));
    else if (dr.fc >= 1000) { char k[8]; fxm_fixed(k, dr.fc, 1); ui_fmt(t, sizeof t, "%s KHZ", k); }
    else ui_fmt(t, sizeof t, "%d HZ", dr.fc);
    ui_fmt(lab, sizeof lab, T("%s%s DB  TONE %s"), dr.gain >= 0 ? "+" : "", g, t);
    g_text(x, y, &FONT_T, T("HARMONICS"), C_LGREY);
    text_right(x + w, y, lab, c->col);
}

/* ------------------------------------------------------------------- LIMITER --- */
static void limiter(const fxpic_t *c)
{
    int x = c->x, ceil = c->k[0], rel = c->k[1] < 1 ? 1 : c->k[1], top = c->y + 2, gh = c->h - 16, LW = 70, RX = x + LW + 1,
        RW = c->w - LW - 1;
#define YD(dq8) (top + gh - 1 - fxm_round((int64_t)(ui_clamp((dq8), -24 * 256, 6 * 256) + 24 * 256) * (gh - 1), 30 * 256))
    for (int i = 0; i < c->w; i += 3) g_px(x + i, YD(ceil * 256), C_LGREY);
    g_vline(x + LW, top, gh, C_DGREY);
    for (int i = 0; i < LW; i++) {
        int din = (int)((int64_t)i * 30 * 256 / (LW - 1)) - 24 * 256, yc = YD(din), yo = YD(din < ceil * 256 ? din : ceil * 256);
        if (yo > yc) g_vline(x + i, yc, yo - yc, C_DEEP + C_RED);
        g_px(x + i, yc, C_DGREY);
        g_px(x + i, yo, c->col);
    }
    /* g0 = min(1, 10^((CEILING - 6) / 20)); the gain after the peak: 1 - (1 - g0) e^(-t / RELEASE) */
    int64_t g0 = (int64_t)fxm_exp2((int32_t)((int64_t)(ceil - 6) * 65536 * 256 / DB_PER_LOG2_Q8));
    if (g0 > 65536) g0 = 65536;
    int held = ceil < -12 ? ceil : -12;
    for (int i = 0; i < RW; i++) {
        int64_t t = (int64_t)i * 1000 * 94548 / ((RW - 1) * (int64_t)rel);   /* t / RELEASE x log2(e), Q16 */
        int64_t e = (int64_t)fxm_exp2((int32_t)-t), g = 65536 - (65536 - g0) * e / 65536;
        int yo = YD(held * 256 + fxm_db((uint64_t)(g > 0 ? g : 1), 65536)), yh = YD(held * 256);
        if (yo > yh) g_vline(RX + i, yh, yo - yh, C_DEEP + C_RED);
        g_px(RX + i, yh, C_DGREY);
        g_px(RX + i, yo, c->col);
    }
#undef YD
    dotted(RX + fxm_round((int64_t)(rel < 1000 ? rel : 1000) * (RW - 1), 1000), top, gh, C_LGREY);
    g_text(x, top + gh + 1, &FONT_T, T("IN"), C_DGREY);
    text_right(x + LW, top + gh + 1, "+6", C_DGREY);
    g_text(RX + 2, top + gh + 1, &FONT_T, "0", C_DGREY);
    text_right(x + c->w, top + gh + 1, "1 S", C_DGREY);
}

/* ---------------------------------------------------------------------- DUCK --- */
static void duck(const fxpic_t *c)
{
    int x = c->x, y = c->y, w = c->w, h = c->h, src = c->k[0], depth = c->k[1], rel = c->k[2] < 1 ? 1 : c->k[2];
    int top = y + 13, gh = h - 26, ntrk = c->ntrk < 1 ? 1 : c->ntrk, n = ntrk < 8 ? 8 : ntrk, cw = w / n, bot = top + gh - 1;
    int bpm10 = c->bpm10 > 0 ? c->bpm10 : 1200, any = 0;
#define HD(dq8) (fxm_round((int64_t)ui_clamp(-(dq8), 0, 24 * 256) * (gh - 1), 24 * 256))
    for (int b = 1; b < 4; b++) g_vline(x + fxm_round((int64_t)b * (w - 1), 4), top, gh, C_DEEP + C_DGREY);
    for (int i = 0; i < n; i++) {
        int bx = x + i * cw, tx = bx + (cw - 6) / 2;
        char dig[2] = {"0123456789ABCDEF"[i & 15], 0};
        if (i == src) { g_rect(bx + 2, top + gh + 1, cw - 4, 12, C_WHITE); g_text(tx, top + gh + 1, &FONT_T, dig, C_BLACK); continue; }
        int64_t a = i < ntrk ? (int64_t)depth * c->dck[i] * 65536 / (100 * 127) : 0;    /* Q16 */
        int bh = HD(fxm_db((uint64_t)(65536 - a), 65536));
        if (a) any = 1;
        if (bh) { g_rect(bx + 3, bot - bh + 1, cw - 6, bh, C_DEEP + c->col); g_hline(bx + 3, bot - bh + 1, cw - 6, c->col); }
        g_text(tx, top + gh + 1, &FONT_T, dig, a ? c->col : C_DGREY);
    }
    g_hline(x, bot, w, C_DGREY);
    g_vline(x, top, gh, C_WHITE);
    /* the dip after one loud hit, back in a straight line over RELEASE, over a bar (4 beats) */
    int64_t span_ms = (int64_t)4 * 600000 / bpm10;
    for (int i = 0; i < w - 1; i++) {
        int64_t f = 65536 - (int64_t)(i + 1) * span_ms * 65536 / ((int64_t)(w - 1) * rel);
        if (f < 0) f = 0;
        int64_t g = 65536 - (int64_t)depth * f / 100;
        curve_px(x + 1 + i, top + HD(fxm_db((uint64_t)(g > 0 ? g : 0), 65536)), C_WHITE, i == 0);
    }
#undef HD
    if (!any) text_right(x + w, top + gh - 13, T("NO TRACK DUCKS: MIXER, DCK"), C_LGREY);
    if (c->src_name && c->src_name[0]) g_text(x, y, &FONT_T, c->src_name, C_WHITE);
    char lab[24];
    int64_t g = 65536 - (int64_t)depth * 65536 / 100;
    ui_fmt(lab, sizeof lab, T("%d DB AT MOST"), fxm_round(fxm_db((uint64_t)(g > 0 ? g : 0), 65536), 256));
    text_right(x + w, y, lab, c->col);
}

/* ----------------------------------------------------------------- METRONOME --- */
static void metronome(const fxpic_t *c)
{
    int x = c->x, n = c->k[1] < 1 ? 1 : c->k[1], top = c->y + 13, gh = c->h - 14, bw = c->w / n;
    for (int i = 0; i < n; i++) {
        int bx = x + i * bw + 1, bwd = bw - 3 < 2 ? 2 : bw - 3, bh = i ? fxm_round((int64_t)gh * 6, 10) : gh, lit = i == c->beat;
        g_rect(bx, top + gh - bh, bwd, bh, lit ? c->col : C_DEEP + c->col);
        if (!lit) g_hline(bx, top + gh - bh, bwd, c->col);
    }
    if (c->k[0] <= 29) {                                /* FOLLOW: the tempo it follows */
        char s[16];
        ui_fmt(s, sizeof s, "%d BPM", fxm_round(c->bpm10, 10));
        text_right(x + c->w - 22, c->y, s, c->col);
    }
}

/* --------------------------------------------------------------------- SENDS --- */
static void sends(const fxpic_t *c)
{
    int x = c->x, y = c->y;
    if (c->dj) {                                        /* DJ's: the decks' songs feed them */
        g_text(x, y + 4, &FONT_T, T("DJ'S OWN, SHARED BY THE DECKS"), C_LGREY);
        g_text(x, y + 18, &FONT_T, T("EACH DECK'S SONG SENDS AS IT WAS MADE"), C_DGREY);
        g_text(x, y + 30, &FONT_T, T("ITS SENDS FOLLOW THE CROSSFADER"), C_DGREY);
        g_text(x, y + 44, &FONT_T, T("AND ITS DJ FILTER"), C_DGREY);
        return;
    }
    g_text(x, y + 4, &FONT_T, T("CHORUS, DELAY AND REVERB RETURN HERE"), C_LGREY);
    g_text(x, y + 18, &FONT_T, T("ABOVE: THE DRY MIX ONLY"), C_DGREY);
    g_text(x, y + 30, &FONT_T, T("BELOW: EVERYTHING"), C_DGREY);
    g_text(x, y + 44, &FONT_T, T("MIXER, CHO DLY REV: EACH TRACK'S SEND"), C_DGREY);
}

void fx_picture(int e, const fxpic_t *c)
{
    switch (e) {
    case FE_GATE: gate(c); break;
    case FE_FILTER: filter(c); break;
    case FE_DRIVE: drive(c); break;
    case FE_LOFI: lofi(c); break;
    case FE_CHORUS: chorus(c); break;
    case FE_DELAY: delay(c); break;
    case FE_REVERB: reverb(c); break;
    case FE_LIMITER: limiter(c); break;
    case FE_DUCK: duck(c); break;
    case FE_METRONOME: metronome(c); break;
    case FE_SENDS: sends(c); break;
    default: break;
    }
}
