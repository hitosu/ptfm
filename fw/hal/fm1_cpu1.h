/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (the second core's HAL this follows)
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (the safe hold before the start, its tries
 * and every reset: fm1_sys.h's fm1_cpu1_halt)
 * Copyright (C) 2026 Kerem Kilic (its start, its loop in RAM, its own stack limit, the check word: first run on the
 * FM-1)
 * Copyright (C) 2026 zednaked (each core's words on a cache line of their own)
 * Copyright (C) 2026 Vasily Kazantsev (the queue, the room, the fallbacks' answers)
 * The start follows the AC79 SDK's (JieLi, Apache-2.0). */
/* The AC791N's second core (specs/second-core.md). The chip's numbering here: its core 1 (C1_CON, cnum 1) is PT/FM's
 * core 2, the screens' CPU 2; app.ld's sections and symbols use PT/FM's (.c2_*, _c2_*).
 *
 * Start (fm1_cpu1_start, from the main loop, interrupts off): held first; its stacks filled with a mark; its
 * interrupt bank (0x1EEF300, 32 words; core 1's at 0x1EEF100) all off; its EMU flags cleared and its stack limit off
 * (the chip's ROM runs first, on a stack of its own inside the jobs', outside any window); its entry (fm1_cpu1.S)
 * into vector 126's word (0x01C7FFF8), the word the chip starts it from, the top's write limit let go around that
 * write; the PC limits opened wide (the ROM is outside them); C1_CON (0x1EEE004) bit 3 set, bit 1 cleared. It
 * answers from its loop within 20 ms (0.41 us measured), or not at all: one try, without the clock divider's bit the
 * SDK sets for the start (it was never needed on the FM-1, and a second try would be a start after a hold). Then
 * vector 126 back to its fatal stub, the top's limit and the PC limits armed again. One start a run of the firmware:
 * a start after a start and a hold faults in the ROM (measured on the FM-1); the hold before a first start (every
 * start of the firmware holds it) does not.
 *
 * Its loop (fm1_c1_loop, in RAM at or above 0x01C72000 with its queue: app.ld) arms its own stack limit, says it is
 * there, then runs what core 1 hands over, in order, until held. It takes no interrupts and calls nothing but the
 * jobs. A core 2 a reset did not hold spins on there, reading its queue: neither a restart nor the update loader
 * writes there.
 *
 * The queue: core 1 writes entry n (at e[n & 3]: a function, its argument and the check word of both and n), then
 * posted = n; core 2 runs entry done + 1 once posted has moved past done and its check word matches, adds the job's
 * TIMER4 time to t_half, then writes done. A check word that fails is a misread (an FM-1's second core was seen
 * reading RAM wrong): core 2 counts it, leaves the entry and runs nothing more until held. Each core's words on a
 * cache line of their own; csync after writing what the other core reads and before reading what it wrote (no cache
 * upkeep for RAM). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"                            /* fm1_ticks_inline */
#include "fm1_sys.h"                             /* fm1_cpu1_halt */
#include "fm1_irq.h"                             /* FM1_VEC, fm1_fatal_stubs, fm1_c1_fault, fm1_c1_runs */
#include "fm1_guard.h"                           /* fm1_guard_pc_open, fm1_guard_top_open, fm1_guard_enable */

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define FM1_C1_ICFG(i) (*(volatile uint32_t *)(0x1EEF300u + 4u * (i)))   /* its interrupt bank */
#define FM1_C1_EMU_CON (*(volatile uint32_t *)0x1EEF2D0u)  /* its EMU bank: core 1's (fm1_irq.h) + 0x200 */
#define FM1_C1_EMU_MSG (*(volatile uint32_t *)0x1EEF2D4u)
#define FM1_C1_EMU_SSP_H (*(volatile uint32_t *)0x1EEF2D8u)
#define FM1_C1_EMU_SSP_L (*(volatile uint32_t *)0x1EEF2DCu)
#define FM1_C1_EMU_USP_H (*(volatile uint32_t *)0x1EEF2E0u)
#define FM1_C1_EMU_USP_L (*(volatile uint32_t *)0x1EEF2E4u)
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")
/* the same for code outside hal/ (USB audio's ring, the clocks core 2 reads): always inlined, as core 2's code calls
 * only code in its room */
FM1_INLINE void fm1_csync(void) { FM1_C1_SYNC(); }
#define FM1_C1_VEC 126u                          /* the word core 2 starts from */
#define FM1_C1_MARK 0x43324D4Bu                  /* "C2MK": how deep its stacks went */
#define FM1_C1_ENTRIES 4u                        /* a half's blocks */
#define FM1_C1_CHECK(fn, arg, n) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (n) * 0x9E3779B1u)

extern void fm1_c1_entry(void);
extern uint32_t _c2_ustack_lo[], _c2_ustack_top[], _c2_sstack_lo[], _c2_sstack_top[];

/* the queue, in core 2's room (app.ld's .c2_queue, above 0x01C72000) */
typedef struct {
    volatile uint32_t posted;                   /* core 1's line: the last entry handed over */
    struct {
        void (*volatile fn)(uint32_t);
        volatile uint32_t arg, check;           /* check: FM1_C1_CHECK of fn, arg and the entry's number */
    } e[FM1_C1_ENTRIES];
    uint32_t pad0[3];
    volatile uint32_t alive, done;              /* core 2's line: it answered from its loop; the last entry it ran */
    volatile uint32_t bad, bad_n;               /* entries it refused (it stops at the first), the last one */
    volatile uint32_t t_half;                   /* its jobs' time in TIMER4 ticks, since core 1 took it last */
    uint32_t pad1[11];
} fm1_c1_queue_t;
fm1_c1_queue_t fm1_c1_q __attribute__((section(".c2_queue"), aligned(64)));
typedef char fm1_c1_lines[sizeof(fm1_c1_queue_t) == 128u ? 1 : -1];   /* (a cache line each) */

/* what core 1 keeps */
enum { FM1_C1_OFF, FM1_C1_RUNS, FM1_C1_HELD, FM1_C1_FAILED };
/* what held it: plat.h's PLAT_C2_WHY_* in the same order */
enum { FM1_C1_WHY_NONE, FM1_C1_WHY_NO_ANSWER, FM1_C1_WHY_MISREAD, FM1_C1_WHY_STUCK, FM1_C1_WHY_FAULT };
static struct {
    uint8_t state;                              /* FM1_C1_* */
    uint8_t why;                                /* FM1_C1_WHY_* (or the caller's): what held it while it ran */
    uint8_t started;                            /* its start ran (answered or not) */
    uint32_t start_ticks;                       /* its answer at the start, TIMER4 ticks */
    uint32_t faults;                            /* its fault record's count at the start: a fault since counts past */
} fm1_c1;

void fm1_c1_loop(void);
/* core 2's loop: in RAM, never returns, calls nothing but the jobs (build.py checks core 2's room) */
void __attribute__((section(".c2_top"), noreturn, used, noinline)) fm1_c1_loop(void)
{
    uint32_t lo = (uint32_t)(uintptr_t)_c2_ustack_lo + 256u, hi = (uint32_t)(uintptr_t)_c2_sstack_top, done, n;
    /* its own stack limit: one window over both its stacks less their lowest 256 B (its EMU bank), the limit on
     * (EMU_CON bit 3), the divide-by-0 trap (bit 2) and bits 16..20 off */
    FM1_C1_EMU_SSP_H = hi;
    FM1_C1_EMU_SSP_L = lo;
    FM1_C1_EMU_USP_H = hi;
    FM1_C1_EMU_USP_L = lo;
    FM1_C1_EMU_CON = (FM1_C1_EMU_CON & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    done = fm1_c1_q.done;
    fm1_c1_q.alive = 1;
    FM1_C1_SYNC();
    for (;;) {
        void (*fn)(uint32_t);
        uint32_t arg, t;
        n = fm1_c1_q.posted;
        FM1_C1_SYNC();
        if (n == done)
            continue;
        n = done + 1u;
        fn = fm1_c1_q.e[n & 3u].fn;
        arg = fm1_c1_q.e[n & 3u].arg;
        if (!fn || fm1_c1_q.e[n & 3u].check != FM1_C1_CHECK(fn, arg, n))
            break;
        t = fm1_ticks_inline();
        fn(arg);
        fm1_c1_q.t_half += fm1_ticks_inline() - t;
        done = n;
        FM1_C1_SYNC();
        fm1_c1_q.done = n;
        FM1_C1_SYNC();
    }
    fm1_c1_q.bad_n = n;                          /* a misread: counted, the entry left, nothing more run */
    FM1_C1_SYNC();
    fm1_c1_q.bad++;
    FM1_C1_SYNC();
    for (;;)
        ;
}

/* hold it (any time, whatever it does; interrupts as they are), saying why when it ran (FM1_C1_WHY_*) */
static void fm1_cpu1_hold(uint32_t why)
{
    fm1_cpu1_halt();
    fm1_c1_runs = 0;
    if (fm1_c1.state == FM1_C1_RUNS) {
        fm1_c1.state = FM1_C1_HELD;
        fm1_c1.why = (uint8_t)why;
    }
}

/* the try (held, set up): 1 it answered within 20 ms */
static int fm1__cpu1_try(void)
{
    uint32_t t0;
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = fm1_ticks_inline();
    while (!fm1_c1_q.alive && fm1_ticks_inline() - t0 < 20000u * FM1_TICKS_PER_US)
        FM1_C1_SYNC();
    fm1_c1.start_ticks = fm1_ticks_inline() - t0;
    return fm1_c1_q.alive != 0u;
}
/* start it (interrupts off: the caller's), once a run: 0 it answered; -1 it did not (held: FM1_C1_WHY_NO_ANSWER), or
 * it was started before */
static int fm1_cpu1_start(void)
{
    uint32_t i, top, *s;
    volatile uint32_t *q;
    int ok;
    if (fm1_c1.state != FM1_C1_OFF)
        return -1;
    fm1_cpu1_halt();
    for (s = _c2_ustack_lo; s < _c2_sstack_top; s++)
        *s = FM1_C1_MARK;
    for (q = (volatile uint32_t *)&fm1_c1_q; q < (volatile uint32_t *)(&fm1_c1_q + 1); q++)
        *q = 0;
    fm1_c1.faults = fm1_c1_fault.magic == FM1_C1_FAULT_MAGIC ? fm1_c1_fault.count : 0u;
    FM1_C1_SYNC();
    for (i = 0; i < 32u; i++)
        FM1_C1_ICFG(i) = 0;
    FM1_C1_EMU_CON &= ~(1u << 3);                /* (fm1_c1_loop arms it again) */
    FM1_C1_EMU_MSG = 0xFFFFFFFFu;
    top = fm1_guard_top_open();
    FM1_VEC[FM1_C1_VEC] = (uint32_t)(uintptr_t)&fm1_c1_entry;
    fm1_guard_pc_open();
    ok = fm1__cpu1_try();
    fm1_c1.started = 1;
    if (ok) {
        /* the ROM's start stack lies over the mark (0x01C6F800-0x01C70000, below the jobs' stack's top 1 KB: app.ld);
         * laid again there, its loop standing at the top and nothing handed over yet */
        for (s = _c2_ustack_lo; s < _c2_ustack_top - 256; s++)
            *s = FM1_C1_MARK;
        fm1_c1.state = FM1_C1_RUNS;
        fm1_c1_runs = 1;
    } else {
        fm1_cpu1_halt();
        fm1_c1.state = FM1_C1_FAILED;
        fm1_c1.why = FM1_C1_WHY_NO_ANSWER;
    }
    FM1_VEC[FM1_C1_VEC] = (uint32_t)(uintptr_t)(fm1_fatal_stubs + 6u * FM1_C1_VEC);
    fm1_guard_top_close(top);
    fm1_guard_enable(FM1_GUARD_PC);
    return ok ? 0 : -1;
}

/* hand it fn(arg), the next entry (the audio context): 1 taken; 0 not (it does not run, or FM1_C1_ENTRIES are not
 * finished yet: wait first) */
static int fm1_cpu1_post(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t n;
    if (fm1_c1.state != FM1_C1_RUNS)
        return 0;
    n = fm1_c1_q.posted + 1u;
    FM1_C1_SYNC();
    if (n - fm1_c1_q.done > FM1_C1_ENTRIES)
        return 0;
    fm1_c1_q.e[n & 3u].fn = fn;
    fm1_c1_q.e[n & 3u].arg = arg;
    fm1_c1_q.e[n & 3u].check = FM1_C1_CHECK(fn, arg, n);
    FM1_C1_SYNC();
    fm1_c1_q.posted = n;
    FM1_C1_SYNC();
    return 1;
}
#ifdef PTFM_DEBUG
/* the debug build's PROVE 4-6 (specs/debug.md, Proving the layout): what core 2 is handed instead of a block's job.
 * fm1_c1_prove_job(0) faults core 2 by its own stack limit: the jobs' stack's window (its EMU bank, which only it
 * uses) shut above where its stack pointer stands, then a push. A fault of its own EMU bank is taken on core 2
 * itself (measured on the FM-1: its record kept, parked); a bus error (a load nothing answers) would be flagged in
 * the debug unit both cores share and taken on core 1, its crash screen and a restart. The exceptions' stack keeps
 * its window, so the fault's own frames fit. (1), or a push the chip let through, never ends: then it ends as STUCK
 * at the next half, never on core 1. fm1_cpu1_post_bad hands fn(arg) over with a wrong check word: core 2 refuses it
 * (a misread) */
void fm1_c1_prove_job(uint32_t stuck);
void __attribute__((section(".c2_text"), noinline, used)) fm1_c1_prove_job(uint32_t stuck)
{
    if (!stuck) {
        FM1_C1_EMU_USP_L = FM1_C1_EMU_USP_H;    /* (the window [top, top]: its stack pointer below it) */
        FM1_C1_SYNC();
        __asm__ volatile("[--sp] = {rets}\n\t{rets} = [sp++]" ::: "memory");   /* (a push, and back) */
    }
    for (;;)
        FM1_C1_SYNC();
}
static int fm1_cpu1_post_bad(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t n;
    if (fm1_c1.state != FM1_C1_RUNS)
        return 0;
    n = fm1_c1_q.posted + 1u;
    FM1_C1_SYNC();
    if (n - fm1_c1_q.done > FM1_C1_ENTRIES)
        return 0;
    fm1_c1_q.e[n & 3u].fn = fn;
    fm1_c1_q.e[n & 3u].arg = arg;
    fm1_c1_q.e[n & 3u].check = ~FM1_C1_CHECK(fn, arg, n);
    FM1_C1_SYNC();
    fm1_c1_q.posted = n;
    FM1_C1_SYNC();
    return 1;
}
#endif
/* it has run every entry handed over */
static int fm1_cpu1_idle(void)
{
    FM1_C1_SYNC();
    return fm1_c1_q.done == fm1_c1_q.posted;
}
/* it faulted since its start (its record: fm1_c1_fault) */
static int fm1_cpu1_faulted(void)
{
    FM1_C1_SYNC();
    return fm1_c1_fault.magic == FM1_C1_FAULT_MAGIC && fm1_c1_fault.count != fm1_c1.faults;
}
/* until it has run every entry handed over (while it runs), us at most: 0 done; -1 not in time (held:
 * FM1_C1_WHY_STUCK); -2 it refused an entry (held: _MISREAD); -3 it faulted (held: _FAULT) */
static int fm1_cpu1_wait_us(uint32_t us)
{
    uint32_t t0 = fm1_ticks_inline();
    for (;;) {
        FM1_C1_SYNC();
        if (fm1_c1_q.done == fm1_c1_q.posted)
            return 0;
        if (fm1_cpu1_faulted()) {
            fm1_cpu1_hold(FM1_C1_WHY_FAULT);
            return -3;
        }
        if (fm1_c1_q.bad) {
            fm1_cpu1_hold(FM1_C1_WHY_MISREAD);
            return -2;
        }
        if (fm1_ticks_inline() - t0 > us * FM1_TICKS_PER_US) {
            fm1_cpu1_hold(FM1_C1_WHY_STUCK);
            return -1;
        }
    }
}
/* its jobs' time since the last call, us, the rest of a us kept (core 1, with it idle: at a half's start once it has
 * finished the half before) */
static uint32_t fm1_cpu1_half_us(void)
{
    uint32_t t = fm1_c1_q.t_half;
    fm1_c1_q.t_half = t % FM1_TICKS_PER_US;
    FM1_C1_SYNC();
    return t / FM1_TICKS_PER_US;
}
/* how deep its jobs' stack went since its start, bytes (from the marks the start laid), and its size */
static uint32_t fm1_cpu1_stack_used(void)
{
    const uint32_t *s = _c2_ustack_lo;
    while (s < _c2_ustack_top && *s == FM1_C1_MARK)
        s++;
    return (uint32_t)((const uint8_t *)_c2_ustack_top - (const uint8_t *)s);
}
static uint32_t fm1_cpu1_stack_size(void)
{
    return (uint32_t)((const uint8_t *)_c2_ustack_top - (const uint8_t *)_c2_ustack_lo);
}
