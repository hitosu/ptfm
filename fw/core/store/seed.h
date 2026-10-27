/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Seeding, the device's side (specs/files/blob.md#seeding): the uploader (fw/web/seed.js) asks for the
 * layout, then writes whole blocks; the device checks each block's region and CRC, erases, programs,
 * reads back, and mounts at the end. The protocol's bytes are described at the top of fw/web/seed.js. */
#ifndef PTFM_SEED_H
#define PTFM_SEED_H
#include <stdint.h>

void seed_init(void);                   /* power-on state (all zero at boot anyway; the tests' power cuts) */
void seed_poll(void);                   /* the main loop: messages waiting to go out, the 10-s time-out */
int seed_active(void);                  /* blocks are being written: no audio, no store reading */
void seed_sysex(const uint8_t *msg, uint32_t n);    /* a PT/FM SysEx: F0 7D 50 54 cmd ... F7 */

/* Seeding refuses (SAVE OR REVERT FIRST) while the store's edit memory holds changes (store_edit_count),
 * or while this says edits wait unsaved elsewhere (the UI's own; none: only the store's count). */
void seed_set_unsaved(int (*edits_unsaved)(void));

/* For the UI's seeding screen. */
enum {
    SEED_OK = 0, SEED_BAD_MSG = 1, SEED_BAD_BLOCK = 2, SEED_UNSAVED = 3, SEED_BAD_CRC = 4, SEED_FLASH = 5,
    SEED_NOT_BEGUN = 6, SEED_VERSION = 7, SEED_BUSY = 8,
    SEED_CHANGED = 9                    /* BEGIN's save number is not the next: the layout changed (plan again) */
};
typedef struct {
    uint8_t active;                     /* writing */
    uint8_t refused;                    /* the last HELLO or BEGIN was refused: result says why */
    uint16_t done, total;               /* blocks written of those BEGIN announced */
    int8_t result;                      /* the last block's or the end's result (SEED_*) */
    uint8_t ended;                      /* a seeding has ended since power-on (END, ABORT or the time-out) */
    uint16_t records, bad;              /* after the end's mount: live records, records that failed */
} seed_info_t;
void seed_info(seed_info_t *s);

#endif
