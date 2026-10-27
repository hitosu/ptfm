/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* CHIP: one voice of a virtual 8-bit sound chip (specs/chip.md). Its parts are the handheld's (BRK), the
 * console's (8BIT) and the computers' PSG (PSG, 16BIT), taken as an instrument's settings: an oscillator
 * (PULSE, TRIANGLE, WAVE, BUZZ, NOISE) stepping at what a period register on its CLOCK gives, a 4-bit volume
 * and its envelopes, a sweep, the noise registers, each family's volume curve and output filter, and the
 * built-in tables (VOL, ARP, PITCH, DUTY, NOISE, played a frame at a time) and the vibrato.
 *
 * Integers only, within what a JS double holds exactly: the playground's twin (playground/dsp-chip.js) computes
 * the same numbers, and fw/tests/run_chip.sh proves the two equal sample for sample. Every change of the output
 * is band-limited: added as a windowed-sinc impulse (chip_tables.h's kernel) to a running sum.
 *
 *   chip_unpack(slot, p)            an instrument's 28 bytes as its settings
 *   chip_note_on(v, p, note)        a new note (MIDI); the voice starts clean
 *   chip_pitch(v, bend64)           the track's pitch: bend64 / 64 semitones from the note (portamento, the
 *                                   vibrato and arpeggio commands)
 *   chip_note_off(v)                let go: RELEASE takes it down (RELEASE 0: it ends at once)
 *   chip_cut(v)                     silence now
 *   chip_set(p, code, value)        a parameter command (0x40-0x7F) on an instrument's settings (a note's start)
 *   chip_param(v, code, value)      the same on the sounding note
 *   chip_render(v, out)             CHIP_N frames into out, which must come zeroed (the engine sums its
 *                                   impulses there first); the 16-bit scale, CHIP_OUT its full level
 *   chip_skip(v)                    a block without its sound (a parked voice): its time goes on
 *   chip_active(v)                  it still sounds
 */
#ifndef CHIP_H
#define CHIP_H
#include <stdint.h>

#define CHIP_N 64                        /* frames a block (= DX7_N = JUNO_N = TB_N) */
#define CHIP_SLOT_BYTES 28
#define CHIP_KW 16                       /* the band-limited kernel's taps (chip_tables.h) */
/* the shortest step of the oscillator and the gate (Q16 samples: two a sample) and of the PSG envelope (one a
 * sample): a voice's cost has a ceiling, 128 + 128 + 64 band-limited steps a block (64 more with slow noise mixed
 * in); a note past it sounds at the
 * highest pitch it allows (a 32-step wave 2,756 Hz, a pulse 11 kHz) */
#define CHIP_STEP_MIN 32768u
#define CHIP_ENV_STEP_MIN 65536u
/* the engine's output scale: a level at full amplitude (the curve's 1.0), in 16-bit units, so that PT/FM's
 * reference (specs/files/instruments.md: PULSE 50 %, CLOCK EXACT, CURVE BRK, FILTER NONE, VOL 15, no table, no
 * envelope; C-4) comes out at -20 dB RMS of the 16-bit full scale (its loudest 300 ms): a 50 % pulse's swing of
 * 6,554 would be, but for its band-limited edges (+0.22 dB); fw/tests/run_chip.sh measures it */
#define CHIP_OUT 6390

enum { CHIP_PULSE, CHIP_TRIANGLE, CHIP_WAVE, CHIP_BUZZ, CHIP_NOISE };                 /* OSC */
enum { CHIP_BRK, CHIP_8BIT, CHIP_PSG177, CHIP_PSG100, CHIP_PSG179, CHIP_16BIT, CHIP_EXACT };   /* CLOCK */
enum { CHIP_ENV_NONE, CHIP_ENV_DOWN, CHIP_ENV_UP, CHIP_ENV_PSG };                     /* ENV */
enum { CHIP_SWEEP_OFF, CHIP_SWEEP_DOWN, CHIP_SWEEP_UP };
enum { CHIP_TVOL, CHIP_TARP, CHIP_TPITCH, CHIP_TDUTY, CHIP_TNOISE, CHIP_NTK };          /* the tables' kinds */

/* the slot's fields: bit position and width (specs/files/instruments.md#chip), least significant bit first */
enum {
    CHIP_B_OSC = 0, CHIP_B_DUTY = 3, CHIP_B_BZ_SHAPE = 5, CHIP_B_BZ_STEPS = 7, CHIP_B_BZ_GATE = 8,
    CHIP_B_N_MIX = 9, CHIP_B_N_SHORT = 10, CHIP_B_N_RATE = 11, CHIP_B_N_KEY = 15,
    CHIP_B_CLOCK = 16, CHIP_B_CURVE = 19, CHIP_B_FILTER = 21, CHIP_B_FPS = 23,
    CHIP_B_SWEEP = 24, CHIP_B_SHIFT = 26, CHIP_B_PACE = 29,
    CHIP_B_VOL = 32, CHIP_B_ENV = 36, CHIP_B_STEP = 38, CHIP_B_RELEASE = 41,
    CHIP_B_PSG_SHAPE = 45, CHIP_B_PERIOD = 49, CHIP_B_WAVE = 65,
    CHIP_B_TAB = 69,                     /* + 8 a kind: its table (6 bits), then its SPEED - 1 (2) */
    CHIP_B_VIB_DELAY = 109, CHIP_B_VIB_SPEED = 114, CHIP_B_VIB_DEPTH = 116,
    CHIP_B_NAME = 121, CHIP_B_LEVEL = 181
};

