/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The engines' reference sounds (specs/files/instruments.md#level-and-the-engines-reference): each engine's
 * plain sound, held a second, its loudest 300 ms at -20 dB RMS of the 16-bit full scale at the voice's
 * output; then the same instruments as records (cartridges 01-04, LEVEL 0) played on the keys through the
 * player: voices enter the mix at half, so -26 dB there. The DX7's and the TB-303's pitch too.
 *   ref_test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/play/player.h"
#include "../../core/store/store.h"
#include "../../core/eng/dx7/dx7.h"
#include "../../core/eng/juno/juno.h"
#include "../../core/eng/tb303/tb303.h"
#include "../../core/eng/samp/samp.h"

void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);
static int fails;

/* bits written as the records keep them: from bit 0 upward, a field's own bits least significant first */
typedef struct { uint8_t *p; uint32_t pos; } bw_t;
static void put(bw_t *w, uint32_t v, int n)
{
    int i;
    for (i = 0; i < n; i++, w->pos++)
        if (v >> i & 1u)
            w->p[w->pos >> 3] |= (uint8_t)(1u << (w->pos & 7u));
}
static void name(bw_t *w, const char *s)  /* 10 characters, 6 bits each */
{
    int i;
    for (i = 0; i < 10; i++) {
        char c = *s ? *s++ : ' ';
        put(w, c >= 'A' && c <= 'Z' ? (uint32_t)(c - 'A' + 1) : c >= '0' && c <= '9' ? (uint32_t)(c - '0' + 27) : 0u, 6);
    }
}

static double loudest(const int32_t *x, int n)   /* the loudest 300 ms, RMS in dB of the 16-bit full scale */
{
    int w = 13230, i;
    double s = 0, best = 0;
    for (i = 0; i < n; i++) {
        s += (double)x[i] * x[i];
        if (i >= w)
            s -= (double)x[i - w] * x[i - w];
        if (i >= w - 1 && s > best)
            best = s;
    }
    return 10 * log10(best / w / (32768.0 * 32768.0) + 1e-20);
}

static double pitch(const int32_t *x, int a, int n)   /* zero crossings upward, Hz */
{
    int i, c = 0, first = -1, last = -1;
    for (i = a + 1; i < a + n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            if (first < 0)
                first = i;
            last = i;
            c++;
        }
    return c > 1 ? (c - 1) * 44100.0 / (last - first) : 0;
}

static void check(const char *what, double v, double want, double tol)
{
    int ok = fabs(v - want) <= tol;
    printf("%-34s %8.2f (want %.2f +- %.2f) %s\n", what, v, want, tol, ok ? "ok" : "WRONG");
    fails += !ok;
}

#define N 44100
static int32_t buf[N];

static uint8_t dx7_slot[112], juno_slot[28], tb_slot[24], samp_cart[11 + 40 + 16], fus_cart[10];
static uint8_t carts[4][10 + 112];
static uint8_t ptr[4 + 4 + 2 + 1 + 100];

