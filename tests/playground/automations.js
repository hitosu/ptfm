// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* AUTOMATIONS (specs/ui/screens/automations.md): fw/tests/ui3_test.c's test_automations on the playground, the same
 * steps at the same times (its press, release, tap, key, turn and home_key, its shots after 40 ms) and the same checks:
 * HOME + OP6 opens it; a white key picks a slot; PLAY on an empty one; REC from SONGS; HOME + OP4 recorded and played
 * back, landing on PLAYBACK; the stream read by a reader of the test's own; a key of the player's stopping a playback,
 * masked till let go; the menu; REPLACE's question; CLEAR; NOTHING RECORDED; REVERT; SAVE and a power cycle; HOME
 * tapped, then OP6; REVERT ALL while recording; HOME held + OP6 keeping earlier HOME taps; REC from FX opened from LOOPS;
 * the coding's vector (fw/tests/auto_vectors.json) byte for byte; then the edit memory full while recording; then
 * test_auto_compact's COMPACT (the pauses while nothing is held shortened at each limit, the held ones kept, NOTHING TO
 * COMPACT, an empty slot, the edit memory too full, REVERT and SAVE, a slot of 3 parts in 2, byte for byte the test's
 * own writer's, a recording's CUT still held past 2 s when played), test_auto_home's (a playback's time standing
 * still across a progress; HOME, ARP, then a white key: its lines and LEDs, a slot played on its screen, an empty one,
 * ONE AUTOMATION AT A TIME, the arpeggiator no longer HOME, ARP's) and test_auto_home_rec's (HOME, ARP, REC, then a
 * white key: REC's switch, held or not, its lines and LEDs; a slot recorded on the screen it is on, HOME tapped or held;
 * REPLACE / KEEP; on AUTOMATIONS itself; HOME, ARP, REC ending it there, its controls taken out, past a flush too; HOME,
 * ARP, SEQ, ARP, REC; ONE AUTOMATION AT A TIME while one plays; HOME, REC still the CUT). Its shots (auto_empty,
 * auto_recording, auto_one, auto_playing, auto_compact, auto_home_arp, auto_home_rec) are the firmware's twins:
 * fw/tests/run_ui3.sh compares them. On the harness's virtual clock:
 *   node tests/playground/harness.js tests/playground/automations.js */
"use strict";
const fs = require("fs"), path = require("path");
const ROOT = path.resolve(__dirname, "../..");

