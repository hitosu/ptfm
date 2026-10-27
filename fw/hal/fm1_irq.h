/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Kerem Kilic (the second core's faults told apart by cnum)
 * Copyright (C) 2026 Vasily Kazantsev (cut: what the controller does not call; core 2's faults) */
/* FM-1 interrupt / exception HAL.
 *
 *   fm1_irq_init()        first thing in cstart: all ICFG off, pendings and
 *                         exception causes cleared, all 128 vectors -> fatal
 *                         stubs (fm1_vec.S), vector 1 (CPU
 *                         exception) enabled at prio 7, ETM on, div0 trap
 *                         off (see below).
 *   fm1_irq_attach(n, h, prio)   h = asm wrapper (fm1_isr.S), prio 0..7
 *   fm1_irq_enable_all()  icfg bit 8 + sti, after every source is set up
 *
 * The application supplies fm1_fault(const fm1_crash_t *) which reports the
 * crash (LCD) and resets; the record survives in .noinit for the next boot.
 * One vector table serves both cores: a fault on core 2 (the chip's core 1,
 * fm1_cpu1.h) keeps its own record in core 2's room and parks it there.
 */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"                                /* fm1_cpu1_halt */

#define FM1_VEC ((volatile uint32_t *)0x01C7FE00u)
#define FM1_ICFG(n) (*(volatile uint32_t *)(0x1EEF100u + 4u * ((n) >> 3)))
#define FM1_IPND(i) (*(volatile uint32_t *)(0x1EEF180u + 4u * (i)))
#define FM1_ILAT_SET (*(volatile uint32_t *)0x1EEF1A0u)
#define FM1_ILAT_CLR (*(volatile uint32_t *)0x1EEF1A4u)
#define FM1_EMU_CON (*(volatile uint32_t *)0x1EEF0D0u)
#define FM1_EMU_MSG (*(volatile uint32_t *)0x1EEF0D4u)
#define FM1_ETM_CON (*(volatile uint32_t *)0x1EEF1C0u)
#define FM1_ETM_PC(i) (*(volatile uint32_t *)(0x1EEF1C4u + 4u * (i)))
#define FM1_DBG_WR_EN (*(volatile uint32_t *)0x1EEE240u)
#define FM1_DBG_MSG (*(volatile uint32_t *)0x1EEE244u)
#define FM1_DBG_MSG_CLR (*(volatile uint32_t *)0x1EEE248u)
#define FM1_DBG_EN (*(volatile uint32_t *)0x1EEE340u)

enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };

/* the core it runs on, the chip's numbering (0 core 1, 1 core 2); always inlined, as core 2's code in RAM calls only
 * code there */
static inline __attribute__((always_inline)) uint32_t fm1_cnum(void)
{
    uint32_t v;
    __asm__ volatile("%0 = cnum" : "=r"(v));
    return v;
}
static inline uint32_t fm1_icfg(void) { uint32_t v; __asm__ volatile("%0 = icfg" : "=r"(v)); return v; }
static inline void fm1_icfg_set(uint32_t v) { __asm__ volatile("icfg = %0" ::"r"(v) : "memory"); }
static inline void fm1_irq_off(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void fm1_irq_on(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }

#define FM1_CRASH_MAGIC 0x43525348u          /* "CRSH" */
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;                          /* consecutive crashes < 3 s after boot */
    uint32_t stack;                          /* FM1_STACK_*: the stack found past its bottom (or in the guard band
                                              * below it) at the fault, or none */
    uint32_t c2;                             /* core 2 running at the fault: 1, else 0 (fm1_c1_runs) */
} fm1_crash_t;
enum { FM1_STACK_NONE = 0, FM1_STACK_MAIN = 1, FM1_STACK_IRQ = 2 };

/* the stacks' ends (app.ld): the main loop's on usp, the interrupts' on ssp */
extern char _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
/* which stack a pointer pair is out of: the interrupts' (ssp past its ends), else the main loop's (usp), else none */
static uint32_t fm1_stack_out(uint32_t usp, uint32_t ssp)
{
    if (ssp < (uint32_t)(uintptr_t)_sstack_lo || ssp > (uint32_t)(uintptr_t)_sstack_top)
        return FM1_STACK_IRQ;
    if (usp < (uint32_t)(uintptr_t)_ustack_lo || usp > (uint32_t)(uintptr_t)_ustack_top)
        return FM1_STACK_MAIN;
    return FM1_STACK_NONE;
}
/* Not static: .noinit holds whatever the last run (or power-on) left, and a
 * file-local object would let the compiler assume its zero initial value and
 * fold the magic check away. */
fm1_crash_t fm1_crash __attribute__((section(".noinit")));
/* core 2 started and not held since (fm1_cpu1.h keeps it): the crash record's c2 */
static volatile uint8_t fm1_c1_runs;

/* A fault on core 2 (its exception runs fm1_fault_c on core 2): its record, kept in core 2's room (app.ld's
 * .c2_keep: never cleared, where neither a restart nor the update loader writes), the count telling a new one from
 * the last (fm1_cpu1.h); then core 2 waits in its park, in RAM, until core 1 holds it: the crash screen and a reset
 * from core 2 would hold the core running them (a frozen FM-1). */
#define FM1_C1_FAULT_MAGIC 0x43324658u       /* "C2FX" */
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, uptime_ms;
} fm1_c1_fault_t;
fm1_c1_fault_t fm1_c1_fault __attribute__((section(".c2_keep")));
void fm1_c1_park(void);
void __attribute__((section(".c2_top"), noreturn, used, noinline)) fm1_c1_park(void)
{
    for (;;)
        ;
}

