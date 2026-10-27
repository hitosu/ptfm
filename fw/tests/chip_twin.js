/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The CHIP engine's twin test (fw/tests/run_chip.sh): the playground's engine (playground/dsp-chip.js, with
 * dsp-chip-tables.js) renders every scenario fw/tests/chip_scenarios.py wrote, as fw/tests/chip_test.c renders
 * them with the firmware's engine, and each must come out the same: the hash of every sample, the peak, the blocks
 * it sounded, the loudest 300 ms.
 *
 *   node fw/tests/chip_twin.js SCENARIOS C_RESULTS        (exit code: the scenarios that differ, at most 100) */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "../..");
const [scenFile, cFile] = process.argv.slice(2);
const ctx = { console };
ctx.window = ctx;
vm.createContext(ctx);
for (const f of ["playground/dsp-chip-tables.js", "playground/dsp-chip.js"])
    vm.runInContext(fs.readFileSync(path.join(ROOT, f), "utf8"), ctx, { filename: f });
const E = { params: {}, engines: {}, SR: 44100 };
for (const fn of ctx.PTFM.dsp) fn(E);
const C = E.chip;
C.setRate(44100);

const scen = fs.readFileSync(scenFile, "utf8").trim().split("\n"), want = fs.readFileSync(cFile, "utf8").trim().split("\n");
let bad = 0;
if (process.argv[4] === "--tuning") {    /* chip_test -t's lines: chip_tuning at C-1 ... C-9 of each scenario's instrument */
    scen.forEach((line, n) => {
        const slot = Array.from({ length: 28 }, (_, i) => parseInt(line.substr(2 * i, 2), 16)), p = E.chipUnpack(slot), got = [];
        for (let note = 24; note <= 120; note += 12) { const t = C.tuning(p, note); got.push(t.reg, t.c10); }
        if (got.join(" ") !== want[n] && bad++ < 10) console.log(`tuning differs: scenario ${n}: JS ${got.join(" ")}, C ${want[n]}`);
    });
    console.log(bad ? `chip tuning: ${bad} of ${scen.length} differ` : `chip tuning: ${scen.length} instruments the same`);
    process.exit(Math.min(bad, 100));
}
scen.forEach((line, n) => {
    const [hex, ...rest] = line.trim().split(/\s+/), [note, blocks, off, bb, bend, pb, code, val, pf, pt, ne] = rest.map(Number);
    const xs = [];
    for (let i = 0; i < ne && i < 8; i++) xs.push(rest.slice(11 + 3 * i, 14 + 3 * i).map(Number));
    const slot = Array.from({ length: 28 }, (_, i) => parseInt(hex.substr(2 * i, 2), 16));
    const v = {};
    C.noteOn(v, E.chipUnpack(slot), note);
    let h = 2166136261, peak = 0, act = 0;
    const sig = [];
    for (let b = 0; b < blocks; b++) {
        const out = new Int32Array(64);
        if (b === off) C.noteOff(v);
        if (b === bb) v.bend64 = bend;
        if (b === pb) C.param(v.p, v, code, val);
        for (const [a, c, x] of xs) if (b === a) C.param(v.p, v, c, x);
        if (b >= pf && b < pt) { if (C.active(v)) C.skip(v); }  /* (parked: its time goes on unheard) */
        else if (C.active(v)) { C.block(v, out); act = b + 1; }
        for (let i = 0; i < 64; i++) {
            h = Math.imul((h ^ out[i]) >>> 0, 16777619) >>> 0;
            const a = Math.abs(out[i]);
            if (a > peak) peak = a;
            sig.push(out[i]);
        }
    }
    let best = 0;
    for (let s = 0; s + 13230 <= sig.length; s += 1024) {
        let e = 0;
        for (let i = s; i < s + 13230; i++) e += sig[i] * sig[i];
        if (e > best) best = e;
    }
    const got = `${h.toString(16).padStart(8, "0")} ${peak} ${act} ${Math.floor(Math.sqrt(Math.floor(best / 13230)))}`;
    if (got !== want[n]) {
        if (bad < 10) console.log(`differs: scenario ${n} (${line.slice(0, 20)}... note ${note}): JS ${got}, C ${want[n]}`);
        bad++;
    }
});
console.log(bad ? `chip twin: ${bad} of ${scen.length} scenarios differ` : `chip twin: ${scen.length} scenarios equal sample for sample`);
process.exit(Math.min(bad, 100));