typedef struct {                         /* an instrument as it plays (a parameter command changes the voice's) */
    uint8_t osc, duty, bz_shape, bz_steps, bz_gate;   /* bz_shape 0 SAW DN, 1 SAW UP, 2 TRI; bz_steps 0: 16, 1: 32 */
    uint8_t n_mix, n_short, n_rate, n_key;
    uint8_t clock, curve, filter, fps50;              /* curve and filter 0 BRK, 1 8BIT, 2 PSG, 3 16BIT / NONE */
    uint8_t sweep, sw_shift, sw_pace;
    uint8_t vol, env, env_step, release;
    uint8_t psg_shape, wave;
    uint16_t psg_period;                 /* 1-65,535 */
    uint8_t tab[CHIP_NTK], spd[CHIP_NTK];   /* a table's number (0: none), its SPEED 1-4 */
    uint8_t vib_delay, vib_speed, vib_depth;          /* frames 0-62, 1-4, cents 0-62 */
} chip_patch_t;

typedef struct {
    chip_patch_t p;                      /* its own copy */
    uint8_t note, gate, dead, silent, sweeping, sw_off;
    uint8_t tp[CHIP_NTK], tc[CHIP_NTK];  /* each table's step, and the frames spent on it */
    int8_t arp;                          /* this frame's: the ARP table's semitones */
    uint8_t lvol, duty, nrate;           /* the VOL table's level (0-15), the pulse's width, the noise's RATE */
    int16_t cents;                       /* the PITCH table's and the vibrato's */
    uint8_t vol, rel, rel_c;             /* the step envelope's volume; the release's level (15-0), its frames */
    uint8_t step, sq_step, e_pos, nbit;  /* the oscillator's step (BUZZ: its place), the gate's, the PSG envelope's;
                                          * the noise's output */
    uint16_t bz_r, e_r;                  /* the buzz's and the PSG envelope's rounds */
    uint32_t frames;                     /* frames since the note (the vibrato's delay) */
    int32_t bend64;
    uint32_t f_acc, e_acc, s_acc;        /* towards the next frame, step-envelope step and sweep step (Q16 samples) */
    uint32_t reg;                        /* the period register (0: to find) */
    uint32_t len_x;                      /* CLOCK EXACT: the step's length as the sweep holds it (0: none) */
    uint32_t len_o, len_q, len_n, len_e; /* the steps' lengths: oscillator, BUZZ's gate, noise, PSG envelope
                                          * (Q16 samples; 0: not running) */
    uint32_t nx_o, nx_q, nx_n, nx_e;     /* their next steps, from the block's start (Q16 samples) */
    uint32_t lfsr;
    int32_t last;                        /* the level last put out (16-bit scale) */
    int32_t acc;                         /* the running sum of the impulses (Q12) */
    int32_t tail[CHIP_KW];               /* the impulses past the block (Q12) */
    int32_t hx[2], hy[2], ly;            /* the output filters: two high-passes, a low-pass */
} chip_voice_t;

uint32_t chip_bits(const uint8_t *b, uint32_t pos, int n);
void chip_unpack(const uint8_t *slot, chip_patch_t *p);
void chip_note_on(chip_voice_t *v, const chip_patch_t *p, int note);
void chip_pitch(chip_voice_t *v, int32_t bend64);
void chip_note_off(chip_voice_t *v);
void chip_cut(chip_voice_t *v);
void chip_set(chip_patch_t *p, uint32_t code, uint32_t value);   /* a parameter command on the settings alone */
void chip_param(chip_voice_t *v, uint32_t code, uint32_t value);
void chip_render(chip_voice_t *v, int32_t *out);
void chip_skip(chip_voice_t *v);
int chip_active(const chip_voice_t *v);
void chip_tuning(const chip_patch_t *p, int note, uint32_t *reg, int32_t *cents10);   /* the screen's register and
                                                                                       * how far off true */
/* the built-in tables for the screens: a kind's (CHIP_TVOL ...) table n (1 ...): its values, length and loop (-1: none),
 * 0 if none; a wave's 32 steps (0-15); the triangle's 32; a pulse width's 8 steps as bits (step s: bit s) */
const int8_t *chip_table(int kind, int n, int *len, int *loop);
const uint8_t *chip_wave(int wave);
const uint8_t *chip_tri(void);
uint32_t chip_duty_bits(int clock, int duty);
#endif
