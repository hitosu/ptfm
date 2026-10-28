/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The emulator's platform: fw/core/plat.h for a browser. The app (fw/core) is compiled to WebAssembly
 * with this file and runs in an AudioWorklet (worklet.js); the worklet asks for audio, and making it
 * runs the device's time, as on the FM-1:
 *   - every 256 frames the audio interrupt's work, app_audio;
 *   - every millisecond the main loop's pass, app_poll, when the main loop is free: a screen transfer
 *     keeps it busy as long as it takes on the FM-1 (60 MHz SPI: a full frame ~30.2 ms), and so do the
 *     flash's erases (17.1 ms) and programs (~1.9 ms).
 * The page sends the panel's input and SysEx in, and reads back the screen, the LEDs, the SysEx the
 * device sends and the flash (to keep it in the browser's storage), the user area's records (DOWNLOAD DISK) and
 * USB audio's frames while it records (RECORD). The sound goes through the FM-1's
 * own last stage (fw/device/out.h: MASTER squared, LINE GAIN, mono, the soft ceiling), as the codec's
 * 24-bit words. CPU figures are the PC's: app_audio timed with the page's clock (web_clock_us), its
 * time per half of the device's.
 *
 * Built natively too (WEB_NATIVE: fw/tests/web_native.c): the same results from the same C. */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "app.h"
#include "store.h"
#include "ui_core.h"
#include "../fw/device/out.h"

#ifdef WEB_NATIVE
#define WEB_EXPORT(name)
double web_clock_us(void);               /* (the test's fake clock) */
#else
#define WEB_EXPORT(name) __attribute__((export_name(name)))
__attribute__((import_module("env"), import_name("web_clock_us"))) double web_clock_us(void);
#endif

#ifndef WEB_FW_END
#define WEB_FW_END 0x50000u              /* where the firmware's image ends (the build passes the device's) */
#endif

#define HALF_US ((uint32_t)((uint64_t)PLAT_HALF * 1000000u / PLAT_RATE))   /* 5804 */

/* ---------------------------------------------------------------- time --- */
static uint64_t web_us;                  /* the device's time */
static uint64_t web_main_us;             /* the main loop's: ahead while it draws or writes the flash */
static uint32_t web_in_main;             /* the main loop is running (its time for plat_us) */
static uint32_t web_ms_now;              /* milliseconds since power-on */
static uint64_t web_frames;              /* frames made since power-on */

uint32_t plat_ms(void) { return web_ms_now; }
uint32_t plat_us(void) { return (uint32_t)(web_in_main ? web_main_us : web_us); }

static void web_busy(uint32_t us)        /* the main loop is taken this long (drawing, the flash) */
{
    if (web_in_main)
        web_main_us += us;
}

/* --------------------------------------------------------------- input --- */
static uint32_t web_buttons, web_notes;
static int32_t web_knobs[K_COUNT];
static uint16_t web_master = 512;

static int32_t web_knobs_pre[K_COUNT];   /* the clicks there were at the first press since the last take */
static uint32_t web_pre_on;
static void web_pressed(void)            /* a button or key down: the clicks so far came before it */
{
    int k;
    if (web_pre_on)
        return;
    for (k = 0; k < K_COUNT; k++)
        web_knobs_pre[k] = web_knobs[k];
    web_pre_on = 1;
}
static int16_t web_clamp16(int32_t s) { return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }

void plat_input(plat_input_t *in)
{
    int k;
    in->buttons = web_buttons;
    in->notes = web_notes;
    for (k = 0; k < K_COUNT; k++) {
        in->knob[k] = web_clamp16(web_knobs[k]);
        in->knob_pre[k] = web_clamp16(web_pre_on ? web_knobs_pre[k] : web_knobs[k]);
        web_knobs[k] = 0;
    }
    web_pre_on = 0;
    in->master = web_master;
}

uint32_t plat_notes(void) { return web_notes; }

/* ---------------------------------------------------------------- LEDs --- */
static uint8_t web_leds[LED_COUNT];

void plat_led(int id, uint8_t level)
{
    if (id >= 0 && id < LED_COUNT)
        web_leds[id] = level;
}

/* -------------------------------------------------------------- screen --- */
static uint16_t web_fb[PLAT_W * PLAT_H]; /* the panel's memory, RGB565 */
static uint32_t web_fb_writes;           /* rows sent since power-on */
static int32_t web_dirty_y0 = PLAT_H, web_dirty_y1;   /* the rows changed since the page last looked */

void plat_lcd_rows(int y, int h, const uint16_t *px)
{
    uint32_t bytes;
    if (y < 0) {
        px -= y * PLAT_W;
        h += y;
        y = 0;
    }
    if (y + h > PLAT_H)
        h = PLAT_H - y;
    if (h <= 0)
        return;
    memcpy(web_fb + y * PLAT_W, px, (uint32_t)h * PLAT_W * sizeof *px);
    if (y < web_dirty_y0)
        web_dirty_y0 = y;
    if (y + h > web_dirty_y1)
        web_dirty_y1 = y + h;
    web_fb_writes++;
    /* the SPI's bits at 60 MHz (133 ns a byte) and the CPU's drawing (~129 ns a byte): a full frame,
     * 115,200 B, took 30.2 ms on the FM-1 */
    bytes = (uint32_t)h * PLAT_W * 2u;
    web_busy(bytes * 262u / 1000u);
}

int plat_screen_lost(void) { return 0; } /* (no screen of the platform's own: no updates here) */

/* --------------------------------------------------------------- audio --- */
#define WEB_RING 4096u                   /* frames made, waiting for the worklet (stereo, 24-bit words) */
static int32_t web_ring[WEB_RING * 2u];
static uint32_t web_ring_w, web_ring_r;
static int16_t web_half[PLAT_HALF * 2];
static int32_t web_conv[PLAT_HALF * 2];
static int32_t web_out[1024 * 2];        /* web_render's frames for the page */
static plat_cpu_t web_cpu = { 0, 0, 0, HALF_US };
static int web_mono, web_line_db;        /* SETTINGS' OUTPUT and LINE GAIN */

void plat_output(int mono, int line_gain_db)
{
    web_mono = mono;
    web_line_db = line_gain_db;
}
/* USB audio, for the page's RECORD (specs/emulator.md): what the FM-1 sends a computer (specs/connections.md#usb-audio),
 * as the device's usb_value (fw/device/main.c): the mix after the effects, the clicks left out, times USB GAIN in
 * MASTER's place, always stereo, then the soft ceiling, as 16-bit values; at the device's own rate (no resampler:
 * the page's file is the computer). Made only while the page records (web_rec), taken after every web_render */
#define WEB_REC 2048u                    /* frames (web_render makes at most 1,024 a call) */
static int16_t web_rec_buf[WEB_REC * 2u];
static uint32_t web_rec_n;
static int32_t web_usb_gain = 5157;      /* USB GAIN (Q12; its 0 dB, out.h's OUT_USB_REF, till the settings come) */

void plat_usb_gain(int db) { web_usb_gain = out_usb_gain_q12(db); }

static int16_t web_usb_value(int32_t v)  /* a mix value (the clicks out) to the computer's */
{
    v = v < -131072 ? -131072 : v;       /* (the mix before the player's ceiling) */
    v = v > 131071 ? 131071 : v;
    v = (int32_t)(((int64_t)v * web_usb_gain) >> 4);
    return (int16_t)(out_ceiling(v) >> 8);
}
static void web_usb_block(const int32_t *lr, const int16_t *click, int n)
{
    int i;
    if (n <= 0 || (uint32_t)n > WEB_REC - web_rec_n)   /* (never: the worklet takes them after every render) */
        return;
    for (i = 0; i < n; i++) {
        int32_t c = click ? click[i] : 0;
        web_rec_buf[2u * web_rec_n] = web_usb_value(lr[2 * i] - c);
        web_rec_buf[2u * web_rec_n + 1u] = web_usb_value(lr[2 * i + 1] - c);
        web_rec_n++;
    }
}

/* the second core: one core here, so a block's job runs at once where it is handed over (the device's order, the
 * same sound) and its time is core 2's; core 1's figure is the half's time without it */
#define WEB_C2_START_US 2000000u         /* the device's time when core 2 is ready (STARTING before) */
static int web_c2_sw = 1, web_c2_forced;     /* SETTINGS' SECOND CORE, and the debug build's force-off */
static double web_c2_half_us;                /* core 2's jobs' time this half */
static uint32_t web_c2_half_n;               /* the blocks handed over this half */
static int web_c2_on(void) { return web_c2_sw && !web_c2_forced; }

void plat_cpu(plat_cpu_t *c, int reset)
{
    *c = web_cpu;
    c->c2_state = !web_c2_on() ? PLAT_C2_OFF : web_us < WEB_C2_START_US ? PLAT_C2_STARTING : PLAT_C2_ON;
    c->c2_why = PLAT_C2_WHY_NONE;        /* (the second core never stops here) */
    if (reset) {
        web_cpu.max_us = 0;
        web_cpu.late = 0;
        web_cpu.c2_max_us = 0;
    }
}

int plat_c2_half(void) { return web_us >= WEB_C2_START_US && web_c2_on(); }

void plat_c2_post(void (*job)(uint32_t), uint32_t j)
{
    double t0 = web_clock_us();
    job(j);
    web_c2_half_us += web_clock_us() - t0;
    web_c2_half_n += j != PLAT_C2_VOICE_JOB;     /* (the blocks: their effects' jobs; the voice jobs the player's) */
}

void plat_c2_enable(int on) { web_c2_sw = on != 0; }
void plat_c2_force_off(int off) { web_c2_forced = off != 0; }
void plat_c2_wait(void) {}                   /* (the jobs are done where they were handed over) */

/* --------------------------------------------------------------- flash --- */
/* 1 MiB, erased (0xFF) at first; the firmware's part below plat_fw_end() written (0x00). The user
 * area reads in place, as on the FM-1; the rest only through plat_flash_read. */
#define FL_SIZE      (1u << 20)
#define FL_USER1     0x093000u
#define FL_USER1_END 0x0E0000u
#define FL_USER2     0x0E5000u
#define FL_USER2_END 0x0FF000u
static uint8_t web_flash[FL_SIZE];
static uint32_t web_flash_gen;           /* erases and programs since power-on: the page saves after them */

uint32_t plat_fw_end(void) { return WEB_FW_END; }
/* the FM-1's RAM, as its release build lays it out (no such RAM here): the variables as measured on its link
 * (fw/tools/build.py's report), the LCD's transfer buffers */
void plat_ram(plat_ram_t *r) { r->total = 512u * 1024u; r->ram_text = 4096u; r->variables = 49024u; r->screen = 3840u; }

static int fl_writable(uint32_t addr, uint32_t n)   /* the user area: the device writes nothing else */
{
    uint32_t end = addr + n;
    if (end < addr)
        return 0;
    if (addr >= FL_USER1 && end <= FL_USER1_END)
        return 1;
    return addr >= FL_USER2 && end <= FL_USER2_END;
}

const uint8_t *plat_flash_ptr(uint32_t addr)
{
#ifdef PTFM_EDITOR
    /* (the editor's preview may lay its user area from the slot's start, 0x4000: store.h's STORE_P_START,
     * store_preview_room) */
    return addr >= 0x004000u && addr < FL_USER2_END ? web_flash + addr : 0;
#else
    return addr >= FL_USER1 && addr < FL_USER2_END ? web_flash + addr : 0;
#endif
}

int plat_flash_read(uint32_t addr, void *dst, uint32_t n)
{
    if (addr >= FL_SIZE || n > FL_SIZE - addr)
        return -1;
    memcpy(dst, web_flash + addr, n);
    if (addr < FL_USER1)                 /* direct reads: ~6 us + 0.72 us a byte, in 256-B windows */
        web_busy((n + 255u) / 256u * 6u + n * 72u / 100u);
    return 0;
}

int plat_flash_erase(uint32_t addr)
{
    if (addr & 0xFFFu || !fl_writable(addr, 4096u))
        return -1;
    memset(web_flash + addr, 0xFF, 4096u);
    web_flash_gen++;
    web_busy(17100u);
    return 0;
}

int plat_flash_prog(uint32_t addr, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (!n || n > 256u || (addr & 0xFFu) + n > 256u || !fl_writable(addr, n))
        return -1;
    for (i = 0; i < n; i++)
        web_flash[addr + i] &= s[i];     /* NOR: a program only clears bits */
    web_flash_gen++;
    web_busy(n == 256u ? 1700u : 1930u);
    return 0;
}

/* -------------------------------------------------------------- SysEx --- */
/* in: the page's messages, each delivered to app_sysex at the next main-loop pass; out: what the
 * device sends, until the page takes it */
#define SX_IN 16384u
#define SX_OUT 16384u
static uint8_t web_sx_stage[2048];       /* the page writes one message here, then web_sysex_push */
static uint8_t web_sx_in[SX_IN];
static uint32_t web_sx_in_n;             /* bytes queued: messages one after another, F0 ... F7 */
static uint8_t web_sx_out[SX_OUT];
static uint32_t web_sx_out_n;

int plat_sysex_send(const uint8_t *msg, uint32_t n)
{
    if (n > SX_OUT - web_sx_out_n)
        return -1;
    memcpy(web_sx_out + web_sx_out_n, msg, n);
    web_sx_out_n += n;
    return 0;
}

static void web_sysex_deliver(void)      /* the main loop: each queued message to the app */
{
    uint32_t i = 0;
    while (i < web_sx_in_n) {
        uint32_t j = i;
        while (j < web_sx_in_n && web_sx_in[j] != 0xF7u)
            j++;
        if (j == web_sx_in_n)
            break;                       /* (never: messages are pushed whole) */
        app_sysex(web_sx_in + i, j + 1u - i);
        i = j + 1u;
    }
    web_sx_in_n = 0;
}

/* ---------------------------------------------------------------- MIDI --- */
/* in: what the page's MIDI input sends (a keyboard, a sequencer), each message with the device's time it
 * came at; out: what the device sends, until the page takes it (3 bytes a message, a realtime one alone) */
#define MIDI_IN 256u
#define MIDI_OUT 4096u
static plat_midi_t web_mi[MIDI_IN];
static uint32_t web_mi_w, web_mi_r;
static uint8_t web_mo[MIDI_OUT];
static uint32_t web_mo_n;

int plat_midi_rx(plat_midi_t *e)
{
    if (web_mi_r == web_mi_w)
        return 0;
    *e = web_mi[web_mi_r++ % MIDI_IN];
    return 1;
}

int plat_midi_tx(const uint8_t m[3])
{
    uint32_t n = m[0] >= 0xF8u ? 1u : (m[0] & 0xE0u) == 0xC0u ? 2u : 3u;   /* realtime 1, Cx Dx 2, else 3 */
    if (m[0] < 0x80u || n > MIDI_OUT - web_mo_n)
        return -1;
    memcpy(web_mo + web_mo_n, m, n);
    web_mo_n += n;
    return 0;
}

void plat_master_set(uint16_t v) { web_master = (uint16_t)(v > 1023u ? 1023u : v); }   /* (the page's MASTER moved takes over) */

/* ---------------------------------------------------------------- rest --- */
static char web_fault_text[128];

int plat_battery(void) { return -1; }
void plat_idle(void) {}                  /* (no watchdog here) */
uint32_t plat_lock(void) { return 0; }   /* (the audio and the main loop share one thread here) */
void plat_unlock(uint32_t was) { (void)was; }

void plat_fault(const char *why)
{
    uint32_t i;
    for (i = 0; why[i] && i + 1u < sizeof web_fault_text; i++)
        web_fault_text[i] = why[i];
    web_fault_text[i] = 0;
    __builtin_trap();
}

/* FACTORY RESET's restart: asked here, made by the page (web_restart, then the power switch, the flash kept); the
 * firmware waits silent meanwhile */
static uint32_t web_restart_asked;
void plat_restart(void) { web_restart_asked = 1; }
WEB_EXPORT("web_restart") uint32_t web_restart(void) { return web_restart_asked; }

/* ------------------------------------------------- the debug build's figures --- */
#ifdef PTFM_DEBUG
#include "dbg.h"
/* the platform's figures for the debug page (core/dbg.h): the audio's time is the PC's (web_clock_us), a half's
 * budget the device's; no flash reads in the audio context, no painted stacks, no USB audio, no CORE. Core 2's
 * window: its jobs' time and blocks as handed over (never late, no waits, no misreads, no stack or start of its own) */
static struct { uint32_t isr_sum, isr_max, halves, late, passes, c2_sum, c2_max, c2_blocks, c2_halves; } web_dbg;
static uint16_t web_sat16(uint32_t v) { return (uint16_t)(v > 65535u ? 65535u : v); }
void plat_dbg(plat_dbg_t *d)
{
    memset(d, 0, sizeof *d);
    d->isr_sum_us = web_dbg.isr_sum;
    d->isr_max_us = web_sat16(web_dbg.isr_max);
    d->halves = web_sat16(web_dbg.halves);
    d->late = web_sat16(web_dbg.late);
    d->passes = web_sat16(web_dbg.passes);
    d->c2.sum_us = web_dbg.c2_sum;
    d->c2.max_us = web_sat16(web_dbg.c2_max);
    d->c2.blocks = web_sat16(web_dbg.c2_blocks);
    d->c2.halves = web_sat16(web_dbg.c2_halves);
    d->c2.late1 = d->late;
    memset(&web_dbg, 0, sizeof web_dbg);
}
uint32_t plat_ticks(void) { return (uint32_t)(web_clock_us() * 24.0); }
void plat_cache_cold(void) {}
int plat_bench_core(int i, dbg_bench_t *r) { (void)i; (void)r; return 0; }
#endif

/* ------------------------------------------------------- the device's time --- */
/* the tests' FM-1 as slow as the real one: every main-loop pass takes this long at least (the FM-1's reach ~140 ms
 * while a list or DJ redraws; here a pass takes only its screen's transfers) */
static uint32_t web_pass_min_us;
WEB_EXPORT("web_pass_min") void web_pass_min(uint32_t us) { web_pass_min_us = us; }

static void web_ms(void)                 /* a millisecond: the main loop's pass, when it is free */
{
    if (web_main_us <= web_us) {
        web_main_us = web_us;
        web_in_main = 1;
        if (web_sx_in_n)
            web_sysex_deliver();
        app_poll();
        if (web_main_us < web_us + web_pass_min_us)
            web_main_us = web_us + web_pass_min_us;
        web_in_main = 0;
#ifdef PTFM_DEBUG
        web_dbg.passes++;
#endif
    }
    web_us += 1000u;
    web_ms_now++;
}

static void web_one_half(void)           /* the device's time to this half's end, then the audio's work */
{
    uint64_t end = (web_frames + PLAT_HALF) * 1000000u / PLAT_RATE;
    double t0, dt;
    uint32_t k, us, c2;
    while (web_us + 1000u <= end)
        web_ms();
    web_c2_half_us = 0;
    web_c2_half_n = 0;
    t0 = web_clock_us();
    app_audio(web_half, PLAT_HALF);
    dt = web_clock_us() - t0;
    c2 = web_c2_half_us <= 0 ? 0u : web_c2_half_us >= 4e9 ? 4000000000u : (uint32_t)web_c2_half_us;
    dt -= web_c2_half_us;                /* core 1's: the half's time without core 2's jobs */
    us = dt <= 0 ? 0u : dt >= 4e9 ? 4000000000u : (uint32_t)dt;
    web_cpu.c2_last_us = c2;
    web_cpu.c2_sum_us += c2;
    if (c2 > web_cpu.c2_max_us)
        web_cpu.c2_max_us = c2;
    web_cpu.last_us = us;
    web_cpu.sum_us += us;
    web_cpu.halves++;
    if (us > web_cpu.max_us)
        web_cpu.max_us = us;
    if (us > HALF_US)
        web_cpu.late++;                  /* longer than the half it made */
#ifdef PTFM_DEBUG
    web_dbg.isr_sum += us;
    if (us > web_dbg.isr_max)
        web_dbg.isr_max = us;
    web_dbg.halves++;
    if (us > HALF_US)
        web_dbg.late++;
    if (web_c2_half_n) {
        web_dbg.c2_sum += c2;
        if (c2 > web_dbg.c2_max)
            web_dbg.c2_max = c2;
        web_dbg.c2_blocks += web_c2_half_n;
        web_dbg.c2_halves++;
    }
#endif
    out_convert(web_half, web_conv, PLAT_HALF,
                (out_master_q12(web_master) * out_line_gain_q12(web_line_db)) >> 12, web_mono);
    for (k = 0; k < PLAT_HALF; k++) {
        uint32_t i = (web_ring_w++ % WEB_RING) * 2u;
        web_ring[i] = web_conv[2u * k];
        web_ring[i + 1u] = web_conv[2u * k + 1u];
    }
    web_frames += PLAT_HALF;
}

/* ------------------------------------------------------------ the page's calls --- */
WEB_EXPORT("web_flash") uint8_t *web_flash_buf(void) { return web_flash; }
WEB_EXPORT("web_flash_gen") uint32_t web_flash_gen_get(void) { return web_flash_gen; }
WEB_EXPORT("web_fw_end") uint32_t web_fw_end(void) { return WEB_FW_END; }
WEB_EXPORT("web_fb") uint16_t *web_fb_buf(void) { return web_fb; }
WEB_EXPORT("web_fb_writes") uint32_t web_fb_writes_get(void) { return web_fb_writes; }
WEB_EXPORT("web_leds") uint8_t *web_leds_buf(void) { return web_leds; }
WEB_EXPORT("web_out") int32_t *web_out_buf(void) { return web_out; }
WEB_EXPORT("web_now_ms") uint32_t web_now_ms(void) { return web_ms_now; }
WEB_EXPORT("web_fault") char *web_fault_buf(void) { return web_fault_text; }
WEB_EXPORT("web_sysex_stage") uint8_t *web_sysex_stage(void) { return web_sx_stage; }
WEB_EXPORT("web_sysex_out") uint8_t *web_sysex_out_buf(void) { return web_sx_out; }
WEB_EXPORT("web_sysex_out_len") uint32_t web_sysex_out_len(void) { return web_sx_out_n; }
WEB_EXPORT("web_sysex_out_clear") void web_sysex_out_clear(void) { web_sx_out_n = 0; }
WEB_EXPORT("web_midi_out") uint8_t *web_midi_out_buf(void) { return web_mo; }
WEB_EXPORT("web_midi_out_len") uint32_t web_midi_out_len(void) { return web_mo_n; }
WEB_EXPORT("web_midi_out_clear") void web_midi_out_clear(void) { web_mo_n = 0; }

/* a MIDI message from the page's input (not SysEx: that is web_sysex_push's), at the device's time now;
 * 0 queued, -1 the queue full */
WEB_EXPORT("web_midi_in") int32_t web_midi_in(uint32_t b0, uint32_t b1, uint32_t b2, uint32_t src)
{
    plat_midi_t *e;
    if (web_mi_w - web_mi_r >= MIDI_IN || b0 < 0x80u || b0 == 0xF0u || b0 == 0xF7u)
        return -1;
    e = &web_mi[web_mi_w % MIDI_IN];
    e->m[0] = (uint8_t)b0;
    e->m[1] = (uint8_t)(b1 & 127u);
    e->m[2] = (uint8_t)(b2 & 127u);
    e->src = src ? PLAT_MIDI_JACK : PLAT_MIDI_USB;
    e->ms = web_ms_now;
    web_mi_w++;
    return 0;
}

/* the rows changed since the last call: y0 << 16 | y1 (y1 > y0), or 0 when none */
WEB_EXPORT("web_fb_dirty") uint32_t web_fb_dirty(void)
{
    uint32_t r = 0;
    if (web_dirty_y1 > web_dirty_y0)
        r = (uint32_t)web_dirty_y0 << 16 | (uint32_t)web_dirty_y1;
    web_dirty_y0 = PLAT_H;
    web_dirty_y1 = 0;
    return r;
}

/* the flash as a new FM-1's: erased, the firmware's part written; the page then copies a kept image
 * over it (web_flash) before web_init */
WEB_EXPORT("web_flash_reset") void web_flash_reset(void)
{
    memset(web_flash, 0xFF, sizeof web_flash);
    memset(web_flash, 0x00, WEB_FW_END);
}

/* power-on */
WEB_EXPORT("web_init") void web_init(void)
{
    web_in_main = 1;
    app_init();
    web_in_main = 0;
}

/* n frames (<= 1024) into web_out, stereo, the codec's 24-bit words: the device runs until they exist */
WEB_EXPORT("web_render") void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (web_ring_w - web_ring_r < n)
        web_one_half();
    for (k = 0; k < n; k++) {
        uint32_t i = (web_ring_r++ % WEB_RING) * 2u;
        web_out[2u * k] = web_ring[i];
        web_out[2u * k + 1u] = web_ring[i + 1u];
    }
}

