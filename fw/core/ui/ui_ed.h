/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the EDITOR and INSTRUMENTS share: the pattern list with its menu and preview (ENV;
 * specs/ui/screens/editor.md#env-the-pattern-list), ROM's refusals and the questions about references
 * (specs/ui/saving.md). */
#ifndef UI_ED_H
#define UI_ED_H
#include <stdint.h>
#include "txt.h"

/* the pattern list: every pattern of the user's, its size, a map of its notes, the songs that use it
 * (`FUSION` when a FUSION slot does); the opener picks (pick(pat)); SEL its menu; PLAY its preview.
 * fusion_inst >= 0: the keys play the highlighted pattern as that FUSION instrument would. */
void ed_pattern_list(const char *title, int current, void (*pick)(int pat), int opener, int fusion_inst);
/* ... with `+ NEW PATTERN` first, picked as pick(ED_PICK_NEW) (SONGS' NEW: the new song's first pattern) */
#define ED_PICK_NEW 0x100
void ed_pattern_list_new(const char *title, int current, void (*pick)(int pat), int opener);
/* ... and one pattern left out, + NEW PATTERN highlighted (STORE IN A PATTERN: the EDITOR's own) */
void ed_pattern_list_new_but(const char *title, int except, void (*pick)(int pat), int opener);
/* ... its first line `plus` instead, highlighted, picked as ED_PICK_NEW (LOOPS' SAVE TO PATTERN: `NEW`) */
void ed_pattern_list_plus(const char *title, txt_t plus, void (*pick)(int pat), int opener);
/* ... PLAY's preview at the timing timing(pat, ...) gives (1 given; 0: the current song's): LOOPS' LOAD, as LOAD
 * would bring the pattern */
typedef int (*ed_timing_fn)(int pat, uint16_t *tempo, uint8_t *ticks, uint8_t *rpb, uint8_t *swing);
void ed_pattern_list_timed(const char *title, int current, void (*pick)(int pat), int opener, ed_timing_fn timing);

int ed_pattern_uses(uint8_t pat);       /* positions of songs that play it, and FUSION slots that name it */
int ed_pattern_fusion(uint8_t pat);     /* 1 a FUSION slot names it */
int ed_inst_uses(uint16_t ref);         /* cells that name the instrument */
/* references updated: every cell of the user's patterns naming `from` made to name `to` (-1: no
 * instrument), the two swapped when swap; whole_cart: from and to are cartridges, the slots kept */
void ed_remap(int from, int to, int swap, int whole_cart);

/* the EDITOR's own pattern (UI_EDITOR_PATTERN; specs/ui/screens/editor.md#the-editors-own-pattern) */
int ed_pattern_holds(uint8_t pat);      /* 1 a cell holds anything (note, instrument, volume or command) */
int ed_own_holds(void);                 /* ... in the EDITOR's own pattern, its unsaved changes included */
/* before another pattern replaces the EDITOR's own in the EDITOR: with anything in it, EDITOR CHANGES WILL BE LOST /
 * DO YOU WANT TO CONTINUE? first (YES empties it, an edit, then go(); NO nothing); empty, go() at once */
void ed_own_ask(void (*go)(void));
void ed_own_empty(void);                /* emptied (its change deleted, an edit): 8 x 64 again */

/* the EDITOR opened on a pattern from elsewhere (a FUSION slot's EDIT PATTERN): deep and locked, HOME
 * comes back; fusion_inst >= 0: its keys play that FUSION while REC is off */
void ed_editor_open(uint8_t pat, int row, int track, int fusion_inst);
/* the EDITOR's cursor on a cell (applied when the EDITOR is next shown, or at once while it is) */
void ed_editor_goto(uint8_t pat, int row, int track);

#endif
