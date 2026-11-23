/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FM-1 supplies HAL: the chip's two core supplies raised, at every start, to the levels the FM-1's stock firmware
 * sets (../../specs/hardware.md). VDC14 (P3_ANA_CON6 [2:0]) is 1.25 V + 50 mV a level; SYSVDD (P3_ANA_CON9 [3:0],
 * the cores and the SRAM, regulated from VDC14) 0.93 V + 30 mV a level. The boot loader leaves 3 (1.40 V) and 11
 * (1.26 V); the stock firmware's are 7 (1.60 V) and 15 (1.38 V), each range's top. Up only, a level every 100 us,
 * VDC14 first. Interrupts off: nothing else touches P33.
 *
 *   fm1_vdc14(), fm1_sysvdd()   the levels now
 *   fm1_supplies_raise()        both up to the stock levels: 0 done, -1 a P33 transfer timed out (stopped there,
 *                               nothing written from a bad read)
 *   fm1_vdc14_mv(l), fm1_sysvdd_mv(l)   a level in mV
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"                            /* fm1_delay_us */
#include "fm1_sys.h"                             /* fm1_p33_read, fm1_p33_write, fm1_p33_timeouts */

#define FM1_P3_ANA_CON6 0x06u
#define FM1_P3_ANA_CON9 0x09u
#define FM1_VDC14_STOCK 7u
#define FM1_SYSVDD_STOCK 15u

static uint32_t fm1_vdc14(void) { return fm1_p33_read(FM1_P3_ANA_CON6) & 7u; }
static uint32_t fm1_sysvdd(void) { return fm1_p33_read(FM1_P3_ANA_CON9) & 15u; }
static inline uint32_t fm1_vdc14_mv(uint32_t level) { return 1250u + 50u * level; }
static inline uint32_t fm1_sysvdd_mv(uint32_t level) { return 930u + 30u * level; }

/* a level at a time up to level (bits mask of reg) */
static int fm1__supply_raise(uint32_t reg, uint32_t mask, uint32_t level)
{
    uint32_t to = fm1_p33_timeouts, v = fm1_p33_read(reg) & mask, r;
    while (v < level && v < mask) {
        r = fm1_p33_read(reg);
        if (fm1_p33_timeouts != to || (r & mask) != v)
            return -1;
        v++;
        fm1_p33_write(reg, (uint8_t)((r & ~mask) | v));
        fm1_delay_us(100);
        if (fm1_p33_timeouts != to)
            return -1;
    }
    return 0;
}

static int fm1_supplies_raise(void)
{
    if (fm1__supply_raise(FM1_P3_ANA_CON6, 7u, FM1_VDC14_STOCK))
        return -1;
    return fm1__supply_raise(FM1_P3_ANA_CON9, 15u, FM1_SYSVDD_STOCK);
}
