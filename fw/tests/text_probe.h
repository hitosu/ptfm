/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI tests' text probe (ui3_test.c, midi_test.c; tests/playground/lists_s.js's textAs the same): a text as drawn,
 * the screen's rows y .. y+10 (FONT_T's) or y .. y+12 (FONT_S's) over the text's width from x, its ink against the text
 * drawn alone on black, pixel for pixel, some of it inked; the rows under them left out (a cursor's or a highlight's
 * foot); the next frame whole again. Included after the test's own `screen` (the fake panel, 240 x 240 RGB565). */
#ifndef PTFM_TEXT_PROBE_H
#define PTFM_TEXT_PROBE_H
#include <string.h>

static int text_as_f(const ui_font_t *f, int x, int y, const char *t, int col)
{
    static uint16_t line[16 * 240];
    int w = (int)strlen(t) * f->w, rows = f->h == 16 ? 13 : 11, same = 1, inked = 0;
    ui_render();
    memcpy(line, screen + y * 240, (size_t)(rows * 240) * sizeof line[0]);
    g_invalidate();
    for (int h = 0; h < 2; h++) { g_band(h); g_clear(C_BLACK); g_text(x, y, f, t, col); g_push(); }
    uint16_t ink = g_rgb565((uint8_t)col);
    for (int j = 0; j < rows; j++)
        for (int i = x; i < x + w && i < 240; i++) {
            inked += line[j * 240 + i] == ink;
            if ((line[j * 240 + i] == ink) != (screen[(y + j) * 240 + i] == ink)) same = 0;
        }
    g_invalidate();
    ui_render();
    return same && inked > 8;
}
__attribute__((unused)) static int text_as(int x, int y, const char *t, int col) { return text_as_f(&FONT_T, x, y, t, col); }
__attribute__((unused)) static int text_as_s(int x, int y, const char *t, int col) { return text_as_f(&FONT_S, x, y, t, col); }

#endif
