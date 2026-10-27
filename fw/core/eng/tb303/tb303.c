/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The TB-303 engine (see tb303.h): Open303's voice (rosic::Open303, Robin Schmidt, MIT licence:
 * LICENSE-open303.txt) in integer arithmetic. The names follow Open303's: triggerNote, slideToNote,
 * releaseNote; mainEnv (DecayEnvelope), rc2 (the accent's LeakyIntegrator), ampEnv
 * (AnalogEnvelope: attack 0, decay 1230 ms, sustain 0, release 1 ms / 50 ms on accents),
 * ampDeClicker, pitchSlewLimiter, highpass1/2, TeeBeeFilter in its TB_303 mode, BlendOscillator
 * between Open303's SAW303 and SQUARE303 waves. Its constants: tb303_tables.h.
 *
 * Fixed point: the signal Q20 (1.0 = 2^20), coefficients, envelopes and octaves Q24. */
#include "tb303.h"
#include "tb303_tables.h"

#define Q20 (1 << 20)
#define Q24 (1 << 24)
#define TB_PW 2013265920u                /* SQUARE303 falls at 15/32 of a cycle (tanh(70 saw + 4.37), 180 deg) */

static inline int32_t mul24(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }

/* polyBLEP (2 points), as juno.c: the step's residual t after (or before) an edge, Q16. The
 * squares in 64 bits: n and m reach 65536, and n * n in an int overflowed above 46340 (undefined: gcc
 * wrapped it negative, a glitch at the edge; clang's optimiser happened to get it right) */
static inline int32_t blep_after(uint32_t t, uint32_t recip)
{
    int32_t n = (int32_t)(((t >> 16) * recip) >> 14);
    return 2 * n - (int32_t)(((int64_t)n * n) >> 16) - 65536;
}
static inline int32_t blep_before(uint32_t t, uint32_t recip)
{
    int32_t m = 65536 - (int32_t)((((0u - t) >> 16) * recip) >> 14);
    return (int32_t)(((int64_t)m * m) >> 16);
}

/* the knobs' consequences: Open303::calculateEnvModScalerAndOffset (its measured mapping), the
 * resonance's skew, the wave blend (BlendOscillator: the square at half) */
static void tb_derive(tb_voice_t *v)
{
    int32_t e = (int32_t)v->k[TB_ENVMOD] * (Q24 / 127), c = (int32_t)v->k[TB_CUTOFF] * (Q24 / 127);
    int32_t slo = mul24(TB_SLOF, e) + TB_SLOC, shi = mul24(TB_SHIF, e) + TB_SHIC, b;
    v->cut_l2 = TB_CUT_L2[v->k[TB_CUTOFF] & 127];
    v->env_sc = mul24(Q24 - c, slo) + mul24(c, shi);
    v->env_off = mul24(TB_OF, c) + TB_OC;
    v->rsk = TB_RSKEW[v->k[TB_RESO] & 127];
    b = (int32_t)v->k[TB_WAVE] * (Q24 / 127);
    v->f_saw = Q24 - b;
    v->f_sq = b >> 1;
}

static void tb_accent(tb_voice_t *v, int accent)   /* triggerNote / slideToNote's accent part */
{
    v->acc = (uint8_t)(accent != 0);
    v->acc_gain = accent ? (int32_t)v->k[TB_ACCENT] * (Q24 / 127) : 0;
    v->meg_c = accent ? TB_ACC_DECAY_C : TB_DECAY_C[v->k[TB_DECAY] & 127];
    v->rel = accent ? TB_REL_A : TB_REL_N;
}

void tb_note_on(tb_voice_t *v, const uint8_t *knobs, int note, int accent)
{
    int i;
    if (!tb_active(v)) {                 /* (silent: start clean, as Open303 does when idle) */
        v->phase = 0;
        v->hp1_x = v->hp1_y = v->y1 = v->y2 = v->y3 = v->y4 = v->fb_x = v->fb_y = 0;
        v->dec[0] = v->dec[1] = v->dec[2] = v->hp2_x = v->hp2_y = 0;
        v->dk_x1 = v->dk_x2 = v->dk_y1 = v->dk_y2 = 0;
        v->rc2 = 0;
    }
    for (i = 0; i < TB_NK; i++)
        v->k[i] = knobs[i];
    tb_derive(v);
    tb_accent(v, accent);
    v->inc = v->inc_to = TB_NOTE_INC[note & 127];
    v->meg = Q24;                        /* mainEnv.trigger(): its first sample is 1 */
    v->amp = Q24;                        /* ampEnv: attack 0, from where it was to 1 at once */
    v->gate = 1;
}

void tb_slide_to(tb_voice_t *v, int note, int accent)
{
    v->inc_to = TB_NOTE_INC[note & 127];
    tb_accent(v, accent);
    v->gate = 1;
}

/* the pitch bent (the player's slides and vibrato, 1/64 semitones): the oscillator glides there as
 * in a slide */
static const uint16_t TB_FINE_Q15[64] = {   /* 2^(j/768), Q15 */
    32768, 32798, 32827, 32857, 32887, 32916, 32946, 32976, 33005, 33035, 33065, 33095, 33125, 33155, 33185, 33215,
    33245, 33275, 33305, 33335, 33365, 33395, 33425, 33455, 33486, 33516, 33546, 33576, 33607, 33637, 33667, 33698,
    33728, 33759, 33789, 33820, 33850, 33881, 33911, 33942, 33973, 34003, 34034, 34065, 34095, 34126, 34157, 34188,
    34219, 34250, 34281, 34312, 34343, 34374, 34405, 34436, 34467, 34498, 34529, 34560, 34591, 34623, 34654, 34685};

void tb_pitch(tb_voice_t *v, int note, int32_t bend64)
{
    int32_t n = note * 64 + bend64;
    n = n < 0 ? 0 : n > 127 * 64 ? 127 * 64 : n;
    v->inc_to = (uint32_t)(((uint64_t)TB_NOTE_INC[n >> 6] * TB_FINE_Q15[n & 63]) >> 15);
}

void tb_note_off(tb_voice_t *v) { v->gate = 0; }

void tb_cut(tb_voice_t *v)
{
    v->gate = 0;
    v->amp = v->meg = v->f_gain = v->f_dgain = 0;
    v->dk_x1 = v->dk_x2 = v->dk_y1 = v->dk_y2 = 0;
}

void tb_knob(tb_voice_t *v, int k, int value)
{
    if (k < 0 || k >= TB_NK)
        return;
    v->k[k] = (uint8_t)(value < 0 ? 0 : value > 127 ? 127 : value);
    tb_derive(v);                        /* (DECAY and ACCENT: from the next note on, as Open303) */
}

int tb_active(const tb_voice_t *v)
{
    return v->gate || v->amp > (Q24 >> 14) || v->dk_y1 > (Q24 >> 14) || v->dk_y1 < -(Q24 >> 14);
}

/* every TB_CTL frames: the slide, the envelope, the accent RC, the cutoff and the ladder's
 * coefficients (Open303::getSample's control part, and TeeBeeFilter::calculateCoefficientsApprox4) */
static void tb_control(tb_voice_t *v)
{
    int32_t in, lc, idx, fr, b0, kp, r = v->rsk, g;
    v->inc = v->inc_to + (uint32_t)mul24((int32_t)(v->inc - v->inc_to), TB_SLEW_C);
    in = v->acc_gain > 0 ? v->meg : 0;   /* rc2 */
    v->rc2 = in + mul24(v->rc2 - in, TB_RC2_C);
    lc = v->cut_l2 + mul24(v->env_sc, v->meg - v->env_off) + mul24(v->acc_gain, v->rc2);
    lc -= TB_L0;
    lc = lc < 0 ? 0 : lc >= ((TB_NLAD - 2) << 20) ? ((TB_NLAD - 2) << 20) - 1 : lc;
    idx = lc >> 20;                      /* 1/16 octave a point */
    fr = (lc & ((1 << 20) - 1)) << 4;    /* Q24 */
    b0 = TB_LAD[idx][0] + mul24(TB_LAD[idx + 1][0] - TB_LAD[idx][0], fr);
    kp = TB_LAD[idx][1] + mul24(TB_LAD[idx + 1][1] - TB_LAD[idx][1], fr);
    g = mul24(mul24(kp, Q24 / 17) - Q24, r) + Q24;   /* g = ((k / 17 - 1) r + 1)(1 + r) */
    g = mul24(g, Q24 + r);
    v->f_b0 = b0;
    v->f_k = mul24(kp, r);
    v->f_g2 = (int32_t)(((int64_t)g * TB_OUT_Q16) >> 15);   /* 2 g, x the output scale */
    v->f_meg_amp = v->gate ? mul24(v->meg, 7549747 + 4 * v->acc_gain) : 0;   /* (0.45 + 4 accentGain) mainEnv */
    v->meg = mul24(v->meg, v->meg_c);
}

/* every TB_AMP frames: the amp, ampEnv (decaying while the note is on, releasing after) + the
 * main envelope's part while it is on, through the declicker; the frames between interpolated */
static inline void tb_amp(tb_voice_t *v)
{
    int32_t in, g;
    v->amp = mul24(v->amp, v->gate ? TB_AMP_DEC : v->rel);
    in = v->amp + v->f_meg_amp;
    g = (int32_t)(((int64_t)(in + v->dk_x2) * TB_DK_B0 + (int64_t)v->dk_x1 * TB_DK_B1 +
                   (int64_t)v->dk_y1 * TB_DK_A1 + (int64_t)v->dk_y2 * TB_DK_A2) >> 24);
    v->dk_x2 = v->dk_x1;
    v->dk_x1 = in;
    v->dk_y2 = v->dk_y1;
    v->dk_y1 = g;
    v->f_gain = v->dk_y2;                /* from the last step's to this one's */
    v->f_dgain = (g - v->dk_y2) / TB_AMP;
}

void tb_render(tb_voice_t *v, int32_t *out)
{
    uint32_t phase = v->phase, inc, recip, inc16;
    int32_t y1 = v->y1, y2 = v->y2, y3 = v->y3, y4 = v->y4, fb_x = v->fb_x, fb_y = v->fb_y;
    int i, j, wave = v->f_sq == 0 ? 0 : v->f_saw == 0 ? 1 : 2;   /* saw, square, both */
    for (j = 0; j < TB_N; j += TB_CTL) {
        tb_control(v);
        inc = v->inc;
        inc16 = inc >> 16 ? inc >> 16 : 1u;
        recip = (1u << 30) / inc16;
        for (i = j; i < j + TB_CTL; i++) {
            int32_t d, a, x = 0, y0;
            if (!(i & (TB_AMP - 1)))
                tb_amp(v);
            phase += inc;
            if (wave != 1) {             /* SAW303: up from -1 to 1, down at half a cycle */
                uint32_t t = phase + (1u << 31);   /* (its edge at t = 0) */
                int32_t saw = (int32_t)(t >> 11) - Q20, b = 0;
                if (__builtin_expect(t < inc, 0))   /* (an edge's frames are rare: the usual way straight on, a
                                                     * taken jump costs ~6 cycles) */
                    b = blep_after(t, recip);
                else if (__builtin_expect(t > 0u - inc, 0))
                    b = blep_before(t, recip);
                saw -= b << 4;
                x = wave ? mul24(saw, v->f_saw) : saw;
            }
            if (wave) {                  /* SQUARE303: up at 0, down at 15/32; at half (BlendOscillator) */
                uint32_t t2 = phase - TB_PW;
                int32_t sq = phase < TB_PW ? Q20 : -Q20, b = 0;
                if (__builtin_expect(phase < inc, 0))
                    b = blep_after(phase, recip);
                else if (__builtin_expect(phase > 0u - inc, 0))
                    b = blep_before(phase, recip);
                sq += b << 4;
                b = 0;
                if (__builtin_expect(t2 < inc, 0))
                    b = blep_after(t2, recip);
                else if (__builtin_expect(t2 > 0u - inc, 0))
                    b = blep_before(t2, recip);
                sq -= b << 4;
                x += wave == 1 ? sq >> 1 : mul24(sq, v->f_sq);
            }
            x = -x;                      /* (Open303 inverts the oscillator) */
            v->hp1_y = mul24(x - v->hp1_x, TB_HP1_B) + mul24(v->hp1_y, TB_HP1_A);   /* highpass1 */
            v->hp1_x = x;
            x = mul24(v->f_k, y4);       /* the feedback through its high-pass */
            fb_y = mul24(x - fb_x, TB_FB_B) + mul24(fb_y, TB_FB_A);
            fb_x = x;
            y0 = v->hp1_y - fb_y;        /* TeeBeeFilter, TB_303 */
            y1 += 2 * mul24(y0 - y1 + y2, v->f_b0);
            y2 += mul24(y1 - 2 * y2 + y3, v->f_b0);
            y3 += mul24(y2 - 2 * y3 + y4, v->f_b0);
            y4 += mul24(y3 - 2 * y4, v->f_b0);
            d = mul24(y4, v->f_g2);
            a = mul24(d - v->hp2_x, TB_HP2_B) + mul24(v->hp2_y, TB_HP2_A);   /* highpass2 */
            v->hp2_x = d;
            v->hp2_y = a;
            out[i] += mul24(a, v->f_gain) >> 7;   /* the amp; Q20 -> 16-bit scale, x Open303's -12 dB */
            v->f_gain += v->f_dgain;
        }
    }
    v->phase = phase;
    v->y1 = y1;
    v->y2 = y2;
    v->y3 = y3;
    v->y4 = y4;
    v->fb_x = fb_x;
    v->fb_y = fb_y;
}
