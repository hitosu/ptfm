/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI's core, for the screens (specs/ui/): the controls' grammar (controls.md: taps on release,
 * holds as modifiers, layers, the combination grace period), the frame (frame.md), overlays (dialogs.md:
 * menus, questions, properties, text entry, progress; help.md), the screens and their order
 * (navigation.md), the LEDs (leds.md), the keys playing the current instrument. README.md here says how
 * a screen is added. */
#ifndef UI_CORE_H
#define UI_CORE_H
#include <stdint.h>
#include "../plat.h"
#include "gfx.h"
#include "txt.h"
#include "../play/player.h"

#define BODY_Y 17                       /* the body: y 17-226 */
#define BODY_H 210
#define LV_DIM 64                       /* an LED's "dim": a quarter */
#define LV_FULL 255
#define LV_NONE (-1)                    /* a screen's lit(): no opinion */
#define BIT(b) (1u << (b))

enum { G_MAKE, G_PLAY, G_HEAR, G_SYSTEM };                       /* the screens' groups */

/* the note keys: the white ones are 0, 2, 4, 5, ... (16), the black ones the screens' keys (11) */
extern const uint8_t UI_WHITE[16], UI_BLACK[11];
int ui_white_index(int n);              /* 0-15, or -1 */
int ui_black_index(int n);              /* 0-10, or -1 */

/* ---------------------------------------------------------------------- a screen --- */
typedef struct ui_screen ui_screen_t;
struct ui_screen {
    txt_t name;                         /* TX("SONGS"): T(s->name) reads it */
    uint8_t group;                      /* G_* */
    uint16_t does;                      /* BIT(B_*): buttons with a function here (lit dim) */
    uint16_t combines;                  /* BIT(B_*): buttons of its own that wait the grace period */
    uint8_t no_keyboard;                /* the keys play no instrument here, PRESETS does nothing (DJ) */
    void (*enter)(const ui_screen_t *from);
    void (*leave)(const ui_screen_t *to);
    void (*draw)(void);                 /* the body: clipped to it */
    const char *(*sub)(void);           /* the header's subpage, white; NULL none */
    const char *(*page)(void);          /* the header's page, light grey; NULL none */
    const char *(*seq)(void);           /* SEQ's place (`8 TRACKS`); NULL none */
    const char *(*footer)(uint8_t *col);    /* the footer's right side; NULL none */
    void (*knob)(int e, int steps, int coarse);     /* coarse: SEL held */
    void (*mark)(int e, int steps);     /* SEL held + SELECT / ALGORITHM */
    int (*tap)(int b);                  /* a button's tap: 1 handled */
    int (*key)(int n, int down);        /* a note key: 1 handled (else it plays the instrument) */
    int (*back)(void);                  /* HOME tap: 1 closed something of the screen's own */
    int (*lit)(int b);                  /* a button's LED here, 0-255, or LV_NONE */
    int (*key_leds)(uint8_t *keys);     /* the keys' LEDs (27 levels): 1 if it set them */
    int (*sel_lines)(const char **lines, int max);  /* SEL held's layer: the screen's own lines */
    int (*play_down)(void);             /* PLAY pressed here: 0 the transport's, 1 a hold of its own, 2 done */
    void (*play_up)(int used);
    void (*to_start)(void);             /* HOME + PLAY here (DJ: both decks back to their cues); NULL: the transport's */
    void (*rec)(void);                  /* REC pressed */
    int (*hold)(int b);                 /* a button held past the hold delay: 1 the screen took it */
    int (*combo)(int held, int b);      /* button + button (either order): 1 it acted */
    int (*can_play)(void);              /* PLAY has something to play here (1), else 0; NULL: the song */
    void (*paused)(void);               /* what played paused (SAVE waiting goes on) */
    void (*tick)(void);                 /* every pass of the main loop while it is the screen */
    int (*has_combo)(int held, int b);  /* whether combo() acts on held + b (asked before it is called) */
    int (*hold_action)(int b);          /* b held is an action of the screen's (LOOPS' LFO erase): other
                                         * controls do not close it */
    void (*layer_up)(int b);            /* b let go after its layer showed (DJ: LFO's, into the LFO pads) */
    int (*armed)(void);                 /* a take armed (COUNT-IN KEY): the count-in's box says TAKE ARMED */
    int (*recording)(void);             /* a take, an overdub, the loops recording: the header's red ● */
    /* HOME + a white key (controls.md#homes-combination): the screen's own mutes (LOOPS: loops; DJ: a deck's
     * tracks); mute_leds lights the white keys for them; home_area: where HOME's one-line panel goes once a
     * white key has acted (y, h), so what the keys mute stays in view */
    void (*mute_key)(int t);
    void (*solo_key)(int t);
    void (*mute_leds)(uint8_t *keys);
    int (*home_area)(int *y, int *h);
    /* HOME + OCT- / OCT+, with the white keys (DJ: the tracks they reach): 1 there is somewhere to go that way
     * (go: and it went); 0 nothing, no message */
    int (*home_oct)(int b, int go);
    int (*foot_hints)(g_run_t *runs, int max);  /* the footer's left as key hints instead of the instrument (DJ) */
    int (*presets)(int step);           /* PRESETS of its own (LOOPS: the loop's sound): 1 taken */
    int (*presets_cart)(int step);      /* SEL + PRESETS of its own: 1 taken */
    uint8_t own_arp;                    /* ARP is the screen's own here, UI_ARP_*: the arpeggiator is not; ARP's
                                         * tap goes to tap() */
    /* what it edits now, for the player (player_editing: PL_ED_*, its key): commands never change it
     * (specs/ui/screens/editor.md); NULL: nothing */
    void (*editing)(uint8_t *what, uint32_t *key);
    void (*cursor)(uint8_t *c);         /* where the screen is, 8 bytes, for the state a page asks for
                                         * (specs/connections.md#the-state); NULL: zeros */
    /* SYNC EXTERNAL: the master's START (0xFA), STOP (0xFC), CONTINUE (0xFB) and song position (0xF2, spp in
     * sixteenths) as the screen's own PLAY takes them (LOOPS: the loops; DJ: the main deck); NULL: the song's */
    void (*transport)(int st, int spp);
    /* the instrument the keys play here and the footer names, in place of the current one (LOOPS: a re-record's
     * loop's, the pattern recorder's); 0xFFFF the current one; NULL the same */
    uint16_t (*inst)(void);
};
enum { UI_ARP_PREVIEW = 1, UI_ARP_PATTERN, UI_ARP_NONE };  /* the EDITOR's row preview (ARP held + SELECT: its
                                         * rhythm), LOOPS' RECORD PATTERN, DJ's nothing */
