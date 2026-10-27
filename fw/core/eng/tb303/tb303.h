/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The TB-303 engine: Open303 (Robin Schmidt, MIT licence: LICENSE-open303.txt) in integer
 * arithmetic. One voice is one 303: its saw / square blend, a
 * high-pass, the TB_303 diode ladder (mystran & kunn's) with a high-pass in its feedback, the
 * main envelope (decay) on the cutoff, accent (a faster decay, the cutoff RC "wow", a louder
 * note), slide (the pitch glides 12 ms), the amp envelope and its declicker. Differences from
 * Open303: no oversampling (Open303's is 4x), so the cutoff stops at 10 kHz (with cutoff and env
 * full the sound is 25 % duller than Open303's; elsewhere it matches it, measured against Open303);
 * the coefficients every TB_CTL frames, not every sample; no 14 Hz allpass and 7.5 Hz notch
 * after the filter (phase and subsonics only).
 *
 *   tb_note_on(v, knobs, note, accent)   a new note: the envelopes start again (the pitch jumps)
 *   tb_slide_to(v, note, accent)         a slid note: the pitch glides there, nothing restarts
 *   tb_pitch(v, note, bend64)            the pitch bent (1/64 semitones; it glides there)
 *   tb_note_off(v)                       the gate closes: the amp envelope releases
 *   tb_knob(v, k, value)                 a knob turned (0..127) while it plays
 *   tb_render(v, out)                    ADDS TB_N frames, the 16-bit scale (Open303's -12 dB, times
 *                                        TB_OUT_Q16, the engine's output scale)
 */
#ifndef TB303_H
#define TB303_H
#include <stdint.h>

#define TB_N 64                          /* frames a block (= DX7_N = JUNO_N) */
/* the engine's output scale (Q16), so that PT/FM's reference (specs/files/instruments.md: WAVE 0,
 * CUTOFF 127, RESONANCE 0, ENV MOD 0, ACCENT 0, and DECAY 64, which the spec leaves open; C-2) comes out at
 * -20 dB RMS of the 16-bit full scale (its loudest 300 ms); it was -11.61 dB. Applied with the filter's output
 * gain (f_g2): only linear stages follow it (the high-pass, the amp), so it scales the output exactly */
#define TB_OUT_Q16 24956
enum { TB_WAVE, TB_CUTOFF, TB_RESO, TB_ENVMOD, TB_DECAY, TB_ACCENT, TB_NK };   /* the knobs */
#define TB_NPATCH_BYTES TB_NK

typedef struct {
    uint8_t k[TB_NK];                    /* its knobs (a copy: the song turns them) */
    uint8_t gate, acc, live;             /* key down, this note accented, sounding */
    uint32_t phase, inc, inc_to;         /* the oscillator at TB_OS x (2^32 a cycle), sliding to inc_to */
    int32_t hp1_x, hp1_y;                /* the high-pass before the filter */
    int32_t y1, y2, y3, y4, fb_x, fb_y;  /* the ladder, the high-pass in its feedback */
    int32_t dec[3];                      /* the decimator's last samples */
    int32_t hp2_x, hp2_y;                /* the high-pass after it */
    int32_t meg, meg_c, rc2;             /* the main envelope (Q24), its factor per TB_CTL, the accent RC */
    int32_t amp, rel;                    /* the amp envelope (Q24), its release factor per TB_CTL */
    int32_t dk_x1, dk_x2, dk_y1, dk_y2;  /* the declicker (a biquad low-pass on the amp, every TB_AMP) */
    int32_t f_meg_amp;                   /* the main envelope's part of the amp (every TB_CTL) */
    int32_t acc_gain;                    /* ACCENT on an accented note, else 0 (Q24) */
    int32_t env_sc, env_off, cut_l2, rsk;   /* from the knobs (Q24) */
    /* the coefficients the loop reads (through the struct: the JieLi clang hoists a constant int32
     * multiplier as a 64-bit one; see juno.h) */
    int32_t f_b0, f_k, f_g2, f_saw, f_sq, f_gain, f_dgain;
} tb_voice_t;

void tb_note_on(tb_voice_t *v, const uint8_t *knobs, int note, int accent);
void tb_slide_to(tb_voice_t *v, int note, int accent);
void tb_pitch(tb_voice_t *v, int note, int32_t bend64);   /* bent by bend64 / 64 semitones (it glides) */
void tb_note_off(tb_voice_t *v);
void tb_cut(tb_voice_t *v);
void tb_knob(tb_voice_t *v, int k, int value);
int tb_active(const tb_voice_t *v);
void tb_render(tb_voice_t *v, int32_t *out);
#endif
