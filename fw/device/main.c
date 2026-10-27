/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM on the FM-1: the device's side. The app (fw/core) runs on what this file gives it (plat.h):
 * time, the panel, the LEDs, the screen, the sound, the flash, USB-MIDI SysEx. One compilation unit:
 * the device layer (the HAL is header-only; libc, lcd, gfx, usb, ota, recovery, core2), then the
 * interrupts, plat_*, the boot and the main loop.
 *
 * Time: TIMER5 at 10 kHz scans the matrix and keeps the clocks; every 5th tick polls USB (2 kHz);
 * every 10th applies the LEDs (1 kHz). ALNK0 asks for a half of 256 frames (5.8 ms): app_audio fills
 * it, then MASTER, LINE GAIN, mono and the soft ceiling turn it into the codec's 24-bit words; with
 * SECOND CORE on, core 2 does each block's effects and that output stage (core2.c). The main loop
 * reads MASTER and the battery, routes SysEx (PT/FM's to the app, the updater's to ota.c), runs
 * app_poll.
 *
 * The update path and the USB rescue: hold OCT- alone at power-on, or two failed boots in a row, for
 * USB RESCUE; the install page works from there. */
#include <stdint.h>
#ifndef FW_ID
#define FW_ID "FM-1_7101"                /* package identity (tools/build.py passes it) */
#endif
#define FW_OTA 1                         /* the update path (ota.c) and the USB rescue (recovery.c) */
#define FW_UAC 1                         /* the USB audio input (usb.c, uac.h) */
#define USB_BCD_DEVICE 0x7102u           /* (hosts cache a device's descriptors per version: 7102 the audio input) */
#define LCD_BAUD 0u                      /* SPI1 at 60 MHz (lcd.c's 4 = 12 MHz): a full frame ~30 ms */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
#define UA_PUBLISH() fm1_csync()         /* the same for USB audio's ring (uac.h): written on core 2, read on core 1 */
#define UAC_C2 PLAT_C2                   /* USB audio's per-block part (uac.h) and the output stage (out.h): a */
#define OUT_C2 PLAT_C2                   /* block's job's, in core 2's room (plat.h; tools/build.py defines it) */
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_cpu1.h"                    /* the second core (specs/second-core.md) */
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"

static volatile uint32_t fm1_ms;         /* milliseconds since boot (TIMER4-based, the TIMER5 ISR) */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));

#include "libc.c"
/* The LCD's buffer (3,840 B): plat_lcd_rows swaps the app's rows into its two halves. Between the app's
 * frames the main loop lends its parts out, after lcd_sync, never while the app's rows go out:
 *   bytes    0 ..  1023  the firmware's own screens (lcd_fill's row, gfx.c's canvas: the update, the
 *                        rescue, the crash), a band of text at a time; a received PT/FM SysEx (sysex_route)
 *   bytes 1024 ..  3711  the update path's buffers (ota.c: the decoded message, the staging's)
 *   bytes 3584 ..  3839  a flash page (plat_flash_prog: never during an update)
 * So the update path needs nothing of the store's and always gets its buffers. */
#define LCD_CHUNK_ROWS 4u
static uint16_t lcd_buf[2u * LCD_CHUNK_ROWS * 240u] __attribute__((section(".pool")));
#define LCD_FILLBUF lcd_buf                      /* lcd_fill's row (240 pixels) */
#include "lcd.c"
#define CV_MAX 512u                              /* 240 x 2: text is drawn in bands of 2 rows */
#define CV_SHARED lcd_buf
#include "gfx.c"
#include "usb.c"
#include "midi_uart.c"                   /* the MIDI IN jack, into usb.c's ring as cable 1 */

/* ---- MIDI: TIMER5 moves what usb.c and the jack queued into mrx with the arrival time (so usb.c's ring
 * never fills: a full one would hold back SysEx, an update's too); plat_midi_rx takes it in the main loop.
 * Out: the main loop and the audio context each fill a ring of their own; TIMER5, usb.c's one producer,
 * moves them into its queue: the main loop's at once, the audio context's when the frame each was made at
 * is heard (the clock out, the tracks' notes: specs/connections.md#clock). ---- */
#define MRX 48u                          /* (between two main-loop passes, ~30 ms at most) */
static uint32_t mrx_pkt[MRX];
static uint16_t mrx_ts[MRX];             /* fm1_ms when it came, its low 16 bits */
static volatile uint32_t mrx_w, mrx_r, mrx_lost;
#define MTX 16u                          /* (TIMER5 empties it every 0.5 ms) */
static uint32_t mtx_q[MTX];              /* the main loop's: usb.c's packets */
static volatile uint32_t mtx_w, mtx_r;
#define MTA 64u                          /* (a half's and the next's: a row of 16 tracks' notes is 32) */
static uint32_t mta_q[MTA];              /* the audio context's: status, data 1, data 2, its time (bit 7: the half
                                          * it was made in, 0-1; bits 0-6: its frame there / 2) */
static volatile uint32_t mta_w, mta_r;
static volatile uint32_t half_at[2];     /* when the halves made last and before start to be heard (fm1_ticks) */
static volatile uint8_t half_par;        /* which of the two the half being made is */
#define HALF_TICKS (5804u * FM1_TICKS_PER_US)   /* (the half made now plays after the one heard now) */
#define FRAME2_TICKS (2u * FM1_TICKS_PER_US * 1000000u / 44100u)   /* (two frames) */
static volatile uint8_t in_audio;        /* app_audio runs (the ALNK0 interrupt) */

static void midi_in_move(void)           /* TIMER5 */
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ];
        RING_PUBLISH();
        mi_r++;
        if (mrx_w - mrx_r < MRX) {
            mrx_pkt[mrx_w % MRX] = pkt;
            mrx_ts[mrx_w % MRX] = (uint16_t)fm1_ms;
            RING_PUBLISH();
            mrx_w++;
        } else {
            mrx_lost++;
        }
    }
}

static uint32_t midi_cin(uint32_t st)    /* USB-MIDI's code index for a status (0: not sent) */
{
    return st >= 0xF8u ? 0xFu : st == 0xF2u ? 3u : st >= 0xF0u ? 0u : st >> 4;
}

static void midi_out_move(void)          /* TIMER5 */
{
    while (mtx_r != mtx_w && midi_out_event(mtx_q[mtx_r % MTX])) {
        RING_PUBLISH();
        mtx_r++;
    }
    while (mta_r != mta_w) {             /* the audio's: each when its frame is heard (in order: their times rise) */
        uint32_t e = mta_q[mta_r % MTA], st = e & 0xFFu, cin = midi_cin(st), tm = e >> 24;
        uint32_t due = half_at[tm >> 7] + (tm & 127u) * FRAME2_TICKS;
        if ((int32_t)(fm1_ticks() - due) < 0)
            break;
        if (!midi_out_event(cin | st << 8 | (cin == 0xFu ? 0u : (e & 0xFFFF00u) << 8)))
            break;
        RING_PUBLISH();
        mta_r++;
    }
}

#include "plat.h"
#include "app.h"
#ifndef PTFM_BRINGUP
#include "store/store.h"                 /* an update waits for a save being written */
#include "store/seed.h"                  /* ... and for a seeding under way */
#else                                    /* (the bring-up app has neither) */
static int store_saving(void) { return 0; }
static int seed_active(void) { return 0; }
#endif

/* ---- the rest of libc that fw/core may call (libc.c has memset, memcpy, memcmp) ---- */
void *memmove(void *d, const void *s, unsigned long n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q)
        while (n--)
            *p++ = *q++;
    else
        while (n--)
            p[n] = q[n];
    return d;
}

unsigned long strlen(const char *s)
{
    unsigned long n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned long n)
{
    for (; n; n--, a++, b++)
        if (*a != *b || !*a)
            return (unsigned char)*a - (unsigned char)*b;
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++))
        ;
    return d;
}

