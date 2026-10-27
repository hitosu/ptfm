/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The controller without hardware (MIDI CONTROLLER, specs/controller.md): the panel's state in, MIDI out; the
 * page's LED messages in. specs/controller.md is the protocol. Code in a header, as hal/'s: one includer,
 * ctlmode.c (its platform), and the host test, fw/tests/ctl_test.c; portable.
 *
 *   ctl_init(c, mode, ch, keys, buttons)  the mode and MIDI channel (0-15) picked at the start; the
 *                                       controls held now send nothing until let go
 *   ctl_tick(c, ms, keys, buttons, steps, master10)
 *                                       once a millisecond: what changed goes out through ctl_emit()
 *   ctl_midi_in(c, pkt)                 a USB-MIDI event packet from the host (mirror: the LEDs)
 *   ctl_answer(c, b)                    the handshake's answer; ctl_answered() then sends the state
 *
 * The includer defines ctl_emit(e): one message (its bytes, and what it was for the screen); it
 * returns 1 when the message was queued. Ids are the firmware's: keys 0-26 (F3-G5), buttons 0-13,
 * knobs 0-6 (SELECT, ALGORITHM, K1-K4, PRESETS); LED ids are the buttons', 14 + key, 41 PLAY's green. */
#ifndef CTL_H
#define CTL_H
#include <stdint.h>

enum { CTL_MIRROR = 0, CTL_GENERAL = 1 };
enum { CTL_NKEY = 27, CTL_NBTN = 14, CTL_NENC = 7, CTL_NLED = 42, CTL_LED_GREEN = 41 };
enum { CTL_OFF = 0, CTL_DIM = 1, CTL_LIT = 2 };
enum { CTL_BTN_OCTDN = 0, CTL_BTN_OCTUP = 1, CTL_BTN_PLAY = 12, CTL_ENC_PRESETS = 6 };
#define CTL_NOTE0 53u                    /* key 0 = F3 */
#define CTL_CC_BTN 102u                  /* a button: CC 102 + id, both modes */
#define CTL_CC_KNOB 20u                  /* mirror: a knob, CC 20 + id */
#define CTL_CC_GREEN 116u                /* mirror: PLAY's green LED */
#define CTL_CC_MASTER 7u
#define CTL_CC_ALL_OFF 123u
#define CTL_MASTER_HYST 8                /* MASTER moves this far (of 1023) before it sends */
#define CTL_VERSION 2u                   /* the protocol's version, in the answer (2: with the channel) */
#define CTL_ANSWER_LEN 10u               /* the answer's bytes (ctl_answer): size its buffers with this */
#define CTL_CH_MIRROR 15u                /* the modes' own channels (16, 1), unless the start screen set one */
#define CTL_CH_GENERAL 0u

/* what a message was, for the screen's log */
enum {
    CTL_EV_NOTE_ON, CTL_EV_NOTE_OFF, CTL_EV_BUTTON, CTL_EV_KNOB, CTL_EV_MASTER, CTL_EV_PROGRAM,
    CTL_EV_MMC, CTL_EV_ALL_OFF, CTL_EV_ANSWER, CTL_EV_HELLO, CTL_EV_MODE
};
typedef struct {
    uint8_t kind, id;    /* id: the key, button or knob */
    int16_t val;         /* NOTE_*: the note; BUTTON: 127 / 0; KNOB: mirror the clicks, general the value;
                            MASTER 0-127; PROGRAM 1-128; MMC 1 play, 0 stop; ANSWER, HELLO, MODE: the mode */
    uint8_t n;           /* the bytes (0: nothing sent, a line of the log only) */
    uint8_t b[CTL_ANSWER_LEN];           /* the longest message: the handshake's answer */
} ctl_ev_t;

static int ctl_emit(const ctl_ev_t *e);

