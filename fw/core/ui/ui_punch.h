/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The punch-ins played live (specs/ui/screens/fx.md#punch-in-effects): one at a time, while its key is
 * held (FX held + a white key on any screen, or a white key on FX LIVE); ARP pressed meanwhile holds it on
 * after the key is let go, until its key or another punch-in's, or all sound off. */
#ifndef UI_PUNCH_H
#define UI_PUNCH_H
#include <stdint.h>
#include "txt.h"

extern const txt_t UI_PUNCH_NAME[16];   /* "LOOP 1/4" ... "VINYL STOP" (commands.md#punch-ins; TX texts: T() them) */
extern const txt_t UI_PUNCH_CHIP[16];   /* in 9 characters: "ECHO THRW" */
void ui_punch_down(int k);              /* its key pressed */
void ui_punch_up(int k);                /* its key let go */
int ui_punch_arp(void);                 /* ARP pressed: a punch-in's key held -> held on (1: ARP taken) */
void ui_punch_off(void);                /* the one running stops */
int ui_punch_now(void);                 /* the one running, -1 none */
int ui_punch_kept(void);                /* held on by ARP */
void ui_punch_leds(uint8_t *keys);      /* the white keys: each group's first dim, the one running full */

#endif
