/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of fw/core/ctl.h, MIDI CONTROLLER's protocol: its table (fw/tests/vectors.json, shared with the page's
 * test, tests/controller_page.js), then scripted sessions: what each mode sends, byte for byte; the page's LEDs.
 *   fw/tests/run_ctl.sh builds it with the sanitizers. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/ctl.h"
#include "vectors.h"

static ctl_ev_t got[256];
static int ngot, fails, checks;

static int ctl_emit(const ctl_ev_t *e)
{
    if (ngot < 256)
        got[ngot++] = *e;
    return 1;
}

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            fails++;                                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static int is_msg(const ctl_ev_t *e, int n, int b0, int b1, int b2)
{
    return e->n == n && e->b[0] == b0 && e->b[1] == b1 && (n < 3 || e->b[2] == b2);
}

static void show(const char *what)
{
    int i, k;
    printf("  %s: %d message(s):", what, ngot);
    for (i = 0; i < ngot; i++) {
        printf(" [");
        for (k = 0; k < got[i].n; k++)
            printf("%s%02X", k ? " " : "", got[i].b[k]);
        printf("]");
    }
    printf("\n");
}

/* the panel's state for a tick */
static uint32_t keys, btns;
static int32_t steps[CTL_NENC], m10 = -1;
static uint32_t now = 1000;

static void tick(ctl_t *c)
{
    ngot = 0;
    ctl_tick(c, now, keys, btns, steps, m10);
    memset(steps, 0, sizeof steps);
}

static void reset_ch(ctl_t *c, uint32_t mode, uint32_t ch)
{
    keys = btns = 0;
    m10 = -1;
    memset(steps, 0, sizeof steps);
    ctl_init(c, mode, ch, 0, 0);
    tick(c);
}

static void reset(ctl_t *c, uint32_t mode)   /* on the mode's own channel */
{
    reset_ch(c, mode, mode == CTL_MIRROR ? CTL_CH_MIRROR : CTL_CH_GENERAL);
}

static int32_t m10_for(int v) { return (v * 1023 + 63) / 127; }

/* a table's events (mirror mode, channel ch) give its bytes; its LED messages light its LEDs */
static void vectors(const vec_out_t *vo, uint32_t no, const vec_led_t *vl, uint32_t nl, uint32_t ch)
{
    ctl_t c;
    uint32_t i;
    for (i = 0; i < no; i++) {
        const vec_out_t *v = &vo[i];
        reset_ch(&c, CTL_MIRROR, ch);
        if (v->kind == 0 || v->kind == 1) {
            uint32_t *m = v->kind == 0 ? &keys : &btns;
            if (!v->val) {                             /* a release: pressed first */
                *m = 1u << v->id;
                tick(&c);
            }
            *m = v->val ? 1u << v->id : 0;
        } else if (v->kind == 2) {
            steps[v->id] = v->val;
        } else {
            m10 = v->val < 64 ? 1023 : 0;              /* where MASTER was at power-on */
            tick(&c);
            m10 = m10_for(v->val);
        }
        tick(&c);
        CHECK(ngot == 1 && is_msg(&got[0], 3, v->midi[0], v->midi[1], v->midi[2]),
              "channel %u vector %u (kind %d id %d val %d): want %02X %02X %02X", ch + 1, i, v->kind, v->id,
              v->val, v->midi[0], v->midi[1], v->midi[2]);
        if (ngot != 1)
            show("got");
    }
    for (i = 0; i < nl; i++) {
        const vec_led_t *v = &vl[i];
        uint32_t pkt = (uint32_t)(v->midi[0] >> 4) | (uint32_t)v->midi[0] << 8 | (uint32_t)v->midi[1] << 16 |
                       (uint32_t)v->midi[2] << 24;
        reset_ch(&c, CTL_MIRROR, ch);
        c.led[v->led] = (uint8_t)(v->state == CTL_LIT ? CTL_OFF : CTL_LIT);   /* something else before */
        CHECK(ctl_midi_in(&c, pkt) == 1 && c.led[v->led] == v->state && c.page,
              "channel %u LED vector %u: led %d want state %d, got %d", ch + 1, i, v->led, v->state,
              c.led[v->led]);
    }
}

