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
 *   samp_skip(v)               a block without its sound (what the forward work stands for): its envelope and its
 *                              place go on
 *   samp_active(v)             it still sounds: cleared by samp_cut, by its place passing END (or, going back, the
 *                              sample's start) in samp_render or samp_skip, and by samp_render once it is let go and
 *                              its envelope is under its silent level (samp_skip never ends it so)
 *   ptr_read(d, rec, pos, n, out)   n samples of a PTR record (read in place) from pos on, 16-bit scale (any
 *                              reader)
 * A note without its voice (a ghost: ../fwd.h, specs/proposals/202611121830-ghost-notes.md) keeps its place in the
 * sample (samp_place_t), moved on by the time at each pitch it had, and comes back into a fresh voice at that place,
 * its envelope worked forward:
 *   samp_place_of(v, p)        a sounding voice's place
 *   samp_place_start(s, offset, p)   the place samp_note_on(v, s, note, offset) starts at
 *   samp_place_go(s, p, step, frames, held)   moved on as samp_skip's frames move a voice's
 *   samp_end_blocks(s, p, step, held)   the blocks until it ends by itself (SAMP_NEVER: it does not)
 *   samp_note_at(v, s, note, p)   samp_note_on at a place
 *   samp_place_set(v, p)       a voice not played yet since its note on put at a place
 *   samp_forward(v, on, off, steps)   its envelope worked on over its time (../fwd.h)
 *   samp_heard(v)              the most it still reaches (../fwd.h)
 *   samp_step(s, note, bend64)   the step samp_pitch gives (samples a frame, Q16)
 *   samp_prep(s)               an instrument made as samp_note_on makes its own copy (END, the loop's end, the
 *                              envelope within their ranges): what a ghost's place walks by
 */
#ifndef SAMP_H
#define SAMP_H
#include <stdint.h>
#include "../fwd.h"                      /* the forward work's contract, FWD_HELD */

#define SAMP_N 64
#define SAMP_WIN 128                     /* decoded samples a voice keeps at hand */
/* the engine's output scale (Q16), so that PT/FM's reference (specs/files/instruments.md: a full-scale 8-bit
 * sine, 441 Hz, looped whole, at its base note, the envelope's rates 99 and levels 99 99 99 0) comes out at
 * -20 dB RMS of the 16-bit full scale (its loudest 300 ms); applied to the envelope's gain */
#define SAMP_OUT_Q16 9330

enum { SAMP_LOOP_OFF, SAMP_LOOP_FORWARD, SAMP_LOOP_PINGPONG };

typedef struct {                         /* an instrument as it plays */
    const uint8_t *rec;                  /* its sample's PTR audio (in place: the sample's record after its entry,
                                          * specs/files/instruments.md#samples); NULL: none */
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

/* Where a note is in its sample, without its voice: 6 B, what samp_skip's walk reads and moves of a voice (its pos,
 * frac, dir and looping, and whether it has ended): the position (samples, 29 bits) and its fraction (Q16), going
 * back or not, in its sustain loop or not, ended or not. A sample holds 2^20 samples at most (its entry's length), and
 * every walk keeps the position within it but one whose loop runs away: a FORWARD loop held at a step past its length
 * (its position on by the step less the loop a frame, 14 samples at the most), or one whose end commands put under its
 * start (on by the step and the points' distance): its voice's position grows past the sample, where it reads
 * silence but for the loop start's sample at the seam, until its 32-bit range runs out. samp_place_go ends such a walk
 * where the position would pass SAMP_PL_MOST (the shorter loop after 14 minutes at the soonest; the turned one sooner,
 * a quarter of the way to where its voice's own position overflows). A LOOP command turned off clears SAMP_PL_LOOP,
 * as it clears a sounding voice's looping */
typedef struct {
    uint16_t frac;                       /* the position's fraction (Q16) */
    uint16_t lo, hi;                     /* the position: lo + (hi & SAMP_PL_POS) << 16; the flags SAMP_PL_* in hi */
} samp_place_t;
#define SAMP_PL_POS 0x1FFFu
#define SAMP_PL_MOST 0x1FFFFFFF          /* the furthest position kept */
#define SAMP_PL_BACK 0x2000u             /* a ping-pong pass going back */
#define SAMP_PL_LOOP 0x4000u             /* in its sustain loop (repeated while held) */
#define SAMP_PL_END 0x8000u              /* ended: past END, or going back past the sample's start */
#define SAMP_NEVER 0xFFFFFFFFu           /* samp_end_blocks: it does not end by itself */

void samp_prep(samp_t *s);
uint32_t samp_step(const samp_t *s, int note, int32_t bend64);
void samp_place_of(const samp_voice_t *v, samp_place_t *p);
void samp_place_start(const samp_t *s, int32_t offset, samp_place_t *p);
/* p moved on `frames` frames (the player's blocks x SAMP_N) at `step` (samp_step's), its key held or not, exactly as
 * samp_skip's (samp_render's) frames move a voice's place: s its voice's copy of the instrument (samp_prep's, with the
 * parameter commands as they stand: END, LOOP, the loop's points); a pitch change between stretches is a call each.
 * In one sum, not frame by frame: a run up to the next event (the loop's end, END, a mirror), the loop's turns held (a
 * FORWARD loop's, a PING-PONG's to and fro) by their remainder, the odd ways (a place past the loop, a step past the
 * loop's length) by their own sums. Returns its steps, the stretches walked (4 at most: a PING-PONG's pass back
 * turned at its start, its turns of two from past its end, its mirror into it, then to and fro in it). An ended place
 * stays */
uint32_t samp_place_go(const samp_t *s, samp_place_t *p, uint32_t step, uint32_t frames, int held);
/* the blocks from p (at a block's start) until its note ends by itself, by its place, at `step`, held or not: the
 * block in which samp_skip or samp_render would turn samp_active false (1: the next block); 0: already ended;
 * SAMP_NEVER: not within 2^32 frames (held in its loop it never does). The envelope's own end, once let go (samp_render
 * alone), is samp_heard's 0 */
uint32_t samp_end_blocks(const samp_t *s, const samp_place_t *p, uint32_t step, int held);
/* samp_note_on(v, s, note, 0), then the place p (its position, way and loop; ended: silent). The decoder seeks there
 * when the voice first plays, as SAMPLE OFFSET's start does. s is the instrument as it was at the note's start (the
 * player's pl_sample_of with START applied, before LOOP START and the other commands), and the commands are applied
 * after samp_note_at as pl_voice_on applies them after samp_note_on: a FORWARD loop's seam sample (ls_val) is read by
 * the note on at the loop's start as s has it, and only so is it the one the note never taken read */
void samp_note_at(samp_voice_t *v, const samp_t *s, int note, const samp_place_t *p);
/* a voice not yet played since its note on put at place p (its position, way and loop; ended: silent), as samp_note_at
 * puts it: the place a ghost's return under way has moved on to (the player's), the decoder seeking there when the
 * voice first plays */
void samp_place_set(samp_voice_t *v, const samp_place_t *p);
/* the forward work (../fwd.h), in blocks of SAMP_N frames: the envelope (its stage, level, target, the static stages'
 * frames, rising, the key) comes out as those blocks of samp_skip (and samp_note_off at the let-go) leave it, and so
 * does amp (the last block's gain); the place (pos, frac, dir, looping, and the end it may reach, which stops
 * samp_skip's envelope) is not moved: it is the place's (samp_place_go, samp_note_at). Its steps: a stretch a step (a
 * static stage's frames, a stage falling to its target, a rising one a band of its level at a time: 9 bands from where
 * a rise starts (about -50 dB) to full, a block's step m x the rate's, m from 10 down to 2; the envelope
 * standing, its sustain held or its release done, read once a step); the note off a step. A call leaves
 * *steps at 0 at the lowest (one entered with 1 takes a stretch); its note off done once, its key telling it was. A
 * voice that does not sound is done at once */
int samp_forward(samp_voice_t *v, uint32_t *on, uint32_t *off, int32_t *steps);
/* the most the note still reaches without a new stage rising (../fwd.h), on samp_render's scale: the envelope's gain
 * (a stage rising now counted at its target; the last block's gain, where the next block ramps from) times the output
 * scale SAMP_OUT_Q16, as a full-scale sample would play (a sample's own peak is not in its record): no gain of the
 * player's in it. The block rendered next peaks at most at it but where a static stage ends into a rising one in that
 * block, and never over the larger of it and samp_heard after that block. 0 for a voice that does not sound, and once
 * its envelope is silent with no stage rising (let go, samp_render ends it as the envelope falls on to its silent
 * level) */
int32_t samp_heard(const samp_voice_t *v);
#endif
