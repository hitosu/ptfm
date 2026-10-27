/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Drawing into the half-screen strip, and the changed rows to the panel (gfx.h). */
#include <string.h>
#include "gfx.h"
#include "txt.h"
#include "../plat.h"

/* ---- the themes (specs/ui/colours.md): 16 colours each, RGB888, on black but the last six (colour 0 the ground) ---- */
static const uint32_t THEMES[G_THEMES][16] = {
    /* DEFAULT */ {0x000000, 0x4C5CD0, 0x4AAA4A, 0x3AA8A8, 0xC84C44, 0xB054B0, 0xC87C34, 0xB4B4AC,
                   0x5C5C5C, 0x7C8CF8, 0x84E884, 0x84E8E8, 0xF87C74, 0xE884E8, 0xF0E074, 0xF4F4EC},
    /* EGA */     {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                   0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF},
    /* PASTEL */  {0x000000, 0x8890D8, 0x8CC88C, 0x8CC4C4, 0xD88C8C, 0xC890C8, 0xD8AC84, 0xC8C8C0,
                   0x6C6C6C, 0xA8B0F0, 0xB8F0B8, 0xB8F0F0, 0xF8B4AC, 0xF0B8F0, 0xF4ECAC, 0xF8F8F0},
    /* NEON */    {0x000000, 0x3A2AE0, 0x30E060, 0x00D0E0, 0xFF2A6A, 0xB030FF, 0xFF8A00, 0xC8C8E0,
                   0x545474, 0x8A6AFF, 0xA8FF30, 0x30F0FF, 0xFF4AA0, 0xFF50FF, 0xFFF030, 0xFFFFFF},
    /* SUNSET */  {0x000000, 0x2A6A80, 0x8AB04A, 0x3AA0A0, 0xE0442E, 0xA84870, 0xD0803A, 0xD8C4AC,
                   0x6A5A50, 0x50C0BC, 0xF0C048, 0x60D4CC, 0xFF7A48, 0xF08AA8, 0xFFD870, 0xFFF2E4},
    /* ICE */     {0x000000, 0x3050C0, 0x40B0A0, 0x58A8D8, 0xF07840, 0x8A60D0, 0xD89858, 0xB8C8D8,
                   0x506070, 0x88ACF8, 0x70E0D0, 0x98DCF8, 0xC4A4F8, 0xE8B4F8, 0xF8C080, 0xF0F8FF},
    /* CLEAR */   {0x000000, 0x0072B2, 0x6CC070, 0xA0A0A0, 0xD55E00, 0x8A5A84, 0xA87848, 0xC0C0C0,
                   0x606060, 0x3A92D8, 0x009E73, 0x56B4E9, 0xE69F00, 0xCC79A7, 0xF0E442, 0xFFFFFF},
    /* the one-colour themes: every role one of a few intensities of one hue */
    /* GREEN */   {0x000000, 0x124D20, 0x259E43, 0x218C3B, 0x39F267, 0x1B7331, 0x23943F, 0x2CBD50,
                   0x145725, 0x23943F, 0x30CC56, 0x2AB34C, 0x37EB63, 0x28A847, 0x34DB5D, 0x3CFF6C},
    /* AMBER */   {0x000000, 0x4D350E, 0x9E6D1E, 0x8C611A, 0xF2A72E, 0x734F16, 0x94661C, 0xBD8224,
                   0x573C10, 0x94661C, 0xCC8D26, 0xB37B22, 0xEBA22C, 0xA87420, 0xDB9729, 0xFFB030},
    /* WHITE */   {0x000000, 0x464646, 0x909090, 0x808080, 0xDCDCDC, 0x686868, 0x878787, 0xACACAC,
                   0x4F4F4F, 0x878787, 0xBABABA, 0xA2A2A2, 0xD5D5D5, 0x999999, 0xC8C8C8, 0xE8E8E8},
    /* the light themes: brightness turned round (the darkest colour the brightest role), and COMMANDER on blue */
    /* DAYLIGHT */ {0xD0D0C8, 0x5060D0, 0x2F762F, 0x227171, 0xB73A33, 0xA040A0, 0xA64B1A, 0x2C2C28,
                    0x767671, 0x3742BE, 0x175D17, 0x085A61, 0x9E221A, 0x832592, 0x5C5300, 0x000000},
    /* PAPER */   {0xFFFFFF, 0x3C78E0, 0x29953E, 0x2F7F8A, 0xD73A30, 0xB03A90, 0xB0481A, 0x1C1C1C,
                   0x999999, 0x0A54C8, 0x177837, 0x0B6E7A, 0xC8201E, 0x8040D0, 0x766A00, 0x000000},
    /* MANUAL */  {0xF4EEDC, 0x4A68B0, 0x48862F, 0x3A7070, 0xC04030, 0x9A4A8A, 0xA04A1C, 0x2A2620,
                   0x958E7F, 0x2A4AA0, 0x386B1F, 0x1A6A70, 0xB02A20, 0x7A3A8A, 0x695E00, 0x0A0806},
    /* SILVER */  {0xD4D4D0, 0x949492, 0x51514F, 0x5F5F5E, 0x0B0B0A, 0x757572, 0x595957, 0x373736,
                   0x7B7B79, 0x595957, 0x2A2A2A, 0x40403E, 0x111111, 0x484847, 0x1E1E1D, 0x000000},
    /* LCD */     {0xC8D2AA, 0x8F9978, 0x525C43, 0x5F694F, 0x141E0C, 0x737D5F, 0x5A644A, 0x3B452F,
                   0x788264, 0x5A644A, 0x303A25, 0x434D36, 0x192311, 0x4B553C, 0x252F1B, 0x0A1404},
    /* COMMANDER */ {0x0000A8, 0x000000, 0x54D454, 0xC8C854, 0xE46464, 0xC570C5, 0xE0A050, 0x00C0C0,
                     0x6464EB, 0xA0A0FF, 0x80FF80, 0xA8FFFF, 0xFF8070, 0xFF80FF, 0xFFFF54, 0xFFFFFF},
};
static const txt_t THEME_NAMES[G_THEMES] = {
    TX("DEFAULT"), TX("EGA"), TX("PASTEL"), TX("NEON"), TX("SUNSET"), TX("ICE"), TX("CLEAR"), TX("GREEN"), TX("AMBER"), TX("WHITE"),
    TX("DAYLIGHT"), TX("PAPER"), TX("MANUAL"), TX("SILVER"), TX("LCD"), TX("COMMANDER")
};

