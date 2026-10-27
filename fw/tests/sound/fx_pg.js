/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's effects (playground/dsp-fx.js) on the noise fx_probe.c feeds the firmware's: one effect
 * alone, its knobs given as a setup keeps them (steps), the output's RMS a quarter second at a time.
 *   node fw/tests/sound/fx_pg.js EFFECT KNOB0 ... */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const PG = path.resolve(__dirname, "../../../playground"), SR = 44100;
const NAMES = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK"];
const MIN = { FILTER: [-64, 0], "LO-FI": [2, 1, 0], LIMITER: [-12, 10], DUCK: [0, 0, 20] };
const STEP = { LIMITER: [1, 10], DUCK: [1, 1, 10] };
const DEF = { GATE: [0, 1, 100, 20], FILTER: [0, 30], DRIVE: [30, 50], "LO-FI": [8, 22, 20], CHORUS: [0, 30, 50, 50],
              DELAY: [7, 40, 60, 0, 30], REVERB: [60, 40, 10, 25], LIMITER: [-1, 100], DUCK: [0, 60, 150] };
const e = +(process.argv[2] || 6), name = NAMES[e];
const wk = { console, sampleRate: SR, currentTime: 0, AudioWorkletProcessor: class {}, registerProcessor() {} };
vm.createContext(wk);
const dsp = {};
dsp.window = dsp;
vm.createContext(dsp);
for (const f of ["dsp-core.js", "dsp-dx7.js", "dsp-synth.js", "dsp-chip-tables.js", "dsp-chip.js", "dsp-fx.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), dsp, { filename: f });
vm.runInContext("const E = {};\n" + dsp.PTFM.dsp.map(f => "(" + f.toString() + ")(E);").join("\n") + "\nglobalThis.E_ = E;\n", wk);
const E = wk.E_, BLK = E.BLK;
const knobs = JSON.parse(JSON.stringify(DEF));
process.argv.slice(3).forEach((v, i) => { knobs[name][i] = (MIN[name] ? MIN[name][i] : 0) + +v * (STEP[name] ? STEP[name][i] : 1); });
const on = {};
for (const n of NAMES) on[n] = n === name;
const fx = E.createFx({ gates: [[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]] });
fx.msg({ t: "fx", setup: { routing: "GLOBAL", chain: [name], on, knobs }, mixer: [{ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }],
         base: 0, tempo: 120, gates: [[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]] });
fx.msg({ t: "master", v: .5 });
const tracks = Array.from({ length: E.NTRK }, () => new Float32Array(BLK));
let seed = 12345, acc = 0, n = 0, f = 0;
for (let b = 0; b < 4 * SR / BLK; b++) {
    for (let i = 0; i < BLK; i++, f++) {
        let v = 0;
        if (f < SR) {
            seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
            v = ((seed >>> 16) & 0x7FFF) - 16384;
            v = Math.trunc(Math.trunc(v * 2 * 10362 / 32768) / 2);
        }
        tracks[0][i] = 2 * v / 32768;                       /* (the mixer takes it at half: the firmware's v) */
    }
    const L = new Float32Array(BLK), R = new Float32Array(BLK);
    fx.process(tracks, L, R, b * BLK / SR);
    for (let i = 0; i < BLK; i++) {
        acc += L[i] * L[i] + R[i] * R[i];
        if (++n === 11025) { console.log((10 * Math.log10(acc / (2 * n) + 1e-20)).toFixed(2)); acc = 0; n = 0; }
    }
}
