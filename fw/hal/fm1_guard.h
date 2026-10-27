/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (cut: what the controller does not call) */
/* FM-1 hardware guards. Every violation raises the CPU exception (vector 1) and
 * ends in the fault report of fm1_irq.h, so install that first.
 *
 *   EMU stack limit   a stack pointer outside its window -> EMU_MSG bit 3.
 *                     The main loop runs on the user stack (usp), interrupts
 *                     on the system stack (ssp): EMU_USP's window is the main
 *                     loop's stack [_ustack_lo, _ustack_top], EMU_SSP's the
 *                     interrupts' [_sstack_lo, _sstack_top] (app.ld names
 *                     both), so either stack running past its bottom is
 *                     caught whatever its frame's size. Should the two
 *                     pointers not stand where crt0 put them when the guards
 *                     are armed, one window over both stacks is armed instead
 *                     (fm1_guard_windows says which).
 *   write limits      CPU0 writes into the two 256-byte stack guard bands,
 *                     into core 2's code once copied [_c2_start, _c2_end)
 *                     (its fourth window) and into _records_top..0x01C7FFFF
 *                     (the unused 76 B, boot info, mailbox, vectors) ->
 *                     DBG_MSG bit 13. The top window is armed separately
 *                     with fm1_guard_lock_top() once every vector is
 *                     attached.
 *   bus-invalid       fetch/read/write of unmapped space (NULL page, XIP
 *                     writes) -> DBG_MSG bits 4, 5, 16..21.
 *   PC limit          instruction fetch outside [0x02000120, _etext] and the
 *                     RAM window [_rt_start, _c2_end) (the flash driver's
 *                     code up to the end of core 2's, its loop and park
 *                     included, the data region between inside it: the two
 *                     cores share the windows, and core 2's code must run
 *                     inside one) -> DBG_MSG bit 12. Core 2 starts through
 *                     the chip's ROM, outside both: fm1_guard_pc_open for
 *                     its start (fm1_cpu1.h).
 */
#pragma once
#include <stdint.h>
#include "fm1_irq.h"

#define FM1_X2(off) (*(volatile uint32_t *)(0x1EEE000u + (off)))
#define FM1_WR_LIMIT_H(n) FM1_X2(0x280u + 4u * (n))
#define FM1_WR_LIMIT_L(n) FM1_X2(0x2C0u + 4u * (n))
#define FM1_C0_WR_LIMIT_EN FM1_X2(0x348u)
#define FM1_PC_LIMIT0_H FM1_X2(0x380u)
#define FM1_PC_LIMIT0_L FM1_X2(0x384u)
#define FM1_PC_LIMIT1_H FM1_X2(0x388u)
#define FM1_PC_LIMIT1_L FM1_X2(0x38Cu)
#define FM1_EMU_SSP_H (*(volatile uint32_t *)0x1EEF0D8u)
#define FM1_EMU_SSP_L (*(volatile uint32_t *)0x1EEF0DCu)
#define FM1_EMU_USP_H (*(volatile uint32_t *)0x1EEF0E0u)
#define FM1_EMU_USP_L (*(volatile uint32_t *)0x1EEF0E4u)

extern char _guard0[], _guard1[], _etext[], _records_top[];   /* (the stacks' ends: fm1_irq.h) */
extern uint32_t _rt_start[], _rt_end[], _c2_start[], _c2_end[];

enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };

static inline uint32_t fm1_usp(void) { uint32_t v; __asm__ volatile("%0 = usp" : "=r"(v)); return v; }
static inline uint32_t fm1_ssp(void) { uint32_t v; __asm__ volatile("%0 = ssp" : "=r"(v)); return v; }

/* the stack-pointer windows armed: 2 one for each stack, 1 one over both, 0 none yet */
static uint8_t fm1_guard_windows;
/* the pointers as fm1_guard_enable read them, which chose the windows: usp, ssp, and sp (the one in use then) */
static uint32_t fm1_guard_sp[3];
static inline uint32_t fm1_sp(void) { uint32_t v; __asm__ volatile("%0 = sp" : "=r"(v)); return v; }

static void fm1__dbg_unlock(void)
{
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;
}

static void fm1__dbg_lock(void)
{
    if (FM1_DBG_WR_EN & 1u)
        FM1_DBG_WR_EN = 0xE7u;
}

