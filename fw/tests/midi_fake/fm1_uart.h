/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UART's registers on the PC: nothing (midi_uart_test.c feeds the parser's ring itself). */
#pragma once
#include <stdint.h>
static inline void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len) { (void)ring; (void)len; }
static inline uint32_t fm1_uart1_rx_take(void) { return 0; }
