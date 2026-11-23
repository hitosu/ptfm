// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* A save in silence (specs/ui/saving.md; fw/tests/ui3_test.c's test_save_cut): at SAVE all sound off as CUT does,
 * before anything is written; the save once it has faded; the sound back at its end. Run by the harness:
 *   node tests/playground/harness.js tests/playground/save_cut.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t;
    const i = M.state.songs.findIndex(s => s && s.name === "CITY LIGHTS");
    if (i < 0) { ok(false, "SAVE's CUT: CITY LIGHTS"); return; }
    const calls = [];
    const audioHush = t.PTFM.audio.hush;
    t.PTFM.audio.hush = on => { calls.push({ on, saving: !!U.saving, unsaved: M.unsaved }); audioHush(on); };
    const k = Object.keys(M.state.songs[i].fx.on)[0];
    U.edit(() => { M.state.songs[i].fx.on[k] = !M.state.songs[i].fx.on[k]; });   /* (something to save) */
    await t.sleep(100);
    ok(M.unsaved && !U.saving, "SAVE's CUT: an edit, unsaved");
    await t.tap("SAVE");
    const first = calls[0];
    ok(first && first.on === true && first.saving && first.unsaved,
       "SAVE's CUT: SAVE turns the sound off first, nothing written yet (" + JSON.stringify(first) + ")");
    await t.sleep(1500);
    const last = calls[calls.length - 1];
    ok(last && last.on === false && !last.saving && !M.unsaved && U.toastNow() === "SAVED",
       "SAVE's CUT: SAVED, the sound back at the save's end (" + JSON.stringify(last) + ")");
    t.PTFM.audio.hush = audioHush;
};
