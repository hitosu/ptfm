/* SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Vasily Kazantsev (the C port) */
/* A DX7 voice for the FM-1: a C port of MSFA, the engine inside Dexed (Copyright 2012-2013 Google
 * Inc., 2016-2025 Pascal Gauthier; Apache License 2.0, see LICENSE-msfa.txt). Integer only: the
 * tables MSFA computes with doubles at init come from fw/tools/gen_dx7.py.
 *
 * Changes from MSFA, each marked FM-1: in dx7.c:
 * - C, one voice = one struct (Dx7Note + its own Lfo; Dexed has one LFO per instance)
 * - standard tuning only, no pitch bend / mod wheel / portamento / MPE / operator switches
 *   (MSFA with its controllers at rest: these terms are 0); a bend (dx7_voice_t.bend, Q24 per
 *   octave, set by the player: the tracker's slides and vibrato), added where Dexed adds its pitch
 *   bend
 * - the amplitude-modulation depth curve (an exp() per block in Dexed) from a table with
 *   interpolation: equal at its 257 points, within rounding between them
 * - phases in uint32_t (MSFA's int32_t overflow, without the undefined behaviour)
 *
 *   dx7_init()                 once: the tables into RAM
 *   dx7_note_on(v, vced, note, velocity)   vced: a patch in the 156-byte layout Dx7Note::init
 *                              reads (the player unpacks a cartridge's slot into it, as Dexed's
 *                              unpackProgram); as Dexed's keydown: transpose, init, LFO key down,
 *                              oscillator sync if the patch asks for it
 *   dx7_note_off(v)            key up
 *   dx7_cut(v)                 silence now (no release)
 *   dx7_render(v, buf)         ADDS DX7_N samples to buf, the 16-bit scale (MSFA's >> 13, times
 *                              DX7_OUT_Q16, the engine's output scale: PT/FM's reference voice comes out at
 *                              -20 dB RMS); FM-1: the operators' scratch on its own stack (512 B), so two cores
 *                              render at once
 *   dx7_skip(v)                FM-1 addition: a block without its sound (a parked voice): its LFO,
 *                              envelopes and oscillators go on as dx7_render's would
 *   dx7_active(v)              any carrier envelope still sounding (Dx7Note::isPlaying)
 */
#ifndef DX7_H
#define DX7_H
#include <stdint.h>

#define DX7_LG_N 6
#define DX7_N (1 << DX7_LG_N)
#define DX7_VCED 156
/* the engine's output scale (Q16), so that PT/FM's reference voice (specs/files/instruments.md: algorithm
 * 32, operator 1 alone at OUTPUT LEVEL 99, ratio 1.00, its EG rates 99 and levels 99 99 99 0; C-4) comes out
 * at -20 dB RMS of the 16-bit full scale (its loudest 300 ms); at MSFA's >> 13 it was -21.06 dB.
 * fw/tests/run_sound.sh measures the four engines' reference sounds */
#define DX7_OUT_Q16 74076

typedef struct {
    uint8_t rates[4], levels[4];
    uint8_t ix, rising, down, initialised;
    int32_t outlevel, rate_scaling;
    int32_t level, targetlevel, inc, staticcount;
} dx7_env_t;

typedef struct {
    uint8_t rates[4], levels[4];
    uint8_t ix, rising, down;
    int32_t level, targetlevel, inc;
} dx7_penv_t;

typedef struct {
    uint32_t phase, delta, delaystate, delayinc, delayinc2;
    uint8_t waveform, randstate, sync;
} dx7_lfo_t;

typedef struct {
    int32_t level_in, gain_out, freq;
    uint32_t phase;
} dx7_op_t;

typedef struct {
    dx7_env_t env[6];
    dx7_op_t op[6];
    dx7_penv_t penv;
    dx7_lfo_t lfo;
    int32_t basepitch[6], fb_buf[2], fb_shift, ampmodsens[6];
    int32_t pitchmoddepth, pitchmodsens, ampmoddepth;
    int32_t bend;                        /* FM-1 addition: Q24 per octave, 0 at note on */
    uint8_t opmode[6], algorithm, initialised;
} dx7_voice_t;

