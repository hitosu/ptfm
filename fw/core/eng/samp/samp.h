/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A sample voice: a sample instrument (specs/files/instruments.md#sample-instruments) playing its PTR audio
 * (specs/files/ptr.md: 8 or 16 bits, coded losslessly, decoded as it plays), at any pitch (linear
 * interpolation), from START to END, with a sustain loop (FORWARD or PING-PONG) repeated while the key is
 * held; let go, it goes on from where it is through the rest of the loop to END while its envelope (a DX7
 * operator's) releases.
 *
 *   samp_note_on(v, s, note, offset)   note: MIDI; the BASE NOTE plays the sample at its rate; it starts
 *                              offset samples after START (SAMPLE OFFSET)
 *   samp_note_off(v)           let go
 *   samp_pitch(v, note, bend64)   its pitch again: note, bent by bend64 / 64 semitones (TUNE added)
 *   samp_cut(v)                silence now
 *   samp_render(v, out)        ADDS SAMP_N frames to out, the 16-bit scale (a full-scale 8-bit sample is
 *                              32767 before the envelope), times SAMP_OUT_Q16, the engine's output scale
 *   samp_skip(v)               a block without its sound (a parked voice): its envelope and its place go on
 *   samp_active(v)             it still sounds
 *   ptr_read(d, rec, pos, n, out)   n samples of a PTR record (read in place) from pos on, 16-bit scale (any
 *                              reader)
 */
#ifndef SAMP_H
#define SAMP_H
#include <stdint.h>

#define SAMP_N 64
#define SAMP_WIN 128                     /* decoded samples a voice keeps at hand */
/* the engine's output scale (Q16), so that PT/FM's reference (specs/files/instruments.md: a full-scale 8-bit
 * sine, 441 Hz, looped whole, at its base note, the envelope's rates 99 and levels 99 99 99 0) comes out at
 * -20 dB RMS of the 16-bit full scale (its loudest 300 ms); applied to the envelope's gain */
#define SAMP_OUT_Q16 9330

enum { SAMP_LOOP_OFF, SAMP_LOOP_FORWARD, SAMP_LOOP_PINGPONG };

typedef struct {                         /* an instrument as it plays */
    const uint8_t *rec;                  /* its sample's PTR record (in place); NULL: none */
    uint32_t rec_len;
    int32_t length;                      /* the sample's samples */
    uint32_t rate;                       /* Hz at the BASE NOTE */
    int32_t start, end;                  /* where a note starts; where it ends (exclusive) */
    int32_t loop_start, loop_end;        /* the sustain loop (end exclusive) */
    uint8_t loop;                        /* SAMP_LOOP_* */
    uint8_t base;                        /* BASE NOTE */
    int16_t tune;                        /* 1/128 semitones */
    uint8_t env[8];                      /* R1-R4, L1-L4 */
} samp_t;

typedef struct {                         /* a PTR decoder, streaming forward */
    const uint8_t *rec;                  /* the record in place */
    uint32_t at;                         /* the next byte's offset in the record */
    uint32_t rec_len;
    int32_t pos;                         /* the next sample it gives */
    int32_t length;
    int32_t left;                        /* samples left in the block */
    int32_t warm;                        /* raw samples still to read (a predictor's first ones, or a raw block) */
    int32_t mode, part, k;
    int32_t x1, x2, x3;
    uint32_t bits;                       /* the bit reader: nbits valid bits, MSB first */
    int32_t nbits;
    uint8_t wide;                        /* 16-bit samples */
} ptr_dec_t;

typedef struct {
    samp_t s;                            /* its own copy (parameter commands change it) */
    ptr_dec_t dec;
    int16_t win[SAMP_WIN];               /* decoded samples w0 .. w0 + wn - 1 */
    int32_t w0, wn;
    int16_t ls_val;                      /* the loop start's sample (a forward loop's seam) */
    int32_t pos;                         /* the position, and its fraction (Q16) */
    uint32_t frac, step;                 /* samples a frame, Q16 */
    int8_t dir;                          /* 1 forward, -1 back (a ping-pong pass) */
    uint8_t on, looping, note;
    /* the envelope (MSFA's env.cc): its stage (0-2 to L1-L3, then held; 3 releasing to L4; 4 done), its level
     * now and where it goes in MSFA's units (Q24 octaves), rising or not, a static stage's frames left, the
     * key down or not, the last block's gain (Q15) */
    uint8_t stage, rising, down;
    int32_t level, target, statics, amp;
} samp_voice_t;

int32_t ptr_read(ptr_dec_t *d, const uint8_t *rec, uint32_t rec_len, int32_t pos, int32_t n, int16_t *out);
void samp_note_on(samp_voice_t *v, const samp_t *s, int note, int32_t offset);
void samp_pitch(samp_voice_t *v, int note, int32_t bend64);
void samp_note_off(samp_voice_t *v);
void samp_cut(samp_voice_t *v);
void samp_render(samp_voice_t *v, int32_t *out);
void samp_skip(samp_voice_t *v);
int samp_active(const samp_voice_t *v);
int32_t samp_env_q15(const samp_voice_t *v);   /* the envelope's gain now, Q15 (the screens' picture) */
#endif
