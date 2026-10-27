// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* REVERT ALL with the song that played paused (fw/tests/ui3_test.c's test_revert_all_fx): what is heard is as saved at
 * once, its effects' setup sent again (the METRONOME switched on FX's LIVE off again), not only at its next PLAY.
 * Run by the harness:
 *   node tests/playground/harness.js tests/playground/revert_fx.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const A = t.PTFM.audio, J = JSON.stringify;
    const i = M.state.songs.findIndex(s => s.name === "CITY LIGHTS");
    if (i < 0) { ok(false, "REVERT ALL FX: CITY LIGHTS"); return; }
    ok(!M.unsaved, "REVERT ALL FX: nothing unsaved before it");
    U.song = i;
    P.playSong(i);
    await t.sleep(300);
    await t.tap("FX");
    for (let k = 0; k < 3 && U.screen.sub() !== "LIVE"; k++) await t.tap("SEQ");
    P.pauseAll();
    await t.sleep(100);
    await t.key(U.BLACK[9]);                                    /* D#5: the METRONOME */
    const was = !!M.state.songs[i].fx.on.METRONOME;
    await t.tap(was ? "OCT-" : "OCT+");
    ok(M.unsaved && !!M.state.songs[i].fx.on.METRONOME !== was, "REVERT ALL FX: the METRONOME switched (an edit)");
    await t.homeKey(1);
    const sent = [], now0 = A.fxNow;
    A.fxNow = () => { const h = A.fxHeard(); sent.push(h ? J(h.fx) : ""); return now0(); };
    U.revert(); await t.sleep(100); await t.tap("OCT+"); await t.sleep(900);
    A.fxNow = now0;
    ok(!M.unsaved && !P.playing && sent.length && sent[sent.length - 1] === J(M.savedThing("fx", i)) &&
       !M.state.songs[i].fx.on.METRONOME === !was,
       "REVERT ALL FX: the paused song's effects as saved, sent at once (" + sent.length + " sent)");
};