static void make_records(void)
{
    bw_t w;
    int op, i;
    /* DX7: algorithm 32, operator 1 alone at OUTPUT LEVEL 99, ratio 1.00, its EG rates 99, levels 99 99 99 0 */
    memset(dx7_slot, 0, sizeof dx7_slot);
    w.p = dx7_slot;
    w.pos = 0;
    for (op = 6; op >= 1; op--) {
        for (i = 0; i < 4; i++)
            put(&w, 99, 7);
        put(&w, 99, 7), put(&w, 99, 7), put(&w, 99, 7), put(&w, 0, 7);
        put(&w, 39, 7), put(&w, 0, 7), put(&w, 0, 7), put(&w, 0, 2), put(&w, 0, 2), put(&w, 0, 3);
        put(&w, 0, 2), put(&w, 0, 3), put(&w, op == 1 ? 99u : 0u, 7), put(&w, 0, 1), put(&w, 1, 5), put(&w, 0, 7), put(&w, 7, 4);
    }
    for (i = 0; i < 4; i++)
        put(&w, 99, 7);
    for (i = 0; i < 4; i++)
        put(&w, 50, 7);
    put(&w, 31, 5), put(&w, 0, 3), put(&w, 1, 1), put(&w, 35, 7), put(&w, 0, 7), put(&w, 0, 7), put(&w, 0, 7);
    put(&w, 0, 1), put(&w, 0, 3), put(&w, 0, 3), put(&w, 24, 6);
    name(&w, "REF");
    put(&w, 0, 8);
    /* Juno-106: SAW alone, 8'; VCF FREQ 127, RES 0, ENV 0, LFO 0, KYBD 0; HPF 1; VCA ENV, LEVEL 127; A 0 D 0 S 127 R 0 */
    memset(juno_slot, 0, sizeof juno_slot);
    w.p = juno_slot;
    w.pos = 0;
    {
        static const uint8_t SL[16] = {0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 127, 0, 0, 127, 0, 0};
        for (i = 0; i < 16; i++)
            put(&w, SL[i], 7);
    }
    put(&w, 1, 2), put(&w, 0, 1), put(&w, 1, 1), put(&w, 0, 2), put(&w, 0, 1), put(&w, 0, 1), put(&w, 0, 1), put(&w, 1, 2);
    name(&w, "REF");
    put(&w, 0, 8);
    /* TB-303: WAVE 0, CUTOFF 127, RESONANCE 0, ENV MOD 0, DECAY 64, ACCENT 0 */
    memset(tb_slot, 0, sizeof tb_slot);
    w.p = tb_slot;
    w.pos = 0;
    put(&w, 0, 7), put(&w, 127, 7), put(&w, 0, 7), put(&w, 0, 7), put(&w, 64, 7), put(&w, 0, 7);
    name(&w, "REF");
    put(&w, 0, 8);
    /* the sampler: a full-scale 8-bit sine, 441 Hz at 44.1 kHz (100 samples), looped whole, at its BASE NOTE;
     * TUNE 0, VOLUME 64, the envelope's rates 99 and levels 99 99 99 0. Its audio: PTR 8-bit, one raw block */
    memset(samp_cart, 0, sizeof samp_cart);
    samp_cart[9] = 1;
    samp_cart[10] = 1;
    w.p = samp_cart + 11;
    w.pos = 0;
    put(&w, 0, 6), put(&w, 0, 20), put(&w, 100, 20), put(&w, 0, 20), put(&w, 100, 20), put(&w, 1, 2);
    put(&w, 60, 7), put(&w, 0x80, 8), put(&w, 0, 8), put(&w, 64, 7);
    for (i = 0; i < 4; i++)
        put(&w, 99, 7);
    put(&w, 99, 7), put(&w, 99, 7), put(&w, 99, 7), put(&w, 0, 7);
    name(&w, "SINE");
    w.p = samp_cart + 11 + 40;
    w.pos = 0;
    put(&w, 100, 20), put(&w, 44100, 18), put(&w, 1, 4);
    name(&w, "SINE");
    ptr[0] = 1;
    ptr[1] = 100;
    ptr[4] = 10;                         /* the group's offset: after the 4 + 4 + 2 bytes of the head */
    ptr[8] = 101;                        /* the block's coded length */
    ptr[10] = 7;                         /* raw */
    for (i = 0; i < 100; i++)
        ptr[11 + i] = (uint8_t)(int8_t)lround(127 * sin(2 * M_PI * i / 100.0));
    for (i = 0; i < 4; i++) {
        static const uint8_t *SLOTS[4] = {dx7_slot, juno_slot, tb_slot, 0};
        static const int SZ[4] = {112, 28, 24, 0};
        memset(carts[i], 0, sizeof carts[i]);
        carts[i][9] = 1;
        if (SLOTS[i])
            memcpy(carts[i] + 10, SLOTS[i], (size_t)SZ[i]);
    }
    disk_put(REC_DX7, 1, 0, carts[0], 10 + 112);
    disk_put(REC_JUNO, 2, 0, carts[1], 10 + 28);
    disk_put(REC_TB303, 3, 0, carts[2], 10 + 24);
    disk_put(REC_SAMPLER, 4, 0, samp_cart, sizeof samp_cart);
    disk_put(REC_AUDIO, 4, 0, ptr, sizeof ptr);
    (void)fus_cart;
}