char *strncpy(char *d, const char *s, unsigned long n)
{
    unsigned long i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, unsigned long n)
{
    char *p = d + strlen(d);
    while (n-- && *s)
        *p++ = *s++;
    *p = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strstr(const char *h, const char *n)
{
    unsigned long k = strlen(n);
    if (!k)
        return (char *)h;
    for (; *h; h++)
        if (*h == *n && !strncmp(h, n, k))
            return (char *)h;
    return 0;
}

void *memchr(const void *s, int c, unsigned long n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return 0;
}

/* ---- 64-bit divides: no runtime library is linked, so the compiler's helpers are here. They are slow
 * (a bit at a time); tools/build.py refuses them in the sound's code (core/eng, core/play, core/fx) and
 * lists who calls them. ---- */
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    int i;
    if (!d) {                            /* as the hardware's divider: no trap, all ones */
        if (rem)
            *rem = n;
        return ~0ull;
    }
    for (i = 63; i >= 0; i--) {
        r = r << 1 | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return __udivmoddi4(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    __udivmoddi4(n, d, &r);
    return r;
}
long long __divdi3(long long n, long long d)
{
    unsigned long long q = __udivmoddi4(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n,
                                        d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, 0);
    return (n < 0) != (d < 0) ? (long long)(0ull - q) : (long long)q;
}
long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    __udivmoddi4(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n,
                 d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, &r);
    return n < 0 ? (long long)(0ull - r) : (long long)r;
}

/* ---- the flash ----
 * The RAM driver may erase and program: the update's staging (ota.c); the user area, fast regions 1
 * and 2 (specs/files/store.md). Never the slot, FWS's free part included: the device never writes FWS
 * (an install's update loader does, with its own range). */
#define FL_USER1_LO 0x00093000u
#define FL_USER1_HI 0x000E0000u
#define FL_USER2_LO 0x000E5000u
#define FL_USER2_HI 0x000FF000u
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x000E0000u, 0x000E5000u) || FL_IN(off, n, FL_USER1_LO, FL_USER1_HI) || \
                             FL_IN(off, n, FL_USER2_LO, FL_USER2_HI))
#include "fm1_flash.h"
static uint8_t flash_ok;                 /* the JEDEC id matched at boot */
static uint8_t fl_plain;                 /* the plain window is set: the user area reads in place */

static void c2_quiet(void);              /* (core2.c, below) */

/* direct reads, 256-byte IRQ-off windows, each once core 2 is idle: its voice job runs from the flash, which a direct
 * read switches off (specs/second-core.md, Core 2 and the flash) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc;
        c2_quiet();
        rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

/* ---- the sound ---- */
#define HALF_FRAMES 256u                 /* I2S half buffer: 5.8 ms */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define HALF_US (HALF_FRAMES * 1000000u / 44100u)
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));   /* the DMA's two halves */
static volatile uint8_t audio_mute;      /* the update path runs: no app_audio */
static volatile uint32_t cpu_last, cpu_max, cpu_late, cpu_sum, cpu_halves;
#include "out.h"                         /* MASTER, LINE GAIN, mono, the soft ceiling */
static volatile int32_t out_master = 4096;       /* MASTER, squared (Q12) */
static volatile int32_t out_line = 6492;         /* LINE GAIN (Q12; its 0 dB, out.h's OUT_LINE_REF, till the settings come) */
static volatile uint8_t out_mono;

/* The half being made, written a block at a time as the player makes each one (app_audio_out): the half
 * holds made sound or silence, never the player's 16-bit values, so a late half (the DMA reaching it before
 * it is done) plays what was made and then silence, never noise. Its gain and mono are set as the half
 * starts: core 2 converts its blocks after the interrupt has returned, while the main loop may change them. */
static int32_t *out_half;
static int32_t out_gain;
static uint8_t out_mono_half;
static PLAT_C2 void out_block(const int32_t *lr, int at, int n)
{
    if (lr && at >= 0 && n > 0 && (uint32_t)(at + n) <= HALF_FRAMES)   /* (lr 0, a silent block: the half was
                                                                       * cleared before the player ran) */
        out_convert_block(lr, out_half + 2 * at, (uint32_t)n, out_gain, out_mono_half);
}

/* USB audio (specs/connections.md#usb-audio): the mix after the effects (the player's, before DJ's PREVIEW), the
 * clicks left out, times USB GAIN in MASTER's place, always stereo, then the soft ceiling, as 16-bit values; into
 * usb.c's stream only while the host reads */
static volatile int32_t usb_gain = 5157;         /* USB GAIN (Q12; its 0 dB, out.h's OUT_USB_REF, till the settings come) */
static int32_t usb_gain_half;                    /* (the half's, as out_gain) */
#ifdef PTFM_DEBUG
#include "dbg.h"                                 /* (the debug build's USB test tone) */
#endif
static inline PLAT_C2 int16_t usb_value(int32_t v, int32_t g)   /* a mix value (the clicks out) to the computer's */
{
    v = v < -131072 ? -131072 : v;           /* (the mix before the player's ceiling; as min and max, no jumps) */
    v = v > 131071 ? 131071 : v;
    v = (int32_t)(((int64_t)v * g) >> 4);    /* (x g in 64 bits: at +14 dB, 2^17 x 20529 passes 2^31; >> 4 fits) */
    return (int16_t)(out_ceiling(v) >> 8);   /* (full scale at the stage's unity: 2^15 << 8, the ceiling's 2^23) */
}
static PLAT_C2 void usb_block(const int32_t *lr, const int16_t *click, int n)
{
    int16_t *b = usb_audio_in();                 /* (straight into the resampler's history) */
    int32_t g = usb_gain_half, i;
    if (!b || n <= 0 || n > (int)UA_BLK)
        return;
#ifdef PTFM_DEBUG
    if (dbg_usb_tone) {                          /* the test tone instead of the mix (specs/debug.md) */
        dbg_tone_block(b, n);
        usb_audio_run((uint32_t)n);
        return;
    }
#endif
    if (click)                                   /* (a frame a turn, the clicks' test out of the loop) */
        for (i = 0; i < 2 * n; i += 2) {
            b[i] = usb_value(lr[i] - click[i >> 1], g);
            b[i + 1] = usb_value(lr[i + 1] - click[i >> 1], g);
        }
    else
        for (i = 0; i < 2 * n; i += 2) {
            b[i] = usb_value(lr[i], g);
            b[i + 1] = usb_value(lr[i + 1], g);
        }
    usb_audio_run((uint32_t)n);
}
void plat_usb_gain(int db) { usb_gain = out_usb_gain_q12(db); }

#include "core2.c"                       /* the second core: its start, the hand-over, the fallbacks, its figures */

#ifdef PTFM_DEBUG
/* The debug build's own figures (specs/debug.md, core/dbg.h's plat_dbg): the audio interrupt's window, flash
 * reads in the audio context, the main loop's passes, the stacks painted at power-on to find their depth */