int ui_arp_on(void);                    /* the arpeggiator is on (controls.md, ARP) */
void ui_arp_off(void);
void ui_arp_refresh(void);              /* its settings changed: a running arpeggio takes them */
void ui_register(const ui_screen_t *s);
void ui_go(const ui_screen_t *s);       /* to a screen (a jump from code) */
void ui_go_name(const char *name);
const ui_screen_t *ui_screen_now(void);
const ui_screen_t *ui_screen_named(const char *name);

/* ----------------------------------------------------------------- the frame --- */
void ui_toast(const char *text);                        /* 2 s in the header, white */
void ui_toast_runs(const g_run_t *r, int n);            /* in colours (a key yellow) */
const char *ui_toast_now(void);                         /* the toast showing now, or NULL */
void ui_toastf(const char *fmt, ...);                   /* with %s %d %x %02X ... (ui_fmt) */
void ui_show_knob(const char *name, const char *value); /* a knob turned: its name and value in the header's place
                                                        * (as a toast's, white), 1.2 s after the last click */
const char *ui_knob_now(void);                          /* the knob turned showing now, or NULL */
void ui_refuse(int b);                                 /* a refusal: the button blinks fast for 0.6 s */
void ui_panel(int x, int y, int w, int h, const char *title, int col);     /* a box over the body */
void ui_dirty(void);                                    /* redraw soon (the next pass) */

/* ------------------------------------------------------------------- overlays ---
 * An overlay is drawn over the body and takes the input first. Its hooks get the overlay itself. */
