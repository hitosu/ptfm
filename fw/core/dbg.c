/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug build's figures (specs/debug.md, dbg.h): each audio half's stages summed into a window, the
 * window answered as FIGURES when the page asks (SysEx 30), core 2's as FIGURES 2 right after it, and a new one
 * started. Only with PTFM_DEBUG. */
#ifdef PTFM_DEBUG
#include <string.h>
#include "dbg.h"
#include "plat.h"
#include "play/player.h"
#include "play/dj.h"
#include "play/rec.h"
#include "fx/fx.h"
#include "ui/ui_core.h"
#include "remote.h"
#include "eng/dx7/dx7.h"                                /* dx7_sin_tab: the USB test tone */

uint32_t dbg_half[DS_N];
uint32_t dbg2_half[DS_N];
uint16_t dbg_blocks_half[4];
uint8_t dbg_notes_half;
volatile uint8_t dbg_usb_tone;
static uint32_t tone_resets;            /* plat_usb_resets when the tone went on */
static const int32_t *tone_sin;         /* the DX7's sine table (dx7_table(0)): the tone's code, in core 2's room,
                                         * reads it and calls nothing; taken before 32 BENCH or 34 USB TONE runs it */

/* the window: written by the audio context (dbg_half_end), read and cleared by the main loop under plat_lock */
typedef struct {
    uint32_t sum[DS_N];
    uint16_t max[DS_N];
    uint16_t blocks[4];
    uint8_t voices_most;
    dbg_worst_t worst;                                 /* its worst half */
} win_t;
static win_t win;
static struct { uint16_t frames; uint32_t frame_sum, frame_max; } fwin;   /* the main loop's own */
static uint32_t win_t0, steals0;
static uint8_t frozen, thaw, asked, win_frozen;   /* (win_frozen: the window started frozen) */
static uint8_t unsent;                  /* FIGURES answers USB could not send whole, since power-on (mod 256) */
static uint8_t win_no;                  /* the window's number (mod 256): its 31 and its 37 carry it */
static uint8_t c2_off;                  /* 38 SECOND CORE 0: core 2 off, whatever SETTINGS says */
typedef char figures_fit[8 + (DBG_FIGURES_LEN * 8 + 6) / 7 <= 640 ? 1 : -1];   /* (remote_send's largest message) */
typedef char figures2_fit[8 + (DBG_FIGURES2_LEN * 8 + 6) / 7 <= 640 ? 1 : -1];

static uint16_t cap16(uint32_t v) { return (uint16_t)(v > 65535u ? 65535u : v); }

void dbg_worst_take(dbg_worst_t *w, const uint32_t *st, const uint16_t *bl, uint8_t voices, uint8_t notes,
                    uint32_t us)
{
    if (us <= w->us)
        return;
    w->us = cap16(us);
    for (int s = 0; s < DS_N; s++) w->st[s] = cap16(st[s]);
    for (int e = 0; e < 4; e++) w->blocks[e] = (uint8_t)(bl[e] > 255u ? 255u : bl[e]);
    w->voices = voices;
    w->notes = notes;
}

void dbg_half_end(uint8_t voices)
{
    uint32_t us = 0;
    for (int s = 0; s < DS_N; s++) {                   /* (the half's jobs' stages joined to the rest: the same sums
                                                        * as one core's) */
        dbg_half[s] += dbg2_half[s];
        dbg2_half[s] = 0;
    }
    for (int s = 0; s < DS_N; s++)
        if (DS_TOP >> s & 1u) us += dbg_half[s];
    dbg_worst_take(&win.worst, dbg_half, dbg_blocks_half, voices, dbg_notes_half, us);
    dbg_notes_half = 0;
    for (int s = 0; s < DS_N; s++) {
        uint32_t t = dbg_half[s];
        win.sum[s] += t;
        if (t > win.max[s]) win.max[s] = (uint16_t)(t > 65535u ? 65535u : t);
        dbg_half[s] = 0;
    }
    for (int e = 0; e < 4; e++) {
        uint32_t b = win.blocks[e] + dbg_blocks_half[e];
        win.blocks[e] = (uint16_t)(b > 65535u ? 65535u : b);
        dbg_blocks_half[e] = 0;
    }
    if (voices > win.voices_most) win.voices_most = voices;
}

void dbg_frame(uint32_t us)
{
    if (fwin.frames < 65535u) fwin.frames++;
    fwin.frame_sum += us;
    if (us > fwin.frame_max) fwin.frame_max = us;
}

int dbg_frozen(void) { return frozen; }

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

void dbg_worst_put(uint8_t *p, const dbg_worst_t *w)
{
    put16(p, w->us);
    p[2] = w->voices;
    p[3] = w->notes;
    for (int e = 0; e < 4; e++) p[4 + e] = w->blocks[e];
    for (int s = 0; s < DS_N; s++) put16(p + 8 + 2 * s, w->st[s]);
}

