/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Editing (ui_core.h): what edits change lives in the store's edit memory as pieces (store_edit_*: read first by
 * every reader, so what plays hears it from its next row or note, and an edit back as the flash holds it drops its
 * piece); here the UI's side of it: MEMORY FOR EDITS IS FULL, running low, REVERT of one thing, an edit mode's copy
 * (HOME drops what it changed), SAVE and REVERT ALL. There is no UNDO (specs/ui/saving.md#revert). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "../store/store.h"
#include "../play/player.h"
#include "../play/loops.h"

void ui_edit_init(void) { store_edit_session(0); }
uint32_t ui_edit_used(void) { return store_edit_used(); }
int ui_edit_count(void) { return store_edit_count(); }
uint32_t ui_edit_epoch(void) { return store_edit_moves(); }

static void full(void)
{
    const char *lines[3];
    lines[0] = T("MEMORY FOR EDITS IS FULL");
    lines[1] = T("SAVE BEFORE MAKING MORE EDITS");
    lines[2] = ui_txt_v(T("# UNSAVED CHANGES"), ui_dec(store_edit_count()));
    ui_ask(lines, 3, T("SAVE NOW"), T("BACK"), ui_save_answer);
}
static int result(int r)
{
    if (r == STORE_ERR_FULL) full();
    else if (r == STORE_ERR_BIG) ui_toast(T("TOO BIG FOR ONE RECORD"));
    else if (r != STORE_OK) ui_toast(T("NOT NOW: THE FLASH IS BUSY"));
    return r == STORE_OK ? 0 : -1;
}
/* running low (specs/ui/saving.md#the-edit-memory): an edit grew it past three quarters: for 2 s, at most once a
 * minute, never over a question */
static uint32_t low_at;
static uint8_t low_seen;
static void low_check(uint32_t was)
{
    uint32_t used = store_edit_used(), now = ui_now();
    const ui_overlay_t *top = ui_top();
    if (used <= was || used * 4u <= STORE_EDIT_MAX * 3u || (top && top->kind == OV_QUESTION)) return;
    if (low_seen && now - low_at < 60000u) return;
    low_seen = 1;
    low_at = now;
    g_run_t r[2] = {{T("SAVE"), C_YELLOW}, {T(" CHANGES TO FREE MEMORY"), C_WHITE}};
    ui_toast_runs(r, 2);
}
static int done(int r, uint32_t was)
{
    if (r == STORE_OK) low_check(was);
    ui_unsaved_refresh();
    ui_dirty();
    return result(r);
}
int ui_edit_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len)
{
    uint32_t was = store_edit_used();
    return done(store_edit_put(kind, num, sub, ver, payload, len), was);
}
int ui_edit_piece(uint8_t kind, uint8_t num, uint16_t id, uint8_t ver, const void *p, uint32_t len)
{
    uint32_t was = store_edit_used();
    return done(store_edit_piece(kind, num, id, ver, p, len), was);
}
int ui_edit_del(uint8_t kind, uint8_t num, uint8_t sub)
{
    uint32_t was = store_edit_used();
    return done(store_edit_del(kind, num, sub), was);
}
int ui_edit_loops(int r, uint32_t was)
{
    if (r == LP_NO) return -1;                          /* (refused: the caller says why) */
    return done(r == LP_FULL ? STORE_ERR_FULL : r == LP_BUSY ? STORE_ERR_STATE : STORE_OK, was);
}
int ui_edit_reserve(uint32_t n)
{
    if (store_edit_reserve(n)) return 0;
    full();
    return -1;
}

/* REVERT (specs/ui/saving.md#revert): the thing back as the flash has it; what plays hears it at once. Whoever
 * keeps the thing in RAM of its own (DJ's stand, the settings) reads it again. */
int ui_edit_revert(uint8_t kind, uint8_t num, uint8_t sub)
{
    int r = store_edit_forget(kind, num, sub);
    if (kind == REC_SETTINGS) ui_settings_reload();
    if (kind == REC_SONG) {                             /* (the song the effects hold: its FX setup as saved, heard) */
        player_state_t st;
        ui_player_state(&st);
        player_song_changed(num);
        if (st.song == num && (st.what == PL_SONG || st.what == PL_LOOP || st.what == PL_NONE)) player_fx_reload();
    }
    ui_unsaved_refresh();
    ui_dirty();
    return result(r);
}
/* REVERT's question: its line(s), OCT+ REVERT, OCT- KEEP; then (on REVERT) `REVERTED` */
static void (*rv_then)(void);
static void rv_answer(int a)
{
    if (a != 1 || !rv_then) return;
    rv_then();
    ui_toast(T("REVERTED"));
}
void ui_ask_revert(const char *l0, const char *l1, void (*then)(void))
{
    const char *l[2] = {l0, l1};
    rv_then = then;
    ui_ask(l, l1 ? 2 : 1, T("REVERT"), T("KEEP"), rv_answer);
}
int ui_edit_revert_slot(uint8_t cart, uint8_t slot)
{
    int r = STORE_OK;
    for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++)
        if (store_edit_obj(k, cart)) r = store_edit_forget_piece(k, cart, slot);
    ui_unsaved_refresh();
    ui_dirty();
    return result(r);
}
int ui_edit_unsaved(uint8_t kind, uint8_t num, uint8_t sub)
{
    if (kind >= REC_DX7 && kind <= REC_CART_LAST) {        /* a cartridge: any kind at its number, its samples' deletes */
        for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) if (store_edit_has(k, num, 0)) return 1;
        for (int s = 0; s < 64; s++) if (store_edit_has(REC_AUDIO, num, (uint8_t)s)) return 1;
        return 0;
    }
    return store_edit_has(kind, num, sub) != 0;
}

/* an edit mode's copy (specs/ui/controls.md): the store keeps each piece as it was before the mode first changed it */
void ui_edit_session(int on) { store_edit_session(on); }
void ui_edit_session_of(uint8_t kind, uint8_t num) { store_edit_session_of(kind, num); }
int ui_edit_session_drop(void)
{
    int r = store_edit_session_drop();
    ui_unsaved_refresh();
    ui_dirty();
    if (r) ui_toast(T("CHANGES DROPPED"));
    return r;
}

/* SAVE and REVERT ALL: the edit modes' copies go with the changes (the store drops them with the edits) */
int ui_edit_save(void) { return store_edit_save(); }
void ui_edit_drop(void)
{
    store_edit_revert();
    ui_unsaved_refresh();
    ui_dirty();
}