typedef struct ui_overlay ui_overlay_t;
struct ui_overlay {
    uint8_t kind;                       /* OV_* */
    int8_t opener;                      /* the button that opened it (lit), or -1 */
    uint8_t passes;                     /* a tap it does not take goes on to the screen */
    uint8_t keep;                       /* stays across a change of screen */
    void (*knob)(ui_overlay_t *o, int e, int steps, int coarse);
    void (*mark)(ui_overlay_t *o, int e, int steps);
    int (*tap)(ui_overlay_t *o, int b);                 /* 1 taken */
    int (*key)(ui_overlay_t *o, int n, int down);       /* 1 taken */
    void (*cancel)(ui_overlay_t *o);                    /* HOME; NULL: just closes */
    void (*draw)(ui_overlay_t *o);
    int (*key_leds)(ui_overlay_t *o, uint8_t *keys);
    void *data;                         /* its owner's */
    void (*play)(ui_overlay_t *o);      /* PLAY pressed while it is on top (a list's preview); NULL: the transport */
    void (*rec)(ui_overlay_t *o);       /* REC pressed while it is on top; NULL: the screen's */
    int (*lit)(ui_overlay_t *o, int b); /* a button's LED while it is on top (LV_NONE: as the screen has it); b
                                         * B_COUNT: PLAY's green */
};
enum { OV_MENU = 1, OV_QUESTION, OV_PROPS, OV_TEXT, OV_PROGRESS, OV_HELP, OV_HOME, OV_LIST, OV_OTHER };
ui_overlay_t *ui_open(const ui_overlay_t *o);           /* pushed (copied); returns the copy */
void ui_close(ui_overlay_t *o);                         /* NULL: the top one */
ui_overlay_t *ui_top(void);
int ui_overlay_count(void);
const char *ui_overlay_words(const ui_overlay_t *o);    /* a question's first line, a menu's highlighted item; else NULL */

/* a menu (dialogs.md#menus): items and groups (one level). pick(id) gets the item's id. */
typedef struct ui_mitem {
    txt_t name;                         /* TX("REVERT"); none (0): left out */
    int id;                             /* what pick() gets */
    const struct ui_mitem *items;       /* a group's items (NULL: an action) */
    uint8_t n;                          /* a group's count */
} ui_mitem_t;
void ui_menu(const char *key, const char *title, const ui_mitem_t *items, int n, void (*pick)(int id), int opener);

/* RECORD PATTERN (specs/ui/dialogs.md#record-pattern, ui_recpat.c): the dialog the EDITOR's SEL menu opens. What is
 * set (ui_rp) stays for the next time until power-off. */
typedef struct {
    uint16_t steps;                     /* the steps on: bit k step k */
    uint8_t size;                       /* 1-16 */
    uint8_t len;                        /* a step's length (ui_rp_step_rows) */
    uint8_t round;                      /* K4: round and round (1) or once */
    uint8_t step_long;                  /* SELECT: A STEP LONG (1) or RING TO THE NEXT */
    int16_t note;                       /* K1 (-1: the first time's, from the opener) */
} ui_rp_t;
extern ui_rp_t ui_rp;
typedef struct {
    int opener;                         /* B_SEL */
    const char *title;                  /* RECORD PATTERN INTO TRACK 03, ... */
    int rpb;                            /* rows a beat: the step lengths that are whole rows */
    int note;                           /* the note the first time */
    txt_t round;                        /* K4's word (TO THE END) */
    uint16_t (*sound)(void);            /* the instrument the steps play (cartridge << 8 | slot) */
    int (*song)(rec_song_t *s);         /* PLAY's preview: the screen's tempo, swing, effects, the track's mixer
                                         * as mix[0]; 0 read */
    void (*pause)(void);                /* PLAY's preview: what plays paused, keeping its place */
    int (*where)(g_run_t *r, char *buf, int bufn);   /* the line saying where it starts: its runs (texts in buf) */
    int (*write)(void);                 /* REC: 1 written (the dialog closes), 0 refused */
} ui_rp_open_t;
void ui_rp_open(const ui_rp_open_t *o);
int ui_rp_step_rows(int rpb);           /* a step's rows at rpb rows a beat */
/* the step lengths (1/32 ... 1/4, A ROW), by index (LOOPS' pattern recorder shares them): those that are whole rows at
 * rpb, in order, into l (7 at most; A ROW first where none is a row long): how many; one's name; its rows at rpb */
