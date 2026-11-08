/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* One RECORD LIVE session as data (fw/tests/live_session.txt, its steps there) on the playground, on the harness's virtual
 * clock with the worklet's pool of voices running; the song it makes as text, in the form fw/tests/ui3_test.c's --session
 * writes it, into build/playground/live_twin.txt: fw/tests/run_ui3.sh compares the two.
 *   node tests/playground/harness.js tests/playground/live_twin.js [out dir] */
"use strict";
const fs = require("fs"), path = require("path");
const ROOT = path.resolve(__dirname, "../..");
const hex = (v, n) => v.toString(16).toUpperCase().padStart(n, "0");
const dec = (v, n) => String(v).padStart(n, "0");

/* the song as text: its header, the mixes that are not the default, each pattern's cells that hold anything */
function dump(t, n) {
    const { U, M } = t, S = M.state.songs[n], o = [];
    if (!S) return "NO SONG\nTOAST " + (U.toastNow() || "-") + "\n";
    o.push("SONG " + hex(n, 2) + " " + S.name + " TEMPO " + S.tempo + " TICKS " + S.ticks + " RPB " + S.rpb + " SWING " + S.swing + " " + S.atEnd + " POSITIONS " + S.order.length);
    S.mixer.forEach((m, k) => {
        if (m.lvl === 127 && m.pan === 128 && !m.cho && !m.dly && !m.rev && !m.dck) return;
        o.push("MIX " + dec(k, 2) + " " + [m.lvl, m.pan, m.cho, m.dly, m.rev, m.dck].join(" "));
    });
    S.order.forEach((num, pos) => {
        const p = M.pattern(num);
        o.push("PATTERN " + pos + " ROWS " + p.rows + " TRACKS " + p.tracks);
        for (let r = 0; r < p.rows; r++) for (let k = 0; k < 16; k++) {
            const c = p.cells[r * 16 + k];
            if (!c || (c.note == null && c.inst == null && c.vol == null && !c.cmds.length)) continue;
            o.push(pos + " " + dec(r, 3) + " " + dec(k, 2) + " " + (c.note === M.OFF ? "OFF" : c.note == null ? "---" : dec(c.note, 3)) + " " +
                   (c.inst == null ? "----" : hex(c.inst, 4)) + " " + (c.vol == null ? "--" : hex(c.vol, 2)) +
                   c.cmds.map(x => " " + hex(x.c, 2) + "=" + hex(x.v, 2)).join(""));
        }
    });
    o.push("TOAST " + (U.toastNow() || "-"));
    return o.join("\n") + "\n";
}

module.exports = async t => {
    const { U, M, P } = t, LS = () => M.state.loops;
    const lines = fs.readFileSync(path.join(ROOT, "fw/tests/live_session.txt"), "utf8").split("\n");
    await t.sound();
    await t.sleep(100);
    P.pause();
    await t.sleep(100);
    await t.homeKey(22);
    const n = M.freeSong();
    /* until the replay's tick `tick` (a step that finds it gone by comes too late: the session's times are wrong) */
    const at = async tick => {
        for (let i = 0; i < 200000 && !(P.what === "loops" && P.playing && P.lpTn() >= tick); i++) await t.sleep(1);
        if (P.lpTn() > tick + 1) t.ok(false, "the session: its step at tick " + tick + " on time (" + P.lpTn() + ")");
    };
    const fresh = (rows, tempo) => {
        U.edit(() => {
            const j = LS();
            j.list = []; j.rows = rows; j.tempo = tempo; j.quantize = "ROW"; j.legato = "GLIDE"; j.countIn = "OFF"; j.nextMono = false;
            for (let k = 0; k < 16; k++) j.mixer[k] = Object.assign({}, j.nextMix);
        });
        P.mutesChanged();
    };
    const loop = (inst, base, reps, mono, shift, cells) => {
        const L = { inst: parseInt(inst, 16), col: LS().list.length % 12, mono: !!+mono, lanes: 1, base: +base, repeats: +reps, muted: false, soloed: false, shift: +shift,
                    cells: new Array(+base * 4).fill(null) };
        for (const c of cells) {
            const [r, l, nt, v] = c.split(":");
            L.cells[+r * 4 + +l] = nt === "off" ? M.newCell(M.OFF, null, null) : M.newCell(+nt, null, v == null ? 100 : parseInt(v, 16));
            L.lanes = Math.max(L.lanes, +l + 1);
        }
        U.edit(() => { const j = LS(); j.mixer[j.list.length] = { lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }; j.list.push(L); });
        P.mutesChanged();
    };
    for (const raw of lines) {
        const w = raw.replace(/#.*/, "").trim().split(/\s+/).filter(Boolean);
        if (!w.length) continue;
        if (w[0].includes(".")) { const [r, k] = w.shift().split("."); await at(r * 6 + +k); }
        const [v, a, b] = w;
        if (v === "fresh") fresh(+a, +b);
        else if (v === "loop") loop(a, b, w[3], w[4], w[5], w.slice(6));
        else if (v === "select") await t.turn("SELECT", +a - U.screen.state().sel);
        else if (v === "wait") await t.sleep(+a);
        else if (v === "homekey") await t.homeKey(+a);
        else if (v === "down") t.down(a);
        else if (v === "up") t.up(a);
        else if (v === "keydown") t.keyDown(+a);
        else if (v === "keyup") t.keyUp(+a);
        else if (v === "turn") await t.turn(a, +b);
        else if (v === "menu") { const m = U.top(), it = w.slice(1).join(" "); m.sel = m.items.indexOf(it); }
        else if (v === "heard" || v === "unheard") {
            t.ok(t.PTFM.audio.stats.voices.some(x => x.midi === +a && !x.rel) === (v === "heard"), "the session: note " + a + " " + v);
        } else if (v === "end") {
            fs.writeFileSync(path.join(ROOT, "build/playground/live_twin.txt"), dump(t, n));
            P.pause();
        } else t.ok(false, "live_session.txt: " + raw);
        await t.sleep(1);
    }
    t.ok(!U.screen.state().song, "the session's song ended");
};
module.exports.virtual = true;
