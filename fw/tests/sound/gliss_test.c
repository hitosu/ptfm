/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* E3x GLISSANDO (specs/files/commands.md#tracker-effects): C-4, then C-5 with a slide to it (3xx): without
 * E3x the track's pitch moves smoothly; with E31 it is heard in whole semitones while it slides, and lands on
 * C-5 either way.
 *   gliss_test */
#include <stdio.h>
#include <string.h>
#include "../../core/play/player.c"

void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);

static uint8_t cart[10 + 112], pat[2][64], song[2][100];

static int run(int gliss, int *steps_off, int *last)
{
    int h, smooth = 0;
    uint8_t *p = pat[gliss], *s = song[gliss];
    int at = 5;
    static int16_t half[512];
    memset(p, 0, 64);
    p[0] = 1;
    p[1] = 15;
    p[2] = 1;
    p[3] = 1;
    p[at++] = 0x80 | 0x60 | 0x10 | (gliss ? 0x08 : 0);   /* row 0: C-4, 01:00, volume, (E31) */
    p[at++] = 60;
    p[at++] = 1;
    p[at++] = 0;
    p[at++] = 100;
    if (gliss) {
        p[at++] = 0x0E;
        p[at++] = 0x31;
    }
    p[at++] = 0x80 | 0x08 | 0;           /* row 1: C-5 with 3xx: slide to it */
    p[at++] = 72;
    p[at++] = 0x03;
    p[at++] = 0x0E;                      /* (14 x 4 / 64: 7/8 of a semitone a tick, the octave in 14 of rows 1-3's 15) */
    p[at++] = 0x08 | 0;                  /* rows 2-3: 300 */
    p[at++] = 0x03;
    p[at++] = 0x00;
    p[at++] = 0x08 | 0;
    p[at++] = 0x03;
    p[at++] = 0x00;
    p[at++] = 0;
    disk_put(REC_PATTERN, (uint8_t)(0x40 + gliss), 0, p, (uint32_t)at);
    memset(s, 0, 100);
    s[12] = 125;
    s[14] = 6;
    s[15] = 4;
    s[16] = 50;
    s[17] = 2 | 1;                       /* linear slides, STOP at the end */
    s[19] = 64;
    s[20] = 0;
    s[21] = (uint8_t)(0x40 + gliss);
    s[22] = 0;
    s[24] = 0xF7;
    memset(s + 25, 0xFF, 5);
    s[30] = 0x80;
    disk_put(REC_SONG, (uint8_t)(0x40 + gliss), 0, s, 23 + 64 + 8);
    player_init();
    player_play_song((uint8_t)(0x40 + gliss), 0, 0);
    *steps_off = 0;
    for (h = 0; h < 120; h++) {          /* 0.7 s: the slide runs over rows 1-3 */
        int b;
        player_audio(half, 256);
        b = pl_ch[0].bend;
        if (b % 64)
            smooth++;
        if (b % 64 && gliss)
            (*steps_off)++;
        *last = b + (int)pl_ch[0].note * 64;
    }
    return smooth;
}

int main(void)
{
    int off0, off1, last0, last1, s0, s1, fails = 0;
    memset(cart, 0, sizeof cart);
    cart[9] = 1;
    disk_put(REC_DX7, 1, 0, cart, sizeof cart);
    s0 = run(0, &off0, &last0);
    s1 = run(1, &off1, &last1);
    printf("without E3x: %d halves between semitones; with E31: %d; both end at %d / %d (1/64 semitones)\n", s0, s1,
           last0, last1);
    if (s0 < 5) {
        printf("glissando: the plain slide was not smooth\n");
        fails++;
    }
    if (off1) {
        printf("glissando: E31's slide was heard between semitones\n");
        fails++;
    }
    if (last0 != 72 * 64 || last1 != 72 * 64) {
        printf("glissando: the slides did not land on C-5\n");
        fails++;
    }
    printf("glissando: %s\n", fails ? "FAILED" : "as FastTracker 2 plays it");
    return fails != 0;
}