typedef struct {
    uint8_t mode;
    uint8_t ch;                          /* the MIDI channel, 0-15 */
    uint32_t keys, btns;                 /* as sent: held */
    uint32_t ign_keys, ign_btns;         /* held at power-on: nothing until let go */
    uint8_t note[CTL_NKEY];              /* general: the note each held key's Note On sent */
    int8_t oct;                          /* general: -3..+3 */
    uint8_t val[CTL_NENC];               /* general: each knob's value; PRESETS: the program, 0-127 */
    uint32_t click_ms[CTL_NENC];         /* general: a knob's last click */
    int32_t pend[CTL_NENC];              /* mirror: clicks not sent yet (more than 63 in a millisecond) */
    uint8_t playing;                     /* general: MMC Play was sent last */
    int16_t master;                      /* the value sent last, -1 none */
    int16_t master_ref;                  /* MASTER (of 1023) when it was sent last or first read, -1 none */
    int16_t master_now;                  /* MASTER as last read */
    uint8_t led[CTL_NLED];               /* CTL_OFF / CTL_DIM / CTL_LIT */
    volatile uint8_t resync;             /* mirror: send the state at the next tick (after an answer) */
    volatile uint8_t all_off;            /* general: All Notes Off at the next tick (USB came up) */
    volatile uint8_t page;               /* mirror: a page has asked or sent an LED since USB came up */
} ctl_t;

static const uint8_t CTL_GENERAL_CC[CTL_NENC] = {1, 21, 74, 71, 73, 72, 0};   /* SELECT .. K4; PRESETS: PC */
static const uint8_t CTL_GENERAL_VAL0[CTL_NENC] = {0, 0, 64, 64, 64, 64, 0};

static void ctl_init(ctl_t *c, uint32_t mode, uint32_t ch, uint32_t keys, uint32_t btns)
{
    static const ctl_t ZERO;
    uint32_t i;
    *c = ZERO;
    c->mode = (uint8_t)(mode == CTL_GENERAL ? CTL_GENERAL : CTL_MIRROR);
    c->ch = (uint8_t)(ch & 15u);
    c->ign_keys = keys;
    c->ign_btns = btns;
    for (i = 0; i < CTL_NENC; i++)
        c->val[i] = CTL_GENERAL_VAL0[i];
    c->master = c->master_ref = c->master_now = -1;
}

static int ctl_msg(uint32_t kind, uint32_t id, int32_t val, uint32_t b0, uint32_t b1, uint32_t b2)
{
    ctl_ev_t e;
    e.kind = (uint8_t)kind;
    e.id = (uint8_t)id;
    e.val = (int16_t)val;
    e.n = (b0 & 0xF0u) == 0xC0u ? 2u : 3u;          /* Program Change has one data byte */
    e.b[0] = (uint8_t)b0;
    e.b[1] = (uint8_t)b1;
    e.b[2] = (uint8_t)b2;
    return ctl_emit(&e);
}

static uint32_t ctl_ch(const ctl_t *c) { return c->ch; }

static void ctl_key(ctl_t *c, uint32_t k, int down)
{
    uint32_t ch = ctl_ch(c), note;
    if (down) {
        int32_t n = (int32_t)(CTL_NOTE0 + k) + (c->mode == CTL_GENERAL ? 12 * c->oct : 0);
        note = (uint32_t)n;
        c->note[k] = (uint8_t)note;
        ctl_msg(CTL_EV_NOTE_ON, k, (int32_t)note, 0x90u | ch, note, c->mode == CTL_MIRROR ? 127u : 100u);
    } else {
        note = c->note[k];
        ctl_msg(CTL_EV_NOTE_OFF, k, (int32_t)note, 0x80u | ch, note, 0);
    }
}

static void ctl_mmc(ctl_t *c)                            /* general PLAY: Play, then Stop, in turn */
{
    ctl_ev_t e;
    c->playing = (uint8_t)!c->playing;
    e.kind = CTL_EV_MMC;
    e.id = CTL_BTN_PLAY;
    e.val = c->playing;
    e.n = 6;
    e.b[0] = 0xF0;
    e.b[1] = 0x7F;                                       /* universal real time */
    e.b[2] = 0x7F;                                       /* every device */
    e.b[3] = 0x06;                                       /* MMC command */
    e.b[4] = c->playing ? 0x02u : 0x01u;                 /* play / stop */
    e.b[5] = 0xF7;
    ctl_emit(&e);
}

static void ctl_button(ctl_t *c, uint32_t id, int down, uint32_t held)
{
    if (c->mode == CTL_GENERAL && (id == CTL_BTN_OCTDN || id == CTL_BTN_OCTUP)) {
        uint32_t other = id == CTL_BTN_OCTDN ? CTL_BTN_OCTUP : CTL_BTN_OCTDN;
        if (!down)
            return;
        if (held & (1u << other))
            c->oct = 0;                                  /* both: back to 0 */
        else if (id == CTL_BTN_OCTDN && c->oct > -3)
            c->oct--;
        else if (id == CTL_BTN_OCTUP && c->oct < 3)
            c->oct++;
        return;                                          /* (nothing sent) */
    }
    ctl_msg(CTL_EV_BUTTON, id, down ? 127 : 0, 0xB0u | ctl_ch(c), CTL_CC_BTN + id, down ? 127u : 0u);
    if (c->mode == CTL_GENERAL && id == CTL_BTN_PLAY && down)
        ctl_mmc(c);
}

