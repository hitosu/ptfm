/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI CONTROLLER, the PT/FM controller (specs/controller.md): SETTINGS' MIDI page, MIDI CONTROLLER, ends the
 * tracker for good (until the FM-1 is switched off and on: app.c calls none of it any more) and starts this: the
 * controller's start screen (REC mirror mode, HOME general mode, SELECT the channel), then the controller: its
 * protocol (ctl.h) on PT/FM's input, LEDs, USB-MIDI and screen.
 *
 *   ctlmode_start()         once, from the main loop, the tracker ended: the start screen
 *   ctlmode_poll()          the main loop's pass: the start, then the panel to MIDI, MIDI to the LEDs, the screen
 *   ctlmode_sysex(msg, n)   a SysEx in (F0 .. F7): the page's question */
#ifndef PTFM_CTLMODE_H
#define PTFM_CTLMODE_H
#include <stdint.h>

void ctlmode_start(void);
void ctlmode_poll(void);
void ctlmode_sysex(const uint8_t *msg, uint32_t n);

#endif
