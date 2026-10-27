// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* What the keys play through with nothing playing (fw/tests/ui3_test.c's test_cue, test_loops_cue, test_unload;
 * specs/ui/screens/fx.md#the-fx-setup): at the start the keys' song, whatever songs there are, another song picked
 * changing nothing; LOOPS shown, the loops' setup at the loops' tempo, before the first loop too; LOOPS left, the keys'
 * song again; a song playing while LOOPS is shown is what is heard; one paused there leaves the loops heard, and after
 * LOOPS it is held again; UNLOAD SONG (SONGS' SEL menu, PLAYBACK's GLO, only while a song is held) cuts it at once and
 * holds the keys' song again. Run by the harness:
 *   node tests/playground/harness.js tests/playground/loops_cue.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const A = t.PTFM.audio;
    const song0 = M.state.songs[U.song], keys = M.state.keys;
    ok(!P.playing && A.fxHeard() === keys && A.fxSong() === keys && P.tempo() === M.bpm(keys) && song0 && song0 !== keys,
       "nothing played: the keys' song's setup and tempo heard and edited (not the current song's)");
    const cur = U.song;
    U.song = cur === 0 ? 1 : 0;
    ok(A.fxHeard() === keys && P.tempo() === M.bpm(keys), "... another song picked: nothing changes");
    U.song = cur;
    await t.homeKey(22);                                        /* LOOPS */
    ok(U.screen.name === "LOOPS" && !M.state.loops.list.length, "LOOPS, no loop yet");
    ok(A.fxHeard() === M.state.loops && P.tempo() === M.bpm(M.state.loops),
       "LOOPS shown, nothing playing: the loops' setup heard, their tempo the keys'");
    await t.tap("GLO"); await t.turn("K1", 7); await t.tap("GLO");   /* LOOPS PROPERTIES: TEMPO first */
    ok(P.tempo() === M.bpm(M.state.loops) && M.state.loops.tempo === 127, "... LOOPS' TEMPO +7, heard at once");
    await t.tap("FX");
    ok(U.screen.name === "FX" && A.fxSong() === M.state.loops && A.fxHeard() === M.state.loops && P.tempo() === M.bpm(M.state.loops),
       "... FX from LOOPS: the loops' setup edited and heard, at their tempo");
    await t.homeKey(8);                                         /* PLAYBACK */
    ok(A.fxHeard() === keys && P.tempo() === M.bpm(keys) && M.bpm(keys) !== M.bpm(M.state.loops),
       "LOOPS left: the keys' song's setup and tempo again");

    /* a song playing while LOOPS is shown: the song is heard; paused there, the loops; LOOPS left, the song again */
    await t.tap("PLAY"); await t.sleep(400);
    ok(P.playing && P.what === "song", "a song plays");
    await t.homeKey(22);
    ok(P.playing && A.fxHeard() === song0 && A.fxSong() === M.state.loops, "LOOPS shown, a song playing: the song heard, LOOPS' FX edits the loops'");
    P.pause(); await t.sleep(100);
    ok(!P.playing && A.fxHeard() === M.state.loops && P.tempo() === M.bpm(M.state.loops), "... the song paused on LOOPS: the loops heard, at their tempo");
    await t.homeKey(8);
    ok(A.fxHeard() === song0 && P.tempo() === M.bpm(song0), "LOOPS left: the paused song held again, its setup and tempo");

    /* UNLOAD SONG */
    const menuHas = async item => {                             /* SONGS' SEL menu: whether it lists item (closed again) */
        await t.tap("SEL");
        const m = U.top(), has = t.top() === "menu" && m.items.includes(item);
        await t.tap("HOME");
        return has;
    };
    const unload = async () => {                                /* ... and picked */
        await t.tap("SEL");
        const m = U.top();
        m.sel = m.items.indexOf("UNLOAD SONG");
        await t.tap("SEL");
    };
    await t.homeKey(1);                                         /* SONGS */
    ok(U.screen.name === "SONGS" && await menuHas("UNLOAD SONG"), "a song held: SONGS' menu has UNLOAD SONG");
    await t.tap("PLAY"); await t.sleep(400);
    ok(P.playing, "... the song plays again");
    await unload();
    ok(!P.playing && !P.held() && A.fxHeard() === keys && A.fxSong() === keys && P.tempo() === M.bpm(keys) && U.toastNow() === "SONG UNLOADED",
       "UNLOAD SONG: cut, the keys' song held and edited again, told");
    ok(!await menuHas("UNLOAD SONG"), "... SONGS' menu has no UNLOAD SONG then");
    await t.homeKey(8);                                         /* PLAYBACK's GLO: SCROLL alone */
    await t.tap("GLO");
    ok(t.top() === "props" && U.top().items.length === 1 && U.top().items[0].name === "SCROLL", "nothing held: PLAYBACK's GLO, SCROLL alone");
    await t.tap("GLO");
    await t.tap("PLAY"); await t.sleep(400);
    await t.tap("GLO");
    ok(U.top().items.length === 2 && U.top().items[1].name === "UNLOAD SONG" && U.top().items[1].get() === song0.name,
       "a song plays: PLAYBACK's GLO has UNLOAD SONG, the song's name");
    await t.turn("SELECT", 1);
    t.shot("playback_glo_unload");
    await t.tap("EDIT");
    ok(!t.top() && !P.playing && !P.held() && A.fxHeard() === keys, "... EDIT on it: the panel closed, the song unloaded");

    /* DJ played: UNLOAD SONG (DJ) cuts the decks, DJ off, its stand kept; DJ's PLAY plays again */
    await t.homeKey(10);                                        /* DJ: OP4 loads the highlighted song on the left */
    await t.key(8); await t.tap("PLAY"); await t.sleep(400);
    const djSong = P.dj && P.dj.decks ? P.dj.decks[0].song : null;
    ok(P.what === "dj" && P.playing, "DJ: the left deck plays");
    await t.homeKey(8); await t.tap("GLO"); await t.turn("SELECT", 1);
    const it = U.top() && U.top().items[1], itName = it && it.get();
    await t.tap("EDIT");
    ok(itName === "DJ" && !P.held() && !P.playing && A.fxHeard() === keys && (!P.dj || P.dj.decks[0].song === djSong),
       "PLAYBACK's GLO: UNLOAD SONG (DJ): the decks cut, DJ off, its stand kept; the keys' song held");
    await t.homeKey(10); await t.tap("PLAY"); await t.sleep(400);
    ok(P.what === "dj" && P.playing, "... DJ's PLAY plays again");
    P.pause();
    M.revert();
};