int main(void)
{
    static dx7_voice_t dv;
    static juno_voice_t jv;
    static tb_voice_t tv;
    static samp_voice_t sv;
    uint8_t vced[156], jp[18], k[6];
    int b, i;
    make_records();
    player_init();
    {   /* the engines on their own */
        extern void rec_dx7_vced(const uint8_t *, uint8_t *);
        extern void rec_juno_patch(const uint8_t *, uint8_t *);
        extern void rec_tb_knobs(const uint8_t *, uint8_t *);
        samp_t s;
        rec_dx7_vced(dx7_slot, vced);
        memset(buf, 0, sizeof buf);
        dx7_note_on(&dv, vced, 60, 100);
        for (b = 0; b + DX7_N <= N; b += DX7_N)
            dx7_render(&dv, buf + b);
        check("DX7 reference, dB", loudest(buf, N), -20.0, 0.3);
        check("DX7 C-4, Hz", pitch(buf, 4410, 30000), 261.63, 2.0);
        rec_juno_patch(juno_slot, jp);
        memset(buf, 0, sizeof buf);
        juno_note_on(&jv, jp, 60, 4096);
        {
            uint32_t acc = 0;
            for (b = 0; b + JUNO_N <= N; b += JUNO_N) {
                int tick;
                acc += juno_tick_q16;
                tick = acc >= 65536u;
                if (tick)
                    acc -= 65536u;
                juno_block(tick);
                juno_render(&jv, buf + b, tick);
            }
        }
        check("Juno-106 reference, dB", loudest(buf, N), -20.0, 0.3);
        rec_tb_knobs(tb_slot, k);
        memset(buf, 0, sizeof buf);
        tb_note_on(&tv, k, 36, 0);
        for (b = 0; b + TB_N <= N; b += TB_N)
            tb_render(&tv, buf + b);
        check("TB-303 reference, dB", loudest(buf, N), -20.0, 0.3);
        check("TB-303 C-2, Hz", pitch(buf, 4410, 30000), 65.41, 1.0);
        memset(&s, 0, sizeof s);
        s.rec = ptr;
        s.rec_len = sizeof ptr;
        s.length = 100;
        s.rate = 44100;
        s.end = 100;
        s.loop_end = 100;
        s.loop = SAMP_LOOP_FORWARD;
        s.base = 60;
        s.env[0] = s.env[1] = s.env[2] = s.env[3] = 99;
        s.env[4] = s.env[5] = s.env[6] = 99;
        memset(buf, 0, sizeof buf);
        samp_note_on(&sv, &s, 60, 0);
        for (b = 0; b + SAMP_N <= N; b += SAMP_N)
            samp_render(&sv, buf + b);
        check("sampler reference, dB", loudest(buf, N), -20.0, 0.3);
        check("sampler 441 Hz, Hz", pitch(buf, 4410, 30000), 441.0, 1.0);
    }
    {   /* through the player: the keys, the mix at half, centred; the default setup (LIMITER at -1 dB) */
        static const uint8_t NOTE[4] = {60, 60, 36, 60};
        static const char *WHAT[4] = {"DX7 played, dB", "Juno-106 played, dB", "TB-303 played, dB", "sampler played, dB"};
        int e;
        for (e = 0; e < 4; e++) {
            static int16_t half[512];
            pl_inst_t in = {(uint8_t)(e + 1), 0};
            player_init();
            player_key_on(NOTE[e], in, 127);
            for (b = 0; b + 256 <= N; b += 256) {
                player_audio(half, 256);
                for (i = 0; i < 256; i++)
                    buf[b + i] = half[2 * i];
            }
            for (; b < N; b++)
                buf[b] = 0;
            player_key_off(NOTE[e]);
            check(WHAT[e], loudest(buf, N), -26.02, 0.4);
        }
    }
    {   /* a punch-in command, TAPE STOP !08 at row 16 of a song at 120 BPM (a row 0.125 s), a note every other
         * row: from 2.0 s it winds down over a beat, is silent for the next (2.5-3.0 s), and the song comes back
         * at row 24 (3.0 s) */
        static uint8_t song[21 + 1 + 1 + 64 + 8], pat[2][200];
        static int16_t half[512];
        static float out[2][4 * 44100];
        int with, r, n;
        for (with = 0; with < 2; with++) {
            uint8_t *p = pat[with];
            int at = 5, s;
            memset(p, 0, 200);
            p[0] = 2;
            p[1] = 31;
            p[2] = 1;
            p[3] = with ? 3 : 1;
            for (r = 0; r < 32; r += 2) {
                p[at++] = (uint8_t)(0x80 | (r ? 0x20 : 0x60) | 0x10 | (r ? 1 : 0));
                p[at++] = (uint8_t)(60 + (r & 7));
                if (!r) {
                    p[at++] = 1;
                    p[at++] = 0;
                }
                p[at++] = 100;
            }
            p[at++] = 0;
            if (with) {
                p[at++] = 0x08 | 7;      /* the command alone, its row after a long skip */
                p[at++] = 0xD1;          /* TAPE STOP */
                p[at++] = 8;
                p[at++] = 16;
                p[at++] = 0;
            }
            disk_put(REC_PATTERN, (uint8_t)(0x30 + with), 0, p, (uint32_t)at);
            memset(song, 0, sizeof song);
            memcpy(song, "\x10\x15\x03\x08", 4);   /* (a name: PUNC) */
            song[12] = 120;
            song[14] = 6;
            song[15] = 4;
            song[16] = 50;
            song[17] = 1;                /* AT THE END: STOP */
            song[19] = 64;
            song[20] = 0;
            song[21] = (uint8_t)(0x30 + with);
            song[22] = 0;                /* (no mixer: the defaults) */
            s = 23;
            song[s + 1] = 0xF7;          /* the chain: LIMITER alone, on, -1 dB, 100 ms */
            memset(song + s + 2, 0xFF, 5);
            song[s + 7] = 0x80;
            song[s + 16 + 0x18] = 11;
            song[s + 16 + 0x19] = 9;
            disk_put(REC_SONG, 0x30, 0, song, sizeof song);
            player_init();
            player_play_song(0x30, 0, 0);
            for (n = 0; n + 256 <= 4 * 44100; n += 256) {
                player_audio(half, 256);
                for (i = 0; i < 256; i++)
                    out[with][n + i] = half[2 * i] / 32768.0f;
            }
        }
        {
            double q[2], bk[2];
            int k;
            for (k = 0; k < 2; k++) {
                int a, z;
                for (z = 0; z < 2; z++) {
                    int from = (int)((z ? 3.2 : 2.6) * 44100), to = (int)((z ? 3.6 : 3.0) * 44100);
                    double m = 0, v = 0;
                    for (a = from; a < to; a++)
                        m += out[k][a];
                    m /= to - from;
                    for (a = from; a < to; a++)
                        v += (out[k][a] - m) * (out[k][a] - m);
                    (z ? bk : q)[k] = sqrt(v / (to - from));
                }
            }
            check("TAPE STOP !08: its 2nd beat, dB", 20 * log10(q[1] / q[0] + 1e-9), -100, 70);
            check("TAPE STOP !08: after its rows, dB", 20 * log10(bk[1] / bk[0] + 1e-9), 0, 1.0);
        }
    }
    printf("reference: %s\n", fails ? "FAILED" : "all as they should be");
    return fails != 0;
}