void dx7_init(void);
uint32_t dx7_ram(void);                 /* the bytes of its tables in RAM (STATUS' RAM MAP: DX7) */
void dx7_note_on(dx7_voice_t *v, const uint8_t *vced, int note, int velocity);
void dx7_note_off(dx7_voice_t *v);
void dx7_cut(dx7_voice_t *v);           /* FM-1 addition: silence now */
void dx7_render(dx7_voice_t *v, int32_t *buf);
void dx7_skip(dx7_voice_t *v);           /* FM-1 addition: a block of time, no sound */
int dx7_active(const dx7_voice_t *v);
int dx7_carrier(int algorithm, int op);  /* FM-1 addition: operator op (1..6) of the algorithm (0..31) is a carrier */
void dx7_live(dx7_voice_t *v, int at, int value);   /* FM-1 addition: byte `at` of the 156 turned while it sounds */

/* the Juno engine borrows MSFA's integer helpers: Q24 in, Q24 out */
int32_t dx7_exp2(int32_t x);
int32_t dx7_tanh(int32_t x);
int32_t dx7_freq(int32_t logfreq);      /* logfreq (Q24 per octave) -> phase step, 2^24 = a cycle */
int32_t dx7_sin(uint32_t phase);        /* FM-1 addition: sin, phase 2^24 a cycle, Q24 out (the effects' LFOs) */
const int32_t *dx7_table(int which);    /* FM-1 addition: 0 sin, 1 exp2, 2 tanh, 3 freqlut, as dx7_init built them */
/* FM-1 addition: dx7_sin inline, on the table dx7_table(0) gives: the same code (MSFA's Sin::lookup), for code that
 * may not call into the engine (the effects' and the output's, which may run on the FM-1's second core): always in
 * place, never a call */
static inline __attribute__((always_inline)) int32_t dx7_sin_tab(uint32_t phase, const int32_t *sintab)
{
    int32_t lowbits = (int32_t)(phase & ((1u << 14) - 1u));
    uint32_t phase_int = (phase >> 13) & ((1024u - 1u) << 1);
    return sintab[phase_int + 1u] + (int32_t)(((int64_t)sintab[phase_int] * (int64_t)lowbits) >> 14);
}

/* FM-1 addition: dx7_tanh inline, for a loop that runs it every sample (the Juno-106 filter):
 * the same code (MSFA's Tanh::lookup, Exp2::lookup) on the tables dx7_tables() gives, its rare
 * branch (|x| >= 4) laid out of the loop's way */
void dx7_tables(const int32_t **exp2tab, const int32_t **tanhtab);
static inline int32_t dx7_tanh_tab(int32_t x, const int32_t *exp2tab, const int32_t *tanhtab)
{
    int32_t signum = x >> 31;
    x ^= signum;
    if (__builtin_expect(x >= (4 << 24), 0)) {
        int32_t sx, lowbits, x_int, y;
        if (x >= (17 << 23))
            return signum ^ (1 << 24);
        sx = (int32_t)(((int64_t)-48408812 * (int64_t)x) >> 24);
        lowbits = sx & ((1 << 14) - 1);  /* dx7_exp2(sx) */
        x_int = (sx >> 13) & ((1024 - 1) << 1);
        y = exp2tab[x_int + 1] + (int32_t)(((int64_t)exp2tab[x_int] * (int64_t)lowbits) >> 14);
        return signum ^ ((1 << 24) - 2 * (y >> (6 - (sx >> 24))));
    } else {
        int32_t lowbits = x & ((1 << 16) - 1);
        int32_t x_int = (x >> 15) & ((1024 - 1) << 1);
        int32_t y = tanhtab[x_int + 1] + (int32_t)(((int64_t)tanhtab[x_int] * (int64_t)lowbits) >> 16);
        return y ^ signum;
    }
}
#endif
