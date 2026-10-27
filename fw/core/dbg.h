/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug build's figures (specs/debug.md): probes around the sound's stages and the main loop's, gathered
 * into a window the debug page reads over USB (SysEx 30 asks, 31 answers, 37 adds core 2's). Only with PTFM_DEBUG:
 * in the release every probe is empty and none of this is compiled. */
#ifndef PTFM_DBG_H
#define PTFM_DBG_H
#include <stdint.h>

/* the sound's stages, in the order a half runs them (specs/debug.md, The figures); some are parts of another
 * (DJ DECKS, DJ LFOS of CONTROL; CELLS, NOTES of REPLAY; the engines, MIX, J-CHORUS of VOICES; REVERB .. PUNCH-INS
 * of EFFECTS; USB AUDIO of OUTPUT). BLOCKS: the half's blocks handed to the effects (each block's settings and clock
 * kept with it, put back for its effects, the changes it made replayed) */
enum { DS_START, DS_CONTROL, DS_DJDECKS, DS_DJLFOS, DS_REPLAY, DS_CELLS, DS_NOTES, DS_VOICES, DS_DX7, DS_JUNO,
       DS_TB303, DS_SAMPLE, DS_MIX, DS_JCHORUS, DS_EFFECTS, DS_REVERB, DS_DELAY, DS_CHORUS, DS_DJFILTERS, DS_DJMIX,
       DS_INSERTS, DS_LIMITER, DS_PUNCH, DS_OUTPUT, DS_USB, DS_CLOCK, DS_BLOCKS, DS_MIDI, DS_END, DS_N };
/* the stages a half's time is the sum of (the others are parts of REPLAY, VOICES, EFFECTS or OUTPUT) */
#define DS_TOP ((1u << DS_START) | (1u << DS_CONTROL) | (1u << DS_REPLAY) | (1u << DS_VOICES) | (1u << DS_EFFECTS) | \
                (1u << DS_OUTPUT) | (1u << DS_CLOCK) | (1u << DS_BLOCKS) | (1u << DS_MIDI) | (1u << DS_END))
#define DBG_FIGURES_VERSION 11
#define DBG_WORST_LEN (8 + 2 * DS_N)            /* a worst half: its sum, voices, notes, blocks, its stages (66) */
#define DBG_DJ_LEN (1 + 2 * 24 + 136)           /* DJ: its flags, each deck as it plays, DJ's record (185) */
#define DBG_USB_LEN 16                          /* USB: audio's state and packets (plat_dbg_t's usb), FIGURES unsent */
#define DBG_FIGURES_LEN (18 + 6 * DS_N + 88 + DBG_WORST_LEN + DBG_DJ_LEN + DBG_USB_LEN)   /* FIGURES' bytes: the head,
                                                                * the stages, the rest, the worst half, DJ, USB (547) */
#define DBG_FIGURES2_VERSION 2
#define DBG_FIGURES2_LEN 56                     /* 37 FIGURES 2's bytes: core 2's window (specs/debug.md, Core 2's bytes) */

#ifdef PTFM_DEBUG
#include "plat.h"
extern uint32_t dbg_half[DS_N];                 /* this half's time by stage, us (the audio context's) */
extern uint16_t dbg_blocks_half[4];             /* this half's voice blocks by engine */
extern uint8_t dbg_notes_half;                  /* this half's notes started */
extern volatile uint8_t dbg_usb_tone;           /* 34 USB TONE: USB audio carries the test tone */
void dbg_tone_block(int16_t *lr, int n) PLAT_C2;   /* the test tone's next n frames, USB audio's 16-bit values (a
                                                 * block's job's: core 2's room) */
#define DBG_T(t) uint32_t t = plat_us()         /* a stage's start */
#define DBG_ADD(s, t) (dbg_half[s] += plat_us() - (t))
#define DBG_ADD_US(s, us) (dbg_half[s] += (us))
#define DBG_BLOCK(e) (dbg_blocks_half[e]++)
#define DBG_NOTE() (dbg_notes_half += dbg_notes_half < 255u)
/* the blocks' jobs' stages (specs/second-core.md: each block's effects and output, core 2's or core 1's running them),
 * counted apart: core 2 may still run a half's last blocks after the half's interrupt has ended. Folded into dbg_half
 * by dbg_half_end, at the next half's start (once core 2 has finished the half). Their time from core 2's own timer
 * read (plat_c2_us: the jobs' code lies in its room) */
extern uint32_t dbg2_half[DS_N];
#define DBG2_T(t) uint32_t t = plat_c2_us()
#define DBG2_ADD(s, t) (dbg2_half[s] += plat_c2_us() - (t))
#define DBG2_ADD_US(s, us) (dbg2_half[s] += (us))
/* the half whose stages (DS_TOP) summed most: its sum, voices, notes started, voice blocks by engine, stages; as
 * FIGURES and a bench step's record carry it (dbg_worst_put) */
typedef struct {
    uint16_t us, st[DS_N];
    uint8_t voices, notes, blocks[4];
} dbg_worst_t;
void dbg_worst_take(dbg_worst_t *w, const uint32_t *st, const uint16_t *bl, uint8_t voices, uint8_t notes,
                    uint32_t us);               /* this half into w if it took longer */
void dbg_worst_put(uint8_t *p, const dbg_worst_t *w);   /* DBG_WORST_LEN bytes */
void dbg_half_end(uint8_t voices);              /* the audio context, the next half's start: the half before (its
                                                 * jobs' stages with it) into the window */