static uint16_t pal[32];
static int theme_now;

static uint16_t to565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

void g_theme(int i)
{
    if (i < 0 || i >= G_THEMES) i = 0;
    theme_now = i;
    uint32_t g0 = THEMES[i][0];
    int r0 = (int)(g0 >> 16), gg0 = (int)(g0 >> 8 & 255), b0 = (int)(g0 & 255);
    for (int k = 0; k < 16; k++) {
        uint32_t v = THEMES[i][k];
        int r = (int)(v >> 16), g = (int)(v >> 8 & 255), b = (int)(v & 255);
        pal[k] = to565(r, g, b);
        /* the deep shade: 30 % of the way from the ground (on black, each part at 30 %, rounded down) */
        pal[16 + k] = to565(r0 + (r - r0) * 3 / 10, gg0 + (g - gg0) * 3 / 10, b0 + (b - b0) * 3 / 10);
    }
    g_invalidate();
}
int g_theme_now(void) { return theme_now; }
const char *g_theme_name(int i) { return i >= 0 && i < G_THEMES ? T(THEME_NAMES[i]) : "?"; }
uint16_t g_rgb565(int c) { return pal[c & 31]; }
static int32_t luma(uint32_t v) { return 2126 * (int32_t)(v >> 16) + 7152 * (int32_t)(v >> 8 & 255) + 722 * (int32_t)(v & 255); }
int g_ink(int c)
{
    int32_t y = luma(THEMES[theme_now][c & 15]), w = luma(THEMES[theme_now][15]), b = luma(THEMES[theme_now][0]);
    return (y > w ? y - w : w - y) >= (y > b ? y - b : b - y) ? C_WHITE : C_BLACK;
}

/* ---- the strip ---- */
static uint16_t strip[G_W * G_STRIP] PLAT_POOL;
uint32_t g_ram(void) { return (uint32_t)sizeof strip; }
static int oy;                                  /* the strip's first screen row */
static int cx0, cy0, cx1 = G_W, cy1 = G_H;      /* the clip, screen coordinates */
static int ey0, ey1 = G_STRIP;                  /* the rows drawable now: the clip within the strip */

