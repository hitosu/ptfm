/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The scan as it is without the keys' readings (hal/fm1_input.h, FM1_INPUT_ANALOG not defined): the one scan_test.c
 * compares its own with, KEYS VELOCITY OFF, access for access */
#include "scan_mock.h"
#include "../hal/fm1_input.h"

void off_init(void) { fm1_input_init(); }
void off_tick(void) { fm1_input_tick(); }
void off_leds(const uint8_t *lit, const uint8_t *dim)
{
    for (uint32_t i = 0; i < FM1_NCOL; i++) {
        fm1_led[i] = lit[i];
        fm1_led_dim[i] = dim[i];
    }
}
uint32_t off_notes(void) { return fm1_in.notes; }
