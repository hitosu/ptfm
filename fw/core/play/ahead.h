/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The patterns about to play, found ahead by the main loop (rec.h's rec_ahead). */
#ifndef AHEAD_H
#define AHEAD_H

void ahead_poll(void);                  /* main loop: each pass (does work only when what plays has moved on) */

#endif