static void clip_rows(void)
{
    ey0 = cy0 > oy ? cy0 : oy;
    ey1 = cy1 < oy + G_STRIP ? cy1 : oy + G_STRIP;
}
void g_clip(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x;
    cy0 = y < 0 ? 0 : y;
    cx1 = x + w > G_W ? G_W : x + w;
    cy1 = y + h > G_H ? G_H : y + h;
    clip_rows();
}
void g_noclip(void) { cx0 = 0; cy0 = 0; cx1 = G_W; cy1 = G_H; clip_rows(); }

void g_band(int half)
{
    oy = half ? G_STRIP : 0;
    g_noclip();
}

void g_clear(int c)
{
    uint16_t v = pal[c & 31];
    for (int i = 0; i < G_W * G_STRIP; i++) strip[i] = v;
}
void g_px(int x, int y, int c)
{
    if (x >= cx0 && x < cx1 && y >= ey0 && y < ey1) strip[(y - oy) * G_W + x] = pal[c & 31];
}
void g_rect(int x, int y, int w, int h, int c)
{
    int x0 = x > cx0 ? x : cx0, x1 = x + w < cx1 ? x + w : cx1;
    int y0 = y > ey0 ? y : ey0, y1 = y + h < ey1 ? y + h : ey1;
    if (x1 <= x0) return;
    uint16_t v = pal[c & 31];
    for (int j = y0; j < y1; j++) {
        uint16_t *p = strip + (j - oy) * G_W;
        for (int i = x0; i < x1; i++) p[i] = v;
    }
}
void g_hline(int x, int y, int w, int c) { g_rect(x, y, w, 1, c); }
void g_vline(int x, int y, int h, int c) { g_rect(x, y, 1, h, c); }
void g_box(int x, int y, int w, int h, int c)
{
    g_hline(x, y, w, c); g_hline(x, y + h - 1, w, c); g_vline(x, y, h, c); g_vline(x + w - 1, y, h, c);
}
void g_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 1000; n++) {
        g_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* cos(2 pi a / 64) in Q16 */
static const int32_t COS64[64] = {
    65536, 65220, 64277, 62714, 60547, 57798, 54491, 50660, 46341, 41576, 36410, 30893, 25080, 19024,
    12785, 6424, 0, -6424, -12785, -19024, -25080, -30893, -36410, -41576, -46341, -50660, -54491, -57798,
    -60547, -62714, -64277, -65220, -65536, -65220, -64277, -62714, -60547, -57798, -54491, -50660, -46341,
    -41576, -36410, -30893, -25080, -19024, -12785, -6424, 0, 6424, 12785, 19024, 25080, 30893, 36410,
    41576, 46341, 50660, 54491, 57798, 60547, 62714, 64277, 65220
};
static int round_q16(int32_t v) { return (int)((v + 32768) >> 16); }    /* floor(v / 65536 + 0.5) */
void g_circle(int x, int y, int r, int c)
{
    for (int a = 0; a < 64; a++)
        g_px(x + round_q16(r * COS64[a]), y + round_q16(r * COS64[(a + 48) & 63]), c);
}
static int isqrt(int v)
{
    if (v <= 0) return 0;
    int r = 0, b = 1 << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}
void g_disc(int x, int y, int r, int c)
{
    for (int j = -r; j <= r; j++) {
        int w = (isqrt(4 * (r * r - j * j) + 2 * r) + 1) / 2;    /* round(sqrt(r^2 - j^2 + r / 2)) */
        g_rect(x - w, y + j, 2 * w + 1, 1, c);
    }
}

/* ---- text ---- */
static const uint16_t *glyph(const ui_font_t *f, int ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;                       /* capitals only, everywhere */
    if (ch < f->first || ch >= f->first + f->count) ch = '?';
    return f->rows + (ch - f->first) * f->gh;
}
int g_text_crop(int x, int y, const ui_font_t *f, const char *s, int c, int r0, int r1)
{
    uint16_t v = pal[c & 31];
    int k = f->k, top = 1 << (f->gw - 1);
    for (; *s; s++, x += f->w) {
        if (*s == ' ') continue;
        if (x >= cx1 || x + f->w <= cx0) continue;
        const uint16_t *g = glyph(f, (unsigned char)*s);
        for (int r = r0; r < r1; r++) {
            int py = y + (r - r0) * k;
            if (py + k <= ey0 || py >= ey1) continue;
            uint16_t bits = g[r];
            if (!bits) continue;
            for (int b = 0; b < f->gw; b++) {
                if (!(bits & (top >> b))) continue;
                int px = x + b * k;
                for (int j = 0; j < k; j++) {
                    int yy = py + j;
                    if (yy < ey0 || yy >= ey1) continue;
                    uint16_t *row = strip + (yy - oy) * G_W;
                    for (int q = 0; q < k; q++) {
                        int xx = px + q;
                        if (xx >= cx0 && xx < cx1) row[xx] = v;
                    }
                }
            }
        }
    }
    return x;
}
int g_text(int x, int y, const ui_font_t *f, const char *s, int c) { return g_text_crop(x, y, f, s, c, 0, f->gh); }
int g_textn(int x, int y, const ui_font_t *f, const char *s, int n, int c)
{
    char b[64];
    int i = 0;
    for (; i < n && i < 63 && s[i]; i++) b[i] = s[i];
    b[i] = 0;
    return g_text(x, y, f, b, c);
}
int g_tw(const ui_font_t *f, const char *s) { return (int)strlen(s) * f->w; }
int g_centre(const ui_font_t *f, const char *s, int x0, int w)
{
    int d = w - g_tw(f, s);
    return x0 + (d >= 0 ? d / 2 : -((-d + 1) / 2));            /* floor, as the playground's */
}
int g_runs_crop(int x, int y, const ui_font_t *f, const g_run_t *r, int n, int r0, int r1)
{
    for (int i = 0; i < n; i++) if (r[i].s) x = g_text_crop(x, y, f, r[i].s, r[i].c, r0, r1);
    return x;
}
int g_runs(int x, int y, const ui_font_t *f, const g_run_t *r, int n) { return g_runs_crop(x, y, f, r, n, 0, f->gh); }
int g_runs_w(const ui_font_t *f, const g_run_t *r, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) if (r[i].s) w += g_tw(f, r[i].s);
    return w;
}

