/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI CONTROLLER, the PT/FM controller (ctlmode.h, specs/controller.md). Its protocol, ctl.h, is included (its own
 * host test, fw/tests/ctl_test.c, and test vectors cover it); this is its
 * platform: PT/FM's input (plat_input), LEDs (plat_led), USB-MIDI (plat_midi_tx / plat_sysex_send in,
 * plat_midi_rx out) and screen (gfx), from the main loop. The tracker has ended for good when it starts: no
 * store, no player, no sound, no UI run any more. */
#include <stdint.h>
#include <string.h>
#include "ctlmode.h"
#include "plat.h"
#include "ui/gfx.h"
#include "ui/txt.h"
#include "ui/ui_core.h"                 /* ui_fmt, ui_dec, ui_note_name */
#include "store/store.h"                /* its scratch: this one's memory (the store is never mounted here) */
#include "ctl.h"

/* The controller's state lives in the store's scratch, which nothing else uses once the tracker has ended:
 * no RAM of its own (the tracker keeps 8 KB spare, and this would take ~550 B of it) */
#define CM_LOG 15u                      /* the log's lines on the screen */
#define CM_Q 32u                        /* the fast tick's events for the main loop (a millisecond's are a few) */
#define CM_KEYS ((1u << B_REC) | (1u << B_HOME))   /* the start's: REC mirror mode, HOME general mode */
typedef struct {
    uint8_t mode;                       /* CTL_MIRROR / CTL_GENERAL, once picked */
    uint8_t on;                         /* picked: the controller runs */
    int8_t chan;                        /* the start's SELECT: 0 the mode's own channel, else 1-16 */
    uint16_t armed;                     /* the start's keys seen up since it began: a press of one of them picks */
    uint8_t led[LED_COUNT];             /* as lit now */
    uint8_t dirty, hello_due, answer_due, usb_up;
    uint32_t resets, t_hello, t_draw;
    volatile uint32_t sent, lost, leds_in;
    ctl_ev_t log[CM_LOG];               /* the newest last */
    int32_t sum[CM_LOG];                /* a mirror knob's clicks added up on its line */
    uint32_t nlog;
    /* the fast tick (plat_fast: the platform's 1 kHz interrupt reads the keys, buttons and knobs, never waiting for
     * the main loop): it sends the channel messages itself; SysEx (MMC) and the log's lines go to the main loop
     * through q */
    uint8_t fast;                       /* the platform runs cm_fast */
    volatile uint8_t in_fast;           /* cm_fast is under way (ctl_emit queues) */
    volatile uint32_t qw, qr;
    struct { ctl_ev_t e; uint8_t send; } q[CM_Q];   /* send: SysEx the main loop sends */
    ctl_t ctl;                          /* ctl.c's */
} cm_t;
typedef char cm_fits[sizeof(cm_t) <= STORE_SCRATCH_BYTES ? 1 : -1];
static cm_t *cm_p;
#define cm (*cm_p)
#define ctl (cm_p->ctl)

static void cm_log(const ctl_ev_t *e)   /* a line of the log; a knob or MASTER turned again: its line */
{
    if (cm.nlog && (e->kind == CTL_EV_KNOB || e->kind == CTL_EV_MASTER) &&
        cm.log[cm.nlog - 1u].kind == e->kind && cm.log[cm.nlog - 1u].id == e->id) {
        cm.sum[cm.nlog - 1u] = e->kind == CTL_EV_KNOB && cm.mode == CTL_MIRROR ? cm.sum[cm.nlog - 1u] + e->val : e->val;
        cm.log[cm.nlog - 1u] = *e;
    } else {
        if (cm.nlog == CM_LOG) {
            memmove(cm.log, cm.log + 1, sizeof cm.log - sizeof cm.log[0]);
            memmove(cm.sum, cm.sum + 1, sizeof cm.sum - sizeof cm.sum[0]);
            cm.nlog--;
        }
        cm.log[cm.nlog] = *e;
        cm.sum[cm.nlog++] = e->val;
    }
    cm.dirty = 1;
}

/* ctl.c's way out: a channel message over USB-MIDI, SysEx (MMC, the answer) as SysEx; 1 queued. In the fast tick: a
 * channel message sent at once, SysEx and the log's line queued for the main loop (cm_drain) */