/* the panel: a button (B_*) or a note key (0..26) down or up, a knob's clicks (+ clockwise), MASTER */
WEB_EXPORT("web_button") void web_button(uint32_t id, uint32_t down)
{
    if (id >= B_COUNT)
        return;
    if (down && !(web_buttons & 1u << id))
        web_pressed();
    web_buttons = down ? web_buttons | 1u << id : web_buttons & ~(1u << id);
}

WEB_EXPORT("web_note") void web_note(uint32_t n, uint32_t down)
{
    if (n >= 27u)
        return;
    if (down && !(web_notes & 1u << n))
        web_pressed();
    web_notes = down ? web_notes | 1u << n : web_notes & ~(1u << n);
}

WEB_EXPORT("web_knob") void web_knob(uint32_t id, int32_t step)
{
    if (id < K_COUNT)
        web_knobs[id] += step;
}

WEB_EXPORT("web_master") void web_master_set(int32_t v)
{
    web_master = (uint16_t)(v < 0 ? 0 : v > 1023 ? 1023 : v);
}

/* a USB bus reset, as the FM-1 sees one when it is unplugged (MIRROR ends: the tests' and the page's) */
static uint32_t web_usb_resets;
uint32_t plat_usb_resets(void) { return web_usb_resets; }
WEB_EXPORT("web_usb_reset") void web_usb_reset(void) { web_usb_resets++; }

