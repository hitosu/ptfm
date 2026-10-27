/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The punch-ins played live (ui_punch.h). */
#include "ui_core.h"
#include "ui_punch.h"
#include "../play/player.h"

const txt_t UI_PUNCH_NAME[16] = {TX("LOOP 1/4"), TX("LOOP 1/8"), TX("LOOP 1/16"), TX("LOOP 1/32"), TX("STUTTER 1/16T"),
                                 TX("BIT CRUSH"), TX("GATE 1/16"), TX("GATE 1/32"), TX("LOW-PASS SWEEP"), TX("HIGH-PASS SWEEP"),
                                 TX("ECHO THROW"), TX("REVERB SWELL"), TX("REVERSE"), TX("HALF SPEED"), TX("TAPE STOP"),
                                 TX("VINYL STOP")};
const txt_t UI_PUNCH_CHIP[16] = {TX("LOOP 1/4"), TX("LOOP 1/8"), TX("LOOP 1/16"), TX("LOOP 1/32"), TX("STUTTER"), TX("BIT CRUSH"),
                                 TX("GATE 1/16"), TX("GATE 1/32"), TX("LP SWEEP"), TX("HP SWEEP"), TX("ECHO THRW"), TX("REV SWELL"),
                                 TX("REVERSE"), TX("HALF SPD"), TX("TAPE STOP"), TX("VINYL STP")};

static struct { int8_t k; uint8_t kept; } pin = {-1, 0};

void ui_punch_down(int k)
{
    if (k < 0 || k > 15) return;
    if (pin.k == k && pin.kept) { ui_punch_off(); return; }        /* its own key again: off */
    player_punch(k, 1);                                             /* (another running is replaced) */
    pin.k = (int8_t)k;
    pin.kept = 0;
    ui_dirty();
}
void ui_punch_up(int k)
{
    if (pin.k != k || pin.kept) return;
    player_punch(k, 0);
    pin.k = -1;
    ui_dirty();
}
void ui_punch_off(void)
{
    if (pin.k < 0) return;
    player_punch(pin.k, 0);
    pin.k = -1;
    pin.kept = 0;
    ui_dirty();
}
int ui_punch_arp(void)
{
    if (pin.k < 0 || pin.kept) return 0;
    pin.kept = 1;
    ui_toast(ui_txt_v(T("# HELD ON: ITS KEY STOPS IT"), T(UI_PUNCH_NAME[pin.k])));
    return 1;
}
int ui_punch_now(void) { return pin.k; }
int ui_punch_kept(void) { return pin.k >= 0 && pin.kept; }
void ui_punch_leds(uint8_t *keys)
{
    for (int t = 0; t < 16; t++) keys[UI_WHITE[t]] = (uint8_t)(pin.k == t ? LV_FULL : t % 4 == 0 ? LV_DIM : 0);
}