static int ctl_emit(const ctl_ev_t *e)
{
    int ok = 1;
    if (cm.in_fast) {
        uint32_t w = cm.qw;
        int sx = e->n && e->b[0] == 0xF0u;
        if (e->n && !sx) {
            uint8_t m[3] = {e->b[0], e->b[1], e->n > 2u ? e->b[2] : 0};
            ok = plat_midi_tx(m) == 0;
            if (ok) cm.sent++;
            else cm.lost++;
        }
        if (w - cm.qr >= CM_Q)
            return sx ? 0 : ok;         /* (the log full: the line goes; a SysEx waits for room) */
        cm.q[w % CM_Q].e = *e;
        cm.q[w % CM_Q].send = (uint8_t)sx;
        __asm__ volatile("" ::: "memory");
        cm.qw = w + 1u;
        return ok;
    }
    if (e->n && e->b[0] == 0xF0u) {
        ok = plat_sysex_send(e->b, e->n) == 0;
    } else if (e->n) {
        uint8_t m[3] = {e->b[0], e->b[1], e->n > 2u ? e->b[2] : 0};
        ok = plat_midi_tx(m) == 0;
    }
    if (e->n) {
        if (ok) cm.sent++;
        else cm.lost++;
    }
    cm_log(e);
    return ok;
}

static void cm_fast(const plat_input_t *in, uint32_t ms)   /* the platform's 1 kHz interrupt */
{
    int32_t steps[CTL_NENC];
    uint32_t i;
    for (i = 0; i < CTL_NENC; i++) steps[i] = in->knob[i];
    cm.in_fast = 1;
    ctl_tick(&ctl, ms, in->notes, in->buttons, steps, in->master);
    cm.in_fast = 0;
}

static void cm_drain(void)              /* main loop: the fast tick's SysEx sent, its lines logged */
{
    while (cm.qr != cm.qw) {
        uint32_t r = cm.qr;
        ctl_ev_t e = cm.q[r % CM_Q].e;
        if (cm.q[r % CM_Q].send) {
            if (plat_sysex_send(e.b, e.n))
                cm.lost++;
            else
                cm.sent++;
        }
        __asm__ volatile("" ::: "memory");
        cm.qr = r + 1u;
        cm_log(&e);
    }
}

void ctlmode_start(void)
{
    cm_p = (cm_t *)(void *)store_scratch();
    memset(cm_p, 0, sizeof *cm_p);
    cm.dirty = 1;
    g_theme(0);                                         /* (the controller's own colours, whatever THEME was) */
    g_invalidate();
}

static int cm_answer(uint32_t kind)     /* the handshake's answer, or unasked: the hello; 1 sent */
{
    uint8_t b[CTL_ANSWER_LEN];
    uint32_t n = ctl_answer(&ctl, b);
    ctl_ev_t e;
    if (plat_sysex_send(b, n))
        return 0;
    ctl_answered(&ctl);                 /* mirror: the panel's state follows */
    memset(&e, 0, sizeof e);
    e.kind = (uint8_t)kind;
    e.val = ctl.mode;
    e.n = (uint8_t)n;
    memcpy(e.b, b, n);
    cm.sent++;
    cm_log(&e);
    return 1;
}

void ctlmode_sysex(const uint8_t *msg, uint32_t n)
{
    if (!cm.on || n < 2u || !ctl_is_ask(msg + 1, n - 2u))
        return;
    if (ctl.mode == CTL_MIRROR)
        ctl.page = 1;
    cm.answer_due = 1;
}

/* ---- the screen: the start (REC or HOME to press), then the MIDI monitor (specs/controller.md) ---- */
static const txt_t CM_BTN[CTL_NBTN] = {TX("OCT-"), TX("OCT+"), TX("FX"), TX("SEL"), TX("ENV"), TX("LFO"), TX("EDIT"), TX("GLO"),
                                       TX("HOME"), TX("SAVE"), TX("ARP"), TX("SEQ"), TX("PLAY"), TX("REC")};
static const txt_t CM_ENC[CTL_NENC] = {TX("SELECT"), TX("ALGORITHM"), TX("K1"), TX("K2"), TX("K3"), TX("K4"), TX("PRESETS")};