#include "dbg.h"
static volatile uint32_t dbg_isr_sum;
static volatile uint16_t dbg_isr_max, dbg_halves, dbg_late, dbg_fl_n, dbg_fl_us;
static uint16_t dbg_passes;
static uint32_t dbg_pass_max, dbg_pass_t0;
/* (the stacks, app.ld's names: the main loop's _ustack_lo .. _ustack_top, the interrupts' _sstack_lo .. _sstack_top) */
#define DBG_US_LO ((uint32_t *)_ustack_lo)
#define DBG_US_TOP ((uint32_t *)_ustack_top)
#define DBG_SS_LO ((uint32_t *)_sstack_lo)
#define DBG_SS_TOP ((uint32_t *)_sstack_top)
#define DBG_PAINT 0x5AA5C33Cu
static void dbg_paint(void)              /* before interrupts: the interrupts' stack whole, the main loop's below here */
{
    volatile uint32_t here = 0;
    uint32_t *p;
    for (p = DBG_SS_LO; p < DBG_SS_TOP; p++)
        *p = DBG_PAINT;
    for (p = DBG_US_LO; p < (uint32_t *)&here - 64; p++)
        *p = DBG_PAINT;
}
static uint16_t dbg_depth(const uint32_t *lo, const uint32_t *top)
{
    const uint32_t *p = lo;
    while (p < top && *p == DBG_PAINT)
        p++;
    return (uint16_t)((const uint8_t *)top - (const uint8_t *)p);
}
static uint16_t sat16u(uint32_t v) { return (uint16_t)(v > 65535u ? 65535u : v); }
void plat_dbg(plat_dbg_t *d)
{
    uint32_t f = irq_save();
    d->isr_sum_us = dbg_isr_sum;
    d->isr_max_us = dbg_isr_max;
    d->halves = dbg_halves;
    d->late = sat16u(dbg_late + c2_win.late);   /* (either core's: 37 tells them apart) */
    d->flash_reads = dbg_fl_n;
    d->flash_us = dbg_fl_us;
    d->c2.late1 = dbg_late;              /* core 2's window (core2.c's c2_win) */
    d->c2.late2 = sat16u(c2_win.late);
    d->c2.sum_us = c2_win.sum;
    d->c2.max_us = sat16u(c2_win.max);
    d->c2.blocks = sat16u(c2_win.blocks);
    d->c2.halves = sat16u(c2_win.halves);
    d->c2.wait_sum_us = c2_win.wait_sum;
    d->c2.wait_max_us = sat16u(c2_win.wait_max);
    dbg_isr_sum = 0;
    dbg_isr_max = dbg_halves = dbg_late = dbg_fl_n = dbg_fl_us = 0;
    c2_win.late = c2_win.sum = c2_win.max = c2_win.blocks = c2_win.halves = c2_win.wait_sum = c2_win.wait_max = 0;
    irq_restore(f);
    d->c2.misreads = sat16u(fm1_c1_q.bad);   /* (its queue cleared once, at its start: one a run of the firmware) */
    if (fm1_c1.state == FM1_C1_RUNS || fm1_c1.state == FM1_C1_HELD) {   /* (it started: its marks, its record) */
        d->c2.stack_used = sat16u(fm1_cpu1_stack_used());
        if (fm1_cpu1_faulted()) {
            d->c2.fault_vec = fm1_c1_fault.vec;
            d->c2.fault_pc = fm1_c1_fault.pc;
        }
    }
    d->c2.stack_size = sat16u(fm1_cpu1_stack_size());
    d->c2.start_10ns = sat16u(fm1_c1.started ? fm1_c1.start_ticks * 100u / FM1_TICKS_PER_US : 0u);
    d->c2.clk = 0;                               /* (one try now, without the clock-divider bit) */
    d->passes = dbg_passes;
    d->pass_max_us = dbg_pass_max;
    dbg_passes = 0;
    dbg_pass_max = 0;
    d->stack_used[0] = dbg_depth(DBG_US_LO, DBG_US_TOP);
    d->stack_size[0] = (uint16_t)((uint8_t *)DBG_US_TOP - (uint8_t *)DBG_US_LO);
    d->stack_used[1] = dbg_depth(DBG_SS_LO, DBG_SS_TOP);
    d->stack_size[1] = (uint16_t)((uint8_t *)DBG_SS_TOP - (uint8_t *)DBG_SS_LO);
    {                                    /* USB audio: its counters since the last window */
        static uint32_t last[5];
        uint32_t now[5], k;
        f = irq_save();
        now[0] = ua.pkts;
        now[1] = ua.underruns;
        now[2] = uac.missed;
        now[3] = uac.stalls;
        now[4] = ua.primes;
        d->usb.on = ua.go;
        d->usb.r48 = ua.r48;
        d->usb.fill = (uint16_t)ua.fill;
        irq_restore(f);
        d->usb.target = ua.r48 ? UA_T48 : UA_T44;
        d->usb.pkts = sat16u(now[0] - last[0]);
        d->usb.dry = sat16u(now[1] - last[1]);
        d->usb.missed = sat16u(now[2] - last[2]);
        d->usb.stalls = sat16u(now[3] - last[3]);
        d->usb.starts = sat16u(now[4] - last[4]);
        for (k = 0; k < 5u; k++)
            last[k] = now[k];
    }
    {                                    /* SysEx lost on the way in (usb.c): since the last window */
        static uint16_t last[2];
        uint16_t now[2] = {usb_sx_lost[0], usb_sx_lost[1]};
        d->sx_lost[0] = (uint16_t)(now[0] - last[0]);
        d->sx_lost[1] = (uint16_t)(now[1] - last[1]);
        last[0] = now[0];
        last[1] = now[1];
    }
}
static void dbg_pass(void)               /* a main loop pass starts: the last one's length */
{
    uint32_t t = plat_us();
    if (dbg_pass_t0) {
        uint32_t dt = t - dbg_pass_t0;
        if (dt > dbg_pass_max)
            dbg_pass_max = dt;
        dbg_passes = sat16u(dbg_passes + 1u);
    }
    dbg_pass_t0 = t;
}

/* ---- PROVE (specs/debug.md, Proving the layout): the RAM layout's guards tried on purpose (and core 2's fallbacks,
 * core2.c's c2_prove), the debug page's 35 PROVE asking, 36 RECORDS answering. Its own record sits beside the crash
 * record and the boot guard, kept across restarts (.noinit). The band: the 256 B at _restart_band (app.ld),
 * patterned before every restart this build makes itself (the watchdog's here, a crash's in fm1_fault) and checked
 * at the next start, before the pool is set up, so whatever a restart writes there shows. ---- */
#define PROVE_MAGIC 0x50525645u          /* "PRVE" */
#define PROVE_VEC 0xFFFFFFFEu            /* the crash record the watchdog test writes (its vector: none of the 128) */
enum { PV_NONE, PV_WDT1, PV_WDT2, PV_MAIN, PV_IRQ };
typedef struct {
    uint32_t magic, step;                /* PV_*: the test under way */
    uint32_t band;                       /* 1 the band patterned before the restart; 2 checked at this start */
    uint32_t band_changed;               /* its bytes found changed at this start */
    uint32_t w_seen;                     /* 1: the first watchdog restart's start found what follows */
    uint32_t w_failed, w_pending;        /* the boot guard then */
    uint32_t w_wdt;                      /* the watchdog as the reset's reason */
    uint32_t w_crash;                    /* the test's crash record, kept */
    uint32_t w_band;                     /* the band's bytes changed by it (0xFFFF: not checked) */
} prove_t;
prove_t prove_rec __attribute__((section(".noinit")));   /* (not static, as fm1_crash: never folded) */
extern uint32_t _restart_band[];
#define BAND_WORDS 64u                   /* (256 B) */
static volatile uint8_t prove_irq;       /* the next audio interrupt overflows the interrupts' stack */
static volatile uint32_t prove_sink;

