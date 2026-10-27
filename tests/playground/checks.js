/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's behaviour against the specs (specs/ui/, specs/files/): the controls' grammar,
 * writing in the EDITOR, takes, blocks, saving and REVERT, the references, FUSION, LOOPS, the
 * transport.
 *   node tests/playground/harness.js tests/playground/checks.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    /* FX by its button (it has no black key); from LOOPS or DJ that would stay theirs, so from PLAYBACK then */
    const toFx = async () => {
        if (U.screen.name === "FX") return;
        if (U.screen.name === "LOOPS" || U.screen.name === "DJ") await t.homeKey(8);
        await t.tap("FX");
    };

    /* ---- the grammar: the grace period, either order ---- */
    U.pattern = M.patternNums()[0];                         /* (the EDITOR on a numbered pattern: its own, editor_own.js) */
    await t.homeKey(3);
    ok(U.screen.name === "EDITOR", "HOME + OP2 goes to the EDITOR");
    const before = JSON.stringify(M.pattern(U.pattern).cells[0]);
    t.down("HOME"); await t.sleep(30); t.up("HOME"); await t.sleep(50); t.down("LFO"); await t.sleep(30); t.up("LFO"); await t.sleep(200);
    ok(t.top() === "help", "HOME, let go, LFO 50 ms later: still HOME + LFO, the help");
    ok(JSON.stringify(M.pattern(U.pattern).cells[0]) === before, "... and the cell was not cleared");
    await t.tap("HOME");
    ok(!t.top(), "HOME closes the help");
    t.down("LFO"); await t.sleep(30); t.down("HOME"); await t.sleep(30); t.up("HOME"); t.up("LFO"); await t.sleep(200);
    ok(t.top() === "help", "LFO held, then HOME: the help too");
    await t.tap("HOME");

    /* ---- OVER 9000 (specs/ui/frame.md#the-header): a second's mean over 90 % shows it, one at 90 % takes it away ---- */
    {
        const S = t.PTFM.audio.stats, was = S.cpu;
        /* (2.1 s: a whole window at the new value, whatever one was under way) */
        const second = async cpu => { for (const end = performance.now() + 2100; performance.now() < end;) { await t.sleep(50); S.cpu = cpu; t.frame(); } };
        await t.homeKey(1);
        await second(95);
        ok(U.over9000(), "OVER 9000: a second at 95 % shows it in the header");
        t.shot("ui3_over9000");
        await second(90);
        ok(!U.over9000(), "OVER 9000: a second at 90 % takes it away");
        S.cpu = was;
        /* either core: core 2 alone at 95 % shows it too, but not while core 2 is off */
        const was2 = S.cpu2, set = M.state.settings;
        const second2 = async (cpu, cpu2) => { for (const end = performance.now() + 2100; performance.now() < end;) { await t.sleep(50); S.cpu = cpu; S.cpu2 = cpu2; t.frame(); } };
        await second2(20, 95);
        ok(U.over9000(), "OVER 9000: core 2 alone at 95 % for a second shows it");
        set["SECOND CORE"] = "OFF";
        await second2(20, 95);
        ok(!U.over9000(), "OVER 9000: core 2 off, its figure is not counted");
        set["SECOND CORE"] = "ON";
        await second2(20, 90);
        ok(!U.over9000(), "OVER 9000: core 2 at 90 % takes it away");
        S.cpu = was; S.cpu2 = was2;
        await t.homeKey(3);
    }

    /* ---- SECOND CORE (specs/second-core.md): the effects' share is core 2's while it is on, core 1's while off ---- */
    {
        const A = t.PTFM.audio, S = A.stats, set = M.state.settings, Dr = t.PTFM.draw;
        ok(M.SETTINGS.findIndex(s => s.k === "SECOND CORE") === M.SETTINGS.findIndex(s => s.k === "VOICES") + 1
           && set["SECOND CORE"] === "ON", "SETTINGS: SECOND CORE (ON / OFF) after VOICES, ON at the start");
        const feed = () => { for (let i = 0; i < 80; i++) A.estimate({ voices: [], count: { DX7: 2 }, steals: 0, fx: { overOn: { REVERB: true, CHORUS: true } } }); };
        feed();
        const on1 = S.cpu, on2 = S.cpu2;
        set["SECOND CORE"] = "OFF"; feed();
        const off1 = S.cpu, off2 = S.cpu2;
        ok(on2 > 8 && on2 < 25 && off2 < .01, "the estimate: core 2 takes the effects and the output (" + on2.toFixed(1) + " %), none while off");
        ok(Math.abs(off1 - (on1 + on2)) < 3 && off1 > on1 + 8, "OFF: the effects' share is core 1's (" + on1.toFixed(1) + " + " + on2.toFixed(1) + " -> " + off1.toFixed(1) + ")");
        ok(A.c2State() === "OFF", "DEVICE: SECOND CORE OFF");
        /* the header's bar: core 2's column (219-220) drawn while on, empty while off */
        const ground = (x, y) => Dr.fb[y * 240 + x];
        const column = x => ground(x, 11) !== ground(x, 2);
        S.cpuNow = 30; S.cpu2Now = 30;
        t.frame();
        ok(column(217) && !column(219), "header, SECOND CORE OFF: core 1's column only");
        set["SECOND CORE"] = "ON"; t.frame();
        ok(column(217) && column(219) && column(220), "header, SECOND CORE ON: both 2 px columns");
        await t.homeKey(17);
        await t.turn("SELECT", -9);
        t.shot("ui_status_first"); await t.turn("SELECT", 1); t.shot("ui_status_performance");
        await t.turn("SELECT", 5);
        ok(U.screen.sub() === "DEVICE", "(STATUS' DEVICE page)");
        ok(["STARTING", "ON"].includes(A.c2State()), "DEVICE: SECOND CORE STARTING, then ON (" + A.c2State() + ")");
        t.shot("ui_status_device");
        await t.turn("SELECT", -9);                      /* (the first page again) */
        await t.homeKey(3);
    }

    /* ---- a held button's layer: up until the button is let go, combinations made meanwhile included ---- */
    t.down("HOME"); await t.sleep(650);
    ok(U.layers.length === 1 && U.layers[0].btn === U.B.HOME, "HOME held past the hold delay: its layer");
    t.keyDown(1); await t.sleep(30); t.keyUp(1); await t.sleep(30);
    ok(U.screen.name === "SONGS" && U.layers.length === 1, "HOME + OP1 goes to SONGS; the layer stays while HOME is held");
    t.keyDown(3); await t.sleep(30); t.keyUp(3); await t.sleep(30);
    ok(U.screen.name === "EDITOR" && U.layers.length === 1, "... and HOME + OP2 then goes on to the EDITOR");
    t.up("HOME"); await t.sleep(200);
    ok(!U.layers.length && U.screen.name === "EDITOR", "HOME let go: the layer closes, and nothing else happens");
    U.editor.row = 0; U.editor.track = 0; U.editor.block = null; U.editor.anchor = null;
    t.down("SEL"); await t.sleep(650);
    await t.turn("SELECT", 1);
    ok(U.layers.length === 1 && U.editor.block && U.editor.block.r1 === 1, "SEL's layer up: the first SELECT click marks (the layer takes no input)");
    t.up("SEL"); await t.sleep(150);
    ok(!U.layers.length, "SEL let go: its layer closes");
    U.editor.block = null; U.editor.anchor = null;

    /* ---- HOME's combination, tapped open (specs/ui/controls.md): its members, the overlay, the LEDs, and
     * the lock: one member acts again and again, any other control then closes it and does its own job ---- */
    const hc = () => U.overlays.find(o => o.kind === "home");
    await t.tap("HOME"); t.frame();
    ok(U.screen.name === "EDITOR" && t.top() === "home", "HOME tap at the top of a screen: its combination, an overlay");
    const lineText = l => Array.isArray(l) ? l.map(r => r[0]).join("") : l;      /* (a line coloured already: its runs) */
    const hl = U.homeLines(hc()).map(lineText);
    ok(hl.length === 11 && hl[0].startsWith("SELECT") && hl[1] === "OP1: SONGS         OP2: EDITOR" && hl[3] === "OP5: DJ            PIT: VISUAL" &&
       hl[5] === "POLY: LOOPS", "... its lines: SELECT, the screens two a line (OP1: SONGS ... OP5: DJ ... POLY: LOOPS), the white keys, SEQ, PLAY, REC, LFO (and HOME: CLOSE)");
    ok(t.leds[8] > .9 && t.leds[6] === 0 && t.leds[5] === .25 && t.leds[11] === .25 && t.leds[13] === .25,
       "... HOME lit, its members dim (LFO, SEQ, REC), the others dark (EDIT)");
    await t.sleep(900); t.frame();
    ok(t.leds[8] < .3 && t.leds[8] >= .25, "... HOME breathes: dim a second after it opened");
    ok(t.leds[14 + 3] === 1 && t.leds[14 + 1] === .25 && t.leds[14 + 0] === .25,
       "... the screen keys (the current one bright) and the tracks (dim: playing) lit");
    await t.turn("SELECT", 1);
    ok(U.screen.name === "INSTRUMENTS" && t.top() === "home", "SELECT: the next screen, and it stays open");
    await t.turn("SELECT", -2);
    ok(U.screen.name === "SONGS" && U.homeLines(hc()).length === 1, "... screen by screen, back too; only SELECT's line left");
    await t.key(3);
    ok(U.screen.name === "SONGS" && !t.top(), "... a black key then closes it and plays its note (the screens were SELECT's)");
    await t.homeKey(3);
    await t.tap("HOME"); await t.tap("HOME");
    ok(!t.top(), "HOME again closes it");
    await t.tap("HOME"); await t.key(1);
    ok(U.screen.name === "SONGS" && !t.top(), "a black key goes to its screen and closes it");
    await t.homeKey(3);
    await t.tap("HOME"); await t.key(0); await t.key(2);
    ok(U.mutes[0] && U.mutes[1] && t.top() === "home", "white keys mute track after track; it stays open");
    const edView = U.editor.view;
    await t.tap("SEQ");
    const toSolo = t.top() === "home" && hc().solo && U.editor.view === edView;
    await t.tap("SEQ");
    ok(toSolo && t.top() === "home" && !hc().solo && U.editor.view === edView,
       "... then SEQ switches them to solo and back, again and again, as long as they are members (the EDITOR's SEQ untouched)");
    await t.tap("HOME");
    U.mutes.fill(false); P.mutesChanged();
    await t.tap("HOME"); await t.tap("SEQ"); t.frame();
    ok(hc() && hc().solo && t.leds[11] === 1 && U.homeLines(hc()).includes("WHITE KEY: SOLO A TRACK"), "HOME, SEQ: the white keys solo (SEQ lit, the line says so)");
    await t.key(4);
    ok(U.solo === 2 && t.top() === "home", "... a white key solos its track");
    await t.key(4);
    ok(U.solo === -1, "... the same again ends it");
    await t.tap("SEQ"); t.frame();
    const backToMute = t.top() === "home" && !hc().solo && t.leds[11] > 0 && U.homeLines(hc()).includes("WHITE KEY: MUTE A TRACK, HELD: SOLO");
    await t.key(4);
    ok(backToMute && U.muted(2) && t.top() === "home", "... SEQ after a white key acted: still switches, back to mute (a white key mutes)");
    U.mutes.fill(false); P.mutesChanged();
    await t.tap("HOME");
    await t.tap("HOME"); await t.tap("LFO");
    ok(t.top() === "help", "HOME, LFO: the help");
    await t.tap("HOME");
    const tr0 = U.editor.track;
    await t.tap("HOME"); await t.turn("ALGORITHM", 1);
    ok(!t.top() && U.editor.track === tr0 + 1, "a knob outside it closes it and turns (ALGORITHM moves the cursor)");
    U.editor.track = tr0;
    await t.tap("HOME"); t.down("REC"); await t.sleep(30);
    const cutHeld = U.cutOn;
    t.up("REC"); await t.sleep(100);
    ok(!t.top() && !U.editor.rec && cutHeld && !U.cutOn, "HOME tapped, then REC: the CUT while REC is held, closed (REC not switched)");
    /* held: the same members, and the same lock */
    t.down("HOME"); await t.sleep(30); await t.key(0); await t.key(1); t.up("HOME"); await t.sleep(200);
    ok(U.mutes[0] && U.screen.name === "EDITOR" && !t.top(), "HOME held: a mute, then a black key plays its note; letting go does nothing");
    t.down("HOME"); await t.sleep(30); await t.tap("SEQ"); await t.key(0); t.up("HOME"); await t.sleep(200);
    ok(U.solo === 0, "HOME held + SEQ, a white key: solo");
    U.solo = -1; U.mutes.fill(false); P.mutesChanged();

    /* ---- SEL's tap menu: MARK, then SELECT / ALGORITHM stretch the block, SEL its menu ---- */
    U.editor.row = 0; U.editor.track = 0;
    await t.tap("SEL");
    ok(U.top().items[0] === "MARK", "SEL: the cell's menu starts with MARK");
    U.top().sel = 0; await t.tap("SEL");
    await t.turn("SELECT", 2); await t.turn("ALGORITHM", 1);
    const bk = U.editor.block;
    ok(bk && bk.r0 === 0 && bk.r1 === 2 && bk.t0 === 0 && bk.t1 === 1, "MARK: SELECT, ALGORITHM mark a block from the cursor");
    await t.tap("SEL");
    ok(U.top().title === "BLOCK 3 X 2" && !U.top().items.includes("MARK"), "SEL: the block's menu (no MARK)");
    await t.tap("HOME"); await t.tap("HOME");
    ok(!t.top() && !U.editor.block && !U.editor.marking, "HOME closes the menu, HOME again drops the mark");
    /* every held combination locks the same way */
    const inst0 = U.inst;
    t.down("SEL"); await t.sleep(30); await t.turn("SELECT", 1); await t.turn("PRESETS", 1); t.up("SEL"); await t.sleep(200);
    ok(U.editor.block && U.inst >> 8 === inst0 >> 8 && U.inst !== inst0 && !t.top(),
       "SEL held: SELECT marks, then PRESETS turns the instruments (not the cartridges); letting go opens no menu");
    U.editor.block = null; U.editor.anchor = null; U.setInst(inst0);
    t.down("SEL"); await t.sleep(30); await t.tap("SEQ"); t.up("SEL"); await t.sleep(200);
    ok(!t.top() && U.editor.view === 4, "SEL held, SEQ: SEQ's own job; SEL let go opens no menu");
    for (let i = 0; i < 3; i++) await t.tap("SEQ");

    /* ---- PRESETS and SEL + PRESETS ---- */
    U.setInst(0x011F);
    await t.turn("PRESETS", 1);
    ok(U.inst === 0x0100, "PRESETS wraps within the cartridge");
    ok(U.knobNow() === "PRESETS 01:00 " + M.inst(0x0100).name, "PRESETS shows in the header as every knob: " + U.knobNow());
    t.down("SEL"); await t.sleep(20); await t.turn("PRESETS", 1);
    const listHeld = U.overlays.length === 1 && !!U.top().presetsList;
    t.up("SEL"); await t.sleep(150);
    ok(U.inst >> 8 === M.state.carts[1].id && listHeld && !U.top(),
       "SEL + PRESETS turns cartridges, their list open while SEL is held; SEL's own tap does not happen");
    ok(U.knobNow() === "CARTRIDGE " + M.hex(M.state.carts[1].id) + " " + M.state.carts[1].name, "SEL + PRESETS: the cartridge in the header: " + U.knobNow());
    U.setInst(0x0100);

    /* ---- step writing ---- */
    U.editor.row = 2; U.editor.track = 0;
    await t.tap("REC");
    ok(U.editor.rec, "REC switches writing on");
    await t.key(7);
    const p = M.pattern(U.pattern);
    ok(p.cells[32] && p.cells[32].note === 60 && p.cells[32].inst === U.inst, "a key writes C-4 with the current instrument");
    ok(U.editor.row === 3 && M.unsaved === 1, "the cursor moves down by the edit step; one unsaved change");
    t.keyDown(7); await t.sleep(20); t.keyDown(11); await t.sleep(20); t.keyDown(14); await t.sleep(50);
    t.keyUp(7); t.keyUp(11); t.keyUp(14); await t.sleep(50);
    ok(p.cells[48].note === 60 && p.cells[49] && p.cells[49].note === 64 && p.cells[50] && p.cells[50].note === 67,
       "a chord spreads to the next tracks on one row");
    U.editor.row = 3; U.editor.track = 0;
    await t.tap("LFO");
    ok(!p.cells[48], "LFO clears the cell");
    U.editor.row = 3;
    await t.tap("LFO");
    ok(p.cells[48] && p.cells[48].note === M.OFF, "LFO on an empty cell writes a note off");
    /* REVERT (no UNDO): in the CELL menu while the pattern has something unsaved, after a question */
    await t.tap("SEL"); let m = U.top();
    ok(m.items.includes("REVERT") && !m.items.includes("UNDO"), "the CELL menu: REVERT while the pattern is unsaved, no UNDO");
    m.sel = m.items.indexOf("REVERT"); await t.tap("SEL");
    ok(t.top() === "question" && U.top().lines[0] === "REVERT PATTERN " + M.hex(U.pattern) + " TO SAVED?" &&
       U.top().answers.join() === "REVERT,KEEP", "REVERT asks first: REVERT PATTERN " + M.hex(U.pattern) + " TO SAVED?, REVERT / KEEP");
    await t.tap("OCT-");
    ok(!t.top() && p.cells[48] && p.cells[48].note === M.OFF && M.unsaved === 1, "KEEP: the pattern as it was");
    const n0 = M.unsaved, C = t.PTFM.cmds;
    U.editor.row = 2; U.editor.track = 0; U.editor.slot = 1;
    await t.turn("K1", -1);
    ok(U.editor.slot === 0, "K1 picks the cell's slot: the volume");
    const v0 = M.pattern(U.pattern).cells[32].vol;
    await t.turn("K4", -3);
    ok(M.pattern(U.pattern).cells[32].vol === v0 - 3 && n0 === 1 && M.unsaved === 1,
       "K4 sets the volume; the pattern changed again is still one change (what differs from the flash, not a log)");
    await t.turn("K1", 1);
    await t.turn("K2", 1);
    let c32 = M.pattern(U.pattern).cells[32];
    ok(U.editor.slot === 1 && c32.cmds.length === 1 && c32.cmds[0].c === 0x00, "K1 on command 1, K2: the first command, ARPEGGIO");
    await t.turn("K3", 4); await t.turn("K4", 7);
    ok(C.text(M.pattern(U.pattern).cells[32].cmds[0]) === "047", "K3 and K4 set its two digits: 047");
    await t.turn("K1", 1);
    ok(U.editor.slot === 2, "K1 goes on to command 2, one past the last");
    t.down("SEL"); await t.sleep(20); await t.turn("K2", 1); t.up("SEL"); await t.sleep(150);
    for (let i = 0; i < 80 && C.entry(M.pattern(U.pattern).cells[32].cmds[1]) !== C.ENTRIES.findIndex(e => e.key === "*"); i++)
        await t.turn("K2", 1);
    c32 = M.pattern(U.pattern).cells[32];
    ok(c32.cmds.length === 2 && C.group(c32.cmds[1].c) === "PARAM", "K2 turned on: an instrument parameter (*)");
    await t.turn("K3", 1);
    ok(C.words(M.pattern(U.pattern).cells[32].cmds[1], "DX7").startsWith("OP1 COARSE"), "K3 picks which parameter (a DX7's: OP1 COARSE)");
    await t.turn("K1", -1); await t.turn("K2", -1);
    ok(M.pattern(U.pattern).cells[32].cmds.length === 1 && C.group(M.pattern(U.pattern).cells[32].cmds[0].c) === "PARAM",
       "K2 back to NO COMMAND removes command 1; command 2 moves up");
    /* LEG in K2's list after A+S; then the punch-ins, in the keys' order; their value never 00 */
    const iAS = C.ENTRIES.findIndex(e => e.code === 0xC2), iLEG = iAS + 1;
    ok(C.ENTRIES[iLEG].code === 0xD3 && C.ENTRIES[iLEG].name === "LEGATO" && C.text({ c: 0xD3, v: 0 }) === "LEG" &&
       C.words({ c: 0xD3, v: 0 }) === "LEGATO" && !C.global({ c: 0xD3, v: 0 }), "K2's list: LEG (LEGATO) after A+S, a track's command");
    ok(C.ENTRIES.slice(iLEG + 1, iLEG + 17).every((e, k) => e.code === 0xC3 + k && e.key === "!") &&
       C.ENTRIES[iLEG + 1].name === "LOOP 1/4" && C.ENTRIES[iLEG + 16].name === "VINYL STOP",
       "K2's list: after LEG the 16 punch-ins, LOOP 1/4 ... VINYL STOP");
    await t.turn("K1", 1);
    for (let i = 0; i < 80 && C.entry(M.pattern(U.pattern).cells[32].cmds[1]) !== iLEG + 1; i++) await t.turn("K2", 1);
    let pin = M.pattern(U.pattern).cells[32].cmds[1];
    ok(pin && pin.c === 0xC3 && pin.v === 1 && C.text(pin) === "!01", "a punch-in picked with K2: !01, never 00");
    await t.turn("K4", -1);
    pin = M.pattern(U.pattern).cells[32].cmds[1];
    ok(pin.v === 1, "K4 down from 01: stays 01");
    await t.turn("K3", 1); await t.turn("K4", -1);
    pin = M.pattern(U.pattern).cells[32].cmds[1];
    ok(C.text(pin) === "!10" && C.words(pin) === "LOOP 1/4 16 ROWS" && C.global(pin),
       "K3 and K4 its two digits: !10, LOOP 1/4 16 ROWS, a global command");
    ok(C.words({ c: 0xD1, v: 8 }) === "TAPE STOP 8 ROWS" && C.words({ c: 0xD2, v: 1 }) === "VINYL STOP 1 ROW",
       "the note editor's words: TAPE STOP 8 ROWS, VINYL STOP 1 ROW");
    /* the keys' order, by kind: the gates under LOOP 1/16 and 1/32, the tape moves last (specs/ui/screens/fx.md) */
    ok(["LOOP 1/4", "LOOP 1/8", "LOOP 1/16", "LOOP 1/32", "STUTTER 1/16T", "BIT CRUSH", "GATE 1/16", "GATE 1/32",
        "LOW-PASS SWEEP", "HIGH-PASS SWEEP", "ECHO THROW", "REVERB SWELL", "REVERSE", "HALF SPEED", "TAPE STOP", "VINYL STOP"]
           .every((n, k) => C.ENTRIES[iLEG + 1 + k].name === n),
       "the punch-ins by key: loops; stutter, crush, the gates; sweeps, echo, swell; reverse, half speed, the two stops");
    /* the numbers are fixed (specs/files/commands.md, The numbers are fixed): the base of 2026-10-10. A change
     * here means a song's meaning changed: add new numbers instead */
    {
        const PUNCH0 = ["LOOP 1/4", "LOOP 1/8", "LOOP 1/16", "LOOP 1/32", "STUTTER 1/16T", "BIT CRUSH", "GATE 1/16", "GATE 1/32",
                        "LOW-PASS SWEEP", "HIGH-PASS SWEEP", "ECHO THROW", "REVERB SWELL", "REVERSE", "HALF SPEED", "TAPE STOP", "VINYL STOP"];
        const FX0 = "GATE PATTERN,GATE RATE,GATE DEPTH,GATE SMOOTH,FILTER CUT,FILTER RESO,DRIVE DRIVE,DRIVE TONE,LO-FI BITS,LO-FI RATE,"
                  + "LO-FI DUST,CHORUS MODE,CHORUS RATE,CHORUS DEPTH,CHORUS MIX,DELAY TIME,DELAY FEEDBACK,DELAY TONE,DELAY PING-PONG,"
                  + "DELAY MIX,REVERB SIZE,REVERB DAMP,REVERB PRE-DELAY,REVERB MIX,LIMITER CEILING,LIMITER RELEASE,DUCK SOURCE,DUCK DEPTH,"
                  + "DUCK RELEASE";
        const fxNow = FX0.split(",").map((s, i) => C.FX[0x90 + i] ? C.FX[0x90 + i][0] + " " + M.FX[C.FX[0x90 + i][0]].knobs[C.FX[0x90 + i][1]].n : "?");
        ok(PUNCH0.every((n, k) => C.words({ c: 0xC3 + k, v: 1 }).startsWith(n + " ")) &&
           fxNow.join() === FX0 && !C.FX[0xAD] &&
           C.FX_NAMES.join() === "GATE,FILTER,DRIVE,LO-FI,CHORUS,DELAY,REVERB,LIMITER,DUCK,METRONOME",
           "the fixed numbers: punch-ins 0xC3-0xD2, FX knobs 0x90-0xAC, the effects 0-9 as of 2026-10-10");
    }
    M.pattern(U.pattern).cells[32].cmds.splice(1);
    U.editor.slot = 1;

    /* ---- the 1-track view: the whole cell, short names, K1's field white (specs/ui/screens/editor.md) ---- */
    {
        const was = { pattern: U.pattern, view: U.editor.view, row: U.editor.row, track: U.editor.track, slot: U.editor.slot, left: U.editor.left };
        const seen = [];
        U.editor.view = 8;
        for (let i = 0; i < 4; i++) { await t.tap("SEQ"); seen.push(U.editor.view); }
        ok(seen.join() === "4,2,1,8", "SEQ: 8, 4, 2, 1 tracks, round");
        /* every command's short name fits 9 characters: the entries, every engine's parameters, the mixer, the FX */
        const all = [];
        for (const e of C.ENTRIES) if (e.code != null && typeof e.code !== "string") all.push(C.short({ c: e.code, v: e.sub != null ? e.sub << 4 : 1 }, "DX7"));
        for (const v of [0, 8, 0x40]) all.push(C.short({ c: 0x0F, v }, "DX7"));
        for (const eng of Object.keys(C.PARAMS)) for (const c of Object.keys(C.PARAMS[eng])) all.push(C.short({ c: +c, v: 0 }, eng));
        for (const c of Object.keys(C.MIXER)) all.push(C.short({ c: +c, v: 0 }, "DX7"));
        for (const c of Object.keys(C.FX)) for (let v = 0; v < C.FX_NAMES.length; v++) all.push(C.short({ c: +c, v }, "DX7"));
        ok(all.every(s => s.length > 0 && s.length <= 9) && all.length > 150 && !all.some(s => s.startsWith("PARAM ")),
           "every command's short name fits 9 characters (" + all.length + " checked)");
        ok(C.short({ c: 0xD1, v: 8 }) === "TAPE STOP" && C.short({ c: 0x41, v: 0x40 }, "TB303") === "CUTOFF" &&
           C.short({ c: 0x83, v: 0x20 }) === "REV SEND", "short names: TAPE STOP, CUTOFF, REV SEND");
        /* a TB-303 line; the cursor on row 14, K1 on CMD3: that field white, the others not */
        const TB = M.state.carts.find(c => c.type === "TB303").id << 8;
        const op = M.state.patterns.indexOf(null), q = M.state.patterns[op] = M.newPattern(op, 2, 32);
        const put = (r, note, inst, vol, cmds) => { const x = M.newCell(note, inst, vol); x.cmds = cmds.map(([c, v]) => ({ c, v })); q.cells[r * 16 + 1] = x; };
        put(0x10, 36, TB, 0x7F, [[0xC0, 0], [0x41, 0x40]]);
        put(0x11, null, null, null, [[0x41, 0x48]]);
        put(0x14, 43, null, 0x60, [[0xC2, 0], [0x41, 0x60], [0xD1, 0x08], [0x90, 0x10]]);
        U.pattern = op;
        Object.assign(U.editor, { view: 1, row: 0x14, track: 1, slot: 3, left: 1 });   /* left: as SEQ's scrolling sets it */
        t.frame();
        const Dr = t.PTFM.draw, [wr, wg, wb] = Dr.rgb[15], WHITE = ((wr >> 3) << 11) | ((wg >> 2) << 5) | (wb >> 3);
        const whites = (x0, y0, w, h) => { let n = 0; for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) if (Dr.fb[y * 240 + x] === WHITE) n++; return n; };
        const fy = 17 + 14 + 4 * 24;                             /* row 14 of the page from row 10 */
        ok(whites(78, fy + 13, 76, 10) > 40 && whites(162, fy + 1, 76, 10) === 0 && whites(78, fy + 1, 76, 10) === 0,
           "the cursor's row: CMD3 (!08 TAPE STOP) drawn white, CMD1 and CMD2 not");
        ok(U.screen.footer()[0] === "!08 TAPE STOP 8 ROWS", "the footer says it in words: !08 TAPE STOP 8 ROWS");
        U.editor.slot = 0; t.frame();
        ok(whites(42, fy + 13, 12, 10) > 10 && whites(78, fy + 13, 76, 10) === 0, "K1 on the volume: the volume white instead");
        U.editor.row = 0x11; U.editor.slot = 1; t.frame();
        const row11 = 17 + 14 + 1 * 24;
        ok(whites(78, row11 + 1, 76, 10) > 20, "a row without a note: its command white under the cursor");
        M.state.patterns[op] = null;
        Object.assign(U.editor, was);
        U.pattern = was.pattern;
    }

    /* ---- blocks ---- */
    t.down("SEL"); await t.sleep(20); await t.turn("SELECT", 1); await t.turn("ALGORITHM", 1); t.up("SEL"); await t.sleep(150);
    ok(U.editor.block && U.editor.block.r1 === 3 && U.editor.block.t1 === 1, "SEL held + SELECT / ALGORITHM mark a block");
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("COPY"); await t.tap("SEL");
    U.editor.block = null; U.editor.row = 10; U.editor.track = 0;
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("PASTE"); await t.tap("SEL");
    ok(M.pattern(U.pattern).cells[160] && M.pattern(U.pattern).cells[160].note === 60, "PASTE puts the block at the cursor");
    /* the clipboard's 2 KB, packed, each track's end byte included (fw/tests/ui2_test.c's twin): tracks 0-1 64 cells
     * of 13 B (note, volume, 4 commands) + 1 = 833 B each, track 2 29 such + one of 4 B (note, 1 command) + 1 =
     * 382 B: 2,048 B fits; with the empty track 3 too, BLOCK TOO BIG TO COPY and the clipboard empty */
    {
        const p = M.pattern(U.pattern), keep = p.cells, rows = p.rows;
        const full = n => ({ note: 48, inst: null, vol: n > 1 ? 0x40 : null, cmds: [[1, 1], [2, 2], [3, 3], [4, 4]].slice(0, n) });
        p.rows = 64;
        p.cells = new Array(64 * 16).fill(null);
        for (let r = 0; r < 64; r++) { p.cells[r * 16] = full(4); p.cells[r * 16 + 1] = full(4); }
        for (let r = 0; r < 29; r++) p.cells[r * 16 + 2] = full(4);
        p.cells[29 * 16 + 2] = full(1);
        const copy = async t1 => {
            U.editor.block = { r0: 0, r1: 63, t0: 0, t1 }; U.editor.row = 0; U.editor.track = 0;
            await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("COPY"); await t.tap("SEL");
            return U.toastNow();
        };
        ok(await copy(2) === "COPIED 64 X 3", "the clipboard: a block of 2,048 B packed is copied");
        const big = await copy(3);
        U.editor.block = null; U.editor.row = 6; U.editor.track = 5;
        await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("PASTE"); await t.tap("SEL");
        ok(big === "BLOCK TOO BIG TO COPY" && U.toastNow() === "NOTHING COPIED", "... one more track (empty): BLOCK TOO BIG TO COPY, the clipboard empty");
        p.cells = keep; p.rows = rows;
        U.editor.block = null; U.editor.row = 10; U.editor.track = 0;
    }

    /* ---- the realtime take ---- */
    M.state.settings["COUNT-IN"] = "OFF";
    U.editor.row = 16;
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
    ok(P.take && P.playing, "PLAY with REC on starts a take when let go (COUNT-IN OFF)");
    await t.sleep(300);
    t.keyDown(9); await t.sleep(250); t.keyUp(9); await t.sleep(100);
    ok(M.pattern(U.pattern).cells.some((x, i) => x && x.note === 62 && (i >> 4) >= 16), "a key during the take writes at the playing row");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    ok(!P.take && !P.playing && U.editor.row === 16 && U.toastNow() === "TAKE STOPPED", "PLAY stops the take (TAKE STOPPED); the cursor goes back to its row");
    /* a take loops: round the whole pattern, past its end, until PLAY or REC */
    {
        const rows = M.pattern(U.pattern).rows;
        U.editor.row = rows - 2;
        t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
        await t.sleep(P.rowSeconds() * 5 * 1000 + 200);
        const h = P.at();
        ok(P.take && P.playing && h && h.row < rows - 2, "the take goes on past the pattern's end, from its first row (row " + (h && h.row) + ")");
        await t.tap("REC");
        ok(!P.take && !P.playing && U.editor.rec && U.editor.row === rows - 2, "REC stops the take too; REC stays on, the cursor back to the take's row");
        U.editor.row = 16;
    }
    /* SETTINGS' QUANTIZE (specs/ui/screens/settings.md, editor.md#realtime-takes): where a take's notes land (where the
     * take is heard stood in for, rows of 0.5 s), REAL TIME's EDx and Kxx; the keys heard as written (a note before
     * its line waits for it, one just after sounds at once by its track) */
    {
        const p = M.pattern(U.pattern), keep = p.cells.slice(), realAt = P.at, A = t.PTFM.audio;
        const heard = (row, frac) => { const h = realAt(); P.at = () => ({ row, pass: h ? h.pass : 0, t: P.time() - frac * .5, dur: .5, rpb: 4, pat: U.pattern }); };
        const press = (k, row, frac) => { heard(row, frac); t.keyDown(k); P.at = realAt; };
        const lift = (k, row, frac) => { heard(row, frac); t.keyUp(k); P.at = realAt; };
        const c = r => p.cells[r * 16 + U.editor.track];
        ok(M.SETTINGS.map(s => s.k).indexOf("QUANTIZE") === M.SETTINGS.map(s => s.k).indexOf("COUNT-IN") + 1 && U.set("QUANTIZE") === "ROW" &&
           M.SETTINGS.find(s => s.k === "QUANTIZE").list.join() === "2 ROWS,3 ROWS,4 ROWS,ROW,REAL TIME", "SETTINGS: QUANTIZE after COUNT-IN, ROW at first");
        U.editor.track = 0;
        let tk = 6;                                         /* (the take's ticks a row: REAL TIME's ticks) */
        const take = async (q, cases) => {
            M.state.settings.QUANTIZE = q;
            p.cells = keep.slice();
            t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
            tk = P.rowTicks();
            const got = [];
            for (const [row, frac, k] of cases) {
                p.cells = keep.slice(); for (let r = 0; r < 16; r++) p.cells[r * 16] = null;
                const calls = [], on0 = A.noteOn;
                A.noteOn = (id, time, midi, ref, vol, track, opts) => { calls.push({ id, midi, track }); return on0(id, time, midi, ref, vol, track, opts); };
                press(k, row, frac);
                A.noteOn = on0;
                const r = [...Array(16).keys()].find(x => c(x) && c(x).note === U.midi(k)), ed = r != null && c(r).cmds.find(x => x.c === 0x0E);
                got.push((r == null ? "-" : r) + (ed ? "d" + (ed.v & 15) : "") + (calls.some(x => x.midi === U.midi(k) && x.track === 0) ? "!" : ""));
                lift(k, row + 1, .1);
            }
            t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
            return got.join();
        };
        ok(await take("ROW", [[5, .3, 2], [9, .6, 4]]) === "5!,10", "QUANTIZE ROW: the nearest row (5.3 on 5, heard at once; 9.6 on 10, waiting for it)");
        ok(await take("2 ROWS", [[4, .9, 2], [7, .1, 4]]) === "4!,8", "2 ROWS: the nearest line of 2 (4.9 on 4, at once; 7.1 on 8, waiting)");
        ok(await take("3 ROWS", [[4, .4, 2], [7, .6, 4]]) === "3!,9", "3 ROWS: the nearest line of 3 (4.4 on 3; 7.6 on 9)");
        ok(await take("4 ROWS", [[5, .9, 2], [10, .1, 4]]) === "4!,12", "4 ROWS: the nearest line of 4 (5.9 on 4; 10.1 on 12)");
        const rt = await take("REAL TIME", [[5, .5, 2], [9, 0, 4]]);
        ok(rt === "5d" + Math.floor(tk / 2) + "!,9!", "REAL TIME: the row it falls in, a note delay of its tick (5.5: ED" + Math.floor(tk / 2) + "), at once (" + rt + ")");
        {   /* the note offs: the nearest row, a row after the note at least; REAL TIME a key off at its tick */
            M.state.settings.QUANTIZE = "ROW";
            p.cells = keep.slice(); for (let r = 0; r < 16; r++) p.cells[r * 16] = null;
            t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
            press(2, 3, .2); lift(2, 3, .4);
            press(4, 6, .1); lift(4, 8, .7);
            t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
            const offs = [...Array(16).keys()].filter(r => c(r) && c(r).note === M.OFF).join();
            M.state.settings.QUANTIZE = "REAL TIME";
            p.cells = keep.slice(); for (let r = 0; r < 16; r++) p.cells[r * 16] = null;
            t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
            press(2, 3, .5); lift(2, 3, .7); press(4, 6, .1); lift(4, 8, .5);
            t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
            const kx = r => { const x = c(r) && c(r).cmds.find(y => y.c === 0x14); return x ? x.v : -1; };
            ok(offs === "4,9" && kx(3) === Math.floor(tk * .7) && c(3).note === U.midi(2) && kx(8) === Math.floor(tk / 2),
               "note offs: ROW the nearest row, a row after its note at least (3.4 on 4, 8.7 on 9); REAL TIME Kxx at its tick (3.7: in its note's cell, 8.5 on 8)");
        }
        M.state.settings.QUANTIZE = "ROW";
        p.cells = keep;
        M.settle();
        U.editor.row = 16;
    }
    /* COUNT-IN (specs/ui/transport.md): SETTINGS', 4 beats a bar, whatever the METRONOME; KEY arms the take */
    M.state.settings["COUNT-IN"] = "1 BAR";
    const clicks = [], click0 = t.PTFM.audio.click;
    t.PTFM.audio.click = (...a) => { clicks.push(a); };
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(150); t.frame();
    const cs = P.countInState();
    ok(P.countingIn() && !P.playing && cs && cs.bars === 1, "COUNT-IN 1 BAR: PLAY counts in first, nothing plays yet");
    ok(clicks.length >= 1 && clicks[0][1] === true && clicks[0][2] === 1, "... its own click: BEEP, the bar's first beat accented");
    await t.sleep(4 * 60 / P.tempo() * 1000 + 300);
    ok(!P.countingIn() && P.take && P.playing && clicks.length === 4, "... 4 beats, then the take plays");
    t.PTFM.audio.click = click0;
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    M.state.settings["COUNT-IN"] = "KEY";
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(150);
    ok(U.editor.armed && !P.take && !P.countingIn(), "COUNT-IN KEY: PLAY arms the take");
    t.keyDown(4); await t.sleep(200); t.keyUp(4); await t.sleep(100);
    ok(!U.editor.armed && P.take && P.playing && M.pattern(U.pattern).cells[16 * 16 + U.editor.track] &&
       M.pattern(U.pattern).cells[16 * 16 + U.editor.track].note === U.midi(4), "... the first key starts it, on the cursor's row");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(150);
    await t.tap("HOME");
    ok(!U.editor.armed && !P.take && !t.top(), "... HOME cancels the armed take");
    M.state.settings["COUNT-IN"] = "OFF";

    /* ---- REC while the loop plays: it plays on, the keys write step by step at the cursor (no overdub) ---- */
    await t.tap("REC");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(P.playing && P.what === "loop", "PLAY with REC off loops the pattern");
    await t.tap("REC");
    ok(P.playing && P.what === "loop" && U.editor.rec && !P.take && !P.recording(), "REC while the loop plays: it plays on, no take, nothing recording");
    {
        U.editor.row = 40; U.editor.track = 0;
        await t.sleep(300); t.frame();
        await t.key(9);
        const c40 = M.pattern(U.pattern).cells[40 * 16];
        ok(c40 && c40.note === U.midi(9) && U.editor.row === 41, "... a key writes at the cursor's row, step by step, the cursor moving on (not the playing row)");
    }
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(150);
    ok(!P.playing && U.editor.rec && !P.take && U.toastNow() === "LOOP STOPPED: PLAY FOR A TAKE", "PLAY stops the loop; REC stays on: LOOP STOPPED: PLAY FOR A TAKE");
    await t.tap("REC");

    /* ---- the row preview: whatever plays stops first ---- */
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(P.playing && P.what === "loop", "(the loop plays)");
    await t.tap("ARP");
    ok(P.repeating() && !P.playing, "ARP on the EDITOR repeats the row; the loop stops first");
    await t.tap("ARP");
    ok(!P.repeating(), "ARP again: off");

    /* ---- SEL → RECORD PATTERN: only notes, into the cursor's track from the cursor, to the end ---- */
    {
        const pE = M.pattern(U.pattern), trE = U.editor.track, was = pE.cells.map(c => JSON.stringify(c));
        U.editor.row = 2;
        await t.tap("SEL"); const mE = U.top(); mE.sel = mE.items.indexOf("RECORD PATTERN"); await t.tap("SEL");
        ok(t.top() === "pattern", "EDITOR: SEL, RECORD PATTERN opens the dialog");
        await t.key(U.WHITE[0]); await t.key(U.WHITE[8]); await t.tap("REC");
        const on = r => r >= 2 && (r - 2) % 8 === 0, nE = U.recPat.note;
        ok(!t.top() && pE.cells.every((c, i) => {
            const r = i >> 4, tr = i & 15;
            return tr === trE && r < pE.rows && on(r) ? c && c.note === nE && c.inst === U.inst : JSON.stringify(c) === was[i];
        }), "REC: a note at each step on, from row 02 to the pattern's end; the cells between and the other tracks as they were (no note offs)");
        U.edit(() => { was.forEach((s, i) => { pE.cells[i] = JSON.parse(s); }); });     /* (back as it was, for what follows) */
        U.recPat.steps.fill(false);
    }
    /* ---- a FUSION row's tick commands: COIN's first note cut at tick 5 (EC5), its second 5 ticks late (ED5) ---- */
    {
        const AU = t.PTFM.audio, ev = [], wrap = {};
        for (const k of ["noteOn", "noteOff"]) { wrap[k] = AU[k]; AU[k] = function (id, time) { if (/\//.test(id)) ev.push([k, id, time]); return wrap[k].apply(this, arguments); }; }
        const fus = M.state.carts.find(c => c.type === "FUSION"), slot = fus.slots.findIndex(s => s && s.name === "COIN"), was = U.inst;
        U.setInst(fus.id << 8 | slot);
        t.keyDown(12); await t.sleep(400); t.keyUp(12); await t.sleep(50);
        const tick = 2.5 / M.state.songs[U.song].tempo, on = ev.filter(e => e[0] === "noteOn");
        const off0 = ev.find(e => e[0] === "noteOff" && e[1] === on[0][1]);
        ok(on.length === 2 && Math.abs(on[1][2] - on[0][2] - 5 * tick) < .002 && off0 && Math.abs(off0[2] - on[0][2] - 5 * tick) < .01,
           "FUSION COIN: the first note cut at tick 5 (EC5), the second starting there (ED5)");
        Object.assign(AU, wrap);
        U.setInst(was);
    }
    /* ---- the pipette: ENV held takes the cursor's cell's instrument; SEL → TAKE ITS INSTRUMENT too ---- */
    {
        const pE = M.pattern(U.pattern), was = U.inst, row0 = U.editor.row, track0 = U.editor.track;
        const i = pE.cells.findIndex((c, k) => c && c.inst != null && c.inst !== was && (k & 15) < pE.tracks);
        ok(i >= 0, "a cell naming another instrument, for the pipette");
        if (i >= 0) {
            U.editor.row = i >> 4; U.editor.track = i & 15;
            t.down("ENV"); await t.sleep(650); t.up("ENV"); await t.sleep(50);
            ok(U.inst === pE.cells[i].inst && !t.top(), "ENV held: the cursor's cell's instrument becomes current (no pattern list)");
            U.setInst(was);
            await t.tap("SEL"); const mP = U.top(); mP.sel = mP.items.indexOf("TAKE ITS INSTRUMENT"); await t.tap("SEL");
            ok(U.inst === pE.cells[i].inst, "SEL, TAKE ITS INSTRUMENT: the same, from the menu");
            await t.tap("ENV");
            ok(t.top() === "list" || !!t.top(), "ENV tapped: still the pattern list");
            await t.tap("HOME");
            U.setInst(was); U.editor.row = row0; U.editor.track = track0;
        }
    }

    /* ---- the pattern's properties ---- */
    await t.tap("GLO"); await t.turn("SELECT", 1);
    t.down("SEL"); await t.sleep(20); await t.turn("K1", -2); t.up("SEL"); await t.sleep(150);
    ok(t.top() === "question", "fewer rows that drop notes: a question first");
    await t.tap("HOME"); await t.tap("GLO");

    /* ---- SAVE, REVERT, memory full ---- */
    t.frame();
    ok(M.unsaved > 0 && t.leds[9] === .25, "SAVE's LED dim while something is unsaved");
    await t.tap("SAVE"); await t.sleep(1800);
    t.frame();
    ok(M.unsaved === 0 && t.leds[9] === 0, "SAVE saves; its LED goes dark");
    await t.tap("REC"); U.editor.row = 30; await t.key(7); await t.tap("REC");
    await t.combo("SAVE", "OCT-");
    ok(t.top() === "question", "SAVE + OCT-: the revert question");
    await t.tap("OCT+"); await t.sleep(800);
    ok(M.unsaved === 0 && !M.pattern(U.pattern).cells[480], "REVERT drops the edit");
    await t.tap("REC"); U.editor.row = 30; await t.key(7); await t.tap("REC");
    P.playSong(U.song); await t.tap("SAVE");
    const saveLv = new Set();
    for (let i = 0; i < 12; i++) { await t.sleep(50); t.frame(); saveLv.add(t.leds[9]); }
    ok(U.saveWaiting && saveLv.size === 2 && saveLv.has(.25) && saveLv.has(0), "SAVE while playing waits, blinking dim");
    P.pause(); await t.sleep(1800); t.frame();
    ok(M.unsaved === 0 && !U.saveWaiting && t.leds[9] === 0, "... and saves at the pause: dark");
    const max = M.EDITS_MAX;
    M.EDITS_MAX = M.editsUsed + 10;
    await t.tap("REC"); U.editor.row = 40; await t.key(7); await t.key(9); await t.tap("REC");
    ok(t.top() === "question" && U.top().full, "memory full: the question");
    await t.tap("SAVE");
    ok(t.top() === "question", "... SAVE does not answer it: only OCT+ / OCT-");
    await t.tap("OCT-");
    ok(!t.top(), "OCT-: BACK");
    M.EDITS_MAX = max;
    /* the edit memory holds each changed piece once: a cell changed again and again costs no more than at first */
    {
        U.editor.row = 44; U.editor.track = 0; U.editor.slot = 1;
        await t.turn("K2", 1);
        const used1 = M.editsUsed;
        await t.turn("K2", 12);
        ok(used1 > 0 && M.editsUsed === used1, "a command turned again and again: the edit memory as after the first click (" + used1 + " B, now " + M.editsUsed + ")");
        await t.turn("K2", -40);
        ok(!M.pattern(U.pattern).cells[44 * 16], "(back to no command: the cell empty again)");
    }

    /* ---- mutes and solo ---- */
    await t.homeKey(4);
    ok(U.mutes[2], "HOME + white key 3 mutes track 03");
    await t.homeKey(4);
    ok(!U.mutes[2], "again: on");
    t.down("HOME"); await t.sleep(30); t.keyDown(0); await t.sleep(650); t.keyUp(0); await t.sleep(30); t.up("HOME"); await t.sleep(200);
    ok(U.solo === 0, "HOME + white key held past the hold delay: solo");
    U.solo = -1;
    t.down("FX"); await t.sleep(650); t.frame();
    const fxKeys = U.WHITE.map(n => t.leds[14 + n]);
    t.up("FX"); await t.sleep(160);
    ok(fxKeys.every((v, i) => v === (i % 4 === 0 ? .25 : 0)), "FX held: white keys 1, 5, 9, 13 dim, where each group of 4 starts; the rest dark");

    /* ---- SONGS, the order, going deep ---- */
    await t.homeKey(1);
    await t.tap("EDIT");
    await t.turn("ALGORITHM", 2);
    const s = M.state.songs[U.song], was = s.order[2], atEdit = JSON.stringify(s.order);
    await t.turn("K1", 1);
    ok(s.order[2] !== was, "K1 changes the pattern at the position");
    const len = s.order.length;
    await t.tap("LFO");
    ok(s.order.length === len - 1, "LFO removes the position");
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("EDIT PATTERN"); await t.tap("SEL");
    t.frame();
    ok(U.screen.name === "EDITOR" && U.isDeep() && U.pattern === s.order[2] && t.leds[6] === 1, "SEL, EDIT PATTERN on a position: the EDITOR, deep, EDIT lit");
    await t.tap("HOME");
    ok(U.screen.name === "SONGS", "HOME comes back to SONGS");
    await t.tap("HOME");
    ok(JSON.stringify(s.order) === atEdit, "HOME leaves the order dropping its changes (K1's, LFO's)");
    await t.tap("EDIT"); await t.turn("K1", 1);
    const keptOrder = JSON.stringify(s.order);
    await t.tap("EDIT");
    ok(keptOrder !== atEdit && JSON.stringify(s.order) === keptOrder, "EDIT leaves the order keeping them");
    /* the song's REVERT: last in the list's menu while the song is unsaved; its record whole as saved */
    {
        const i = U.song, saved = M.savedThing("song", i);
        await t.tap("SEL"); m = U.top();
        ok(m.items[m.items.length - 1] === "REVERT" && !m.items.includes("UNDO"), "SONGS' menu: REVERT last while the song is unsaved: " + m.items.join());
        m.sel = m.items.indexOf("REVERT"); await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "REVERT SONG " + M.hex(i) + " TO SAVED?" && U.top().answers.join() === "REVERT,KEEP",
           "REVERT asks first: REVERT SONG " + M.hex(i) + " TO SAVED?, REVERT / KEEP");
        await t.tap("OCT+");
        ok(!t.top() && JSON.stringify(M.state.songs[i]) === JSON.stringify(saved) && !M.unsavedThing("song", i) && U.toastNow() === "REVERTED",
           "... REVERT: the song as saved, its order too (REVERTED)");
        await t.tap("SEL"); m = U.top();
        ok(!m.items.includes("REVERT"), "... nothing unsaved in it: no REVERT");
        await t.tap("HOME");
    }
    await t.tap("HOME"); await t.turn("SELECT", 1); await t.tap("HOME");
    ok(U.screen.name === "EDITOR", "then HOME, SELECT goes to the next screen");

    /* ---- SONGS: CLONE and DELETE take the player's song places along with the list ---- */
    await t.homeKey(1);
    U.song = 0;
    P.kept.song[1] = { pos: 2, row: 0 };
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("CLONE"); await t.tap("SEL");
    ok(U.song === 1 && P.kept.song[2] && P.kept.song[2].pos === 2 && !P.kept.song[1],
       "CLONE: the songs after it keep their places (song 1's is now song 2's)");
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("DELETE"); await t.tap("SEL");
    await t.tap("OCT+");
    ok(U.song === 1 && P.kept.song[1] && P.kept.song[1].pos === 2 && !P.kept.song[2],
       "DELETE: they move back (song 2's place is song 1's again)");
    delete P.kept.song[1];

    /* ---- SONGS' DELETE (specs/ui/screens/songs.md#deletes-question): one line, ITS PATTERNS TOO, ticked at first: the
     * song's own patterns go too (toast); cleared: the song alone; a clone's (all shared): NONE, OCT+ still deletes ---- */
    {
        const s = M.state.songs[0], own = M.songOwn(s), k = own.items.length, n0 = M.state.songs.length, u0 = M.unsaved;
        const keepSongs = M.state.songs.slice(), undo = () => U.edit(() => {
            M.state.songs.splice(0, M.state.songs.length, ...keepSongs);
            for (const p of own.items) M.state.patterns[p.n] = p;
            P.songsRemap(i => i + 1);
        });
        const del = async clear => {
            U.song = 0;
            await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("DELETE"); await t.tap("SEL");
            const q = U.top();
            if (clear) await t.turn("K1", -1);
            await t.tap("OCT+");
            return q;
        };
        const q = await del(false);
        ok(q.title === "DELETE SONG 00?" && q.more.sub === s.name && q.more.bare && q.rows.length === 1 && q.rows[0].name === "ITS PATTERNS TOO" &&
           q.rows[0].count === k && k > 0 && q.rows[0].note === k + "  " + M.kb(own.bytes),
           "DELETE asks with one line: ITS PATTERNS TOO, the song's own (" + q.rows[0].note + ")");
        ok(M.state.songs.length === n0 - 1 && own.items.every(p => !M.state.patterns[p.n]) && U.toastNow() === "SONG 00 DELETED, " + k + " PATTERNS TOO",
           "... ticked at first: OCT+ deletes the song and its own patterns (" + U.toastNow() + ")");
        undo();
        ok(M.state.songs.length === n0 && own.items.every(p => M.state.patterns[p.n]) && M.unsaved === u0, "... (put back)");
        await t.sleep(2100);                                 /* (the toast gone) */
        await del(true);
        ok(M.state.songs.length === n0 - 1 && own.items.every(p => M.state.patterns[p.n]) && !/DELETED/.test(U.toastNow() || ""),
           "... the tick cleared: the song alone, no toast");
        undo();
        const c = Object.assign(structuredClone(s), { name: "COPY" });
        U.edit(() => { M.state.songs.splice(1, 0, c); P.songsRemap(i => i ? i + 1 : i); });
        U.song = 1;
        await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("DELETE"); await t.tap("SEL");
        const cq = U.top();
        ok(cq.rows[0].count === 0 && !cq.rows[0].on && cq.rows[0].note === "NONE" && cq.more.foot === (k + own.shared) + " SHARED WITH OTHER SONGS STAY",
           "... a copy (all shared): NONE, not ticked; " + cq.more.foot);
        await t.turn("K1", 1);
        ok(!cq.rows[0].on, "... NONE cannot be ticked");
        await t.tap("OCT+");
        ok(M.state.songs.length === n0 && M.state.songs[1] !== c && own.items.every(p => M.state.patterns[p.n]),
           "... OCT+ with nothing ticked: the copy alone");
        ok(M.unsaved === u0, "... (as before)");
    }

    /* ---- switching songs rewinds (specs/ui/transport.md); the same song goes on from its place ---- */
    P.playSong(0); await t.sleep(700); P.pause();
    const kept0 = P.kept.song[0];
    P.playSong(0); await t.sleep(100);
    ok(kept0 && kept0.row > 2 && P.at().row >= kept0.row, "the same song again: on from where it paused");
    P.pause(); P.playSong(1); await t.sleep(100);
    ok(P.at().pos === 0 && P.at().row < 3, "another song: from its beginning");
    P.pause(); P.playSong(0); await t.sleep(100);
    ok(P.at().pos === 0 && P.at().row < 3, "... and the one left went back to its start");
    P.pause(); P.stopToStart();

    /* ---- the transport ---- */
    await t.homeKey(1);
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(900);
    ok(P.playing && P.what === "song", "PLAY on SONGS plays the song");
    await t.homeKey(8);
    const r0 = P.at().row;
    await t.turn("SELECT", 1);
    ok(!P.playing, "SELECT on PLAYBACK pauses");
    await t.turn("SELECT", 1);
    ok(P.at().row === r0 + 1 || P.at().row === r0 + 2, "then each click plays the next row");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(300);
    t.down("HOME"); await t.sleep(30); t.down("PLAY"); await t.sleep(30); t.up("PLAY"); t.up("HOME"); await t.sleep(400);
    ok(P.playing && P.at().pos === 0 && P.at().row < 4, "HOME + PLAY restarts from the start");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200); t.frame();
    ok(!P.playing && t.leds[12] === 1 && t.leds[41] === 0, "paused: PLAY's red on, its green off");
    t.down("HOME"); await t.sleep(30); t.down("PLAY"); await t.sleep(30); t.up("PLAY"); t.up("HOME"); await t.sleep(400); t.frame();
    ok(!P.playing && P.atStart && t.leds[12] === .25, "HOME + PLAY while paused: at the start, PLAY's red dim (a song to play)");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(300);
    ok(P.playing && P.at().pos === 0, "... and PLAY plays it from the start");
    await t.homeKey(15);
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(!P.playing && P.what === "song", "PLAY elsewhere pauses what played last");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(P.playing && P.what === "song", "... and resumes it");
    await t.homeKey(22);
    ok(U.screen.name === "LOOPS" && P.playing && P.what === "song", "into LOOPS: the song plays on");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(300);
    ok(!P.playing && P.what === "song", "PLAY there with no loops yet: pauses the song");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(300);
    ok(!P.playing && U.flash[12], "PLAY again, nothing to play: refused (REC first)");
    await t.sleep(700); t.frame();
    ok(t.leds[12] === 0, "... and PLAY's red dark there: nothing to play");
    await t.tap("HOME"); t.frame();
    {
        const hl3 = U.homeLines(U.home()).map(lineText);
        ok(t.top() === "home" && hl3[0] === "SELECT: THE SCREENS, ONE BY ONE" && hl3.includes("WHITE KEY: MUTE A LOOP, HELD: SOLO") &&
           hl3.includes("SEQ: WHITE KEYS SOLO INSTEAD"), "HOME in LOOPS: its combination, as everywhere; WHITE KEY: MUTE A LOOP, HELD: SOLO; SEQ: solo instead");
        await t.tap("SEQ");
        ok(U.homeLines(U.home()).map(lineText).includes("WHITE KEY: SOLO A LOOP"), "... after SEQ: WHITE KEY: SOLO A LOOP");
        await t.tap("SEQ");
    }
    await t.turn("SELECT", 2); await t.tap("HOME");
    ok(U.screen.name === "SETTINGS", "HOME, SELECT: LOOPS is in the round, DJ after it, then SETTINGS");
    await t.tap("HOME"); await t.turn("SELECT", -3); await t.tap("HOME");
    ok(U.screen.name === "FX", "... FX before it");
    await t.homeKey(15);

    /* ---- LOOPS: two loops made, for the transport's checks below (its behaviour: tests/playground/loops.js) ---- */
    U.setInst(0x0113);
    await t.homeKey(22);
    const LS = () => M.state.loops;
    await t.tap("REC");
    ok(U.screen.state().arm && !P.playing, "LOOPS: REC on + NEW LOOP with nothing playing: armed, nothing plays yet");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(150);
    ok(P.playing && P.what === "loops" && LS().list.length === 1 && P.recording(), "... PLAY (COUNT-IN OFF): the loops and the first loop's take at once");
    t.keyDown(7); await t.sleep(150); t.keyUp(7); await t.sleep(100);
    await t.tap("REC");
    ok(LS().rows === 16 && LS().list.length === 1 && LS().list[0].inst === 0x0113 && !U.screen.state().take,
       "... REC in bar 1: the first loop closes on its bar line (1 bar), in the current instrument");
    await t.tap("ARP");
    for (const k of [0, 4, 8, 12]) await t.key(U.WHITE[k]);
    await t.tap("REC");
    ok(LS().list.length === 2 && !t.top(), "... ARP's pattern recorder, REC: a second loop");
    /* the loops play on outside LOOPS until something else starts; PLAY there pauses and resumes them */
    P.playSong(0); await t.sleep(200);
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(300);
    ok(P.playing && P.what === "loops" && P.kept.song[0], "a song playing, PLAY in LOOPS: the loops start, the song pauses (its place kept)");
    await t.tap("HOME"); await t.turn("SELECT", -2); await t.tap("HOME");
    ok(U.screen.name === "VISUAL" && P.playing && P.what === "loops", "HOME, SELECT back twice: VISUAL; the loops play on");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(!P.playing && P.what === "loops", "PLAY there pauses them");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(200);
    ok(P.playing && P.what === "loops", "... and resumes them");
    P.pause();
    /* the loops played and stopped, LOOPS left: what the keys hear, so FX edits THE LOOPS; PLAYBACK's GLO, UNLOAD SONG
     * (THE LOOPS): let go, the keys' song held, the loops themselves kept (fw/tests/ui3_test.c test_unload_loops) */
    ok(t.PTFM.audio.fxSong() === M.state.loops && t.PTFM.audio.fxHeard() === M.state.loops,
       "the loops paused outside LOOPS: FX edits THE LOOPS, what the keys hear");
    {
        const had = M.state.loops.list.length;
        await t.homeKey(8); await t.tap("GLO"); await t.turn("SELECT", 1);
        const it = U.top() && U.top().items[1], itName = it && it.get();
        await t.tap("EDIT");
        ok(it && it.name === "UNLOAD SONG" && itName === "THE LOOPS" && !P.held() && !t.top() &&
           t.PTFM.audio.fxHeard() === M.state.keys && M.state.loops.list.length === had && had > 0,
           "PLAYBACK's GLO: UNLOAD SONG (THE LOOPS): the loops let go, kept; the keys' song held");
    }
    P.playSong(U.song); await t.sleep(100); P.pause();   /* (the current song held again: FX's checks below edit its setup) */

    /* ---- INSTRUMENTS: references, FUSION ---- */
    await t.homeKey(5);
    U.setInst(0x0100);
    const s00 = M.inst(0x0100), s01 = M.inst(0x0101);
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("SWAP"); await t.tap("SEL");
    ok(!t.top(), "SEL, SWAP: no box, the instrument floats in the list");
    await t.turn("SELECT", 1); await t.tap("SEL");
    ok(t.top() === "question" && M.inst(0x0100) === s00, "SELECT shows where, SEL puts it down: a used one asks about references first");
    await t.tap("HOME");
    ok(M.inst(0x0100) === s00 && M.inst(0x0101) === s01, "... HOME there: nothing moved");
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("SWAP"); await t.tap("SEL");
    await t.turn("SELECT", 1); await t.tap("HOME");
    ok(!t.top() && M.inst(0x0100) === s00, "afloat, HOME takes it back");
    /* COPY takes 4 slots at most (the firmware's COPY holds 4), the first 4 marked */
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("MARK"); await t.tap("SEL");
    await t.turn("SELECT", 5);
    await t.tap("SEL"); m = U.top();
    const marked = m.title;
    m.sel = m.items.indexOf("COPY"); await t.tap("SEL");
    ok(marked === "SLOTS 00-05" && U.toastNow() === "COPIED 4", "6 slots marked (" + marked + "), COPY: COPIED 4");
    await t.tap("HOME"); await t.tap("HOME");
    U.setInst(0x0100);
    if (!M.cart(1).slots.some(x => !x)) {
        await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("MOVE"); await t.tap("SEL");
        await t.tap("ENV");
        ok(t.top() === "list", "MOVE in a full cartridge: nowhere to go, nothing floats (ENV still opens the list)");
        await t.tap("HOME");
    }
    /* the built-in instruments, the firmware's: edited as any; changed, BACK TO BUILT-IN drops the change (an edit) */
    {
        const D0 = M.state.carts.find(c => c.builtin && c.type === "DX7"), label = "CARTRIDGE " + M.hex(D0.id);
        U.setInst(D0.id << 8);
        await t.tap("EDIT");
        const opened = U.screen.editing("inst", U.inst), before = JSON.stringify(D0.slots[0]);
        await t.turn("K1", 3);                               /* (OP1's LEVEL; at its top, down instead) */
        if (JSON.stringify(M.cart(D0.id).slots[0]) === before) await t.turn("K1", -6);
        await t.tap("EDIT");                                 /* (leaving, keeping the change) */
        ok(opened && JSON.stringify(M.cart(D0.id).slots[0]) !== before && M.builtinChanged("cart", M.cart(D0.id)),
           "EDIT on a built-in instrument: its edit page, a knob changes it (edited as any)");
        ok(!M.asBuilt("cart", M.cart(D0.id)), "... no longer brown in the lists from the edit on (as the firmware's: before SAVE)");
        await t.tap("ENV"); await t.tap("SEL");
        ok(t.top() === "menu" && U.top().title === label && U.top().items.join() === "NEW,CLONE,RENAME,AUTO LEVEL ALL,BACK TO BUILT-IN,REVERT",
           "the cartridge list's menu on a changed built-in one, REVERT last while unsaved: " + U.top().title + ": " + U.top().items.join());
        m = U.top(); m.sel = m.items.indexOf("BACK TO BUILT-IN"); await t.tap("SEL");
        ok(!M.builtinChanged("cart", M.cart(D0.id)) && U.toastNow() === label + " BUILT-IN AGAIN" && !M.unsavedThing("cart", D0.id) &&
           M.asBuilt("cart", M.cart(D0.id)), "BACK TO BUILT-IN: the firmware's own again (brown), nothing unsaved in it (" + U.toastNow() + ")");
        await t.tap("SEL");
        ok(t.top() === "menu" && U.top().items.join() === "NEW,CLONE,RENAME,AUTO LEVEL ALL", "... its menu now: no BACK TO BUILT-IN, no REVERT: " + U.top().items.join());
        await t.tap("HOME");
        M.settle();
        await t.tap("HOME");
    }
    /* a built-in cartridge's menu (a synth cartridge as shipped); then the demo disk's FUSION cloned: the clone is
     * edited below */
    const BI = M.state.carts.find(c => c.builtin && c.rom).id;
    U.setInst(BI << 8);
    await t.tap("ENV"); await t.tap("SEL");
    ok(t.top() === "menu" && U.top().title === "CARTRIDGE " + M.hex(BI) && U.top().items.join() === "NEW,CLONE,RENAME,AUTO LEVEL ALL",
       "the cartridge list's menu on a built-in one as shipped: " + U.top().title + ": " + U.top().items.join());
    await t.tap("HOME"); await t.tap("HOME");
    const FD = M.state.carts.find(c => c.type === "FUSION").id;
    U.setInst(FD << 8);
    await t.tap("ENV"); await t.tap("SEL");
    m = U.top(); m.sel = m.items.indexOf("CLONE"); await t.tap("SEL");
    const fclone = M.state.carts.find(c => c.type === "FUSION" && c.id !== FD);
    ok(fclone && fclone.id <= M.USER_LAST && fclone.id === Math.max(...M.state.carts.filter(c => !c.builtin).map(c => c.id)),
       "CLONE: an editable copy, after the last of the user's cartridges (" + (fclone && M.hex(fclone.id)) + ")");
    await t.tap("HOME");
    const FUS = fclone.id << 8;
    U.setInst(FUS);
    await t.tap("ENV");
    ok(t.top() === "list" && U.top().title === "CARTRIDGES", "ENV on a FUSION slot, browsing: the cartridges, as on every type");
    await t.tap("HOME");
    await t.tap("EDIT");
    ok(U.screen.name === "INSTRUMENTS" && !U.isDeep(), "EDIT on a FUSION instrument: its edit page");
    await t.tap("ENV");
    ok(t.top() === "list" && U.top().title.startsWith("PATTERN FOR"), "ENV on its PATTERN: the pattern list");
    const AUf = t.PTFM.audio, on0 = AUf.noteOn, heardRefs = [];
    AUf.noteOn = (id, tt, midi, ref, ...rest) => { heardRefs.push({ id, midi, ref }); return on0(id, tt, midi, ref, ...rest); };
    const listPat = M.state.patterns.filter(Boolean)[U.top().sel], listRow = U.midi(7);
    const listCells = listPat.cells.slice(listRow * 16, listRow * 16 + listPat.tracks).filter(c => c && c.note != null && c.note !== M.OFF).length;
    await t.key(7);
    ok(t.top() === "list" && heardRefs.filter(h => h.id.startsWith("pl")).length === listCells,
       "... the keys play the highlighted pattern as the FUSION would (" + listCells + " cells on that key's row)");
    await t.tap("HOME");
    await t.tap("SEL");
    ok(U.screen.name === "EDITOR" && U.locked() && U.pattern === M.inst(FUS).p.pattern, "SEL on PATTERN: the pattern in the EDITOR, locked, at once");
    heardRefs.length = 0;
    t.keyDown(7); await t.sleep(30); t.frame();
    ok(heardRefs.length > 0 && heardRefs.every(h => h.id.startsWith("k7/")), "REC off, a key plays the FUSION being written");
    t.keyUp(7); await t.sleep(30);
    AUf.noteOn = on0;
    await t.homeKey(1);
    ok(U.screen.name === "EDITOR", "HOME + black key is refused there");
    await t.tap("HOME");
    ok(U.screen.name === "INSTRUMENTS", "HOME tap goes back");
    await t.tap("HOME");
    /* an empty FUSION slot: EDIT offers the last instrument picked, its note on each row */
    U.setInst(0x0100);
    const fc = M.cart(FUS >> 8), fe = fc.slots.findIndex(s => !s);
    U.setInst(FUS | fe);
    await t.tap("EDIT");
    ok(t.top() === "question" && U.top().lines[1].includes("01:00"), "EDIT on an empty FUSION slot: fill it with 01:00?");
    await t.turn("SELECT", 1);
    ok(U.top().lines[1].includes("01:01"), "... SELECT offers the next sound (" + U.top().lines[1] + ")");
    await t.turn("K1", 1);
    const otherCart = U.top().lines[1];
    ok(!otherCart.includes("FILL IT WITH 01:"), "... K1 the next cartridge's (" + otherCart + ")");
    const fillHeard = [], fon0 = t.PTFM.audio.noteOn;
    t.PTFM.audio.noteOn = (id, tt, midi, ref, ...rest) => { fillHeard.push(ref); return fon0(id, tt, midi, ref, ...rest); };
    await t.key(7);
    t.PTFM.audio.noteOn = fon0;
    ok(fillHeard.length && otherCart.includes(M.refText(fillHeard[0])), "... the keys play the one offered");
    await t.turn("K1", -1);
    ok(U.top().lines[1].includes("01:00"), "... K1 back: that cartridge's first, 01:00");
    await t.tap("OCT+");
    const fp = M.pattern(fc.slots[fe] && fc.slots[fe].p.pattern);
    ok(U.screen.name === "EDITOR" && fp && fp.step === 0 && fp.cells[0x3C * 16].note === 60 && fp.cells[0x3C * 16].inst === 0x0100
       && fp.cells[0x7F * 16].note === 0x7F && !fp.cells[0x0B * 16], "OCT+: filled, C-4 on row 3C with 01:00, rows 0C-7F; the EDITOR");
    await t.tap("HOME"); await t.tap("HOME");
    /* no nesting: the FUSION named in its own row (any FUSION may be written anywhere) is silent there */
    fp.cells[0x3C * 16 + 1] = M.newCell(60, FUS | fe, null);
    const nestIds = [], non0 = t.PTFM.audio.noteOn;
    t.PTFM.audio.noteOn = (id, ...rest) => { nestIds.push(id); return non0(id, ...rest); };
    P.noteOn("nest", 0x3C, FUS | fe, 100, -1); P.noteOff("nest");
    t.PTFM.audio.noteOn = non0;
    fp.cells[0x3C * 16 + 1] = null;
    ok(nestIds.join() === "nest/0", "a FUSION's row naming a FUSION (itself): only the other cells sound (" + nestIds.join() + ")");
    /* a sample instrument's RELEASE: R4 as a time, right is longer, as far as the sample's tail */
    const kitRef = M.state.carts.find(c => c.type === "SAMPLER").id << 8;
    await t.homeKey(5);
    U.setInst(kitRef);
    await t.tap("EDIT"); await t.turn("SELECT", 1);
    const relR4 = () => M.inst(kitRef).p.env.rates[3], rel0 = relR4();
    await t.turn("K4", 1);
    ok(relR4() < rel0, "the LOOP row's K4, RELEASE: right lowers R4, a longer fade (" + rel0 + " -> " + relR4() + ")");
    for (let i = 0; i < 12; i++) await t.turn("K4", 8);
    ok(relR4() === 57, "... and stops at the tail's length: the kick's 0.5 s, R4 57's 463 ms (R4 " + relR4() + ")");
    await t.tap("HOME");

    /* ---- names, FX, settings, the round ---- */
    await t.homeKey(1);
    await t.tap("GLO"); await t.tap("EDIT");
    ok(t.top() === "text", "GLO, EDIT on NAME: text entry");
    const first = M.state.songs[U.song].name[0];
    await t.turn("SELECT", 1); await t.tap("EDIT");
    ok(M.state.songs[U.song].name[0] === String.fromCharCode(first.charCodeAt(0) + 1), "SELECT changes the letter");
    await t.tap("GLO");
    await toFx();
    const song = M.state.songs[U.song];
    await t.tap("OCT+");
    ok(song.fx.on.GATE, "OCT+ on FX switches the selected effect on");
    const chain0 = song.fx.chain.join(","), r0_ = song.fx.routing;
    await t.tap("GLO"); await t.turn("K1", 1); await t.tap("GLO");
    ok(song.fx.routing !== r0_ && song.fx.chain.length === 9, "GLO, ROUTING: switched; the chain keeps its 9 items");
    await t.tap("GLO"); await t.turn("K1", 1); await t.tap("GLO");
    ok(song.fx.routing === r0_ && song.fx.chain.join(",") === chain0, "switched back: the same chain as before");
    /* KEYS, EVERYWHERE (specs/ui/screens/fx.md#the-keys-dry): SETTINGS' KEYS, here too */
    await t.tap("GLO");
    ok(U.top().title === "FX PROPERTIES FOR" && U.top().sub === song.name && !U.top().items.some(it => it.name === "FOR"),
       "GLO on FX: FX PROPERTIES FOR, the song's name on the second line, no FOR item");
    const kl = U.top().items[1];
    await t.turn("SELECT", 1); await t.turn("K1", 1);
    ok(kl && kl.name === "KEYS, EVERYWHERE" && U.set("KEYS") === "DRY" && U.toastNow() === "KEYS DRY",
       "GLO, KEYS, EVERYWHERE: K1, the keys DRY (SETTINGS' KEYS)");
    await t.turn("K1", 1); await t.tap("GLO");
    ok(U.set("KEYS") === "THROUGH FX", "... and back, THROUGH FX");
    /* FILTER's envelope (specs/ui/screens/fx.md#filters-envelope): its knobs FILTER's 3rd to 8th, on two pages, their
     * commands 0xB0-0xB5 after RESO in the EDITOR's list, their values as the firmware writes them */
    {
        const C = t.PTFM.cmds, F = song.fx.knobs.FILTER;
        await t.turn("SELECT", 1);                              /* GATE picked: FILTER, after it on LIVE and EDIT */
        await t.turn("K3", 60); await t.turn("K4", 19);
        ok(F.length === 8 && F[2] === 60 && F[3] === 19 && M.fxValue("FILTER", 3, F[3]) === "BEAT",
           "FILTER ENV: page 1's K3 ENV, K4 SOURCE (BEAT)");
        await t.turn("ALGORITHM", 1); await t.turn("K2", 26);
        ok(F[5] === 101 && M.fxValue("FILTER", 5, F[5]) === "1.22 S", "FILTER ENV: page 2's K2 DECAY, 1.22 S");
        ok(M.fxValue("FILTER", 4, 0) === "2.0 MS" && M.fxValue("FILTER", 7, 75) === "296 MS" && M.fxValue("FILTER", 7, 127) === "5.00 S" &&
           M.fxValue("FILTER", 3, 0) === "KEYS" && M.fxValue("FILTER", 3, 3) === "TRACK 02" && M.fxValue("FILTER", 3, 21) === "1/4 BEAT",
           "FILTER ENV: the values as the firmware writes them");
        const codes = C.codes("FX"), at = codes.indexOf(0x95);
        ok(codes.slice(at + 1, at + 7).join() === [0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5].join() && codes[at + 7] === 0x96 &&
           C.FX[0xB0].join() === "FILTER,2" && C.FX[0xB5].join() === "FILTER,7" && C.FX[0x96].join() === "DRIVE,0",
           "FILTER ENV: its commands 0xB0-0xB5, after RESO in the list, DRIVE still at 0x96");
        ok(C.short({ c: 0xB0, v: 0 }) === "FILT ENV" && C.short({ c: 0xB4, v: 0 }) === "FILT SUS" && C.group(0xB5) === "FX",
           "FILTER ENV: the commands' short names, FILT ENV ... FILT SUS");
        t.shot("ui3_fx_filter_env_page2");
        await t.turn("K2", -26); await t.turn("ALGORITHM", -1); await t.turn("K3", -60); await t.turn("K4", -19);
        ok(F[2] === 0 && F[3] === 0 && F[5] === 75, "FILTER ENV: back to ENV 0 %, KEYS, 296 MS");
    }
    await t.tap("FX");
    ok(U.screen.name === "SONGS", "FX tap on FX: back");
    await t.homeKey(20);
    await t.turn("SELECT", 2); await t.turn("K1", 3); await t.tap("LFO");
    ok(U.set("THEME") === "DEFAULT", "LFO resets a setting to its default");
    /* the themes (specs/ui/colours.md): the six light ones (COMMANDER on blue) after the ten on black; each deep shade
     * 30 % of the way from the ground, the fraction dropped (the firmware's integers); text on a solid red white, or
     * black where red is nearer white (the one-colour themes) */
    {
        const Dr = t.PTFM.draw, to565 = ([r, g, b]) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        const was = Dr.name, deep = (c, g) => g + Math.trunc((c - g) * 3 / 10);
        Dr.theme("DAYLIGHT");
        Dr.rect(0, 0, 1, 1, Dr.DEEP + Dr.LGREY);
        const dayDeep = Dr.fb[0] === to565([deep(0x2C, 0xD0), deep(0x2C, 0xD0), deep(0x28, 0xC8)]), dayInk = Dr.ink(Dr.RED);
        Dr.theme("GREEN"); const greenInk = Dr.ink(Dr.RED);
        Dr.theme("DEFAULT"); const defInk = Dr.ink(Dr.RED);
        Dr.theme("DEFAULT");
        Dr.rect(0, 0, 1, 1, Dr.DEEP + Dr.LGREY);
        const defDeep = Dr.fb[0] === to565([180 * 3 / 10 | 0, 180 * 3 / 10 | 0, 172 * 3 / 10 | 0]);
        Dr.theme(was);
        ok(Dr.THEMES.slice(10).join() === "DAYLIGHT,PAPER,MANUAL,SILVER,LCD,COMMANDER" && Dr.THEMES.length === 16,
           "THEMES: DAYLIGHT, PAPER, MANUAL, SILVER, LCD, COMMANDER after the ten on black");
        ok(dayDeep && defDeep, "THEMES: a deep shade 30 % of the way from the ground (DAYLIGHT's text, DEFAULT's as before)");
        ok(defInk === Dr.WHITE && dayInk === Dr.BLACK && greenInk === Dr.BLACK,
           "THEMES: text on red white in DEFAULT; the ground's on DAYLIGHT's mid red and GREEN's bright one");
    }
    await t.tap("HOME"); await t.turn("SELECT", 1); await t.tap("HOME");
    ok(U.screen.name === "STATUS", "after SETTINGS, the last, HOME, SELECT: STATUS, the first");
    await t.homeKey(1);
    await t.tap("OCT+"); await t.tap("OCT+"); await t.sleep(1700); t.frame();
    ok(U.octave === 2 && t.leds[1] === 1 && t.leds[0] === 0, "OCT +2: OCT+ lit full, OCT- dark");

    /* ---- Dxx: the row in hex, as the row numbers (specs/files/commands.md) ---- */
    P.pauseAll();
    const pa = M.state.patterns.indexOf(null), pb = M.state.patterns.indexOf(null, pa + 1);
    M.state.patterns[pa] = M.newPattern(pa, 1, 64); M.state.patterns[pb] = M.newPattern(pb, 1, 64);
    const brk = M.newCell(null, null, null);
    brk.cmds.push({ c: 0x0D, v: 0x20 });
    M.state.patterns[pa].cells[0] = brk;
    const ds = Object.assign(M.newSong(), { name: "BREAK", tempo: 255, ticks: 1, order: [pa, pb] });
    M.state.songs.push(ds);
    ok(C.words(brk.cmds[0]) === "BREAK TO ROW 20", "D20 reads BREAK TO ROW 20");
    P.playSong(M.state.songs.length - 1);
    const seen = [];
    for (let i = 0; i < 12; i++) { await t.sleep(25); const h = P.at(); if (h) seen.push(h); }
    P.pauseAll();
    ok(seen.some(h => h.pos === 1) && seen.every(h => h.pos === 0 ? h.row === 0 : h.row >= 0x20),
       "D20 breaks to row 20 of the next position");
    M.state.songs.pop(); M.state.patterns[pa] = M.state.patterns[pb] = null;

    /* ---- FX LIVE: a punch-in held while the screen changes still ends when its key is let go (it went on
     *      forever, replaying the old sound under the new: "two songs at once") ---- */
    {
        const AUq = t.PTFM.audio, q = [], q0 = AUq.punch;
        AUq.punch = (k, on) => { q.push(k + (on ? " on" : " off")); return q0(k, on); };
        await toFx();
        t.keyDown(U.WHITE[8]); await t.sleep(40);
        U.go("SONGS"); await t.sleep(40);
        t.keyUp(U.WHITE[8]); await t.sleep(40);
        AUq.punch = q0;
        ok(q.join() === "8 on,8 off" && U.punch == null, "FX LIVE: a punch-in held into another screen ends when its key is let go (" + q.join() + ")");
    }
    /* ---- punch-in commands: as if the key were held that many rows (specs/files/commands.md) ---- */
    const pp = M.state.patterns.indexOf(null);
    M.state.patterns[pp] = M.newPattern(pp, 2, 16);
    const pcell = (r, tr, c, v) => { const x = M.newCell(null, null, null); x.cmds.push({ c, v }); M.state.patterns[pp].cells[r * 16 + tr] = x; };
    pcell(0, 1, 0xD1, 4);                                   /* TAPE STOP (key 15) 4 rows, from track 2 */
    pcell(2, 0, 0xC3, 3);                                   /* row 2: TAPE STOP's rows not over yet */
    pcell(8, 0, 0xD1, 8);                                   /* TAPE STOP 8 rows: still on when the song stops */
    M.state.songs.push(Object.assign(M.newSong(), { name: "PUNCH", tempo: 255, ticks: 1, swing: 50, atEnd: "STOP", order: [pp] }));
    const AUp = t.PTFM.audio, pev = [], pAt0 = AUp.punchAt, pNow0 = AUp.punch;
    AUp.punchAt = (k, on, time) => { pev.push({ k, on, time }); return pAt0(k, on, time); };
    AUp.punch = (k, on) => { pev.push({ k, on, now: true }); return pNow0(k, on); };
    P.kept.song[M.state.songs.length - 1] = { pos: 0, row: 0 };   /* not the place an earlier test song kept */
    P.playSong(M.state.songs.length - 1);
    for (let i = 0; i < 40 && P.playing; i++) await t.sleep(25);
    P.pauseAll();
    AUp.punchAt = pAt0; AUp.punch = pNow0;
    const rowT = 2.5 / 255, at = (i, k, on) => pev[i] && pev[i].k === k && pev[i].on === on && !pev[i].now;
    const dt = (i, n) => Math.abs(pev[i].time - pev[0].time - n * rowT) < 1e-6;
    ok(at(0, 14, true) && at(1, 0, true) && dt(1, 2),
       "!04 TAPE STOP on row 0 (track 2), then !03 LOOP 1/4 on row 2: it replaces the first, which posts no end");
    ok(at(2, 0, false) && dt(2, 5), "LOOP 1/4's 3 rows end at row 5");
    ok(at(3, 14, true) && dt(3, 8), "TAPE STOP again at row 8");
    ok(pev.length === 5 && pev[4].now && pev[4].k === 14 && !pev[4].on,
       "the song stops at its end: the punch-in still running (TAPE STOP) ends at once");
    M.state.songs.pop(); M.state.patterns[pp] = null;

    /* ---- a song's commands change only what is not being edited (specs/ui/screens/editor.md) ---- */
    const pc = M.state.patterns.indexOf(null);
    M.state.patterns[pc] = M.newPattern(pc, 1, 16);
    for (let r = 0; r < 16; r++) {
        const c = M.newCell(null, null, null);
        c.cmds.push({ c: 0x90, v: r & 3 }, { c: 0x80, v: 0x40 + r });
        M.state.patterns[pc].cells[r * 16] = c;
    }
    M.state.songs.push(Object.assign(M.newSong(), { name: "COMMANDS", tempo: 255, ticks: 1, order: [pc] }));
    const AU = t.PTFM.audio, sent = { fx: 0, mix: 0 }, fx0 = AU.fxCmd, mix0 = AU.trackMix;
    AU.fxCmd = (...a) => { sent.fx++; return fx0(...a); };
    AU.trackMix = (...a) => { if (a[1] !== "pan") sent.mix++; return mix0(...a); };
    const playFor = async () => { sent.fx = sent.mix = 0; P.playSong(M.state.songs.length - 1); await t.sleep(300); P.pauseAll(); };
    U.song = M.state.songs.length - 1;
    await toFx();
    for (let i = 0; i < 3 && U.screen.sub() !== "LIVE"; i++) await t.tap("SEQ");
    await t.key(1);                                          /* F#3: GATE, the effect the song's commands turn */
    await playFor();
    ok(U.editing("fx", "GATE") && !U.editing("fx", "FILTER") && sent.fx === 0 && sent.mix > 0,
       "on FX's LIVE, GATE under the cursor: its # commands skipped (the hand's), the = commands not");
    await t.key(3);                                          /* G#3: FILTER */
    await playFor();
    ok(U.editing("fx", "FILTER") && sent.fx > 0, "... FILTER picked: GATE's commands play again (only the effect under the cursor)");
    for (let i = 0; i < 3 && U.screen.sub() !== "MIXER"; i++) await t.tap("SEQ");     /* LIVE, EDIT, MIXER */
    await playFor();
    ok(U.editing("mix", 0) && sent.mix === 0 && sent.fx > 0, "on FX's MIXER: its = commands skipped, its # commands not");
    await t.homeKey(1);
    await playFor();
    ok(sent.fx > 0 && sent.mix > 0, "elsewhere: both play");
    AU.fxCmd = fx0; AU.trackMix = mix0;

    /* ---- FX LIVE, the pages, FX from LOOPS (specs/ui/screens/fx.md) ---- */
    {
        const calls = { punch: [], bypass: [], watch: [], notes: 0 };
        const p0 = AU.punch, b0 = AU.fxBypass, w0 = AU.fxWatch, k0 = U.keyOn;
        AU.punch = (k, on) => { calls.punch.push([k, on]); return p0(k, on); };
        AU.fxBypass = on => { calls.bypass.push(on); return b0(on); };
        AU.fxWatch = on => { calls.watch.push(on); return w0(on); };
        U.keyOn = n => { calls.notes++; return k0(n); };
        U.song = 0;
        await toFx();
        for (let i = 0; i < 3 && U.screen.sub() !== "LIVE"; i++) await t.tap("SEQ");
        const fxs = t.PTFM.audio.fxSong().fx, wasOn = fxs.on.CHORUS;
        await t.key(U.BLACK[4]);                                /* D#4: CHORUS */
        ok(U.screen.sub() === "LIVE" && U.screen.footer()[0].startsWith("CHORUS "), "LIVE: a black key picks its effect (D#4 CHORUS), the footer its CPU");
        await t.tap(wasOn ? "OCT-" : "OCT+");
        ok(fxs.on.CHORUS === !wasOn, "LIVE: OCT switches the picked effect");
        /* FX's REVERT: the whole FX setup FX edits as saved, after its question naming whose it is; the song's other
         * changes stay */
        {
            const sg = t.PTFM.audio.fxSong(), i = M.state.songs.indexOf(sg), savedFx = M.savedThing("fx", i), order0 = JSON.stringify(sg.order);
            await t.tap("SEL"); m = U.top();
            ok(t.top() === "menu" && m.items[m.items.length - 1] === "REVERT" && !m.items.includes("UNDO"), "FX's menu: REVERT while the effects are unsaved: " + m.items.join());
            m.sel = m.items.indexOf("REVERT"); await t.tap("SEL");
            ok(t.top() === "question" && U.top().lines.join("/") === "REVERT THE EFFECTS TO SAVED?/FOR " + sg.name && U.top().answers.join() === "REVERT,KEEP",
               "REVERT asks first: REVERT THE EFFECTS TO SAVED? / FOR " + sg.name);
            await t.tap("OCT+");
            ok(!t.top() && JSON.stringify(M.state.songs[i].fx) === JSON.stringify(savedFx) && !M.unsavedThing("fx", i) &&
               JSON.stringify(M.state.songs[i].order) === order0 && U.toastNow() === "REVERTED", "... REVERT: the effects as saved, the song's order untouched (REVERTED)");
        }
        if (t.PTFM.audio.fxSong().fx.on.CHORUS !== wasOn) await t.tap(wasOn ? "OCT+" : "OCT-");
        t.keyDown(U.WHITE[14]); await t.sleep(40); t.frame();
        const ledHeld = t.leds[14 + U.WHITE[14]], ledPicked = t.leds[14 + U.BLACK[4]];
        const groups = [1, 5, 9, 13].map(i => t.leds[14 + U.WHITE[i]]);
        t.keyUp(U.WHITE[14]); await t.sleep(40);
        ok(calls.punch.length === 2 && calls.punch[0].join() === "14,true" && calls.punch[1].join() === "14,false" && calls.notes === 0,
           "LIVE: a white key runs its punch-in while held (key 15, TAPE STOP), and plays no note");
        const firsts = [0, 4, 8, 12].map(i => t.leds[14 + U.WHITE[i]]);
        ok(ledHeld === 1 && firsts.every(v => v > 0 && v < 1) && groups.every(v => v === 0) && ledPicked === 1,
           "LIVE's LEDs: each group of four's first key dim, the rest dark (" + firsts.join(" ") + " / " + groups.join(" ") + "), the punch-in running bright, the picked effect bright");
        /* ARP holds a punch-in on: past its key's release and a change of screen, until its key again or another */
        calls.punch.length = 0;
        t.keyDown(U.WHITE[2]); await t.sleep(30);
        await t.tap("ARP");
        const heldToast = U.toastNow();
        t.keyUp(U.WHITE[2]); await t.sleep(40); t.frame();
        ok(calls.punch.join() === "2,true" && U.punch === 2 && U.punchKept() && !U.arp && heldToast === "LOOP 1/16 HELD ON: ITS KEY STOPS IT",
           "LIVE: ARP with a punch-in's key held holds it on past the key's release, the arpeggiator untouched (" + heldToast + ")");
        ok(t.leds[2] === 1, "a punch-in held on: the FX button full");
        U.go("SONGS"); await t.sleep(40);
        ok(calls.punch.join() === "2,true" && U.punch === 2, "held on: leaving LIVE leaves it running");
        await t.tap("FX"); await t.sleep(200);
        ok(calls.punch.join() === "2,true", "FX tap: only the FX screen, the punch-in still held on");
        await t.key(U.WHITE[2]);
        ok(calls.punch.join() === "2,true,2,false" && U.punch == null && !U.punchKept(), "its key again stops it, its release nothing more");
        /* FX held + a key elsewhere, ARP; then another punch-in replaces it and runs while its key is held */
        U.go("SONGS"); await t.sleep(40);
        calls.punch.length = 0;
        t.down("FX"); await t.sleep(30); t.keyDown(U.WHITE[12]); await t.sleep(30);
        await t.tap("ARP");
        t.keyUp(U.WHITE[12]); await t.sleep(20); t.up("FX"); await t.sleep(200);
        ok(U.screen.name === "SONGS" && U.punch === 12 && U.punchKept() && calls.punch.join() === "12,true",
           "FX held + a white key, ARP: held on after both are let go, the screen kept");
        t.down("FX"); await t.sleep(30); t.keyDown(U.WHITE[6]); await t.sleep(30);
        const replaced = U.punch === 6 && !U.punchKept();
        t.keyUp(U.WHITE[6]); await t.sleep(20); t.up("FX"); await t.sleep(200);
        ok(replaced && U.punch == null && calls.punch.join() === "12,true,6,true,6,false" && U.screen.name === "SONGS",
           "another punch-in replaces the one held on and runs while its key is held");
        await toFx();
        for (let i = 0; i < 3 && U.screen.sub() !== "LIVE"; i++) await t.tap("SEQ");
        t.keyDown(U.BLACK[10]); await t.sleep(40);
        const byOn = calls.bypass.slice();
        t.keyUp(U.BLACK[10]); await t.sleep(40);
        ok(byOn.join() === "true" && calls.bypass.join() === "true,false", "LIVE: F#5 is BYPASS, every effect off while held");
        await t.tap("SEQ");
        ok(U.screen.sub() === "EDIT" && calls.watch[calls.watch.length - 1] === true, "SEQ: LIVE, then EDIT; its activity meters measured only there");
        await t.tap("SEQ");
        ok(U.screen.sub() === "MIXER" && calls.watch[calls.watch.length - 1] === false, "then MIXER: the meters not measured");
        await t.tap("SEQ");
        ok(U.screen.sub() === "LIVE", "and round to LIVE");
        ok(M.fxValue("FILTER", 0, -20) === "LP 3.8K" && M.fxValue("FILTER", 0, 30) === "HP 347" && M.fxValue("FILTER", 0, 0) === "OFF",
           "FILTER's CUT in Hz: LP 3.8K, HP 347, OFF");
        /* FX from LOOPS does not leave LOOPS: its own setup, and FX again goes back */
        await t.homeKey(22);
        await t.tap("FX"); await t.sleep(200);
        ok(U.screen.name === "FX" && U.fxLoops && t.PTFM.audio.fxSong() === M.state.loops, "FX from LOOPS: LOOPS' own FX setup, LOOPS not left");
        await t.tap("FX"); await t.sleep(200);
        ok(U.screen.name === "LOOPS" && !U.fxLoops, "FX again: back to LOOPS");
        AU.punch = p0; AU.fxBypass = b0; AU.fxWatch = w0; U.keyOn = k0;
        await t.homeKey(1);
    }
    M.state.songs.pop(); M.state.patterns[pc] = null; U.song = 0;
    await t.homeKey(5);
    U.setInst(0x0100);
    ok(!U.editing("inst", 0x0100), "INSTRUMENTS, browsing: nothing is being edited");
    await t.tap("EDIT");
    ok(U.editing("inst", 0x0100) && !U.editing("inst", 0x0101), "its edit page: that instrument is being edited, no other");
    await t.tap("HOME");

    /* ---- edit modes entered with EDIT: EDIT keeps the changes, HOME drops them (specs/ui/controls.md) ---- */
    P.pauseAll();
    await t.tap("SAVE"); await t.sleep(1800);
    const insNow = () => JSON.stringify(M.inst(0x0100)), ins0 = insNow();
    const nextNow = () => JSON.stringify(M.inst(0x0101)), next0 = nextNow();
    await t.tap("EDIT"); await t.turn("K1", 3); await t.turn("PRESETS", 1); await t.turn("K1", 3);
    ok(insNow() !== ins0 && nextNow() !== next0 && M.unsaved > 0, "an instrument's edit page: K1 changes it, and the next one PRESETS turns to");
    await t.tap("HOME");
    ok(insNow() === ins0 && nextNow() === next0 && !U.editing("inst", U.inst), "HOME leaves dropping the changes: both instruments as they were");
    ok(M.unsaved === 0, "... and nothing is unsaved again");
    U.setInst(0x0100);
    await t.tap("EDIT"); await t.turn("K1", -2);
    const ins1 = insNow();
    await t.tap("EDIT");
    ok(insNow() === ins1 && insNow() !== ins0 && !U.editing("inst", 0x0100), "EDIT leaves keeping them");
    /* the slot's REVERT: last in its menu while it has something unsaved, after its question; the next slot's not */
    {
        await t.tap("SEL"); m = U.top();
        ok(m.items[m.items.length - 1] === "REVERT" && !m.items.includes("UNDO"), "the slot's menu: REVERT last while the slot is unsaved: " + m.items.join());
        m.sel = m.items.indexOf("REVERT"); await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "REVERT SLOT 01:00 TO SAVED?" && U.top().answers.join() === "REVERT,KEEP",
           "REVERT asks first: REVERT SLOT 01:00 TO SAVED?, REVERT / KEEP");
        await t.tap("OCT+");
        ok(!t.top() && insNow() === ins0 && !M.unsavedThing("slot", [1, 0]) && U.toastNow() === "REVERTED", "... REVERT: the slot as saved, REVERTED");
        await t.tap("SEL"); m = U.top();
        ok(!m.items.includes("REVERT"), "... nothing unsaved in it: no REVERT in its menu");
        await t.tap("HOME");
        /* the whole cartridge's REVERT, from the cartridge list's menu */
        await t.tap("EDIT"); await t.turn("K1", -2); await t.tap("EDIT");
        await t.tap("ENV"); await t.tap("SEL"); m = U.top();
        ok(m.items[m.items.length - 1] === "REVERT", "the cartridge list's menu: REVERT last while the cartridge is unsaved: " + m.items.join());
        m.sel = m.items.indexOf("REVERT"); await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "REVERT CARTRIDGE 01 TO SAVED?", "REVERT asks first: REVERT CARTRIDGE 01 TO SAVED?");
        await t.tap("OCT+");
        ok(insNow() === ins0 && !M.unsavedThing("cart", 1), "... REVERT: the whole cartridge as saved");
        if (t.top()) await t.tap("HOME");
        /* AUTO LEVEL ALL asks first */
        await t.tap("ENV"); await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("AUTO LEVEL ALL"); await t.tap("SEL");
        const nS = M.cart(1).slots.filter(Boolean).length;
        ok(t.top() === "question" && U.top().lines.join("/") === "AUTO LEVEL " + nS + (nS === 1 ? " SOUND?" : " SOUNDS?") + "/EACH ONE'S LEVEL SET AGAIN" &&
           U.top().answers.join() === "LEVEL,KEEP", "a cartridge's AUTO LEVEL ALL asks first: " + U.top().lines.join(" / "));
        await t.tap("OCT-");
        ok(t.top() !== "progress", "... KEEP: nothing measured");
        if (t.top()) await t.tap("HOME");                    /* (the cartridge list) */
        U.setInst(0x0100);
    }
    await t.tap("EDIT"); await t.turn("K1", -2);
    const ins2 = insNow();
    await t.tap("EDIT");
    ok(insNow() === ins2 && insNow() !== ins0, "(changed again, kept, for what follows)");
    await t.homeKey(3);
    U.editor.row = 0x10; U.editor.track = 0;
    const cellNow = () => JSON.stringify(M.pattern(U.pattern).cells[0x10 * 16] || null), cell0 = cellNow();
    await t.tap("EDIT"); await t.key(9);
    ok(cellNow() !== cell0, "the note editor: a key changes the note");
    await t.tap("HOME");
    ok(cellNow() === cell0 && !U.editor.noteEd, "HOME closes it dropping the change");
    /* the note editor's fields: SELECT the line, ALGORITHM the part, K1 a step, K2 a big step, SEL the cells */
    {
        const TB = M.state.carts.find(c => c.type === "TB303").id << 8;
        const np = M.state.patterns.indexOf(null), q = M.state.patterns[np] = M.newPattern(np, 2, 32);
        const x0 = M.newCell(43, TB, 0x60);
        x0.cmds = [{ c: 0xC2, v: 0 }, { c: 0x41, v: 0x60 }, { c: 0xD1, v: 0x08 }];    /* TAPE STOP 8 rows */
        q.cells[0x14 * 16 + 1] = x0;
        const was = { pattern: U.pattern, row: U.editor.row, track: U.editor.track, slot: U.editor.slot, view: U.editor.view, left: U.editor.left };
        U.pattern = np; Object.assign(U.editor, { row: 0x14, track: 1, slot: 3, view: 2, left: 0 });
        const cx = () => M.pattern(np).cells[0x14 * 16 + 1], nf = () => U.editor.nf;
        await t.tap("EDIT");
        ok(U.editor.noteEd && nf().line === 5 && nf().part === 0, "EDIT: the note editor, on the line of K1's slot (CMD 3)");
        await t.turn("ALGORITHM", 1); await t.turn("K1", 1);
        ok(nf().part === 1 && cx().cmds[2].v === 9, "ALGORITHM: its value; K1: a step (TAPE STOP 9 rows)");
        await t.turn("K2", 1);
        ok(cx().cmds[2].v === 25 && U.screen.footer()[0] === "!19 TAPE STOP 25 ROWS", "K2: a big step, 16 (25 rows); the footer in words");
        await t.turn("K3", 5); await t.turn("K4", 5);
        ok(cx().cmds[2].v === 25, "K3, K4: nothing in the note editor");
        await t.turn("ALGORITHM", -1); await t.turn("K2", 1);
        ok(C.group(cx().cmds[2].c) === "PARAM" && cx().cmds[2].c === 0x40, "K2 on what it is: the next group's first (the TB-303's WAVE)");
        await t.turn("K1", -1);
        ok(cx().cmds[2].c === 0xD2 && cx().cmds[2].v === 1, "K1 back: the group before's last (VINYL STOP, 1 row)");
        await t.turn("SELECT", -5); await t.turn("K2", 1);
        ok(nf().line === 0 && cx().note === 55, "SELECT up to NOTE; K2: an octave (G-3)");
        await t.turn("SELECT", 2); await t.turn("K1", 2);
        ok(nf().line === 2 && cx().vol === 0x62, "VOLUME: K1 two clicks, 62");
        await t.turn("K2", 2);
        ok(cx().vol === null, "... K2 past 7F: the instrument's own");
        await t.turn("SELECT", 10);
        ok(nf().line === 6, "SELECT down stops at the first empty command, + COMMAND");
        await t.turn("K1", 1);
        ok(cx().cmds.length === 4 && cx().cmds[3].c === 0x00, "K1 on + COMMAND adds one (ARPEGGIO)");
        await t.tap("SEL");
        ok(nf().cells, "SEL: SELECT and ALGORITHM move between cells");
        await t.turn("SELECT", 1);
        ok(U.editor.row === 0x15 && U.editor.noteEd, "... SELECT: the next row, still in the note editor");
        ok(nf().line === 3, "... an empty cell shows its first line");
        await t.turn("SELECT", -1); await t.tap("SEL");
        ok(!nf().cells && U.editor.row === 0x14 && nf().line === 6, "SEL again: back to the fields, on the line picked before (CMD 4)");
        await t.turn("SELECT", -1);
        await t.tap("EDIT");
        ok(!U.editor.noteEd && cx().cmds.length === 4 && U.editor.slot === 3, "EDIT keeps the changes; the grid's K1 on the line left (CMD 3)");
        M.state.patterns[np] = null;
        Object.assign(U.editor, was); U.pattern = was.pattern;
    }
    await toFx();
    for (let i = 0; i < 3 && U.screen.sub() !== "EDIT"; i++) await t.tap("SEQ");      /* the EDIT page */
    await t.turn("SELECT", -20);
    const gateNow = () => JSON.stringify(M.state.gates) + JSON.stringify(t.PTFM.audio.fxSong().fx.knobs.GATE), gate0 = gateNow();
    await t.tap("EDIT"); await t.key(0); await t.key(2);
    ok(gateNow() !== gate0, "GATE's steps: EDIT, then the white keys change them");
    const gSteps = () => M.state.gates[t.PTFM.audio.fxSong().fx.knobs.GATE[0]].steps, gs0 = gSteps()[0];
    await t.key(0);
    ok(gSteps()[0] === (gs0 ? 0 : 1), "a white key switches its step on and off (" + gs0 + " -> " + gSteps()[0] + ")");
    t.keyDown(4); await t.sleep(30);
    await t.turn("ALGORITHM", 2);
    ok(gSteps()[2] === 3, "a key held, ALGORITHM: its ratchets, X3 two clicks after ON (" + gSteps()[2] + ")");
    await t.turn("ALGORITHM", -5);
    t.keyUp(4); await t.sleep(30);
    ok(gSteps()[2] === 1, "... back to ON, never off; turned, it stays on as the key comes up (" + gSteps()[2] + ")");
    await t.tap("HOME");
    ok(gateNow() === gate0, "HOME drops them: the gate patterns and GATE's knobs as before");

    /* ---- a built-in pattern (specs/ui/saving.md#fws): edited as any, its menus' titles as any; saved, its change is
     * the user area's (no longer brown); BACK TO BUILT-IN drops it. (With no built-in pattern in the firmware:
     * skipped) ---- */
    const rnb = M.patternNums().find(n => n > M.USER_LAST && n !== M.EDITOR_PATTERN);
    if (rnb == null) console.log("(no built-in pattern in the firmware: the built-in pattern checks skipped)");
    else {
        M.save();
        const rn = rnb;
        U.pattern = rn; U.editor.rec = false;
        await t.homeKey(3);
        await t.tap("ENV"); await t.tap("SEL");
        ok(t.top() === "menu" && U.top().title === "PATTERN " + M.hex(rn) && U.top().items.join() === "NEW,CLONE",
           "a built-in pattern's menu in the list: " + U.top().title + ": " + U.top().items.join());
        await t.tap("HOME"); await t.tap("HOME");
        await t.tap("SEL");
        ok(U.top().title === "CELL" && U.top().items.includes("CUT") && U.top().items.includes("PASTE"),
           "the cell's menu on it: every item (" + U.top().title + ": " + U.top().items.filter(x => typeof x === "string").join() + ")");
        await t.tap("HOME");
        const rc0 = JSON.stringify(M.pattern(rn).cells);
        await t.tap("REC");
        const recOn = U.editor.rec;
        await t.key(7);
        await t.tap("REC");
        ok(recOn && JSON.stringify(M.pattern(rn).cells) !== rc0 && M.unsaved && M.pattern(rn).rom,
           "REC, a key: a built-in pattern is written as any (unsaved, still the firmware's own until a SAVE)");
        await t.tap("SAVE"); await t.sleep(900);
        ok(!M.unsaved && !M.pattern(rn).rom, "SAVE: the changed pattern is the user area's (no longer brown)");
        await t.tap("ENV"); await t.tap("SEL");
        m = U.top(); m.sel = m.items.indexOf("BACK TO BUILT-IN"); await t.tap("SEL");
        ok(!M.builtinChanged("pattern", M.pattern(rn)) && M.unsaved, "BACK TO BUILT-IN: the firmware's own again (an edit)");
        await t.tap("HOME");
        M.save();
    }
    /* ---- a sampler's CLONE with its samples (specs/ui/screens/instruments.md): saved at once, a bar first ---- */
    {
        M.save();
        const sc = M.state.carts.find(c => c.type === "SAMPLER" && !c.rom && (c.samples || []).length);
        const carts0 = M.state.carts.length;
        U.setInst(sc.id << 8);
        await t.homeKey(5);
        await t.tap("ENV"); await t.tap("SEL");
        m = U.top(); m.sel = m.items.indexOf("CLONE"); await t.tap("SEL");
        ok(t.top() === "progress" && U.top().text === "CLONING ITS SAMPLES", "a sampler's CLONE: " + (U.top() && U.top().text));
        await t.sleep(2200);
        const cl = M.state.carts.find(c => c.id !== sc.id && c.name === (sc.name.slice(0, 9) + " 2").trim()) || { samples: [] };
        ok(M.state.carts.length === carts0 + 1 && cl.samples.length === sc.samples.length && !M.unsaved &&
           U.toastNow() === M.hex(sc.id) + " CLONED TO " + M.hex(cl.id), "... its samples copied, saved at once: " + U.toastNow());
        await t.tap("HOME");
        M.commit(st => { st.carts.splice(st.carts.findIndex(c => c.id === cl.id), 1); });
    }
    const pt = M.state.patterns.indexOf(null);
    M.state.patterns[pt] = M.newPattern(pt, 1, 4);
    const fcell = M.newCell(null, null, null);
    fcell.cmds.push({ c: 0x0F, v: 0x40 });
    M.state.patterns[pt].cells[0] = fcell;
    M.state.songs.push(Object.assign(M.newSong(), { name: "TEMPO", tempo: 200, ticks: 1, order: [pt] }));
    U.song = M.state.songs.length - 1;
    await t.homeKey(1);
    P.playSong(U.song); await t.sleep(300);
    ok(Math.round(P.tempo()) === 384, "F40 sets the tempo as the song plays (64 at 1 tick, 4 rows a beat: 384)");
    await t.tap("GLO"); await t.sleep(300);
    ok(U.editing("time", U.song) && Math.round(P.tempo()) === 1200, "the song's properties open: it plays at its own TEMPO, F skipped");
    await t.tap("HOME"); P.pauseAll();
    M.state.songs.pop(); M.state.patterns[pt] = null; U.song = 0;

    /* ---- E3x glissando, an extension to IBXM: a slide to the note heard in semitones ---- */
    const pg = M.state.patterns.indexOf(null), gp = M.newPattern(pg, 1, 8);
    gp.cells[0] = M.newCell(60, 0x0100, null);
    gp.cells[16] = M.newCell(64, null, null);
    gp.cells[16].cmds.push({ c: 0x03, v: 0x04 }, { c: 0x0E, v: 0x31 });
    for (let r = 2; r < 8; r++) { gp.cells[r * 16] = M.newCell(null, null, null); gp.cells[r * 16].cmds.push({ c: 0x03, v: 0 }); }
    M.state.patterns[pg] = gp;
    M.state.songs.push(Object.assign(M.newSong(), { name: "GLISS", tempo: 255, ticks: 6, order: [pg] }));
    const bends = [], ctl0 = AU.ctl;
    AU.ctl = (id, time, bend, vol) => { bends.push(bend); return ctl0(id, time, bend, vol); };
    delete P.kept.song[M.state.songs.length - 1];          /* from its start, not where a song before it stopped */
    P.playSong(M.state.songs.length - 1); await t.sleep(400); P.pauseAll();
    AU.ctl = ctl0;
    const steps = [...new Set(bends.map(b => Math.round(b * 1000) / 1000))];
    ok(bends.length > 3 && bends.every(b => Math.abs(b - Math.round(b)) < 1e-6) && steps.includes(4) && steps.length >= 4,
       "E31 + 304 from C-4 to E-4: whole semitones only, in steps (" + steps.join(" ") + ")");
    M.state.songs.pop(); M.state.patterns[pg] = null;

    /* ---- LEG (commands.md#legato): onto the held note its voice goes on (the worklet told it); after === a plain note ---- */
    {
        const pl = M.state.patterns.indexOf(null), lp = M.newPattern(pl, 1, 10);
        lp.cells[0] = M.newCell(60, 0xF000, null);
        lp.cells[2 * 16] = M.newCell(64, null, null);
        lp.cells[2 * 16].cmds.push({ c: 0xD3, v: 0 });
        lp.cells[4 * 16] = M.newCell(M.OFF, null, null);
        lp.cells[6 * 16] = M.newCell(67, null, null);
        lp.cells[6 * 16].cmds.push({ c: 0xD3, v: 0 });
        M.state.patterns[pl] = lp;
        M.state.songs.push(Object.assign(M.newSong(), { name: "LEGATO", tempo: 255, ticks: 6, order: [pl] }));
        const ons = [], on0 = AU.noteOn;
        AU.noteOn = (id, time, midi, ref, vol, track, opts) => { ons.push({ id, midi, leg: opts.leg }); return on0(id, time, midi, ref, vol, track, opts); };
        delete P.kept.song[M.state.songs.length - 1];
        P.playSong(M.state.songs.length - 1); await t.sleep(500); P.pauseAll();
        AU.noteOn = on0;
        ok(ons.length === 3 && ons[1].midi === 64 && ons[1].leg === ons[0].id && ons[2].midi === 67 && !ons[2].leg,
           "LEG: E-4 onto the held C-4's voice; G-4 after === a plain note (" + ons.map(x => x.midi + (x.leg ? "L" : "")).join(" ") + ")");
        M.state.songs.pop(); M.state.patterns[pl] = null;
    }

    /* ---- VISUAL's views, the pattern list's preview, NEW PATTERN, PLAY held, ARP's turns, K1 on PLAYBACK,
     * the EDITOR's PLAY and footer, the edit page's pages and NAME ---- */
    P.pauseAll(); U.song = 0;
    await t.homeKey(15);
    const view0 = U.screen.sub();
    await t.turn("SELECT", 1);
    ok(U.screen.sub() !== view0 && U.screen.sub() === "SCOPE", "VISUAL: SELECT turns the views, the header naming each (VOICES, SCOPE ...)");
    await t.homeKey(1);
    P.playSong(0); await t.sleep(150);
    await t.tap("EDIT"); await t.tap("ENV");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(150);
    const heard = P.auditioning();
    ok(t.top() === "list" && heard >= 0 && P.what === "loop", "the pattern list: PLAY plays the highlighted pattern, the song pauses");
    await t.turn("SELECT", 1); await t.sleep(50);
    ok(P.auditioning() >= 0 && P.auditioning() !== heard, "... and follows the highlight");
    await t.tap("HOME");
    ok(!t.top() && !P.playing && P.what === "song", "leaving the list ends it; the song is again what PLAY resumes");
    const ord = () => M.state.songs[0].order, ordLen = ord().length, newN = M.freePattern();
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("NEW PATTERN"); await t.tap("SEL");
    ok(ord().length === ordLen + 1 && ord().includes(newN) && M.pattern(newN).cells.every(c => !c), "the order's SEL, NEW PATTERN: an empty pattern, put after the position");
    await t.tap("HOME");
    ok(ord().length === ordLen && !M.pattern(newN), "HOME drops it with the order's other changes");
    P.playSong(0); await t.sleep(400);
    t.down("PLAY"); await t.sleep(700); t.up("PLAY"); await t.sleep(50);
    ok(!P.playing && P.kept.song[0].pos === 0 && P.kept.song[0].row === 0, "PLAY held: stops, back to the start");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(150);
    const from0 = P.at();
    ok(P.playing && from0 && from0.pos === 0 && from0.row <= 2, "PLAY then plays from the beginning");
    P.pauseAll();
    await t.homeKey(8);
    P.kept.lastSong = 0; P.kept.song[0] = { pos: 0, row: 5 };
    await t.turn("K1", 2);
    ok(P.kept.song[0].pos === 2 && P.kept.song[0].row === 0, "PLAYBACK, paused: K1 picks another position, from its first row");
    P.playSong(0); await t.sleep(300);
    await t.turn("K1", 1); await t.sleep(500);
    ok(P.at() && P.at().pos === 3, "playing: K1 jumps to the next position as the row ends");
    await t.tap("ENV");
    ok(t.top() === "list" && U.top().sel === 0, "PLAYBACK: ENV opens the song list, on the song playing");
    await t.turn("SELECT", 2); await t.tap("ENV");
    ok(!t.top() && P.playingSong() === 0, "... ENV again only closes it");
    await t.tap("ENV"); await t.turn("SELECT", 2); await t.tap("PLAY"); await t.sleep(200);
    ok(!t.top() && U.screen.name === "PLAYBACK" && U.song === 2 && P.playingSong() === 2 && P.at() && P.at().pos === 0,
       "... PLAY there plays song 02 from its start, PLAYBACK following it");
    {   /* K4 the playback speed, K3 the songs' volume (specs/ui/screens/playback.md): turned in BPM and dB, kept
         * through other songs, heard at once; LFO resets the one turned last; back when something else plays */
        const sg = M.state.songs[2], own = Math.floor((Math.floor(sg.tempo * 2400 / (sg.ticks * sg.rpb)) + 5) / 10), b0 = P.tempo();
        await t.turn("K4", -10);
        const share = Math.floor((own - 100) * 65536 / own) / 65536;
        ok(Math.abs(P.speed() - share) < 1e-9 && Math.abs(P.tempo() - b0 * share) < .01,
           "PLAYBACK: K4 10 BPM slower, heard at once (" + b0.toFixed(1) + " -> " + P.tempo().toFixed(1) + " BPM)");
        ok(/^K4 TEMPO \d+\.\d BPM -\d+\.\d%$/.test(U.knobNow() || ""), "... the header in BPM and %, as DJ's: " + U.knobNow());
        await t.turn("K3", -6);
        ok(P.volume() === -6 && U.knobNow() === "K3 VOLUME -6 DB", "... K3: the songs' volume, a dB a click (" + U.knobNow() + ")");
        await t.tap("LFO");
        ok(P.volume() === 0 && Math.abs(P.speed() - share) < 1e-9 && U.knobNow() === "K3 VOLUME 0 DB", "... LFO resets the last turned: the volume");
        await t.turn("K4", 1); await t.turn("K4", -1);       /* (K4 the last turned now) */
        await t.tap("LFO");
        ok(P.speed() === 1 && /^K4 TEMPO .* \+0\.0%$/.test(U.knobNow() || ""), "... K4 then: LFO back to 100 % (" + U.knobNow() + ")");
        await t.turn("K4", 3); await t.turn("K3", -3);
        const s3 = P.speed();
        P.playSong(0); await t.sleep(100);
        ok(P.speed() === s3 && s3 > 1 && P.volume() === -3, "... another song keeps the speed and the volume");
        P.playLoop(M.state.songs[0].order[0], 0); await t.sleep(100);
        ok(P.speed() === 1 && P.volume() === 0, "... something else playing (a pattern): 100 %, 0 dB");
    }
    P.pauseAll();
    await t.homeKey(3);
    U.editor.row = 0x10;
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(150);
    const fromRow = P.at();
    ok(P.what === "loop" && fromRow && fromRow.row >= 0x10 && fromRow.row <= 0x13, "the EDITOR: PLAY loops the pattern from the cursor's row");
    P.pauseAll();
    const footPat = M.pattern(U.pattern), fcell0 = footPat.cells[0x20 * 16];
    footPat.cells[0x20 * 16] = Object.assign(M.newCell(60, null, null), { cmds: [{ c: 0x0A, v: 0x04 }] });
    U.editor.row = 0x20; U.editor.track = 0; U.editor.slot = 1;
    const foot = U.screen.footer();
    ok(foot && foot[0].startsWith(t.PTFM.cmds.text({ c: 0x0A, v: 0x04 }) + " "), "the EDITOR's footer: the command at the cursor, in words (" + (foot && foot[0]) + ")");
    footPat.cells[0x20 * 16] = fcell0;
    await t.homeKey(5); U.setInst(0x0100);
    await t.tap("ARP");
    t.keyDown(4); await t.sleep(30); t.keyDown(0); await t.sleep(30); t.keyDown(7); await t.sleep(30);
    t.keyUp(4); t.keyUp(0); t.keyUp(7); await t.sleep(30);
    ok(P.repeatNotes().join() === [U.midi(4), U.midi(0), U.midi(7)].join(), "ARP's repeat: keys pressed together take turns, as pressed");
    t.keyDown(2); await t.sleep(30); t.keyUp(2); await t.sleep(30);
    ok(P.repeatNotes().join() === String(U.midi(2)), "... a key pressed with none held starts anew");
    await t.turn("SELECT", 1);
    ok(P.repeatInst() === U.inst && U.inst !== 0x0100, "... SELECT to the next slot: the repeat plays it (as PRESETS does)");
    await t.turn("ALGORITHM", 1);
    ok(P.repeatInst() === U.inst, "... ALGORITHM too");
    /* the arpeggiator's rules (specs/ui/controls.md, ARP): the pattern, the latch, ARP held + the knobs */
    ok(P.arpNote([64, 60, 67], "UP", 0) === 60 && P.arpNote([64, 60, 67], "DOWN", 0) === 67 && P.arpNote([64, 60, 67], "AS PLAYED", 0) === 64 &&
       [0, 1, 2, 3].map(k => P.arpNote([60, 64, 67], "UP-DOWN", k)).join() === "60,64,67,64", "ARP's patterns: UP, DOWN, AS PLAYED, UP-DOWN");
    for (const k of [0, 2, 4, 5, 7]) { t.keyDown(k); await t.sleep(20); }
    for (const k of [0, 2, 4, 5, 7]) t.keyUp(k);
    await t.sleep(30);
    ok(P.repeatNotes().join() === [2, 4, 5, 7].map(k => U.midi(k)).join(), "LATCH 4 NOTES: 5 keys together, the oldest goes; let go, they stay");
    t.down("ARP"); await t.sleep(30); await t.turn("ALGORITHM", 1); await t.turn("K1", -4); t.up("ARP"); await t.sleep(150);
    ok(U.set("ARP PATTERN") === "DOWN" && U.set("ARP LATCH") === "OFF" && !P.repeatNotes().length && U.arp,
       "ARP held + ALGORITHM: the pattern (DOWN); + K1: the latch (OFF: the latched notes go, ARP stays on)");
    t.keyDown(4); await t.sleep(30);
    const offHeld = P.repeatNotes().join();
    t.keyUp(4); await t.sleep(30);
    ok(offHeld === String(U.midi(4)) && !P.repeatNotes().length, "LATCH OFF: a note plays while its key is held, and stops with it");
    M.state.settings["ARP PATTERN"] = "UP"; M.state.settings["ARP LATCH"] = "4 NOTES";
    await t.tap("ARP");
    ok(!U.arp && !P.repeating(), "ARP again (nothing latched): off");
    for (const [key, name] of [[1, "SONGS"], [8, "PLAYBACK"], [15, "VISUAL"]]) {
        await t.homeKey(key);
        await t.tap("ARP");
        const waits = !P.repeatNotes().length;
        t.keyDown(9); await t.sleep(30); t.keyUp(9); await t.sleep(30); t.frame();
        ok(U.screen.name === name && U.arp && waits && P.repeatNotes().join() === String(U.midi(9)) && t.leds[10] === 1,
           "ARP on " + name + ": the arpeggiator (ARP lit) waits for a key, never the last note by itself; a key feeds it");
    }
    await t.homeKey(5);
    ok(!U.arp && !P.repeating(), "... a change of screen switches it off");
    await t.homeKey(17);
    const st0 = U.screen.sub();
    await t.turn("ALGORITHM", 1);
    const st1 = U.screen.sub();
    await t.turn("ALGORITHM", -1);
    ok(st0 === "PT/FM" && st1 === "PERFORMANCE" && U.screen.sub() === "PT/FM", "STATUS: ALGORITHM turns the pages too, as SELECT does");
    /* EDIT on STATUS (specs/ui/screens/status.md#controls): SETTINGS from every page, as HOME + MONO: a plain switch */
    t.frame();
    ok(t.leds[6] === .25, "STATUS: EDIT lit dim (it works here)");
    let toSettings = true;
    for (let p = 0; p < 6; p++) {
        await t.homeKey(17);
        await t.turn("SELECT", -6); await t.turn("SELECT", p);
        await t.tap("EDIT");
        if (U.screen.name !== "SETTINGS" || U.isDeep()) toSettings = false;
    }
    ok(toSettings, "STATUS: EDIT on each of its 6 pages: SETTINGS, not deep");
    await t.tap("HOME");
    ok(U.screen.name === "SETTINGS", "... HOME there does not come back to STATUS");
    await t.tap("HOME");
    await t.homeKey(5);
    /* a DX7's pages: the built-in ones are read-only (ROM), so a copy of one in the user area */
    let dxUser = M.state.carts.find(c => c.type === "DX7" && !c.rom);
    if (!dxUser) {
        dxUser = structuredClone(M.state.carts.find(c => c.type === "DX7"));
        dxUser.id = M.freeCart(); delete dxUser.rom; delete dxUser.builtin;
        M.state.carts.push(dxUser); M.state.carts.sort((a, b) => a.id - b.id);
    }
    U.setInst(dxUser.id << 8 | dxUser.slots.findIndex(Boolean));
    await t.tap("EDIT"); await t.turn("ALGORITHM", 8); await t.tap("SEL");
    ok(t.top() === "text", "an instrument's edit page: ALGORITHM turns the pages; SEL on NAME starts typing it");
    await t.tap("HOME"); await t.tap("HOME");

    /* ---- SEL, MOVE: afloat; SELECT shows where, SEL puts it down, HOME takes it back ---- */
    P.pauseAll();
    await t.homeKey(1); U.song = 0;
    const song0 = M.state.songs[0], moveSong = async d => { await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("MOVE"); await t.tap("SEL"); await t.turn("SELECT", d); };
    await moveSong(2);
    ok(!t.top() && M.state.songs[0] === song0, "SONGS, MOVE: the song afloat, nothing moved yet");
    await t.tap("SEL");
    ok(M.state.songs[2] === song0 && U.song === 2, "SEL puts it down two places on");
    await moveSong(-2); await t.tap("HOME");
    ok(M.state.songs[2] === song0 && U.screen.name === "SONGS", "afloat, HOME takes it back");
    await moveSong(-2); await t.tap("SEL");
    ok(M.state.songs[0] === song0, "... and back where it was");
    U.pattern = M.patternNums()[0];                         /* (one of the user's: MOVE is offered) */
    await t.homeKey(3); await t.tap("ENV");
    const plist = U.top(), pn = M.patternNums()[plist.sel];
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("MOVE"); await t.tap("SEL");
    ok(t.top() === "list" && plist.mv && plist.mv.n === pn, "the pattern list, MOVE: the pattern afloat in the list");
    await t.turn("SELECT", 1);
    ok(plist.mv.at > pn && !M.state.patterns[plist.mv.at], "SELECT: to the next free number");
    await t.tap("HOME");
    ok(!plist.mv && t.top() === "list" && M.pattern(pn), "HOME takes it back; the list stays");
    {   /* NEW with the edit memory full: not made, no MADE toast; the question asks */
        const max = M.EDITS_MAX, free = M.freePattern();
        M.EDITS_MAX = M.editsUsed;
        await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("NEW"); await t.tap("SEL");
        ok(!M.state.patterns[free] && !/MADE/.test(U.toastNow() || "") && t.top() === "question" && U.top().full,
           "the pattern list, NEW with the edit memory full: nothing made, no toast, the question");
        await t.tap("OCT-");
        M.EDITS_MAX = max;
    }
    await t.tap("HOME");
    await t.homeKey(5); await t.tap("ENV");
    /* (the user's numbers, 00-EF: the built-in cartridges, F0-FA, come after them) */
    const clist = U.top(), lastId = Math.max(...M.state.carts.filter(c => !c.builtin).map(c => c.id));
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("NEW"); await t.tap("SEL");
    ok(U.top().level && U.top().level.group === "NEW" && U.top().items[0] === "BACK" && U.top().items.includes("FUSION"),
       "NEW: a group (the types), BACK first");
    m = U.top(); m.sel = m.items.indexOf("DX7"); await t.tap("SEL");
    const ci = M.cartIndex(lastId + 1);
    ok(ci >= 0 && M.state.carts[ci].type === "DX7" && M.state.carts[ci + 1].builtin,
       "a NEW cartridge: the number after the last of the user's, the end of their part of the list");
    await t.tap("SEL"); m = U.top();
    ok(m.level && m.level.group === "NEW" && m.items[m.sel] === "DX7", "the menu again: at the last action's level, inside NEW, on DX7");
    m.goto("MOVE"); await t.tap("SEL");
    await t.turn("SELECT", 1);
    ok(clist.mv && clist.mv.at === lastId + 2 && M.cart(lastId + 1), "the cartridge list, MOVE: afloat, SELECT to the next free number");
    await t.tap("SEL");
    ok(M.cart(lastId + 2) && !M.cart(lastId + 1), "SEL puts it down there");
    M.state.carts = M.state.carts.filter(c => c.id !== lastId + 2);
    await t.tap("HOME");

    /* ---- what writing the specs found missing ---- */
    await t.homeKey(3); await t.tap("ENV");
    await t.tap("SEL"); m = U.top(); m.sel = m.items.indexOf("MOVE"); await t.tap("SEL"); t.frame();
    ok(t.leds[3] === 1, "an item afloat in a list: SEL lit");
    await t.tap("HOME"); await t.tap("HOME");
    const kindOf = type => M.state.carts.find(c => c.type === type);
    await t.homeKey(5); U.setInst(kindOf("DX7").id << 8); await t.sleep(30);
    await t.tap("EDIT"); await t.tap("ENV");
    ok(!t.top() && U.screen.name === "INSTRUMENTS", "edit mode: ENV off SAMPLE / PATTERN opens nothing (the instrument stays the edited one)");
    await t.tap("EDIT");
    const sc = kindOf("SAMPLER"), own = sc.slots.findIndex(s => s && sc.slots.some((o, j) => o && j !== sc.slots.indexOf(s) && o.p.sample !== s.p.sample));
    const k = sc.slots.find((s, j) => s && j !== own && s.p.sample !== sc.slots[own].p.sample).p.sample;
    const users = sc.slots.map((s, j) => s && s.p.sample === k ? j : -1).filter(j => j >= 0);
    U.setInst(sc.id << 8 | own); await t.sleep(30);
    await t.tap("EDIT"); await t.tap("ENV");
    ok(t.top() === "list", "edit mode, on SAMPLE: ENV opens the samples");
    U.top().sel = k;
    const scSnap = structuredClone(sc);
    await t.tap("SEL"); await t.tap("SEL");
    ok(t.top() === "question" && U.top().lines.join(" ").includes(users.length + " INSTRUMENT"), "deleting a used sample warns: its instruments go too");
    await t.tap("OCT+");
    ok(sc.samples[k].deleted && users.every(j => !sc.slots[j]) && sc.slots[own], "DELETE: the sample and the instruments that use it; the others stay");
    U.edit(() => { M.state.carts[M.cartIndex(sc.id)] = structuredClone(scSnap); });     /* (put back, for what follows) */
    const sc2 = M.cart(sc.id);
    ok(!sc2.samples[k].deleted && users.every(j => sc2.slots[j]), "(put back: the sample and its instruments)");
    await t.tap("HOME"); await t.tap("EDIT");
    /* LINE GAIN and USB GAIN (specs/ui/screens/settings.md): 0 dB at first, K1 a dB up to +12, LFO back
     * to 0; LINE GAIN reaches the sound, USB GAIN is only kept (the playground has no USB audio) */
    const gainAt = M.SETTINGS.findIndex(s => s.k === "LINE GAIN"), AUg = t.PTFM.audio, g0 = AUg.gain, gains = [];
    AUg.gain = db => { gains.push(db); return g0(db); };
    ok(gainAt === 1 && M.SETTINGS[2].k === "USB GAIN" && U.set("LINE GAIN") === 0 && U.set("USB GAIN") === 0,
       "LINE GAIN and USB GAIN follow OUTPUT, both at 0 DB");
    await t.homeKey(20); await t.turn("SELECT", -M.SETTINGS.length); await t.turn("SELECT", gainAt);
    await t.turn("K1", 3);
    ok(U.set("LINE GAIN") === 3 && gains[gains.length - 1] === 3 && M.SETTINGS[gainAt].fmt(3) === "+3 DB",
       "K1 on LINE GAIN: a dB a click, the sound takes it, shown +3 DB");
    await t.turn("K1", 10);
    ok(U.set("LINE GAIN") === 12, "LINE GAIN stops at +12 DB");
    await t.tap("LFO");
    ok(U.set("LINE GAIN") === 0 && gains[gains.length - 1] === 0, "LFO: LINE GAIN back to 0 DB, and the sound with it");
    await t.turn("SELECT", 1); const gn = gains.length; await t.turn("K1", -5);
    ok(U.set("USB GAIN") === -5 && gains.length === gn && M.SETTINGS[gainAt + 1].fmt(-5) === "-5 DB",
       "K1 on USB GAIN: kept, the analog out's sound untouched");
    await t.tap("LFO");
    AUg.gain = g0;
    /* the MIDI page (specs/connections.md): SEQ or ALGORITHM; its items; LEARN takes an FM-1 control,
     * then waits for the MIDI one (none in the playground), HOME cancels; LFO deletes a mapping */
    await t.tap("SEQ");
    ok(U.screen.sub() === "MIDI" && U.screen.seq() === "2/2", "SETTINGS: SEQ turns to the MIDI page (header SETTINGS MIDI, SEQ 2/2: pages counted from 1)");
    await t.turn("SELECT", -9); await t.turn("SELECT", 1); await t.turn("K1", 1);
    ok(U.set("MIDI OUT") === "ALL", "MIDI OUT: KEYS, K1 to ALL");
    await t.tap("LFO");
    ok(U.set("MIDI OUT") === "KEYS", "... LFO: back to KEYS");
    await t.turn("SELECT", 4);                              /* (MIDI OUT is item 1: MIDI CONTROLLER after the 5 items) */
    ok(U.screen.footer()[0] === "EDIT: SWITCH", "MIDI CONTROLLER after the items: the footer says EDIT: SWITCH");
    await t.turn("SELECT", 1);
    ok(U.screen.footer()[0] === "EDIT: LEARN", "LEARN: the footer says EDIT: LEARN");
    const instL = U.inst, outL = U.set("MIDI OUT");
    await t.tap("EDIT");
    ok(t.top() === "learn", "EDIT on LEARN: the learn box, TOUCH AN FM-1 CONTROL");
    await t.turn("PRESETS", 1); await t.turn("K1", 1);
    ok(t.top() === "learn" && U.top().ctl === 6 && U.inst === instL && U.set("MIDI OUT") === outL,
       "the FM-1 control touched (PRESETS) is taken, not acting; the next does nothing either");
    await t.tap("HOME");
    ok(!t.top(), "HOME cancels it (the playground has no outside MIDI to learn from)");
    M.state.midi.push({ ch: 0, msg: "CC", num: 74, ctl: 2, kind: 1 });
    await t.turn("SELECT", 1);
    ok(U.screen.footer()[0] === "LFO: DELETE", "a mapping picked (K1, CC 74 CH 1): LFO: DELETE");
    await t.tap("LFO");
    ok(!M.state.midi.length, "LFO deletes it");
    await t.turn("ALGORITHM", -1);
    ok(!U.screen.sub(), "ALGORITHM back: the list");
    const toolsAt = M.SETTINGS.findIndex(s => s.k === "TOOLS");
    const runTool = async name => {
        await t.homeKey(20); await t.turn("SELECT", -M.SETTINGS.length); await t.turn("SELECT", toolsAt);
        U.pickTool(name); await t.tap("EDIT");
    };
    M.state.songs[0].order.push(M.freePattern());           /* (a pattern that is not there) */
    await runTool("CHECK REFERENCES"); await t.sleep(900);
    ok(t.top() === "list" && U.top().title === "MISSING REFERENCES: 1", "CHECK REFERENCES lists the missing ones");
    await t.tap("HOME");
    M.state.songs[0].order.pop();
    const spare = 0xDF;                                      /* (a user number nothing uses: F0-FF are the firmware's,
                                                              * E0-E5 the disk's FUSION's) */
    M.state.patterns[spare] = Object.assign(structuredClone(M.state.patterns.find(Boolean)), { n: spare });
    await runTool("CLEAN UP");
    const cq = U.top();
    ok(t.top() === "question" && cq.rows.map(r => r.k).join() === "patterns,samples,instruments" && cq.rows[0].on && !cq.rows[2].on,
       "CLEAN UP asks what goes: a checklist, unused patterns and samples ticked, instruments not");
    await t.turn("SELECT", 2); await t.turn("K1", 1);
    const ticked = cq.rows[2].on;
    await t.turn("K1", -1);
    ok(ticked && !cq.rows[2].on && /^CLEAN UP: FREES /.test(cq.title(cq.rows)), "SELECT picks a line, K1 ticks it and clears it; the title says what it frees");
    const instCount = M.state.carts.reduce((a, c) => a + c.slots.filter(Boolean).length, 0);
    await t.tap("OCT+");
    ok(!M.state.patterns[spare] && M.state.carts.reduce((a, c) => a + c.slots.filter(Boolean).length, 0) === instCount,
       "OCT+: the ticked lines deleted (the unused pattern), the instruments kept");
    await t.tap("HOME");

    /* ---- SONGS' play modes (specs/ui/screens/songs.md#the-play-modes): SEQ turns ONE PLAY, REPEAT SONG, PLAY NEXT;
     * PLAY here puts the mode in hold, on every screen; a looping song loops in every mode, the mode decides the end
     * of a song that stops ---- */
    {
        P.pause();
        await t.homeKey(1);
        const all = M.state.songs, i = 0, s = all[i], was = s.atEnd, last = s.order.length - 1;
        const from = Math.min(s.loopFrom || 0, last);
        const nearEnd = () => { P.kept.lastSong = i; P.kept.song[i] = { pos: last, row: M.pattern(s.order[last]).rows - 4 }; };
        s.atEnd = "LOOP";                                    /* (a looping song first) */
        await t.turn("SELECT", i - U.song);
        P.playMode = 0; t.frame();
        ok(!U.screen.sub() && t.leds[11] === .25, "SONGS: ONE PLAY at first (SEQ dim, nothing after SONGS)");
        nearEnd();
        await t.tap("PLAY");
        ok(P.playing && P.endMode === "once", "... PLAY here: ONE PLAY in hold");
        await t.sleep(1500);
        ok(P.playing && P.playingSong() === i && P.at() && P.at().pos === from, "... ONE PLAY: a looping song loops from its LOOP FROM");
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 1 && U.toastNow() === "REPEAT SONG" && U.screen.sub() === "REPEAT" && t.leds[11] === 1,
           "SEQ: REPEAT SONG (SEQ lit, REPEAT after SONGS)");
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 2 && U.toastNow() === "PLAY NEXT" && U.screen.sub() === "NEXT" && P.endMode === "next",
           "SEQ: PLAY NEXT, at once on the song playing (NEXT after SONGS)");
        P.pause(); nearEnd();
        await t.tap("PLAY");
        await t.sleep(1500);
        ok(P.playing && P.playingSong() === i && P.at() && P.at().pos === from, "... PLAY NEXT: a looping song loops, no next song");
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 0 && U.toastNow() === "ONE PLAY", "SEQ: ONE PLAY");
        s.atEnd = "STOP";                                    /* (now a song that stops) */
        P.pause(); nearEnd();
        await t.tap("PLAY");
        for (let k = 0; k < 40 && P.playing; k++) await t.sleep(100);
        ok(!P.playing, "... ONE PLAY: a song that stops stops at its end");
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 1 && U.toastNow() === "REPEAT SONG", "SEQ: REPEAT SONG");
        nearEnd();
        await t.tap("PLAY");
        await t.sleep(1500);
        ok(P.playing && P.playingSong() === i && P.at() && P.at().pos === 0, "... REPEAT SONG: a song that stops plays again from its start");
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 2 && P.endMode === "next", "SEQ: PLAY NEXT");
        P.pause(); nearEnd();
        await t.tap("PLAY");
        await t.homeKey(8);                                  /* (on PLAYBACK: the mode holds on every screen) */
        let n = i + 1;
        while (!all[n].order.length) n++;
        for (let k = 0; k < 40 && P.playingSong() === i; k++) await t.sleep(100);
        await t.sleep(300);
        const h = P.at();
        ok(P.playing && P.playingSong() === n && U.song === n && h && h.pos === 0,
           "... PLAY NEXT, on PLAYBACK: at its end the next song plays on from its start, the cursor with it");
        await t.homeKey(1);
        await t.tap("SEQ"); t.frame();
        ok(P.playMode === 0 && U.toastNow() === "ONE PLAY" && t.leds[11] === .25 && !U.screen.sub(), "SEQ again: ONE PLAY (SEQ dim)");
        s.atEnd = was;
        /* ALGORITHM: the position PLAY starts from, boxed; while the song plays only the box moves */
        await t.turn("ALGORITHM", 2);
        const boxed = U.screen.boxAt(), playingAt = P.at() ? P.at().pos : -1;
        ok(boxed === 2 && P.playing && playingAt === 0, "SONGS, ALGORITHM while the song plays: the box to position 02, the playing goes on in 00");
        await t.tap("PLAY"); await t.tap("PLAY"); await t.sleep(300);
        ok(P.playing && P.at() && P.at().pos === 2, "... PLAY (pause), PLAY: from the boxed position");
        await t.tap("PLAY");
        const pausedBox = U.screen.boxAt();
        await t.turn("SELECT", 1);
        const otherBox = U.screen.boxAt();
        await t.turn("SELECT", -1);
        ok(pausedBox === 2 && otherBox === 0 && U.screen.boxAt() === 2,
           "paused: the box where PLAY goes on; another song: its first position; back: where this one goes on");
        P.pause();
    }

    /* ---- PRESETS' cartridge list (specs/ui/dialogs.md#presets-cartridge-list): SEL tapped, then PRESETS: the tap's
     * menu gives way to the list, each click a cartridge; any other control closes it and does its own job, HOME only
     * closes it; SEL held + PRESETS opens it too; a control between the tap and PRESETS: PRESETS alone ---- */
    {
        await t.homeKey(1);
        const cs = M.state.carts, keep = U.inst, i0 = Math.max(0, M.cartIndex(keep >> 8)), at = k => cs[(i0 + k + cs.length) % cs.length].id;
        const listOn = () => !!U.top() && !!U.top().presetsList;
        await t.tap("SEL");
        ok(t.top() === "menu", "PRESETS' list: SEL's tap on SONGS, its menu");
        await t.turn("PRESETS", 1); t.frame();
        ok(listOn() && U.overlays.length === 1 && U.inst >> 8 === at(1) && t.leds[U.B.SEL] === 1,
           "PRESETS' list: PRESETS, the menu gives way to the list, the next cartridge picked, SEL lit");
        ok(U.knobNow && /^CARTRIDGE /.test(U.knobNow()), "... the header names it");
        await t.turn("PRESETS", 1);
        ok(listOn() && U.inst >> 8 === at(2) && U.top().sel === M.cartIndex(at(2)), "... the next click, the next one, highlighted");
        t.shot("presets_list");
        const song = U.song;
        await t.turn("SELECT", song ? -1 : 1);
        ok(!U.overlays.length && U.song !== song, "... SELECT closes it and moves the cursor");
        await t.turn("SELECT", song ? 1 : -1);
        await t.tap("SEL");
        await t.turn("PRESETS", -1);
        ok(listOn() && U.inst >> 8 === at(1), "... back a click, SEL tapped again");
        await t.tap("HOME");
        ok(!U.overlays.length, "... HOME only closes it");
        t.down("SEL"); await t.sleep(30);
        await t.turn("PRESETS", -1);
        const heldOn = listOn() && U.inst >> 8 === at(0);
        t.up("SEL"); await t.sleep(300);
        const after = U.inst;
        await t.turn("PRESETS", 1);
        ok(heldOn && !U.overlays.length && U.inst >> 8 === at(0) && U.inst !== after,
           "... SEL held + PRESETS: the list while held, gone with SEL; PRESETS then the instruments");
        await t.tap("SEL");
        await t.turn("PRESETS", 1);
        t.keyDown(12); await t.sleep(30);
        ok(!U.overlays.length && U.keysHeld().includes(12), "... a key closes it and plays");
        t.keyUp(12); await t.sleep(30);
        await t.tap("SEL");
        await t.turn("SELECT", 1);                           /* (in the menu: another control first) */
        const was = U.inst;
        await t.turn("PRESETS", 1);
        ok(t.top() === "menu" && U.inst >> 8 === was >> 8 && U.inst !== was, "... a control between: PRESETS turns the instrument, the menu stays");
        await t.tap("HOME");
        U.setInst(keep);
    }

    /* ---- DJ (specs/ui/screens/dj.md): the decks, LOAD, PLAY, SYNC, HOLD, the knobs and SEL's reset, the pads
     * (PLAY, LOOPS, the LFO pads), SWAP, mutes, the PLAY button, REC, FX on DJ's setup, a song moving onto the
     * main deck ---- */
    {
        const DJ = P.dj, L = () => DJ.sides[0], R = () => DJ.sides[1], song = n => M.state.songs.findIndex(s => s.name === n);
        const pick = async n => { await t.turn("SELECT", n - U.screen.listAt()); };
        const foot = () => U.screen.footHints().map(x => x[0]).join("");
        const barRows = d => { const s = M.state.songs[d.song]; return s.rpb * (((s.fx.knobs || {}).METRONOME || [])[1] || 4); };
        /* the black keys, a job a group; PLAY's pads 1 SET CUE, 2 CUE, 3 JUMP, 4 MOVE, 5 BRAKE; LOOPS' pad 1, PATTERN;
         * the LFO pads by number (1-16, across both decks' keys) */
        const K = { LPLAY: 1, LHOLD: 3, LFILTER: 5, LLOAD: 8, RLOAD: 10, RPLAY: 13, RHOLD: 15, RFILTER: 17, SYNC: 20, PREVIEW: 22, MODE: 25 };
        const SET = U.WHITE[0], LCUE = U.WHITE[1], JUMP = U.WHITE[2], MOVE = U.WHITE[3], BRAKE = U.WHITE[4], LREP = U.WHITE[0];
        const LFO = k => U.WHITE[k - 1];
        /* what DJ's mix last sent to the sound (its LFOs, REC's silence) */
        let sent = null;
        const audioDj = t.PTFM.audio.dj;
        t.PTFM.audio.dj = m => { if (m.on) sent = m; audioDj(m); };
        P.pause();
        await t.homeKey(22); await t.tap("HOME"); await t.turn("SELECT", 1); await t.tap("HOME");
        ok(U.screen.name === "DJ" && U.screen.title() === "DJ", "the round: LOOPS, then DJ");
        await t.turn("SELECT", 0); await t.tap("HOME"); await t.turn("SELECT", 1); await t.tap("HOME");
        ok(U.screen.name === "SETTINGS", "... then SETTINGS");
        await t.homeKey(10);
        ok(U.screen.name === "DJ", "HOME + OP5: DJ");
        t.frame();
        ok(DJ.xf === 0 && DJ.sync && U.screen.padsAt().mode === "PLAY" && t.leds[5] === .25 && foot() === "SET CUE JUMP MOVE|STOPS",
           "at first: the crossfader all the way to the left, SYNC on, the pads PLAY (SET CUE JUMP MOVE|STOPS), LFO dim");
        await t.tap("SEQ");
        ok(U.toastNow() === "NO SONGS TO SWAP", "SEQ with both decks empty: refused, NO SONGS TO SWAP");
        await t.key(K.LPLAY);
        ok(U.toastNow() === "NO SONG ON THE DECK", "OP1 on an empty deck: NO SONG ON THE DECK");
        t.frame();
        ok(t.leds[14 + K.LHOLD] === 0 && t.leds[14 + K.RHOLD] === 0 && t.leds[14 + K.LFILTER] === 0,
           "HOLD and FILTER keys: dark with no song on the decks");
        ok([0, 4].map(i => t.leds[14 + U.WHITE[i]]).every(v => v === .25) && [1, 2, 3, 5].map(i => t.leds[14 + U.WHITE[i]]).every(v => v === 0),
           "the pads: each group of four's first dim, the rest dark, for finding them by touch");
        await pick(song("TRANCE LOOP")); await t.key(K.LLOAD);
        ok(L().song === song("TRANCE LOOP") && U.toastNow() === "LEFT: TRANCE LOOP", "OP4 loads the highlighted song on the left deck: LEFT: TRANCE LOOP");
        await pick(song("BREAKS LOOP")); await t.key(K.RLOAD);
        ok(R().song === song("BREAKS LOOP") && P.djBpm(R()) === P.djBpm(L()) * DJ.mult,
           "OP5 on the right; SYNC on, so at the main's BPM (" + P.djBpm(R()).toFixed(1) + ", its own " + P.djOwnBpm(R()).toFixed(1) + ")");
        await t.key(K.LPLAY); await t.sleep(300);
        ok(L().playing && P.what === "dj" && P.playing, "OP1: the left deck plays; the decks are what plays");
        await t.key(K.LLOAD);
        const waiting = L().loadTo === song("BREAKS LOOP") && U.toastNow() === "LEFT: BREAKS LOOP AT THE BAR";
        await t.key(K.LLOAD);
        ok(waiting && L().loadTo == null && U.toastNow() === "LEFT: LOAD CANCELLED" && L().song === song("TRANCE LOOP"),
           "LOAD on a playing deck: waits for its bar line (LEFT: BREAKS LOOP AT THE BAR); LOAD again cancels it");
        {
            const bpm0 = P.djBpm(L()), atBar = async name => {
                await pick(song(name)); await t.key(K.LLOAD);
                for (let i = 0; i < 40 && L().song !== song(name); i++) await t.sleep(100);
                return L().song === song(name) && L().playing && L().loadTo == null;
            };
            const swapped = await atBar("BREAKS LOOP"), st = P.djState(L());
            ok(swapped && st.at.pos === 0 && Math.abs(P.djBpm(L()) - bpm0) < .05 && L().cue.pos === 0 && L().cue.row === 0,
               "... at the bar line the new song plays on from its start, at the BPM the deck played (" + P.djBpm(L()).toFixed(1) + ")");
            ok(await atBar("TRANCE LOOP"), "... and back again, the same way");
            await pick(song("BREAKS LOOP"));
        }
        await t.key(K.SYNC);
        const syncOff = !DJ.sync && U.toastNow() === "SYNC OFF" && P.djBpm(R()) === P.djBpm(L());
        await t.turn("K4", 1);
        const ownKnob = Math.abs(P.djBpm(R()) - P.djBpm(L()) - .1) < 1e-6;
        await t.key(K.SYNC);
        ok(syncOff && ownKnob && DJ.sync && P.djBpm(R()) === P.djBpm(L()) && U.toastNow() === "SYNC ON",
           "MONO: SYNC OFF, the right keeping the BPM it played at, K4 its own; again: SYNC ON, the right at the main's BPM");
        await t.turn("K4", 1);
        ok(U.toastNow() === "SYNC: TEMPO ON THE MAIN DECK", "K4 while synced: SYNC: TEMPO ON THE MAIN DECK");
        const inst0 = U.inst;
        await t.key(K.RPLAY); await t.sleep(2200);
        /* HOLD (PIT): a hand on the right deck: it stops while held, K4 moves it by hand; let go, it plays on from there */
        const at0 = P.djState(R()).at;
        t.keyDown(K.RHOLD); await t.sleep(30); t.frame();
        const held = !R().playing && !!R().scratch && t.leds[14 + K.RHOLD] === 1 && foot() === "K4 ROWS HEARD  K3 BEATS";
        await t.turn("K4", 2);
        const moved = R().scratch && (R().scratch.at.row !== at0.row || R().scratch.at.pos !== at0.pos);
        t.keyUp(K.RHOLD); await t.sleep(60);
        ok(held && moved && R().playing && !R().scratch, "PIT held (lit): the right deck stops, K4 moves it by hand; let go, it plays on from there");
        await t.turn("PRESETS", 1);
        ok(U.inst === inst0, "PRESETS: nothing on DJ, the instrument stays");
        await t.key(K.SYNC);
        await t.key(K.RPLAY); await t.key(K.SYNC);
        await t.key(K.RPLAY);
        ok(R().playing && R().r.t > P.time(), "OP6: the synced right deck waits for the main's bar line");
        await t.key(K.RPLAY);
        ok(!R().playing, "... a second tap cancels the start");
        await t.key(K.RPLAY); await t.sleep(2200);
        ok(R().playing && !R().startAt, "... and starts on the bar line");
        await t.turn("K1", 5);
        ok(Math.abs(P.djBpm(L()) - P.djOwnBpm(L()) - .5) < 1e-6 && P.djBpm(R()) === P.djBpm(L()), "K1: the main's tempo by 0.1 BPM; the synced right follows");
        await t.tap("SEL");
        ok(P.djBpm(L()) === P.djOwnBpm(L()) && /: THE SONG'S$/.test(U.toastNow()), "SEL tapped after it: the song's own tempo");
        t.frame();
        ok(L().filterOn && L().filter === 0 && L().filterSet === 0 && t.leds[14 + K.LFILTER] === 1,
           "the left deck's DJ FILTER on at OFF at first (OP3 lit)");
        await t.turn("K2", -10);
        ok(L().filterSet === -10 && L().filter === -10, "K2: the left deck's DJ FILTER heard as it turns, at once");
        await t.key(K.LFILTER); t.frame();
        ok(L().filter === 0 && !L().filterOn && L().filterSet === -10 && U.toastNow() === "FILTER L OFF" &&
           t.leds[14 + K.LFILTER] === .25, "OP3 tapped: off at once, its setting kept, unheard (OP3 dim)");
        t.keyDown(K.LFILTER); await t.sleep(200);
        const mid = L().filter, sliding = !!L().slide && L().filterOn && /^FILTER L ON /.test(U.toastNow());
        t.keyUp(K.LFILTER); await t.sleep(30); t.frame();
        ok(sliding && mid < 0 && mid > -10 && L().filter === -10 && !L().slide && t.leds[14 + K.LFILTER] === 1,
           "OP3 held: on, sliding to the setting while held (" + mid + "); let go, it is there (OP3 lit)");
        await t.turn("K2", 2);
        ok(L().filter === -8 && L().filterSet === -8, "K2 while on: heard as it turns");
        await t.key(K.LFILTER);
        ok(L().filter === 0 && !L().filterOn && L().filterSet === -8 && U.toastNow() === "FILTER L OFF", "OP3 tapped: off at once, its setting kept");
        /* ENV: what K2 / K3 turn, a cycle: the filters, the decks' VOLUMEs */
        {
            await t.tap("ENV"); t.frame();
            const envOn = U.toastNow() === "K2 K3: VOLUME" && t.leds[4] === 1;
            await t.turn("K2", -3);
            const vol = L().vol === -3 && L().filterSet === -8 && sent.vol[L().i] > .7 && sent.vol[L().i] < .71;
            await t.turn("K3", 2);
            const rVol = R().vol === 2;
            await t.tap("SEL");
            const back = R().vol === 0;
            await t.tap("ENV"); t.frame();
            ok(envOn && vol && rVol && back && U.toastNow() === "K2 K3: FILTER" && t.leds[4] === .25,
               "ENV: K2 K3: VOLUME (ENV lit): K2 the left's VOLUME (-3 dB, sent as its gain), K3 the right's; SEL: 0 dB; ENV again: K2 K3: FILTER");
            P.djVol(L(), 0);
        }
        await t.turn("ALGORITHM", 3);
        ok(DJ.xf === 3, "ALGORITHM: the crossfader");
        await t.tap("SEL");
        ok(DJ.xf === 16, "SEL after it: the crossfader to the middle");
        /* the pads: the blank key turns their mode; a LOOP tapped on and off; a ROLL held lands where the deck would be */
        await t.key(K.MODE);
        ok(U.screen.padsAt().mode === "LOOPS" && U.toastNow() === "PADS: LOOPS", "the blank key: the pads' mode, PLAY -> LOOPS");
        await t.key(U.WHITE[3]);
        ok(L().loop && !L().loop.roll && L().loop.beats === 4 && L().loop.row % barRows(L()) === 0,
           "LOOPS pad 4 tapped: REPEAT 1 BAR, the bar the deck is in (on the bar grid: row " + M.hex(L().loop ? L().loop.row : 0) + ")");
        t.keyDown(K.LHOLD); await t.sleep(80); t.keyUp(K.LHOLD); await t.sleep(80);
        ok(L().loop && !L().loop.roll && L().loop.beats === 4 && L().playing, "OP2 (HOLD) during a LOOP: let go, the deck plays on in the loop");
        await t.key(U.WHITE[3]);
        ok(!L().loop, "... tapped again: off");
        t.keyDown(U.WHITE[6]); await t.sleep(700);
        const rl = L().loop ? Object.assign({}, L().loop) : null;
        ok(rl && rl.roll && !L().ghost, "LOOPS pad 7 held: a ROLL 1/2 BAR, no place counted on");
        t.keyUp(U.WHITE[6]); await t.sleep(80);
        const pl = P.djHeard(L()) || { pos: -1, row: -1 };   /* (the row heard: a playing deck's place) */
        ok(!L().loop && L().playing && rl && pl.pos === rl.pos && pl.row >= rl.row && pl.row <= rl.row + rl.len,
           "... let go: on from where the roll is (row " + M.hex(pl.row) + " in the loop " + M.hex(rl ? rl.row : 0) + "+" + (rl ? rl.len : 0) + ")");
        await t.key(K.MODE);
        ok(U.screen.padsAt().mode === "PLAY" && U.toastNow() === "PADS: PLAY", "the blank key again: LOOPS -> PLAY");
        {   /* LFO held: its layer the LFO pads by key; let go, the pads are the LFO's; held again there: they stay */
            t.down("LFO"); await t.sleep(700);
            const l = U.layers.length ? U.layers[U.layers.length - 1].lines() : [];
            const map = l.length === 7 && Array.isArray(l[1]) && l[1].map(r => r[0]).join("") === "1 FADER|2 SHAPE|3 RANGE|4 REVERSE";
            t.up("LFO"); await t.sleep(160);
            const into = U.screen.padsAt().mode === "LFO";
            t.down("LFO"); await t.sleep(700); t.up("LFO"); await t.sleep(160);
            ok(map && into && U.screen.padsAt().mode === "LFO",
               "LFO held: its layer lists the LFO pads by key; let go, the pads are the LFO's; held again there: they stay");
            await t.tap("LFO");
            ok(U.screen.padsAt().mode === "PLAY", "... LFO tapped: back to the PLAY pads");
        }
        /* the LFO pads (specs/ui/screens/dj.md#the-lfo-pads): LFO brings them in; the crossfader's LFO sets off on the
         * main's next row, at a note length of its tempo (1/2 at first) */
        await t.tap("LFO");
        t.frame();
        ok(U.screen.padsAt().mode === "LFO" && U.toastNow() === "PADS: LFO" && t.leds[5] === 1 && foot() === "1 FADER 5 FILTER 13 REVERB",
           "LFO: the LFO pads (LFO lit full); the footer 1 FADER 5 FILTER 13 REVERB");
        {
            const F = DJ.lfo.FADER, rowS = 60 / (P.djBpm(L()) * M.state.songs[L().song].rpb), t0 = P.time();
            await t.key(LFO(1));
            const fm = sent.lfo.fader;
            ok(F.on && U.toastNow() === "FADER LFO: TRIANGLE 1/2" && F.at >= t0 && F.at <= t0 + rowS + .02,
               "pad 1: the crossfader's LFO on (FADER LFO: TRIANGLE 1/2), setting off on the main's next row");
            ok(fm && fm.shape === 2 && F.range === 3 && fm.from === 16 && fm.to === 16 && Math.abs(fm.hz - P.djBpm(L()) / 120) < 1e-9,
               "... sent to the sound: TRIANGLE, MIRROR at first (in the middle: 16 to 16), a swing every 2 beats (" + (fm ? fm.hz.toFixed(3) : "-") + " Hz)");
            await t.sleep(150); t.frame();
            ok(t.leds[14 + LFO(1)] === 1, "... its pad lit full while it runs");
            t.keyDown(LFO(1)); await t.sleep(30);
            await t.turn("ALGORITHM", -2);
            const slow = P.djLfoSpeed(F) === "2 BARS" && U.knobNow() === "FADER LFO 2 BARS" && DJ.xf === 16 && foot() === "ALG SPEED  LET GO STAYS ON";
            t.keyUp(LFO(1)); await t.sleep(40);
            ok(slow && F.on && Math.abs(sent.lfo.fader.hz * 4 - fm.hz) < 1e-9,
               "pad 1 held: ALGORITHM its speed (2 BARS: ALG SPEED  LET GO STAYS ON), not the crossfader; let go after a turn, it runs on");
            t.keyDown(LFO(1)); await t.sleep(30);
            await t.turn("ALGORITHM", 12);
            const fastest = P.djLfoSpeed(F);
            t.keyUp(LFO(1)); await t.sleep(40);
            await t.tap("SEL");
            ok(fastest === "1/8" && P.djLfoSpeed(F) === "1/2" && F.on, "... clockwise faster, 1/8 the last, no 1/16 (" + fastest + " at " + P.djBpm(L()).toFixed(0) + " BPM); SEL after: back to 1/2");
            const bpm0 = L().bpm;
            L().bpm = 320; P.djLfoTurn(F, 20);
            const capped = P.djLfoSpeed(F);
            L().bpm = bpm0; P.djLfoReset(F);
            ok(capped === "1/4", "... never over 10 Hz: at 320 BPM 1/4 the fastest (" + capped + ")");
            await t.key(LFO(2));
            const shaped = U.toastNow() === "FADER: SAW UP" && F.shape === 3;
            await t.key(LFO(3));
            const here = U.toastNow() === "FADER: L TO HERE" && sent.lfo.fader.from === 16 && sent.lfo.fader.to === 0;
            await t.key(LFO(3));
            const wide = here && U.toastNow() === "FADER: L TO R" && sent.lfo.fader.from === 32 && sent.lfo.fader.to === 0;
            await t.key(LFO(4));
            t.frame();
            ok(shaped && wide && U.toastNow() === "FADER: R TO L" && sent.lfo.fader.from === 0 && sent.lfo.fader.to === 32 &&
               t.leds[14 + LFO(3)] === 0 && t.leds[14 + LFO(4)] === 1,
               "pads 2-4: its shape (TRIANGLE -> SAW UP), its range (MIRROR -> L TO HERE -> L TO R, dark: nothing on), reversed (R TO L, lit: off toward the right)");
            await t.key(LFO(3)); t.frame();
            const swing = U.toastNow() === "FADER: SWING" && t.leds[14 + LFO(3)] === 0;
            await t.turn("ALGORITHM", -6);
            ok(swing && DJ.xf === 10 && sent.lfo.fader.from === 20 && sent.lfo.fader.to === 0,
               "pad 3 again: SWING (lit dim), around where the crossfader is as far as the nearer end (ALGORITHM to 10: 0 to 20; reversed, off toward L)");
            await t.key(LFO(3)); t.frame();
            ok(U.toastNow() === "FADER: MIRROR" && t.leds[14 + LFO(3)] === 0 && sent.lfo.fader.from === 10 && sent.lfo.fader.to === 22,
               "pad 3 again: MIRROR (the range pad dark), from where the crossfader is to the same mirrored (10 to 22)");
            await t.tap("SEL"); await t.key(LFO(3));
            const hz0 = sent.lfo.fader.hz;
            await t.turn("K1", 10); await t.sleep(100);
            ok(sent.lfo.fader.hz > hz0 && Math.abs(sent.lfo.fader.hz - P.djBpm(L()) / 120) < 1e-9, "it follows the tempo: K1 turned, its speed sent again");
            await t.tap("SEL");
            await t.key(LFO(1));
            ok(!F.on && U.toastNow() === "FADER LFO OFF" && sent.lfo.fader === null, "pad 1 tapped while it runs: FADER LFO OFF");
        }
        /* a deck's filter's LFO follows its FILTER key; KEY by a held pad and the deck's tempo knob */
        await t.key(LFO(5));
        const pm = sent.lfo.filt[L().i];
        ok(L().lfo.on && U.toastNow() === "FILTER L LFO: ITS FILTER IS OFF" && pm && !pm.heard && pm.home === L().filterSet && pm.from === L().filterSet && pm.to === 0,
           "pad 5, the left filter's LFO, its filter off: FILTER L LFO: ITS FILTER IS OFF; it swings unheard around the setting (a preview)");
        await t.key(K.LFILTER);
        const lm = sent.lfo.filt[L().i];
        ok(L().filterOn && lm && lm.heard && lm.gen === pm.gen && lm.from === L().filter && lm.to === 0,
           "... OP3 switches the filter on: the same swing heard (OFF TO HERE, from the filter heard to OFF)");
        {
            const f = () => sent.lfo.filt[L().i], h = L().filter;
            await t.key(LFO(7));
            const full = U.toastNow() === "FILTER L: OFF TO FULL" && f().from === 0 && f().to === -64;
            await t.key(LFO(7));
            const hereFull = U.toastNow() === "FILTER L: HERE TO FULL" && f().from === h && f().to === -64;
            await t.key(LFO(7)); t.frame();
            ok(full && hereFull && U.toastNow() === "FILTER L: SWING" && f().from === h && f().to === -h && t.leds[14 + LFO(7)] === 0,
               "pad 7: its range, OFF TO FULL (OFF to its side's end, LP's), HERE TO FULL, SWING (" + h + " to " + -h + ", the pad dark)");
        }
        await t.key(K.LFILTER);
        {
            const bpm0 = P.djBpm(L());
            t.keyDown(LFO(8)); await t.sleep(30);
            const keyFoot = foot();
            await t.turn("K1", 2);
            t.frame();
            const keyLed = t.leds[14 + LFO(8)];
            t.keyUp(LFO(8)); await t.sleep(40); t.frame();
            ok(L().key === 2 && P.djBpm(L()) === bpm0 && keyFoot === "K1 KEY, A SEMITONE A CLICK" && keyLed === 1 && t.leds[14 + LFO(8)] === 1,
               "pad 8 held: K1 the left deck's KEY (+2), its tempo kept; the pad full while held, and with KEY not 0");
            await t.tap("SEL");
            ok(L().key === 0 && U.toastNow() === "KEY 0", "SEL after: KEY 0");
            t.keyDown(LFO(12)); await t.sleep(30);
            await t.turn("K4", -1);
            t.keyUp(LFO(12)); await t.sleep(40);
            const rKey = R().key;
            await t.tap("SEL");
            ok(rKey === -1 && R().key === 0, "pad 12 held: K4 the right deck's KEY, synced or not (-1); SEL: 0");
        }
        /* the FX LFO: the MIX of DJ's REVERB at first, GLO's FX LFO another effect's; it follows DJ's FX */
        {
            const X = DJ.lfo.FX, revOn = M.state.dj.fx.on.REVERB;
            await t.key(LFO(13));
            const xm = sent.lfo.fx;
            const fr = U.screen.footer();
            ok(X.on && U.toastNow() === "REVERB LFO: TRIANGLE 1/2" && xm && xm.fx === "REVERB" && xm.range === 0 && U.screen.sub() === "LFO" &&
               fr && /^REV( \d+%)?$/.test(fr[0]) && fr[1] === t.PTFM.draw.WHITE,
               "pad 13: the FX LFO on the REVERB's MIX (REVERB LFO: TRIANGLE 1/2); the footer's right shows it, white, no voices count (the header has it) (" + (fr && fr[0]) + ")");
            await t.key(LFO(15));
            ok(U.toastNow() === "REVERB: 0 TO 100 %" && sent.lfo.fx.range === 1, "pad 15: its range, 0 TO 100 %");
            await t.tap("GLO"); await t.turn("SELECT", 3); await t.turn("K1", 1);   /* (FX LFO, the fourth) */
            const picked = M.state.dj.lfoFx;
            await t.tap("GLO");
            ok(picked === "DELAY MIX" && sent.lfo.fx.fx === "DELAY" && foot() === "1 FADER 5 FILTER 13 DELAY", "GLO's FX LFO: DELAY MIX, the LFO on the DELAY's MIX now");
            await t.tap("GLO"); await t.turn("SELECT", 3); await t.turn("K1", -1); await t.tap("GLO");
            await t.key(LFO(13));
            const off = !X.on && U.toastNow() === "REVERB LFO OFF";
            M.state.dj.fx.on.REVERB = false;
            await t.key(LFO(13));
            ok(off && X.on && U.toastNow() === "REVERB LFO: REVERB IS OFF" && U.screen.footer() && U.screen.footer()[0] === "REV OFF",
               "... tapped: REVERB LFO OFF; on with DJ's REVERB off: REVERB LFO: REVERB IS OFF, the footer REV OFF");
            M.state.dj.fx.on.REVERB = revOn;
        }
        await t.key(LFO(5));
        await t.key(LFO(16));
        ok(!DJ.lfo.FADER.on && !DJ.lfo.FX.on && !L().lfo.on && !R().lfo.on && U.toastNow() === "ALL LFOS OFF", "pad 16: ALL LFOS OFF");
        await t.tap("LFO");
        const backTo = U.screen.padsAt().mode === "PLAY" && U.toastNow() === "PADS: PLAY";
        await t.tap("LFO"); await t.key(K.MODE);
        ok(backTo && U.screen.padsAt().mode === "PLAY", "LFO again: back to the pads' mode before (PLAY); the blank key too");
        /* SWAP: the sides exchanged, the crossfader mirrored (its LFO's swing too), SYNC with the side */
        await t.turn("ALGORITHM", -4);
        const was = [L().song, R().song], rev0 = DJ.lfo.FADER.rev;
        await t.tap("SEQ");
        ok(L().song === was[1] && R().song === was[0] && DJ.xf === 20 && DJ.lfo.FADER.rev === !rev0 && DJ.sync && U.toastNow() === "DECKS SWAPPED",
           "SEQ: DECKS SWAPPED, the crossfader mirrored (its LFO's direction too), SYNC still the right's");
        await t.tap("SEL");
        /* HOME + white key: a deck's track */
        await t.homeKey(U.WHITE[9]);
        ok(R().mutes[1] && U.toastNow() === "RIGHT TRACK 01 MUTED", "HOME + white key 10: the right deck's track 01 muted");
        await t.homeKey(U.WHITE[9]);
        /* PLAY: both pause, and resume together; CUE */
        await t.tap("PLAY");
        ok(!L().playing && !R().playing && !P.playing, "PLAY: the decks pause");
        await t.tap("PLAY"); await t.sleep(100);
        ok(L().playing && R().playing, "PLAY again: both resume");
        /* PLAY's pads: 2 CUE (as a DJ player's), 1 SET CUE, 3 JUMP, 4 MOVE, 5-8 the stops, held */
        await t.key(LCUE);
        ok(U.screen.padsAt().mode === "PLAY" && !L().playing && P.djAtCue(L()), "PLAY pad 2, CUE, tapped while playing: back to the cue point, paused");
        t.keyDown(LCUE); await t.sleep(300);
        ok(L().playing && L().cueHeld, "CUE held at the cue point: it plays from there");
        t.keyUp(LCUE); await t.sleep(80);
        ok(!L().playing && P.djAtCue(L()), "... let go: back at the cue point");
        t.keyDown(LCUE); await t.sleep(200);
        const cueFoot0 = foot();
        await t.key(K.LPLAY);
        const cueFoot1 = foot();
        t.keyUp(LCUE); await t.sleep(80);
        ok(cueFoot0 === "LET GO CUE  OP1 PLAYS ON" && cueFoot1 === "LET GO PLAYS ON  OP1 CUE" && L().playing && !L().cueHeld,
           "CUE held, then OP1 (LET GO CUE  OP1 PLAYS ON -> LET GO PLAYS ON  OP1 CUE): let go, the deck plays on");
        await t.sleep(700);
        await t.key(SET);
        ok(L().playing && L().cue.row % barRows(L()) === 0 && !(L().cue.pos === 0 && L().cue.row === 0) && /^CUE: POS 00 BAR \d$/.test(U.toastNow()),
           "PLAY pad 1, SET CUE, while playing: the cue point on the nearest bar line (" + U.toastNow() + "), the deck playing on");
        {
            await t.sleep(300);
            const at0 = P.djState(L()).at, rpb = M.state.songs[L().song].rpb;
            t.keyDown(SET); await t.sleep(400);
            const word = U.toastNow(), held = U.screen.footHints && foot();
            t.keyUp(SET); await t.sleep(40);
            ok(L().playing && L().cue.pos === at0.pos && L().cue.row === Math.floor(at0.row / rpb) * rpb && /^CUE: POS 00 BAR \d( BEAT \d)?$/.test(word) && /^CUE POS /.test(held),
               "SET CUE held past 250 ms: the cue on the start of the beat the press fell in (" + word + ")");
        }
        await t.sleep(400);
        await t.key(JUMP); await t.sleep(400);            /* (the row it lands on is scheduled ahead: heard a row or so later) */
        {
            const at = P.djState(L()).at, d = at.row - L().cue.row;
            ok(L().playing && !L().jump && at.pos === L().cue.pos && d >= 0 && d < 5, "PLAY pad 3, JUMP tapped: back to the cue point at the next row, playing on");
        }
        await t.key(K.LPLAY);
        P.djToStart(L());
        t.keyDown(MOVE); await t.sleep(30);
        await t.turn("K1", 2);
        const mvFoot = foot();
        t.keyUp(MOVE); await t.sleep(40);
        ok(mvFoot === "K1 BARS  K2 PATTERNS" && !L().playing && L().place.pos === 0 && L().place.row === 2 * barRows(L()),
           "PLAY pad 4, MOVE held (K1 BARS  K2 PATTERNS): K1 its cursor 2 bars on; let go, a paused deck is there at once");
        /* (the main started beside a synced right deck playing waits for the right's bar line: until then it is not
         * playing yet for MOVE or the stops) */
        const started = async () => { for (let k = 0; k < 40 && L().startAt; k++) await t.sleep(100); };
        await t.key(K.LPLAY); await started(); await t.sleep(200);
        t.keyDown(MOVE); await t.sleep(30); await t.turn("K1", 2); t.keyUp(MOVE); await t.sleep(20);
        {
            const waits = !!L().jump && !L().jump.cue && L().jump.beat;
            await t.sleep(60000 / P.djBpm(L()) + 150);
            const bar = Math.floor(P.djState(L()).at.row / barRows(L()));
            ok(waits && !L().jump && L().playing && bar === 4, "... playing: let go, the deck moves on the next beat into the cursor's bar (bar " + bar + "), playing on");
        }
        t.keyDown(BRAKE); await t.sleep(900);
        const braked = !!L().brake && L().brake.held && L().halted && L().playing;
        await t.key(K.LPLAY); const keptFoot = foot();
        await t.key(K.LPLAY); const backFoot = foot();
        t.keyUp(BRAKE); await t.sleep(150);
        ok(braked && keptFoot === "LET GO STAYS  OP1 PLAYS ON" && backFoot === "LET GO PLAYS ON  OP1 STAYS" && L().playing && !L().brake && !L().halted,
           "PLAY pad 5, BRAKE held: the deck stopped, standing there; OP1 meanwhile turns what letting go does; let go, it plays on");
        t.keyDown(BRAKE); await t.sleep(200);
        await t.key(K.LPLAY);
        t.keyUp(BRAKE); await t.sleep(700);
        ok(!L().playing && L().braked, "... OP1 pressed while held: let go, it stays where the BRAKE stopped it, paused");
        await t.key(BRAKE); t.frame();
        ok(!L().brake && t.leds[14 + BRAKE] !== .25, "... a stop on a paused deck: refused, its pad blinking");
        await t.key(K.LPLAY); await started();
        /* REC alone: nothing; HOME + REC: the CUT (specs/ui/controls.md#cut), silent while REC is held, the decks
         * keeping time; let go, heard again */
        {
            let hush = null;
            const audioHush = t.PTFM.audio.hush;
            t.PTFM.audio.hush = on => { hush = on; audioHush(on); };
            await t.tap("REC");
            const alone = hush === null && L().playing && R().playing;
            t.down("HOME"); await t.sleep(30); t.down("REC"); await t.sleep(40);
            const hushed = hush === true && U.cutOn && L().playing && R().playing && U.toastNow() === "SOUND CUT: LET GO OF REC";
            t.up("REC"); await t.sleep(20); t.up("HOME"); await t.sleep(160);
            t.PTFM.audio.hush = audioHush;
            ok(alone, "REC alone on DJ: nothing (the decks play on)");
            ok(hushed && hush === false && !U.cutOn && L().playing && R().playing,
               "HOME + REC: the CUT, silent while REC is held (sent to the sound), the decks playing on; let go, heard again");
        }
        t.down("PLAY"); await t.sleep(700); t.up("PLAY"); await t.sleep(100);
        ok(!L().playing && !R().playing && P.djAtCue(R()) && U.toastNow() === "BACK TO THE CUES", "PLAY held: both decks back to their cue points");
        /* FX from DJ: DJ's own setup, LIVE and EDIT; FX again back */
        await t.tap("FX");
        ok(U.screen.name === "FX" && t.PTFM.audio.fxSong() === M.state.dj, "FX from DJ: DJ's own setup");
        const pg0 = U.screen.sub(); await t.tap("SEQ"); const pg1 = U.screen.sub(); await t.tap("SEQ");
        ok(pg0 !== pg1 && pg0 !== "MIXER" && pg1 !== "MIXER" && U.screen.sub() === pg0, "... LIVE and EDIT only, SEQ between them, no MIXER");
        await t.tap("FX");
        ok(U.screen.name === "DJ", "FX again: back to DJ");
        await t.tap("GLO");
        ok(t.top() === "props" && U.top().items.map(i => i.name).join() === "CROSSFADER,FILTER RESONANCE,FILTER THE SENDS,FX LFO,RESET DJ",
           "GLO: DJ PROPERTIES (no VOICES of its own: SETTINGS', 16 the most)");
        {
            /* the voices are the header's on every screen (specs/ui/frame.md#the-header): DJ's footer counts none */
            const D = t.PTFM.draw, S = t.PTFM.audio.stats, u0 = S.used;
            S.used = 9;
            const f9 = U.screen.footer();
            S.used = u0;
            ok(!(f9 && /\d+\/\d+/.test(f9[0])), "DJ's footer: no voices count (the header's) (" + (f9 && f9[0]) + ")");
            ok(U.voicesCol(7, 12) === D.LGREY && U.voicesCol(8, 12) === D.YELLOW && U.voicesCol(11, 12) === D.YELLOW &&
               U.voicesCol(12, 12) === D.RED && U.voicesCol(11, 16) === D.LGREY && U.voicesCol(12, 16) === D.YELLOW &&
               U.voicesCol(15, 16) === D.YELLOW && U.voicesCol(16, 16) === D.RED && U.voicesCol(4, 8) === D.YELLOW,
               "the header's voices, a voice count's colours against the limit in force: red at it, yellow from 4 under");
        }
        await t.tap("GLO");
        /* something else starts: the decks pause; DJ opened with a song playing: DJ apart till its first start */
        await t.key(K.RPLAY); await t.sleep(100);
        await t.homeKey(1); await t.tap("PLAY"); await t.sleep(200);
        ok(P.what === "song" && !R().playing && !L().playing, "a song played on SONGS: the decks pause");
        {
            const lSong = L().song;
            await t.homeKey(10); await t.sleep(200);
            const apart = P.what === "song" && P.playing && !L().playing && !R().playing && L().song === lSong;
            await t.tap("PLAY"); await t.sleep(100);
            ok(apart && P.what === "song" && !P.playing && U.toastNow() === "PAUSED: PLAY AGAIN FOR DJ",
               "DJ opened with a song playing: it plays on, the decks untouched; PLAY pauses it (PAUSED: PLAY AGAIN FOR DJ)");
            await t.homeKey(1); await t.tap("PLAY"); await t.sleep(200);
            await t.homeKey(10); await t.sleep(100);
            const A_ = t.PTFM.audio, allOff = A_.allOff;
            let offs = 0;
            A_.allOff = () => { offs++; allOff(); };
            await t.key(K.LPLAY); await t.sleep(200);
            A_.allOff = allOff;
            ok(P.what === "dj" && L().playing && offs === 1, "... a deck's PLAY key: the song stops, its sound cut, the deck plays");
        }
        /* a song that stops at its end: the deck paused there, PLAY refused, CUE back to the cue point */
        P.pause();
        const ps = M.state.songs[song("PUNCH IN")], wasEnd = ps.atEnd;
        ps.atEnd = "STOP";
        P.djLfoOn(L().lfo);
        P.djLoad(L(), song("PUNCH IN"));
        ok(!L().lfo.on, "a LOAD on a paused deck: its filter's LFO off with its filter");
        const lastPos = ps.order.length - 1;
        L().place = { pos: lastPos, row: M.pattern(ps.order[lastPos]).rows - 2 };
        await t.key(K.LPLAY); await t.sleep(1500);
        ok(!L().playing && L().atEnd && P.djState(L()).end && P.djState(L()).secsLeft === 0, "a song that stops: its deck paused at its end, AT THE END");
        await t.key(K.LPLAY);
        ok(!L().playing && U.toastNow() === "THE END: CUE GOES BACK", "... its PLAY refused: THE END: CUE GOES BACK");
        await t.key(LCUE);
        ok(!L().atEnd && P.djAtCue(L()), "... CUE: back to the cue point");
        ps.atEnd = wasEnd;
        /* OCT-, OCT+: HOME's mutes' tracks, only in HOME's combination (with the white keys); nowhere to go, or
         * without HOME: nothing, no message */
        {
            const trk = s => Math.max(0, ...s.order.map(x => (M.pattern(x) || { tracks: 0 }).tracks));
            const few = M.state.songs.findIndex(s => trk(s) <= 8), many = M.state.songs.findIndex(s => trk(s) > 8);
            const homeOct = async b => { t.down("HOME"); await t.sleep(30); await t.tap(b); t.up("HOME"); await t.sleep(200); };
            P.djLoad(L(), few); P.djLoad(R(), few);
            U.toast("-"); await t.tap("OCT+"); await homeOct("OCT+"); t.frame();
            ok(U.screen.padsAt().page === 0 && U.toastNow() === "-" && t.leds[1] === 0,
               "OCT+ with no tracks 08-0F: nothing, alone or with HOME, no message; dark");
            if (many >= 0) {
                P.djLoad(L(), many);
                await t.tap("OCT+"); t.frame();
                const alone = U.screen.padsAt().page === 0 && t.leds[1] === 0;     /* (OCT- 0, OCT+ 1) */
                t.down("HOME"); await t.sleep(30); t.frame();
                const lit = t.leds[1] > 0 && t.leds[0] === 0;
                await t.tap("OCT+");
                const moved = U.screen.padsAt().page === 1 && U.toastNow() === "MUTES: TRACKS 08-0F";
                U.toast("-"); await t.tap("OCT+");
                const stays = U.screen.padsAt().page === 1 && U.toastNow() === "-";
                await t.tap("OCT-"); t.up("HOME"); await t.sleep(200);
                ok(alone && lit && moved && stays && U.screen.padsAt().page === 0,
                   "a song of more than 8 tracks: OCT+ alone nothing; HOME held: OCT+ lit, to 08-0F (MUTES: TRACKS 08-0F), again nothing; OCT- back");
            }
        }
        /* LOOPS' PATTERN: the deck's current pattern over and over; a deck's PLAY held: back to its start */
        P.djLoad(L(), song("PUNCH IN"));
        const rows0 = M.pattern(ps.order[0]).rows;
        L().place = { pos: 0, row: rows0 - 4 };
        await t.key(K.LPLAY); await t.sleep(150);
        await t.key(K.MODE);                                     /* (PLAY -> LOOPS) */
        await t.key(LREP);
        const repOn = U.screen.padsAt().mode === "LOOPS" && L().repeat && U.toastNow() === "LEFT: REPEAT PATTERN ON";
        t.frame();
        const repLed = t.leds[14 + LREP];
        await t.sleep(1000);
        const stR = P.djState(L());
        ok(repOn && repLed === 1 && stR.at.pos === 0 && stR.at.row < rows0 - 4,
           "LOOPS pad 1, PATTERN (lit): at its end the deck's pattern plays again (POS " + M.hex(stR.at.pos) + " ROW " + M.hex(stR.at.row) + ")");
        await t.key(LREP);
        ok(!L().repeat && U.toastNow() === "LEFT: REPEAT PATTERN OFF", "... tapped again: REPEAT PATTERN OFF");
        t.keyDown(K.LPLAY); await t.sleep(U.set("HOLD DELAY") + 150); t.keyUp(K.LPLAY); await t.sleep(40);
        ok(!L().playing && L().place.pos === 0 && L().place.row === 0 && U.toastNow() === "LEFT: BACK TO THE START",
           "OP1 held: the left deck paused, back to its song's start");
        /* the PLAY button: both decks stopped, it starts both at the same moment; again, both stop */
        if (R().playing) await t.key(K.RPLAY);
        await t.tap("PLAY"); await t.sleep(100);
        ok(L().playing && R().playing && Math.abs(L().r.t0 - R().r.t0) < .001, "the PLAY button, nothing playing: both decks start at once");
        await t.key(K.RPLAY);
        ok(L().playing && !R().playing, "OP6: the right deck's own play / pause, the left plays on");
        await t.tap("PLAY");
        ok(!L().playing && !R().playing, "the PLAY button, a deck playing: every deck stops");
        P.pause();
        /* the stand (specs/files/dj.md#the-stand): SAVE keeps the decks as they stand; REVERT, as a power cycle,
         * brings them back, paused; the decks changed since count as a change (SAVE dim) */
        {
            P.djKey(L(), 2); P.djVol(L(), -3); P.djFilter(L(), -12 - L().filterSet);
            if (!L().filterOn) { P.djFilterSwitch(L()); P.djFilterSettle(L()); }
            L().place = { pos: 1, row: 16 }; L().cue = { pos: 0, row: 32 };
            P.djXf(9 - DJ.xf);
            Object.assign(DJ.lfo.FADER, { on: true, range: 2, len: 4, shape: 0 });
            R().mutes[2] = true;
            await t.tap("SAVE");
            for (let k = 0; k < 40 && U.toastNow() !== "SAVED"; k++) await t.sleep(100);   /* (its progress bar first) */
            t.frame();
            const saved = U.toastNow() === "SAVED" && t.leds[9] === 0 && !!M.state.dj.stand;
            const want = JSON.stringify(P.djStand());
            P.djKey(L(), -5); t.frame();
            const dim = t.leds[9] === .25;
            P.djXf(30 - DJ.xf); P.djLoad(R(), song("ACID LOOP"));
            U.revert(); await t.sleep(100); await t.tap("OCT+"); await t.sleep(900); t.frame();
            const back = JSON.stringify(P.djStand());
            ok(saved && dim && back === want && L().key === 2 && L().vol === -3 && L().place.pos === 1 && L().place.row === 16 && DJ.xf === 9 &&
               DJ.lfo.FADER.on && DJ.lfo.FADER.range === 2 && R().mutes[2] && !L().playing && !R().playing && t.leds[9] === 0,
               "SAVE keeps the DJ stand (SAVE dark after; a deck changed: dim); REVERT brings the decks back as kept, paused (KEY, VOLUME, place, crossfader, its LFO's SWING, mutes)");
            /* GLO's RESET DJ: after its question, both decks empty, all as at first; the stand changed (SAVE dim) */
            await t.tap("GLO"); await t.turn("SELECT", 4);             /* (RESET DJ, the fifth) */
            await t.tap("EDIT");
            const asked = t.top() === "question";
            await t.tap("OCT+"); t.frame();
            const reset = L().song == null && R().song == null && DJ.xf === 0 && DJ.sync && !DJ.lfo.FADER.on && U.toastNow() === "DJ RESET";
            ok(asked && reset && t.leds[9] === .25 && JSON.stringify(P.djStand()) === JSON.stringify(P.djStandToSave() || P.djStand()) && P.djStandToSave() === null,
               "GLO, RESET DJ, EDIT: a question; RESET: both decks empty, the crossfader at L, SYNC on, LFOs off (DJ RESET); SAVE dim");
            await t.tap("GLO");
        }
        t.PTFM.audio.dj = audioDj;
    }
    /* SETTINGS' SEL menu: REVERT alone, the page's own as saved (the settings; on MIDI, the MIDI map); with nothing
     * unsaved there, SEL refused (NOTHING TO REVERT). TOOLS' REVERT ALL, SAVE + OCT-'s question; SAVE's layer */
    {
        P.pauseAll();
        await t.tap("SAVE"); await t.sleep(1800);
        await t.homeKey(20);
        if (U.screen.sub() === "MIDI") await t.tap("SEQ");
        await t.tap("SEL");
        ok(!t.top() && U.toastNow() === "NOTHING TO REVERT", "SETTINGS with nothing unsaved: SEL refused, NOTHING TO REVERT");
        const tun = M.SETTINGS.findIndex(s => s.k === "TUNING"), tun0 = U.set("TUNING");
        await t.turn("SELECT", -40); await t.turn("SELECT", tun); await t.turn("K1", 1);
        ok(U.set("TUNING") !== tun0 && M.unsavedThing("settings"), "(TUNING changed: the settings unsaved)");
        await t.tap("SEL");
        ok(t.top() === "menu" && U.top().title === "SETTINGS" && U.top().items.join() === "REVERT", "SEL: a menu titled SETTINGS, REVERT alone");
        await t.tap("SEL");
        ok(t.top() === "question" && U.top().lines.join("/") === "REVERT THE SETTINGS TO SAVED?" && U.top().answers.join() === "REVERT,KEEP",
           "REVERT asks first: REVERT THE SETTINGS TO SAVED?, REVERT / KEEP");
        await t.tap("OCT+");
        ok(!t.top() && U.set("TUNING") === tun0 && !M.unsavedThing("settings") && U.toastNow() === "REVERTED", "... REVERT: the settings as saved, REVERTED");
        /* SECOND CORE reverts with the rest (and a kept state without the key reads ON: model's restore) */
        const sc = M.SETTINGS.findIndex(s => s.k === "SECOND CORE");
        await t.turn("SELECT", -40); await t.turn("SELECT", sc); await t.turn("K1", 1);
        ok(U.set("SECOND CORE") === "OFF" && M.unsavedThing("settings"), "(SECOND CORE turned OFF: the settings unsaved)");
        await t.tap("SEL"); await t.tap("SEL"); await t.tap("OCT+");
        ok(U.set("SECOND CORE") === "ON" && !M.unsavedThing("settings"), "REVERT: SECOND CORE back to ON");
        await t.tap("SEQ");
        ok(U.screen.sub() === "MIDI", "(the MIDI page)");
        await t.tap("SEL");
        ok(!t.top() && U.toastNow() === "NOTHING TO REVERT", "the MIDI page, its map as saved: SEL refused, NOTHING TO REVERT");
        await t.tap("SEQ");
        const tl = M.SETTINGS.findIndex(s => s.k === "TOOLS");
        ok(M.SETTINGS[tl].list.join() === "CHECK REFERENCES,CHECK STORAGE,CLEAN UP,AUTO LEVEL ALL,REVERT ALL,FACTORY RESET",
           "TOOLS: ... AUTO LEVEL ALL, REVERT ALL, FACTORY RESET");
        await t.turn("SELECT", -40); await t.turn("SELECT", tl);
        const un0 = M.unsaved, v0 = M.version;
        await t.turn("K1", 9); await t.turn("K1", -2);
        ok(U.tool() === "AUTO LEVEL ALL" && M.unsaved === un0 && M.version === v0, "K1 on TOOLS: AUTO LEVEL ALL picked, no change made");
        await t.tap("LFO");
        ok(U.tool() === "AUTO LEVEL ALL" && M.unsaved === un0 && M.version === v0, "LFO on TOOLS: nothing (no default to go back to)");
        await t.tap("EDIT");
        ok(t.top() === "question" && /^AUTO LEVEL \d+ SOUNDS?\?$/.test(U.top().lines[0]) && U.top().lines[1] === "EACH ONE'S LEVEL SET AGAIN" &&
           U.top().answers.join() === "LEVEL,KEEP", "SETTINGS' AUTO LEVEL ALL asks first: " + U.top().lines.join(" / "));
        await t.tap("OCT-");
        ok(!t.top(), "... KEEP: nothing measured");
        await t.turn("K1", 1);
        await t.tap("EDIT");
        ok(!t.top() && U.toastNow() === "NOTHING TO REVERT", "TOOLS' REVERT ALL with nothing unsaved: NOTHING TO REVERT, no question");
        await t.turn("SELECT", -40); await t.turn("SELECT", tun); await t.turn("K1", 1);
        await t.turn("SELECT", tl - tun); await t.tap("EDIT");
        ok(t.top() === "question" && U.top().lines[0] === "REVERT TO SAVED?" && U.top().answers.join() === "REVERT,KEEP", "TOOLS' REVERT ALL: SAVE + OCT-'s question");
        await t.tap("OCT+"); await t.sleep(800);
        ok(!M.unsaved && U.set("TUNING") === tun0 && U.toastNow() === "REVERTED TO SAVED", "... REVERT: everything as saved, REVERTED TO SAVED");
        ok(U.tool() === "CHECK REFERENCES", "... TOOLS back on its first, CHECK REFERENCES (the settings read anew, as the firmware's)");
        t.down("SAVE"); await t.sleep(700); t.frame();
        const lay = U.layers.length ? U.layers[U.layers.length - 1] : null, layText = lay && lay.lines ? (typeof lay.lines === "function" ? lay.lines() : lay.lines) : null;
        t.up("SAVE"); await t.sleep(150);
        ok(layText && layText.join() === "OCT+: SAVE,OCT-: REVERT ALL", "SAVE held, its layer: OCT+: SAVE, OCT-: REVERT ALL (" + (layText && layText.join(" | ")) + ")");
        /* FACTORY RESET, TOOLS' last (before the MIDI CONTROLLER's checks: it ends the sound too): two questions, KEEP
         * and HOME leaving all as it was, the second counting the changes not saved; ERASE: the sound stopped, the
         * ERASING bar, then the restart (the page's reload: a stand-in here) */
        await t.turn("SELECT", -40); await t.turn("SELECT", tl); await t.turn("K1", 9);
        ok(U.tool() === "FACTORY RESET", "K1 on TOOLS: FACTORY RESET, the last");
        await t.tap("EDIT");
        ok(t.top() === "question" && U.top().lines.join("|") === "FACTORY RESET?|ERASES ALL YOU SAVED OR UPLOADED|ONLY THE BUILT-IN INSTRUMENTS STAY" &&
           U.top().answers.join() === "RESET,KEEP", "FACTORY RESET asks first: " + U.top().lines.join(" / "));
        t.shot("settings_factory_reset");
        await t.tap("OCT-");
        ok(!t.top(), "... KEEP: nothing");
        await t.tap("EDIT"); await t.tap("OCT+");
        ok(t.top() === "question" && U.top().lines.join("|") === "ERASE IT ALL NOW?|THERE IS NO WAY BACK" && U.top().answers.join() === "ERASE,KEEP",
           "... RESET: ERASE IT ALL NOW?, nothing unsaved: two lines (" + U.top().lines.join(" / ") + ")");
        await t.tap("HOME");
        ok(!t.top(), "... HOME: nothing");
        await t.turn("SELECT", tun - tl); await t.turn("K1", 1); await t.turn("SELECT", tl - tun);
        await t.tap("EDIT"); await t.tap("OCT+");
        ok(t.top() === "question" && U.top().lines.join("|") === "ERASE IT ALL NOW?|THERE IS NO WAY BACK|1 UNSAVED CHANGE LOST TOO",
           "... with a change unsaved, counted: " + U.top().lines.join(" / "));
        t.shot("settings_factory_reset_2");
        const restart = U.restart;
        let restarted = 0;
        U.restart = () => { restarted++; };
        await t.tap("OCT+"); await t.sleep(100);
        ok(t.top() === "progress" && U.top().text === "ERASING" && t.PTFM.audio.ended && !restarted,
           "... ERASE: the sound stopped, the ERASING bar, every control waiting");
        await t.sleep(2500);
        ok(!t.top() && restarted === 1, "... then the restart");
        U.restart = restart;
    }
    /* SETTINGS' MIDI CONTROLLER (the MIDI page's line after its items), the last (the tracker ends): its question,
     * BACK; SWITCH with a change unsaved: the save question, DROP; the controller's start (REC, HOME lit); REC:
     * mirror mode; a key: its Note On on 16 */
    const midiCtl = async () => {
        await t.homeKey(20);
        if (U.screen.sub() !== "MIDI") await t.tap("SEQ");
        await t.turn("SELECT", -20); await t.turn("SELECT", M.MIDI_SETTINGS.findIndex(s => s.ctl));
        await t.tap("EDIT");
    };
    await midiCtl();
    ok(t.top() === "question" && U.top().lines[0] === "SWITCH TO MIDI CONTROLLER?", "MIDI CONTROLLER asks first");
    t.shot("ui_controller_question");
    await t.tap("OCT-");
    ok(!t.top() && !t.PTFM.ctl.on, "its BACK: the tracker goes on");
    U.edit(() => { M.state.settings.TUNING = 441; });
    await midiCtl(); await t.tap("OCT+");
    ok(t.top() === "question" && U.top().lines[0] === "SAVE BEFORE SWITCHING?" && /^\d+ UNSAVED CHANGES?$/.test(U.top().lines[1]),
       "SWITCH with a change unsaved: SAVE BEFORE SWITCHING?, how many");
    t.shot("ui_controller_save");
    await t.tap("OCT-");
    t.frame();
    const lit = Object.keys(t.leds).filter(k => t.leds[k]).map(Number).sort((a, b) => a - b).join();
    ok(t.PTFM.ctl.on && !t.top() && lit === "8,13" && t.PTFM.audio.ended && !P.playing,
       "DROP: the tracker ended (nothing plays, the sound stopped), the controller's start: REC and HOME lit, nothing else");
    t.shot("ui_controller_start");
    await t.tap("REC"); await t.key(7); await t.turn("K1", 2); t.frame();
    const ctlS = t.PTFM.ctl.state(), on = ctlS.log.find(e => e.kind === "on"), k1 = ctlS.log.filter(e => e.kind === "knob").pop();
    ok(ctlS.mode === 0 && on && on.bytes.join() === [0x9F, 60, 127].join() && k1 && k1.sum === 2 && k1.bytes[1] === 22,
       "REC: mirror mode on 16; key 7: Note On 60 (9F 3C 7F); K1 two clicks: CC 22, +2 on its line");
    t.shot("controller_mirror");
    ok(PTFM_helpOk(t),"the help: every page within 40 characters and 17 lines");
    const hs = PTFM_helpSame(t);
    ok(!hs.length, "the help: every page as the specs write it (the firmware's source)" + (hs.length ? ": " + hs.slice(0, 3).join(" | ") : ""));
};
function PTFM_helpOk(t) { return t.PTFM.help.check().length === 0; }
/* the help's pages against the specs', read as fw/tools/gen_texts.py reads them for the firmware: each screen's Help
 * part (specs/ui/screens/), help.md's shared pages; what differs back */
function PTFM_helpSame(t) {
    const fs = require("fs"), path = require("path"), ui = path.resolve(__dirname, "../../specs/ui");
    const blocks = (file, head) => {
        const md = fs.readFileSync(file, "utf8"), at = md.indexOf("\n" + head + "\n");
        if (at < 0) return [];
        const rest = md.slice(at + head.length + 2), nxt = rest.search(/\n## /), sec = nxt < 0 ? rest : rest.slice(0, nxt);
        return [...sec.matchAll(/```\n([\s\S]*?)```/g)].map(m => m[1].trimEnd().split("\n").map(l => l.trimEnd()));
    };
    const { own, shared } = t.PTFM.help.source(), bad = [];
    const same = (what, a, b) => { if (JSON.stringify(a) !== JSON.stringify(b)) bad.push(what); };
    const files = fs.readdirSync(path.join(ui, "screens")).filter(f => f.endsWith(".md"));
    for (const f of files) {
        const s = f.slice(0, -3).toUpperCase(), pg = blocks(path.join(ui, "screens", f), "## Help");
        if (pg.length || own[s]) same(s, own[s] || [], pg);
    }
    same("SHARED", shared, blocks(path.join(ui, "help.md"), "## The shared pages"));
    return bad;
}
