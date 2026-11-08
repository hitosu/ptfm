/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The LOOPS tab's preview hears SHIFT (specs/editor.md): the worklet's code itself plays a loops record through the
 * firmware, one loop with one note at its row 0; with SHIFT 0 it sounds at once, with SHIFT 8 (half the length of 16
 * rows, a row 120 ms at TEMPO 125 and 6 ticks) about a second later, with SHIFT 8 written as -8 the same.
 *
 *   node tests/editor/loops_preview_test.js [WASM] */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model, P = ED.player;
const RATE = 44100, Q = 128;

(async () => {
    const bytes = fs.readFileSync(process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm"));
    const fw = await new ED.Firmware(await WebAssembly.compile(bytes)).start(null);
    M.setBuiltins(fw.builtins());
    const L = M.fromRecords(S.parseData(demo()).files, { side: "L", src: "DEMO.PTFMDSK" }).items;
    let Proc = null;
    const g = { sampleRate: RATE, currentTime: 0 };
    class AudioWorkletProcessor { constructor() { this.port = { postMessage: m => this.msgs.push(m), onmessage: null }; this.msgs = []; } }
    new Function("AudioWorkletProcessor", "registerProcessor", "sampleRate", "g", P.SOURCE.replace(/currentTime/g, "g.currentTime"))(
        AudioWorkletProcessor, (n, c) => { Proc = c; }, RATE, g);
    const proc = new Proc();
    await proc.msg({ t: "load", wasm: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) });

    const cart = M.builtins.find(b => b.kind === K.DX7);      /* (a built-in cartridge: the firmware holds it) */
    check(!!cart, "a built-in DX7 cartridge");
    const fx = new Uint8Array(64);
    /* the first tenth of a second the loops sound in, from the start of the play (null: none within secs) */
    function onset(shift, secs) {
        const loops = { tempo: 125, ticks: 6, countIn: 0, legato: 0, swing: 50, grid: 0, quantize: 0, nextMono: 0, selected: 0, length: 16, fx,
            mix: R.MIX_DEFAULT.slice(), loops: [{ cart: cart.num, slot: 0, colour: 0, mono: 0, muted: false, lanes: 1, base: 16, repeat: 1, shift,
            mixer: R.MIX_DEFAULT.slice(), cells: new Map([[R.cellKey(0, 0), { note: 60, ins: -1, vol: 0x7F, cmds: [] }]]) }] };
        const recs = R.packLoops(loops).map((payload, sub) => ({ kind: K.LOOPS, num: 0, sub, ver: 4, payload }));
        const isB = r => r.num >= 0xF0 && R.isCart(r.kind);
        proc.msg({ t: "flash", blocks: ED.flash.image(recs.filter(r => !isB(r)).map(r => Object.assign({ crc: 0 }, r)), { preview: true }) });
        proc.msg({ t: "play", job: { kind: "loops" } });
        const out = [[new Float32Array(Q), new Float32Array(Q)]];
        let acc = 0, n = 0, t = 0;
        for (let f = 0; f < secs * RATE; f += Q) {
            proc.msgs.length = 0;
            proc.process([], out);
            g.currentTime += Q / RATE;
            for (let k = 0; k < Q; k++) acc += out[0][0][k] * out[0][0][k] + out[0][1][k] * out[0][1][k];
            n += Q;
            if (n >= RATE / 20) { if (Math.sqrt(acc / n / 2) > 1e-3) { proc.msg({ t: "stop" }); return t; } acc = 0; n = 0; t += 0.05; }
        }
        proc.msg({ t: "stop" });
        return null;
    }
    const a = onset(0, 3);
    for (let f = 0; f < 3 * RATE; f += Q) { proc.msgs.length = 0; proc.process([], [[new Float32Array(Q), new Float32Array(Q)]]); g.currentTime += Q / RATE; }   /* (the first note dies away) */
    const b = onset(8, 3);
    check(a !== null && a < 0.3, "SHIFT 0: the note sounds at once (" + a + " s)");
    check(b !== null && b > a + 0.7 && b < a + 1.3, "SHIFT 8 (8 rows of 120 ms): the note sounds about a second later (" + b + " s)");
    const sh = R.unpackLoops(R.packLoops({ tempo: 125, ticks: 6, countIn: 0, legato: 0, swing: 50, grid: 0, quantize: 0, nextMono: 0, selected: 0, length: 16, fx,
        mix: R.MIX_DEFAULT.slice(), loops: [{ cart: 1, slot: 0, colour: 0, mono: 0, muted: false, lanes: 1, base: 16, repeat: 1, shift: ED.defs.shiftParse("-8", 16),
        mixer: R.MIX_DEFAULT.slice(), cells: new Map() }] })).loops[0].shift;
    check(sh === 8, "-8 over 16 rows is the same SHIFT as 8 (" + sh + ")");
    done("loops_preview");
    process.exit(0);
})().catch(e => { console.log("FAIL: " + e.stack); process.exit(1); });
