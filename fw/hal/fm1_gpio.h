/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (FM1_PR overridable: the scan's host test, fw/tests/scan_test.c) */
/* FM-1 GPIO port registers: one 0x40 block per
 * port at 0x50000 + p * 0x40. HAL-internal: src/ uses the per-driver
 * helpers (fm1_input.h, fm1_audio.h, fm1_adc.h, fm1_lcd_hw.h). */
#pragma once
#include <stdint.h>

#define FM1_PORT(p) (0x50000u + (p) * 0x40u)
#ifndef FM1_PR
#define FM1_PR(p, r) (*(volatile uint32_t *)(FM1_PORT(p) + (r)))
#endif
enum { FM1_PA = 0, FM1_PB = 1, FM1_PC = 2, FM1_PH = 7 };
enum { FM1_OUT = 0x00, FM1_IN = 0x04, FM1_DIR = 0x08, FM1_DIE = 0x0C, FM1_PU = 0x10,
       FM1_PD = 0x14, FM1_HD0 = 0x18, FM1_HD = 0x1C };
