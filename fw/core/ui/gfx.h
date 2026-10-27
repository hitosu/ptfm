/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Drawing (specs/ui/frame.md, colours.md). The screen is drawn a half at a time into one strip of 240 x
 * 120 pixels (specs/files/ram.md: the screen's strip): the frame's drawing runs once a half, every
 * call taking screen coordinates, and only the strip's rows land; then the rows that changed since the
 * last frame go to the panel. Colours are the palette's roles (C_BLACK .. C_WHITE) and their deep
 * shades (C_DEEP + a role: a fill, never text); a theme is a palette. */
#ifndef UI_GFX_H
#define UI_GFX_H
#include <stdint.h>
#include "fonts.h"

#define G_W 240
#define G_H 240
#define G_STRIP 120                     /* the strip's rows: half the screen */

enum {
    C_BLACK, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_LGREY, C_DGREY, C_LBLUE, C_LGREEN,
    C_LCYAN, C_LRED, C_LMAGENTA, C_YELLOW, C_WHITE, C_DEEP = 16
};

/* the themes (SETTINGS' THEME): DEFAULT, EGA, PASTEL, NEON, SUNSET, ICE, CLEAR, GREEN, AMBER, WHITE; the light ones
 * DAYLIGHT, PAPER, MANUAL, SILVER, LCD, and COMMANDER (on blue) */
#define G_THEMES 16
void g_theme(int i);
int g_theme_now(void);
const char *g_theme_name(int i);       /* (a text: use it at once) */
uint16_t g_rgb565(int c);               /* a role's colour now */
uint32_t g_ram(void);                   /* the bytes of the drawing strip (STATUS' RAM MAP: SCREEN) */
int g_ink(int c);                       /* text on a fill of colour c: C_WHITE, or C_BLACK where the theme's c is
                                         * nearer white than black (specs/ui/colours.md) */

/* clipping, in screen coordinates (the strip's own rows are clipped besides) */
void g_clip(int x, int y, int w, int h);
void g_noclip(void);

void g_clear(int c);
void g_px(int x, int y, int c);
void g_rect(int x, int y, int w, int h, int c);
void g_hline(int x, int y, int w, int c);
void g_vline(int x, int y, int h, int c);
void g_box(int x, int y, int w, int h, int c);
void g_line(int x0, int y0, int x1, int y1, int c);
void g_circle(int x, int y, int r, int c);
void g_disc(int x, int y, int r, int c);

/* text: capitals only (lowercase is drawn as capitals), a missing character as `?`. g_text returns
 * the end x. g_text_crop draws only the glyph's rows r0 .. r1 - 1, from y (S in a 12-px row: 2, 14). */
int g_text(int x, int y, const ui_font_t *f, const char *s, int c);
int g_textn(int x, int y, const ui_font_t *f, const char *s, int n, int c);     /* at most n characters */
int g_text_crop(int x, int y, const ui_font_t *f, const char *s, int c, int r0, int r1);
int g_tw(const ui_font_t *f, const char *s);
int g_centre(const ui_font_t *f, const char *s, int x0, int w);

/* runs: texts in colours one after another */
typedef struct { const char *s; uint8_t c; } g_run_t;
int g_runs(int x, int y, const ui_font_t *f, const g_run_t *r, int n);
int g_runs_crop(int x, int y, const ui_font_t *f, const g_run_t *r, int n, int r0, int r1);
int g_runs_w(const ui_font_t *f, const g_run_t *r, int n);

/* the drawn symbols (frame.md: symbols are drawn, not glyphs) */
void g_play(int x, int y, int c);       /* 5 x 9 */
void g_pause(int x, int y, int c);      /* 6 x 9 */
void g_rec(int x, int y, int c);        /* 7 x 9 */
void g_updown(int x, int y, int c);     /* 5 x 9 */
void g_leftright(int x, int y, int c);  /* 9 x 5 */
void g_diamond(int x, int y, int c);

/* the frame's halves: g_band(0) and g_band(1) before drawing it, g_push() after each */
void g_band(int half);
void g_push(void);                      /* the strip's changed rows to the panel */
void g_invalidate(void);                /* the next frame goes to the panel whole */
uint32_t g_rows_sent(void);             /* rows sent so far (STATUS, the tests) */

#endif