static uint32_t band_word(uint32_t i) { return 0xB5A50000u ^ (i * 0x9E3779B1u); }
static void prove_band_set(void)         /* before a restart this build makes (nothing writes the band till then) */
{
    uint32_t i;
    for (i = 0; i < BAND_WORDS; i++)
        _restart_band[i] = band_word(i);
    prove_rec.band = 1;
}
/* at every start, before the pool is set up: a fresh record at a power-on, else the band checked if it was set */
static void prove_start(int power_on)
{
    uint32_t i, n = 0;
    if (power_on || prove_rec.magic != PROVE_MAGIC) {
        uint32_t *w = (uint32_t *)&prove_rec;
        for (i = 0; i < sizeof prove_rec / 4u; i++)
            w[i] = 0;
        prove_rec.magic = PROVE_MAGIC;
        return;
    }
    if (prove_rec.band != 1u) {
        prove_rec.band = 0;
        return;
    }
    for (i = 0; i < BAND_WORDS; i++) {
        uint32_t d = _restart_band[i] ^ band_word(i);
        n += (d & 0xFFu ? 1u : 0u) + (d & 0xFF00u ? 1u : 0u) + (d & 0xFF0000u ? 1u : 0u) + (d >> 24 ? 1u : 0u);
    }
    prove_rec.band = 2;
    prove_rec.band_changed = n;
}
static void prove_screen(const char *a, const char *b, const char *c)
{
    lcd_sync();
    fm1_lcd_baud(4);
    lcd_fill(0, 0, 240, 240, RGB(0, 0, 120));
    draw_text_box(0, 60, 240, &FONT_S, a, C_WHITE, 1);
    draw_text_box(0, 100, 240, &FONT_S, b, C_WHITE, 1);
    draw_text_box(0, 130, 240, &FONT_S, c, C_WHITE, 1);
    lcd_sync();
}
static void prove_starve(uint32_t step)  /* the main loop stops feeding the watchdog: its restart (~8 s) */
{
    c2_hold_reset();                     /* (core 2 held before the reset: the watchdog's may not hold it; a restart
                                          * of ours, so no ban) */
    fm1_audio_stop();                    /* (the band left alone from here: the delay lines lie there) */
    prove_rec.step = step;
    prove_band_set();
    for (;;)
        prove_sink++;
}
/* the deepest call: a frame of ~140 B a level, until a stack check stops it (stackdepth.py's DELIBERATE) */
static void __attribute__((noinline)) prove_dive(uint32_t n)
{
    volatile uint32_t pad[32];
    pad[0] = n;
    pad[31] = prove_sink;
    prove_dive(n + 1u);
    prove_sink += pad[0] + pad[31];
}
static void prove_watchdog(void)         /* 35 PROVE 1: two watchdog restarts in a row, the second ending in USB RESCUE */
{
    uint32_t i, *w = (uint32_t *)&fm1_crash, count = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    for (i = 0; i < sizeof fm1_crash / 4u; i++)
        w[i] = 0;
    fm1_crash.magic = FM1_CRASH_MAGIC;   /* (a crash record of the test's own: is it readable after the restart?) */
    fm1_crash.count = count + 1u;
    fm1_crash.vec = PROVE_VEC;
    fm1_crash.uptime_ms = fm1_ms;
    bootguard.failed = 0;                /* (from here the boot guard counts: this start is not yet healthy, so */
    bootguard.pending = 1;               /* each restart counts as a failed start: the second, the rescue) */
    prove_rec.w_seen = 0;
    prove_screen("WATCHDOG TEST", "THE MAIN LOOP STOPS", "RESTART 1 OF 2: ~8 S");
    prove_starve(PV_WDT1);
}
static void prove_boot(void)             /* a start: the watchdog test's second step, after the first restart */
{
    char b[24];
    if (prove_rec.magic != PROVE_MAGIC || prove_rec.step != PV_WDT1)
        return;
    prove_rec.w_seen = 1;
    prove_rec.w_failed = bootguard.failed;
    prove_rec.w_pending = bootguard.pending;
    prove_rec.w_wdt = (uint32_t)(fm1_boot.p3_rst >> 2 & 1u);
    prove_rec.w_crash = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.vec == PROVE_VEC;
    prove_rec.w_band = prove_rec.band == 2u ? prove_rec.band_changed : 0xFFFFu;
    prove_screen("WATCHDOG TEST", prove_rec.w_crash ? "CRASH RECORD: KEPT" : "CRASH RECORD: LOST",
                 prove_rec.w_failed == 1u ? "BOOT GUARD: 1 FAILED" : "BOOT GUARD: WRONG");
    str_cpy(b, "RESET: ", sizeof b);
    str_cpy(b + 7, prove_rec.w_wdt ? "WATCHDOG" : "NOT THE WATCHDOG", sizeof b - 7u);
    draw_text_box(0, 160, 240, &FONT_S, b, C_WHITE, 1);
    draw_text_box(0, 190, 240, &FONT_S, prove_rec.w_band == 0u ? "BAND: KEPT" : prove_rec.w_band == 0xFFFFu ?
                  "BAND: NOT CHECKED" : "BAND: CHANGED", C_WHITE, 1);
    lcd_sync();
    {
        uint32_t t0 = fm1_ticks();
        while ((uint32_t)(fm1_ticks() - t0) < 5000u * 1000u * FM1_TICKS_PER_US)
            fm1_wdt_feed();
    }
    prove_screen("WATCHDOG TEST", "THE MAIN LOOP STOPS", "RESTART 2 OF 2: RESCUE");
    prove_starve(PV_WDT2);
}
static void prove_overflow(int irq)      /* 35 PROVE 2, 3: a stack run past its bottom on purpose */
{
    prove_rec.step = irq ? PV_IRQ : PV_MAIN;
    prove_screen("STACK TEST", irq ? "THE INTERRUPTS' STACK" : "THE MAIN LOOP'S STACK", "OVERFLOWS NOW");
    if (irq)
        prove_irq = 1;                   /* (the next audio half dives) */
    else
        prove_dive(0);
}
static void prove_records(void)          /* 36 RECORDS (specs/debug.md, RECORDS' bytes) */
{
    uint8_t r[120];
    uint32_t i;
    const uint32_t *cw = (const uint32_t *)&fm1_crash;
    const uint32_t ends[4] = {(uint32_t)(uintptr_t)_ustack_lo, (uint32_t)(uintptr_t)_ustack_top,
                              (uint32_t)(uintptr_t)_sstack_lo, (uint32_t)(uintptr_t)_sstack_top};
    memset(r, 0, sizeof r);
    r[0] = 3;                            /* the version */
    r[1] = fm1_guard_windows;
    r[2] = (uint8_t)prove_rec.step;
    r[3] = (uint8_t)(prove_rec.band == 2u);
    r[4] = (uint8_t)prove_rec.band_changed;
    r[5] = (uint8_t)(prove_rec.band_changed >> 8);
    r[6] = fm1_boot.p3_rst;
    r[7] = (uint8_t)bootguard.failed;
    r[8] = (uint8_t)bootguard.pending;
    r[9] = (uint8_t)prove_rec.w_seen;
    r[10] = (uint8_t)prove_rec.w_failed;
    r[11] = (uint8_t)prove_rec.w_pending;
    r[12] = (uint8_t)prove_rec.w_wdt;
    r[13] = (uint8_t)prove_rec.w_crash;
    r[14] = (uint8_t)prove_rec.w_band;
    r[15] = (uint8_t)(prove_rec.w_band >> 8);
    for (i = 0; i < 4u; i++)
        r[16 + i] = (uint8_t)(fm1_boot.rst_src >> (8u * i));
    for (i = 0; i < 72u; i++)            /* the crash record as kept (18 words, core 2's the last) */
        r[20 + i] = (uint8_t)(cw[i >> 2] >> (8u * (i & 3u)));
    for (i = 0; i < 16u; i++)            /* the stacks' ends */
        r[92 + i] = (uint8_t)(ends[i >> 2] >> (8u * (i & 3u)));
    for (i = 0; i < 12u; i++)            /* the pointers the windows were chosen by: usp, ssp, sp (version 3) */
        r[108 + i] = (uint8_t)(fm1_guard_sp[i >> 2] >> (8u * (i & 3u)));
    dbg_send(0x36, r, sizeof r);
}
typedef char prove_crash_len[sizeof(fm1_crash_t) == 72u ? 1 : -1];   /* (its 18 words sent) */
/* a PT/FM SysEx for PROVE (F0 7D 50 54 35 what F7): 1 taken. 4-6 only while core 2 takes the blocks (else nothing:
 * 37 says why); after one it stays off till the next start (core2.c) */
