/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Every screen built, registered at power-on (a screen's file defines its ui_screen_t; add a line here). */
#include "ui_core.h"
#include "ui_priv.h"

extern const ui_screen_t SCR_STATUS, SCR_SONGS, SCR_PLAYBACK, SCR_SETTINGS;
extern const ui_screen_t SCR_FX, SCR_VISUAL, SCR_LOOPS, SCR_DJ;
extern const ui_screen_t SCR_EDITOR, SCR_INSTRUMENTS;

void ui_screens_register(void)
{
    ui_register(&SCR_STATUS);
    ui_register(&SCR_SONGS);
    ui_register(&SCR_PLAYBACK);
    ui_register(&SCR_SETTINGS);
    ui_register(&SCR_FX);
    ui_register(&SCR_VISUAL);
    ui_register(&SCR_LOOPS);
    ui_register(&SCR_DJ);
    ui_register(&SCR_EDITOR);
    ui_register(&SCR_INSTRUMENTS);
}
