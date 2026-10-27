/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor's part of the firmware's WebAssembly (specs/editor.md, The preview): built with fw/core and the
 * emulator's platform (emulator/build.py --extra editor/wasm), it lets the page call the player directly, so the
 * editor plays what it edits through the firmware's own sound code:
 *   - the page writes a flash image of what it needs into web_flash, then ed_remount() mounts it as at power-on;
 *   - ed_play_song / ed_play_pattern / ed_play_loops / ed_stop / ed_key_on / ed_key_off / ed_mutes ask the player,
 *     as the UI does;
 *   - ed_state() copies what plays and where into bytes the page reads;
 *   - ed_level() measures an instrument as AUTO LEVEL does;
 *   - ed_builtin*() hand the built-in instruments' records to the page (their names, slots, previews).
 * The same WebAssembly is the editor's virtual FM-1: the page then only seeds it, as the emulator's SEED does. */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "app.h"
#include "store/store.h"
#include "store/builtin.h"
#include "play/player.h"
#include "play/loops.h"
#include "play/dj.h"
#include "ui/ui_core.h"

#define ED_EXPORT(name) __attribute__((export_name(name)))
/* (the C names are edg_*: the firmware's own ed_* are its EDITOR's; the page knows them by their export names) */

/* the preview's instance only (never the virtual FM-1's): its user area the flash from the slot's start, so an item
 * bigger than an FM-1 holds plays (store.h, store_preview_room); then a mount */
ED_EXPORT("ed_preview_room") void edg_preview_room(uint32_t on) { store_preview_room((int)on); }

/* the flash as the page laid it out: read again, every record and the settings (the edits put before dropped) */
ED_EXPORT("ed_remount") void edg_remount(void)
{
    player_all_sound_off();
    store_edit_revert();
    store_mount();
    player_store_changed();
    ui_settings_load();
}

/* Changes on top of the flash, through the store's edit memory (as the FM-1's own edits before SAVE), so what plays
 * goes on: a record put (its payload first written into ed_edit_buf, at most 8 KB) or deleted (a built-in's change
 * deleted: the firmware's own again). Then the player told: the cells of the patterns read again from the next row
 * (ed_cells_changed: a pattern's cells, its size the same), a song's record (ed_song_changed), or everything
 * (ed_store_changed: what plays pauses). STORE_OK (0) or why not (the edit memory full: the page mounts again). */
static uint8_t edg_buf[8192];
ED_EXPORT("ed_edit_buf") uint8_t *edg_edit_buf(void) { return edg_buf; }
ED_EXPORT("ed_edit_put") int32_t edg_edit_put(uint32_t kind, uint32_t num, uint32_t sub, uint32_t ver, uint32_t len)
{
    if (len > sizeof edg_buf) return STORE_ERR_BIG;
    return store_edit_put((uint8_t)kind, (uint8_t)num, (uint8_t)sub, (uint8_t)ver, edg_buf, len);
}
ED_EXPORT("ed_edit_del") int32_t edg_edit_del(uint32_t kind, uint32_t num, uint32_t sub)
{
    return store_edit_del((uint8_t)kind, (uint8_t)num, (uint8_t)sub);
}
ED_EXPORT("ed_edit_room") uint32_t edg_edit_room(void) { return store_edit_room(); }
ED_EXPORT("ed_cells_changed") void edg_cells_changed(void) { player_source_changed(0); }
ED_EXPORT("ed_song_changed") void edg_song_changed(uint32_t song) { player_song_changed((uint8_t)song); }
ED_EXPORT("ed_store_changed") void edg_store_changed(void) { player_store_changed(); }

ED_EXPORT("ed_play_song") void edg_play_song(uint32_t song, uint32_t pos, uint32_t row)
{
    player_play_song((uint8_t)song, (uint8_t)pos, (uint8_t)row);
}

/* the pattern in a loop, at a song's tempo, mixer and FX (song 0xFF: the pattern loop's defaults): the song is
 * started first, in the same half, so the loop takes it as its current song; it never sounds */
ED_EXPORT("ed_play_pattern") void edg_play_pattern(uint32_t song, uint32_t pat, uint32_t row)
{
    if (song < 0x100u) player_play_song((uint8_t)song, 0, 0);
    player_play_pattern((uint8_t)pat, (uint8_t)row);
}

/* a record as the store answers it (the firmware's own where nothing was saved: the keys' song FE, the EDITOR's
 * pattern FE): its payload into ed_edit_buf, the answer its version << 24 | its length; -1 none */
ED_EXPORT("ed_record") int32_t edg_record(uint32_t kind, uint32_t num, uint32_t sub)
{
    uint32_t len = 0;
    uint8_t ver = 0;
    const uint8_t *p = store_get((uint8_t)kind, (uint8_t)num, (uint8_t)sub, &len, &ver);
    if (!p || len > sizeof edg_buf) return -1;
    memcpy(edg_buf, p, len);
    return (int32_t)((uint32_t)ver << 24 | len);
}
/* the records a new store starts from where it has none (files/dj.md, files/loops.md): into ed_edit_buf, their
 * length answered */
