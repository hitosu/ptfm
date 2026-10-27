// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* CUT held 2 s (specs/ui/controls.md#cut; fw/tests/ui3_test.c's test_cut_fx_off): every effect of the song's setup
 * switched off, an edit, SOUND CUT, EFFECTS OFF; under 2 s nothing changes; REVERT ALL brings them back. Run by the
 * harness:
 *   node tests/playground/harness.js tests/playground/cut_fx.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const i = M.state.songs.findIndex(s => s.name === "CITY LIGHTS");
    if (i < 0) { ok(false, "CUT FX: CITY LIGHTS"); return; }
    const on = () => Object.keys(M.state.songs[i].fx.on).filter(k => M.state.songs[i].fx.on[k]).join();
    const on0 = on();
    ok(!M.unsaved && on0 !== "", "CUT FX: nothing unsaved, CITY LIGHTS has effects on (" + on0 + ")");
    U.song = i;
    P.playSong(i); await t.sleep(100); P.pause();               /* (played and paused: its setup the one FX edits) */
    await t.homeKey(U.BLACK[0]);                                /* (SONGS) */
    t.down("HOME"); await t.sleep(30); t.down("REC");
    await t.sleep(1500);
    ok(!M.unsaved && on() === on0, "CUT FX: CUT held 1.5 s, the effects as they were");
    await t.sleep(700);
    ok(M.unsaved && on() === "" && U.toastNow() === "SOUND CUT, EFFECTS OFF",
       "CUT FX: held 2 s, every effect off (an edit): SOUND CUT, EFFECTS OFF");
    t.up("REC"); await t.sleep(30); t.up("HOME"); await t.sleep(200);
    U.revert(); await t.sleep(100); await t.tap("OCT+"); await t.sleep(900);
    ok(!M.unsaved && on() === on0, "CUT FX: REVERT ALL, the effects on again as saved");
};
