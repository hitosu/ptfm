/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A fake FM-1 for the store's tests: the 1 MiB flash in memory (programming only clears bits, within a
 * page; erase 4 KB), a clock, SysEx out to a callback, and power cuts: after cut_at flash operations
 * the next one longjmps to cut_env, done in part when cut_torn says so. */
#ifndef STORE_PLAT_H
#define STORE_PLAT_H
#include <setjmp.h>
#include <stdint.h>

extern uint8_t fake_flash[1u << 20];
extern uint32_t fake_fw_end;            /* the firmware's end (FWS's free part after it, never read) */
extern uint32_t fake_ms;
extern long fake_ops;                   /* flash operations (erase, program) so far */
extern long fake_erases;
extern long fake_idles;                 /* plat_idle calls (the watchdog fed) */
extern long fake_locks;                 /* plat_lock calls, and how deep they are now (0 when balanced) */
extern int fake_lock_depth;
extern long fake_reads;                 /* direct reads (plat_flash_read) so far, and their bytes */
extern long fake_read_bytes;
extern long fake_low_reads;             /* reads of the flash below the user area (plat_flash_ptr, plat_flash_read) */
extern long cut_at;                     /* -1: no cut */
extern int cut_torn;                    /* the cut operation half done: a program's first half, an erase's most */
extern jmp_buf cut_env;
extern void (*fake_sysex)(const uint8_t *msg, uint32_t n);

void fake_reset(void);                  /* all 0xFF, no cut */

#endif