/* DJ as it stands (specs/debug.md, FIGURES' DJ): remote.c's, the state carries the same */
typedef char dj_len_ok[DBG_DJ_LEN == REMOTE_DJ_LEN ? 1 : -1];
#define dj_put remote_dj_put

/* core 2's window (specs/debug.md, Core 2's bytes): 37 FIGURES 2, sent right after its 31 */
static void dbg_figures2(uint8_t *f, const plat_dbg_t *d)
{
    plat_cpu_t c;
    plat_cpu(&c, 0);
    memset(f, 0, DBG_FIGURES2_LEN);
    f[0] = DBG_FIGURES2_VERSION;
    f[1] = win_no;
    f[2] = c.c2_state;
    f[3] = c.c2_why;
    f[4] = (uint8_t)((ui_set[S_SECOND_CORE] ? 0u : 1u) |  /* SETTINGS' SECOND CORE on; off by 38; its start needed
                                                           * the clock-divider bit; the blocks on core 2 all the window */
                     (c2_off ? 2u : 0u) | (d->c2.clk ? 4u : 0u) | (d->halves && d->c2.halves >= d->halves ? 8u : 0u));
    put16(f + 6, d->c2.start_10ns);
    put32(f + 8, d->c2.sum_us);
    put16(f + 12, d->c2.max_us);
    put16(f + 14, d->c2.blocks);
    put16(f + 16, d->c2.misreads);
    put32(f + 18, d->c2.wait_sum_us);
    put16(f + 22, d->c2.wait_max_us);
    put16(f + 24, d->c2.late1);
    put16(f + 26, d->c2.late2);
    put16(f + 28, d->c2.stack_used);
    put16(f + 30, d->c2.stack_size);
    put32(f + 32, d->c2.fault_vec);
    put32(f + 36, d->c2.fault_pc);
    {                                   /* its voice jobs (the voices in its places) */
        player_v2_t v;
        player_v2_figures(&v);
        put32(f + 40, v.us);
        put16(f + 44, (uint16_t)(v.jobs > 0xFFFFu ? 0xFFFFu : v.jobs));
        f[46] = v.most;
        put32(f + 48, v.wait_sum);
        put16(f + 52, (uint16_t)(v.wait_max > 0xFFFFu ? 0xFFFFu : v.wait_max));
    }
    f[54] = (uint8_t)(d->sx_lost[0] > 255u ? 255u : d->sx_lost[0]);   /* SysEx lost on the way in, this window */
    f[55] = (uint8_t)(d->sx_lost[1] > 255u ? 255u : d->sx_lost[1]);
}

int dbg_figures(uint8_t *f, uint8_t *f2)
{
    plat_dbg_t d;
    player_state_t st;
    uint32_t was = plat_lock(), now = plat_ms();
    win_t w = win;
    memset(&win, 0, sizeof win);
    plat_unlock(was);
    memset(&d, 0, sizeof d);                           /* (0 for what the platform has none of) */
    plat_dbg(&d);
    player_state(&st, 0);
    memset(f, 0, DBG_FIGURES_LEN);
    f[0] = DBG_FIGURES_VERSION;
    f[1] = (uint8_t)(win_frozen | (frozen && !thaw) << 1);   /* frozen through the window; frozen after this answer */
    win_frozen = (uint8_t)(frozen && !thaw);
    put16(f + 2, cap16(now - win_t0));
    win_t0 = now;
    put16(f + 4, d.halves);
    put16(f + 6, d.late);
    put32(f + 8, d.isr_sum_us);
    put16(f + 12, d.isr_max_us);
    put16(f + 14, 5804);
    f[16] = DS_N;
    f[17] = win_no;                                    /* (its 37 carries the same) */
    dbg_figures2(f2, &d);
    win_no++;
    uint8_t *p = f + 18;                               /* (the stages, then the rest: specs/debug.md) */
    for (int s = 0; s < DS_N; s++, p += 6) {
        put32(p, w.sum[s]);
        put16(p + 4, w.max[s]);
    }
    for (int e = 0; e < 4; e++, p += 2) put16(p, w.blocks[e]);
    p[0] = st.voices;
    p[1] = w.voices_most > st.voices ? w.voices_most : st.voices;
    put16(p + 2, cap16(st.steals - steals0 < 0x80000000u ? st.steals - steals0 : 0));
    steals0 = st.steals;
    put16(p + 4, d.flash_reads);
    put16(p + 6, d.flash_us);
    put16(p + 8, d.passes);
    put16(p + 10, fwin.frames);
    put32(p + 12, d.pass_max_us);
    put32(p + 16, fwin.frame_sum);
    put32(p + 20, fwin.frame_max);
    memset(&fwin, 0, sizeof fwin);
    for (int k = 0; k < 2; k++) {
        put16(p + 24 + 2 * k, d.stack_used[k]);
        put16(p + 28 + 2 * k, d.stack_size[k]);
    }
    put32(p + 32, now);
    p[36] = st.what;
    p[37] = st.playing;
    p[38] = st.song;
    p[39] = st.deck[0].song;
    p[40] = st.deck[1].song;
    p[41] = st.voice_count;
    put16(p + 42, st.bpm10);
    put16(p + 44, fx_active());
    const ui_screen_t *sc = ui_screen_now();
    if (sc && sc->name) strncpy((char *)p + 46, T(sc->name), 9);
#ifdef PTFM_VERSION
    strncpy((char *)p + 56, PTFM_VERSION, 23);
#endif
    dbg_worst_put(p + 80, &w.worst);                   /* (the blocks' 8 B and these 80: the 88 after the stages) */
    dj_put(p + 80 + DBG_WORST_LEN);
    p += 80 + DBG_WORST_LEN + DBG_DJ_LEN;              /* USB audio (specs/debug.md, FIGURES' USB audio) */
    p[0] = (uint8_t)((d.usb.on ? 1u : 0u) | (d.usb.r48 ? 2u : 0u));
    p[1] = unsent;
    put16(p + 2, d.usb.pkts);
    put16(p + 4, d.usb.dry);
    put16(p + 6, d.usb.missed);
    put16(p + 8, d.usb.stalls);
    put16(p + 10, d.usb.starts);
    put16(p + 12, d.usb.fill);
    put16(p + 14, d.usb.target);
    return DBG_FIGURES_LEN;
}

