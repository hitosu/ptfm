// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The insert and LFO's eraser with the loops stopped (specs/ui/screens/loops.md#the-insert,
 * #lfo-held-erasing; fw/tests/ui3_test.c's test_loops_lfo_tools, test_loops_insert and test_loops_insert_play): the keys
 * put their notes at the play position as keys pressed there while the loops play would, the time going on at TEMPO while
 * they are held, the position still (POLY a chord, each note as long as its key was held; past four keys neither written
 * nor heard; MONO the first key alone; drawn as it grows; the next insert at that row replaces it; held past the length's
 * end, ended there; OVERWRITE over all four lanes; PLAY with keys held: they write on as the loops play); LFO held makes no
 * other insert, its release then switches nothing; LFO held + ALGORITHM erases the rows it passes; playing, none of it.
 * The keys' ends round PLAY and a jump: none left sounding by a jump, none cut short however far ahead, none lost as PLAY
 * starts; an insert's key that writes no more taken at PLAY, never written again. Run by the harness:
 *   node tests/playground/harness.js tests/playground/loops_lfo.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const B = U.B, stg = () => U.screens.LOOPS.stage(), LS = () => M.state.loops;
    const isNote = c => !!c && c.note != null && c.note !== M.OFF;
    const isOff = c => !!c && c.note === M.OFF;
    const sCell = (r, l = 0) => stg().cells ? stg().cells[r * 4 + l] : null;
    const sNote = (r, l = 0) => { const c = sCell(r, l); return isNote(c) ? c.note : -1; };
    const sNotes = () => (stg().cells || []).map((c, k) => isNote(c) ? (k >> 2) + ":" + (k & 3) + ":" + c.note : null).filter(Boolean).join();
    const find = (r, m) => [0, 1, 2, 3].find(l => sNote(r, l) === m) ?? -1;   /* (the lane holding m's note on row r) */
    const end = (r, l) => { for (let x = r + 1; x < LS().rows; x++) if (isOff(sCell(x, l)) || isNote(sCell(x, l))) return x; return -1; };
    const row = () => P.kept.loops.row || 0;
    const to = async r => { if (row() !== r) await t.turn("ALGORITHM", r - row()); };
    const empty = () => { const s = stg(); s.cells = null; s.erasers = []; M.version++; };
    const sounded = new Set(), k0 = U.keyOn;
    U.keyOn = (n, i) => { sounded.add(n); return k0(n, i); };
    async function chord(keys, atOnce, lfo = true) {
        if (lfo) { t.down("LFO"); await t.sleep(30); }
        if (atOnce) {
            for (const k of keys) t.keyDown(k);
            await t.sleep(40);
            for (const k of keys) t.keyUp(k);
        } else for (const k of keys) { t.keyDown(k); await t.sleep(40); t.keyUp(k); await t.sleep(40); }
        await t.sleep(40);
        if (lfo) { t.up("LFO"); await t.sleep(100); }
    }
    await t.homeKey(22);
    ok(U.screen.name === "LOOPS" && !P.playing, "the insert: LOOPS, stopped");
    const over = t.leds[B.LFO] === 1;
    await chord([11, 4, 7], true);
    ok(sNote(0, 0) === U.midi(11) && sNote(0, 1) === U.midi(4) && sNote(0, 2) === U.midi(7) && isOff(sCell(1, 0)) && isOff(sCell(1, 1)) &&
       isOff(sCell(1, 2)) && !P.playing && (t.leds[B.LFO] === 1) === over,
       "LFO held + a chord, stopped: the insert, as without LFO: a lane each in the keys' order at the play position, each ended on the next row (held under a row); LFO used: OVERDUB / OVERWRITE as it was (" + sNotes() + ")");
    await chord([0, 9], false);
    ok(sNote(0, 0) === U.midi(9) && sNote(0, 1) < 0 && sNote(0, 2) < 0 && isOff(sCell(1, 0)) && !sCell(1, 1),
       "... again, keys one after the other: the last alone replaces the row (" + sNotes() + ")");
    await to(4); await chord([11], true);
    await to(8); await chord([4], true);
    await to(12); await chord([7], true);
    await to(2);
    t.down("LFO"); await t.sleep(30);
    await t.turn("ALGORITHM", 1);
    await t.turn("ALGORITHM", 2);
    ok(row() === 5 && sNote(4) < 0 && !sCell(5) && sNote(8) >= 0 && sNote(0) >= 0,
       "LFO held + ALGORITHM: the rows entered and left erased (row 4's note and its end), the row the hold began on and the one it stands on kept (" + sNotes() + "; at " + row() + ")");
    await t.turn("ALGORITHM", -1);
    t.up("LFO"); await t.sleep(100);
    await to(8);
    t.down("LFO"); await t.sleep(30);
    await t.turn("ALGORITHM", 1); await t.turn("ALGORITHM", -1);
    ok(sNote(8) >= 0, "... back onto the row the hold began on: kept while it stands there");
    await t.turn("ALGORITHM", -1);
    t.up("LFO"); await t.sleep(100);
    ok(sNote(8) < 0 && !sCell(9) && sNote(12) >= 0 && sNote(0) >= 0 && (t.leds[B.LFO] === 1) === over,
       "... left again: erased; the other notes kept; LFO used: nothing switched (" + sNotes() + ")");
    await to(10);
    await t.tap("PLAY"); await t.sleep(100);
    t.down("LFO"); await t.sleep(30); await t.turn("ALGORITHM", 2); t.up("LFO"); await t.sleep(100);
    await t.tap("PLAY"); await t.sleep(100);
    ok(sNote(12) >= 0 && sNote(0) >= 0, "LFO held + ALGORITHM while playing: a jump, nothing erased");
    await t.tap("LFO"); await t.sleep(100);
    ok(U.toastNow() === (over ? "OVERDUB" : "OVERWRITE"), "LFO tapped alone: switches as ever");
    await t.tap("LFO"); await t.sleep(100);

    /* the insert (ui3's test_loops_insert): a row's time at TEMPO */
    empty();
    const rms = LS().ticks * 2500 / LS().tempo;
    await to(8);
    sounded.clear();
    t.keyDown(0); await t.sleep(rms);                     /* (a chord: the second key a row later; held 2 and 3 rows) */
    t.keyDown(4); await t.sleep(rms);
    const both = find(8, U.midi(0)) >= 0 && find(8, U.midi(4)) >= 0 && sounded.has(4);
    t.shot("loops_insert_held");
    t.keyUp(0); await t.sleep(2 * rms);
    t.keyUp(4); await t.sleep(100);
    const la = find(8, U.midi(0)), lb = find(8, U.midi(4)), ea = la >= 0 ? end(8, la) : -1, eb = lb >= 0 ? end(8, lb) : -1;
    ok(both && la >= 0 && lb >= 0 && la !== lb && ea === 10 && eb === 11 && !P.playing && row() === 8,
       "the insert, POLY: two keys a row apart, held 2 and 3 rows: both on the play position's row 8 (lanes " + la + " " + lb +
       "), heard, each as long as it was held (to rows " + ea + " and " + eb + "); nothing played, the position kept");
    await to(16);
    sounded.clear();
    const five = [0, 2, 4, 5, 7];
    for (const k of five) t.keyDown(k);
    await t.sleep(40);
    for (const k of five) t.keyUp(k);
    await t.sleep(100);
    const n4 = five.slice(0, 4).filter(k => find(16, U.midi(k)) >= 0).length;
    ok(n4 === 4 && find(16, U.midi(7)) < 0 && !sounded.has(7), "... five keys: four on the row (" + n4 + "), the fifth neither written nor heard (" + sNotes() + ")");
    await to(24);
    LS().nextMono = true; M.version++;
    sounded.clear();
    t.keyDown(2); await t.sleep(40);
    t.keyDown(9); await t.sleep(40);
    t.keyUp(9); t.keyUp(2); await t.sleep(100);
    LS().nextMono = false; M.version++;
    ok(find(24, U.midi(2)) >= 0 && find(24, U.midi(9)) < 0 && !sounded.has(9), "... MONO: the first key alone, the second neither written nor heard");
    await to(32);
    t.keyDown(9); await t.sleep(40); t.keyUp(9); await t.sleep(50);
    t.keyDown(11); await t.sleep(40); t.keyUp(11); await t.sleep(50);
    ok(find(32, U.midi(11)) >= 0 && find(32, U.midi(9)) < 0, "... again at that row: the last insert replaced");
    const last = LS().rows - 2;
    await to(last);
    t.keyDown(5); await t.sleep(4 * rms); t.keyUp(5); await t.sleep(50);
    const l5 = find(last, U.midi(5));
    ok(l5 >= 0 && end(last, l5) < 0 && isOff(sCell(0, l5)), "... held past the length's end: its note to the end, its off on row 0 (lane " + l5 + ", " + sNotes() + ")");
    await to(41);                                         /* (OVERWRITE: two notes on row 41, then an insert from 40, 3 rows) */
    t.keyDown(2); t.keyDown(4); await t.sleep(40); t.keyUp(2); t.keyUp(4); await t.sleep(50);
    await t.tap("LFO"); await t.sleep(50);
    await to(40);
    t.keyDown(0); await t.sleep(3 * rms); t.keyUp(0); await t.sleep(50);
    const gone = find(41, U.midi(2)) < 0 && find(41, U.midi(4)) < 0, l0 = find(40, U.midi(0));
    await t.tap("LFO"); await t.sleep(50);
    ok(gone && l0 >= 0 && end(40, l0) === 43, "... OVERWRITE: its span over all four lanes (row 41's notes gone; its own to row " + (l0 >= 0 ? end(40, l0) : -1) + ")");
    empty();
    await to(4);
    const lpn = P.lpNote, heard = [];
    P.lpNote = (...a) => { if (a[2] && a[2].note != null) heard.push(a[2].note); return lpn(...a); };
    t.keyDown(9);
    const t9 = P.time();
    await t.sleep(2 * rms);
    await t.tap("PLAY"); await t.sleep(50);
    const on = P.playing;
    await t.sleep(2 * rms);
    const sp9 = LS().ticks, pos9 = 4 * sp9 + Math.floor(Math.floor((P.time() - t9) * 1000) * LS().tempo / 2500);
    const want9 = Math.floor((2 * pos9 + sp9) / (2 * sp9));
    t.keyUp(9); await t.sleep(100);
    P.lpNote = lpn;
    await t.tap("PLAY"); await t.sleep(100);
    const l9 = find(4, U.midi(9));
    ok(on && heard.includes(U.midi(9)) && l9 >= 0 && want9 > 8 && end(4, l9) === want9, "... PLAY with its key held: it writes on as the loops " +
       "play, its note as long as it was held (row 4 to " + (l9 >= 0 ? end(4, l9) : -1) + ", " + want9 + " wanted), heard from the loops " +
       "meanwhile (" + sNotes() + ")");

    /* the keys' ends round PLAY and a jump (ui3's test_loops_insert_play): a key's end a jump passes over heard where it
     * lands (a key pressed while playing, ALGORITHM late in its row; a key held into PLAY, its own time ahead); an end
     * more than half the length ahead of the play line heard at its row, its first pass too (let go, or held to the
     * length's end); one let go as PLAY starts the loops, before they are heard, heard to its own time; an insert's key
     * that stopped writing (its time at the length's end, a new instrument, ALGORITHM moving the play position) taken at
     * PLAY, its free sound let go, never written again. The voices each key's lane starts, and their ends, as sent */
    const A = t.PTFM.audio, on0 = A.noteOn, off0 = A.noteOff, vs = {}, kOff0 = U.keyOff, taken = [];
    A.noteOn = (id, time, ...a) => { vs[id] = { on: time, off: null }; return on0(id, time, ...a); };
    A.noteOff = (id, time) => { const v = vs[id]; if (v && (v.off == null || time < v.off)) v.off = time; return off0(id, time); };
    U.keyOff = n => { taken.push(n); return kOff0(n); };
    const sounds = n => Object.entries(vs).some(([id, v]) => id.includes(".") && id.endsWith("k" + n) && v.on <= P.time() && (v.off == null || v.off > P.time()));
    const count = m => (stg().cells || []).filter(c => isNote(c) && c.note === m).length;
    const at = () => { const h = P.playing && P.what === "loops" ? P.at() : null; return h && h.pass != null ? h : null; };
    const until = async (f, ms = 20000) => {           /* (the page's frames meanwhile: what follows the time follows it) */
        const end = P.time() + ms / 1000;
        while (!f() && P.time() < end) { t.frame(); await t.sleep(10); }
        return f();
    };
    const rowAt = (r, frac = 0, pass) => until(() => { const h = at(); return !!h && h.row === r && (pass == null || h.pass === pass) && (P.time() - h.t) / h.dur >= frac; });
    const stop = async () => { if (P.playing) await t.tap("PLAY"); await t.sleep(60); };
    const wantOf = (r, t0) => { const sp = LS().ticks, pos = r * sp + Math.floor(Math.floor((P.time() - t0) * 1000) * LS().tempo / 2500); return Math.floor((2 * pos + sp) / (2 * sp)); };
    const inst0 = U.inst, rows0 = LS().rows, m9 = U.midi(9);
    U.setInst(0xF900);
    empty();
    await to(0);
    await t.tap("PLAY");
    await rowAt(8); t.keyDown(9);
    await rowAt(10, .7);
    await t.turn("ALGORITHM", 1);
    await until(() => false, 600);
    let l = find(8, m9), e = l >= 0 ? end(8, l) : -1, gn = !sounds(9);
    t.keyUp(9); await t.sleep(50);
    await stop();
    ok(e === 11 && gn, "the keys' ends round a jump: a key held as ALGORITHM jumps late in its row: its end on the next row (to " + e +
       "), heard there or where the jump lands, the note not left sounding (" + (gn ? "gone" : "sounding") + ")");
    empty();
    await to(4);
    t.keyDown(9);
    let tk = P.time();
    await t.sleep(2.5 * rms);
    await t.tap("PLAY");
    await rowAt(5);
    let want = wantOf(4, tk);
    t.knob("ALGORITHM", 8);
    await until(() => false, 600);
    l = find(4, m9); e = l >= 0 ? end(4, l) : -1; gn = !sounds(9);
    t.keyUp(9); await t.sleep(50);
    await stop();
    ok(e === want && e > 6 && gn, "... a key held into PLAY, its own time ahead: its end (row " + e + ", " + want + " wanted) on a row the jump passes over: " +
       "heard where it lands (" + (gn ? "gone" : "sounding") + ")");
    empty();
    U.edit(() => { LS().rows = 16; });
    await to(2);
    t.keyDown(9);
    tk = P.time();
    await t.sleep(10 * rms);
    await t.tap("PLAY");
    want = wantOf(2, tk);
    t.keyUp(9);
    await rowAt(10);
    let held = sounds(9);
    await rowAt(0, 0, 1); await rowAt(1, 0, 1);
    gn = !sounds(9);
    l = find(2, m9); e = l >= 0 ? end(2, l) : -1;
    await stop();
    ok(e === want && want > 10 && held && gn, "... 16 rows: a key held into PLAY 10 rows, let go at once: its end (row " + e + ", " + want +
       " wanted) more than half the length ahead: heard at its row, the first pass too (row 10 " + (held ? "sounding" : "silent") + "; after its end " +
       (gn ? "gone" : "sounding") + ")");
    empty();
    /* ... in REAL TIME, let go mid-row: its end a key off at its tick (Kxx), the first pass heard to that tick, not cut at its
     * row's start */
    U.edit(() => { LS().quantize = "REAL TIME"; });
    await to(2);
    t.keyDown(9);
    await t.sleep(10 * rms);
    await t.tap("PLAY");
    t.keyUp(9);
    l = find(2, m9);
    let er = -1, et = -1;
    for (let r = 0; r < 16 && l >= 0 && er < 0; r++) {
        const c = sCell(r, l), k = c && c.cmds.find(x => x.c === 0x14);
        if (k) { er = r; et = k.v; }
    }
    if (er > 0 && et > 0) await rowAt(er, (et - 1) / LS().ticks);
    held = sounds(9);
    if (er > 0) await rowAt(Math.min(er + 2, 15));
    gn = !sounds(9);
    await stop();
    ok(er > 10 && et >= 2 && held && gn, "... REAL TIME, let go mid-row: its end a key off at its tick (row " + er + ", tick " + et + " of " + LS().ticks +
       "), more than half the length ahead: the first pass heard to that tick (" + (held ? "sounding" : "silent") + "), not cut at its row's start, then gone (" +
       (gn ? "gone" : "sounding") + ")");
    U.edit(() => { LS().quantize = "ROW"; });
    empty();
    await to(4);
    t.keyDown(9); await t.sleep(9 * rms);
    await t.tap("PLAY");
    await rowAt(12);
    held = sounds(9);
    await rowAt(0, 0, 1); await rowAt(3, 0, 1);
    gn = !sounds(9);
    t.keyUp(9); await t.sleep(50);
    l = find(4, m9);
    await stop();
    ok(l >= 0 && end(4, l) < 0 && isOff(sCell(0, l)) && held && gn, "... held on to the length's end as they play: its off on row 0 (lane " + l + ", " +
       sNotes() + "), heard there, the first pass's sound not cut before (row 12 " + (held ? "sounding" : "silent") + "; row 3 " + (gn ? "gone" : "sounding") + ")");
    empty();
    await to(2);
    t.keyDown(9);
    tk = P.time();
    await t.sleep(3 * rms);
    t.down("PLAY");
    t.keyUp(9);
    want = wantOf(2, tk);
    await t.sleep(20); t.up("PLAY");
    await rowAt(want - 1);
    held = sounds(9);
    await rowAt(want + 3);
    gn = !sounds(9);
    l = find(2, m9); e = l >= 0 ? end(2, l) : -1;
    await stop();
    ok(e === want && want > 3 && held && gn, "... let go as PLAY starts the loops, before they are heard: its end at its own time (row " + e + ", " + want +
       " wanted), heard to it (" + (held ? "sounding" : "silent") + "), not ringing round the loop (" + (gn ? "gone" : "sounding") + ")");
    empty();
    await to(12);
    sounded.clear();
    t.keyDown(9); await until(() => false, 6 * rms);
    const free9 = sounded.has(9);
    taken.length = 0;
    await t.tap("PLAY"); await t.sleep(300);
    let n1 = count(m9), took = taken.includes(9);
    t.keyUp(9); await t.sleep(100);
    await stop();
    ok(free9 && took && n1 === 1 && count(m9) === 1 && find(12, m9) >= 0, "an insert's key held to the length's end, then PLAY: sounding on freely till then, taken at " +
       "PLAY (its free sound let go: " + took + "), never written again (" + sNotes() + ")");
    empty();
    await to(8);
    t.keyDown(9); await until(() => false, 2 * rms);
    U.setInst(0xF901); t.frame();
    taken.length = 0;
    await t.tap("PLAY"); await t.sleep(300);
    n1 = count(m9); took = taken.includes(9);
    t.keyUp(9); await t.sleep(100);
    await stop();
    U.setInst(0xF900); t.frame();
    l = find(8, m9);
    ok(l >= 0 && end(8, l) === 10 && took && n1 === 1 && count(m9) === 1, "... one stopped by a new instrument, then PLAY: its note as it stood (to row " +
       (l >= 0 ? end(8, l) : -1) + "), taken at PLAY (" + took + "), never written again (" + sNotes() + ")");
    empty();
    await to(4);
    t.keyDown(9); await t.sleep(2 * rms);
    await t.turn("ALGORITHM", 4);
    const moved = row();
    taken.length = 0;
    await t.tap("PLAY"); await t.sleep(300);
    n1 = count(m9); took = taken.includes(9);
    await until(() => false, 500);
    gn = !sounds(9);
    t.keyUp(9); await t.sleep(100);
    await stop();
    l = find(4, m9);
    ok(moved === 8 && l >= 0 && end(4, l) === 6 && took && gn && n1 === 1 && count(m9) === 1, "ALGORITHM with an insert's key held: the play position " +
       "moved (to " + moved + "), the key's note ended as it stood (row 4 to " + (l >= 0 ? end(4, l) : -1) + "), PLAY from there: the key taken (" + took +
       "), never written again, nothing of it heard (" + (gn ? "silent" : "sounding") + "; " + sNotes() + ")");
    empty();
    U.edit(() => { LS().rows = rows0; });
    await to(0);
    U.setInst(inst0);
    A.noteOn = on0; A.noteOff = off0; U.keyOff = kOff0;
    empty();
    U.keyOn = k0;
};
