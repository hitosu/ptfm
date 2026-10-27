/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A Juno-106 voice for the FM-1, integer only, after the models of Ultramaster KR-106 (GPL-3.0;
 * measured on Juno-106 hardware, its voice CPU's firmware routines rebuilt from the ROM).
 *
 * A patch is the Juno-106's own: 16 slider bytes and 2 switch bytes, in its SysEx order (LFO rate,
 * LFO delay, DCO LFO, DCO PWM, noise, VCF freq, res, env, LFO, kbd, VCA level, A, D, S, R, sub;
 * switches 1: range 16'/8'/4', pulse, saw, chorus on (0) / off, chorus I (1) / II; switches 2:
 * PWM manual (1) / LFO, VCF env - (1) / +, VCA gate (1) / env, HPF (3 - bits 3..4)).
 *
 * As on the 106: the envelope, the LFO (its delay too) and the VCF cutoff are computed by the
 * voice CPU's routines, integer, at its main-loop rate (~236 Hz, a "tick"; KR-106's
 * KR106ADSR / KR106LFO / KR106VcfFreqJ106), the cutoff and VCA control voltages are smoothed as
 * the hardware's RC networks do. The audio path is cheaper than KR-106's (which runs a 4-pole
 * filter with an OTA model per stage at 2-4x oversampling): one sample rate, band-limited
 * (polyBLEP) saw / pulse / sub, a 4-pole TPT ladder with the resonance feedback through one OTA
 * saturation (tanh), the measured cutoff and VCA curves, the HPF (bass boost / flat / 236 /
 * 754 Hz), and the BBD chorus as a modulated delay per channel. Differences from the 106: one
 * LFO per voice (the 106 has one for all; voices playing the same patch stay in step), the
 * chorus I+II mode (not in a patch) is not there.
 *
 *   juno_init()                       once
 *   juno_block(tick)                  once per JUNO_N frames, before the voices: tick = 1 when the
 *                                     firmware loop runs in this block (the caller's JUNO_TICK_Q16
 *                                     accumulator)
 *   juno_note_on(v, patch, note, level_q12)   level: 4096 = the patch's own volume
 *   juno_note_off(v), juno_cut(v)     release / silence now
 *   juno_patch(v, patch)              a sounding voice's patch changed (parameter commands on the
 *                                     player's own copy of it); juno_level(v, level_q12): its level;
 *                                     v->bend (Q24 octaves): the player's pitch bend, 0 at note on
 *   juno_render(v, out, tick)         ADDS JUNO_N mono frames (16-bit scale) to out (times JUNO_OUT_Q16,
 *                                     the engine's output scale)
 *   juno_idle(v, tick)                a voice not rendered: its LFO runs on
 *   juno_skip(v, tick)                a sounding voice's block without its sound (parked): its envelope, LFO
 *                                     and oscillator go on
 *   juno_busy(v)                      still sounding
 *   juno_chorus_mode(patch)           0 off, 1 chorus I, 2 chorus II
 *   juno_chorus(c, mode, in, l, r)    a chorus bus: ADDS the stereo output of JUNO_N frames
 */
#ifndef JUNO_H
#define JUNO_H
#include <stdint.h>

#define JUNO_N 64
#define JUNO_PATCH_BYTES 18
/* the engine's output scale (Q16), so that PT/FM's reference patch (specs/files/instruments.md:
 * SAW alone, 8', VCF FREQ 127, RES 0, ENV 0, LFO 0, KYBD 0, HPF 1, VCA ENV at LEVEL 127, ENV A 0 D 0 S 127 R 0,
 * chorus off; C-4) comes out at -20 dB RMS of the 16-bit full scale (its loudest 300 ms); it was +2.37 dB.
 * Applied with the output make-up (K_OUT), after the filter's saturation: only linear stages follow it (the
 * VCA, the HPF, the chorus), so it scales the output exactly, in 32 bits */
#define JUNO_OUT_Q16 4990

typedef struct {
    const uint8_t *p;                    /* the patch (18 bytes) */
    /* from the patch, at note on */
    uint16_t atk, dec, rel, sus, lfo_coeff, hold_inc, ramp_inc;
    uint8_t dco_lfo, flags;              /* JF_* */
    int32_t g_saw, g_pulse, g_sub, g_noise, pwm_q15, in_env;
    int32_t k_q12, kfb_q12, kfbinv_q12, res_off, level_q12;
    /* the note */
    int32_t logfreq;                     /* DCO pitch, Q24 octaves (the range switch included) */
    int32_t bend, gain_q12, note;        /* the player's bend (Q24 octaves), its level, the key */
    uint16_t pitch88;                    /* the key, 8.8 semitones: VCF key follow */
    uint8_t gate;
    /* envelope (14 bit), LFO (13 bit + polarity) and its delay: the firmware's integer state */
    uint8_t env_state, lfo_rising, lfo_pol, in_hold;
    uint16_t env, lfo_acc, hold_acc, ramp_acc;
    uint8_t lfo_amp;                     /* the LFO's onset envelope 0..255 */
    /* DCO */
    uint32_t phase, dphase, pw;          /* pw: the pulse's rising edge, in phase units */
    uint8_t sub;
    int64_t dc;                          /* one-pole states in 64 bits (Q44): see lp1() */
    /* VCF: integrator states (Q20), the cutoff DAC value (14 bit) and its smoothed value (<< 8) */
    int32_t s[4], dac, dac_s;
    /* its coefficients for the current 16 frames, handed to the filter's pass here (f_out and 1 / kfb
     * are read from here in the loops: as locals, the compiler made such a constant multiplier a
     * 64-bit value and the multiply 64 x 64, 3 multiplies) */
    int32_t f_g1, f_g1_2, f_g1_3, f_om, f_invden, f_k, f_out;
    /* VCA: gain (Q15, x the levels), smoothed per frame towards its target */
    int32_t vca, vca_target;
    /* HPF / bass boost */
    int64_t hp[2];
} juno_voice_t;

typedef struct {
    int32_t buf[256];
    uint32_t w, lfo;
    int32_t lp[2];
} juno_chorus_t;

extern const uint32_t juno_tick_q16;    /* firmware ticks per JUNO_N frames, Q16 (~236 Hz) */
void juno_init(void);
void juno_block(int tick);
/* the noise generator's state, kept and put back (a measurement from juno_init's state between) */
typedef struct { uint32_t seed; int64_t st[6]; } juno_noise_t;
void juno_noise_get(juno_noise_t *n);
void juno_noise_set(const juno_noise_t *n);
void juno_note_on(juno_voice_t *v, const uint8_t *patch, int note, int32_t level_q12);
void juno_patch(juno_voice_t *v, const uint8_t *patch);
void juno_key(juno_voice_t *v, int note);  /* a legato note: the VCF's key follow moves there */
void juno_level(juno_voice_t *v, int32_t level_q12);
void juno_note_off(juno_voice_t *v);
void juno_cut(juno_voice_t *v);
void juno_render(juno_voice_t *v, int32_t *out, int tick);
void juno_idle(juno_voice_t *v, int tick);
void juno_skip(juno_voice_t *v, int tick);
int juno_busy(const juno_voice_t *v);
int juno_chorus_mode(const uint8_t *patch);
void juno_chorus(juno_chorus_t *c, int mode, const int32_t *in, int32_t *l, int32_t *r);
#endif
