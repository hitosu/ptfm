/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The second core on the FM-1 (specs/second-core.md): plat.h's side of it. The chip's side is hal/fm1_cpu1.h; the
 * holds before every reset are main.c's (fm1_cpu1_halt; c2_hold_reset before ours). Included by main.c after the
 * sound's part.
 *   the start   from the main loop, 2 s into every start (c2_poll), with SECOND CORE on, the debug build's 38 not
 *               holding it off and no restart but ours with it running since the power came on (the ban): interrupts
 *               off around fm1_cpu1_start. Once a run of the firmware: when SECOND CORE comes on later, then;
 *   a half      first thing in the audio interrupt (c2_half_start): the blocks handed over in the half before done
 *               (late when they were not yet, unless core 1 was late for that half: one dropout counted once), their
 *               time into the figures; then plat_c2_half decides for the half (never asked in CUT's), plat_c2_post
 *               hands each block over;
 *   fallbacks   no answer at the start, a misread, stuck (not done 1 ms into the next half), a fault: the HAL holds
 *               it, and core 1 runs the effects from that half on, for the rest of this start;
 *   the ban     c2_ban.runs, kept across restarts beside the crash record, says core 2 runs: set before its start's
 *               first try, cleared when a fallback or a failed start holds it and by the restarts we mean (an
 *               update's, the chip's USB update mode, the debug build's watchdog test: c2_hold_reset), never by a
 *               crash's. A start after any other restart (a crash, the watchdog) finding it set leaves core 2 off
 *               until a power-on (c2_boot);
 *   PROVE 4-6   the debug build's: the next block handed over faults, is misread or never ends, so each fallback is
 *               tried on the FM-1 (c2_prove, set by main.c's prove_sysex).
 * The main loop's flash paths and the debug build's bench wait for core 2 first (c2_quiet), interrupts off. */

#define C2_WAIT_HALF 1000u               /* us past a half's start: its blocks not done by then, stuck */
#define C2_WAIT_LONG (HALF_US + 1000u)   /* us elsewhere (a half's blocks are done within the half; then the same) */
#define C2_BAN_MAGIC 0x43324E4Fu         /* "C2NO" */
#define C2_RUNS 0x4332524Eu              /* "C2RN": c2_ban.runs set */

typedef struct {
    uint32_t magic;                      /* C2_BAN_MAGIC */
    uint32_t ban;                        /* 1: a restart not ours with core 2 running since the power came on */
    volatile uint32_t runs;              /* C2_RUNS: core 2 runs, or its start is under way (else 0) */
} c2_ban_t;
c2_ban_t c2_ban __attribute__((section(".noinit")));   /* (not static, as fm1_crash: never folded) */

static uint8_t c2_banned;                /* this start found c2_ban set: STOPPED, AFTER A CRASH */
static uint8_t c2_late1;                 /* the audio interrupt ran past the half it made (main.c, beside cpu_late):
                                          * core 2 still on that half's last block at the next half's start is the
                                          * same dropout, counted once, as core 1's */
static volatile uint8_t c2_want = 1;     /* SETTINGS' SECOND CORE (plat_c2_enable; its default ON till the app says) */
static volatile uint8_t c2_forced;       /* the debug build's 38: off whatever SETTINGS says */
static volatile uint8_t c2_pend;         /* blocks handed over and not waited for yet */
static uint8_t c2_used;                  /* blocks went to core 2 since the last half's start: its time to take */
static uint8_t c2_half_on;               /* this half's blocks go to core 2 (plat_c2_half) */
static volatile uint32_t c2_last, c2_max, c2_sum;   /* its jobs' time: the last half's, the most since reset, summed
                                          * since power-on (plat_cpu's) */
static volatile uint32_t c2_late;        /* halves its last block was not done for when the next began (core 1 in time
                                          * for them), since reset */
#ifdef PTFM_DEBUG
extern volatile uint8_t bench_running;   /* core/bench.c: its pieces run the effects' code on core 1 */
#define C2_BENCH bench_running
static volatile struct {                 /* the debug build's window (37 FIGURES 2): since plat_dbg last took it */
    uint32_t wait_sum, wait_max;         /* core 1's waits for core 2 at the halves' starts, us */
    uint32_t late;                       /* halves core 2 was late for (core 1 in time) */
    uint32_t blocks, halves;             /* the blocks handed over; the halves they went in */
    uint32_t sum, max;                   /* its jobs' time, summed; a half's most */
} c2_win;
static volatile uint8_t c2_prove;        /* PROVE 4-6 (main.c): the next block handed over faults, is misread, sticks */
static volatile uint8_t c2_prove_halves; /* the halves begun since it was asked: one that handed nothing over drops it */
#else
#define C2_BENCH 0
#endif
typedef char c2_whys[FM1_C1_WHY_NO_ANSWER == PLAT_C2_WHY_NO_ANSWER && FM1_C1_WHY_MISREAD == PLAT_C2_WHY_MISREAD &&
                     FM1_C1_WHY_STUCK == PLAT_C2_WHY_STUCK && FM1_C1_WHY_FAULT == PLAT_C2_WHY_FAULT ? 1 : -1];

/* every start, its variables set up: the ban as the restarts since the power came on left it. Core 2 still marked
 * as running (c2_ban.runs): the restart that brought this start was none of ours (a crash, the watchdog), with core 2
 * running or starting */
static void c2_boot(int power_on)
{
    if (power_on || c2_ban.magic != C2_BAN_MAGIC) {
        c2_ban.magic = C2_BAN_MAGIC;     /* (what a run before the power went off left counts for nothing) */
        c2_ban.ban = 0;
    } else if (c2_ban.runs == C2_RUNS) {
        c2_ban.ban = 1;
    }
    c2_ban.runs = 0;                     /* (held first thing at this start: fm1_cstart) */
    c2_banned = c2_ban.ban == 1u;
}

/* before a restart of ours (an update's, the chip's USB update mode, the debug build's watchdog test): core 2 held,
 * and not running at it for the next start (a crash's restart holds it alone: crash_wait_reboot) */
static void c2_hold_reset(void)
{
    fm1_cpu1_halt();
    c2_ban.runs = 0;
}

/* the main loop, `up` ms into this start: core 2 started once wanted, 2 s in */
static void c2_poll(uint32_t up)
{
    uint32_t f;
    if (fm1_c1.state != FM1_C1_OFF || c2_banned || !c2_want || c2_forced || up < 2000u)
        return;
    f = irq_save();
    c2_ban.runs = C2_RUNS;               /* (before its first try: a crash on core 1 while it starts bans it too) */
    if (fm1_cpu1_start())                /* (no answer: held, its why NO_ANSWER) */
        c2_ban.runs = 0;
    irq_restore(f);
}

/* core 1, interrupts off (the audio interrupt, or the main loop with them off): core 2 done with what it was handed,
 * us at most; else held (the HAL says why). 0 done; not 0: it does not run (held, or not started: its queue is not
 * read then), or is held now. Nothing handed over: at once */
static int c2_wait(uint32_t us)
{
    int rc = -1;
    if (fm1_c1.state == FM1_C1_RUNS)
        rc = c2_pend ? fm1_cpu1_wait_us(us) : 0;
    c2_pend = 0;
    if (rc) {
        c2_used = 0;                     /* (its time not taken: core 1 runs the effects from here) */
        c2_ban.runs = 0;                 /* (held by a fallback: a restart from here owes it nothing) */
    }
    return rc;
}
/* the main loop, interrupts off: before audio_silence (it clears the codec's halves core 2 may be writing), a bench
 * piece; and plat_c2_wait */
static void c2_quiet(void)
{
    if (c2_pend)
        c2_wait(C2_WAIT_LONG);
}

/* first thing in the audio interrupt, before anything core 2 works on changes (the half to write into, the output's
 * gains, USB audio's steering, the requests): the half before's blocks done, their time into the figures; a fault
 * since (its wait sees one only with blocks outstanding) holds it. Every half, CUT's too */
static void c2_half_start(void)
{
    uint32_t us = 0;
    uint8_t late1 = c2_late1;            /* (the half before late on core 1 already) */
    c2_late1 = 0;
    c2_half_on = 0;
#ifdef PTFM_DEBUG
    if (c2_prove && c2_prove_halves++)   /* (PROVE 4-6: the half since it was asked handed no block over, CUT's, SECOND
                                          * CORE off, 38: it does nothing) */
        c2_prove = 0;
#endif
    if (fm1_c1.state != FM1_C1_RUNS) {   /* (not started, or held: its queue is not read) */
        c2_pend = c2_used = 0;
        c2_last = 0;
        return;
    }
    if (c2_pend) {
#ifdef PTFM_DEBUG
        uint32_t t = fm1_ticks();
#endif
        if (!fm1_cpu1_idle() && !late1) {
            c2_late++;                   /* (its last block not done when this half began, core 1 in time) */
#ifdef PTFM_DEBUG
            c2_win.late++;
#endif
        }
        c2_wait(C2_WAIT_HALF);
#ifdef PTFM_DEBUG
        t = (fm1_ticks() - t) / FM1_TICKS_PER_US;
        c2_win.wait_sum += t;
        if (t > c2_win.wait_max)
            c2_win.wait_max = t;
#endif
    }
    if (fm1_c1.state == FM1_C1_RUNS && fm1_cpu1_faulted()) {
        fm1_cpu1_hold(FM1_C1_WHY_FAULT);
        c2_used = 0;
        c2_ban.runs = 0;
    }
    if (c2_used) {
        c2_used = 0;
        us = fm1_cpu1_half_us();
        c2_sum += us;
        if (us > c2_max)
            c2_max = us;
#ifdef PTFM_DEBUG
        c2_win.halves++;
        c2_win.sum += us;
        if (us > c2_win.max)
            c2_win.max = us;
#endif
    }
    c2_last = us;
}

#ifdef PTFM_DEBUG
/* PROVE 4-6 (specs/debug.md, Proving the layout): this block handed over as the test asks, in its job's place (the
 * block plays as silence): a job that faults (4), its own job with a wrong check word (5), a job that never ends (6);
 * 1 taken */
static int c2_prove_post(void (*job)(uint32_t), uint32_t j)
{
    int taken = c2_prove == 5u ? fm1_cpu1_post_bad(job, j) : fm1_cpu1_post(fm1_c1_prove_job, c2_prove == 6u);
    if (taken)
        c2_prove = 0;
    return taken;
}
#endif

/* ---- plat.h ---- */
int plat_c2_half(void)
{
    c2_half_on = fm1_c1.state == FM1_C1_RUNS && c2_want && !c2_forced && !C2_BENCH;
    return c2_half_on;
}

/* (not taken: its queue full, never as a half's blocks fit it, so done first; or it stopped. Core 1 runs a job only
 * once core 2 holds no slot: idle, or held) */
void plat_c2_post(void (*job)(uint32_t), uint32_t j)
{
    int taken;
#ifdef PTFM_DEBUG
    if (c2_prove && c2_half_on && c2_prove_post(job, j)) {   /* (the next job: a voice job's, past 10 voices) */
        c2_pend = c2_used = 1;
        c2_win.blocks += j != PLAT_C2_VOICE_JOB;
        return;
    }
#endif
    taken = c2_half_on && fm1_cpu1_post(job, j);
    if (c2_half_on && !taken && !c2_wait(C2_WAIT_LONG))
        taken = fm1_cpu1_post(job, j);
    if (taken) {
        c2_pend = c2_used = 1;
#ifdef PTFM_DEBUG
        c2_win.blocks += j != PLAT_C2_VOICE_JOB;   /* (the blocks: their effects' jobs; the voice jobs the player's) */
#endif
        return;
    }
    c2_half_on = 0;                      /* (this block and the half's rest on core 1) */
    job(j);
}

/* (at once when nothing is handed over: always after the half's start, which waited) */
void plat_c2_wait(void) { c2_quiet(); }

void plat_c2_enable(int on) { c2_want = on ? 1u : 0u; }   /* (a repeat changes nothing) */
void plat_c2_force_off(int off) { c2_forced = off ? 1u : 0u; }

/* plat_cpu's part: core 2's figures, state and why, its late halves added to core 1's */
static void c2_cpu(plat_cpu_t *c, int reset)
{
    uint8_t st = fm1_c1.state;
    c->late += c2_late;
    c->c2_last_us = c2_last;
    c->c2_max_us = c2_max;
    c->c2_sum_us = c2_sum;
    c->c2_why = c2_banned ? PLAT_C2_WHY_CRASH :
                st == FM1_C1_HELD || st == FM1_C1_FAILED ? fm1_c1.why : PLAT_C2_WHY_NONE;
    c->c2_state = c->c2_why ? PLAT_C2_STOPPED : !c2_want || c2_forced ? PLAT_C2_OFF :
                  st == FM1_C1_RUNS ? PLAT_C2_ON : PLAT_C2_STARTING;
    if (reset) {
        c2_max = 0;
        c2_late = 0;
    }
}