static void ctl_master_send(ctl_t *c, int32_t m10)
{
    int32_t v = (m10 * 127 + 511) / 1023;
    c->master = (int16_t)v;
    c->master_ref = (int16_t)m10;
    ctl_msg(CTL_EV_MASTER, 0, v, 0xB0u | ctl_ch(c), CTL_CC_MASTER, (uint32_t)v);
}

static void ctl_master(ctl_t *c, int32_t m10)
{
    int32_t v, d;
    if (m10 < 0)
        return;
    if (m10 > 1023)
        m10 = 1023;
    c->master_now = (int16_t)m10;
    v = (m10 * 127 + 511) / 1023;
    if (c->master_ref < 0) {                             /* first reading: where it is, nothing sent */
        c->master_ref = (int16_t)m10;
        c->master = (int16_t)v;                          /* (at an end too: no "end" send right after) */
        return;
    }
    d = m10 - c->master_ref;
    if (v != c->master && (d >= CTL_MASTER_HYST || d <= -CTL_MASTER_HYST || v == 0 || v == 127))
        ctl_master_send(c, m10);
}

static void ctl_knob(ctl_t *c, uint32_t e, int32_t s, uint32_t now)
{
    if (c->mode == CTL_MIRROR) {
        int32_t k;
        c->pend[e] += s;
        k = c->pend[e] > 63 ? 63 : c->pend[e] < -63 ? -63 : c->pend[e];
        if (!k)
            return;
        c->pend[e] -= k;                                 /* (the rest goes next millisecond) */
        ctl_msg(CTL_EV_KNOB, e, k, 0xB0u | ctl_ch(c), CTL_CC_KNOB + e, (uint32_t)k & 0x7Fu);
        return;
    }
    if (!s)
        return;
    if (e == CTL_ENC_PRESETS) {                          /* the program, one a click */
        int32_t p = (int32_t)c->val[e] + s;
        p = p < 0 ? 0 : p > 127 ? 127 : p;
        if (p != c->val[e]) {
            c->val[e] = (uint8_t)p;
            ctl_msg(CTL_EV_PROGRAM, e, p + 1, 0xC0u | ctl_ch(c), (uint32_t)p, 0);
        }
        return;
    }
    {
        uint32_t dt = now - c->click_ms[e];
        int32_t m = (s > 1 || s < -1 || dt < 20u) ? 4 : dt < 40u ? 2 : 1, v;
        c->click_ms[e] = now;
        v = (int32_t)c->val[e] + s * m;
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (v != c->val[e]) {
            c->val[e] = (uint8_t)v;
            ctl_msg(CTL_EV_KNOB, e, v, 0xB0u | ctl_ch(c), CTL_GENERAL_CC[e], (uint32_t)v);
        }
    }
}

/* general mode lights its own LEDs; mirror mode's come from the page (ctl_midi_in) */
static void ctl_leds_local(ctl_t *c, uint32_t now)
{
    uint32_t i, a = (uint32_t)(c->oct < 0 ? -c->oct : c->oct);
    uint8_t oct = a == 0 ? CTL_OFF : a == 1 ? CTL_DIM : a == 2 ? CTL_LIT : ((now / 400u) & 1u) ? CTL_LIT : CTL_OFF;
    for (i = 0; i < CTL_NKEY; i++)
        c->led[14 + i] = (c->keys >> i) & 1u ? CTL_LIT : CTL_OFF;
    for (i = 0; i < CTL_NBTN; i++)
        c->led[i] = (c->btns >> i) & 1u ? CTL_LIT : CTL_OFF;
    c->led[CTL_BTN_OCTDN] = c->oct < 0 ? oct : CTL_OFF;
    c->led[CTL_BTN_OCTUP] = c->oct > 0 ? oct : CTL_OFF;
    c->led[CTL_LED_GREEN] = c->playing ? CTL_LIT : CTL_OFF;
}

