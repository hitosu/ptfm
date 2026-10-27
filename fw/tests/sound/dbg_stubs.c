/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the debug build's figures (fw/core/dbg.c) need beyond the sound, as nothing, so c2_test runs with PTFM_DEBUG:
 * the player's debug counters on both threads, its sound the same as without them (run_sound.sh compares). */
#include <string.h>
#include "../../core/plat.h"
#include "../../core/dbg.h"
#include "../../core/remote.h"
#include "../../core/ui/ui_core.h"

int16_t ui_set[S_COUNT];
void plat_cpu(plat_cpu_t *c, int reset) { (void)reset; memset(c, 0, sizeof *c); }
void plat_dbg(plat_dbg_t *d) { memset(d, 0, sizeof *d); }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t was) { (void)was; }
uint32_t plat_usb_resets(void) { return 0; }
void remote_dj_put(uint8_t *p) { (void)p; }
int remote_send(uint8_t cmd, const uint8_t *b, uint32_t len) { (void)cmd; (void)b; (void)len; return 0; }
int bench_sysex(int cmd) { (void)cmd; return 0; }
void bench_poll(void) {}
const ui_screen_t *ui_screen_now(void) { return 0; }
void ui_toast(const char *text) { (void)text; }
const char *ui_txt_s(const char *s) { return s; }
