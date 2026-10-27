/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI (specs/connections.md): notes in as the keys, program change and bank select, pitch bend, the
 * sustain pedal; MIDI learn onto the FM-1's own controls (the MIDI map record, specs/files/midi.md);
 * MIDI OUT (the keys, or everything that plays); the clock in (SYNC EXTERNAL) and out (CLOCK OUT).
 * USB and the MIDI IN jack come in through plat_midi_rx alike; out goes over USB (plat_midi_tx). */
#ifndef PTFM_MIDI_H
#define PTFM_MIDI_H
#include <stdint.h>
#include "../plat.h"

#define MIDI_MAP_MAX 64

/* the FM-1's controls, by their numbers in the record (specs/files/midi.md) */
enum { MC_KNOB = 0, MC_MASTER = 7, MC_BUTTON = 8, MC_KEY = 22, MC_COUNT = 49 };
/* a mapping's kind */
enum { MK_ABS, MK_REL_2C, MK_REL_64, MK_REL_SM, MK_MOMENTARY, MK_TOGGLE };
typedef struct {
    uint8_t ch;                         /* 0-15: channel 1-16 */
    uint8_t note;                       /* 1 a note, 0 a control change */
    uint8_t num;                        /* the CC's or the note's number */
    uint8_t ctl;                        /* MC_*: the FM-1 control */
    uint8_t kind;                       /* MK_* */
} midi_map_t;

void midi_init(void);                   /* after the settings are read (ui_init) */
void midi_poll(void);                   /* the main loop, before ui_poll: what came in; MIDI OUT's and the clock's state */
void midi_audio(int frames);            /* the audio context, after the player: CLOCK OUT, the tracks' notes ended */

/* The FM-1's input with MIDI's added (ui_poll, after plat_input): the learned controls, and MIDI notes that
 * fall on the keys at their octave, as presses of the FM-1's own; a press and its release that came between
 * two passes are seen as a press, then a release. */
void midi_merge(plat_input_t *in);
int midi_key_vel(int n);                /* key n held by a MIDI note: its velocity (1-127); 0: not by MIDI */

/* MIRROR (specs/connections.md#mirror-the-panel-from-a-page): the PT/FM controller's mirror messages from USB as
 * the FM-1's panel, through midi_merge as the learned controls. midi_sysex takes the controller's hello (43),
 * which switches it on or off: 1 taken. midi_mirror: on (1), its channel 0-15. */
int midi_sysex(const uint8_t *msg, uint32_t n);
int midi_mirror(int *ch);

/* MIDI OUT: an FM-1 key played (a key's note sounding the current instrument): sent as KEYS and ALL say */
void midi_out_key(uint8_t note, uint8_t vel, int on);

/* ---- MIDI learn (specs/ui/screens/settings.md#learn) ----
 * While learning, the FM-1's own input goes to midi_learn_take (ui_poll) and does nothing else: step 1
 * takes the control touched (HOME cancels); step 2 waits for the MIDI control, then learns it (or HOME
 * cancels). The step goes back to 0 by itself once learned (the toast says what) or cancelled. */
int midi_learning(void);                /* 0 off, 1 TOUCH AN FM-1 CONTROL, 2 MOVE THE MIDI CONTROL */
int midi_learn_start(void);             /* 0 begun; -1 64 mappings already */
int midi_learn_take(const plat_input_t *in, const plat_input_t *last);   /* 1: the input was learning's */
void midi_learn_cancel(void);
int midi_learn_ctl(void);               /* step 2: the FM-1 control picked (MC_*) */

/* ---- the mappings: the MIDI map record, read as it stands (the edit memory's copy first) ---- */
int midi_map_count(void);
int midi_map_at(int i, midi_map_t *m);  /* 0 there */
int midi_map_delete(int i);             /* an edit (UNDO brings it back): 0 done */
void midi_ctl_name(char *dst, int ctl); /* K1, MASTER, PLAY, KEY F-3 (16 B) */
void midi_src_name(char *dst, const midi_map_t *m);  /* CC 74 CH 1, NOTE 36 CH 10 (16 B) */
const char *midi_kind_name(int kind);   /* ABSOLUTE, RELATIVE, MOMENTARY, TOGGLE */

/* for STATUS and the tests: what came in and went out */
typedef struct {
    uint32_t in, out, dropped;          /* messages */
    uint16_t bpm10;                     /* the external clock's tempo (0: none heard) */
    uint8_t clock_on;                   /* clocks arriving */
} midi_info_t;
void midi_info(midi_info_t *i);

#endif
