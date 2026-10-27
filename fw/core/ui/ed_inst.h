/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Editing instruments (specs/files/instruments.md; specs/ui/screens/instruments.md): a slot's fields where
 * its bits lie, the edit pages INSTRUMENTS shows (pages of groups of up to four parameters, K1-K4), and
 * the cartridges' records written into the edit memory. */
#ifndef ED_INST_H
#define ED_INST_H
#include <stdint.h>
#include "../play/rec.h"
#include "txt.h"

/* (the names and words are TX() texts: T() draws them) */
enum { PK_NUM, PK_SGN, PK_LIST, PK_LEVEL, PK_ALG, PK_RELEASE, PK_SAMPLE, PK_POS, PK_NOTE, PK_TUNE, PK_PATTERN,
       /* CHIP's: X1-X4 (stored 0-3); frames or cents stored halved; a table's number and name (`min` its kind:
        * VOL, ARP, PITCH, DUTY, NOISE); a wave's number and name; a PSG shape's number and picture; the PSG PERIOD's
        * first byte, shown whole (its four hex digits) */
       PK_XN, PK_HALF, PK_CHTAB, PK_WAVE, PK_PSGSHAPE, PK_PERIOD };
typedef struct {
    txt_t n;                            /* its name above its column (7 characters) */
    txt_t lng;                          /* a longer name for the knob's readout, or 0 */
    uint16_t bit;                       /* where it lies in the slot */
    uint8_t bits, kind;                 /* PK_* */
    int16_t min, max;                   /* as shown (PK_SGN: stored + min); a list: 0 .. its length - 1 */
    const txt_t *labels;                /* a list's words */
} ins_prm_t;
/* a group: its row of up to four parameters; op a DX7 operator's (0-5), -1 none, or below it a CHIP group's own
 * layout (OPL_*: ins_layout) */
typedef struct { txt_t name; int8_t op; uint8_t name_row, pattern_row, np; ins_prm_t p[4]; } ins_grp_t;
/* a page: its groups; g0, when set, stands in g[0]'s place (a CHIP TONE page: its oscillator's row, the rest shared) */
typedef struct { txt_t name; uint8_t ng; const ins_grp_t *g, *g0; } ins_page_t;
const ins_grp_t *ins_grp(const ins_page_t *p, int i);   /* a page's group i */
enum { OPL_OSC = -2, OPL_WIDE = -3, OPL_VIB = -4, OPL_LONG = -5, OPL_TABLE = -10 /* - the table's kind */ };
typedef struct { uint8_t cx[4], cw[4], row; } ins_lay_t;
/* a group's own columns (x, characters) and a CHIP TABLES row's drawing at its right (row: 1-5 the table kind + 1, 6
 * the vibrato): 1; 0: the usual, at x 60 + 45 k, 7 characters */
int ins_layout(const ins_grp_t *g, ins_lay_t *l);

int ins_pages(int kind, const ins_page_t **pages);     /* the edit pages of a cartridge kind (REC_*): how many */
/* the same for a slot: a CHIP instrument's TONE page has its oscillator's own knobs */
int ins_pages_of(int kind, const uint8_t *slot, const ins_page_t **pages);
extern const txt_t *const CHIP_TAB_NAMES[5];          /* chip_names.c (fw/tools/gen_chip.py): each table kind's names */
extern const uint8_t CHIP_TAB_N[5];                   /* and how many */
extern const txt_t CHIP_WAVE_NAMES[16];
int ins_get(const uint8_t *slot, const ins_prm_t *p);   /* its value as shown */
void ins_set(uint8_t *slot, const ins_prm_t *p, int v);
void ins_text(const uint8_t *slot, const ins_prm_t *p, uint8_t cart, char *out, int max);   /* its value in words */
int ins_name_pos(int kind);             /* where a slot's name and LEVEL lie (bits) */
int ins_level_pos(int kind);
void ins_name_set(uint8_t *p, int bit, int chars, const char *name);    /* a name into 6-bit codes */
void ins_init(int kind, uint8_t *slot); /* INIT: the engine's defaults, named INIT */
int ins_slot_bytes(uint8_t cart, int slot, uint8_t *out);       /* the slot's bytes: 1 used, 0 empty */
/* the cartridge's record as it stands (the flash's with its edits) into dst (UI_SCRATCH at most), c read: 0 done; a
 * slot then at rec_slot_at, a sampler's samples' table after the slots */
int ins_cart_copy(uint8_t cart, rec_cart_t *c, uint8_t *dst, uint32_t *len, uint8_t *ver);
int ins_put_slot(uint8_t cart, int slot, const uint8_t *bytes); /* bytes NULL: emptied. 0 done */
int ins_new_cart(uint8_t num, int kind, const char *name);
int ins_copy_cart(uint8_t from, uint8_t to, const char *name);
int ins_delete_cart(uint8_t num);
int ins_rename_cart(uint8_t num, const char *name);
int ins_free_cart(void);                /* after the last of the user's (00-EF), else the first free; -1 none */
int ins_builtin(uint8_t cart);          /* 1 built in (F0-FF) as the firmware has it; 0 the user area's or edited (all
                                         * editable) */
int ins_kind(uint8_t cart);             /* REC_DX7 .. REC_CART_LAST, 0 none */
extern const uint8_t DX7_ALG[32][6];    /* the DX7's algorithms as MSFA keeps them */
extern const uint32_t RELEASE_MS[100];  /* a sample instrument's R4 as a time (ms to -60 dB; 0 endless) */

#endif
