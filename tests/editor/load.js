/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor's scripts in Node, as the page loads them: each adds to the global ED; seed.js is the install page's
 * module (fw/web/seed.js). wasm() instantiates the editor's WebAssembly (build/editor/wasm/ptfm.wasm). */
"use strict";
const fs = require("fs"), path = require("path");
const ROOT = path.join(__dirname, "..", "..");
global.ED = global.ED || {};
global.PTFM_seed = require(path.join(ROOT, "fw", "web", "seed.js"));
const FILES = ["util.js", "defs.js", "records.js", "model.js", "flash.js", "cmds.js", "conv_syx.js", "conv_sound.js", "optimize.js", "ibxm_load.js",
    "conv_mod.js", "conv_midi.js", "fm1.js", "drops.js", "player.js"];
for (const f of FILES) {
    const p = path.join(ROOT, "editor", "js", f);
    if (fs.existsSync(p)) require(p);
}
function wasm(file) {
    let fake = 0;
    const inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(file || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm"))),
        { env: { web_clock_us: () => (fake += 1) } });
    return inst.exports;
}
const demo = () => new Uint8Array(fs.readFileSync(path.join(ROOT, "dist", "firmware", "demo.ptfmdsk")));
/* records looked at by object: "S:00" a song, "P:03" a pattern (all its parts), "C:0A" a cartridge (its kind by its
 * number), "A:0A.03" a sample's audio */
function lib(recs) {
    const R = global.ED.rec, H = v => R.hex2(v), objs = new Map();
    const idOf = r => R.isCart(r.kind) ? "C:" + H(r.num) : r.kind === R.KIND.PATTERN ? "P:" + H(r.num) : r.kind === R.KIND.SONG ? "S:" + H(r.num)
        : r.kind === R.KIND.AUDIO ? "A:" + H(r.num) + "." + H(r.sub) : "K" + r.kind;
    for (const r of recs) { const id = idOf(r); if (!objs.has(id)) objs.set(id, { id, kind: r.kind, recs: [] }); objs.get(id).recs.push(r); }
    return {
        objects: () => objs,
        get: (kind, num, sub) => recs.find(r => r.kind === kind && r.num === num && r.sub === sub) || null,
        decode(id) {
            const o = objs.get(id);
            if (!o) return null;
            const parts = o.recs.slice().sort((a, b) => a.sub - b.sub).map(r => r.payload);
            if (o.kind === R.KIND.SONG) return R.unpackSong(parts[0]);
            if (o.kind === R.KIND.PATTERN) return R.unpackPattern(parts);
            if (R.isCart(o.kind)) return R.unpackCart(o.kind, parts[0]);
            return null;
        }
    };
}
let fails = 0, checks = 0;
function check(ok, what) {
    checks++;
    if (!ok) { fails++; console.log("FAIL: " + what); }
    else if (process.env.VERBOSE) console.log("ok: " + what);
}
function done(name) {
    console.log(`${name}: ${checks - fails} of ${checks} checks passed`);
    if (fails) process.exit(1);
}
module.exports = { ED: global.ED, S: global.PTFM_seed, ROOT, wasm, demo, lib, check, done };