ED_EXPORT("ed_dj_default") uint32_t edg_dj_default(void) { return dj_record_default(edg_buf); }
ED_EXPORT("ed_loops_default") uint32_t edg_loops_default(void) { return loops_record_default(edg_buf); }

/* the loops, all of them together, from their start (as LOOPS' PLAY) */
ED_EXPORT("ed_play_loops") void edg_play_loops(void) { loops_play(0); }

ED_EXPORT("ed_pause") void edg_pause(void) { player_pause(); }
ED_EXPORT("ed_continue") void edg_continue(void) { player_continue(); }
ED_EXPORT("ed_stop") void edg_stop(void) { player_stop(); }
ED_EXPORT("ed_all_off") void edg_all_off(void) { player_all_sound_off(); }
ED_EXPORT("ed_mutes") void edg_mutes(uint32_t muted) { player_mutes(muted); }
ED_EXPORT("ed_voices") void edg_voices(uint32_t n) { player_voice_count((int)n); }

ED_EXPORT("ed_key_on") void edg_key_on(uint32_t note, uint32_t cart, uint32_t slot, uint32_t vel)
{
    pl_inst_t in = { (uint8_t)cart, (uint8_t)slot };
    player_key_on((uint8_t)note, in, (uint8_t)(vel ? vel : 0x7F));
}
ED_EXPORT("ed_key_off") void edg_key_off(uint32_t note) { player_key_off((uint8_t)note); }
ED_EXPORT("ed_keys_off") void edg_keys_off(void) { player_keys_off(); }

/* what plays and where, as bytes (the page's preview.js reads them):
 *   0 what (PL_*), 1 playing, 2 ended, 3 song, 4 pos, 5 pat, 6 row, 7 rows - 1, 8 tracks, 9 voices sounding,
 *   10-11 tempo, BPM x 10, 12 tick, 13 ticks a row, 14 voice count, 15 parked, 16-19 halves played,
 *   20-23 muted tracks, 24-39 each track's last note (0xFF none), 40-55 a track sounds (0 / 1),
 *   56-71 each track's instrument's cartridge, 72-87 its slot, 88-91 voices taken (steals) */
static uint8_t ed_st[96];
ED_EXPORT("ed_state") uint8_t *edg_state(void)
{
    player_state_t s;
    int t;
    player_state(&s, 0);
    memset(ed_st, 0, sizeof ed_st);
    ed_st[0] = s.what;
    ed_st[1] = s.playing;
    ed_st[2] = s.ended;
    ed_st[3] = s.song;
    ed_st[4] = s.pos;
    ed_st[5] = s.pat;
    ed_st[6] = s.row;
    ed_st[7] = s.rows;
    ed_st[8] = s.ntrk;
    ed_st[9] = s.voices;
    ed_st[10] = (uint8_t)(s.bpm10 & 255u);
    ed_st[11] = (uint8_t)(s.bpm10 >> 8);
    ed_st[12] = s.tick;
    ed_st[13] = s.speed;
    ed_st[14] = s.voice_count;
    ed_st[15] = s.parked;
    memcpy(ed_st + 16, &s.halves, 4);
    memcpy(ed_st + 20, &s.muted, 4);
    for (t = 0; t < PL_TRACKS; t++) {
        ed_st[24 + t] = s.track[t].note;
        ed_st[40 + t] = s.track[t].sounding;
        ed_st[56 + t] = s.track[t].inst.cart;
        ed_st[72 + t] = s.track[t].inst.slot;
    }
    memcpy(ed_st + 88, &s.steals, 4);
    return ed_st;
}

/* an instrument's LEVEL as AUTO LEVEL measures it: the level in half dB + 1000, or 0 (silent, nothing there) */
ED_EXPORT("ed_level") int32_t edg_level(uint32_t cart, uint32_t slot)
{
    int8_t lv;
    if (!ui_level_measure((uint8_t)cart, (int)slot, &lv)) return 0;
    return 1000 + lv;
}

/* the built-in instruments: how many records, and one's header and payload (kind, num, sub, ver, u32 length,
 * u32 the payload's address in the WebAssembly's memory) */
static uint8_t ed_bi[12];
ED_EXPORT("ed_builtin_count") int32_t edg_builtin_count(void) { return builtin_count; }
ED_EXPORT("ed_builtin") uint8_t *edg_builtin(uint32_t i)
{
    uint32_t p;
    memset(ed_bi, 0, sizeof ed_bi);
    if ((int)i >= builtin_count) return ed_bi;
    ed_bi[0] = builtin_recs[i].kind;
    ed_bi[1] = builtin_recs[i].num;
    ed_bi[2] = builtin_recs[i].sub;
    ed_bi[3] = builtin_recs[i].ver;
    memcpy(ed_bi + 4, &builtin_recs[i].len, 4);
    p = (uint32_t)(uintptr_t)builtin_recs[i].data;
    memcpy(ed_bi + 8, &p, 4);
    return ed_bi;
}
