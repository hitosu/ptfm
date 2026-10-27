/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The state a page asks for (specs/connections.md#the-state): 40 STATE? in, 41 STATE out, in every build, so
 * the debug page's autotests check where the FM-1 is between their steps. Also PT/FM's messages packed 7 bytes
 * in 8 (seeding's packing), for this answer and the debug build's. */
#ifndef PTFM_REMOTE_H
#define PTFM_REMOTE_H
#include <stdint.h>

#define REMOTE_STATE_VERSION 1
#define REMOTE_STATE_LEN 331
#define REMOTE_DJ_LEN (1 + 2 * 24 + 136)        /* DJ's bytes: as FIGURES' (specs/debug.md#djs-bytes) */

int remote_sysex(const uint8_t *msg, uint32_t n);   /* 40 STATE?: 1 taken (answered from remote_poll) */
void remote_poll(void);                             /* the main loop: an answer waiting is sent */
int remote_state(uint8_t *out);                     /* the state's bytes (REMOTE_STATE_LEN) */
void remote_dj_put(uint8_t *p);                     /* DJ's bytes (REMOTE_DJ_LEN) */
/* F0 7D 50 54 cmd, b packed 7 in 8, F7, handed to USB (it copies it at once): 0 not taken (dropped) */
int remote_send(uint8_t cmd, const uint8_t *b, uint32_t len);

#endif
