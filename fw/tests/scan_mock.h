/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The FM-1's registers as a model, for the scan's host test (scan_test.c, scan_off.c): every register access of the
 * HAL's goes through mock_reg / mock_adc / mock_t4 (fm1_gpio.h, fm1_adc.h, fm1_time.h take these macros first) */
#include <stdint.h>
volatile uint32_t *mock_reg(uint32_t port, uint32_t reg);
volatile uint32_t *mock_adc(uint32_t which);
volatile uint32_t *mock_t4(void);
#define FM1_PR(p, r) (*mock_reg((uint32_t)(p), (uint32_t)(r)))
#define FM1_ADC_CON (*mock_adc(0))
#define FM1_ADC_RES (*mock_adc(1))
#define FM1_WLA_CON0 (*mock_adc(2))
#define FM1_T4_CNT (*mock_t4())