static int prove_sysex(const uint8_t *m, uint32_t n)
{
    if (n != 7u || m[4] != 0x35u || m[6] != 0xF7u)
        return 0;
    if (m[5] == 0u)
        prove_records();
    else if (m[5] == 1u)
        prove_watchdog();
    else if (m[5] == 2u || m[5] == 3u)
        prove_overflow(m[5] == 3u);
    else if (m[5] >= 4u && m[5] <= 6u && fm1_c1.state == FM1_C1_RUNS && c2_want && !c2_forced) {
        c2_prove_halves = 0;             /* (core 2's next block: faults, misread, stuck; dropped when the next half
                                          * hands none over: core2.c) */
        c2_prove = m[5];
    }
    return 1;
}

#include "bench_core.c"                  /* the bench's THE CORE, plat_ticks, plat_cache_cold */
#endif

void fm1_alnk0_irq(void)                 /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    uint32_t t0 = fm1_ticks();
#ifdef PTFM_DEBUG
    if (prove_irq)                       /* (PROVE: the interrupts' stack run past its bottom) */
        prove_dive(0);
#endif
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half, dt, late;
        int32_t *o;
        c2_half_start();                 /* first: core 2 done with the half before's blocks (core2.c) */
        half = fm1_audio_free_half();
        o = &abuf[half * HALF_WORDS];
        memset(o, 0, HALF_WORDS * sizeof *o);   /* silence first: what is not made in time stays silent (a word at a
                                                 * time: 5 us, a loop of stores took 13) */
        if (!audio_mute) {
            uint8_t par = (uint8_t)(half_par ^ 1u);
            half_at[par] = t0 + HALF_TICKS;   /* (the MIDI it sends goes out as its frames are heard) */
            RING_PUBLISH();
            half_par = par;
            out_half = o;
            out_gain = (out_master * out_line) >> 12;
            out_mono_half = out_mono;
            usb_gain_half = usb_gain;
            usb_audio_start();           /* (USB audio: the ring primed, or the resampler steered) */
            in_audio = 1;
            app_audio(0, (int)HALF_FRAMES);  /* each block into the half as it is made (out_block) */
            in_audio = 0;
        }
        fm1_audio_ack_half();
        dt = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        cpu_last = dt;
        cpu_sum += dt;
        cpu_halves++;
        if (dt > cpu_max)
            cpu_max = dt;
        late = fm1_audio_free_half() != half;   /* the DMA moved on while we rendered */
        if (late) {
            cpu_late++;
            c2_late1 = 1;                /* (core 2 still on its last block at the next half: this dropout, counted) */
        }
#ifdef PTFM_DEBUG
        dbg_isr_sum += dt;
        if (dt > dbg_isr_max)
            dbg_isr_max = sat16u(dt);
        dbg_halves = sat16u(dbg_halves + 1u);
        if (late)
            dbg_late = sat16u(dbg_late + 1u);
#endif
    }
}
extern void isr_alnk0(void);

static void audio_silence(void)          /* before the flash stops: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ---- the update path: ota.c's hooks ---- */
static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FW_ID)
/* The update path's buffers (2,688 B): lcd_buf's bytes 1024 on (above), used only while an M-UPGRADE
 * message is handled or an update runs (the app stands still then, and the screen is the update's). */
#define OTA_BUFS ((uint8_t *)lcd_buf + 1024u)
#include "ota.c"
typedef char ota_bufs_fit[1024u + OTA_FRAME_MAX + 0x400u + 1024u <= sizeof lcd_buf ? 1 : -1];
typedef char ota_canvas_fit[CV_MAX * 2u <= 1024u ? 1 : -1];
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active)
        recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    f = irq_save();
    c2_quiet();                          /* (core 2 done writing the halves first) */
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    if (!ota_in_area(off, n))
        return -8;
    while (n) {                          /* (fl_write's pages, each once core 2 is idle: its voice job runs from the
                                          * flash, which a program switches off) */
        uint32_t chunk = 256u - (off & 0xFFu), took, f;
        int rc;
        if (chunk > n)
            chunk = n;
        f = irq_save();
        c2_quiet();
        rc = FL_FAR(fl_prog_ram)(off, s, chunk, &took);
        irq_restore(f);
        if (rc)
            return rc;
        off += chunk;
        s += chunk;
        n -= chunk;
    }
    return 0;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    char b[24];
    if (recovery_active)
        return;                          /* keep polled USB alive during OTA */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_WHITE, 1);
        return;
    }
    str_cpy(b, "FAILED  -", sizeof b);
    fmt_int(b + 9, -code);
    draw_text_box(0, 124, 240, &FONT_S, b, C_WHITE, 1);
    fm1_delay_ms(1500);
}
/* Before every reset: no DMA may write RAM through it. A core reset (the update's) may leave the
 * peripherals running while the next boot unpacks the update loader over the variables (MIDI IN's
 * ring among them), so the jack's DMA stops here; the sound's too (it would play the loader as noise).
 * USB is detached by the paths that had it attached. And core 2 is held (fm1_cpu1_halt) first, on every
 * path to a reset: an update's, a crash's, the chip's USB update mode (the rescue's too), the debug build's
 * watchdog test. Ours from a run that may have started it go through c2_hold_reset, so the next start does not ban
 * it; a crash's does not (core2.c). */
static void dma_quiet(void)
{
    fm1_audio_stop();
    fm1_uart1_off();
}
static void ota_commit(const uint8_t *parm)
{
    c2_hold_reset();                     /* (the rescue never started core 2: harmless there) */
    bootguard_clear(&bootguard);         /* intentional update reset */
    if (!recovery_active)
        dma_quiet();                     /* (the rescue starts neither: its path stays as it was) */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);              /* record into RAM, core reset (fm1_sys.h) */
}
#include "recovery.c"

/* ---- the clocks ---- */
static volatile uint32_t us_seq, us_base, us_tbase;   /* plat_us: us_base at TIMER4 tick us_tbase */

/* ---- the LEDs: plat_led stores a level, the 1 kHz tick turns the levels into the scan's two layers
 * (lit and the dim glow: fm1_input.h) ---- */
static volatile uint8_t led_level[LED_COUNT];
static uint8_t led_col[LED_COUNT], led_bit[LED_COUNT];

static void leds_init(void)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] >= 0 && FM1_KEYMAP[r][p] < LED_COUNT) {
                led_col[FM1_KEYMAP[r][p]] = (uint8_t)p;
                led_bit[FM1_KEYMAP[r][p]] = (uint8_t)r;
            }
    led_col[LED_PLAY_G] = 8;             /* PLAY's green LED has no key: column 8, bit 1 */
    led_bit[LED_PLAY_G] = 1;
}

#define LED_LIT_FROM 96u                 /* a level this high or more is lit; under it, above 0, the glow */
static void leds_apply(void)             /* TIMER5, 1 kHz */
{
    uint8_t lit[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0};
    uint32_t i;
    for (i = 0; i < LED_COUNT; i++) {
        uint32_t v = led_level[i];
        if (v >= LED_LIT_FROM)
            lit[led_col[i]] |= (uint8_t)(1u << led_bit[i]);
        else if (v)
            dim[led_col[i]] |= (uint8_t)(1u << led_bit[i]);
    }
    for (i = 0; i < FM1_NCOL; i++) {
        fm1_led[i] = lit[i];
        fm1_led_dim[i] = dim[i];
    }
}

static volatile uint8_t ticking;         /* the app runs: the 1 kHz tick applies the LEDs */
static void (*volatile fast_fn)(const plat_input_t *in, uint32_t ms);   /* plat_fast's (the controller inside PT/FM) */
static void fast_tick(void);
static uint16_t usb_frame_seen;          /* the host's frame number as the tick last saw it, and when it moved */
static volatile uint32_t usb_frame_at;