static uint32_t ch_of(uint32_t mode) { return cm.chan ? (uint32_t)cm.chan - 1u : mode == CTL_MIRROR ? CTL_CH_MIRROR : CTL_CH_GENERAL; }
static uint32_t cm_ch(void) { return ch_of(cm.mode); }

static void cm_row(int y, int i)        /* log line i */
{
    const ctl_ev_t *e = &cm.log[i];
    char b[8];
    int col = C_LBLUE;
    switch (e->kind) {
    case CTL_EV_NOTE_ON:
    case CTL_EV_NOTE_OFF:
        col = C_WHITE;
        ui_note_name(b, e->val);
        g_text(2, y, &FONT_T, b, col);
        g_text(62, y, &FONT_T, e->kind == CTL_EV_NOTE_ON ? T("NOTE ON") : T("NOTE OFF"), col);
        if (e->kind == CTL_EV_NOTE_ON) g_text(122, y, &FONT_T, ui_dec(e->b[2]), col);
        break;
    case CTL_EV_BUTTON:
        g_text(2, y, &FONT_T, T(CM_BTN[e->id % CTL_NBTN]), col);
        g_text(62, y, &FONT_T, T("CC"), col);
        g_text(80, y, &FONT_T, ui_dec(e->b[1]), col);
        g_text(122, y, &FONT_T, ui_dec(e->val), col);
        break;
    case CTL_EV_KNOB:
    case CTL_EV_MASTER: {
        int32_t v = cm.sum[i];
        g_text(2, y, &FONT_T, e->kind == CTL_EV_MASTER ? T("MASTER") : T(CM_ENC[e->id % CTL_NENC]), col);
        g_text(62, y, &FONT_T, T("CC"), col);
        g_text(80, y, &FONT_T, ui_dec(e->b[1]), col);
        if (e->kind == CTL_EV_KNOB && cm.mode == CTL_MIRROR && v > 0) g_text(116, y, &FONT_T, "+", col);
        if (v < 0) g_text(116, y, &FONT_T, "-", col);
        g_text(122, y, &FONT_T, ui_dec(v < 0 ? -v : v), col);
        break;
    }
    case CTL_EV_PROGRAM:
        col = C_YELLOW;
        g_text(2, y, &FONT_T, T("PRESETS"), col);
        g_text(62, y, &FONT_T, T("PROGRAM"), col);
        g_text(110, y, &FONT_T, ui_dec(e->val), col);
        break;
    case CTL_EV_MMC:
        col = C_GREEN;
        g_text(2, y, &FONT_T, T("PLAY"), col);
        g_text(62, y, &FONT_T, e->val ? T("MMC PLAY") : T("MMC STOP"), col);
        break;
    case CTL_EV_ALL_OFF:
        g_text(2, y, &FONT_T, T("FM-1"), col);
        g_text(62, y, &FONT_T, T("ALL NOTES OFF"), col);
        break;
    case CTL_EV_ANSWER:
    case CTL_EV_HELLO:
        col = C_LMAGENTA;
        g_text(2, y, &FONT_T, T("FM-1"), col);
        g_text(62, y, &FONT_T, e->kind == CTL_EV_ANSWER ? T("ANSWER") : T("HELLO"), col);
        g_text(110, y, &FONT_T, e->val == CTL_MIRROR ? T("MIRROR") : T("GENERAL"), col);
        break;
    case CTL_EV_MODE:
        g_text(2, y, &FONT_T, e->val == CTL_MIRROR ? T("REC") : T("HOME"), C_WHITE);
        g_text(62, y, &FONT_T, e->val == CTL_MIRROR ? T("MODE MIRROR") : T("MODE GENERAL"), C_WHITE);
        return;
    default:
        return;
    }
    if (e->n && e->n <= 3u) {           /* the bytes, dark grey (SysEx: its ends) */
        for (uint32_t k = 0; k < e->n; k++) {
            static const char HX[] = "0123456789ABCDEF";
            char h[3] = {HX[e->b[k] >> 4], HX[e->b[k] & 15u], 0};
            g_text(170 + 18 * (int)k, y, &FONT_T, h, C_DGREY);
        }
    } else if (e->n) {
        g_text(170, y, &FONT_T, "F0 .. F7", C_DGREY);
    }
}

