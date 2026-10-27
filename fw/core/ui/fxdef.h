/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The effects as the FX screen shows them (specs/ui/screens/fx.md, The effects) and the FX setup's bytes
 * (specs/files/songs.md#the-fx-setup): each knob's name, its values, its default, where the setup keeps
 * it, its FX command; the chain, the switches, the GATE patterns' names. */
#ifndef UI_FXDEF_H
#define UI_FXDEF_H
#include <stdint.h>
#include "txt.h"

/* the effects' numbers (specs/files/commands.md#the-numbers-are-fixed) */
enum { FE_GATE, FE_FILTER, FE_DRIVE, FE_LOFI, FE_CHORUS, FE_DELAY, FE_REVERB, FE_LIMITER, FE_DUCK, FE_METRONOME,
       FE_SENDS, FE_N = 10 };
#define FX_SETUP_LEN 64

typedef struct {
    txt_t name;                         /* "PATTERN" (the names and values are TX() texts: T() draws them) */
    const txt_t *list;                  /* a list's values, or NULL: a number */
    uint8_t n;                          /* the list's length */
    int16_t min, max, def, step;        /* a number's range and default (a list: def its index); step: a click */
    uint8_t fmt;                        /* KF_* */
} fx_knob_t;
enum { KF_NUM, KF_PCT, KF_HEX, KF_CUT, KF_UNIT, KF_TRACK, KF_TEMPO,
       KF_ESRC,                         /* FILTER's envelope's SOURCE: KEYS, TRACK 00-0F, 4 BEATS .. 1/4 BEAT */
       KF_ATK, KF_DREL };               /* its times by step: ATTACK's; DECAY's and RELEASE's (`2.0 MS`, `1.22 S`) */
typedef struct {
    txt_t name;                         /* "LO-FI" */
    uint8_t col;                        /* its colour (C_*) */
    uint8_t code;                       /* its first knob's FX command (0x90 + ...); 0: none (the METRONOME); the
                                         * next ones follow it, but FILTER's envelope's (fxd_code) */
    uint8_t nk;
    const fx_knob_t *k;
    const char *unit[8];                /* KF_UNIT's unit for each knob (" KHZ", " MS", " DB": a value's, not packed) */
    uint8_t cpu10;                      /* the CPU it takes, % x 10 (estimates) */
} fx_eff_t;
extern const fx_eff_t FX_EFF[FE_N];
extern const txt_t FX_SHORT_NAME[FE_N]; /* LIVE's words: METRONOME as `METRO` */

/* the setup's bytes */
int fxd_get(const uint8_t *s, int e, int k);            /* a knob's value (a list: its index) */
void fxd_set(uint8_t *s, int e, int k, int v);
int fxd_on(const uint8_t *s, int e);
void fxd_set_on(uint8_t *s, int e, int on);
int fxd_sends(const uint8_t *s);        /* ROUTING: 1 SENDS */
int fxd_chain(const uint8_t *s, uint8_t *items);        /* the chain's items in order (12 at most) */
void fxd_set_chain(uint8_t *s, const uint8_t *items, int n);
void fxd_set_routing(uint8_t *s, int sends);            /* GLOBAL: the sends where SENDS was; SENDS: SENDS before the first of them */
void fxd_reset(uint8_t *s, int e);      /* the effect's knobs to their defaults */
void fxd_default(uint8_t *s);           /* a new song's setup: only LIMITER on */
uint8_t fxd_code(int e, int k);         /* the knob's FX command (0x90-0xAC, 0xB0-0xB5), 0 none */
int fxd_byte(int e, int k);             /* where the setup keeps it */
void fxd_text(int e, int k, int v, char *out);          /* its value as the screens write it (`LP 3.8K`) */
const char *fxd_knob_short(int e, int k);               /* its name in 6 characters (`PATTRN`; a text: use it at once) */
int fxd_filter_hz(int cut);             /* CUT -64..63 -> Hz (0: OFF) */
void fxd_hz_short(char *dst, int hz_x10); /* `3.8K`, `347`, `20K` (Hz x 10) */
void fxd_time(char *dst, int t10);      /* `2.0 MS`, `296 MS`, `1.22 S` (0.1 ms) */

/* GATE's patterns: 16 built in, 16 the user's (`MY GATE 10` ...): a text (use it at once) or buf */
const char *fxd_gate_name(int i, char *buf);

#endif