static void fm1_guard_enable(uint32_t which)
{
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    if (which & FM1_GUARD_STACK) {
        uint32_t ulo = (uint32_t)(uintptr_t)_ustack_lo, utop = (uint32_t)(uintptr_t)_ustack_top;
        uint32_t slo = (uint32_t)(uintptr_t)_sstack_lo, stop = (uint32_t)(uintptr_t)_sstack_top;
        uint32_t usp = fm1_usp(), ssp = fm1_ssp();
        fm1_guard_sp[0] = usp;
        fm1_guard_sp[1] = ssp;
        fm1_guard_sp[2] = fm1_sp();
        FM1_EMU_CON &= ~(1u << 3);
        if (usp > ulo && usp <= utop && ssp == stop) {   /* (as crt0 left them: the main loop on its own, no
                                                          * interrupt yet on the other) */
            FM1_EMU_USP_L = ulo;
            FM1_EMU_USP_H = utop;
            FM1_EMU_SSP_L = slo;
            FM1_EMU_SSP_H = stop;
            fm1_guard_windows = 2;
        } else {                                         /* one window over both, either order */
            uint32_t lo = ulo < slo ? ulo : slo, hi = utop > stop ? utop : stop;
            FM1_EMU_USP_L = lo;
            FM1_EMU_USP_H = hi;
            FM1_EMU_SSP_L = lo;
            FM1_EMU_SSP_H = hi;
            fm1_guard_windows = 1;
        }
        FM1_EMU_CON |= 1u << 3;
    }
    if (which & FM1_GUARD_WRITE) {
        FM1_WR_LIMIT_L(0) = (uint32_t)(uintptr_t)_guard0;
        FM1_WR_LIMIT_H(0) = (uint32_t)(uintptr_t)_ustack_lo - 1u;
        FM1_WR_LIMIT_L(1) = (uint32_t)(uintptr_t)_guard1;
        FM1_WR_LIMIT_H(1) = (uint32_t)(uintptr_t)_sstack_lo - 1u;
        FM1_C0_WR_LIMIT_EN |= 3u;
        if ((uint32_t)(uintptr_t)_c2_end > (uint32_t)(uintptr_t)_c2_start) {   /* core 2's code and tables, copied
                                                                                 * by now: core 1 never writes them */
            FM1_WR_LIMIT_L(3) = (uint32_t)(uintptr_t)_c2_start;
            FM1_WR_LIMIT_H(3) = (uint32_t)(uintptr_t)_c2_end - 1u;
            FM1_C0_WR_LIMIT_EN |= 8u;
        }
    }
    if (which & FM1_GUARD_BUS)
        FM1_DBG_EN |= (0x3Fu << 16) | (0x3u << 4);
    if (which & FM1_GUARD_PC) {
        FM1_PC_LIMIT0_L = 0x02000120u;
        FM1_PC_LIMIT0_H = (uint32_t)(uintptr_t)_etext;
        {   /* RAM code (.ram_text up to core 2's code's end, its loop and park included, the RAM between inside);
             * with none, repeat the XIP window */
            volatile uint32_t rs = (uint32_t)(uintptr_t)_rt_start, re = (uint32_t)(uintptr_t)_rt_end;
            if ((uint32_t)(uintptr_t)_c2_end > (uint32_t)(uintptr_t)_c2_start)
                re = (uint32_t)(uintptr_t)_c2_end;
            FM1_PC_LIMIT1_L = re > rs ? rs : 0x02000120u;
            FM1_PC_LIMIT1_H = re > rs ? re - 1u : (uint32_t)(uintptr_t)_etext;
        }
    }
    fm1__dbg_lock();
}

/* write-protect _records_top..0x01C7FFFF (the 76 B unused, then the chip's own from 0x01C7FD4C: boot info,
 * mailbox, vectors): call after the last fm1_irq_attach */
static void fm1_guard_lock_top(void)
{
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    FM1_WR_LIMIT_L(2) = (uint32_t)(uintptr_t)_records_top;
    FM1_WR_LIMIT_H(2) = 0x01C7FFFFu;
    FM1_C0_WR_LIMIT_EN |= 4u;
    fm1__dbg_lock();
}

/* ---- core 2's start (fm1_cpu1.h, interrupts off): it starts through the chip's ROM, which the PC limits refuse,
 * so both windows open until it has answered from its loop in RAM, then armed again (fm1_guard_enable with
 * FM1_GUARD_PC); its entry's word lies in the write-protected top (0x01C7FFF8), whose window is let go around that
 * write: fm1_guard_top_open returns what it found, for fm1_guard_top_close. ---- */
static void fm1_guard_pc_open(void)
{
    fm1__dbg_unlock();
    FM1_PC_LIMIT0_L = 0;
    FM1_PC_LIMIT0_H = 0xFFFFFFFFu;
    FM1_PC_LIMIT1_L = 0;
    FM1_PC_LIMIT1_H = 0xFFFFFFFFu;
    fm1__dbg_lock();
}
static uint32_t fm1_guard_top_open(void)
{
    uint32_t was;
    fm1__dbg_unlock();
    was = FM1_C0_WR_LIMIT_EN & 4u;
    FM1_C0_WR_LIMIT_EN &= ~4u;
    fm1__dbg_lock();
    return was;
}
static void fm1_guard_top_close(uint32_t was)
{
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    FM1_C0_WR_LIMIT_EN |= was & 4u;
    fm1__dbg_lock();
}