static void test_vectors(void)
{
    ctl_t c;
    uint8_t b[16];
    uint32_t i, n;
    vectors(VEC_OUT, sizeof VEC_OUT / sizeof VEC_OUT[0], VEC_LED, sizeof VEC_LED / sizeof VEC_LED[0], 15);
    vectors(VEC_OUT5, sizeof VEC_OUT5 / sizeof VEC_OUT5[0], VEC_LED5, sizeof VEC_LED5 / sizeof VEC_LED5[0], 4);
    CHECK(ctl_is_ask(VEC_ASK + 1, sizeof VEC_ASK - 2), "the page's question is recognised");
    for (i = 0; i < 3u; i++) {
        reset_ch(&c, i == 1u ? CTL_GENERAL : CTL_MIRROR, i == 0u ? 15u : i == 1u ? 0u : 4u);
        n = ctl_answer(&c, b);
        CHECK(n == 10 && !memcmp(b, VEC_ANSWER[i], 10), "the answer %u (mode, version 2, channel)", i);
    }
    reset_ch(&c, CTL_MIRROR, 4);                        /* channel 5: an LED on 16 is not ours */
    CHECK(!ctl_midi_in(&c, 0x9u | 0x9Fu << 8 | 53u << 16 | 127u << 24) && !c.page, "channel 5: 16's LED ignored");
    reset_ch(&c, CTL_GENERAL, 9);                       /* general on channel 10 */
    keys = 1u;
    btns = 1u << 3;
    tick(&c);
    CHECK(ngot == 2 && is_msg(&got[0], 3, 0x99, 53, 100) && is_msg(&got[1], 3, 0xB9, 105, 127),
          "general on channel 10: notes and CCs there");
    steps[6] = 1;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 2, 0xC9, 1, 0), "general on channel 10: Program Change there");
}