void fm1_timer5_irq(void)
{
    static uint32_t sub, last, acc, uacc;
    uint32_t t0 = fm1_ticks(), el;
    fm1_timer5_ack();
    fm1_input_tick();
    el = t0 - last;                      /* milliseconds and microseconds from the 24 MHz TIMER4 */
    last = t0;
    acc += el;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    uacc += el;
    us_seq++;
    fm1_csync();                         /* (the chip's sync: core 2 reads them too, plat_c2_us) */
    us_base += uacc / FM1_TICKS_PER_US;
    uacc %= FM1_TICKS_PER_US;
    us_tbase = t0 - uacc;
    fm1_csync();
    us_seq++;
    if (sub == 0u && fast_fn)
        fast_tick();                     /* (its messages and LEDs go in this tick) */
    if (sub == 0u && ticking)
        leds_apply();
    if (sub % 5u == 0u) {                /* 2 kHz: USB-MIDI and the jack */
        midi_out_move();                 /* (before the poll: they leave in this tick) */
        usb_poll();
        if (usb.frame != usb_frame_seen) {   /* (plat_usb_power: the host's frames still coming) */
            usb_frame_seen = usb.frame;
            usb_frame_at = fm1_ms;
        }
        if (ticking)
            uart_midi_poll();
        midi_in_move();
    }
    if (++sub == 10u)
        sub = 0;
}
extern void isr_timer5(void);

/* ---- the crash screen ---- */
static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void crash_wait_reboot(void)
{
    uint32_t t0 = fm1_ticks();
    fm1_cpu1_halt();                     /* (not c2_hold_reset: core 2 running at a crash, the next start bans it) */
    dma_quiet();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void fm1_fault(const fm1_crash_t *c)   /* (core 1's: core 2's park in fm1_fault_c) */
{
    char b[12];
    fm1_cpu1_halt();                     /* (the record says whether it ran: fm1_fault_c) */
    fm1_audio_stop();
#ifdef PTFM_DEBUG
    prove_band_set();                    /* (the restart this crash makes: what it writes into the band shows) */
#endif
    lcd_sync();
    fm1_lcd_baud(4);                     /* a readable screen whatever the clock was */
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "PT/FM CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    if (c->stack != FM1_STACK_NONE)      /* (the stack check, or the guard band's write limit, on an overflow) */
        draw_text_box(0, 150, 240, &FONT_S, c->stack == FM1_STACK_IRQ ? "STACK: INTERRUPTS" : "STACK: MAIN LOOP",
                      C_WHITE, 1);
    crash_wait_reboot();
}

/* ---- plat.h ---- */
uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_us(void)
{
    uint32_t s, b, t;
    do {
        s = us_seq;
        RING_PUBLISH();
        b = us_base;
        t = us_tbase;
        RING_PUBLISH();
    } while (s != us_seq || (s & 1u));
    return b + (fm1_ticks() - t) / FM1_TICKS_PER_US;
}
/* the same in core 2's room (plat.h), for its code: TIMER4 read in place (fm1_ticks_inline: no call out of the room),
 * TIMER5's words read between the chip's syncs (it writes them on the other core) */
PLAT_C2 uint32_t plat_c2_us(void)
{
    uint32_t s, b, t;
    do {
        s = us_seq;
        fm1_csync();
        b = us_base;
        t = us_tbase;
        fm1_csync();
    } while (s != us_seq || (s & 1u));
    return b + (fm1_ticks_inline() - t) / FM1_TICKS_PER_US;
}

static volatile int32_t master10 = 1023; /* MASTER, 0..1023: the pot, smoothed, or a MIDI knob learned onto it */
static int32_t pot10 = -1;               /* the pot itself, smoothed (the main loop reads the ADC) */
static int32_t pot_at_midi = -1;         /* the pot when a MIDI knob took MASTER over; -1: the pot rules */


typedef char knobs_match[FM1_NENC == K_COUNT ? 1 : -1];   /* (the scan's encoders are plat.h's knobs) */
void plat_input(plat_input_t *in)
{
    uint32_t f;
    in->buttons = fm1_in.buttons;
    in->notes = fm1_in.notes;
    f = irq_save();                      /* the scan adds to the steps in TIMER5 */
    fm1_input_take(in->knob, in->knob_pre);   /* (and which of them came before a press) */
    irq_restore(f);
    in->master = (uint16_t)master10;
}

static void fast_tick(void)              /* TIMER5, 1 kHz: plat_fast's fn with the input now (the scan runs in this
                                          * same interrupt: the knobs' clicks taken without a lock) */
{
    plat_input_t in;
    in.buttons = fm1_in.buttons;
    in.notes = fm1_in.notes;
    fm1_input_take(in.knob, in.knob_pre);
    in.master = (uint16_t)master10;
    fast_fn(&in, fm1_ms);
}
int plat_fast(void (*fn)(const plat_input_t *in, uint32_t ms))
{
    fast_fn = fn;
    return 1;
}
int plat_usb_up(void) { return *(volatile uint8_t *)&usb.config != 0; }
/* configured, and the frame number usb_poll keeps (every 10 ms while configured) moved in the last half second */
int plat_usb_power(void) { return plat_usb_up() && fm1_ms - usb_frame_at < 500u; }

uint32_t plat_notes(void) { return fm1_in.notes; }

void plat_led(int id, uint8_t level)
{
    if (id >= 0 && id < LED_COUNT)
        led_level[id] = level;
}

/* The panel takes RGB565 big-endian: the rows are swapped into one half of lcd_buf, a few rows at a
 * time, while the other half's DMA runs. */
static uint8_t lcd_swap_i;
static volatile uint8_t screen_lost;

void plat_lcd_rows(int y, int h, const uint16_t *px)
{
    uint32_t r, i;
    if (y < 0 || h <= 0 || y >= 240)
        return;
    if (y + h > 240)
        h = 240 - y;
    lcd_window(0, (uint32_t)y, 239u, (uint32_t)(y + h - 1));
    for (r = 0; r < (uint32_t)h; r += LCD_CHUNK_ROWS) {
        uint32_t k = (uint32_t)h - r < LCD_CHUNK_ROWS ? (uint32_t)h - r : LCD_CHUNK_ROWS, n = k * 240u;
        uint16_t *d = lcd_buf + lcd_swap_i * (LCD_CHUNK_ROWS * 240u);
        const uint16_t *s = px + r * 240u;
        for (i = 0; i < n; i++)          /* (the other buffer's transfer may still run) */
            d[i] = swap16(s[i]);
        lcd_data(d, n * 2u);             /* waits for the last transfer, starts this one */
        lcd_swap_i ^= 1u;
    }
}

int plat_screen_lost(void)
{
    if (!screen_lost)
        return 0;
    screen_lost = 0;
    return 1;
}

void plat_cpu(plat_cpu_t *c, int reset)
{
    c->last_us = cpu_last;
    c->max_us = cpu_max;
    c->late = cpu_late;
    c->half_us = HALF_US;
    c->sum_us = cpu_sum;                 /* (read in either order: a half between them is one half's error) */
    c->halves = cpu_halves;
    c2_cpu(c, reset);                    /* (core 2's; its late halves added to these) */
    if (reset) {
        cpu_max = 0;
        cpu_late = 0;
    }
}

void plat_output(int mono, int line_gain_db)   /* (from the next half: out_mono_half) */
{
    out_line = out_line_gain_q12(line_gain_db);
    out_mono = mono ? 1u : 0u;
}

const uint8_t *plat_flash_ptr(uint32_t addr)
{
    return fl_plain && addr >= FL_USER1_LO && addr < 0x00100000u ? (const uint8_t *)FL_XIP(addr) : 0;
}

int plat_flash_read(uint32_t addr, void *dst, uint32_t n)
{
    const uint8_t *p = plat_flash_ptr(addr);
    if (!n)
        return 0;
    if (addr >= 0x00100000u || n > 0x00100000u - addr)
        return -1;
#ifdef PTFM_DEBUG
    uint32_t dbg_t0 = fm1_ticks();
#define DBG_FL() if (in_audio) { dbg_fl_n = sat16u(dbg_fl_n + 1u); \
                                 dbg_fl_us = sat16u(dbg_fl_us + (fm1_ticks() - dbg_t0) / FM1_TICKS_PER_US); }
#else
#define DBG_FL()
#endif
    if (p && plat_flash_ptr(addr + n - 1u)) {
        memcpy(dst, p, n);
        DBG_FL();
        return 0;
    }
    if (in_audio) {                      /* (none in the sound: the user area and the built-ins read in place) */
        uint8_t *d = dst;
        /* interrupts off for the rest of the audio interrupt: st_read's sti would let interrupts in while
         * ALNK0's own runs (it is the interrupt wrapper that restores them, at its return); core 2 idle first, as for
         * every direct read */
        irq_save();
        c2_quiet();
        while (n) {
            uint32_t k = n > 256u ? 256u : n;
            if (FL_FAR(fl_read_ram)(addr, d, k))
                return -1;
            addr += k;
            d += k;
            n -= k;
        }
        DBG_FL();
        return 0;
    }
#undef DBG_FL
    return st_read(addr, dst, n) ? -1 : 0;
}

