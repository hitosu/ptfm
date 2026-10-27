/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The device's seeding code (fw/core/store/seed.c, store.c) on a fake flash, driven a line at a time by
 * seed_test.js. Each command's answer ends with a line ".".
 *   M <hex>            a SysEx message to the device; what it sends back: S <hex> lines
 *   T <ms>             the clock moves on (the 10-s time-out)
 *   CUT <ops> <torn>   a power cut after <ops> more flash operations
 *   BOOT               power on again: the store mounts
 *   INSTALL            a firmware install: ROM's free part rewritten (the package's padding), then BOOT
 *   SAVE k n s v <hex> a save of one record on the device: R <result>
 *   UNSAVED 0|1        edits wait unsaved elsewhere (the UI's), or not
 *   EDIT k n s v <hex> a change in the store's edit memory: R <result>
 *   REVERT             the edit memory dropped
 *   DUMP               every live record: R kind num sub ver len crc (hex)
 *   ACTIVE             A 0|1: seeding under way
 *   PLAYER             P off changed early: the player told all sound off, the store changed (early: of
 *                      those, while seeding was still active, i.e. before the player could run again)
 *   Q                  the end */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store_plat.h"
#include "../core/plat.h"
#include "../core/store/store.h"
#include "../core/store/seed.h"
#include "../core/store/crc.h"

static int dead, unsaved_flag;

/* the player, as seeding tells it: counted (PLAYER prints P <sound off> <store changed> <active then>) */
static int n_off, n_changed, changed_while_active;
void player_all_sound_off(void) { n_off++; }
void player_store_changed(void) { n_changed++; if (seed_active()) changed_while_active++; }
static int edits_unsaved(void) { return unsaved_flag; }

static void out(const uint8_t *m, uint32_t n)
{
    if (n > 640) { printf("E message of %u bytes: past the 640-byte USB-MIDI limit\n", n); }
    printf("S ");
    for (uint32_t i = 0; i < n; i++) printf("%02X", m[i]);
    printf("\n");
}

static int hexbytes(const char *s, uint8_t *b, int max)
{
    int n = 0;
    while (s[0] && s[1] && n < max) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        b[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void polls(void)
{
    for (int i = 0; i < 64; i++) seed_poll();
}

int main(void)
{
    static char line[1 << 16];
    static uint8_t msg[1 << 15];
    /* the built-ins the firmware makes at start (ui_data.c): the keys' song, the EDITOR's own pattern */
    static const uint8_t keys_song[22] = {0}, editor_pattern[5] = {8, 63, 1, 0, 0};
    static const builtin_rec_t keys = {REC_SONG, 0xFE, 0, 1, sizeof keys_song, keys_song};
    static const builtin_rec_t editor = {REC_PATTERN, STORE_EDITOR_PAT, 0, 1, sizeof editor_pattern, editor_pattern};
    setvbuf(stdout, 0, _IOLBF, 0);
    fake_reset();
    fake_sysex = out;
    seed_set_unsaved(edits_unsaved);
    store_builtin_extra(&keys);
    store_builtin_extra(&editor);
    store_init();
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "Q")) break;
        if (!setjmp(cut_env)) {
            if (line[0] == 'M' && line[1] == ' ') {
                if (!dead) {
                    int n = hexbytes(line + 2, msg, sizeof msg);
                    seed_sysex(msg, (uint32_t)n);
                    polls();
                }
            } else if (line[0] == 'T' && line[1] == ' ') {
                fake_ms += (uint32_t)atoi(line + 2);
                if (!dead) polls();
            } else if (!strncmp(line, "CUT ", 4)) {
                long ops; int torn = 0;
                sscanf(line + 4, "%ld %d", &ops, &torn);
                cut_at = fake_ops + ops;
                cut_torn = torn;
            } else if (!strcmp(line, "BOOT")) {
                dead = 0;
                cut_at = -1;
                seed_init();
                store_init();
            } else if (!strcmp(line, "INSTALL")) {      /* a firmware install: FWS's free part rewritten with
                                                           the package's padding (not 0xFF), then power on */
                for (uint32_t a = fake_fw_end; a < 0x092000u; a++)
                    fake_flash[a] = (uint8_t)((a * 2654435761u) >> 24);
                dead = 0;
                cut_at = -1;
                seed_init();
                store_init();
            } else if (!strncmp(line, "SAVE ", 5)) {
                unsigned k, n, s, v;
                char hex[1 << 15];
                if (sscanf(line + 5, "%x %x %x %x %32767s", &k, &n, &s, &v, hex) == 5) {
                    int len = hexbytes(hex, msg, sizeof msg);
                    int r = store_save_begin();
                    if (!r) r = store_save_put((uint8_t)k, (uint8_t)n, (uint8_t)s, (uint8_t)v, msg, (uint32_t)len);
                    if (!r) r = store_save_run();
                    printf("R %d\n", r);
                }
            } else if (!strncmp(line, "EDIT ", 5)) {
                unsigned k, n, s, v;
                char hex[256];
                if (sscanf(line + 5, "%x %x %x %x %255s", &k, &n, &s, &v, hex) == 5) {
                    int len = hexbytes(hex, msg, sizeof msg);
                    printf("R %d\n", store_edit_put((uint8_t)k, (uint8_t)n, (uint8_t)s, (uint8_t)v, msg, (uint32_t)len));
                }
            } else if (!strcmp(line, "REVERT")) {
                store_edit_revert();
            } else if (!strncmp(line, "UNSAVED ", 8)) {
                unsaved_flag = atoi(line + 8);
            } else if (!strcmp(line, "PLAYER")) {
                printf("P %d %d %d\n", n_off, n_changed, changed_while_active);
            } else if (!strcmp(line, "ACTIVE")) {
                printf("A %d\n", seed_active());
            } else if (!strcmp(line, "DUMP")) {
                for (int i = 0; i < store_count(); i++) {
                    store_rec_t r;
                    uint32_t crc;
                    store_entry(i, &r, 0, &crc);
                    if (!r.deleted) printf("R %X %X %X %X %X %X\n", r.kind, r.num, r.sub, r.ver, r.len, crc);
                }
            }
        } else {
            dead = 1;                                   /* the power is off until BOOT */
            cut_at = -1;
            printf("CUT\n");
        }
        printf(".\n");
    }
    return 0;
}