static void cm_frame(void)
{
    char b[32];
    int mc = cm.mode == CTL_MIRROR ? C_LCYAN : C_YELLOW;
    g_clear(C_BLACK);
    if (!cm.on) {                       /* the start: REC or HOME, SELECT the channel */
        g_text(g_centre(&FONT_L, T("PT/FM"), 0, 240), 24, &FONT_L, T("PT/FM"), C_WHITE);
        g_text(g_centre(&FONT_L, T("CONTROLLER"), 0, 240), 58, &FONT_L, T("CONTROLLER"), C_WHITE);
        g_text(g_centre(&FONT_T, T("PRESS A BUTTON TO START"), 0, 240), 104, &FONT_T, T("PRESS A BUTTON TO START"), C_LGREY);
        ui_fmt(b, sizeof b, T("REC   MIRROR  CH %-2d"), (int)ch_of(CTL_MIRROR) + 1);
        g_text(g_centre(&FONT_S, b, 0, 240), 130, &FONT_S, b, C_LCYAN);
        ui_fmt(b, sizeof b, T("HOME  GENERAL CH %-2d"), (int)ch_of(CTL_GENERAL) + 1);
        g_text(g_centre(&FONT_S, b, 0, 240), 154, &FONT_S, b, C_YELLOW);
        if (cm.chan) ui_fmt(b, sizeof b, T("SELECT: CHANNEL %d"), cm.chan);
        else ui_fmt(b, sizeof b, "%s", T("SELECT: CHANNEL DEFAULT"));
        g_text(g_centre(&FONT_T, b, 0, 240), 188, &FONT_T, b, cm.chan ? C_WHITE : C_LGREY);
        g_hline(0, 216, 240, C_DGREY);
        g_text(2, 222, &FONT_T, T("SWITCH OFF AND ON FOR THE TRACKER"), C_DGREY);
        return;
    }
    g_text(2, 2, &FONT_T, T("PT/FM CONTROLLER"), C_WHITE);
    ui_fmt(b, sizeof b, T("%s CH %d"), cm.mode == CTL_MIRROR ? T("MIRROR") : T("GENERAL"), (int)cm_ch() + 1);
    g_text(238 - g_tw(&FONT_T, b), 2, &FONT_T, b, mc);
    g_hline(0, 15, 240, C_DGREY);
    ui_fmt(b, sizeof b, T("SENT %u  LOST %u"), (unsigned)cm.sent, (unsigned)cm.lost);
    g_text(2, 19, &FONT_T, b, cm.lost ? C_RED : C_LGREY);
    if (!cm.usb_up) {                   /* (unplugged, or no host yet: nothing goes out) */
        g_text(238 - g_tw(&FONT_T, T("USB OFF")), 19, &FONT_T, T("USB OFF"), C_RED);
    } else if (cm.mode == CTL_MIRROR) {
        ui_fmt(b, sizeof b, T("LEDS IN %u"), (unsigned)cm.leds_in);
        g_text(238 - g_tw(&FONT_T, b), 19, &FONT_T, b, ctl.page ? C_GREEN : C_DGREY);
    }
    g_hline(0, 32, 240, C_DGREY);
    for (uint32_t i = 0; i < cm.nlog; i++)
        cm_row(35 + 12 * (int)(CM_LOG - cm.nlog + i), (int)i);
    g_hline(0, 216, 240, C_DGREY);
    g_text(2, 222, &FONT_T, T("SWITCH OFF AND ON FOR THE TRACKER"), C_DGREY);
}

static void cm_draw(void)
{
    for (int half = 0; half < 2; half++) {
        g_band(half);
        cm_frame();
        g_push();
    }
    cm.dirty = 0;
}