int ui_rp_lens(int rpb, uint8_t *l);
const char *ui_rp_len_name(int i);
int ui_rp_len_rows(int i, int rpb);
int ui_rp_step_at(int q, int sr, int limit, int once);   /* the step written q rows from the start, or -1 */
/* what REC writes into n rows from start (wrap: round the end to the start, as a loop does; once: the size's
 * steps once whatever K4 says): on[r] a note at row r; with A STEP LONG off[r] a note off a step after a note
 * where the next step is not on (the screen writes those on empty cells only); returns the notes */
int ui_rp_plan(int n, int start, int wrap, int sr, int once, uint8_t *on, uint8_t *off);

/* a question: every one is a yes or a no, in its own words. done(1) yes (OCT+), done(0) no (OCT-),
 * done(-1) cancelled (HOME) */
void ui_ask(const char *const *lines, int nlines, const char *yes, const char *no, void (*done)(int answer));

/* properties (GLO): SELECT picks, K1 changes at once, EDIT / LFO an item's own, the opener closes */
typedef struct ui_prop {
    txt_t name;                         /* TX("TEMPO") */
    const char *(*get)(void);
    void (*change)(int d, int coarse);  /* NULL: fixed */
    void (*edit)(void);                 /* EDIT on it */
    void (*reset)(void);                /* LFO on it */
    int (*col)(int on);                 /* its value's colour; NULL: white / light grey */
} ui_prop_t;
void ui_props(const char *title, const ui_prop_t *items, int n, int opener);
void ui_props_for(const char *name);    /* (right after ui_props) a second title line, white: whose they are */

/* text entry, the arcade way: done(name) or done(NULL) cancelled */
void ui_text_entry(const char *title, const char *name, int max, int opener, void (*done)(const char *name));

/* a progress bar for ms: input waits; then() after */
void ui_progress(const char *text, int ms, void (*then)(void));
int ui_blocking(void);

/* the song list (ENV on STATUS, PLAYBACK ...): the songs, their tempo and length; ENV picks (pick may be
 * NULL: ENV just closes it); PLAY plays the highlighted one (play may be NULL). Songs by their numbers. */
void ui_song_list(const char *title, int current, void (*pick)(int song), void (*play)(int song));
/* the pattern list (ENV): the user's patterns, their size and the songs using them; the opener picks
 * (a first, plain version: the EDITOR's notes map and its SEL menu are to come) */

/* a question with a checklist (dialogs.md): SELECT picks a line, K1 ticks it (clockwise) or clears it; a
 * line with nothing in it stays clear and dim; OCT+ yes with the ticked lines (refused with none), OCT- no,
 * HOME cancels (-1). title(rows, n) may follow the ticks. 4 lines at most. sub: a second title line, foot: a line
 * under the rows (or 0), kept as given (not copied); bare: yes stands with none ticked (DELETE's question, songs.md) */
typedef struct { const char *name, *note; int count; int on; } ui_check_t;
void ui_checklist(const char *(*title)(const ui_check_t *rows, int n), const ui_check_t *rows, int n, const char *yes,
                  const char *no, void (*done)(int answer, ui_check_t *rows), const char *sub, const char *foot, int bare);
/* a song's own patterns (DELETE's question): in its order, the user's (under F0), no other song's or FUSION slot's;
 * own a bit each; note `4  1.3K` or `NONE` (20 B); *shared those others use too. How many */
int ui_song_own(uint8_t song, uint8_t own[32], char *note, int *shared);
/* SETTINGS' tools, and what asks for them (storage full: CLEAN UP, then after()) */
void ui_check_references(void);
void ui_clean_up(void (*after)(void));
uint32_t ui_clean_up_frees(void);       /* the bytes CLEAN UP frees as it opens (unused patterns and samples) */
/* AUTO LEVEL (ui_level.c): one instrument, a cartridge's (cart >= 0) or the user area's (-1) every one, measured
 * and its LEVEL set under a progress bar, one a pass of the main loop; then a toast and after(LEVELs set) */
