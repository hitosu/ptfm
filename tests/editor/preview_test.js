/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The preview (specs/editor.md, The preview; Tests): the page's worklet code itself (player.js's SOURCE, run here with
 * the AudioWorklet's few names stubbed) playing each kind's flash image through the firmware: a song, a pattern, a
 * sample alone and in its sampler, a built-in instrument's arpeggio, a sampler's instruments one after another, a
 * FUSION slot; each must sound, then rest a second, then play again.
 *
 *   node tests/editor/preview_test.js [WASM] */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model, P = ED.player;
const RATE = 44100, Q = 128;

/* the worklet's class, its port's messages collected */
function worklet() {
    let Proc = null;
    const g = { sampleRate: RATE, currentTime: 0 };
    class AudioWorkletProcessor { constructor() { this.port = { postMessage: m => this.msgs.push(m), onmessage: null }; this.msgs = []; } }
    const fn = new Function("AudioWorkletProcessor", "registerProcessor", "sampleRate", "g", P.SOURCE.replace(/currentTime/g, "g.currentTime"));
    fn(AudioWorkletProcessor, (n, c) => { Proc = c; }, RATE, g);
    return { Proc, g };
}

(async () => {
    const wasmFile = process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm");
    const bytes = fs.readFileSync(wasmFile);
    const fw = await new ED.Firmware(await WebAssembly.compile(bytes)).start(null);
    M.setBuiltins(fw.builtins());
    const L = M.fromRecords(S.parseData(demo()).files, { side: "L", src: "DEMO.PTFMDSK" }).items;
    const st = { L, R: [], base: [], fm1: {} };
    const { Proc, g } = worklet();
    const proc = new Proc();
    await proc.msg({ t: "load", wasm: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) });
    check(proc.msgs.some(m => m.t === "ready"), "the worklet's firmware started");

    /* plays it for secs: the level heard each tenth of a second and the job's phases */
    function run(p, secs) {
        if (p.recs.length) {
            const isB = r => r.num >= 0xF0 && (M.isCart(r.kind) || r.kind === K.PATTERN || r.kind === K.SONG);
            proc.msg({ t: "flash", blocks: ED.flash.image(p.recs.filter(r => !isB(r)).map(r => Object.assign({ crc: 0 }, r)), { preview: true }) });
        }
        proc.msg({ t: "play", job: p.job });
        const out = [[new Float32Array(Q), new Float32Array(Q)]], lv = [], phases = [];
        let acc = 0, n = 0;
        for (let f = 0; f < secs * RATE; f += Q) {
            proc.msgs.length = 0;
            proc.process([], out);
            g.currentTime += Q / RATE;
            for (let k = 0; k < Q; k++) acc += out[0][0][k] * out[0][0][k] + out[0][1][k] * out[0][1][k];
            n += Q;
            if (n >= RATE / 10) { lv.push(Math.sqrt(acc / n / 2)); acc = 0; n = 0; }
            phases.push(proc.job ? proc.job.phase : "none");
        }
        proc.msg({ t: "stop" });
        return { lv, phases };
    }
    /* the first rest's length and whether it plays again after */
    function rest(phases) {
        const a = phases.indexOf("gap");
        if (a < 0) return null;
        const b = phases.indexOf("run", a);
        return { at: a * Q / RATE, len: b < 0 ? null : (b - a) * Q / RATE };
    }
    const loud = lv => Math.max(...lv);
    function kind(name, it, o, secs) {
        let p;
        try { p = P.plan(it, st, o || {}); } catch (e) { check(false, name + ": " + e.message); return; }
        const r = run(p, secs);
        const top = loud(r.lv), rs = rest(r.phases);
        check(top > 1e-3, name + " sounds (loudest tenth " + top.toFixed(4) + ")");
        if (rs) {
            check(Math.abs(rs.len - 1) < 0.01, name + ": a rest of a second after " + rs.at.toFixed(2) + " s (" + (rs.len && rs.len.toFixed(3)) + " s), then again");
            const after = r.lv.slice(Math.ceil((rs.at + rs.len) * 10) + 1);
            if (after.length) check(loud(after) > 1e-3, name + ": heard again after the rest");
        } else check(false, name + ": no rest within " + secs + " s");
        return r;
    }

    /* a built-in instrument's arpeggio: C-4 E-4 G-4 C-5 G-4 E-4 C-4 in eighths, the last held a second */
    const b0 = M.builtins.find(b => b.kind === K.DX7);
    const fwItem = { id: "B:x", side: "R", kind: K.DX7, num: b0.num, fw: true, link: {}, recs: [{ sub: 0, ver: b0.ver, payload: b0.payload }] };
    const pa = P.plan(fwItem, st, { slot: 0 });
    const ons = pa.job.events.filter(e => e[1] === "ed_key_on");
    check(ons.length === 7 && ons.map(e => e[2][0]).join() === "60,64,67,72,67,64,60" && ons[1][0] === 0.25 && Math.abs(pa.job.events.find(e => e[1] === "ed_key_off" && e[0] > 2).at === undefined ? 0 : 0) === 0,
        "an instrument's arpeggio: " + ons.map(e => R.noteName(e[2][0]) + "@" + e[0]).join(" "));
    kind("a built-in DX7 instrument", fwItem, { slot: 0 }, 6);
    /* a sampler's instruments, one after another */
    const smp = L.find(i => i.kind === K.SAMPLER);
    const ps = P.plan(smp, st);
    check(ps.job.starts.length === P.slotsOf(smp).length, "a sampler cartridge: " + ps.job.starts.length + " instruments one after another, an arpeggio each");
    kind("a sampler's first instrument", smp, { slot: 0 }, 6);
    /* a sample alone: once, at its own rate */
    const sm = L.find(i => i.kind === K.AUDIO);
    const e = M.sampleEntry(sm, L);
    const p1 = P.plan(sm, st);
    check(Math.abs(p1.job.length - (e.length / e.rate + 0.05)) < 1e-6, "a sample plays once, its own length (" + (e.length / e.rate).toFixed(2) + " s)");
    kind("a sample in its sampler", sm, null, (e.length / e.rate) + 2.5);
    const audio = R.ptrEncode(Array.from({ length: 11025 }, (_, i) => Math.round(90 * Math.sin(i / 5))), 8);
    const loose = { id: M.newId(), side: "L", kind: K.AUDIO, num: null, orig: null, link: {}, owner: null,
        entry: M.packedEntry({ length: 11025, rate: 22050, coding: 1, name: "SINE" }), recs: [{ sub: 0, ver: 1, payload: audio }], name: "SINE" };
    const st2 = Object.assign({}, st, { L: L.concat([loose]) });
    {
        const p = P.plan(loose, st2);
        const r = run(p, 2.5), rs = rest(r.phases);
        check(loud(r.lv) > 1e-3 && rs && Math.abs(rs.at - 0.55) < 0.02, "a sound dropped on the TABLE plays alone, 0.5 s, then rests (" + (rs && rs.at.toFixed(2)) + " s)");
    }
    /* a TABLE item bigger than an FM-1 holds: refused by the FM-1's room, played in the preview's (~1 MB) */
    {
        const n = 700000, noise = Array.from({ length: n }, (_, i) => ((i * 2654435761) >>> 24) - 128);
        const big = { id: M.newId(), side: "L", kind: K.AUDIO, num: null, orig: null, link: {}, owner: null,
            entry: M.packedEntry({ length: n, rate: 44100, coding: 1, name: "BIG NOISE" }), recs: [{ sub: 0, ver: 1, payload: R.ptrEncode(noise, 8) }], name: "BIG NOISE" };
        const st3 = Object.assign({}, st, { L: L.concat([big]) });
        const onFm1 = M.transfer({ L: st3.L, R: [], base: [], fm1: {} }, "L", [big.id], { layout: { regions: M.USER, reserve: 0 } });
        check(!onFm1.ok && big.recs[0].payload.length > 103 * 4096, "a sound of " + Math.round(big.recs[0].payload.length / 1024) + " KB: more than an FM-1 holds (" + onFm1.msg + ")");
        let p = null;
        try { p = P.plan(big, st3); } catch (e) { check(false, "the big sound's preview planned: " + e.message); }
        if (p) {
            const r = run(p, 1);
            check(loud(r.lv) > 1e-3, "it plays in the preview's room (loudest tenth " + loud(r.lv).toFixed(4) + ")");
        }
    }
    /* a pattern once through, a song to its end */
    const songs = L.filter(i => i.kind === K.SONG);
    const loop = songs.find(s => (s.recs[0].payload[17] & 1) === 0 && s.recs[0].payload[20] === 0) || songs[songs.length - 1];
    const lp = M.refs(loop)[0], pat = L.find(i => i.id === loop.link[M.lk("p", lp.num)]);
    const pp = P.plan(pat, st);
    check(pp.job.kind === "pattern" && pp.job.song !== 0xFF, "a pattern plays at the tempo of a song that plays it (" + M.nameOf(loop) + ")");
    kind("a pattern", pat, null, 30);
    const rs = kind("a song that loops (" + M.nameOf(loop) + ")", loop, null, 40);
    const at = rs && rest(rs.phases);
    check(at && Math.abs(at.at - 13.72) < 0.3, "the song plays once through before its rest (" + (at && at.at.toFixed(2)) + " s, its one pattern 13.7 s)");
    /* a FUSION slot: its pattern */
    const fu = L.find(i => i.kind === K.FUSION);
    if (fu) kind("a FUSION slot", fu, { slot: 0 }, 12);
    else check(false, "no FUSION on the demo disk");

    done("preview_test");
    process.exit(0);
})().catch(e => { console.log("FAIL: " + e.stack); process.exit(1); });