static void ctl_tick(ctl_t *c, uint32_t now, uint32_t keys, uint32_t btns, const int32_t *steps, int32_t m10)
{
    uint32_t i, d;
    c->ign_keys &= keys;                                 /* a control let go is free again */
    c->ign_btns &= btns;
    keys &= ~c->ign_keys;
    btns &= ~c->ign_btns;
    if (c->all_off) {
        c->all_off = 0;
        ctl_msg(CTL_EV_ALL_OFF, 0, 0, 0xB0u | ctl_ch(c), CTL_CC_ALL_OFF, 0);
    }
    if (c->resync) {                                     /* after an answer: the panel as it is */
        c->resync = 0;
        if (c->mode == CTL_MIRROR) {
            if (c->master_now >= 0)
                ctl_master_send(c, c->master_now);
            for (i = 0; i < CTL_NKEY; i++)
                if ((c->keys >> i) & 1u)
                    ctl_msg(CTL_EV_NOTE_ON, i, (int32_t)c->note[i], 0x90u | ctl_ch(c), c->note[i], 127);
            for (i = 0; i < CTL_NBTN; i++)
                if ((c->btns >> i) & 1u)
                    ctl_msg(CTL_EV_BUTTON, i, 127, 0xB0u | ctl_ch(c), CTL_CC_BTN + i, 127);
        }
    }
    d = keys ^ c->keys;
    for (i = 0; d; i++, d >>= 1)
        if (d & 1u)
            ctl_key(c, i, (keys >> i) & 1u);
    c->keys = keys;
    d = btns ^ c->btns;
    for (i = 0; d; i++, d >>= 1)
        if (d & 1u)
            ctl_button(c, i, (btns >> i) & 1u, btns);
    c->btns = btns;
    for (i = 0; i < CTL_NENC; i++)
        ctl_knob(c, i, steps ? steps[i] : 0, now);
    ctl_master(c, m10);
    if (c->mode == CTL_GENERAL)
        ctl_leds_local(c, now);
}

/* an LED level from the page: 0 off, 1-63 dim, 64-127 lit */
static uint8_t ctl_level(uint32_t v) { return v == 0 ? CTL_OFF : v < 64u ? CTL_DIM : CTL_LIT; }

/* a USB-MIDI event packet from the host; returns 1 for an LED message (in mirror mode, on its channel) */
static int ctl_midi_in(ctl_t *c, uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, id;
    uint8_t lv;
    if (c->mode != CTL_MIRROR || (st & 15u) != c->ch)
        return 0;
    st &= 0xF0u;
    if ((st == 0x90u || st == 0x80u) && d1 >= CTL_NOTE0 && d1 < CTL_NOTE0 + CTL_NKEY) {
        id = 14u + d1 - CTL_NOTE0;
        lv = st == 0x80u ? CTL_OFF : ctl_level(d2);
    } else if (st == 0xB0u && d1 >= CTL_CC_BTN && d1 <= CTL_CC_GREEN) {
        id = d1 == CTL_CC_GREEN ? (uint32_t)CTL_LED_GREEN : d1 - CTL_CC_BTN;
        lv = ctl_level(d2);
    } else {
        return 0;
    }
    c->page = 1;
    c->led[id] = lv;
    return 1;
}

/* the handshake: the page's question is F0 7D 50 54 43 01 F7 (p: the bytes between F0 and F7) */
static int ctl_is_ask(const uint8_t *p, uint32_t n)
{
    return n == 5u && p[0] == 0x7Du && p[1] == 0x50u && p[2] == 0x54u && p[3] == 0x43u && p[4] == 0x01u;
}

/* the answer, F0 7D 50 54 43 02 <mode> <version> <channel 0-15> F7, into b; returns CTL_ANSWER_LEN.
 * Callers size b with CTL_ANSWER_LEN (a 9-byte buffer from version 1 overran the stack on the FM-1) */
static uint32_t ctl_answer(const ctl_t *c, uint8_t b[CTL_ANSWER_LEN])
{
    static const uint8_t A[CTL_ANSWER_LEN] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, 0, CTL_VERSION, 0, 0xF7};
    uint32_t i;
    for (i = 0; i < CTL_ANSWER_LEN; i++)
        b[i] = A[i];
    b[6] = c->mode;
    b[8] = c->ch;
    return CTL_ANSWER_LEN;
}

/* the answer went out: mirror mode sends the panel's state next (the page let go of everything) */
static void ctl_answered(ctl_t *c)
{
    if (c->mode == CTL_MIRROR)
        c->resync = 1;
}

#endif