void ui_auto_level(uint8_t cart, int slot, void (*after)(int set));
void ui_auto_level_all(int cart, void (*after)(int set));
int ui_level_measure(uint8_t cart, int slot, int8_t *level);   /* its LEVEL measured (half dB): 1, else 0 silent */
void ui_level_poll(void);

/* the help (help.md): HOME + LFO */
void ui_open_help(void);
void ui_octave(int step);               /* the keys' octave a step up (1) or down (-1), with its toast */

/* ------------------------------------------------------------ the grammar's state --- */
int ui_held(int b);                     /* the button is down */
int ui_active(int b);                   /* held (its combination open) or in its grace period */
int ui_use_combo(int b, int kind);      /* may this member of b's combination act (and so lock it) */
void ui_consume(int b);                 /* b's release is no tap */
uint32_t ui_now(void);                  /* ms */
int ui_blink(int period_ms);            /* 1 in the first half of each period */
int ui_breath(uint32_t t0);             /* HOME's breath: dim to full and back in 2 s, full at t0 */

/* ------------------------------------------------------------- the current things --- */
typedef struct {
    uint8_t song;                       /* the current song */
    uint8_t pattern;                    /* the EDITOR's pattern */
    uint16_t inst;                      /* the current instrument: cartridge << 8 | slot (kept by SAVE) */
    int8_t octave;                      /* the keys' octave, -4..4 */
    uint32_t mutes;                     /* bit t: track t muted */
    int8_t solo;                        /* the track soloed, or -1 */
    uint8_t play_mode;                  /* SONGS' play mode: 0 ONE PLAY, 1 REPEAT SONG, 2 PLAY NEXT (not stored) */
} ui_cur_t;
extern ui_cur_t ui_cur;
#define UI_FIRST_INST 0xF000u           /* F0:00, the first built-in instrument: the current one at first */
int ui_muted(int t);
void ui_mutes_changed(void);            /* to the player */
int ui_midi(int n);                     /* key n's MIDI note at the octave */
void ui_set_inst(uint16_t ref);
extern uint16_t ui_last_inst;           /* the last instrument picked that sounds by itself (not a FUSION slot), 0xFFFF
                                         * none: a new FUSION pattern is first offered filled with it */
void ui_all_sound_off(void);

/* The player as the screens read it: while a frame is drawn (both halves of it), one snapshot taken at
 * the frame's start, so the halves agree though the audio moves on between them; elsewhere (a knob, a tap)
 * the player's own state now. Screens read these, never player_state() / player_voices_now(). */
void ui_player_state(player_state_t *s);
int ui_player_voices(pl_voice_info_t *v, int max);

/* the transport, as PLAY does it on a screen without a PLAY of its own (transport.md) */
void ui_play_press(void);               /* pause what plays, else go on with what played last, else the song */
void ui_play_song(uint8_t song, int from);      /* the song from a position; -1: from its own place, its start if
                                                 * another song played last (switching songs rewinds) */
int ui_song_held(void);                 /* a song held (played, stopped or not; the EDITOR's pattern): UNLOAD SONG offered */
void ui_unload_song(void);              /* UNLOAD SONG: the song played last let go at once, the keys' song held */

/* EDIT's jumps into the EDITOR go deep: HOME tap comes back (navigation.md) */
void ui_go_deep(const ui_screen_t *s, void (*back)(void), int locked);
int ui_is_deep(void);

int ui_keys_held(void);                 /* note keys down that play */
int ui_over9000(void);                  /* the header's OVER 9000 shows (either core over 90 % for a second) */
int ui_cpu_now(void);                   /* the sound's CPU share as the screens show it, %: its mean over ~0.1 s */
int ui_cpu2_now(void);                  /* the same of core 2 (its blocks' time over the half; 0 while it does not run) */
int ui_cpu_mean(uint32_t *sum0, uint32_t *halves0);   /* the mean since the sums were last read, %, -1 if no half has
                                                         * passed; they move on to now */
