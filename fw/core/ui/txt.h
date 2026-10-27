/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The firmware's fixed texts (specs/ui/texts.md). A text is written where it is drawn, as T("SAVED"),
 * T("LOOP # CLEARED") with `#` its value's place. fw/tools/gen_texts.py collects them, with the help's
 * pages, packs them by one byte-pair tokenizer into texts.c (build/fw/gen/) and can write copies of the
 * sources where each T("...") becomes T(n /- "..." -/): compiled so, the texts come from the packed table.
 * T() takes either and gives the text. */
#ifndef UI_TXT_H
#define UI_TXT_H
#include <stdint.h>

const char *ui_txt_s(const char *s);    /* a text written plain: itself */
const char *ui_txt_n(int id);           /* a packed text by its number: unpacked into a rotating buffer */
#define T(x) _Generic((x) + 0, char *: ui_txt_s, const char *: ui_txt_s, default: ui_txt_n)(x)
/* a fixed text kept in a table (a menu's items, the properties, a list's values): TX("REVERT") where it is written,
 * T(it->name) where it is drawn or copied. Compiled from the packed copies (UI_TXT_PACKED: the build's) it is the
 * text's number, else the text itself; 0 / NULL is none (no text is numbered 0) */
#ifdef UI_TXT_PACKED
typedef int txt_t;
#else
typedef const char *txt_t;
#endif
#define TX(x) (x)

/* a text with its value: the first `#` in t replaced by v (ui_txt_v("LOOP # CLEARED", "03")); two values
 * go in by turns: ui_txt_v(ui_txt_v("BLOCK # OF #", a), b) */
const char *ui_txt_v(const char *t, const char *v);

/* ---- the packed table (texts.c, generated) ---- */
extern const uint16_t TXT_COUNT;
int txt_unpack(int id, char *dst, int max);     /* the text's length; dst ends in 0 */

/* the help's pages (specs/ui/help.md): each page one packed text, its lines split by '\n', marked as the
 * specs write them ({KEY}, [SCREEN]) */
typedef struct {
    const char *screen;                 /* the screen's name */
    uint16_t first;                     /* its own pages: TXT_HELP_PAGES[first .. first + n - 1] */
    uint8_t n;
} txt_help_t;
extern const txt_help_t TXT_HELP[];
extern const uint8_t TXT_HELP_N;
extern const uint16_t TXT_HELP_PAGES[];         /* the text of each page */
extern const uint16_t TXT_HELP_SHARED;          /* the shared pages: TXT_HELP_PAGES[TXT_HELP_SHARED ..] */
extern const uint8_t TXT_HELP_SHARED_N;

#endif