void plat_idle(void)                     /* a long walk or compaction in the main loop: not a hang */
{
    fm1_wdt_feed();
}

/* the spare zone's room past its own array (app.ld: the data region's last object, .spare): up to the region's top */
uint32_t plat_spare_room(void)
{
    extern char _spare_end[], _data_top[];
    return (uint32_t)(_data_top - _spare_end) & ~3u;
}

/* the RAM's parts for STATUS' RAM MAP (app.ld): 512 KB, RAMTEXT's 4 KB, the variables .data .. .bss, the LCD's
 * transfer buffers (in the pool) */
void plat_ram(plat_ram_t *r)
{
    extern uint32_t _data_start[], _bss_end[];
    r->total = 512u * 1024u;
    r->ram_text = 4096u;
    r->variables = (uint32_t)((uint8_t *)_bss_end - (uint8_t *)_data_start);
    r->screen = (uint32_t)sizeof lcd_buf;
}

/* The audio interrupt held off while the store changes what the audio context reads (nestable). In the
 * audio context itself nothing that reads the store can interrupt it: a no-op there. */
static uint32_t lock_depth;
uint32_t plat_lock(void)
{
    if (in_audio)
        return 0x80000000u;
    irq_save();
    c2_quiet();                          /* (core 2 done: its voice job reads what the store changes, the patches and
                                          * records; and the debug build's bench runs the effects' code on core 1) */
    return lock_depth++;
}

void plat_unlock(uint32_t was)
{
    if (was & 0x80000000u)
        return;
    lock_depth = was;
    if (!was)
        irq_restore(0);
}

int plat_flash_erase(uint32_t addr)
{
    uint32_t took, f;
    int rc;
    if (!flash_ok || (addr & 0xFFFu) || addr == 0x000E0000u || !FL_RANGE_OK(addr, 0x1000u) ||
        FL_IN(addr, 0x1000u, 0x000E0000u, 0x000E5000u))
        return -1;                       /* (the staging is the updater's alone) */
    fm1_wdt_feed();
    f = irq_save();
    c2_quiet();                          /* (core 2 done writing the halves first, and idle in its loop in RAM: its
                                          * voice job runs from the flash, which the erase switches off) */
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(addr, &took);
    irq_restore(f);
    return rc ? -1 : 0;
}

int plat_flash_prog(uint32_t addr, const void *src, uint32_t n)
{
    /* the source in RAM, as the flash is off while it programs: lcd_buf's last 256 bytes (main loop only) */
    uint8_t *page = (uint8_t *)lcd_buf + sizeof lcd_buf - 256u;
    uint32_t took, f;
    int rc;
    if (!flash_ok || !n || n > 256u || (addr & 0xFFu) + n > 256u || !FL_RANGE_OK(addr, n) ||
        FL_IN(addr, n, 0x000E0000u, 0x000E5000u))
        return -1;
    lcd_sync();                          /* the app's last rows have gone out of lcd_buf */
    memmove(page, src, n);
    fm1_wdt_feed();
    f = irq_save();
    c2_quiet();                          /* (as plat_flash_erase) */
    audio_silence();
    rc = FL_FAR(fl_prog_ram)(addr, page, n, &took);
    irq_restore(f);
    return rc ? -1 : 0;
}

/* the flash offset where the firmware ends (its .data's load address + size) */
uint32_t plat_fw_end(void)
{
    extern uint32_t _data_load[], _data_start[], _data_end[];
    uint32_t end = (uint32_t)_data_load + (uint32_t)((uint8_t *)_data_end - (uint8_t *)_data_start);
    return end - FL_XIP(0x4000u) + 0x4000u;
}

int plat_sysex_send(const uint8_t *msg, uint32_t n)
{
    if (!*(volatile uint8_t *)&usb.config || n < 2u || n > OTA_FRAME_MAX)
        return -1;
    return ota_wire_send(msg, n) ? -1 : 0;
}

static volatile int32_t battery_pct = -1;
int plat_battery(void) { return battery_pct; }

int plat_midi_rx(plat_midi_t *e)
{
    uint32_t pkt, now = fm1_ms, st;
    uint16_t ts;
    if (mrx_r == mrx_w)
        return 0;
    RING_PUBLISH();
    pkt = mrx_pkt[mrx_r % MRX];
    ts = mrx_ts[mrx_r % MRX];
    RING_PUBLISH();
    mrx_r++;
    st = (pkt >> 8) & 0xFFu;
    e->m[0] = (uint8_t)st;
    e->m[1] = st >= 0xF8u ? 0 : (uint8_t)(pkt >> 16);
    e->m[2] = st >= 0xF8u || (st & 0xE0u) == 0xC0u ? 0 : (uint8_t)(pkt >> 24);
    e->src = (pkt & 0xF0u) ? PLAT_MIDI_JACK : PLAT_MIDI_USB;   /* (the jack's: cable 1, midi_uart.c) */
    e->ms = now - (uint16_t)((uint16_t)now - ts);
    return 1;
}

/* (two queues, each its own function: neither entry point calls the other, so the interrupts' tree holds no
 * recursion for the build's stack check) */
static int midi_tx_audio(const uint8_t m[3], int frame)   /* the audio context's ring */
{
    uint32_t cin = midi_cin(m[0]), f;
    if (!*(volatile uint8_t *)&usb.config || mta_w - mta_r >= MTA || m[0] < 0x80u || !cin)
        return -1;                       /* (other system common: not sent) */
    f = frame < 0 ? 0u : frame > 255 ? 255u : (uint32_t)frame;
    mta_q[mta_w % MTA] = (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 |
                         ((uint32_t)half_par << 7 | f >> 1) << 24;
    RING_PUBLISH();
    mta_w++;
    return 0;
}

static int midi_tx_main(const uint8_t m[3])   /* the main loop's ring */
{
    uint32_t cin = midi_cin(m[0]);
    if (!*(volatile uint8_t *)&usb.config || mtx_w - mtx_r >= MTX || m[0] < 0x80u || !cin)
        return -1;                       /* (other system common: not sent) */
    mtx_q[mtx_w % MTX] = cin | (uint32_t)m[0] << 8 |
                         (cin == 0xFu ? 0u : (uint32_t)m[1] << 16 | (uint32_t)m[2] << 24);
    RING_PUBLISH();
    mtx_w++;
    return 0;
}

int plat_midi_tx_at(const uint8_t m[3], int frame)
{
    return in_audio ? midi_tx_audio(m, frame) : midi_tx_main(m);
}

int plat_midi_tx(const uint8_t m[3])
{
    return in_audio ? midi_tx_audio(m, 0) : midi_tx_main(m);   /* (the audio context's: as its half starts to be heard) */
}

void plat_master_set(uint16_t v)
{
    int32_t k = v > 1023u ? 1023 : (int32_t)v;
    pot_at_midi = pot10 < 0 ? 0 : pot10;
    master10 = k;
    out_master = out_master_q12(k);
}
uint32_t plat_usb_resets(void) { return *(volatile uint32_t *)&usb.resets; }   /* (TIMER5 counts them) */

void plat_fault(const char *why)
{
    fm1_audio_stop();
    lcd_sync();
    fm1_lcd_baud(4);
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "PT/FM STOPPED", C_WHITE, 1);
    draw_text_box(0, 100, 240, &FONT_T, why ? why : "", C_WHITE, 1);
    crash_wait_reboot();
    for (;;)
        ;
}