int ui_cpu2_mean(uint32_t *sum0, uint32_t *halves0);  /* the same of core 2 */
extern uint16_t ui_master;              /* MASTER now, 0-1023 */
uint32_t ui_fps(void);                  /* frames a second lately */
uint32_t ui_frame_ms(void);             /* the last frame's time */
extern const txt_t UI_BNAME[B_COUNT];   /* the buttons as printed (TX texts: T() them) */
extern const uint8_t UI_GROUP_COL[4];           /* MAKE light blue, PLAY green, HEAR brown, SYSTEM magenta */
/* a line `KEY: WHAT IT DOES` as runs: the key yellow, the rest light grey (runs[] at least 2) */
void ui_key_line(g_run_t *runs, int *n, const char *line);

/* --------------------------------------------------------------------- settings --- */
enum {
    S_OUTPUT, S_LINE_GAIN, S_USB_GAIN, S_VOICES, S_SECOND_CORE, S_THEME, S_KEYS_VOLUME, S_KEYS_GAIN, S_KEYS_FX, S_KEYS_OCTAVE,
    S_KNOB_SPEED,
    S_HOLD_DELAY, S_COMBO_GRACE, S_PLAYBACK_SCROLL, S_COUNT_IN, S_QUANTIZE, S_ARP_RHYTHM, S_ARP_PATTERN, S_ARP_LATCH,
    S_POWER_ON, S_TUNING, S_TOOLS, S_ABOUT, S_COUNT
};
enum { S_MIDI_IN_CH, S_MIDI_OUT, S_MIDI_OUT_CH, S_SYNC, S_CLOCK_OUT, S_MIDI_COUNT };
extern int16_t ui_set[S_COUNT];         /* each setting's value: a number, or an index in its list */
extern int16_t ui_midi_set[S_MIDI_COUNT];
void ui_settings_load(void);            /* from the settings record, else the defaults */
void ui_settings_apply(int k);          /* a setting changed: to where it acts (THEME, VOICES ...) */
void ui_settings_changed(void);         /* a setting changed: unsaved until SAVE (FX's GLO sets KEYS too) */
extern const txt_t UI_KEYS_FX[2];      /* KEYS' values: THROUGH FX, DRY (TX: drawn through T()) */
extern uint8_t ui_keys_mix[RM_N];       /* FX MIXER's KEYS column: the keys' PAN, DUCK and sends (RM_*), every setup's */
extern const uint8_t UI_KEYS_MIX_DEF[RM_N];
void ui_keys_mix_set(int key, int v);   /* one of them (RM_LEVEL: KEYS GAIN, dB), as a setting changed: kept, heard */
int ui_sync_pulse(void);                /* SYNC PULSE's side as it acts (OUTPUT's SYNC PULSE L / R): 1 left, 2 right, 0 none
                                         * (SYNC EXTERNAL: none) */
void ui_voices_poll(void);              /* the main loop: the voices notes take, SETTINGS' or DJ's (16 at most) */
void ui_play_mode_poll(void);           /* the main loop: SONGS' PLAY NEXT kept up on every screen (scr_songs.c) */
extern uint32_t ui_unsaved;             /* changes not saved (SAVE's LED) */
/* the user's GATE patterns (0-15 = `MY GATE 10` ...), kept in the settings record: 16 steps, 0 OFF, 1 ON,
 * 2-4 ratchets; set: into the record (an edit) and to the sound */
void ui_gate_get(int i, uint8_t *steps);
void ui_gate_set(int i, const uint8_t *steps);
void ui_gates_to_sound(void);

/* ------------------------------------------------------------------- RAM ---
 * The UI's RAM is specs/files/ram.md's "the screens' and the player's state": every screen together keeps
 * within it. A table that never changes is const (in flash). ui_scratch is one buffer every screen and
 * dialog borrows inside a handler (draw, knob, tap, an edit), never across passes of the main loop: a
 * record copied to change it, a list being built. Its halves (UI_SCRATCH_HALF) for two at once. */
#define UI_SCRATCH 4096
#define UI_SCRATCH_HALF (UI_SCRATCH / 2)
extern uint8_t ui_scratch[UI_SCRATCH];

/* STATUS' RAM MAP (specs/ui/screens/status.md): the RAM's parts in bytes. SYSTEM is the rest; ram_text (first)
 * and pool_system (the store's index and scratch, ui_scratch: among the big buffers) are its parts drawn where
 * they lie, the rest of it after the spare zone (the stacks, the chip's own). dj: the spare zone's part lent. */
