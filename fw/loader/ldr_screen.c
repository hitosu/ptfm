/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The update loader's screen (loader.c; fw/tests/ldr_screen_test.c renders it on the PC): white on black, a title at
 * 2x, a line at 1x, a bar of the slot's blocks, DON'T SWITCH OFF while it installs; UPDATE MODE and why when the start
 * check started it. 8x16 glyphs (ldr_font.h). Drawn with lcd_fill, lcd_blit and lcd_sync (device/lcd.c's, the
 * includer's). */
#define LDR_WHITE 0xFFFFu
#define LDR_GREY 0x8410u
#define LDR_CYAN 0x57FFu                            /* (85, 255, 255) */
#define LDR_RED 0xFAAAu                             /* (255, 85, 85) */
#define BAR_X 20u
#define BAR_Y 130u
#define BAR_W 200u
#define BAR_H 14u
static uint16_t ldr_px[16 * 32];                    /* one glyph at 2x */

static void ldr_text(uint32_t y, const char *s, uint32_t k, uint16_t c)   /* a line, centred; k 1 or 2 */
{
    uint32_t n = 0, x, r, col, i, rr;
    const char *p;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (p = s; *p; p++)
        n++;
    lcd_fill(0, y, 240, 16u * k, 0);
    x = n * 8u * k < 240u ? (240u - n * 8u * k) / 2u : 0;
    for (p = s; *p && x + 8u * k <= 240u; p++, x += 8u * k) {
        const uint8_t *g = 0;
        for (i = 0; LDR_CHARS[i]; i++)
            if (LDR_CHARS[i] == *p)
                g = LDR_GLYPHS[i];
        if (!g)
            continue;                               /* (a space, or a character the loader does not draw) */
        lcd_sync();
        for (r = 0; r < 16u * k; r++) {
            rr = r / k;
            for (col = 0; col < 8u * k; col++)
                ldr_px[r * 8u * k + col] = (g[rr] >> (7u - col / k)) & 1u ? sw : 0;
        }
        lcd_blit(x, y, 8u * k, 16u * k, ldr_px);
    }
    lcd_sync();
}

static void ldr_num(char *d, uint32_t v)            /* v (0..100) and "%" */
{
    if (v >= 100u)
        *d++ = '1', *d++ = '0', *d++ = '0';
    else if (v >= 10u)
        *d++ = (char)('0' + v / 10u), *d++ = (char)('0' + v % 10u);
    else
        *d++ = (char)('0' + v);
    *d++ = '%';
    *d = 0;
}

static uint32_t bar_shown;                          /* the bar's filled pixels */
static void ldr_bar(uint32_t done, uint32_t total)
{
    uint32_t w = total ? done * (BAR_W - 4u) / total : 0;
    if (w < bar_shown) {
        lcd_fill(BAR_X + 2u, BAR_Y + 2u, BAR_W - 4u, BAR_H - 4u, 0);
        bar_shown = 0;
    }
    if (w > bar_shown)
        lcd_fill(BAR_X + 2u + bar_shown, BAR_Y + 2u, w - bar_shown, BAR_H - 4u, LDR_CYAN);
    bar_shown = w;
}

static void ldr_frame(const char *title, uint32_t bar)   /* a new screen: the title, the bar's frame or none */
{
    lcd_fill(0, 0, 240, 240, 0);
    ldr_text(40, title, 2, LDR_WHITE);
    bar_shown = 0;
    if (bar) {
        lcd_fill(BAR_X, BAR_Y, BAR_W, 1, LDR_WHITE);
        lcd_fill(BAR_X, BAR_Y + BAR_H - 1u, BAR_W, 1, LDR_WHITE);
        lcd_fill(BAR_X, BAR_Y, 1, BAR_H, LDR_WHITE);
        lcd_fill(BAR_X + BAR_W - 1u, BAR_Y, 1, BAR_H, LDR_WHITE);
    }
}

static uint32_t ldr_phase_shown, ldr_pct_shown = 101u;
static void ldr_installing(void)                    /* the install's screen, once its session starts */
{
    ldr_frame("INSTALLING", 1);
    ldr_text(176, "DON'T SWITCH OFF", 1, LDR_WHITE);
    ldr_phase_shown = 0;
    ldr_pct_shown = 101u;
}

static void ldr_connecting(void)                    /* a firmware's install: the page goes on in a moment */
{
    ldr_frame("INSTALLING", 0);
    ldr_text(100, "CONNECTING", 1, LDR_WHITE);
}

static void ldr_update_mode(uint32_t why)           /* started by the start check: update mode, and why */
{
    ldr_frame("UPDATE MODE", 0);
    ldr_text(110, "CONNECT USB AND OPEN", 1, LDR_WHITE);
    ldr_text(130, "THE INSTALL PAGE", 1, LDR_WHITE);
    ldr_text(176, why & STUB_W_OCT ? "OCT- WAS HELD" : why & STUB_W_REQ ? "PT/FM DID NOT START" :
                  "PT/FM IS NOT WHOLE", 1, LDR_GREY);
}

static void ldr_shown(uint32_t phase, uint32_t done, uint32_t total)   /* the bar and its line: CHECKING / WRITING */
{
    uint32_t pct = total ? done * 100u / total : 0;
    if (phase != ldr_phase_shown) {
        ldr_phase_shown = phase;
        ldr_pct_shown = 101u;
        ldr_bar(0, total);
    }
    if (pct != ldr_pct_shown) {
        char t[16], *d = t;
        const char *w = phase == 1u ? "CHECKING " : "WRITING ";   /* (ldr_core.c's LDR_PH_CHECK, _WRITE) */
        while (*w)
            *d++ = *w++;
        ldr_num(d, pct);
        ldr_text(100, t, 1, LDR_WHITE);
        ldr_pct_shown = pct;
    }
    ldr_bar(done, total);
}

static void ldr_ended(int rc)                       /* the session's end: rc 0 done, -14 another flash, else stopped */
{
    if (rc == 0 || rc == -14) {
        ldr_text(100, rc ? "UNKNOWN FLASH: NOT WRITTEN" : "DONE", 1, rc ? LDR_RED : LDR_WHITE);
        ldr_text(176, "STARTING", 1, LDR_WHITE);
    } else {
        ldr_text(100, "STOPPED: INSTALL AGAIN", 1, LDR_RED);   /* (the host resumes: the loader waits) */
        ldr_text(176, "DON'T SWITCH OFF", 1, LDR_WHITE);
    }
}
