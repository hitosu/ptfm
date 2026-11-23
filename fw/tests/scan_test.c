/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the scan's readings of the keys (hal/fm1_input.h with FM1_INPUT_ANALOG, hal/fm1_adc.h;
 * specs/hardware.md#the-keys-velocity), against a model of the FM-1 (scan_mock.h): the GPIO ports, the two 595s
 * shifted and latched as the tick drives them, the 11 x 6 matrix with its keys closed or open and each read key's
 * pulled-up and driven readings, the converter (a conversion done 60 us after its start, its value the channel's at
 * the start: the row's key in the latched column, pulled up or driven; MASTER), TIMER4 running on. Checked:
 *   - the conversion slot: after each latch, column n's PA7 key (even frames) or PA8 key (odd frames), taken at the
 *     next tick; MASTER in OCT-'s slot (odd frames, column 0), none in OCT+'s; each of the 20 keys every 2 frames;
 *   - a key the hook drives: its row an output driven high at HD0 alone while it is read, an input with its pull-up
 *     again at every read of the rows (the rows' direction and digital inputs checked at every read); the key's
 *     reading the driven one; a held key read driven still debounces as held;
 *   - the LEDs: a key the hook darkens has its column's lines dark while it is read, the column lit otherwise;
 *   - MASTER from the slot (fm1_an.master); hold: none started, the one running taken;
 *   - OFF (fm1_an.on 0): no converter access, no row driven, and every register access the same as the scan's
 *     without the readings (scan_off.c, FM1_INPUT_ANALOG undefined), lit and dim LEDs and keys held;
 *   - fm1_input_analog_off: both rows inputs, the conversion given up.
 *   scan_test */
#include <stdio.h>
#include <string.h>
#include "scan_mock.h"

/* ---- the model ---- */
static uint32_t regs[8][8];             /* port, register / 4 */
static uint32_t adc[3];                 /* CON, RES, WLA_CON0 */
static uint32_t t4;                     /* TIMER4, 24 MHz: on at each access (about one a 42 ns), 100 us a tick */
static uint32_t sr, latched = 0xFFFFu, last_pa;
static uint8_t closed[11];              /* each column's keys closed (row bits) */
static uint16_t pulled[27], driven_v[27], master_v = 512;
static int adc_on, adc_ch;              /* a conversion runs: its channel, its start, its value */
static uint32_t adc_t0, adc_val;
static int bad_rows, bad_drive;         /* violations seen */
static int logging;
static struct { uint8_t p, r; uint32_t v; } logv[200000];
static uint32_t nlog;
static uint32_t adc_starts;
static uint8_t start_lines[64], start_col[64];   /* the LED lines and column at each conversion start (the last 64) */
static uint32_t adc_reads;                       /* converter accesses */

static const int8_t KEYMAP[6][11] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static int col_now(void)                /* the column the 595s drive low (11 or more: none, or several) */
{
    int c, n = 0, at = 11;
    for (c = 0; c < 11; c++)
        if (!(latched >> c & 1u)) { n++; at = c; }
    return n == 1 ? at : 11;
}
static uint32_t led_lines(void)         /* lit now: row bit 1 PA9, 2 PA10, 3 PH6, 4 PH9 */
{
    uint32_t a = regs[0][0], h = regs[7][0];
    return (a >> 9 & 1u) << 1 | (a >> 10 & 1u) << 2 | (h >> 6 & 1u) << 3 | (h >> 9 & 1u) << 4;
}
static int is_driven(uint32_t pin)      /* PA7 / PA8 an output, high */
{
    return !(regs[0][2] >> pin & 1u) && (regs[0][0] >> pin & 1u);
}
static void sync(void)
{
    uint32_t a = regs[0][0];
    if ((a & 8u) && !(last_pa & 8u))
        sr = (sr << 1 | (a >> 4 & 1u)) & 0xFFFFu;
    if ((a & 2u) && !(last_pa & 2u))
        latched = sr;
    last_pa = a;
    if ((adc[0] & 0x50u) == 0x50u && !adc_on) {    /* started: the value as the line stands now */
        int c = col_now();
        adc_on = 1;
        adc_ch = (int)(adc[0] >> 8 & 15u);
        adc_t0 = t4;
        adc_starts++;
        start_lines[adc_starts % 64] = (uint8_t)led_lines();
        start_col[adc_starts % 64] = (uint8_t)c;
        if (adc_ch == 4)
            adc_val = master_v;
        else if ((adc_ch == 0 || adc_ch == 1) && c < 11) {
            int id = KEYMAP[adc_ch ? 4 : 3][c], pin = adc_ch ? 8 : 7;
            int drv = is_driven((uint32_t)pin), hd0 = regs[0][6] >> pin & 1, hd = regs[0][7] >> pin & 1;
            if (drv && (!hd0 || hd))
                bad_drive++;                          /* (driven, but not at HD0 alone) */
            adc_val = id >= 14 ? (drv ? driven_v[id - 14] : pulled[id - 14]) : 1023u;
        } else
            adc_val = 1023u;
    }
    if (!(adc[0] & 0x10u))
        adc_on = 0;
}
volatile uint32_t *mock_reg(uint32_t p, uint32_t r)
{
    sync();
    t4++;
    if (logging && nlog < sizeof logv / sizeof logv[0]) {
        logv[nlog].p = (uint8_t)p;
        logv[nlog].r = (uint8_t)r;
        logv[nlog++].v = regs[p & 7u][(r / 4u) & 7u];
    }
    if (r == 0x04u) {                   /* IN: the rows as the matrix and the pins make them */
        int c = col_now();
        uint32_t in = 0xFFFFFFFFu, k;
        static const uint8_t PIN[6] = {0, 5, 6, 7, 8, 7};
        if (p == 0u && ((regs[0][2] >> 7 & 3u) != 3u || (regs[0][3] >> 7 & 3u) != 3u))
            bad_rows++;                 /* (PA7 / PA8 not inputs with their digital inputs on, at a read) */
        for (k = 0; k < 6u; k++) {
            uint32_t port = k == 5u ? 1u : 0u, pin = PIN[k];
            if (port != p)
                continue;
            if (!(regs[port][2] >> pin & 1u))
                in = (in & ~(1u << pin)) | (regs[port][0] & 1u << pin);   /* (an output reads its own level) */
            else if (c < 11 && (closed[c] >> k & 1u))
                in &= ~(1u << pin);
        }
        regs[p][1] = in;
    }
    return &regs[p & 7u][(r / 4u) & 7u];
}
volatile uint32_t *mock_adc(uint32_t w)
{
    sync();
    t4++;
    adc_reads++;
    if (logging && nlog < sizeof logv / sizeof logv[0]) {
        logv[nlog].p = 0xAD;
        logv[nlog].r = (uint8_t)w;
        logv[nlog++].v = adc[w];
    }
    if (w == 0u)
        adc[0] = adc_on && t4 - adc_t0 >= 60u * 24u ? adc[0] | 0x80u : adc[0] & ~0x80u;
    if (w == 1u)
        adc[1] = adc_val;
    return &adc[w];
}
volatile uint32_t *mock_t4(void)
{
    sync();
    t4 += 24u;                          /* (a read of the timer: a microsecond, so its loops end) */
    return &t4;
}

/* ---- the scan, with the readings' hooks ---- */
static struct { uint8_t n, driven; uint16_t v; uint32_t frame; } got[4096];
static uint32_t ngot;
static uint32_t drive_mask, dark_mask, frame_no;
static void hook_read(uint32_t n, uint32_t v, uint32_t d)
{
    if (ngot < 4096u) {
        got[ngot].n = (uint8_t)n;
        got[ngot].v = (uint16_t)v;
        got[ngot].driven = (uint8_t)d;
        got[ngot++].frame = frame_no;
    }
}
static uint32_t notes_seen;
#define FM1_INPUT_ANALOG 1
#define FM1_INPUT_READ(n, v, d) hook_read((n), (v), (d))
#define FM1_INPUT_DRIVE(n) ((drive_mask >> (n)) & 1u)
#define FM1_INPUT_DARK(n) ((dark_mask >> (n)) & 1u)
#define FM1_INPUT_NOTE(n, down) (notes_seen ^= 1u << (n))
#include "../hal/fm1_adc.h"
#include "../hal/fm1_input.h"

void off_init(void);
void off_tick(void);
void off_leds(const uint8_t *lit, const uint8_t *dim);
uint32_t off_notes(void);

static int fails, checks;
static void check(const char *what, int ok)
{
    checks++;
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void model_reset(void)
{
    memset(regs, 0, sizeof regs);
    memset(adc, 0, sizeof adc);
    memset(closed, 0, sizeof closed);
    sr = 0;
    latched = 0xFFFFu;
    last_pa = 0;
    adc_on = 0;
    t4 = 1000;
    for (int i = 0; i < 27; i++)
        pulled[i] = driven_v[i] = 1023;
    bad_rows = bad_drive = 0;
}
static void tick(void)                  /* one TIMER5 tick, then 100 us on */
{
    fm1_input_tick();
    if (fm1__tick_col == 0u)
        frame_no++;
    sync();
    t4 += 2400u;
}
static void ticks(uint32_t k) { while (k--) tick(); }

int main(void)
{
    char what[200];
    model_reset();
    fm1_input_init();
    fm1_input_analog(1);                /* (KEYS VELOCITY on: the rows' drive set once) */
    ticks(22);                          /* (two frames: the parity settled) */
    ngot = 0;
    {   /* the slot: 4 frames, nothing pressed */
        uint32_t f0 = frame_no, k, pa7 = 0, pa8 = 0, bad = 0, seen[20] = {0};
        ticks(44);
        for (k = 0; k < ngot; k++) {
            uint32_t n = got[k].n;
            if (n >= 20u || got[k].v != 1023u || got[k].driven)
                bad++;
            else
                seen[n]++;
            if (n >= 9u && n <= 19u) pa7++;
            else pa8++;
        }
        for (k = 0; k < 20u; k++)
            if (seen[k] != 2u)
                bad++;
        snprintf(what, sizeof what, "4 frames: %u readings, each of the 20 keys twice (every 2 frames), PA7's 22, PA8's 18 (%u, %u)",
                 ngot, pa7, pa8);
        check(what, ngot == 40u && !bad && pa7 == 22u && pa8 == 18u && frame_no - f0 == 4u);
        check("... MASTER read in OCT-'s slot (fm1_an.master)", fm1_an.mvalid && fm1_an.master == 512u);
        master_v = 700;
        ticks(22);
        check("... and follows it (700)", fm1_an.master == 700u && !fm1_an.lost);
        check("... the rows inputs with their digital inputs on at every read", bad_rows == 0);
    }
    {   /* the order: column n's key read after its latch, taken at the next tick */
        uint32_t k, ok = 1;
        ngot = 0;
        while (fm1__tick_col != 10u) tick();
        tick();                         /* (now column 0 latched: its conversion running) */
        int odd = fm1_an.odd;
        for (k = 0; k < 11u; k++) {
            uint32_t want = (uint32_t)KEYMAP[odd ? 4 : 3][(k + 0u) % 11u];
            tick();                     /* (column k's taken; k + 1's started) */
            if (want >= 14u) {
                if (!ngot || got[ngot - 1u].n != want - 14u) ok = 0;
            }
        }
        snprintf(what, sizeof what, "a frame's readings in the columns' order, the frame's row (%s)", odd ? "PA8" : "PA7");
        check(what, ok);
    }
    {   /* a key driven: note 9 (D4, PA7, column 0) */
        uint32_t k, n9 = 0, d9 = 0;
        closed[0] |= 1u << 3;
        pulled[9] = 190;
        driven_v[9] = 455;
        drive_mask = 1u << 9;
        ticks(1);                       /* (the conversion already running: from before) */
        ngot = 0;
        ticks(88);
        for (k = 0; k < ngot; k++)
            if (got[k].n == 9u) { n9++; if (got[k].driven && got[k].v == 455u) d9++; }
        snprintf(what, sizeof what, "a key read driven: its readings the driven ones (%u of %u), the row at HD0 alone",
                 d9, n9);
        check(what, n9 == 4u && d9 == 4u && bad_drive == 0);
        check("... its row an input with its pull-up again at every read of the rows", bad_rows == 0);
        check("... held, read driven: the digital scan holds it (F3 + 9)", (fm1_in.notes >> 9 & 1u) && (notes_seen >> 9 & 1u));
        drive_mask = 0;
        ticks(22);
        check("... the drive off: its readings pulled up again (190)", got[ngot - 1u].n != 9u || got[ngot - 1u].v == 190u);
    }
    {   /* the LEDs: column 0 lit (rows 1-4), key 9 dark while it is read */
        uint32_t k, dark_ok = 1, lit_ok = 1, d = 0, l = 0, s0;
        fm1_led[0] = 0x1Eu;
        fm1_led[3] = 0x06u;
        dark_mask = 1u << 9;
        s0 = adc_starts;
        ticks(44);
        for (k = s0 + 1u; k <= adc_starts; k++) {
            uint8_t lines = start_lines[k % 64], c = start_col[k % 64];
            int pa7 = fm1_an.odd ? 0 : 1;           /* (only for the 22-tick window: both parities seen) */
            (void)pa7;
            if (c == 0u && lines == 0u) d++;
            else if (c == 0u && lines == 0x1Eu) l++;
            if (c == 3u && lines != 0x06u) lit_ok = 0;
            if (c == 0u && lines != 0u && lines != 0x1Eu) dark_ok = 0;
        }
        snprintf(what, sizeof what, "the LEDs: column 0 dark while key 9 is read (%u times), lit at its other slot (%u: MASTER's)", d, l);
        check(what, d == 2u && l == 2u && dark_ok);
        check("... another column's LEDs as lit, a key there not darkened", lit_ok);
        dark_mask = 0;
        memset(fm1_led, 0, sizeof fm1_led);
    }
    {   /* hold: none started, the one running taken */
        uint32_t s0;
        fm1_an.hold = 1;
        tick();
        s0 = adc_starts;
        check("hold: the conversion running taken", !fm1_an.pend);
        ticks(30);
        check("... none started while it holds", adc_starts == s0);
        fm1_an.hold = 0;
        ticks(2);
        check("... let go: the readings on", adc_starts > s0);
    }
    {   /* fm1_input_analog_off */
        drive_mask = 1u << 9;
        while (!(fm1_an.pend && fm1_an.driven)) tick();
        fm1_input_analog_off();
        sync();
        check("fm1_input_analog_off: both rows inputs with their pull-ups, no conversion",
              (regs[0][2] >> 7 & 3u) == 3u && !(regs[0][0] >> 7 & 3u) && !fm1_an.pend && !fm1_an.on && !adc_on);
        drive_mask = 0;
    }
    {   /* OFF: the scan as without the readings, access for access */
        static const uint8_t LIT[11] = {0x02, 0, 0x10, 0, 0x04, 0, 0, 0x08, 0, 0x1E, 0x02};
        static const uint8_t DIM[11] = {0x04, 0x08, 0, 0x02, 0, 0, 0x10, 0, 0x02, 0, 0};
        static uint32_t log_on[200000], n_on;
        uint32_t i, same = 1, adc0;
        model_reset();
        memset((void *)&fm1_in, 0, sizeof fm1_in);
        fm1__tick_col = 0;
        fm1_input_init();
        fm1_an.on = 0;
        for (i = 0; i < 11u; i++) { fm1_led[i] = LIT[i]; fm1_led_dim[i] = DIM[i]; }
        closed[2] = 1u << 4 | 1u << 2;  /* (F#3-ish on PA8, a top key: held) */
        closed[5] = 1u << 3;
        adc0 = adc_reads;
        nlog = 0;
        logging = 1;
        for (i = 0; i < 330u; i++) tick();
        logging = 0;
        n_on = nlog;
        for (i = 0; i < nlog; i++) log_on[i] = (uint32_t)logv[i].p << 24 | (uint32_t)logv[i].r << 16 | (logv[i].v & 0xFFFFu);
        check("OFF: no converter access, no row driven", adc_reads == adc0 && bad_rows == 0);
        uint32_t notes_on = fm1_in.notes;
        model_reset();
        off_init();
        off_leds(LIT, DIM);
        closed[2] = 1u << 4 | 1u << 2;
        closed[5] = 1u << 3;
        nlog = 0;
        logging = 1;
        for (i = 0; i < 330u; i++) { off_tick(); sync(); t4 += 2400u; }
        logging = 0;
        for (i = 0; i < nlog && i < n_on; i++)
            if (log_on[i] != ((uint32_t)logv[i].p << 24 | (uint32_t)logv[i].r << 16 | (logv[i].v & 0xFFFFu))) { same = 0; break; }
        snprintf(what, sizeof what, "OFF: every register access the same as the scan's without the readings (%u accesses, 30 frames)", n_on);
        check(what, same && nlog == n_on && notes_on == off_notes() && notes_on != 0u);
    }
    {   /* the cost at ON: register accesses a tick, against OFF's */
        uint32_t i, a0, a1, b0, b1;
        model_reset();
        memset((void *)&fm1_in, 0, sizeof fm1_in);
        fm1__tick_col = 0;
        fm1_input_init();
        fm1_an.on = 0;
        logging = 1;
        nlog = 0;
        for (i = 0; i < 220u; i++) tick();
        a0 = nlog;
        fm1_input_analog(1);
        drive_mask = 0xFFFFFu;
        dark_mask = 0xFFFFFu;
        nlog = 0;
        for (i = 0; i < 220u; i++) tick();
        a1 = nlog;
        logging = 0;
        b0 = a0 / 220u;
        b1 = a1 / 220u;
        snprintf(what, sizeof what, "the cost: %u register accesses a tick at OFF, %u with every key read driven (%u more)", b0, b1, b1 - b0);
        check(what, b1 > b0 && b1 - b0 < 40u);
    }
    printf("scan_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