/* ---- the symbols ---- */
void g_play(int x, int y, int c) { for (int i = 0; i < 9; i++) g_rect(x, y + i, 1 + (i < 8 - i ? i : 8 - i), 1, c); }
void g_pause(int x, int y, int c) { g_rect(x, y, 2, 9, c); g_rect(x + 4, y, 2, 9, c); }
void g_rec(int x, int y, int c) { g_disc(x + 3, y + 4, 3, c); }
void g_updown(int x, int y, int c)
{
    for (int i = 0; i < 3; i++) { g_hline(x + 2 - i, y + i, 1 + 2 * i, c); g_hline(x + 2 - i, y + 8 - i, 1 + 2 * i, c); }
    g_vline(x + 2, y + 3, 3, c);
}
void g_leftright(int x, int y, int c)
{
    for (int i = 0; i < 3; i++) { g_vline(x + i, y + 2 - i, 1 + 2 * i, c); g_vline(x + 8 - i, y + 2 - i, 1 + 2 * i, c); }
    g_hline(x + 3, y + 2, 3, c);
}
void g_diamond(int x, int y, int c)
{
    for (int i = 0; i < 7; i++) { int m = i < 6 - i ? i : 6 - i; g_rect(x + 3 - m, y + i + 2, 1 + 2 * m, 1, c); }
}

/* ---- to the panel: only the rows that changed ----
 * A hash for each group of 4 rows (240 B for the screen): a group that changed goes whole. */
#define G_GROUP 4
static uint32_t group_hash[G_H / G_GROUP];
static uint8_t hashes_ok;
static uint32_t rows_sent;

void g_invalidate(void) { hashes_ok = 0; }
uint32_t g_rows_sent(void) { return rows_sent; }

static uint32_t hash_rows(const uint16_t *p)
{
    uint32_t h = 2166136261u;                                   /* FNV-1a over the rows' pixels */
    for (int i = 0; i < G_W * G_GROUP; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
void g_push(void)
{
    int run = -1;
    for (int j = 0; j <= G_STRIP; j += G_GROUP) {
        int changed = 0;
        if (j < G_STRIP) {
            uint32_t h = hash_rows(strip + j * G_W);
            int g = (oy + j) / G_GROUP;
            changed = !hashes_ok || h != group_hash[g];
            group_hash[g] = h;
        }
        if (changed && run < 0) run = j;
        if (!changed && run >= 0) {
            plat_lcd_rows(oy + run, j - run, strip + run * G_W);
            rows_sent += (uint32_t)(j - run);
            run = -1;
        }
    }
    if (oy) hashes_ok = 1;                                      /* both halves known now */
}