/* ---- the main loop ---- */

/* MASTER: read every 2 ms, smoothed (~16 ms), squared; the battery every 2 s */
static void adc_poll(void)
{
    static uint32_t t_adc, t_batt;
    static int32_t knob = -1;
    uint32_t now = fm1_ms;
    int32_t a;
    if (now - t_adc >= 2u) {
        t_adc = now;
        a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) {
            knob = knob < 0 ? a * 16 : knob + (a * 16 - knob) / 8;
            pot10 = knob / 16;
            if (pot_at_midi >= 0 && (pot10 > pot_at_midi + 8 || pot10 < pot_at_midi - 8))
                pot_at_midi = -1;        /* MASTER itself moved: it rules again */
            if (pot_at_midi < 0) {
                master10 = pot10;
                out_master = out_master_q12(pot10);
            }
        }
    }
    if (now - t_batt >= 2000u || !t_batt) {
        t_batt = now | 1u;
        a = fm1_adc_read(FM1_ADC_BATT);  /* 501-621 as 0-100 %: the bars from 513 / 542 / 582; the voltage is not known */
        if (a >= 0)
            battery_pct = a <= 501 ? 0 : a >= 621 ? 100 : (a - 501) * 100 / 120;
    }
}

/* A received SysEx: PT/FM's (7D 50 54 ...) to the app, whole (F0 .. F7); else the updater's */
/* (a PT/FM message in lcd_buf's first OTA_FRAME_MAX + 2 bytes; plat_flash_prog borrows its last 256:
 * app_sysex may program; the update path's handshake decodes into its bytes 1024 on) */
static void sysex_route(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (n >= 3u && p[0] == 0x7Du && p[1] == 0x50u && p[2] == 0x54u) {
        uint8_t *m = (uint8_t *)lcd_buf;
        lcd_sync();                      /* the app's last rows have gone out of lcd_buf */
        m[0] = 0xF0u;
        memcpy(m + 1, p, n);
        m[n + 1u] = 0xF7u;
        ota_frame_done();
#ifdef PTFM_DEBUG
        if (prove_sysex(m, n + 2u))      /* 35 PROVE: the device's own (specs/debug.md) */
            return;
#endif
        app_sysex(m, n + 2u);
        return;
    }
    lcd_sync();                          /* (its buffers are lcd_buf's) */
    ota_service();                       /* the M-UPGRADE handshake (it takes any other frame) */
}
typedef char sysex_fits[OTA_FRAME_MAX + 2u <= 1024u ? 1 : -1];

static void enter_uboot_now(const char *why)
{
    c2_hold_reset();
    dma_quiet();
    lcd_sync();
    fm1_lcd_baud(4);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;               /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

static void ptfm_main(void)
{
    uint32_t healthy_since, dac_on = 0;
#ifdef PTFM_DEBUG
    dbg_paint();                         /* (interrupts are still off) */
#endif
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part */
        irq_restore(f);
    }
    if (flash_ok)
        ota_boot_cleanup();              /* staging area left by an update */
    lcd_init();
#ifdef PTFM_DEBUG
    prove_boot();                        /* (PROVE's watchdog test: its second restart, after the first's report) */
#endif
    if (flash_ok) {
        fl_plain_window_init();          /* the user area reads plainly over XIP */
        fl_plain = 1;
    }
    fm1_input_init();
    fm1_adc_init();
    uart_midi_init();                    /* the MIDI IN jack (before TIMER5: PORTH) */
    leds_init();
    {                                    /* MASTER as it stands, before the first sound */
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) {
            master10 = a;
            out_master = out_master_q12(a);
        }
    }
    app_init();                          /* the store, the player, the first screen */
    app_audio_out(out_block);            /* the sound into the DMA's half a block at a time */
    app_audio_usb(usb_block);            /* and the mix to USB audio */
    audio_silence();
    fm1_audio_init_muted(abuf, HALF_WORDS, isr_alnk0, 3);   /* (PC6, the data line, low until the clocks have run: a softer click) */
    usb_start();
    ticking = 1;
    fm1_timer5_start(isr_timer5, 4);     /* above ALNK0 (3) */
    fm1_guard_lock_top();
    fm1_irq_enable_all();

    healthy_since = fm1_ms;
    for (;;) {
        uint32_t now = fm1_ms;
#ifdef PTFM_DEBUG
        dbg_pass();
#endif
        if (!dac_on && now - healthy_since >= 150u) {   /* 150 ms of silence through the codec, then its sound */
            fm1_audio_unmute();
            dac_on = 1;
        }
        c2_poll(now - healthy_since);    /* core 2 started 2 s in, once SECOND CORE is on (core2.c) */
        fm1_wdt_feed();
        usb_retry(now);
        if ((uint32_t)(now - healthy_since) >= 30000u && bootguard.pending)
            bootguard_clear(&bootguard); /* 30 s up: this boot did not fail */
        adc_poll();
        usb_sx_poll();                   /* a SysEx cut short (the host stopped reading): its end out */
        sysex_route();
        /* the installer asks for an update: it runs once a save being written has finished and a seeding
         * under way has ended (or timed out, 10 s), so neither is cut halfway; the app runs on meanwhile */
        if (usb.ota_req && !store_saving() && !seed_active()) {
            usb.ota_req = 0;
            audio_mute = 1;
            fm1_delay_ms(12);            /* two halves of silence before the flash work */
            lcd_sync();
            if (flash_ok)
                ota_session();           /* returns only if nothing was committed */
            audio_mute = 0;
            screen_lost = 1;
        }
        if (usb.uboot_req)               /* SysEx F0 22 24 35 7D F7 from the host */
            enter_uboot_now("UBOOT (USB)");
        app_poll();
    }
}

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
extern uint32_t _c2_start[], _c2_end[], _c2_load[];

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, boot_mode, power_on;
    fm1_cpu1_halt();                     /* first: a core 2 a reset did not hold spins in its loop, which the room's
                                          * copy below writes over */
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    power_on = (p3 & 1u) && !(p3 & (4u | 0x40u));   /* a power-on (not a watchdog or soft reset) */
    if (power_on)
        bootguard_clear(&bootguard);     /* (no failed boot to count) */
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
#ifdef PTFM_DEBUG
    prove_start((int)power_on);          /* (before the pool is set up: the band as the restart left it) */
#endif
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                         /* flash driver code that must run from RAM */
    for (s = _c2_load, d = _c2_start; d < _c2_end; s++, d++)
        *d = *s;                         /* core 2's room: a block's job's code and tables, its loop and park
                                          * (app.ld's C2RAM) */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    c2_boot((int)power_on);              /* (core 2 left off after a crash or the watchdog with it running: core2.c) */
    if (boot_mode == BOOT_RECOVERY || recovery_key())   /* recovery_key scans the panel (50 ms) */
        recovery_main();
    ptfm_main();
    for (;;)
        ;
}
