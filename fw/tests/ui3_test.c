/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FX, VISUAL, LOOPS and DJ on the PC: all of fw/core (the real player, engines and effects) with a fake
 * platform, the demo disk laid into its flash, a script of presses, turns and waits on a fake clock; the
 * screens shot (PPM) and what they do checked: the records changed in the edit memory, what the sound gets.
 *
 *   ui3_test DISK.ptfmdsk OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "app.h"
#include "store/store.h"
#include "store/crc.h"
#include "store/seed.h"
#include "ui/ui_core.h"
#include "ui/ui_priv.h"
#include "ui/gfx.h"
#include "ui/fxdef.h"
#include "ui/ui_data.h"
#include "play/player.h"
#include "play/rec.h"
#include "play/loops.h"
#include "play/live.h"
#include "play/dj.h"
#include "fx/fx.h"
#include "ui/ui_ed.h"
#include "ui/ed_pat.h"
#include "ui/ed_inst.h"
#include "ui/fx_target.h"
#include "ui/ui_punch.h"
#include "eng/juno/juno.h"
const char *ui_toast_now(void);

void ui_render(void);

/* ------------------------------------------------------------- the fake FM-1 --- */
static uint8_t flash[1 << 20];
static uint16_t screen[240 * 240];
static uint32_t now_ms;
static plat_input_t in_now;
static uint8_t leds[LED_COUNT];
static int fails, checks;
static int64_t energy;                  /* the audio's energy since the last look */
static int64_t energy_lr[2];            /* ... each side's, since set to 0 */

uint32_t plat_ms(void) { return now_ms; }
#ifndef PTFM_DEBUG
uint32_t plat_us(void) { return now_ms * 1000u; }
#else
/* the debug build's test (specs/debug.md): the real clock, so the stages have times; the platform's own
 * figures as the device keeps them; the answers caught */
#include <time.h>
#include "dbg.h"
uint32_t plat_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
static plat_dbg_t fake_dbg;
void plat_dbg(plat_dbg_t *d)
{
    *d = fake_dbg;
    memset(&fake_dbg, 0, sizeof fake_dbg);
    d->stack_used[0] = 3000; d->stack_size[0] = 24320;
    d->stack_used[1] = 1500; d->stack_size[1] = 7936;
}
static void fake_usb(const int32_t *lr, const int16_t *click, int n)   /* USB audio's tap: some work */
{
    static volatile int32_t s;
    for (int k = 0; k < 40; k++)
        for (int i = 0; i < 2 * n; i++) s += lr[i] - (click ? click[i >> 1] : 0);
}
static uint8_t caught[4][640];          /* (FIGURES: 632 B on the wire) */
static uint32_t caught_n[4], ncaught;
static uint8_t caught2[4][80];          /* each FIGURES' 37 FIGURES 2 (70 B on the wire), in the same order */
static uint32_t caught2_n[4], ncaught2;
static int c2_forced = -1;              /* the debug build's 38 as last applied (plat_c2_force_off) */
void plat_c2_force_off(int off) { c2_forced = off; }
/* the bench's side (specs/debug.md, The bench): the clock in TIMER4's ticks, no cache to empty, THE CORE two
 * made-up rows; its records caught */
uint32_t plat_ticks(void) { return plat_us() * 24u; }
void plat_cache_cold(void) {}
int plat_bench_core(int i, dbg_bench_t *r)
{
    if (i < 0 || i > 1) return 0;
    if (!r) return 1;
    memset(r, 0, sizeof *r);
    strcpy(r->name, i ? "MEMSET 1 KB" : "LOOP: ADD, JUMP");
    r->unit = i ? DBU_BYTES : DBU_CYCLES;
    r->ops = i ? 1024 : 4096;
    r->first = i ? 70 : 1300;
    r->runs = 8;
    r->min = i ? 60 : 1100;
    r->max = r->min + 8;
    r->sum = r->min * 8 + 20;
    return 1;
}
static uint8_t bench_msg[160][320];
static uint32_t bench_len[160], nbench;
#endif
void plat_input(plat_input_t *in)
{
    *in = in_now;
    memset(in_now.knob, 0, sizeof in_now.knob);
    memset(in_now.knob_pre, 0, sizeof in_now.knob_pre);
}
uint32_t plat_notes(void) { return in_now.notes; }
void plat_led(int id, uint8_t level) { if (id >= 0 && id < LED_COUNT) leds[id] = level; }
void plat_lcd_rows(int y, int h, const uint16_t *px) { memcpy(screen + y * 240, px, (size_t)h * 240 * 2); }
int plat_screen_lost(void) { return 0; }
/* (the FM-1's RAM as its release build of 2026-10-21 lays it out: STATUS' RAM MAP) */
void plat_ram(plat_ram_t *r) { r->total = 512u * 1024u; r->ram_text = 4096u; r->variables = 49024u; r->screen = 3840u; }
static uint32_t cpu_pct, cpu_sum, cpu_halves;   /* OVER 9000's check: the share the fake FM-1 reports a half */
/* the second core as the fake FM-1 reports it (STATUS, the header, its toast): core 2's share a half and its sum,
 * its state and why it stopped; the most of each core, the late halves; SETTINGS' SECOND CORE as last applied */
static uint32_t cpu2_pct, cpu2_sum, cpu_max_us, cpu2_max_us, cpu_late;
static uint8_t c2_state = PLAT_C2_OFF, c2_why = PLAT_C2_WHY_NONE;
static int c2_enabled = -1;
void plat_cpu(plat_cpu_t *c, int reset)
{
    (void)reset;
    memset(c, 0, sizeof *c);
    c->half_us = 5804;
    c->sum_us = cpu_sum;
    c->halves = cpu_halves;
    c->max_us = cpu_max_us;
    c->late = cpu_late;
    c->c2_sum_us = cpu2_sum;
    c->c2_max_us = cpu2_max_us;
    c->c2_state = c2_state;
    c->c2_why = c2_why;
}
void plat_c2_enable(int on) { c2_enabled = on; }
void plat_output(int mono, int db) { (void)mono; (void)db; }
void plat_usb_gain(int db) { (void)db; }
static int user(uint32_t a, uint32_t n)
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END) || (a >= STORE_U2_START && a + n <= STORE_U2_END);
}
const uint8_t *plat_flash_ptr(uint32_t a) { return user(a, 1) ? flash + a : 0; }
int plat_flash_read(uint32_t a, void *d, uint32_t n) { if (a + n > sizeof flash) return -1; memcpy(d, flash + a, n); return 0; }
int plat_flash_erase(uint32_t a) { if (a & 4095 || a >= sizeof flash) return -1; memset(flash + a, 0xFF, 4096); return 0; }
int plat_flash_prog(uint32_t a, const void *s, uint32_t n)
{
    if (n > 256 || (a & ~255u) != ((a + n - 1) & ~255u)) return -1;
    for (uint32_t i = 0; i < n; i++) flash[a + i] &= ((const uint8_t *)s)[i];
    return 0;
}
uint32_t plat_fw_end(void) { return 0x50000; }
#ifndef PTFM_DEBUG
int plat_sysex_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
#else
static int sx_refuse;                   /* USB refuses the next FIGURES (as a cut or a full ring) */
int plat_sysex_send(const uint8_t *m, uint32_t n)
{
    if (n >= 6 && m[4] == 0x31 && sx_refuse) { sx_refuse = 0; return -1; }
    if (n >= 6 && m[4] == 0x31 && ncaught < 4 && n <= sizeof caught[0]) { memcpy(caught[ncaught], m, n); caught_n[ncaught++] = n; }
    if (n >= 6 && m[4] == 0x37 && ncaught2 < 4 && n <= sizeof caught2[0]) { memcpy(caught2[ncaught2], m, n); caught2_n[ncaught2++] = n; }
    if (n >= 6 && m[4] == 0x33 && nbench < 160 && n <= sizeof bench_msg[0]) { memcpy(bench_msg[nbench], m, n); bench_len[nbench++] = n; }
    return 0;
}
#endif
int plat_battery(void) { return 50; }
void plat_idle(void) {}
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t was) { (void)was; }
void plat_fault(const char *why) { fprintf(stderr, "FAULT: %s\n", why); exit(2); }

/* ------------------------------------------------ the disk laid into the flash --- */
static uint32_t at = STORE_U1_START, last_hdr;
static void put(const uint8_t *h12, const uint8_t *payload, uint32_t len, int audio)
{
    uint32_t size = 16 + len;
    if (!audio && (at & 4095) + size > 4096) at = (at + 4095) & ~4095u;
    uint8_t hd[16];
    memcpy(hd, h12, 4);
    hd[4] = (uint8_t)len; hd[5] = (uint8_t)(len >> 8); hd[6] = (uint8_t)(len >> 16);
    hd[7] = 0;
    hd[8] = 1; hd[9] = hd[10] = hd[11] = 0;
    uint32_t c = crc32_update(0, hd, 12);
    c = crc32_update(c, payload, len);
    hd[12] = (uint8_t)c; hd[13] = (uint8_t)(c >> 8); hd[14] = (uint8_t)(c >> 16); hd[15] = (uint8_t)(c >> 24);
    memcpy(flash + at, hd, 16);
    memcpy(flash + at + 16, payload, len);
    last_hdr = at;
    at = (at + size + 3) & ~3u;
}
static void lay_disk(const char *path)
{
    memset(flash, 0xFF, sizeof flash);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ui3_test: no %s\n", path); exit(1); }
    static uint8_t d[1 << 20];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 16 || memcmp(d, "PTFMDISK", 8)) { fprintf(stderr, "ui3_test: %s is no disk\n", path); exit(1); }
    uint32_t count = d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24, p;
    for (int pass = 0; pass < 2; pass++) {
        if (pass) at = (at + 4095) & ~4095u;
        p = 16;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
            int kind = d[p], audio = kind == REC_AUDIO;
            if (kind < 0xE0 && audio == pass) put(d + p, d + p + 12, len, audio);
            p = (p + 12 + len + 3) & ~3u;
        }
    }
    /* the save's last record flagged */
    flash[last_hdr + 7] |= 1;
    uint32_t len = flash[last_hdr + 4] | flash[last_hdr + 5] << 8 | flash[last_hdr + 6] << 16;
    uint32_t c = crc32_update(0, flash + last_hdr, 12);
    c = crc32_update(c, flash + last_hdr + 16, len);
    flash[last_hdr + 12] = (uint8_t)c; flash[last_hdr + 13] = (uint8_t)(c >> 8);
    flash[last_hdr + 14] = (uint8_t)(c >> 16); flash[last_hdr + 15] = (uint8_t)(c >> 24);
}

/* --------------------------------------------------------------- the script --- */
static const char *out;
static void run(uint32_t ms)
{
    static int16_t lr[PLAT_HALF * 2];
    static uint32_t audio_at;
    for (uint32_t t = 0; t < ms; t++) {
        now_ms++;
        while (audio_at * 5804u / 1000u < now_ms) {
#ifdef PTFM_DEBUG
            uint32_t t0 = plat_us();
            app_audio(lr, PLAT_HALF);
            uint32_t dt = plat_us() - t0;            /* (the device's interrupt time, as main.c keeps it) */
            fake_dbg.isr_sum_us += dt;
            if (dt > fake_dbg.isr_max_us) fake_dbg.isr_max_us = (uint16_t)(dt > 65535 ? 65535 : dt);
            fake_dbg.halves++;
#else
            app_audio(lr, PLAT_HALF);
#endif
            for (int i = 0; i < PLAT_HALF * 2; i++) {
                energy += (int64_t)lr[i] * lr[i] / 256;
                energy_lr[i & 1] += (int64_t)lr[i] * lr[i] / 256;
            }
            cpu_sum += 5804u * cpu_pct / 100u;
            cpu2_sum += 5804u * cpu2_pct / 100u;
            cpu_halves++;
            audio_at++;
        }
        app_poll();
    }
}
static void press(int b) { in_now.buttons |= 1u << b; run(30); }
static void release(int b) { in_now.buttons &= ~(1u << b); run(30); }
static void tap(int b) { press(b); release(b); run(200); }
static void key(int n, int down) { if (down) in_now.notes |= 1u << n; else in_now.notes &= ~(1u << n); run(30); }
static void turn(int k, int clicks)
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(120); }
}
static void combo(int held, int b) { press(held); tap(b); release(held); run(200); }
static void home_key(int n) { press(B_HOME); key(n, 1); key(n, 0); release(B_HOME); run(200); }
static void ppm(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", out, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint32_t c = screen[i];
        fputc((int)((c >> 11) * 255u / 31u), f);
        fputc((int)(((c >> 5) & 63u) * 255u / 63u), f);
        fputc((int)((c & 31u) * 255u / 31u), f);
    }
    fclose(f);
}
static void shot(const char *name)
{
    run(40);
    ui_render();                                        /* (a frame now: the main loop's last can be 33 ms old, and a
                                                         * playing screen moves on meanwhile) */
    ppm(name);
    static uint16_t parts[240 * 240];
    memcpy(parts, screen, sizeof parts);
    g_invalidate();
    ui_render();
    if (memcmp(parts, screen, sizeof parts)) {          /* (the whole frame beside it, to look at) */
        printf("FAIL %s: the parts sent differ from the whole frame\n", name);
        fails++;
        char nm[96];
        snprintf(nm, sizeof nm, "%s_whole", name);
        ppm(nm);
    }
}
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { printf("FAIL %s\n", what); fails++; }
}
static int screen_is(const char *name) { const ui_screen_t *s = ui_screen_now(); return s && !strcmp(T(s->name), name); }
static const uint8_t *song_fx(uint8_t song)
{
    static rec_song_t s;
    return rec_song(song, &s) ? 0 : s.fx;
}
static int64_t listen(uint32_t ms) { energy = 0; run(ms); return energy; }

/* -------------------------------------------------------------- OVER 9000 --- */
/* the header's warning (specs/ui/frame.md#the-header): a second over 90 % shows it, a second at 90 % or under
 * takes it away; 90 % itself is not over */
static void test_over9000(void)
{
    home_key(1);                                        /* SONGS */
    cpu_pct = 95;
    run(2100);
    check(ui_over9000(), "OVER 9000: a second at 95 % shows it");
    tap(B_OCTP);                                        /* (a toast meanwhile: not shown, the header is OVER 9000's) */
    shot("ui3_over9000");
    tap(B_OCTM);
    cpu_pct = 90;
    run(2100);
    check(!ui_over9000(), "OVER 9000: a second at 90 % takes it away");
    cpu_pct = 0;
    run(2100);
    cpu_pct = 99;                                       /* (a second's mean: 0.4 s of 99 % after silence is not over) */
    run(400);
    check(!ui_over9000(), "OVER 9000: not after 0.4 s");
    run(1800);
    check(ui_over9000(), "OVER 9000: 99 % for a second shows it");
    cpu_pct = 0;
    run(2100);
    check(!ui_over9000(), "OVER 9000: gone once the CPU drops");
    shot("ui3_over9000_gone");
    /* either core (each core's mean apart): core 2 alone at 95 %, core 1 at 10 % */
    c2_state = PLAT_C2_ON;
    cpu_pct = 10;
    cpu2_pct = 95;
    run(2100);
    check(ui_over9000(), "OVER 9000: core 2 alone at 95 % for a second shows it");
    cpu2_pct = 90;
    run(2100);
    check(!ui_over9000(), "OVER 9000: core 2 at 90 % (core 1 at 10 %) takes it away");
    cpu_pct = cpu2_pct = 0;
    c2_state = PLAT_C2_OFF;
    run(2100);
}

/* ------------------------------------------------------------ the second core --- */
static uint16_t px(int x, int y) { return screen[y * 240 + x]; }
static int count_px(int x0, int y0, int x1, int y1, int col)   /* pixels of a colour in x0..x1-1, y0..y1-1 */
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += px(x, y) == g_rgb565((uint8_t)col);
    return n;
}
static void frame_at_blink(int on) { while ((now_ms / 80u % 2u == 0) != on) run(1); ui_render(); }
/* the header's CPU bar (specs/ui/frame.md#the-header): two columns of 2 px in its 4 x 10 at 217, 2, core 1's at
 * x 217-218, core 2's at 219-220 only while core 2 runs (else the ground); both blink red and black at a late half */
static void test_header_cores(void)
{
    home_key(1);                                        /* SONGS */
    uint16_t ground = g_rgb565(C_DEEP + C_DGREY), green = g_rgb565(C_GREEN), yellow = g_rgb565(C_YELLOW);
    c2_state = PLAT_C2_ON;
    cpu_pct = 30;                                       /* (3 px of core 1's, green) */
    cpu2_pct = 60;                                      /* (6 px of core 2's, yellow) */
    run(300);
    shot("ui3_header_cores");
    check(px(217, 11) == green && px(218, 9) == green && px(217, 8) == ground && px(218, 2) == ground &&
          px(219, 11) == yellow && px(220, 6) == yellow && px(220, 5) == ground && px(219, 2) == ground,
          "the header's CPU bar: core 1's column at 217-218 (30 %: 3 px green), core 2's at 219-220 (60 %: 6 px yellow)");
    static const uint8_t NOT_RUNNING[2] = {PLAT_C2_STARTING, PLAT_C2_OFF};
    int bare = 1;
    for (int i = 0; i < 2; i++) {
        c2_state = NOT_RUNNING[i];
        run(300);
        for (int y = 2; y < 12; y++) bare &= px(219, y) == ground && px(220, y) == ground;
        bare &= px(217, 11) == green && px(218, 9) == green;
    }
    shot("ui3_header_core2_off");
    check(bare, "core 2 not running (starting, switched off): its column the ground only, core 1's as before");
    c2_state = PLAT_C2_ON;
    run(300);
    cpu_late++;
    run(40);                                            /* (a frame sees the late half) */
    frame_at_blink(1);
    uint16_t red = g_rgb565(C_RED), black = g_rgb565(C_BLACK);
    int lit = px(217, 11) == red && px(218, 9) == red && px(219, 11) == red && px(220, 6) == red;
    frame_at_blink(0);
    int dark = px(217, 11) == black && px(220, 6) == black;
    check(lit && dark, "a late half: both columns blink red and black");
    run(500);
    ui_render();
    check(px(217, 11) == green && px(219, 11) == yellow, "... for 400 ms, then each in its load's colour again");
    cpu_pct = cpu2_pct = 0;
    c2_state = PLAT_C2_OFF;
    run(300);
}
/* core 2 stopped (a fallback, specs/second-core.md#start-switch-fallback): the toast SECOND CORE STOPPED once, on any
 * screen; its column the ground from then on */
static void test_second_core_toast(void)
{
    home_key(1);                                        /* SONGS */
    c2_state = PLAT_C2_ON;
    cpu2_pct = 40;
    run(2100);
    check(!ui_toast_now(), "core 2 running: no toast");
    c2_state = PLAT_C2_STOPPED;
    c2_why = PLAT_C2_WHY_MISREAD;
    cpu2_pct = 0;
    run(50);
    check(ui_toast_now() && !strcmp(ui_toast_now(), "SECOND CORE STOPPED"), "core 2 stopped: the toast SECOND CORE STOPPED");
    shot("ui3_second_core_stopped");
    check(count_px(219, 2, 221, 12, C_DEEP + C_DGREY) == 20, "... its column the ground only");
    run(2100);
    check(!ui_toast_now(), "... for 2 s");
    home_key(17);                                       /* (another screen, still stopped) */
    run(1000);
    check(!ui_toast_now(), "... once: not again while core 2 stays stopped");
    c2_state = PLAT_C2_ON;
    run(300);
    c2_state = PLAT_C2_STOPPED;
    run(300);
    check(!ui_toast_now(), "... once a start: not again");
    c2_state = PLAT_C2_OFF;
    c2_why = PLAT_C2_WHY_NONE;
    home_key(1);
}
/* STATUS with core 2 running, switched off and stopped (specs/ui/screens/status.md): the first page's CPU 2 (`OFF` dark
 * grey while not running), PERFORMANCE's rows NOW / AVERAGE / MOST with core 2's graph marks (white), DEVICE's SECOND
 * CORE line (`STOPPED: ...` red); shots of each, and of the three pages in a light theme */
static void test_status_second_core(void)
{
    static const struct { uint8_t state, why; const char *name; } C2[3] = {
        {PLAT_C2_OFF, PLAT_C2_WHY_NONE, "off"}, {PLAT_C2_ON, PLAT_C2_WHY_NONE, "on"},
        {PLAT_C2_STOPPED, PLAT_C2_WHY_MISREAD, "stopped"}};
    home_key(17);                                       /* STATUS */
    cpu_pct = 30;
    cpu_max_us = 2400;                                  /* (41 %) */
    cpu2_max_us = 3200;                                 /* (55 %: yellow) */
    for (int i = 0; i < 3; i++) {
        char nm[96];
        int on = C2[i].state == PLAT_C2_ON;
        c2_state = C2[i].state;
        c2_why = C2[i].why;
        cpu2_pct = on ? 45 : 0;
        turn(K_SELECT, -6);                             /* PT/FM */
        run(1100);                                      /* (the graph's samples: each half second) */
        snprintf(nm, sizeof nm, "ui3_status_c2_%s", C2[i].name);
        shot(nm);
        /* the right column's first row (126, 125): `CPU 2 `, then the share and MAX, or `OFF` (dark grey) alone */
        int grey = count_px(162, 125, 240, 137, C_DGREY), col = count_px(162, 125, 240, 137, C_GREEN) +
                   count_px(162, 125, 240, 137, C_YELLOW);
        snprintf(nm, sizeof nm, "STATUS' first page, core 2 %s: CPU 2 %s", C2[i].name, on ? "and its MAX" : "OFF, dark grey");
        check(on ? col > 0 && !grey : grey > 0 && !col && count_px(0, 125, 120, 137, C_GREEN) > 0, nm);
        turn(K_SELECT, 1);                              /* PERFORMANCE */
        run(600);
        snprintf(nm, sizeof nm, "ui3_status_c2_%s_performance", C2[i].name);
        shot(nm);
        /* core 2's row (y 43): its three figures in the CPU colours, or `OFF` under NOW (to x 96) and the rest blank */
        grey = count_px(60, 43, 97, 55, C_DGREY);
        col = count_px(60, 43, 240, 55, C_GREEN) + count_px(60, 43, 240, 55, C_YELLOW);
        int marks = count_px(1, 126, 239, 224, C_WHITE);
        snprintf(nm, sizeof nm, "PERFORMANCE, core 2 %s: %s", C2[i].name, on ? "its row's figures, its graph marks" :
                 i ? "OFF under NOW, the rest blank" : "OFF under NOW, the rest blank, no graph marks");
        check(on ? col > 0 && !grey && marks > 0 : grey > 0 && !col && (i || !marks) &&
              count_px(60, 31, 240, 43, C_GREEN) > 0, nm);
        turn(K_SELECT, 4);                              /* DEVICE */
        snprintf(nm, sizeof nm, "ui3_status_c2_%s_device", C2[i].name);
        shot(nm);
        /* SECOND CORE, the 9th line (y 131): its state light grey; stopped, why in red */
        int red = count_px(60, 131, 240, 143, C_RED), lgrey = count_px(60, 131, 240, 143, C_LGREY);
        snprintf(nm, sizeof nm, "DEVICE, core 2 %s: SECOND CORE's line %s", C2[i].name, i == 2 ? "red" : "light grey");
        check(i == 2 ? red > 0 && !lgrey : lgrey > 0 && !red, nm);
    }
    int keep = ui_set[S_THEME];
    ui_set[S_THEME] = 10;                               /* DAYLIGHT */
    ui_settings_apply(S_THEME);
    c2_state = PLAT_C2_ON;
    c2_why = PLAT_C2_WHY_NONE;
    cpu2_pct = 45;
    turn(K_SELECT, -6);
    run(600);
    shot("ui3_status_c2_daylight");
    turn(K_SELECT, 1);
    shot("ui3_status_c2_daylight_performance");
    turn(K_SELECT, 4);
    shot("ui3_status_c2_daylight_device");
    ui_set[S_THEME] = keep;
    ui_settings_apply(S_THEME);
    turn(K_SELECT, -6);
    cpu_pct = cpu2_pct = 0;
    cpu_max_us = cpu2_max_us = 0;
    c2_state = PLAT_C2_OFF;
    run(200);
}
/* SETTINGS' VOICES (specs/ui/screens/settings.md#voices): 8, 12, 16, 16 by default; 16 counts as 12 while core 2 does
 * not take the halves (this platform's never do: the limit in force 12); picked while core 2 does not run, the toast
 * `16 VOICES: 12 WITHOUT CPU 2`, else none; the record holds 8 / 12 / 16 */
static void test_settings_voices(void)
{
    player_state_t st;
    home_key(20);                                       /* SETTINGS */
    turn(K_SELECT, -30); turn(K_SELECT, 3);             /* OUTPUT, LINE GAIN, USB GAIN, VOICES */
    check(UI_SETTINGS[S_VOICES].n == 3 && !strcmp(T(UI_SETTINGS[S_VOICES].list[2]), "16") && UI_SETTINGS[S_VOICES].def == 2 &&
          ui_set[S_VOICES] == 2, "VOICES: 8, 12, 16; 16 by default and at power-on");
    run(100);
    ui_player_state(&st);
    check(st.voice_count == 12, "VOICES 16 with core 2 not taking the halves: 12 in force (STATUS' limit)");
    uint8_t c2s = c2_state;
    c2_state = PLAT_C2_STARTING;
    turn(K_K1, -1);
    check(ui_set[S_VOICES] == 1 && !ui_toast_now(), "VOICES 12: no toast");
    turn(K_K1, 1);
    check(ui_set[S_VOICES] == 2 && ui_toast_now() && !strcmp(ui_toast_now(), "16 VOICES: 12 WITHOUT CPU 2"),
          "VOICES 16 while core 2 does not run (STARTING): the toast 16 VOICES: 12 WITHOUT CPU 2");
    shot("ui3_settings_voices_16");
    run(2500);
    c2_state = PLAT_C2_ON;
    turn(K_K1, -1); turn(K_K1, 1);
    check(ui_set[S_VOICES] == 2 && !ui_toast_now(), "VOICES 16 with core 2 running: no toast");
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(p && p[1] == 16, "the settings record's VOICES: 16");
    turn(K_K1, -2);
    p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(ui_set[S_VOICES] == 0 && p && p[1] == 8, "VOICES 8: 8 in the record");
    tap(B_LFO);                                         /* (back to the default) */
    check(ui_set[S_VOICES] == 2, "LFO: VOICES back to 16");
    ui_edit_revert(REC_SETTINGS, 0, 0);
    ui_settings_reload();
    c2_state = c2s;
    run(2500);
}

/* SEL + OCT on FX's EDIT (specs/ui/screens/fx.md, controls.md): the keys' octave, the picked effect left as it is; OCT
 * alone still switches it; SEL's let-go opens no menu after the combination; SEL held shows `OCT: THE OCTAVE` */
static void test_fx_sel_oct(void)
{
    home_key(8);                                        /* PLAYBACK, then FX */
    tap(B_FX);
    tap(B_SEQ);                                         /* EDIT */
    check(screen_is("FX") && !strcmp(ui_screen_now()->sub(), "EDIT"), "FX EDIT");
    uint8_t song = UI_KEYS_SONG;
    int oct = ui_cur.octave, on = fxd_on(song_fx(song), FE_GATE);
    press(B_SEL);
    tap(B_OCTP);
    release(B_SEL);
    run(300);
    check(ui_cur.octave == (oct < 4 ? oct + 1 : 4) && fxd_on(song_fx(song), FE_GATE) == on && !ui_top(),
          "FX EDIT: SEL + OCT+ the octave up, the effect as it was, no menu");
    check(ui_toast_now() && !strncmp(ui_toast_now(), "OCT ", 4), "... with its toast (OCT +1)");
    press(B_SEL);
    tap(B_OCTM);
    release(B_SEL);
    run(300);
    check(ui_cur.octave == oct && fxd_on(song_fx(song), FE_GATE) == on, "SEL + OCT-: the octave back");
    press(B_SEL);
    run(1500);                                          /* (held past the hold delay: its layer) */
    shot("ui3_fx_edit_sel_layer");
    release(B_SEL);
    run(300);
    uint32_t ons = 0, off = 0, onn = 0;               /* (the effects on: as found, after OCT-, after OCT+) */
    for (int e = 0; e < FE_N; e++) ons |= (uint32_t)(fxd_on(song_fx(song), e) != 0) << e;
    check(!ui_top(), "SEL let go after its layer: nothing open");
    tap(B_OCTM);
    for (int e = 0; e < FE_N; e++) off |= (uint32_t)(fxd_on(song_fx(song), e) != 0) << e;
    tap(B_OCTP);
    for (int e = 0; e < FE_N; e++) onn |= (uint32_t)(fxd_on(song_fx(song), e) != 0) << e;
    check(off != onn && (off | onn) == onn && ui_cur.octave == oct,
          "OCT- / OCT+ alone on EDIT: the picked effect off, on; the octave as it was");
    if (onn != ons) tap(B_OCTM);                        /* (it was off) */
    uint32_t now = 0;
    for (int e = 0; e < FE_N; e++) now |= (uint32_t)(fxd_on(song_fx(song), e) != 0) << e;
    check(now == ons, "(the effects as they were)");
    tap(B_SEQ); tap(B_SEQ);                             /* LIVE */
    tap(B_FX);
}

/* SETTINGS' SECOND CORE (specs/ui/screens/settings.md), after VOICES: ON, OFF; applied to the platform at once; kept in
 * the settings record's byte 123, bit 1 set for OFF (specs/files/settings.md), bit 0 still the current instrument's */
static void test_settings_second_core(void)
{
    check(c2_enabled == 1, "SECOND CORE: ON at power-on (no record says otherwise), given to the platform");
    home_key(20);                                       /* SETTINGS */
    turn(K_SELECT, -30); turn(K_SELECT, 4);             /* OUTPUT, LINE GAIN, USB GAIN, VOICES, SECOND CORE */
    uint8_t cur[8] = {0};
    ui_screen_now()->cursor(cur);
    check(cur[1] == S_SECOND_CORE && S_SECOND_CORE == S_VOICES + 1 && !strcmp(T(UI_SETTINGS[S_SECOND_CORE].name), "SECOND CORE") &&
          UI_SETTINGS[S_SECOND_CORE].n == 2 && !strcmp(T(UI_SETTINGS[S_SECOND_CORE].list[0]), "ON") &&
          !strcmp(T(UI_SETTINGS[S_SECOND_CORE].list[1]), "OFF") && ui_set[S_SECOND_CORE] == 0,
          "SETTINGS: SECOND CORE after VOICES, its values ON, OFF; ON");
    uint32_t unsaved0 = ui_unsaved;
    turn(K_K1, 1);
    shot("ui3_settings_second_core_off");
    check(ui_set[S_SECOND_CORE] == 1 && c2_enabled == 0 && ui_unsaved == unsaved0 + 1,
          "SECOND CORE OFF: given to the platform at once, unsaved");
    uint32_t len;
    uint8_t ver, rec[128];
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(p && len == 133 && (p[123] & 3) == 3 && !(p[123] & 0xFC),
          "... the settings record's byte 123: bit 1 (SECOND CORE OFF), bit 0 (the current instrument kept)");
    check(!unsaved0, "(nothing else unsaved before: SAVE writes the settings alone)");
    tap(B_SAVE); run(1500);
    store_mount();
    ui_settings_load();
    for (int k = 0; k < S_COUNT; k++) ui_settings_apply(k);   /* (as power-on: read, then applied) */
    p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(!ui_unsaved && p && (p[123] & 2) && ui_set[S_SECOND_CORE] == 1 && c2_enabled == 0,
          "... saved, then read again from the flash: OFF, given to the platform");
    memcpy(rec, p, sizeof rec);
    rec[123] = 1;                                       /* a record from before SECOND CORE: 126 B, bit 1 clear */
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 126);
    ui_settings_reload();
    check(ui_set[S_SECOND_CORE] == 0 && c2_enabled == 1, "a record from before (126 B, byte 123 bit 1 clear): SECOND CORE ON");
    rec[121] = 0x01; rec[122] = 0x03; rec[123] = 2;     /* bit 1 alone: OFF, the current instrument not kept */
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 128);
    ui_settings_reload();
    check(ui_set[S_SECOND_CORE] == 1 && ui_settings_inst() == UI_FIRST_INST, "byte 123 = 2: OFF, the current instrument F0:00");
    rec[123] = 3;
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 128);
    ui_settings_reload();
    check(ui_set[S_SECOND_CORE] == 1 && ui_settings_inst() == 0x0103, "byte 123 = 3: OFF, the current instrument 01:03");
    ui_edit_revert(REC_SETTINGS, 0, 0);                 /* (the flash's again: OFF) */
    turn(K_K1, -1);                                     /* ON, saved again: the flash as it was */
    tap(B_SAVE); run(1500);
    ui_settings_reload();
    check(ui_set[S_SECOND_CORE] == 0 && c2_enabled == 1 && !ui_unsaved, "SECOND CORE ON again, saved");
    tap(B_HOME); run(200);
}

/* STATUS -> STORAGE (specs/ui/screens/status.md#storage): the user area alone, its bar green; no FWS bar (brown,
 * where row 47 had it) and no IN FWS line */
static void test_status_storage(void)
{
    home_key(17);                                       /* STATUS */
    turn(K_SELECT, -5); turn(K_SELECT, 2);              /* PT/FM, PERFORMANCE, STORAGE */
    shot("ui3_status_storage");
    int brown = 0, green = 0;
    for (int y = 47; y < 57; y++)
        for (int x = 61; x < 179; x++) brown += screen[y * 240 + x] == g_rgb565(C_BROWN);
    for (int y = 34; y < 42; y++)
        for (int x = 61; x < 179; x++) green += screen[y * 240 + x] == g_rgb565(C_GREEN);
    check(screen_is("STATUS") && green > 0 && !brown, "STATUS -> STORAGE: USER's bar alone (no FWS bar)");
    turn(K_SELECT, -5);
}

/* STATUS' USER FLASH MAP and RAM MAP (specs/ui/screens/status.md): a 4 x 4 square a KB, 48 a line from (0, 33) */
static uint16_t map_px(int i) { return screen[(33 + (i / 48) * 5 + 1) * 240 + (i % 48) * 5 + 1]; }
/* a knob's clicks and a press in one main-loop pass, in the order they came (plat_input_t.knob_pre): SELECT
 * turned, then SEL pressed, is a step and SEL's tap (its menu); SEL pressed, then SELECT turned, is SEL + SELECT
 * (on SONGS a step, no menu). Before, a pass took its presses first: a quick turn then tap was the combination */
static int menu_open(void) { return ui_top() && ui_top()->kind == OV_MENU; }
static void test_knob_then_press(void)
{
    home_key(1);                                        /* SONGS */
    turn(K_SELECT, -40);
    run(200);
    uint8_t s0 = ui_cur.song;
    in_now.buttons |= 1u << B_SEL;                      /* one pass: SEL down, then the click */
    in_now.knob[K_SELECT] += 1;
    run(30);
    release(B_SEL);
    run(300);
    check(!menu_open() && ui_cur.song != s0, "SEL pressed, then SELECT clicked, in one pass: SEL + SELECT, no menu");
    if (menu_open()) tap(B_HOME);
    uint8_t s1 = ui_cur.song;
    in_now.knob[K_SELECT] += 1;                         /* one pass: the click, then SEL down */
    in_now.knob_pre[K_SELECT] += 1;
    in_now.buttons |= 1u << B_SEL;
    run(30);
    release(B_SEL);
    run(300);
    check(menu_open() && ui_cur.song != s1, "SELECT clicked, then SEL pressed, in one pass: a step, then SEL's tap (its menu)");
    if (menu_open()) tap(B_HOME);
    turn(K_SELECT, -40);                                /* (the first song again) */
    run(200);
}
/* EDIT on STATUS (specs/ui/screens/status.md#controls): SETTINGS from every page, as HOME + MONO: a plain switch */
/* PERFORMANCE counts every engine's voices, CHIP's too (specs/ui/screens/status.md): its figure on the engines' third
 * row (left, y 107) dark grey with none, in CHIP's colour with two keys held on a CHIP instrument; EFFECTS beside it */
static void test_status_chip(void)
{
    uint16_t was = ui_cur.inst;
    ui_set_inst(0xF900);                                /* (CHIPTUNE F9:00, built in) */
    home_key(17);                                       /* STATUS */
    turn(K_SELECT, -6); turn(K_SELECT, 1);              /* PERFORMANCE */
    run(300);
    int grey0 = count_px(0, 107, 117, 119, C_DGREY), lit0 = count_px(0, 107, 117, 119, ENG_COL[ENG_CHIP]);
    key(0, 1); key(4, 1);
    run(300);
    shot("ui3_status_performance_chip");
    int grey = count_px(0, 107, 117, 119, C_DGREY), lit = count_px(0, 107, 117, 119, ENG_COL[ENG_CHIP]),
        fx = count_px(124, 107, 240, 119, C_LGREY);
    key(0, 0); key(4, 0);
    run(1000);
    tap(B_HOME);
    ui_set_inst(was);
    check(grey0 > 0 && !lit0 && lit > 0 && !grey && fx > 0,
          "PERFORMANCE: CHIP's figure dark grey with no voices, in its colour with two keys on a CHIP instrument; "
          "EFFECTS beside it");
}
static void test_status_edit(void)
{
    home_key(17);                                       /* STATUS */
    run(50);
    check(screen_is("STATUS") && leds[B_EDIT] == LV_DIM, "STATUS: EDIT lit dim (it works here)");
    int all = 1;
    for (int p = 0; p < 6; p++) {
        home_key(17);
        turn(K_SELECT, -6); turn(K_SELECT, p);
        tap(B_EDIT);
        if (!screen_is("SETTINGS") || ui_is_deep()) all = 0;
    }
    check(all, "STATUS: EDIT on each of its 6 pages: SETTINGS, not deep");
    tap(B_HOME);
    check(screen_is("SETTINGS"), "... HOME there does not come back to STATUS");
    tap(B_HOME);
}
static void test_status_maps(void)
{
    static uint8_t kb[512];
    home_key(17);                                       /* STATUS */
    turn(K_SELECT, -6); turn(K_SELECT, 3);              /* PT/FM, PERFORMANCE, STORAGE, USER FLASH MAP */
    shot("ui3_status_flash_map");
    check(screen_is("STATUS") && !strcmp(ui_screen_now()->sub(), "USER FLASH MAP"), "STATUS -> USER FLASH MAP");
    int n = store_kb_map(kb, 512), nfree = 0, kept = 0, wrong = 0;
    for (int i = 0; i < n; i++) nfree += kb[i] == STORE_KB_FREE;
    static const uint8_t COL[8] = {C_DEEP + C_LGREY, C_DEEP + C_LGREY, C_YELLOW, C_LBLUE, C_CYAN, C_GREEN, C_LGREEN, C_LGREY};
    for (int i = 0, seen = 0; i < n; i++) {
        int col = COL[kb[i]];
        if (kb[i] == STORE_KB_FREE && ++seen > nfree - 40) { col = C_MAGENTA; kept++; }   /* (the last 40 free KB) */
        if (map_px(i) != g_rgb565((uint8_t)col)) wrong++;
    }
    check(n == 412 && !wrong && kept == (nfree < 40 ? nfree : 40) && map_px(n) == g_rgb565(C_BLACK),
          "USER FLASH MAP: 412 squares, each the colour of what its KB holds, the last 40 free ones KEPT FOR SAVING");
    turn(K_SELECT, 1);
    shot("ui3_status_ram_map");
    check(!strcmp(ui_screen_now()->sub(), "RAM MAP"), "STATUS -> RAM MAP");
    ui_ram_t r;
    ui_ram(&r);
    uint32_t sum = r.effects + r.edits + r.screen + r.dx7 + r.sound + r.loops + r.variables + r.spare + r.system;
    int lit = 0;
    for (int i = 0; i < 512; i++) lit += map_px(i) != g_rgb565(C_BLACK);
    check(r.total == 512u * 1024 && sum == r.total && r.edits == 68608u && r.edits_used == ui_edit_used() &&
          r.dj <= r.spare && r.system > 20u * 1024, "RAM MAP: its parts add up to 512K, EDITS 67K and its use");
    check(lit == 512 && map_px(512) == g_rgb565(C_BLACK), "RAM MAP: 512 squares");
    int eff = 0;
    for (int i = 0; i < 512; i++) eff += map_px(i) == g_rgb565(C_GREEN);
    check(eff == (int)((r.effects + 512) / 1024), "RAM MAP: EFFECTS' squares, a KB each");
    /* every theme: each square stands out from the ground (shots of both pages, theme by theme) */
    int keep = ui_set[S_THEME], hidden = 0;
    for (int t = 0; t < G_THEMES; t++) {
        char nm[48];
        ui_set[S_THEME] = t;
        ui_settings_apply(S_THEME);
        for (int pg = 0; pg < 2; pg++) {
            turn(K_SELECT, -6); turn(K_SELECT, 3 + pg);
            snprintf(nm, sizeof nm, "ui3_maps_%02d_%s", t, pg ? "ram" : "flash");
            shot(nm);
            for (int i = 0; i < (pg ? 512 : n); i++) hidden += map_px(i) == g_rgb565(C_BLACK);
        }
    }
    ui_set[S_THEME] = keep;
    ui_settings_apply(S_THEME);
    check(!hidden, "USER FLASH MAP, RAM MAP: on every theme each square stands out from the ground");
    turn(K_SELECT, -6);
}

/* --------------------------------------------------------------------- FX --- */
/* the keys as a synth at power-on, nothing played yet (player_cue): the keys' song held whatever songs there are (not
 * the current song), its tempo the clock's, its FX setup what the keys play through; another song picked changes
 * nothing; FX edits the keys' song, an FX knob turned heard at once (it had waited for a first PLAY) */
static void test_cue(void)
{
    player_state_t st;
    ui_song_t s, cs;
    uint8_t now[64];
    uint8_t song = UI_KEYS_SONG, cur = ui_cur.song;
    run(300);
    player_state(&st, 0);
    if (!ui_song(song, &s) || !ui_song(cur, &cs)) { check(0, "cue: the keys' song and a current song"); return; }
    int was_edited = store_edit_has(REC_SONG, song, 0), b = s.tempo * 240 / (s.ticks * s.rpb);
    check(st.what == PL_NONE && !st.playing && st.song == song && cur != song,
          "cue: nothing played, the keys' song held (not the current song)");
    check(st.bpm10 == b, "cue: its tempo the clock's (the keys' ARP, GATE, DELAY, FILTER's envelope)");
    const uint8_t *sf = song_fx(song);
    fx_now(now);
    check(sf && now[7] == sf[7] && now[8] == sf[8] && !memcmp(now + 16, sf + 16, 48), "cue: its FX setup what the keys play through");
    uint8_t nums[4];
    if (ui_songs(nums, 4) > 1) {                        /* another song picked: the keys' song still held */
        ui_cur.song = nums[1] == cur ? nums[0] : nums[1];
        run(300);
        player_state(&st, 0);
        fx_now(now);
        check(st.what == PL_NONE && st.song == song && st.bpm10 == b && !memcmp(now + 16, sf + 16, 48),
              "cue: another song picked: nothing changes, the keys' song held");
        ui_cur.song = cur;
        run(200);
    }
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* G#3: FILTER */
    turn(K_K1, 3);
    run(50);
    fx_now(now);
    sf = song_fx(song);
    check(sf && now[16 + 4] == sf[16 + 4] && (was_edited || store_edit_has(REC_SONG, song, 0)),
          "cue: FILTER's CUT turned before anything played, heard at once");
    key(UI_BLACK[0], 1); key(UI_BLACK[0], 0);           /* (GATE picked again, as the FX page starts) */
    tap(B_GLO);                                         /* FX PROPERTIES FOR THE KEYS (nothing played) */
    run(100);
    shot("fx_props_song");
    tap(B_GLO);
    home_key(8);
    if (!was_edited) ui_edit_revert(REC_SONG, song, 0);
    run(200);
}
/* LOOPS shown with nothing playing (player_cue_loops): the keys through the loops' FX setup at the loops' tempo (before
 * the first loop too), LOOPS' TEMPO and an FX knob turned from LOOPS heard at once; LOOPS left, the keys' song held
 * again; a song paused before LOOPS is paused where it was after it, and PLAY goes on with it there */
static int loops_fx_now(void)
{
    rec_song_t ls;
    uint8_t now[64];
    fx_now(now);
    return !loops_song(&ls) && now[7] == ls.fx[7] && now[8] == ls.fx[8] && !memcmp(now + 16, ls.fx + 16, 48);
}
static void test_loops_cue(void)
{
    player_state_t st;
    ui_song_t s;
    uint8_t now[64];
    uint8_t song = UI_KEYS_SONG;                        /* (nothing played yet: the keys' song held) */
    int was_edited = store_edit_has(REC_LOOPS, 0, 0), loops_b = loops_tempo() * 240 / (loops_ticks() * loops_rpb());
    if (!ui_song(song, &s)) { check(0, "loops cue: the keys' song"); return; }
    int song_b = s.tempo * 240 / (s.ticks * s.rpb);
    home_key(UI_BLACK[9]);                              /* LOOPS */
    run(300);
    loops_b = loops_tempo() * 240 / (loops_ticks() * loops_rpb());   /* (LOOPS read now) */
    player_state(&st, 0);
    check(screen_is("LOOPS") && st.what == PL_LOOPS && st.lcue && !st.playing,
          "loops cue: LOOPS shown, nothing playing: the loops held for the keys");
    check(st.bpm10 == loops_b, "loops cue: the loops' tempo the clock's (the keys' song's after LOOPS: below)");
    check(loops_fx_now(), "loops cue: the loops' FX setup what the keys play through");
    tap(B_GLO);                                         /* LOOPS PROPERTIES: TEMPO first */
    turn(K_K1, 7);
    tap(B_GLO);
    run(100);
    player_state(&st, 0);
    loops_b = loops_tempo() * 240 / (loops_ticks() * loops_rpb());
    check(st.lcue && st.bpm10 == loops_b, "loops cue: LOOPS' TEMPO +7, heard at once");
    tap(B_FX);                                          /* FX from LOOPS: the loops' setup */
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* G#3: FILTER */
    turn(K_K1, 3);
    run(50);
    player_state(&st, 0);
    check(screen_is("FX") && st.lcue && loops_fx_now(), "loops cue: FX from LOOPS: FILTER's CUT turned, heard at once");
    key(UI_BLACK[0], 1); key(UI_BLACK[0], 0);           /* (GATE picked again, as the FX page starts) */
    home_key(8);                                        /* PLAYBACK */
    run(300);
    player_state(&st, 0);
    const uint8_t *sf = song_fx(song);
    fx_now(now);
    check(st.what == PL_NONE && !st.lcue && st.song == song && st.bpm10 == song_b && sf && now[7] == sf[7] &&
          !memcmp(now + 16, sf + 16, 48), "loops cue: LOOPS left: the keys' song held again, its setup and tempo");
    if (!was_edited) ui_edit_revert(REC_LOOPS, 0, 0);
    run(200);
}
/* (last: a song played) a song paused, LOOPS, back: the loops held meanwhile, the song paused where it was after; PLAY
 * goes on with it there */
static void test_loops_cue_paused(void)
{
    player_state_t st;
    home_key(8);                                        /* PLAYBACK */
    ui_play_song(ui_cur.song, 0);
    run(1500);
    tap(B_PLAY);
    run(100);
    player_state(&st, 0);
    uint8_t pos = st.pos, row = st.row;
    check(st.what == PL_SONG && !st.playing, "loops cue: a song played, paused");
    home_key(UI_BLACK[9]);                              /* LOOPS */
    run(300);
    int loops_b = loops_tempo() * 240 / (loops_ticks() * loops_rpb());
    player_state(&st, 0);
    check(st.what == PL_LOOPS && st.lcue && st.bpm10 == loops_b && loops_fx_now(), "loops cue: LOOPS: the loops held over the paused song");
    home_key(8);
    run(300);
    player_state(&st, 0);
    check(st.what == PL_SONG && !st.lcue && !st.playing && st.pos == pos && st.row == row,
          "loops cue: LOOPS left: the song paused where it was");
    tap(B_PLAY);
    run(60);
    player_state(&st, 0);
    check(st.what == PL_SONG && st.playing && st.pos == pos && st.row >= row && st.row <= row + 2,
          "loops cue: PLAY: the song goes on from there");
    tap(B_PLAY);
    run(100);
}
/* the FM-1 as a synth only (no disk: no song, no pattern, the built-in instruments): the keys' song held (FE, the
 * firmware's own, never listed: TEMPO 120, a new song's FX setup with the REVERB on), the FX page edits it, the keys hear it at once and
 * SAVE would keep it (an edit of it), FX without its MIXER (no tracks); a key sounds; every screen a black key opens
 * with HOME opens (the sanitizers watching) */
static void test_keys_only(void)
{
    player_state_t st;
    uint8_t now[64], def[64], nums[256];
    run(500);
    player_state(&st, 0);
    check(st.what == PL_NONE && st.song == UI_KEYS_SONG && st.bpm10 == 1200 && ui_songs(nums, 256) == 0,
          "keys only: no song listed, the keys' song held, at 120 BPM");
    fxd_default(def);
    fxd_set_on(def, FE_REVERB, 1);                      /* (THE KEYS: a new song's setup with the REVERB on) */
    fx_now(now);
    check(now[7] == def[7] && now[8] == def[8] && !memcmp(now + 16, def + 16, 48),
          "keys only: a new song's FX setup, the REVERB on");
    key(12, 1);                                         /* (F4, on the screen it starts on: a note) */
    int64_t e = listen(300);
    key(12, 0);
    run(300);
    check(e > 0, "keys only: a key sounds");
    tap(B_FX);
    check(screen_is("FX"), "keys only: FX opens");
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* G#3: FILTER */
    if (!(now[7] >> FE_FILTER & 1)) tap(B_OCTP);        /* (on) */
    turn(K_K1, -5);
    run(50);
    fx_now(now);
    check((now[7] >> FE_FILTER & 1) && fxd_get(now, FE_FILTER, 0) == fxd_get(def, FE_FILTER, 0) - 5,
          "keys only: FILTER on and its CUT turned, heard at once");
    check(store_edit_has(REC_SONG, UI_KEYS_SONG, 0) && ui_songs(nums, 256) == 0,
          "keys only: the keys' song changed as an edit (SAVE keeps it), still not listed");
    tap(B_SEQ);
    tap(B_SEQ);
    check(!strcmp(ui_screen_now()->sub(), "MIXER"), "keys only: FX's pages LIVE, EDIT and MIXER (the KEYS column alone)");
    shot("ui3_keys_only_mixer");
    tap(B_SEQ);
    check(!strcmp(ui_screen_now()->sub(), "LIVE"), "keys only: SEQ round to LIVE");
    tap(B_GLO);                                         /* FX PROPERTIES FOR THE KEYS: TEMPO first */
    run(100);
    shot("keys_fx_props");
    turn(K_K1, 10);
    run(200);
    shot("keys_fx_props_130");
    player_state(&st, 0);
    check(st.bpm10 == 1300, "keys only: FX's GLO, the keys' TEMPO +10, heard at once (130 BPM)");
    tap(B_GLO);
    for (int i = 0; i < 11; i++) { home_key(UI_BLACK[i]); run(300); }
    home_key(UI_BLACK[0]);
    run(300);
    check(1, "keys only: every screen opened");
    shot("keys_only");
    /* LOOPS: the loops held instead of the keys' song; left, the keys' song again */
    home_key(UI_BLACK[9]);
    run(300);
    player_state(&st, 0);
    check(st.what == PL_LOOPS && st.lcue && loops_fx_now(), "keys only: LOOPS: the loops held for the keys");
    home_key(UI_BLACK[0]);                              /* SONGS */
    run(300);
    player_state(&st, 0);
    check(st.what == PL_NONE && !st.lcue && st.song == UI_KEYS_SONG && st.bpm10 == 1300, "keys only: LOOPS left: the keys' song again");
    /* SONGS' NEW: NEW SONG STARTS WITH, only + NEW PATTERN (no pattern): the song made, its one position a new empty
     * pattern of 8 tracks x 64 rows */
    int f = ui_free_pattern();
    tap(B_SEL);
    turn(K_SELECT, -8);
    check(ui_top() && ui_top()->kind == OV_MENU && ui_overlay_words(ui_top()) && !strcmp(ui_overlay_words(ui_top()), "NEW"),
          "keys only: SONGS' menu, NEW");
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_LIST, "keys only: NEW: the pattern list (NEW SONG STARTS WITH)");
    turn(K_SELECT, -8);                                 /* (+ NEW PATTERN, on top: no pattern on a bare firmware) */
    shot("keys_new_song_list");
    tap(B_SEL);
    ui_song_t s;
    int n = ui_songs(nums, 256);
    check(!ui_top() && n == 1 && ui_song(nums[0], &s) && s.npos == 1 && s.order[0] == f && ui_pattern_tracks((uint8_t)f) == 8 &&
          ui_pattern_rows((uint8_t)f) == 64, "keys only: + NEW PATTERN: a song, its one position a new empty 8 x 64 pattern");
    shot("keys_new_song");
}
static void test_fx(void)
{
    home_key(8);                                        /* PLAYBACK, then FX */
    tap(B_FX);
    check(screen_is("FX"), "FX opens on FX");
    shot("23_fx_live");
    tap(B_SEQ); shot("23b_fx_edit");
    for (int i = 0; i < 12; i++) {                      /* each item's picture (tests/playground/screens.js the same) */
        char nm[32];
        snprintf(nm, sizeof nm, "23c_fx_pic_%02d", i);
        turn(K_SELECT, 1);
        shot(nm);
    }
    turn(K_SELECT, -12);
    tap(B_SEQ); shot("24_mixer");
    tap(B_SEQ);                                         /* LIVE again */

    /* (nothing played yet: FX edits THE KEYS' setup, the song's only once it plays, below) */
    uint8_t song = UI_KEYS_SONG;
    /* LIVE: GATE picked; K1 its PATTERN, into the setup in the edit memory */
    int before = song_fx(song)[16];
    turn(K_K1, 1);
    check(song_fx(song)[16] == (uint8_t)(before + 1), "FX LIVE: K1 turns GATE's PATTERN in the song's setup");
    tap(B_OCTP);
    check(fxd_on(song_fx(song), FE_GATE), "FX LIVE: OCT+ switches GATE on");
    tap(B_OCTM);
    check(!fxd_on(song_fx(song), FE_GATE), "FX LIVE: OCT- switches it off");
    /* a black key picks an effect: G#3 FILTER (black key 1) */
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);
    turn(K_K1, -5);
    check(fxd_get(song_fx(song), FE_FILTER, 0) == -5, "FX LIVE: the black key picked FILTER, K1 its CUT (LP)");
    shot("ui3_fx_live_filter");
    /* playing: FX on the playing song's setup; a knob turned reaches the sound at once */
    tap(B_PLAY);
    run(300);
    song = ui_cur.song;
    turn(K_K2, 7);
    run(50);
    uint8_t now[64];
    fx_now(now);
    check(fxd_get(now, FE_FILTER, 1) == fxd_get(song_fx(song), FE_FILTER, 1), "FX LIVE: a knob turned while playing plays (fx_now)");
    {   /* a knob's run while playing: one change, the song's record rewritten where it lies: 600 clicks (as copies
         * ~100 KB, more than the edit memory) never fill it */
        uint32_t len = 0;
        uint8_t ver;
        store_get(REC_SONG, song, 0, &len, &ver);
        int v0 = fxd_get(song_fx(song), FE_FILTER, 1), changes = ui_edit_count(), full_seen = 0;
        uint32_t used = ui_edit_used();
        run(1600);
        for (int i = 0; i < 300; i++) {
            turn(K_K2, 1);
            turn(K_K2, -1);
            if (ui_top()) full_seen = 1;
        }
        turn(K_K2, 1);
        check(!full_seen && fxd_get(song_fx(song), FE_FILTER, 1) == v0 + 1 && ui_edit_count() == changes &&
              len * 600u > STORE_EDIT_MAX * 3u / 2u && ui_edit_used() == used, "FX LIVE: 600 clicks of a knob while "
              "playing: one change, rewritten where it lies, never MEMORY FOR EDITS IS FULL");
        turn(K_K2, -1);
    }
    /* a punch-in: a white key held */
    key(UI_WHITE[0], 1); run(200); shot("ui3_fx_live_punch"); key(UI_WHITE[0], 0);
    /* BYPASS held (F#5) */
    key(UI_BLACK[10], 1); run(100); shot("ui3_fx_live_bypass"); key(UI_BLACK[10], 0);
    tap(B_PLAY);                                        /* paused */
    run(300);

    /* EDIT: every item, its picture */
    tap(B_EDIT);
    for (int i = 0; i < 10; i++) {
        char nm[48];
        snprintf(nm, sizeof nm, "ui3_fx_edit_%02d", i);
        shot(nm);
        turn(K_SELECT, 1);
    }
    turn(K_SELECT, -10);
    /* ROUTING: GLO, K1 switches it, and back */
    int sends0 = fxd_sends(song_fx(song));
    tap(B_GLO); turn(K_K1, 1); tap(B_GLO);
    check(fxd_sends(song_fx(song)) == !sends0, "FX EDIT: GLO ROUTING switched");
    shot(sends0 ? "ui3_fx_edit_global" : "ui3_fx_edit_sends");
    tap(B_GLO); turn(K_K1, 1); tap(B_GLO);
    check(fxd_sends(song_fx(song)) == sends0, "FX EDIT: and back");
    /* MOVE: FILTER down a place, SEL puts it down */
    uint8_t c0[12], c1[12];
    int n0 = fxd_chain(song_fx(song), c0);
    turn(K_SELECT, 1);                                  /* FILTER */
    tap(B_SEL); tap(B_SEL);                             /* the menu, MOVE (its first item) */
    turn(K_SELECT, 1);
    tap(B_SEL);
    int n1 = fxd_chain(song_fx(song), c1);
    check(n0 == n1 && c1[1] == c0[2] && c1[2] == c0[1], "FX EDIT: MOVE swaps FILTER with the next");
    turn(K_SELECT, -5);                                 /* GATE */
    /* GATE's steps: EDIT copies the built-in pattern into the user's, a key switches a step */
    tap(B_EDIT);
    int pat = fxd_get(song_fx(song), FE_GATE, 0);
    check(pat >= 16, "FX EDIT: EDIT on a built-in GATE pattern edits a copy among the user's");
    uint8_t st[16], st2[16];
    ui_gate_get(pat - 16, st);
    key(UI_WHITE[3], 1); key(UI_WHITE[3], 0);
    ui_gate_get(pat - 16, st2);
    check(st2[3] != st[3], "FX EDIT: a white key switches its step");
    shot("ui3_fx_gate_edit");
    tap(B_EDIT);
    /* MIXER: K1 the level of track 00 */
    tap(B_SEQ);
    rec_song_t s;
    rec_song(song, &s);
    int lvl = s.mix[0][RM_LEVEL];
    turn(K_K1, -3);
    rec_song(song, &s);
    check(s.mix[0][RM_LEVEL] == lvl - 3, "FX MIXER: K1 the track's level in the song's mixer");
    tap(B_LFO);
    rec_song(song, &s);
    check(s.mix[0][RM_LEVEL] == 0x7F, "FX MIXER: LFO back to the default");
    tap(B_SEQ);
    tap(B_FX);                                          /* back where it came from */
    check(screen_is("PLAYBACK"), "FX again goes back");
}

/* ----------------------------------------------------------------- VISUAL --- */
static void test_visual(void)
{
    tap(B_PLAY);
    run(500);
    home_key(15);
    check(screen_is("VISUAL"), "PIT opens VISUAL");
    run(300);
    /* each view frozen for its shot (GLO, FREEZE), so the frame drawn whole equals the one sent in parts */
    static const char *const V[6] = {"25_visual", "ui3_visual_scope", "ui3_visual_wave", "ui3_visual_spectrum",
                                     "ui3_visual_levels", "ui3_visual_foma"};
    for (int i = 0; i < 6; i++) {
        if (i) tap(B_SEQ);
        run(i == 5 ? 2500 : 400);                       /* (FOMA: on its way a while) */
        tap(B_GLO); turn(K_SELECT, 1); turn(K_K1, 1); tap(B_GLO);
        run(100);
        shot(V[i]);
        tap(B_GLO); turn(K_SELECT, 1); turn(K_K1, 1); tap(B_GLO);
    }
    tap(B_SEQ);
    tap(B_PLAY);
    run(300);
}

/* ----------------------------------------------------- FUSION on the keys --- */
#define FUS 0x06                                /* the demo disk's FUSION cartridge (demo-disk/tools/catalog.py) */
static int fusion_slot(const char *name)        /* the FUSION cartridge's slot of that name, -1 none */
{
    rec_cart_t c;
    char nm[11];
    size_t n = strlen(name);
    if (rec_cart(FUS, &c) || c.kind != REC_FUSION) return -1;
    for (int s = 0; s < c.nslots; s++) {
        const uint8_t *p = rec_slot(&c, (uint8_t)s);
        if (!p) continue;
        rec_slot_name(REC_FUSION, p, nm);
        if (!strncmp(nm, name, n) && (nm[n] == ' ' || !nm[n])) return s;
    }
    return -1;
}
static int key_notes(uint8_t *notes, int max)   /* the keys' sounding voices' notes */
{
    pl_voice_info_t vi[PL_VOICES];
    int n = player_voices_now(vi, PL_VOICES), k = 0;
    for (int i = 0; i < n; i++)
        if (vi[i].on && vi[i].track == 0xFF && k < max) notes[k++] = vi[i].note;
    return k;
}
static int keys_quiet(void)                     /* the keys' notes rung out (5 s at most) */
{
    uint8_t x[16];
    for (int ms = 0; ms < 5000; ms += 10) { if (!key_notes(x, 16)) return 1; run(10); }
    return 0;
}
static void test_fusion_keys(void)
{
    /* a key's FUSION row runs its tick commands from the press: COIN, a fourth under the key cut at tick 5 (EC5),
     * the key's note from tick 5 (ED5): one note, then the other, never both */
    int coin = fusion_slot("COIN"), chipin = fusion_slot("CHIPIN");
    check(coin >= 0 && chipin >= 0, "FUSION: COIN and CHIPIN in the demo disk's FUSION cartridge");
    if (coin < 0 || chipin < 0) return;
    uint8_t a[16], b[16];
    ui_set_inst((uint16_t)(FUS << 8 | coin));
    run(300);
    check(keys_quiet(), "FUSION: the keys quiet before COIN");
    in_now.notes |= 1u << 12;
    run(20);
    int na = key_notes(a, 16), ms = 20, nb = 0;
    for (int got = 0; ms < 400 && !got; ms += 5) {      /* (the key's note: when it comes) */
        run(5);
        nb = key_notes(b, 16);
        for (int i = 0; i < nb; i++) got |= b[i] == a[0] + 5;
    }
    run(300);
    int nc = key_notes(b, 16);
    in_now.notes &= ~(1u << 12);
    run(500);
    printf("  FUSION COIN on a key: %d note(s) at the press, the key's note after %d ms, then %d\n", na, ms, nc);
    check(na == 1 && nc == 1 && b[0] == a[0] + 5 && ms >= 40 && ms < 400,
          "FUSION COIN on a key: a fourth under, cut at tick 5, then the key's note (EC5, ED5)");
    check(keys_quiet(), "FUSION: the keys quiet after COIN");
    in_now.notes |= 1u << 12;                           /* let go before tick 5: its note never comes */
    run(20);
    in_now.notes &= ~(1u << 12);
    run(400);
    nb = key_notes(b, 16);
    int late = 0;
    for (int i = 0; i < nb; i++) late |= b[i] == a[0] + 5;
    check(!late, "FUSION COIN: a key let go before tick 5, the key's note never starts");
    /* CHIPIN: the lead, the bass and the blip at the press, the echo from tick 3 (ED3), the blip again every 3
     * ticks and cut at tick 9: 3 voices, 4, then 3 (the blip gone) */
    ui_set_inst((uint16_t)(FUS << 8 | chipin));
    run(300);
    check(keys_quiet(), "FUSION: the keys quiet before CHIPIN");
    in_now.notes |= 1u << 12;
    run(20);
    na = key_notes(a, 16);
    int most = na;
    for (ms = 0; ms < 600; ms += 5) { run(5); int k = key_notes(b, 16); most = k > most ? k : most; }
    nc = key_notes(b, 16);
    in_now.notes &= ~(1u << 12);
    run(800);
    printf("  FUSION CHIPIN on a key: %d at the press, %d at most, %d after the blip's cut\n", na, most, nc);
    check(na == 3 && most >= 4 && nc == 3, "FUSION CHIPIN on a key: the echo from tick 3 (ED3), the blip cut at tick 9");
}

/* a song's FUSION cell runs its row's tick commands as a key's does (specs/files/patterns.md#fusion-patterns): CHIPIN
 * (E9x: the blip again every 3 ticks) written into the EDITOR's pattern on track 00 and played as its loop: 3 voices
 * at its start, 4 with the echo (ED3), 3 once the blip is cut (EC9); the edit taken back after */
static int track_notes(int t)
{
    pl_voice_info_t vi[PL_VOICES];
    int n = player_voices_now(vi, PL_VOICES), k = 0;
    for (int i = 0; i < n; i++) k += vi[i].on && vi[i].track == t;
    return k;
}
static void test_fusion_song(void)
{
    int chipin = fusion_slot("CHIPIN");
    if (chipin < 0) { check(0, "FUSION in a song: CHIPIN in the built-in cartridge"); return; }
    home_key(3);                                        /* the EDITOR */
    run(100);
    uint8_t pat = ui_cur.pattern;
    ui_set_inst((uint16_t)(FUS << 8 | chipin));
    turn(K_SELECT, -300); turn(K_ALG, -64);             /* (the cursor on track 00, row 00) */
    tap(B_REC);
    key(12, 1); key(12, 0); run(100);
    tap(B_REC);
    player_stop();
    player_all_sound_off();
    run(500);
    player_play_pattern(pat, 0);
    run(30);
    int na = track_notes(0), most = na, nc;
    for (int ms = 0; ms < 600; ms += 5) { run(5); int k = track_notes(0); most = k > most ? k : most; }
    nc = track_notes(0);
    printf("  FUSION CHIPIN in a song: %d at its start, %d at most, %d after the blip's cut\n", na, most, nc);
    check(na == 3 && most >= 4 && nc == 3, "FUSION CHIPIN in a song: the echo from tick 3 (ED3), the blip again (E93) "
          "and cut at tick 9 (EC9), as on a key");
    player_stop();
    player_all_sound_off();
    ui_edit_revert(REC_PATTERN, pat, 0);
    run(500);
}

/* ------------------------------------------------------------------ LOOPS --- */
/* ----------------------------------------------------------------------- LOOPS --- */
/* specs/ui/screens/loops.md, the loop station (fw/core/ui/scr_loops.c, lp_rec.c). loops_as_playground:
 * tests/playground/screens.js' LOOPS part, press for press, under the same shot names (the keys at the rows its run
 * wrote them on, so the paused states show the same loops); then test_loops checks each part: the first loop, the
 * takes landing by QUANTIZE, MONO legato, POLY's fifth note, armed while playing, re-record and peel, repeats and the cut
 * drawn, CONTROL mode and HOME's combination, the properties, the menu, the pattern recorder. LOOPS is left with no
 * loops. The loops' timing: 16 rows a bar, a row 6 ticks */
static int menu_to(const char *item);
static const char *asking(void);
static int lv_free(void);
static int lv_row(void);
static int lv_arm(void);
static void lv_drop(int song);
static int lp_here(void) { player_state_t st; player_state(&st, 0); return st.what == PL_LOOPS && st.playing; }
/* run until the loops play row `row` at tick `tick` or later in it: 1 there, 0 not within 20 s */
static int lp_at(int row, int tick)
{
    for (int i = 0; i < 20000; i++) {
        player_state_t st;
        player_state(&st, 0);
        if (st.what == PL_LOOPS && st.playing && st.row == row && st.tick >= tick) return 1;
        run(1);
    }
    return 0;
}
static void kdown(int n) { in_now.notes |= 1u << n; run(1); }
static void kup(int n) { in_now.notes &= ~(1u << n); run(1); }
static int lp_note(int i, int row, int lane)            /* a loop's cell's note (REC_NOTE_OFF an off), -1 none */
{
    rec_cell_t c;
    return loops_cell(i, row, lane, &c) && (c.has & RC_NOTE) ? c.note : -1;
}
static int lp_cmd(int i, int row, int lane, int code)   /* a command's value in a loop's cell, -1 none */
{
    rec_cell_t c;
    if (!loops_cell(i, row, lane, &c)) return -1;
    for (int k = 0; k < c.n; k++) if (c.cmd[k][0] == code) return c.cmd[k][1];
    return -1;
}
static int lp_cur(int k)                                /* LOOPS' cursor in the state (specs/connections.md#the-state) */
{
    uint8_t c[8] = {0};
    const ui_screen_t *s = ui_screen_now();
    if (s && s->cursor) s->cursor(c);
    return c[k];
}
static int lp_toast(const char *t) { return ui_toast_now() && !strcmp(ui_toast_now(), t); }
static int lp_wait(int bit, int on)                     /* run until the cursor's bit is on (off): 1, else 0 in 10 s */
{
    for (int i = 0; i < 10000; i++) { if (!(lp_cur(3) & bit) == !on) return 1; run(1); }
    return 0;
}
/* no loops, nothing playing; a loop more as a test wants it: the current instrument, base rows played reps times (the
 * first: the length len), MONO, its cells (row, lane, note: REC_NOTE_OFF an off) in their order */
typedef struct { uint8_t row, lane, note; } lpc_t;
static void lp_none(void)
{
    player_stop();
    run(30);
    ui_edit_loops(loops_clear(), ui_edit_used());
    loops_changed(1);
    run(30);
}
static int lp_make(int len, int base, int reps, int mono, const lpc_t *c, int n)
{
    static uint8_t b[600];
    lp_enc_t e;
    rec_cell_t x;
    int lanes = 1;
    loops_set_next_mono(mono);
    int i = loops_new((uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst);
    if (i < 0) return -1;
    if (!i) loops_set_rows(len);
    loops_enc_start(&e, b, sizeof b);
    for (int k = 0; k < n; k++) {
        memset(&x, 0, sizeof x);
        x.has = c[k].note == REC_NOTE_OFF ? RC_NOTE : RC_NOTE | RC_VOL;
        x.note = c[k].note;
        x.vol = 100;
        loops_enc_cell(&e, 4 * c[k].row + c[k].lane, &x);
        if (c[k].lane + 1 > lanes) lanes = c[k].lane + 1;
    }
    loops_put(i, base, reps, mono, b, loops_enc_end(&e), lanes);
    loops_changed(1);
    return i;
}
static void lp_select(int i) { turn(K_SELECT, -20); if (i) turn(K_SELECT, i); }
static void rp_notes(int *lo, int *hi)                  /* the notes the pattern recorder's preview plays over 2.5 s */
{
    pl_voice_info_t vi[PL_VOICES];
    *lo = *hi = 0;
    for (int t = 0; t < 250; t++) {
        int n = player_voices_now(vi, PL_VOICES);
        for (int i = 0; i < n; i++)
            if (vi[i].on && vi[i].track >= 16 && vi[i].track != 0xFF) {
                if (!*lo || vi[i].note < *lo) *lo = vi[i].note;
                if (vi[i].note > *hi) *hi = vi[i].note;
            }
        run(10);
    }
}
/* the first cartridge of a kind in the user area, the built-in one cloned to the first number free (as screens.js
 * makes them for its shots); -1 none */
static int lp_clone(int kind)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++)
        if (ins_kind(nums[i]) == kind) {
            rec_cart_t c;
            char nm[16];
            int to = ins_free_cart();
            rec_cart(nums[i], &c);
            rec_cart_name(&c, nm);
            ins_copy_cart(nums[i], (uint8_t)to, nm);
            return to;
        }
    return -1;
}
/* the loop's stream copied (the 16 loops' shot: copies of the two) */
static uint32_t lp_stream(int i, uint8_t *dst, uint32_t max)
{
    const uint8_t *end, *s = loops_stream(i, &end);
    rec_cur_t cu;
    rec_cell_t c;
    lp_enc_t e;
    loops_enc_start(&e, dst, max);
    rec_cur_set_end(&cu, s, end);
    while (s && cu.row < 4 * loops_base(i)) { int r = cu.row; rec_cur_at(&cu, r, &c); loops_enc_cell(&e, r, &c); }
    return loops_enc_end(&e);
}
static int lp_dx = -1, lp_ju = -1;                      /* the cartridges cloned for the shots (reverted after) */
static const int LP_S2[5] = {2, 6, 10, 14, 15};        /* the pattern recorder's second row's steps in the shots */
static void loops_as_playground(void)
{
    char msg[200];
    lp_dx = lp_clone(REC_DX7);
    lp_ju = lp_clone(REC_JUNO);
    printf("  LOOPS' shots: the first DX7 cartridge cloned to %02X, the JUNO-106 to %02X (the playground's 07)\n", lp_dx, lp_ju);
    ui_set_inst((uint16_t)(lp_ju << 8));
    player_stop();
    run(100);
    home_key(22);                                       /* POLY: LOOPS */
    check(screen_is("LOOPS") && !loops_n(), "POLY opens LOOPS: no loops yet (the demo disk has no LOOPS record)");
    shot("27_loops_empty");
    {
        uint8_t def[FX_SETUP_LEN];
        fxd_default(def);
        check(!memcmp(loops_fx(), def, FX_SETUP_LEN), "LOOPS with no record: its FX setup a new song's (FX can switch effects on)");
    }
    /* the first loop: REC, PLAY (COUNT-IN OFF: the loops and the take at once), a chord, two notes; REC closes it */
    tap(B_REC);
    press(B_PLAY); release(B_PLAY);
    lp_at(0, 5); kdown(0); kdown(4); kdown(7);           /* (0.83: on row 1, waiting for it) */
    lp_at(5, 5); kup(0); kup(4); kup(7);                 /* (their offs on row 6) */
    lp_at(6, 0); kdown(11);
    lp_at(7, 5); kup(11);
    lp_at(9, 0); kdown(9);
    lp_at(9, 2); kup(9);                                 /* (its off a row after it: 10) */
    lp_at(10, 0);
    shot("28_loops_first");
    press(B_REC); release(B_REC);
    snprintf(msg, sizeof msg, "LOOPS: the first loop closed by REC in its first bar: 1 bar (%d rows, %s)", loops_rows(),
             ui_toast_now() ? ui_toast_now() : "-");
    check(loops_n() == 1 && loops_rows() == 16 && lp_toast("LOOP 00: 1 BAR"), msg);
    check(lp_note(0, 1, 0) == ui_midi(0) && lp_note(0, 1, 1) == ui_midi(4) && lp_note(0, 1, 2) == ui_midi(7) &&
          lp_note(0, 6, 1) == REC_NOTE_OFF && lp_note(0, 6, 2) == REC_NOTE_OFF && lp_note(0, 6, 0) == ui_midi(11) &&
          lp_note(0, 8, 0) == REC_NOTE_OFF && lp_note(0, 9, 0) == ui_midi(9) && lp_note(0, 10, 0) == REC_NOTE_OFF &&
          loops_lanes(0) == 3 && !loops_mono(0),
          "LOOPS: the chord a lane a key on row 1 (offs on 6), the notes after it on lane 0 (each on the lane free)");
    /* CONTROL's MONO: the next loop a line, recorded as the loops play */
    tap(B_SEQ); key(20, 1); key(20, 0); tap(B_SEQ);
    lp_at(4, 0);
    tap(B_REC);
    check((lp_cur(3) & 1) && loops_n() == 1, "LOOPS: REC on + NEW LOOP while the loops play: armed, no new loop yet");
    check(lp_wait(2, 1) && loops_n() == 2 && loops_mono(1) && loops_sel() == 1, "LOOPS: ... the take at the loops' start, "
          "the new loop MONO (the next loops' mode), selected");
    static const int K[4] = {14, 16, 18, 21};
    for (int k = 0; k < 4; k++) { lp_at(3 * k, 1); kdown(K[k]); lp_at(3 * k + 1, 5); kup(K[k]); }
    check(lp_wait(2, 0) && loops_sel() == 2, "LOOPS: the take stops by itself after one pass, the selection on + NEW LOOP");
    run(500);
    lp_at(4, 0);
    shot("28b_loops_two");
    player_pause();
    run(100);
    check(lp_note(1, 0, 0) == ui_midi(14) && lp_note(1, 2, 0) == REC_NOTE_OFF && lp_note(1, 9, 0) == ui_midi(21) &&
          lp_note(1, 11, 0) == REC_NOTE_OFF && loops_lanes(1) == 1, "LOOPS: the MONO take's notes, its one lane");
    /* paused: HOME's combination, a white key muting loop 00 (only its line left), again: on */
    tap(B_HOME);
    shot("31c_home_loops");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0); run(100);
    shot("31d_home_loops_muted");
    check(loops_muted(0) && lp_toast("LOOP 00 MUTED"), "LOOPS: HOME + white key 1: loop 00 muted at once (LOOP 00 MUTED)");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);
    tap(B_HOME);
    check(!loops_muted(0) && !ui_top(), "LOOPS: ... again: on");
    /* the pattern recorder (its preview stopped by ARP again), steps on two rows */
    tap(B_ARP); tap(B_ARP);
    for (int k = 0; k < 16; k += 4) { key(UI_WHITE[k], 1); key(UI_WHITE[k], 0); }
    turn(K_SELECT, 1); turn(K_K3, 7);
    for (int k = 0; k < 5; k++) { key(UI_WHITE[LP_S2[k]], 1); key(UI_WHITE[LP_S2[k]], 0); }
    shot("28c_loops_pattern");
    tap(B_HOME);
    /* armed on + NEW LOOP, paused (a note or PLAY starts it); CONTROL mode with loop 01 soloed; GLO; SEL */
    tap(B_REC);
    shot("28d_loops_armed");
    tap(B_REC);
    tap(B_SEQ); key(17, 1); key(17, 0); key(UI_WHITE[1], 1); key(UI_WHITE[1], 0); run(2100);
    shot("28e_loops_control");
    key(UI_WHITE[1], 1); key(UI_WHITE[1], 0); key(17, 1); key(17, 0); tap(B_SEQ); run(2100);
    tap(B_GLO);
    shot("28f_loops_glo");
    tap(B_GLO);
    turn(K_SELECT, -1);
    tap(B_SEL);
    shot("28g_loops_sel");
    tap(B_HOME);
    /* 16 loops: the two, again and again, colour by colour, loop 05 muted */
    {
        static uint8_t s0[600], s1[600];
        uint32_t l0 = lp_stream(0, s0, sizeof s0), l1 = lp_stream(1, s1, sizeof s1);
        for (int k = 2; k < LP_MAX; k++) {
            loops_set_next_mono(k & 1);
            int i = loops_new((uint8_t)(loops_inst(k & 1) >> 8), (uint8_t)loops_inst(k & 1));
            if (i >= 0) loops_set_stream(i, k & 1 ? s1 : s0, k & 1 ? l1 : l0);
        }
        loops_set_muted(5, 1);
        loops_changed(1);
        turn(K_SELECT, 20);
        shot("28i_loops_full16");
        check(loops_n() == LP_MAX && loops_sel() == LP_MAX - 1 && loops_colour(13) == 1,
              "LOOPS: 16 loops: SELECT stops at the last (no + NEW LOOP), the colours round again");
        for (int k = LP_MAX - 1; k >= 2; k--) loops_peel(k);
        loops_set_next_mono(1);
        loops_changed(1);
        turn(K_SELECT, -20);
    }
    /* RECORD LIVE recording, the loops' second pass (the song dropped after) */
    {
        int n = lv_free();
        lv_arm();
        press(B_PLAY); release(B_PLAY);
        for (int i = 0; i < 20000 && lv_row() < loops_rows(); i++) run(1);
        run(300);
        shot("28h_loops_song");
        player_pause();
        run(100);
        lv_drop(n);
    }
}
/* a take's notes landed by QUANTIZE: loop 00 recorded again from silence (a key starts it on row 0, let go at 0.83:
 * its off on row 1), then each key pressed at (row, tick) and let go a row on: where each landed, `row` (`/lane` past
 * lane 0, `dED` a note delay), joined by commas */
static void lp_quant(int q, const int (*cases)[3], int n, char *out, int max)
{
    out[0] = 0;
    ui_edit_loops(loops_set_quant(q), ui_edit_used());
    tap(B_EDIT);
    kdown(0);
    lp_at(0, 5); kup(0);
    for (int k = 0; k < n; k++) {
        lp_at(cases[k][0], cases[k][1]);
        kdown(cases[k][2]);
        if (cases[k][0] + 1 < loops_rows()) { lp_at(cases[k][0] + 1, 1); kup(cases[k][2]); }
    }
    press(B_REC); release(B_REC);
    for (int k = 0; k < n; k++) {
        kup(cases[k][2]);
        int m = ui_midi(cases[k][2]), found = 0;
        for (int r = 0; r < loops_rows() && !found; r++)
            for (int l = 0; l < LP_LANES && !found; l++)
                if (lp_note(0, r, l) == m) {
                    int ed = lp_cmd(0, r, l, 0x0E), len = (int)strlen(out);
                    snprintf(out + len, (size_t)(max - len), "%s%d", k ? "," : "", r);
                    len = (int)strlen(out);
                    if (l) snprintf(out + len, (size_t)(max - len), "/%d", l);
                    len = (int)strlen(out);
                    if (ed >= 0) snprintf(out + len, (size_t)(max - len), "d%d", ed & 15);
                    found = 1;
                }
    }
    player_stop();
    run(100);
}
/* SAVE TO PATTERN, LOAD FROM PATTERN (specs/ui/screens/loops.md#storing-in-a-pattern-and-loading-one): the list with NEW
 * first and picked, the question over a pattern not empty; LOAD's list, its PLAY at the timing LOAD brings, the question
 * over loops, the toast; the round trip; a DJ loop of the demo disk loaded: a loop an instrument, at its song's tempo */
static int plist_to(int pat, int plus)  /* the pattern list open: SELECT onto pattern pat (plus: a line before them) */
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256);
    turn(K_SELECT, -300);
    for (int i = 0; i < n; i++) if (nums[i] == pat) { if (i + plus) turn(K_SELECT, i + plus); return 1; }
    return 0;
}
static void test_loops_patterns(void)
{
    char msg[200], t[48];
    rec_cell_t c;
    player_state_t st;
    static const lpc_t s0[2] = {{0, 0, 60}, {2, 0, REC_NOTE_OFF}}, s1[3] = {{0, 0, 64}, {0, 1, 67}, {8, 0, REC_NOTE_OFF}};
    lp_none();
    lp_make(16, 4, 4, 0, s0, 2);
    lp_make(16, 16, 1, 0, s1, 3);
    uint16_t in0 = loops_inst(0);
    ui_edit_loops(loops_set_inst(1, (uint8_t)(in0 >> 8), (uint8_t)(in0 + 1)), ui_edit_used());
    ui_edit_loops(loops_set_mix(1, RM_LEVEL, 0x40), ui_edit_used());
    int fp = ed_first_free();
    tap(B_SEL); check(menu_to("SAVE TO PATTERN"), "LOOPS' menu: SAVE TO PATTERN"); tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_LIST, "LOOPS: SAVE TO PATTERN: the pattern list (SAVE THE LOOPS TO), NEW first and picked");
    shot("ui3_loops_save_to");
    tap(B_SEL);
    snprintf(t, sizeof t, "LOOPS STORED IN PATTERN %02X", fp);
    snprintf(msg, sizeof msg, "LOOPS: ... SEL on NEW: a pattern of the loops' length at the first free number (%s)", t);
    check(!ui_top() && ui_pattern_rows((uint8_t)fp) == 16 && ui_pattern_tracks((uint8_t)fp) == 3 && lp_toast(t), msg);
    check(ed_cell((uint8_t)fp, 4, 0, &c) && c.note == 60 && (c.has & RC_INS) && (c.cart << 8 | c.slot) == in0 &&
          ed_cell((uint8_t)fp, 6, 0, &c) && c.note == REC_NOTE_OFF && ed_cell((uint8_t)fp, 0, 1, &c) && c.note == 64 && c.n == 1 &&
          c.cmd[0][0] == 0x80 && c.cmd[0][1] == 0x40 && ed_cell((uint8_t)fp, 0, 2, &c) && c.note == 67,
          "LOOPS: ... the repeats written out, each note naming its instrument, a lane a track, the loop's mix as = commands");
    tap(B_SEL); menu_to("SAVE TO PATTERN"); tap(B_SEL);
    plist_to(fp, 1);
    tap(B_SEL);
    snprintf(t, sizeof t, "PATTERN %02X IS NOT EMPTY", fp);
    check(!strcmp(asking(), t), "LOOPS: SAVE TO PATTERN onto a pattern not empty: asks first (... IS NOT EMPTY / REPLACE IT WITH "
          "THE LOOPS?)");
    tap(B_OCTM);
    check(!ui_top(), "LOOPS: ... NO: nothing written");
    /* back from it: LOAD FROM PATTERN over loops asks; a loop an instrument (two tracks of one: its lanes) */
    tap(B_SEL); check(menu_to("LOAD FROM PATTERN"), "LOOPS' menu: LOAD FROM PATTERN"); tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_LIST, "LOOPS: LOAD FROM PATTERN: the pattern list (LOAD INTO LOOPS)");
    plist_to(fp, 0);
    press(B_PLAY); release(B_PLAY); run(60);
    player_state(&st, 0);
    check(st.playing && st.what == PL_LOOP && st.pat == fp && st.bpm10 == (uint16_t)(loops_bpm() * 10), "LOOPS: ... its PLAY: the "
          "pattern at the timing LOAD brings (in no song: the loops')");
    press(B_PLAY); release(B_PLAY); run(30);
    tap(B_SEL);
    check(!strcmp(asking(), "REPLACE THE LOOPS"), "LOOPS: ... over loops: asks first (REPLACE THE LOOPS / WITH PATTERN ..?)");
    ui_edit_loops(loops_set_mix(1, RM_LEVEL, 0x7F), ui_edit_used());
    tap(B_OCTP);
    snprintf(t, sizeof t, "PATTERN %02X IN LOOPS: 2 LOOPS", fp);
    snprintf(msg, sizeof msg, "LOOPS: ... YES: the pattern as loops, a loop an instrument, its mix from its = command (%s)",
             ui_toast_now() ? ui_toast_now() : "-");
    check(loops_n() == 2 && loops_inst(0) == in0 && loops_base(0) == 16 && loops_repeats(0) == 1 && lp_note(0, 12, 0) == 60 &&
          lp_note(0, 14, 0) == REC_NOTE_OFF && loops_lanes(1) == 2 && lp_note(1, 0, 1) == 67 && loops_mix(1)[RM_LEVEL] == 0x40 &&
          loops_sel() == 0 && lp_toast(t), msg);
    ui_edit_del(REC_PATTERN, (uint8_t)fp, 0);
    /* a DJ loop of the demo disk (each track one instrument): a loop an instrument, at its song's tempo */
    ui_song_t s;
    rec_pat_t pt;
    uint16_t insts[16];
    int ni = 0, pat;
    check(ui_song(6, &s), "(the demo disk's first DJ loop, song 06)");
    pat = s.order[s.npos - 1];
    rec_pattern((uint8_t)pat, &pt);
    {
        rec_cur_t cu[16];
        for (int k = 0; k < pt.ntrk; k++) rec_cur_set_end(&cu[k], pt.trk[k], pt.trk_end[k]);
        for (int r = 0; r < pt.rows; r++)               /* (the instruments in the order they first play: by rows, then tracks) */
            for (int k = 0; k < pt.ntrk; k++)
                if (rec_cur_at(&cu[k], r, &c) && (c.has & RC_NOTE) && c.note != REC_NOTE_OFF && cu[k].ins) {
                    int in = cu[k].cart << 8 | cu[k].slot, j = 0;
                    while (j < ni && insts[j] != in) j++;
                    if (j == ni && ni < 16) insts[ni++] = (uint16_t)in;
                }
    }
    tap(B_SEL); menu_to("LOAD FROM PATTERN"); tap(B_SEL);
    plist_to(pat, 0);
    tap(B_SEL); tap(B_OCTP);
    int all = loops_n() == ni;
    for (int k = 0; k < ni && all; k++) all &= loops_inst(k) == insts[k] && !loops_mono(k) && loops_repeats(k) == 1;
    snprintf(msg, sizeof msg, "LOOPS: LOAD of DJ loop %02X (%d tracks x %d rows): a loop an instrument, in the order they first play "
             "(%d loops of %d instruments), at its song's tempo (%d, song %d), its rows (%d) (%s)", pat, pt.ntrk, pt.rows, loops_n(), ni,
             loops_tempo(), s.tempo, loops_rows(), ui_toast_now() ? ui_toast_now() : "-");
    check(all && loops_tempo() == s.tempo && loops_rows() == pt.rows, msg);
}
/* RECORD LIVE (specs/ui/screens/loops.md#record-live), as tests/playground/loops.js checks it: armed, REC starts it with
 * the loops; a pass a position and a pattern, identical passes sharing one; the loops' notes as they sounded, a mute
 * from its row, the keys landed by QUANTIZE (heard as written), FX turns, an effect switched on, a punch-in, a tempo
 * change and a loop's mix turned as commands at their rows; leaving LOOPS ends it at once; what fits no track left out
 * and counted; the edit memory full, 256 positions, no song number free */
static int lv_free(void)                /* the first song number free */
{
    ui_song_t s;
    for (int n = 0; n <= UI_SONG_LAST; n++) if (!ui_song((uint8_t)n, &s)) return n;
    return -1;
}
static int lv_cmd(uint8_t pat, int row, int trk, int code)   /* a cell's command's value, -1 none */
{
    rec_cell_t c;
    if (!ed_cell(pat, row, trk, &c)) return -1;
    for (int k = 0; k < c.n; k++) if (c.cmd[k][0] == code) return c.cmd[k][1];
    return -1;
}
static int lv_cmd_row(uint8_t pat, int row, int code, int v)  /* 1: a cell of the row holds it */
{
    for (int t = 0; t < 16; t++) if (lv_cmd(pat, row, t, code) == v) return 1;
    return 0;
}
static int lv_row(void)                 /* the song's row heard (its cursor recording), -99 none */
{
    player_state_t st;
    player_state(&st, 0);
    return (lp_cur(3) & 64) && st.what == PL_LOOPS && st.playing && live_rows0() ? (int)(st.rows_played - live_rows0()) : -99;
}
static int lv_until(int (*f)(void))     /* run until f: 1, 0 not within 40 s */
{
    for (int i = 0; i < 40000 && !f(); i++) run(1);
    return f();
}
static int lv_ended(void) { return !(lp_cur(3) & (32 | 64)); }
static int lv_at(int row, int tick)     /* run until the song's row `row` is heard, at tick `tick` or later: 1 there */
{
    for (int i = 0; i < 40000; i++) {
        player_state_t st;
        player_state(&st, 0);
        int r = lv_row();
        if (r > row || (r == row && st.tick >= tick)) return r == row;
        run(1);
    }
    return 0;
}
static int lv_arm(void)                 /* SEL's RECORD LIVE, then its panel's REC: armed (1) */
{
    tap(B_SEL); menu_to("RECORD LIVE"); tap(B_SEL);
    int panel = ui_top() && ui_top()->kind == OV_OTHER;
    tap(B_REC);
    return panel && (lp_cur(3) & 32) && !ui_top();
}
static int lv_switch(int rest, int row)   /* run until the player's last switch is that one (the loops silent, or at
                                         * that row): its song row, -1 not within 3 s */
{
    for (int i = 0; i < 3000; i++) {
        player_state_t st;
        player_state(&st, 0);
        if (st.lp_rest == rest && (rest ? 1 : st.lp_sw_row == row)) return (int)(st.lp_sw_rows - live_rows0());
        run(1);
    }
    return -1;
}
static int lv_key_voice(int note)       /* a keys' voice sounding that note (the keys' lanes) */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), on = 0;
    for (int i = 0; i < n; i++) on |= v[i].on && v[i].track == 0xFF && v[i].note == note;
    return on;
}
static void lv_drop(int song)           /* a song made here and its patterns gone again (they were edits alone) */
{
    ui_song_t s;
    if (!ui_song((uint8_t)song, &s)) return;
    for (int p = 0; p < s.npos; p++) store_edit_forget(REC_PATTERN, s.order[p], 0);
    store_edit_forget(REC_SONG, (uint8_t)song, 0);
}
static void test_loops_live(void)
{
    char msg[220], t[48];
    uint8_t fx[FX_SETUP_LEN];
    static const lpc_t a[4] = {{0, 0, 60}, {4, 0, REC_NOTE_OFF}, {8, 0, 62}, {12, 0, REC_NOTE_OFF}}, b[2] = {{0, 0, 64}, {2, 0, REC_NOTE_OFF}};
    lp_none();
    lp_make(16, 16, 1, 0, a, 4);
    lp_make(16, 4, 4, 0, b, 2);
    uint16_t in0 = loops_inst(0), in1 = (uint16_t)(in0 + 1);
    ui_edit_loops(loops_set_inst(1, (uint8_t)(in1 >> 8), (uint8_t)in1), ui_edit_used());
    ui_edit_loops(loops_set_tempo(30), ui_edit_used());   /* (a row 500 ms: a tap's 260 ms fit between the changes) */
    ui_edit_loops(loops_set_quant(LQ_ROW), ui_edit_used());
    memcpy(fx, loops_fx(), FX_SETUP_LEN);
    fx[7] &= (uint8_t)~(1u << 5);                       /* (the DELAY off) */
    ui_edit_loops(loops_set_fx(fx), ui_edit_used());
    loops_changed(1);
    lp_select(0);
    tap(B_SEL); menu_to("RECORD LIVE"); tap(B_SEL);
    int panel = ui_top() && ui_top()->kind == OV_OTHER;
    shot("ui3_loops_record_live");
    turn(K_K1, 2);
    int t2 = loops_tempo();
    turn(K_K1, -2);
    tap(B_HOME);
    check(panel && t2 == 32 && loops_tempo() == 30 && !ui_top() && !(lp_cur(3) & 32),
          "RECORD LIVE: SEL's RECORD LIVE opens its panel; K1 turns LOOPS' tempo; HOME closes it, nothing armed");
    check(lv_arm(), "RECORD LIVE: ... the panel's REC arms it and closes it");
    tap(B_EDIT);
    check(lp_toast("RECORDING A SONG") && lp_note(0, 0, 0) == 60, "RECORD LIVE: ... EDIT refused while armed (RECORDING A SONG)");
    tap(B_SEL); check(menu_to("CANCEL RECORD LIVE"), "RECORD LIVE: ... the menu's item reads CANCEL RECORD LIVE"); tap(B_SEL);
    check(!(lp_cur(3) & 32) && lp_toast("CANCELLED") && !lp_here(), "RECORD LIVE: ... CANCEL RECORD LIVE disarms it (CANCELLED)");
    lv_arm();
    int n0 = lv_free(), k0 = ui_midi(0), k4 = ui_midi(4);
    press(B_PLAY); release(B_PLAY); run(30);
    check((lp_cur(3) & 64) && lp_here() && ui_screen_now()->recording(), "RECORD LIVE: armed with nothing playing, PLAY "
          "(COUNT-IN OFF): the loops from their start, the song recording with them (the header's red dot)");
    lp_at(3, 1); memcpy(fx, loops_fx(), FX_SETUP_LEN); fx[7] |= 1u << 5; ui_edit_loops(loops_set_fx(fx), ui_edit_used());
    lp_at(5, 2); kdown(0);                              /* (ROW: on row 5) */
    lp_at(6, 0); ui_punch_down(2); run(2);
    lp_at(7, 1); kup(0);
    tap(B_SEQ);
    lp_at(9, 0); key(UI_WHITE[1], 1); lp_at(9, 1); key(UI_WHITE[1], 0);   /* (CONTROL: loop 01 muted at once) */
    ui_edit_loops(loops_set_mix(0, RM_LEVEL, 0x50), ui_edit_used());
    loops_changed(0);
    tap(B_SEQ);
    lp_at(10, 0); ui_punch_up(2); run(2);
    ui_edit_loops(loops_set_quant(LQ_4_ROWS), ui_edit_used());
    lp_at(10, 2); kdown(4);                             /* (4 ROWS: 10.33 lands on 12, ahead: it waits for it) */
    lp_at(11, 3);
    int early = lv_key_voice(k4);
    lp_at(12, 2);
    int heard = lv_key_voice(k4);
    memcpy(fx, loops_fx(), FX_SETUP_LEN); fx[16 + 0xA7 - 0x90] = 70; ui_edit_loops(loops_set_fx(fx), ui_edit_used());
    lp_at(13, 5); kup(4);
    lp_at(14, 1); ui_edit_loops(loops_set_tempo(160), ui_edit_used());
    check(!early && heard, "RECORD LIVE: ... a key's note heard as written: on the keys' voices at its line, not before");
    check(loops_muted(1), "RECORD LIVE: ... the mute is live");
    run(40);
    {
        const char *f = ui_screen_now()->footer(&(uint8_t){0});
        snprintf(msg, sizeof msg, "RECORD LIVE: recording: the footer's MEMORY n%% FREE (%s), REC full", f);
        check(!strncmp(f, "MEMORY ", 7) && strstr(f, "% FREE") && leds[B_REC] == LV_FULL, msg);
    }
    tap(B_EDIT);
    check(lp_toast("RECORDING A SONG") && loops_n() == 2, "RECORD LIVE: ... RE-RECORD refused (RECORDING A SONG)");
    tap(B_LFO);
    check(lp_toast("RECORDING A SONG") && loops_n() == 2, "RECORD LIVE: ... PEEL refused");
    tap(B_ARP);
    check(lp_toast("RECORDING A SONG") && !ui_top(), "RECORD LIVE: ... the pattern recorder refused");
    tap(B_SEL); menu_to("DOUBLE"); tap(B_SEL);
    check(lp_toast("RECORDING A SONG") && loops_rows() == 16, "RECORD LIVE: ... DOUBLE refused");
    /* the loops' third pass (song rows 32-47): PLAY: the loops silent from the song's next row, it going on; again: on
     * from their place; held: to their start */
    player_state_t st;
    lv_at(34, 1); press(B_PLAY); release(B_PLAY);
    int s0 = lv_switch(1, 0), place = -1;
    player_state(&st, 0);
    place = st.lp_rest_row;
    lv_at(36, 0);
    int silent = (lp_cur(3) & 64) && lp_here();
    lv_at(38, 1); press(B_PLAY); release(B_PLAY);
    int s1 = lv_switch(0, 3);
    lv_at(46, 1); press(B_PLAY); run(ui_set[S_HOLD_DELAY] + 20); release(B_PLAY);
    int s2 = lv_switch(0, 0);
    snprintf(msg, sizeof msg, "RECORD LIVE: ... PLAY: the loops silent from the next row, their place kept (row %d, place %d), "
             "the song going on; PLAY: on again from there on the next row (%d); held: to their start on the next row (%d)",
             s0, place, s1, s2);
    check(s0 == 35 && place == 3 && silent && s1 == 39 && s2 > 46, msg);
    lv_at(s2 + 2, 1);
    tap(B_REC);                                         /* it ends at once */
    run(10);
    snprintf(t, sizeof t, "SONG %02X RECORDED: ", n0);
    snprintf(msg, sizeof msg, "RECORD LIVE: REC: it ends at once, the loops playing on (%s)", ui_toast_now() ? ui_toast_now() : "-");
    check(lv_ended() && lp_here() && ui_toast_now() && !strncmp(ui_toast_now(), t, strlen(t)) &&
          strstr(ui_toast_now(), ", 1 PATTERN"), msg);
    ui_song_t s;
    rec_song_t rs;
    int got = ui_song((uint8_t)n0, &s) && !rec_song((uint8_t)n0, &rs);
    uint8_t p0 = got ? s.order[0] : 0;
    snprintf(msg, sizeof msg, "RECORD LIVE: a new song at the first free number, one pattern of the rows heard (%d, its last "
             "row %d)", got ? ui_pattern_rows(p0) : 0, s2 + 2);
    check(got && s.npos == 1 && ui_pattern_rows(p0) == s2 + 3, msg);
    check(got && rs.tempo == 30 && rs.rpb == 4 && (rs.flags & RS_STOP) && !(rs.fx[7] >> 5 & 1) && rs.fx[16 + 0xA7 - 0x90] != 70 &&
          rs.mix[0][RM_LEVEL] == 0x7F && !strcmp(s.name, "LOOPS 01"), "RECORD LIVE: ... LOOPS' tempo, FX setup and mixes as they "
          "were at its start; it plays once and stops; named as the panel had it (LOOPS 01)");
    rec_cell_t c;
    check(ed_cell(p0, 0, 0, &c) && c.note == 60 && (c.cart << 8 | c.slot) == in0 && ed_cell(p0, 8, 0, &c) && c.note == 62 &&
          ed_cell(p0, 4, 0, &c) && c.note == REC_NOTE_OFF && ed_cell(p0, 12, 0, &c) && c.note == REC_NOTE_OFF,
          "RECORD LIVE: ... loop 00's notes on track 0, each naming its instrument");
    int l1 = 1;
    for (int r = 0; r <= 8; r += 4) l1 &= ed_cell(p0, r, 1, &c) && c.note == 64 && (c.cart << 8 | c.slot) == in1;
    check(l1 && ed_cell(p0, 9, 1, &c) && c.note == REC_NOTE_OFF && !ed_cell(p0, 12, 1, &c) && !ed_cell(p0, 10, 1, &c),
          "RECORD LIVE: ... loop 01's repeats written out on track 1, left out from row 9 (its mute), a note off there for the "
          "note it had sounding");
    check(ed_cell(p0, 5, 2, &c) && c.note == k0 && (c.cart << 8 | c.slot) == ui_cur.inst && c.vol == ui_set[S_KEYS_VOLUME] &&
          ed_cell(p0, 7, 2, &c) && c.note == REC_NOTE_OFF, "RECORD LIVE: ... the keys on a track of their own, in the current "
          "instrument, landed by QUANTIZE (ROW: 5.33 on 5, its note off on 7)");
    check(ed_cell(p0, 12, 2, &c) && c.note == k4 && ed_cell(p0, 14, 2, &c) && c.note == REC_NOTE_OFF,
          "RECORD LIVE: ... 4 ROWS: 10.33 on row 12, its note off on 14");
    check(lv_cmd(p0, 9, 0, 0x80) == 0x50, "RECORD LIVE: ... loop 00's mix turned: =80 50 on its track at its row (9)");
    snprintf(msg, sizeof msg, "RECORD LIVE: ... DELAY switched on (#ON 05, row 3), a punch-in of 4 rows (!04, row 6: %d), REVERB "
             "MIX turned (row 12), the tempo (FA0, row 14)", lv_cmd(p0, 6, 0, 0xC5));
    check(lv_cmd_row(p0, 3, 0xAE, 5) && lv_cmd_row(p0, 6, 0xC5, 4) && lv_cmd_row(p0, 12, 0xA7, 70) && lv_cmd_row(p0, 14, 0x0F, 0xA0), msg);
    int empty = 1;
    for (int r = 16; r < 32; r++) empty &= !ed_cell(p0, r, 1, &c) && !ed_cell(p0, r, 2, &c);
    check(ed_cell(p0, 16, 0, &c) && c.note == 60 && !c.n && empty, "RECORD LIVE: ... the loops' next pass (row 16): loop 01 still "
          "muted, nothing played on the keys, no commands");
    int hush = 1;
    for (int r = 36; r <= 38; r++) hush &= !ed_cell(p0, r, 0, &c);
    check(ed_cell(p0, 35, 0, &c) && c.note == REC_NOTE_OFF && hush && ed_cell(p0, 40, 0, &c) && c.note == REC_NOTE_OFF &&
          ed_cell(p0, 44, 0, &c) && c.note == 62 && ed_cell(p0, s2, 0, &c) && c.note == 60,
          "RECORD LIVE: ... silent from row 35 (a note off for the note sounding there, then nothing), on again from row 39 at "
          "their row 3 (row 40 their 4, row 44 their 8), at their start from the hold's row");
    ui_edit_loops(loops_set_muted(1, 0), ui_edit_used());
    ui_edit_loops(loops_set_tempo(150), ui_edit_used());
    ui_edit_loops(loops_set_quant(LQ_ROW), ui_edit_used());
    loops_changed(0);
    lv_drop(n0);

    /* armed while the loops play: REC starts it at once, at the row playing; leaving LOOPS ends it at once, what was heard
     * written */
    check(lp_here(), "(the loops play on after it)");
    lv_arm();
    lp_at(5, 0);
    tap(B_REC);
    run(5);
    player_state(&st, 0);
    int r1 = ((st.row - lv_row()) % 16 + 16) % 16;
    snprintf(msg, sizeof msg, "RECORD LIVE: armed while the loops play, REC starts it at once: its row 0 the loops' row "
             "playing (%d)", r1);
    check((lp_cur(3) & 64) && lv_row() >= 0 && r1 == 5, msg);
    int n1 = lv_free();
    lv_at(4, 0);                                        /* (the loops' row 9: their 8 heard on the song's 3) */
    home_key(8);
    int ok1 = ui_song((uint8_t)n1, &s);
    snprintf(t, sizeof t, "SONG %02X RECORDED: ", n1);
    snprintf(msg, sizeof msg, "RECORD LIVE: leaving LOOPS: it ends at once (%s), the loops playing on; its pattern up to there "
             "(%d rows), the loops' row 8 on its row 3", ui_toast_now() ? ui_toast_now() : "-", ok1 ? ui_pattern_rows(s.order[0]) : 0);
    check(screen_is("PLAYBACK") && lp_here() && ok1 && s.npos == 1 && ui_toast_now() && !strncmp(ui_toast_now(), t, strlen(t)) &&
          ui_pattern_rows(s.order[0]) >= 4 && ui_pattern_rows(s.order[0]) <= 6 && ed_cell(s.order[0], 3, 0, &c) && c.note == 62, msg);
    lv_drop(n1);
    player_stop(); run(100);
    home_key(22);

    /* what does not fit 16 tracks is left out and counted; the keys then have no track */
    static const lpc_t f4[4] = {{0, 0, 60}, {0, 1, 64}, {0, 2, 67}, {0, 3, 71}};
    lp_none();
    for (int k = 0; k < 5; k++) lp_make(16, 16, 1, 0, f4, 4);
    lv_arm();
    int n2 = lv_free();
    kdown(2); run(40); kup(2);                          /* (nothing playing: the key starts it, its note on row 0) */
    player_state(&st, 0);
    check((lp_cur(3) & 64) && lp_here() && st.lp_rest == 1, "RECORD LIVE: armed with nothing playing, a key starts it, the "
          "loops' replay running silent");
    press(B_PLAY); release(B_PLAY);                     /* (the loops on from their place: their start) */
    lv_switch(0, 0);
    run(30);
    tap(B_REC);
    lv_until(lv_ended);
    snprintf(t, sizeof t, "SONG %02X: 5 NOTES LEFT OUT", n2);
    snprintf(msg, sizeof msg, "RECORD LIVE: 16 tracks: 4 loops of 4 lanes fill them; the fifth loop's notes and the key's left out, "
             "counted (%s)", ui_toast_now() ? ui_toast_now() : "-");
    check(ui_song((uint8_t)n2, &s) && ui_pattern_tracks(s.order[0]) == 16 && lp_toast(t), msg);
    lv_drop(n2);
    player_stop(); run(100);

    /* it stops by itself: the edit memory full (nothing past it written), 256 positions; no song number free: refused */
    lp_none();
    static const lpc_t one[1] = {{0, 0, 60}};
    lp_make(16, 16, 1, 0, one, 1);
    int nd = 0;
    static uint8_t junk[4000];
    for (uint32_t sz = sizeof junk; sz >= 64; sz /= 2)  /* (the edit memory filled with records nothing reads, as far as
                                                         * 200 B from full) */
        while (store_edit_room() > sz + 200 && nd < 240 && store_edit_put(REC_DJ, (uint8_t)(0x10 + nd), 0, 1, junk, sz) == STORE_OK) nd++;
    int n3 = lv_free();
    lv_arm(); press(B_PLAY); release(B_PLAY);
    lv_until(lv_ended);
    snprintf(t, sizeof t, "SONG %02X STOPPED: MEMORY FOR EDITS IS FULL", n3);
    snprintf(msg, sizeof msg, "RECORD LIVE: the edit memory full: it stops (%s), nothing past it written (no song)",
             ui_toast_now() ? ui_toast_now() : "-");
    check(lp_toast(t) && !ui_song((uint8_t)n3, &s), msg);
    for (int k = 0; k < nd; k++) store_edit_forget(REC_DJ, (uint8_t)(0x10 + k), 0);
    player_stop(); run(100);
    /* every pattern number taken: it stops at its first pattern (256 positions, 32,768 rows, are not run here) */
    uint8_t pn[256];
    int np = 0;
    for (int k = 0; k <= 0xEF; k++)                     /* (empty patterns of 1 track, 1 row) */
        if (!ui_pattern_rows((uint8_t)k) && !ed_new_pattern((uint8_t)k, 1, 1, 1)) pn[np++] = (uint8_t)k;
    int n4 = lv_free();
    lv_arm(); press(B_PLAY); release(B_PLAY);
    lv_until(lv_ended);
    snprintf(t, sizeof t, "SONG %02X STOPPED: ALL 240 PATTERNS USED", n4);
    snprintf(msg, sizeof msg, "RECORD LIVE: no pattern number free: it stops (%s)", ui_toast_now() ? ui_toast_now() : "-");
    check(lp_toast(t) && !ui_song((uint8_t)n4, &s), msg);
    for (int k = 0; k < np; k++) store_edit_forget(REC_PATTERN, pn[k], 0);
    player_stop(); run(100);
    ui_edit_loops(loops_set_tempo(120), ui_edit_used());
    uint8_t sn[256];
    static uint8_t srec[95];
    int ns = 0;
    memset(srec, 0, sizeof srec);
    srec[12] = 120; srec[14] = 6; srec[15] = 4; srec[16] = 50; srec[19] = 64; srec[21] = 0xFF;
    for (int k = 0; k <= UI_SONG_LAST; k++) if (lv_free() == k && store_edit_put(REC_SONG, (uint8_t)k, 0, 1, srec, sizeof srec) == STORE_OK) sn[ns++] = (uint8_t)k;
    tap(B_SEL); menu_to("RECORD LIVE"); tap(B_SEL);
    check(!(lp_cur(3) & 32) && lp_toast("NO SONG NUMBER FREE"), "RECORD LIVE: every song number taken: refused (NO SONG NUMBER FREE)");
    for (int k = 0; k < ns; k++) store_edit_forget(REC_SONG, sn[k], 0);
}
/* RECORD LIVE's keys with the next loops MONO: a key over a held one as MONO LEGATO says (LEGATO: LEG, heard on the voice
 * held; GLIDE: 320; JUMP: a plain note) */
static void test_loops_live_mono(void)
{
    static const lpc_t a[2] = {{0, 0, 60}, {12, 0, REC_NOTE_OFF}};
    int k0 = ui_midi(0), k4 = ui_midi(4), n0, voices = 0, kt = -1;
    lp_none();
    lp_make(16, 16, 1, 0, a, 2);
    ui_edit_loops(loops_set_tempo(30), ui_edit_used());   /* (a row 500 ms) */
    ui_edit_loops(loops_set_quant(LQ_ROW), ui_edit_used());
    ui_edit_loops(loops_set_next_mono(1), ui_edit_used());
    ui_edit_loops(loops_set_legato(LG_LEGATO), ui_edit_used());
    loops_changed(1);
    lp_select(0);
    n0 = lv_free();
    lv_arm(); press(B_PLAY); release(B_PLAY); run(30);
    lv_at(1, 1); kdown(0);
    lv_at(3, 1); kdown(4);                              /* (LEGATO: row 3) */
    lv_at(4, 1);
    {
        pl_voice_info_t v[PL_VOICES];
        int n = player_voices_now(v, PL_VOICES);
        for (int i = 0; i < n; i++) voices += v[i].on && v[i].track == 0xFF;
    }
    kup(4); kup(0);
    ui_edit_loops(loops_set_legato(LG_GLIDE), ui_edit_used());
    lv_at(6, 1); kdown(0);
    lv_at(8, 1); kdown(4);                              /* (GLIDE: row 8) */
    lv_at(9, 1); kup(4); kup(0);
    ui_edit_loops(loops_set_legato(LG_JUMP), ui_edit_used());
    lv_at(11, 1); kdown(0);
    lv_at(13, 1); kdown(4);                             /* (JUMP: row 13) */
    lv_at(14, 1); kup(4); kup(0);
    lv_at(15, 1);
    tap(B_REC);
    run(10);
    ui_song_t s;
    int got = ui_song((uint8_t)n0, &s);
    uint8_t p0 = got ? s.order[0] : 0;
    rec_cell_t c;
    for (int t = 0; t < 16 && kt < 0; t++) if (ed_cell(p0, 1, t, &c) && c.note == k0) kt = t;
    check(got && kt >= 0 && voices == 1, "RECORD LIVE, MONO: the keys' track; LEGATO heard on the one voice held");
    check(ed_cell(p0, 3, kt, &c) && c.note == k4 && lv_cmd(p0, 3, kt, 0xD3) == 0 && lv_cmd(p0, 3, kt, 0x03) < 0,
          "RECORD LIVE, MONO LEGATO LEGATO: LEG with the note over the held one");
    check(ed_cell(p0, 8, kt, &c) && c.note == k4 && lv_cmd(p0, 8, kt, 0x03) == 0x20 && lv_cmd(p0, 8, kt, 0xD3) < 0,
          "RECORD LIVE, ... GLIDE: 320");
    check(ed_cell(p0, 13, kt, &c) && c.note == k4 && lv_cmd(p0, 13, kt, 0x03) < 0 && lv_cmd(p0, 13, kt, 0xD3) < 0 &&
          lv_cmd(p0, 13, kt, 0xC1) < 0, "RECORD LIVE, ... JUMP: a plain note");
    lv_drop(n0);
    player_stop(); run(100);
    ui_edit_loops(loops_set_next_mono(0), ui_edit_used());
    ui_edit_loops(loops_set_legato(LG_GLIDE), ui_edit_used());
    ui_edit_loops(loops_set_tempo(120), ui_edit_used());
    loops_changed(1);
}
static void test_loops(void)
{
    char msg[200], got[64];
    player_state_t st;
    loops_as_playground();
    lp_none();
    ui_edit_loops(loops_set_next_mono(0), ui_edit_used());
    ui_set_inst(UI_FIRST_INST);                         /* (F0:00, a DX7's) */
    lp_select(0);
    /* no loops: EDIT and LFO dark, EDIT says so; PLAY with nothing playing refused */
    run(50);
    check(leds[B_EDIT] == 0 && leds[B_LFO] == 0, "LOOPS: no loops: EDIT and LFO dark");
    tap(B_EDIT);
    check(lp_toast("NO LOOPS YET"), "LOOPS: EDIT with no loops: NO LOOPS YET");
    press(B_PLAY); release(B_PLAY);
    check(!lp_here() && lp_toast("REC, THEN PLAY: THE FIRST LOOP"), "LOOPS: PLAY with no loops, nothing playing: refused (REC, THEN PLAY: THE FIRST LOOP)");
    /* the first loop: from silence a key starts it with its note; REC closes it on a bar line */
    tap(B_REC);
    check((lp_cur(3) & 1) && !lp_here(), "LOOPS: REC on + NEW LOOP, nothing playing: armed, waiting");
    run(50);
    check(leds[B_REC] == LV_FULL || leds[B_REC] == 0, "LOOPS: REC blinks while armed");
    tap(B_REC);
    check(!(lp_cur(3) & 1) && lp_toast("CANCELLED"), "LOOPS: REC while armed: disarmed (CANCELLED)");
    tap(B_REC);
    kdown(7);
    run(30);
    check(lp_here() && loops_n() == 1 && (lp_cur(3) & 2) && lp_note(0, 0, 0) == ui_midi(7), "LOOPS: a key starts the loops and the "
          "first loop's take at once, its note on row 0");
    lp_at(0, 5); kup(7);
    check(lp_note(0, 1, 0) == REC_NOTE_OFF, "LOOPS: its note off on the nearest row, at least a row after it");
    lp_at(17, 1); kdown(9);
    check(lp_note(0, 17, 0) == ui_midi(9), "LOOPS: a note in bar 2");
    lp_at(17, 2); kup(9);
    lp_at(17, 4); press(B_REC); release(B_REC);
    check(loops_rows() == 16 && loops_base(0) == 16 && lp_toast("LOOP 00: 1 BAR") && lp_note(0, 0, 0) == ui_midi(7) &&
          loops_note_count(0) == 1, "LOOPS: REC up to a beat after a bar line: cut back to it (LOOP 00: 1 BAR), what came after "
          "it gone");
    check(loops_inst(0) == UI_FIRST_INST && loops_colour(0) == 0 && !loops_mono(0) && loops_sel() == 1,
          "LOOPS: the first loop: the current instrument, light red, POLY; the selection on + NEW LOOP");
    lp_none();
    tap(B_REC); kdown(4); run(40); kup(4);
    lp_at(20, 1); press(B_REC); release(B_REC);
    check(loops_rows() == 32 && lp_toast("LOOP 00: 2 BARS"), "LOOPS: REC well into bar 2: runs on to its end (LOOP 00: 2 BARS)");
    lp_none();
    tap(B_REC); press(B_PLAY); release(B_PLAY); run(60);
    check(lp_here() && (lp_cur(3) & 2), "LOOPS: armed, PLAY (COUNT-IN OFF): the loops and the take at once");
    press(B_REC); release(B_REC); run(60);
    check(!loops_n() && !loops_rows() && !lp_here() && lp_toast("NOTHING PLAYED: NO LOOP"), "LOOPS: nothing played: no loop "
          "(NOTHING PLAYED: NO LOOP)");

    /* QUANTIZE: where a note lands (detail 1), loop 00 recorded again, at 60 */
    static const lpc_t none[1] = {{0, 0, 0}};
    lp_make(16, 16, 1, 0, none, 0);
    ui_edit_loops(loops_set_tempo(60), ui_edit_used());
    lp_select(0);
    static const int QR[3][3] = {{5, 2, 2}, {9, 4, 4}, {15, 4, 5}}, Q2[2][3] = {{4, 5, 2}, {7, 1, 4}}, Q3[2][3] = {{4, 2, 2}, {7, 4, 4}};
    static const int Q4[2][3] = {{5, 5, 2}, {10, 1, 4}}, QT[2][3] = {{5, 3, 2}, {9, 0, 4}};
    lp_quant(LQ_ROW, QR, 3, got, sizeof got);
    snprintf(msg, sizeof msg, "QUANTIZE ROW: the nearest row (5.33 on 5, 9.67 on 10; 15.67 on row 0 for the next pass, lane 1): %s", got);
    check(!strcmp(got, "5,10,0/1"), msg);
    lp_quant(LQ_2_ROWS, Q2, 2, got, sizeof got);
    snprintf(msg, sizeof msg, "QUANTIZE 2 ROWS: the nearest line of 2 (4.83 on 4, 7.17 on 8): %s", got);
    check(!strcmp(got, "4,8"), msg);
    lp_quant(LQ_3_ROWS, Q3, 2, got, sizeof got);
    snprintf(msg, sizeof msg, "QUANTIZE 3 ROWS: the nearest line of 3 (4.33 on 3, 7.67 on 9): %s", got);
    check(!strcmp(got, "3,9"), msg);
    lp_quant(LQ_4_ROWS, Q4, 2, got, sizeof got);
    snprintf(msg, sizeof msg, "QUANTIZE 4 ROWS: the nearest line of 4 (5.83 on 4, 10.17 on 12): %s", got);
    check(!strcmp(got, "4,12"), msg);
    lp_quant(LQ_REAL, QT, 2, got, sizeof got);
    snprintf(msg, sizeof msg, "QUANTIZE REAL TIME: the row it falls in, a note delay of its tick (5.5: ED3; 9.0 none): %s", got);
    check(!strcmp(got, "5d3,9"), msg);
    tap(B_EDIT); kdown(0); lp_at(3, 3); kup(0);         /* (REAL TIME's note off: a key off at its tick) */
    check(lp_cmd(0, 3, 0, 0x14) == 3, "QUANTIZE REAL TIME: a note's end a key off at its tick (K03 on row 3)");
    press(B_REC); release(B_REC); player_stop(); run(100);
    ui_edit_loops(loops_set_quant(LQ_ROW), ui_edit_used());

    /* MONO: one lane, legato as MONO LEGATO says (detail 4) */
    lp_none();
    lp_make(16, 16, 1, 1, none, 0);
    ui_edit_loops(loops_set_tempo(60), ui_edit_used());
    ui_edit_loops(loops_set_legato(0), ui_edit_used());
    lp_select(0);
    tap(B_EDIT); kdown(0);                              /* (held) */
    lp_at(2, 1); kdown(4);
    check(lp_note(0, 2, 0) == ui_midi(4) && lp_cmd(0, 2, 0, 0x03) == 0x20 && lp_note(0, 2, 1) < 0,
          "LOOPS: MONO, GLIDE: a key over a held one, its note on the one lane with 320 (a glide)");
    lp_at(3, 1); kup(0);
    check(lp_note(0, 3, 0) < 0, "LOOPS: ... the held key let go: nothing (its note no longer sounds)");
    lp_at(4, 1); kup(4);
    check(lp_note(0, 4, 0) == REC_NOTE_OFF, "LOOPS: ... the sounding one let go: its note off");
    lp_at(6, 1); kdown(5); kdown(7);
    rec_cell_t c6;
    check(lp_note(0, 6, 0) == ui_midi(7) && loops_cell(0, 6, 0, &c6) && !c6.n && lp_note(0, 6, 1) < 0,
          "LOOPS: MONO, keys landing on the same row: the last registered is written");
    lp_at(7, 1); kup(5); lp_at(8, 1); kup(7);
    press(B_REC); release(B_REC); player_stop(); run(100);
    check(loops_lanes(0) == 1 && loops_mono(0), "LOOPS: ... the loop stays MONO, one lane");
    ui_edit_loops(loops_set_legato(1), ui_edit_used());
    tap(B_EDIT); kdown(0); lp_at(2, 1); kdown(4);
    check(lp_note(0, 2, 0) == ui_midi(4) && lp_cmd(0, 2, 0, 0x03) < 0, "LOOPS: MONO, JUMP: a plain new note");
    lp_at(3, 1); kup(4); kup(0); press(B_REC); release(B_REC); player_stop(); run(100);
    ui_edit_loops(loops_set_legato(LG_LEGATO), ui_edit_used());
    tap(B_EDIT); kdown(0); lp_at(2, 1); kdown(4);
    check(lp_note(0, 2, 0) == ui_midi(4) && lp_cmd(0, 2, 0, 0xD3) == 0 && lp_cmd(0, 2, 0, 0x03) < 0,
          "LOOPS: MONO, LEGATO: LEG with the new note, no glide");
    lp_at(3, 1); kup(4); kup(0); press(B_REC); release(B_REC); player_stop(); run(100);
    {
        uint8_t nums[256];
        int n = ui_carts(nums, 256), tb = -1;
        for (int i = 0; i < n && tb < 0; i++) if (ui_cart_kind(nums[i]) == ENG_TB303) tb = nums[i];
        ui_edit_loops(loops_set_legato(0), ui_edit_used());
        ui_edit_loops(loops_set_inst(0, (uint8_t)tb, 0), ui_edit_used());
        loops_changed(0);
        tap(B_EDIT); kdown(0); lp_at(2, 1); kdown(4);
        check(lp_cmd(0, 0, 0, 0xC1) == 0 && lp_note(0, 2, 0) == ui_midi(4) && lp_cmd(0, 2, 0, 0x03) < 0,
              "LOOPS: MONO, GLIDE on a TB-303: SLD on the note held, the new one plain");
        lp_at(3, 1); kup(4); kup(0); press(B_REC); release(B_REC); player_stop(); run(100);
    }

    /* POLY: up to 4 notes at once, a fifth neither heard nor written (detail 3) */
    lp_none();
    lp_make(16, 16, 1, 0, none, 0);
    ui_edit_loops(loops_set_tempo(60), ui_edit_used());
    lp_select(0);
    tap(B_EDIT); kdown(0);
    lp_at(2, 1);
    static const int CH[4] = {2, 4, 5, 7};
    for (int k = 0; k < 4; k++) kdown(CH[k]);
    run(30);
    {
        pl_voice_info_t v[PL_VOICES];
        int nv = player_voices_now(v, PL_VOICES), fifth = 0, third = 0;
        for (int i = 0; i < nv; i++) if (v[i].on && v[i].track == 16) { fifth |= v[i].note == ui_midi(7); third |= v[i].note == ui_midi(5); }
        check(lp_note(0, 2, 1) == ui_midi(2) && lp_note(0, 2, 2) == ui_midi(4) && lp_note(0, 2, 3) == ui_midi(5) && !fifth && third,
              "LOOPS: POLY: a chord a key a lane (lanes 1-3 by the keys' order) on the loop's track; the fifth note neither "
              "written nor heard");
        int any7 = 0;
        for (int r = 0; r < 16; r++) for (int l = 0; l < 4; l++) any7 |= lp_note(0, r, l) == ui_midi(7);
        check(!any7, "LOOPS: ... (nowhere in the loop)");
    }
    lp_at(3, 1);
    for (int k = 0; k < 4; k++) kup(CH[k]);
    kup(0);
    press(B_REC); release(B_REC); player_stop(); run(100);
    check(loops_lanes(0) == 4, "LOOPS: ... the loop's lanes: the most it holds at once (4)");

    /* a take on a new loop while the loops play: armed, it starts at their start, one pass */
    lp_none();
    static const lpc_t c60[1] = {{0, 0, 60}};
    lp_make(16, 16, 1, 0, c60, 1);
    lp_select(1);
    press(B_PLAY); release(B_PLAY); run(100);
    tap(B_REC);
    check((lp_cur(3) & 1) && loops_n() == 1, "LOOPS: REC on + NEW LOOP while the loops play: armed for their next start, no loop yet");
    run(60);
    check(count_px(80, 45, 240, 56, C_RED) > 20, "LOOPS: ... the placeholder's row says how long (ARMED: STARTS IN ..., red)");
    check(lp_wait(2, 1), "LOOPS: ... the take starts at the loops' start");
    player_state(&st, 0);
    check(loops_n() == 2 && loops_sel() == 1 && st.row < 3, "LOOPS: ... the new loop made then, selected");
    lp_at(4, 0); kdown(12); lp_at(6, 0); kup(12);
    check(lp_wait(2, 0) && loops_n() == 2 && loops_note_count(1) == 1 && lp_here(), "LOOPS: ... one pass and it stops by "
          "itself, the loops playing on");
    check(loops_sel() == 2 && loops_colour(1) == 1, "LOOPS: ... the selection on + NEW LOOP again; the next colour (yellow)");
    player_stop(); run(100);

    /* repeats and the cut drawn: base 4 x3 over 16 rows (then it rests); base 6 x3 cut at the length's end */
    lp_none();
    static const lpc_t r4[2] = {{0, 0, 60}, {2, 0, 62}}, r6[2] = {{0, 0, 64}, {4, 0, 65}};
    lp_make(16, 4, 3, 0, r4, 2);
    lp_make(16, 6, 3, 0, r6, 2);
    ui_render();
    shot("ui3_loops_repeats");
    {
        uint16_t col0 = g_rgb565(C_LRED), deep0 = g_rgb565(C_DEEP + C_LRED), deep1 = g_rgb565(C_DEEP + C_YELLOW);
        uint16_t ground = g_rgb565(C_DEEP + C_DGREY);
        check(px(111 + 8 + 4, 38) == col0 && px(111 + 40 + 4, 38) == deep0 && px(111 + 104, 38) == ground,
              "LOOPS: a strip: the base in the loop's colour, its repeats in the deep shade, nothing past them");
        check(px(111 + 112 + 4, 50) == deep1 && count_px(91, 33, 103, 45, C_LGREY) > 4, "LOOPS: a repeat cut at the length's end "
              "drawn to it; X3 by the mode");
    }

    /* re-record, peel */
    lp_none();
    static const lpc_t a0[1] = {{0, 0, 60}}, a2[1] = {{2, 0, 62}}, a4[1] = {{4, 0, 64}};
    lp_make(16, 16, 1, 0, a0, 1);
    lp_make(16, 16, 1, 1, a2, 1);
    lp_make(16, 16, 1, 0, a4, 1);
    ui_edit_loops(loops_set_mix(1, RM_LEVEL, 0x40), ui_edit_used());
    lp_select(1);
    tap(B_EDIT);
    check(!loops_has_notes(1) && (lp_cur(3) & 1) && lp_toast("LOOP 01 RE-RECORDING"), "LOOPS: EDIT: a re-record, its notes gone at "
          "once (LOOP 01 RE-RECORDING), armed");
    tap(B_REC);
    check(!(lp_cur(3) & 1) && !loops_has_notes(1) && loops_n() == 3 && loops_mono(1) && loops_colour(1) == 1,
          "LOOPS: REC: disarmed; its notes do not come back; its place, MONO, colour kept");
    tap(B_REC); kdown(3); run(40); kup(3); run(40);
    press(B_REC); release(B_REC); player_stop(); run(100);
    check(lp_note(1, 0, 0) == ui_midi(3) && loops_mix(1)[RM_LEVEL] == 0x40 && loops_sel() == 1,
          "LOOPS: a re-record's take: written into the loop, its mix kept, the selection on it");
    tap(B_LFO);
    check(loops_n() == 2 && loops_colour(1) == 2 && loops_mix(1)[RM_LEVEL] == 0x7F && loops_sel() == 1 && lp_toast("LOOP 01 PEELED"),
          "LOOPS: LFO: PEEL, the loop goes (LOOP 01 PEELED); the selection on the loop moved into its place");
    lp_select(2);
    tap(B_LFO);
    check(loops_n() == 1 && loops_colour(0) == 0, "LOOPS: on + NEW LOOP: LFO peels the last loop");
    tap(B_LFO);
    check(!loops_n() && !loops_rows(), "LOOPS: peeling the only loop takes the length with it");

    /* mutes and solos: CONTROL mode (SEQ), now and at the bar; HOME's combination */
    static const lpc_t m32[1] = {{8, 0, 64}};
    lp_make(16, 16, 1, 0, a0, 1);
    lp_make(16, 16, 1, 0, m32, 1);
    tap(B_SEQ);
    run(2100);
    check((lp_cur(3) & 4) && leds[B_SEQ] == LV_FULL && leds[LED_NOTE(UI_WHITE[0])] == LV_FULL && leds[LED_NOTE(UI_WHITE[2])] == 0 &&
          leds[LED_NOTE(UI_BLACK[3])] == LV_FULL && leds[LED_NOTE(UI_BLACK[9])] == LV_FULL,
          "LOOPS: SEQ: the keys CONTROL the loops, SEQ full; white key n full while loop n sounds, dark with none; OP4 (ROW) "
          "and POLY full");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);
    player_state(&st, 0);
    check(loops_muted(0) && (st.muted & 1) && lp_toast("LOOP 00 MUTED"), "LOOPS: stopped: a white key mutes its loop at once (LOOP 00 MUTED)");
    key(UI_BLACK[6], 1); key(UI_BLACK[6], 0);
    check(!loops_muted(0) && lp_toast("ALL LOOPS ON"), "LOOPS: PIT: all back on (ALL LOOPS ON)");
    key(UI_BLACK[0], 1); key(UI_BLACK[0], 0);
    check(loops_quant() == LQ_2_ROWS && lp_toast("QUANTIZE: 2 ROWS"), "LOOPS: OP1: QUANTIZE 2 ROWS");
    key(UI_BLACK[3], 1); key(UI_BLACK[3], 0);
    key(UI_BLACK[8], 1); key(UI_BLACK[8], 0);
    check(loops_next_mono() && lp_toast("NEXT LOOPS: MONO"), "LOOPS: MONO: the next loops MONO");
    key(UI_BLACK[9], 1); key(UI_BLACK[9], 0);
    key(UI_BLACK[7], 1); key(UI_BLACK[7], 0);
    check(lp_toast("SEQ: SOLO") && ui_screen_now()->seq && !strcmp(ui_screen_now()->seq(), "SOLO"), "LOOPS: the GLO key: SOLO (SEQ SOLO "
          "in the header)");
    key(UI_WHITE[1], 1); key(UI_WHITE[1], 0);
    player_state(&st, 0);
    check(loops_soloed(1) && (st.muted & 3) == 1, "LOOPS: a white key solos its loop: only the soloed sound");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);
    player_state(&st, 0);
    check(loops_soloed(0) && !loops_soloed(1) && (st.muted & 3) == 2 && lp_toast("LOOP 00 SOLO"),
          "LOOPS: another white key moves the solo to its loop (one solo at a time)");
    key(UI_WHITE[1], 1); key(UI_WHITE[1], 0);
    key(UI_WHITE[1], 1); key(UI_WHITE[1], 0); key(UI_BLACK[7], 1); key(UI_BLACK[7], 0);
    press(B_PLAY); release(B_PLAY); run(100);
    lp_at(1, 0);
    key(UI_WHITE[0], 1); run((uint32_t)ui_set[S_HOLD_DELAY] + 100); key(UI_WHITE[0], 0);
    check(!loops_muted(0) && lp_toast("LOOP 00 MUTED AT THE BAR") && (lp_cur(3) & 4), "LOOPS: playing, held: the mute waits for the "
          "bar line (LOOP 00 MUTED AT THE BAR)");
    lp_at(0, 1);
    run(100);
    player_state(&st, 0);
    check(loops_muted(0) && (st.muted & 1), "LOOPS: ... the bar line: muted (the player first, then the loops' record)");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);
    check(!loops_muted(0) && lp_toast("LOOP 00 ON"), "LOOPS: playing, a tap: at once (LOOP 00 ON)");
    key(UI_BLACK[7], 1); key(UI_BLACK[7], 0);           /* (SOLO) */
    key(UI_WHITE[1], 1); key(UI_WHITE[1], 0);
    key(UI_WHITE[0], 1); run((uint32_t)ui_set[S_HOLD_DELAY] + 100); key(UI_WHITE[0], 0);
    check(loops_soloed(1) && !loops_soloed(0) && lp_toast("LOOP 00 SOLO AT THE BAR"), "LOOPS: SOLO, playing, held: the solo moves "
          "at the bar line (LOOP 00 SOLO AT THE BAR)");
    lp_at(0, 1);
    run(100);
    player_state(&st, 0);
    check(loops_soloed(0) && !loops_soloed(1) && (st.muted & 3) == 2, "LOOPS: ... the bar line: loop 00 soloed alone");
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);
    check(!loops_soloed(0) && !loops_soloed(1) && lp_toast("LOOP 00 SOLO OFF"), "LOOPS: SOLO, a tap on the soloed loop: its solo "
          "off (LOOP 00 SOLO OFF)");
    key(UI_BLACK[7], 1); key(UI_BLACK[7], 0);           /* (MUTE again) */
    player_stop(); run(100);
    tap(B_SEQ);
    check(!(lp_cur(3) & 4) && lp_toast("KEYS: PLAY"), "LOOPS: SEQ again: the keys PLAY");
    home_key(UI_WHITE[1]);
    check(loops_muted(1) && lp_toast("LOOP 01 MUTED"), "LOOPS: HOME + white key: its loop muted at once");
    home_key(UI_WHITE[1]);
    press(B_HOME); key(UI_WHITE[0], 1); run((uint32_t)ui_set[S_HOLD_DELAY] + 100); key(UI_WHITE[0], 0); release(B_HOME); run(200);
    check(loops_soloed(0) && lp_toast("LOOP 00 SOLO"), "LOOPS: HOME + white key held: soloed");
    ui_edit_loops(loops_set_soloed(0, 0), ui_edit_used());
    ui_edit_loops(loops_set_next_mono(0), ui_edit_used());
    ui_edit_loops(loops_set_quant(LQ_ROW), ui_edit_used());
    loops_changed(0);

    /* the pattern recorder (ARP), its preview; a first loop with TIMES, a later one filling the length, into a loop */
    lp_none();
    lp_select(0);
    tap(B_ARP);
    run(60);
    player_state(&st, 0);
    check(ui_top() && ui_top()->kind == OV_OTHER && (lp_cur(3) & 128) && st.lp_alone && leds[B_ARP] == LV_FULL,
          "LOOPS: ARP: the pattern recorder, its preview playing alone, ARP full");
    {   /* OCT on the 4th row moves that row's note alone: rows 1-2 (filled) keep theirs, the first row's C included */
        int lo0, hi0, lo1, hi1;
        rp_notes(&lo0, &hi0);
        turn(K_SELECT, 3);
        tap(B_OCTP);
        rp_notes(&lo1, &hi1);
        tap(B_OCTM);
        snprintf(msg, sizeof msg, "LOOPS: the pattern recorder: OCT+ on the 4th row leaves the other rows' notes (heard %d-%d, "
                 "then %d-%d)", lo0, hi0, lo1, hi1);
        check(lo0 > 0 && lo0 == lo1 && hi0 == hi1, msg);
    }
    tap(B_ARP);
    player_state(&st, 0);
    check(!st.lp_alone, "LOOPS: ARP: the preview stops (the loops stopped)");
    /* (what is set stays: the shots left row 0 with steps 1, 5, 9, 13 on, row 1 with 3, 7, 11, 15, 16, row 1 picked) */
    turn(K_SELECT, -3); turn(K_SELECT, 1);
    for (int k = 0; k < 5; k++) { key(UI_WHITE[LP_S2[k]], 1); key(UI_WHITE[LP_S2[k]], 0); }   /* (row 1 empty again) */
    turn(K_SELECT, -1);
    turn(K_K4, 3);
    tap(B_SEL); key(UI_WHITE[2], 1); key(UI_WHITE[2], 0);
    tap(B_REC);
    run(60);
    snprintf(msg, sizeof msg, "LOOPS: REC with no loops: the first loop, its base the 16 steps, x4, 4 bars (%d loops, %d rows, base "
             "%d x%d, %d notes, %s)", loops_n(), loops_rows(), loops_base(0), loops_repeats(0), loops_note_count(0),
             ui_toast_now() ? ui_toast_now() : "-");
    check(!ui_top() && loops_n() == 1 && loops_rows() == 64 && loops_base(0) == 16 && loops_repeats(0) == 4 &&
          lp_note(0, 0, 0) == ui_midi(UI_WHITE[2]) && lp_note(0, 12, 0) == ui_midi(UI_WHITE[2]) && loops_note_count(0) == 4 &&
          lp_toast("PATTERN: LOOP 00, 4 BARS"), msg);
    check(!lp_here(), "LOOPS: ... the preview stopped: nothing starts");
    tap(B_ARP); tap(B_ARP);
    turn(K_K2, -1);                                     /* (12 steps: row 0's 1, 5, 9 within them) */
    turn(K_SELECT, 1);
    press(B_SEL); key(UI_WHITE[4], 1); key(UI_WHITE[4], 0); release(B_SEL); run(200);
    check(!ui_top() || ui_top()->kind == OV_OTHER, "LOOPS: SEL held + a key: the row's note at once, no menu");
    key(UI_WHITE[2], 1); key(UI_WHITE[2], 0);
    tap(B_GLO);
    check(lp_toast("NOTES A STEP LONG"), "LOOPS: GLO: the notes a step long (NOTES A STEP LONG)");
    tap(B_REC);
    snprintf(msg, sizeof msg, "LOOPS: REC: a new loop of 2 lanes (POLY), its 12 steps x6, the last repeat cut (base %d x%d, %d lanes, %s)",
             loops_base(1), loops_repeats(1), loops_lanes(1), ui_toast_now() ? ui_toast_now() : "-");
    check(loops_n() == 2 && loops_base(1) == 12 && loops_repeats(1) == 6 && loops_lanes(1) == 2 && !loops_mono(1) &&
          loops_colour(1) == 1 && lp_toast("PATTERN: LOOP 01, 4 BARS") && lp_note(1, 2, 1) == ui_midi(UI_WHITE[4]), msg);
    check(lp_note(1, 1, 0) == REC_NOTE_OFF && lp_note(1, 3, 1) == REC_NOTE_OFF && lp_note(1, 5, 0) == REC_NOTE_OFF,
          "LOOPS: A STEP LONG: a note off a step after each note whose next step is off");
    lp_select(0);
    tap(B_ARP);
    check(ui_top() && ui_top()->kind == OV_OTHER, "LOOPS: ARP on a loop: the recorder, into it");
    tap(B_ARP);
    key(UI_WHITE[2], 1); key(UI_WHITE[2], 0);           /* (every step off: row 1's, then row 0's) */
    turn(K_SELECT, -1);
    for (int k = 0; k < 12; k += 4) { key(UI_WHITE[k], 1); key(UI_WHITE[k], 0); }
    tap(B_REC);
    check(ui_top() && ui_top()->kind == OV_OTHER && lp_toast("NO STEPS ON"), "LOOPS: no step on: REC refused (NO STEPS ON)");
    key(UI_WHITE[3], 1); key(UI_WHITE[3], 0);
    tap(B_REC);
    check(!ui_top() && loops_n() == 2 && loops_colour(0) == 0 && loops_mono(0) && loops_note_count(0) == 1 &&
          lp_toast("PATTERN INTO LOOP 00"), "LOOPS: REC on a loop: its notes replaced, its place and colour kept, one row filled: "
          "MONO (PATTERN INTO LOOP 00)");
    press(B_PLAY); release(B_PLAY); run(100);
    tap(B_ARP);
    player_state(&st, 0);
    check(ui_top() && ui_top()->kind == OV_OTHER && st.playing && !st.lp_alone, "LOOPS: the loops playing: the preview in their time "
          "(not alone)");
    press(B_PLAY); release(B_PLAY); run(100);
    player_state(&st, 0);
    check(!st.playing && st.lp_alone, "LOOPS: PLAY pausing the loops: the preview goes on alone");
    press(B_PLAY); release(B_PLAY); run(100);
    player_state(&st, 0);
    check(st.playing && !st.lp_alone && st.what == PL_LOOPS, "LOOPS: PLAY starting them: the preview with them again");
    tap(B_HOME);
    check(!ui_top() && loops_n() == 2, "LOOPS: HOME: closed, nothing written");
    player_stop(); run(100);

    /* DOUBLE, the properties' LENGTH and GRID, CLEAR LOOPS, APPLY INSTRUMENT, REVERT */
    lp_none();
    static const lpc_t d40[1] = {{10, 0, 62}};
    lp_make(16, 16, 1, 0, a0, 1);
    lp_make(16, 12, 2, 0, d40, 1);
    tap(B_SEL); check(menu_to("DOUBLE"), "LOOPS' menu: DOUBLE"); tap(B_SEL);
    check(loops_rows() == 32 && loops_repeats(0) == 2 && loops_base(0) == 16 && loops_base(1) == 16 && loops_repeats(1) == 2 &&
          lp_note(1, 10, 0) == 62 && loops_note_count(1) == 1 && lp_toast("DOUBLED: 2 BARS"),
          "LOOPS: DOUBLE: a loop filling the length doubles its repeats, one cut at the end written out first (DOUBLED: 2 BARS)");
    ui_edit_loops(loops_set_rows(128), ui_edit_used());
    tap(B_SEL); menu_to("DOUBLE"); tap(B_SEL);
    check(loops_rows() == 128 && lp_toast("DOUBLE: AT MOST 8 BARS"), "LOOPS: DOUBLE past 128 rows: refused (DOUBLE: AT MOST 8 BARS)");
    ui_edit_loops(loops_set_rows(32), ui_edit_used());
    loops_changed(1);
    tap(B_GLO);
    check(ui_top() && ui_top()->kind == OV_PROPS, "LOOPS: GLO: LOOPS PROPERTIES");
    turn(K_SELECT, 3); turn(K_K1, 1);
    check(lp_toast("GRID: ONLY BEFORE THE FIRST LOOP") && loops_grid() == 0, "LOOPS: GRID with loops: GRID: ONLY BEFORE THE FIRST LOOP");
    turn(K_SELECT, -1); turn(K_K1, -1);
    check(!strcmp(asking(), "CUT TO 1 BAR?"), "LOOPS: LENGTH shorter with notes past it: asks (CUT TO 1 BAR? / NOTES IN THE BARS CUT GO)");
    tap(B_OCTM);
    check(loops_rows() == 32, "LOOPS: ... KEEP: nothing cut");
    turn(K_K1, -1); tap(B_OCTP);
    check(loops_rows() == 16 && loops_repeats(0) == 1 && loops_base(1) == 16 && loops_repeats(1) == 1, "LOOPS: ... CUT: 1 bar, each loop cut there");
    turn(K_K1, 1);
    check(loops_rows() == 32 && ui_top() && ui_top()->kind == OV_PROPS, "LOOPS: LENGTH longer: silence added, no question");
    turn(K_K1, -1);
    check(loops_rows() == 16 && ui_top() && ui_top()->kind == OV_PROPS, "LOOPS: LENGTH shorter with no note past it: no question");
    tap(B_GLO);
    lp_select(0);
    ui_set_inst((uint16_t)(lp_ju << 8));
    tap(B_SEL); check(menu_to("APPLY INSTRUMENT"), "LOOPS' menu on a loop: APPLY INSTRUMENT"); tap(B_SEL);
    {
        char t[40], nm[16];
        ui_inst_name((uint16_t)(lp_ju << 8), nm);
        snprintf(t, sizeof t, "LOOP 00: %02X:00 %s", lp_ju, nm);
        check(loops_inst(0) == (uint16_t)(lp_ju << 8) && lp_toast(t), "LOOPS: APPLY INSTRUMENT: the loop takes the current one "
              "(LOOP 00: 07:00 ...)");
    }
    ui_set_inst(UI_FIRST_INST);
    tap(B_SEL); check(menu_to("CLEAR LOOPS"), "LOOPS' menu: CLEAR LOOPS"); tap(B_SEL);
    check(!strcmp(asking(), "CLEAR ALL 2 LOOPS?"), "LOOPS: CLEAR LOOPS asks first (CLEAR ALL 2 LOOPS? / THE NEXT ONE SETS THE LENGTH)");
    tap(B_OCTP);
    check(!loops_n() && !loops_rows() && lp_toast("LOOPS CLEARED"), "LOOPS: ... CLEAR: every loop gone, the length too (LOOPS CLEARED)");
    lp_make(16, 16, 1, 0, a0, 1);
    tap(B_SEL); check(menu_to("REVERT"), "LOOPS' menu with LOOPS unsaved: REVERT"); tap(B_SEL);
    check(!strcmp(asking(), "REVERT LOOPS TO SAVED?"), "LOOPS: REVERT asks first (REVERT LOOPS TO SAVED?)");
    tap(B_OCTP);
    check(!loops_n() && lp_toast("REVERTED") && !ui_edit_unsaved(REC_LOOPS, 0, 0), "LOOPS: ... REVERT: LOOPS as saved (none), nothing "
          "of LOOPS unsaved");
    tap(B_SEL); check(menu_to("RECORD LIVE"), "LOOPS' menu: RECORD LIVE"); tap(B_SEL);
    check(lp_toast("NO LOOPS YET"), "LOOPS: RECORD LIVE with no loops: NO LOOPS YET");

    /* RECORD LIVE armed (its recording comes later): REC blinks, the changes of the loops refused */
    lp_make(16, 16, 1, 0, a0, 1);
    check(lv_arm(), "LOOPS: SEL, RECORD LIVE: its panel; its REC arms it");
    tap(B_LFO);
    check(loops_n() == 1 && lp_toast("RECORDING A SONG"), "LOOPS: a song armed: PEEL refused (RECORDING A SONG)");
    tap(B_SEL); check(menu_to("CANCEL RECORD LIVE"), "LOOPS' menu while armed: CANCEL RECORD LIVE"); tap(B_SEL);
    check(!(lp_cur(3) & 32) && lp_toast("CANCELLED"), "LOOPS: CANCEL RECORD LIVE: disarmed (CANCELLED)");
    test_loops_patterns();
    test_loops_live();
    test_loops_live_mono();
    lp_none();
    lp_make(16, 16, 1, 0, a0, 1);

    /* K1-K4 the selected loop's mix (on + NEW LOOP the next loop's), ALGORITHM the tempo; ENV the cartridges */
    lp_select(0);
    int lv0 = loops_mix(0)[RM_LEVEL], t0 = loops_tempo();
    turn(K_K1, -2);
    check(loops_mix(0)[RM_LEVEL] == lv0 - 2 && ui_knob_now() && !strncmp(ui_knob_now(), "LEVEL ", 6), "LOOPS: K1: the loop's LEVEL (shown "
          "in the header)");
    lp_select(1);
    turn(K_K4, 3);
    check(loops_mix(1)[RM_REVERB] == 3 && loops_mix(0)[RM_REVERB] == 0, "LOOPS: K4 on + NEW LOOP: the next loop's REVERB SEND");
    turn(K_ALG, 1);
    check(loops_tempo() == t0 + 1 && ui_knob_now() && !strncmp(ui_knob_now(), "TEMPO ", 6), "LOOPS: ALGORITHM: TEMPO");
    turn(K_ALG, -1); turn(K_K4, -3);
    tap(B_ENV);
    check(ui_top() && ui_top()->kind == OV_LIST, "LOOPS: ENV: the cartridges (NEW LOOP: A CARTRIDGE)");
    uint16_t in0 = ui_cur.inst;
    turn(K_SELECT, 1); tap(B_ENV);
    check(!ui_top() && ui_cur.inst >> 8 != in0 >> 8, "LOOPS: ENV again: the current instrument the picked cartridge's first");
    ui_set_inst(UI_FIRST_INST);
    /* PLAY held: stop, back to the start, as on every screen */
    if (!lp_here()) { tap(B_PLAY); run(300); }
    press(B_PLAY); run((uint32_t)ui_set[S_HOLD_DELAY] + 100); release(B_PLAY); run(100);
    {
        player_state_t st;
        player_state(&st, 0);
        check(!st.playing && st.row == 0, "LOOPS: PLAY held: stopped, back to the start");
    }
    tap(B_PLAY); run(300);
    check(lp_here(), "LOOPS: ... PLAY again: the loops play");
    press(B_PLAY); run((uint32_t)ui_set[S_HOLD_DELAY] + 100); release(B_PLAY); run(100);   /* (stopped again for what follows) */
    /* leaving LOOPS ends a take, as REC does; FX does not */
    tap(B_REC); kdown(2); run(40); kup(2);
    tap(B_FX);
    check(screen_is("FX") && lp_here(), "LOOPS: FX from a take: FX's screen, the loops playing on");
    tap(B_FX);
    check(screen_is("LOOPS") && (lp_cur(3) & 2), "LOOPS: ... FX again: the take still recording");
    home_key(8);
    home_key(22);
    check(!(lp_cur(3) & 2) && loops_n() == 2 && loops_has_notes(1) && lp_here(), "LOOPS: leaving for another screen ended the take "
          "(its notes kept, the loops playing on)");
    /* going on elsewhere from LOOPS' FX leaves LOOPS then: a take armed (the loops playing) is disarmed there too */
    tap(B_REC);
    check(lp_cur(3) & 3, "LOOPS: REC again: a take armed or recording");
    tap(B_FX);
    home_key(8);
    check(!screen_is("FX") && !screen_is("LOOPS"), "LOOPS: ... FX, then another screen from that FX");
    home_key(22);
    check(screen_is("LOOPS") && !(lp_cur(3) & 3), "LOOPS: ... back: the take gone (LOOPS left from its FX)");
    lp_none();
    lp_select(0);
    ui_edit_revert(REC_DX7, (uint8_t)lp_dx, 0);         /* (the shots' cartridges gone again) */
    ui_edit_revert(REC_JUNO, (uint8_t)lp_ju, 0);
    ui_edit_revert(REC_LOOPS, 0, 0);
    loops_revert();
    loops_changed(1);
    ui_set_inst(UI_FIRST_INST);                         /* (the current instrument the checks after these play: F0:00) */
    (void)msg;
}
/* SAVE keeps the whole LOOPS setup (specs/ui/screens/loops.md#saving): the loops (notes, instruments, colours, mixes, MONO
 * / POLY, mutes and solos), the length, the properties, the next loop's mix and the next loops' mode, the selection; read
 * again after a power cycle (the flash read anew), nothing playing. Then no loops again, saved (after the suite's own
 * save: nothing else is saved here that was not) */
static void test_loops_save(void)
{
    static const lpc_t a[2] = {{0, 0, 60}, {4, 1, 64}}, b[1] = {{2, 0, 62}};
    home_key(22);
    player_stop(); run(100);
    lp_make(32, 32, 1, 0, a, 2);
    lp_make(32, 8, 4, 1, b, 1);
    loops_set_muted(1, 1);
    loops_set_soloed(0, 1);
    loops_set_mix(0, RM_PAN, 0x40);
    loops_set_mix(2, RM_REVERB, 0x22);                  /* (the next loop's) */
    loops_set_tempo(140);
    loops_set_swing(60);
    loops_set_quant(LQ_3_ROWS);
    loops_set_legato(1);
    loops_set_countin(2);
    loops_set_next_mono(1);
    ui_edit_loops(LP_OK, ui_edit_used());               /* (made here, not by the screen: the unsaved changes told) */
    loops_changed(1);
    lp_select(1);
    tap(B_SAVE);
    run(3000);
    check(!ui_edit_unsaved(REC_LOOPS, 0, 0), "LOOPS saved: nothing of LOOPS unsaved");
    store_mount();                                      /* (as after a power cycle: the flash read again) */
    player_store_changed();
    run(100);
    rec_loops_t l;
    int got = !rec_loops(&l, 0) && l.n == 2 && l.head[6] == 1;
    check(got && loops_n() == 2 && loops_rows() == 32 && loops_base(0) == 32 && loops_base(1) == 8 && loops_repeats(1) == 4 &&
          loops_lanes(0) == 2 && !loops_mono(0) && loops_mono(1) && loops_colour(1) == 1 && loops_muted(1) &&
          !(l.head[REC_LOOPS_TABLE + 2] & 0x40) && loops_mix(0)[RM_PAN] == 0x40 && loops_mix(2)[RM_REVERB] == 0x22 &&
          lp_note(0, 0, 0) == 60 && lp_note(0, 4, 1) == 64 && lp_note(1, 2, 0) == 62,
          "LOOPS after a power cycle: the loops, their notes, mixes and mutes, the selection (01); loop 00's solo not saved");
    check(loops_tempo() == 140 && loops_swing() == 60 && loops_quant() == LQ_3_ROWS && loops_legato() == 1 && loops_countin() == 2 &&
          loops_next_mono() && !lp_here(), "LOOPS after a power cycle: the properties and the next loops' mode; nothing playing");
    lp_none();
    loops_set_mix(0, RM_REVERB, 0);
    loops_set_tempo(120);
    loops_set_swing(50);
    loops_set_quant(LQ_ROW);
    loops_set_legato(0);
    loops_set_countin(0);
    loops_set_next_mono(0);
    ui_edit_loops(LP_OK, ui_edit_used());
    loops_changed(1);
    tap(B_SAVE);
    run(3000);
    check(!ui_edit_unsaved(REC_LOOPS, 0, 0) && !loops_n(), "LOOPS: no loops again, saved");
    home_key(8);
    run(100);
}

/* ------------------------------------------------- DJ as the playground's shots --- */
/* tests/playground/screens.js' DJ part, press for press and wait for wait, under the same shot names: ui_shots.js
 * then compares the two screens state by state (a playing deck's place can differ by a few of the two clocks' ms).
 * DJ is reset after it, as at first. */
static int song_named(const char *name)
{
    for (int n = 0; n < 0xF0; n++) {
        ui_song_t s;
        if (ui_song((uint8_t)n, &s) && !strcmp(s.name, name)) return n;
    }
    return -1;
}
static void dj_list_to(int song) { turn(K_SELECT, -60); turn(K_SELECT, song); }
static void dj_as_playground(void)
{
    home_key(10);
    shot("33_dj_empty");
    dj_list_to(song_named("TRANCE LOOP")); key(8, 1); key(8, 0);
    dj_list_to(song_named("BREAKS LOOP")); key(10, 1); key(10, 0);
    key(1, 1); key(1, 0); key(13, 1); key(13, 0);
    run(2600);
    key(25, 1); key(25, 0);                             /* LOOPS: the right's PATTERN, the left's 1 BAR */
    key(UI_WHITE[8], 1); key(UI_WHITE[8], 0); key(UI_WHITE[3], 1); key(UI_WHITE[3], 0);
    turn(K_K3, -12); turn(K_K2, 20); key(5, 1); key(5, 0); turn(K_ALG, 12); run(1300);
    dj_deck_key(0, 2); home_key(UI_WHITE[9]);           /* (the left's KEY +2, the right's track 01 muted; VOLUMEs -6, +3) */
    tap(B_ENV); turn(K_K2, -6); turn(K_K3, 3); tap(B_ENV);
    shot("33b_dj_decks");                               /* (the filters: the right's LP on, the left's HP set, off) */
    dj_deck_key(0, 0); home_key(UI_WHITE[9]);
    dj_deck(0)[DK_VOL] = 0; dj_deck(1)[DK_VOL] = 0; dj_deck_volume(0, 0);
    tap(B_HOME); shot("33c_dj_home"); tap(B_HOME);
    tap(B_GLO); shot("33d_dj_props"); tap(B_GLO);
    tap(B_FX); shot("33e_dj_fx_live"); tap(B_SEQ); shot("33f_dj_fx_edit"); tap(B_SEQ); tap(B_FX);
    press(B_LFO); run(670); shot("33fa_dj_lfo_layer"); release(B_LFO); run(170);   /* LFO held: the pads' map */
    /* the LFO pads: the crossfader's L TO R, the right filter's, FX */
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0); key(UI_WHITE[2], 1); key(UI_WHITE[2], 0);
    key(UI_WHITE[8], 1); key(UI_WHITE[8], 0); key(UI_WHITE[12], 1); key(UI_WHITE[12], 0); run(1300);
    shot("33g_dj_lfo");
    key(UI_WHITE[8], 1); turn(K_ALG, -2); shot("33h_dj_lfo_speed"); key(UI_WHITE[8], 0);
    key(UI_WHITE[15], 1); key(UI_WHITE[15], 0); tap(B_LFO); run(1300);
    key(25, 1); key(25, 0);                             /* PLAY's pads: CUE held, OP1; MOVE held, 2 bars on; BRAKE held */
    key(UI_WHITE[1], 1); run(170); key(1, 1); key(1, 0); shot("33i_dj_cue_play"); key(UI_WHITE[1], 0); run(270);
    dj_deck_state_t ds;
    dj_deck_state(0, &ds);
    check(ds.playing, "DJ: CUE held, then the deck's PLAY key: let go, it plays on");
    check(dj_deck(0)[DK_LOOP_POS] == dj_deck(0)[DK_CUE_POS] && dj_deck(0)[DK_LOOP_ROW] == dj_deck(0)[DK_CUE_ROW],
          "DJ: CUE takes the deck's REPEAT 1 BAR along to the cue's bar");
    key(UI_WHITE[3], 1); turn(K_K1, 2); shot("33j_dj_move"); key(UI_WHITE[3], 0); run(770);
    key(UI_WHITE[4], 1); run(870); shot("33k_dj_brake"); key(UI_WHITE[4], 0); run(270);
    press(B_HOME); tap(B_LFO); release(B_HOME);
    for (int k = 0; k < 4; k++) {
        char nm[24];
        snprintf(nm, sizeof nm, "33%c_dj_help%d", "lmno"[k], k + 1);
        shot(nm);
        turn(K_SELECT, 1);
    }
    tap(B_HOME);
    key(1, 1); key(1, 0); run(300);                     /* OP1: paused away from its cue; JUMP: from the cue, playing */
    dj_deck_state(0, &ds);
    int was_row = ds.row;
    key(UI_WHITE[2], 1); key(UI_WHITE[2], 0); run(60);
    for (int i = 0; i < 300; i++) { dj_deck_state(0, &ds); if (ds.playing) break; run(10); }   /* (synced: from the
                                                         * other deck's bar line, as its PLAY key) */
    run(100);
    dj_deck_state(0, &ds);
    check(was_row >= dj_deck(0)[DK_CUE_ROW] + 8 && ds.playing && ds.pos == dj_deck(0)[DK_CUE_POS] &&
          ds.row < dj_deck(0)[DK_CUE_ROW] + 4, "DJ: JUMP on a paused deck plays from its cue");
    tap(B_GLO); turn(K_SELECT, 10); tap(B_EDIT); tap(B_OCTP);   /* RESET DJ, yes */
    check(dj_deck(0)[DK_SONG] == DJ_NO_SONG && dj_deck(1)[DK_SONG] == DJ_NO_SONG && !dj_xf(), "DJ: RESET DJ empties both decks");
    home_key(8);
}

/* --------------------------------------------------------------- COUNT-IN --- */
/* specs/ui/transport.md#count-in: the EDITOR's take and LOOPS' recording after 1 or 2 bars of the count-in's own
 * click, timed by the audio; KEY arms; PLAY, REC, HOME cancel */
static int song_bpm10(void)
{
    ui_song_t s;
    int d = ui_song(ui_cur.song, &s) ? s.ticks * s.rpb : 0;
    return d ? s.tempo * 240 / d : 1200;
}
static uint32_t until_playing(uint32_t most)            /* ms until the player plays (most + 1: never) */
{
    player_state_t st;
    for (uint32_t t = 0; t <= most; t += 5) {
        player_state(&st, 0);
        if (st.playing) return t;
        run(5);
    }
    return most + 1;
}
/* PLAYBACK's K4, the playback speed (specs/ui/screens/playback.md): turned in BPM, kept as a share of the song's
 * own tempo, heard at once; LFO back to 100 %; another song plays at 100 % */
static void test_speed(void)
{
    player_state_t st;
    ui_song_t s;
    int song = ui_cur.song, other = -1;
    for (int n = 0; n < 0xF0 && other < 0; n++) if (n != song && ui_song((uint8_t)n, &s) && s.npos) other = n;
    if (!ui_song((uint8_t)song, &s) || other < 0) { check(0, "PLAYBACK speed: two songs"); return; }
    int own = (s.tempo * 2400 / (s.ticks * s.rpb) + 5) / 10;
    home_key(8);                                        /* (PLAYBACK) */
    check(screen_is("PLAYBACK"), "PLAYBACK speed: on PLAYBACK");
    ui_play_song((uint8_t)song, 0);
    run(500);
    player_state(&st, 0);
    int b0 = st.bpm10;
    turn(K_K4, -10);                                    /* 10 BPM slower */
    run(100);
    player_state(&st, 0);
    uint32_t want = ((uint32_t)(own - 100) << 16) / (uint32_t)own;
    char k[40];
    int c = (own - 100 - own) * 1000 / own;
    snprintf(k, sizeof k, "K4 TEMPO %d.%d BPM -%d.%d%%", (own - 100) / 10, (own - 100) % 10, -c / 10, -c % 10);
    check(st.playing && st.tempo_share == want, "PLAYBACK speed: K4 -10, the share 10 BPM under the song's");
    check(ui_knob_now() && !strcmp(ui_knob_now(), k), "PLAYBACK speed: the header in BPM and % (DJ's words)");
    int b1 = st.bpm10, w1 = (int)(((uint32_t)b0 * want + 32768u) >> 16);
    check(b1 >= w1 - 1 && b1 <= w1 + 1, "PLAYBACK speed: heard at once, the tempo now scaled (its commands too)");
    printf("  PLAYBACK speed: %d.%d BPM -> %d.%d (the share %u, %s)\n", b0 / 10, b0 % 10, b1 / 10, b1 % 10, (unsigned)st.tempo_share,
           ui_knob_now() ? ui_knob_now() : "-");
    tap(B_PLAY); run(300);                              /* a pause and a new start: the share stays */
    tap(B_PLAY); run(300);
    player_state(&st, 0);
    check(st.playing && st.tempo_share == want && st.bpm10 == b1, "PLAYBACK speed: kept through a pause and PLAY");
    turn(K_K3, -6);                                     /* K3: the songs' volume */
    run(100);
    player_state(&st, 0);
    check(st.vol_db == -6 && ui_knob_now() && !strcmp(ui_knob_now(), "K3 VOLUME -6 DB"), "PLAYBACK volume: K3 -6, -6 DB");
    tap(B_LFO);                                         /* LFO: the last turned back, K3's */
    run(100);
    player_state(&st, 0);
    check(st.vol_db == 0 && st.tempo_share == want && ui_knob_now() && !strcmp(ui_knob_now(), "K3 VOLUME 0 DB"),
          "PLAYBACK: LFO resets the last knob turned (the volume)");
    turn(K_K4, 1); turn(K_K4, -1);
    tap(B_LFO);
    run(100);
    player_state(&st, 0);
    check(st.tempo_share == 65536u && st.bpm10 == b0 && ui_knob_now() && strstr(ui_knob_now(), "+0.0%"),
          "PLAYBACK speed: LFO after K4 back to 100 %");
    {                                                   /* the range (specs/ui/screens/dj.md#tempo-and-sync): 3 to
                                                         * 600 BPM whatever the song's own, its rows at that pace */
        int lo, hi;
        ui_tempo_range(&s, &lo, &hi);
        press(B_SEL);                                   /* (held: 10 BPM a click) */
        turn(K_K4, -(own / 100 + 2));
        release(B_SEL);
        run(100);
        player_state(&st, 0);
        uint32_t r0 = st.rows_played;
        check(lo == 30 && st.bpm10 >= 29 && st.bpm10 <= 31 && !ui_overlay_count() && ui_knob_now()
              && !strncmp(ui_knob_now(), "K4 TEMPO 3.0 BPM -", 18), "PLAYBACK speed: down to 3 BPM, whatever the song's own");
        run(10500);
        player_state(&st, 0);
        uint32_t slow = st.rows_played - r0;
        press(B_SEL);
        turn(K_K4, (hi - lo) / 100 + 2);
        release(B_SEL);
        run(100);
        player_state(&st, 0);
        r0 = st.rows_played;
        check(hi == 6000 && st.bpm10 >= 5999 && st.bpm10 <= 6001 && ui_knob_now()
              && !strncmp(ui_knob_now(), "K4 TEMPO 600.0 BPM +", 20), "PLAYBACK speed: up to 600 BPM");
        run(1000);
        player_state(&st, 0);
        uint32_t fast = st.rows_played - r0, want = 600u * s.rpb / 60u;
        printf("  PLAYBACK speed: 3 BPM, %u rows in 10.5 s; 600 BPM, %u rows in 1 s (%u a second)\n", (unsigned)slow,
               (unsigned)fast, (unsigned)want);
        check(slow >= 1 && slow <= 3 && fast + 2 >= want && fast <= want + 2,
              "PLAYBACK speed: the rows at that pace (a row 5 s at 3 BPM, 40 a second at 600)");
        tap(B_LFO);                                     /* (back to 100 %) */
        run(100);
    }
    turn(K_K4, 3);
    turn(K_K3, -3);
    run(100);
    player_state(&st, 0);
    uint32_t s3 = st.tempo_share;
    ui_play_song((uint8_t)other, 0);                    /* another song: both kept */
    run(300);
    player_state(&st, 0);
    check(s3 > 65536u && st.tempo_share == s3 && st.vol_db == -3, "PLAYBACK: another song keeps the speed and the volume");
    int64_t loud = listen(1000);                        /* the volume heard: OFF silences the song, not the keys */
    turn(K_K3, -40);
    run(7000);                                          /* (its effects' tails rung out) */
    int64_t quiet = listen(1000);
    key(12, 1);
    int64_t keys = listen(500);
    key(12, 0);
    run(500);
    printf("  PLAYBACK volume: -3 dB %lld, OFF %lld, OFF with a key %lld\n", (long long)loud, (long long)quiet, (long long)keys);
    check(loud > 0 && quiet * 1000 < loud && keys > quiet * 10 + 1000, "PLAYBACK volume: OFF silences the song, the keys play on");
    player_play_pattern(s.order[0], 0);                 /* something else plays: 100 %, 0 dB */
    run(300);
    player_state(&st, 0);
    check(st.tempo_share == 65536u && st.vol_db == 0, "PLAYBACK: a pattern playing takes the speed and the volume back");
    tap(B_PLAY);
    run(300);
    ui_cur.song = (uint8_t)song;
}
/* SETTINGS' KEYS GAIN (specs/ui/screens/settings.md): the keys as heard, -12 dB a sixteenth of the energy; a note the
 * same key plays the same way each time, nothing else playing */
static void test_keys_gain(void)
{
    int64_t e[2];
    for (int s = RM_CHORUS; s <= RM_REVERB; s++) ui_keys_mix_set(s, 0);   /* (the gain alone: no sends, no effects' tails) */
    for (int k = 0; k < 2; k++) {
        ui_set[S_KEYS_GAIN] = (int16_t)(k ? -12 : 0);
        ui_settings_apply(S_KEYS_GAIN);
        run(3000);                                      /* (what rang before, rung out) */
        key(12, 1);
        e[k] = listen(400);
        key(12, 0);
    }
    run(3000);
    ui_set[S_KEYS_GAIN] = 0;
    ui_settings_apply(S_KEYS_GAIN);
    ui_edit_revert(REC_SETTINGS, 0, 0);                 /* (the sends as they were) */
    ui_settings_reload();
    double r = e[0] ? (double)e[1] / (double)e[0] : 0;
    printf("  KEYS GAIN: 0 dB %lld, -12 dB %lld (x %.4f, a sixteenth is 0.0631)\n", (long long)e[0], (long long)e[1], r);
    check(e[0] > 0 && r > 0.05 && r < 0.08, "KEYS GAIN: -12 dB, the keys' energy a sixteenth");
}
/* FX MIXER's KEYS column (specs/ui/screens/fx.md#mixer): in a SENDS song with REVERB on, the keys go into the reverb
 * by their REV (7F at first: a tail after the note), none at 00 or DRY; PAN 00 puts them left; their DCK ducks them
 * under DUCK's source; the MIXER's third page shows KEYS, K1 there sets KEYS GAIN; the settings record keeps the
 * strip (133 B), one of 128 B reads as the defaults */
static int keys_level(void)                     /* the keys' voices' loudness now, summed */
{
    pl_voice_info_t vi[PL_VOICES];
    int n = player_voices_now(vi, PL_VOICES), s = 0;
    for (int i = 0; i < n; i++) if (vi[i].on && vi[i].track == 0xFF) s += vi[i].level;
    return s;
}
static void test_keys_mix(void)
{
    int song = -1, dsong = -1;
    for (int n = 0; n < 256; n++) {                     /* (the reverb's song: nothing else in its chain, no DUST) */
        const uint8_t *f = song_fx((uint8_t)n);
        if (f && fxd_sends(f) && fxd_on(f, FE_REVERB) && !fxd_on(f, FE_GATE) && !fxd_on(f, FE_FILTER) &&
            !fxd_on(f, FE_DRIVE) && !fxd_on(f, FE_LOFI) && !fxd_on(f, FE_DUCK) && song < 0) song = n;
        if (f && fxd_on(f, FE_DUCK) && dsong < 0) dsong = n;
    }
    check(song >= 0 && dsong >= 0, "KEYS: the demo disk has a SENDS song with REVERB alone on, and one with DUCK on");
    if (song < 0 || dsong < 0) return;
    uint16_t keep = ui_cur.inst;
    uint8_t keep_song = ui_cur.song;
    ui_set_inst(UI_FIRST_INST);
    ui_play_song((uint8_t)song, 0);                     /* (played and stopped, quiet: its setup what the keys play
                                                         * through) */
    run(300);
    player_stop();
    run(100);
    player_all_sound_off();
    /* the reverb's tail after a note: REV 7F, REV 00, DRY (CHORUS and DELAY sends 00) */
    int64_t tail[3];
    ui_keys_mix_set(RM_CHORUS, 0);
    ui_keys_mix_set(RM_DELAY, 0);
    for (int c = 0; c < 3; c++) {
        ui_keys_mix_set(RM_REVERB, c == 1 ? 0 : 0x7F);
        ui_set[S_KEYS_FX] = (int16_t)(c == 2);
        ui_settings_apply(S_KEYS_FX);
        run(6000);                                      /* (what rang before, rung out) */
        key(12, 1); run(300); key(12, 0);
        run(400);
        tail[c] = listen(2000);
    }
    ui_set[S_KEYS_FX] = 0;
    ui_settings_apply(S_KEYS_FX);
    printf("  KEYS in song %02X (SENDS, REVERB on): the tail after a note: REV 7F %lld, REV 00 %lld, DRY %lld\n", song,
           (long long)tail[0], (long long)tail[1], (long long)tail[2]);
    check(tail[0] > tail[1] * 2 && tail[0] > tail[2] * 2, "KEYS in SENDS: their REV 7F gives the note a reverb's tail, "
          "REV 00 and DRY none");
    /* PAN 00, the sends 00: all on the left */
    ui_keys_mix_set(RM_REVERB, 0);
    ui_keys_mix_set(RM_PAN, 0);
    run(6000);
    energy_lr[0] = energy_lr[1] = 0;
    key(12, 1); run(400); key(12, 0);
    printf("  KEYS PAN 00: left %lld, right %lld\n", (long long)energy_lr[0], (long long)energy_lr[1]);
    check(energy_lr[0] > 0 && energy_lr[1] * 100 < energy_lr[0], "KEYS PAN 00: the keys on the left alone");
    ui_keys_mix_set(RM_PAN, 0x80);
    /* DCK: a key held through the DUCK song from its second position, DCK 00 then 7F: the keys' voices quieter under its
     * source */
    int lv[2];
    for (int c = 0; c < 2; c++) {
        ui_keys_mix_set(RM_DUCK, c ? 0x7F : 0);
        run(6000);
        ui_play_song((uint8_t)dsong, 1);
        run(500);
        key(12, 1);
        lv[c] = 0;
        for (int t = 0; t < 100; t++) { run(20); lv[c] += keys_level(); }
        key(12, 0);
        tap(B_PLAY);
    }
    printf("  KEYS DCK in song %02X: the keys' level held 2 s, DCK 00 %d, DCK 7F %d\n", dsong, lv[0], lv[1]);
    check(lv[0] > 0 && lv[1] * 100 < lv[0] * 97, "KEYS DCK 7F: the keys ducked under DUCK's source (the same render "
          "but DCK: 3 % lower at least over 2 s of hits and releases)");
    ui_keys_mix_set(RM_DUCK, 0);
    ui_keys_mix_set(RM_CHORUS, 0x7F);
    ui_keys_mix_set(RM_DELAY, 0x7F);
    ui_keys_mix_set(RM_REVERB, 0x7F);
    /* the MIXER's third page: KEYS; K1 on LVL sets KEYS GAIN; DRY darkens DCK and the sends */
    home_key(8);
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "MIXER"); i++) tap(B_SEQ);
    turn(K_ALG, 20);
    turn(K_SELECT, -6);
    int g0 = ui_set[S_KEYS_GAIN];
    turn(K_K1, 3);
    shot("24b_mixer_keys");
    check(screen_is("FX") && ui_set[S_KEYS_GAIN] == g0 + 3 && ui_unsaved,
          "MIXER: KEYS the last column, K1 on its LVL turns KEYS GAIN (a setting, unsaved)");
    ui_set[S_KEYS_FX] = 1;
    ui_settings_apply(S_KEYS_FX);
    shot("24c_mixer_keys_dry");
    ui_set[S_KEYS_FX] = 0;
    ui_settings_apply(S_KEYS_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);   /* (FX as it was: LIVE) */
    /* the record: 133 B, the strip in bytes 128-132 (0 its default); one of 128 B reads as the defaults */
    ui_keys_mix_set(RM_PAN, 0x40);
    ui_keys_mix_set(RM_REVERB, 0x20);
    uint32_t len;
    uint8_t ver, rec[133];
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(p && len == 133 && p[128] == (0x40 ^ 0x80) && p[129] == 0 && p[130] == 0 && p[131] == 0 && p[132] == 0x7F - 0x20,
          "the settings record: 133 B, the keys' strip in bytes 128-132, 0 at its defaults");
    memcpy(rec, p, sizeof rec);
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 128);    /* (a record from before: 128 B) */
    ui_settings_reload();
    check(!memcmp(ui_keys_mix + RM_PAN, UI_KEYS_MIX_DEF + RM_PAN, RM_N - RM_PAN), "a settings record of 128 B: the keys' strip at its defaults");
    ui_edit_revert(REC_SETTINGS, 0, 0);
    ui_settings_reload();
    tap(B_HOME);
    ui_set_inst(keep);
    ui_play_song(keep_song, 0);                         /* (the current song as it was, paused) */
    run(300);
    tap(B_PLAY);
    run(200);
}
/* FX's MIXER on a playing song (specs/ui/screens/fx.md#the-fx-setup: what you change is what you hear): each track's
 * LVL turned down and its PAN to 00 by hand while it plays, heard at once */
static void mixer_all(int key, int clicks)      /* SEL held, K1 on row `key`'s cell of tracks 00-0F */
{
    turn(K_SELECT, -6);
    turn(K_SELECT, key);
    turn(K_ALG, -20);
    for (int t = 0; t < 16; t++) {
        press(B_SEL); turn(K_K1, clicks); release(B_SEL);
        turn(K_ALG, 1);
    }
}
static void test_mixer_heard(void)
{
    uint8_t song = ui_cur.song;
    ui_play_song(2, 0);                                 /* (MIDNIGHT RUN: stereo, its tracks panned) */
    run(2000);
    home_key(8);
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "MIXER"); i++) tap(B_SEQ);
    energy_lr[0] = energy_lr[1] = 0;
    run(1000);
    int64_t l0 = energy_lr[0], r0 = energy_lr[1];
    mixer_all(1, -8);                                   /* (PAN 80 -> 00: every track left) */
    run(300);
    energy_lr[0] = energy_lr[1] = 0;
    run(1000);
    int64_t l1 = energy_lr[0], r1 = energy_lr[1];
    mixer_all(0, -8);                                   /* (LVL 7F -> 00) */
    run(300);
    int64_t e2 = listen(1000);
    printf("  MIXER while MIDNIGHT RUN plays: before L %lld R %lld; every PAN 00: L %lld R %lld; every LVL 00: %lld\n",
           (long long)l0, (long long)r0, (long long)l1, (long long)r1, (long long)e2);
    check(r0 > 0 && r1 * 4 < l1 && (double)r1 / (double)(l1 + 1) < 0.25 * (double)r0 / (double)(l0 + 1),
          "MIXER: every track's PAN to 00 while it plays: the song moves left at once");
    check(e2 * 20 < l0 + r0, "MIXER: every track's LVL to 00 while it plays: quiet at once (its effects' tails aside)");
    ui_edit_revert(REC_SONG, 2, 0);
    for (int i = 0; i < 3 && screen_is("FX") && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    home_key(1);
    player_stop();
    ui_cur.song = song;
    run(500);
}
/* FX EDIT's activity meters (specs/ui/screens/fx.md#edit): measured only while EDIT shows; a key played through the
 * chain moves an effect's meter that is on; left alone (FX left) they stand; in silence they fall */
static int32_t meters_sum(const uint8_t *now)   /* the chain effects' meters that are on, summed */
{
    int32_t s = 0;
    for (int e = FE_GATE; e <= FE_LIMITER; e++) if ((now[7] | now[8] << 8) >> e & 1) s += fx_meter_q15(e);
    return s;
}
static void test_fx_meters(void)
{
    uint8_t now[FX_SETUP_LEN];
    player_stop();                                      /* (silence: nothing held from before) */
    run(100);
    player_all_sound_off();
    home_key(8);
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "EDIT"); i++) tap(B_SEQ);
    fx_now(now);
    run(4000);
    int32_t quiet = meters_sum(now);
    key(12, 1); run(600);
    int32_t played = meters_sum(now);
    shot("23d_fx_edit_meters");
    key(12, 0);
    home_key(1);                                        /* (FX left: no more measured) */
    int32_t left = meters_sum(now);
    key(12, 1); run(600); key(12, 0);
    int32_t stood = meters_sum(now);
    home_key(8);
    tap(B_FX);
    run(4000);
    int32_t fell = meters_sum(now);
    printf("  FX EDIT's meters (the chain's on, summed, Q15): quiet %d, a key played %d, FX left %d, then a key %d, back and "
           "silent 4 s %d\n", (int)quiet, (int)played, (int)left, (int)stood, (int)fell);
    check(quiet == 0 && played > 300, "FX EDIT: a key through the chain moves the meters of the effects on");
    check(left && stood == left, "FX left: the meters not measured (they stand)");
    check(fell * 10 < played, "FX EDIT again, in silence: the meters fall");
    for (int i = 0; i < 3 && screen_is("FX") && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    home_key(1);
    run(200);
}
/* SONGS' play modes (specs/ui/screens/songs.md#the-play-modes): a looping song loops in every mode; the end of a
 * song that stops is the mode's: ONE PLAY stops it, REPEAT SONG plays it again from its start, PLAY NEXT goes on into
 * the next song on any screen, the cursor following; something else playing takes the mode out of hold. Each from
 * its last position (ALGORITHM's box) */
static void test_play_modes(void)
{
    player_state_t st;
    ui_song_t l, s;
    uint8_t ns[256];
    int loop = song_named("TRANCE LOOP"), stop = song_named("ADVENTURE"), next = -1, n = ui_songs(ns, 256), again = 0;
    for (int i = 0; i + 1 < n; i++) if (ns[i] == stop) next = ns[i + 1];
    if (loop < 0 || stop < 0 || next < 0 || !ui_song((uint8_t)loop, &l) || !ui_song((uint8_t)stop, &s) || (l.flags & 1)
        || !(s.flags & 1)) { check(0, "play modes: TRANCE LOOP loops, ADVENTURE stops, a song after it"); return; }
    uint32_t llen = ui_song_secs(&l) * 1000u + 1500u, slen = ui_song_secs(&s) * 1000u;
    int keep = ui_cur.song;
    home_key(1);                                        /* (SONGS) */
    check(screen_is("SONGS"), "play modes: on SONGS");
    ui_cur.song = (uint8_t)loop;
    ui_cur.play_mode = 0;
    turn(K_ALG, l.npos);
    tap(B_PLAY);
    player_state(&st, 0);
    check(st.playing && st.song == loop && st.pos == l.npos - 1 && st.end_mode == PL_END_ONCE,
          "play modes: PLAY here from the last position, ONE PLAY in hold");
    run(llen);
    player_state(&st, 0);
    check(st.playing && st.song == loop && st.pos >= l.loop_from, "play modes: ONE PLAY, a looping song loops from its LOOP FROM");
    tap(B_SEQ);
    tap(B_SEQ);                                         /* REPEAT SONG, PLAY NEXT: at once on the song playing */
    player_state(&st, 0);
    check(ui_cur.play_mode == 2 && ui_toast_now() && !strcmp(ui_toast_now(), "PLAY NEXT") && st.end_mode == PL_END_NEXT,
          "play modes: SEQ twice, PLAY NEXT");
    run(llen);
    player_state(&st, 0);
    check(st.playing && st.song == loop, "play modes: PLAY NEXT, a looping song loops (no next song)");
    tap(B_PLAY);                                        /* (paused) */
    ui_cur.song = (uint8_t)stop;
    tap(B_SEQ);
    check(ui_cur.play_mode == 0 && ui_toast_now() && !strcmp(ui_toast_now(), "ONE PLAY"), "play modes: SEQ, ONE PLAY");
    turn(K_ALG, s.npos);
    tap(B_PLAY);
    player_state(&st, 0);
    check(st.playing && st.song == stop && st.pos == s.npos - 1 && st.end_mode == PL_END_ONCE,
          "play modes: a song that stops, from its last position, ONE PLAY in hold");
    for (uint32_t t = 0; t < slen && st.playing; t += 250) { run(250); player_state(&st, 0); }
    check(!st.playing && st.ended, "play modes: ONE PLAY, a song that stops stops at its end");
    tap(B_SEQ);
    check(ui_cur.play_mode == 1 && ui_toast_now() && !strcmp(ui_toast_now(), "REPEAT SONG"), "play modes: SEQ, REPEAT SONG");
    turn(K_ALG, s.npos);
    tap(B_PLAY);
    for (uint32_t t = 0; t < slen && !again; t += 250) {
        run(250);
        player_state(&st, 0);
        again = st.playing && st.song == stop && st.pos == 0;
    }
    check(again && st.end_mode == PL_END_REPEAT, "play modes: REPEAT SONG, a song that stops plays again from its start");
    tap(B_SEQ);                                         /* PLAY NEXT, at once on the song playing */
    run(100);
    player_state(&st, 0);
    check(ui_cur.play_mode == 2 && st.end_mode == PL_END_NEXT && st.end_next == next, "play modes: SEQ, PLAY NEXT, its next song");
    turn(K_ALG, s.npos);
    tap(B_PLAY);
    home_key(8);                                        /* (PLAYBACK: on any screen) */
    for (uint32_t t = 0; t < slen && st.song == stop; t += 250) { run(250); player_state(&st, 0); }
    check(st.playing && st.song == next && st.pos == 0 && ui_cur.song == next,
          "play modes: PLAY NEXT on PLAYBACK, the next song from its start, the cursor following");
    player_play_pattern(s.order[0], 0);                 /* something else plays: out of hold */
    run(300);
    player_state(&st, 0);
    check(st.end_mode == PL_END_OWN, "play modes: a pattern playing takes the mode out of hold");
    tap(B_PLAY);
    run(300);
    ui_cur.play_mode = 0;
    ui_cur.song = (uint8_t)keep;
}
/* the effect under the FX page's cursor is yours (specs/ui/screens/fx.md#the-fx-setup): PUNCH IN sweeps FILTER's CUT
 * (#94 every beat from its start); FILTER picked on LIVE and turned to the bottom, the sweep is skipped; DRIVE
 * picked, the sweep plays FILTER again. Then FILTER picked once more, another song started: FILTER plays on as it
 * was, REVERB takes the new song's setup */
static void test_fx_mine(void)
{
    uint8_t now[64];
    int song = song_named("PUNCH IN"), next = song_named("SPICE ROAD"), keep = ui_cur.song;
    if (song < 0 || next < 0) { check(0, "FX mine: PUNCH IN, SPICE ROAD"); return; }
    int was_edited = store_edit_has(REC_SONG, (uint8_t)song, 0);
    ui_cur.song = (uint8_t)song;
    ui_play_song((uint8_t)song, 0);
    run(300);
    tap(B_FX);
    check(screen_is("FX"), "FX mine: on FX");
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* G#3: FILTER */
    turn(K_K1, -40);                                    /* CUT to the bottom: the hand's */
    run(100);
    fx_now(now);
    int mine = now[16 + 4], held = 1, followed = 0;
    for (int t = 0; t < 6000; t += 100) { run(100); fx_now(now); if (now[16 + 4] != mine) held = 0; }
    check(held, "FX mine: FILTER under the cursor, PUNCH IN's sweep of its CUT skipped (the hand's)");
    key(UI_BLACK[2], 1); key(UI_BLACK[2], 0);           /* A#3: DRIVE */
    for (int t = 0; t < 3000 && !followed; t += 100) { run(100); fx_now(now); followed = now[16 + 4] != mine; }
    check(followed, "FX mine: DRIVE picked, the sweep plays FILTER's CUT again (only the effect under the cursor)");
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* FILTER again */
    turn(K_K1, -40);
    run(100);
    fx_now(now);
    int cut = now[16 + 4], on = now[7] >> FE_FILTER & 1;
    ui_play_song((uint8_t)next, 0);                     /* another song starts (as PLAY NEXT's would) */
    run(300);
    fx_now(now);
    const uint8_t *nf = song_fx((uint8_t)next);
    uint8_t rv = fxd_code(FE_REVERB, 0);
    check(now[16 + 4] == cut && (now[7] >> FE_FILTER & 1) == on && nf && now[16 + rv - 0x90] == nf[16 + rv - 0x90],
          "FX mine: another song's start keeps FILTER as it plays, REVERB takes the new song's setup");
    tap(B_PLAY);
    run(200);
    home_key(1);
    if (!was_edited) ui_edit_revert(REC_SONG, (uint8_t)song, 0);   /* (PUNCH IN's CUT as saved, for what follows) */
    run(200);
    ui_cur.song = (uint8_t)keep;
}
/* commands never change what you are editing (specs/ui/screens/editor.md): what the screens tell the player (a song's
 * properties, an instrument's edit page, FX's MIXER), and a song's properties open: its TEMPO heard at once as it
 * changes (the replay's own tempo held at it) */
static void test_editing(void)
{
    player_state_t st;
    int song = song_named("CITY LIGHTS"), keep = ui_cur.song;
    if (song < 0) { check(0, "editing: CITY LIGHTS"); return; }
    int was_edited = store_edit_has(REC_SONG, (uint8_t)song, 0);
    home_key(1);                                        /* (SONGS) */
    ui_cur.song = (uint8_t)song;
    ui_play_song((uint8_t)song, 0);
    run(300);
    player_state(&st, 0);
    int b0 = st.bpm10;
    check(st.playing && st.ed_what == PL_ED_NONE, "editing: SONGS' list, nothing edited");
    tap(B_GLO);                                         /* its properties */
    run(100);
    player_state(&st, 0);
    check(st.ed_what == PL_ED_TIME && st.ed_key == (uint32_t)song, "editing: its properties open, the song's time");
    turn(K_SELECT, 1);                                  /* TEMPO */
    turn(K_K1, 10);
    run(300);
    player_state(&st, 0);
    printf("  editing: TEMPO +10 in the properties, %d.%d BPM -> %d.%d\n", b0 / 10, b0 % 10, st.bpm10 / 10, st.bpm10 % 10);
    check(st.playing && st.bpm10 >= b0 + 95 && st.bpm10 <= b0 + 105, "editing: TEMPO +10 heard at once (it plays at its properties)");
    tap(B_GLO);                                         /* closed */
    run(100);
    player_state(&st, 0);
    check(st.ed_what == PL_ED_NONE, "editing: the properties closed, nothing edited");
    home_key(5);                                        /* INSTRUMENTS, its edit page */
    tap(B_EDIT);
    run(100);
    player_state(&st, 0);
    check(st.ed_what == PL_ED_INST && st.ed_key == ui_cur.inst, "editing: an instrument's edit page, that instrument");
    tap(B_HOME);
    run(100);
    tap(B_FX);                                          /* FX's MIXER: the song's */
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "MIXER"); i++) tap(B_SEQ);
    run(100);
    player_state(&st, 0);
    check(st.ed_what == PL_ED_MIX && st.ed_key == 0xFFFFu, "editing: FX's MIXER, the song's mixer (its 16 tracks)");
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    tap(B_PLAY);
    run(200);
    home_key(1);
    run(100);
    player_state(&st, 0);
    check(st.ed_what == PL_ED_NONE, "editing: back on SONGS' list, nothing");
    if (!was_edited) ui_edit_revert(REC_SONG, (uint8_t)song, 0);   /* (its TEMPO as saved, for what follows) */
    run(200);
    ui_cur.song = (uint8_t)keep;
}
/* LOOPS' MERGE (specs/ui/screens/loops.md#merging): two loops of one sound, the current (01) over the one below (00);
 * MERGE listed only then, asks first, and the current wins: its notes stay, the lower's move to the nearest free row
 * within a sixteenth or are dropped (counted), the lower's LEVEL in its volumes, its PAN as commands where the loop
 * sounding changes, each loop's note offs where its own note sounds; one loop, the current's track, mixer and mute */
static int menu_to(const char *item)    /* SEL's menu open: SELECT to that item (1 found) */
{
    turn(K_SELECT, -16);
    for (int i = 0; i < 16; i++) {
        const char *w = ui_overlay_words(ui_top());
        if (w && !strcmp(w, item)) return 1;
        turn(K_SELECT, 1);
    }
    return 0;
}

/* UNLOAD SONG (SONGS' SEL menu, PLAYBACK's GLO): offered only while a song is held; the song playing cut at once (no
 * tail), the keys' song held again with its setup and tempo, as at power-on; PLAY then plays the current song */
static void unloaded(const char *how)
{
    player_state_t st;
    ui_song_t ks;
    uint8_t now[64];
    char what[96];
    run(30);
    int64_t e = listen(200);
    player_state(&st, 0);
    fx_now(now);
    const uint8_t *kf = song_fx(UI_KEYS_SONG);
    snprintf(what, sizeof what, "unload: %s: the sound cut at once (energy %lld after 30 ms)", how, (long long)e);
    check(e == 0, what);
    snprintf(what, sizeof what, "unload: %s: the keys' song held, its tempo and setup; told", how);
    check(st.what == PL_NONE && !st.playing && st.song == UI_KEYS_SONG && ui_song(UI_KEYS_SONG, &ks) &&
          st.bpm10 == ks.tempo * 240 / (ks.ticks * ks.rpb) && kf && !memcmp(now + 16, kf + 16, 48) && ui_toast_now() &&
          !strcmp(ui_toast_now(), "SONG UNLOADED"), what);
}
static void test_unload(void)
{
    player_state_t st;
    uint8_t song = ui_cur.song;
    run(300);
    player_state(&st, 0);
    if (st.what != PL_NONE) { check(0, "unload: nothing played yet"); return; }
    home_key(1);                                        /* SONGS: its menu without UNLOAD SONG */
    tap(B_SEL);
    check(!menu_to("UNLOAD SONG") && menu_to("RENAME"), "unload: nothing played: SONGS' menu has no UNLOAD SONG");
    tap(B_HOME);
    ui_play_song(song, 0);
    run(1500);
    player_state(&st, 0);
    check(st.what == PL_SONG && st.playing && listen(200) > 0, "unload: the song plays");
    tap(B_SEL);
    check(menu_to("UNLOAD SONG"), "unload: a song held: SONGS' menu has UNLOAD SONG");
    shot("songs_menu_unload");
    tap(B_SEL);
    unloaded("SONGS' menu");
    tap(B_SEL);
    check(!menu_to("UNLOAD SONG"), "unload: unloaded: SONGS' menu has no UNLOAD SONG again");
    tap(B_HOME);
    tap(B_PLAY);                                        /* PLAY: the current song, from its start */
    run(500);
    player_state(&st, 0);
    check(st.what == PL_SONG && st.playing && st.song == song && st.pos == 0, "unload: PLAY then plays the current song from its start");
    home_key(8);                                        /* PLAYBACK's GLO: SCROLL, then UNLOAD SONG with the song's name */
    tap(B_GLO);
    turn(K_SELECT, 1);
    shot("playback_glo_unload");
    tap(B_EDIT);
    check(!ui_overlay_count(), "unload: PLAYBACK's GLO: EDIT on UNLOAD SONG closes the panel");
    unloaded("PLAYBACK's GLO");
    tap(B_GLO);                                         /* (nothing held: SCROLL alone, EDIT does nothing) */
    turn(K_SELECT, 1);
    tap(B_EDIT);
    player_state(&st, 0);
    check(ui_overlay_count() == 1 && st.what == PL_NONE && st.song == UI_KEYS_SONG, "unload: nothing held: PLAYBACK's GLO, SCROLL alone");
    tap(B_GLO);
    run(200);
}
static void unload_from_playback(const char *how)      /* PLAYBACK's GLO, UNLOAD SONG, EDIT */
{
    home_key(8);
    tap(B_GLO);
    turn(K_SELECT, 1);
    tap(B_EDIT);
    unloaded(how);
}
/* the loops played and stopped, LOOPS left: still what the keys hear, so FX edits THE LOOPS; UNLOAD SONG lets them go
 * (the loops themselves kept), LOOPS then holds them again for the keys */
static void test_unload_loops(void)
{
    player_state_t st;
    fxt_t tg;
    int made = !loops_n() && loops_new(0xF0, 0) == 0 && loops_set_rows(16) == LP_OK;   /* (a loop to play) */
    home_key(UI_BLACK[9]);                              /* LOOPS: the loops play, then stop */
    tap(B_PLAY);
    run(800);
    player_state(&st, 0);
    if (!(st.what == PL_LOOPS && st.playing)) { check(0, "unload loops: the loops play"); return; }
    int had = loops_n();
    tap(B_PLAY);
    run(200);
    home_key(8);                                        /* PLAYBACK, FX from it: THE LOOPS */
    tap(B_FX);
    player_state(&st, 0);
    check(st.what == PL_LOOPS && !st.lcue && !st.playing && fxt_get(&tg) && tg.what == FT_LOOPS && tg.plays,
          "unload loops: the loops played and stopped, LOOPS left: FX edits THE LOOPS, what the keys hear");
    tap(B_FX);
    unload_from_playback("the loops, PLAYBACK's GLO");
    check(loops_n() == had && had > 0, "unload loops: the loops themselves kept");
    home_key(UI_BLACK[9]);
    run(300);
    player_state(&st, 0);
    check(st.what == PL_LOOPS && st.lcue && !st.playing, "unload loops: LOOPS again: the loops held for the keys, nothing playing");
    home_key(8);
    run(300);
    if (made) ui_edit_revert(REC_LOOPS, 0, 0);
}
/* DJ played: UNLOAD SONG cuts the decks, DJ off, its stand kept; DJ's PLAY then plays again */
static void test_unload_dj(void)
{
    player_state_t st;
    home_key(10);                                       /* DJ: the main deck plays */
    tap(B_PLAY);
    run(800);
    player_state(&st, 0);
    if (!(st.what == PL_DJ && st.playing)) { check(0, "unload DJ: the decks play"); return; }
    uint8_t song0 = dj_deck(0)[DK_SONG];
    unload_from_playback("DJ, PLAYBACK's GLO");
    check(!dj_live() && dj_deck(0)[DK_SONG] == song0, "unload DJ: DJ off, its stand kept");
    home_key(10);
    tap(B_PLAY);
    run(800);
    player_state(&st, 0);
    check(st.what == PL_DJ && st.playing, "unload DJ: DJ's PLAY plays again");
    tap(B_PLAY);
    run(200);
    home_key(8);
    run(200);
}
/* SETTINGS' KEYS (specs/ui/screens/fx.md#the-keys-dry), set on FX's GLO: a FILTER low-pass at its lowest in the
 * chain; the keys THROUGH FX lose most of their sound to it, DRY none, a chorused Juno-106 patch's chorus included
 * (DRY with the FILTER on as with it off) */
/* the sampler's picture: a yellow line where each voice on its sample plays, live (the main loop's own redraws), gone
 * once the note has rung out; the envelope's graph under it as it was (the trace kept) */
static int mark_at(int y0, int y1, int most)    /* a yellow line in rows y0..y1-1 (most of them), -1 none */
{
    for (int x = 0; x < 240; x++) if (count_px(x, y0, x + 1, y1, C_YELLOW) >= most) return x;
    return -1;
}
static int mark_x(void) { return mark_at(100, 172, 60); }   /* the edit pages' waveform (rows 100-171) */
static void test_samp_marks(void)
{
    uint8_t carts[256];
    int nc = ui_carts(carts, 256), sc = -1, ss = -1;
    rec_cart_t c;
    rec_sinst_t si;
    rec_sample_t sa;
    for (int i = 0; i < nc && sc < 0; i++) {            /* (a looped instrument on a sample of a second or more) */
        if (ins_kind(carts[i]) != REC_SAMPLER || rec_cart(carts[i], &c)) continue;
        for (int s = 0; s < c.nslots && sc < 0; s++) {
            const uint8_t *x = rec_slot(&c, (uint8_t)s);
            if (!x) continue;
            rec_sampler_inst(x, &si);
            if (si.loop && !rec_sample(&c, si.sample, &sa) && sa.length >= (sa.rate ? sa.rate : 44100u)) { sc = carts[i]; ss = s; }
        }
    }
    check(sc >= 0, "the demo disk has a looped sample instrument");
    if (sc < 0) return;
    uint16_t keep = ui_cur.inst;
    player_pause();
    ui_set_inst((uint16_t)(sc << 8 | ss));
    home_key(5);                                        /* INSTRUMENTS: the list's preview (rows 149-207) first */
    run(100);
    int p0 = mark_at(149, 208, 50);
    key(7, 1); run(300);
    int p1 = mark_at(149, 208, 50);
    shot("18b_instr_sampler_playing");
    key(7, 0);
    for (int i = 0; i < 100 && mark_at(149, 208, 50) >= 0; i++) run(50);
    int p2 = mark_at(149, 208, 50);
    printf("  the list's preview: none %d, held x %d, let go %d\n", p0, p1, p2);
    check(p0 < 0 && p1 >= 0 && p2 < 0, "INSTRUMENTS' list: the preview shows a sample voice's yellow line, gone once rung out");
    run(4000);
    tap(B_EDIT);                                        /* ... then its edit page */
    run(100);
    static uint16_t was[240 * 240];
    memcpy(was, screen, sizeof was);
    int x0 = mark_x();
    key(7, 1);                                          /* C-4 held */
    run(200);
    int x1 = mark_x();
    run(300);
    int x2 = mark_x();
    shot("19b_instr_sampler_playing");
    int same = !memcmp(&was[172 * 240], &screen[172 * 240], (size_t)(213 - 172) * 240 * sizeof *screen);
    key(7, 0);
    for (int i = 0; i < 100 && mark_x() >= 0; i++) run(50);
    int x3 = mark_x();
    printf("  the sampler's play marks: %02X:%02X, none %d, held x %d, then %d; let go %d; the graph the same %d\n", sc, ss,
           x0, x1, x2, x3, same);
    check(x0 < 0 && x1 >= 0 && x2 >= 0 && x2 != x1 && x3 < 0, "INSTRUMENTS: a sample voice's yellow line, moving as it "
          "plays (the screen redrawn by itself), gone once it has rung out");
    check(same, "INSTRUMENTS: the envelope's graph as it was while the voice plays (the trace kept)");
    tap(B_HOME);
    ui_set_inst(keep);
    run(100);
}
static void test_keys_dry(void)
{
    uint8_t song = ui_cur.song;
    uint16_t keep = ui_cur.inst, juno = 0;
    rec_cart_t c;
    if (!rec_cart(0xF8, &c))                            /* (JUNO-106: a patch with its chorus on) */
        for (int s = 0; s < c.nslots && !juno; s++) {
            const uint8_t *x = rec_slot(&c, (uint8_t)s);
            uint8_t p[18];
            if (!x) continue;
            rec_juno_patch(x, p);
            if (juno_chorus_mode(p)) juno = (uint16_t)(0xF800 | s);
        }
    check(juno != 0, "KEYS: a chorused Juno-106 patch to play");
    ui_set_inst(juno);
    home_key(8);                                        /* PLAYBACK: the song played and paused (its effects the
                                                         * sound's: what FX edits reaches it), then FX */
    ui_play_song(song, 0);
    run(300);
    tap(B_PLAY);
    player_state_t st;
    player_state(&st, 0);
    check(!st.playing && st.what == PL_SONG && st.song == song, "KEYS: the song paused, its effects the sound's");
    tap(B_FX);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* FILTER */
    int cut0 = fxd_get(song_fx(song), FE_FILTER, 0), on0 = fxd_on(song_fx(song), FE_FILTER);
    turn(K_K1, -80);                                    /* (its lowest low-pass) */
    if (!on0) tap(B_OCTP);
    tap(B_SEQ);                                         /* EDIT: the keys play */
    check(screen_is("FX") && fxd_on(song_fx(song), FE_FILTER) && ui_set[S_KEYS_FX] == 0, "KEYS: FX EDIT, the FILTER on, THROUGH FX");
    int64_t e[3];
    for (int k = 0; k < 3; k++) {
        if (k == 1) {                                   /* GLO: KEYS, EVERYWHERE, K1: DRY */
            tap(B_GLO);
            turn(K_SELECT, 2);
            turn(K_K1, 1);
            check(ui_set[S_KEYS_FX] == 1 && ui_toast_now() && !strcmp(ui_toast_now(), "KEYS DRY"), "KEYS: FX's GLO, K1 on KEYS: DRY");
            shot("ui3_fx_keys_dry");
            tap(B_GLO);
        }
        if (k == 2) tap(B_OCTM);                        /* (the FILTER off) */
        run(4000);                                      /* (what rang before, rung out) */
        key(12, 1);
        e[k] = listen(400);
        key(12, 0);
    }
    printf("  KEYS: through a low-pass %lld, DRY %lld, DRY with the FILTER off %lld\n", (long long)e[0], (long long)e[1], (long long)e[2]);
    check(e[0] * 4 < e[1], "KEYS: THROUGH FX, the low-pass takes most of the keys' sound");
    check(e[2] > 0 && e[1] * 10 > e[2] * 9 && e[1] * 10 < e[2] * 11, "KEYS: DRY, the FILTER does nothing to them (their chorus with them)");
    tap(B_GLO);                                         /* THROUGH FX again, the FILTER as it was */
    turn(K_SELECT, 2);
    turn(K_K1, 1);
    tap(B_GLO);
    check(ui_set[S_KEYS_FX] == 0, "KEYS: back to THROUGH FX");
    tap(B_SEQ); tap(B_SEQ);                             /* (LIVE again) */
    turn(K_K1, cut0 - fxd_get(song_fx(song), FE_FILTER, 0));
    if (on0) tap(B_OCTP);
    tap(B_FX);
    run(3000);
    ui_set_inst(keep);
}
/* the themes (specs/ui/colours.md): each deep shade 30 % of the way from the ground, the fraction dropped (on black as
 * before: each part at 30 %, rounded down); the light ones and COMMANDER by name, on a few screens */
static uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static void test_themes(void)
{
    static const char *const LIGHT[6] = {"DAYLIGHT", "PAPER", "MANUAL", "SILVER", "LCD", "COMMANDER"};
    static const uint32_t GROUND[6] = {0xD0D0C8, 0xFFFFFF, 0xF4EEDC, 0xD4D4D0, 0xC8D2AA, 0x0000A8};
    int keep = ui_set[S_THEME], dark = 1, light = 1;
    for (int t = 0; t < 10; t++) {                      /* (the dark ones: black, its deep shade black) */
        g_theme(t);
        dark &= g_rgb565(0) == 0 && g_rgb565(16) == 0;
    }
    g_theme(0);
    check(dark && g_rgb565(16 + 7) == rgb565(180 * 3 / 10, 180 * 3 / 10, 172 * 3 / 10),
          "THEMES: on black, every deep shade as before (DEFAULT's light grey's at 30 %, rounded down)");
    for (int i = 0; i < 6; i++) {
        uint32_t g = GROUND[i];
        g_theme(10 + i);
        light &= !strcmp(g_theme_name(10 + i), LIGHT[i]) && g_rgb565(0) == rgb565((int)(g >> 16), (int)(g >> 8 & 255), (int)(g & 255))
                 && g_rgb565(16) == g_rgb565(0);
    }
    g_theme(10);                                        /* DAYLIGHT's normal text 2C2C28 on D0D0C8: 3/10 of the way */
    check(light && G_THEMES == 16 && g_rgb565(16 + 7) == rgb565(208 + (44 - 208) * 3 / 10, 208 + (44 - 208) * 3 / 10, 200 + (40 - 200) * 3 / 10),
          "THEMES: DAYLIGHT .. COMMANDER (10-15), each on its ground, the deep shades 30 % of the way from it");
    for (int i = 0; i < 6; i++) {
        char nm[48];
        ui_set[S_THEME] = 10 + i;
        ui_settings_apply(S_THEME);
        const int SCR[3] = {1, 3, 10};                  /* SONGS, the EDITOR, DJ */
        const char *const SN[3] = {"songs", "editor", "dj"};
        for (int s = 0; s < 3; s++) {
            home_key(SCR[s]);
            snprintf(nm, sizeof nm, "ui3_theme_%s_%s", LIGHT[i], SN[s]);
            shot(nm);
        }
    }
    ui_set[S_THEME] = keep;
    ui_settings_apply(S_THEME);
    check(g_theme_now() == keep, "THEMES: back to the one set");
}
/* FILTER's envelope (specs/ui/screens/fx.md#filters-envelope): its knobs on FILTER's second page, kept at 0xB0-0xB5 in
 * the setup; SOURCE KEYS opens a low-pass while a key is held, the keys DRY fire it too; a beat SOURCE fires with
 * nothing playing (the clock runs on) */
static void test_filter_env(void)
{
    uint8_t song = ui_cur.song;
    char b[16];
    home_key(8);                                        /* PLAYBACK: the song played and paused, then FX */
    ui_play_song(song, 0);
    run(300);
    tap(B_PLAY);
    tap(B_FX);
    key(UI_BLACK[1], 1); key(UI_BLACK[1], 0);           /* FILTER */
    check(song_fx(song)[48] == 0 && song_fx(song)[51] == 75 && song_fx(song)[53] == 75,
          "FILTER ENV: the demo song's setup: ENV 0 %, DECAY and RELEASE at step 75");
    int cut0 = fxd_get(song_fx(song), FE_FILTER, 0), on0 = fxd_on(song_fx(song), FE_FILTER);
    turn(K_K1, -80);                                    /* (its lowest low-pass) */
    if (!on0) tap(B_OCTP);
    tap(B_SEQ);                                         /* EDIT: the keys play */
    turn(K_ALG, 1);                                     /* page 2: ATTACK, DECAY, SUSTAIN, RELEASE */
    turn(K_K3, 100);
    shot("ui3_fx_filter_env_page2");
    check(song_fx(song)[52] == 100 && fxd_get(song_fx(song), FE_FILTER, 6) == 100, "FILTER ENV: page 2's K3, SUSTAIN, at byte 52");
    fxd_text(FE_FILTER, 4, fxd_get(song_fx(song), FE_FILTER, 4), b);
    check(!strcmp(b, "2.0 MS"), "FILTER ENV: ATTACK at first 2.0 MS");
    fxd_text(FE_FILTER, 5, 75, b);
    check(!strcmp(b, "296 MS"), "FILTER ENV: DECAY's step 75, 296 MS");
    fxd_text(FE_FILTER, 7, 101, b);
    check(!strcmp(b, "1.22 S"), "FILTER ENV: RELEASE's step 101, 1.22 S");
    fxd_text(FE_FILTER, 7, 127, b);
    check(!strcmp(b, "5.00 S"), "FILTER ENV: RELEASE's top, 5.00 S");
    turn(K_ALG, -1);                                    /* page 1: CUT, RESO, ENV, SOURCE */
    run(4000);
    key(12, 1);
    int64_t e0 = listen(400);
    key(12, 0);
    turn(K_K3, 100);
    check(song_fx(song)[48] == 100, "FILTER ENV: K3, ENV, at byte 48");
    fxd_text(FE_FILTER, 3, fxd_get(song_fx(song), FE_FILTER, 3), b);
    check(!strcmp(b, "KEYS"), "FILTER ENV: SOURCE at first KEYS");
    shot("ui3_fx_filter_env");
    run(4000);
    uint32_t h0 = fx_filter_hits();
    key(12, 1);
    int64_t e1 = listen(400);
    shot("ui3_fx_filter_env_open");
    key(12, 0);
    printf("  FILTER ENV: a key through the lowest low-pass %lld, with ENV 100 %% %lld\n", (long long)e0, (long long)e1);
    check(fx_filter_hits() == h0 + 1u, "FILTER ENV: a key's note, one hit");
    check(e1 > e0 * 4, "FILTER ENV: ENV 100 %, SUSTAIN 100 %: the key opens the low-pass while held");
    tap(B_GLO);                                         /* the keys DRY: they still fire it */
    turn(K_SELECT, 2);
    turn(K_K1, 1);
    tap(B_GLO);
    run(1000);
    h0 = fx_filter_hits();
    key(12, 1); run(200); key(12, 0);
    check(ui_set[S_KEYS_FX] == 1 && fx_filter_hits() == h0 + 1u, "FILTER ENV: the keys DRY still fire it");
    tap(B_GLO);
    turn(K_SELECT, 2);
    turn(K_K1, 1);
    tap(B_GLO);
    turn(K_K4, 19);                                     /* SOURCE: BEAT */
    fxd_text(FE_FILTER, 3, fxd_get(song_fx(song), FE_FILTER, 3), b);
    check(song_fx(song)[49] == 19 && !strcmp(b, "BEAT"), "FILTER ENV: K4, SOURCE, at byte 49: BEAT");
    player_state_t st;
    player_state(&st, 0);
    run(500);
    h0 = fx_filter_hits();
    run(3000);
    printf("  FILTER ENV: SOURCE BEAT, nothing playing: %u hits in 3 s\n", (unsigned)(fx_filter_hits() - h0));
    check(!st.playing && fx_filter_hits() - h0 >= 2u && fx_filter_hits() - h0 <= 13u,
          "FILTER ENV: SOURCE BEAT with nothing playing: it fires on, at the tempo");
    turn(K_K4, -19);                                    /* (as it was) */
    turn(K_K3, -100);
    turn(K_ALG, 1);
    turn(K_K3, -100);
    turn(K_ALG, -1);
    check(song_fx(song)[48] == 0 && song_fx(song)[49] == 0 && song_fx(song)[52] == 0, "FILTER ENV: back to ENV 0 %");
    tap(B_SEQ); tap(B_SEQ);                             /* (LIVE again) */
    turn(K_K1, cut0 - fxd_get(song_fx(song), FE_FILTER, 0));
    if (!on0) tap(B_OCTM);
    tap(B_FX);
    run(3000);
}
/* PRESETS' cartridge list (specs/ui/dialogs.md#presets-cartridge-list): SEL tapped, then PRESETS: the tap's menu
 * gives way to the list, each click a cartridge; any other control closes it and does its own job, HOME only closes
 * it; SEL held + PRESETS opens it too; a control between the tap and PRESETS: PRESETS alone */
static int carts_list_on(void) { ui_overlay_t *o = ui_top(); return o && o->kind == OV_LIST && o->opener == B_SEL; }
static void test_presets_list(void)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), i0 = 0;
    uint16_t keep = ui_cur.inst;
    for (int i = 0; i < n; i++) if (nums[i] == keep >> 8) i0 = i;
    home_key(1);                                        /* (SONGS: SEL's tap opens its menu) */
    check(screen_is("SONGS") && n > 2, "PRESETS' list: on SONGS, cartridges to turn");
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_MENU, "PRESETS' list: SEL's tap, its menu");
    turn(K_PRESETS, 1);
    check(carts_list_on() && ui_overlay_count() == 1, "PRESETS' list: PRESETS, the menu gives way to the list");
    check(ui_cur.inst >> 8 == nums[(i0 + 1) % n], "PRESETS' list: the first click picks the next cartridge");
    char want[40], nm[16];
    ui_cart_name(nums[(i0 + 1) % n], nm);
    snprintf(want, sizeof want, "CARTRIDGE %02X %s", nums[(i0 + 1) % n], nm);
    check(ui_knob_now() && !strcmp(ui_knob_now(), want), "PRESETS' list: the header names it");
    turn(K_PRESETS, 1);
    check(carts_list_on() && ui_cur.inst >> 8 == nums[(i0 + 2) % n], "PRESETS' list: the next click, the next one");
    shot("presets_list");
    int song = ui_cur.song;
    turn(K_SELECT, 1);
    check(!ui_overlay_count() && ui_cur.song != song, "PRESETS' list: SELECT closes it and moves the cursor");
    turn(K_SELECT, -1);
    tap(B_SEL);
    turn(K_PRESETS, -1);
    check(carts_list_on() && ui_cur.inst >> 8 == nums[(i0 + 1) % n], "PRESETS' list: back a click, SEL tapped again");
    tap(B_HOME);
    check(!ui_overlay_count() && !ui_home_combo(), "PRESETS' list: HOME only closes it");
    tap(B_SEL);
    turn(K_PRESETS, 1);
    turn(K_PRESETS, -1);                                /* (back: the cartridge as before, for what follows) */
    int was_on = carts_list_on();
    tap(B_SEL);
    run(600);                                           /* (past COMBO GRACE: its tap's menu would be open by now) */
    check(was_on && !ui_overlay_count(), "PRESETS' list: SEL tapped again only closes it, no menu after");
    press(B_SEL);                                       /* held */
    turn(K_PRESETS, -1);
    int held_on = carts_list_on() && ui_cur.inst >> 8 == nums[i0];
    release(B_SEL);
    run(300);
    uint16_t after = ui_cur.inst;
    turn(K_PRESETS, 1);
    check(held_on && !carts_list_on() && ui_cur.inst >> 8 == nums[i0] && ui_cur.inst != after,
          "PRESETS' list: SEL held + PRESETS, the list while held, gone with SEL; PRESETS then the instruments");
    tap(B_SEL);
    turn(K_PRESETS, 1);
    run(3000);
    key(12, 1);
    int64_t e = listen(300);
    key(12, 0);
    check(!ui_overlay_count() && e > 0, "PRESETS' list: a key closes it and plays");
    tap(B_SEL);
    turn(K_SELECT, 1);                                  /* (in the menu: another control first) */
    uint16_t was = ui_cur.inst;
    turn(K_PRESETS, 1);
    check(ui_top() && ui_top()->kind == OV_MENU && ui_cur.inst >> 8 == was >> 8 && ui_cur.inst != was,
          "PRESETS' list: a control between, PRESETS turns the instrument, the menu stays");
    tap(B_HOME);
    run(300);
    ui_set_inst(keep);
}

/* PRESETS' instrument list (specs/ui/dialogs.md#presets-instrument-list): PRESETS on SONGS opens it, the cartridge's
 * used slots with the current one highlighted; each click keeps it a second more, gone a second after the last; any
 * other control closes it and does its own job; none with PRESETS LIST OFF, none over a menu, none on INSTRUMENTS */
static int il_on(void) { ui_overlay_t *o = ui_top(); return o && o->kind == OV_LIST && o->opener < 0; }
static void test_presets_instruments(void)
{
    uint16_t keep = ui_cur.inst;
    ui_set_inst(0xF000);                                /* (F0, built in: every slot used) */
    home_key(1);                                        /* SONGS */
    turn(K_PRESETS, 1);
    check(il_on() && ui_overlay_count() == 1 && ui_cur.inst == 0xF001, "PRESETS' instruments: a click opens them");
    shot("presets_instruments");
    run(600);
    turn(K_PRESETS, 1);
    run(600);
    int kept = il_on();
    run(400);
    check(kept && !ui_overlay_count() && ui_cur.inst == 0xF002,
          "PRESETS' instruments: a click keeps them a second more, gone a second after the last");
    int song = ui_cur.song;
    turn(K_PRESETS, -1);
    turn(K_SELECT, 1);
    check(!ui_overlay_count() && ui_cur.song != song, "PRESETS' instruments: SELECT closes them and moves the cursor");
    turn(K_SELECT, -1);
    ui_set[S_PRESETS_LIST] = 1;                         /* OFF */
    turn(K_PRESETS, 1);
    check(!ui_overlay_count() && ui_cur.inst == 0xF002, "PRESETS' instruments: none with PRESETS LIST OFF");
    ui_set[S_PRESETS_LIST] = 0;
    tap(B_SEL);
    turn(K_SELECT, 1);                                  /* (in the menu: another control first, no cartridges' list) */
    turn(K_PRESETS, 1);
    check(ui_top() && ui_top()->kind == OV_MENU && ui_overlay_count() == 1 && ui_cur.inst == 0xF003,
          "PRESETS' instruments: none over a menu (the instrument turns, the menu stays)");
    tap(B_HOME);
    home_key(5);                                        /* INSTRUMENTS */
    turn(K_PRESETS, 1);
    check(screen_is("INSTRUMENTS") && !ui_overlay_count() && ui_cur.inst == 0xF004,
          "PRESETS' instruments: none on INSTRUMENTS (its own list)");
    home_key(1);
    ui_set_inst(keep);
    run(300);
}

/* ------------------------------------------------------------- QUANTIZE --- */
/* SETTINGS' QUANTIZE (specs/ui/screens/settings.md; editor.md#realtime-takes): after COUNT-IN, ROW at first, kept in the
 * settings record's byte 109 (bits 4-2: 0 ROW, 1-3 2-4 ROWS, 4 REAL TIME; a record from before it, or 5-7, ROW). A take's
 * notes land as it says and are heard as written, by their track and never as the keys: one before its line waits for
 * it and sounds there, one just after sounds at once; REAL TIME's EDx and Kxx; the note offs; a take started by a key.
 * Where the loop is as the player says (its row and tick, 6 ticks a row); the notes track 0 starts and ends caught on
 * MIDI OUT's hook, each with where the loop was as it came */
static struct { uint8_t track, note, row, tick; } qev[64];
static int qn;
static void (*q_was)(uint8_t track, uint8_t note, uint8_t vel);
static void q_hook(uint8_t track, uint8_t note, uint8_t vel)
{
    if (qn < 64) { qev[qn].track = track; qev[qn].note = note; qev[qn].row = 0xFF; qn++; }
    if (q_was) q_was(track, note, vel);
}
static void q_run(uint32_t ms)
{
    player_state_t st;
    for (uint32_t i = 0; i < ms; i++) {
        run(1);
        player_state(&st, 0);
        for (int k = 0; k < qn; k++) if (qev[k].row == 0xFF) { qev[k].row = st.row; qev[k].tick = st.tick; }
    }
}
static void q_to(int row, int tick)                     /* on till the loop is there (as soon as the player says so) */
{
    player_state_t st;
    for (int i = 0; i < 20000; i++) { player_state(&st, 0); if (st.row == row && st.tick == tick) return; q_run(1); }
}
static void q_key(int n, int down)                      /* a key, taken with the loop where it is now */
{
    if (down) in_now.notes |= 1u << n; else in_now.notes &= ~(1u << n);
    app_poll();
}
static int q_heard(int note, int row, int tick)         /* track 0 started the note (0: ended its note) there (tick -1:
                                                         * any); row -1: where it first did, row << 8 | tick, -1 never */
{
    for (int k = 0; k < qn; k++)
        if (qev[k].track == 0 && qev[k].note == note && qev[k].row != 0xFF) {
            if (row < 0) return qev[k].row << 8 | qev[k].tick;
            if (qev[k].row == row && (tick < 0 || qev[k].tick == tick)) return 1;
        }
    return row < 0 ? -1 : 0;
}
static void q_clear(int f) { for (int r = 0; r < 16; r++) ed_put_cell((uint8_t)f, r, 0, 0); }
static int q_kx(int f, int r)                           /* track 0's key off Kxx on row r, -1 none */
{
    rec_cell_t c;
    if (!ed_cell((uint8_t)f, r, 0, &c) || !(c.has & RC_CMD)) return -1;
    for (int j = 0; j < c.n; j++) if (c.cmd[j][0] == 0x14) return c.cmd[j][1];
    return -1;
}
static void test_quantize(void)
{
    char msg[400], all[64] = "", got[96] = "";
    player_state_t st;
    for (int i = 0; i < UI_SETTINGS[S_QUANTIZE].n; i++) { strcat(all, i ? "," : ""); strcat(all, T(UI_SETTINGS[S_QUANTIZE].list[i])); }
    check(S_QUANTIZE == S_COUNT_IN + 1 && !strcmp(T(UI_SETTINGS[S_QUANTIZE].name), "QUANTIZE") &&
          !strcmp(all, "2 ROWS,3 ROWS,4 ROWS,ROW,REAL TIME") && ui_set[S_QUANTIZE] == 3,
          "SETTINGS: QUANTIZE after COUNT-IN: 2 ROWS, 3 ROWS, 4 ROWS, ROW, REAL TIME; ROW at first");
    /* the record: K1 turns it (an edit of the settings record), every value there and back */
    home_key(20);                                       /* SETTINGS */
    turn(K_SELECT, -30); turn(K_SELECT, S_QUANTIZE);
    uint8_t cur[8] = {0};
    ui_screen_now()->cursor(cur);
    turn(K_K1, -1);                                     /* 4 ROWS */
    shot("ui3_settings_quantize");
    uint32_t len;
    uint8_t ver, rec[133];
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(cur[1] == S_QUANTIZE && ui_set[S_QUANTIZE] == 2 && p && len == 133 && (p[109] >> 2 & 7) == 3 &&
          (p[109] & 3) == ui_set[S_COUNT_IN] && !(p[109] >> 5),
          "QUANTIZE 4 ROWS: the settings record's byte 109, bits 4-2 = 3, COUNT-IN's bits 1-0 as they were");
    static const uint8_t QREC[5] = {1, 2, 3, 0, 4};
    int ok = 1;
    for (int v = 0; v < 5; v++) {
        ui_set[S_QUANTIZE] = (int16_t)v;
        ui_settings_changed();
        p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
        ok &= p && (p[109] >> 2 & 7) == QREC[v];
        ui_set[S_QUANTIZE] = -1;
        ui_settings_reload();
        ok &= ui_set[S_QUANTIZE] == v;
    }
    check(ok, "QUANTIZE: each value into the record (0 ROW, 1-3 2-4 ROWS, 4 REAL TIME) and read back");
    p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    memcpy(rec, p, sizeof rec);
    rec[109] = 2;                                       /* (a record from before QUANTIZE: COUNT-IN 2 BARS alone) */
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, sizeof rec);
    ui_settings_reload();
    ok = ui_set[S_COUNT_IN] == 2 && ui_set[S_QUANTIZE] == 3;
    rec[109] = 3 | 6 << 2;                              /* (bits 4-2 = 6: none of its values) */
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, sizeof rec);
    ui_settings_reload();
    check(ok && ui_set[S_COUNT_IN] == 3 && ui_set[S_QUANTIZE] == 3,
          "QUANTIZE: a record from before it (bits 4-2 = 0) reads ROW, COUNT-IN as it was; 5-7 read ROW too");
    ui_edit_revert(REC_SETTINGS, 0, 0);
    ui_settings_reload();
    tap(B_HOME); run(200);

    /* the takes: an empty pattern of 16 rows, COUNT-IN OFF */
    int f = ed_first_free(), ci = ui_set[S_COUNT_IN];
    ed_new_pattern((uint8_t)f, 4, 16, 1);
    home_key(3);                                        /* the EDITOR */
    ed_editor_goto((uint8_t)f, 0, 0);
    ui_cur.mutes = 0; ui_cur.solo = -1; ui_mutes_changed();
    run(100);
    ui_set[S_COUNT_IN] = 0;
    q_was = player_note_hook;
    player_note_hook = q_hook;
    tap(B_REC);
    press(B_PLAY); run(40); release(B_PLAY);
    until_playing(500);
    player_state(&st, 0);
    snprintf(msg, sizeof msg, "QUANTIZE: a take of an empty pattern, 6 ticks a row (%d, BPM %d)", st.speed, song_bpm10() / 10);
    check(st.playing && st.what == PL_LOOP && st.pat == f && st.speed == 6, msg);
    /* where each key lands (its row; dN its EDx) and where it was heard: ! where it was pressed, @ at its row later */
    static const struct { uint8_t q, row, tick, key; } QC[10] = {
        {3, 5, 2, 0}, {3, 9, 4, 2},                     /* ROW: 5.33 on 5, at once; 9.67 on 10, waiting for it */
        {0, 4, 5, 4}, {0, 7, 1, 5},                     /* 2 ROWS: 4.83 on 4; 7.17 on 8 */
        {1, 4, 2, 7}, {1, 7, 4, 9},                     /* 3 ROWS: 4.33 on 3; 7.67 on 9 */
        {2, 5, 5, 11}, {2, 10, 1, 12},                  /* 4 ROWS: 5.83 on 4; 10.17 on 12 */
        {4, 5, 3, 14}, {4, 9, 0, 16},                   /* REAL TIME: 5.5 on 5, ED3; 9.0 on 9; both at once */
    };
    int as_keys = 0;
    for (int i = 0; i < 10; i++) {
        int k = QC[i].key, m = ui_midi(k), row = -1, d = 0;
        uint8_t kn[16];
        ui_set[S_QUANTIZE] = QC[i].q;
        q_clear(f);
        q_to(QC[i].row, QC[i].tick);
        qn = 0;
        q_key(k, 1);
        q_run(12);                                      /* (two halves: a note at once is heard by now) */
        for (int j = 0, nk = key_notes(kn, 16); j < nk; j++) if (kn[j] == m) as_keys = 1;
        q_to((QC[i].row + 4) % 16, 0);                  /* (past its line) */
        for (int r = 0; r < 16; r++) {
            rec_cell_t c;
            if (!ed_cell((uint8_t)f, r, 0, &c) || !(c.has & RC_NOTE) || c.note != m) continue;
            row = r;
            for (int j = 0; j < ((c.has & RC_CMD) ? c.n : 0); j++) if (c.cmd[j][0] == 0x0E && c.cmd[j][1] >> 4 == 0xD) d = c.cmd[j][1] & 15;
        }
        int h = q_heard(m, -1, -1), hr = h < 0 ? -1 : h >> 8;
        char w[16];
        if (hr == QC[i].row) strcpy(w, "!");
        else if (hr >= 0 && hr == row) strcpy(w, "@");
        else snprintf(w, sizeof w, "?%d", hr);
        snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d%s%s", i ? "," : "", row, d ? (d == 3 ? "d3" : "d?") : "", w);
        q_key(k, 0);
    }
    snprintf(msg, sizeof msg, "QUANTIZE: ROW 5.33 on 5 heard at once, 9.67 on 10 waiting for it; 2 ROWS 4.83 on 4, 7.17 on 8; "
             "3 ROWS 4.33 on 3, 7.67 on 9; 4 ROWS 5.83 on 4, 10.17 on 12; REAL TIME 5.5 on 5 with ED3, 9.0 on 9 (%s)", got);
    check(!strcmp(got, "5!,10@,4!,8@,3!,9@,4!,12@,5d3!,9!"), msg);
    check(!as_keys, "QUANTIZE: a take's keys heard by their track only, never as the keys");
    /* the note offs: ROW the nearest row, a row after its note at least (pressed at 3.17, let go at 3.33: on 4; pressed at
     * 6.17, let go at 8.67: on 9), each heard there; REAL TIME a key off at its tick (pressed at 3.5, let go at 3.67: K04
     * in its note's own cell; pressed at 6.17, let go at 8.5: K03 on 8), heard at once */
    int ka = UI_WHITE[3], kb = UI_WHITE[5];
    ui_set[S_QUANTIZE] = 3;
    q_clear(f);
    q_to(3, 1); qn = 0; q_key(ka, 1); q_to(3, 2); q_key(ka, 0);
    q_to(6, 1); q_key(kb, 1); q_to(8, 4); q_key(kb, 0);
    q_to(11, 0);
    char offs[32] = "";
    for (int r = 0; r < 16; r++) {
        rec_cell_t c;
        if (ed_cell((uint8_t)f, r, 0, &c) && (c.has & RC_NOTE) && c.note == REC_NOTE_OFF)
            snprintf(offs + strlen(offs), sizeof offs - strlen(offs), "%s%d", offs[0] ? "," : "", r);
    }
    snprintf(msg, sizeof msg, "note offs, ROW: the nearest row, a row after its note at least (3.33 on 4, 8.67 on 9: %s), "
             "heard there (%d %d)", offs, q_heard(0, 4, -1), q_heard(0, 9, -1));
    check(!strcmp(offs, "4,9") && q_heard(0, 4, -1) && q_heard(0, 9, -1), msg);
    ui_set[S_QUANTIZE] = 4;
    q_clear(f);
    q_to(3, 3); qn = 0; q_key(ka, 1); q_to(3, 4); q_key(ka, 0);
    q_to(6, 1); q_key(kb, 1); q_to(8, 3); q_key(kb, 0);
    q_to(11, 0);
    rec_cell_t c3;
    int n3 = ed_cell((uint8_t)f, 3, 0, &c3) && (c3.has & RC_NOTE) && c3.note == ui_midi(ka);
    snprintf(msg, sizeof msg, "note offs, REAL TIME: Kxx at its tick (3.67: K%02X in its note's own cell; 8.5: K%02X on 8), "
             "heard at once", q_kx(f, 3), q_kx(f, 8));
    check(n3 && q_kx(f, 3) == 4 && q_kx(f, 8) == 3 && q_heard(0, 3, 4) && q_heard(0, 8, 3), msg);
    qn = 0;                                             /* (the next pass: what REAL TIME wrote, at its ticks) */
    q_to(3, 0); q_to(9, 0);
    snprintf(msg, sizeof msg, "REAL TIME: the next pass plays them at their ticks (ED3 and K04 on 3; ED1 on 6, K03 on 8: "
             "%d %d %d %d)", q_heard(ui_midi(ka), 3, 3), q_heard(0, 3, 4), q_heard(ui_midi(kb), 6, 1), q_heard(0, 8, 3));
    check(q_heard(ui_midi(ka), 3, 3) && q_heard(0, 3, 4) && q_heard(ui_midi(kb), 6, 1) && q_heard(0, 8, 3), msg);
    /* a take started by a key (PLAY held + a key): written on the cursor's row, the loop playing it there as it starts;
     * let go in that row, its note off on the next, waiting for it */
    tap(B_PLAY); run(200);                              /* (the take stopped) */
    ui_set[S_QUANTIZE] = 3;
    q_clear(f);
    ed_editor_goto((uint8_t)f, 4, 0);
    run(100);
    qn = 0;
    int ks = UI_WHITE[7], ms = ui_midi(ks), as_key = 0;
    press(B_PLAY); q_key(ks, 1); release(B_PLAY);
    until_playing(500);
    q_to(4, 1);
    uint8_t kn[16];
    for (int j = 0, nk = key_notes(kn, 16); j < nk; j++) if (kn[j] == ms) as_key = 1;
    q_key(ks, 0);
    q_to(6, 0);
    rec_cell_t c4, c5;
    int w4 = ed_cell((uint8_t)f, 4, 0, &c4) && (c4.has & RC_NOTE) && c4.note == ms;
    int w5 = ed_cell((uint8_t)f, 5, 0, &c5) && (c5.has & RC_NOTE) && c5.note == REC_NOTE_OFF;
    snprintf(msg, sizeof msg, "a take started by a key: on the cursor's row (%d), heard there by its track as the loop "
             "starts (%d), not as the keys (%d); let go in that row, its note off on the next (%d), heard there (%d)", w4,
             q_heard(ms, 4, -1), !as_key, w5, q_heard(0, 5, -1));
    check(w4 && q_heard(ms, 4, -1) && !as_key && w5 && q_heard(0, 5, -1), msg);
    /* a chord started that way: its other keys, before the loop is seen playing (the player takes the take a half or two
     * later), land on the take's row too, on the next tracks, heard there with it */
    tap(B_PLAY); run(200);
    for (int t = 0; t < 3; t++) for (int r = 0; r < 16; r++) ed_put_cell((uint8_t)f, r, t, 0);
    ed_editor_goto((uint8_t)f, 4, 0);
    run(100);
    qn = 0;
    int kc[3] = {UI_WHITE[7], UI_WHITE[9], UI_WHITE[11]}, wc = 1, hc = 1;
    press(B_PLAY); q_key(kc[0], 1); q_key(kc[1], 1); q_key(kc[2], 1); release(B_PLAY);
    until_playing(500);
    q_to(5, 0);
    for (int j = 0; j < 3; j++) {
        rec_cell_t cc;
        int heard = 0;
        wc &= ed_cell((uint8_t)f, 4, j, &cc) && (cc.has & RC_NOTE) && cc.note == ui_midi(kc[j]);
        for (int k = 0; k < qn; k++) heard |= qev[k].track == j && qev[k].note == ui_midi(kc[j]) && qev[k].row == 4;
        hc &= heard;
    }
    for (int j = 0; j < 3; j++) q_key(kc[j], 0);
    q_to(7, 0);
    snprintf(msg, sizeof msg, "a chord starting a take: all three on the take's row, tracks 00-02 (%d), heard there (%d)", wc, hc);
    check(wc && hc, msg);
    tap(B_PLAY); run(200);
    /* the edit memory full during a take (filled with records nothing reads, to a few bytes from full): the take ends
     * there, its loop paused, MEMORY FOR EDITS IS FULL asked */
    int nd = 0, fullq = 0, nk = 0;
    static uint8_t junk[4000];
    for (uint32_t sz = sizeof junk; sz >= 16; sz /= 2)
        while (store_edit_room() > sz + 24 && nd < 240 && store_edit_put(REC_DJ, (uint8_t)(0x10 + nd), 0, 1, junk, sz) == STORE_OK) nd++;
    uint32_t room = store_edit_room();
    press(B_PLAY); run(40); release(B_PLAY);
    until_playing(500);
    for (; nk < 120 && !fullq; nk++) {
        key(UI_WHITE[nk % 14], 1); run(60); key(UI_WHITE[nk % 14], 0); run(60);
        fullq = ui_top() && ui_top()->kind == OV_QUESTION;
    }
    printf("  a take, the edit memory full: %u B left at its start, the question after %d keys\n", (unsigned)room, nk);
    run(100);
    player_state(&st, 0);
    snprintf(msg, sizeof msg, "a take with the edit memory full: MEMORY FOR EDITS IS FULL asked (%s), the take ended there (%s)",
             fullq ? asking() : "-", st.playing ? "still playing" : "paused");
    check(fullq && !st.playing, msg);
    if (ui_top()) tap(B_HOME);
    for (int k = 0; k < nd; k++) store_edit_forget(REC_DJ, (uint8_t)(0x10 + k), 0);
    player_state(&st, 0);
    if (st.playing) { tap(B_PLAY); run(200); }
    tap(B_REC);
    player_note_hook = q_was;
    ui_set[S_QUANTIZE] = 3;
    ui_set[S_COUNT_IN] = (int16_t)ci;
    ui_edit_revert(REC_PATTERN, (uint8_t)f, 0);
    run(200);
}
static void test_count(void)
{
    player_state_t st;
    home_key(3);                                        /* OP2: the EDITOR */
    check(screen_is("EDITOR"), "OP2 opens the EDITOR");
    ui_set[S_COUNT_IN] = 1;                             /* 1 BAR (SETTINGS' at first) */
    tap(B_REC);
    int edits = ui_edit_count();
    press(B_PLAY); run(40);
    uint32_t t0 = now_ms;                               /* (the EDITOR starts the take as PLAY is let go) */
    release(B_PLAY);                                    /* a tap: the take after the count */
    run(20);
    player_state(&st, 0);
    check(st.count_beats == 4 && !st.playing, "EDITOR: REC on, PLAY tapped, COUNT-IN 1 BAR: 4 beats counted first");
    check(listen(200) > 0, "the count-in clicks, nothing else playing");
    key(UI_WHITE[2], 1); run(60); key(UI_WHITE[2], 0);
    check(ui_edit_count() == edits, "a key during the count-in is heard, not written");
    uint32_t beat = 600000u / (uint32_t)song_bpm10();   /* ms a beat */
    run(t0 + beat + 200 - now_ms - 40);                 /* (the second beat, as the playground's shot) */
    shot("32_count_in");
    player_state(&st, 0);
    check(st.count_beat == 2, "the count-in's box: the second beat sounding");
    until_playing(4 * beat);
    uint32_t t = now_ms - t0;                           /* since PLAY was let go */
    char msg[160];
    snprintf(msg, sizeof msg, "the take starts on the count's fifth beat: 4 beats after PLAY (%u ms, a beat %u ms)",
             (unsigned)t, (unsigned)beat);
    check(t + 15 >= 4 * beat && t <= 4 * beat + 15, msg);
    player_state(&st, 0);
    check(st.what == PL_LOOP && !st.count_beats, "... as the EDITOR's take of its pattern");
    tap(B_PLAY);                                        /* stops it */
    run(200);
    /* PLAY during the count: stopped, nothing recorded */
    press(B_PLAY); run(40); release(B_PLAY); run(300);
    tap(B_PLAY); run(300);
    player_state(&st, 0);
    check(!st.count_beats && !st.playing, "PLAY during the count-in stops it");
    /* KEY: armed, the box says so; a note starts the take */
    ui_set[S_COUNT_IN] = 3;
    press(B_PLAY); run(40); release(B_PLAY); run(150);
    shot("32b_take_armed");
    player_state(&st, 0);
    check(!st.count_beats && !st.playing, "COUNT-IN KEY: armed, nothing counts, nothing plays");
    tap(B_HOME);
    check(ui_toast_now() && !strcmp(ui_toast_now(), "TAKE CANCELLED"), "HOME cancels the armed take");
    press(B_PLAY); run(40); release(B_PLAY); run(150);
    key(UI_WHITE[0], 1); run(100); key(UI_WHITE[0], 0); run(100);
    player_state(&st, 0);
    check(st.playing && st.what == PL_LOOP, "armed: a note starts the take");
    tap(B_PLAY); run(200);
    ui_set[S_COUNT_IN] = 1;
    tap(B_REC);                                         /* REC off */
    /* LOOPS: its own COUNT-IN before a take PLAY starts (the first loop's); HOME cancels it, armed no more */
    home_key(22);
    player_stop(); run(100);
    ui_edit_loops(loops_set_countin(1), ui_edit_used());
    tap(B_REC);                                         /* armed on + NEW LOOP, nothing playing */
    press(B_PLAY); run(40); release(B_PLAY); run(20);
    player_state(&st, 0);
    check(st.count_beats == 4 && !st.playing, "LOOPS: armed, PLAY: its COUNT-IN first");
    tap(B_HOME);
    const char *ts = ui_toast_now();
    snprintf(msg, sizeof msg, "LOOPS: HOME during the count-in: CANCELLED (%s), nothing starts, no loop", ts ? ts : "-");
    int was_cancel = ts && !strcmp(ts, "CANCELLED");
    run(3000);
    player_state(&st, 0);
    check(!st.playing && !st.count_beats && was_cancel && !loops_n(), msg);
    tap(B_REC);
    uint32_t l0 = now_ms;                               /* (LOOPS acts on PLAY's press) */
    press(B_PLAY); run(40); release(B_PLAY);
    key(UI_WHITE[3], 1); key(UI_WHITE[3], 0);           /* (heard, not written: the count) */
    until_playing(4000);
    uint32_t lt = now_ms - l0, lbeat = 60000u / (uint32_t)(loops_bpm() ? loops_bpm() : 120);
    player_state(&st, 0);
    snprintf(msg, sizeof msg, "LOOPS: the loops and the take start after the count-in's bar (%u ms, a beat %u ms, what %d)",
             (unsigned)lt, (unsigned)lbeat, st.what);
    check(st.what == PL_LOOPS && lt + 25 >= 4 * lbeat && lt <= 4 * lbeat + 25 && loops_n() == 1 && !loops_has_notes(0), msg);
    run(200);
    key(UI_WHITE[5], 1); key(UI_WHITE[5], 0);
    tap(B_REC);                                         /* (the first loop closed) */
    check(loops_n() == 1 && loops_note_count(0) == 1 && loops_rows() == 16, "LOOPS: a key in the take written, the one during "
          "the count not; REC closes the first loop (1 bar)");
    player_stop(); run(100);
    ui_edit_revert(REC_LOOPS, 0, 0);
    loops_revert();
    loops_changed(1);
}

/* --------------------------------------------------------- RECORD PATTERN --- */
/* specs/ui/dialogs.md#record-pattern: the EDITOR's SEL menu opens it; the white keys the steps; PLAY previews them (the
 * player plays them as a source of their own); REC writes them */
static void test_recpat(void)
{
    player_state_t st;
    static const int K[4] = {0, 3, 6, 10};
    char msg[160];
    /* the EDITOR: SEL's menu, RECORD PATTERN (its last line but REVERT); 12 steps, once, into track 01 from row 00 */
    home_key(3);
    uint8_t P = ui_cur.pattern;
    ed_editor_goto(P, 0, 1);
    run(50);
    tap(B_SEL);
    turn(K_SELECT, 30);
    if (ui_edit_unsaved(REC_PATTERN, P, 0)) turn(K_SELECT, -1);
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_OTHER, "EDITOR: SEL's menu opens RECORD PATTERN");
    ui_rp.steps = 0;                                   /* (the white keys the steps; PLAY previews them) */
    for (int i = 0; i < 4; i++) { key(UI_WHITE[K[i]], 1); key(UI_WHITE[K[i]], 0); }
    check(ui_rp.steps == (1u | 8u | 64u | 1024u), "the white keys switch the steps on");
    press(B_PLAY); run(40); release(B_PLAY); run(300);
    player_state(&st, 0);
    int64_t e = listen(800);
    check(st.what == PL_PREVIEW && st.playing && e > 0, "PLAY previews the steps on their own (PL_PREVIEW), heard");
    tap(B_PLAY); run(100);
    player_state(&st, 0);
    check(st.what != PL_PREVIEW && !st.playing, "PLAY again stops the preview, what played before back");
    turn(K_K3, -4); turn(K_K4, 1);
    shot("28d_editor_pattern");
    check(ui_rp.size == 12 && !ui_rp.round, "K3 the size, K4 once");
    tap(B_REC); run(100);
    ui_song_t sg;
    int esr = ui_rp_step_rows(ui_song(ui_cur.song, &sg) && sg.rpb ? sg.rpb : 4), got = !ui_top();
    for (int i = 0; i < 4; i++) {
        rec_cell_t c;
        got &= ed_cell(P, K[i] * esr, 1, &c) && (c.has & RC_NOTE) && c.note == ui_rp.note;
    }
    rec_cell_t past;
    got &= !(ed_cell(P, 12 * esr, 1, &past) && (past.has & RC_NOTE) && past.note == ui_rp.note);   /* once: 12 steps */
    snprintf(msg, sizeof msg, "REC writes the steps into the EDITOR's pattern %02X, track 01 (step x %d), the dialog closed", P, esr);
    check(got, msg);
    /* no step on: REC refused, the dialog stays; HOME cancels */
    ui_rp.steps = 0;
    tap(B_SEL);
    turn(K_SELECT, 30);
    if (ui_edit_unsaved(REC_PATTERN, P, 0)) turn(K_SELECT, -1);
    tap(B_SEL);
    tap(B_REC); run(100);
    int stays = ui_top() && ui_top()->kind == OV_OTHER;
    tap(B_HOME); run(100);
    check(stays && !ui_top(), "no step on: REC refused (the dialog stays), HOME cancels");
    ui_rp.size = 16; ui_rp.round = 1;
}

/* ------------------------------------------------------------- AUTO LEVEL --- */
/* specs/files/instruments.md, LEVEL: measured as the demo disk's were (held 1 s of 1.2 s, the loudest 300 ms at
 * -20 dB, the peak under -6 dB), so measuring the disk's instruments gives back their LEVELs (the built-in synths'
 * too: measured, not set) within its half-dB steps; then INSTRUMENTS' AUTO LEVEL, a cartridge's AUTO LEVEL ALL and
 * SETTINGS' set them, as one edit each */
static int level_of(uint8_t c, int s)
{
    uint8_t b[128];
    if (!ins_slot_bytes(c, s, b)) return -999;
    ins_prm_t p = {0, 0, (uint16_t)ins_level_pos(ins_kind(c)), 8, PK_LEVEL, -128, 127, 0};   /* (LEVEL: no name needed) */
    return ins_get(b, &p);
}
static int bar_shown(void)                              /* (SEL's and EDIT's taps wait out their grace first) */
{
    for (int i = 0; i < 400; i++) { if (ui_top() && ui_top()->kind == OV_PROGRESS) return 1; run(1); }
    return 0;
}
static void test_auto_level(void)
{
    uint8_t carts[256], b[128];
    int nc = ui_carts(carts, 256), n = 0, same = 0, half = 0, worst = 0, wc = -1, ws = -1, silent = 0, fn = 0, fsame = 0;
    int by_eng[6][2] = {{0}};
    for (int i = 0; i < nc; i++) {
        int k = ins_kind(carts[i]), e = k == REC_DX7 ? 0 : k == REC_JUNO ? 1 : k == REC_TB303 ? 2 : k == REC_SAMPLER ? 3 :
                                        k == REC_CHIP ? 4 : 5;
        for (int s = 0; s < ui_cart_slots(carts[i]); s++) {
            int8_t lv;
            if (!ins_slot_bytes(carts[i], s, b)) continue;
            if (!ui_level_measure(carts[i], s, &lv)) { silent++; continue; }
            int d = lv - level_of(carts[i], s), ad = d < 0 ? -d : d;
            if (e == 5) {                               /* (FUSION: the disk's set by other rules) */
                fn++;
                fsame += ad <= 2;
                char nm[16];
                ui_inst_name((uint16_t)(carts[i] << 8 | s), nm);
                printf("  AUTO LEVEL FUSION %02X:%02X %-10s measured %+.1f dB, the disk's %+.1f dB\n", carts[i], s, nm,
                       lv / 2.0, level_of(carts[i], s) / 2.0);
                continue;
            }
            n++;
            by_eng[e][0]++;
            by_eng[e][1] += !d;
            same += !d;
            half += ad <= 1;
            if (ad > 1) {
                char nm[16];
                ui_inst_name((uint16_t)(carts[i] << 8 | s), nm);
                printf("  AUTO LEVEL %02X:%02X %-10s measured %+.1f dB, the disk's %+.1f dB\n", carts[i], s, nm, lv / 2.0,
                       level_of(carts[i], s) / 2.0);
            }
            if (ad > worst) { worst = ad; wc = carts[i]; ws = s; }
        }
    }
    printf("AUTO LEVEL on the demo disk: %d instruments, %d the same (DX7 %d/%d, JUNO %d/%d, TB-303 %d/%d, SAMPLER "
           "%d/%d, CHIP %d/%d), %d within 0.5 dB, worst %d.%d dB (%02X:%02X); FUSION %d of %d within 1 dB; silent %d\n",
           n, same, by_eng[0][1], by_eng[0][0], by_eng[1][1], by_eng[1][0], by_eng[2][1], by_eng[2][0],
           by_eng[3][1], by_eng[3][0], by_eng[4][1], by_eng[4][0], half, worst / 2, worst & 1 ? 5 : 0, wc, ws, fsame, fn,
           silent);
    /* (the samplers' few others are made on another's sample and keep its LEVEL, as the disk's rule has it) */
    check(n > 500 && by_eng[0][1] == by_eng[0][0] && by_eng[1][1] == by_eng[1][0] && by_eng[2][1] == by_eng[2][0] &&
          by_eng[3][1] * 100 >= by_eng[3][0] * 80 && by_eng[4][0] == 128 && by_eng[4][1] == by_eng[4][0],
          "AUTO LEVEL measures the demo disk's instruments back to their LEVELs (every synth's exactly, CHIP's 128 "
          "too, the samplers' but those on another's sample)");
    /* INSTRUMENTS: a sampler's slot, its SEL menu's AUTO LEVEL (its last line but REVERT) */
    int sc = -1;
    for (int i = 0; i < nc && sc < 0; i++) if (ins_kind(carts[i]) == REC_SAMPLER && !ins_builtin(carts[i])) sc = carts[i];
    check(sc >= 0, "the demo disk has a sampler in the user area");
    if (sc < 0) return;
    int8_t want;
    ui_level_measure((uint8_t)sc, 0, &want);
    int lv0 = level_of((uint8_t)sc, 0);                 /* (as saved) */
    uint8_t s0[128];
    ins_slot_bytes((uint8_t)sc, 0, s0);
    ins_prm_t lp = {0, 0, (uint16_t)ins_level_pos(REC_SAMPLER), 8, PK_LEVEL, -128, 127, 0};
    ins_set(s0, &lp, want + 8);                         /* (4 dB off first, so AUTO LEVEL has to change it) */
    ins_put_slot((uint8_t)sc, 0, s0);
    ui_set_inst((uint16_t)(sc << 8));
    home_key(5);
    tap(B_SEL);
    turn(K_SELECT, 30);
    if (store_piece(REC_SAMPLER, (uint8_t)sc, 0, 0)) turn(K_SELECT, -1);
    press(B_SEL); release(B_SEL);
    check(bar_shown(), "INSTRUMENTS' AUTO LEVEL: a progress bar");
    shot("34_auto_level");
    run(600);
    char t[40], msg[160];
    snprintf(t, sizeof t, "LEVEL SET TO %s%d%s DB", want > 0 ? "+" : want < 0 ? "-" : "", (want < 0 ? -want : want) / 2,
             want & 1 ? ".5" : "");
    check(!ui_top() && level_of((uint8_t)sc, 0) == want && ui_toast_now() && !strcmp(ui_toast_now(), t),
          "... then the slot's LEVEL measured and set, its toast");
    /* the slot's REVERT (its menu's last line while the slot has a change), after its question: as saved */
    ins_set(s0, &lp, want + 8);
    ins_put_slot((uint8_t)sc, 0, s0);
    tap(B_SEL); turn(K_SELECT, 30); tap(B_SEL);         /* REVERT */
    check(ui_top() && ui_top()->kind == OV_QUESTION, "the slot's REVERT asks first");
    tap(B_OCTP);
    check(level_of((uint8_t)sc, 0) == lv0 && !store_piece(REC_SAMPLER, (uint8_t)sc, 0, 0), "REVERT: the slot as saved");
    /* a cartridge's AUTO LEVEL ALL (ENV's list, its SEL menu; asked first), and SETTINGS' (every sound of the user
     * area) */
    int before = 0, after_n = 0;
    for (int s = 0; s < ui_cart_slots((uint8_t)sc); s++) before += ins_slot_bytes((uint8_t)sc, s, b);
    tap(B_ENV); run(50);
    tap(B_SEL); turn(K_SELECT, 30);                     /* (… RENAME, AUTO LEVEL ALL: its last line) */
    if (ui_edit_unsaved(REC_SAMPLER, (uint8_t)sc, 0)) turn(K_SELECT, -1);     /* (REVERT last while unsaved) */
    tap(B_SEL);                                         /* (the pick comes after SEL's grace) */
    check(ui_top() && ui_top()->kind == OV_QUESTION && ui_overlay_words(ui_top()) &&
          !strncmp(ui_overlay_words(ui_top()), "AUTO LEVEL ", 11), "a cartridge's AUTO LEVEL ALL asks first");
    press(B_OCTP); release(B_OCTP);                     /* LEVEL (the bar looked for at once) */
    int bar = bar_shown();
    shot("34b_auto_level_all");
    for (int i = 0; i < 400 && ui_top() && ui_top()->kind == OV_PROGRESS; i++) run(50);
    for (int s = 0; s < ui_cart_slots((uint8_t)sc); s++) {
        int8_t lv;
        if (ins_slot_bytes((uint8_t)sc, s, b) && ui_level_measure((uint8_t)sc, s, &lv) && level_of((uint8_t)sc, s) == lv) after_n++;
    }
    snprintf(msg, sizeof msg, "a cartridge's AUTO LEVEL ALL: a bar, every slot's LEVEL as measured (%d of %d; slot 00 "
             "%d, %d wanted), LEVELS SET (%s)", after_n, before, level_of((uint8_t)sc, 0), want, ui_toast_now() ? ui_toast_now() : "");
    check(bar && after_n == before && level_of((uint8_t)sc, 0) == want && ui_toast_now() && !strcmp(ui_toast_now(), "LEVELS SET"),
          msg);
    tap(B_HOME); run(100);
    home_key(20);
    turn(K_SELECT, 20); turn(K_SELECT, -1);             /* TOOLS */
    turn(K_K1, -9); turn(K_K1, 3);                      /* AUTO LEVEL ALL */
    tap(B_EDIT);                                        /* (the pick comes after EDIT's grace) */
    check(ui_top() && ui_top()->kind == OV_QUESTION, "SETTINGS' AUTO LEVEL ALL asks first");
    press(B_OCTP); release(B_OCTP);                     /* LEVEL (the bar looked for at once) */
    bar = bar_shown();
    for (int i = 0; i < 2000 && ui_top(); i++) run(50);
    check(bar && !ui_top() && ui_toast_now() && !strcmp(ui_toast_now(), "LEVELS SET"),
          "SETTINGS' AUTO LEVEL ALL: MEASURING EVERY SOUND, then LEVELS SET");
    turn(K_K1, -9);
    ui_edit_drop();                                     /* (REVERT ALL: the levels as saved for what follows) */
    tap(B_HOME); run(100);
}

/* ------------------------------------------------------- CLONE / MOVE samples --- */
/* specs/ui/screens/instruments.md: a sampler's CLONE copies its samples' audio too, written at once (the edit
 * memory cannot hold audio); MOVE takes them along (before, they stayed under the old number: silent) */
static int same_audio(uint8_t a, uint8_t b, int *n)
{
    *n = 0;
    for (int s = 0; s < 64; s++) {
        uint32_t la, lb;
        uint8_t va, vb;
        const uint8_t *pa = store_get(REC_AUDIO, a, (uint8_t)s, &la, &va), *pb = store_get(REC_AUDIO, b, (uint8_t)s, &lb, &vb);
        if (!pa && !pb) continue;
        if (!pa || !pb || la != lb || va != vb || memcmp(pa, pb, la)) return 0;
        (*n)++;
    }
    return 1;
}
static void test_clone_samples(void)
{
    uint8_t carts[256];
    int nc = ui_carts(carts, 256), last = -1, n = 0, src = -1, big = -1;
    uint32_t least = 0xFFFFFFFFu, most = 0;
    for (int i = 0; i < nc; i++) {                      /* (the smallest sampler cloned and moved; the largest
                                                         * deleted first) */
        if (carts[i] <= 0xEF && carts[i] > last) last = carts[i];
        store_key_t k[STORE_MOVE_MAX];
        int nk = store_thing(REC_SAMPLER, carts[i], 0, k, STORE_MOVE_MAX);
        uint32_t b = store_thing_bytes(k, nk);
        if (ins_kind(carts[i]) != REC_SAMPLER || ins_builtin(carts[i]) || nk < 2) continue;
        if (b < least) { least = b; src = carts[i]; }
        if (b > most) { most = b; big = carts[i]; }
    }
    check(src > 0 && big > 0 && big != src, "the demo disk has samplers with samples in the user area");
    if (src < 0 || big < 0 || big == src) return;
    /* the room for two more copies of its audio, the clone and its move, whatever the disk leaves: the largest
     * other sampler deleted with its audio, its blocks free at once (this runs last: nothing after needs it) */
    store_key_t dk[STORE_MOVE_MAX];
    int nd = store_thing(REC_SAMPLER, (uint8_t)big, 0, dk, STORE_MOVE_MAX), del = store_save_begin() == STORE_OK;
    for (int i = 0; del && i < nd; i++) del = store_save_del(dk[i].kind, dk[i].num, dk[i].sub) == STORE_OK;
    del = del && store_save_run() == STORE_OK;
    player_store_changed();
    store_usage_t su;
    store_usage(&su);
    printf("CLONE / MOVE: sampler %02X, %u B (sampler %02X deleted first, %u B); the user area %u of %u blocks free\n",
           src, least, big, most, su.user_free_blocks, su.user_blocks);
    check(del, "the largest sampler deleted, its audio with it");
    int8_t lv0, lv1;
    ui_level_measure((uint8_t)src, 0, &lv0);
    player_pause();
    ui_set_inst((uint16_t)(src << 8));
    home_key(5);
    tap(B_ENV); run(50);                                /* the cartridges, the sampler highlighted */
    tap(B_SEL); turn(K_SELECT, -30); turn(K_SELECT, 1); /* NEW, CLONE (the menu keeps its last line) */
    press(B_SEL); release(B_SEL);
    check(bar_shown(), "CLONE of a sampler with samples: a progress bar (written at once)");
    shot("35_clone_samples");
    for (int i = 0; i < 200 && ui_top() && ui_top()->kind == OV_PROGRESS; i++) run(50);
    int to = last + 1;
    char want[40], msg[200], nm[16], nm0[16];
    snprintf(want, sizeof want, "%02X CLONED TO %02X", src, to);
    rec_cart_t rc;
    rec_cart((uint8_t)src, &rc);
    rec_cart_name(&rc, nm0);
    nm0[9] = 0;
    strcat(nm0, " 2");
    int named = !rec_cart((uint8_t)to, &rc) && (rec_cart_name(&rc, nm), !strncmp(nm, nm0, strlen(nm0)));
    int same = same_audio((uint8_t)src, (uint8_t)to, &n);
    lv1 = -99;
    ui_level_measure((uint8_t)to, 0, &lv1);
    snprintf(msg, sizeof msg, "... its samples' audio copied under %02X (%d, byte for byte), named %s, sounding as the "
             "original (LEVEL %d, %d), saved (no edit), %s", to, n, nm0, lv1, lv0, want);
    check(named && same && n > 0 && lv1 == lv0 && store_edit_has(REC_SAMPLER, (uint8_t)to, 0) == 0 && ui_toast_now() &&
          !strcmp(ui_toast_now(), want), msg);
    /* MOVE the clone one number on: its audio along, nothing left under the old number, the original untouched */
    tap(B_SEL); turn(K_SELECT, -30); turn(K_SELECT, 2); /* NEW, CLONE, MOVE */
    tap(B_SEL);
    turn(K_SELECT, 1);
    press(B_SEL); release(B_SEL);
    int bar = bar_shown();
    for (int i = 0; i < 200 && ui_top() && ui_top()->kind == OV_PROGRESS; i++) run(50);
    uint32_t len;
    uint8_t ver;
    int gone = !store_get(REC_SAMPLER, (uint8_t)to, 0, &len, &ver), left = 0;
    for (int s = 0; s < 64; s++) left += store_get(REC_AUDIO, (uint8_t)to, (uint8_t)s, &len, &ver) != 0;
    int n2;
    same = same_audio((uint8_t)src, (uint8_t)(to + 1), &n2);
    lv1 = -99;
    ui_level_measure((uint8_t)(to + 1), 0, &lv1);
    snprintf(msg, sizeof msg, "MOVE of a sampler with samples: a bar, its audio along to %02X (%d), none left under %02X "
             "(%d), sounding (LEVEL %d), %s", to + 1, n2, to, left, lv1, ui_toast_now() ? ui_toast_now() : "");
    check(bar && gone && !left && same && n2 == n && lv1 == lv0, msg);
    tap(B_HOME); run(100);
    tap(B_HOME); run(100);
    /* as after a power cycle: the flash read again */
    store_mount();
    player_store_changed();
    gone = !store_get(REC_SAMPLER, (uint8_t)to, 0, &len, &ver);
    left = 0;
    for (int s = 0; s < 64; s++) left += store_get(REC_AUDIO, (uint8_t)to, (uint8_t)s, &len, &ver) != 0;
    check(gone && !left && same_audio((uint8_t)src, (uint8_t)(to + 1), &n2) && n2 == n &&
          store_get(REC_SAMPLER, (uint8_t)(to + 1), 0, &len, &ver), "... and so after the flash is read again (a power cycle)");
    /* a MOVE with changes waiting: refused as it is put down, before any question */
    uint8_t sb[128];
    ins_prm_t lp = {0, 0, (uint16_t)ins_level_pos(REC_SAMPLER), 8, PK_LEVEL, -128, 127, 0};
    ins_slot_bytes((uint8_t)(to + 1), 0, sb);
    ins_set(sb, &lp, ins_get(sb, &lp) + 1);
    ins_put_slot((uint8_t)(to + 1), 0, sb);             /* (an edit: its LEVEL half a dB up) */
    ui_set_inst((uint16_t)((to + 1) << 8));
    home_key(5);
    tap(B_ENV); run(50);
    tap(B_SEL); turn(K_SELECT, -30); turn(K_SELECT, 2); /* MOVE */
    tap(B_SEL);
    turn(K_SELECT, 1);
    tap(B_SEL);
    snprintf(msg, sizeof msg, "a sampler's MOVE with changes waiting: SAVE IT FIRST, nothing moved (%s; edit %d)",
             ui_toast_now() ? ui_toast_now() : "", store_edit_has(REC_SAMPLER, (uint8_t)(to + 1), 0));
    check(ui_toast_now() && !strcmp(ui_toast_now(), "SAVE IT FIRST") && store_get(REC_SAMPLER, (uint8_t)(to + 1), 0, &len, &ver),
          msg);
    ui_edit_revert_slot((uint8_t)(to + 1), 0);          /* (the slot as saved again) */
    tap(B_HOME); run(100);
    tap(B_HOME); run(100);
}

/* ------------------------------------------------------- UPDATE REFERENCES? --- */
/* specs/ui/saving.md#references: YES makes the change with its references, pauses playback and writes everything
 * pending as one save (its bar, then REFERENCES UPDATED, SAVED once written: nothing unsaved after); a used slot
 * SWAPped with the next one, then back (the demo disk as it was) */
static int refs_swap(int c, int k, const char *what)
{
    char msg[200];
    player_state_t st;
    ui_play_song(0, 0);
    run(300);
    ui_player_state(&st);
    int played = st.playing;
    ui_set_inst((uint16_t)(c << 8 | k));
    home_key(5);
    tap(B_SEL);
    int swap = menu_to("SWAP");
    if (!swap) { check(0, "the slot menu has SWAP"); tap(B_HOME); player_stop(); return 0; }
    tap(B_SEL);                                         /* (picked after SEL's grace: then SELECT floats the slot) */
    turn(K_SELECT, 1);
    tap(B_SEL);                                         /* put down: the question */
    int asked = ui_top() && ui_top()->kind == OV_QUESTION && swap;
    press(B_OCTP);                                      /* YES (taken as it is let go), looked at every ms: the bar up, */
    in_now.buttons &= ~(1u << B_OCTP);                  /* playback paused by its end, no toast yet */
    int bar = 0, toast_early = 0;
    for (int i = 0; i < 60; i++) {
        run(1);
        if (!ui_top() || ui_top()->kind != OV_PROGRESS) continue;
        bar = 1;
        toast_early |= ui_toast_now() && !strcmp(ui_toast_now(), "REFERENCES UPDATED, SAVED");
    }
    for (int i = 0; i < 400 && ui_top() && ui_top()->kind == OV_PROGRESS; i++) run(50);
    ui_player_state(&st);
    int paused = !st.playing;
    snprintf(msg, sizeof msg, "UPDATE REFERENCES? YES (%s): asked %d, a bar %d, playback paused %d (played %d), the toast "
             "only once written (%s, early %d), nothing unsaved (%u, edits %d)", what, asked, bar, paused, played,
             ui_toast_now() ? ui_toast_now() : "", toast_early, ui_unsaved, store_edit_count());
    int ok = asked && played && paused && bar && !toast_early && ui_toast_now() &&
             !strcmp(ui_toast_now(), "REFERENCES UPDATED, SAVED") && !ui_unsaved && !store_edit_count();
    check(ok, msg);
    player_stop();
    tap(B_HOME); run(100);
    return ok;
}
static void test_refs_save(void)
{
    uint8_t carts[256], s[128];
    int nc = ui_carts(carts, 256), c = -1, k = -1;
    for (int i = 0; i < nc && c < 0; i++) {
        if (carts[i] > 0xEF) continue;
        for (int j = 0; j + 1 < ui_cart_slots(carts[i]); j++)
            if (ins_slot_bytes(carts[i], j, s) && ed_inst_uses((uint16_t)(carts[i] << 8 | j)) > 0) { c = carts[i]; k = j; break; }
    }
    check(c >= 0, "the demo disk has a used instrument in the user area");
    if (c < 0) return;
    int u0 = ed_inst_uses((uint16_t)(c << 8 | k)), u1 = ed_inst_uses((uint16_t)(c << 8 | (k + 1)));
    printf("UPDATE REFERENCES?: slot %02X:%02X (%d uses) and %02X (%d)\n", c, k, u0, k + 1, u1);
    refs_swap(c, k, "SWAP");
    check(ed_inst_uses((uint16_t)(c << 8 | (k + 1))) == u0 && ed_inst_uses((uint16_t)(c << 8 | k)) == u1,
          "... the references along with the slots");
    refs_swap(c, k, "SWAP back");
    store_mount();                                      /* (as after a power cycle: the flash read again) */
    player_store_changed();
    check(ed_inst_uses((uint16_t)(c << 8 | k)) == u0 && ed_inst_uses((uint16_t)(c << 8 | (k + 1))) == u1,
          "... and back, as the flash has it after a power cycle");
}

/* --------------------------------------------------------------------- DJ --- */
static void test_dj(void)
{
    home_key(10);                                       /* OP5: DJ */
    check(screen_is("DJ"), "OP5 opens DJ");
    key(8, 1); key(8, 0);                              /* OP4: LOAD the highlighted song (the first) on the left */
    check(dj_deck(0)[DK_SONG] != DJ_NO_SONG, "DJ: OP4 loads the highlighted song on the left deck");
    turn(K_SELECT, 1);
    key(10, 1); key(10, 0);                             /* OP5: the next one on the right */
    check(dj_deck(1)[DK_SONG] != DJ_NO_SONG && dj_deck(1)[DK_SONG] != dj_deck(0)[DK_SONG], "DJ: OP5 loads the next on the right");
    check(dj_sync(), "DJ: SYNC on at first");
    int b0 = dj_deck_bpm10(0);
    turn(K_K1, 3);
    check(dj_deck_bpm10(0) == b0 + 3, "DJ: K1 the left deck's tempo, 0.1 BPM a click");
    dj_deck_set_bpm10(0, 32);                           /* the range: 3 to 600 BPM, whatever its song's own */
    turn(K_K1, -5);
    int lo_dj = dj_deck_bpm10(0);
    dj_deck_set_bpm10(0, 5997);
    turn(K_K1, 5);
    check(lo_dj == 30 && dj_deck_bpm10(0) == 6000, "DJ: a deck's tempo from 3 to 600 BPM, whatever its song's own");
    dj_deck_set_bpm10(0, b0 + 4);
    turn(K_K1, -1);                                     /* (b0 + 3 again, the player told) */
    turn(K_K4, 1);                                      /* synced: nothing */
    turn(K_ALG, 8);
    check(dj_xf() == 8, "DJ: ALGORITHM the crossfader");
    check((dj_deck(0)[DK_FLAGS] & DF_FILTER_ON) && (dj_deck(1)[DK_FLAGS] & DF_FILTER_ON) && !dj_deck(0)[DK_FILTER] &&
          !dj_deck(1)[DK_FILTER], "DJ: a loaded deck's FILTER on at OFF");
    turn(K_K2, -10);
    check((int8_t)dj_deck(0)[DK_FILTER] == -10 && (dj_deck(0)[DK_FLAGS] & DF_FILTER_ON), "DJ: K2 the left's filter (LP) at once");
    key(5, 1); key(5, 0);                               /* OP3: the left's FILTER off, and on again */
    check(!(dj_deck(0)[DK_FLAGS] & DF_FILTER_ON), "DJ: OP3 switches the left's FILTER off");
    key(5, 1); key(5, 0);
    check(dj_deck(0)[DK_FLAGS] & DF_FILTER_ON, "DJ: OP3 again: on");
    {                                                   /* LFO held: its layer the LFO pads' keys; let go, the pads the LFOs' */
        const char *before = ui_screen_now()->sub();
        int was_play = before && !strncmp(before, "PLAY", 4);
        press(B_LFO);
        run(700);
        shot("ui3_dj_lfo_layer");
        int pads_dim = 1;
        release(B_LFO);
        run(200);
        const char *after = ui_screen_now()->sub();
        check(was_play && after && !strncmp(after, "LFO", 3), "DJ: LFO held then let go: the pads the LFOs'");
        for (int wi = 0; wi < 16; wi++) {               /* each four's first dim, the rest dark (nothing on) */
            int v = leds[LED_NOTE(UI_WHITE[wi])];
            if (wi % 4 == 0 ? v != LV_DIM : v != 0) pads_dim = 0;
        }
        check(pads_dim, "DJ: the LFO pads lit: each four's first dim, the rest dark");
        press(B_LFO);
        run(700);
        release(B_LFO);
        run(200);
        after = ui_screen_now()->sub();
        check(after && !strncmp(after, "LFO", 3), "DJ: LFO held on the LFO pads: they stay");
        tap(B_LFO);
        after = ui_screen_now()->sub();
        check(after && !strncmp(after, "PLAY", 4), "DJ: LFO tapped: back to the pads before");
    }
    tap(B_ENV);
    turn(K_K3, -3);
    check((int8_t)dj_deck(1)[DK_VOL] == -3, "DJ: ENV, K3 the right deck's volume");
    tap(B_ENV);
    shot("ui3_dj_decks");
    uint8_t l = dj_deck(0)[DK_SONG], r = dj_deck(1)[DK_SONG];
    tap(B_SEQ);
    check(dj_deck(0)[DK_SONG] == r && dj_deck(1)[DK_SONG] == l && dj_xf() == 24, "DJ: SEQ swaps the decks, the crossfader mirrored");
    tap(B_SEQ);
    tap(B_FX);
    check(screen_is("FX"), "FX from DJ");
    tap(B_FX);
    check(screen_is("DJ"), "FX again goes back to DJ");
    /* MOVE held, K1 a bar, let go: the deck there; SET CUE there */
    key(UI_WHITE[3], 1); turn(K_K1, 2); key(UI_WHITE[0], 1); key(UI_WHITE[0], 0); key(UI_WHITE[3], 0);
    check(dj_deck(0)[DK_CUE_ROW] > 0 || dj_deck(0)[DK_CUE_POS] > 0, "DJ: SET CUE with MOVE held sets the cue at the cursor");
    check(dj_deck(0)[DK_ROW] > 0 || dj_deck(0)[DK_POS] > 0, "DJ: MOVE let go moves the deck");
    shot("ui3_dj_moved");
    /* the sound: the left deck plays (the crossfader towards the left), then both, the crossfader past the middle:
     * the right deck heard, in time */
    turn(K_ALG, -8);
    key(1, 1); key(1, 0);                               /* OP1: the left deck plays */
    run(300);
    dj_deck_state_t ds;
    dj_deck_state(0, &ds);
    player_state_t pst;
    player_state(&pst, 0);
    check(pst.what == PL_DJ && pst.playing && ds.playing, "DJ: OP1 plays the left deck (PL_DJ)");
    check(listen(1500) > 0, "DJ: the left deck sounds");
    shot("ui3_dj_playing");
    key(13, 1); key(13, 0);                             /* OP6: the right deck, synced: on the main's bar line */
    run(4000);
    dj_deck_state(1, &ds);
    check(ds.playing, "DJ: OP6 started the right deck on the main's bar line");
    turn(K_ALG, 24);                                    /* past the middle: the right deck heard */
    run(400);
    check(listen(1500) > 0, "DJ: the right deck heard past the middle");
    tap(B_PLAY);                                        /* both paused */
    run(300);
    player_state(&pst, 0);
    check(!pst.playing, "DJ: PLAY pauses both decks");
    home_key(8);                                        /* leaving: the stand into the edit memory */
    run(800);
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_DJ, 0, 0, &len, &ver);
    check(p && len == DJ_REC_LEN && p[88] == l && p[112] == r, "DJ: the stand in the edit memory (both decks' songs)");
    /* DJ apart till its first start: a song played elsewhere plays on into DJ, the decks untouched; PLAY pauses it;
     * a deck's PLAY key stops it (its sound cut) and plays the deck */
    home_key(1);                                        /* SONGS: the song plays */
    tap(B_PLAY);
    run(300);
    home_key(10);
    run(300);
    player_state(&pst, 0);
    dj_deck_state(0, &ds);
    check(pst.what == PL_SONG && pst.playing && !ds.playing && dj_deck(0)[DK_SONG] == l && !dj_live(),
          "DJ opened with a song playing: it plays on, the decks untouched (DJ apart)");
    tap(B_PLAY);
    player_state(&pst, 0);
    check(pst.what == PL_SONG && !pst.playing && ui_toast_now() && !strcmp(ui_toast_now(), "PAUSED: PLAY AGAIN FOR DJ"),
          "DJ: PLAY with a song playing pauses it (PAUSED: PLAY AGAIN FOR DJ)");
    home_key(1);
    tap(B_PLAY);
    run(300);
    home_key(10);
    key(1, 1); key(1, 0);                               /* OP1 */
    run(300);
    player_state(&pst, 0);
    dj_deck_state(0, &ds);
    check(pst.what == PL_DJ && pst.playing && ds.playing && dj_live(), "DJ: a deck's PLAY key with a song playing: DJ's, the deck plays");
    press(B_PLAY);
    run(1200);                                          /* (held past the hold delay: back to the cues) */
    release(B_PLAY);
    run(200);
    player_state(&pst, 0);
    check(!pst.playing && dj_deck(0)[DK_POS] == dj_deck(0)[DK_CUE_POS] && dj_deck(0)[DK_ROW] == dj_deck(0)[DK_CUE_ROW] &&
          ui_toast_now() && !strcmp(ui_toast_now(), "BACK TO THE CUES"), "DJ: PLAY held: both decks back to their cue points, paused");
    key(1, 1); key(1, 0);
    run(300);
    press(B_HOME); run(50); tap(B_PLAY); release(B_HOME); run(200);
    player_state(&pst, 0);
    check(!pst.playing && ui_toast_now() && !strcmp(ui_toast_now(), "BACK TO THE CUES"), "DJ: HOME + PLAY: back to the cues too");
    home_key(8);
    run(800);
    /* two DJ loops (one position of 8 bars each): the overview's columns as wide as their bars, not a column a bar */
    home_key(10);
    turn(K_SELECT, -40); turn(K_SELECT, 6);
    key(8, 1); key(8, 0);                               /* OP4: 06 on the left */
    turn(K_SELECT, 5);
    key(10, 1); key(10, 0);                             /* OP5: 0B on the right */
    check(dj_deck(0)[DK_SONG] == 6 && dj_deck(1)[DK_SONG] == 11, "DJ: songs 06 and 0B on the decks");
    key(25, 1); key(25, 0);                             /* the blank key: the LOOPS pads */
    key(UI_WHITE[3], 1); key(UI_WHITE[3], 0);           /* the left: REPEAT 1 BAR */
    run(300);
    shot("ui3_dj_loops");
    int tall = 0;
    for (int x = 2; x < 2 + 114; x++) tall += screen[(61 + 9) * 240 + x] != 0;   /* 5 px or more */
    check(tall >= 100, "DJ: an 8-bar song's overview fills its columns (each bar as wide as 114 / 8)");
    /* OCT-, OCT+: HOME's mutes' tracks, only in HOME's combination; nowhere to go, or alone: nothing, no message */
    ui_toast("-");
    tap(B_OCTP);
    press(B_HOME); tap(B_OCTP); release(B_HOME); run(200);
    check(ui_toast_now() && !strcmp(ui_toast_now(), "-") && !ui_top() && leds[B_OCTP] == 0,
          "DJ: OCT+ alone, and HOME + OCT+ with no tracks 08-0F: nothing, no message, HOME's let-go no tap; dark");
    int many = -1;
    for (int n = 0; n < 0xF0 && many < 0; n++) {
        ui_song_t s;
        if (!ui_song((uint8_t)n, &s)) continue;
        for (int p = 0; p < s.npos; p++) if (ui_pattern_tracks(s.order[p]) > 8) { many = n; break; }
    }
    if (many >= 0) {
        turn(K_SELECT, -40); turn(K_SELECT, many);
        key(8, 1); key(8, 0);                           /* OP4: on the left */
        press(B_HOME); run(50);
        int lit = leds[B_OCTP] > 0 && leds[B_OCTM] == 0;
        tap(B_OCTP);
        int moved = ui_toast_now() && !strcmp(ui_toast_now(), "MUTES: TRACKS 08-0F");
        tap(B_OCTM); release(B_HOME); run(200);
        check(lit && moved && ui_toast_now() && !strcmp(ui_toast_now(), "MUTES: TRACKS 00-07"),
              "DJ: a song of more than 8 tracks: HOME held, OCT+ lit, to 08-0F, OCT- back");
    }
    /* HOME's SEQ: mute <-> solo again and again while the white keys are members */
    press(B_HOME); tap(B_SEQ); run(50);
    int solo1 = leds[B_SEQ] == LV_FULL;
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);           /* the left deck's track 00 soloed */
    int soloed = dj_deck(0)[DK_SOLO] == 0;
    tap(B_SEQ); run(50);
    int back = leds[B_SEQ] > 0 && leds[B_SEQ] < LV_FULL;
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0);           /* now a mute (the solo stays) */
    release(B_HOME); run(200);
    check(solo1 && soloed && back && (dj_deck(0)[DK_MUTES] & 1),
          "HOME held: SEQ to solo, a white key solos, SEQ again back to mute (after a use), a white key mutes");
}

/* REVERT (SAVE + OCT-) reloads DJ's stand (specs/files/dj.md#the-stand): the decks as saved, live ones handed to the
 * player again, paused where the stand has them */
/* DJ: a LOAD tapped on a playing deck waits for its bar line at the deck's tempo; held past the hold delay, the song's
 * own (LEFT: ... AT ITS TEMPO). SYNC off: the right deck keeps the tempo it played at, synced */
static void dj_after_bar(void)          /* on till the left deck has just passed a bar line (16 rows a bar) */
{
    dj_deck_state_t ds;
    for (int i = 0; i < 5000; i++) { dj_deck_state(0, &ds); if (ds.row % 16 == 1) return; run(1); }
}
static void test_dj_load_sync(void)
{
    player_state_t pst;
    ui_song_t s;
    char want[48], msg[160];
    home_key(10);                                       /* DJ */
    player_state(&pst, 0);
    if (pst.playing) tap(B_PLAY);
    turn(K_SELECT, -60); turn(K_SELECT, 6);
    key(8, 1); key(8, 0);                               /* OP4: 06 on the left */
    turn(K_SELECT, 5);
    key(10, 1); key(10, 0);                             /* OP5: 0B on the right */
    key(1, 1); key(1, 0);                               /* OP1: the left plays */
    run(500);
    turn(K_SELECT, -2);                                 /* (09) */
    dj_after_bar();                                     /* (each step just after a bar line: the next a bar away) */
    key(8, 1); run(60); key(8, 0); run(700);            /* tapped */
    check(ui_toast_now() && strstr(ui_toast_now(), "AT THE BAR") && !strstr(ui_toast_now(), "ITS TEMPO"),
          "DJ: LOAD tapped on a playing deck: at the bar, the deck's tempo (no AT ITS TEMPO)");
    key(8, 1); key(8, 0);                               /* (a second tap: cancelled) */
    dj_after_bar();
    key(8, 1); run(1000);                               /* held past the hold delay */
    snprintf(want, sizeof want, "LEFT: %s AT ITS TEMPO", ui_song(9, &s) ? s.name : "?");
    snprintf(msg, sizeof msg, "DJ: LOAD held on a playing deck: the song's own tempo (%s)", ui_toast_now() ? ui_toast_now() : "-");
    check(ui_toast_now() && !strcmp(ui_toast_now(), want), msg);
    key(8, 0);
    for (int i = 0; i < 5000 && dj_deck(0)[DK_SONG] != 9; i++) run(1);   /* (on till the bar line) */
    snprintf(msg, sizeof msg, "DJ: ... loaded at the bar line at the song's own tempo (%02X, %d, its own %d)",
             dj_deck(0)[DK_SONG], dj_deck_bpm10(0), dj_song_own10(9));
    check(dj_deck(0)[DK_SONG] == 9 && dj_deck_bpm10(0) == dj_song_own10(9), msg);
    if (!dj_sync()) { key(20, 1); key(20, 0); }         /* SYNC on */
    static const int MULT[3][2] = {{1, 1}, {1, 2}, {2, 1}};      /* (SYNC's multiple: x1, x1/2, x2) */
    int synced = dj_deck_bpm10(0) * MULT[dj_sync_mult()][0] / MULT[dj_sync_mult()][1];
    key(20, 1); key(20, 0);                             /* SYNC off */
    snprintf(msg, sizeof msg, "DJ: SYNC off: the right deck keeps the tempo it played at (%d, now %d)", synced, dj_deck_bpm10(1));
    check(!dj_sync() && dj_deck_bpm10(1) == synced, msg);
    key(20, 1); key(20, 0);                             /* (SYNC on again) */
    tap(B_PLAY);
    run(300);
}
/* DJ: a pad let go in the mode it went down in; another LOOPS pad with a loop going round (its length from the same
 * start, no ROLL); a LOAD clearing the loop, the pad's next tap setting one; a LOAD at the bar landing with another
 * screen shown; JUMP on a paused synced deck waiting for the other's bar line; a KEY pad held and the other deck's
 * tempo knob; PLAY with no deck to start (specs/ui/screens/dj.md, as the playground's scr-dj.js) */
static void dj_pads(const char *mode)   /* the blank key till the pads are in that mode (PADS: ...) */
{
    char want[24];
    snprintf(want, sizeof want, "PADS: %s", mode);
    for (int i = 0; i < 4; i++) {
        key(25, 1); key(25, 0); run(30);
        if (ui_toast_now() && !strcmp(ui_toast_now(), want)) return;
    }
}
static void test_dj_pads_waits(void)
{
    dj_deck_state_t ds;
    player_state_t pst;
    ui_song_t s;
    char msg[200], want[48];
    uint8_t *r0 = dj_deck(0);
    home_key(10);                                       /* DJ */
    player_state(&pst, 0);
    if (pst.playing) tap(B_PLAY);
    if (dj_sync()) { key(20, 1); key(20, 0); }          /* SYNC off */
    turn(K_SELECT, -60); turn(K_SELECT, 6);
    key(8, 1); key(8, 0);                               /* OP4: 06 on the left */
    turn(K_SELECT, 5);
    key(10, 1); key(10, 0);                             /* OP5: 0B on the right */
    dj_pads("PLAY");
    key(1, 1); key(1, 0);                               /* OP1: the left plays */
    run(800);
    key(UI_WHITE[4], 1); run(300);                      /* BRAKE held ... */
    dj_deck_state(0, &ds);
    int braked = ds.stop != 0;
    dj_pads("LOOPS");                                   /* ... the pads to LOOPS meanwhile ... */
    key(UI_WHITE[4], 0); run(600);                      /* ... let go: BRAKE's release */
    dj_deck_state(0, &ds);
    snprintf(msg, sizeof msg, "DJ: BRAKE held, the pads to LOOPS, let go: the deck plays on (braked %d, stop %d, playing %d)",
             braked, ds.stop, ds.playing);
    check(braked && !ds.stop && ds.playing, msg);
    dj_after_bar();
    key(UI_WHITE[4], 1); key(UI_WHITE[4], 0);           /* LOOPS' pad 5 tapped: a free LOOP of 2 BARS */
    run(1200);                                          /* (into the loop, past its first beats) */
    int lp = r0[DK_LOOP], start = r0[DK_LOOP_ROW];
    key(UI_WHITE[6], 1); run(400); key(UI_WHITE[6], 0); run(300);   /* pad 7 (1/2 BAR) held 0.4 s */
    snprintf(msg, sizeof msg, "DJ: a free LOOP going round, another free pad held and let go: its length from the same "
             "start, looping on, no ROLL (pad %d -> %d, row %d -> %d)", lp, r0[DK_LOOP], start, r0[DK_LOOP_ROW]);
    check(lp == 5 && r0[DK_LOOP] == 7 && r0[DK_LOOP_ROW] == start, msg);
    tap(B_PLAY); run(300);                              /* both paused */
    turn(K_SELECT, -2);                                 /* (09) */
    key(8, 1); key(8, 0); run(100);                     /* a LOAD on the paused left: its loop cleared */
    int cleared = r0[DK_LOOP] == 0;
    key(UI_WHITE[6], 1); key(UI_WHITE[6], 0); run(100);   /* pad 7 tapped: a loop again, not a second tap */
    check(cleared && r0[DK_LOOP] == 7, "DJ: a LOAD clears the deck's loop; its pad's next tap sets one (no stale second tap)");
    key(UI_WHITE[6], 1); key(UI_WHITE[6], 0); run(100);   /* (off) */
    /* a LOAD at the bar, another screen shown meanwhile: it lands there */
    key(1, 1); key(1, 0); run(500);                     /* the left plays (09) */
    turn(K_SELECT, -3);                                 /* (06) */
    dj_after_bar();
    key(8, 1); key(8, 0);                               /* LOAD tapped: 06 at the bar */
    home_key(17);                                       /* STATUS */
    run(4000);
    int landed = r0[DK_SONG] == 6;
    home_key(10);                                       /* DJ */
    snprintf(msg, sizeof msg, "DJ: a LOAD at the bar lands with another screen shown (the left's song %02X)", r0[DK_SONG]);
    check(landed, msg);
    /* LOAD held past the hold delay, its bar line come first: loaded at the bar, then the song's own tempo */
    turn(K_SELECT, 3);                                  /* (09) */
    turn(K_K1, 20);                                     /* the left 2.0 BPM up: 09 lands at that, its own only by
                                                         * the hold */
    for (int i = 0; i < 5000; i++) { dj_deck_state(0, &ds); if (ds.row % 16 == 15) break; run(1); }
    key(8, 1); run(1000);                               /* (pressed a row before the bar line, held 1 s) */
    snprintf(want, sizeof want, "LEFT: %s AT ITS TEMPO", ui_song(9, &s) ? s.name : "?");
    snprintf(msg, sizeof msg, "DJ: LOAD held, the bar line first: loaded, then its own tempo (%02X, %s, %d, its own %d)",
             r0[DK_SONG], ui_toast_now() ? ui_toast_now() : "-", dj_deck_bpm10(0), dj_song_own10(9));
    check(r0[DK_SONG] == 9 && ui_toast_now() && !strcmp(ui_toast_now(), want) && dj_deck_bpm10(0) == dj_song_own10(9), msg);
    key(8, 0);
    /* SET CUE with HOLD down: the cue on the bar line at or before the hand, the deck left where the hand is */
    dj_pads("PLAY");
    dj_after_bar();
    key(3, 1); run(100);                                /* OP2: HOLD the left */
    turn(K_K1, 6); run(100);                            /* by hand, 6 rows on */
    dj_deck_state(0, &ds);
    int hand = ds.row;
    key(UI_WHITE[0], 1); key(UI_WHITE[0], 0); run(100); /* SET CUE */
    dj_deck_state(0, &ds);
    snprintf(msg, sizeof msg, "DJ: SET CUE with HOLD down: the cue at or before the hand, the deck left there (hand %d, "
             "now %d, cue %d)", hand, ds.row, r0[DK_CUE_ROW]);
    check(ds.row == hand && r0[DK_CUE_ROW] == hand / 16 * 16, msg);
    key(3, 0); run(300);                                /* (let go: it plays on from the hand) */
    /* JUMP on the paused right deck, SYNC on, the left playing: on the left's next bar line */
    key(20, 1); key(20, 0);                             /* SYNC on */
    dj_pads("PLAY");
    dj_after_bar();
    key(UI_WHITE[10], 1); key(UI_WHITE[10], 0); run(60);   /* the right's JUMP (pad 3) */
    dj_deck_state(1, &ds);
    int armed = ds.armed && !ds.playing;
    run(3000);
    dj_deck_state(1, &ds);
    snprintf(msg, sizeof msg, "DJ: JUMP on a paused synced deck waits for the other's bar line, then plays (armed %d, "
             "playing %d)", armed, ds.playing);
    check(armed && ds.playing, msg);
    /* a KEY pad held: its deck's tempo knob turns KEY, the other deck's its tempo */
    key(20, 1); key(20, 0);                             /* SYNC off (the right's tempo its own) */
    tap(B_LFO);                                         /* the LFO pads */
    int b1 = dj_deck_bpm10(1), k0 = (int8_t)r0[DK_KEY];
    key(UI_WHITE[7], 1);                                /* the left's KEY pad held */
    turn(K_K4, 3);
    turn(K_K1, 1);
    int b1n = dj_deck_bpm10(1), k0n = (int8_t)r0[DK_KEY];
    turn(K_K1, -1);
    key(UI_WHITE[7], 0);
    tap(B_LFO);                                         /* (back to the pads before) */
    snprintf(msg, sizeof msg, "DJ: the left's KEY pad held: K1 its KEY (%d -> %d), K4 still the right's tempo (%d -> %d)",
             k0, k0n, b1, b1n);
    check(k0n == k0 + 1 && b1n == b1 + 3, msg);
    /* a LOAD waiting for the bar line, the deck paused first: dropped (the next tap loads at once) */
    turn(K_SELECT, -3);                                 /* (06) */
    dj_after_bar();
    key(8, 1); key(8, 0); run(60);                      /* LOAD tapped: 06 at the bar */
    key(1, 1); key(1, 0); run(300);                     /* the left's PLAY: paused */
    key(8, 1); key(8, 0); run(100);                     /* LOAD again */
    snprintf(want, sizeof want, "LEFT: %s", ui_song(6, &s) ? s.name : "?");
    snprintf(msg, sizeof msg, "DJ: a LOAD waiting, the deck paused: dropped, the next LOAD loads at once (%02X, %s)",
             r0[DK_SONG], ui_toast_now() ? ui_toast_now() : "-");
    check(r0[DK_SONG] == 6 && ui_toast_now() && !strcmp(ui_toast_now(), want), msg);
    /* SET CUE held on a paused deck: the cue on the beat its press fell in, the deck there too */
    dj_pads("PLAY");
    key(3, 1); run(100); turn(K_K1, 6); run(100); key(3, 0); run(100);   /* (HOLD: by hand to a row off the beat) */
    dj_deck_state(0, &ds);
    int at = ds.row, beat = at / 4 * 4;
    key(UI_WHITE[0], 1); run(400);                      /* SET CUE held */
    key(UI_WHITE[0], 0); run(200);
    dj_deck_state(0, &ds);
    snprintf(msg, sizeof msg, "DJ: SET CUE held on a paused deck: the cue on its beat, the deck there (row %d: deck %d, "
             "cue %d)", at, ds.row, r0[DK_CUE_ROW]);
    check(!ds.playing && at % 4 && beat % 16 && ds.row == beat && r0[DK_CUE_ROW] == beat, msg);
    /* CUE held at the song's end: from the cue point while held, there again when let go */
    ui_song_t s0s;
    int last = 0, rows = 16;
    if (ui_song(0, &s0s)) { last = s0s.npos - 1; rows = ui_pattern_rows(s0s.order[last]); }
    turn(K_SELECT, -60);                                /* (00: it stops at its end) */
    key(8, 1); key(8, 0); run(100);                     /* on the paused left */
    r0[DK_CUE_POS] = 1; r0[DK_CUE_ROW] = 16;
    r0[DK_POS] = (uint8_t)last; r0[DK_ROW] = (uint8_t)(rows - 4);
    dj_deck_place(0, last, rows - 4);
    key(1, 1); key(1, 0);                               /* the left plays its last rows */
    for (int i = 0; i < 3000; i++) { dj_deck_state(0, &ds); if (ds.ended) break; run(10); }
    int ended = ds.ended;
    key(UI_WHITE[1], 1); run(500);                      /* CUE held */
    dj_deck_state(0, &ds);
    int held_pos = ds.pos, held_playing = ds.playing;
    key(UI_WHITE[1], 0); run(200);
    dj_deck_state(0, &ds);
    snprintf(msg, sizeof msg, "DJ: CUE held at the song's end: plays from the cue (ended %d; held: pos %d, playing %d; "
             "let go: pos %d row %d, playing %d)", ended, held_pos, held_playing, ds.pos, ds.row, ds.playing);
    check(ended && held_playing && held_pos == 1 && !ds.playing && ds.pos == 1 && ds.row == 16, msg);
    /* a LOAD in the last bar of a song that stops: its end is the bar line, the new song plays on from its start */
    turn(K_SELECT, 6);                                  /* (06) */
    r0[DK_POS] = (uint8_t)last; r0[DK_ROW] = (uint8_t)(rows - 4);
    dj_deck_place(0, last, rows - 4);
    key(1, 1); key(1, 0); run(60);                      /* the left plays its last rows */
    key(8, 1); key(8, 0);                               /* LOAD tapped: 06 at the bar */
    for (int i = 0; i < 300 && r0[DK_SONG] != 6; i++) run(10);
    run(300);
    dj_deck_state(0, &ds);
    snprintf(msg, sizeof msg, "DJ: a LOAD in a stopping song's last bar: at its end the new song plays on (%02X, "
             "playing %d, pos %d)", r0[DK_SONG], ds.playing, ds.pos);
    check(r0[DK_SONG] == 6 && ds.playing && ds.pos == 0, msg);
    /* a LOAD waiting, a song started on SONGS: dropped (back on DJ the next LOAD loads at once) */
    turn(K_SELECT, 3);                                  /* (09) */
    dj_after_bar();
    key(8, 1); key(8, 0); run(60);                      /* LOAD tapped: 09 at the bar */
    home_key(1); tap(B_PLAY); run(500);                 /* SONGS: a song plays (DJ apart) */
    home_key(10); run(200);                             /* DJ */
    key(8, 1); key(8, 0); run(100);                     /* LOAD again */
    snprintf(want, sizeof want, "LEFT: %s", ui_song(9, &s) ? s.name : "?");
    snprintf(msg, sizeof msg, "DJ: a LOAD waiting, another song started: dropped, the next LOAD loads at once (%02X, %s)",
             r0[DK_SONG], ui_toast_now() ? ui_toast_now() : "-");
    check(r0[DK_SONG] == 9 && ui_toast_now() && !strcmp(ui_toast_now(), want), msg);
    tap(B_PLAY); run(300);                              /* (the song paused: PLAY AGAIN FOR DJ) */
    /* SYNC on, a LOAD held on the main past its bar line: the song's own tempo and SYNC's multiple picked again
     * (TRANCE LOOP, 140, on the right: x1 against 09's 128, x2 against HIP-HOP LOOP's 86) */
    static const int MULT[3][2] = {{1, 1}, {1, 2}, {2, 1}};
    int trance = song_named("TRANCE LOOP"), hiphop = song_named("HIP-HOP LOOP");
    dj_list_to(trance); key(10, 1); key(10, 0); run(100);   /* on the paused right */
    if (!dj_sync()) { key(20, 1); key(20, 0); run(100); }   /* SYNC on */
    key(1, 1); key(1, 0); run(300);                     /* the left plays (09) */
    key(13, 1); key(13, 0); run(2500);                  /* the right: on the left's bar line */
    int m0 = dj_sync_mult();
    dj_list_to(hiphop);
    for (int i = 0; i < 5000; i++) { dj_deck_state(0, &ds); if (ds.row % 16 == 15) break; run(1); }
    key(8, 1); run(1000); key(8, 0); run(100);          /* LOAD held across the left's bar line */
    dj_deck_state(1, &ds);
    snprintf(msg, sizeof msg, "DJ: SYNC on, LOAD held on the main past its bar line: its own tempo, SYNC's multiple "
             "picked again (%02X at %d, its own %d; x%d -> x%d; the right playing %d)", r0[DK_SONG], dj_deck_bpm10(0),
             dj_song_own10((uint8_t)hiphop), m0, dj_sync_mult(), ds.playing);
    check(r0[DK_SONG] == hiphop && dj_deck_bpm10(0) == dj_song_own10((uint8_t)hiphop) && m0 == 0 && dj_sync_mult() == 2
          && ds.playing, msg);
    /* SWAP with SYNC on: each deck plays on at its BPM (the new main keeps the BPM it played at, SYNC's multiple
     * picked again for the new right), the FADER LFO's swing mirrored */
    turn(K_K1, 20); run(100);                           /* the left 2.0 BPM up: the right (x2) 4.0 */
    int rev = dj_lfo_bytes(DJ_LFO_FADER)[0] & 2;
    int bl = dj_deck_bpm10(0), br = bl * MULT[dj_sync_mult()][0] / MULT[dj_sync_mult()][1];
    tap(B_SEQ); run(300);                               /* SWAP */
    int bl2 = dj_deck_bpm10(0), br2 = bl2 * MULT[dj_sync_mult()][0] / MULT[dj_sync_mult()][1];
    snprintf(msg, sizeof msg, "DJ: SWAP with SYNC on: each deck plays on at its BPM, the FADER LFO's swing mirrored "
             "(left %d, right %d -> left %d, right %d; reverse %d -> %d)", bl, br, bl2, br2, rev,
             dj_lfo_bytes(DJ_LFO_FADER)[0] & 2);
    check(dj_sync() && bl == dj_song_own10((uint8_t)hiphop) + 20 && bl2 == br && br2 == bl
          && rev != (dj_lfo_bytes(DJ_LFO_FADER)[0] & 2), msg);
    tap(B_SEQ); run(300);                               /* (back) */
    key(20, 1); key(20, 0);                             /* (SYNC off) */
    /* PLAY with no song on either deck: refused, nothing plays */
    tap(B_PLAY); run(300);                              /* (both paused) */
    uint8_t s0 = r0[DK_SONG], s1 = dj_deck(1)[DK_SONG];
    r0[DK_SONG] = dj_deck(1)[DK_SONG] = DJ_NO_SONG;     /* (the decks empty, as RESET DJ leaves them) */
    press(B_PLAY);
    snprintf(msg, sizeof msg, "DJ: PLAY with no deck to start: refused (%s), nothing plays", ui_toast_now() ? ui_toast_now() : "-");
    player_state(&pst, 0);
    check(ui_toast_now() && !strcmp(ui_toast_now(), "NO SONG ON THE DECK") && !pst.playing, msg);
    release(B_PLAY); run(200);
    r0[DK_SONG] = s0; dj_deck(1)[DK_SONG] = s1;
    key(20, 1); key(20, 0);                             /* (SYNC on again, as before) */
    run(300);
}
/* SWAP as the decks play (dj.md#the-main-and-swap): each goes on from where it was, nothing reloaded; the screen reads
 * them on their new sides at once, before the player has made it; a key held across it lets go on its deck; a LOAD
 * waiting goes with its deck */
static void test_dj_swap_playing(void)
{
    dj_deck_state_t a0, a1, b0, b1;
    player_state_t pst;
    char msg[200];
    home_key(10);                                       /* DJ */
    player_state(&pst, 0);
    if (pst.playing) tap(B_PLAY);
    if (dj_sync()) { key(20, 1); key(20, 0); }          /* SYNC off */
    turn(K_SELECT, -60); turn(K_SELECT, 6);
    key(8, 1); key(8, 0);                               /* 06 on the left */
    turn(K_SELECT, 5);
    key(10, 1); key(10, 0);                             /* 0B on the right */
    dj_pads("PLAY");
    key(1, 1); key(1, 0);                               /* both play */
    key(13, 1); key(13, 0);
    run(1500);
    dj_deck_state(0, &a0);
    dj_deck_state(1, &a1);
    in_now.buttons |= 1u << B_SEQ;                      /* SWAP: a millisecond at a time till the stand has it */
    for (int i = 0; i < 400 && dj_deck(0)[DK_SONG] != a1.song; i++) {
        if (i == 60) in_now.buttons &= ~(1u << B_SEQ);   /* (taken as let go, if not as pressed) */
        run(1);
    }
    dj_deck_state(0, &b0);
    dj_deck_state(1, &b1);
    player_state(&pst, 0);
    snprintf(msg, sizeof msg, "DJ: SWAP read at once, before the player has made it: each deck on its new side (%02X %02X; "
             "the player's left still %02X)", b0.song, b1.song, pst.deck[0].song);
    check(b0.song == a1.song && b1.song == a0.song && pst.deck[0].song == a0.song && b0.playing && b1.playing &&
          b0.rows_played == pst.deck[1].rows_played && b1.rows_played == pst.deck[0].rows_played, msg);
    in_now.buttons &= ~(1u << B_SEQ);
    run(600);
    dj_deck_state(0, &b0);
    dj_deck_state(1, &b1);
    player_state(&pst, 0);
    snprintf(msg, sizeof msg, "DJ: SWAP as they play: each goes on from where it was (rows played %u -> %u, %u -> %u; the "
             "player's sides %02X %02X)", (unsigned)a1.rows_played, (unsigned)b0.rows_played, (unsigned)a0.rows_played,
             (unsigned)b1.rows_played, pst.deck[0].song, pst.deck[1].song);
    check(b0.playing && b1.playing && pst.deck[0].song == a1.song && pst.deck[1].song == a0.song &&
          b0.rows_played > a1.rows_played && b0.rows_played < a1.rows_played + 64 &&
          b1.rows_played > a0.rows_played && b1.rows_played < a0.rows_played + 64, msg);
    key(3, 1);                                          /* the left's HOLD down: it stands, held */
    run(200);
    dj_deck_state(0, &a0);
    tap(B_SEQ);                                         /* SWAP while held: the held deck to the right */
    key(3, 0);                                          /* let go: on its deck, the right now, which plays on */
    run(400);
    dj_deck_state(0, &b0);
    dj_deck_state(1, &b1);
    snprintf(msg, sizeof msg, "DJ: HOLD held across SWAP, let go: its deck, on the right now, plays on (held playing %d; "
             "left %d, right %d)", a0.playing, b0.playing, b1.playing);
    check(!a0.playing && b1.song == a0.song && b1.playing && b0.playing, msg);
    /* HOLD's hand (dj.md#hold): K1 a row a click, heard, the deck held there (not playing); K2 a beat silently; let go,
     * it plays on from where the hand left it */
    key(3, 1); run(300);                                /* the left's HOLD: it stands */
    dj_deck_state(0, &a0);
    turn(K_K1, 1); run(100);                            /* K1: a row on, heard */
    dj_deck_state(0, &b0);
    uint8_t hp = dj_deck(0)[DK_POS], hr = dj_deck(0)[DK_ROW];
    turn(K_K2, 1); run(100);                            /* K2: a beat on, silently */
    dj_deck_state(0, &b1);
    snprintf(msg, sizeof msg, "DJ: HOLD's hand: K1 a row on, heard and held (%02X:%02X -> %02X:%02X, playing %d), K2 a "
             "beat on silently (-> %02X:%02X, the stand %02X:%02X)", a0.pos, a0.row, b0.pos, b0.row, b0.playing, b1.pos,
             b1.row, dj_deck(0)[DK_POS], dj_deck(0)[DK_ROW]);
    check(!a0.playing && !b0.playing && !b1.playing && b0.pos == hp && b0.row == hr &&
          (hp != a0.pos || hr == a0.row + 1) && b1.pos == dj_deck(0)[DK_POS] && b1.row == dj_deck(0)[DK_ROW] &&
          (b1.pos != hp || b1.row == hr + 4), msg);
    key(3, 0); run(500);                                /* let go: it plays on from there */
    dj_deck_state(0, &b0);
    check(b0.playing, "DJ: HOLD let go after the hand: the deck plays on from where it left it");
    dj_pads("LOOPS");
    key(UI_WHITE[4], 1); key(UI_WHITE[4], 0); run(300);   /* a free LOOP of 2 BARS on the left */
    uint8_t lp0 = dj_deck(0)[DK_LOOP_POS], lr0 = dj_deck(0)[DK_LOOP_ROW];
    key(3, 1); run(300);                                /* HOLD */
    turn(K_K1, 1); run(100);                            /* a row on: the loop starts at the head */
    int at_head = dj_deck(0)[DK_LOOP_POS] == dj_deck(0)[DK_POS] && dj_deck(0)[DK_LOOP_ROW] == dj_deck(0)[DK_ROW];
    turn(K_K1, -1); run(100);                           /* back where it stood: the loop as it was */
    snprintf(msg, sizeof msg, "DJ: HOLD's hand takes a loop along (at the head %d), back where the deck stood it is as it "
             "was (%02X:%02X, was %02X:%02X)", at_head, dj_deck(0)[DK_LOOP_POS], dj_deck(0)[DK_LOOP_ROW], lp0, lr0);
    check(dj_deck(0)[DK_LOOP] && at_head && dj_deck(0)[DK_LOOP_POS] == lp0 && dj_deck(0)[DK_LOOP_ROW] == lr0, msg);
    key(3, 0); run(300);
    key(UI_WHITE[4], 1); key(UI_WHITE[4], 0); run(300);   /* (the loop off) */
    dj_pads("PLAY");
    int disco = song_named("DISCO LOOP");
    uint8_t right = dj_deck(1)[DK_SONG];
    dj_list_to(disco);
    for (int i = 0; i < 5000; i++) { dj_deck_state(0, &a0); if (a0.row % 16 == 1) break; run(1); }
    key(8, 1); key(8, 0);                               /* LOAD on the playing left: at its bar line ... */
    tap(B_SEQ);                                         /* ... SWAP before it: the LOAD goes with its deck */
    for (int i = 0; i < 4000 && dj_deck(1)[DK_SONG] != disco; i++) run(1);
    run(100);
    dj_deck_state(1, &b1);
    snprintf(msg, sizeof msg, "DJ: a LOAD waiting goes with its deck in a SWAP, on its bar line on the right (%02X %02X, "
             "the right playing %d)", dj_deck(0)[DK_SONG], dj_deck(1)[DK_SONG], b1.playing);
    check(dj_deck(1)[DK_SONG] == disco && dj_deck(0)[DK_SONG] == right && b1.playing, msg);
    tap(B_PLAY); run(300);                              /* (both paused) */
}

static void test_dj_revert(void)
{
    player_state_t pst;
    dj_deck_state_t ds;
    home_key(10);                                       /* DJ */
    player_state(&pst, 0);
    if (pst.playing) tap(B_PLAY);
    turn(K_SELECT, -60); turn(K_SELECT, 6);
    key(8, 1); key(8, 0);                               /* OP4: 06 on the left */
    turn(K_SELECT, 5);
    key(10, 1); key(10, 0);                             /* OP5: 0B on the right */
    key(1, 1); key(1, 0);                               /* OP1: the left plays */
    run(1500);
    tap(B_PLAY);                                        /* paused */
    run(300);
    tap(B_SAVE); run(1500);
    int xf = dj_xf();
    check(!ui_unsaved && dj_deck(0)[DK_SONG] == 6 && dj_deck(1)[DK_SONG] == 11 && dj_live(),
          "DJ REVERT: the stand saved (06 on the left, 0B on the right, live, paused)");
    turn(K_ALG, 6);                                     /* the crossfader moved */
    turn(K_SELECT, -2);
    key(10, 1); key(10, 0);                             /* OP5: 09 on the right */
    key(1, 1); key(1, 0);                               /* the left plays on */
    run(1500);
    tap(B_PLAY);                                        /* paused: the changed stand goes into the edit memory */
    run(1000);
    check(ui_unsaved && dj_xf() != xf && dj_deck(1)[DK_SONG] == 9, "DJ REVERT: the stand changed (played, paused)");
    key(1, 1); key(1, 0);                               /* the left plays again: REVERT pauses it first */
    run(500);
    combo(B_SAVE, B_OCTM);                              /* REVERT's question */
    tap(B_OCTP);                                        /* REVERT */
    run(500);
    player_state(&pst, 0);
    dj_deck_state(0, &ds);
    check(dj_xf() == xf && dj_deck(0)[DK_SONG] == 6 && dj_deck(1)[DK_SONG] == 11,
          "DJ REVERT: the stand as saved (the crossfader, both decks' songs)");
    check(dj_live() && !pst.playing && !ds.playing && ds.song == 6 && ds.pos == dj_deck(0)[DK_POS] &&
          ds.row == dj_deck(0)[DK_ROW], "DJ REVERT: the live decks paused where the saved stand has them");
    check(!ui_unsaved, "DJ REVERT: nothing unsaved after it");
    /* the decks standing otherwise than written count at once, playing too (specs/files/dj.md#the-stand); SAVE then
     * waits for the pause and keeps where they paused */
    key(1, 1); key(1, 0);                               /* the left plays: its place moves on */
    run(500);
    check(ui_unsaved == 1, "DJ: a deck playing, the stand moves: one unsaved change at once (SAVE lit)");
    tap(B_SAVE); run(100);
    check(ui_toast_now() && !strcmp(ui_toast_now(), "SAVE WAITS FOR A PAUSE"), "... SAVE while it plays: SAVE WAITS FOR A PAUSE");
    tap(B_PLAY);                                        /* paused: the save goes on */
    run(1500);
    uint32_t len;
    const uint8_t *rec = store_get(REC_DJ, 0, 0, &len, 0);
    dj_deck_state(0, &ds);
    check(!ui_unsaved && rec && len == DJ_REC_LEN && !memcmp(rec, DJ_REC, DJ_REC_LEN) && ds.pos == dj_deck(0)[DK_POS] &&
          ds.row == dj_deck(0)[DK_ROW], "... paused: saved, the stand where the deck paused, nothing unsaved");
}

#ifdef PTFM_DEBUG
/* ------------------------------------------------------- the debug build's figures --- */
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static int unpack(const uint8_t *m, uint32_t n, uint8_t *f)   /* FIGURES' payload, 7 bytes in 8 */
{
    uint32_t len = 0;
    for (uint32_t i = 5; i < n - 1; ) {
        uint8_t top = m[i++];
        for (uint32_t j = 0; j < 7 && i < n - 1; j++, i++) f[len++] = (uint8_t)(m[i] | (top >> j & 1) << 7);
    }
    return (int)len;
}
static void save_caught(int k, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out, name);
    FILE *fp = fopen(path, "wb");
    if (fp) { fwrite(caught[k], 1, caught_n[k], fp); fclose(fp); }
}
static void save_caught2(int k, const char *name)   /* (a 37: the page's side joins it to its 31) */
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", out, name);
    FILE *fp = fopen(path, "wb");
    if (fp) { fwrite(caught2[k], 1, caught2_n[k], fp); fclose(fp); }
}
static void test_debug(void)
{
    static const uint8_t ask_freeze[7] = {0xF0, 0x7D, 0x50, 0x54, 0x30, 0x02, 0xF7};
    static const uint8_t ask_thaw[7] = {0xF0, 0x7D, 0x50, 0x54, 0x30, 0x04, 0xF7};
    uint8_t f[DBG_FIGURES_LEN + 8];
    player_play_song(0, 0, 0);                          /* PUNCH IN */
    run(2000);
    app_sysex(ask_freeze, 7);                           /* the first window: 2 s of a song; the screen frozen */
    run(20);
    check(ncaught == 1, "debug: 30 FIGURES? answered with one 31 FIGURES");
    int len = ncaught ? unpack(caught[0], caught_n[0], f) : 0;
    const uint8_t *p = f + 18 + 6 * DS_N;               /* (the rest, after the stages) */
#define STAGE(s) rd32(f + 18 + 6 * (s))
#define INSIDE(s) (!(DS_TOP >> (s) & 1u))
    uint32_t halves = rd16(f + 4), top = 0, replay_parts = 0, voices_parts = 0, fx_parts = 0;
    for (int s = 0; s < DS_N; s++) {
        if (!INSIDE(s)) top += STAGE(s);
        else if (s < DS_VOICES) replay_parts += STAGE(s);
        else if (s <= DS_JCHORUS) voices_parts += STAGE(s);
        else if (s < DS_OUTPUT) fx_parts += STAGE(s);
    }
    check(len >= DBG_FIGURES_LEN && f[0] == DBG_FIGURES_VERSION && f[16] == DS_N && f[1] == 2,
          "debug: FIGURES of its version, its stages counted; not frozen through it, frozen from now");
    check(len >= DBG_FIGURES_LEN && !(p[88 + DBG_WORST_LEN + DBG_DJ_LEN] & 1) &&
          !rd16(p + 88 + DBG_WORST_LEN + DBG_DJ_LEN + 2), "debug: FIGURES' USB audio, at its end: nothing recording");
    {                                                   /* the window's worst half: its own stages */
        const uint8_t *w = p + 88;
        uint32_t wtop = 0, wr = 0, wv = 0;
        for (int s = 0; s < DS_N; s++) {
            if (DS_TOP >> s & 1u) wtop += rd16(w + 8 + 2 * s);
            else if (s < DS_VOICES) wr += rd16(w + 8 + 2 * s);
            else if (s <= DS_JCHORUS) wv += rd16(w + 8 + 2 * s);
        }
        check(rd16(w) > 0 && wtop == rd16(w) && rd16(w) * halves >= top && rd16(w) <= rd16(f + 12) &&
              wr <= rd16(w + 8 + 2 * DS_REPLAY) + 3 && wv <= rd16(w + 8 + 2 * DS_VOICES) + 3 && w[2] <= p[9],
              "debug: the worst half: its stages sum to its time, at least the mean half, within the interrupt's "
              "longest; REPLAY's and VOICES' parts inside them; its voices");
    }
    check(STAGE(DS_CELLS) > 0 && STAGE(DS_NOTES) > 0 && replay_parts <= STAGE(DS_REPLAY) + 3 * halves,
          "debug: CELLS and NOTES measured, parts of REPLAY");
    check(halves >= 340 && halves <= 380, "debug: the window's halves (the first window: since power-on, ~2.1 s: 362)");
    check(top > 0 && top <= rd32(f + 8) + halves, "debug: the stages add up to no more than the interrupt's time");
    check(STAGE(DS_DX7) + STAGE(DS_JUNO) + STAGE(DS_TB303) + STAGE(DS_SAMPLE) > 0 && STAGE(DS_MIX) > 0 &&
          voices_parts <= STAGE(DS_VOICES) + 3 * halves, "debug: the engines, MIX and J-CHORUS are parts of VOICES");
    check(STAGE(DS_REVERB) > 0 && fx_parts <= STAGE(DS_EFFECTS) + 3 * halves, "debug: REVERB .. PUNCH-INS are parts of EFFECTS");
    check(rd16(p) + rd16(p + 2) + rd16(p + 4) + rd16(p + 6) > 0, "debug: voice blocks counted");
    check(p[44] == 1 && p[45] == 1 && p[46] == 0, "debug: what plays: song 00, playing");
    check(rd16(p + 18) > 30 && rd16(p + 36) == 24320, "debug: frames drawn (~60 in 2 s), the stacks' sizes");
    check(!strncmp((const char *)p + 64, PTFM_VERSION, 23), "debug: the firmware's version");
    {   /* 37 FIGURES 2 right after it, the same window's number (specs/debug.md, Core 2's bytes) */
        uint8_t g[DBG_FIGURES2_LEN + 8];
        plat_cpu_t c;
        plat_cpu(&c, 0);
        int len2 = ncaught2 == 1 ? unpack(caught2[0], caught2_n[0], g) : 0;
        check(len2 >= DBG_FIGURES2_LEN && g[0] == DBG_FIGURES2_VERSION && g[1] == f[17] && f[0] == 11,
              "debug: FIGURES version 11, its 37 after it with its window's number");
        check(len2 >= DBG_FIGURES2_LEN && g[2] == c.c2_state && g[3] == c.c2_why &&
              (g[4] & 1) == !ui_set[S_SECOND_CORE] && !(g[4] & 2) && !rd32(g + 8) && !rd16(g + 14),
              "debug: 37: core 2's state and why, SETTINGS' SECOND CORE, not off by 38; no core 2 here, no blocks");
        save_caught2(0, "figures1c2.syx");
    }
    save_caught(0, "figures1.syx");
    run(2000);                                          /* frozen: one frame (its message), then none */
    {   /* 38 SECOND CORE 0: core 2 off whatever SETTINGS says, the setting untouched, nothing saved */
        static const uint8_t one_core[7] = {0xF0, 0x7D, 0x50, 0x54, 0x38, 0x00, 0xF7};
        int16_t was = ui_set[S_SECOND_CORE];
        app_sysex(one_core, 7);
        run(20);
        check(c2_forced == 1 && ui_set[S_SECOND_CORE] == was && !ui_unsaved,
              "debug: 38 SECOND CORE 0: core 2 off (the platform told), SETTINGS' SECOND CORE untouched, nothing unsaved");
    }
    uint8_t w1 = f[17];
    app_sysex(ask_thaw, 7);
    run(20);
    check(ncaught == 2, "debug: the second window answered");
    if (ncaught == 2) unpack(caught[1], caught_n[1], f);
    check(rd16(p + 18) <= 1 && f[1] == 1, "debug: FREEZE SCREEN: no frames drawn while frozen (but its message); "
          "frozen through the window, thawed after it");
#undef STAGE
#undef INSIDE
    check(rd16(f + 2) >= 1900 && rd16(f + 2) <= 2200, "debug: the window's length, ms");
    {
        uint8_t g[DBG_FIGURES2_LEN + 8];
        int len2 = ncaught2 == 2 ? unpack(caught2[1], caught2_n[1], g) : 0;
        check(f[17] == (uint8_t)(w1 + 1) && len2 >= DBG_FIGURES2_LEN && g[1] == f[17] && (g[4] & 2),
              "debug: the next window numbered one more, its 37 the same; 37 says core 2 off by 38");
    }
    save_caught(1, "figures2.syx");
    {   /* 38 SECOND CORE 1: core 2 back to what SETTINGS says */
        static const uint8_t two_cores[7] = {0xF0, 0x7D, 0x50, 0x54, 0x38, 0x01, 0xF7};
        app_sysex(two_cores, 7);
        check(c2_forced == 0, "debug: 38 SECOND CORE 1: core 2 as SETTINGS says");
    }
    run(500);
    check(!dbg_frozen(), "debug: unfrozen");
    {   /* a window on DJ: two decks playing, the left's FILTER, the crossfader's LFO on; FIGURES' DJ */
        static const uint8_t ask[7] = {0xF0, 0x7D, 0x50, 0x54, 0x30, 0x00, 0xF7};
        const uint8_t *dj = p + 88 + DBG_WORST_LEN;
        rec_song_t so;
        player_stop();                                  /* (PUNCH IN would move onto the left deck) */
        run(300);
        home_key(10);                                   /* OP5: DJ */
        key(8, 1); key(8, 0);                           /* OP4: the first song on the left */
        turn(K_SELECT, 1);
        key(10, 1); key(10, 0);                         /* OP5: the next on the right */
        turn(K_K2, -10);                                /* K2: the left's FILTER (on at OFF) towards LP */
        dj_lfo_bytes(DJ_LFO_FADER)[0] |= 1;             /* the crossfader's LFO on, as its pad does */
        dj_lfo_restart(DJ_LFO_FADER);
        player_state_t vs;
        DJ_REC[0] = 1;                                  /* (DJ's VOICES 16 of an older record: no effect now) */
        run(100);
        ui_player_state(&vs);
        check(vs.voice_count == 12, "DJ: SETTINGS' VOICES (12) before the decks are live");
        key(1, 1); key(1, 0);                           /* OP1: the left deck plays */
        run(300);
        ui_player_state(&vs);
        check(vs.voice_count == 12, "DJ: the decks live play with SETTINGS' VOICES too (12 the most, no 16)");
        key(13, 1); key(13, 0);                         /* OP6: the right deck on the main's bar line */
        app_audio_usb(fake_usb);                        /* (a computer recording: USB audio's own stage) */
        app_sysex(ask, 7);                              /* (a window starts here) */
        run(4000);
        fake_dbg.usb.on = 1;                            /* (the device's counters, as plat_dbg gives them) */
        fake_dbg.usb.r48 = 1;
        fake_dbg.usb.pkts = 4000;
        fake_dbg.usb.dry = 3;
        fake_dbg.usb.missed = 2;
        fake_dbg.usb.starts = 1;
        fake_dbg.usb.fill = 130;
        fake_dbg.usb.target = 139;
        fake_dbg.c2.sum_us = 400000;                    /* (core 2's, as the device's plat_dbg gives them) */
        fake_dbg.c2.max_us = 1500;
        fake_dbg.c2.blocks = 2700;
        fake_dbg.c2.halves = 65535;                     /* (every half's blocks on core 2) */
        fake_dbg.c2.misreads = 2;
        fake_dbg.c2.wait_sum_us = 3000;
        fake_dbg.c2.wait_max_us = 25;
        fake_dbg.c2.late1 = 1;
        fake_dbg.c2.late2 = 3;
        fake_dbg.c2.stack_used = 2100;
        fake_dbg.c2.stack_size = 6144;
        fake_dbg.c2.fault_vec = 9;
        fake_dbg.c2.fault_pc = 0x01C71234u;
        fake_dbg.c2.start_10ns = 41;
        fake_dbg.c2.clk = 1;
        app_sysex(ask, 7);
        run(20);
        app_audio_usb(0);
        check(ncaught == 4, "debug: DJ's windows answered");
        if (ncaught == 4) unpack(caught[3], caught_n[3], f);
        {   /* its 37: the platform's core 2 figures, as specs/debug.md lays them out; 38 1 before: not off by it */
            uint8_t g[DBG_FIGURES2_LEN + 8];
            int len2 = ncaught2 == 4 ? unpack(caught2[3], caught2_n[3], g) : 0;
            check(len2 >= DBG_FIGURES2_LEN && g[1] == f[17] && (g[4] & 14) == (4 | 8) && rd16(g + 6) == 41 &&
                  rd32(g + 8) == 400000 && rd16(g + 12) == 1500 && rd16(g + 14) == 2700 && rd16(g + 16) == 2 &&
                  rd32(g + 18) == 3000 && rd16(g + 22) == 25 && rd16(g + 24) == 1 && rd16(g + 26) == 3 &&
                  rd16(g + 28) == 2100 && rd16(g + 30) == 6144 && rd32(g + 32) == 9 && rd32(g + 36) == 0x01C71234u,
                  "debug: 37's bytes: the start's divider bit, the whole window, the start, the blocks' time and "
                  "count, misreads, core 1's waits, the lates by core, the stack, the fault record");
            save_caught2(3, "figures3c2.syx");
        }
        {
            const uint8_t *u = p + 88 + DBG_WORST_LEN + DBG_DJ_LEN;
            check(u[0] == 3 && rd16(u + 2) == 4000 && rd16(u + 4) == 3 && rd16(u + 6) == 2 && rd16(u + 10) == 1 &&
                  rd16(u + 12) == 130 && rd16(u + 14) == 139, "debug: FIGURES' USB audio: recording at 48 kHz, its packets");
            check(u[1] == 0, "debug: FIGURES' USB byte 1: every answer sent so far");
            check(rd32(f + 18 + 6 * DS_USB) > 0 && rd32(f + 18 + 6 * DS_USB) <= rd32(f + 18 + 6 * DS_OUTPUT) + 3 * rd16(f + 4),
                  "debug: USB AUDIO measured, a part of OUTPUT");
        }
        check(p[44] == PL_DJ && (dj[0] & 7) == 7 && (dj[1] & 1) && (dj[25] & 1),
              "debug: FIGURES' DJ: the decks in the player, both playing");
        check(!memcmp(dj + 49, DJ_REC, DJ_REC_LEN) && (int8_t)dj[49 + 88 + DK_FILTER] == -10 && (dj[49 + 80] & 1),
              "debug: FIGURES' DJ: DJ's record as it stands (the left's filter, the crossfader's LFO on)");
        check(!rec_song(DJ_REC[88], &so) && !strncmp((const char *)dj + 1 + 8, so.name, 16) && rd16(dj + 1 + 3) > 0 &&
              !rec_song(DJ_REC[112], &so) && !strncmp((const char *)dj + 25 + 8, so.name, 16),
              "debug: FIGURES' DJ: each deck's song by name, its tempo");
        check(rd32(f + 18 + 6 * DS_DJMIX) > 0 && rd32(f + 18 + 6 * DS_DJFILTERS) > 0 &&
              rd32(f + 18 + 6 * DS_DJDECKS) > 0 && rd32(f + 18 + 6 * DS_DJLFOS) > 0 &&
              rd32(f + 18 + 6 * DS_DJDECKS) + rd32(f + 18 + 6 * DS_DJLFOS) <= rd32(f + 18 + 6 * DS_CONTROL),
              "debug: DJ MIX, DJ FILTERS, DJ DECKS and DJ LFOS (parts of CONTROL) measured on DJ");
        save_caught(3, "figures3.syx");
        {                                               /* the bench refuses while DJ is on */
            static const uint8_t go[7] = {0xF0, 0x7D, 0x50, 0x54, 0x32, 0x01, 0xF7};
            uint8_t e[320];
            int refused = 0;
            nbench = 0;
            dbg_sysex(go, 7);
            run(200);
            for (uint32_t i = 0; i < nbench; i++)
                if (unpack(bench_msg[i], bench_len[i], e) > 26 && e[1] == 4 && e[26] == 4 && !strncmp((char *)e + 6, "DJ ON", 5))
                    refused = 1;
            check(refused && nbench == 1, "debug: the bench refuses while DJ is on (END: DJ ON: PLAY A SONG)");
        }
        DJ_REC[0] = 0;
        run(100);
        ui_player_state(&vs);
        check(vs.voice_count == 12, "DJ: SETTINGS' VOICES (12) still");
        tap(B_PLAY);                                    /* both paused, DJ left */
        run(300);
        home_key(8);
        run(300);
    }
    {   /* an answer USB refused (cut short, or the ring full): that window lost, counted, the next answer says so */
        static const uint8_t ask[7] = {0xF0, 0x7D, 0x50, 0x54, 0x30, 0x00, 0xF7};
        const uint8_t *u = p + 88 + DBG_WORST_LEN + DBG_DJ_LEN;
        ncaught = ncaught2 = 0;
        sx_refuse = 1;
        app_sysex(ask, 7);
        run(20);
        check(ncaught == 0 && !sx_refuse && ncaught2 == 0, "debug: the answer USB refused went nowhere, nor its 37");
        app_sysex(ask, 7);
        run(20);
        if (ncaught == 1) unpack(caught[0], caught_n[0], f);
        check(ncaught == 1 && f[0] == DBG_FIGURES_VERSION && u[1] == 1 && ncaught2 == 1,
              "debug: FIGURES' USB byte 1: the one answer the FM-1 could not send, counted (the next sent, its 37 too)");
    }
    static const uint8_t other[7] = {0xF0, 0x7D, 0x50, 0x54, 0x7E, 0x00, 0xF7};
    check(!dbg_sysex(other, 7), "debug: other commands are not the figures'");
}

/* the bench (specs/debug.md, The bench), its steps short (BENCH_FAST): a whole run on the demo disk, its records */
static void test_bench(void)
{
    static const uint8_t go[7] = {0xF0, 0x7D, 0x50, 0x54, 0x32, 0x01, 0xF7};
    uint8_t f[320];
    player_state_t st;
    int ends = 0;
    player_play_song(1, 0, 0);                          /* (something playing: the bench stops it) */
    run(300);
    nbench = 0;
    check(dbg_sysex(go, 7) == 1, "bench: 32 BENCH taken");
    for (int t = 0; t < 120 && !ends; t++) {
        run(500);
        for (uint32_t i = 0; i < nbench; i++) if (bench_len[i] > 8 && unpack(bench_msg[i], bench_len[i], f) > 2 && f[1] == 4) ends = 1;
    }
    check(ends == 1, "bench: it ends (an END record)");
    int len = nbench ? unpack(bench_msg[0], bench_len[0], f) : 0;
    uint32_t total = len > 6 ? rd16(f + 4) : 0, core = 0, alone = 0, seq_ok = 1, kinds_ok = 1, last_kind = 0;
    check(len >= 56 && f[0] == DBG_BENCH_VERSION && f[1] == 0 && !strncmp((const char *)f + 6, "BENCH", 5) &&
          !strncmp((const char *)f + 26, PTFM_VERSION, 23) && f[50] == 12, "bench: START: its version, the firmware, VOICES 12");
    int eng_alone = 0, end_ok = 0, rev_alone = 0, others = 0;
    uint32_t pieces = 0;
    for (uint32_t i = 1; i < nbench; i++) {
        len = unpack(bench_msg[i], bench_len[i], f);
        uint32_t kind = f[1], n = rd16(f + 2);
        const char *name = (const char *)f + 6;
        if (kind < last_kind) kinds_ok = 0;
        last_kind = kind;
        if (kind == 4) { end_ok = f[26] == 0 && n == total && !strncmp(name, "DONE", 4); continue; }
        if (n != ++pieces) seq_ok = 0;
        if (kind == 1) core++;
        else if (kind == 2) {
            alone++;
            if ((!strncmp(name, "DX7 F", 5) || !strncmp(name, "JUNO F", 6) || !strncmp(name, "TB-303 F", 8) ||
                 !strncmp(name, "SAMPLE ", 7)) && rd32(f + 36) > 0 && f[26] == DBU_BLOCK && f[27] == 8) eng_alone++;
            if (!strcmp(name, "REVERB") && rd32(f + 36) > 0) rev_alone = 1;
        } else others++;
    }
    check(core == 2 && alone >= 2 + FXB_N && eng_alone >= 8 && rev_alone, "bench: THE CORE's rows, ALONE's: the engines "
          "(3 DX7, the Juno-106's paths, the TB-303, a sample twice) and the effects, measured");
    check(seq_ok && kinds_ok && core + alone == total && end_ok && !others, "bench: the records numbered in order, START, "
          "THE CORE, ALONE, END (DONE, every piece); no steps (the autotests' now)");
    run(500);
    player_state(&st, 0);
    check(st.voice_count == 12 && !st.playing && st.voices == 0, "bench: after it, VOICES 12 as before, nothing plays");
    char path[512];
    snprintf(path, sizeof path, "%s/bench.syx", out);
    FILE *fp = fopen(path, "wb");
    if (fp) { for (uint32_t i = 0; i < nbench; i++) fwrite(bench_msg[i], 1, bench_len[i], fp); fclose(fp); }
}
#endif

/* a question's LEDs (specs/ui/leds.md): its answers OCT- and OCT+ full, HOME dim, every other button dark; HOME
 * cancels it, and the screen's own come back */
static int asked_answer = -2;
static void asked(int a) { asked_answer = a; }
/* patterns at most 128 rows (specs/files/patterns.md#size): GLO's ROWS stops at 128 (SEL held: 16 rows a click);
 * LOOPS' length at most a pattern's 128 rows (specs/files/loops.md#what-is-kept): 8 bars at 1/16, 5 at 1/16T, 4 at
 * 1/32; a LOOPS record of more rows (written before the cap) reads as its first 128 */
static void test_rows_max(void)
{
    home_key(3);                                        /* (the EDITOR) */
    int P = ed_first_free();                            /* (a new empty one, 8 x 64: no question growing it) */
    check(P >= 0 && !ed_new_pattern((uint8_t)P, 8, 64, 1), "rows: a new pattern for the test");
    if (P < 0) return;
    ed_editor_goto((uint8_t)P, 0, 1);
    run(50);
    tap(B_GLO);                                         /* PATTERN xx PROPERTIES: TRACKS, ROWS */
    turn(K_SELECT, -4); turn(K_SELECT, 1);
    press(B_SEL);                                       /* (held: 16 rows a click) */
    turn(K_K1, 3);                                      /* 112 */
    int r112 = ui_pattern_rows((uint8_t)P);
    turn(K_K1, 1);
    int r1 = ui_pattern_rows((uint8_t)P);
    turn(K_K1, 1);
    int r2 = ui_pattern_rows((uint8_t)P);
    uint32_t hl = 0;
    const uint8_t *h = store_piece(REC_PATTERN, (uint8_t)P, STORE_PC_HEAD, &hl);   /* (the edit's head: rows - 1) */
    check(h && hl >= 3 && h[1] == 127, "GLO's ROWS: the edit writes 128 rows, never more");
    release(B_SEL);
    turn(K_K1, 1);                                      /* (a row more) */
    int r3 = ui_pattern_rows((uint8_t)P);
    shot("ui3_rows_max");
    tap(B_GLO);
    char msg[160];
    snprintf(msg, sizeof msg, "GLO's ROWS stops at 128 (SEL held: %d, %d, %d; a row more: %d)", r112, r1, r2, r3);
    check(r112 == 112 && r1 == 128 && r2 == 128 && r3 == 128, msg);
    ui_edit_revert(REC_PATTERN, (uint8_t)P, 0);         /* (the new pattern gone again) */
    check(!ui_pattern_rows((uint8_t)P), "rows: the test's pattern gone again");
    run(200);
    /* LOOPS' most bars by its grid (BEATS A BAR 4) */
    home_key(22);
    run(100);
    int g = loops_grid(), b[3];
    for (int k = 0; k < 3; k++) { loops_set_grid(k); b[k] = loops_max_bars(); }
    loops_set_grid(g);
    snprintf(msg, sizeof msg, "LOOPS: at most 8 bars at 1/16, 5 at 1/16T, 4 at 1/32 (%d, %d, %d; BEATS A BAR %d)",
             b[0], b[1], b[2], loops_beats());
    check(loops_beats() != 4 || (b[0] == 8 && b[1] == 5 && b[2] == 4), msg);
    loops_set_rows(256);
    check(loops_rows() <= 128, "LOOPS: a length past 128 rows refused");
}
/* ------------------------------------------------------- the EDITOR's own pattern ---
 * specs/ui/screens/editor.md#the-editors-own-pattern: pattern FE, never listed, the EDITOR's to start from; NEW
 * PATTERN, LOAD A PATTERN, STORE IN A PATTERN; the warning before anything replaces it; no number shown on it */
#define OWN UI_EDITOR_PATTERN
static const char *asking(void)         /* a question's first line on top, else "" */
{
    const ui_overlay_t *o = ui_top();
    const char *w = o && o->kind == OV_QUESTION ? ui_overlay_words(o) : 0;
    return w ? w : "";
}
static void own_note(void)              /* a note in the EDITOR's own pattern (row 00, track 00) */
{
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE;
    c.note = 60;
    ed_put_cell(OWN, 0, 0, &c);
}
static int listed(uint8_t pat)
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256);
    for (int i = 0; i < n; i++) if (nums[i] == pat) return 1;
    return 0;
}
static int list_index(uint8_t pat, int except)          /* its line in a pattern list (except: one left out, -1 none) */
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256), k = 0;
    for (int i = 0; i < n; i++) {
        if (nums[i] == pat) return k;
        if (nums[i] != except) k++;
    }
    return -1;
}
static int same_pattern(uint8_t a, uint8_t b)           /* every cell alike, and the size */
{
    rec_pat_t p, q;
    if (rec_pattern(a, &p) || rec_pattern(b, &q) || p.ntrk != q.ntrk || p.rows != q.rows || p.step != q.step) return 0;
    for (int t = 0; t < p.ntrk; t++)
        for (int r = 0; r < p.rows; r++) {
            rec_cell_t x, y;
            int hx = ed_cell(a, r, t, &x), hy = ed_cell(b, r, t, &y);
            if (hx != hy || (hx && memcmp(&x, &y, sizeof x))) return 0;
        }
    return 1;
}
static int with_notes(void)             /* a pattern of the user's with notes (not built in), -1 none */
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256);
    for (int i = 0; i < n; i++) if (nums[i] < 0xF0 && ed_pattern_holds(nums[i])) return nums[i];
    return -1;
}
static void editor_menu(const char *item) { tap(B_SEL); check(menu_to(item), item); }
static void own_new(void)               /* the EDITOR on its own pattern, emptied (NEW PATTERN; its question: YES) */
{
    home_key(3);
    editor_menu("NEW PATTERN");
    tap(B_SEL);
    if (*asking()) tap(B_OCTP);
}

/* a fresh FM-1 (no disk): the EDITOR on its own pattern, REC and the keys write into it */
static void test_editor_own_fresh(void)
{
    home_key(3);
    run(100);
    check(screen_is("EDITOR") && ui_cur.pattern == OWN && !ui_screen_now()->sub(),
          "EDITOR's own: a fresh FM-1 opens the EDITOR on its own pattern, the header EDITOR alone");
    check(!listed(OWN), "EDITOR's own: never listed");
    check(ui_pattern_tracks(OWN) == 8 && ui_pattern_rows(OWN) == 64 && !ed_own_holds(), "EDITOR's own: empty, 8 x 64");
    tap(B_REC);
    key(12, 1); key(12, 0); run(100);
    key(14, 1); key(14, 0); run(100);
    tap(B_REC);
    check(ed_own_holds() && ui_edit_unsaved(REC_PATTERN, OWN, 0), "EDITOR's own: REC and the keys write into it (an edit)");
    shot("ui3_editor_own_fresh");
}

/* SEL's NEW PATTERN, LOAD A PATTERN, STORE IN A PATTERN */
static void test_editor_own_menu(void)
{
    char msg[160], want[40];
    int P = with_notes();
    check(P >= 0, "EDITOR's own: the disk has a pattern with notes");
    if (P < 0) return;
    int p_was = ui_edit_unsaved(REC_PATTERN, (uint8_t)P, 0);
    own_note();
    own_new();
    check(ui_cur.pattern == OWN && !ed_own_holds() && ui_pattern_tracks(OWN) == 8 && ui_pattern_rows(OWN) == 64,
          "NEW PATTERN: the EDITOR on its own pattern, emptied, 8 x 64");
    shot("ui3_editor_own_new");
    /* LOAD A PATTERN: a copy of P */
    editor_menu("LOAD A PATTERN");
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_LIST, "LOAD A PATTERN: the pattern list (LOAD INTO THE EDITOR)");
    turn(K_SELECT, -300);
    turn(K_SELECT, list_index((uint8_t)P, -1));
    shot("ui3_editor_own_load_list");
    tap(B_SEL);
    snprintf(want, sizeof want, "PATTERN %02X LOADED", P);
    snprintf(msg, sizeof msg, "LOAD A PATTERN: %02X copied into the EDITOR's own, the EDITOR on it, %s", P, want);
    check(ui_cur.pattern == OWN && same_pattern(OWN, (uint8_t)P) && ui_toast_now() && !strcmp(ui_toast_now(), want), msg);
    check(ui_edit_unsaved(REC_PATTERN, (uint8_t)P, 0) == p_was, "LOAD A PATTERN: the pattern picked not changed");
    /* STORE IN A PATTERN, + NEW PATTERN: a new number, the EDITOR on it, its own emptied */
    int f = ed_first_free();
    editor_menu("STORE IN A PATTERN");
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_LIST, "STORE IN A PATTERN: the pattern list (STORE THE PATTERN IN)");
    turn(K_SELECT, -300);                               /* (+ NEW PATTERN first) */
    shot("ui3_editor_own_store_list");
    tap(B_SEL);
    snprintf(want, sizeof want, "STORED IN PATTERN %02X", f);
    snprintf(msg, sizeof msg, "STORE IN A PATTERN, + NEW PATTERN: %s, the EDITOR on it, its own emptied", want);
    check(f >= 0 && ui_cur.pattern == f && same_pattern((uint8_t)f, (uint8_t)P) && !ed_own_holds() && ui_toast_now() &&
          !strcmp(ui_toast_now(), want), msg);
    /* STORE over a pattern with notes: asked first; NO keeps it, YES replaces it (the EDITOR following) */
    rec_cell_t c, was;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE; c.note = 0x30;
    ed_put_cell((uint8_t)f, 0, 0, &c);
    ed_cell((uint8_t)P, 0, 0, &was);
    editor_menu("STORE IN A PATTERN");
    tap(B_SEL);
    turn(K_SELECT, -300);
    turn(K_SELECT, 1 + list_index((uint8_t)P, f));
    tap(B_SEL);
    snprintf(want, sizeof want, "PATTERN %02X WILL BE REPLACED", P);
    check(!strcmp(asking(), want), "STORE over a pattern with notes asks first");
    shot("ui3_editor_own_replace");
    tap(B_OCTM);
    rec_cell_t now;
    ed_cell((uint8_t)P, 0, 0, &now);
    check(ui_cur.pattern == f && !memcmp(&now, &was, sizeof now), "STORE: NO keeps the pattern as it was, the EDITOR where it was");
    editor_menu("STORE IN A PATTERN");
    tap(B_SEL);
    turn(K_SELECT, -300);
    turn(K_SELECT, 1 + list_index((uint8_t)P, f));
    tap(B_SEL);
    tap(B_OCTP);
    ed_cell((uint8_t)P, 0, 0, &now);
    snprintf(want, sizeof want, "STORED IN PATTERN %02X", P);
    rec_cell_t kept;
    check(ui_cur.pattern == P && (now.has & RC_NOTE) && now.note == 0x30 && ed_cell((uint8_t)f, 0, 0, &kept) &&
          kept.note == 0x30 && ui_toast_now() && !strcmp(ui_toast_now(), want),
          "STORE: YES replaces it, the EDITOR on it, the numbered one it came from kept as it is");
    ui_edit_revert(REC_PATTERN, (uint8_t)f, 0);
    if (!p_was) ui_edit_revert(REC_PATTERN, (uint8_t)P, 0);
    run(200);
}

/* LOAD A PATTERN of more than 128 rows (a record from before the rule, specs/files/patterns.md#size): the EDITOR's own
 * gets its first 128 rows, the cells past row 127 left out of the copy (its room is the largest pattern's) */
static void test_editor_own_load_long(void)
{
    static uint8_t b[512];
    char msg[160];
    int f = ed_first_free(), at = 5;
    check(f >= 0, "LOAD, 200 rows: a free number");
    if (f < 0) return;
    b[0] = 1; b[1] = 199; b[2] = 1; b[3] = 1; b[4] = 0; /* 1 track of 200 rows */
    for (int c = 0; c < 100; c++) { b[at++] = 0x81; b[at++] = (uint8_t)(36 + c % 48); }  /* (a note every other row:
                                                                                         * rows 1, 3, ... 199) */
    b[at++] = 0;
    check(!ui_edit_put(REC_PATTERN, (uint8_t)f, 0, 1, b, (uint32_t)at), "LOAD, 200 rows: a pattern of 200 rows (its bytes)");
    own_new();
    check(!ed_copy_pattern((uint8_t)f, OWN), "LOAD, 200 rows: copied into the EDITOR's own");
    uint32_t l = 0;
    const uint8_t *t0 = store_piece(REC_PATTERN, OWN, 0, &l);
    snprintf(msg, sizeof msg, "LOAD, 200 rows: the EDITOR's own has the first 128 rows, 64 notes, nothing past them (%u B)",
             (unsigned)l);
    check(ui_pattern_rows(OWN) == 128 && same_pattern(OWN, (uint8_t)f) && t0 && l == 64 * 2 + 1 && !t0[l - 1] &&
          !memcmp(t0, b + 5, 128), msg);
    ui_edit_revert(REC_PATTERN, (uint8_t)f, 0);
    own_new();
    run(200);
}

/* the warning: with notes in the EDITOR's own pattern, every way to another pattern asks first */
static void own_ask_check(const char *way)
{
    char msg[120];
    snprintf(msg, sizeof msg, "the warning: %s asks EDITOR CHANGES WILL BE LOST", way);
    check(!strcmp(asking(), "EDITOR CHANGES WILL BE LOST"), msg);
}
static void test_editor_own_warning(void)
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256);
    check(n > 0, "the warning: patterns listed");
    if (!n) return;
    own_new();
    own_note();
    tap(B_ENV); tap(B_ENV);                             /* ENV's list, its first pattern picked */
    own_ask_check("ENV's pick");
    shot("ui3_editor_own_warning");
    tap(B_OCTM);
    check(ui_cur.pattern == OWN && ed_own_holds(), "the warning: NO stays on the EDITOR's own, its notes kept");
    tap(B_ENV); tap(B_ENV);
    tap(B_OCTP);
    check(ui_cur.pattern == nums[0] && !ed_own_holds(), "the warning: YES opens the pattern picked, the EDITOR's own emptied");
    tap(B_ENV); tap(B_ENV);                             /* (empty: no question) */
    check(!*asking() && ui_cur.pattern == nums[0], "the warning: an empty EDITOR's own never asks");
    /* SONGS' EDIT PATTERN */
    own_new();
    own_note();
    home_key(1);
    tap(B_EDIT);                                        /* (the order) */
    tap(B_SEL);
    check(menu_to("EDIT PATTERN"), "SONGS: the position's menu, EDIT PATTERN");
    tap(B_SEL);
    own_ask_check("SONGS' EDIT PATTERN");
    tap(B_OCTP);
    run(100);
    check(screen_is("EDITOR") && ui_cur.pattern != OWN && !ed_own_holds(), "SONGS' EDIT PATTERN, YES: the EDITOR on the position's pattern");
    tap(B_HOME); run(200);
    if (screen_is("SONGS")) { tap(B_HOME); run(200); }
    /* SONGS' ENV list: PATTERN 1A FOR THE EDITOR */
    own_new();
    own_note();
    home_key(1);
    tap(B_ENV); tap(B_ENV);
    own_ask_check("SONGS' ENV pick");
    tap(B_OCTP);
    check(ui_cur.pattern != OWN && !ed_own_holds() && ui_toast_now() && strstr(ui_toast_now(), "FOR THE EDITOR"),
          "SONGS' ENV pick, YES: that pattern the EDITOR's (PATTERN 1A FOR THE EDITOR)");
    /* PLAYBACK's EDIT */
    own_new();
    own_note();
    home_key(8);
    tap(B_EDIT);
    own_ask_check("PLAYBACK's EDIT");
    tap(B_OCTM);
    check(ui_cur.pattern == OWN && ed_own_holds(), "PLAYBACK's EDIT, NO: nothing changed");
    tap(B_HOME); run(200);
    /* a FUSION slot's EDIT PATTERN */
    int coin = fusion_slot("COIN");
    if (coin >= 0) {
        home_key(5);
        ui_set_inst((uint16_t)(FUS << 8 | coin));
        run(200);
        tap(B_EDIT);                                    /* (its edit page: SEL on the PATTERN line goes straight there) */
        tap(B_SEL);
        own_ask_check("a FUSION slot's PATTERN");
        tap(B_OCTP);
        run(100);
        check(screen_is("EDITOR") && ui_cur.pattern >= 0xE0 && ui_cur.pattern < 0xF0 && !ed_own_holds(),   /* (the disk's
                                                         * FUSION patterns: E0-E5) */
              "a FUSION slot's PATTERN, YES: the EDITOR on its pattern");
        tap(B_HOME); run(200);
        tap(B_HOME); run(200);                          /* (the edit page closed) */
    }
    home_key(3);
}

/* no number on the EDITOR's own pattern: the header, GLO's title (no USED BY), the where line, REVERT's question,
 * PLAYBACK's header and footer while its loop plays */
static void test_editor_own_texts(void)
{
    own_new();
    own_note();
    check(!ui_screen_now()->sub(), "texts: the header EDITOR, no number");
    tap(B_GLO);
    shot("ui3_editor_own_props");
    tap(B_GLO);
    tap(B_EDIT);                                        /* the note editor */
    shot("ui3_editor_own_where");
    tap(B_EDIT);
    editor_menu("REVERT");
    tap(B_SEL);
    check(!strcmp(asking(), "REVERT THE PATTERN TO SAVED?"), "texts: REVERT THE PATTERN TO SAVED?");
    tap(B_OCTM);                                        /* (KEEP) */
    tap(B_PLAY);                                        /* the EDITOR's loop plays its own */
    run(600);
    home_key(8);
    run(100);
    uint8_t col;
    const char *pg = ui_screen_now()->page(), *ft = ui_screen_now()->footer(&col);
    char msg[120];
    snprintf(msg, sizeof msg, "texts: PLAYBACK while the EDITOR's own plays: the header %s, the footer %s", pg ? pg : "-", ft ? ft : "-");
    check(pg && !strcmp(pg, "EDITOR") && ft && !strncmp(ft, "ROW ", 4) && strlen(ft) == 6, msg);
    shot("ui3_editor_own_playback");
    tap(B_PLAY);
    run(200);
    home_key(3);
}

/* never listed, never taken: FUSION's PATTERN knob, a song position's K1 PATTERN, NEW / CLONE / MOVE's free number;
 * the keys' song FE and the EDITOR's pattern FE side by side */
static void test_editor_own_hidden(void)
{
    uint32_t len;
    check(store_get(REC_SONG, 0xFE, 0, &len, 0) && store_get(REC_PATTERN, OWN, 0, &len, 0),
          "hidden: the keys' song FE and the EDITOR's pattern FE both there");
    check(ed_first_free() != OWN && ui_free_pattern() != OWN, "hidden: NEW, CLONE, MOVE never take FE");
    int coin = fusion_slot("COIN"), seen = 0;
    if (coin >= 0) {                                    /* FUSION's PATTERN knob over every value */
        home_key(5);
        ui_set_inst((uint16_t)(FUS << 8 | coin));
        run(200);
        int was_edited = ui_edit_unsaved(REC_FUSION, FUS, 0);
        uint8_t s0[128];
        ins_slot_bytes(FUS, coin, s0);
        tap(B_EDIT);                                    /* (the PATTERN line first) */
        for (int i = 0; i < 260; i++) {
            uint8_t s[128];
            in_now.knob[K_K1] += 1; run(20);
            if (ins_slot_bytes(FUS, coin, s) && s[0] == OWN) seen = 1;
        }
        tap(B_HOME);
        run(200);
        if (!was_edited) ui_edit_revert(REC_FUSION, FUS, 0);
        check(!seen, "hidden: FUSION's PATTERN knob never shows FE");
    }
    int song = song_named("CITY LIGHTS");               /* a song position's K1 PATTERN */
    if (song >= 0) {
        int was_edited = ui_edit_unsaved(REC_SONG, (uint8_t)song, 0);
        home_key(1);
        ui_cur.song = (uint8_t)song;
        tap(B_EDIT);
        seen = 0;
        for (int i = 0; i < 260; i++) {
            ui_song_t s;
            in_now.knob[K_K1] += 1; run(20);
            if (ui_song((uint8_t)song, &s) && s.npos && s.order[0] == OWN) seen = 1;
        }
        tap(B_HOME);
        run(200);
        if (!was_edited) ui_edit_revert(REC_SONG, (uint8_t)song, 0);
        check(!seen, "hidden: a song position's K1 PATTERN never shows FE");
    }
}

/* the settings record keeps which pattern the EDITOR is on (bytes 126-127); one from before (126 B) opens it on its
 * own; a numbered one gone: its own */
static void test_editor_own_settings(void)
{
    uint8_t rec[128];
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    int had = p != 0;
    if (p) memcpy(rec, p, len > sizeof rec ? sizeof rec : len);
    else memset(rec, 0, sizeof rec);
    rec[126] = 1; rec[127] = 0x0C;
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 126);    /* (a record from before: 126 B) */
    ui_settings_reload();
    check(ui_settings_pattern() == OWN, "settings: a record of 126 B: the EDITOR's own");
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, 128);
    ui_settings_reload();
    check(ui_settings_pattern() == 0x0C, "settings: bytes 126-127 keep a numbered pattern");
    /* SAVE's keeping: the EDITOR on a new pattern with a note */
    home_key(3);
    int f = ed_first_free();
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE; c.note = 60;
    ed_new_pattern((uint8_t)f, 8, 64, 1);
    ed_put_cell((uint8_t)f, 0, 0, &c);
    ed_editor_goto((uint8_t)f, 0, 0);
    run(100);
    ui_settings_keep_state();
    p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(p && len == 133 && (p[126] & 1) && p[127] == f, "settings: SAVE keeps the numbered pattern the EDITOR is on");
    ui_settings_reload();
    ui_cur.pattern = OWN;
    ui_cur.pattern = ui_settings_pattern();             /* (as power-on does) */
    home_key(3);
    run(100);
    check(ui_cur.pattern == f, "settings: power-on: the EDITOR on the pattern kept");
    ed_delete_pattern((uint8_t)f);                      /* (gone, the record still naming it) */
    home_key(1);
    ui_settings_reload();
    ui_cur.pattern = ui_settings_pattern();
    home_key(3);
    run(100);
    check(ui_cur.pattern == OWN, "settings: power-on with the kept pattern gone: the EDITOR's own");
    ui_settings_keep_state();
    p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    check(p && len == 133 && !(p[126] & 1), "settings: then SAVE keeps the EDITOR's own");
    (void)had;
    ui_edit_revert(REC_SETTINGS, 0, 0);
    ui_edit_revert(REC_PATTERN, (uint8_t)f, 0);
    ui_settings_reload();
    run(200);
}

/* REVERT ALL brings a saved EDITOR's pattern back while the EDITOR is elsewhere: the next switch asks */
static void test_editor_own_revert_all(void)
{
    uint8_t nums[256];
    int n = ui_patterns(nums, 256);
    if (!n) return;
    own_new();
    own_note();
    tap(B_SAVE);
    run(3000);
    check(!ui_edit_unsaved(REC_PATTERN, OWN, 0) && ed_own_holds(), "REVERT ALL: the EDITOR's own saved with its note");
    tap(B_ENV); tap(B_ENV);
    tap(B_OCTP);                                        /* (YES: emptied, an edit) */
    check(ui_cur.pattern == nums[0] && !ed_own_holds(), "REVERT ALL: the EDITOR on another, its own emptied");
    ui_revert_all();
    run(200);
    check(ui_cur.pattern == nums[0] && ed_own_holds(), "REVERT ALL: the EDITOR's own has its notes again, the EDITOR stays");
    tap(B_ENV); tap(B_ENV);
    own_ask_check("the next pick after REVERT ALL");
    tap(B_OCTP);
    tap(B_SAVE);                                        /* (the EDITOR's own empty again, saved) */
    run(3000);
}

/* THE KEYS' METRONOME with nothing playing (specs/ui/screens/fx.md#metronome): at FOLLOW it counts at THE KEYS'
 * TEMPO (120: a beat every 0.5 s); at a TEMPO of its own at that; the beat it clicked last (fx_met_beat, what FX
 * EDIT's picture lights) goes round the bar */
static void test_metronome_keys(void)
{
    int b0, b1, steps = 0, wrong = 0;
    check(!ui_unsaved, "METRONOME on THE KEYS: nothing unsaved before it");
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    key(UI_BLACK[9], 1); key(UI_BLACK[9], 0);           /* D#5: the METRONOME */
    tap(B_OCTP);                                        /* (on) */
    run(1000);
    b0 = fx_met_beat();
    for (int i = 0; i < 20; i++) {                      /* (2 s, a look every 100 ms) */
        run(100);
        b1 = fx_met_beat();
        if (b1 != b0) { steps++; wrong += b0 < 0 || b1 != (b0 + 1) % 4; b0 = b1; }
    }
    check(steps >= 3 && steps <= 5 && !wrong, "METRONOME on THE KEYS at FOLLOW, nothing playing: THE KEYS' 120 BPM");
    turn(K_K1, 1);                                      /* TEMPO 30 BPM, its own: a beat every 2 s */
    run(2500);
    b0 = fx_met_beat();
    run(2000);
    b1 = fx_met_beat();
    check(b0 >= 0 && b1 == (b0 + 1) % 4, "METRONOME on THE KEYS at its own TEMPO, nothing playing: it counts the bar");
    home_key(1);
    ui_revert_all();
    run(200);
    check(!ui_unsaved, "METRONOME on THE KEYS: REVERT ALL, nothing unsaved");
    check(fx_met_beat() < 0, "METRONOME on THE KEYS: REVERT ALL, off again (no beat)");
}

/* REVERT ALL with the song that played paused: its effects as saved at once (the METRONOME switched on from FX's LIVE
 * off again), not only at its next PLAY */
static void test_revert_all_fx(void)
{
    player_state_t st;
    uint8_t now[64];
    int song = song_named("CITY LIGHTS"), keep = ui_cur.song;
    if (song < 0) { check(0, "REVERT ALL FX: CITY LIGHTS"); return; }
    check(!ui_unsaved, "REVERT ALL FX: nothing unsaved before it");
    const uint8_t *sf = song_fx((uint8_t)song);
    int saved_on = sf && fxd_on(sf, FE_METRONOME);
    ui_cur.song = (uint8_t)song;
    ui_play_song((uint8_t)song, 0);
    run(300);
    tap(B_FX);
    for (int i = 0; i < 3 && strcmp(ui_screen_now()->sub(), "LIVE"); i++) tap(B_SEQ);
    tap(B_PLAY);                                        /* (paused, on FX) */
    run(200);
    key(UI_BLACK[9], 1); key(UI_BLACK[9], 0);           /* D#5: the METRONOME */
    tap(saved_on ? B_OCTM : B_OCTP);                    /* (switched the other way: an edit) */
    run(100);
    fx_now(now);
    check(ui_unsaved && fxd_on(now, FE_METRONOME) != saved_on, "REVERT ALL FX: the METRONOME switched, heard at once");
    home_key(1);
    ui_revert_all();
    run(200);
    player_state(&st, 0);
    fx_now(now);
    sf = song_fx((uint8_t)song);
    check(!ui_unsaved && st.what == PL_SONG && !st.playing && st.song == song, "REVERT ALL FX: nothing unsaved, the song paused");
    check(sf && now[7] == sf[7] && now[8] == sf[8] && !memcmp(now + 16, sf + 16, 48),
          "REVERT ALL FX: the paused song's effects as saved at once");    ui_cur.song = (uint8_t)keep;
}

/* CUT held 2 s (specs/ui/controls.md#cut): every effect of the song's setup switched off, an edit, its toast, heard so;
 * under 2 s nothing changes; REVERT ALL brings them back */
static void test_cut_fx_off(void)
{
    uint8_t now[64];
    int song = song_named("CITY LIGHTS"), keep = ui_cur.song;
    if (song < 0) { check(0, "CUT FX: CITY LIGHTS"); return; }
    check(!ui_unsaved, "CUT FX: nothing unsaved before it");
    const uint8_t *sf = song_fx((uint8_t)song);
    int on0 = sf ? sf[7] | sf[8] << 8 : 0;
    check(on0 != 0, "CUT FX: CITY LIGHTS has effects on");
    ui_cur.song = (uint8_t)song;
    home_key(1);                                        /* (SONGS) */
    press(B_HOME); press(B_REC);
    run(1500);
    sf = song_fx((uint8_t)song);
    check(!ui_unsaved && sf && (sf[7] | sf[8] << 8) == on0, "CUT FX: CUT held 1.5 s, the effects as they were");
    run(700);
    sf = song_fx((uint8_t)song);
    check(ui_unsaved && sf && !(sf[7] | sf[8] << 8) && ui_toast_now() && !strcmp(ui_toast_now(), "SOUND CUT, EFFECTS OFF"),
          "CUT FX: held 2 s, every effect off (an edit): SOUND CUT, EFFECTS OFF");
    release(B_REC); release(B_HOME);
    run(200);
    fx_now(now);
    check(!(now[7] | now[8] << 8), "CUT FX: let go, the effects heard off");
    ui_revert_all();
    run(200);
    fx_now(now);
    sf = song_fx((uint8_t)song);
    check(!ui_unsaved && sf && (sf[7] | sf[8] << 8) == on0 && (now[7] | now[8] << 8) == on0,
          "CUT FX: REVERT ALL, the effects on again as saved");
    ui_cur.song = (uint8_t)keep;
}

/* a full EDITOR's pattern rewritten with the rest of the flash full: SAVE asks as storage full, the edit stays */
static void own_full_fn(int t, int r, rec_cell_t *c, void *a)
{
    (void)a;
    c->has = RC_NOTE | RC_INS | RC_VOL | RC_CMD;
    c->note = (uint8_t)(36 + (r + t) % 48);
    c->cart = 0xF0; c->slot = (uint8_t)t;
    c->vol = (uint8_t)(r & 0x7F);
    c->n = 4;
    for (int k = 0; k < 4; k++) { c->cmd[k][0] = (uint8_t)(0x0A + k); c->cmd[k][1] = (uint8_t)(r * 4 + k); }
}
static void test_editor_own_full(void)
{
    store_usage_t u;
    static uint8_t junk[4000];
    int fill = 0, num = 0xE0, first = -1;               /* (first: the fill's first pattern, deleted below) */
    memset(junk, 0, sizeof junk);
    junk[0] = 1; junk[1] = 0; junk[2] = 1;              /* (a pattern of 1 x 1, then padding) */
    for (int sub = 0; num <= 0xEF; ) {                  /* the user area filled until a save is refused */
        if (ui_pattern_rows((uint8_t)num) && sub == 0) { num++; continue; }   /* (the disk's: FUSION's E0-E5) */
        if (store_save_begin() || store_save_put(REC_PATTERN, (uint8_t)num, (uint8_t)sub, 1, junk, sizeof junk) ||
            store_save_run() != STORE_OK) { store_save_cancel(); break; }
        if (first < 0) first = num;
        fill++;
        if (++sub == 16) { sub = 0; num++; }
    }
    player_store_changed();
    store_usage(&u);
    printf("  EDITOR's own, full: %d records of 4000 B fill the user area: %u blocks free\n", fill, u.user_free_blocks);
    own_new();
    ed_new_pattern(OWN, 16, 128, 1);
    check(!ed_rewrite(OWN, 0, 15, own_full_fn, 0, -1, -1, -1), "full: the EDITOR's own made full (16 x 128, 4 commands a cell)");
    tap(B_SAVE);
    run(4000);
    store_usage(&u);
    printf("  EDITOR's own, full: saved, %u blocks free\n", u.user_free_blocks);
    check(!ui_edit_unsaved(REC_PATTERN, OWN, 0), "full: saved into the room kept for it");
    own_note();
    tap(B_SAVE);
    run(1000);
    check(!strcmp(asking(), "NOT ENOUGH STORAGE TO SAVE") && ui_edit_unsaved(REC_PATTERN, OWN, 0),
          "full: a change of it asks as storage full, the edit kept");
    shot("ui3_editor_own_full");
    tap(B_HOME);
    run(100);
    /* a pattern deleted (the list's DELETE), then SAVE: the deletion goes first, then the EDITOR's pattern
       (specs/files/store.md#the-room-for-the-editors-pattern) */
    check(first >= 0 && !ed_delete_pattern((uint8_t)first) && ui_edit_unsaved(REC_PATTERN, (uint8_t)first, 0),
          "full: the fill's first pattern deleted (an edit)");
    tap(B_SAVE);
    run(4000);
    if (ui_top()) printf("  full: asked %s\n", asking());
    check(!ui_top() && !ui_edit_unsaved(REC_PATTERN, OWN, 0) && !ui_edit_count() && !ui_pattern_rows((uint8_t)first),
          "full: then SAVE writes both, the deletion first");
}
/* LOOPS' PLAY after something else played: the loops go on from where they paused (specs/ui/transport.md: each keeps
 * its place against the others), not from their start; the pattern list's preview (LOAD INTO LOOPS) is one such */
static void test_loops_kept(void)
{
    player_state_t st;
    char msg[160];
    static const lpc_t c4[4] = {{0, 0, 60}, {4, 0, 64}, {8, 0, 67}, {12, 0, 71}};
    home_key(22);                                       /* LOOPS */
    lp_none();
    lp_make(16, 16, 1, 0, c4, 4);
    tap(B_PLAY);
    run(1000);
    tap(B_PLAY);                                        /* paused */
    player_state(&st, 0);
    int row = st.row;
    check(st.what == PL_LOOPS && !st.playing && row >= 3, "loops kept: the loops played, paused past their start");
    ui_play_song(ui_cur.song, 0);                       /* something else plays, then pauses */
    run(600);
    player_pause();
    run(100);
    press(B_PLAY);
    run(30);
    player_state(&st, 0);
    release(B_PLAY);
    snprintf(msg, sizeof msg, "loops kept: LOOPS' PLAY after a song played: on from where they paused (row %d, paused at %d)",
             st.row, row);
    check(st.what == PL_LOOPS && st.playing && ((st.row - row) & 15) <= 2, msg);
    tap(B_PLAY);
    lp_none();
}
/* SONGS' DELETE (specs/ui/screens/songs.md#deletes-question): one line to tick, ITS PATTERNS TOO, ticked at first: the
 * song's own patterns go with it (toast), those another song uses stay; cleared: the song alone; a clone (every
 * pattern shared): nothing of its own, OCT+ deletes it alone. Each undone by REVERT ALL */
static int songs_now(void) { uint8_t nums[256]; return ui_songs(nums, 256); }
static int own_left(const uint8_t *own) { int k = 0; for (int p = 0; p < 0xF0; p++) if (own[p >> 3] >> (p & 7) & 1) k += ui_pattern_rows((uint8_t)p) > 0; return k; }
static void song_delete(int sn, int clear)              /* SONGS, the song current, DELETE, its question answered OCT+ */
{
    ui_cur.song = (uint8_t)sn;
    home_key(1);
    tap(B_SEL);
    check(menu_to("DELETE"), "delete: SONGS' menu has DELETE");
    tap(B_SEL);
    check(ui_top() && ui_top()->kind == OV_QUESTION, "delete: it asks first");
    if (clear) turn(K_K1, -1);
    tap(B_OCTP);
    run(100);
}
static void test_song_delete(void)
{
    uint8_t own[32], cown[32];
    char note[20], want[48];
    int sh, sn = song_named("PUNCH IN"), n0 = songs_now();
    if (sn < 0) { check(0, "delete: PUNCH IN on the disk"); return; }
    int n = ui_song_own((uint8_t)sn, own, note, &sh), sh0 = sh;
    printf("  delete: PUNCH IN's own patterns %d (%s), %d shared; %d edits before\n", n, note, sh, ui_edit_count());
    check(n > 0 && own_left(own) == n, "delete: PUNCH IN has patterns of its own");
    ui_cur.song = (uint8_t)sn;
    home_key(1);
    tap(B_SEL);
    menu_to("DELETE");
    tap(B_SEL);
    shot("ui3_songs_delete");
    tap(B_HOME);
    song_delete(sn, 0);
    snprintf(want, sizeof want, "SONG %02X DELETED, %d PATTERNS TOO", sn, n);
    check(songs_now() == n0 - 1 && !own_left(own) && ui_toast_now() && !strcmp(ui_toast_now(), want),
          "delete: ticked at first: the song and its own patterns go (SONG 00 DELETED, n PATTERNS TOO)");
    ui_revert_all();
    run(100);
    check(songs_now() == n0 && own_left(own) == n, "delete: REVERT ALL brings them back");
    song_delete(sn, 1);
    check(songs_now() == n0 - 1 && own_left(own) == n && !(ui_toast_now() && strstr(ui_toast_now(), "DELETED")),
          "delete: the tick cleared: the song alone, no toast");
    ui_revert_all();
    run(100);
    /* a clone: every pattern shared with PUNCH IN, so none of its own; PUNCH IN's own now shared */
    ui_cur.song = (uint8_t)sn;
    home_key(1);
    tap(B_SEL);
    menu_to("CLONE");
    tap(B_SEL);
    run(100);
    int cl = ui_cur.song, cn = ui_song_own((uint8_t)cl, cown, note, &sh);
    check(cl != sn && songs_now() == n0 + 1 && cn == 0 && !strcmp(note, "NONE") && sh == n + sh0,
          "delete: a clone has none of its own (NONE), all shared");
    tap(B_SEL);
    menu_to("DELETE");
    tap(B_SEL);
    shot("ui3_songs_delete_clone");
    tap(B_HOME);
    song_delete(cl, 0);
    check(songs_now() == n0 && own_left(own) == n && !(ui_toast_now() && strstr(ui_toast_now(), "DELETED")),
          "delete: the clone: OCT+ deletes it alone (nothing ticked), PUNCH IN's patterns stay");
    ui_revert_all();
    run(100);
    check(songs_now() == n0 && !ui_edit_count(), "delete: REVERT ALL: as before");
}
/* a pattern's DELETE, YES: every FUSION slot that played it set to FF (no pattern, `--`), saved
 * (specs/ui/saving.md#references); run last: the disk's DRUM KIT pattern goes */
static uint8_t fusion_pat(int slot)
{
    rec_cart_t c;
    if (slot < 0 || rec_cart(FUS, &c) || c.kind != REC_FUSION) return 0;
    const uint8_t *p = rec_slot(&c, (uint8_t)slot);
    return p ? rec_fusion_pattern(p) : 0;
}
static void plist_pick(int pat) { (void)pat; }
static void test_plist_delete_fusion(void)
{
    char msg[200];
    int kit = fusion_slot("DRUM KIT");
    uint8_t pat = fusion_pat(kit);
    check(kit >= 0 && pat >= 0xE0 && pat < 0xF0, "pattern DELETE: the DRUM KIT plays one of the disk's patterns");
    if (kit < 0) return;
    home_key(3);
    ed_pattern_list("TEST", pat, plist_pick, B_ENV, -1);   /* (opened by ENV: SEL then opens the list's menu) */
    turn(K_SELECT, -300);
    turn(K_SELECT, list_index(pat, -1));
    tap(B_SEL);
    check(menu_to("DELETE"), "pattern DELETE: the pattern list's menu has DELETE");
    tap(B_SEL);                                         /* (the question: USED 1 TIME, UPDATE REFERENCES?) */
    int asked = ui_top() && ui_top()->kind == OV_QUESTION;
    tap(B_OCTP);                                        /* YES */
    for (int i = 0; i < 400 && ui_top() && ui_top()->kind == OV_PROGRESS; i++) run(50);
    snprintf(msg, sizeof msg, "pattern DELETE, YES: the FUSION slot that played it set to FF (asked %d; %02X gone %d; "
             "the slot %02X; %s)", asked, pat, !ui_pattern_rows(pat), fusion_pat(kit), ui_toast_now() ? ui_toast_now() : "-");
    check(asked && !ui_pattern_rows(pat) && fusion_pat(kit) == 0xFF, msg);
    for (int i = 0; i < 3 && ui_top(); i++) tap(B_HOME);
}
static void test_question_leds(void)
{
    static const char *lines[1] = {"A QUESTION?"};
    int others = 0;
    ui_ask(lines, 1, "YES", "NO", asked);
    run(100);
    for (int b = 0; b < B_COUNT; b++) if (b != B_OCTM && b != B_OCTP && b != B_HOME && leds[b]) others++;
    check(leds[B_OCTM] == LV_FULL && leds[B_OCTP] == LV_FULL && leds[B_HOME] == LV_DIM && !others,
          "a question lights its answers OCT- and OCT+, HOME dim, the rest dark");
    tap(B_HOME);
    run(100);
    check(!ui_top() && asked_answer != 1 && leds[B_OCTP] == 0, "HOME cancels it; OCT+ back to its own rule (OCT 0: dark)");
}

/* ------------------------------------------------------------- FACTORY RESET --- */
/* specs/ui/screens/settings.md: SETTINGS' TOOLS -> FACTORY RESET asks twice (KEEP and HOME leave all as it was), the
 * second counting the changes not saved; ERASE: every block of the user area erased, a block a pass under the ERASING
 * bar (the sound silent, the keys and the seeding's commands waiting), then the restart, asked once. Last: it erases */
static int restarts;
void plat_restart(void) { restarts++; }
static int user_erased(void)
{
    for (uint32_t a = STORE_U1_START; a < STORE_U2_END; a++) if (user(a, 1) && flash[a] != 0xFF) return 0;
    return 1;
}
static void test_factory_reset(void)
{
    char msg[200];
    for (int i = 0; i < 3 && ui_top(); i++) tap(B_HOME);
    home_key(20);
    turn(K_SELECT, 20); turn(K_SELECT, -1);             /* TOOLS */
    turn(K_K1, 9);                                      /* FACTORY RESET, the last */
    check(screen_is("SETTINGS") && ui_set[S_TOOLS] == 5 && !user_erased(), "FACTORY RESET: TOOLS' last, the user area holds records");
    tap(B_EDIT);
    check(!strcmp(asking(), "FACTORY RESET?"), "FACTORY RESET asks first");
    shot("settings_factory_reset");
    tap(B_OCTM);
    check(!ui_top() && !seed_active() && !user_erased(), "FACTORY RESET: KEEP, nothing");
    tap(B_EDIT); tap(B_OCTP);
    check(!strcmp(asking(), "ERASE IT ALL NOW?"), "FACTORY RESET: RESET, then ERASE IT ALL NOW?");
    tap(B_HOME);
    check(!ui_top() && !seed_active() && !user_erased(), "FACTORY RESET: HOME on the second, nothing");
    turn(K_SELECT, -1); turn(K_K1, 1); turn(K_SELECT, 1);   /* (TUNING 441 HZ: a change not saved) */
    uint32_t unsaved = ui_unsaved;
    tap(B_EDIT); tap(B_OCTP);
    shot("settings_factory_reset_2");
    check(!strcmp(asking(), "ERASE IT ALL NOW?") && unsaved >= 1, "FACTORY RESET: a change not saved, counted (the shot)");
    press(B_OCTP);                                      /* ERASE */
    if (!seed_wiping()) release(B_OCTP);                /* (taken at the press, or at the let-go) */
    energy = 0;
    seed_info_t si;
    seed_info(&si);
    int mid = si.done > 0 && si.done < si.total && !restarts;   /* (here a pass a ms: an erase takes no time) */
    snprintf(msg, sizeof msg, "FACTORY RESET: ERASE, the ERASING bar on SETTINGS, a block a pass (%s, wiping %d, %u of %u)",
             ui_top() && ui_top()->kind == OV_PROGRESS ? "a bar" : "no bar", seed_wiping(), si.done, si.total);
    check(ui_top() && ui_top()->kind == OV_PROGRESS && seed_wiping() && seed_active() && screen_is("SETTINGS") && mid, msg);
    shot("settings_factory_reset_erasing");
    static const uint8_t hello[] = {0xF0, 0x7D, 0x50, 0x54, 0x01, 0x00, 0x01, 0xF7};
    app_sysex(hello, sizeof hello);                     /* (answered busy: nothing begins) */
    release(B_OCTP); key(12, 1); tap(B_HOME); turn(K_SELECT, 3); key(12, 0);
    for (int i = 0; i < 400 && !restarts; i++) run(10);
    seed_info(&si);
    snprintf(msg, sizeof msg, "FACTORY RESET: %u of %u blocks a pass at a time, the user area erased, the restart asked "
             "(mid-way %d, restarts %d, energy %lld, %s)", si.done, si.total, mid, restarts, (long long)energy,
             ui_top() && ui_top()->kind == OV_PROGRESS ? "the bar still up" : "no bar");
    check(mid && si.total == 103 && si.done == si.total && user_erased() && restarts == 1 && !energy && seed_wiping(), msg);
    run(500);
    check(restarts == 1 && seed_active() && !energy, "FACTORY RESET: the restart asked once; meanwhile silent, waiting");
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ui3_test DISK.ptfmdsk|- OUTDIR\n"); return 1; }
    out = argv[2];
    if (!strcmp(argv[1], "-")) {                        /* (no disk: the FM-1 as a synth only) */
        memset(flash, 0xFF, sizeof flash);
        app_init();
        run(100);
        test_keys_only();
        test_editor_own_fresh();
        printf(fails ? "ui3_test (no disk): %d of %d checks FAILED\n" : "ui3_test (no disk): %d checks passed\n", fails ? fails : checks, checks);
        return fails ? 1 : 0;
    }
    lay_disk(argv[1]);
    app_init();
    run(100);
    store_usage_t mu;                                   /* (the figure specs/files/disk.md gives) */
    store_usage(&mu);
    printf("the demo disk at the mount: the user area %u of %u blocks free (%u KB free, %u the reserve), records %u "
           "B (audio %u B)\n", mu.user_free_blocks, mu.user_blocks, (unsigned)(mu.user_free / 1024), STORE_RESERVE,
           (unsigned)mu.user_records, (unsigned)mu.user_audio);
#ifdef PTFM_DEBUG
    test_debug();                                       /* (the debug build: its figures and the bench only) */
    test_bench();
#else
    test_cue();
    test_loops_cue();
    test_unload();
    test_question_leds();
    test_over9000();
    test_header_cores();
    test_second_core_toast();
    test_status_second_core();
    test_settings_second_core();
    test_settings_voices();
    test_status_storage();
    test_status_maps();
    test_status_chip();
    test_status_edit();
    test_knob_then_press();
    test_fx_sel_oct();                  /* (before test_fx plays a song: FX edits THE KEYS' setup) */
    test_metronome_keys();              /* (the same) */
    test_fx();
    test_visual();
    test_fusion_keys();
    test_loops();
    test_unload_loops();
    test_rows_max();
    dj_as_playground();
    test_dj();
    test_dj_revert();
    test_dj_load_sync();
    test_dj_pads_waits();
    test_dj_swap_playing();
    test_unload_dj();
    test_speed();
    test_play_modes();
    test_fx_mine();
    test_editing();
    test_keys_gain();
    test_keys_mix();
    test_fx_meters();
    test_mixer_heard();
    test_presets_list();
    test_presets_instruments();
    test_keys_dry();
    test_filter_env();
    test_themes();
    test_count();
    test_quantize();
    test_recpat();
    test_auto_level();
    test_samp_marks();
    test_refs_save();                                   /* (saves: a slot swapped and back) */
    test_loops_cue_paused();
    test_editor_own_menu();
    test_editor_own_load_long();
    test_editor_own_warning();
    test_editor_own_texts();
    test_editor_own_hidden();
    test_editor_own_settings();
    test_editor_own_revert_all();                       /* (saves) */
    test_loops_save();                                  /* (saves LOOPS, then none again) */
    test_revert_all_fx();
    test_cut_fx_off();
    test_fusion_song();
    test_loops_kept();
    test_song_delete();                                 /* (REVERT ALL after each) */
    test_editor_own_full();                             /* (fills the user area) */
    test_clone_samples();                               /* (deletes a sampler to make room) */
    test_plist_delete_fusion();                         /* (deletes the DRUM KIT's pattern, saved) */
    test_factory_reset();                               /* (last: erases the user area) */
#endif
    (void)combo; (void)listen;
    printf(fails ? "ui3_test: %d of %d checks FAILED\n" : "ui3_test: %d checks passed\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
