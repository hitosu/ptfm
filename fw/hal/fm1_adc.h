/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (the conversions split for the keys' readings; the reading method after Charles
 * Vestal's measurements, fm1-velocity) */
/* FM-1 SARADC: PB6 = MASTER pot (ch4),
 * PB1 = battery divider (ch3); the key matrix's rows PA7 (ch0) and PA8 (ch1), the keys' carbon pills
 * (fm1_input.h's readings: specs/hardware.md#the-keys-velocity). 10 bit.
 *
 *   fm1_adc_init()        both pins analog inputs, no pulls
 *   fm1_adc_read(ch)      0..1023, -1 on timeout: polled, from the main loop (the SDK's settings)
 *   fm1_adc_start(ch, con) a conversion started: the scan's (con FM1_ADC_CON_SCAN), taken a tick later
 *   fm1_adc_done()        it has finished
 *   fm1_adc_take()        its result, the converter idle again; fm1_adc_abort() gives one up
 *
 * One converter: while the scan reads the keys (fm1_input.h's fm1_an.on, or a conversion of its pending) it is the
 * scan's, and the main loop reads MASTER from it; the battery in a pause of the readings (fm1_an.hold).
 *
 * CON: bits 0-2 the clock's divider, bit 3 set, bit 4 on, bit 6 start (and clear the done flag), bit 7 done, bits
 * 8-11 the channel, bits 12-15 the start's delay (8 converter clocks a unit). The SDK's: divider 6, delay 15. The
 * scan's: divider 7 (the only one reading true at the scan's pace), delay 2 (~60 us: inside a 100 us tick; 0 leaves
 * the sampling short, 4 runs past the tick). */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#ifndef FM1_ADC_CON                     /* (fw/tests/scan_test.c: the converter as a model) */
#define FM1_ADC_CON (*(volatile uint32_t *)0x13100u)
#define FM1_ADC_RES (*(volatile uint32_t *)0x13104u)
#define FM1_WLA_CON0 (*(volatile uint32_t *)0x11900u)
#endif
enum { FM1_ADC_ROW_PA7 = 0, FM1_ADC_ROW_PA8 = 1, FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define FM1_ADC_CON_SCAN (7u | 8u | 2u << 12)   /* the scan's: divider 7, delay 2 */

/* Both are only ever analog inputs: PB1 held low for 8 s would reset the
 * chip (isd_config). */
static void fm1_adc_init(void)
{
    uint32_t m = (1u << 6) | (1u << 1);
    FM1_PR(FM1_PB, FM1_DIE) &= ~m;
    FM1_PR(FM1_PB, FM1_PU) &= ~m;
    FM1_PR(FM1_PB, FM1_PD) &= ~m;
    FM1_PR(FM1_PB, FM1_DIR) |= m;
    if (FM1_WLA_CON0 & (1u << 14))       /* (the scan's starts leave it: nothing sets it after this, the radio never on) */
        FM1_WLA_CON0 &= ~(1u << 14);
}

static int32_t fm1_adc_read(uint32_t ch)
{
    uint32_t t, v;
    FM1_ADC_CON = 0;
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    FM1_ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    FM1_ADC_CON |= 0x10u;
    FM1_ADC_CON |= 0x40u;
    for (t = 0; t < 20000u; t++)
        if (FM1_ADC_CON & 0x80u)
            break;
    v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON = 0x40u;
    FM1_ADC_CON = 0;
    return t == 20000u ? -1 : (int32_t)v;
}

static void fm1_adc_start(uint32_t ch, uint32_t con)
{
    uint32_t w = (con & 0xF00Fu) | ((ch & 0xFu) << 8);
    FM1_ADC_CON = 0;
    FM1_ADC_CON = w | 0x40u;             /* (the setup, the done flag cleared) */
    FM1_ADC_CON |= 0x10u;                /* on */
    FM1_ADC_CON = w | 0x50u;             /* started (the SDK's order): the word a read-modify-write wrote here, without
                                          * its read (an access fewer a conversion) */
}
static inline int fm1_adc_done(void) { return (FM1_ADC_CON & 0x80u) != 0u; }
static inline uint32_t fm1_adc_take(void)
{
    uint32_t v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON = 0x40u;
    FM1_ADC_CON = 0;
    return v;
}
static inline void fm1_adc_abort(void)
{
    FM1_ADC_CON = 0x40u;
    FM1_ADC_CON = 0;
}
