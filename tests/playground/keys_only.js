// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The playground with no song at all (fw/tests/ui3_test.c's test_keys_only, run on no disk): the keys play as a
 * synth through the keys' song (specs/ui/screens/fx.md#the-fx-setup), never listed; FX edits its setup, GLO its
 * TEMPO and SWING; its changes kept as edits, SAVE keeps them; every screen opens. Run by the harness:
 *   node tests/playground/harness.js tests/playground/keys_only.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const A = t.PTFM.audio;
    /* each step's error caught, so one breaking does not hide the rest */
    const errs = [];
    const step = async (what, fn) => { try { await fn(); } catch (e) { errs.push(what + ": " + String(e && e.stack || e).split("\n").slice(0, 2).join(" ")); } };

    /* the flash with no song (as an FM-1 with no demo disk) */
    M.state.songs.length = 0;
    U.song = 0;
    M.save();
    await t.homeKey(8);                                         /* (PLAYBACK) */
    t.frame();
    ok(A.fxSong() === M.state.keys && P.tempo() === 120 && J(M.state.keys.fx) === J(M.newKeys().fx) &&
       M.state.keys.fx.on.REVERB && J(Object.assign(M.newFx(), { on: Object.assign(M.newFx().on, { REVERB: true }) })) === J(M.state.keys.fx),
       "keys only: the keys' song held, 120 BPM, a new song's FX setup with the REVERB on");
    ok(!M.state.songs.length, "... never listed");

    await step("a key", () => t.key(10, 200));

    /* FX: the keys' setup, LIVE, EDIT and a MIXER of the KEYS column alone (no tracks) */
    await step("FX", async () => { await t.tap("FX"); t.frame(); });
    ok(U.screen.name === "FX" && U.screen.seq() === M.pageOf(0, 3), "keys only: FX opens, its pages LIVE, EDIT and MIXER");
    await step("SEQ twice", async () => { await t.tap("SEQ"); await t.tap("SEQ"); t.frame(); });
    ok(U.screen.seq() === M.pageOf(2, 3) && U.screen.footer()[0] === "KEYS", "... SEQ twice: the MIXER, the KEYS column alone");
    await step("SEQ", async () => { await t.tap("SEQ"); t.frame(); });
    ok(U.screen.seq() === M.pageOf(0, 3), "... SEQ: back on LIVE");
    const on0 = J(M.state.keys.fx.on), kn0 = J(M.state.keys.fx.knobs);
    await step("OCT+ and K1 on FX", async () => { await t.tap("OCT+"); await t.turn("K1", 3); t.frame(); });   /* (GATE, at first) */
    ok(J(M.state.keys.fx.on) !== on0 && J(M.state.keys.fx.knobs) !== kn0 && M.unsavedThing("fx", "keys"),
       "keys only: OCT+ and K1 on FX change the keys' setup, kept as an edit");
    ok(!M.state.songs.length, "... still no song listed");

    /* GLO: FX PROPERTIES FOR / THE KEYS: TEMPO and SWING first */
    await step("GLO", async () => { await t.tap("GLO"); t.frame(); });
    const g = U.overlays[U.overlays.length - 1] || {};
    ok(g.title === "FX PROPERTIES FOR" && g.sub === "THE KEYS"
       && J((g.items || []).map(it => it.name)) === J(["TEMPO", "SWING", "ROUTING", "KEYS, EVERYWHERE"]),
       "keys only: GLO on FX: FX PROPERTIES FOR, THE KEYS; TEMPO, SWING, ROUTING, KEYS, EVERYWHERE");
    ok(g.items && g.items[0].get() === "120 BPM" && g.items[1].get() === "50 %", "... TEMPO 120 BPM, SWING 50 %");
    await step("TEMPO", async () => {
        await t.turn("K1", 1);
        t.down("SEL"); await t.sleep(30); await t.turn("K1", 1); t.up("SEL"); await t.sleep(150);
    });
    ok(M.state.keys.tempo === 131 && P.tempo() === 131, "keys only: TEMPO +1, then +10 with SEL held: 131 BPM, heard at once");
    await step("SWING", async () => { await t.turn("SELECT", 1); await t.turn("K1", 1); t.shot("keys_fx_props"); await t.tap("GLO"); });
    ok(M.state.keys.swing === 51, "keys only: SWING +1: 51 %");
    await step("SAVE", () => { M.save(); M.revert(); });
    ok(M.state.keys.tempo === 131 && M.state.keys.swing === 51 && J(M.state.keys.fx.on) !== on0,
       "keys only: SAVE keeps the keys' song's changes");

    /* SONGS: none listed; its menu's NEW makes one, which FX then edits (no TEMPO in its GLO) */
    await step("SONGS", async () => { await t.homeKey(1); t.frame(); });
    ok(U.screen.name === "SONGS" && !M.state.songs.length, "keys only: SONGS opens, no song listed");
    await step("SONGS' SEL", async () => { await t.tap("SEL"); t.frame(); });
    const m = U.overlays[U.overlays.length - 1] || {};
    ok(m.kind === "menu" && m.title === "SONG 00", "keys only: SONGS' SEL: the menu SONG 00");
    await step("NEW", async () => {
        if (m.items) await t.turn("SELECT", -m.items.length);
        await t.tap("SEL"); t.frame();
    });
    /* NEW SONG STARTS WITH: + NEW PATTERN first, then the patterns; HOME makes nothing; + NEW PATTERN picked: a song
     * whose one position is a new empty 8 x 64 pattern at the first free number */
    const l = U.overlays[U.overlays.length - 1] || {};
    ok(l.kind === "list" && l.title === "NEW SONG STARTS WITH", "keys only: NEW opens the pattern list NEW SONG STARTS WITH");
    await step("PLAY on a pattern", async () => { await t.tap("PLAY"); t.shot("new_song_list"); await t.tap("PLAY"); });
    await step("HOME", async () => { await t.tap("HOME"); t.frame(); });
    ok(!M.state.songs.length && !t.top(), "... HOME: no song made");
    await step("NEW again", async () => {
        await t.tap("SEL"); const mm = U.top() ? U.overlays[U.overlays.length - 1] : null;
        if (mm && mm.goto) mm.goto("NEW");
        await t.tap("SEL"); await t.turn("SELECT", -3); t.frame();
    });
    const free = M.freePattern();
    await step("+ NEW PATTERN", async () => { await t.tap("SEL"); t.frame(); });
    const s0 = M.state.songs[0], p0 = s0 && M.pattern(s0.order[0]);
    ok(M.state.songs.length === 1 && s0.name === "NEW SONG" && s0.order.length === 1 && s0.order[0] === free &&
       p0 && p0.tracks === 8 && p0.rows === 64 && !p0.cells.some(Boolean),
       "keys only: + NEW PATTERN: NEW SONG, its one position a new empty 8 x 64 pattern at the first free number");
    await step("FX before the new song plays", async () => { await t.tap("FX"); t.frame(); });
    ok(A.fxSong() === M.state.keys, "... FX still edits THE KEYS: the new song has not played");
    await step("FX of the new song, played", async () => {
        await t.tap("FX"); await t.tap("PLAY"); await t.tap("PLAY");   /* (SONGS again: played, paused) */
        await t.tap("FX"); await t.tap("GLO"); t.frame();
    });
    const g2 = U.overlays[U.overlays.length - 1] || {};
    ok(A.fxSong() === M.state.songs[0] && g2.sub === "NEW SONG" && !(g2.items || []).some(it => it.name === "TEMPO"),
       "... played: FX edits NEW SONG's setup, its GLO without TEMPO and SWING");
    await step("GLO closed", async () => { await t.tap("GLO"); await t.tap("FX"); });
    /* NEW again, a pattern picked: a song of that pattern, after the first */
    const first = M.state.patterns.filter(Boolean)[0].n;
    await step("NEW, a pattern", async () => {
        await t.homeKey(1);
        await t.tap("SEL"); U.overlays[U.overlays.length - 1].goto("NEW");
        await t.tap("SEL"); await t.turn("SELECT", -3); await t.turn("SELECT", 1);
        await t.tap("SEL"); t.frame();
    });
    ok(M.state.songs.length === 2 && J(M.state.songs[1].order) === J([first]) && U.song === 1,
       "keys only: NEW, pattern " + M.hex(first) + " picked: a second song of that pattern, current");
    await step("no song again", () => { M.state.songs.length = 0; U.song = 0; M.version++; });

    /* every screen by HOME + its black key, drawn, with a key played on each */
    for (const k of [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25])
        await step("HOME + key " + k, async () => { await t.homeKey(k); t.frame(); await t.key(12, 80); t.frame(); });
    await step("back", async () => { await t.homeKey(8); t.frame(); });
    ok(!errs.length, "keys only: every screen opened" + (errs.length ? ": " + errs.join(" | ") : ""));
};
const J = JSON.stringify;