void ctlmode_poll(void)
{
    plat_input_t in;
    plat_midi_t m;
    uint32_t now = plat_ms(), i, r;
    int up;
    if (cm.on && cm.fast)               /* (the fast tick takes the input: a take here would eat the knobs' clicks) */
        memset(&in, 0, sizeof in);
    else
        plat_input(&in);
    if (!cm.on) {                       /* the start: SELECT the channel, a press of REC or HOME starts it */
        int c = cm.chan + in.knob[K_SELECT];
        uint32_t pick = in.buttons & cm.armed;          /* (pressed after being seen up: not one held since before) */
        c = c < 0 ? 0 : c > 16 ? 16 : c;
        if (c != cm.chan) { cm.chan = (int8_t)c; cm.dirty = 1; }
        cm.armed |= (uint16_t)(~in.buttons & CM_KEYS);
        for (i = 0; i < LED_COUNT; i++) plat_led((int)i, i == B_REC || i == B_HOME ? 255 : 0);
        if (pick) {
            ctl_ev_t e;
            cm.mode = pick & (1u << B_REC) ? CTL_MIRROR : CTL_GENERAL;   /* (both at once: mirror, as the controller's) */
            ctl_init(&ctl, cm.mode, cm_ch(), in.notes, in.buttons);   /* (held now: nothing till let go) */
            cm.on = 1;
            cm.hello_due = 1;
            cm.resets = plat_usb_resets();
            cm.usb_up = (uint8_t)plat_usb_up();
            cm.fast = (uint8_t)plat_fast(cm_fast);      /* (the keys at 1 kHz where the platform can) */
            memset(&e, 0, sizeof e);
            e.kind = CTL_EV_MODE;
            e.val = cm.mode;
            cm_log(&e);
            for (i = 0; i < LED_COUNT; i++) { plat_led((int)i, 0); cm.led[i] = 0; }
        }
    } else {
        int32_t steps[CTL_NENC];
        while (plat_midi_rx(&m)) {      /* the page's LEDs (mirror mode), as USB-MIDI packets for ctl.c */
            uint32_t pkt = (uint32_t)(m.m[0] >> 4) | (uint32_t)m.m[0] << 8 | (uint32_t)m.m[1] << 16 | (uint32_t)m.m[2] << 24;
            if (m.src == PLAT_MIDI_USB && ctl_midi_in(&ctl, pkt)) {
                cm.leds_in++;
                cm.dirty = 1;
            }
        }
        r = plat_usb_resets();
        up = plat_usb_up();
        if (r != cm.resets || up != cm.usb_up) {   /* USB down, or up again (replugged, the computer restarted): no
                                                 * page (mirror mode's LEDs, the page's, go dark); up, the hello */
            cm.resets = r;
            cm.usb_up = (uint8_t)up;
            cm.hello_due = (uint8_t)up;
            ctl.page = 0;
            cm.dirty = 1;
            if (ctl.mode == CTL_MIRROR)
                for (i = 0; i < CTL_NLED; i++) ctl.led[i] = CTL_OFF;
        }
        if (cm.hello_due && (!cm.t_hello || now - cm.t_hello >= 1000u)) {
            if (ctl.mode == CTL_GENERAL) ctl.all_off = 1;   /* (Note Offs may have been lost meanwhile) */
            if (cm_answer(CTL_EV_HELLO)) { cm.hello_due = 0; cm.t_hello = now | 1u; }
        }
        if (cm.answer_due && cm_answer(CTL_EV_ANSWER))
            cm.answer_due = 0;
        if (cm.fast) {
            cm_drain();                 /* (the keys went at 1 kHz: their SysEx and lines here) */
        } else {
            for (i = 0; i < CTL_NENC; i++) steps[i] = in.knob[i];   /* (K_* are ctl.c's knob ids) */
            ctl_tick(&ctl, now, in.notes, in.buttons, steps, in.master);
        }
        for (i = 0; i < LED_COUNT && i < CTL_NLED; i++) {
            uint8_t lv = ctl.led[i] == CTL_LIT ? 255 : ctl.led[i] == CTL_DIM ? 64 : 0;
            if (lv != cm.led[i]) { plat_led((int)i, lv); cm.led[i] = lv; }
        }
    }
    if (plat_screen_lost()) { g_invalidate(); cm.dirty = 1; }
    if (cm.dirty && now - cm.t_draw >= 33u) {           /* at most 30 frames a second, only when it changed */
        cm.t_draw = now;
        cm_draw();
    }
}
