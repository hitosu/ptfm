/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The platform calls (plat.h) a platform may leave out, as no-ops: the store's long-work and lock calls, the
 * restart, MIDI's and the second core's (the host tests' fakes link these; the FM-1's and the emulator's own take over: weak).
 *
 * They live alone in this unit on purpose, and no other unit may define a weak function it calls itself: the
 * FM-1's compiler, seeing a weak body in the calling unit, keeps values across the call in the registers that
 * body leaves alone, and the platform's own, linked in its place, overwrites them. Measured on the FM-1 with
 * these in store.c: every change was filed under a wrong record (its kind and number taken from plat_lock's
 * registers), so none was ever found again (fw/tools/build.py refuses a weak function anywhere else). */
#include <stdint.h>
#include "plat.h"

__attribute__((weak)) void plat_idle(void) {}
__attribute__((weak)) uint32_t plat_spare_room(void) { return 0; } /* (the spare zone's array alone) */
/* (the FM-1's RAM and its code in RAM; the variables and screen buffers unknown here: SYSTEM takes them) */
__attribute__((weak)) void plat_ram(plat_ram_t *r) { r->total = 512u * 1024u; r->ram_text = 4096u; r->variables = 0; r->screen = 0; }
__attribute__((weak)) uint32_t plat_lock(void) { return 0; }
__attribute__((weak)) void plat_unlock(uint32_t was) { (void)was; }
__attribute__((weak)) void plat_restart(void) {}  /* (returns: the caller waits, silent, as for the emulator's page) */

__attribute__((weak)) int plat_midi_rx(plat_midi_t *e) { (void)e; return 0; }
__attribute__((weak)) int plat_midi_tx(const uint8_t m[3]) { (void)m; return -1; }
/* (at once: a platform that times nothing; the FM-1 defines its own, so this one's call of a weak function of this
 * unit never runs there) */
__attribute__((weak)) int plat_midi_tx_at(const uint8_t m[3], int frame) { (void)frame; return plat_midi_tx(m); }
__attribute__((weak)) void plat_master_set(uint16_t v) { (void)v; }
__attribute__((weak)) uint32_t plat_usb_resets(void) { return 0; }
__attribute__((weak)) int plat_usb_up(void) { return 1; }
__attribute__((weak)) int plat_usb_power(void) { return 1; }
__attribute__((weak)) int plat_fast(void (*fn)(const plat_input_t *in, uint32_t ms)) { (void)fn; return 0; }

/* (one core: no half goes to core 2; a job handed over all the same runs at once, nothing ever waits) */
__attribute__((weak)) int plat_c2_half(void) { return 0; }
__attribute__((weak)) void plat_c2_post(void (*job)(uint32_t), uint32_t j) { job(j); }
__attribute__((weak)) void plat_c2_enable(int on) { (void)on; }
__attribute__((weak)) void plat_c2_force_off(int off) { (void)off; }
__attribute__((weak)) void plat_c2_wait(void) {}