typedef struct {
    uint32_t total, effects, edits, edits_used, screen, dx7, sound, loops, variables, spare, dj, system;
    uint32_t ram_text, pool_system;
} ui_ram_t;
void ui_ram(ui_ram_t *r);

/* ------------------------------------------------------------ the edit memory ---
 * Changes wait in RAM until SAVE (specs/files/ram.md#editing-and-saving), as pieces of what the flash holds:
 * an edit puts a whole record (a pattern's, a cartridge's, LOOPS' kept only as their pieces that changed) or one
 * piece (a pattern's track or head: ui_edit_piece); everyone reads it from then on, what plays from its next row
 * or note. REVERT takes one thing back to the flash's, REVERT ALL everything (specs/ui/saving.md#revert); there is
 * no UNDO. SAVE writes them all as one save. A full edit memory refuses (MEMORY FOR EDITS IS FULL). An edit mode
 * entered with EDIT keeps a copy of what it changes (ui_edit_session): HOME puts it back. */
#define UI_EDIT_MAX (63 * 1024)
int ui_edit_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len);   /* 0 done */
int ui_edit_piece(uint8_t kind, uint8_t num, uint16_t id, uint8_t ver, const void *p, uint32_t len);      /* 0 done */
int ui_edit_del(uint8_t kind, uint8_t num, uint8_t sub);                /* 0 done */
int ui_edit_loops(int r, uint32_t was); /* a change of LOOPS' made (play/loops.h: r its LP_* result; was ui_edit_used()
                                         * before it), told as an edit's (MEMORY FOR EDITS IS FULL ...): 0 done */
int ui_edit_reserve(uint32_t n);        /* room for an edit of several pieces first (store_edit_reserve): 0 there,
                                         * -1 the edit memory full (its question up) */
int ui_edit_revert(uint8_t kind, uint8_t num, uint8_t sub);    /* REVERT: the thing as the flash has it (a pattern, a
                                         * cartridge, LOOPS whole); 0 done */
int ui_edit_revert_slot(uint8_t cart, uint8_t slot);           /* REVERT of one slot */
int ui_edit_unsaved(uint8_t kind, uint8_t num, uint8_t sub);   /* the thing has a change not saved (REVERT listed) */
void ui_ask_revert(const char *l0, const char *l1, void (*then)(void));    /* REVERT's question (l1 may be NULL):
                                         * REVERT runs then, and says `REVERTED`; KEEP or HOME nothing */
void ui_edit_session(int on);           /* an edit mode: 1 at EDIT (its copy kept from now), 0 when EDIT leaves it */
void ui_edit_session_of(uint8_t kind, uint8_t num);    /* the same for one thing only (SONGS' order) */
int ui_edit_session_drop(void);         /* HOME: what the mode changed back as it was (`CHANGES DROPPED`); 1 done */
uint32_t ui_edit_epoch(void);           /* changes whenever an edited piece moves or goes: an address into one taken
                                         * before then is no longer good */
uint32_t ui_edit_used(void);            /* bytes the changes take */
int ui_edit_count(void);                /* things changed */

/* ------------------------------------------------------------------ formatting --- */
/* ui_fmt: a small printf: %d %u %s %c %x %X, widths and 0 padding (%02X, %3d, %-6s) */
int ui_fmt(char *dst, int max, const char *fmt, ...);
const char *ui_hex2(int v);             /* `0B` (a rotating buffer: a few at once) */
const char *ui_dec(int v);              /* `12` (a rotating buffer) */
void ui_ref_text(char *dst, uint16_t ref);              /* `0B:05` */
void ui_note_name(char *dst, int midi);                 /* `C-4`, `C#4`; `---` none (-1), `===` off (1) */
void ui_page_of(char *dst, int i, int n);               /* `3/18` */
void ui_of(char *dst, int i, int n);                    /* `00/11` */
void ui_fmt_time(char *dst, uint32_t secs);             /* `03:12` */
int ui_voices_col(int n, int limit);                    /* a count of voices against the limit in force (VOICES; 16
                                                         * is 12 without core 2): red at it, yellow from 4 under */
int ui_clamp(int v, int a, int b);
int ui_wrap(int v, int n);

#endif