module.exports = async t => {
    const { U, M, P, ok } = t;
    const B = U.B, W = U.WHITE, BL = U.BLACK, BN = U.BNAME;
    const op6 = BL[5], op4 = BL[3], w2 = W[2], w5 = W[5];
    let kept = null;                                    /* (the browser's store: what SAVE keeps, for the power cycle) */
    M.useStore({ get: async () => kept, put: async v => { kept = structuredClone(v); }, del: async () => { kept = null; } });

    /* ui3_test.c's helpers: a press or a release then 30 ms, a tap then 200 ms more, a knob's click every 120 ms */
    const run = ms => t.sleep(ms);
    const press = async b => { t.down(BN[b]); await run(30); };
    const release = async b => { t.up(BN[b]); await run(30); };
    const tap = async b => { await press(b); await release(b); await run(200); };
    const key = async (n, down) => { if (down) t.keyDown(n); else t.keyUp(n); await run(30); };
    const homeKey = async n => { await press(B.HOME); await key(n, 1); await key(n, 0); await release(B.HOME); await run(200); };
    const shot = async name => { await run(40); t.shot(name); };
    const screenIs = name => U.screen.name === name;
    const state = () => U.auto.state(), slot = () => U.auto.slot();
    const toast = s => U.toastNow() === s;
    const asking = () => { const o = U.top(); return o && o.kind === "question" ? o.lines[0] : ""; };
    const menuHas = async item => { await tap(B.SEL); const o = U.top(), r = !!o && o.kind === "menu" && o.items.includes(item); await tap(B.HOME); return r; };
    const menuPick = async item => { await tap(B.SEL); const o = U.top(); if (o && o.kind === "menu") o.sel = o.items.indexOf(item); await tap(B.SEL); };
    const unsaved = sub => M.unsavedThing("auto", sub);
    /* a slot's records read as specs/files/automations.md codes them (a reader of the test's own), each event's time from
     * time 0: k 0 pressed, 1 let go, 2 a knob (c its clicks); n -1: a part that does not read whole */
    function auRead(s) {
        const TB = [5, 8, 12, 16, 24], e = [];
        let tm = 0, scr = -1;
        for (let part = 0; part < 16; part++) {
            const p = M.state.autos[s * 16 + part];
            if (!p) break;
            const h = part ? 0 : 1, cnt = p[h] | p[h + 1] << 8;
            let b = 8 * (h + 2);
            const bits = n => { let v = 0; for (let i = 0; i < n; i++, b++) v |= ((p[b >> 3] | 0) >> (b & 7) & 1) << i; return v; };
            if (!part) scr = p[0];
            for (let i = 0; i < cnt; i++) {
                let k = 0, off = 0;
                while (k < 4 && bits(1)) k++;
                for (let j = 0; j < k; j++) off += 1 << TB[j];
                tm += off + bits(TB[k]);
                const x = { t: tm, c: 0 };
                if (!bits(1)) { x.k = 0; x.id = bits(6); }
                else if (!bits(1)) { x.k = 1; x.id = bits(6); }
                else { x.k = 2; x.id = bits(3); const c = bits(4); x.c = c >= 8 ? c - 16 : c; }
                e.push(x);
            }
            if (b > 8 * p.length) return { e, n: -1, scr };
        }
        return { e, n: e.length, scr };
    }
    const has = (e, k, id) => e.findIndex(x => x.k === k && x.id === id);
    const keyNotes = () => U.keysHeld().length;         /* (the keys sounding) */

    await homeKey(1);                                   /* SONGS, nothing playing */
    if (P.playing) P.pause();
    await run(100);
    await homeKey(op6);
    ok(screenIs("AUTOMATIONS") && !state(), "AUTOMATIONS: HOME + OP6 opens it, nothing recording or playing");
    await shot("auto_empty");
    await t.turn("SELECT", 2);
    await key(W[3], 1); await key(W[3], 0);             /* (white key 4: slot 03) */
    await tap(B.PLAY);
    ok(screenIs("AUTOMATIONS") && toast("AUTOMATION 03 IS EMPTY") && !state(),
       "AUTOMATIONS: a white key picks its slot; PLAY on an empty one: AUTOMATION 03 IS EMPTY");
    /* recording: back on SONGS (where it was opened from); its time 0 the first press, after a wait */
    await tap(B.REC);
    ok(screenIs("SONGS") && state() === 1 && slot() === 3, "AUTOMATIONS: REC records slot 03 from SONGS, the screen it came from");
    await run(2100);                                    /* (the toast gone: the header's AUTO 03 seen) */
    await shot("auto_recording");
    await homeKey(op4);                                 /* HOME + OP4: PLAYBACK (HOME held back until OP4 shows it) */
    ok(screenIs("PLAYBACK") && state() === 1, "... HOME + OP4 while recording: PLAYBACK, recording on");
    await t.turn("ALGORITHM", 2);
    await key(w2, 1); await run(180); await key(w2, 0);
    await key(w5, 1);                                   /* (held through the end) */
    await homeKey(op6);
    await key(w5, 0);
    ok(screenIs("AUTOMATIONS") && !state() && (U.toastNow() || "").startsWith("AUTOMATION 03 RECORDED: 0:0"),
       "AUTOMATIONS: HOME + OP6 ends it: AUTOMATIONS, AUTOMATION 03 RECORDED: 0:0X");
    let r = auRead(3), e = r.e, n = r.n;
    const hp = has(e, 0, 27 + B.HOME), kp = has(e, 0, op4), wp = has(e, 0, w2), wr = has(e, 1, w2), w5p = has(e, 0, w5), w5r = has(e, 1, w5);
    ok(n > 0 && r.scr === U.ROUND.indexOf("SONGS") && e[0].t === 0 && hp === 0 && kp === 1 && e[1].t >= 25 && e[1].t <= 40,
       "... its file: part 0 says SONGS; time 0 HOME's press (held back, written at its own time), OP4 30 ms on");
    ok(has(e, 2, 1) > kp && wp > kp && wr > wp && e[wr].t - e[wp].t >= 180 && e[wr].t - e[wp].t <= 240,
       "... ALGORITHM's clicks, a white key pressed and let go 0.2 s on");
    ok(w5p > wr && w5r === n - 1 && has(e, 0, op6) < 0 && has(e, 0, 27 + B.REC) < 0 && has(e, 1, 27 + B.REC) < 0,
       "... the key held at its end let go there, last; neither HOME + OP6 nor REC's release recorded");
    ok(e.every((x, i) => !i || x.t >= e[i - 1].t) && unsaved(0x30), "... the times in order; an edit not saved yet");
    await shot("auto_one");
    /* playing: SONGS first (its screen), then what it holds: HOME + OP4 takes it to PLAYBACK */
    await run(2100);
    await press(B.PLAY);
    t.up("PLAY"); await run(1);                         /* (its first event, time 0, given at once: HOME held) */
    ok(screenIs("SONGS") && state() === 2, "AUTOMATIONS: PLAY (at its release) goes to SONGS, the automation's screen, and plays it");
    await shot("auto_playing");
    await run(1500);
    ok(screenIs("PLAYBACK") && !state() && toast("AUTOMATION 03 PLAYED"), "... HOME + OP4 played: PLAYBACK; at its end AUTOMATION 03 PLAYED");
    /* a key of the player's stops it, and does nothing else until let go */
    await homeKey(op6);
    await tap(B.PLAY);
    await run(5);
    await key(w5, 1);
    ok(!state() && toast("AUTOMATION STOPPED") && keyNotes() === 0, "AUTOMATIONS: a key of the player's stops it (AUTOMATION STOPPED), that key playing nothing");
    await key(w5, 0); await run(300);
    await key(w5, 1); await run(50);
    ok(keyNotes() > 0, "... let go, it plays again");
    await key(w5, 0); await run(1500);
    /* the menu; REPLACE's question; CLEAR; NOTHING RECORDED; REVERT */
    await homeKey(op6);
    ok(await menuHas("RECORD") && await menuHas("PLAY") && await menuHas("CLEAR") && await menuHas("REVERT"),
       "AUTOMATIONS: SEL's menu: RECORD, PLAY, CLEAR, REVERT (changes not saved)");
    await tap(B.REC);
    ok(asking().startsWith("AUTOMATION 03 HOLDS 0:0") && U.top().lines[1] === "REPLACE IT?",
       "AUTOMATIONS: REC on a slot holding one asks first: AUTOMATION 03 HOLDS 0:0X / REPLACE IT?");
    await tap(B.OCTM);
    ok(screenIs("AUTOMATIONS") && !state() && auRead(3).n === n, "... KEEP: nothing recorded, the slot as it was");
    await menuPick("CLEAR");
    ok(asking() === "CLEAR AUTOMATION 03?", "AUTOMATIONS: CLEAR asks: CLEAR AUTOMATION 03?");
    await tap(B.OCTP);
    ok(toast("AUTOMATION 03 CLEARED") && auRead(3).n === 0, "... CLEAR: AUTOMATION 03 CLEARED, the slot empty");
    await tap(B.REC);
    await homeKey(op6);
    ok(screenIs("AUTOMATIONS") && !state() && toast("NOTHING RECORDED") && auRead(3).n === 0,
       "AUTOMATIONS: REC, then HOME + OP6 at once: NOTHING RECORDED, the slot empty");
    await t.turn("SELECT", 1);                          /* (slot 04) */
    await tap(B.REC);
    await tap(B.OCTP); await run(100);
    await homeKey(op6);
    ok(auRead(4).n > 0, "AUTOMATIONS: slot 04 recorded");
    await menuPick("REVERT");
    await tap(B.OCTP); await run(100);
    ok(auRead(4).n === 0 && !unsaved(0x40), "AUTOMATIONS: REVERT drops the changes not saved");
    /* SAVE keeps it, the flash read again (as after a power cycle) */
    await tap(B.REC);
    await tap(B.OCTP); await run(300); await tap(B.OCTM); await run(100);
    await homeKey(op6);
    n = auRead(4).n;
    await tap(B.SAVE);
    await run(3000);
    await M.kept();
    await M.startUp();
    await run(100);
    const n2 = auRead(4).n;
    ok(n > 0 && n2 === n && !unsaved(0x40), "AUTOMATIONS: SAVE keeps slot 04, read again from the flash");
    await menuPick("CLEAR");
    await tap(B.OCTP);
    await tap(B.SAVE);
    await run(3000);
    ok(auRead(4).n === 0, "... cleared and saved: none again");
    /* HOME tapped, then OP6 (the combination's other form): the end's HOME and OP6 taken out of the recording, also when
     * HOME's tap reached the edit memory first (a second's wait: flushed); with nothing before them, NOTHING RECORDED */
    await homeKey(op6);
    await key(W[6], 1); await key(W[6], 0);             /* (slot 06) */
    await tap(B.REC);
    await key(w2, 1); await key(w2, 0);
    await run(200);
    await tap(B.HOME);
    await run(1500);
    await key(op6, 1);
    ok(screenIs("AUTOMATIONS") && !state(), "AUTOMATIONS: HOME tapped, then OP6: the recording ends");
    await key(op6, 0);
    r = auRead(6); e = r.e;
    ok(r.n === 2 && e[0].k === 0 && e[0].id === w2 && e[1].k === 1 && e[1].id === w2 && has(e, 0, 27 + B.HOME) < 0 &&
       has(e, 1, 27 + B.HOME) < 0 && has(e, 0, op6) < 0 && has(e, 1, op6) < 0,
       "... its HOME and OP6 not in it (HOME's tap had reached the edit memory): the white key alone");
    await menuPick("CLEAR");
    await tap(B.OCTP);
    await key(W[7], 1); await key(W[7], 0);             /* (slot 07) */
    await tap(B.REC);
    await run(100);
    await tap(B.HOME);
    await run(1500);
    await key(op6, 1);
    await key(op6, 0);
    ok(screenIs("AUTOMATIONS") && toast("NOTHING RECORDED") && auRead(7).n === 0,
       "... HOME tapped, then OP6, nothing else: NOTHING RECORDED, the slot as it was");
    /* REVERT ALL while recording: it ends, its changes dropped with the others (here through its question, answered; the
     * slot read after its RELOADING) */
    await homeKey(op6);
    await key(W[8], 1); await key(W[8], 0);             /* (slot 08) */
    await tap(B.REC);
    await key(w2, 1); await key(w2, 0);
    await run(1200);                                    /* (a second: written into the edit memory) */
    const wrote8 = !!M.state.autos[0x80];
    U.revert(); await tap(B.OCTP);
    const stopped8 = !state() && toast("AUTOMATION STOPPED");
    await run(600);
    ok(wrote8 && stopped8 && auRead(8).n === 0, "AUTOMATIONS: REVERT ALL while recording: it stops (AUTOMATION STOPPED), the slot as saved");
    /* HOME held + OP6 after an earlier HOME tap: that tap stays in it */
    await homeKey(op6);
    await key(W[10], 1); await key(W[10], 0);           /* (slot 0A) */
    await tap(B.REC);
    await key(w2, 1); await key(w2, 0);
    await tap(B.HOME); await tap(B.HOME);               /* (HOME's combination opened and closed) */
    await run(300);
    await homeKey(op6);
    r = auRead(10); e = r.e;
    ok(screenIs("AUTOMATIONS") && e.filter(x => x.k === 0 && x.id === 27 + B.HOME).length === 2 && has(e, 0, op6) < 0,
       "AUTOMATIONS: HOME held + OP6 ends it, the HOME taps before it kept (2)");
    await menuPick("CLEAR");
    await tap(B.OCTP);
    /* recorded from FX: back on FX as it was (its own origin, LOOPS), FX's tap going back there, the recording on */
    await homeKey(22);                                  /* (POLY: LOOPS) */
    await tap(B.FX);
    await homeKey(op6);
    await key(W[9], 1); await key(W[9], 0);             /* (slot 09) */
    await tap(B.REC);
    const onFx = screenIs("FX");
    await tap(B.FX);
    ok(onFx && screenIs("LOOPS") && state() === 1, "AUTOMATIONS: REC from FX opened from LOOPS: back on FX, its FX tap back to LOOPS, still recording");
    await homeKey(op6);
    await menuPick("CLEAR");
    await tap(B.OCTP);
    /* the coding's vector (fw/tests/auto_vectors.json, which the EDITOR's tests read too): recorded on STATUS, every time
     * class, a knob split at +7 and -8, a press and a turn in one pass (here: the same ms); its bytes those the file
     * gives */
    await homeKey(BL[7]);                               /* GLO: STATUS */
    await homeKey(op6);
    await key(W[5], 1); await key(W[5], 0);             /* (slot 05) */
    await tap(B.REC);
    await run(300);
    t.knob("SELECT", -3); await run(40);
    t.keyDown(w2); t.knob("K1", 2); await run(1);
    await run(5000);
    t.keyUp(w2); await run(300);
    t.knob("K2", 9); await run(30);
    t.knob("K3", -10); await run(70000);
    await press(B.OCTP); await release(B.OCTP);
    await key(w5, 1);
    await homeKey(op6);
    await key(w5, 0);
    await tap(B.OCTM);                                  /* (the octave back: not recorded) */
    {
        const p = M.state.autos[0x50], hex = p ? p.map(x => x.toString(16).padStart(2, "0")).join("") : "";
        const want = JSON.parse(fs.readFileSync(path.join(ROOT, "fw/tests/auto_vectors.json"), "utf8")).vectors[0].hex;
        ok(hex === want, "AUTOMATIONS: the recorded stream byte for byte fw/tests/auto_vectors.json's (the firmware's)");
        if (hex !== want) {                             /* (what it recorded, to compare) */
            r = auRead(5);
            console.log("  recorded: screen " + r.scr + ", " + r.n + " events, hex " + hex);
            for (const x of r.e) console.log("  " + x.t + " " + ["press", "release", "knob"][x.k] + " " + x.id + " " + x.c);
        }
    }
    await menuPick("CLEAR");
    await tap(B.OCTP);
    ok(auRead(5).n === 0, "... and cleared");

    /* the edit memory full while recording (specs/ui/screens/automations.md#recording): the flush a second on refused,
     * MEMORY FOR EDITS IS FULL asked; the recording stops there, nothing of it kept */
    await key(W[8], 1); await key(W[8], 0);             /* (slot 08) */
    const max = M.EDITS_MAX;
    M.EDITS_MAX = M.editsUsed + 8;
    await tap(B.REC);
    await key(w2, 1); await key(w2, 0);
    await run(1100);
    ok(!state() && asking() === "MEMORY FOR EDITS IS FULL" && !M.state.autos[0x80] && screenIs("STATUS"),
       "AUTOMATIONS: the edit memory full while recording: MEMORY FOR EDITS IS FULL, the recording stopped where it was");
    await tap(B.OCTM);                                  /* (BACK) */
    M.EDITS_MAX = max;

    /* a fast turn played back as recorded (KNOB SPEED LOW: a click under 30 ms after the one before x4, under 60 x2):
     * PLAYBACK's K4 (a BPM a step), a song playing, its clicks 27, 30, 58, 60, 115 and 25 ms apart (x4 x2 x2 x1 x1 x4):
     * the steps counted on the gaps recorded, so the same tempo at its end (the firmware's test plays it back with its
     * passes 7 ms apart; here each recorded pass comes at its own time) */
    {
        const sp = M.state.settings["KNOB SPEED"];
        M.state.settings["KNOB SPEED"] = "LOW";
        await homeKey(op4);                             /* PLAYBACK */
        P.playSong(t.PTFM.ui.song);
        await run(500);
        await homeKey(op6);
        await key(W[11], 1); await key(W[11], 0);       /* (slot 0B) */
        await tap(B.REC);
        for (const g of [0, 27, 30, 58, 60, 115, 25]) { await run(g); t.knob("K4", 1); }
        await run(100);
        const k1 = U.knobNow(), s1 = P.speed(), sg = M.state.songs[t.PTFM.ui.song];
        const own = Math.floor((Math.floor(sg.tempo * 2400 / ((sg.ticks || 6) * (sg.rpb || 4))) + 5) / 10);
        ok(s1 === Math.floor((own + 150) * 65536 / own) / 65536, "AUTOMATIONS: (recording it: 15 BPM up, 1 + 4 + 2 + 2 + 1 + 1 + 4 steps: " + k1 + ")");
        await homeKey(op6);                             /* (the end) */
        await homeKey(op4);
        await tap(B.LFO);                               /* (K4 back to 100 %) */
        await run(100);
        const back = P.speed() === 1;
        await homeKey(op6);
        t.down(BN[B.PLAY]); await run(30);
        t.up(BN[B.PLAY]); await run(1);
        await run(420);
        ok(back && s1 > 1 && P.speed() === s1 && U.knobNow() === k1 && !state(),
           "AUTOMATIONS: a fast turn played back: the knob's steps as recorded (the same tempo: " + k1 + ")");
        await homeKey(op6);
        await menuPick("CLEAR");
        await tap(B.OCTP);
        ok(auRead(11).n === 0, "... and cleared");
        await homeKey(op4);
        await tap(B.LFO);
        P.pause();
        await run(100);
        M.state.settings["KNOB SPEED"] = sp;
    }
    /* ---- COMPACT (specs/ui/screens/automations.md#compacting): ui3_test.c's test_auto_compact, the same steps ---- */
    /* the test's own writer of a slot's records, as another writer (the EDITOR's) would write them: events e (times from
     * time 0, k 0 pressed, 1 let go, 2 a knob) into parts by the file's rule; its parts' bytes */
    function auEnc(e, scr) {
        const TB = [5, 8, 12, 16, 24], OF = [0, 32, 288, 4384, 69920], parts = [];
        let p = new Uint8Array(4080), hb = 3, b = 24, prev = 0, cnt = 0;
        const put = (n, v) => { for (let i = 0; i < n; i++, b++) if ((v >>> i) & 1) p[b >> 3] |= 1 << (b & 7); };
        for (let i = 0; ; i++) {
            if (i === e.length || hb + ((b - 8 * hb + 44) >> 3) > 4080) {   /* (the part closes: the next event might not fit) */
                if (!parts.length) p[0] = scr;
                p[hb - 2] = cnt & 255;
                p[hb - 1] = cnt >> 8 & 255;
                parts.push(Array.from(p.subarray(0, (b + 7) >> 3)));
                p = new Uint8Array(4080); hb = 2; b = 16; cnt = 0;
                if (i === e.length || parts.length === 16) break;
            }
            const x = e[i], d = x.t - prev;
            let k = 0;
            prev = x.t;
            while (k < 4 && d >= OF[k + 1]) k++;
            put(k < 4 ? k + 1 : 4, (1 << k) - 1);
            put(TB[k], d - OF[k]);
            if (x.k === 2) { put(2, 3); put(3, x.id); put(4, x.c & 15); }
            else { put(x.k + 1, x.k ? 1 : 0); put(6, x.id); }
            cnt++;
        }
        return parts;
    }
    const auWrite = (s, scr, e) => { const ps = auEnc(e, scr); U.edit(() => { for (let k = 0; k < 16; k++) M.state.autos[s * 16 + k] = ps[k] || null; }); return ps.length; };
    const auSame = (s, scr, e) => {                     /* the slot's records byte for byte the test's writer's of e: its parts, 0 not */
        const ps = auEnc(e, scr);
        for (let k = 0; k < 16; k++) {
            const p = M.state.autos[s * 16 + k];
            if (k >= ps.length ? p : !p || p.length !== ps[k].length || p.some((x, i) => x !== ps[k][i])) return 0;
        }
        return ps.length;
    };
    const auParts = s => { let k = 0; while (k < 16 && M.state.autos[s * 16 + k]) k++; return k; };
    /* e compacted as the spec says (the test's own reading): each pause from an event after which nothing of the
     * automation's is held, longer than lim, made lim; the first event's time as it was */
    function auSquash(e, lim) {
        const o = [], held = new Set();
        let t = 0, cut = false;
        e.forEach((x, i) => {
            let d = i ? x.t - e[i - 1].t : x.t;
            if (i && !held.size && d > lim) { d = lim; cut = true; }
            t += d;
            o.push(Object.assign({}, x, { t }));
            if (x.k < 2 && x.id <= 40) { if (x.k) held.delete(x.id); else held.add(x.id); }
        });
        return { o, cut };
    }
    const auEq = (a, b) => a.length === b.length && a.every((x, i) => x.t === b[i].t && x.k === b[i].k && x.id === b[i].id && x.c === b[i].c);
    const compactWith = async lim => { await menuPick("COMPACT"); const o = U.top(); if (o && o.kind === "menu") o.sel = o.items.indexOf(lim); await tap(B.SEL); };
    const homeHas = line => { const c = U.home(); return !!c && U.homeLines(c).map(l => Array.isArray(l) ? l.map(r => r[0]).join("") : l).includes(line); };
    const waitToast = async (s, ms) => { for (let i = 0; i < ms; i++) { if (toast(s)) return i; await run(1); } return -1; };
    const DIM = .25, led = n => t.leds[n] || 0;
    {
        const LIM = ["1 S", "0.5 S", "0.3 S"], LIMS = [1000, 500, 300];
        const LEN = ["AUTOMATION 0C COMPACTED: 0:09", "AUTOMATION 0C COMPACTED: 0:09", "AUTOMATION 0C COMPACTED: 0:08"];
        const FX = 27 + B.FX, SEL = 27 + B.SEL, ev = (t, k, id, c = 0) => ({ t, k, id, c });
        /* a key held 3 s; 3 s idle; FX held 2.5 s; 0.7 s idle; SEL held, K1 then K2 1.5 s on (one pass), let go 0.4 s later;
         * 0.4 s idle; ALGORITHM and a key in one pass, the key let go 50 ms on; 0.25 s idle (under every limit), SELECT */
        const E = [ev(0, 0, w2), ev(3000, 1, w2), ev(6000, 0, FX), ev(8500, 1, FX), ev(9200, 0, SEL), ev(10700, 2, 2, 3),
                   ev(10700, 2, 3, -2), ev(11100, 1, SEL), ev(11500, 2, 1, 1), ev(11500, 0, w5), ev(11550, 1, w5), ev(11800, 2, 0, 1)];
        P.pause(); await run(100);
        await homeKey(BL[7]);                           /* (STATUS) */
        ok(auWrite(12, 0, E) === 1, "(AUTOMATIONS: slot 0C written by the test's writer: 12 events)");
        await tap(B.SAVE); await run(3000);
        await homeKey(op6);
        await key(W[12], 1); await key(W[12], 0);       /* (slot 0C) */
        await tap(B.SEL);
        const its = U.top() && U.top().kind === "menu" ? U.top().items.join(",") : "";
        await tap(B.HOME);
        ok(its === "RECORD,PLAY,COMPACT,CLEAR", "AUTOMATIONS: SEL's menu RECORD, PLAY, COMPACT, CLEAR (REVERT with changes not saved: " + its + ")");
        await menuPick("COMPACT");
        await shot("auto_compact");                     /* (its list) */
        const cl = U.top() && U.top().kind === "menu" ? U.top().items.join(",") : "";
        ok(cl === "1 S,0.5 S,0.3 S" && U.top().title === "COMPACT AUTOMATION 0C", "AUTOMATIONS: COMPACT opens its list: 1 S, 0.5 S, 0.3 S (" + cl + ")");
        await tap(B.HOME);
        let w = E, r, e;
        for (let i = 0; i < 3; i++) {
            await compactWith(LIM[i]);
            const q = auSquash(w, LIMS[i]);
            r = auRead(12); e = r.e;
            ok(q.cut && auEq(e, q.o) && auSame(12, 0, q.o) === 1 && r.scr === 0 && toast(LEN[i]) && unsaved(0xC0),
               "AUTOMATIONS: COMPACT " + LIM[i] + ": the pauses while nothing is held over " + LIM[i] + " shortened, the rest as they were; " + LEN[i] +
               ", an edit; byte for byte the test's writer's (" + U.toastNow() + "; times " + e.map(x => x.t).join(" ") + ")");
            if (!i)
                ok(e[1].t - e[0].t === 3000 && e[2].t - e[1].t === 1000 && e[3].t - e[2].t === 2500 && e[4].t - e[3].t === 700 &&
                   e[5].t - e[4].t === 1500 && e[6].t === e[5].t && e[7].t - e[6].t === 400 && e[8].t - e[7].t === 400 && e[9].t === e[8].t &&
                   e[11].t - e[10].t === 250, "... at 1 S: the key held 3 s, FX held 2.5 s, SEL held 1.5 s + its knobs (one pass) kept; " +
                   "3 s idle made 1 s, 0.7 s and 0.4 s idle kept");
            w = q.o;
        }
        ok(e[2].t - e[1].t === 300 && e[4].t - e[3].t === 300 && e[8].t - e[7].t === 300 && e[11].t - e[10].t === 250 && e[3].t - e[2].t === 2500,
           "AUTOMATIONS: ... compacted again shorter it goes further: at 0.3 S every idle pause over it 0.3 s (0.25 s kept)");
        await compactWith("0.3 S");
        ok(toast("NOTHING TO COMPACT") && auSame(12, 0, w) === 1, "AUTOMATIONS: COMPACT with no pause over the limit: NOTHING TO COMPACT, the slot as it was");
        await menuPick("REVERT");
        await tap(B.OCTP); await run(100);
        ok(auSame(12, 0, E) === 1 && !unsaved(0xC0), "AUTOMATIONS: REVERT drops a COMPACT: the slot as saved");
        /* the edit memory too full for its parts: MEMORY FOR EDITS IS FULL, the slot as it was */
        {
            const max = M.EDITS_MAX;
            M.EDITS_MAX = M.editsUsed + 8;
            await compactWith("0.3 S");
            ok(asking() === "MEMORY FOR EDITS IS FULL" && auSame(12, 0, E) === 1 && !unsaved(0xC0),
               "AUTOMATIONS: COMPACT with the edit memory too full: MEMORY FOR EDITS IS FULL, the slot as it was (" + asking() + ")");
            await tap(B.OCTM);                          /* (BACK) */
            M.EDITS_MAX = max;
        }
        await compactWith("0.3 S");
        const q3 = auSquash(E, 300);
        await tap(B.SAVE); await run(3000);
        await M.kept();
        await M.startUp();
        await run(100);
        ok(auSame(12, 0, q3.o) === 1 && !unsaved(0xC0), "AUTOMATIONS: SAVE keeps a COMPACT, read again from the flash");
        /* an empty slot */
        await key(W[13], 1); await key(W[13], 0);       /* (slot 0D) */
        await menuPick("COMPACT");
        ok(toast("AUTOMATION 0D IS EMPTY") && !U.top() && !auParts(13), "AUTOMATIONS: COMPACT on an empty slot: AUTOMATION 0D IS EMPTY, no list");
        /* a slot of 3 parts (1,350 notes, most pauses over a minute), written again in 2: event for event as the rule says,
         * byte for byte as the test's writer lays them out */
        {
            const IDLE = [70000, 70000, 5000, 40, 70000], HOLD = [100, 4500, 0], e3 = [];
            let tm = 0;
            for (let i = 0; i < 1350; i++) {
                const id = i % 41;
                tm += i ? IDLE[i % 5] : 0;
                e3.push(ev(tm, 0, id));
                if (i % 7 === 3) { tm += 50; e3.push(ev(tm, 2, i % 7, 2)); }   /* (a knob turned while it is held) */
                tm += HOLD[i % 3];
                e3.push(ev(tm, 1, id));
            }
            const was = auWrite(14, 3, e3), q = auSquash(e3, 300);
            await key(W[14], 1); await key(W[14], 0);   /* (slot 0E) */
            await compactWith("0.3 S");
            const rr = auRead(14), np = auParts(14), tl = q.o[q.o.length - 1].t;
            const want = "AUTOMATION 0E COMPACTED: " + Math.floor(tl / 60000) + ":" + String(Math.floor(tl / 1000) % 60).padStart(2, "0");
            ok(was === 3 && q.cut && np === 2 && auEq(rr.e, q.o) && auSame(14, 3, q.o) === 2 && rr.scr === 3 && toast(want),
               "AUTOMATIONS: COMPACT on a slot of " + was + " parts (" + e3.length + " events): " + np + " parts now, event for event the rule's (" +
               rr.n + " read), byte for byte the test's writer's, screen kept; " + want + " (" + U.toastNow() + ")");
            await menuPick("CLEAR");
            await tap(B.OCTP);
            ok(!auParts(14), "(... and cleared)");
        }
        /* recorded on SONGS: a key held 3 s, HOME + REC held 2.5 s (CUT: every effect off at 2 s), SEL held + a knob 1.5 s
         * on; the idle pauses between them 2 s, 1.5 s, 2 s. Compacted at 0.3 S: those 0.3 s, the held ones kept; played back,
         * CUT's hold still switches the effects off (SOUND CUT, EFFECTS OFF) */
        {
            await homeKey(1);                           /* SONGS */
            P.pause(); await run(100);
            await homeKey(op6);
            await key(W[1], 1); await key(W[1], 0);     /* (slot 01) */
            await tap(B.REC);
            await run(500);
            await key(w2, 1); await run(3000); await key(w2, 0);
            await run(2000);
            await press(B.HOME); await press(B.REC); await run(2500);
            const cut1 = toast("SOUND CUT, EFFECTS OFF");
            await release(B.REC); await release(B.HOME);
            await run(1500);
            await press(B.SEL); await run(1500); t.knob("K1", 1); await run(300); await release(B.SEL);
            await run(2000);
            await key(w5, 1); await key(w5, 0);
            await run(100);
            await homeKey(op6);                         /* (the end) */
            const e0 = auRead(1).e;
            await compactWith("0.3 S");
            const ww = auRead(1).e, at = (k, id) => has(ww, k, id), q = auSquash(e0, 300);
            const wp = at(0, w2), wr = at(1, w2), hp = at(0, 27 + B.HOME), rp = at(0, 27 + B.REC), rr = at(1, 27 + B.REC), hr = at(1, 27 + B.HOME);
            const sp = at(0, SEL), kn = at(2, 2), sr = at(1, SEL), fp = at(0, w5);
            ok(cut1 && auEq(ww, q.o) && wp >= 0 && ww[wr].t - ww[wp].t >= 2990 && hp === wr + 1 && ww[hp].t - ww[wr].t === 300 &&
               ww[rr].t - ww[rp].t >= 2490 && sp === hr + 1 && ww[sp].t - ww[hr].t === 300 && ww[kn].t - ww[sp].t >= 1490 && fp === sr + 1 &&
               ww[fp].t - ww[sr].t === 300, "AUTOMATIONS: a recording compacted at 0.3 S: the key held 3 s, HOME + REC held 2.5 s, SEL held 1.5 s " +
               "before its knob kept, the idle pauses between 0.3 s (cut while recording " + cut1 + "; " + ww.length + " events)");
            t.down(BN[B.PLAY]); await run(30);
            t.up(BN[B.PLAY]); await run(1);
            const due = ww[rp].t + 2000;                /* (CUT's 2 s, from REC's press) */
            const seen = await waitToast("SOUND CUT, EFFECTS OFF", due + 600);
            const played = await waitToast("AUTOMATION 01 PLAYED", 5000);
            ok(seen >= due - 5 && seen <= due + 40 && played >= 0, "AUTOMATIONS: ... played back: HOME + REC still held past 2 s: SOUND CUT, " +
               "EFFECTS OFF (" + seen + " ms in, " + due + " due), played to its end (" + played + ")");
            U.revert(); await tap(B.OCTP);              /* (its recording and the effects it switched off dropped) */
            await run(600);
        }
    }

    /* ---- HOME, ARP, then a white key; a playback across a progress: ui3_test.c's test_auto_home, the same steps ---- */
    {
        let t1, t2;
        /* slot 02, recorded on STATUS: OCT+ tapped 4 times, 100 ms apart (taps act as let go: at 30, 130, 230, 330 ms) */
        P.pause(); await run(100);
        await homeKey(BL[7]);                           /* (STATUS) */
        await homeKey(op6);
        await key(W[2], 1); await key(W[2], 0);         /* (slot 02) */
        await tap(B.REC);
        await run(200);
        for (let i = 0; i < 4; i++) { await press(B.OCTP); await release(B.OCTP); await run(40); }
        await run(100);
        await homeKey(op6);
        ok(U.octave === 4 && auParts(2) === 1, "(AUTOMATIONS: slot 02 recorded on STATUS: OCT+ 4 times)");
        /* the stand-still: played with a progress opened 50 ms in for 500 ms: the taps due meanwhile come after it */
        U.octave = 0;
        t.down(BN[B.PLAY]); await run(30);
        t.up(BN[B.PLAY]); await run(1);
        await run(49);
        U.progress("SAVING 1 CHANGE", 500, () => {});
        const o1 = U.octave;
        await run(500);
        const o2 = U.octave, blk = U.blocking();
        for (t1 = 0; t1 < 400 && U.octave < 4; t1++) await run(1);
        ok(o1 === 1 && o2 === 1 && !blk && U.octave === 4 && t1 >= 270 && t1 <= 300, "AUTOMATIONS: a playback across a progress: its time " +
           "stands still, the taps after it all given, none lost (octave " + o1 + " as it opened, " + o2 + " as it closed, " + U.octave +
           " after, the last " + t1 + " ms after the close; blocking " + blk + ")");
        await run(200);
        ok(!state(), "(... played to its end)");
        /* HOME tapped, then ARP: the white keys play the automations; the lines, the LEDs */
        U.octave = 0;
        await homeKey(1);                               /* SONGS */
        await tap(B.HOME);
        ok(homeHas("SEQ: SOLO INSTEAD  ARP: AUTOMATIONS") && homeHas("WHITE KEY: MUTE A TRACK, HELD: SOLO"),
           "HOME's combination: SEQ's and ARP's switches two a line, SEQ: SOLO INSTEAD  ARP: AUTOMATIONS");
        await tap(B.ARP);
        t.frame();
        ok(U.top() && U.top().kind === "home" && homeHas("WHITE KEY: PLAY ITS AUTOMATION") && homeHas("SEQ: SOLO INSTEAD  ARP: MUTE INSTEAD") &&
           !homeHas("WHITE KEY: MUTE A TRACK, HELD: SOLO") && led(B.ARP) === 1 && led(B.SEQ) === DIM && led(14 + W[2]) === DIM && led(14 + W[3]) === 0 &&
           !U.arp, "HOME, ARP: the white keys play the automations (WHITE KEY: PLAY ITS AUTOMATION, ARP: MUTE INSTEAD), ARP full, SEQ dim, a " +
           "slot's key dim where it holds one, dark where empty; the arpeggiator untouched (ARP " + led(B.ARP) + " SEQ " + led(B.SEQ) + ", keys " +
           led(14 + W[2]) + " " + led(14 + W[3]) + ")");
        await shot("auto_home_arp");
        await tap(B.SEQ);
        t.frame();
        ok(U.top() && U.top().kind === "home" && homeHas("WHITE KEY: SOLO A TRACK") && homeHas("SEQ: MUTE INSTEAD  ARP: AUTOMATIONS") &&
           led(B.SEQ) === 1 && led(B.ARP) === DIM, "HOME, ARP, SEQ: from the automations to SOLO (SEQ full, ARP dim)");
        await tap(B.ARP);
        await tap(B.ARP);
        t.frame();
        ok(U.top() && U.top().kind === "home" && homeHas("WHITE KEY: MUTE A TRACK, HELD: SOLO") && homeHas("SEQ: SOLO INSTEAD  ARP: AUTOMATIONS") &&
           led(B.ARP) === DIM && led(B.SEQ) === DIM && !U.arp, "... ARP again: the automations; again: back to MUTE");
        await tap(B.ARP);
        await key(W[3], 1); await key(W[3], 0);         /* (slot 03: empty) */
        ok(toast("AUTOMATION 03 IS EMPTY") && U.top() && U.top().kind === "home" && homeHas("SELECT: THE SCREENS, ONE BY ONE") &&
           homeHas("WHITE KEY: PLAY ITS AUTOMATION") && !state(), "HOME, ARP, an empty slot's key: AUTOMATION 03 IS EMPTY, the combination open as it was");
        await key(W[2], 1);
        let on2 = screenIs("STATUS") && state() === 2 && !U.top();
        await key(W[2], 0);
        await run(600);
        ok(on2 && U.octave === 4 && !state(), "HOME, ARP, a white key: its slot played on its screen (SONGS to STATUS, the combination closed), to its end");
        /* HOME held */
        U.octave = 0;
        await homeKey(1);
        await press(B.HOME); await press(B.ARP); await release(B.ARP);
        await key(W[2], 1);
        on2 = screenIs("STATUS") && state() === 2;
        await key(W[2], 0);
        await release(B.HOME);
        await run(600);
        ok(on2 && U.octave === 4 && !state() && !U.top(), "HOME held, ARP, a white key: the same, HOME's release nothing more");
        /* HOME, then ARP: not the arpeggiator any more; ARP alone still is */
        await homeKey(1);
        await tap(B.HOME); await tap(B.ARP);
        t1 = U.arp || !U.top();
        await tap(B.HOME);
        await tap(B.ARP);
        t2 = U.arp && !U.top();
        await tap(B.ARP);
        ok(!t1 && t2 && !U.arp, "HOME tapped, then ARP: the arpeggiator untouched, the combination open; ARP alone: on, again off");
        /* ONE AUTOMATION AT A TIME, while one records (slot 04, on SONGS: its HOME, ARP, white key in it), then while it plays */
        await homeKey(op6);
        await key(W[4], 1); await key(W[4], 0);         /* (slot 04) */
        await tap(B.REC);
        await run(300);
        await tap(B.HOME); await tap(B.ARP);
        await key(W[2], 1); await key(W[2], 0);
        ok(state() === 1 && slot() === 4 && toast("ONE AUTOMATION AT A TIME") && U.top() && U.top().kind === "home",
           "HOME, ARP, a white key while recording: ONE AUTOMATION AT A TIME, the recording on, the combination open");
        await tap(B.HOME);
        await run(300);
        await homeKey(op6);                             /* (the end) */
        await tap(B.PLAY);
        t1 = await waitToast("ONE AUTOMATION AT A TIME", 3000);
        t2 = await waitToast("AUTOMATION 04 PLAYED", 3000);
        await run(300);                                 /* (its last HOME's tap: after COMBO GRACE, it closes the combination) */
        ok(t1 >= 0 && t2 >= 0 && screenIs("SONGS") && !U.top(), "... played: its HOME, ARP, white key while it plays: ONE AUTOMATION AT A TIME (" +
           t1 + "), on to its end (" + t2 + ")");
        /* the slots cleared, saved */
        await homeKey(op6);
        for (let s = 1; s < 16; s++)
            if (auParts(s)) { await key(W[s], 1); await key(W[s], 0); await menuPick("CLEAR"); await tap(B.OCTP); }
        await tap(B.SAVE); await run(3000);
        let left = 0;
        for (let s = 0; s < 16; s++) left += auParts(s);
        ok(!left, "(AUTOMATIONS: every slot cleared again, saved)");
        U.octave = 0;
        await homeKey(1);
    }

    /* ---- HOME, ARP, REC, then a white key: ui3_test.c's test_auto_home_rec, the same steps ---- */
    {
        const songs = U.ROUND.indexOf("SONGS"), D = t.PTFM.draw;
        const c565 = c => { const [r, g, b] = D.rgb[c & 15]; return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); };
        const count = (x0, y0, x1, y1, c) => { let k = 0; for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (t.px(x, y) === c) k++; return k; };
        const isHome = () => !!U.top() && U.top().kind === "home";
        let r, e, okk, put, t1, t2;
        P.pause(); await run(100);
        await homeKey(1);                               /* SONGS */
        await run(2100);                                /* (no toast left) */
        /* the switch: the lines, the LEDs */
        await tap(B.HOME); await tap(B.ARP);
        t.frame();
        ok(homeHas("REC: RECORD INSTEAD") && !homeHas("REC: CUT, SILENT WHILE HELD") && led(B.REC) === DIM && led(14 + W[3]) === 0,
           "HOME, ARP: REC: RECORD INSTEAD in the CUT's place, REC dim, an empty slot's key dark");
        await tap(B.REC);
        t.frame();
        ok(isHome() && homeHas("WHITE KEY: RECORD ITS AUTOMATION") && homeHas("REC: PLAY INSTEAD") && homeHas("SEQ: SOLO INSTEAD  ARP: MUTE INSTEAD") &&
           led(B.REC) === 1 && led(B.ARP) === 1 && led(B.SEQ) === DIM && led(14 + W[3]) === DIM &&
           led(14 + W[15]) === DIM && !U.toastNow() && !state(), "HOME, ARP, REC: the white keys record the automations (WHITE KEY: RECORD ITS " +
           "AUTOMATION, REC: PLAY INSTEAD), REC and ARP full, SEQ dim, every white key dim (the empty slots' too); no CUT (REC " + led(B.REC) +
           " ARP " + led(B.ARP) + " SEQ " + led(B.SEQ) + ", keys " + led(14 + W[3]) + " " + led(14 + W[15]) + ")");
        await shot("auto_home_rec");
        await press(B.REC);
        t.frame();
        okk = !U.toastNow() && led(B.REC) < 1;
        await run(2100);
        t.frame();
        okk = okk && !U.toastNow() && led(B.REC) < 1 && !state();
        await release(B.REC);
        t.frame();
        ok(okk && isHome() && homeHas("WHITE KEY: PLAY ITS AUTOMATION") && homeHas("REC: RECORD INSTEAD") && led(B.REC) === DIM && led(14 + W[3]) === 0,
           "... REC again, held 2 s: back to playing them (REC: RECORD INSTEAD, REC dim, the empty slot's key dark), no CUT, no effects switched off");
        await tap(B.REC);
        await tap(B.SEQ);
        t.frame();
        ok(homeHas("WHITE KEY: SOLO A TRACK") && homeHas("REC: CUT, SILENT WHILE HELD") && led(B.SEQ) === 1 && led(B.ARP) === DIM && led(B.REC) === DIM,
           "... SEQ from recording them: SOLO (REC's line the CUT's again)");
        await tap(B.ARP); await tap(B.REC); await tap(B.ARP);
        t.frame();
        ok(homeHas("WHITE KEY: MUTE A TRACK, HELD: SOLO") && homeHas("SEQ: SOLO INSTEAD  ARP: AUTOMATIONS") && homeHas("REC: CUT, SILENT WHILE HELD") &&
           led(B.ARP) === DIM, "... ARP from recording them: back to MUTE");
        /* a recording from SONGS, ended there by HOME, ARP, REC; its part in the edit memory before HOME */
        await tap(B.ARP); await tap(B.REC);
        await key(W[3], 1);
        t.frame();
        okk = screenIs("SONGS") && state() === 1 && slot() === 3 && !U.top() && count(12, 0, 68, 16, c565(D.RED)) > 0;
        await key(W[3], 0);
        ok(okk, "HOME, ARP, REC, white key 4: slot 03 recording on SONGS, the screen it is on (no AUTOMATIONS), the combination closed, the header's AUTO 03 red");
        await run(300);
        await key(w2, 1); await key(w2, 0);
        await run(1200);                                /* (a second on: its events in the edit memory) */
        put = !!M.state.autos[0x30];
        await tap(B.HOME); await tap(B.ARP);
        t.frame();
        ok(state() === 1 && homeHas("REC: END THE RECORDING") && homeHas("WHITE KEY: PLAY ITS AUTOMATION") && led(B.REC) === DIM,
           "HOME, ARP while recording: REC: END THE RECORDING, REC dim, the white keys playing them (as ever)");
        await tap(B.REC);
        r = auRead(3); e = r.e;
        ok(put && screenIs("SONGS") && !state() && !U.top() && toast("AUTOMATION 03 RECORDED: 0:00") && r.n === 2 && r.scr === songs &&
           e[0].k === 0 && e[0].id === w2 && e[0].t === 0 && e[1].k === 1 && e[1].id === w2, "... REC: it ends where it is (SONGS, the combination " +
           "closed), AUTOMATION 03 RECORDED: 0:00; in it the white key alone, none of HOME, ARP, REC, white key 4 (its part in the edit memory " +
           "before HOME: " + put + "; " + r.n + " events)");
        /* HOME held, for the start and the end */
        await press(B.HOME); await press(B.ARP); await release(B.ARP); await press(B.REC); await release(B.REC);
        await key(w5, 1);
        okk = screenIs("SONGS") && state() === 1 && slot() === 5;
        await key(w5, 0);
        await release(B.HOME);
        await run(200);
        ok(okk && state() === 1 && !U.top(), "HOME held, ARP, REC, white key 6: slot 05 recording on SONGS, HOME's release nothing more");
        await key(w2, 1); await key(w2, 0);
        await run(100);
        await press(B.HOME); await press(B.ARP); await release(B.ARP); await press(B.REC);
        okk = screenIs("SONGS") && !state() && toast("AUTOMATION 05 RECORDED: 0:00");
        await release(B.REC); await release(B.HOME);
        await run(200);
        r = auRead(5); e = r.e;
        ok(okk && !U.top() && r.n === 2 && e[0].k === 0 && e[0].id === w2 && e[1].k === 1 && e[1].id === w2, "... HOME held, ARP, REC: it ends " +
           "where it is, AUTOMATION 05 RECORDED: 0:00; in it the white key alone, none of HOME, ARP, REC, white key 6");
        /* a slot holding one: KEEP, then REPLACE; HOME + OP6 ends it as before */
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        await key(W[3], 1); await key(W[3], 0);
        okk = asking() === "AUTOMATION 03 HOLDS 0:00" && U.overlays.length === 1 && screenIs("SONGS") && !state();
        await tap(B.OCTM);
        ok(okk && screenIs("SONGS") && !state() && !U.top() && auRead(3).n === 2 && auRead(3).e[0].id === w2,
           "HOME, ARP, REC, white key 4 on a slot holding one: AUTOMATION 03 HOLDS 0:00 / REPLACE IT? over SONGS (the combination closed); KEEP: " +
           "nothing recorded, the slot as it was");
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        await key(W[3], 1); await key(W[3], 0);
        await tap(B.OCTP);
        okk = screenIs("SONGS") && state() === 1 && slot() === 3 && !U.top();
        await key(w5, 1); await run(100); await key(w5, 0);
        await run(100);
        await homeKey(op6);
        r = auRead(3); e = r.e;
        ok(okk && screenIs("AUTOMATIONS") && !state() && toast("AUTOMATION 03 RECORDED: 0:00") && r.n === 2 && r.scr === songs && e[0].k === 0 &&
           e[0].id === w5 && e[1].k === 1 && e[1].id === w5, "... REPLACE: recording on SONGS, the answer not in it; HOME + OP6 ends it as before, " +
           "AUTOMATIONS open: the white key alone in it");
        /* on AUTOMATIONS itself: the screen it was opened from; HOME, ARP, REC at once: NOTHING RECORDED */
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        await key(W[6], 1);
        okk = screenIs("SONGS") && state() === 1 && slot() === 6;
        await key(W[6], 0);
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        ok(okk && screenIs("SONGS") && !state() && !U.top() && toast("NOTHING RECORDED") && !auParts(6), "HOME, ARP, REC, white key 7 on " +
           "AUTOMATIONS: slot 06 recording on SONGS, where AUTOMATIONS was opened from; HOME, ARP, REC at once: NOTHING RECORDED, the slot empty");
        /* the end's own events past a flush (the buffer full while ARP switches on): taken out again from the edit memory */
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        await key(W[7], 1); await key(W[7], 0);
        await key(w2, 1); await key(w2, 0);
        await tap(B.HOME);
        for (let i = 0; i < 61; i++) await tap(B.ARP);   /* (the buffer full meanwhile: flushed) */
        put = !!M.state.autos[0x70];
        okk = state() === 1 && homeHas("REC: END THE RECORDING");
        await tap(B.REC);
        r = auRead(7); e = r.e;
        ok(put && okk && !state() && screenIs("SONGS") && toast("AUTOMATION 07 RECORDED: 0:00") && r.n === 2 && e[0].k === 0 && e[0].id === w2 &&
           e[1].k === 1 && e[1].id === w2, "HOME, ARP 61 times, REC: the end's events past a flush (in the edit memory before REC: " + put +
           ") taken out again: the white key alone in it (" + r.n + " events)");
        /* something else between HOME and REC (SEQ): it ends there, its controls kept, REC let go at the end */
        await tap(B.HOME); await tap(B.ARP); await tap(B.REC);
        await key(W[8], 1); await key(W[8], 0);
        await key(w2, 1); await key(w2, 0);
        await tap(B.HOME); await tap(B.ARP); await tap(B.SEQ); await tap(B.ARP); await tap(B.REC);
        r = auRead(8); e = r.e;
        ok(!state() && screenIs("SONGS") && !U.top() && (U.toastNow() || "").startsWith("AUTOMATION 08 RECORDED: 0:0") && has(e, 0, 27 + B.HOME) === 2 &&
           has(e, 0, 27 + B.SEQ) > 2 && has(e, 0, 27 + B.REC) > 2 && has(e, 1, 27 + B.REC) === r.n - 1, "HOME, ARP, SEQ, ARP, REC: it ends there, " +
           "HOME, ARP, SEQ and REC kept in it (something else between), REC let go at its end");
        /* HOME, ARP, REC, a white key while one plays (slot 09, the test's writer's): ONE AUTOMATION AT A TIME */
        {
            const HM = 27 + B.HOME, AR = 27 + B.ARP, RC = 27 + B.REC, ev = (t, k, id) => ({ t, k, id, c: 0 });
            const E = [ev(0, 0, HM), ev(30, 1, HM), ev(260, 0, AR), ev(290, 1, AR), ev(520, 0, RC), ev(550, 1, RC), ev(780, 0, W[10]), ev(810, 1, W[10]),
                       ev(1100, 0, HM), ev(1130, 1, HM)];
            ok(auWrite(9, songs, E) === 1, "(AUTOMATIONS: slot 09 written by the test's writer: HOME, ARP, REC, white key 11, HOME)");
        }
        await tap(B.HOME); await tap(B.ARP);
        await key(W[9], 1); await key(W[9], 0);
        await run(640);                                 /* (700 ms in: HOME, ARP, REC given) */
        okk = state() === 2 && homeHas("WHITE KEY: RECORD ITS AUTOMATION") && homeHas("REC: PLAY INSTEAD");
        t1 = await waitToast("ONE AUTOMATION AT A TIME", 1000);
        t2 = await waitToast("AUTOMATION 09 PLAYED", 2000);
        await run(300);
        ok(okk && t1 >= 0 && t2 >= 0 && !auParts(10) && !state() && screenIs("SONGS") && !U.top(), "HOME, ARP, REC, a white key while one plays: " +
           "ONE AUTOMATION AT A TIME (" + t1 + "), slot 0A untouched, on to its end (" + t2 + ")");
        /* HOME, REC before ARP: the CUT as ever */
        await tap(B.HOME);
        await press(B.REC);
        t.frame();
        okk = toast("SOUND CUT: LET GO OF REC") && !U.top() && led(B.REC) === 1;
        await release(B.REC);
        await run(200);
        ok(okk, "HOME, REC (no ARP): the CUT as ever, the combination closed");
        /* the slots cleared, saved */
        await homeKey(op6);
        for (let s = 0; s < 16; s++)
            if (auParts(s)) { await key(W[s], 1); await key(W[s], 0); await menuPick("CLEAR"); await tap(B.OCTP); }
        await tap(B.SAVE); await run(3000);
        let left = 0;
        for (let s = 0; s < 16; s++) left += auParts(s);
        ok(!left, "(AUTOMATIONS: every slot cleared again, saved)");
        await homeKey(1);
    }
};
module.exports.virtual = true;