void dbg_frame(uint32_t us);                    /* the main loop: a frame drawn, its time */
int dbg_frozen(void);                           /* FREEZE THE SCREEN on */
int dbg_sysex(const uint8_t *msg, uint32_t n);  /* 30 FIGURES?, 32, 34, 38: 1 taken (30 answered from dbg_poll) */
void dbg_poll(void);                            /* the main loop: an answer waiting is sent (31, then its 37) */
int dbg_figures(uint8_t *out, uint8_t *out2);   /* the window's FIGURES (DBG_FIGURES_LEN B) and core 2's FIGURES 2
                                                 * (DBG_FIGURES2_LEN B), a new window started */

/* the platform's own figures for the window (fw/device; a test's fake): since the last call, which starts
 * another window, but for the stacks' depth (since power-on). The caller zeroes it: 0 where a platform has none */
typedef struct {
    uint32_t isr_sum_us;                        /* the audio interrupt's time (core 1's), summed */
    uint16_t isr_max_us;                        /* its longest half */
    uint16_t halves, late;                      /* halves in the window, late ones (either core's: c2's late1 + late2) */
    uint16_t flash_reads, flash_us;             /* flash reads in the audio context, their time */
    uint16_t passes;                            /* main loop passes */
    uint32_t pass_max_us;                       /* the longest */
    uint16_t stack_used[2], stack_size[2];      /* the main loop's stack, the interrupts': deepest, size (B) */
    struct {                                    /* USB audio (specs/connections.md#usb-audio) */
        uint8_t on, r48;                        /* the stream fed now (the computer reads it); at 48 kHz */
        uint16_t pkts;                          /* packets sent */
        uint16_t dry;                           /* packets the buffer ran short for (the last frame repeated) */
        uint16_t missed;                        /* the computer's asks that found no packet ready */
        uint16_t stalls;                        /* the computer stopped reading (> 20 ms) */
        uint16_t starts;                        /* the stream started again (the buffer primed) */
        uint16_t fill, target;                  /* the buffer at the last half's start, its mark (frames) */
    } usb;
    struct {                                    /* core 2 (specs/second-core.md), as 37 FIGURES 2 carries it: since the
                                                 * last window, but the misreads (since power-on), its stack (since its
                                                 * start), its fault record and its start */
        uint32_t sum_us;                        /* its blocks' time, summed */
        uint16_t max_us;                        /* its longest half (the blocks of one half summed) */
        uint16_t blocks, halves;                /* the blocks handed over; the halves they went in */
        uint16_t misreads;                      /* entries it refused */
        uint32_t wait_sum_us;                   /* core 1's waits for it at the halves' starts, summed */
        uint16_t wait_max_us;                   /* the longest of them */
        uint16_t late1, late2;                  /* late halves core 1 caused (its interrupt past the half); core 2 (its
                                                 * last block not done when the next half began) */
        uint16_t stack_used, stack_size;        /* its jobs' stack: the deepest it went, its size (B) */
        uint32_t fault_vec, fault_pc;           /* its fault record if it faulted (0 none) */
        uint16_t start_10ns;                    /* its answer at the start, 10 ns (0 not started) */
        uint8_t clk;                            /* 1: its start needed the clock-divider bit */
    } c2;
    uint16_t sx_lost[2];                        /* SysEx messages it lost on the way in: begun while others waited
                                                 * for the main loop, cut short by another message (37's 54, 55) */
} plat_dbg_t;
void plat_dbg(plat_dbg_t *d);

/* ---- the bench (specs/debug.md, The bench; core/bench.c): measurements run on the page's word (SysEx 32), each
 * sent as a record (33) ---- */
#define DBG_BENCH_VERSION 5
enum { DBU_CYCLES, DBU_BYTES, DBU_CALL, DBU_BLOCK };    /* what a measurement's ops are: an op, bytes, a call, a block */
typedef struct {
    char name[21];
    uint8_t unit;                               /* DBU_* */
    uint8_t runs;                               /* the runs after the first */
    uint32_t ops;                               /* ops a run */
    uint32_t first;                             /* the first run, ticks */
    uint32_t min, max, sum;                     /* the runs after it, ticks */
} dbg_bench_t;
int dbg_send(uint8_t cmd, const uint8_t *b, uint32_t len);  /* a message out, packed: 0 not taken (dropped) */
int bench_sysex(int cmd);                       /* 32 BENCH: 1 run it, 0 stop it; 1 taken */
void bench_poll(void);                          /* the main loop: the bench's next piece, its records sent */

/* the platform's side of the bench */
#define PLAT_TICKS_HZ 24000000u
uint32_t plat_ticks(void);                      /* a free-running counter at PLAT_TICKS_HZ (the FM-1: TIMER4) */
void plat_cache_cold(void);                     /* the code cache emptied: the firmware's code and constants read
                                                 * from the flash again at their next use (interrupts on between) */
int plat_bench_core(int i, dbg_bench_t *r);     /* the platform's own measurement i (THE CORE): 1 done, 0 none (past
                                                 * the last); runs with the interrupts off, a few ms in all. r NULL:
                                                 * only whether there is an i */
#else
#define DBG_T(t)
#define DBG_ADD(s, t) ((void)0)
#define DBG_ADD_US(s, us) ((void)0)
#define DBG_BLOCK(e) ((void)0)
#define DBG_NOTE() ((void)0)
#define DBG2_T(t)
#define DBG2_ADD(s, t) ((void)0)
#define DBG2_ADD_US(s, us) ((void)0)
#endif

#endif