static void test_mirror(void)
{
    ctl_t c;
    static const uint8_t NOT_ASK[][5] = {{0x7D, 0x50, 0x54, 0x43, 0x02}, {0x7E, 0x50, 0x54, 0x43, 0x01},
                                         {0x00, 0x59, 0x11, 0x00, 0x00}};
    uint32_t i;
    /* controls held at power-on send nothing until let go, then work */
    ctl_init(&c, CTL_MIRROR, CTL_CH_MIRROR, 1u << 3, 3u);
    keys = 1u << 3;
    btns = 3u;
    m10 = -1;
    tick(&c);
    CHECK(ngot == 0, "held at power-on: nothing sent");
    keys = 0;
    btns = 1u;
    tick(&c);
    CHECK(ngot == 0, "let go after power-on: nothing sent");
    keys = 1u << 3;
    btns = 3u;
    tick(&c);
    CHECK(ngot == 2 && is_msg(&got[0], 3, 0x9F, 56, 127) && is_msg(&got[1], 3, 0xBF, 103, 127),
          "pressed again: the key and OCT+ (OCT- is still held from power-on)");
    /* a chord in one millisecond: every note, in key order */
    reset(&c, CTL_MIRROR);
    keys = (1u << 0) | (1u << 4) | (1u << 7);
    tick(&c);
    CHECK(ngot == 3 && got[0].b[1] == 53 && got[1].b[1] == 57 && got[2].b[1] == 60, "a chord: three Note Ons");
    /* a knob: more than 63 clicks go over two milliseconds */
    reset(&c, CTL_MIRROR);
    steps[2] = 100;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xBF, 22, 63), "100 clicks: 63 first");
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xBF, 22, 37), "then the 37 left");
    tick(&c);
    CHECK(ngot == 0, "then nothing");
    steps[2] = -70;
    tick(&c);
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xBF, 22, 121), "-70: -63, then -7 (121)");
    /* MASTER: a first reading sends nothing; noise under the threshold sends nothing; the ends always */
    reset(&c, CTL_MIRROR);
    m10 = 500;
    tick(&c);
    CHECK(ngot == 0, "MASTER's first reading: nothing");
    m10 = 503;
    tick(&c);
    CHECK(ngot == 0, "MASTER moved 3: nothing");
    m10 = 510;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xBF, 7, 63), "MASTER moved 10: CC 7 63");
    m10 = 515;
    tick(&c);
    CHECK(ngot == 0, "MASTER 5 more (64 in 7 bits): nothing");
    m10 = 1020;
    tick(&c);
    m10 = 1023;
    tick(&c);
    CHECK(ngot == 0, "MASTER at the top: 127 already sent");
    m10 = 4;
    tick(&c);
    CHECK(ngot == 1 && got[0].b[2] == 0, "MASTER near the bottom: 0");
    m10 = 5;
    tick(&c);
    m10 = 4;
    tick(&c);
    CHECK(ngot == 0, "MASTER's noise at the bottom: nothing");
    {                                                   /* parked at an end at power-on: nothing either */
        static const int32_t ENDS[] = {0, 3, 1020, 1023};
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            reset(&c, k & 1u);
            m10 = ENDS[k];
            tick(&c);
            m10 = ENDS[k] + (k < 2u ? 1 : -1);
            tick(&c);
            tick(&c);
            CHECK(ngot == 0, "MASTER parked at %d at power-on: nothing sent", (int)ENDS[k]);
        }
    }
    /* the answer: the panel's state follows (MASTER, then what is held) */
    reset(&c, CTL_MIRROR);
    m10 = 773;
    keys = 1u << 5;
    btns = 1u << 9;
    tick(&c);
    ctl_answered(&c);
    tick(&c);
    CHECK(ngot == 3 && is_msg(&got[0], 3, 0xBF, 7, 96) && is_msg(&got[1], 3, 0x9F, 58, 127) &&
              is_msg(&got[2], 3, 0xBF, 111, 127),
          "after an answer: MASTER, the held key, the held button");
    if (ngot != 3)
        show("resync");
    /* LED messages: other channels and numbers are not LEDs; a Note Off is off */
    reset(&c, CTL_MIRROR);
    CHECK(!ctl_midi_in(&c, 0x9u | 0x90u << 8 | 60u << 16 | 127u << 24) && !c.page, "channel 1: not an LED");
    CHECK(!ctl_midi_in(&c, 0x9u | 0x9Fu << 8 | 52u << 16 | 127u << 24), "note 52: not a key");
    CHECK(!ctl_midi_in(&c, 0xBu | 0xBFu << 8 | 117u << 16 | 127u << 24), "CC 117: not an LED");
    for (i = 0; i < sizeof NOT_ASK / sizeof NOT_ASK[0]; i++)
        CHECK(!ctl_is_ask(NOT_ASK[i], 5), "not the question %u", i);
    CHECK(c.mode == CTL_MIRROR && c.led[14] == CTL_OFF, "no LED lit by itself");
}

