// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The EDITOR's realtime take, its keys heard at once (specs/ui/screens/editor.md#realtime-takes, How a take records; the
 * proposal 202611201537-editor-takes-at-once): fw/tests/ui3_test.c's test_quantize, the same steps on the replay itself.
 * Each key sounds at its press and ends at its release, with every QUANTIZE, and is written where QUANTIZE lands it; a
 * note landing ahead is not started again at its line in that pass, then the replay plays it there; a quick tap let go
 * before its row heard short, its note and its off written, the off not cutting a next key on that track before the
 * off's row; REAL TIME at its press, its EDx written; a chord spreading, heard at once on its tracks. Rows of 0.5 s
 * (the scheduler runs 0.12 s ahead: the rows a check needs not yet scheduled). On the harness's virtual clock:
 *   node tests/playground/harness.js tests/playground/editor_takes.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t, A = t.PTFM.audio, W = U.WHITE, OWN = M.EDITOR_PATTERN;
    const ons = [], offs = [], on0 = A.noteOn, off0 = A.noteOff;
    A.noteOn = (id, time, midi, ref, vol, track, o) => { ons.push({ id, time, midi, track }); return on0(id, time, midi, ref, vol, track, o); };
    A.noteOff = (id, time) => { offs.push({ id, time }); return off0(id, time); };
    /* where the take is: every row's time as it is heard (pass * 100 + row), the place in rows from the take's start */
    const rowsT = {};
    const look = () => { const h = P.at(); if (h && h.pass != null) rowsT[h.pass * 100 + h.row] = h.t; return h; };
    const pos = () => { const h = look(); return h ? h.pass * 16 + h.row + (P.time() - h.t) / h.dur : -1; };
    /* on to the next time the take is at row + frac (this pass or the next); its pass */
    const to = async (row, frac) => {
        const p0 = pos(), pass = Math.floor(p0 / 16) + (p0 % 16 >= row + frac ? 1 : 0), end = pass * 16 + row + frac;
        for (let n = 0; n < 20000 && pos() < end; n++) await t.sleep(2);
        return pass;
    };
    const soon = (o, t0) => !!o && o.time >= t0 - 1e-6 && o.time - t0 < .03;
    const onsOf = (midi, track) => ons.filter(o => o.midi === midi && o.track === track);
    const atRow = (o, pass, row) => rowsT[pass * 100 + row] != null && Math.abs(o.time - rowsT[pass * 100 + row]) < 1e-6;
    const offOf = o => offs.find(x => x.id === o.id && x.time >= o.time - 1e-6);
    const p = () => M.pattern(OWN);
    const cell = (r, tr = 0) => p().cells[r * 16 + tr];
    const isNote = (c, m) => !!c && c.note === m;
    const isOff = c => !!c && c.note === M.OFF;
    const clear = (n = 1) => { for (let r = 0; r < 16; r++) for (let tr = 0; tr < n; tr++) p().cells[r * 16 + tr] = null; };
    const edx = c => { const x = c && c.cmds.find(y => y.c === 0x0E && y.v >> 4 === 0xD); return x ? x.v & 15 : -1; };

    /* the EDITOR on its own pattern, 16 rows, empty; the song's tempo 30 (6 ticks a row: 0.5 s); COUNT-IN OFF; REC, PLAY */
    U.pattern = OWN;
    await t.homeKey(3);
    U.edit(() => { const q = p(); q.rows = 16; q.cells = new Array(16 * 16).fill(null); });
    U.editor.row = 0; U.editor.track = 0;
    U.setInst(0xF000);
    const sg = M.songOrKeys(U.song), tempo0 = sg.tempo, ticks0 = sg.ticks, swing0 = sg.swing;
    sg.tempo = 30; sg.ticks = 6; sg.swing = 50;
    M.state.settings["COUNT-IN"] = "OFF";
    await t.tap("REC");
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(80);
    ok(P.take && P.playing && P.what === "loop" && Math.abs(P.rowSeconds() - .5) < 1e-6, "a take of 16 rows of 0.5 s (COUNT-IN OFF)");

    /* 4 ROWS, a key at 10.5, a row and a half before its line 12, held past it, let go at 13.5: heard at its press, not
     * started again at 12 that pass, written there, its off on 14, its end at the release; the next pass plays it from the
     * pattern at 12 */
    M.state.settings.QUANTIZE = "4 ROWS";
    clear();
    const k1 = W[4], m1 = U.midi(k1);
    let pass = await to(10, .5), t0 = P.time();
    t.keyDown(k1);
    const p1 = onsOf(m1, 0).slice();
    await to(13, .5);
    const n1 = onsOf(m1, 0).length;
    let t1 = P.time();
    t.keyUp(k1);
    const e1 = p1.length ? offOf(p1[0]) : null;
    const w12 = isNote(cell(12), m1), w14 = isOff(cell(14));
    await to(12, .3);
    const next = onsOf(m1, 0).filter(o => atRow(o, pass + 1, 12)).length;
    ok(p1.length === 1 && soon(p1[0], t0) && n1 === 1 && w12 && w14 && soon(e1, t1) && next === 1,
       "the keys at once: 4 ROWS, a key at 10.5: heard at its press, not started again at its line 12, written there, its off on 14; " +
       "let go at 13.5: ended there; the next pass plays it from the pattern at 12 (" + [p1.length, n1, w12, w14, !!e1, next] + ")");

    /* a quick tap at 6.5 (its line 8) let go at 6.67, before its row: heard short (started and ended at its press), its note
     * on 8 and its off on 9 written, not started at 8 that pass; a next key on that track at 8.5, before the off's row:
     * heard at once, not cut at 9 (the tap's off stands in for itself), ended at its release at 10.5 */
    clear();
    const kt = W[2], mt = U.midi(kt), k2 = W[6], m2 = U.midi(k2);
    await to(6, .5);
    const tOn = P.time();
    t.keyDown(kt);
    await to(6, .67);
    const tOff = P.time();
    t.keyUp(kt);
    const tp = onsOf(mt, 0).slice(), te = tp.length ? offOf(tp[0]) : null;
    const w8 = isNote(cell(8), mt), w9 = isOff(cell(9));
    await to(8, .5);
    const t8 = onsOf(mt, 0).length;
    t0 = P.time();
    t.keyDown(k2);
    const s2 = onsOf(m2, 0).slice();
    await to(10, .5);
    const cut9 = !!s2.length && offs.some(x => x.id === s2[0].id && x.time < P.time());
    t1 = P.time();
    t.keyUp(k2);
    const e2 = s2.length ? offOf(s2[0]) : null;
    ok(tp.length === 1 && soon(tp[0], tOn) && soon(te, tOff) && w8 && w9 && t8 === 1 &&
       s2.length === 1 && soon(s2[0], t0) && !cut9 && soon(e2, t1) && isOff(cell(11)),
       "... a quick tap at 6.5 let go at 6.67: started and ended at its press, its note on 8 and its off on 9 written, not started at 8 " +
       "that pass; a next key at 8.5: heard at once, not cut at 9 by the tap's off, ended at its release (" + [tp.length, !!te, w8, w9, t8,
       s2.length, !cut9, !!e2] + ")");

    /* REAL TIME: a key at 2.4 heard at its press, its note delay ED2 written, not waited for */
    M.state.settings.QUANTIZE = "REAL TIME";
    clear();
    await to(2, .4); t0 = P.time();
    const nr = onsOf(m1, 0).length;
    t.keyDown(k1);
    const rk = onsOf(m1, 0).slice(nr);
    await to(3, .5);
    t.keyUp(k1);
    ok(rk.length === 1 && soon(rk[0], t0) && isNote(cell(2), m1) && edx(cell(2)) === 2,
       "... REAL TIME: a key at 2.4 heard at its press, written on 2 with ED2 (" + [rk.length, edx(cell(2))] + ")");

    /* a chord at 2.5 (4 ROWS: its line 4), three keys at once: spreading to tracks 00-02, each heard at once on its track,
     * none started again at 4 that pass, all written on 4 */
    M.state.settings.QUANTIZE = "4 ROWS";
    clear(3);
    const kc = [W[7], W[9], W[11]], mc = kc.map(k => U.midi(k)), nc = mc.map((m, j) => onsOf(m, j).length);
    await to(2, .5); t0 = P.time();
    for (const k of kc) t.keyDown(k);
    const at2 = mc.every((m, j) => onsOf(m, j).length === nc[j] + 1 && soon(onsOf(m, j)[nc[j]], t0));
    await to(5, .5);
    const once = mc.every((m, j) => onsOf(m, j).length === nc[j] + 1);
    for (const k of kc) t.keyUp(k);
    const w = mc.every((m, j) => isNote(cell(4, j), m));
    ok(at2 && once && w, "... a chord at 2.5: heard at once on tracks 00-02, none started again at its line 4, written on 4, tracks 00-02 (" +
       [at2, once, w] + ")");

    /* the keys' pressure in a take (fw/tests/ui3_test.c's test_key_pressure, the same steps): held steady, nothing written;
     * pressed harder, its volume alone on its track's next rows, rising; held there, nothing more; eased, back to its
     * velocity; its note at its velocity; the next pass plays them (its track's level up there) */
    M.state.settings.QUANTIZE = "ROW";
    clear();
    {
        const hold = async ms => { for (let e = 0; e < ms; e += 5) { await t.sleep(5); U.pressTick(); } };
        const vols = () => p().cells.map((c, k) => k % 16 === 0 && c && c.note == null && c.vol != null ? (k >> 4) + ":" + c.vol : null).filter(Boolean);
        const kp = W[4], mp = U.midi(kp);
        await to(2, 0);
        t.keyDown(kp, { reading: 600 });
        const vp = U.keyVol(kp);
        await hold(300);
        const v0 = vols().join();
        t.pressure(kp, 470); await hold(250);
        const v1 = vols().join();
        await hold(300);
        const v2 = vols().join();
        t.pressure(kp, 600); await hold(250);
        const v3 = vols(), vs = v3.map(x => +x.split(":")[1]), top = Math.max(...vs), tr = +v3[vs.indexOf(top)].split(":")[0];
        t.keyUp(kp);
        const shape = vs.length >= 2 && vs[vs.length - 1] === vp && top > vp && top <= 127;
        ok(!v0 && v1 && v1 === v2 && shape && isNote(cell(2), mp) && cell(2).vol === vp,
           "the keys' pressure in a take: nothing while held steady (" + (v0 || "none") + "), pressed harder its volume alone on the " +
           "next rows (" + v1 + "), nothing more while held there, eased back to its velocity " + vp + " (" + v3.join() + "); its note at " +
           (cell(2) && cell(2).vol));
        /* each volume's row before it a slide toward it (Axy: 2 of 0-127 a tick on the row's ticks but the first) */
        const rowsOf = v3.map(x => +x.split(":")[0]);
        const slides = rowsOf.filter(r => r > 0 && cell(r - 1) && cell(r - 1).cmds.some(c => c.c === 0x0A)).length;
        ok(slides >= rowsOf.length - 1, "... a volume slide on the row before " + slides + " of its " + rowsOf.length + " volumes (" +
           rowsOf.map(r => r > 0 && cell(r - 1) ? cell(r - 1).cmds.filter(c => c.c === 0x0A).map(c => "A" + c.v.toString(16).padStart(2, "0")).join("") : "").join(",") + ")");
        /* the next pass: its track's level tick by tick from its note's row past the last volume: its largest jump the
         * slide's step or its rounding at a row, not the whole change */
        const lv = [], c0 = A.ctl;
        A.ctl = (id, time, bend, vol, track, glide) => { if (track === 0) lv.push({ time, vol }); return c0(id, time, bend, vol, track, glide); };
        const pa = await to(2, .5);
        await to(Math.min(15, rowsOf[rowsOf.length - 1] + 1), .5);
        A.ctl = c0;
        const t2 = rowsT[pa * 100 + 2], seen = lv.filter(o => o.time >= t2 - 1e-6).map(o => o.vol);
        let jump = 0;
        for (let k = 1; k < seen.length; k++) jump = Math.max(jump, Math.abs(seen[k] - seen[k - 1]));
        const whole = Math.max(...seen) - Math.min(...seen);
        ok(seen.length > 6 && whole > 0.1 && jump < whole / 2.5, "... the next pass: from its note's row its level moves by " +
           jump.toFixed(3) + " a tick at the most, the whole change " + whole.toFixed(3) + " (" + seen.length + " ticks)");
    }

    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    await t.tap("REC");
    ok(!P.take && !P.playing, "the take stopped");
    sg.tempo = tempo0; sg.ticks = ticks0; sg.swing = swing0;
    M.state.settings.QUANTIZE = "ROW";
    A.noteOn = on0; A.noteOff = off0;
};
module.exports.virtual = true;