/* RECORD: USB audio's frames from the next block on (on), or none; web_rec_len the frames made since the last
 * web_rec_take, 16-bit stereo at web_rec_at */
WEB_EXPORT("web_rec") void web_rec(uint32_t on)
{
    web_rec_n = 0;
    app_audio_usb(on ? web_usb_block : 0);
}
WEB_EXPORT("web_rec_at") int16_t *web_rec_at(void) { return web_rec_buf; }
WEB_EXPORT("web_rec_len") uint32_t web_rec_len(void) { return web_rec_n; }
WEB_EXPORT("web_rec_take") void web_rec_take(void) { web_rec_n = 0; }

/* DOWNLOAD DISK: the user area's live records, as seeding's layout lists them (fw/core/store/seed.c), but asked
 * whatever is not saved (seeding refuses then). web_disk fills web_disk_at with n entries of 3 words (kind | num << 8
 * | sub << 16 | ver << 24, the header's flash address, the payload's length) and returns n, or -1 while a save is
 * being written; web_unsaved: 1 while changes are not saved (SAVE's LED), which are not in the flash, so on no disk
 * of it */
#define WEB_DISK_MAX 1024
static uint32_t web_disk_buf[WEB_DISK_MAX * 3];
WEB_EXPORT("web_disk_at") uint32_t *web_disk_at(void) { return web_disk_buf; }
WEB_EXPORT("web_disk") int32_t web_disk(void)
{
    int i, n = 0, all = store_count();
    if (store_saving())
        return -1;
    for (i = 0; i < all && n < WEB_DISK_MAX; i++) {
        store_rec_t r;
        uint32_t *e = web_disk_buf + 3 * n;
        if (!store_entry(i, &r, 0, 0) || r.area != STORE_AREA_USER || r.deleted)
            continue;
        e[0] = (uint32_t)r.kind | (uint32_t)r.num << 8 | (uint32_t)r.sub << 16 | (uint32_t)r.ver << 24;
        e[1] = r.addr;
        e[2] = r.len;
        n++;
    }
    return n;
}
WEB_EXPORT("web_unsaved") uint32_t web_unsaved(void) { return ui_unsaved || store_edit_count() ? 1u : 0u; }

/* a SysEx the page staged in web_sysex_stage (n bytes, F0 ... F7): queued for the main loop; 0 when
 * queued, -1 when the queue is full (try again after some audio) */
WEB_EXPORT("web_sysex_push") int32_t web_sysex_push(uint32_t n)
{
    if (n < 2u || n > sizeof web_sx_stage || web_sx_stage[0] != 0xF0u || web_sx_stage[n - 1u] != 0xF7u)
        return -1;
    if (n > SX_IN - web_sx_in_n)
        return -1;
    memcpy(web_sx_in + web_sx_in_n, web_sx_stage, n);
    web_sx_in_n += n;
    return 0;
}
