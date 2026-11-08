/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the red screens' drawing (fw/device/crash_screen.c): PT/FM CRASH drawn into a 240x240 frame as the
 * panel would show it, for a crash as seen on the FM-1, for the widest values each line can hold, for every write
 * limit and every state of core 2; every line checked to fit its box and to keep clear of the others; saved as PPMs
 * to look at.
 *   crash_screen_test OUTDIR   (fonts.h from fw/build.sh's build/fw/gen_dev on the include path; built with the
 *                               longest version the firmware gives, a debug build's stamp) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = y; j < y + h && j < 240; j++)
        for (uint32_t i = x; i < x + w && i < 240; i++)
            fb[j * 240 + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* (pixels byte-swapped) */
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            uint16_t p = px[j * w + i];
            if (y + j < 240 && x + i < 240)
                fb[(y + j) * 240 + x + i] = (uint16_t)((p >> 8) | (p << 8));
        }
}
#include "../device/gfx.c"

/* every line drawn: its text fits its box and keeps 8 px clear of the screen's sides, its box the screen, and it keeps
 * clear of the lines drawn before it */
static int bad, nl;
static uint32_t ly[32], lh[32];
static void cs_checked(uint32_t x, uint32_t y, uint32_t w, const gfx_font_t *f, const char *s, uint16_t c, int align)
{
    int32_t tw = text_w(f, s), left = align == 1 ? (int32_t)x + ((int32_t)w - tw) / 2 : align == 2 ? (int32_t)(x + w) - tw
                                                                                                   : (int32_t)x;
    if (tw > (int32_t)w || x + w > 240u || y + f->h > 240u || left < 8 || left + tw > 232) {
        printf("  FAIL  past its box or the 8 px margins (%d px at x %d, box %u at %u): \"%s\"\n", tw, left, w, x, s);
        bad++;
    }
    for (int i = 0; i < nl; i++)
        if (y < ly[i] + lh[i] && ly[i] < y + f->h) {
            printf("  FAIL  over the line at y %u: \"%s\" at y %u\n", ly[i], s, y);
            bad++;
        }
    if (nl < 32) { ly[nl] = y; lh[nl++] = f->h; }
    draw_text_box(x, y, w, f, s, c, align);
}
#define CS_DRAW cs_checked
#include "../device/crash_screen.c"

static const char *dir;
static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); bad++; return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void draw(const crash_info_t *x, const char *name)
{
    nl = 0;
    crash_screen(x);
    crash_tail();
    if (name) save(name);
}

int main(int argc, char **argv)
{
    crash_info_t x;
    dir = argc > 1 ? argv[1] : ".";
    /* a crash as one looked on the FM-1 (its five words), the rest made up */
    memset(&x, 0, sizeof x);
    x.vec = 1; x.pc = 0x0200A0F4; x.dbg = 0x2000; x.rets = 0x02009DC2; x.sp = 0x01C7CCF0;
    x.wr = 2; x.etm[0] = 0x0200A0C6; x.etm[1] = 0x0200A0CC; x.etm[2] = 0x0200A0E0; x.etm[3] = 0x02009E10;
    x.usp = 0x01C01114; x.ssp = 0x01C7CCF4; x.icfg = 0x108; x.up_s = 312; x.late = 4; x.c2 = CS_C2_RUNS;
    draw(&x, "crash");
    /* the widest each line can be */
    memset(&x, 0xFF, sizeof x);
    x.wr = 2; x.stack = 2; x.c2 = CS_C2_STOPPED; x.c2_why = 4; x.c2_pc_ok = 1;
    draw(&x, "crash_widest");
    for (uint32_t w = 0; w < 16u; w++) { x.wr = w; draw(&x, w == 8u ? "crash_core2_code" : 0); }
    x.wr = 0x80;
    draw(&x, 0);
    for (uint8_t s = 0; s < 4u; s++)
        for (uint8_t why = 0; why < 6u; why++) { x.c2 = s; x.c2_why = why; x.c2_pc_ok = why == 4u; draw(&x, 0); }
    x.stack = 1;
    draw(&x, "crash_main_stack");
    printf("%-60s %s\n", "the red screen: every line in its box, none over another", bad ? "FAIL" : "ok");
    return bad != 0;
}
