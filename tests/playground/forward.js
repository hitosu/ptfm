/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's engines' forward work (a ghost coming back worked to where its note would be,
 * specs/ui/screens/settings.md#coming-back) against their own steps, as the firmware's fw/tests/sound/forward_test.c
 * checks its engines: a note held h blocks of 64 frames (and let go after h, then r more) stepped as it renders, against
 * the same note started fresh and worked forward over those blocks, with a budget large or a few steps a call (called
 * again till done). The DX7's six envelopes, pitch envelope and LFO; the Juno-106's envelope, LFO, delay, cutoff, VCA and
 * width; the sampler's envelope, and its place by E.samp.go against render's walk; the CHIP's whole voice but its
 * oscillators' phases (against its block step skip()), its end by endBlocks, its loudness by heard, and walks with its
 * parameters turned between calls.
 *   node tests/playground/forward.js */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "../.."), PG = path.join(ROOT, "playground"), SR = 44100;

const page = { console, performance, structuredClone, atob, setInterval: () => 0 };
page.window = page;
vm.createContext(page);
for (const f of ["fonts.js", "content.js", "draw.js", "model.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), page, { filename: f });
const M = page.PTFM.model;
M.demoDisk();
let Proc = null;
const wk = { console, sampleRate: SR, currentTime: 0, Math,
             AudioWorkletProcessor: class { constructor() { this.port = { postMessage: () => {}, onmessage: null }; } },
             registerProcessor: (n, c) => { Proc = c; } };
vm.createContext(wk);
const dsp = {};
dsp.window = dsp;
vm.createContext(dsp);
for (const f of ["dsp-core.js", "dsp-dx7.js", "dsp-synth.js", "dsp-chip-tables.js", "dsp-chip.js", "dsp-fx.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), dsp, { filename: f });
vm.runInContext("var E = {};\n" + dsp.PTFM.dsp.map(f => "(" + f.toString() + ")(E);").join("\n") + "\nregisterProcessor('ptfm', E.Proc); this.E = E;", wk, { filename: "worklet.js" });
const E = wk.E, proc = new Proc();
M.state.samples.forEach((s, n) => { if (s) proc.samples[n] = { data: Float32Array.from(M.audio[s.audio], v => v / 128), rate: s.rate }; });

let failed = 0;
const fail = s => { if (failed++ < 20) console.log("FAIL " + s); };
const near = (a, b, tol) => Math.abs(a - b) <= tol * Math.max(1, Math.abs(b));
const instP = ref => { const c = M.cart(ref >> 8), i = M.inst(ref); return c.type === "SAMPLER" ? Object.assign({ key: M.sample(i.p.sample) ? i.p.sample : null }, i.p) : i.p; };
const refs = (type, every) => { const out = []; for (const c of M.state.carts) if (c.type === type) c.slots.forEach((s, i) => { if (s) out.push(c.id << 8 | i); }); return out.filter((r, k) => k % every === 0); };
/* held h blocks (-1: none let go), or held h and let go r more */
const AGES = [[1, -1], [40, -1], [3000, -1], [40, 20], [5, 600], [2000, 300]];
const counts = {};

/* the DX7: its blocks of 64 frames */
{
    const eng = E.engines.DX7;
    let n = 0;
    for (const ref of refs("DX7", 6)) for (const [h, r] of AGES) for (const budget of [1e9, 3]) {
        const p = instP(ref), A = { midi: 60, bend: 0, rel: false }, B = { midi: 60, bend: 0, rel: false };
        eng.start(A, p, 60, {}, proc); eng.start(B, p, 60, {}, proc);
        B.lfo.ph = A.lfo.ph;
        for (let k = 0; k < h; k++) eng.block(A);
        if (r >= 0) { eng.release(A); A.rel = true; for (let k = 0; k < r; k++) eng.block(A); }
        const w = { on: h, off: r }, st = { n: budget };
        let calls = 0;
        while (!eng.forward(B, w, st)) { st.n = budget; if (++calls > 1e6) break; }
        n++;
        const what = `DX7 ${M.refText(ref)} held ${h}${r >= 0 ? ", let go " + r : ""}, ${budget > 1e8 ? "a large budget" : budget + " steps a call"}`;
        for (let k = 0; k < 6; k++) {
            const a = A.ops[k].e, b = B.ops[k].e;
            if (a.level !== b.level || a.ix !== b.ix || a.st !== b.st || a.down !== b.down) { fail(`${what}: OP${6 - k}'s envelope ${a.level} ${a.ix} | ${b.level} ${b.ix}`); break; }
        }
        if (A.pe.ix !== B.pe.ix || !near(A.pe.level, B.pe.level, 1e-6)) fail(`${what}: its pitch envelope ${A.pe.level} ${A.pe.ix} | ${B.pe.level} ${B.pe.ix}`);
        if (!near(A.lfo.t, B.lfo.t, 1e-9) || !near(A.lfo.ph, B.lfo.ph, 1e-9)) fail(`${what}: its LFO ${A.lfo.t} ${A.lfo.ph} | ${B.lfo.t} ${B.lfo.ph}`);
    }
    counts.DX7 = n;
}
/* the Juno-106: its loop's ticks, stepped by rendering whole 16-frame chunks */
{
    const eng = E.engines.JUNO, buf = new Float32Array(16 * 4096);
    const render = (v, frames) => { while (frames > 0) { const f = Math.min(frames, buf.length); buf.fill(0, 0, f); eng.render(v, buf, 0, f, proc); frames -= f; } };
    const KEPT = ["env", "envState", "lfoAcc", "lfoRising", "lfoPol", "holdAcc", "inHold", "rampAcc", "lfoAmp", "dac", "vcaT", "pw"];
    let n = 0;
    for (const ref of refs("JUNO", 3)) for (const [h, r] of AGES) for (const budget of [1e9, 2]) {
        const p = instP(ref), A = { midi: 60, bend: 0, rel: false }, B = { midi: 60, bend: 0, rel: false };
        eng.start(A, p, 60, {}, proc); eng.start(B, p, 60, {}, proc);
        const fOn = h * 64, fOff = r >= 0 ? fOn + r * 64 : fOn;
        render(A, fOn);
        if (r >= 0) { eng.release(A); render(A, r * 64); }
        const on = eng.units(fOn, true), all = eng.units(fOff, false);
        const w = { on: Math.min(on, all), off: r >= 0 ? all - Math.min(on, all) : -1 }, st = { n: budget };
        let calls = 0;
        while (!eng.forward(B, w, st)) { st.n = budget; if (++calls > 1e6) break; }
        n++;
        for (const k of KEPT) if (A[k] !== B[k] && !(typeof A[k] === "number" && near(A[k], B[k], 1e-9))) {
            fail(`JUNO ${M.refText(ref)} held ${h}${r >= 0 ? ", let go " + r : ""}, budget ${budget}: ${k} ${A[k]} | forward ${B[k]}`);
            break;
        }
    }
    counts.JUNO = n;
}
/* the sampler: its envelope's 64-frame steps; its place by E.samp.go against render's, bent 0, +7.3, -12 semitones */
{
    const eng = E.engines.SAMPLER, buf = new Float32Array(64 * 1024);
    let n = 0, places = 0;
    for (const ref of refs("SAMPLER", 1)) for (const [h, r] of AGES) for (const bend of [0, 7.3, -12]) {
        const p = instP(ref), s = proc.samples[p.key];
        if (!s) continue;
        const A = { midi: 60, bend, rel: false };
        eng.start(A, p, 60, {}, proc);
        const pl = E.samp.start(p, s, 0), P = E.samp.facts(p, s), inc = E.samp.inc(p, s, 60, bend);
        let alive = true;
        const go = (frames, held) => {
            let left = frames;
            while (left > 0 && alive) { const f = Math.min(left, buf.length); alive = eng.render(A, buf, 0, f, proc) || false; left -= f; }
            E.samp.go(pl, P, inc, frames, held);
        };
        go(h * 64, true);
        if (r >= 0) { eng.release(A); go(r * 64, false); }
        const what = `SAMPLER ${M.refText(ref)} held ${h}${r >= 0 ? ", let go " + r : ""}, bent ${bend}`;
        for (const budget of [1e9, 1]) {                /* (whole, and a step a call called again till done) */
            const B = { midi: 60, bend, rel: false }, w = { on: h, off: r }, st = { n: budget };
            eng.start(B, p, 60, {}, proc);
            let calls = 0;
            while (!eng.forward(B, w, st)) { st.n = budget; if (++calls > 1e6) break; }
            n++;
            const a = A.env, b = B.env;
            if (!A.dead && alive && (a.level !== b.level || a.ix !== b.ix || a.st !== b.st))
                fail(`${what}, ${budget > 1e8 ? "a large budget" : "a step a call"}: its envelope ${a.level} ${a.ix} | ${b.level} ${b.ix}`);
        }
        const ended = !alive && !E.opEnv.done(A.env);   /* (ended by its place, its envelope still going) */
        if (alive) {
            places++;
            if (pl.ended || Math.abs(pl.pos - A.pos) > 1e-3 * Math.max(1, A.pos) || pl.dir !== A.dir || pl.looping !== A.looping)
                fail(`${what}: its place ${A.pos.toFixed(3)} ${A.dir} ${A.looping} | walked ${pl.pos.toFixed(3)} ${pl.dir} ${pl.looping} ${pl.ended}`);
        } else if (ended && !pl.ended) fail(`${what}: ended by its place, the walk not (${pl.pos} of ${P.end})`);
    }
    counts.SAMPLER = n;
    counts.places = places;
}
/* the CHIP: the F9 cartridge's instruments (every third) and some at random (every field, the tables, a sweep, the PSG
 * envelope, the step envelope and vibrato), a third of them bent; against its block step skip() */
{
    const C = E.chip, W = E.engines.CHIP, T = E.CHIPT;
    W.start({}, M.inst(0xF900).p, 60, {}, proc);        /* (its tables made) */
    let seed = 20261115;
    const rnd = n => {                                  /* (a fixed-seed generator: mulberry32) */
        seed = (seed + 0x6D2B79F5) | 0;
        let t = seed;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return Math.floor(((t ^ (t >>> 14)) >>> 0) / 4294967296 * n);
    };
    const inst = [];
    for (const ref of refs("CHIP", 3)) inst.push({ name: M.refText(ref), p: JSON.parse(JSON.stringify(M.inst(ref).p)), bend: 0 });
    for (let i = 0; i < 25; i++) {
        const k = i % 5, s = Array.from({ length: 28 }, () => rnd(256)), p = E.chipUnpack(s);
        if (k && (k === 1 || rnd(2))) for (let j = 0; j < 5; j++) { p.tab[j] = rnd(5) ? 1 + rnd(T.TAB_COUNT[j]) : 0; p.spd[j] = 1 + rnd(4); }
        if (k === 2) { p.osc = rnd(4); p.sweep = 1 + rnd(2); p.sw_shift = rnd(8); p.sw_pace = rnd(8); p.clock = rnd(7); }
        else if (k === 3) { p.env = 3; p.psg_shape = rnd(16); p.psg_period = rnd(2) ? 1 + rnd(40) : 1 + rnd(65535); }
        else if (k === 4) { p.env = 1 + rnd(2); p.env_step = rnd(8); p.vib_depth = 2 * rnd(32); p.vib_delay = 2 * rnd(32); p.vib_speed = 1 + rnd(4); p.release = rnd(16); }
        inst.push({ name: `a random one ${i} (kind ${k})`, p, bend: rnd(3) ? 0 : (rnd(2) ? 1 : -1) * rnd(36 * 64) });
    }
    const clone = v => Object.assign({}, v, { p: Object.assign({}, v.p, { tab: v.p.tab.slice(), spd: v.p.spd.slice() }), tp: v.tp.slice(), tc: v.tc.slice(), tail: v.tail.slice() });
    const start = (ins, note) => { const v = {}; C.noteOn(v, ins.p, note); if (ins.bend) v.bend64 = ins.bend; return v; };
    /* what the forward work does not keep: the oscillator's, the gate's and the noise's phases, the band-limited sum and
     * its filters */
    const UNKEPT = ["step", "sq_step", "bz_r", "lfsr", "nbit", "nx_o", "nx_q", "nx_n", "last", "acc", "tail", "hx0", "hy0", "hx1", "hy1", "ly"];
    const same = (x, y) => Array.isArray(x) || ArrayBuffer.isView(x) ? x.length === y.length && x.every((e, i) => e === y[i])
                         : typeof x === "object" && x ? JSON.stringify(x) === JSON.stringify(y) : x === y;
    const diff = (a, b) => {                            /* (once either ended, only that both did; else every kept field) */
        if (a.dead || b.dead) return a.dead && b.dead ? null : a.dead ? "its steps ended it, the forward work not" : "the forward work ended it, its steps not";
        for (const k of new Set(Object.keys(a).concat(Object.keys(b)))) if (!UNKEPT.includes(k) && !same(a[k], b[k])) return `${k}: steps ${JSON.stringify(a[k])}, forward ${JSON.stringify(b[k])}`;
        return null;
    };
    const fwd = (ins, note, on, off, small) => {
        const v = start(ins, note), w = { on, off };
        let calls = 0;
        while (!C.forward(v, w, { n: small ? 1 + rnd(3) : 1 << 30 }) && ++calls < 1e6);
        return v;
    };
    const endCheck = (a, what) => {                     /* (endBlocks, held and let go now, against the steps a copy ends in) */
        if (a.dead) return;
        for (const held of [true, false]) {
            const e = C.endBlocks(a, held), c = clone(a);
            if (!held && c.gate) C.noteOff(c);
            if (e === 0) { if (!c.dead) fail(`${what}: endBlocks 0 (${held ? "held" : "let go now"}) but not ended`); continue; }
            let t = 0;
            const stop = e === Infinity ? 2000 : e;
            while (t < stop && !c.dead) { C.skip(c); t++; }
            if (e === Infinity ? c.dead : !c.dead || t !== e) fail(`${what}: endBlocks ${e} (${held ? "held" : "let go now"}), its steps end it ${c.dead ? "at " + t : "later"}`);
        }
    };
    const heardCheck = (a, b, what) => {                /* (the next 4 blocks: never louder than heard said, the two alike) */
        if (a.dead) return;
        const x = clone(a), y = clone(b);
        for (const k of UNKEPT) y[k] = k === "tail" ? x.tail.slice() : x[k];
        let most = 0;
        for (let k = 0; k < 4; k++) {
            const h0 = C.heard(x), ox = new Int32Array(64), oy = new Int32Array(64);
            C.block(x, ox); C.block(y, oy);
            if (!same(ox, oy)) { fail(`${what}: block ${k} after: the forward voice with the phases the same sounds otherwise`); return; }
            const h1 = C.heard(x);
            if (h1 > h0) { fail(`${what}: block ${k} after: heard rose from ${h0} to ${h1}`); return; }
            let pk = 0;
            for (let i = 0; i < 64; i++) pk = Math.max(pk, Math.abs(ox[i]));
            most = Math.max(most, h0, h1);
            if (pk > most + 1) { fail(`${what}: block ${k} after peaks at ${pk}, heard ${h0} before, ${h1} after`); return; }
        }
    };
    let n = 0, walks = 0;
    for (const ins of inst) {
        const note = 36 + rnd(48), A = start(ins, note), snaps = {};
        let t = 0;
        for (const h of [1, 5, 40, 300]) {
            for (; t < h; t++) C.skip(A);
            const what = `CHIP ${ins.name} note ${note}: held ${h}`;
            for (const small of [false, true]) { const d = diff(A, fwd(ins, note, h, -1, small)); if (d) fail(`${what}${small ? " (small budgets)" : ""}: ${d}`); }
            endCheck(A, what);
            heardCheck(A, fwd(ins, note, h, -1, false), what);
            snaps[h] = clone(A);
            n++;
        }
        for (const h of [5, 40]) {
            const Ar = clone(snaps[h]);
            C.noteOff(Ar);
            let u = 0;
            for (const r of [1, 17, 200]) {
                for (; u < r; u++) C.skip(Ar);
                const what = `CHIP ${ins.name} note ${note}: held ${h}, let go ${r}`;
                for (const small of [false, true]) { const d = diff(Ar, fwd(ins, note, h, r, small)); if (d) fail(`${what}${small ? " (small budgets)" : ""}: ${d}`); }
                endCheck(Ar, what);
                heardCheck(Ar, fwd(ins, note, h, r, false), what);
                n++;
                if (Ar.dead) break;
            }
        }
        /* a walk: pieces added as the time goes on, let go at a round at random, 1-5 steps a call, its parameters and pitch
         * turned on both between calls */
        const g = start(ins, note), ref = start(ins, note), w = { on: 0, off: -1 }, let_ = 2 + rnd(30);
        let ton = 0, toff = 0, ron = 0, roff = 0, noted = false;
        for (let r = 0; r < 40; r++) {
            const k = rnd(3) ? 1 + rnd(4) : 1 + rnd(1000);
            if (r === let_) w.off = 0;
            if (w.off < 0) { w.on += k; ton += k; } else { w.off += k; toff += k; }
            C.forward(g, w, { n: 1 + rnd(5) });
            for (; ron < ton - w.on; ron++) C.skip(ref);
            if (w.off >= 0 && !g.gate) {
                if (!noted) { C.noteOff(ref); noted = true; }
                for (; roff < toff - w.off; roff++) C.skip(ref);
            }
            const d = diff(ref, g);
            if (d) { fail(`CHIP ${ins.name} note ${note}: walked, round ${r}: ${d}`); break; }
            if (ref.dead) break;
            if (rnd(2)) {
                if (rnd(8)) { const c = [0x57, 0x60, 0x61, 0x50, 0x52, 0x54, 0x40, 0x41][rnd(8)], v = rnd(16); C.param(g.p, g, c, v); C.param(ref.p, ref, c, v); }
                else { g.bend64 = rnd(48 * 64) - 24 * 64; ref.bend64 = g.bend64; }
            }
        }
        walks++;
    }
    counts.CHIP = n;
    counts.walks = walks;
    counts.chips = inst.length;
}
console.log(`forward: the DX7 ${counts.DX7} cases, the Juno-106 ${counts.JUNO}, the sampler ${counts.SAMPLER} (${counts.places} places), the CHIP ` +
            `${counts.CHIP} of ${counts.chips} instruments and ${counts.walks} walks`);
console.log(failed ? failed + " FAILED" : "ALL PASSED");
process.exit(failed ? 1 : 0);