int dbg_sysex(const uint8_t *msg, uint32_t n)
{
    if (n != 7 || msg[0] != 0xF0 || msg[1] != 0x7D || msg[2] != 0x50 || msg[3] != 0x54 || msg[6] != 0xF7)
        return 0;
    if (msg[4] == 0x32 || msg[4] == 0x34)
        tone_sin = dx7_table(0);
    if (msg[4] == 0x32)                                /* F0 7D 50 54 32 cmd F7: the bench */
        return bench_sysex(msg[5]);
    if (msg[4] == 0x34) {                              /* F0 7D 50 54 34 on F7: the USB test tone */
        dbg_usb_tone = msg[5] & 1;
        tone_resets = plat_usb_resets();
        return 1;
    }
    if (msg[4] == 0x38) {                              /* F0 7D 50 54 38 on F7: core 2 off (0) till a 38 with 1, or as
                                                        * SETTINGS says (1); the setting untouched, nothing saved */
        c2_off = !(msg[5] & 1);
        plat_c2_force_off(c2_off);
        return 1;
    }
    if (msg[4] != 0x30)
        return 0;                                      /* F0 7D 50 54 30 flags F7 */
    uint8_t fl = msg[5];
    if (fl & 2) { frozen = 1; ui_toast(T("FROZEN FOR MEASURING")); }
    if (fl & 4) thaw = 1;                              /* (after this answer: the window it closes was frozen) */
    asked = 1;
    return 1;
}

/* the USB test tone (specs/debug.md#the-usb-test-tone): n frames of 997 Hz on the left, 1,499 Hz on the right,
 * -12 dBFS, as USB audio's 16-bit values (USB GAIN not applied); the platform's USB tap calls it while
 * dbg_usb_tone is on (a block's job) */
PLAT_C2 void dbg_tone_block(int16_t *lr, int n)
{
    static uint32_t pl, pr;                            /* phases, 2^24 a cycle */
    const int32_t *st = tone_sin;
    for (int i = 0; i < n; i++) {
        lr[2 * i] = (int16_t)(((dx7_sin_tab(pl, st) >> 9) * 8231) >> 15);
        lr[2 * i + 1] = (int16_t)(((dx7_sin_tab(pr, st) >> 9) * 8231) >> 15);
        pl = (pl + 379296u) & 0xFFFFFFu;                /* 997 / 44,100 of 2^24 */
        pr = (pr + 570277u) & 0xFFFFFFu;                /* 1,499 */
    }
}

/* a message out (F0 7D 50 54 cmd, the bytes packed as seeding's: 7 in 8, F7): remote.c's (FIGURES, 606 B on the
 * wire, the largest, of USB's 640); 0 when USB did not take it (it is dropped) */
int dbg_send(uint8_t cmd, const uint8_t *b, uint32_t len) { return remote_send(cmd, b, len); }

void dbg_poll(void)
{
    if (dbg_usb_tone && plat_usb_resets() != tone_resets)
        dbg_usb_tone = 0;                              /* (a USB bus reset ends the test tone) */
    bench_poll();                                      /* (its next piece, a record) */
    if (asked) {                                       /* the window, then core 2's */
        uint8_t f[DBG_FIGURES_LEN], f2[DBG_FIGURES2_LEN];
        dbg_figures(f, f2);
        if (!dbg_send(0x31, f, DBG_FIGURES_LEN))
            unsent++;                                  /* (cut short or not sent: the window is lost, its 37 not sent) */
        else
            dbg_send(0x37, f2, DBG_FIGURES2_LEN);
        asked = 0;
        if (thaw) frozen = thaw = 0;
    }
}
#endif
