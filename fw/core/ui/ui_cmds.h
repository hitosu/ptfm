/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The commands a cell holds (specs/files/commands.md), as the screens show them (specs/ui/screens/editor.md):
 * in words, in a 9-character short name (the 1-track view), and as the list K2 turns through in the
 * EDITOR. The list has an entry for each tracker effect, each E and X sub-command, each XM volume-column
 * effect and PT/FM's own; the instrument's parameters (`*`), the mixer (`=`) and the FX (`#`) are one
 * entry each, their code picked with K3. The FX's knobs are here too (specs/ui/screens/fx.md). */
#ifndef UI_CMDS_H
#define UI_CMDS_H
#include <stdint.h>
#include "txt.h"

enum { CG_NONE, CG_PARAM, CG_MIXER, CG_FX };            /* the three groups (ui_cmd_group's 1-3) */

typedef struct {
    txt_t name;                         /* `VIBRATO`, `PARAMETER` */
    int16_t code;                       /* its code; -1 NO COMMAND; for a group its CG_* as 0x100 + CG_* */
    int8_t sub;                         /* an E / X sub-command (the value's high digit), else -1 */
    int8_t digits;                      /* 0 no value, 1 one digit (K4), 2 two digits, -1 a group's (K3 which, K4 its value) */
    uint8_t min;                        /* the least value (a punch-in's 01) */
} ui_entry_t;
#define CE_GROUP 0x100
extern const ui_entry_t UI_ENTRIES[];
extern const int UI_NENTRIES;

int ui_cmd_entry(uint8_t code, uint8_t value);          /* the entry a command is (0: none) */
int ui_cmd_is_global(uint8_t code, uint8_t value);      /* acts from any track; ignored in FUSION rows */
int ui_cmd_punch(uint8_t code);                         /* the punch-in it runs (0-15), or -1 */

/* in words (`SLIDE UP 04`, `CUTOFF 40`), for the engine of the track's instrument (ENG_*) */
void ui_cmd_words(uint8_t code, uint8_t value, int engine, char *out, int max);
/* the parameter / mixer value / FX knob a group code names (`OP1 LEVEL`, `REVERB SEND`, `DELAY FEEDBACK`) */
void ui_cmd_code_name(uint8_t code, int engine, char *out, int max);
/* the codes K3 turns through in a group, for the engine; returns how many */
int ui_cmd_codes(int group, int engine, uint8_t *codes, int max);
int ui_cmd_max(uint8_t code, int engine);               /* the most a group command's value goes to */
const char *ui_cmd_param_name(int engine, uint8_t code);/* a parameter's name (a text: use it at once), NULL if none */
int ui_cmd_param_max(int engine, uint8_t code);

/* the FX command's effect and knob (fxdef.h's FE_* and knob), 1 when it is one */
int ui_cmd_fx(uint8_t code, int *effect, int *knob);

#endif