extern const char fm1_fatal_stubs[];
static void fm1_fault(const fm1_crash_t *c);   /* application: report, then reset */

static void fm1_irq_init(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++)
        *(volatile uint32_t *)(0x1EEF100u + 4u * i) = 0;   /* every source off (CPU0 bank) */
    FM1_ILAT_CLR = 0xFFu;
    FM1_EMU_MSG = 0xFFFFFFFFu;
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;                              /* unlock */
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    for (i = 0; i < 128u; i++)
        FM1_VEC[i] = (uint32_t)(uintptr_t)(fm1_fatal_stubs + 6u * i);
    FM1_ICFG(1) = (FM1_ICFG(1) & ~0xF0u) | 0xF0u;           /* exception: enable, prio 7 */
    /* div0 trap off (cleared, in case a reset kept it from a build that armed it): this compiler treats a divide
     * as harmless and may run a loop-invariant one ahead of the test that guards it, and the build has a few hundred divides by a register. Off, such a divide gives a value
     * nobody uses; the cost: a real divide by 0 gives a wrong value instead of a crash report (the host tests run
     * with -fsanitize=integer-divide-by-zero to catch one: tests/run_tests.sh) */
    FM1_EMU_CON &= ~(1u << 2);
    FM1_ETM_CON |= 1u;                                      /* branch trace for the report */
}

static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)   /* IRQs off */
{
    uint32_t sh = (n & 7u) * 4u;
    FM1_VEC[n] = (uint32_t)(uintptr_t)h;
    FM1_ICFG(n) = (FM1_ICFG(n) & ~(0xFu << sh)) | ((((prio & 7u) << 1) | 1u) << sh);
}

static void fm1_irq_enable_all(void)
{
    fm1_icfg_set(fm1_icfg() | 0x100u);
    fm1_irq_on();
}

/* called from fm1_fatal_common (fm1_vec.S) with the saved frame:
 * f[0..15] r0..r15, f[16] reti, f[17] rete, f[18] retx, f[19] stub+6,
 * f[20] psr, f[21] icfg, f[22] usp, f[23] ssp, f[24] sp, f[25] interrupted rets */
void fm1_fault_c(uint32_t *f)
{
    static volatile uint32_t in_fault;
    uint32_t i, count = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    uint32_t early = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.early : 0;
    if (fm1_cnum()) {                         /* core 2's own: its record, then its park (before in_fault: core 1's
                                               * faults stay its own) */
        fm1_c1_fault.count = fm1_c1_fault.magic == FM1_C1_FAULT_MAGIC ? fm1_c1_fault.count + 1u : 1u;
        fm1_c1_fault.vec = (f[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u;
        fm1_c1_fault.pc = f[16];
        fm1_c1_fault.rets = f[25];
        fm1_c1_fault.emu = *(volatile uint32_t *)0x1EEF2D4u;   /* (its EMU_MSG: its bank is core 1's + 0x200) */
        fm1_c1_fault.dbg = FM1_DBG_MSG;
        fm1_c1_fault.sp = f[24];
        fm1_c1_fault.uptime_ms = *(volatile uint32_t *)0x10804u / 24000u;
        fm1_c1_fault.magic = FM1_C1_FAULT_MAGIC;
        __asm__ volatile("csync" ::: "memory");
        {
            void (*volatile park)(void) = fm1_c1_park;   /* (through a register: RAM is out of a call's reach) */
            park();
        }
        for (;;)
            ;
    }
    fm1_irq_off();
    if (in_fault++) {                         /* fault while reporting: reset at once, core 2 held first */
        fm1_cpu1_halt();
        *(volatile uint32_t *)0x10000u |= 1u << 4;
    }
    /* the stack check and the write limits off: on an overflowed stack, or in a guard band, what the report pushes
     * must not fault again (fm1_guard.h arms them) */
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;
    FM1_EMU_CON &= ~(1u << 3);
    *(volatile uint32_t *)0x1EEE348u = 0;     /* (C0_WR_LIMIT_EN, as fm1_enter_uboot drops them) */
    fm1_crash.magic = FM1_CRASH_MAGIC;
    fm1_crash.count = count + 1u;
    fm1_crash.vec = (f[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u;
    fm1_crash.pc = f[16];
    fm1_crash.rets = f[25];
    fm1_crash.emu = FM1_EMU_MSG;
    fm1_crash.dbg = FM1_DBG_MSG;
    fm1_crash.sp = f[24];
    fm1_crash.psr = f[20];
    fm1_crash.icfg = f[21];
    fm1_crash.uptime_ms = *(volatile uint32_t *)0x10804u / 24000u;   /* TIMER4 since boot */
    for (i = 0; i < 4u; i++)
        fm1_crash.etm[i] = FM1_ETM_PC(i);
    fm1_crash.early = fm1_crash.uptime_ms < 3000u ? early + 1u : 0u;
    fm1_crash.stack = fm1_stack_out(f[22], f[23]);
    fm1_crash.c2 = fm1_c1_runs;
    fm1_fault(&fm1_crash);
    for (;;)
        ;
}