static void test_general(void)
{
    ctl_t c;
    reset(&c, CTL_GENERAL);
    keys = 1u;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0x90, 53, 100), "a key: channel 1, velocity 100");
    keys = 0;
    btns = 1u << 1;                                     /* OCT+ */
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0x80, 53, 0) && c.oct == 1, "key up, OCT+: one octave up, nothing sent");
    btns = 0;
    keys = 1u;
    tick(&c);
    CHECK(ngot == 1 && got[0].b[1] == 65, "the key an octave up: 65");
    btns = 1u;                                          /* OCT- while the key is held */
    tick(&c);
    keys = 0;
    btns = 0;
    tick(&c);
    CHECK(c.oct == 0 && ngot == 1 && is_msg(&got[0], 3, 0x80, 65, 0), "Note Off to the note it started (65)");
    btns = 1u << 1;
    tick(&c);
    btns = 0;
    tick(&c);
    btns = 1u << 1;
    tick(&c);
    btns = 0;
    tick(&c);
    btns = 1u << 1;
    tick(&c);
    btns = 0;
    tick(&c);
    btns = 1u << 1;
    tick(&c);
    btns = 0;
    tick(&c);
    CHECK(c.oct == 3, "OCT+ stops at +3 (%d)", c.oct);
    CHECK(c.led[1] == CTL_LIT || c.led[1] == CTL_OFF, "+3: OCT+ blinks");
    now += 400;
    tick(&c);
    {
        uint8_t a = c.led[1];
        now += 400;
        tick(&c);
        CHECK(a != c.led[1], "+3: OCT+ blinks (%d then %d)", a, c.led[1]);
    }
    btns = 1u << 1;
    tick(&c);
    btns = 3u;                                          /* both: back to 0 */
    tick(&c);
    btns = 0;
    tick(&c);
    CHECK(c.oct == 0 && c.led[0] == CTL_OFF && c.led[1] == CTL_OFF, "OCT- with OCT+: back to 0");
    btns = 1u;
    tick(&c);
    btns = 0;
    tick(&c);
    CHECK(c.oct == -1 && c.led[0] == CTL_DIM, "-1: OCT- dim");
    btns = 1u;
    tick(&c);
    btns = 0;
    tick(&c);
    CHECK(c.oct == -2 && c.led[0] == CTL_LIT, "-2: OCT- lit");
    /* knobs: values kept by the FM-1, faster on fast turns */
    reset(&c, CTL_GENERAL);
    now += 1000;
    steps[2] = 1;                                       /* K1, slow */
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 74, 65), "K1 a click: CC 74 65");
    now += 30;
    steps[2] = 1;
    tick(&c);
    CHECK(ngot == 1 && got[0].b[2] == 67, "30 ms later: 2 (67)");
    now += 10;
    steps[2] = 1;
    tick(&c);
    CHECK(ngot == 1 && got[0].b[2] == 71, "10 ms later: 4 (71)");
    now += 1;
    steps[2] = 30;
    tick(&c);
    CHECK(ngot == 1 && got[0].b[2] == 127, "and up to 127");
    now += 1000;
    steps[2] = 1;
    tick(&c);
    CHECK(ngot == 0, "past 127: nothing");
    now += 1000;
    steps[0] = -1;
    tick(&c);
    CHECK(ngot == 0, "SELECT under 0: nothing");
    steps[0] = 1;
    now += 1000;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 1, 1), "SELECT: CC 1");
    now += 1000;
    steps[1] = 1;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 21, 1), "ALGORITHM: CC 21");
    steps[5] = -1;
    now += 1000;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 72, 63), "K4: CC 72");
    steps[6] = -1;
    tick(&c);
    CHECK(ngot == 0, "PRESETS under 1: nothing");
    steps[6] = 3;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 2, 0xC0, 3, 0) && got[0].val == 4, "PRESETS 3 clicks: program 4 (C0 03)");
    /* PLAY: its CC, and MMC Play / Stop in turn */
    reset(&c, CTL_GENERAL);
    btns = 1u << 12;
    tick(&c);
    CHECK(ngot == 2 && is_msg(&got[0], 3, 0xB0, 114, 127) && got[1].n == 6 &&
              !memcmp(got[1].b, "\xF0\x7F\x7F\x06\x02\xF7", 6) && c.led[CTL_LED_GREEN] == CTL_LIT,
          "PLAY: CC 114 127 and MMC Play; green");
    btns = 0;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 114, 0), "PLAY up: CC 114 0");
    btns = 1u << 12;
    tick(&c);
    CHECK(ngot == 2 && !memcmp(got[1].b, "\xF0\x7F\x7F\x06\x01\xF7", 6) && c.led[CTL_LED_GREEN] == CTL_OFF,
          "PLAY again: MMC Stop; green off");
    btns = 1u << 13;
    tick(&c);
    CHECK(ngot == 2 && is_msg(&got[1], 3, 0xB0, 115, 127) && c.led[13] == CTL_LIT, "REC: CC 115, lit while held");
    btns = 0;
    keys = 1u << 26;
    tick(&c);
    CHECK(c.led[40] == CTL_LIT, "a key: lit while held");
    /* USB came up: All Notes Off; the page's LEDs are ignored */
    c.all_off = 1;
    tick(&c);
    CHECK(ngot == 1 && is_msg(&got[0], 3, 0xB0, 123, 0), "All Notes Off on channel 1");
    CHECK(!ctl_midi_in(&c, 0x9u | 0x9Fu << 8 | 53u << 16 | 127u << 24) && !c.page, "general: LED messages ignored");
    ctl_answered(&c);
    tick(&c);
    CHECK(ngot == 0, "general: nothing follows an answer");
}

int main(void)
{
    test_vectors();
    test_mirror();
    test_general();
    printf("ctl_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
