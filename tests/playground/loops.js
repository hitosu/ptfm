// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* LOOPS, the loop station (specs/ui/screens/loops.md, specs/files/loops.md): the length always set; PLAY, the play
 * position (ALGORITHM), REC armed; THE STAGE: notes landing by QUANTIZE, MONO legato (GLIDE, JUMP, LEGATO, a TB-303's
 * slide), POLY's lanes and its fifth note dropped, the overdub replacing what the stage had over a key's span (POLY every
 * lane, MONO its own) only up to the play line, a key held across the line, OVERWRITE (LFO's press), the stage's clear
 * (EDIT held, EDIT on + NEW LOOP, CLEAR STAGE), REC sending the stage (a new loop, or into the selected one), EDIT copying a
 * loop onto the stage, the selected loop silent while the stage holds notes, the stage kept through moves; K1-K4's jobs, VIEW,
 * LENGTH planned until GLO closes, SEL's menu (DOUBLE only when it can, QUANTIZE, ADJUST, DELETE LOOP, the instruments);
 * the overview column; repeats, rests and SHIFT played by the lanes; mutes and solos, now and at the bar, CONTROL mode,
 * SEQ held, HOME's combination; the pattern recorder (onto the stage, NOTE LENGTH, ties); SAVE TO / LOAD FROM PATTERN;
 * DOUBLE; CLEAR LOOPS; REVERT; SAVE and a power cycle; RECORD LIVE (its panel, armed, started, what sounded written, the
 * keys quantized and heard as written, the changes as commands, the loops silenced and on again while it goes on, ended
 * at once, leaving, 16 tracks, stopping by itself). Where the loops are heard is stood in for where a case must land on
 * a row. Run by the harness:
 *   node tests/playground/harness.js tests/playground/loops.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const A = t.PTFM.audio, D = t.PTFM.draw, B = U.B, LS = () => M.state.loops, sc = () => U.screen.state(), stg = () => U.screens.LOOPS.stage();
    const isNote = c => !!c && c.note != null && c.note !== M.OFF;
    const isOff = c => !!c && c.note === M.OFF;
    const cell = (i, r, l = 0) => LS().list[i] && LS().list[i].cells[r * 4 + l];
    const notesAt = i => LS().list[i].cells.map((c, k) => isNote(c) ? (k >> 2) + ":" + (k & 3) + ":" + c.note : null).filter(Boolean);
    /* the stage: a cell, its notes as row:lane:note, whether it holds notes */
    const sCell = (r, l = 0) => stg().cells ? stg().cells[r * 4 + l] : null;
    const sNotes = () => (stg().cells || []).map((c, k) => isNote(c) ? (k >> 2) + ":" + (k & 3) + ":" + c.note : null).filter(Boolean).join();
    const staged = () => !!stg().cells && stg().cells.some(isNote);
    const stageOn = async () => { if (stg().off) await t.tap("ARP"); };   /* (REC's send left it off) */
    /* (a setup's clean stage; the stage's own ways of emptying are checked below) */
    const emptyStage = () => { const s = stg(); s.cells = null; s.erasers = []; s.over = false; M.version++; };
    const titleOf = () => U.screen.stageTitle().split("|")[0];
    let kept = null;
    M.useStore({ get: async () => kept, put: async v => { kept = structuredClone(v); }, del: async () => { kept = null; } });
    /* where the loops are heard, stood in for: row + frac of pass (rows of 0.5 s) */
    const realAt = P.at;
    const heardAt = (row, frac, pass = 0) => { P.at = () => ({ row, pass, t: P.time() - frac * .5, dur: .5, rpb: 4 }); };
    const real = () => { P.at = realAt; };
    const press = (k, row, frac, pass) => { heardAt(row, frac, pass); t.keyDown(k); real(); };
    const lift = (k, row, frac, pass) => { heardAt(row, frac, pass); t.keyUp(k); real(); };
    const frameAt = (row, frac, pass) => { heardAt(row, frac, pass); t.frame(); real(); };   /* (the line there: the held spans grow to it) */
    const until = async (f, ms = 30000) => { for (let n = 0; n < ms / 10 && !f(); n++) await t.sleep(10); return f(); };
    const plain = () => ({ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 });
    const cs = (base, list) => { const a = new Array(base * 4).fill(null); for (const [r, l, n] of list) a[r * 4 + l] = n === M.OFF ? M.newCell(M.OFF, null, null) : M.newCell(n, null, 100); return a; };
    const mkLoop = o => Object.assign({ inst: 0xF000, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted: false, soloed: false,
                                        cells: new Array(((o && o.base) || 16) * 4).fill(null) }, o);
    const setLoops = (list, rows) => {
        U.edit(() => { const j = LS(); j.list = list; j.rows = rows; for (let k = 0; k < 16; k++) j.mixer[k] = k < list.length ? plain() : Object.assign({}, j.nextMix); });
        P.mutesChanged();
        emptyStage();
    };
    const pick = async i => { const d = i - sc().sel; if (d) await t.turn("SELECT", d); };
    const menuPick = async item => { await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf(item); await t.tap("SEL"); };
    const menuItems = async () => { await t.tap("SEL"); const it = t.top() === "menu" ? U.top().items.slice() : []; await t.tap("HOME"); return it; };
    const stopAll = async () => { P.pause(); await t.sleep(60); };
    /* the loops playing from their start, so slow a row lasts 5 s (the cases stand in for where they are heard) */
    const slow = async () => { await stopAll(); U.edit(() => { LS().tempo = 3; }); P.kept.loops.row = 0; await t.tap("PLAY"); };
    const hold = async b => { t.down(b); await t.sleep(U.set("HOLD DELAY") + 100); t.up(b); await t.sleep(60); };
    /* the pattern recorder: ARP held + REC (it opens at REC's release) */
    const openRec = () => t.combo("ARP", "REC");
    /* every open is clean: opened again, what the last open had set, set again as a hand would */
    const openRecAgain = async () => {
        const w = U.screen.recorder(), was = { rows: w.rows.map(r => ({ note: r.note, steps: r.steps.slice() })), sel: w.sel, len: w.len, noteLen: w.noteLen };
        await openRec();
        w.rows.forEach((r, k) => { r.note = was.rows[k].note; r.steps = was.rows[k].steps; });
        w.sel = was.sel; w.len = was.len; w.noteLen = was.noteLen;
    };
    /* a palette colour (D.DEEP + c: its deep shade) as the screen's RGB565, in the theme shown */
    const c565 = c => {
        const g0 = D.rgb[0], f = (v, g) => c >= D.DEEP ? g + Math.trunc((v - g) * 3 / 10) : v, [r, g, b] = D.rgb[c & 15];
        return ((f(r, g0[0]) >> 3) << 11) | ((f(g, g0[1]) >> 2) << 5) | (f(b, g0[2]) >> 3);
    };
    const tb303 = M.state.carts.find(c => c.type === "TB303").id << 8, dx7 = 0xF000;
    U.setInst(dx7);
    ok(M.state.loops.fx.routing === "SENDS" && !M.unsavedThing("fx", "loops"), "LOOPS with no record: its FX setup ROUTING SENDS, nothing unsaved");
    /* the stage's column (the worklet's track 32): the mix of the loop's track it stands for, told as the loops play */
    const STAGE_T = 32, stageCol0 = A.stageCol;
    let stageCol = null;
    A.stageCol = tr => { stageCol = tr; return stageCol0(tr); };

    /* ---- at first: the length set, nothing on the stage; PLAY with no loops; REC armed; the play position ---- */
    await t.homeKey(22);
    t.frame();
    ok(U.screen.name === "LOOPS" && !LS().list.length && LS().rows === 64 && LS().view === 2 && LS().knobs.join() === "LEVEL,PAN,REVERB SEND,TEMPO",
       "LOOPS: no loops; the length always set, 4 bars at first; VIEW 2 bars; K1-K4: LEVEL, PAN, REVERB SEND, TEMPO");
    ok(LS().legato === "JUMP" && M.newLoops().legato === "JUMP", "... MONO LEGATO JUMP at first (a new LOOPS setup)");
    ok(!staged() && t.leds[B.EDIT] === 0 && t.leds[B.LFO] === .25 && t.leds[B.REC] === .25, "... the stage empty; EDIT dark (on + NEW LOOP), LFO and REC dim");
    await t.tap("EDIT");
    ok(U.flash[B.EDIT] > U.now() && !staged(), "EDIT on + NEW LOOP: nothing (EDIT blinks)");
    await t.tap("PLAY"); await t.sleep(150);
    ok(P.playing && P.what === "loops", "PLAY with no loops: the loops play over the length");
    await t.tap("REC");
    ok(U.flash[B.REC] > U.now() && U.toastNow() === "THE STAGE IS EMPTY" && !sc().arm, "REC while they play, the stage empty: refused (THE STAGE IS EMPTY)");
    await t.sleep(300);
    await t.tap("PLAY");
    ok(!P.playing && P.kept.loops.row === 0, "PLAY while they play: stop, the play position back to the start");
    await t.turn("ALGORITHM", 3);
    ok(P.kept.loops.row === 3, "ALGORITHM, stopped: the play position a row a click (row 3)");
    t.down("SEL"); await t.sleep(30); await t.turn("ALGORITHM", 1); t.up("SEL"); await t.sleep(250);
    ok(P.kept.loops.row === 19 && !t.top(), "... SEL held: a bar a click (row 19), no menu after");
    t.down("SEL"); await t.sleep(30); await t.turn("ALGORITHM", -2); t.up("SEL"); await t.sleep(250);
    ok(P.kept.loops.row === 51, "... round the length (19 - 32: row 51)");
    await t.tap("REC");
    t.frame();
    ok(sc().arm && sc().arm.wait && !P.playing, "REC stopped, the stage empty: armed (a note or PLAY starts)");
    await t.sleep(400);                                     /* (REC's 500 ms after a job) */
    await t.tap("REC");
    ok(!sc().arm && U.toastNow() === "CANCELLED", "REC again: disarmed (CANCELLED)");
    await t.sleep(400);
    await t.tap("REC");
    t.keyDown(7); await t.sleep(40);
    ok(P.playing && P.what === "loops" && !sc().arm && isNote(sCell(51)) && sCell(51).note === U.midi(7) && sCell(51).vol === U.set("KEYS VOLUME"),
       "armed, a key: the loops start at the play position, its note on the stage there (row 51), at KEYS VOLUME");
    t.keyUp(7); await t.sleep(40);
    t.frame();
    ok(t.leds[B.REC] === 1, "... REC full: the stage holds notes (REC sends them)");
    await t.tap("PLAY");
    ok(!P.playing && staged(), "PLAY: stopped; the stage keeps its notes");
    {
        const before = sNotes(), heard = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ...a) => { heard.push(midi); return on0(id, time, midi, ...a); };
        await t.key(12);
        A.noteOn = on0;
        ok(sNotes() === before && heard.includes(U.midi(12)) && !P.playing, "stopped (not armed): a key plays freely, nothing onto the stage");
    }
    await t.turn("ALGORITHM", 5);
    await hold("PLAY");
    ok(!P.playing && P.kept.loops.row === 0 && U.toastNow() === "BACK TO THE START", "PLAY held: stopped, back to the start (BACK TO THE START)");
    emptyStage();
    await t.tap("REC"); await t.tap("PLAY"); await t.sleep(80);
    ok(P.playing && !sc().arm, "armed, PLAY (COUNT-IN OFF): the loops at once");
    await stopAll();
    {   /* armed, a chord (its keys at once): the first key starts the loops, the others land with it */
        emptyStage();
        P.kept.loops.row = 0;
        await t.sleep(600);
        await t.tap("REC");
        for (const k of [0, 4, 7, 11]) t.keyDown(k);
        await t.sleep(40);
        for (const k of [0, 4, 7, 11]) t.keyUp(k);
        await t.sleep(200);
        ok(P.playing && [0, 4, 7, 11].every((k, l) => isNote(sCell(0, l)) && sCell(0, l).note === U.midi(k)),
           `armed, a chord of four in one scan: the loops start, all four on the stage at the play position, a lane each (${sNotes()})`);
        await stopAll();
        emptyStage();
    }

    /* ---- the stage: where a note lands by QUANTIZE ---- */
    setLoops([], 64);
    await slow();
    const quant = (q, cases) => {
        U.edit(() => { LS().quantize = q; });
        emptyStage();
        const got = [];
        for (const [row, frac, k] of cases) {
            press(k, row, frac);
            const m = U.midi(k), at = (stg().cells || []).findIndex(c => isNote(c) && c.note === m), c = at >= 0 ? stg().cells[at] : null;
            const ed = c && c.cmds.find(x => x.c === 0x0E);
            got.push(at < 0 ? "none" : (at >> 2) + ((at & 3) ? "/" + (at & 3) : "") + (ed ? "d" + (ed.v & 15) : ""));
            lift(k, (row + 1) % 64, .1, row + 1 >= 64 ? 1 : 0);
        }
        return got.join();
    };
    let q = quant("ROW", [[5, .3, 2], [9, .6, 4], [63, .7, 5]]);
    ok(q === "5,10,0/1", "ROW: the nearest row (5.3 on 5, 9.6 on 10; 63.7 on row 0, the next pass's, lane 1: lane 0's notes ahead of it, the lane free longest) (" + q + ")");
    q = quant("2 ROWS", [[4, .9, 2], [7, .1, 4]]);
    ok(q === "4,8", "2 ROWS: the nearest line of 2 (4.9 on 4, 7.1 on 8) (" + q + ")");
    q = quant("3 ROWS", [[4, .4, 2], [7, .6, 4]]);
    ok(q === "3,9", "3 ROWS: the nearest line of 3 (4.4 on 3, 7.6 on 9) (" + q + ")");
    q = quant("4 ROWS", [[5, .9, 2], [10, .1, 4]]);
    ok(q === "4,12", "4 ROWS: the nearest line of 4 (5.9 on 4, 10.1 on 12) (" + q + ")");
    q = quant("REAL TIME", [[5, .5, 2], [9, .1, 4]]);
    ok(q === "5d3,9", "REAL TIME: the row it falls in, a note delay of its tick (5.5: ED3) (" + q + ")");
    emptyStage();
    press(0, 12, 0); lift(0, 15, .5);
    {
        const k = sCell(15) && sCell(15).cmds.find(x => x.c === 0x14);
        ok(k && k.v === 3 && sCell(15).note == null, "REAL TIME: a key's end a key off at its tick (K03 on row 15)");
    }
    U.edit(() => { LS().quantize = "ROW"; });

    /* ---- MONO: one lane, legato as MONO LEGATO says ---- */
    U.edit(() => { LS().nextMono = true; LS().legato = "GLIDE"; });
    emptyStage();
    press(0, 3, .1); press(4, 5, .1);
    ok(isNote(sCell(5)) && sCell(5).note === U.midi(4) && sCell(5).cmds.some(x => x.c === 0x03 && x.v === 0x20) && !sCell(5, 1),
       "MONO, GLIDE: a key over a held one: its note on the one lane with 320 (a glide)");
    lift(0, 6, .1);
    ok(!sCell(6), "... the held key let go: nothing (its note no longer sounds)");
    lift(4, 7, .2);
    ok(isOff(sCell(7)), "... the sounding one let go: its note off");
    press(5, 9, .1); press(7, 9, .2);
    ok(isNote(sCell(9)) && sCell(9).note === U.midi(7) && !sCell(9).cmds.length && !sCell(9, 1), "keys landing on the same row: the last registered is written");
    lift(5, 10, .1); lift(7, 11, .1);
    U.edit(() => { LS().legato = "JUMP"; });
    press(0, 13, .1); press(4, 15, .1);
    ok(isNote(sCell(15)) && sCell(15).note === U.midi(4) && !sCell(15).cmds.length, "MONO, JUMP: a plain new note");
    lift(4, 16, .1); lift(0, 16, .2);
    {   /* LEGATO: LEG with the new note, heard on the held voice (the worklet told its id) */
        const calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, midi, leg: opts && opts.leg }); return on0(id, time, midi, ref, vol, track, opts); };
        U.edit(() => { LS().legato = "LEGATO"; });
        press(0, 18, .1); press(4, 20, .1);
        A.noteOn = on0;
        const held = calls.find(x => x.midi === U.midi(0)), leg = calls.find(x => x.midi === U.midi(4));
        ok(isNote(sCell(20)) && sCell(20).cmds.some(x => x.c === 0xD3) && !sCell(20).cmds.some(x => x.c === 0x03) && held && leg && leg.leg === held.id,
           "MONO, LEGATO: LEG with the new note, heard as a legato onto the held one's voice");
        lift(4, 21, .1); lift(0, 21, .2);
    }
    U.edit(() => { LS().legato = "GLIDE"; });
    U.setInst(tb303);
    press(0, 23, .1); press(4, 25, .1);
    ok(sCell(23).cmds.some(x => x.c === 0xC1) && isNote(sCell(25)) && !sCell(25).cmds.length, "MONO, GLIDE on a TB-303: SLD on the note held, the new one plain");
    lift(4, 26, .1); lift(0, 26, .2);
    U.setInst(dx7);
    /* MONO over a stage with chords: one lane from then on, the later note of a chord kept */
    U.edit(() => { LS().nextMono = false; });
    emptyStage();
    press(0, 30, .1); press(4, 30, .2); lift(0, 31, .1); lift(4, 31, .1);
    const chord = isNote(sCell(30, 1));
    U.edit(() => { LS().nextMono = true; });
    press(7, 34, .1); lift(7, 35, .1);
    ok(chord && !stg().cells.some((c, k) => c && (k & 3)) && sCell(30).note === U.midi(4) && isNote(sCell(34)),
       "MONO over a POLY stage: merged to one lane for good (a chord: its later note)");

    /* ---- POLY: up to 4 notes at once, a fifth neither heard nor written; REC on + NEW LOOP ---- */
    U.edit(() => { LS().nextMono = false; });
    emptyStage();
    {
        const calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
        for (const k of [2, 4, 5, 7, 9]) press(k, 40, .1);
        A.noteOn = on0;
        ok([0, 1, 2, 3].map(l => sCell(40, l) && sCell(40, l).note).join() === [2, 4, 5, 7].map(U.midi).join() &&
           !stg().cells.some(c => isNote(c) && c.note === U.midi(9)), "POLY: chords a key a lane (by the keys' order); a fifth note not written");
        ok(!calls.some(c => /k9$/.test(c.id)) && calls.some(c => /k7$/.test(c.id) && c.track === STAGE_T) && stageCol === 16,
           "... nor heard; the keys sound through the mix REC puts the stage in (the stage's column, carrying + NEW LOOP's: the next loop's, track 16)");
        for (const k of [2, 4, 5, 7, 9]) lift(k, 42, .1);
    }
    t.down("REC"); await t.sleep(30);
    {
        const L0 = LS().list[0];
        ok(L0 && L0.inst === dx7 && L0.col === 0 && !L0.mono && L0.lanes === 4 && L0.base === 64 && L0.repeats === 1 && !L0.shift &&
           notesAt(0).join() === [2, 4, 5, 7].map((k, l) => "40:" + l + ":" + U.midi(k)).join() && !staged() && sc().sel === 1 && U.toastNow() === "STAGE AS LOOP 00",
           "REC (at its press) on + NEW LOOP: the stage a new loop (STAGE AS LOOP 00) in the keys' instrument and mode, the next colour, " +
           "the length once; the stage emptied, the selection on + NEW LOOP again");
    }
    t.up("REC"); await t.sleep(100);
    ok(stg().off && !t.top(), "REC's send leaves the stage off");
    await t.sleep(600);
    await t.tap("REC");
    ok(!stg().off && U.toastNow() === "STAGE ON" && LS().list.length === 1 && !sc().arm, "... REC again: the stage on (STAGE ON), nothing sent or armed");
    await stopAll();

    /* ---- EDIT: the selected loop onto the stage, as it plays, and its sound ---- */
    setLoops([mkLoop({ inst: 0xF001, lanes: 2, repeats: 2, shift: 2, cells: cs(16, [[0, 0, 60], [0, 1, 64], [8, 0, 62]]) }),
              mkLoop({ col: 1, cells: cs(16, [[4, 0, 67]]) })], 64);
    U.edit(() => { LS().nextMono = true; });
    await pick(0);
    await t.tap("EDIT");
    ok(sNotes() === "2:0:60,2:1:64,10:0:62,18:0:60,18:1:64,26:0:62" && isOff(sCell(34)) && isOff(sCell(34, 1)) && notesAt(0).join() === "0:0:60,0:1:64,8:0:62",
       "EDIT: loop 00 copied onto the stage as it plays: its repeats written out, a note off where it rests, its SHIFT applied (the loop unchanged)");
    ok(U.inst === 0xF001 && !LS().nextMono && U.toastNow() === "LOOP 00 ON THE STAGE", "... its sound the keys' (its instrument, POLY) (LOOP 00 ON THE STAGE)");
    ok(P.lpSilent(0) && !P.lpSilent(1), "... the loop silent at once (the stage holds notes: it is what REC would put there)");
    await pick(2);
    await t.tap("EDIT");
    ok(!staged() && U.toastNow() === "STAGE CLEARED", "EDIT on + NEW LOOP: the stage cleared (STAGE CLEARED)");
    await pick(0);
    await t.tap("EDIT");

    /* ---- the overdub: a key takes a lane free there and what the stage had stays (specs/ui/screens/loops.md#the-stage) ---- */
    await slow();
    press(2, 8, .1);
    ok(isNote(sCell(8, 2)) && sCell(8, 2).note === U.midi(2) && !isOff(sCell(8)) && !isOff(sCell(8, 1)),
       "POLY OVERDUB: a note on the stage, on a lane free there (2: lanes 0 and 1 sounding), nothing cut");
    frameAt(12, .5);
    ok(isNote(sCell(10)) && isNote(sCell(18)) && isNote(sCell(18, 1)), "... held: the other lanes' notes it passes kept (row 10)");
    lift(2, 14, .1);
    ok(sNotes() === "2:0:60,2:1:64,8:2:" + U.midi(2) + ",10:0:62,18:0:60,18:1:64,26:0:62" && isOff(sCell(14, 2)),
       "... let go: its note off; the stage keeps all its notes (" + sNotes() + ")");
    /* no lane free: the one whose note ends soonest, cut there; a note starting there while the key is held moved on with it,
     * put down where it is let go (one covered whole gone) */
    setLoops([mkLoop({ lanes: 4, cells: cs(16, [[0, 0, 60], [0, 1, 64], [0, 2, 67], [0, 3, 71], [6, 2, 69], [7, 2, 74], [8, 0, 62], [10, 3, 72],
                                                [12, 1, 65], [14, 2, 76]]) })], 16);
    await pick(0); await t.tap("EDIT");
    await slow();
    press(2, 4, .1);
    const cut = isNote(sCell(4, 2)) && sCell(4, 2).note === U.midi(2) && isNote(sCell(0, 2)) && isNote(sCell(6, 2));
    frameAt(8, .2);
    lift(2, 8, .3);
    ok(cut && sNotes() === "0:0:60,0:1:64,0:2:67,0:3:71,4:2:" + U.midi(2) + ",8:0:62,8:2:74,10:3:72,12:1:65,14:2:76",
       "POLY OVERDUB, no lane free: lane 2's note cut where the key lands (it ends soonest), its next start moved on with the key, put " +
       "down where it is let go (row 6's covered whole by row 7's: gone; row 7's at 8) (" + sNotes() + ")");
    setLoops([mkLoop({ mono: true, repeats: 4, cells: cs(16, [[0, 0, 60], [4, 0, 62], [8, 0, 64], [12, 0, 65]]) })], 64);
    await t.tap("EDIT");
    await slow();
    press(2, 2, .1); frameAt(6, .5);
    const ahead = isNote(sCell(8));
    lift(2, 9, .2);
    ok(LS().nextMono && ahead && sNotes().startsWith("0:0:60,2:0:" + U.midi(2) + ",9:0:64,12:0:65,16:0:60"),
       "MONO OVERDUB: a note starting while the key is held moved on with it (row 4's covered whole: gone; row 8's put down at 9, where it is " +
       "let go, only once the line passed it) (" + sNotes() + ")");

    /* ---- OVERWRITE: LFO's press switches it on and off, at once; it stays until the next press; the title shows the mode ---- */
    setLoops([mkLoop({ repeats: 4, cells: cs(16, [[0, 0, 60], [2, 0, 62], [4, 0, 64], [6, 0, 65], [8, 0, 67], [10, 0, 69], [12, 0, 71], [14, 0, 72]]) })], 64);
    const hintOf = () => U.screen.hintText ? U.screen.hintText() : "-";
    t.frame();
    ok(titleOf() === "STAGE OVERDUB LFO" && t.leds[B.LFO] === .25 && hintOf() === "" && !stg().over,
       "OVERDUB is the plain state: the title STAGE OVERDUB and a dim LFO, LFO dim, no hint (" + titleOf() + ")");
    await t.tap("EDIT");
    await slow();
    heardAt(4, .1); t.down("LFO"); real(); await t.sleep(30);
    t.frame();
    const waits = isNote(sCell(4)) && titleOf() === "STAGE OVERDUB LFO" && !stg().over;
    heardAt(4, .2); t.up("LFO"); real(); await t.sleep(30);
    ok(waits && !isNote(sCell(4)) && isOff(sCell(4)) && isNote(sCell(2)), "LFO's press: nothing yet; its release: OVERWRITE, at once, no grace; the stage's row under the line erased (the note before it cut there)");
    t.frame();
    ok(U.toastNow() === "OVERWRITE" && titleOf() === "STAGE OVERWRITE LFO" && t.leds[B.LFO] === 1 && hintOf() === "" && stg().over,
       "... OVERWRITE: the title STAGE OVERWRITE and a dim LFO, LFO full, no hint (" + titleOf() + ")");
    frameAt(6, .5);
    press(9, 7, .1);
    frameAt(9, .3);
    lift(9, 9, .3);
    heardAt(9, .35); t.down("LFO"); t.frame();
    const still = titleOf() === "STAGE OVERWRITE LFO";
    t.up("LFO"); real(); await t.sleep(30);
    t.frame();
    const dubAgain = still && U.toastNow() === "OVERDUB" && titleOf() === "STAGE OVERDUB LFO" && !stg().over;
    await t.sleep(30);
    frameAt(11, .5);
    ok(dubAgain && t.leds[B.LFO] === .25, "... its tap again: OVERDUB, at its release (" + titleOf() + "), LFO dim");
    ok(sNotes().startsWith("0:0:60,2:0:62,7:0:" + U.midi(9) + ",10:0:69,12:0:71,14:0:72,16:0:60") && isOff(sCell(9)),
       "... what the line passes becomes what is played, silence included (rows 4-9: the key's note alone) (" + sNotes() + ")");
    heardAt(20, .1); await t.tap("LFO"); real();
    ok(stg().over && U.toastNow() === "OVERWRITE" && LS().list.length === 1 && staged() && !t.top(), "LFO's tap again: OVERWRITE; nothing sent, nothing opens");
    frameAt(23, .5);
    ok(!isNote(sCell(22)) && isNote(sCell(24)), "... LFO let go, it goes on erasing as the line passes");
    await t.tap("LFO");
    frameAt(27, .5);
    ok(!stg().over && U.toastNow() === "OVERDUB" && isNote(sCell(26)), "LFO tapped again: OVERDUB (the stage kept from there)");
    heardAt(28, .1); t.down("LFO"); t.up("LFO"); real(); await t.sleep(30); await t.tap("ARP"); await t.sleep(60);
    ok(U.toastNow() === "STAGE OFF" && stg().off && !t.top() && stg().over && titleOf() === "STAGE OFF" && !stg().erasers.length,
       "ARP with OVERWRITE on: the plain switch (STAGE OFF), nothing opens; OVERWRITE on, at rest (" + titleOf() + ")");
    await t.tap("ARP");
    ok(U.toastNow() === "STAGE ON" && !stg().off && stg().over && !t.top(), "... ARP's tap: the stage on, OVERWRITE on again");
    await t.tap("LFO");
    await stopAll();
    /* LFO's release: however long it was held, whatever was played with it; only HOME takes it */
    t.down("LFO"); await t.sleep(U.set("HOLD DELAY") + 100);
    const longLfo = !t.top() && !stg().over;
    t.up("LFO"); await t.sleep(30);
    ok(longLfo && !t.top() && stg().over && U.toastNow() === "OVERWRITE", "LFO held past HOLD DELAY: no layer, no switch yet; let go: OVERWRITE");
    t.down("LFO"); await t.sleep(30); t.keyDown(U.WHITE[3]); await t.sleep(30); t.keyUp(U.WHITE[3]); t.up("LFO"); await t.sleep(30);
    ok(!stg().over && U.toastNow() === "OVERDUB", "... a key played while it is held: the switch all the same");
    t.down("LFO"); await t.sleep(30); t.down("HOME"); await t.sleep(30); t.up("HOME"); t.up("LFO"); await t.sleep(200);
    ok(t.top() === "help" && !stg().over, "LFO held, then HOME: the help, and no switch at LFO's release");
    await t.tap("HOME");
    await t.tap("LFO");                                     /* (stopped, OVERWRITE erases nothing) */
    {
        const was = sNotes(), seen = new Set();
        for (let n = 0; n < 6; n++) { t.frame(); seen.add(t.leds[B.LFO]); await t.sleep(200); }
        ok(seen.size === 1 && seen.has(1) && sNotes() === was, "... stopped: OVERWRITE erases nothing; LFO full, no blink");
    }
    /* outside LOOPS OVERWRITE rests, FX too: the stage plays on, its notes kept; back, it goes on from the line */
    {
        const noteOn = A.noteOn;
        U.edit(() => { LS().tempo = 120; });
        P.kept.loops.row = 0; await t.tap("PLAY"); await t.sleep(200); t.frame();
        const running = stg().erasers.length;
        await t.tap("FX");
        const was = sNotes(), onFx = U.screen.name === "FX", rest = !stg().erasers.length;
        await t.sleep(2500);
        ok(running && onFx && rest && stg().over && sNotes() === was, "OVERWRITE on, on FX: it rests, the stage's notes kept (" + sNotes() + ")");
        await t.tap("FX");
        t.frame();
        ok(U.screen.name === "LOOPS" && stg().erasers.length === 1, "... FX again: back on LOOPS, at work");
        await t.homeKey(1);
        const was2 = sNotes(), songs = U.screen.name === "SONGS", ids = [];
        A.noteOn = (id, ...a) => { ids.push(id); return noteOn(id, ...a); };
        await t.sleep(2500);
        A.noteOn = noteOn;
        ok(songs && ids.some(i => i[0] === "g") && !stg().erasers.length && stg().over && sNotes() === was2,
           "... on another screen: at rest, the stage heard, its notes kept (" + sNotes() + ")");
        await t.homeKey(22);
        t.frame();
        ok(U.screen.name === "LOOPS" && stg().erasers.length === 1, "... LOOPS again: at work");
        await t.tap("LFO");
        ok(U.toastNow() === "OVERDUB" && !stg().over && !stg().erasers.length, "... LFO: OVERDUB");
    }
    await stopAll();
    emptyStage();

    /* ---- the stage's clear: EDIT held, EDIT's tap on + NEW LOOP, SEL's CLEAR STAGE, each turning the stage on; what leaves
     * OVERWRITE on (REC's send, a clear, a move of the selection), what ends it (EDIT on a loop, the pattern recorder's
     * fill); LFO held + REC plain REC; ARP held + REC the recorder however long REC is held ---- */
    {
        const e3 = () => cs(16, [[0, 0, 60], [4, 0, 64], [8, 0, 67]]), playingNow = () => P.playing && P.what === "loops";
        const onStage = () => { const s = stg(); s.cells = new Array(64).fill(null); for (const r of [0, 4, 8]) s.cells[r * 4] = M.newCell(60 + r, null, 100); M.version++; };
        const edHeld = async () => { t.down("EDIT"); await t.sleep(U.LONG_HOLD + 100); t.up("EDIT"); await t.sleep(60); };
        U.edit(() => { LS().tempo = 120; });
        setLoops([mkLoop({ cells: e3() }), mkLoop({ col: 1, cells: e3() })], 16);
        await pick(0);
        t.frame();
        ok(t.leds[B.EDIT] === .25, "EDIT's LED: dim on a loop, the stage empty");
        await pick(2);
        t.frame();
        ok(t.leds[B.EDIT] === 0 && !(await menuItems()).includes("CLEAR STAGE"), "... on + NEW LOOP, the stage empty and on: dark; SEL: no CLEAR STAGE");
        U.flash[B.EDIT] = 0;
        await t.tap("EDIT");
        ok(U.flash[B.EDIT] > U.now() && !staged() && !stg().off, "EDIT on + NEW LOOP, the stage empty and on: EDIT blinks");
        await t.tap("ARP");
        U.flash[B.EDIT] = 0;
        t.frame();
        ok(stg().off && t.leds[B.EDIT] === .25, "... the stage off: EDIT's LED dim");
        await t.tap("EDIT");
        ok(!stg().off && !staged() && U.toastNow() === "STAGE ON", "... its tap, the stage empty and off: it just turns on (STAGE ON)");
        await pick(0); await t.tap("EDIT"); await pick(2);
        t.frame();
        ok(staged() && t.leds[B.EDIT] === .25, "... the stage holding notes: EDIT's LED dim");
        await t.tap("EDIT");
        ok(!staged() && U.toastNow() === "STAGE CLEARED" && !stg().off && LS().list.length === 2, "EDIT on + NEW LOOP: the stage cleared (STAGE CLEARED)");
        await pick(0); await t.tap("EDIT"); await pick(2); await t.tap("ARP");
        const off1 = stg().off && staged();
        await t.tap("EDIT");
        ok(off1 && !staged() && !stg().off && U.toastNow() === "STAGE CLEARED", "... with the stage off: cleared, and on");
        /* EDIT held */
        await pick(0);
        await t.tap("EDIT");
        const fill = staged();
        await edHeld();
        ok(fill && !staged() && U.toastNow() === "STAGE CLEARED" && !t.top(), "EDIT held: the stage cleared (STAGE CLEARED); its tap does not fire after it (the loop not put on the stage)");
        U.flash[B.EDIT] = 0;
        t.down("EDIT"); await t.sleep(U.LONG_HOLD + 100);
        const blink = U.flash[B.EDIT] > U.now();
        t.up("EDIT"); await t.sleep(60);
        ok(blink && !staged(), "... the stage empty and on: EDIT blinks, nothing filled");
        t.down("EDIT"); await t.sleep(U.LONG_HOLD - 200);
        const slow = !staged();
        t.up("EDIT"); await t.sleep(150);
        ok(slow && staged() && U.toastNow() === "LOOP 00 ON THE STAGE", "... let go before 2 s, past HOLD DELAY: EDIT's tap, the loop on the stage");
        t.down("EDIT"); await t.sleep(U.LONG_HOLD - 200);
        const kept = staged();
        t.up("EDIT"); await t.sleep(150);
        ok(kept && staged() && U.toastNow() === "LOOP 00 ON THE STAGE", "... the stage holding notes: no clear before 2 s, EDIT's tap again");
        await t.tap("EDIT");
        ok(staged() && U.toastNow() === "LOOP 00 ON THE STAGE", "EDIT's tap still fills the stage on a loop");
        await t.tap("ARP");
        const off2 = stg().off && staged();
        await edHeld();
        ok(off2 && !staged() && !stg().off && U.toastNow() === "STAGE CLEARED", "EDIT held with the stage off: cleared, and on");
        await t.tap("ARP");
        const off3 = stg().off && !staged();
        await edHeld();
        ok(off3 && !staged() && !stg().off && U.toastNow() === "STAGE ON", "... empty and off: it just turns on (STAGE ON)");
        await t.tap("EDIT");
        await pick(2);
        await edHeld();
        ok(!staged() && U.toastNow() === "STAGE CLEARED", "... on + NEW LOOP too");
        /* nothing in ADJUST or the pattern recorder */
        await pick(0);
        await t.tap("EDIT");
        await menuPick("ADJUST");
        await edHeld();
        ok(staged() && t.top() === "adjust", "ADJUST: EDIT held: nothing");
        await t.tap("HOME");
        await t.sleep(200);
        await openRec();
        const pr1 = t.top() === "recorder";
        await edHeld();
        ok(pr1 && t.top() === "recorder" && staged(), "the pattern recorder: EDIT held: nothing");
        await t.tap("HOME");
        /* SEL's CLEAR STAGE, listed only while the stage holds notes */
        {
            const it = await menuItems();
            ok(it.includes("CLEAR STAGE") && it.indexOf("CLEAR STAGE") === it.indexOf("CLEAR LOOPS") - 1, "SEL, the stage holding notes: CLEAR STAGE, beside CLEAR LOOPS");
        }
        await menuPick("CLEAR STAGE");
        ok(!staged() && !t.top() && U.toastNow() === "STAGE CLEARED" && LS().list.length === 2, "... at once, no question: the stage cleared (STAGE CLEARED), the loops kept");
        ok(!(await menuItems()).includes("CLEAR STAGE"), "... the stage empty: not listed");
        await t.tap("EDIT");
        await t.tap("ARP");
        const off4 = stg().off && staged();
        await menuPick("CLEAR STAGE");
        ok(off4 && !staged() && !stg().off, "... with the stage off: cleared, and on");
        /* OVERWRITE stays through REC's send, a clear and a move of the selection; EDIT on a loop ends it */
        await t.tap("EDIT");
        await t.tap("LFO");
        const on1 = stg().over;
        await t.sleep(600);
        await t.tap("REC");
        ok(on1 && !staged() && U.toastNow() === "STAGE INTO LOOP 00" && stg().over, "OVERWRITE on: REC's send leaves it");
        onStage();
        await edHeld();
        ok(!staged() && stg().over, "... a clear leaves it");
        await pick(2);
        const moved = stg().over;
        await pick(1);
        t.frame();
        ok(moved && stg().over && titleOf() === "STAGE OVERWRITE LFO", "... a move of the selection leaves it");
        await t.tap("EDIT");
        t.frame();
        ok(staged() && !stg().over && titleOf() === "STAGE OVERDUB LFO" && t.leds[B.LFO] === .25 && U.toastNow() === "LOOP 01 ON THE STAGE",
           "EDIT on a loop: OVERDUB (the title, LFO dim), its toast its own");
        {
            const was = sNotes();
            P.kept.loops.row = 0; await t.tap("PLAY");
            for (let n = 0; n < 150; n++) { t.frame(); await t.sleep(20); }
            ok(sNotes() === was && playingNow(), "... its notes go through the line: a pass and more, as they were (" + sNotes() + ")");
            await t.tap("LFO");                             /* (OVERWRITE at work: EDIT's fill ends its eraser with the tap, no frame between) */
            t.frame();
            const running = stg().erasers.length;
            await t.tap("EDIT");
            ok(running === 1 && !stg().over && !stg().erasers.length, "... EDIT with the eraser at work: it ends at once, as in the firmware");
            t.frame();
        }
        await t.tap("LFO");                                 /* (OVERWRITE: the recorder's fill ends it) */
        const on2 = stg().over;
        await stopAll();
        await openRec();
        {
            const rp = U.screen.recorder();
            for (const r of rp.rows) r.steps.fill(false);
            for (const k of [0, 4, 8, 12]) rp.rows[0].steps[k] = true;
            rp.sel = 0; rp.len = 16;
        }
        await t.tap("REC");
        t.frame();
        ok(on2 && U.toastNow() === "PATTERN ON THE STAGE" && staged() && !stg().over && titleOf() === "STAGE OVERDUB LFO", "the pattern recorder's REC: OVERDUB too");
        await stopAll();                                    /* (its preview alone: the loops go on from it) */
        {
            const was = sNotes();
            P.kept.loops.row = 0; await t.tap("PLAY");
            for (let n = 0; n < 150; n++) { t.frame(); await t.sleep(20); }
            ok(sNotes() === was && playingNow(), "... its notes go through the line too");
        }
        await stopAll();
        /* ADJUST's title: the mode, no LFO */
        await menuPick("ADJUST");
        ok(titleOf() === "STAGE OVERDUB", "ADJUST's title: the mode, no dim LFO (" + titleOf() + ")");
        await t.tap("HOME");
        await t.sleep(600);
        /* REC with LFO held: plain REC, nothing cleared; LFO's tap its own */
        const n0 = LS().list.length;
        t.down("LFO"); await t.sleep(30); t.down("REC"); await t.sleep(U.set("HOLD DELAY") + 100); t.up("REC"); await t.sleep(60);
        ok(!staged() && LS().list.length === n0 && U.toastNow() === "STAGE INTO LOOP 01" && notesAt(1).join() !== "0:0:60,4:0:64,8:0:67",
           "LFO held + REC held: plain REC, sent at its press, no clear (" + U.toastNow() + ")");
        t.up("LFO"); await t.sleep(60);
        await t.tap("LFO");
        await t.sleep(600);
        onStage();
        await stageOn();
        /* ARP held + REC: the recorder, however long REC is held; nothing cleared, no switch */
        t.down("ARP"); await t.sleep(30); t.down("REC"); await t.sleep(U.set("HOLD DELAY") * 2);
        const heldOn = staged() && U.toastNow() !== "STAGE CLEARED";
        t.up("REC"); await t.sleep(30); t.up("ARP"); await t.sleep(200);
        ok(heldOn && t.top() === "recorder" && staged() && !stg().off, "ARP held + REC held long: the recorder at REC's release, the stage kept, no stage switch");
        await t.tap("HOME");
        await t.sleep(600);
        await pick(0);
        emptyStage();
    }

    /* ---- REC's 500 ms: for 500 ms after REC does a job it does nothing (no blink, no toast), LFO held or not; EDIT held
     * (the stage's clear) is outside it; a refused REC starts no wait; RECORD LIVE armed from its panel starts it (the
     * pattern recorder's REC: below). The time since the job stood in for where it must be exact (sc().recAt) ---- */
    {
        setLoops([], 16);
        await pick(0);
        await stageOn();
        await t.sleep(600);
        await t.tap("REC");
        const armed = !!sc().arm;
        U.flash[B.REC] = 0;
        await t.tap("REC");                                 /* (180 ms after it) */
        ok(armed && !!sc().arm && U.toastNow() !== "CANCELLED" && !(U.flash[B.REC] > U.now()),
           "REC's 500 ms: a double tap does one job (armed once; the second tap: nothing, no blink, no toast)");
        await t.sleep(400);
        await t.tap("REC");
        ok(!sc().arm && U.toastNow() === "CANCELLED", "... past 500 ms: its job (disarmed: CANCELLED)");
        await t.sleep(600);
        const s = stg();
        s.cells = new Array(64).fill(null); s.cells[8] = M.newCell(60, null, 100); M.version++;
        await t.tap("REC");
        const sent = LS().list.length === 1 && !staged() && U.toastNow() === "STAGE AS LOOP 00";
        sc().recAt = U.now() - 499;
        await t.tap("REC");
        const quiet = stg().off;
        sc().recAt = U.now() - 500;
        await t.tap("REC");
        ok(sent && quiet && !stg().off && U.toastNow() === "STAGE ON: ARMED" && sc().arm,
           "... REC sent the stage, leaving it off; at 499 ms REC does nothing, at 500 ms its job (the stage on and, stopped, armed)");
        s.cells = new Array(64).fill(null); s.cells[8] = M.newCell(60, null, 100); M.version++;
        sc().recAt = U.now();                               /* (a job just done) */
        U.flash[B.REC] = 0;
        t.down("LFO"); await t.sleep(30); await t.tap("REC"); t.up("LFO"); await t.sleep(30);
        ok(staged() && !(U.flash[B.REC] > U.now()), "... REC with LFO held inside it: plain REC, nothing");
        await t.tap("LFO");
        sc().recAt = U.now();
        t.down("EDIT"); await t.sleep(U.LONG_HOLD + 60); t.up("EDIT"); await t.sleep(30);
        ok(!staged() && U.toastNow() === "STAGE CLEARED", "... EDIT held from inside it clears the stage (a hold of its own, outside the wait)");
        await t.sleep(600);
        await t.tap("PLAY"); await t.sleep(100);
        await t.tap("REC");
        const r1 = U.toastNow() === "THE STAGE IS EMPTY", n1 = LS().list.length;
        s.cells = new Array(64).fill(null); s.cells[8] = M.newCell(60, null, 100); M.version++;
        await t.tap("REC");
        ok(r1 && LS().list.length === n1 + 1 && U.toastNow() === "STAGE AS LOOP " + M.hex(n1),
           "... a refused REC starts no wait (THE STAGE IS EMPTY, then REC at once: STAGE AS LOOP " + M.hex(n1) + ")");
        await stopAll();
        await t.sleep(600);
        const songs0 = M.state.songs.slice(), pats0 = M.state.patterns.slice();
        await menuPick("RECORD LIVE"); await t.tap("REC");
        const songArmed = sc().song && sc().song.state === "armed";
        await t.tap("REC");
        const notStarted = sc().song && sc().song.state === "armed";
        await t.sleep(400);
        await t.tap("REC");
        ok(songArmed && notStarted && sc().song && sc().song.state === "on", "... RECORD LIVE armed from its panel starts it: REC at once "
           + "nothing, past 500 ms it starts the song");
        await t.sleep(400);
        await t.tap("HOME");
        await stopAll();
        ok(!sc().song, "... (HOME ends it)");
        M.state.songs = songs0; M.state.patterns = pats0; M.settle(); M.version++;   /* (the song made here gone) */
    }

    /* ---- REC on a loop: its notes replaced by the stage ---- */
    setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) }), mkLoop({ inst: 0xF001, col: 3, repeats: 4, shift: 4, cells: cs(16, [[0, 0, 64]]) })], 64);
    U.edit(() => { LS().mixer[1].lvl = 0x40; });
    await pick(1);
    await t.tap("EDIT");
    await slow();
    press(5, 30, .1); lift(5, 31, .1);
    {
        const before = sNotes();
        U.setInst(dx7);
        U.edit(() => { LS().nextMono = true; });
        t.frame();
        ok(sNotes() === before && staged(), "a new instrument or mode: the stage keeps its notes (played in the new sound)");
    }
    t.down("REC"); await t.sleep(30);
    {
        const L1 = LS().list[1];
        ok(L1.inst === dx7 && L1.mono && L1.lanes === 1 && !L1.shift && L1.base === 64 && L1.repeats === 1 && L1.col === 3 && LS().mixer[1].lvl === 0x40 &&
           notesAt(1).join() === ["4:0:64", "20:0:64", "30:0:" + U.midi(5), "36:0:64", "52:0:64"].join() && LS().list.length === 2 && sc().sel === 1 &&
           !staged() && U.toastNow() === "STAGE INTO LOOP 01",
           "REC (at its press) on a loop: its notes the stage's (STAGE INTO LOOP 01), the keys' instrument and mode written (MONO: one lane), " +
           "SHIFT back to 0; its place, colour and mix kept; the stage emptied");
    }
    t.up("REC"); await t.sleep(100);
    await stopAll();

    /* ---- the selected loop silent while the stage holds notes; the stage kept through moves ---- */
    {
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, 62], [8, 0, 64], [12, 0, 65]]) }),
                  mkLoop({ col: 1, cells: cs(16, [[2, 0, 67], [6, 0, 69], [10, 0, 71], [14, 0, 72]]) })], 16);
        U.edit(() => { LS().tempo = 300; });
        await pick(0);
        await t.tap("EDIT");
        const heard = async ms => { const ids = [], on0 = A.noteOn; A.noteOn = (id, ...a) => { ids.push(id); return on0(id, ...a); }; await t.sleep(ms); A.noteOn = on0; return ids; };
        const has = (ids, re) => ids.some(i => re.test(i));
        P.kept.loops.row = 0; await t.tap("PLAY");
        let ids = await heard(900);
        ok(!has(ids, /^s16\./) && has(ids, /^g32\./) && stageCol === 16 && has(ids, /^s17\./), "the stage holding notes: the selected loop silent, the stage " +
           "heard in its place (its column carrying the loop's mix), the other loops heard");
        const was = JSON.stringify(stg().cells);
        await t.turn("SELECT", 1);
        ids = await heard(900);
        ok(JSON.stringify(stg().cells) === was && has(ids, /^s16\./) && !has(ids, /^s17\./) && has(ids, /^g32\./) && stageCol === 17 && P.lpSilent(1),
           "SELECT to loop 01: the stage stays (through loop 01's mix now); loop 01 silent now, loop 00 heard again");
        await t.turn("SELECT", 1);
        ids = await heard(600);
        ok(JSON.stringify(stg().cells) === was && has(ids, /^s16\./) && has(ids, /^s17\./) && !P.lpSilent(0) && !P.lpSilent(1),
           "... on + NEW LOOP: no loop silent, the stage kept");
        await t.turn("SELECT", -1);
        await t.tap("REC");
        ok(notesAt(1).join() === "0:0:60,4:0:62,8:0:64,12:0:65" && notesAt(0).join() === "0:0:60,4:0:62,8:0:64,12:0:65" && LS().list.length === 2 &&
           U.toastNow() === "STAGE INTO LOOP 01", "REC: the stage goes where the selection is (loop 00's notes, copied by EDIT, into loop 01)");
        await stopAll();
    }

    /* ---- a key held across the line: its note ends there, written once; the key forgotten (the stage plays its note again
     * where it was written, the next pass writing nothing for it; its release does nothing), POLY and MONO alike ---- */
    for (const mono of [false, true]) {
        setLoops([], 16);
        U.edit(() => { LS().tempo = 300; LS().nextMono = mono; });
        await pick(0);
        await stageOn();
        const heardN = [], on0 = A.noteOn, cellsNow = () => (stg().cells || []).map(c => c ? (c.note == null ? "-" : c.note) + c.cmds.map(x => x.c).join("") : "").join();
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.pass === 0 && h.row >= 8; }, 3000);
        A.noteOn = (id, time, midi, ...a) => { heardN.push({ id, midi }); return on0(id, time, midi, ...a); };
        t.keyDown(9);
        await until(() => { const h = realAt(); return !!h && h.pass >= 1 && h.row >= 13; }, 3000);
        const before = cellsNow(), writing = Object.values(stg().keys).some(k => k.rec);
        t.keyUp(9); await t.sleep(40);
        A.noteOn = on0;
        const after = cellsNow();
        await stopAll();
        const m = U.midi(9), at = (stg().cells || []).map((c, k) => isNote(c) && c.note === m ? k >> 2 : -1).filter(r => r >= 0);
        const live = heardN.filter(o => /k9$/.test(o.id)).length, fromStage = heardN.some(o => /^g32\./.test(o.id) && o.midi === m);
        ok(at.length === 1 && at[0] >= 8 && isOff(sCell(0)) && !writing && after === before && live === 1 && fromStage,
           "a key held across the line (" + (mono ? "MONO" : "POLY") + "): its note ends there (a note off at row 0), written once (on the stage at row " +
           at.join(", ") + "); the key forgotten: heard again from the stage where it was written (" + live + " live, from the stage " + fromStage +
           "), its release writing nothing");
    }
    U.edit(() => { LS().nextMono = false; });

    /* ---- ALGORITHM while the loops play: a jump, at once ---- */
    {
        setLoops([], 64);
        U.edit(() => { LS().tempo = 120; });
        const rows = [], hook = P.loopsRow;
        P.loopsRow = function (row) { rows.push(row); return hook.apply(this, arguments); };
        P.kept.loops.row = 0; await t.tap("PLAY"); await t.sleep(400);
        t.down("SEL"); await t.sleep(30); await t.turn("ALGORITHM", 1); t.up("SEL"); await t.sleep(400);
        P.loopsRow = hook;
        await stopAll();
        ok(rows.some((r, k) => k && (r - rows[k - 1] + 64) % 64 === 17) && !t.top(), "ALGORITHM while they play (SEL held): the loops a bar on at once");
    }

    /* ---- K1-K4: each the job GLO gives it ---- */
    setLoops([mkLoop({}), mkLoop({ col: 1 })], 64);
    await pick(0);
    {
        const tempo0 = LS().tempo;
        await t.turn("K1", -2); await t.turn("K2", 3); await t.turn("K3", 4); await t.turn("K4", 2);
        ok(LS().mixer[0].lvl === 125 && LS().mixer[0].pan === 131 && LS().mixer[0].rev === 4 && LS().tempo === tempo0 + 2 && LS().mixer[1].lvl === 127,
           "K1-K4 at first: the selected loop's LEVEL, PAN, REVERB SEND, and the loops' TEMPO");
        t.down("SEL"); await t.sleep(30); await t.turn("K1", -1); t.up("SEL"); await t.sleep(250);
        ok(LS().mixer[0].lvl === 109 && !t.top(), "... SEL held: by 16");
        await pick(2);
        await t.turn("K1", -1);
        ok(LS().nextMix.lvl === 126 && LS().mixer[2].lvl === 126 && LS().mixer[1].lvl === 127, "... on + NEW LOOP: the next loop's mix");
        {   /* the next loop's sends are the KEYS column's: heard so (the worklet told at once), K3's REVERB SEND on + NEW LOOP
             * turns the KEYS column's (a setting), REC writes them into the new loop; its own sends kept, not read */
            const km0 = U.set("KEYS MIX"), fx0 = A.fxNow;
            let told = 0;
            A.fxNow = function () { told++; return fx0.apply(this, arguments); };
            U.edit(() => { LS().nextMix.rev = 5; LS().nextMix.cho = 6; });
            U.changeSetting("KEYS MIX", { ...km0, cho: 0x11, dly: 0x22, rev: 0x33 });
            const heardMix = M.loopsMixer(LS())[2], told1 = told;
            ok(heardMix.lvl === 126 && heardMix.cho === 0x11 && heardMix.dly === 0x22 && heardMix.rev === 0x33 && M.loopsMixer(LS())[1] === LS().mixer[1] && told1 > 0,
               "... the next loop's mix as heard (the stage on + NEW LOOP, the preview): its LEVEL its own, its sends the KEYS column's (" +
               JSON.stringify(heardMix) + "); a change of the KEYS sends told to the sound at once (" + told1 + ")");
            const loops0 = JSON.stringify(LS());
            await t.turn("K3", 1);
            ok(U.set("KEYS MIX").rev === 0x34 && JSON.stringify(LS()) === loops0 && M.unsavedThing("settings") && U.knobNow() === "REVERB SEND 34 41%" && told > told1,
               "... K3 (REVERB SEND) on + NEW LOOP: the KEYS column's REVERB SEND turned, a setting (the loops unchanged, the next loop's own " +
               "send kept); the header as ever (" + U.knobNow() + ")");
            A.fxNow = fx0;
            await stageOn();
            stg().cells = cs(64, [[0, 0, 60], [4, 0, M.OFF]]); M.version++;
            await t.sleep(600);
            await t.tap("REC");
            const nl = LS().mixer[2];
            ok(LS().list.length === 3 && nl.lvl === 126 && nl.cho === 0x11 && nl.dly === 0x22 && nl.rev === 0x34 && LS().nextMix.rev === 5 && LS().nextMix.cho === 6 &&
               U.toastNow() === "STAGE AS LOOP 02", "... REC on + NEW LOOP: the new loop's mix the next loop's LEVEL, PAN and DUCK, the KEYS column's sends " +
               "(" + JSON.stringify(nl) + "); the next loop's own sends kept as written");
            U.changeSetting("KEYS MIX", km0);
            U.edit(() => { LS().nextMix.rev = 0; LS().nextMix.cho = 0; });
            setLoops(LS().list.slice(0, 2), 64);
            await pick(2);
            await stageOn();                                /* (REC's send left it off) */
            await t.sleep(600);
        }
        await t.tap("GLO");
        const pp = U.top();
        ok(pp.items.map(i => i.name).join() === "TEMPO,SWING,LENGTH,GRID,QUANTIZE,MONO LEGATO,COUNT-IN,VIEW,K1 TURNS,K2 TURNS,K3 TURNS,K4 TURNS" &&
           pp.title === "LOOPS PROPERTIES", "GLO: LOOPS PROPERTIES: TEMPO, SWING, LENGTH, GRID, QUANTIZE, MONO LEGATO, COUNT-IN, VIEW, K1-K4 TURNS");
        pp.sel = 8; await t.turn("K1", 7);
        ok(LS().knobs[0] === "VIEW" && pp.items[8].get() === "VIEW", "GLO's K1 TURNS: a job from the list (LEVEL ... TEMPO, VIEW)");
        await t.tap("GLO");
        U.edit(() => { LS().view = 2; });
        await t.turn("K1", 1);
        const v4 = LS().view;
        await t.turn("K1", -2);
        ok(v4 === 4 && LS().view === 1, "... K1 then turns VIEW (2 bars, 4, then 1)");
        U.edit(() => { LS().knobs[0] = "LEVEL"; LS().nextMix.lvl = 127; });
        setLoops(LS().list, 64);
    }

    /* ---- VIEW: the bars a screen (the length's bar: the bars in view light) ---- */
    {
        P.kept.loops.row = 0;
        const lit = c565(D.LGREY), dim = c565(D.DEEP + D.LGREY);
        /* GLO's VIEW turned d, GLO closed: the length's bar's 4 bars, lit or dim, and VIEW's words */
        const view = async d => {
            await t.tap("GLO"); U.top().sel = 7; await t.turn("K1", d);
            const words = U.top().items[7].get();
            await t.tap("GLO"); t.frame();
            return [0, 1, 2, 3].map(b => t.px(36 + 45 * b, 205) === lit ? "L" : t.px(36 + 45 * b, 205) === dim ? "d" : "?").join("") + " " + words;
        };
        const one = await view(-1), two = await view(1), four = await view(1);
        await view(-1);
        ok(one === "Lddd 1 BAR A SCREEN" && two === "LLdd 2 BARS A SCREEN" && four === "LLLL 4 BARS A SCREEN" && LS().view === 2,
           "GLO's VIEW: 1, 2 or 4 bars a screen (the length's bar lights the bars in view: " + [one, two, four].join(", ") + ")");
    }

    /* ---- LENGTH: longer at once, shorter only planned while GLO is open ---- */
    setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) }), mkLoop({ base: 64, col: 1, cells: cs(64, [[40, 0, 62]]) })], 64);
    {
        await t.tap("GLO");
        const pp = U.top();
        pp.sel = 2; await t.turn("K1", 1);
        ok(LS().rows === 80 && pp.items[2].get() === "5 BARS", "LENGTH longer: at once (5 BARS), silence added");
        await t.turn("K1", -3);
        ok(LS().rows === 80 && pp.items[2].get() === "2 BARS (NOW 5 BARS)" && t.top() === "props", "LENGTH shorter: only planned while GLO is open (2 BARS (NOW 5 BARS))");
        await t.tap("GLO");
        ok(t.top() === "question" && U.top().lines.join("/") === "CUT TO 2 BARS?/NOTES IN THE BARS CUT GO" && U.top().answers.join() === "CUT,KEEP" && LS().rows === 80,
           "... GLO closed: one question, as notes would go (CUT TO 2 BARS? / NOTES IN THE BARS CUT GO)");
        await t.tap("OCT-");
        ok(LS().rows === 80 && !t.top() && notesAt(1).join() === "40:0:62", "... KEEP: the length as it was");
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -3); await t.tap("HOME");
        const asked = t.top() === "question";
        await t.tap("OCT+");
        ok(asked && LS().rows === 32 && LS().list[1].base === 32 && !notesAt(1).length && notesAt(0).join() === "0:0:60",
           "... HOME closing GLO asks too; CUT: 2 bars, each loop cut there");
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -1); await t.tap("GLO");
        ok(LS().rows === 16 && !t.top(), "shorter with no note past it: cut as GLO closes, no question");
        await t.tap("GLO"); U.top().sel = 3; await t.turn("K1", 1);
        ok(U.toastNow() === "GRID: ONLY WITH NO LOOPS" && LS().rpb === 4, "GRID with loops: refused (GRID: ONLY WITH NO LOOPS)");
        await t.tap("GLO");
    }

    /* ---- SEL's menu: DOUBLE only when it can be done; QUANTIZE; ADJUST; DELETE LOOP; the instruments ---- */
    setLoops([], 64);
    ok(!(await menuItems()).includes("DOUBLE"), "SEL with no loops: no DOUBLE");
    setLoops([mkLoop({})], 64);
    await pick(0);
    {
        const it = await menuItems();
        ok(it.join() === "TAKE ITS INSTRUMENT,APPLY INSTRUMENT,ADJUST,QUANTIZE,DELETE LOOP,DOUBLE,RECORD PATTERN,RECORD LIVE,SAVE TO PATTERN,LOAD FROM PATTERN," +
           "CLEAR LOOPS,REVERT", "SEL on a loop: TAKE ITS INSTRUMENT, APPLY INSTRUMENT, ADJUST, QUANTIZE, DELETE LOOP, DOUBLE, RECORD PATTERN, RECORD LIVE, " +
           "SAVE TO / LOAD FROM PATTERN, CLEAR LOOPS, REVERT (" + it.join() + ")");
    }
    setLoops([mkLoop({ base: 128 })], 128);
    ok(!(await menuItems()).includes("DOUBLE"), "... 8 bars long already: no DOUBLE");
    {   /* QUANTIZE */
        const qc = cs(16, [[1, 0, 60], [3, 0, M.OFF], [5, 0, 62]]);
        qc[5 * 4].cmds.push({ c: 0x0E, v: 0xD3 });
        setLoops([mkLoop({ cells: qc })], 64);
        await menuPick("QUANTIZE");
        ok(t.top() === "menu" && U.top().items.join() === "2 ROWS,3 ROWS,4 ROWS,ROW", "QUANTIZE: a size from a list (2 ROWS, 3 ROWS, 4 ROWS, ROW)");
        U.top().sel = 2; await t.tap("SEL");
        ok(notesAt(0).join() === "0:0:60,4:0:62" && isOff(cell(0, 2)) && !cell(0, 4).cmds.length && U.toastNow() === "LOOP 00 QUANTIZED: 4 ROWS",
           "... 4 ROWS: each note to the nearest line (1 on 0, 5.5 on 4), its note off moved with it, the note delay gone (LOOP 00 QUANTIZED: 4 ROWS)");
    }
    {   /* ADJUST */
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 64);
        await menuPick("ADJUST");
        t.frame();
        ok(t.top() === "adjust" && U.screen.sub() === "ADJUST", "ADJUST: open (ADJUST in the header)");
        await t.turn("K1", 1); await t.turn("K2", 1);
        ok(LS().list[0].shift === 17 && notesAt(0).join() === "0:0:60", "... K1 a row, K2 a bar: SHIFT 17, no note moved");
        await t.tap("SEL");
        ok(!t.top() && LS().list[0].shift === 17 && U.toastNow() === "LOOP 00: SHIFT +17 ROWS", "... SEL keeps it (LOOP 00: SHIFT +17 ROWS)");
        await menuPick("ADJUST"); await t.turn("K1", -3); await t.tap("HOME");
        ok(!t.top() && LS().list[0].shift === 17 && U.toastNow() === "CANCELLED", "... HOME cancels: the SHIFT as it was");
    }
    {   /* DELETE LOOP */
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) }), mkLoop({ col: 1, cells: cs(16, [[0, 0, 62]]) }), mkLoop({ col: 2, cells: cs(16, [[0, 0, 64]]) })], 16);
        U.edit(() => { LS().mixer[2].lvl = 0x30; });
        await pick(1);
        await menuPick("DELETE LOOP");
        ok(LS().list.length === 2 && LS().list[1].col === 2 && LS().mixer[1].lvl === 0x30 && sc().sel === 1 && LS().rows === 16 && !t.top() &&
           U.toastNow() === "LOOP 01 DELETED", "DELETE LOOP: at once (LOOP 01 DELETED); the loops after it move up with their mixes, the selection on " +
           "the one in its place; the length stays");
    }
    {   /* the instruments: ENV held, TAKE ITS INSTRUMENT, APPLY INSTRUMENT */
        setLoops([mkLoop({ inst: 0xF001, mono: true })], 16);
        U.setInst(dx7);
        U.edit(() => { LS().nextMono = false; });
        await pick(0);
        await hold("ENV");
        ok(U.inst === 0xF001 && LS().nextMono && !t.top() && (U.toastNow() || "").endsWith(", MONO"),
           "ENV held (the pipette): the loop's instrument and MONO / POLY become the keys'");
        U.setInst(dx7);
        U.edit(() => { LS().nextMono = false; });
        await menuPick("TAKE ITS INSTRUMENT");
        ok(U.inst === 0xF001 && LS().nextMono, "SEL's TAKE ITS INSTRUMENT: the same");
        U.setInst(dx7);
        U.edit(() => { LS().nextMono = false; });
        await menuPick("APPLY INSTRUMENT");
        ok(LS().list[0].inst === dx7 && !LS().list[0].mono && (U.toastNow() || "").endsWith(", POLY"), "SEL's APPLY INSTRUMENT: the keys' instrument and MONO / POLY onto the loop");
    }

    /* ---- the overview column: a cell a loop, 16 in all ---- */
    {
        setLoops([mkLoop({}), mkLoop({ col: 1, muted: true }), mkLoop({ col: 2 })], 16);
        await pick(2);
        t.frame();
        const y = i => 33 + i * 8;
        ok(t.px(233, y(0) + 3) === c565(M.LOOP_COLS[0]) && t.px(233, y(1) + 3) === c565(D.DGREY), "the overview column: a cell a loop in its colour (a muted one grey)");
        ok(t.px(232, y(2) + 1) === c565(D.WHITE) && t.px(231, y(2) + 2) === c565(D.WHITE) && t.px(231, y(2) + 1) === c565(D.BLACK) &&
           t.px(233, y(2) + 3) === c565(M.LOOP_COLS[2]) && t.px(232, y(2) + 3) === c565(D.BLACK),
           "... the selection a small circle: a white ring round black, its loop's colour a dot in the middle");
        ok(t.px(233, y(3) + 1) === c565(D.DGREY) && t.px(231, y(3) + 3) === c565(D.DGREY) && t.px(231, y(3) + 1) === c565(D.BLACK),
           "... + NEW LOOP a grey plus");
        ok(t.px(233, y(4) + 3) === c565(D.BLACK) && t.px(233, y(15) + 3) === c565(D.BLACK), "... the cells after it empty, to the 16th");
        ok(t.px(0, 185) === c565(D.DGREY) && t.px(239, 185) === c565(D.DGREY) && t.px(120, 196) === c565(D.DGREY) && t.px(2, 185) === c565(D.BLACK) &&
           t.px(120, 195) === c565(D.BLACK), "the stage drawn in a dark grey frame, black inside it");
        await pick(3);
        t.frame();
        ok(t.px(233, y(3) + 3) === c565(D.LGREY) && t.px(232, y(3) + 1) === c565(D.WHITE), "... the selection on + NEW LOOP: the circle in its place, a light grey dot");
    }

    /* ---- each loop's LEVEL on its slot: hex, FONT T, right-aligned to 226 at the strip's row (y + 14), light grey (a
     * muted or silenced loop's dark grey); + NEW LOOP's the next loop's ---- */
    {
        setLoops([mkLoop({}), mkLoop({ col: 1, muted: true }), mkLoop({ col: 2 })], 16);
        U.edit(() => { LS().mixer[2].lvl = 0x40; LS().nextMix.lvl = 0x55; });
        await pick(0);
        t.frame();
        /* slot k's level: its pixels of colour c in x 208-226 (past the strip; the slot's box at 227 aside), y + 14 to y + 26
         * (x from, x to, how many); its pattern at 214-225 */
        const lev = (k, c) => { const y0 = 33 + 32 * k + 14, xs = [];
                                for (let y = y0; y < y0 + 12; y++) for (let x = 208; x < 227; x++) if (t.px(x, y) === c565(c)) xs.push(x);
                                return { a: Math.min(...xs), b: Math.max(...xs), n: xs.length }; };
        const glyphs = k => { let s = ""; for (let y = 33 + 32 * k + 14; y < 33 + 32 * k + 26; y++) for (let x = 214; x < 226; x++) s += t.px(x, y) === c565(D.BLACK) ? "." : "#"; return s; };
        const l0 = lev(0, D.LGREY), l1 = lev(1, D.LGREY), d1 = lev(1, D.DGREY), l2 = lev(2, D.LGREY), l3 = lev(3, D.LGREY);
        ok(l0.n && l0.a >= 214 && l0.b <= 225 && !l1.n && d1.n && d1.a >= 214 && d1.b <= 225 && l2.n && l3.n && l3.a >= 214 && l3.b <= 225 &&
           glyphs(0) === glyphs(1) && glyphs(0) !== glyphs(2) && glyphs(2) !== glyphs(3),
           "each loop's LEVEL on its slot, at 214-225 on the strip's row: 7F light grey, a muted loop's dark grey, 40; + NEW LOOP the next " +
           "loop's (55) (" + [l0, d1, l2, l3].map(o => o.a + "-" + o.b).join(", ") + ")");
        U.edit(() => { LS().nextMix.lvl = 127; });
    }

    /* ---- the lanes: repeats, the rest, a repeat cut at the length's end, a SHIFT ---- */
    {
        const c4 = o => { const a = new Array(o.base * 4).fill(null); for (const [r, n] of o.notes) a[r * 4] = M.newCell(n, null, 100); return a; };
        setLoops([mkLoop({ base: 4, repeats: 3, cells: c4({ base: 4, notes: [[0, 60], [2, 62]] }) }),
                  mkLoop({ base: 6, repeats: 3, cells: c4({ base: 6, notes: [[0, 64], [4, 65]] }) }),
                  mkLoop({ shift: 3, cells: c4({ base: 16, notes: [[0, 67]] }) })], 16);
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
        const rest = rows.find(r => r.pass === 0 && r.row === 12);
        ok(rest && calls.some(c => !c.on && Math.abs(c.time - rest.time) < 1e-6 && /^s16\.0\./.test(c.id)), "... its rest ends its note (row 12)");
        ok(starts(17) === "0:64,4:65,6:64,10:65,12:64", "a repeat cut at the length's end: base 6 x3 over 16 rows (" + starts(17) + ")");
        const wrap = rows.find(r => r.pass === 1 && r.row === 0);
        ok(wrap && calls.some(c => !c.on && Math.abs(c.time - wrap.time) < 1e-6 && /^s17\.0\./.test(c.id)), "... cut there: its note ends at the length's end" +
           (wrap ? "" : " (no wrap: " + rows.length + " rows)"));
        ok(starts(18) === "3:67", "a SHIFT of 3: the loop plays 3 rows later (" + starts(18) + ")");
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- mutes and solos: CONTROL mode (SEQ), now and at the bar; SEQ held; HOME's combination ---- */
    setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) }), mkLoop({ cells: cs(16, [[8, 0, 64]]), col: 1 })], 16);
    await pick(0);
    {
        const mutes = {}, m0 = A.trackMute;
        A.trackMute = (track, on) => { mutes[track] = on; return m0(track, on); };
        await t.tap("SEQ");
        t.frame();
        ok(sc().control && U.toastNow() === "KEYS: CONTROL" && t.leds[B.SEQ] === 1, "SEQ: the keys CONTROL the loops (KEYS: CONTROL), SEQ full");
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
        ok(LS().nextMono && U.toastNow() === "KEYS: MONO" && U.screen.footer()[0] === "MONO", "MONO: the keys MONO (KEYS: MONO; the footer says so)");
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
        t.down("SEQ"); await t.sleep(30); await t.key(U.WHITE[1]); t.up("SEQ"); await t.sleep(200);
        ok(LS().list[1].muted && !sc().control && !staged() && U.toastNow() === "LOOP 01 MUTED", "SEQ held + a white key: as in CONTROL mode, once (LOOP 01 MUTED); " +
           "the keys stay in PLAY, nothing played");
        t.down("SEQ"); await t.sleep(30); await t.key(U.BLACK[8]); t.up("SEQ"); await t.sleep(200);
        ok(LS().nextMono && !sc().control, "SEQ held + MONO: the keys MONO");
        U.edit(() => { LS().nextMono = false; LS().list[1].muted = false; });
        await t.homeKey(U.WHITE[1]);
        ok(LS().list[1].muted && U.toastNow() === "LOOP 01 MUTED", "HOME + white key: its loop muted at once");
        await t.homeKey(U.WHITE[1]);
        t.down("HOME"); await t.sleep(30); t.keyDown(U.WHITE[0]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[0]); await t.sleep(20); t.up("HOME"); await t.sleep(160);
        ok(LS().list[0].soloed && U.toastNow() === "LOOP 00 SOLO", "HOME + white key held: soloed");
        A.trackMute = m0;
        U.edit(() => { LS().list[0].soloed = false; LS().quantize = "ROW"; });
    }

    /* ---- the pattern recorder (ARP): onto the stage ---- */
    setLoops([], 64);
    U.edit(() => { LS().nextMono = false; });
    U.setInst(dx7);
    await pick(0);
    await openRec();
    const pr = U.screen.recorder();
    t.frame();
    ok(t.top() === "recorder" && P.pvOn() && t.leds[B.ARP] === 1 && U.screen.footer()[0] === "REC: TO STAGE" && pr.noteLen === "1/16" && pr.len === 16,
       "ARP held + REC: the pattern recorder (REC: TO STAGE); its preview playing alone; ARP full; NOTE LENGTH 1/16 and 16 steps at first");
    {   /* OCT+ on the 4th row moves that row's note alone: the first row's C kept from the recorder's open */
        const oct0 = U.octave;
        pr.rows[0].steps[0] = true; pr.sel = 3; t.frame();
        const firstNote = () => (P.lpPreview().cells.find(c => c) || {}).note;
        const n0 = firstNote();
        await t.tap("OCT+");
        const n1 = firstNote();
        await t.tap("OCT-");
        ok(n0 != null && n0 === n1 && U.octave === oct0, `OCT+ on the 4th row leaves the first row's note (${n0}, then ${n1})`);
        pr.rows[3].note = null;
        pr.rows[0].steps[0] = false; pr.sel = 0; t.frame();
    }
    await t.tap("ARP");
    ok(!P.pvOn(), "ARP: the preview stops (the loops stopped)");
    await t.turn("K1", -1);
    ok(pr.noteLen === "1/16", "K1 NOTE LENGTH: none shorter than a row (no 1/32 at 1/16)");
    for (const k of [0, 4, 8, 12]) await t.key(U.WHITE[k]);
    ok(pr.rows[0].steps.map((s, k) => s ? k : -1).filter(k => k >= 0).join() === "0,4,8,12" && U.screen.makes() === "4 X 16 = 64 ROWS, 1 LANE",
       "a white key tapped: its step on (0, 4, 8, 12); 4 X 16 = 64 ROWS, 1 LANE");
    await t.tap("SEL"); await t.key(U.WHITE[2]);
    ok(pr.rows[0].note === U.midi(U.WHITE[2]) && !pr.keys, "SEL, then a key: the row's note (back to the steps)");
    const rowsEvery = (step, from = 0) => { const a = []; for (let r = from; r < 64; r += step) a.push(r); return a; };
    const nn = U.midi(U.WHITE[2]);
    await t.tap("REC");
    ok(!t.top() && !P.playing && !P.pvOn() && sNotes() === rowsEvery(4).map(r => r + ":0:" + nn).join() && rowsEvery(4, 1).every(r => isOff(sCell(r))) &&
       !LS().list.length && U.toastNow() === "PATTERN ON THE STAGE",
       "REC: the pattern onto the stage over the length (PATTERN ON THE STAGE), each note 1/16 (a row) long; no loop made, nothing starts");
    await t.tap("REC");
    ok(!LS().list.length && staged() && !sc().arm, "... REC on the screen at once: nothing (REC's 500 ms after the recorder's REC)");
    await t.sleep(400);
    await t.tap("REC");
    ok(LS().list.length === 1 && notesAt(0).length === 16 && !staged(), "... REC on the screen then: the stage as LOOP 00");
    await stageOn();
    await t.sleep(400);
    await openRecAgain(); await t.tap("ARP");
    await t.turn("K1", 1);
    await t.tap("REC");
    ok(pr.noteLen === "1/8" && rowsEvery(4, 2).every(r => isOff(sCell(r))) && !sCell(1), "NOTE LENGTH 1/8: a note off two rows on");
    await openRecAgain(); await t.tap("ARP");
    await t.turn("K1", 10);
    await t.tap("REC");
    ok(pr.noteLen === "TO THE NEXT" && !stg().cells.some(isOff) && sNotes().split(",").length === 16, "NOTE LENGTH TO THE NEXT: no note off (each rings to the next)");
    await openRecAgain(); await t.tap("ARP");
    await t.key(U.WHITE[0]);
    await t.sleep(300);                                    /* (REC's 500 ms after the last write) */
    await t.tap("REC");
    ok(isOff(sCell(0)) && stg().cells.filter(isOff).length === 1 && sNotes().split(",").length === 12,
       "... the last note sounding at the length's end: a note off at row 0 where the lane has none (no wrap)");
    emptyStage();
    await openRec(); await t.tap("ARP");
    await t.key(U.WHITE[0]);
    t.keyDown(U.WHITE[1]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[1]); await t.sleep(40);
    ok(pr.rows[0].steps[1] === "hold", "a white key held past the hold delay after a step on: a tie");
    t.keyDown(U.WHITE[5]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[5]); await t.sleep(40);
    ok(pr.rows[0].steps[5] === true, "... after a step off: a plain note (no orphan tie)");
    t.keyDown(U.WHITE[2]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[2]); await t.sleep(40);
    await t.tap("REC");
    ok(isNote(sCell(0)) && !sCell(1) && !sCell(2) && isOff(sCell(3)) && isNote(sCell(5)) && isOff(sCell(6)),
       "REC: the ties add their rows to the note (two ties: a note off three rows on)");
    emptyStage();
    await openRecAgain(); await t.tap("ARP");
    await t.key(U.WHITE[0]);
    ok(pr.rows[0].steps.slice(0, 3).join() === "false,true,hold", "switching a run's first note off: the next tie a plain note (no orphan tie)");
    {
        const s13 = pr.rows[0].steps[13];
        t.keyDown(U.WHITE[13]);
        await t.turn("K2", -1);
        t.keyUp(U.WHITE[13]); await t.sleep(40);
        ok(pr.rows[0].steps[13] === s13, "a step key let go after K2 made LENGTH shorter than its step: nothing (keys past LENGTH do nothing)");
    }
    ok(U.screen.makes() === "5 X 12 + 4 = 64 ROWS, 1 LANE", "K2 LENGTH 12 steps: " + U.screen.makes());
    await t.turn("K2", 1);
    await t.turn("SELECT", 1);
    t.down("SEL"); await t.sleep(30); await t.key(U.WHITE[4]); t.up("SEL"); await t.sleep(200);
    ok(pr.rows[1].note === U.midi(U.WHITE[4]) && pr.sel === 1 && !pr.keys, "SEL held + a key: row 2's note at once (the keys the steps' again)");
    {
        const t0 = LS().tempo, p0 = P.kept.loops.row;
        await t.turn("ALGORITHM", 1);
        const t1 = LS().tempo;
        t.down("SEL"); await t.sleep(30); await t.turn("ALGORITHM", -1); t.up("SEL"); await t.sleep(250);
        const t2 = LS().tempo;
        await t.turn("ALGORITHM", 9);
        ok(t1 === t0 + 1 && t2 === t0 - 9 && LS().tempo === t0 && P.kept.loops.row === p0 && !pr.keys && t.top() === "recorder",
           `ALGORITHM in the pattern recorder: TEMPO (SEL held: by 10), the play position kept (${t0}, ${t1}, ${t2}, back ${LS().tempo})`);
    }
    await t.key(U.WHITE[8]);
    await t.tap("HOME");
    ok(!t.top() && !P.pvOn() && !staged(), "HOME: closed, nothing written");
    setLoops([mkLoop({ inst: 0xF001 })], 64);
    U.edit(() => { LS().nextMono = true; });
    await pick(0);
    await openRecAgain();
    ok(U.top().target === 0, "opened on a loop: its preview in that loop's sound");
    await t.tap("ARP");
    await t.tap("REC");
    ok(U.inst === 0xF001 && !LS().nextMono && isNote(sCell(1)) && isNote(sCell(8, 1)) && !notesAt(0).length && P.lpSilent(0),
       "REC: onto the stage in the loop's instrument (the keys' now), POLY for two rows; the loop untouched (silent: the stage holds notes)");
    emptyStage();
    await openRec();
    await t.sleep(400);                                     /* (REC's 500 ms after the last write) */
    await t.tap("REC");
    ok(t.top() === "recorder" && U.toastNow() === "NO STEPS ON", "no step on: REC refused (NO STEPS ON)");
    await t.key(U.WHITE[3]);
    await t.tap("PLAY"); await t.sleep(100);
    ok(P.playing && !P.pvOn() && P.lpPreview() && t.top() === "recorder", "PLAY: the loops play, the preview in their time (not alone)");
    await t.tap("ARP");
    ok(!P.lpPreview(), "... ARP switches its sound off");
    await t.tap("ARP");
    await t.tap("PLAY"); await t.sleep(100);
    ok(!P.playing && P.pvOn(), "PLAY pausing the loops: the preview goes on alone");
    await t.tap("PLAY"); await t.sleep(100);
    ok(P.playing && !P.pvOn() && P.lpPreview(), "PLAY starting them: the preview with them again");
    await t.tap("HOME");
    ok(!t.top() && !P.lpPreview() && !staged(), "HOME: closed, nothing written");
    await stopAll();
    {   /* it opens clean every time, after HOME as after REC: no steps or ties, the first row on its C, 16 steps, 1/16 */
        const clean = () => !pr.sel && pr.len === 16 && pr.noteLen === "1/16" && !pr.keys &&
            pr.rows.every((r, k) => r.steps.every(s => !s) && (k ? r.note == null : r.note === U.clamp(60 + 12 * U.octave, 12, 127)));
        const setSome = async () => {
            await t.key(U.WHITE[0]);
            t.keyDown(U.WHITE[1]); await t.sleep(U.set("HOLD DELAY") + 100); t.keyUp(U.WHITE[1]); await t.sleep(40);
            await t.turn("K1", 2); await t.turn("K2", -1); await t.turn("SELECT", 2); await t.turn("K3", 3);
        };
        await t.sleep(600);
        await openRec();
        await setSome();
        await t.tap("HOME");
        await openRec();
        const home = clean();
        await setSome();
        await t.sleep(600);
        await t.tap("REC");
        const sent = !t.top() && staged();
        await t.sleep(600);
        await openRec();
        const rec = clean();
        await t.tap("HOME");
        await stopAll();
        emptyStage();
        ok(home && sent && rec, "the recorder opens clean every time, after HOME as after REC: no steps or ties, the first row on its C, 16 steps, 1/16");
    }
    {   /* a song playing on into LOOPS pauses as the recorder opens, its place kept: the preview plays alone */
        P.playSong(0xA0); await t.sleep(300);
        const was = P.playing && P.what === "song";
        await openRec();
        ok(was && !P.playing && P.pvOn() && t.top() === "recorder", "ARP + REC with a song playing on into LOOPS: the song pauses (its place kept), "
           + "the preview plays alone");
        await t.tap("HOME");
        await stopAll();
    }
    {   /* REC with the preview playing: its sounding note goes on as the stage's, without a break, no row played twice;
         * the loops playing, in their time; the preview alone, the loops start from where it was (rows of 62.5 ms, a note
         * a pass: the next one 0.7 s after REC) */
        setLoops([], 16);
        U.edit(() => { LS().nextMono = false; LS().tempo = 240; });
        U.setInst(dx7);
        await pick(0);
        const on0 = A.noteOn, off0 = A.noteOff, fl0 = A.flush, fi0 = A.flushIds;
        let ons = [], cuts = [];
        A.noteOn = (id, time, ...a) => { ons.push({ id, time, track: a[3] }); return on0(id, time, ...a); };
        A.noteOff = (id, time) => { cuts.push({ id, time }); return off0(id, time); };
        A.flush = (prefix, time, at) => { cuts.push({ prefix, time }); return fl0(prefix, time, at); };
        A.flushIds = (ids, time) => { cuts.push({ ids, time }); return fi0(ids, time); };
        const sounding = () => { const n = P.time(), x = ons.filter(o => o.track === STAGE_T && o.id !== "rv" && o.time <= n).pop(); return x ? x.id : null; };
        const cutOf = (id, t0, t1) => cuts.some(c => c.time >= t0 - .001 && c.time <= t1 &&
                                                     (c.id === id || (c.prefix && id[0] === c.prefix) || (c.ids && c.ids.includes(id))));
        /* the rows heard every 10 ms for 0.4 s: on by one, never back, never the same row twice over */
        const rowsGoOn = async () => {
            const seen = [];
            for (let k = 0; k < 40; k++) { const h = P.at(); if (h && (!seen.length || seen[seen.length - 1].n !== h.n)) seen.push({ n: h.n, row: h.row }); await t.sleep(10); }
            return seen.length > 2 && seen.every((x, k) => { if (!k) return true; const d = x.n - seen[k - 1].n; return d >= 1 && d <= 2 && x.row === (seen[k - 1].row + d) % 16; });
        };
        P.kept.loops.row = 0;
        await openRec();
        pr.rows[0].steps[0] = true; pr.noteLen = "TO THE NEXT";
        await t.tap("PLAY");                                /* (the loops from their start, the preview with them) */
        await t.sleep(250);
        const idA = sounding(), tA = P.time();
        await t.tap("REC");
        const goA = await rowsGoOn();
        ok(P.playing && P.what === "loops" && !P.pvOn() && !t.top() && staged() && idA && !cutOf(idA, tA, P.time()) && goA,
           `REC with the loops playing and the preview on: they play on, the note sounding goes on as the stage's (${idA} not cut), no row twice`);
        await stopAll(); emptyStage(); await t.sleep(600);
        ons = []; cuts = [];
        await openRec();
        pr.rows[0].steps[0] = true; pr.noteLen = "TO THE NEXT";
        await t.sleep(1250);                                /* (the step's next pass: a pass is 1 s) */
        const idB = sounding(), tB = P.time(), rowB = P.pvAt() ? P.pvAt().row : -1;
        await t.tap("REC");
        const h = P.at(), goB = await rowsGoOn();
        await t.sleep(400);
        ok(P.playing && P.what === "loops" && !P.pvOn() && !t.top() && staged() && idB && idB[0] === "v" && !cutOf(idB, tB, tB + .4) && goB &&
           h && h.row >= rowB && h.row <= rowB + 6 && ons.some(o => o.track === STAGE_T && o.id[0] === "g" && o.time > tB),
           `REC with the preview playing alone: the loops start from where it was (row ${rowB}, then ${h ? h.row : -1}), the note sounding `
           + `goes on as the stage's (${idB} not cut), no row twice, the stage's next note heard`);
        A.noteOn = on0; A.noteOff = off0; A.flush = fl0; A.flushIds = fi0;
        await stopAll(); emptyStage();
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- the stage switch (ARP's tap): off, the stage keeps its notes, drawn dimmed, not heard, the selected loop heard,
     * the keys heard at once in the stage's sound and not written; REC's own jobs and LFO do nothing, EDIT held clears it
     * and turns it on; EDIT and the recorder's REC fill it and turn it on; off disarms; ARP's tap waits the grace, a hold
     * shows ARP's layer and does nothing; ARP held + REC the pattern recorder (REC's own job not done), REC first plain
     * REC; SEL's RECORD PATTERN; refused armed and counting in ---- */
    {
        const heard = async ms => { const got = [], on0 = A.noteOn; A.noteOn = (id, time, ...a) => { got.push({ id, time, track: a[3] }); return on0(id, time, ...a); };
                                    await t.sleep(ms); A.noteOn = on0; return got; };
        const has = (got, re) => got.some(o => re.test(o.id));
        const title = () => U.screen.stageTitle ? U.screen.stageTitle() : "-";
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, 62], [8, 0, 64], [12, 0, 65]]) }),
                  mkLoop({ col: 1, cells: cs(16, [[2, 0, 67], [6, 0, 69], [10, 0, 71], [14, 0, 72]]) })], 16);
        U.edit(() => { LS().tempo = 300; LS().quantize = "ROW"; LS().nextMono = false; });
        U.setInst(dx7);
        await pick(0);
        stg().cells = cs(16, [[1, 0, 72], [5, 0, M.OFF]]);
        M.version++;
        t.frame();
        const leds0 = t.leds[B.ARP], notes0 = sNotes();
        /* the tap waits the combination grace: nothing at the release, the switch once it has passed */
        t.down("ARP"); await t.sleep(20); t.up("ARP");
        const atRelease = stg().off;
        await t.sleep(U.set("COMBO GRACE") + 60);
        t.frame();
        ok(!atRelease && stg().off && U.toastNow() === "STAGE OFF" && sNotes() === notes0 && leds0 === 1 && t.leds[B.ARP] === .25,
           "ARP tapped: the stage off once the combination grace has passed (STAGE OFF), its notes kept; ARP dim while it is off (full before)");
        ok(!P.lpSilent(0) && title() === "STAGE OFF|REC: STAGE ON" && hintOf() === "STAGE OFF: THE KEYS ARE NOT KEPT",
           "... the selected loop heard (not STAGED); the stage's title STAGE OFF, its right REC: STAGE ON (" + title() + "); the hint line " +
           "STAGE OFF: THE KEYS ARE NOT KEPT (" + hintOf() + ")");
        {   /* drawn much fainter: the stage's note at row 2 (a sustain cell) deep light grey; on: the selected loop's colour */
            const x = 28 + Math.floor(2 * 180 / 16) + 2, y = 180;
            t.frame();
            const off = t.px(x, y), frame = t.px(0, 185), inTitle = c => { for (let py = 162; py < 174; py++) for (let px = 0; px < 240; px++) if (t.px(px, py) === c565(c)) return true; return false; };
            const titleOff = inTitle(D.DGREY) && !inTitle(D.CYAN) && !inTitle(D.YELLOW);
            await t.tap("ARP");
            t.frame();
            const on = t.px(x, y);
            ok(off === c565(D.DEEP + D.LGREY) && on === c565(M.LOOP_COLS[0]) && !stg().off && U.toastNow() === "STAGE ON" && P.lpSilent(0),
               "... drawn much fainter: its notes deep light grey (then, ARP again: STAGE ON, in the loop's colour, the loop silent again)");
            ok(frame === c565(D.DEEP + D.LGREY) && titleOff, "... its frame deep light grey, its title and REC: STAGE ON dark grey");
            await t.tap("ARP");
        }
        P.kept.loops.row = 0; await t.tap("PLAY");
        let got = await heard(900);
        ok(stg().off && !has(got, /^g32\./) && has(got, /^s16\./) && has(got, /^s17\./), "off, the loops playing: the stage not heard, the selected loop " +
           "heard in its place, the other loops too");
        U.edit(() => { LS().quantize = "4 ROWS"; });
        {   /* a key: heard at once in the stage's sound (its column), no wait for QUANTIZE's line, not written */
            const on0 = A.noteOn, ons = [];
            A.noteOn = (id, time, ...a) => { ons.push({ id, time, track: a[3] }); return on0(id, time, ...a); };
            const t0 = P.time();
            heardAt(1, .3); t.keyDown(9); real();
            await t.sleep(40);
            heardAt(1, .6); t.keyUp(9); real();
            A.noteOn = on0;
            const k = ons.find(o => /k9$/.test(o.id));
            ok(k && k.track === STAGE_T && k.time - t0 < .05 && sNotes() === notes0 && !Object.keys(stg().keys).length,
               "... a key: heard at once on the stage's column (4 ROWS: no wait for the line), not written onto the stage (" + (k ? (k.time - t0).toFixed(3) : "none") + " s)");
        }
        U.edit(() => { LS().quantize = "ROW"; });
        /* REC: the stage on, as ARP's tap; LFO: nothing */
        U.flash[B.REC] = 0;
        await t.tap("REC");
        ok(!(U.flash[B.REC] > U.now()) && !stg().off && U.toastNow() === "STAGE ON" && LS().list.length === 2 && sNotes() === notes0 && !sc().arm && P.playing,
           "... REC, the loops playing: the stage on (STAGE ON), nothing sent or armed: the keys write from there");
        await t.tap("ARP");
        const cells0 = JSON.stringify(stg().cells);
        heardAt(0, .1); t.down("LFO"); real(); await t.sleep(30);
        frameAt(1, .5);                                      /* (the line past the stage's note at row 1, LFO still held: OVERWRITE would erase it) */
        const lfoHint = hintOf(), erasing = stg().erasers.length, heldOnly = JSON.stringify(stg().cells) === cells0 && title().startsWith("STAGE OFF|");
        frameAt(3, .5); frameAt(6, .5);
        t.up("LFO"); await t.sleep(60);
        frameAt(9, .5);
        ok(sNotes() === notes0 && !erasing && heldOnly && JSON.stringify(stg().cells) === cells0 && !stg().erasers.length && !stg().over && stg().off &&
           lfoHint === "STAGE OFF: THE KEYS ARE NOT KEPT",
           "... LFO: nothing (no OVERWRITE: the stage's notes kept as the line passes; the hint line the stage's; no dim LFO)");
        t.down("EDIT"); await t.sleep(U.LONG_HOLD + 100); t.up("EDIT"); await t.sleep(60);
        ok(!staged() && U.toastNow() === "STAGE CLEARED" && !stg().off, "... EDIT held: the stage cleared (STAGE CLEARED), and on");
        await t.tap("ARP");
        /* EDIT: the selected loop onto the stage, the stage on */
        await t.tap("EDIT");
        ok(!stg().off && sNotes() === "0:0:60,4:0:62,8:0:64,12:0:65" && U.toastNow() === "LOOP 00 ON THE STAGE" && P.lpSilent(0),
           "EDIT with the stage off: the loop onto the stage (LOOP 00 ON THE STAGE), the stage on, the loop silent");
        await stopAll();
        /* the pattern recorder's REC: its pattern onto the stage, the stage on */
        await t.tap("ARP");
        await openRec();
        const rp = U.screen.recorder();
        for (const r of rp.rows) r.steps.fill(false);
        rp.rows[0].steps[0] = true; rp.sel = 0; rp.len = 16;
        await t.sleep(400);
        await t.tap("REC");
        ok(!t.top() && !stg().off && U.toastNow() === "PATTERN ON THE STAGE" && sNotes().startsWith("0:0:"),
           "with the stage off, ARP held + REC opens the recorder; its REC: the pattern onto the stage (PATTERN ON THE STAGE), the stage on");
        await stopAll();                                    /* (the preview alone went on as the loops: stopped again) */
        await t.sleep(600);
        /* off while armed: disarmed (CANCELLED); stopped and off, REC turns it on and arms in one tap, whatever it holds */
        emptyStage();
        await t.tap("REC");
        const armed = !!sc().arm;
        await t.tap("ARP");
        ok(armed && !sc().arm && stg().off && U.toastNow() === "CANCELLED", "the stage off while armed: disarmed (CANCELLED)");
        await t.sleep(600);
        await t.tap("REC");
        t.frame();
        ok(sc().arm && sc().arm.wait && !stg().off && !P.playing && U.toastNow() === "STAGE ON: ARMED" && hintOf().startsWith("ARMED: ") &&
           title().startsWith("STAGE OVERDUB") && title().endsWith("|REC: INTO LOOP 00"), "... stopped and off: REC turns it on and arms at once " +
           "(STAGE ON: ARMED; the hint line " + hintOf() + "; " + title() + ")");
        {   /* the first key starts the loops writing, onto the stage */
            t.keyDown(4); await t.sleep(40); t.keyUp(4); await t.sleep(40);
            ok(P.playing && P.what === "loops" && !sc().arm && staged(), "... armed so, the first key starts the loops, its note onto the stage");
            await stopAll();
        }
        /* with notes on the stage too: armed (REC then sends them, as any armed REC with notes on the stage) */
        stg().cells = cs(16, [[1, 0, 72], [5, 0, M.OFF]]); M.version++;
        await t.tap("ARP");
        await t.sleep(600);
        await t.tap("REC");
        ok(sc().arm && !stg().off && U.toastNow() === "STAGE ON: ARMED" && staged(), "... the stage holding notes: the same (STAGE ON: ARMED), its notes kept");
        await t.tap("ARP");
        emptyStage();
        /* off outside LOOPS too */
        await t.homeKey(8);
        ok(U.screen.name === "PLAYBACK" && U.screens.LOOPS.stage().off, "the stage off stays off outside LOOPS");
        await t.homeKey(22);
        await t.tap("ARP");
        ok(!stg().off, "(on again)");
        /* ARP held past the hold delay: its layer (REC: RECORD PATTERN); let go, nothing */
        t.down("ARP"); await t.sleep(U.set("HOLD DELAY") + 100);
        const lay = U.layers.find(l => l.btn === B.ARP), lines = lay ? lay.lines() : [];
        t.up("ARP"); await t.sleep(U.set("COMBO GRACE") + 100);
        ok(lines.join("/") === "REC: RECORD PATTERN" && !stg().off && !t.top(), "ARP held past the hold delay: its layer (" + lines.join("/") + "); let go: nothing");
        /* ARP held + REC: the recorder at REC's release; REC's own job not done; ARP's tap not after it */
        stg().cells = cs(16, [[1, 0, 72], [5, 0, M.OFF]]); M.version++;
        await pick(2);
        t.down("ARP"); await t.sleep(30); t.down("REC"); await t.sleep(30);
        const atPress = t.top(), listAtPress = LS().list.length;
        t.up("REC"); await t.sleep(20);
        const atRel = t.top();
        t.up("ARP"); await t.sleep(U.set("COMBO GRACE") + 100);
        ok(!atPress && listAtPress === 2 && atRel === "recorder" && t.top() === "recorder" && LS().list.length === 2 && staged() && !stg().off,
           "ARP held + REC: the pattern recorder at REC's release; REC's press sent nothing; ARP's tap not after it (the stage still on)");
        await t.tap("HOME");
        /* ARP held past the hold delay (its layer shown), then REC: the recorder opens at REC's release; no switch after ARP's */
        t.down("ARP"); await t.sleep(U.set("HOLD DELAY") + 100);
        t.down("REC"); await t.sleep(30);
        const latePress = t.top(), listLate = LS().list.length;
        t.up("REC"); await t.sleep(20);
        const lateRel = t.top();
        t.up("ARP"); await t.sleep(U.set("COMBO GRACE") + 100);
        ok(!latePress && listLate === 2 && lateRel === "recorder" && t.top() === "recorder" && LS().list.length === 2 && !stg().off,
           "ARP held past the hold delay (its layer shown), then REC: the pattern recorder at REC's release; no switch after ARP's release");
        await t.tap("HOME");
        /* ARP let go within the grace, then REC: the combination too */
        t.down("ARP"); await t.sleep(20); t.up("ARP"); await t.sleep(30);
        await t.tap("REC");
        ok(t.top() === "recorder" && LS().list.length === 2 && !stg().off, "ARP let go within the combination grace, then REC: the pattern recorder (no switch, nothing sent)");
        await t.tap("HOME");
        /* REC first: plain REC (the stage sent at its press), ARP then its own tap */
        await t.sleep(600);
        t.down("REC"); await t.sleep(30);
        const sent = LS().list.length === 3;
        t.down("ARP"); await t.sleep(20); t.up("ARP"); await t.sleep(30); t.up("REC");
        await t.sleep(U.set("COMBO GRACE") + 100);
        ok(sent && !t.top() && !stg().off, "REC held first, then ARP: REC's own job at its press (the stage sent, off), ARP's tap the switch (on)");
        /* refused while armed and during a count-in (REC and ARP blink) */
        await t.sleep(600);
        await t.tap("REC");
        U.flash[B.REC] = U.flash[B.ARP] = 0;
        await openRec();
        ok(!t.top() && sc().arm && U.flash[B.REC] > U.now() && U.flash[B.ARP] > U.now(), "ARP held + REC while armed: refused (REC and ARP blink), still armed");
        U.edit(() => { LS().countIn = "1 BAR"; });
        await t.tap("PLAY");
        const counting = P.countingIn();
        U.flash[B.REC] = U.flash[B.ARP] = 0;
        await openRec();
        ok(counting && P.countingIn() && !t.top() && U.flash[B.ARP] > U.now(), "... during a count-in: refused, the count goes on");
        await t.tap("HOME");
        U.edit(() => { LS().countIn = "OFF"; });
        await stopAll();
        /* SEL's RECORD PATTERN: the tap route */
        await t.sleep(600);
        await menuPick("RECORD PATTERN");
        ok(t.top() === "recorder", "SEL → RECORD PATTERN: the pattern recorder");
        await t.tap("HOME");
        await pick(0);
        await menuPick("RECORD PATTERN");
        ok(t.top() === "recorder" && U.top().target === 0, "... on a loop too (its preview in the loop's sound)");
        await t.tap("HOME");
        emptyStage();
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- the stage off, the keys: POLY every key a note of its own (16 lanes, the 17th key taking one in turn), MONO one note (MONO LEGATO), a release
     * ends it, keys held across the switch, LOOPS left or FX taking the keys ends them; written nowhere ---- */
    {
        const snd = new Set(), calls = [], on0 = A.noteOn, off0 = A.noteOff;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { snd.add(id); calls.push({ id, midi, opts }); return on0(id, time, midi, ref, vol, track, opts); };
        A.noteOff = (id, ...a) => { snd.delete(id); return off0(id, ...a); };
        const keysSnd = () => [...snd].map(id => /k(\d+)$/.exec(id)).filter(Boolean).map(m => +m[1]).sort((a, b) => a - b).join();
        const down = async ks => { for (const k of ks) t.keyDown(k); await t.sleep(40); };
        const up = async ks => { for (const k of ks) t.keyUp(k); await t.sleep(40); };
        await stopAll();
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, 62], [8, 0, 64], [12, 0, 65]]) })], 16);
        U.edit(() => { LS().tempo = 300; LS().quantize = "ROW"; LS().nextMono = false; LS().legato = "GLIDE"; });
        U.setInst(dx7);
        await pick(0);
        P.kept.loops.row = 0; await t.tap("PLAY");
        await t.tap("ARP");
        const offNow = stg().off && P.playing && P.what === "loops";
        const chord = [0, 2, 4, 5, 7, 9, 11];
        await down(chord);
        ok(offNow && keysSnd() === chord.join() && !staged() && !Object.keys(stg().keys).length,
           "the stage off, POLY: seven keys down, all seven heard (not four), none written (" + keysSnd() + ")");
        await up([4]);
        ok(keysSnd() === "0,2,5,7,9,11", "... a key let go: its note ends, the others sound on (" + keysSnd() + ")");
        await up([0, 2, 5, 7, 9, 11]);
        ok(keysSnd() === "" && !staged(), "... the rest let go: every note ended, nothing written");
        /* 17 keys: 16 lanes, the 17th key takes lane 0 (the counter) and lets that note go; the displaced key's release does
         * nothing; a second overflow takes lane 1; key 2 let go and down again (the lowest free lane, 2, the youngest note), the
         * third overflow takes lane 2: the counter, not the oldest key */
        const rng = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
        await down(rng(0, 16));
        ok(keysSnd() === rng(1, 16).join() && !staged(), "the stage off, POLY: 17 keys down: the 17th sounds, the first's note let go (" + keysSnd() + ")");
        await up([0]);
        ok(keysSnd() === rng(1, 16).join(), "... the displaced first key let go: nothing changes (it was forgotten)");
        await down([17]);
        ok(keysSnd() === [...rng(2, 17)].join(), "... an 18th key: it sounds, key 1's note (lane 1) let go (" + keysSnd() + ")");
        await up([2]); await down([2]); await down([18]);
        ok(keysSnd() === rng(3, 18).join(), "... a 19th key: the counter's lane (2) is taken, key 2's young note let go, not the oldest key's (" + keysSnd() + ")");
        await up(rng(1, 18));
        ok(keysSnd() === "" && !staged(), "... all let go: no note left, nothing written");
        /* MONO: one note; GLIDE glides the one voice, JUMP a new note ends the first, LEGATO onto the held voice, a TB-303 slides */
        U.edit(() => { LS().nextMono = true; });
        calls.length = 0;
        await down([0]); await down([4]);
        ok(keysSnd() === "0" && !calls.some(c => /k4$/.test(c.id)), "the stage off, MONO, GLIDE: the second key glides the one voice, no second note (" + keysSnd() + ")");
        await up([0]);
        ok(keysSnd() === "0", "... the key whose note is not sounding let go: the note sounds on");
        await up([4]);
        ok(keysSnd() === "", "... the sounding one let go: the note ends");
        U.edit(() => { LS().legato = "JUMP"; });
        await down([0]); await down([4]);
        ok(keysSnd() === "4", "MONO, JUMP: the second key a new note, the first one ended (" + keysSnd() + ")");
        await up([4]); await up([0]);
        U.edit(() => { LS().legato = "LEGATO"; });
        calls.length = 0;
        await down([0]); await down([4]);
        const held = calls.find(c => c.midi === U.midi(0)), leg = calls.find(c => c.midi === U.midi(4));
        ok(held && leg && leg.opts && leg.opts.leg === held.id, "MONO, LEGATO: the second key a legato onto the first one's voice");
        await up([4]); await up([0]);
        ok(keysSnd() === "" || keysSnd() === "0", "(legato released)");
        U.edit(() => { LS().legato = "GLIDE"; });
        U.setInst(tb303);
        calls.length = 0;
        await down([0]); await down([4]);
        const tbs = calls.filter(c => /p4tb$/.test(c.id));
        ok(tbs.length === 2 && tbs[1].id === tbs[0].id && tbs[1].opts.glide && !tbs[0].opts.glide, "MONO on a TB-303: the second key a slide on the one voice");
        await up([4]); await up([0]);
        U.setInst(dx7);
        for (const id of [...snd]) snd.delete(id);
        /* keys held across the switch: down while off, the stage switched on: they sound until let go, written nowhere */
        U.edit(() => { LS().nextMono = false; });
        await down([0, 2]);
        await t.tap("ARP");
        const nowOn = !stg().off;
        ok(nowOn && keysSnd() === "0,2", "keys down with the stage off, the stage switched on: they sound on (" + keysSnd() + ")");
        await up([0, 2]);
        ok(keysSnd() === "" && !staged(), "... let go: ended, nothing written");
        /* down on the stage, the stage switched off: they stop writing; the sound ends with the release */
        await down([0, 2]);
        const written = Object.keys(stg().keys).length;
        await t.tap("ARP");
        const nowOff = stg().off;
        await up([0, 2]);
        ok(written === 2 && nowOff && keysSnd() === "", "keys down on the stage, the stage switched off: they stop writing, let go they end (" + keysSnd() + ")");
        /* a key down with the stage off, LOOPS left (HOME + OP4... as the checks above do) or FX taking the keys: the note ends */
        await down([0, 2]);
        const heldOff = keysSnd();
        await t.tap("FX");
        const afterFx = keysSnd();
        await t.tap("FX");
        await up([0, 2]);
        ok(heldOff === "0,2" && afterFx === "", "keys down with the stage off, FX: their notes end (" + heldOff + " then " + afterFx + ")");
        await down([0, 2]);
        await t.homeKey(8);
        const afterLeave = keysSnd(), leftTo = U.screen.name;
        await up([0, 2]);
        await t.homeKey(22);
        ok(leftTo === "PLAYBACK" && afterLeave === "" && U.screen.name === "LOOPS", "... LOOPS left: their notes end (" + afterLeave + ")");
        A.noteOn = on0; A.noteOff = off0;
        t.frame();
        const ledOff = stg().off ? t.leds[B.ARP] : -1;
        if (stg().off) await t.tap("ARP");
        t.frame();
        ok(ledOff === .25 && t.leds[B.ARP] === 1, "the ARP LED: dim with the stage off, full on (" + ledOff + ", " + t.leds[B.ARP] + ")");
        /* ARP held, then a key: the key closes ARP's combination (the key plays, ARP's let go switches nothing) */
        await stopAll();
        P.kept.loops.row = 0; await t.tap("PLAY");
        t.down("ARP"); await t.sleep(30); t.keyDown(2); await t.sleep(30); t.keyUp(2); t.up("ARP");
        await t.sleep(U.set("COMBO GRACE") + 100);
        ok(!stg().off && !t.top(), "ARP held, a key played, ARP let go: the stage not switched (the key closed ARP's combination)");
        /* a song's count-in: ARP's tap switches the stage and leaves the count-in going */
        await stopAll();
        U.edit(() => { LS().countIn = "1 BAR"; });
        await menuPick("RECORD LIVE"); await t.tap("REC");
        await t.tap("PLAY");
        const cnt = P.countingIn() && !!sc().song;
        await t.tap("ARP");
        const stillCounting = P.countingIn() && !!sc().song && stg().off && U.toastNow() === "STAGE OFF";
        ok(cnt && stillCounting, "RECORD LIVE counting in: ARP's tap switches the stage off (STAGE OFF), the count-in and the song untouched");
        t.frame();
        ok(hintOf() === "COUNTING IN: HOME CANCELS", "... the hint line COUNTING IN: HOME CANCELS, not the stage off's, while a song counts " +
           "in (" + hintOf() + ")");
        await t.tap("HOME");
        if (stg().off) await t.tap("ARP");
        U.edit(() => { LS().countIn = "OFF"; });
        await stopAll(); emptyStage();
        U.edit(() => { LS().tempo = 120; LS().nextMono = false; });
    }

    /* ---- SAVE TO PATTERN, LOAD FROM PATTERN ---- */
    {
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
        /* LOAD of a pattern with no notes (NEW in the list makes one): no loops, its length and its song's timing all the same */
        const song = M.state.songs.find(s => s && s.rpb === 4 && !s.order.includes(free)), was = { ...LS() };
        U.edit(() => { M.state.patterns[free] = M.newPattern(free, 3, 40); song.order.push(free); LS().tempo = 99; LS().ticks = 5; LS().swing = 70; });
        await menuPick("LOAD FROM PATTERN");
        U.top().sel = M.patternNums().indexOf(free); await t.tap("SEL"); await t.tap("OCT+");
        ok(!LS().list.length && LS().rows === 48 && LS().tempo === U.clamp(song.tempo, M.TEMPO_MIN, M.TEMPO_MAX) && LS().ticks === song.ticks &&
           LS().swing === U.clamp(song.swing, 50, 75) && LS().rpb === 4 && U.toastNow() === "PATTERN " + M.hex(free) + " IN LOOPS: 0 LOOPS",
           "LOAD of a pattern with no notes: no loops, its 40 rows in whole bars (48), its song's timing (" + LS().tempo + ", " + LS().ticks + " ticks)");
        U.edit(() => { M.state.patterns[free] = null; song.order.pop(); Object.assign(LS(), { tempo: was.tempo, ticks: was.ticks, swing: was.swing, rpb: was.rpb }); });
    }

    /* ---- the player's stops kept where notes are written out (DOUBLE, LENGTH shorter, SAVE TO PATTERN); SAVE TO
     * PATTERN's mix commands in time order; LOAD's key offs and note delays at their ticks ---- */
    {
        const offAt = (i, r, l) => isOff(cell(i, r, l));
        setLoops([mkLoop({ cells: cs(16, [[4, 0, 60]]) }),
                  mkLoop({ base: 12, repeats: 3, col: 1, lanes: 2, cells: cs(12, [[2, 1, 62], [9, 0, 64], [10, 0, M.OFF]]) })], 32);
        await menuPick("DOUBLE");
        ok(LS().rows === 64 && notesAt(0).join() === "4:0:60" && offAt(0, 16, 0) && notesAt(1).join() === "2:1:62,9:0:64,14:1:62,21:0:64,26:1:62" &&
           offAt(1, 0, 1) && !cell(1, 0, 0) && offAt(1, 22, 0), "DOUBLE: a note held into a loop's rest stops there; one held over a cut repeat's end stops at the wrap (a note off at row 0)");
        setLoops([mkLoop({ base: 32, cells: cs(32, [[4, 0, 60]]) })], 32);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -1); await t.tap("GLO");
        ok(LS().rows === 16 && LS().list[0].base === 16 && offAt(0, 0, 0) && notesAt(0).join() === "4:0:60", "LENGTH shorter: a note held over the cut stops at the wrap (a note off at row 0)");
        setLoops([mkLoop({ base: 4, repeats: 2, cells: cs(4, [[1, 0, 60]]) }), mkLoop({ inst: 0xF001, col: 1, cells: cs(16, [[12, 0, 64], [14, 0, M.OFF]]) })], 16);
        const free = M.freePattern();
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

    /* ---- outside LOOPS: PLAYBACK acts as if nothing were loaded and changes nothing (one line in the body, no header
     * page, no footer; K1, K3, K4, SELECT, LFO and EDIT do nothing; K2, SEQ and GLO's SCROLL as ever, no UNLOAD SONG), FX's
     * MIXER a column a loop ---- */
    P.playLoops(); await t.sleep(200);
    await t.homeKey(8); t.frame();
    const gray = () => { let n = 0, other = 0; for (let y = U.BODY_Y; y < 226; y++) for (let x = 0; x < 240; x++) { const p = t.px(x, y); if (p === c565(D.DGREY)) n += y < U.BODY_Y + 52 ? 1 : 100; else if (p !== c565(D.BLACK)) other++; } return [n, other]; };
    const pbCase = async (how, playing, line) => {
        await t.sleep(1400); t.frame();                     /* (a knob line from before gone) */
        const [dg, other] = gray();
        ok(U.screen.name === "PLAYBACK" && P.what === "loops" && P.playing === playing && !U.screen.page() && !U.screen.footer() &&
           dg > 150 && dg < 1000 && other === 0, "PLAYBACK, the loops " + how + ": the line in the body (" + line + "), no header page, no footer, no grid");
        const s0 = P.speed(), v0 = P.volume(), row0 = P.kept.loops.row, song0 = U.song;
        for (const [k, n] of [["K1", 2], ["K3", -3], ["K4", -5], ["SELECT", 2]]) {
            await t.turn(k, n); await t.sleep(60);
            ok(!U.knobNow() && P.what === "loops" && P.playing === playing && P.speed() === s0 && P.volume() === v0 && U.song === song0 &&
               (playing || P.kept.loops.row === row0) && U.screen.name === "PLAYBACK", "PLAYBACK, the loops " + how + ": " + k + " does nothing (no knob line)");
        }
        await t.tap("LFO");
        ok(!U.knobNow() && P.speed() === s0 && P.volume() === v0, "PLAYBACK, the loops " + how + ": LFO does nothing (no knob line)");
        await t.tap("EDIT");
        ok(U.screen.name === "PLAYBACK" && !t.top() && P.what === "loops" && P.playing === playing, "PLAYBACK, the loops " + how + ": EDIT does nothing (no EDITOR, no question)");
        await t.tap("ENV");
        const listed = !!t.top();
        await t.tap("ENV");
        ok(listed && !t.top() && U.screen.name === "PLAYBACK" && P.what === "loops", "PLAYBACK, the loops " + how + ": ENV opens the song list, ENV again closes it");
        await t.tap("ARP"); await t.tap("ARP");
        ok(U.screen.name === "PLAYBACK" && !t.top() && P.what === "loops" && P.playing === playing, "PLAYBACK, the loops " + how + ": ARP (twice) leaves the screen and the loops as they were");
        await t.turn("K2", 5);
        await t.turn("K2", -1);
        ok(U.knobNow() === "K2 VIEW 4 TRACKS", "PLAYBACK, the loops " + how + ": K2 still turns the view (" + U.knobNow() + ")");
        await t.tap("SEQ");
        ok(U.toastNow() === "2 TRACKS", "PLAYBACK, the loops " + how + ": SEQ still turns the view");
        await t.turn("K2", 5);
        await t.tap("GLO");
        ok(U.top() && U.top().items && U.top().items.length === 1 && U.top().items[0].name === "SCROLL", "PLAYBACK, the loops " + how + ": GLO has SCROLL alone, no UNLOAD SONG");
        await t.tap("GLO");
    };
    await pbCase("playing", true, "THE LOOPS PLAY: LOOPS SHOWS THEM");
    await t.tap("PLAY"); await t.sleep(100);
    await pbCase("paused", false, "THE LOOPS ARE PAUSED: PLAY GOES ON");
    await t.tap("PLAY"); await t.sleep(100);
    ok(P.playing && P.what === "loops", "PLAYBACK, the loops: PLAY resumes them");
    await t.tap("FX"); await t.tap("SEQ"); await t.tap("SEQ"); t.frame();
    ok(U.screen.sub() === "MIXER" && t.PTFM.audio.fxSong() === LS(), "FX's MIXER: THE LOOPS' (drawn)");
    await t.tap("SEQ"); await t.tap("FX");
    await stopAll();
    await t.homeKey(22);
    {   /* the loops held for the keys with nothing played (LOOPS opened and left) are not "what played last": PLAYBACK is
         * as with nothing loaded, the current song's start (the firmware's cue state) */
        P.unload();
        await t.homeKey(22);
        const onLoops = U.screen.name === "LOOPS" && P.what !== "loops";
        await t.homeKey(8); t.frame();
        ok(onLoops && U.screen.name === "PLAYBACK" && P.what !== "loops" && !!U.screen.page() && !!U.screen.footer(),
           "PLAYBACK, LOOPS opened and left with nothing played: the current song's start (page, footer), not the loops' line");
        await t.homeKey(22);                                  /* (LOOPS again, for what follows) */
    }

    /* ---- DOUBLE, CLEAR LOOPS, REVERT, SAVE and a power cycle ---- */
    setLoops([mkLoop({ cells: Object.assign(new Array(64).fill(null), { 0: M.newCell(60, null, 100) }) }),
              mkLoop({ base: 12, repeats: 2, col: 1, cells: Object.assign(new Array(48).fill(null), { 40: M.newCell(62, null, 100) }) })], 16);
    await menuPick("DOUBLE");
    ok(LS().rows === 32 && LS().list[0].repeats === 2 && LS().list[0].base === 16 && LS().list[1].base === 16 && LS().list[1].repeats === 2 &&
       notesAt(1).join() === "10:0:62" && U.toastNow() === "DOUBLED: 2 BARS",
       "DOUBLE: 2 bars; a loop filling the length doubles its repeats, one cut at the end written out first (DOUBLED: 2 BARS)");
    await t.tap("ARP");                                     /* (the stage off: SAVE, CLEAR LOOPS and REVERT leave it so) */
    await t.tap("SAVE"); await t.sleep(1600); await M.kept();
    ok(!M.unsavedThing("loops") && kept && kept.loops.list.length === 2, "SAVE keeps LOOPS");
    ok(stg().off, "... SAVE leaves the stage off");
    await t.tap("ARP");
    await pick(0);
    await t.tap("EDIT");                                    /* (notes on the stage: CLEAR LOOPS empties it too) */
    const onStage = staged();
    await t.tap("ARP");
    await menuPick("CLEAR LOOPS");
    ok(t.top() === "question" && U.top().lines.join("/") === "CLEAR ALL 2 LOOPS?/THE STAGE TOO; THE LENGTH STAYS" && U.top().answers.join() === "CLEAR,KEEP",
       "CLEAR LOOPS asks (CLEAR ALL 2 LOOPS? / THE STAGE TOO; THE LENGTH STAYS)");
    await t.tap("OCT+");
    ok(onStage && !LS().list.length && LS().rows === 32 && LS().tempo === 120 && !staged() && U.toastNow() === "LOOPS CLEARED",
       "... CLEAR: no loops, the stage empty, the length and the properties stay (LOOPS CLEARED)");
    ok(stg().off, "... CLEAR LOOPS leaves the stage off");
    await t.tap("SEL");
    ok(U.top().items[U.top().items.length - 1] === "REVERT", "LOOPS unsaved: REVERT last in its menu");
    await t.tap("HOME");
    await menuPick("REVERT");
    ok(t.top() === "question" && U.top().lines.join("/") === "REVERT LOOPS TO SAVED?", "REVERT asks (REVERT LOOPS TO SAVED?)");
    await t.tap("OCT+");
    ok(LS().list.length === 2 && LS().rows === 32 && !M.unsavedThing("loops") && U.toastNow() === "REVERTED", "... REVERT: LOOPS as saved (REVERTED)");
    ok(stg().off, "... REVERT leaves the stage off");
    U.edit(() => { LS().list[1].muted = true; LS().nextMix.rev = 0x20; LS().view = 4; });
    await pick(1);
    stg().over = true;                                      /* (OVERWRITE on: never saved) */
    await t.tap("SAVE"); await t.sleep(1600); await M.kept();
    U.edit(() => { LS().list.pop(); });
    ok(await M.startUp() && LS().list.length === 2 && LS().list[1].muted && LS().nextMix.rev === 0x20 && LS().sel === 1 && LS().view === 4 &&
       notesAt(0).join() === "0:0:60", "a power cycle: LOOPS as SAVE kept it (its loops, notes, mutes, the next loop's mix, VIEW, the selection), the change not saved gone");
    ok(!stg().off, "... the stage on at the power-on (the switch is not saved)");
    ok(!stg().over, "... OVERDUB at the power-on (the mode is not saved)");
    t.frame();
    ok(U.screen.selected() === 1, "... the selection back");
    U.edit(() => { LS().view = 2; });

    /* ---- SHIFT wherever a loop is read: SAVE TO PATTERN, the loops' pattern as heard, QUANTIZE's lines, LENGTH (written out as
     * heard, SHIFT 0; the CUT question counting the notes as heard), DOUBLE (SHIFT kept) ---- */
    const eight = () => cs(16, [[0, 0, 60], [2, 0, 62], [4, 0, 64], [6, 0, 65], [8, 0, 67], [10, 0, 69], [12, 0, 71], [14, 0, 72]]);
    {
        /* base 4 x2, its note at 1 sounding into its rest, SHIFT 3, over 16 rows: heard at 4 and 8, its rest's note off at 11 */
        setLoops([mkLoop({ base: 4, repeats: 2, shift: 3, cells: cs(4, [[1, 0, 60]]) })], 16);
        await pick(0);
        const free = M.freePattern();
        await menuPick("SAVE TO PATTERN"); await t.tap("SEL");
        const p = M.pattern(free), at = r => p && p.cells[r * 16];
        ok(p && p.tracks === 1 && isNote(at(4)) && isNote(at(8)) && isOff(at(11)) && !at(1) && !at(5),
           "SAVE TO PATTERN: each loop written out as it is heard, SHIFT included (base 4 x2, its note at 1, SHIFT 3: rows 4 and 8, its rest's note off at 11)");
        U.edit(() => { M.state.patterns[free] = null; });
        const v = U.loopsPattern(), vc = r => v.cells[r * 16];
        ok(isNote(vc(4)) && isNote(vc(8)) && isOff(vc(11)) && !vc(1) && !vc(5), "the loops as they are heard (their pattern): SHIFT applied");
        setLoops([mkLoop({ shift: 1, cells: cs(16, [[1, 0, 60], [3, 0, M.OFF]]) })], 64);
        await menuPick("QUANTIZE"); U.top().sel = 2; await t.tap("SEL");
        ok(notesAt(0).join() === "3:0:60" && isOff(cell(0, 5)) && LS().list[0].shift === 1,
           "QUANTIZE: the lines counted as the loop is heard (SHIFT 1: its note at 1 heard at 2, to the line at 4: back in its base at 3, its note off with it)");
        setLoops([mkLoop({ cells: cs(16, [[14, 0, 60], [15, 0, M.OFF]]) })], 64);
        await menuPick("QUANTIZE"); U.top().sel = 2; await t.tap("SEL");
        ok(notesAt(0).join() === "0:0:60" && isOff(cell(0, 1)), "QUANTIZE: a note rounded to the base's end goes to its start, its note off moved with it");
        {   /* (at 6 ticks a row: a note delayed a tick, its key off 9 ticks after its start: 1 1/2 rows, a half row up) */
            const rc = cs(16, [[0, 0, 60]]);
            rc[0].cmds.push({ c: 0x0E, v: 0xD1 });
            rc[4] = Object.assign(M.newCell(null, null, null), { cmds: [{ c: 0x14, v: 4 }] });
            setLoops([mkLoop({ cells: rc })], 64);
            U.edit(() => { LS().ticks = 6; });
            await menuPick("QUANTIZE"); U.top().sel = 3; await t.tap("SEL");
            ok(notesAt(0).join() === "0:0:60" && isOff(cell(0, 2)) && !cell(0, 1) && !cell(0, 0).cmds.length,
               "QUANTIZE ROW: the delay and the key off gone, a length of 1 1/2 rows rounded up (its note off at 2), in whole ticks");
        }
        setLoops([mkLoop({ shift: 8, cells: cs(16, [[12, 0, 60], [14, 0, M.OFF]]) })], 16);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", 1); await t.tap("GLO");
        const L = LS().list[0];
        ok(LS().rows === 32 && !L.shift && L.base === 16 && L.repeats === 1 && notesAt(0).join() === "4:0:60" && isOff(cell(0, 6)),
           "LENGTH longer with a SHIFT: the loop first written out as it was heard (SHIFT 8: its note at 12 heard at 4), SHIFT 0; the bar added silent");
        /* a loop whose last repeat the old length cut: base 6 x3 over 16 rows plays 0-15, the third repeat cut at 16 */
        setLoops([mkLoop({ base: 6, repeats: 3, cells: cs(6, [[0, 0, 60]]) })], 16);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", 1); await t.tap("GLO");
        const C = LS().list[0], pv = U.loopsPattern(), pc = r => pv.cells[r * 16], quiet = [...Array(15).keys()].every(k => !pc(17 + k));
        ok(LS().rows === 32 && C.base === 16 && C.repeats === 1 && notesAt(0).join() === "0:0:60,6:0:60,12:0:60" && isOff(pc(16)) && quiet && !C.cells.some(isOff),
           "LENGTH longer: a loop whose last repeat the old length cut written out over it first (base 16, once): rows 16-31 silent, the note at the old line ending there");
        setLoops([mkLoop({ base: 32, shift: 20, cells: cs(32, [[4, 0, 60]]) })], 32);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -1); await t.tap("GLO");
        const asked = t.top() === "question";
        if (asked) await t.tap("OCT-");
        setLoops([mkLoop({ base: 32, shift: 12, cells: cs(32, [[24, 0, 60]]) })], 32);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", -1); await t.tap("GLO");
        ok(asked && !t.top() && LS().rows === 16 && notesAt(0).join() === "4:0:60" && !LS().list[0].shift,
           "LENGTH shorter: the CUT question counts the notes as heard (a note at 4 heard at 24: asked; one at 24 heard at 4: cut at once, kept, SHIFT 0)");
        setLoops([mkLoop({ shift: 3, cells: cs(16, [[0, 0, 60]]) })], 16);
        await menuPick("DOUBLE");
        const w = U.loopsPattern(), heard = [...Array(32).keys()].filter(r => isNote(w.cells[r * 16])).join();
        ok(LS().rows === 32 && LS().list[0].shift === 3 && LS().list[0].repeats === 2 && heard === "3,19",
           "DOUBLE keeps the SHIFT: the loop plays the same, twice (rows " + heard + ")");
    }

    /* ---- REC ends the arm; PLAY's red after LOOPS' stop; EMPTY's place; PLAY outside LOOPS with no loops ---- */
    {
        await stopAll();
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        await pick(0);
        P.kept.loops.row = 0;
        await t.tap("REC");
        const armed = !!sc().arm;
        await t.tap("EDIT");
        await t.sleep(400);                                 /* (REC's 500 ms after arming) */
        await t.tap("REC");
        const toast = U.toastNow();
        await t.key(7);
        ok(armed && toast === "STAGE INTO LOOP 00" && !sc().arm && !P.playing && !staged(),
           "armed, EDIT, then REC: the stage sent (STAGE INTO LOOP 00) and the arm ended with it: a key then plays freely");
        await t.tap("PLAY"); await t.sleep(100);
        await t.tap("PLAY"); t.frame();
        ok(!P.playing && P.kept.loops.row === 0 && t.leds[B.PLAY] === .25, "PLAY stopping the loops: back to the start, PLAY's red dim (nothing to resume)");
        emptyStage(); t.frame();
        const dg = c565(D.DGREY), xs = [];
        for (let y = 179; y < 191; y++) for (let x = 200; x < 239; x++) if (t.px(x, y) === dg) xs.push(x);
        ok(xs.length && Math.min(...xs) === 208 && Math.max(...xs) === 236,
           "EMPTY at x 208, its last pixel at 236, two clear of the frame (" + (xs.length ? Math.min(...xs) + "-" + Math.max(...xs) : "none") + ")");
        setLoops([], 16);
        await t.tap("PLAY"); await t.sleep(100);
        await t.homeKey(8);
        await t.tap("PLAY");
        const paused = !P.playing && P.what === "loops";
        t.frame();
        const red = t.leds[B.PLAY];
        await t.tap("PLAY"); await t.sleep(100);
        ok(paused && red === 1 && P.playing && P.what === "loops", "PLAY outside LOOPS: pauses the loops with none (PLAY red: paused), and resumes them");
        await stopAll();
        await t.homeKey(22);
    }

    /* ---- a record from before the new fields: filled where it is loaded, no change of its own ---- */
    {
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        await t.tap("SAVE"); await t.sleep(1600); await M.kept();
        const old = structuredClone(kept);
        delete old.loops.view; delete old.loops.knobs;
        for (const l of old.loops.list) delete l.shift;
        kept = structuredClone(old);
        const up = await M.startUp();
        t.frame();
        ok(up && LS().view === 2 && LS().knobs.join() === "LEVEL,PAN,REVERB SEND,TEMPO" && LS().list[0].shift === 0 && !M.unsavedThing("loops") && !M.unsaved,
           "a record from before VIEW, K1-K4's jobs and SHIFT: read with their defaults, filled where it is loaded (no unsaved change)");
        old.loops.list = []; old.loops.rows = 0; old.loops.rpb = 8; old.loops.fx.knobs.METRONOME[1] = 5;
        kept = structuredClone(old);
        await M.startUp();
        t.frame();
        ok(LS().rows === 120 && !M.unsaved, "... one with no loops and no length: 4 bars, or the most whole bars that fit 128 rows (1/32, 5 beats a bar: 3 bars, " +
           LS().rows + " rows)");
        U.edit(() => { LS().rpb = 4; LS().fx.knobs.METRONOME[1] = 4; });
        setLoops([], 64);
    }

    /* ---- the stage: a key silences only the lanes it replaces, from its landing row; heard whatever the mutes and solos ---- */
    {
        await stopAll();
        setLoops([], 16);
        U.edit(() => { LS().tempo = 300; LS().quantize = "4 ROWS"; LS().nextMono = false; });
        await pick(0);
        const s = stg();
        s.cells = new Array(64).fill(null);
        /* (every lane full, a pitch a lane: OVERDUB's key takes lane 0, the first whose note ends soonest, and silences it alone) */
        for (let r = 0; r < 16; r++) [60, 64, 67, 71].forEach((m, l) => { s.cells[r * 4 + l] = M.newCell(m, null, 100); });
        M.version++;
        const rowsT = {}, ons = [], flushes = [], hook = P.loopsRow, on0 = A.noteOn, fl0 = A.flushIds, flg = A.flush;
        P.loopsRow = function (row, time, dur, pass) { rowsT[pass * 100 + row] = time; return hook.apply(this, arguments); };
        A.noteOn = (id, time, midi, ...a) => { ons.push({ id, time, midi }); return on0(id, time, midi, ...a); };
        A.flushIds = (ids, time) => { flushes.push({ ids, time }); return fl0(ids, time); };
        A.flush = (p, time, at) => { flushes.push({ prefix: p, time }); return flg(p, time, at); };
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.pass >= 1 && h.row % 4 === 2 && h.row < 14 && (P.time() - h.t) / h.dur < .5; }, 5000);
        const h = realAt(), q = h.pass * 100, land = h.row + 2;     /* (4 ROWS: lands on the next line, two rows on) */
        flushes.length = 0;                                /* (the start's own flush aside) */
        t.keyDown(9);
        await until(() => { const x = realAt(); return !!x && x.pass * 100 + x.row >= q + land + 1; }, 3000);
        t.keyUp(9); await t.sleep(30);
        P.loopsRow = hook; A.noteOn = on0; A.flushIds = fl0; A.flush = flg;
        await stopAll();
        const gone = o => flushes.some(f => (f.prefix ? o.id[0] === f.prefix : f.ids.includes(o.id)) && o.time >= f.time - 1e-9);
        const stage = ons.filter(o => o.id[0] === "g"), atRow = r => stage.filter(o => rowsT[q + r] != null && Math.abs(o.time - rowsT[q + r]) < 1e-6);
        const before = atRow(land - 1), after = atRow(land).concat(atRow(land + 1));
        const its = after.filter(o => o.midi === 60), others = after.filter(o => o.midi !== 60);
        ok(before.length === 4 && !before.some(gone) && its.every(gone) && others.length === 6 && !others.some(gone) &&
           ons.some(o => /k9$/.test(o.id) && Math.abs(o.time - rowsT[q + land]) < 1e-6),
           "a key landing ahead (4 ROWS), no lane free: the stage's row before its landing still heard, its lane silenced from the landing " +
           "row on, the other lanes heard, the key's note there (" + its.length + " of its lane, " + others.length + " others)");
        U.edit(() => { LS().quantize = "ROW"; });
        /* the stage heard whatever the mutes and solos: a column of its own, carrying the loop's mix */
        setLoops([mkLoop({ muted: true, cells: eight() }), mkLoop({ col: 1, cells: cs(16, [[2, 0, 67]]) })], 16);
        U.edit(() => { LS().tempo = 300; });
        await pick(0); await t.tap("EDIT");
        const mutes = {}, m0 = A.trackMute, got = [];
        A.trackMute = (tr, on) => { mutes[tr] = on; return m0(tr, on); };
        A.noteOn = (id, time, midi, ref, vol, track, o) => { got.push({ id, track }); return on0(id, time, midi, ref, vol, track, o); };
        P.mutesChanged();
        P.kept.loops.row = 0; await t.tap("PLAY"); await t.sleep(900);
        const muted = mutes[16] === true && got.some(x => x.id[0] === "g" && x.track === STAGE_T) && stageCol === 16;
        got.length = 0;
        U.edit(() => { LS().list[0].muted = false; LS().list[1].soloed = true; }); P.mutesChanged();
        await t.sleep(900);
        A.noteOn = on0; A.trackMute = m0;
        await stopAll();
        ok(muted && mutes[16] === true && mutes[17] === false && !mutes[STAGE_T] && got.some(x => x.id[0] === "g" && x.track === STAGE_T),
           "the stage heard whatever the mutes and solos (loop 00 muted, then another soloed): its column carries the loop's mix, never a mute");
        U.edit(() => { LS().list[1].soloed = false; });
    }

    /* ---- the pattern recorder's preview and its single notes on the stage's column: heard though the loop is muted ---- */
    {
        await stopAll();
        setLoops([mkLoop({ muted: true })], 16);
        U.edit(() => { LS().tempo = 300; });
        await pick(0);
        const mutes = {}, m0 = A.trackMute, got = [], on0 = A.noteOn;
        A.trackMute = (tr, on) => { mutes[tr] = on; return m0(tr, on); };
        P.mutesChanged();
        A.noteOn = (id, time, midi, ref, vol, track, o) => { got.push({ id, track }); return on0(id, time, midi, ref, vol, track, o); };
        stageCol = null;
        await openRec();
        const rec = U.screen.recorder();
        for (const r of rec.rows) r.steps.fill(false);
        rec.rows[0].steps[0] = rec.rows[0].steps[2] = true;
        await t.sleep(700);
        await t.turn("K3", 1);
        await t.tap("HOME");
        A.noteOn = on0; A.trackMute = m0;
        ok(mutes[16] === true && got.some(x => x.id[0] === "v" && x.track === STAGE_T) && got.some(x => x.id === "rv" && x.track === STAGE_T) && stageCol === 16 &&
           !got.some(x => x.track === 16), "the pattern recorder's preview and a single note (K3) on the stage's column, carrying the loop's mix: heard though the loop is muted");
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- REC: stray note offs left out ---- */
    {
        setLoops([], 16);
        U.edit(() => { LS().nextMono = false; });
        await pick(0);
        const s = stg();
        s.cells = new Array(64).fill(null);
        const put = (r, l, n) => {                         /* (as played onto the stage: an overdub's cell, its pass marked) */
            s.cells[r * 4 + l] = Object.assign(n === "K" ? Object.assign(M.newCell(null, null, null), { cmds: [{ c: 0x14, v: 2 }] }) : n === M.OFF ? M.newCell(M.OFF, null, null)
                                               : M.newCell(n, null, 100), { wp: 0 });
        };
        put(0, 0, 60); put(4, 0, M.OFF); put(8, 0, M.OFF); put(10, 0, "K");    /* (lane 0: its note's end, then two ending nothing) */
        put(2, 1, M.OFF); put(5, 1, 64);                                     /* (lane 1: an end before any note; a note to the end) */
        put(12, 2, 67); put(0, 2, M.OFF);                                    /* (lane 2: its note ended round from the length's end) */
        put(0, 3, M.OFF);                                                    /* (lane 3: nothing but a note off) */
        M.version++;
        await t.tap("REC");
        const L = LS().list[0], c = (r, l) => L && L.cells[r * 4 + l];
        ok(L && isNote(c(0, 0)) && isOff(c(4, 0)) && !c(8, 0) && !c(10, 0) && !c(2, 1) && isNote(c(5, 1)) && isOff(c(0, 1)) && isOff(c(0, 2)) && !c(0, 3) && L.lanes === 3 &&
           !L.cells.some(x => x && (x.wp != null || x.sub != null)),
           "REC: a note off or key off that ends nothing left out (lane 0's 8 and 10, lane 1's 2, lane 3's 0); a note played at the end ends at the line, one ended " +
           "round from the length's end kept");
        /* what EDIT brought, as it is: SHIFT 2 turns a note at 12 ended at 15 into one at 14 ended past the line, at 1 */
        setLoops([mkLoop({ shift: 2, cells: cs(16, [[12, 0, 60], [15, 0, M.OFF]]) })], 16);
        await pick(0);
        const rowsOf = () => { const v = U.loopsPattern(); return [...Array(16).keys()].map(r => { const x = v.cells[r * 16]; return x ? x.note : "-"; }).join(); };
        const before = rowsOf();
        await t.sleep(400);                                 /* (REC's 500 ms after the REC before) */
        await t.tap("EDIT"); await t.tap("REC");
        const E = LS().list[0];
        ok(U.toastNow() === "STAGE INTO LOOP 00" && !E.shift && notesAt(0).join() === "14:0:60" && isOff(cell(0, 1)) && !cell(0, 0) && rowsOf() === before,
           "REC writes what EDIT brought as it is: a note across the line keeps its end past it (row 1, not 0); the loop plays the same (" + before + ")");
        /* a full stage whose played notes still sound at the line: their ends at row 0 take the loop past 3,800 B */
        setLoops([], 128);
        await pick(0);
        const fill = played => Object.assign(M.newCell(60, null, 100), { cmds: [0, 1, 2, 3].map(() => ({ c: 0x80, v: 1 })) }, played ? { wp: 0 } : {});
        const full = played => { const f = stg(); f.cells = new Array(512).fill(null); for (let k = 0; k < 292; k++) f.cells[128 + k] = fill(played); M.version++; };
        await stageOn();
        full(true);                                    /* (292 notes of 13 B from row 32: 3,798 B with the skip's byte and the 0x00,
                                                         * every lane's last sounding at the line) */
        const was = sNotes();
        U.flash[B.REC] = 0;
        await t.sleep(500);
        await t.tap("REC");
        const refusedFull = U.toastNow() === "THE STAGE IS FULL" && U.flash[B.REC] > U.now() && !LS().list.length && sNotes() === was;
        full(false);                                   /* (the same notes brought by EDIT: written as they are, no end added) */
        await t.tap("REC");
        ok(refusedFull && U.toastNow() === "STAGE AS LOOP 00" && LS().list.length === 1 && !staged(),
           "REC: a loop past 3,800 B with the ends at the line added (four played notes sounding there): refused (THE STAGE IS FULL, REC " +
           "blinking), nothing changed; the same notes EDIT brought: written");
    }

    /* ---- the stage rests while a song is armed ---- */
    {
        setLoops([mkLoop({ cells: eight() })], 16);
        U.edit(() => { LS().tempo = 300; LS().countIn = "OFF"; });
        await pick(0); await t.tap("EDIT");
        P.kept.loops.row = 0; await t.tap("PLAY"); await t.sleep(200);
        const silent0 = P.lpSilent(0);
        await menuPick("RECORD LIVE"); await t.tap("REC");
        const ids = [], on0 = A.noteOn;
        A.noteOn = (id, ...a) => { ids.push(id); return on0(id, ...a); };
        await t.sleep(900);
        A.noteOn = on0;
        t.down("LFO"); t.up("LFO"); await t.sleep(80); t.frame();
        const lfo = !!stg().erasers.length;
        await t.sleep(60);
        t.down("LFO"); t.up("LFO"); await t.sleep(30);
        ok(silent0 && sc().song && sc().song.state === "armed" && staged() && P.lpSilent(0) && !ids.some(i => /^s16\./.test(i)) && ids.some(i => i[0] === "g") && lfo,
           "a song armed: the stage works as ever (heard, the selected loop silent, LFO's OVERWRITE)");
        await menuPick("CANCEL RECORD LIVE");
        ok(!sc().song && P.lpSilent(0), "... the song cancelled: the stage as it was, the selected loop silent");
        await stopAll();
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- a jump: the keys held stop writing, a loop landing in its rest falls silent, the stage plays what it holds
     * from there, OVERWRITE goes on from where the loops land ---- */
    {
        setLoops([mkLoop({ cells: eight() })], 16);
        U.edit(() => { LS().quantize = "ROW"; LS().nextMono = false; });
        await pick(0); await t.tap("EDIT");
        await slow();
        press(9, 3, .1);
        frameAt(5, .2);
        heardAt(5, .3); await t.turn("ALGORITHM", 4); real();
        frameAt(9, .5);
        lift(9, 10, .1);
        ok(isNote(sCell(3, 1)) && sCell(3, 1).note === U.midi(9) && isOff(sCell(5, 1)) && isNote(sCell(4)) && isNote(sCell(6)) && isNote(sCell(8)) && !stg().keys[9],
           "a jump: the key held stops writing (its note ends where the line was, row 5); the rows after keep their notes");
        await t.tap("EDIT");
        heardAt(2, .2); t.down("LFO"); t.up("LFO"); real(); await t.sleep(30);
        frameAt(4, .5);
        const jumps = stg().jumps + 1;
        P.at = () => ({ row: 11, pass: 0, t: P.time() - .25, dur: .5, rpb: 4, jumps, land: { row: 10, pass: 0 } });   /* (landed on row 10) */
        t.frame(); real();
        await t.sleep(30);
        await t.tap("LFO");
        ok(!isNote(sCell(2)) && !isNote(sCell(4)) && isNote(sCell(6)) && isNote(sCell(8)) && !isNote(sCell(10)),
           "OVERWRITE across a jump: it goes on from where the loops land (rows 6 and 8, jumped over, keep their notes; 10 on erased)");
        await stopAll();
        emptyStage();
        setLoops([mkLoop({ base: 8, cells: cs(8, [[0, 0, 60]]) })], 32);
        U.edit(() => { LS().tempo = 300; });
        const rows = [], offs = [], ons = [], hook = P.loopsRow, on0 = A.noteOn, off0 = A.noteOff;
        P.loopsRow = function (row, time) { rows.push({ row, time }); return hook.apply(this, arguments); };
        A.noteOn = (id, time, ...a) => { ons.push({ id, time }); return on0(id, time, ...a); };
        A.noteOff = (id, time) => { offs.push({ id, time }); return off0(id, time); };
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.pass === 0 && h.row >= 1; }, 3000);
        t.down("SEL"); await t.sleep(10); await t.turn("ALGORITHM", 1); t.up("SEL"); await t.sleep(300);
        P.loopsRow = hook; A.noteOn = on0; A.noteOff = off0;
        await stopAll();
        const k = rows.findIndex((r, x) => x && r.row !== (rows[x - 1].row + 1) % 32), land = k > 0 ? rows[k] : null;
        const note = ons.find(o => /^s16\.0\./.test(o.id));
        ok(land && land.row >= 16 && note && offs.some(o => o.id === note.id && Math.abs(o.time - land.time) < 1e-6),
           "a jump landing in a loop's rest: the loop falls silent there (its note ends on the row it lands on, " + (land ? land.row : "-") + ")");
        setLoops([], 32);
        U.edit(() => { LS().tempo = 300; LS().quantize = "ROW"; });
        const heard = [];
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.pass === 0 && h.row >= 12; }, 3000);
        t.keyDown(9); await t.sleep(60); t.keyUp(9);
        await until(() => { const h = realAt(); return !!h && h.row >= 22; }, 3000);
        A.noteOn = (id, time, midi, ...a) => { heard.push({ id, midi }); return on0(id, time, midi, ...a); };
        t.down("SEL"); await t.sleep(10); await t.turn("ALGORITHM", -1); t.up("SEL"); await t.sleep(400);
        A.noteOn = on0;
        const back = realAt();
        await stopAll();
        ok(heard.some(o => o.id[0] === "g" && o.midi === U.midi(9)), "a jump back in the same pass: the note just played onto the stage is heard from it as the line " +
           "reaches it again (" + (back ? "row " + back.row + ", pass " + back.pass : "-") + ")");
        emptyStage();
        /* a jump is a switch of RECORD LIVE's rest's too (a song recording follows the loops from where they land); while
         * the rest keeps them silent, a jump moves their place */
        setLoops([mkLoop({ cells: eight() })], 16);
        U.edit(() => { LS().tempo = 300; });
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.row >= 2 && h.row < 10; }, 3000);
        const sw0 = P.lpRestState().sw;
        P.lpJump(-1);
        await t.sleep(400);
        const rs = P.lpRestState(), landed = realAt() && realAt().land;
        P.lpRest(1);
        await until(() => P.lpRestState().on === 1, 3000);
        const kr = P.lpRestState().kr;
        P.lpJump(3);
        const kr2 = P.lpRestState().kr;
        P.lpRest(0);
        await stopAll();
        ok(rs.sw === sw0 + 1 && landed && rs.swRow === landed.row && kr2 === (kr + 3) % 16,
           "a jump: a switch of the rest's from where the loops land (row " + (landed ? landed.row : "-") + "); silenced by the rest, their place moved (" +
           kr + " to " + kr2 + ")");
    }

    /* ---- a POLY stage switched to MONO: its lanes 1-3's notes end, lane 0's rings on (one voice); a SHIFT's cut repeat
     * ended at its own row 0 in the first pass too ---- */
    {
        setLoops([], 16);
        U.edit(() => { LS().tempo = 300; LS().nextMono = false; });
        await pick(0);
        const s = stg();
        s.cells = cs(16, [[0, 0, 60], [0, 1, 64], [0, 2, 67]]);
        M.version++;
        const ons = [], offs = [], on0 = A.noteOn, off0 = A.noteOff;
        A.noteOn = (id, time, midi, ...a) => { ons.push({ id, time, midi }); return on0(id, time, midi, ...a); };
        A.noteOff = (id, time) => { offs.push({ id, time }); return off0(id, time); };
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { const h = realAt(); return !!h && h.row >= 3; }, 3000);
        U.edit(() => { LS().nextMono = true; });
        await t.sleep(300);
        A.noteOn = on0; A.noteOff = off0;
        await stopAll();
        const id = l => { const o = ons.find(x => x.id.startsWith("g32." + l + ".")); return o && o.id; };
        const ended = l => offs.some(o => o.id === id(l));
        ok(id(0) && id(1) && id(2) && ended(1) && ended(2) && !ended(0), "a POLY stage switched to MONO: its lanes 1-3's notes end, lane 0's rings on (one voice)");
        U.edit(() => { LS().nextMono = false; });
        emptyStage();
        setLoops([mkLoop({ base: 6, repeats: 3, shift: 4, cells: cs(6, [[2, 0, 62]]) })], 16);
        const calls = [], rows = [], hook = P.loopsRow;
        A.noteOn = (id, time, midi, ...a) => { calls.push({ on: 1, id, time, midi }); return on0(id, time, midi, ...a); };
        A.noteOff = (id, time) => { calls.push({ on: 0, id, time }); return off0(id, time); };
        P.loopsRow = function (row, time, dur, pass) { rows.push({ row, time, pass }); return hook.apply(this, arguments); };
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => rows.some(r => r.pass === 0 && r.row >= 5), 3000);
        A.noteOn = on0; A.noteOff = off0; P.loopsRow = hook;
        await stopAll();
        const at = (p, r) => { const x = rows.find(y => y.pass === p && y.row === r); return x ? x.time : -1; };
        const cut = calls.find(c => c.on && Math.abs(c.time - at(0, 2)) < 1e-6);
        ok(cut && calls.some(c => !c.on && c.id === cut.id && Math.abs(c.time - at(0, 4)) < 1e-6),
           "SHIFT 4, base 6 x3 over 16 rows: its cut repeat's note (row 2) ends at its own row 0 (row 4) in the first pass");
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- OVERWRITE on through a stop: PLAY again, the line erases again from where the loops are heard first (the
     * erasing of the run before, which stopped at row 11, does not wait for its row) ---- */
    {
        setLoops([mkLoop({ cells: eight() })], 16);
        await pick(0); await t.tap("EDIT");
        U.edit(() => { LS().tempo = 300; });
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { t.frame(); const h = realAt(); return !!h && h.row >= 9 && h.row < 12; }, 3000);
        t.down("LFO"); await t.sleep(10); t.up("LFO"); await t.sleep(10);
        const over = stg().over;
        await t.tap("PLAY"); await t.sleep(60);
        const s = stg();
        s.cells = new Array(64).fill(null);
        for (let r = 0; r < 16; r += 2) s.cells[r * 4] = M.newCell(60 + r, null, 100);
        M.version++;
        P.kept.loops.row = 0; await t.tap("PLAY");
        await until(() => { t.frame(); const h = realAt(); return !!h && h.row >= 9 && h.row < 12; }, 3000);
        t.frame();
        const erased = [4, 6, 8].every(r => !isNote(sCell(r))), ahead = isNote(sCell(14));
        await t.tap("PLAY"); await t.sleep(60);
        await t.tap("LFO");
        ok(over && stg().over === false && erased && ahead,
           "OVERWRITE on through a stop: PLAY again, the line erases again from where the loops are heard (rows 4-8 gone, row 14 ahead " +
           "kept); LFO: OVERDUB");
        emptyStage();
        U.edit(() => { LS().tempo = 120; });
    }

    /* ---- the keys down stop writing at a length change and when FX opens ---- */
    {
        setLoops([mkLoop({ cells: eight() })], 16);
        await pick(0); await t.tap("EDIT");
        await slow();
        press(9, 3, .1); frameAt(5, .2);
        heardAt(5, .3); await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", 1); await t.tap("GLO"); real();
        frameAt(9, .5); lift(9, 10, .1);
        ok(LS().rows === 32 && isNote(sCell(3, 1)) && isOff(sCell(5, 1)) && isNote(sCell(4)) && isNote(sCell(6)) && isNote(sCell(8)),
           "a length change: the key held stops writing (its note ends where the line was); the rows after keep their notes");
        press(11, 9, .1); frameAt(10, .2);
        heardAt(10, .3); await t.tap("FX"); real();
        const onFx = U.screen.name === "FX";
        heardAt(13, .5); await t.tap("FX"); t.frame(); real();
        lift(11, 14, .1);
        ok(onFx && U.screen.name === "LOOPS" && isNote(sCell(9, 1)) && isOff(sCell(10, 1)) && isNote(sCell(10)) && isNote(sCell(12)),
           "FX from LOOPS: the key held stops writing (on FX the keys are FX's) (" + sNotes() + ")");
        await stopAll();
    }

    /* ---- GRID with notes on the stage; LENGTH in whole bars ---- */
    {
        setLoops([], 64);
        const s = stg();
        s.cells = new Array(256).fill(null); s.cells[0] = M.newCell(60, null, 100); M.version++;
        await t.tap("GLO"); U.top().sel = 3; await t.turn("K1", 1);
        const toast = U.toastNow(), dark = !!U.top().items[3].fixed;
        await t.tap("GLO");
        ok(toast === "GRID: ONLY WITH THE STAGE EMPTY" && dark && LS().rpb === 4, "GRID with notes on the stage: refused (GRID: ONLY WITH THE STAGE EMPTY), drawn dark grey");
        emptyStage();
        U.edit(() => { LS().fx.knobs.METRONOME[1] = 3; });          /* (a bar of 12 rows: 64 rows are 5 1/3 bars, 16 beats) */
        t.frame();
        const lg = c565(D.LGREY), field = [], past = [], top = U.screen.lengthShown();
        for (let y = 19; y < 31; y++) for (let x = 50; x < 146; x++) if (t.px(x, y) === lg) (x < 98 ? field : past).push(x);
        await t.tap("GLO");
        const shown = U.top().items[2].get();
        U.top().sel = 2; await t.turn("K1", -1);
        const planned = U.top().items[2].get();
        await t.tap("GLO");
        const down = LS().rows;
        ok(top === "16 BEATS" && field.length && Math.max(...field) > 92 && !past.length && shown === "16 BEATS" && planned === "5 BARS (NOW 16 BEATS)",
           "a part bar shown in beats: the top line's 16 BEATS (8 characters, in its field), GLO's LENGTH 16 BEATS, a shorter one planned 5 BARS (NOW 16 BEATS)");
        setLoops([], 64);
        await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", 1); await t.tap("GLO");
        ok(down === 60 && LS().rows === 72 && !t.top(), "LENGTH from a part bar: the next whole bar each way (5 1/3 bars: down to 5, up to 6)");
        setLoops([], 64);
        await t.tap("GLO"); U.top().sel = 3; await t.turn("K1", 1); await t.tap("GLO");
        ok(LS().rpb === 6 && LS().rows === 90, "GRID from a part bar: its whole bars rounded down (5 1/3 bars at 1/16: 5 bars at 1/16T, " + LS().rows + " rows)");
        U.edit(() => { LS().rpb = 4; LS().fx.knobs.METRONOME[1] = 4; });
        setLoops([], 64);
    }

    /* ---- the stage in the edit memory: its room, the notes it cannot take, kept through SAVE and REVERT ALL, gone at a
     * power cycle ---- */
    {
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, 62]]) })], 16);
        U.edit(() => { LS().nextMono = false; });
        await pick(0);
        const base0 = M.editsUsed, un0 = M.unsaved;
        await t.tap("EDIT");
        ok(M.editsUsed - base0 === M.pieceRoom(8) && M.unsaved === un0, "the stage takes room in the edit memory (its stream, 8 B as the " +
           "firmware encodes it: a piece of " + M.pieceRoom(8) + "), no unsaved change");
        await slow();
        const calls = [], on0 = A.noteOn;
        A.noteOn = (id, ...a) => { calls.push(id); return on0(id, ...a); };
        const was = M.EDITS_MAX, before = sNotes();
        M.EDITS_MAX = M.editsUsed + 2;                     /* (the note and its end would take 4 B more of rounded room) */
        press(9, 6, .1); lift(9, 7, .1);
        const full = U.toastNow();
        M.EDITS_MAX = was;
        ok(full === "MEMORY FOR EDITS IS FULL" && sNotes() === before && !calls.some(i => /k9$/.test(i)), "the edit memory full: a note neither written nor heard (MEMORY FOR EDITS IS FULL)");
        U.saving = true;
        press(9, 6, .1); lift(9, 7, .1);
        const busy = U.toastNow();
        U.saving = false;
        ok(busy === "STAGE NOT WRITTEN: STORAGE BUSY" && sNotes() === before && !calls.some(i => /k9$/.test(i)),
           "the store busy (a save being written): STAGE NOT WRITTEN: STORAGE BUSY, the note neither written nor heard");
        await stopAll();
        setLoops([], 128);
        const s = stg(), fill = () => Object.assign(M.newCell(60, null, 100), { cmds: [0, 1, 2, 3].map(() => ({ c: 0x80, v: 1 })) });
        s.cells = new Array(512).fill(null);
        for (let k = 0; k < 292; k++) s.cells[128 + k] = fill();          /* (292 notes of 13 B from row 32: 3,798 B with the skip's
                                                         * byte and the 0x00; a note at row 6 and its end's 3 B pass 3,800) */
        M.version++;
        await slow();
        press(9, 6, .1); lift(9, 7, .1);
        ok(U.toastNow() === "THE STAGE IS FULL" && !isNote(sCell(6)) && !calls.some(i => /k9$/.test(i)), "a stage at its 3,800 B: a note neither written nor heard (THE STAGE IS FULL)");
        A.noteOn = on0;
        await stopAll();
        /* EDIT or DOUBLE past the stage's 3,800 B: refused, nothing changed */
        const dense = base => Array.from({ length: base * 4 }, fill);         /* (13 B a cell) */
        setLoops([mkLoop({ lanes: 4, repeats: 8, cells: dense(16) })], 128); /* (832 B a base, 8 times over the length: 6,657 B) */
        await pick(0); await t.tap("EDIT");
        const editRefused = U.toastNow() === "THE STAGE IS FULL" && !staged() && U.flash[B.EDIT] > U.now() && !P.lpSilent(0);
        setLoops([mkLoop({})], 64);
        const big = stg();
        big.cells = new Array(256).fill(null);
        for (let k = 0; k < 200; k++) big.cells[k] = fill();                 /* (2,601 B: twice is past it) */
        M.version++;
        const was2 = sNotes();
        await menuPick("DOUBLE");
        ok(editRefused && U.toastNow() === "THE STAGE IS FULL" && LS().rows === 64 && sNotes() === was2 && LS().list[0].repeats === 1 && U.flash[B.SEL] > U.now(),
           "EDIT or DOUBLE past the stage's 3,800 B: refused (THE STAGE IS FULL, EDIT or SEL blinking), nothing changed");
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        await pick(0); await t.tap("EDIT");
        const sv = sNotes(), piece = M.editsUsed - M.editsWithout();
        await t.tap("SAVE"); await t.sleep(1600); await M.kept();
        const saved = sNotes() === sv && !M.unsaved && M.editsUsed === piece && piece > 0 && P.lpSilent(0);
        U.edit(() => { LS().tempo = 121; });
        U.revert(); await t.tap("OCT+"); await t.sleep(800);
        const all = sNotes() === sv && LS().tempo !== 121 && !M.unsaved && M.editsUsed === piece;
        U.edit(() => { LS().tempo = 122; });
        await menuPick("REVERT"); await t.tap("OCT+");
        const own = sNotes() === sv && LS().tempo !== 122;
        await M.startUp();
        ok(saved && all && own && !staged() && !M.editsUsed, "the stage kept through SAVE, REVERT ALL and LOOPS' REVERT (no change, its room kept); a power cycle drops it");
    }

    /* ---- CONTROL mode's numbers 10-16 at x 2; ADJUST: LFO held + REC does nothing ---- */
    {
        setLoops([...Array(10).keys()].map(k => mkLoop({ col: k % 12 })), 16);
        t.frame();                                         /* (the selection as the power cycle left it, taken up) */
        await pick(9);
        await t.tap("SEQ"); t.frame();
        /* the rightmost white pixel of a slot's number: ` 9` drawn at 5, `10` at 2 (their last digits' glyphs alike) */
        const right = i => { const y = 33 + 32 * (i - sc().top) + 8, w = c565(D.WHITE); let m = -1;
                             for (let yy = y; yy < y + 16; yy++) for (let x = 1; x < 26; x++) if (t.px(x, yy) === w) m = Math.max(m, x); return m; };   /* (the play line at 27 aside) */
        const r9 = right(8), r10 = right(9), top = sc().top, ctl = sc().control;
        await t.tap("SEQ");
        ok(ctl && top === 6 && r9 > 0 && r10 === r9 - 3, "CONTROL mode: the keys' numbers 10-16 drawn at x 2, ` 1`-` 9` at 5 (loop 09's key 10 ends at " + r10 +
           ", 3 px left of key 9's " + r9 + ")");
        setLoops([mkLoop({ shift: 3, muted: true }), mkLoop({ shift: 3, col: 1 })], 16);
        await pick(2); t.frame();
        const shiftIn = (y, c) => { for (let yy = y; yy < y + 12; yy++) for (let x = 118; x < 166; x++) if (t.px(x, yy) === c565(c)) return true; return false; };
        ok(shiftIn(33, D.DGREY) && !shiftIn(33, D.LGREY) && shiftIn(65, D.LGREY), "a slot's SHIFT light grey, dark grey while the loop is silenced (muted)");
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        await pick(0);
        await menuPick("ADJUST");
        t.down("LFO"); await t.sleep(40); await t.tap("REC"); t.up("LFO"); await t.sleep(60);
        ok(t.top() === "adjust" && !stg().over && U.flash[B.REC] > U.now(), "ADJUST: LFO and REC do nothing (REC blinks, OVERWRITE not on)");
        await t.tap("HOME");
    }

    /* ---- RECORD LIVE (fw/tests/ui3_test.c's test_loops_live and test_loops_tap, step for step; the loops' rows 250 ms,
     * 234 after the tempo change): its panel; armed, PLAY starts the loops and the song; a pattern of the rows heard, the
     * loops' notes as they sounded, a mute from its row, the keys as they land on the stage, an effect switched on, a
     * punch-in, FX turned, a tempo change and a loop's mix turned as commands at their rows; PLAY silencing the loops and
     * playing them on again, held taking them to their start, the song going on; HOME ending it at the row it was pressed
     * in; REC starting it at the row playing; leaving LOOPS; 16 tracks; stopping by itself; every LOOPS function during a
     * song, written as heard; only notes with a voice ---- */
    {
        const sg = () => sc().song, lvs = () => U.screen.liveState(), row = () => U.screen.liveRow();
        const tickNow = () => { const h = realAt(); return h ? Math.min(LS().ticks - 1, Math.floor((P.time() - h.t) / h.dur * LS().ticks)) : 0; };
        /* until the song's row `r` is heard, at tick `k` or later: true there */
        const lvAt = async (r, k) => { await until(() => row() > r || (row() === r && tickNow() >= k)); return row() === r; };
        /* until the player's last switch is that one (the loops silent, or on again at that row): its song row, -1 none */
        const lvSwitch = async (rest, r) => {
            const is = () => { const s = P.lpRestState(); return s.on === rest && (rest || s.swRow === r); };
            await until(is, 3000);
            return is() && lvs() ? P.lpRestState().swN - realAt().n + row() : -1;
        };
        /* SEL's RECORD LIVE, then its panel's REC: armed */
        const lvArm = async () => {
            await menuPick("RECORD LIVE");
            const panel = t.top() === "live";
            await t.tap("REC");
            return panel && !!sg() && sg().state === "armed" && !t.top();
        };
        const pc = (p, r, tr) => p && p.cells[r * 16 + tr];
        const has = (c, code, v) => !!c && c.cmds.some(x => x.c === code && (v == null || x.v === v));
        const cmdRow = (p, r, code, v) => [...Array(16).keys()].some(tr => has(pc(p, r, tr), code, v));
        /* a note's end in a cell: a note off, or a key off (the cell's command, its tick) */
        const ends = c => !!c && (c.note === M.OFF || has(c, 0x14));
        /* the track holding note m at row r of pattern p, -1 none */
        const noteTrk = (p, r, m) => { for (let tr = 0; tr < 16; tr++) { const c = pc(p, r, tr); if (isNote(c) && c.note === m) return tr; } return -1; };
        /* the notes of pattern p as row:note, in rows' order */
        const songNotes = p => { const a = []; for (let r = 0; r < p.rows; r++) for (let tr = 0; tr < 16; tr++) { const c = pc(p, r, tr); if (isNote(c)) a.push(r + ":" + c.note); } return a; };
        if (U.screen.name !== "LOOPS") await t.homeKey(22);
        await stopAll();
        setLoops([], 64);
        await menuPick("RECORD LIVE");
        ok(!sg() && !t.top() && U.toastNow() === "NO LOOPS YET", "RECORD LIVE with no loops: NO LOOPS YET");
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60], [4, 0, M.OFF], [8, 0, 62], [12, 0, M.OFF]]) }),
                  mkLoop({ inst: 0xF001, base: 4, repeats: 4, col: 1, cells: cs(4, [[0, 0, 64], [2, 0, M.OFF]]) })], 16);
        const rev0 = LS().fx.knobs.REVERB[3], dly0 = !!LS().fx.on.DELAY;
        U.edit(() => { Object.assign(LS(), { tempo: 60, quantize: "ROW", countIn: "OFF", nextMono: false }); LS().fx.on.DELAY = false; });
        U.setInst(dx7);
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
        ok(t.leds[B.REC] === 1 || t.leds[B.REC] === 0, "(REC blinks while armed)");
        await t.tap("SEL");
        ok(U.top().items.includes("CANCEL RECORD LIVE"), "RECORD LIVE: ... the menu's item reads CANCEL RECORD LIVE");
        U.top().sel = U.top().items.indexOf("CANCEL RECORD LIVE"); await t.tap("SEL");
        ok(!sg() && U.toastNow() === "CANCELLED" && !P.playing, "RECORD LIVE: ... CANCEL RECORD LIVE disarms it (CANCELLED)");
        await lvArm();
        await t.tap("HOME");
        ok(!sg() && U.toastNow() === "CANCELLED" && !t.top() && !P.playing, "RECORD LIVE: armed, HOME cancels it (CANCELLED), no combination open");
        await pick(2);                                     /* (+ NEW LOOP: the stage silences no loop) */
        await lvArm();
        ok(U.screen.hintText() === "SONG ARMED: A NOTE STARTS  HOME CANCELS", "RECORD LIVE: armed, the hint line SONG ARMED: A NOTE STARTS  HOME " +
           "CANCELS (" + U.screen.hintText() + ")");
        const n0 = M.freeSong(), k0 = U.midi(0), k4 = U.midi(4), calls = [], on0 = A.noteOn;
        A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, time, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
        await t.tap("PLAY");
        ok(U.screen.hintText() === "RECORDING: HOME ENDS AND KEEPS IT", "RECORD LIVE: while it records, the hint line RECORDING: HOME ENDS AND " +
           "KEEPS IT (" + U.screen.hintText() + ")");
        ok(sg() && sg().state === "on" && P.playing && P.what === "loops" && P.recording(), "RECORD LIVE: armed with nothing playing, PLAY (COUNT-IN " +
           "OFF): the loops from their start, the song recording with them (the header's red dot)");
        await lvAt(3, 1); U.edit(() => { LS().fx.on.DELAY = true; });
        await lvAt(5, 2); t.keyDown(0);                    /* (ROW: on row 5) */
        await lvAt(6, 0); U.punchDown(2);
        await lvAt(7, 1); t.keyUp(0);
        await t.tap("SEQ");
        await lvAt(9, 0); t.keyDown(U.WHITE[1]); await lvAt(9, 1); t.keyUp(U.WHITE[1]);   /* (CONTROL: loop 01 muted at once) */
        await t.tap("SEQ");
        await lvAt(11, 0); U.edit(() => { LS().mixer[0].lvl = 0x50; });
        await lvAt(12, 0); U.punchUp(2);
        U.edit(() => { LS().quantize = "4 ROWS"; });
        await lvAt(13, 0); U.edit(() => { LS().fx.knobs.REVERB[3] = 70; });
        await lvAt(14, 2);
        const pressed = P.time();
        t.keyDown(4);                                      /* (4 ROWS: 14.33 lands on 16, ahead: it waits for it) */
        await lvAt(15, 1); U.edit(() => { LS().tempo = 64; });
        await lvAt(17, 5); t.keyUp(4);
        const kc = calls.find(c => /k4$/.test(c.id));
        ok(kc && kc.track === STAGE_T && kc.time - pressed > .15, "RECORD LIVE: ... a key's note heard as written: on the stage at its line, not " +
           "before (" + (kc ? (kc.time - pressed).toFixed(2) : "none") + " s)");
        ok(LS().list[1].muted && staged(), "RECORD LIVE: ... the mute is live; the keys overdub the stage as ever");
        t.frame();
        const foot = U.screen.footer();
        ok(/^MEMORY \d+% FREE$/.test(foot[0]) && foot[1] === D.RED && t.leds[B.REC] === 1, "RECORD LIVE: recording: the footer's MEMORY n% FREE (" +
           foot[0] + "), red; REC full");
        await t.tap("ARP");
        const offInSong = stg().off && U.toastNow() === "STAGE OFF" && sg() && sg().state === "on";
        await t.tap("ARP");
        ok(offInSong && !stg().off && U.toastNow() === "STAGE ON" && sg() && sg().state === "on",
           "RECORD LIVE: ... ARP's tap switches the stage off and on as anywhere (STAGE OFF, STAGE ON), the song going on");
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
        await t.tap("HOME");                               /* it ends */
        A.noteOn = on0;
        const toast0 = U.toastNow() || "-";
        ok(!sg() && !t.top() && U.screen.name === "LOOPS" && P.playing && P.what === "loops" && toast0.startsWith("SONG " + M.hex(n0) + " RECORDED: ") && toast0.endsWith(", 1 PATTERN"),
           "RECORD LIVE: HOME tapped: it ends, the loops playing on, LOOPS staying, no combination open (" + toast0 + ")");
        const S = M.state.songs[n0], p0 = S && M.pattern(S.order[0]);
        ok(S && S.order.length === 1 && p0.rows === s2 + 3, "RECORD LIVE: a new song at the first free number, one pattern of the rows heard (" +
           (p0 ? p0.rows : 0) + ", its last row " + (s2 + 2) + ")");
        ok(S && S.tempo === 60 && S.rpb === 4 && S.atEnd === "STOP" && !S.fx.on.DELAY && S.fx.knobs.REVERB[3] !== 70 && S.mixer[0].lvl === 127 &&
           S.name === "LOOPS 01", "RECORD LIVE: ... LOOPS' tempo, FX setup and mixes as they were at its start; it plays once and stops; named as the " +
           "panel had it (LOOPS 01)");
        ok(pc(p0, 0, 0) && pc(p0, 0, 0).note === 60 && pc(p0, 0, 0).inst === 0xF000 && pc(p0, 8, 0) && pc(p0, 8, 0).note === 62 &&
           pc(p0, 4, 0) && pc(p0, 4, 0).note === M.OFF && pc(p0, 12, 0) && pc(p0, 12, 0).note === M.OFF, "RECORD LIVE: ... loop 00's notes on track 0, each naming its instrument");
        ok([0, 4, 8].every(r => pc(p0, r, 1) && pc(p0, r, 1).note === 64 && pc(p0, r, 1).inst === 0xF001) && ends(pc(p0, 9, 1)) &&
           !pc(p0, 12, 1) && !pc(p0, 10, 1), "RECORD LIVE: ... loop 01's repeats on track 1, left out from row 9 (its mute), its note ending there");
        ok(pc(p0, 5, 2) && pc(p0, 5, 2).note === k0 && !pc(p0, 5, 2).cmds.length && pc(p0, 5, 2).inst === U.inst && pc(p0, 5, 2).vol === U.set("KEYS VOLUME") && pc(p0, 7, 2) &&
           pc(p0, 7, 2).note === M.OFF, "RECORD LIVE: ... the keys as they land on the stage (ROW: 5.33 on 5, its note off on 7), in the current instrument");
        ok(pc(p0, 16, 2) && pc(p0, 16, 2).note === k4 && pc(p0, 18, 2) && pc(p0, 18, 2).note === M.OFF, "RECORD LIVE: ... 4 ROWS: 14.33 on row 16, its note off on 18");
        ok(has(pc(p0, 11, 0), 0x80, 0x50), "RECORD LIVE: ... loop 00's mix turned: =80 50 on its track at its row (11)");
        ok(cmdRow(p0, 3, 0xAE, 5) && cmdRow(p0, 6, 0xC5, 6) && cmdRow(p0, 13, M.fxCode("REVERB", 3), 70) && cmdRow(p0, 15, 0x0F, 0x40),
           "RECORD LIVE: ... DELAY switched on (#ON 05, row 3), a punch-in of 6 rows (!04, row 6), REVERB MIX turned (row 13), the tempo (F40, row 15)");
        let empty = true, hush = true;
        for (let r = 16; r < 32; r++) empty = empty && !pc(p0, r, 1);
        ok(pc(p0, 16, 0) && pc(p0, 16, 0).note === 60 && !pc(p0, 16, 0).cmds.length && empty, "RECORD LIVE: ... the loops' next pass (row 16): loop 01 " +
           "still muted, no commands");
        ok(pc(p0, 21, 2) && pc(p0, 21, 2).note === k0 && pc(p0, 23, 2) && pc(p0, 23, 2).note === M.OFF && pc(p0, 32, 2) && pc(p0, 32, 2).note === k4 && pc(p0, 34, 2) &&
           pc(p0, 34, 2).note === M.OFF, "RECORD LIVE: ... the stage's notes as they sound each cycle");
        for (let r = 36; r <= 38; r++) hush = hush && !pc(p0, r, 0) && !pc(p0, r, 2);
        ok(pc(p0, 35, 0) && pc(p0, 35, 0).note === M.OFF && hush && !pc(p0, 40, 0) && pc(p0, 41, 2) && pc(p0, 41, 2).note === k0 && pc(p0, 44, 0) && pc(p0, 44, 0).note === 62 &&
           pc(p0, s2, 0) && pc(p0, s2, 0).note === 60, "RECORD LIVE: ... silent from row 35 (a note off for the note sounding there, then nothing, the stage " +
           "with them), on again from row 39 at their row 3 (their note off on row 40 ends nothing: not written; row 41 the stage's 5, row 44 their 8), at " +
           "their start from the hold's row");
        emptyStage();
        U.edit(() => { LS().list[1].muted = false; LS().tempo = 60; LS().quantize = "ROW"; LS().fx.knobs.REVERB[3] = rev0; LS().fx.on.DELAY = dly0; });

        /* armed while the loops play: REC starts it at once, at the row playing; leaving LOOPS ends it at once, what was
         * heard written */
        ok(P.playing && P.what === "loops", "(the loops play on after it)");
        await lvArm();
        await t.sleep(500);                                /* (REC's 500 ms after arming it) */
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
        const n2 = M.freeSong(), k2 = U.midi(2);
        t.keyDown(2); await t.sleep(40); t.keyUp(2);       /* (nothing playing: the key starts it, its note on row 0) */
        ok(sg() && sg().state === "on" && P.playing && P.what === "loops" && P.lpRestState().on === 1,
           "RECORD LIVE: armed with nothing playing, a key starts it, the loops' replay running silent");
        await t.tap("PLAY");                               /* (the loops on from their place: their start) */
        await lvAt(await lvSwitch(0, 0), 1);
        await t.tap("HOME");
        await until(() => !sg());
        const S2 = M.state.songs[n2], r0 = S2 && M.pattern(S2.order[0]), toast2 = U.toastNow() || "-";
        ok(S2 && r0.tracks === 16 && noteTrk(r0, 0, k2) >= 0 && toast2 === "SONG " + M.hex(n2) + ": 4 NOTES LEFT OUT", "RECORD LIVE: 16 tracks: a key's note on its row, then " +
           "4 loops of 4 lanes on one row fill them; the fifth loop's notes left out, counted (" + toast2 + ")");
        await stopAll();

        /* past a pattern's 128 rows: 2 patterns; a note held across the boundary ends in the second on its own track (a loop
         * of 16 rows, its note at row 14 ended at row 2 of its next repeat, a mix turned on the way) */
        setLoops([mkLoop({ cells: cs(16, [[2, 0, M.OFF], [14, 0, 60]]) })], 16);
        U.edit(() => { LS().tempo = 240; });
        P.kept.loops.row = 0;
        await lvArm();
        const nx = M.freeSong();
        await t.tap("PLAY");
        await lvAt(60, 0); U.edit(() => { LS().mixer[0].lvl = 0x50; });
        await lvAt(150, 0);
        await t.tap("HOME");
        await until(() => !sg());
        const Sx = M.state.songs[nx], x0 = Sx && M.pattern(Sx.order[0]), x1 = Sx && Sx.order.length > 1 ? M.pattern(Sx.order[1]) : null, toastx = U.toastNow() || "-";
        const tx = x0 ? noteTrk(x0, 126, 60) : -1;
        ok(Sx && Sx.order.length === 2 && x0.rows === 128 && tx >= 0 && x1 && noteTrk(x1, 14, 60) >= 0 && ends(pc(x1, 2, tx)) &&
           toastx.startsWith("SONG " + M.hex(nx) + " RECORDED: ") && toastx.endsWith(", 2 PATTERNS"),
           "RECORD LIVE: past 128 rows: 2 patterns; a note held across the boundary ends in the second on its own track (" + toastx + ")");
        await stopAll();

        /* a note off exactly on the boundary: row 128's tick 0 lands at row 0 of the next pattern */
        setLoops([mkLoop({ cells: cs(16, [[0, 0, M.OFF], [14, 0, 60]]) })], 16);
        P.kept.loops.row = 0;
        await lvArm();
        const ny = M.freeSong();
        await t.tap("PLAY");
        await lvAt(150, 0);
        await t.tap("HOME");
        await until(() => !sg());
        const Sy = M.state.songs[ny], y0 = Sy && M.pattern(Sy.order[0]), y1 = Sy && Sy.order.length > 1 ? M.pattern(Sy.order[1]) : null;
        const ty = y0 ? noteTrk(y0, 126, 60) : -1;
        ok(Sy && Sy.order.length === 2 && ty >= 0 && y1 && ends(pc(y1, 0, ty)),
           "RECORD LIVE: a note off exactly on row 128 lands at row 0 of the second pattern, on its note's track");
        await stopAll();

        /* the log full (8 entries): 8 notes of a loop, an off between each: the first four notes and their offs fill it; a mix
         * turned then; the count is the four notes really left out (an off or a mix dropped counts nothing), the song written */
        const cells8 = [];
        for (let k = 0; k < 8; k++) cells8.push([2 * k, 0, 60], [2 * k + 1, 0, M.OFF]);
        setLoops([mkLoop({ cells: cs(16, cells8) })], 16);
        U.edit(() => { LS().tempo = 60; });
        U.screen.liveLogMax(8);
        P.kept.loops.row = 0;
        await lvArm();
        const nf = M.freeSong();
        await t.tap("PLAY");
        await lvAt(9, 0); U.edit(() => { LS().mixer[0].lvl = 0x50; });
        await lvAt(15, 1);
        const leftf = lvs() ? lvs().left : -1;
        await t.tap("HOME");
        await until(() => !sg());
        U.screen.liveLogMax(2000);
        const Sf = M.state.songs[nf], f0 = Sf && M.pattern(Sf.order[0]), toastf = U.toastNow() || "-";
        ok(leftf === 4 && f0 && noteTrk(f0, 6, 60) >= 0 && noteTrk(f0, 8, 60) < 0 && toastf === "SONG " + M.hex(nf) + ": 4 NOTES LEFT OUT",
           "RECORD LIVE: a full log: a mix turned with it full: the notes really left out counted (" + leftf + "), the song written (" + toastf + ")");
        await stopAll();

        /* it stops by itself (a pattern every 128 rows: the loops at the fastest tempo): the edit memory full, nothing past
         * it written; every pattern number taken, at its first pattern (256 positions, 32,768 rows, are not run here); no
         * song number free: refused */
        setLoops([mkLoop({ cells: cs(16, [[0, 0, 60]]) })], 16);
        U.edit(() => { LS().tempo = M.TEMPO_MAX; });
        const stopsAt = async set => {
            P.kept.loops.row = 0;
            await lvArm();
            const n = M.freeSong();
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
        /* each loop written as it played, its SHIFT included */
        setLoops([mkLoop({ shift: 3, cells: cs(16, [[0, 0, 60], [4, 0, M.OFF]]) })], 16);
        U.edit(() => { LS().tempo = 300; });
        P.kept.loops.row = 0;
        await lvArm();
        const n3 = M.freeSong();
        await t.tap("PLAY");
        await until(() => row() >= 10, 5000);
        await t.tap("HOME");
        await until(() => !sg());
        await stopAll();
        const S3 = M.state.songs[n3], q3 = S3 && M.pattern(S3.order[0]);
        ok(q3 && pc(q3, 3, 0) && pc(q3, 3, 0).note === 60 && pc(q3, 7, 0) && pc(q3, 7, 0).note === M.OFF && !pc(q3, 0, 0),
           "RECORD LIVE: each loop written as it played, its SHIFT included (SHIFT 3: its note at 0 on row 3, its note off on 7)");
        U.edit(() => { LS().tempo = 120; });
        const songs0 = M.state.songs.slice();
        while (M.freeSong() >= 0) M.state.songs[M.freeSong()] = M.newSong();
        await menuPick("RECORD LIVE");
        ok(!sg() && !t.top() && U.toastNow() === "NO SONG NUMBER FREE", "RECORD LIVE: every song number taken: refused (NO SONG NUMBER FREE)");
        M.state.songs = songs0;
        M.settle();

        /* ---- every LOOPS function during a song, each written as heard; the song's rows its own ---- */
        const songOf = n => { const s = M.state.songs[n]; return s && M.pattern(s.order[0]); };
        const edOf = c => { const x = c && c.cmds.find(y => y.c === 0x0E); return x ? x.v : 0; };
        const prOn = () => t.top() === "recorder";
        /* the stage holding note m at row r, a note off at r + 2 */
        const stageOf = (rows, r, m) => { const s = stg(); s.cells = new Array(rows * 4).fill(null); s.cells[r * 4] = M.newCell(m, null, 100); s.cells[(r + 2) * 4] = M.newCell(M.OFF, null, null); M.version++; };
        const start = async (loops, rows, sel, before) => {   /* the loops set, armed on `sel`, PLAY: recording; the song's number */
            await stopAll();
            await stageOn();
            setLoops(loops, rows);
            U.edit(() => { Object.assign(LS(), { tempo: 60, quantize: "ROW", countIn: "OFF", nextMono: false }); });
            U.setInst(dx7);
            P.kept.loops.row = 0;
            await pick(sel);
            if (before) before();
            await t.sleep(500);                            /* (REC's 500 ms) */
            await lvArm();
            const n = M.freeSong();
            await t.tap("PLAY");
            return n;
        };
        /* REC's send, QUANTIZE, DELETE LOOP, LENGTH, DOUBLE */
        let n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF], [9, 0, 62], [10, 0, M.OFF]]) })], 16, 1);
        const k0b = U.midi(0);
        await lvAt(1, 3); t.keyDown(0); await lvAt(3, 3); t.keyUp(0);
        await lvAt(6, 0); await t.tap("REC");
        ok(sg() && sg().state === "on" && LS().list.length === 2 && !staged() && U.toastNow() === "STAGE AS LOOP 01",
           "RECORD LIVE keeps LOOPS working: REC sends the stage as anywhere (STAGE AS LOOP 01), the song going on");
        await lvAt(20, 0); await pick(0); await menuPick("QUANTIZE"); U.top().sel = 2; await t.tap("SEL");   /* (9: 8 at 4 ROWS) */
        ok(sg() && U.toastNow() === "LOOP 00 QUANTIZED: 4 ROWS", "... SEL's QUANTIZE (LOOP 00 QUANTIZED: 4 ROWS)");
        await lvAt(26, 0); await pick(1); await menuPick("DELETE LOOP");
        ok(sg() && LS().list.length === 1 && U.toastNow() === "LOOP 01 DELETED", "... DELETE LOOP (LOOP 01 DELETED)");
        await lvAt(44, 0); await t.tap("GLO"); U.top().sel = 2; await t.turn("K1", 1); await t.tap("GLO");
        ok(sg() && LS().rows === 32, "... GLO's LENGTH (2 BARS)");
        await lvAt(50, 0); await menuPick("DOUBLE");
        ok(sg() && LS().rows === 64 && /^DOUBLED/.test(U.toastNow() || ""), "... DOUBLE (" + U.toastNow() + ")");
        await lvAt(66, 0); await t.tap("HOME"); await until(() => !sg());
        let p = songOf(n);
        const sn = p ? songNotes(p).join() : "";
        ok(p && noteTrk(p, 2, k0b) >= 0 && noteTrk(p, 18, k0b) >= 0 && noteTrk(p, 34, k0b) < 0 && noteTrk(p, 9, 62) >= 0 && noteTrk(p, 25, 62) < 0 &&
           noteTrk(p, 24, 62) >= 0 && noteTrk(p, 40, 62) >= 0 && noteTrk(p, 56, 62) < 0 && noteTrk(p, 48, 60) < 0 && noteTrk(p, 64, 60) >= 0,
           "... the song as heard: the stage's note on row 2, the loop it became on row 18, gone with DELETE LOOP (34); QUANTIZE's note from row 24 on; " +
           "the bars LENGTH added silent (48-63), the loop on at row 64 (" + sn + ")");
        /* GRID, with no loops left: the song's rows stay its own; the stage off: the keys as played, at their ticks */
        await stopAll();
        n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF]]) })], 16, 0);
        await lvAt(2, 3); await menuPick("DELETE LOOP");
        await lvAt(8, 0); await t.tap("GLO"); U.top().sel = 3; await t.turn("K1", 1); await t.tap("GLO");
        ok(sg() && LS().rpb === 6, "... GRID, with no loops left and the stage empty (1/16T)");
        await t.tap("ARP");
        ok(sg() && stg().off, "... ARP's tap: the stage off");
        await lvAt(20, 3); t.keyDown(1); await lvAt(22, 2); t.keyUp(1);
        await lvAt(24, 0); await t.tap("ARP");
        await t.tap("HOME"); await until(() => !sg());
        p = songOf(n);
        const S4 = M.state.songs[n], tk = p ? noteTrk(p, 20, U.midi(1)) : -1;
        ok(p && S4.rpb === 4 && tk >= 0 && (edOf(pc(p, 20, tk)) >> 4) === 0xD && (edOf(pc(p, 20, tk)) & 15) >= 2 && has(pc(p, 22, tk), 0x14),
           "... the song keeps its rows (ROWS A BEAT 4); the stage off: the keys as played: a note delay at their tick, a key off at theirs");
        /* the stage live: the selected loop silent where it holds notes; OVERWRITE's erasing, the emptied stage letting the
         * loop be heard again; the stage off: the loop heard, the stage not */
        const two = () => [mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF]]) }), mkLoop({ col: 1, inst: 0xF001, cells: cs(16, [[4, 0, 64], [6, 0, M.OFF]]) })];
        n = await start(two(), 16, 0, () => stageOf(16, 8, 67));
        await lvAt(18, 0); t.down("LFO"); t.up("LFO");
        ok(sg() && sg().state === "on", "... OVERWRITE during a song, the song going on");
        for (const r of [20, 22, 24, 26, 28, 30, 32]) { await lvAt(r, 0); t.frame(); }
        t.down("LFO"); t.up("LFO");
        await lvAt(36, 0); await t.tap("HOME"); await until(() => !sg());
        p = songOf(n);
        ok(p && noteTrk(p, 0, 60) < 0 && noteTrk(p, 16, 60) < 0 && noteTrk(p, 4, 64) >= 0 && noteTrk(p, 8, 67) >= 0 && noteTrk(p, 24, 67) < 0 && noteTrk(p, 32, 60) >= 0,
           "... the stage live: its note written, the selected loop silent where it holds notes; OVERWRITE's erasing as heard, the stage emptied, the loop heard (" +
           (p ? songNotes(p).join() : "") + ")");
        n = await start(two(), 16, 0, () => stageOf(16, 8, 67));
        await lvAt(20, 0); await t.tap("ARP");
        await lvAt(34, 0); await t.tap("HOME"); await until(() => !sg());
        p = songOf(n);
        ok(p && noteTrk(p, 8, 67) >= 0 && noteTrk(p, 24, 67) < 0 && noteTrk(p, 16, 60) < 0 && noteTrk(p, 32, 60) >= 0, "... ARP's tap during a song: the stage off, the song as heard: the stage no more, the selected loop");
        if (stg().off) await t.tap("ARP");
        await stopAll();
        /* the loops silent (PLAY): the keys sound at once, as with the stage off, as played */
        n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF]]) })], 16, 1);
        await lvAt(2, 0); await t.tap("PLAY");
        await lvAt(6, 2); t.keyDown(3); await lvAt(8, 2); t.keyUp(3);
        ok(!staged(), "... the loops silent: the keys do not go on the stage");
        await lvAt(10, 0); await t.tap("HOME"); await until(() => !sg());
        p = songOf(n);
        const tk3 = p ? noteTrk(p, 6, U.midi(3)) : -1;
        ok(p && tk3 >= 0 && (edOf(pc(p, 6, tk3)) & 15) >= 2 && has(pc(p, 8, tk3), 0x14), "... the loops silent: the keys written as played");
        await stopAll();
        /* the pattern recorder's preview heard and written; its REC onto the stage, never the song's end */
        n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF]]) })], 16, 1);
        await lvAt(1, 0);
        await openRec();
        ok(prOn() && sg() && sg().state === "on", "RECORD LIVE: ARP held + REC opens the pattern recorder during a song, REC's press ending nothing");
        await t.key(U.WHITE[8]);                           /* (a step on: step 9 of 16) */
        await t.sleep(3000);
        ok(sg() && sg().state === "on", "... its preview plays, the song going on");
        await t.tap("REC");
        ok(!prOn() && staged() && U.toastNow() === "PATTERN ON THE STAGE" && sg() && sg().state === "on", "... its REC puts the pattern on the stage, the song going on");
        await t.sleep(600);
        await lvAt(row() + 2, 0);
        await t.tap("HOME"); await until(() => !sg());
        p = songOf(n);
        ok(p && songNotes(p).some(x => +x.split(":")[0] % 16 === 8), "... the preview's note written where it sounded (" + (p ? songNotes(p).join() : "") + ")");
        /* HOME: its row the song's last, what came after it cut away; held: its combination, the song going on; leaving by it */
        n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF], [8, 0, 62], [10, 0, M.OFF]]) })], 16, 1);
        await lvAt(5, 1);
        t.down("HOME"); await lvAt(6, 3); t.up("HOME");
        await until(() => !sg());
        p = songOf(n);
        ok(p && p.rows === 6 && U.screen.name === "LOOPS" && !t.top() && P.playing, "HOME: the song's last row the one it was pressed in (" + (p ? p.rows : 0) + " rows), though let go a row on");
        n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [2, 0, M.OFF]]) })], 16, 1);
        await lvAt(1, 0);
        t.down("HOME"); await t.sleep(U.set("HOLD DELAY") + 120); t.up("HOME"); await t.sleep(200);
        ok(sg() && sg().state === "on", "... HOME held past the hold delay: its combination, the song going on");
        await t.homeKey(8);
        ok(!P.lpTapOn() && U.screen.name !== "LOOPS", "... a HOME combination that leaves LOOPS ends it as leaving does");
        await stopAll();
        await t.homeKey(22);
        /* only notes with a voice: the worklet's word (tests/playground/sound.js has the pool's side) */
        {
            const ons = [], o0 = A.noteOn;
            A.noteOn = (id, time, midi, ...a) => { ons.push({ id, time, midi }); return o0(id, time, midi, ...a); };
            n = await start([mkLoop({ cells: cs(16, [[0, 0, 60], [6, 0, M.OFF], [8, 0, 62], [10, 0, M.OFF], [12, 0, 64], [14, 0, M.OFF]]) })], 16, 1);
            await lvAt(3, 0);
            const a60 = ons.find(x => x.midi === 60);
            A.tapped({ t: "tap", id: a60.id, lost: true, time: P.time() });     /* (its voice taken at row 3) */
            await until(() => ons.some(x => x.midi === 62));
            A.tapped({ t: "tap", id: ons.find(x => x.midi === 62).id, none: true, time: P.time() });     /* (no voice given) */
            await lvAt(14, 0);
            await t.tap("HOME"); await until(() => !sg());
            A.noteOn = o0;
            p = songOf(n);
            ok(p && noteTrk(p, 0, 60) >= 0 && ends(pc(p, 3, noteTrk(p, 0, 60))) && !pc(p, 6, noteTrk(p, 0, 60)) && noteTrk(p, 8, 62) < 0 && noteTrk(p, 12, 64) >= 0,
               "only notes with a voice: one losing its voice ends there (no note off at its own end), one never given a voice not written");
        }
    }
    A.stageCol = stageCol0;
    M.demoDisk();
};
