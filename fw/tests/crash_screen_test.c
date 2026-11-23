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
    crash_tail("PRESS A BUTTON TO RESTART");
    if (name) save(name);
}
static void draw_freeze(const freeze_info_t *x, const char *name)
{
    nl = 0;
    freeze_screen(x);
    crash_tail("PRESS A BUTTON TO GO ON");
    if (name) save(name);
}

int main(int argc, char **argv)
{
    crash_info_t x;
    dir = argc > 1 ? argv[1] : ".";
    /* a crash as one looked on the FM-1 (its words as the screen showed them), the culprit registers and core 2's PC
     * made up */
    memset(&x, 0, sizeof x);
    x.vec = 1; x.pc = 0x0200B086; x.dbg = 0x20; x.rets = 0x0200B196; x.sp = 0x01C7F534;
    x.bus[0] = 0xEB; x.bus[5] = 0x100;
    x.etm[0] = 0x0200B086; x.etm[1] = 0x0200B054; x.etm[2] = 0x0200B02E; x.etm[3] = 0x0200AFF8;
    x.usp = 0x01C7BF74; x.ssp = 0x01C7FC00; x.psr = 4; x.icfg = 0x07010288; x.up_s = 1129; x.late = 1;
    x.c2 = CS_C2_RUNS; x.c2_pc_ok = 1; x.c2_pc = 0x01C72A40;
    draw(&x, "crash");
    /* core 2 faulted itself (its record: PC and RETS), core 1's AXI W naming it (made up) */
    x.bus[5] = 0x140; x.c2 = CS_C2_STOPPED; x.c2_why = 4; x.c2_pc = 0x01C73A10; x.c2_rets = 0x01C76E1C;
    draw(&x, "crash_core2_fault");
    x.bus[5] = 0x100; x.c2 = CS_C2_RUNS; x.c2_why = 0; x.c2_pc = 0x01C72A40; x.c2_rets = 0;
    /* CORE 2 FAULTED at a start: an earlier run's record (made up), then the widest its line can be */
    {
        c2_fault_info_t c = {1, 0x01C73A10, 0, 0x20, 0x01C76E1C, 0x01C6FF80, 181, 1, 0, 0};
        nl = 0;
        c2_fault_screen(&c);
        crash_tail("PRESS A BUTTON TO GO ON");
        save("c2_fault");
        memset(&c, 0xFF, sizeof c);
        nl = 0;
        c2_fault_screen(&c);
        crash_tail("PRESS A BUTTON TO GO ON");
        save("c2_fault_widest");
        /* a fill refused (made up), then each kind at its widest */
        c2_fault_info_t b = {0x100, 0x01C76E1C, 0, 0x20, 0, 0, 611, 1, 0x01C80040, 256};
        nl = 0;
        c2_fault_screen(&b);
        crash_tail("PRESS A BUTTON TO GO ON");
        save("c2_bad_fill");
        for (uint32_t k = 0x100; k <= 0x103u; k++) {
            memset(&b, 0xFF, sizeof b);
            b.vec = k;
            nl = 0;
            c2_fault_screen(&b);
            crash_tail("PRESS A BUTTON TO GO ON");
        }
    }
    /* a write into a window, the stack past its end */
    x.dbg = 0x2000; x.bus[0] = 2; x.bus[5] = 0; x.stack = 2;
    draw(&x, "crash_window");
    /* the widest each line can be: every window's name */
    memset(&x, 0xFF, sizeof x);
    x.stack = 2; x.c2 = CS_C2_STOPPED; x.c2_why = 4; x.c2_pc_ok = 1;
    for (uint32_t w = 0; w < 16u; w++) {
        x.bus[0] = w;
        draw(&x, w == 2u ? "crash_widest" : w == 8u ? "crash_core2_code" : 0);
    }
    x.bus[0] = 0x80;
    draw(&x, 0);
    for (uint8_t s = 0; s < 4u; s++)
        for (uint8_t why = 0; why < 6u; why++)
            for (uint8_t ok = 0; ok < 2u; ok++) { x.c2 = s; x.c2_why = why; x.c2_pc_ok = ok; draw(&x, 0); }
    x.c2 = CS_C2_RUNS;
    x.stack = 1;
    draw(&x, "crash_main_stack");
    /* PT/FM FROZE: a freeze in a flash erase, the audio going on (made up), then the widest each line can be with
     * every step's name (main.c's longest two) */
    {
        freeze_info_t f;
        memset(&f, 0, sizeof f);
        f.step = "A FLASH ERASE"; f.flash = 1; f.arg = 0x000A3000; f.pass_ms = 717123; f.halves = 1378; f.ticks = 0;
        f.in_audio = 0; f.c2 = 1; f.c2_posted = 0x0001A2B4; f.c2_done = 0x0001A2B3;
        draw_freeze(&f, "froze");
        static const char *const longest[] = {"WAITING FOR CORE 2", "THE PAGE'S ASKING", "INPUT, SCREENS"};
        for (int i = 0; i < 3; i++) {
            f.step = longest[i]; f.arg = 0xFFFFFFFF; f.pass_ms = 0xFFFFFFFF; f.halves = f.ticks = 0xFFFFFFFF;
            f.c2_posted = f.c2_done = 0xFFFFFFFF;
            f.in_audio = f.in_input = (uint8_t)(i & 1);
            draw_freeze(&f, i ? 0 : "froze_widest");
        }
        f.c2 = 0; f.flash = 0;
        draw_freeze(&f, "froze_no_core2");
        /* inside the audio interrupt at the end: a flash read there (made up), then its longest step names */
        f.c2 = 1; f.in_audio = 1; f.in_input = 0; f.step = "DRAWING"; f.halves = 3; f.ticks = 127;
        f.astep = "A FLASH READ"; f.aread = 1; f.aarg = 0x000A3000;
        draw_freeze(&f, "froze_audio");
        f.aread = 0;
        static const char *const asteps[] = {"CORE 2'S QUEUE FULL", "WAITING FOR CORE 2", "CORE 2'S LAST HALF"};
        for (int i = 0; i < 3; i++) { f.astep = asteps[i]; draw_freeze(&f, 0); }
        /* ... in the sound's work, the notes taken from their engines coming back, and a crash begun in that run (made
         * up) */
        f.astep = "THE GHOSTS"; f.crash = 1; f.crash_vec = 1; f.crash_pc = 0x0200B086;
        draw_freeze(&f, "froze_sound");
        f.crash = 0;
        /* 115081ED's freeze as the next build shows it: core 2 stopped earlier in the run (each why, the widest), a
         * Juno-106 voice's render and its part (each name), the supplies raised or not (made up) */
        f.c2 = 0; f.step = "DRAWING"; f.pass_ms = 3160725; f.halves = 4; f.ticks = 200; f.in_audio = 1;
        f.astep = "A JUNO-106 VOICE"; f.part = "FILTER 2/4"; f.part_v = 0x01C3A0B4; f.part_p = 0x01C3A0A2;
        f.c2_why = 3; f.c2_s = 1234; f.c2_pc = 0x01C78472; f.vdc14_mv = 1600; f.sysvdd_mv = 1380; f.raised = 1;
        draw_freeze(&f, "froze_juno");
        static const char *const parts[] = {"START", "OSCILLATORS", "FILTER 4/4", "VCA", "END"};
        for (int i = 0; i < 5; i++) {
            f.part = parts[i]; f.part_v = f.part_p = 0xFFFFFFFF;
            f.c2_why = (uint8_t)(1 + i % 4); f.c2_s = 99999; f.c2_pc = (uint32_t)(i & 1) * 0xFFFFFFFFu;
            draw_freeze(&f, i == 1 ? "froze_juno_widest" : 0);
        }
        f.raised = 0; f.vdc14_mv = 1400; f.sysvdd_mv = 1260; f.c2_why = 0; f.part = 0; f.in_audio = 0;
        draw_freeze(&f, "froze_not_raised");
        f.in_audio = 1; f.part = "VCA"; f.crash = 1; f.c2_why = 2; f.c2_pc = 0x01C78472;   /* (every row: the last left out) */
        draw_freeze(&f, "froze_full");
        f.crash = 0;
    }
    printf("%-60s %s\n", "the red screen: every line in its box, none over another", bad ? "FAIL" : "ok");
    return bad != 0;
}
