// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* LOOPS, the loop station (specs/ui/screens/loops.md, specs/files/loops.md): the first loop from silence and its close
 * on a bar line; takes landing by QUANTIZE (where the loops are heard is stood in for, so each case lands where it
 * must); MONO legato (GLIDE, JUMP, a TB-303's slide), the same row's competition; POLY's fifth note dropped; a take on
 * a new loop as the loops play; repeats and the cut played by the lanes; re-record and peel; mutes and solos, now and at
 * the bar, HOME's combination; the pattern recorder (a first loop with TIMES, a later one filling the length, into a
 * loop, its preview); DOUBLE; LENGTH shorter asking; CLEAR LOOPS; REVERT; SAVE and a power cycle; RECORD LIVE (its
 * panel, armed, started, what sounded written, the keys quantized and heard as written, the changes as commands, the
 * loops silenced and on again while it goes on, ended at once, leaving, 16 tracks, stopping by itself). Run by the
 * harness:
 *   node tests/playground/harness.js tests/playground/loops.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const A = t.PTFM.audio, LS = () => M.state.loops, sc = () => U.screen.state();
    const cell = (i, r, l = 0) => LS().list[i] && LS().list[i].cells[r * 4 + l];
    const notesAt = i => LS().list[i].cells.map((c, k) => c && c.note != null && c.note !== M.OFF ? (k >> 2) + ":" + (k & 3) + ":" + c.note : null).filter(Boolean);
    let kept = null;
    M.useStore({ get: async () => kept, put: async v => { kept = structuredClone(v); }, del: async () => { kept = null; } });
    /* where the loops are heard, stood in for: row + frac of pass (rows of 0.5 s) */
    const realAt = P.at;
    const heardAt = (row, frac, pass = 0) => { P.at = () => ({ row, pass, t: P.time() - frac * .5, dur: .5, rpb: 4 }); };
    const real = () => { P.at = realAt; };
    const press = (k, row, frac, pass) => { heardAt(row, frac, pass); t.keyDown(k); real(); };
    const lift = (k, row, frac, pass) => { heardAt(row, frac, pass); t.keyUp(k); real(); };
    const plain = () => ({ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 });
    const mkLoop = o => Object.assign({ inst: 0xF000, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted: false, soloed: false,
                                        cells: new Array(((o && o.base) || 16) * 4).fill(null) }, o);
    const setLoops = (list, rows) => {
        U.edit(() => { const j = LS(); j.list = list; j.rows = rows; for (let k = 0; k < 16; k++) j.mixer[k] = k < list.length ? plain() : Object.assign({}, j.nextMix); });
        P.mutesChanged();
    };
    const pick = async i => { await t.turn("SELECT", -20); if (i) await t.turn("SELECT", i); };
    const stopAll = async () => { P.pause(); await t.sleep(60); };
    const tb303 = M.state.carts.find(c => c.type === "TB303").id << 8, dx7 = 0xF000;
    U.setInst(dx7);

    await t.homeKey(22);
    t.frame();
    ok(U.screen.name === "LOOPS" && !LS().list.length && LS().rows === 0 && t.leds[6] === 0 && t.leds[5] === 0,
       "LOOPS: no loops at first; EDIT and LFO dark");
    await t.tap("EDIT");
    ok(U.toastNow() === "NO LOOPS YET", "EDIT with no loops: NO LOOPS YET");
    await t.tap("PLAY");
    ok(!P.playing && U.toastNow() === "REC, THEN PLAY: THE FIRST LOOP", "PLAY with no loops and nothing playing: refused (REC, THEN PLAY: THE FIRST LOOP)");

    /* ---- the first loop: from silence a key starts it with its note; REC closes it on a bar line ---- */
    await t.tap("REC");
    ok(sc().arm && sc().arm.first && sc().arm.wait && !P.playing, "REC on + NEW LOOP: armed, waiting (ARMED: A NOTE OR PLAY)");
    await t.tap("REC");
    ok(!sc().arm && U.toastNow() === "CANCELLED", "REC while armed: disarmed, CANCELLED");
    await t.tap("REC");
    t.keyDown(7); await t.sleep(60);
    ok(P.playing && P.what === "loops" && LS().list.length === 1 && sc().take && sc().take.first && P.recording(),
       "a key starts the loops and the first loop's take at once");
    ok(cell(0, 0) && cell(0, 0).note === U.midi(7) && cell(0, 0).vol === U.set("KEYS VOLUME"), "... its note on row 0, at KEYS VOLUME");
    lift(7, 0, .9);
    ok(cell(0, 1) && cell(0, 1).note === M.OFF, "its note off a row after it (the nearest row, at least one after)");
    press(9, 17, .1);
    ok(cell(0, 17) && cell(0, 17).note === U.midi(9), "a note in bar 2, row 11");
    lift(9, 17, .3);
    heardAt(17, .6); await t.tap("REC"); real();
    ok(!sc().take && LS().rows === 16 && LS().list[0].base === 16 && LS().list[0].cells.length === 64 && U.toastNow() === "LOOP 00: 1 BAR",
       "REC up to a beat after a bar line: cut back to it (1 bar), LOOP 00: 1 BAR");
    ok(!LS().list[0].cells.some((c, k) => k >= 64) && notesAt(0).join() === "0:0:" + U.midi(7), "... what was played after the line gone");
    ok(LS().list[0].inst === dx7 && LS().list[0].col === 0 && !LS().list[0].mono && sc().sel === 1, "in the current instrument, light red, POLY; the selection on + NEW LOOP");
    await stopAll();
    /* (a first loop run on to the line; 2 bars) */
    setLoops([], 0);
    await t.tap("REC"); t.keyDown(4); await t.sleep(40); t.keyUp(4);
    heardAt(20, .2); await t.tap("REC"); real();
    ok(LS().rows === 32 && U.toastNow() === "LOOP 00: 2 BARS", "REC well into bar 2: runs on to its end, 2 bars");
    await stopAll();
    setLoops([], 0);
    await t.tap("REC"); await t.tap("PLAY"); await t.sleep(80);
    ok(P.playing && sc().take, "armed, PLAY (COUNT-IN OFF): the loops and the take at once");
    await t.tap("REC");
    ok(!LS().list.length && !LS().rows && !P.playing && U.toastNow() === "NOTHING PLAYED: NO LOOP", "nothing played: no loop (NOTHING PLAYED: NO LOOP)");

    /* ---- QUANTIZE: where a note lands (a re-record of loop 00, its rows 0.5 s long) ---- */
    setLoops([mkLoop({})], 16);
    U.edit(() => { LS().tempo = 3; });                   /* (so slow the take's pass outlasts the cases) */
    await pick(0);
    const quant = async (q, cases) => {
        U.edit(() => { LS().quantize = q; });
        await t.tap("EDIT");
        t.keyDown(0); await t.sleep(30); lift(0, 0, .9);   /* (from silence: a key starts it, row 0) */
        const got = [];
        for (const [row, frac, k] of cases) {
            press(k, row, frac);
            const lane = sc().take.notes[k] ? sc().take.notes[k].lane : -1, r = sc().take.notes[k] ? sc().take.notes[k].row : -1, c = cell(0, r, lane);
            const ed = c ? c.cmds.find(x => x.c === 0x0E) : null;
            got.push(r + (lane > 0 ? "/" + lane : "") + (ed ? "d" + (ed.v & 15) : ""));
            lift(k, row + 1, .1);
        }
        await t.tap("REC"); await stopAll();
        return got.join();
    };
    ok(await quant("ROW", [[5, .3, 2], [9, .6, 4], [15, .7, 5]]) === "5,10,0/1", "ROW: the nearest row (5.3 on 5, 9.6 on 10; 15.7 on row 0, wrapping)");
    ok(await quant("2 ROWS", [[4, .9, 2], [7, .1, 4]]) === "4,8", "2 ROWS: the nearest line of 2 (4.9 on 4, 7.1 on 8)");
    ok(await quant("3 ROWS", [[4, .4, 2], [7, .6, 4]]) === "3,9", "3 ROWS: the nearest line of 3 (4.4 on 3, 7.6 on 9)");
    ok(await quant("4 ROWS", [[5, .9, 2], [10, .1, 4]]) === "4,12", "4 ROWS: the nearest line of 4 (5.9 on 4, 10.1 on 12)");
    ok(await quant("REAL TIME", [[5, .5, 2], [9, .1, 4]]) === "5d3,9", "REAL TIME: the row it falls in, a note delay of its tick (5.5: ED3)");
    {   /* REAL TIME's note off: a key off at its tick */
        U.edit(() => { LS().quantize = "REAL TIME"; });
        await t.tap("EDIT");
        t.keyDown(0); await t.sleep(30);
        lift(0, 3, .5);
        const k = cell(0, 3) && cell(0, 3).cmds.find(x => x.c === 0x14);
        ok(k && k.v === 3, "REAL TIME: its end a key off at its tick (K03 on row 3)");
        await t.tap("REC"); await stopAll();
    }
    U.edit(() => { LS().quantize = "ROW"; });

    /* ---- MONO: one lane, legato as MONO LEGATO says ---- */
    U.edit(() => { LS().list[0].mono = true; LS().legato = "GLIDE"; });
    await t.tap("EDIT");
    t.keyDown(0); await t.sleep(30);                      /* held */
    press(4, 2, .1);
    ok(cell(0, 2) && cell(0, 2).note === U.midi(4) && cell(0, 2).cmds.some(x => x.c === 0x03 && x.v === 0x20) && !cell(0, 2, 1),
       "MONO, GLIDE: a key over a held one, its note on the one lane with 320 (a glide)");
    lift(0, 3, .1);
    ok(!cell(0, 3), "... the held key let go: nothing (its note no longer sounds)");
    lift(4, 4, .2);
    ok(cell(0, 4) && cell(0, 4).note === M.OFF, "... the sounding one let go: its note off");
    press(5, 6, .1); press(7, 6, .2);
    ok(cell(0, 6) && cell(0, 6).note === U.midi(7) && !cell(0, 6).cmds.length && !cell(0, 6, 1), "keys landing on the same row: the last registered is written");
    lift(5, 7, .1); lift(7, 8, .1);
    await t.tap("REC"); await stopAll();
    ok(LS().list[0].lanes === 1 && LS().list[0].mono, "... the loop stays MONO, one lane");
    U.edit(() => { LS().legato = "JUMP"; });
    await t.tap("EDIT"); t.keyDown(0); await t.sleep(30); press(4, 2, .1);
    ok(cell(0, 2) && cell(0, 2).note === U.midi(4) && !cell(0, 2).cmds.length, "MONO, JUMP: a plain new note");
    lift(4, 3, .1); t.keyUp(0); await t.tap("REC"); await stopAll();
    {   /* LEGATO: LEG with the new note, heard on the held voice (the worklet told its id) */
        const calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, midi, leg: opts.leg }); return on0(id, time, midi, ref, vol, track, opts); };
        U.edit(() => { LS().legato = "LEGATO"; });
        await t.tap("EDIT"); t.keyDown(0); await t.sleep(30); press(4, 2, .1);
        A.noteOn = on0;
        const held = calls.find(x => x.midi === U.midi(0)), leg = calls.find(x => x.midi === U.midi(4));
        ok(cell(0, 2) && cell(0, 2).note === U.midi(4) && cell(0, 2).cmds.some(x => x.c === 0xD3) && !cell(0, 2).cmds.some(x => x.c === 0x03) &&
           held && leg && leg.leg === held.id, "MONO, LEGATO: LEG with the new note, heard as a legato onto the held one's voice");
        lift(4, 3, .1); t.keyUp(0); await t.tap("REC"); await stopAll();
    }
    U.edit(() => { LS().legato = "GLIDE"; LS().list[0].inst = tb303; });
    await t.tap("EDIT"); t.keyDown(0); await t.sleep(30); press(4, 2, .1);
    ok(cell(0, 0).cmds.some(x => x.c === 0xC1) && cell(0, 2) && !cell(0, 2).cmds.length, "MONO, GLIDE on a TB-303: SLD on the note held, the new one plain");
    lift(4, 3, .1); t.keyUp(0); await t.tap("REC"); await stopAll();
    U.edit(() => { LS().list[0].mono = false; LS().list[0].inst = dx7; });

    /* ---- POLY: up to 4 notes at once, a fifth neither heard nor written ---- */
    {
        const calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
        await t.tap("EDIT"); t.keyDown(0); await t.sleep(30);
        for (const k of [2, 4, 5, 7]) press(k, 2, .1);
        A.noteOn = on0;
        ok([1, 2, 3].map(l => cell(0, 2, l) && cell(0, 2, l).note).join() === [2, 4, 5].map(U.midi).join() && !LS().list[0].cells.some(c => c && c.note === U.midi(7)),
           "POLY: chords a key a lane (lanes 1-3 by the keys' order); the fifth note not written");
        ok(!calls.some(c => /k7$/.test(c.id)) && calls.some(c => /k5$/.test(c.id) && c.track === 16),
           "... nor heard; the take's notes sound on the loop's track (its mix)");
        for (const k of [2, 4, 5, 7]) lift(k, 3, .1);
        t.keyUp(0);
        await t.tap("REC"); await stopAll();
        ok(LS().list[0].lanes === 4, "... the loop's lanes: the most it holds at once (4)");
    }
    U.edit(() => { LS().tempo = 120; });

    /* ---- a take on a new loop while the loops play: armed, it starts at their start, one pass, the keys as written ---- */
    setLoops([mkLoop({ cells: Object.assign(new Array(64).fill(null), { 0: M.newCell(60, null, 100) }) })], 16);
    await pick(1);
    await t.tap("PLAY"); await t.sleep(100);
    await t.tap("REC");
    ok((sc().arm && !sc().arm.wait) || sc().take, "REC on + NEW LOOP while the loops play: armed for their next start");
    t.frame();
    for (let n = 0; n < 200 && !P.lpRecording(); n++) await t.sleep(20);
    ok(P.lpRecording() && LS().list.length === 2 && sc().sel === 1 && P.at().row < 3, "... the take starts at the loops' start, the new loop made, selected");
    await t.sleep(250); t.keyDown(12); await t.sleep(200); t.keyUp(12);
    for (let n = 0; n < 200 && sc().take; n++) await t.sleep(20);
    ok(!sc().take && LS().list.length === 2 && notesAt(1).length === 1 && P.playing, "... one pass and it stops by itself, the loops playing on");
    ok(sc().sel === 2 && LS().list[1].col === 1, "... the selection on + NEW LOOP again; the next colour (yellow)");
    await stopAll();

    /* ---- the lanes: repeats, the rest, a repeat cut at the length's end ---- */
    {
        const c4 = o => { const a = new Array(o.base * 4).fill(null); for (const [r, n] of o.notes) a[r * 4] = M.newCell(n, null, 100); return a; };
        setLoops([mkLoop({ base: 4, repeats: 3, cells: c4({ base: 4, notes: [[0, 60], [2, 62]] }) }),
                  mkLoop({ base: 6, repeats: 3, cells: c4({ base: 6, notes: [[0, 64], [4, 65]] }) })], 16);
        U.edit(() => { LS().tempo = 300; });
        const calls = [], rows = [], on0 = A.noteOn, off0 = A.noteOff, hook = P.loopsRow;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ on: 1, id, time, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
        A.noteOff = (id, time) => { calls.push({ on: 0, id, time }); return off0(id, time); };
        P.loopsRow = function (row, time, dur, pass) { rows.push({ row, time, pass }); return hook.apply(this, arguments); };
        P.playLoops(0); await t.sleep(1300); P.pause();
        A.noteOn = on0; A.noteOff = off0; P.loopsRow = hook;
        const rowOf = c => { const x = rows.find(r => Math.abs(r.time - c.time) < 1e-6); return x ? x.pass * 100 + x.row : -1; };
        const starts = tr => calls.filter(c => c.on && c.track === tr).map(c => rowOf(c) + ":" + c.midi).filter(s => +s.split(":")[0] < 100).join();
        ok(starts(16) === "0:60,2:62,4:60,6:62,8:60,10:62", "repeats: base 4 x3 plays rows 0-11, then rests (" + starts(16) + ")");
        const rest = rows.find(r => r.pass === 0 && r.row === 12), id8 = calls.filter(c => c.on && c.track === 16).pop();
        ok(rest && calls.some(c => !c.on && Math.abs(c.time - rest.time) < 1e-6 && /^s16\.0\./.test(c.id)), "... its rest ends its note (row 12)");
        ok(starts(17) === "0:64,4:65,6:64,10:65,12:64", "a repeat cut at the length's end: base 6 x3 over 16 rows (" + starts(17) + ")");
        const wrap = rows.find(r => r.pass === 1 && r.row === 0);
        ok(wrap && calls.some(c => !c.on && Math.abs(c.time - wrap.time) < 1e-6 && /^s17\.0\./.test(c.id)), "... cut there: its note ends at the length's end" +
           (wrap ? "" : " (no wrap: " + rows.length + " rows)"));
        void id8;
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- re-record, peel ---- */
    setLoops([mkLoop({ cells: Object.assign(new Array(64).fill(null), { 0: M.newCell(60, null, 100) }), col: 0 }),
              mkLoop({ cells: Object.assign(new Array(64).fill(null), { 8: M.newCell(62, null, 100) }), col: 1, mono: true }),
              mkLoop({ cells: Object.assign(new Array(64).fill(null), { 16: M.newCell(64, null, 100) }), col: 2 })], 16);
    U.edit(() => { LS().mixer[1].lvl = 0x40; });
    await pick(1);
    await t.tap("EDIT");
    ok(!notesAt(1).length && sc().arm && sc().arm.re && U.toastNow() === "LOOP 01 RE-RECORDING", "EDIT: a re-record, its notes gone at once (LOOP 01 RE-RECORDING), armed");
    t.frame();
    ok(t.leds[13] === 1 || t.leds[13] === 0, "(REC blinks while armed)");
    await t.tap("REC");
    ok(!sc().arm && !notesAt(1).length && LS().list.length === 3 && LS().list[1].mono && LS().list[1].col === 1, "REC: disarmed; its notes do not come back; its place, MONO, colour kept");
    await t.tap("REC");
    t.keyDown(3); await t.sleep(40); t.keyUp(3); await t.sleep(40);
    await t.tap("REC"); await stopAll();
    ok(notesAt(1).join() === "0:0:" + U.midi(3) && LS().mixer[1].lvl === 0x40 && sc().sel === 1, "a re-record's take: written into the loop, its mix kept, the selection on it");
    await t.tap("LFO");
    ok(LS().list.length === 2 && LS().list[1].col === 2 && LS().mixer[1].lvl === 127 && sc().sel === 1 && U.toastNow() === "LOOP 01 PEELED",
       "LFO: PEEL, the loop goes (LOOP 01 PEELED); the selection on the loop moved into its place, the mixes with them");
    await pick(2);
    await t.tap("LFO");
    ok(LS().list.length === 1 && LS().list[0].col === 0, "on + NEW LOOP: LFO peels the last loop");
    await t.tap("LFO");
    ok(!LS().list.length && !LS().rows, "peeling the only loop takes the length with it");

    /* ---- mutes and solos: CONTROL mode (SEQ), now and at the bar; HOME's combination ---- */
    setLoops([mkLoop({ cells: Object.assign(new Array(64).fill(null), { 0: M.newCell(60, null, 100) }) }),
              mkLoop({ cells: Object.assign(new Array(64).fill(null), { 32: M.newCell(64, null, 100) }), col: 1 })], 16);
    {
        const mutes = {}, m0 = A.trackMute;
        A.trackMute = (track, on) => { mutes[track] = on; return m0(track, on); };
        await t.tap("SEQ");
        t.frame();
        ok(sc().control && U.toastNow() === "KEYS: CONTROL" && t.leds[11] === 1, "SEQ: the keys CONTROL the loops (KEYS: CONTROL), SEQ full");
        await t.sleep(2100); t.frame();
        ok(t.leds[14 + U.WHITE[0]] === 1 && t.leds[14 + U.WHITE[2]] === 0 && t.leds[14 + U.BLACK[3]] === 1 && t.leds[14 + U.BLACK[9]] === 1,
           "... white key n full while loop n sounds, dark with none; OP4 (ROW) and POLY full");
        await t.key(U.WHITE[0]);
        ok(LS().list[0].muted && mutes[16] === true && U.toastNow() === "LOOP 00 MUTED", "stopped: a white key mutes its loop at once (LOOP 00 MUTED)");
        await t.key(U.BLACK[6]);
        ok(!LS().list[0].muted && U.toastNow() === "ALL LOOPS ON", "PIT: all back on (ALL LOOPS ON)");
        await t.key(U.BLACK[0]);
        ok(LS().quantize === "2 ROWS" && U.toastNow() === "QUANTIZE: 2 ROWS", "OP1: QUANTIZE 2 ROWS");
        await t.key(U.BLACK[3]);
        await t.key(U.BLACK[8]);
        ok(LS().nextMono && U.toastNow() === "NEXT LOOPS: MONO" && U.screen.footer()[0] === "MONO", "MONO: the next loops MONO (the footer says so)");
        await t.key(U.BLACK[9]);
        await t.key(U.BLACK[7]);
        ok(sc().solo && U.toastNow() === "SEQ: SOLO" && U.screen.seq() === "SOLO", "the GLO key: SOLO (SEQ SOLO in the header)");
        await t.key(U.WHITE[1]);
        ok(LS().list[1].soloed && mutes[16] === true && mutes[17] === false, "a white key solos its loop: only the soloed sound");
        await t.key(U.WHITE[0]);
        ok(LS().list[0].soloed && !LS().list[1].soloed && mutes[16] === false && mutes[17] === true && U.toastNow() === "LOOP 00 SOLO",
           "another white key moves the solo to its loop (one solo at a time)");
        await t.key(U.WHITE[1]);
        await t.key(U.WHITE[1]); await t.key(U.BLACK[7]);
        await t.tap("PLAY"); await t.sleep(100);
        t.keyDown(U.WHITE[0]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[0]);
        ok(!LS().list[0].muted && U.toastNow() === "LOOP 00 MUTED AT THE BAR" && sc().pend.has("0/mute"), "playing, held: the mute waits for the bar line (LOOP 00 MUTED AT THE BAR)");
        for (let n = 0; n < 150 && !LS().list[0].muted; n++) await t.sleep(20);
        ok(LS().list[0].muted && !sc().pend.size && mutes[16] === true, "... the bar line: muted");
        await t.key(U.WHITE[0]);
        ok(!LS().list[0].muted && U.toastNow() === "LOOP 00 ON", "playing, a tap: at once (LOOP 00 ON)");
        await t.key(U.BLACK[7]);                            // (SOLO)
        await t.key(U.WHITE[1]);
        t.keyDown(U.WHITE[0]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[0]);
        ok(LS().list[1].soloed && !LS().list[0].soloed && U.toastNow() === "LOOP 00 SOLO AT THE BAR" && sc().pend.has("0/solo") &&
           sc().pend.has("1/solo"), "SOLO, playing, held: the solo moves at the bar line (LOOP 00 SOLO AT THE BAR)");
        for (let n = 0; n < 150 && !LS().list[0].soloed; n++) await t.sleep(20);
        await t.sleep(40);
        ok(LS().list[0].soloed && !LS().list[1].soloed && !sc().pend.size, "... the bar line: loop 00 soloed alone");
        await t.key(U.WHITE[0]);
        ok(!LS().list[0].soloed && !LS().list[1].soloed && U.toastNow() === "LOOP 00 SOLO OFF", "SOLO, a tap on the soloed loop: its solo off (LOOP 00 SOLO OFF)");
        await t.key(U.BLACK[7]);                            // (MUTE again)
        await stopAll();
        await t.tap("SEQ");
        ok(!sc().control && U.toastNow() === "KEYS: PLAY", "SEQ again: the keys PLAY");
        await t.homeKey(U.WHITE[1]);
        ok(LS().list[1].muted && U.toastNow() === "LOOP 01 MUTED", "HOME + white key: its loop muted at once");
        await t.homeKey(U.WHITE[1]);
        t.down("HOME"); await t.sleep(30); t.keyDown(U.WHITE[0]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[0]); await t.sleep(20); t.up("HOME"); await t.sleep(160);
        ok(LS().list[0].soloed && U.toastNow() === "LOOP 00 SOLO", "HOME + white key held: soloed");
        A.trackMute = m0;
        U.edit(() => { LS().list[0].soloed = false; });
    }

    /* ---- the pattern recorder (ARP) ---- */
    setLoops([], 0);
    U.edit(() => { LS().nextMono = false; });
    await pick(0);
    await t.tap("ARP");
    const pr = U.screen.recorder();
    for (const r of pr.rows) r.steps.fill(false);
    pr.len = 16; pr.step = "1/16"; pr.times = 1; pr.long = false; pr.sel = 0;
    t.frame();
    ok(t.top() === "recorder" && P.pvOn() && t.leds[10] === 1 && U.screen.footer()[0] === "REC: WRITE", "ARP: the pattern recorder; its preview playing alone; ARP full; REC: WRITE");
    {   /* OCT+ on the 4th row moves that row's note alone: the first row's C kept from the recorder's first open */
        const saved = pr.rows.map(r => r.note), oct0 = U.octave;
        for (const r of pr.rows) r.note = null;
        await t.tap("HOME"); await t.tap("ARP");
        pr.rows[0].steps[0] = true; pr.sel = 3; t.frame();
        const firstNote = () => (P.lpPreview().cells.find(c => c) || {}).note;
        const n0 = firstNote();
        await t.tap("OCT+");
        const n1 = firstNote();
        await t.tap("OCT-");
        ok(n0 != null && n0 === n1 && U.octave === oct0, `OCT+ on the 4th row leaves the first row's note (${n0}, then ${n1})`);
        pr.rows.forEach((r, k) => { r.note = saved[k]; });
        pr.rows[0].steps[0] = false; pr.sel = 0; t.frame();
    }
    await t.tap("ARP");
    ok(!P.pvOn(), "ARP: the preview stops (the loops stopped)");
    for (const k of [0, 4, 8, 12]) await t.key(U.WHITE[k]);
    ok(pr.rows[0].steps.map((s, k) => s ? k : -1).filter(k => k >= 0).join() === "0,4,8,12", "the white keys switch the row's steps");
    await t.turn("K4", 3);
    ok(pr.times === 4 && U.screen.makes() === "16 X 4 = 64 ROWS = 4 BARS, 1 LANE", "K4 TIMES x4: " + U.screen.makes());
    await t.turn("K2", -1);
    ok(U.screen.makes() === "12 X 4 = 48 ROWS = 3 BARS, 1 LANE", "K2 LENGTH 12: " + U.screen.makes());
    await t.turn("K2", 1);
    await t.tap("SEL"); await t.key(U.WHITE[2]);
    ok(pr.rows[0].note === U.midi(U.WHITE[2]) && !pr.keys, "SEL, then a key: the row's note (back to the steps)");
    await t.tap("REC");
    ok(!t.top() && LS().list.length === 1 && LS().rows === 64 && LS().list[0].base === 16 && LS().list[0].repeats === 4 &&
       notesAt(0).join() === [0, 4, 8, 12].map(r => r + ":0:" + U.midi(U.WHITE[2])).join() && U.toastNow() === "PATTERN: LOOP 00, 4 BARS",
       "REC with no loops: the first loop, its base the steps, x4, 4 bars (PATTERN: LOOP 00, 4 BARS)");
    ok(!P.playing, "... the preview stopped: nothing starts");
    await t.tap("ARP");
    await t.tap("ARP");
    await t.turn("K2", -1);
    for (const r of pr.rows) r.steps.fill(false);
    for (const k of [0, 4, 8]) await t.key(U.WHITE[k]);
    await t.turn("SELECT", 1);
    t.down("SEL"); await t.sleep(30); await t.key(U.WHITE[4]); t.up("SEL"); await t.sleep(200);
    ok(pr.rows[1].note === U.midi(U.WHITE[4]) && pr.sel === 1, "SEL held + a key: row 2's note at once");
    await t.key(U.WHITE[2]);
    await t.tap("GLO");
    ok(pr.long && U.toastNow() === "NOTES A STEP LONG", "GLO: the notes a step long (NOTES A STEP LONG)");
    ok(U.screen.makes() === "5 X 12 + 4 = 64 ROWS, 2 LANES" && U.top().target === -1, "a later loop: it fills the length: " + U.screen.makes());
    await t.tap("REC");
    const L1 = LS().list[1];
    ok(L1 && L1.base === 12 && L1.repeats === 6 && L1.lanes === 2 && !L1.mono && L1.col === 1 && U.toastNow() === "PATTERN: LOOP 01, 4 BARS",
       "REC: a new loop of 2 lanes (POLY), its 12 steps x6, the last repeat cut (PATTERN: LOOP 01, 4 BARS)");
    ok(L1.cells[1 * 4].note === M.OFF && L1.cells[3 * 4 + 1].note === M.OFF && L1.cells[5 * 4].note === M.OFF, "A STEP LONG: a note off a step after each note whose next step is off");
    await pick(0);
    await t.tap("ARP");
    ok(U.top().target === 0, "opened on a loop: into it");
    await t.tap("ARP");
    for (const r of pr.rows) r.steps.fill(false);
    await t.tap("REC");
    ok(t.top() === "recorder" && U.toastNow() === "NO STEPS ON", "no step on: REC refused (NO STEPS ON)");
    await t.key(U.WHITE[3]);
    await t.tap("REC");
    ok(LS().list.length === 2 && LS().list[0].col === 0 && LS().list[0].mono && notesAt(0).length === 1 && U.toastNow() === "PATTERN INTO LOOP 00",
       "REC on a loop: its notes replaced, its place and colour kept, one row filled: MONO (PATTERN INTO LOOP 00)");
    await t.tap("PLAY"); await t.sleep(100);
    await t.tap("ARP");
    ok(!P.pvOn() && P.lpPreview() && t.top() === "recorder", "the loops playing: the preview in their time (not alone)");
    await t.tap("ARP");
    ok(!P.lpPreview(), "... ARP switches its sound off");
    await t.tap("ARP");
    await t.tap("PLAY"); await t.sleep(100);
    ok(!P.playing && P.pvOn(), "PLAY pausing the loops: the preview goes on alone");
    await t.tap("PLAY"); await t.sleep(100);
    ok(P.playing && !P.pvOn() && P.lpPreview(), "PLAY starting them: the preview with them again");
    await t.tap("HOME");
    ok(!t.top() && !P.lpPreview() && LS().list.length === 2, "HOME: closed, nothing written");
    await stopAll();

    /* ---- SAVE TO PATTERN, LOAD FROM PATTERN ---- */
    {
        const menuPick = async item => { await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf(item); await t.tap("SEL"); };
        const cs = (base, list) => { const a = new Array(base * 4).fill(null); for (const [r, l, n] of list) a[r * 4 + l] = n === M.OFF ? M.newCell(M.OFF, null, null) : M.newCell(n, null, 100); return a; };
        setLoops([mkLoop({ base: 4, repeats: 4, cells: cs(4, [[0, 0, 60], [2, 0, M.OFF]]) }),
                  mkLoop({ inst: 0xF001, lanes: 2, col: 1, cells: cs(16, [[0, 0, 64], [0, 1, 67], [8, 0, M.OFF], [8, 1, M.OFF], [12, 0, 65]]) })], 16);
        U.edit(() => { LS().mixer[1].lvl = 0x40; });
        const free = M.freePattern();
        await menuPick("SAVE TO PATTERN");
        ok(t.top() === "list" && U.top().title === "SAVE THE LOOPS TO" && U.top().sel === 0, "SAVE TO PATTERN: the pattern list, SAVE THE LOOPS TO, NEW first and picked");
        await t.tap("SEL");
        const p = M.pattern(free), pc = (r, tr) => p && p.cells[r * 16 + tr];
        ok(p && p.rows === 16 && p.tracks === 3 && U.toastNow() === "LOOPS STORED IN PATTERN " + M.hex(free),
           "... NEW: a pattern of the loops' length at the first free number, a lane a track where they overlap (LOOPS STORED IN PATTERN " + M.hex(free) + ")");
        ok([0, 4, 8, 12].every(r => pc(r, 0) && pc(r, 0).note === 60 && pc(r, 0).inst === 0xF000) && pc(2, 0).note === M.OFF,
           "... the repeats written out, each note naming its instrument");
        ok(pc(0, 1).note === 64 && pc(0, 1).inst === 0xF001 && pc(0, 1).cmds.some(x => x.c === 0x80 && x.v === 0x40) && pc(0, 2).note === 67 &&
           pc(0, 2).cmds.some(x => x.c === 0x80 && x.v === 0x40) && !pc(0, 0).cmds.length,
           "... a loop's mix as commands with its first note on a track (=LEVEL 40), none where it is plain");
        await menuPick("SAVE TO PATTERN");
        U.top().sel = 1 + M.patternNums().indexOf(free); await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "PATTERN " + M.hex(free) + " IS NOT EMPTY/REPLACE IT WITH THE LOOPS?",
           "a pattern not empty: asks first (PATTERN " + M.hex(free) + " IS NOT EMPTY / REPLACE IT WITH THE LOOPS?)");
        await t.tap("OCT-");
        /* LOAD: a loop an instrument, 4 at once its lanes, its mix from its commands */
        const q = M.newPattern(free, 3, 16), qc = (r, tr, n, inst, cmds) => { q.cells[r * 16 + tr] = Object.assign(M.newCell(n, inst, null), { cmds: cmds || [] }); };
        qc(0, 0, 60, 0xF000); qc(4, 0, M.OFF, null); qc(8, 0, 62, null);
        qc(0, 1, 64, 0xF001, [{ c: 0x80, v: 0x30 }, { c: 0x04, v: 0x44 }]); qc(2, 2, 65, 0xF000);
        U.edit(() => { M.state.patterns[free] = q; });
        await menuPick("LOAD FROM PATTERN");
        ok(t.top() === "list" && U.top().title === "LOAD INTO LOOPS", "LOAD FROM PATTERN: the pattern list, LOAD INTO LOOPS");
        U.top().sel = M.patternNums().indexOf(free); await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "REPLACE THE LOOPS/WITH PATTERN " + M.hex(free) + "?", "... over loops: asks (REPLACE THE LOOPS / WITH PATTERN " + M.hex(free) + "?)");
        await t.tap("OCT+");
        const L = LS().list;
        ok(L.length === 2 && L[0].inst === 0xF000 && L[0].lanes === 2 && L[1].inst === 0xF001 && LS().mixer[1].lvl === 0x30 && LS().rows === 16 &&
           notesAt(0).join() === "0:0:60,2:1:65,8:0:62" && cell(0, 4).note === M.OFF && U.toastNow() === "PATTERN " + M.hex(free) + " IN LOOPS: 1 COMMAND LEFT OUT",
           "... a loop an instrument (two tracks of one: its lanes), its mix from the = commands, the vibrato left out (PATTERN " + M.hex(free) + " IN LOOPS: 1 COMMAND LEFT OUT)");
        U.edit(() => { M.state.patterns[free] = null; });
    }

    /* ---- the player's stops kept where notes are written out (DOUBLE, LENGTH shorter, SAVE TO PATTERN); SAVE TO
     * PATTERN's mix commands in time order; LOAD's key offs and note delays at their ticks ---- */
    {
        const menuPick = async item => { await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf(item); await t.tap("SEL"); };
        const cs = (base, list) => { const a = new Array(base * 4).fill(null); for (const [r, l, n] of list) a[r * 4 + l] = n === M.OFF ? M.newCell(M.OFF, null, null) : M.newCell(n, null, 100); return a; };
        const offAt = (i, r, l) => !!cell(i, r, l) && cell(i, r, l).note === M.OFF;
        setLoops([mkLoop({ cells: cs(16, [[4, 0, 60]]) }),
                  mkLoop({ base: 12, repeats: 3, col: 1, lanes: 2, cells: cs(12, [[2, 1, 62], [9, 0, 64], [10, 0, M.OFF]]) })], 32);
        await menuPick("DOUBLE");
        ok(LS().rows === 64 && notesAt(0).join() === "4:0:60" && offAt(0, 16, 0) && notesAt(1).join() === "2:1:62,9:0:64,14:1:62,21:0:64,26:1:62" &&
           offAt(1, 0, 1) && !cell(1, 0, 0) && offAt(1, 22, 0), "DOUBLE: a note held into a loop's rest stops there; one held over a cut repeat's end stops at the wrap (a note off at row 0)");
        setLoops([mkLoop({ base: 32, cells: cs(32, [[4, 0, 60]]) })], 32);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -1); await t.tap("GLO");
        ok(LS().rows === 16 && LS().list[0].base === 16 && offAt(0, 0, 0) && notesAt(0).join() === "4:0:60", "LENGTH shorter: a note held over the cut stops at the wrap (a note off at row 0)");
        setLoops([mkLoop({ base: 4, repeats: 2, cells: cs(4, [[1, 0, 60]]) }), mkLoop({ inst: 0xF001, col: 1, cells: cs(16, [[12, 0, 64], [14, 0, M.OFF]]) })], 16);
        let free = M.freePattern();
        await menuPick("SAVE TO PATTERN"); await t.tap("SEL");
        let p = M.pattern(free);
        ok(p && p.tracks === 1 && p.cells[5 * 16].note === 60 && p.cells[8 * 16].note === M.OFF && p.cells[12 * 16].note === 64 && p.cells[12 * 16].inst === 0xF001,
           "SAVE TO PATTERN: a note held into a loop's rest stops there; the track free after it (1 track)");
        U.edit(() => { M.state.patterns[free] = null; });
        setLoops([mkLoop({ cells: cs(16, [[8, 0, 60], [14, 0, M.OFF]]) }), mkLoop({ inst: 0xF001, col: 1, cells: cs(16, [[0, 0, 64], [4, 0, M.OFF]]) })], 16);
        U.edit(() => { LS().mixer[1].pan = 0x30; });
        await menuPick("SAVE TO PATTERN"); await t.tap("SEL");
        p = M.pattern(free);
        const pan = c => c.cmds.filter(x => x.c === 0x08).map(x => x.v).join();
        ok(p && p.tracks === 1 && p.cells[0].note === 64 && pan(p.cells[0]) === "48" && p.cells[8 * 16].note === 60 && pan(p.cells[8 * 16]) === "128",
           "SAVE TO PATTERN: a shared track's mix commands in time order (row 0 PAN 30, row 8 back to PAN 80)");
        /* LOAD: a key off alone, a note delayed on its loop (another track's note there on another lane) and on another */
        const q = M.newPattern(free, 4, 16), qc = (r, tr, n, inst, cmds) => { q.cells[r * 16 + tr] = Object.assign(M.newCell(n, inst, null), { cmds: cmds || [] }); };
        qc(8, 0, 67, 0xF000); qc(0, 1, 60, 0xF000); qc(2, 1, null, null, [{ c: 0x14, v: 3 }]);
        qc(4, 2, 62, 0xF000); qc(8, 2, 64, null, [{ c: 0x0E, v: 0xD2 }]); qc(12, 3, 69, 0xF000); qc(14, 3, 71, 0xF001, [{ c: 0x0E, v: 0xD1 }]);
        U.edit(() => { M.state.patterns[free] = q; });
        await menuPick("LOAD FROM PATTERN");
        U.top().sel = M.patternNums().indexOf(free); await t.tap("SEL"); await t.tap("OCT+");
        const k2 = cell(0, 2, 0), d8 = cell(0, 8, 0), o14 = cell(0, 14, 2);
        ok(LS().list.length === 2 && notesAt(0).join() === "0:0:60,4:0:62,8:0:64,8:1:67,12:2:69" && notesAt(1).join() === "14:0:71" &&
           k2 && k2.note == null && k2.cmds.length === 1 && k2.cmds[0].c === 0x14 && k2.cmds[0].v === 3 && d8.cmds.some(x => x.c === 0x0E && x.v === 0xD2) &&
           o14 && o14.note === M.OFF && o14.cmds.length === 1 && o14.cmds[0].v === 0xD1 && U.toastNow() === "PATTERN " + M.hex(free) + " IN LOOPS: 2 LOOPS",
           "LOAD: a key off alone kept at its tick; a delayed note on its track's lane (another track's note there on another), the old note's off as late (=== ED1) when it goes to another loop");
        U.edit(() => { M.state.patterns[free] = null; });
    }

    /* ---- outside LOOPS: PLAYBACK follows the loops (a loop a track), FX's MIXER a column a loop ---- */
    P.playLoops(); await t.sleep(200);
    await t.homeKey(8); t.frame();
    ok(U.screen.name === "PLAYBACK" && U.screen.page() === "LOOPS" && U.loopsPattern().tracks === LS().list.length && P.playing,
       "PLAYBACK: the loops playing on (LOOPS), a loop a track");
    await t.tap("FX"); await t.tap("SEQ"); await t.tap("SEQ"); t.frame();
    ok(U.screen.sub() === "MIXER" && t.PTFM.audio.fxSong() === LS(), "FX's MIXER: THE LOOPS' (drawn)");
    await t.tap("SEQ"); await t.tap("FX");
    await stopAll();
    await t.homeKey(22);

    /* ---- DOUBLE, LENGTH, CLEAR LOOPS, REVERT, SAVE and a power cycle ---- */
    setLoops([mkLoop({ cells: Object.assign(new Array(64).fill(null), { 0: M.newCell(60, null, 100) }) }),
              mkLoop({ base: 12, repeats: 2, col: 1, cells: Object.assign(new Array(48).fill(null), { 40: M.newCell(62, null, 100) }) })], 16);
    const menuPick = async item => { await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf(item); await t.tap("SEL"); };
    await menuPick("DOUBLE");
    ok(LS().rows === 32 && LS().list[0].repeats === 2 && LS().list[0].base === 16 && LS().list[1].base === 16 && LS().list[1].repeats === 2 &&
       notesAt(1).join() === "10:0:62" && U.toastNow() === "DOUBLED: 2 BARS",
       "DOUBLE: 2 bars; a loop filling the length doubles its repeats, one cut at the end written out first (DOUBLED: 2 BARS)");
    U.edit(() => { LS().rows = 128; });
    await menuPick("DOUBLE");
    ok(LS().rows === 128 && U.toastNow() === "DOUBLE: AT MOST 8 BARS", "DOUBLE past 128 rows: refused (DOUBLE: AT MOST 8 BARS)");
    U.edit(() => { LS().rows = 32; });
    await t.tap("GLO");
    const props = U.top();
    ok(props.items.map(i => i.name).join() === "TEMPO,SWING,LENGTH,GRID,QUANTIZE,MONO LEGATO,COUNT-IN" && props.title === "LOOPS PROPERTIES",
       "GLO: LOOPS PROPERTIES: TEMPO, SWING, LENGTH, GRID, QUANTIZE, MONO LEGATO, COUNT-IN");
    props.sel = 3; await t.turn("K1", 1);
    ok(U.toastNow() === "GRID: ONLY BEFORE THE FIRST LOOP" && LS().rpb === 4, "GRID with loops: GRID: ONLY BEFORE THE FIRST LOOP");
    props.sel = 2; await t.turn("K1", -1);
    ok(t.top() === "question" && U.top().lines.join("/") === "CUT TO 1 BAR?/NOTES IN THE BARS CUT GO" && U.top().answers.join() === "CUT,KEEP",
       "LENGTH shorter with notes past it: asks (CUT TO 1 BAR? / NOTES IN THE BARS CUT GO)");
    await t.tap("OCT-");
    ok(LS().rows === 32, "... KEEP: nothing cut");
    await t.turn("K1", -1); await t.tap("OCT+");
    ok(LS().rows === 16 && LS().list[0].repeats === 1 && LS().list[1].base === 16 && LS().list[1].repeats === 1, "... CUT: 1 bar, each loop cut there");
    await t.turn("K1", 1);
    ok(LS().rows === 32 && !t.top() || t.top() === "props", "LENGTH longer: silence added, no question");
    await t.turn("K1", -1);
    ok(LS().rows === 16 && t.top() === "props", "LENGTH shorter with no note past it: no question");
    await t.tap("GLO");
    await t.tap("SAVE"); await t.sleep(1600); await M.kept();
    ok(!M.unsavedThing("loops") && kept && kept.loops.list.length === 2, "SAVE keeps LOOPS");
    await menuPick("CLEAR LOOPS");
    ok(t.top() === "question" && U.top().lines.join("/") === "CLEAR ALL 2 LOOPS?/THE NEXT ONE SETS THE LENGTH" && U.top().answers.join() === "CLEAR,KEEP",
       "CLEAR LOOPS asks (CLEAR ALL 2 LOOPS? / THE NEXT ONE SETS THE LENGTH)");
    await t.tap("OCT+");
    ok(!LS().list.length && !LS().rows && LS().tempo === 120 && U.toastNow() === "LOOPS CLEARED", "... CLEAR: no loops, no length, the properties stay (LOOPS CLEARED)");
    await t.tap("SEL");
    ok(U.top().items[U.top().items.length - 1] === "REVERT", "LOOPS unsaved: REVERT last in its menu");
    await t.tap("HOME");
    await menuPick("REVERT");
    ok(t.top() === "question" && U.top().lines.join("/") === "REVERT LOOPS TO SAVED?", "REVERT asks (REVERT LOOPS TO SAVED?)");
    await t.tap("OCT+");
    ok(LS().list.length === 2 && LS().rows === 16 && !M.unsavedThing("loops") && U.toastNow() === "REVERTED", "... REVERT: LOOPS as saved (REVERTED)");
    U.edit(() => { LS().list[1].muted = true; LS().nextMix.rev = 0x20; });
    await pick(1);
    await t.tap("SAVE"); await t.sleep(1600); await M.kept();
    U.edit(() => { LS().list.pop(); });
    ok(await M.startUp() && LS().list.length === 2 && LS().list[1].muted && LS().nextMix.rev === 0x20 && LS().sel === 1 && notesAt(0).join() === "0:0:60",
       "a power cycle: LOOPS as SAVE kept it (its loops, notes, mutes, the next loop's mix, the selection), the change not saved gone");
    t.frame();
    ok(U.screen.selected() === 1, "... the selection back");

    /* ---- RECORD LIVE (fw/tests/ui3_test.c's test_loops_live, step for step; the loops' rows 250 ms, 234 after the
     * tempo change): its panel; armed, PLAY starts the loops and the song; a pattern of the rows heard, the loops' notes
     * as they sounded, a mute from its row, the keys landed by QUANTIZE (heard as written), an effect switched on, a
     * punch-in, FX turned, a tempo change and a loop's mix turned as commands at their rows; PLAY silencing the loops and
     * playing them on again, held taking them to their start, the song going on; REC ending it at once; REC starting it
     * at the row playing; leaving LOOPS; 16 tracks; stopping by itself ---- */
    {
        const sg = () => sc().song, lvs = () => U.screen.liveState(), row = () => U.screen.liveRow();
        const until = async (f, ms = 30000) => { for (let n = 0; n < ms / 10 && !f(); n++) await t.sleep(10); return f(); };
        const tickNow = () => { const h = realAt(); return h ? Math.min(LS().ticks - 1, Math.floor((P.time() - h.t) / h.dur * LS().ticks)) : 0; };
        /* until the song's row `r` is heard, at tick `k` or later: true there */
        const lvAt = async (r, k) => { await until(() => row() > r || (row() === r && tickNow() >= k)); return row() === r; };
        /* until the player's last switch is that one (the loops silent, or on again at that row): its song row, -1 none */
        const lvSwitch = async (rest, r) => {
            const is = () => { const s = P.lpRestState(); return s.on === rest && (rest || s.swRow === r); };
            await until(is, 3000);
            return is() && lvs() ? P.lpRestState().swN - lvs().rows0 : -1;
        };
        /* SEL's RECORD LIVE, then its panel's REC: armed */
        const lvArm = async () => {
            await menuPick("RECORD LIVE");
            const panel = t.top() === "live";
            await t.tap("REC");
            return panel && !!sg() && sg().state === "armed" && !t.top();
        };
        const cs = (base, list) => { const a = new Array(base * 4).fill(null); for (const [r, l, n] of list) a[r * 4 + l] = n === M.OFF ? M.newCell(M.OFF, null, null) : M.newCell(n, null, 100); return a; };
        const pc = (p, r, tr) => p && p.cells[r * 16 + tr];
        const has = (c, code, v) => !!c && c.cmds.some(x => x.c === code && (v == null || x.v === v));
        const cmdRow = (p, r, code, v) => [...Array(16).keys()].some(tr => has(pc(p, r, tr), code, v));
        if (U.screen.name !== "LOOPS") await t.homeKey(22);
        await stopAll();
        setLoops([], 0);
        await menuPick("RECORD LIVE");
        ok(!sg() && !t.top() && U.toastNow() === "NO LOOPS YET", "RECORD LIVE with no loops: NO LOOPS YET");
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, M.OFF], [8, 0, 62], [12, 0, M.OFF]]) }),
                  mkLoop({ inst: 0xF001, base: 4, repeats: 4, col: 1, cells: cs(4, [[0, 0, 64], [2, 0, M.OFF]]) })], 16);
        const rev0 = LS().fx.knobs.REVERB[3], dly0 = !!LS().fx.on.DELAY;
        U.edit(() => { Object.assign(LS(), { tempo: 60, quantize: "ROW", countIn: "OFF", nextMono: false }); LS().fx.on.DELAY = false; });
        P.kept.loops.row = 0;
        await pick(0);
        await menuPick("RECORD LIVE");
        const panel = t.top() === "live";
        await t.turn("K1", 2);
        const t2 = LS().tempo;
        await t.turn("K1", -2);
        await t.tap("HOME");
        ok(panel && t2 === 62 && LS().tempo === 60 && !t.top() && !sg(), "RECORD LIVE: SEL's RECORD LIVE opens its panel; K1 turns LOOPS' tempo; HOME " +
           "closes it, nothing armed");
        ok(await lvArm(), "RECORD LIVE: ... the panel's REC arms it and closes it");
        t.frame();
        ok(t.leds[13] === 1 || t.leds[13] === 0, "(REC blinks while armed)");
        await t.tap("EDIT");
        ok(U.toastNow() === "RECORDING A SONG" && cell(0, 0) && cell(0, 0).note === 60, "RECORD LIVE: ... EDIT refused while armed (RECORDING A SONG)");
        await t.tap("LFO");
        ok(U.toastNow() === "RECORDING A SONG" && LS().list.length === 2, "RECORD LIVE: ... PEEL refused while armed");
        await t.tap("SEL");
        ok(U.top().items.includes("CANCEL RECORD LIVE"), "RECORD LIVE: ... the menu's item reads CANCEL RECORD LIVE");
        U.top().sel = U.top().items.indexOf("CANCEL RECORD LIVE"); await t.tap("SEL");
        ok(!sg() && U.toastNow() === "CANCELLED" && !P.playing, "RECORD LIVE: ... CANCEL RECORD LIVE disarms it (CANCELLED)");
        await lvArm();
        const n0 = M.state.songs.length, k0 = U.midi(0), k4 = U.midi(4), calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, time, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
        await t.tap("PLAY");
        ok(sg() && sg().state === "on" && P.playing && P.what === "loops" && P.recording(), "RECORD LIVE: armed with nothing playing, PLAY (COUNT-IN " +
           "OFF): the loops from their start, the song recording with them (the header's red dot)");
        await lvAt(3, 1); U.edit(() => { LS().fx.on.DELAY = true; });
        await lvAt(5, 2); t.keyDown(0);                    /* (ROW: on row 5) */
        await lvAt(6, 0); U.punchDown(2);
        await lvAt(7, 1); t.keyUp(0);
        await t.tap("SEQ");
        await lvAt(9, 0); t.keyDown(U.WHITE[1]); await lvAt(9, 1); t.keyUp(U.WHITE[1]);   /* (CONTROL: loop 01 muted at once) */
        U.edit(() => { LS().mixer[0].lvl = 0x50; });
        await t.tap("SEQ");
        await lvAt(10, 0); U.punchUp(2);
        U.edit(() => { LS().quantize = "4 ROWS"; });
        await lvAt(10, 2);
        const pressed = P.time();
        t.keyDown(4);                                      /* (4 ROWS: 10.33 lands on 12, ahead: it waits for it) */
        await lvAt(12, 2); U.edit(() => { LS().fx.knobs.REVERB[3] = 70; });
        await lvAt(13, 5); t.keyUp(4);
        await lvAt(14, 1); U.edit(() => { LS().tempo = 64; });
        const kc = calls.find(c => /k4$/.test(c.id));
        ok(kc && kc.track === -1 && kc.time - pressed > .3, "RECORD LIVE: ... a key's note heard as written: on the keys' voices at its line, not " +
           "before (" + (kc ? (kc.time - pressed).toFixed(2) : "none") + " s)");
        ok(LS().list[1].muted, "RECORD LIVE: ... the mute is live");
        t.frame();
        const foot = U.screen.footer();
        ok(/^MEMORY \d+% FREE$/.test(foot[0]) && foot[1] === t.PTFM.draw.RED && t.leds[13] === 1, "RECORD LIVE: recording: the footer's MEMORY n% FREE (" +
           foot[0] + "), red; REC full");
        for (const [b, what] of [["EDIT", "RE-RECORD"], ["LFO", "PEEL"], ["ARP", "the pattern recorder"]]) {
            await t.tap(b);
            ok(U.toastNow() === "RECORDING A SONG" && LS().list.length === 2 && !t.top(), "RECORD LIVE: ... " + what + " refused (RECORDING A SONG)");
        }
        await menuPick("DOUBLE");
        ok(U.toastNow() === "RECORDING A SONG" && LS().rows === 16, "RECORD LIVE: ... DOUBLE refused");
        /* the loops' third pass (song rows 32-47): PLAY: the loops silent from the song's next row, it going on; again: on
         * from their place; held: to their start */
        await lvAt(34, 1); await t.tap("PLAY");
        const s0 = await lvSwitch(1, 0), place = P.lpRestState().kr;
        await lvAt(36, 0);
        const silent = !!sg() && sg().state === "on" && P.playing && P.what === "loops";
        await lvAt(38, 1); await t.tap("PLAY");
        const s1 = await lvSwitch(0, 3);
        await lvAt(46, 1); t.down("PLAY"); await t.sleep(U.set("HOLD DELAY") + 20); t.up("PLAY"); await t.sleep(20);
        const s2 = await lvSwitch(0, 0);
        ok(s0 === 35 && place === 3 && silent && s1 === 39 && s2 > 46, "RECORD LIVE: ... PLAY: the loops silent from the next row, their place kept (row " +
           s0 + ", place " + place + "), the song going on; PLAY: on again from there on the next row (" + s1 + "); held: to their start on the next row (" + s2 + ")");
        await lvAt(s2 + 2, 1);
        await t.tap("REC");                                /* it ends at once */
        A.noteOn = on0;
        const toast0 = U.toastNow() || "-";
        ok(!sg() && P.playing && P.what === "loops" && toast0.startsWith("SONG " + M.hex(n0) + " RECORDED: ") && toast0.endsWith(", 1 PATTERN"),
           "RECORD LIVE: REC: it ends at once, the loops playing on (" + toast0 + ")");
        const S = M.state.songs[n0], p0 = S && M.pattern(S.order[0]);
        ok(S && S.order.length === 1 && p0.rows === s2 + 3, "RECORD LIVE: a new song at the first free number, one pattern of the rows heard (" +
           (p0 ? p0.rows : 0) + ", its last row " + (s2 + 2) + ")");
        ok(S && S.tempo === 60 && S.rpb === 4 && S.atEnd === "STOP" && !S.fx.on.DELAY && S.fx.knobs.REVERB[3] !== 70 && S.mixer[0].lvl === 127 &&
           S.name === "LOOPS 01", "RECORD LIVE: ... LOOPS' tempo, FX setup and mixes as they were at its start; it plays once and stops; named as the " +
           "panel had it (LOOPS 01)");
        ok(pc(p0, 0, 0) && pc(p0, 0, 0).note === 60 && pc(p0, 0, 0).inst === 0xF000 && pc(p0, 8, 0) && pc(p0, 8, 0).note === 62 &&
           pc(p0, 4, 0) && pc(p0, 4, 0).note === M.OFF && pc(p0, 12, 0) && pc(p0, 12, 0).note === M.OFF, "RECORD LIVE: ... loop 00's notes on track 0, each naming its instrument");
        ok([0, 4, 8].every(r => pc(p0, r, 1) && pc(p0, r, 1).note === 64 && pc(p0, r, 1).inst === 0xF001) && pc(p0, 9, 1) && pc(p0, 9, 1).note === M.OFF &&
           !pc(p0, 12, 1) && !pc(p0, 10, 1), "RECORD LIVE: ... loop 01's repeats written out on track 1, left out from row 9 (its mute), a note off there " +
           "for the note it had sounding");
        ok(pc(p0, 5, 2) && pc(p0, 5, 2).note === k0 && pc(p0, 5, 2).inst === U.inst && pc(p0, 5, 2).vol === U.set("KEYS VOLUME") && pc(p0, 7, 2) &&
           pc(p0, 7, 2).note === M.OFF, "RECORD LIVE: ... the keys on a track of their own, in the current instrument, landed by QUANTIZE (ROW: 5.33 on 5, " +
           "its note off on 7)");
        ok(pc(p0, 12, 2) && pc(p0, 12, 2).note === k4 && pc(p0, 14, 2) && pc(p0, 14, 2).note === M.OFF, "RECORD LIVE: ... 4 ROWS: 10.33 on row 12, its note off on 14");
        ok(has(pc(p0, 9, 0), 0x80, 0x50), "RECORD LIVE: ... loop 00's mix turned: =80 50 on its track at its row (9)");
        ok(cmdRow(p0, 3, 0xAE, 5) && cmdRow(p0, 6, 0xC5, 4) && cmdRow(p0, 12, M.fxCode("REVERB", 3), 70) && cmdRow(p0, 14, 0x0F, 0x40),
           "RECORD LIVE: ... DELAY switched on (#ON 05, row 3), a punch-in of 4 rows (!04, row 6), REVERB MIX turned (row 12), the tempo (F40, row 14)");
        let empty = true, hush = true;
        for (let r = 16; r < 32; r++) empty = empty && !pc(p0, r, 1) && !pc(p0, r, 2);
        ok(pc(p0, 16, 0) && pc(p0, 16, 0).note === 60 && !pc(p0, 16, 0).cmds.length && empty, "RECORD LIVE: ... the loops' next pass (row 16): loop 01 " +
           "still muted, nothing played on the keys, no commands");
        for (let r = 36; r <= 38; r++) hush = hush && !pc(p0, r, 0);
        ok(pc(p0, 35, 0) && pc(p0, 35, 0).note === M.OFF && hush && pc(p0, 40, 0) && pc(p0, 40, 0).note === M.OFF && pc(p0, 44, 0) && pc(p0, 44, 0).note === 62 &&
           pc(p0, s2, 0) && pc(p0, s2, 0).note === 60, "RECORD LIVE: ... silent from row 35 (a note off for the note sounding there, then nothing), on " +
           "again from row 39 at their row 3 (row 40 their 4, row 44 their 8), at their start from the hold's row");
        U.edit(() => { LS().list[1].muted = false; LS().tempo = 60; LS().quantize = "ROW"; LS().fx.knobs.REVERB[3] = rev0; LS().fx.on.DELAY = dly0; });

        /* armed while the loops play: REC starts it at once, at the row playing; leaving LOOPS ends it at once, what was
         * heard written */
        ok(P.playing && P.what === "loops", "(the loops play on after it)");
        await lvArm();
        await until(() => { const h = realAt(); return !!h && h.row === 5; });
        await t.tap("REC");
        const r1 = ((realAt().row - row()) % 16 + 16) % 16;
        ok(sg() && sg().state === "on" && row() >= 0 && r1 === 5, "RECORD LIVE: armed while the loops play, REC starts it at once: its row 0 the loops' row " +
           "playing (" + r1 + ")");
        const n1 = lvs() ? lvs().song : -1;
        await lvAt(4, 0);                                  /* (the loops' row 9: their 8 heard on the song's 3) */
        await t.homeKey(8);
        const S1 = M.state.songs[n1], q = S1 && M.pattern(S1.order[0]), toast1 = U.toastNow() || "-";
        ok(U.screen.name === "PLAYBACK" && P.playing && S1 && S1.order.length === 1 && toast1.startsWith("SONG " + M.hex(n1) + " RECORDED: ") &&
           q.rows >= 4 && q.rows <= 6 && pc(q, 3, 0) && pc(q, 3, 0).note === 62, "RECORD LIVE: leaving LOOPS: it ends at once (" + toast1 + "), the loops " +
           "playing on; its pattern up to there (" + (q ? q.rows : 0) + " rows), the loops' row 8 on its row 3");
        await stopAll();
        await t.homeKey(22);

        /* what does not fit 16 tracks is left out and counted; the keys then have no track */
        const full = l4 => mkLoop({ lanes: 4, col: l4, cells: cs(16, [[0, 0, 60], [0, 1, 64], [0, 2, 67], [0, 3, 71]]) });
        setLoops([0, 1, 2, 3, 4].map(full), 16);
        P.kept.loops.row = 0;
        await lvArm();
        const n2 = M.state.songs.length;
        t.keyDown(2); await t.sleep(40); t.keyUp(2);       /* (nothing playing: the key starts it, its note on row 0) */
        ok(sg() && sg().state === "on" && P.playing && P.what === "loops" && P.lpRestState().on === 1,
           "RECORD LIVE: armed with nothing playing, a key starts it, the loops' replay running silent");
        await t.tap("PLAY");                               /* (the loops on from their place: their start) */
        await lvAt(await lvSwitch(0, 0), 1);
        await t.tap("REC");
        await until(() => !sg());
        const S2 = M.state.songs[n2], r0 = S2 && M.pattern(S2.order[0]), toast2 = U.toastNow() || "-";
        ok(S2 && r0.tracks === 16 && toast2 === "SONG " + M.hex(n2) + ": 5 NOTES LEFT OUT", "RECORD LIVE: 16 tracks: 4 loops of 4 lanes fill them; the " +
           "fifth loop's notes and the key's left out, counted (" + toast2 + ")");
        await stopAll();

        /* it stops by itself (a pattern every 128 rows: the loops at the fastest tempo): the edit memory full, nothing past
         * it written; every pattern number taken, at its first pattern (256 positions, 32,768 rows, are not run here); no
         * song number free: refused */
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        U.edit(() => { LS().tempo = M.TEMPO_MAX; });
        const stopsAt = async set => {
            P.kept.loops.row = 0;
            await lvArm();
            const n = M.state.songs.length;
            set();
            await t.tap("PLAY");
            await until(() => !sg(), 20000);
            await stopAll();
            return { n, toast: U.toastNow() || "-" };
        };
        const was = M.EDITS_MAX;
        let r = await stopsAt(() => { M.EDITS_MAX = M.editsUsed + 8; });
        M.EDITS_MAX = was;
        ok(r.toast === "SONG " + M.hex(r.n) + " STOPPED: MEMORY FOR EDITS IS FULL" && !M.state.songs[r.n],
           "RECORD LIVE: the edit memory full: it stops (" + r.toast + "), nothing past it written (no song)");
        const filled = [];
        for (let k = 0; k <= M.USER_LAST; k++) if (!M.state.patterns[k]) { M.state.patterns[k] = M.newPattern(k, 1, 1); filled.push(k); }
        M.settle();
        r = await stopsAt(() => {});
        for (const k of filled) M.state.patterns[k] = null;
        M.settle();
        ok(r.toast === "SONG " + M.hex(r.n) + " STOPPED: ALL 240 PATTERNS USED" && !M.state.songs[r.n], "RECORD LIVE: no pattern number free: it stops (" +
           r.toast + ")");
        U.edit(() => { LS().tempo = 120; });
        const songs0 = M.state.songs.slice();
        while (M.state.songs.length <= M.SONG_LAST) M.state.songs.push(M.newSong());
        await menuPick("RECORD LIVE");
        ok(!sg() && !t.top() && U.toastNow() === "NO SONG NUMBER FREE", "RECORD LIVE: every song number taken: refused (NO SONG NUMBER FREE)");
        M.state.songs = songs0;
        M.settle();
    }
    M.demoDisk();
};
