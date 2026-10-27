/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI (specs/ui/): the screens, the controls' grammar, the LEDs. app.c calls these. */
#ifndef UI_H
#define UI_H

void ui_init(void);                     /* power-on: the start page */
void ui_poll(void);                     /* a pass of the main loop: input, the screen, the LEDs */
int ui_controller_wanted(void);         /* SETTINGS' MIDI CONTROLLER asked and answered: 1, the tracker to end */

#endif
