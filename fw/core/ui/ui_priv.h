/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Between the UI core's own files (ui.c, ui_dlg.c, ui_set.c, screens.c): not for the screens. */
#ifndef UI_PRIV_H
#define UI_PRIV_H
#include "ui_core.h"

/* the members of a held combination (what acted locks it) */
enum { CK_NONE, CK_SELECT, CK_BLACK, CK_WHITE, CK_SEQ, CK_PLAY, CK_REC, CK_LFO, CK_PUNCH, CK_PRESETS, CK_MARK, CK_BIG,
       CK_ARP, CK_BUTTON = 32 };

/* HOME's combination, held or tapped open */
typedef struct { int lock; uint8_t solo, closed; uint32_t at; uint8_t is_ov; } combo_t;
combo_t *ui_home_combo(void);
int ui_home_seq_switches(const combo_t *c);    /* SEQ switches the white keys mute / solo (while they are members) */
#define UI_LINE_RUNS 12                 /* runs a line of a combination's panel holds at most */
int ui_home_lines(const combo_t *c, g_run_t (*lines)[UI_LINE_RUNS], int *nruns, char (*buf)[48], int max);
void ui_open_home(void);

/* the held buttons' layers (dialogs.md#hold-layers) */
int ui_layer_open(int b);               /* 1 if b has a layer here (then it shows until let go) */
void ui_layer_close(int b);
void ui_layers_draw(void);

const char *ui_help_page_text(ui_overlay_t *help, char *buf);   /* the help's `1/7` */
/* a question's foot (dialogs.md#questions): the buttons lb (no) and rb (yes) over their answers from ay, the keys'
 * line at the bottom */
void ui_ask_foot(int ay, int bottom, const char *yes, const char *no, const g_run_t *keys, int nk, int lb, int rb);
void ui_seed_draw(void);                /* the body while seeding */
void ui_dlg_poll(void);                 /* the progress bar's time */
ui_overlay_t *ui_overlay_at(int i);
void ui_tap(int b);
void ui_render(void);
extern void (*ui_between_halves)(void); /* the tests': called between a frame's two halves */

int ui_save_all(void);                  /* SAVE: every change to the flash; 0 done */
void ui_save_refs(int uses);            /* YES to UPDATE REFERENCES?: playback paused, SAVE with its own texts */
void ui_ask_storage_full(void);         /* NOT ENOUGH STORAGE TO SAVE, CLEAN UP or BACK */
void ui_revert_all(void);               /* REVERT ALL: the changes dropped, all read again */
void ui_revert_all_ask(void);           /* REVERT ALL's question (SAVE + OCT-, SETTINGS' TOOLS), then ui_revert_all */
void ui_factory_reset_ask(void);        /* SETTINGS' TOOLS: FACTORY RESET's two questions, the erase, the restart */
void ui_save_answer(int a);             /* MEMORY FOR EDITS IS FULL's answer: SAVE NOW (1) */
void ui_save_again(void);               /* SAVE as its tap does (after a CLEAN UP) */
void ui_controller_ask(void);           /* SETTINGS' MIDI CONTROLLER: its questions, then the switch (ui.h) */
void ui_settings_reload(void);          /* the settings record read again (after REVERT) and applied */
void ui_unsaved_refresh(void);          /* ui_unsaved from the edit memory and the settings, DJ's stand moved */
int ui_dj_stand_moved(void);            /* DJ's decks standing otherwise than written, not an edit yet (scr_dj.c) */
void ui_dj_stand_put(void);             /* a save, the decks paused: where they stand put as the stand's edit */
void ui_dj_poll(void);                  /* the main loop, on every screen: DJ's waits while it is live elsewhere */
uint16_t ui_settings_inst(void);        /* the current instrument the settings record keeps (F0:00 with none) */
uint8_t ui_settings_pattern(void);      /* the EDITOR's pattern it keeps (UI_EDITOR_PATTERN: its own, also with none) */
void ui_settings_keep_state(void);      /* SAVE: the current instrument (and the screen) into the record, if changed */
void ui_edit_init(void);                /* the edit memory's hooks into the store */
int ui_edit_save(void);                 /* SAVE: starts writing every change (store_poll writes it); STORE_* */
void ui_progress_pct(const char *text, int (*pct)(void));  /* a progress bar its owner fills, 0-100; input waits */
void ui_progress_close(void);
void ui_edit_drop(void);                /* every change dropped */

/* the settings as SETTINGS lists them (specs/ui/screens/settings.md) */
typedef struct {
    txt_t name;                         /* TX("VOICES"), drawn through T() */
    const txt_t *list;                  /* a list's words (TX); NULL: a number from min to max */
    uint8_t n;                          /* the list's length */
    int16_t min, max, step, def;        /* a list's def: its index */
    uint8_t fmt;                        /* SF_* */
} ui_setting_t;
enum { SF_LIST, SF_DB, SF_HEX, SF_SIGNED, SF_MS, SF_HZ, SF_NUM };
extern const ui_setting_t UI_SETTINGS[S_COUNT];
extern const ui_setting_t UI_MIDI_SETTINGS[S_MIDI_COUNT];
void ui_setting_text(const ui_setting_t *s, int v, char *dst);
void ui_settings_changed(void);         /* a setting changed: unsaved until SAVE */

void ui_screens_register(void);         /* every screen built (screens.c) */
const ui_screen_t *ui_power_on_screen(void);    /* SETTINGS' POWER-ON SCREEN */

#endif
