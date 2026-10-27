/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's sound without a browser: the worklet's code (dsp-*.js) run as audio.js puts it
 * together, with a stand-in for the browser's audio worklet. Every instrument of the demo data plays
 * C-4 for a second: its level, its peak, its pitch (DX7 and TB-303: C-4 within 2 %), nothing
 * non-finite; then 16 voices through the whole chain with every effect on; then every demo song and
 * the loops through the replay (player.js, with a stand-in for audio.js on the worklet's clock): what
 * its commands sent, its level, its peak, the most voices at once.
 *   node tests/playground/sound.js [wav dir] [seconds a song, default 30]   (WAVs, if a dir is given) */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "../.."), PG = path.join(ROOT, "playground"), SR = 44100;
const wavdir = process.argv[2] && process.argv[2] !== "-" ? process.argv[2] : null, SONG_S = +(process.argv[3] || 30);

let tickFn = null;
const page = { console, performance, structuredClone, atob, setInterval: (fn, ms) => { if (ms === 20) tickFn = fn; return 0; } };
page.window = page;
vm.createContext(page);
for (const f of ["fonts.js", "content.js", "draw.js", "model.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), page, { filename: f });
const M = page.PTFM.model;
M.demoDisk();                                           /* (the playground starts clean: DEMO DISK pressed first) */

let Proc = null;
const wk = {
    console, sampleRate: SR, currentTime: 0,
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage: m => { this.stats = m; }, onmessage: null }; } },
    registerProcessor: (n, c) => { Proc = c; },
};
vm.createContext(wk);
const dsp = {};
dsp.window = dsp;
vm.createContext(dsp);
for (const f of ["dsp-core.js", "dsp-dx7.js", "dsp-synth.js", "dsp-chip-tables.js", "dsp-chip.js", "dsp-fx.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), dsp, { filename: f });
vm.runInContext("const E = {};\n" + dsp.PTFM.dsp.map(f => "(" + f.toString() + ")(E);").join("\n") + "\nregisterProcessor('ptfm', E.Proc);\n",
                wk, { filename: "worklet.js" });

function fresh(setup) {
    wk.currentTime = 0;
    const p = new Proc(), send = m => p.port.onmessage({ data: m });
    const list = [];
    for (const c of M.state.carts) if (c.samples) c.samples.forEach((s, i) => list.push({ key: c.id + ":" + i, data: M.audio[s.audio], rate: s.rate }));
    send({ t: "samples", list });
    send({ t: "voices", n: 16 });
    send({ t: "master", v: .5 });
    send({ t: "fx", setup, mixer: M.state.songs[0].mixer, base: 0, tempo: 120, gates: M.state.gates.map(g => g.steps) });
    return { p, send };
}
function inst(ref) {
    const c = M.cart(ref >> 8), i = M.inst(ref);
    return { t: "inst", ref, engine: c.type, p: c.type === "SAMPLER" ? Object.assign({ key: c.id + ":" + i.p.sample }, i.p) : i.p, level: i.level };
}
function render(p, seconds) {
    const n = Math.round(seconds * SR / 128), L = new Float32Array(n * 128), R = new Float32Array(n * 128), K = new Float32Array(n * 128);
    for (let b = 0; b < n; b++) {
        const l = new Float32Array(128), r = new Float32Array(128);
        p.process([], [[l, r]]);
        L.set(l, b * 128); R.set(r, b * 128);
        K.set(p.tracks[p.tracks.length - 1], b * 128);   /* the keys' track (the last): the voices' output */
        wk.currentTime += 128 / SR;
    }
    return { L, R, K };
}
function f0(x, a, n) {                                  /* the fundamental, by autocorrelation */
    let best = 0, e = 0, fell = false, from = 0;
    const s = new Float64Array(1001);
    for (let i = a; i < a + n; i += 2) e += x[i] * x[i];
    for (let k = 40; k <= 1000; k++) {
        for (let i = a; i < a + n; i += 2) s[k] += x[i] * x[i + k];
        if (s[k] < 0 && !fell) { fell = true; from = k; }   /* past the first slope: a lag before it is no period */
        if (fell && s[k] > best) best = s[k];
    }
    /* the period is the first peak near the best: a clean tone repeats almost as well at every multiple
     * of it, and the best alone could be any of them (a sine read as 5 periods, 52 Hz for C-4) */
    let lag = 0;
    for (let k = Math.max(from, 41); k < 1000 && fell; k++) if (s[k] >= .9 * best && s[k] >= s[k - 1] && s[k] >= s[k + 1]) { lag = k; break; }
    /* how far it repeats at that lag, 0-1: a drum (a snare, a clap) barely does, and has no pitch to check */
    return { hz: lag ? SR / lag : 0, periodic: e && lag ? s[lag] / e : 0 };
}
/* the loudest 300 ms, RMS in dB: as LEVEL is measured (specs/files/instruments.md) */
function loudest(L) {
    const W = Math.round(.3 * SR);
    let best = 0;
    for (let a = 0; a + W <= L.length; a += 441) { let s = 0; for (let k = a; k < a + W; k++) s += L[k] * L[k]; best = Math.max(best, s / W); }
    return 10 * Math.log10(best + 1e-20);
}
function wav(name, L) {
    if (!wavdir) return;
    fs.mkdirSync(wavdir, { recursive: true });
    const b = Buffer.alloc(44 + L.length * 2);
    b.write("RIFF", 0); b.writeUInt32LE(36 + L.length * 2, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16);
    b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(SR, 24); b.writeUInt32LE(SR * 2, 28);
    b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(L.length * 2, 40);
    for (let i = 0; i < L.length; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(L[i] * 32767))), 44 + i * 2);
    fs.writeFileSync(path.join(wavdir, name + ".wav"), b);
}

let failed = 0;
const fail = s => { console.log("FAIL " + s); failed++; };

/* the engines' reference sounds (specs/files/instruments.md, LEVEL): each at -20 dB RMS, its loudest
 * 300 ms at the voice's output (the keys' track, full and centred; MASTER full), whatever the data */
{
    const op = level => ({ eg: { rates: [99, 99, 99, 99], levels: [99, 99, 99, 0] }, level, mode: "ratio", coarse: 1, fine: 0, detune: 0,
        rate_scaling: 0, amp_mod_sens: 0, key_vel_sens: 0, kbd: { break_point: 39, left_depth: 0, right_depth: 0, left_curve: "-lin", right_curve: "-lin" } });
    const sine = new Int8Array(SR);                     /* 8 bits, as the samples are */
    for (let i = 0; i < SR; i++) sine[i] = Math.round(127 * Math.sin(2 * Math.PI * i / 100));
    const REFS = {
        DX7: [60, { algorithm: 32, feedback: 0, osc_key_sync: true, transpose: 0, pitch_eg: { rates: [99, 99, 99, 99], levels: [50, 50, 50, 50] },
                    lfo: { speed: 35, delay: 0, pm_depth: 0, am_depth: 0, key_sync: true, wave: "triangle", pitch_mod_sens: 0 },
                    ops: [op(99), op(0), op(0), op(0), op(0), op(0)] }],
        JUNO: [60, { lfo: { rate: 0, delay: 0 }, dco: { lfo: 0, pwm: 0, noise: 0, sub: 0, range: "8'", pulse: false, saw: true, pwm_source: "manual" },
                     hpf: 1, vcf: { freq: 127, res: 0, env: 0, lfo: 0, kybd: 0, env_polarity: "+" }, vca: { level: 127, mode: "env" },
                     env: { a: 0, d: 0, s: 127, r: 0 }, chorus: "off" }],
        TB303: [36, { wave: 0, cutoff: 127, reso: 0, envmod: 0, decay: 64, accent: 0 }],
        SAMPLER: [69, { key: "ref", sample: 0, start: 0, end: SR, loop: "FORWARD", loopStart: 0, loopEnd: SR, base: 69, tune: 0, volume: 64,
                        env: { rates: [99, 99, 99, 99], levels: [99, 99, 99, 0] } }],
    };
    const out = [];
    for (const e in REFS) {
        wk.currentTime = 0;
        const p = new Proc(), send = m => p.port.onmessage({ data: m });
        send({ t: "samples", list: [{ key: "ref", data: sine, rate: SR }] }); send({ t: "voices", n: 16 }); send({ t: "master", v: 1 });
        send({ t: "fx", setup: null, mixer: [], base: 0, tempo: 120, gates: [] });
        send({ t: "inst", ref: 1, engine: e, p: REFS[e][1], level: 0 });
        send({ t: "on", id: "k", time: 0, midi: REFS[e][0], ref: 1, vol: 127, track: -1 });
        send({ t: "off", id: "k", time: 1 });
        const db = loudest(render(p, 1.6).L);
        out.push(e + " " + db.toFixed(1));
        if (Math.abs(db + 20) > .5) fail(`${e}'s reference sound at ${db.toFixed(1)} dB, not -20 dB`);
    }
    console.log("the engines' reference sounds (dB):", out.join(", "));
    /* LINE GAIN (specs/ui/screens/settings.md): the analog out's level with MASTER, under the soft ceiling */
    const gains = [];
    for (const g of [-6, 6]) {
        wk.currentTime = 0;
        const p = new Proc(), send = m => p.port.onmessage({ data: m });
        send({ t: "voices", n: 16 }); send({ t: "master", v: 1 }); send({ t: "gain", v: Math.pow(10, g / 20) });
        send({ t: "fx", setup: null, mixer: [], base: 0, tempo: 120, gates: [] });
        send({ t: "inst", ref: 1, engine: "TB303", p: REFS.TB303[1], level: 0 });
        send({ t: "on", id: "k", time: 0, midi: REFS.TB303[0], ref: 1, vol: 127, track: -1 });
        send({ t: "off", id: "k", time: 1 });
        const db = loudest(render(p, 1.6).L);
        gains.push((g > 0 ? "+" : "") + g + " " + db.toFixed(1));
        if (Math.abs(db - (-20 + g)) > .5) fail(`LINE GAIN ${g} dB: the reference sound at ${db.toFixed(1)} dB, not ${-20 + g}`);
    }
    console.log("LINE GAIN on the TB-303's reference sound (dB):", gains.join(", "));
    /* SETTINGS' KEYS (specs/ui/screens/fx.md#the-keys-dry): a FILTER low-pass at its lowest in the chain; the keys
     * THROUGH FX lose most of the Juno-106's saw to it, DRY none of it */
    const keysAt = {};
    for (const dryOn of [false, true]) {
        wk.currentTime = 0;
        const p = new Proc(), send = m => p.port.onmessage({ data: m }), fxs = M.newFx();
        fxs.on.FILTER = true; fxs.knobs.FILTER[0] = -64;
        send({ t: "voices", n: 16 }); send({ t: "master", v: 1 });
        send({ t: "fx", setup: fxs, mixer: [], base: 0, tempo: 120, gates: [] });
        send({ t: "keysdry", on: dryOn });
        send({ t: "inst", ref: 1, engine: "JUNO", p: REFS.JUNO[1], level: 0 });
        send({ t: "on", id: "k", time: 0, midi: REFS.JUNO[0], ref: 1, vol: 127, track: -1 });
        send({ t: "off", id: "k", time: 1 });
        keysAt[dryOn ? "DRY" : "THROUGH FX"] = loudest(render(p, 1.6).L);
    }
    console.log(`KEYS through a low-pass at 100 Hz: THROUGH FX ${keysAt["THROUGH FX"].toFixed(1)} dB, DRY ${keysAt.DRY.toFixed(1)} dB`);
    if (Math.abs(keysAt.DRY + 20) > .5 || keysAt["THROUGH FX"] > keysAt.DRY - 12)
        fail("KEYS: DRY does not skip the FILTER (DRY at -20 dB, THROUGH FX 12 dB or more under it)");
    /* FX MIXER's KEYS column (specs/ui/screens/fx.md#mixer): in SENDS with REVERB on, the keys go into it by their REV
     * (7F at first): a tail after the note, none at REV 00 or DRY; PAN 00 puts them on the left */
    const ks = {}, sq = (a, from) => { let s = 0; for (let i = from; i < a.length; i++) s += a[i] * a[i]; return s; };
    const KM = { pan: 128, dck: 0, cho: 0, dly: 0, rev: 127 };
    for (const [name, mix, dryOn] of [["REV 7F", KM, false], ["REV 00", { ...KM, rev: 0 }, false], ["DRY", KM, true],
                                       ["PAN 00", { ...KM, pan: 0, rev: 0 }, false]]) {
        wk.currentTime = 0;
        const p = new Proc(), send = m => p.port.onmessage({ data: m }), fxs = M.newFx();
        fxs.routing = "SENDS"; fxs.on.REVERB = true;
        send({ t: "voices", n: 16 }); send({ t: "master", v: 1 });
        send({ t: "fx", setup: fxs, mixer: [], base: 0, tempo: 120, gates: [] });
        send({ t: "keysdry", on: dryOn }); send({ t: "keysmix", m: mix });
        send({ t: "inst", ref: 1, engine: "TB303", p: REFS.TB303[1], level: 0 });
        send({ t: "on", id: "k", time: 0, midi: REFS.TB303[0], ref: 1, vol: 127, track: -1 });
        send({ t: "off", id: "k", time: .3 });
        const o = render(p, 2.5);
        ks[name] = { tail: sq(o.L, Math.round(1.2 * SR)), l: sq(o.L, 0), r: sq(o.R, 0) };
    }
    console.log(`KEYS in SENDS, REVERB on, the tail after a note: REV 7F ${ks["REV 7F"].tail.toExponential(2)}, REV 00 ` +
                `${ks["REV 00"].tail.toExponential(2)}, DRY ${ks.DRY.tail.toExponential(2)}; PAN 00 left ${ks["PAN 00"].l.toExponential(2)}, ` +
                `right ${ks["PAN 00"].r.toExponential(2)}`);
    if (!(ks["REV 7F"].tail > 0 && ks["REV 7F"].tail > ks["REV 00"].tail * 100 && ks["REV 7F"].tail > ks.DRY.tail * 100))
        fail("KEYS in SENDS: REV 7F does not give the note a reverb's tail, or REV 00 / DRY do");
    if (!(ks["PAN 00"].l > 0 && ks["PAN 00"].r < ks["PAN 00"].l * 1e-6)) fail("KEYS PAN 00: not on the left alone");
    /* FILTER's envelope (specs/ui/screens/fx.md#filters-envelope): the same low-pass, ENV 100 %, SUSTAIN 100 %; SOURCE
     * KEYS opens it while the key is held (the keys' hit as the player posts it), SOURCE BEAT on the beat with no
     * note to fire it; ENV 0 % the filter as it was */
    const envAt = {};
    for (const [name, env, src] of [["ENV 0", 0, 0], ["KEYS", 100, 0], ["BEAT", 100, 19], ["TRACK 00", 100, 1]]) {
        wk.currentTime = 0;
        const p = new Proc(), send = m => p.port.onmessage({ data: m }), fxs = M.newFx();
        fxs.on.FILTER = true; fxs.knobs.FILTER[0] = -64; fxs.knobs.FILTER[2] = env; fxs.knobs.FILTER[3] = src; fxs.knobs.FILTER[6] = 100;
        send({ t: "voices", n: 16 }); send({ t: "master", v: 1 });
        send({ t: "fx", setup: fxs, mixer: [], base: 0, tempo: 120, gates: [] });
        send({ t: "inst", ref: 1, engine: "JUNO", p: REFS.JUNO[1], level: 0 });
        send({ t: "fire", track: -1, time: 0 });
        send({ t: "on", id: "k", time: 0, midi: REFS.JUNO[0], ref: 1, vol: 127, track: -1, key: true });
        send({ t: "off", id: "k", time: 1 });
        envAt[name] = loudest(render(p, 1.6).L);
    }
    console.log("FILTER ENV through a low-pass at 100 Hz, a key held 1 s, by SOURCE (dB):",
                Object.entries(envAt).map(([k, v]) => k + " " + v.toFixed(1)).join(", "));
    if (Math.abs(envAt["ENV 0"] - keysAt["THROUGH FX"]) > .1) fail("FILTER ENV: at ENV 0 % the filter is not as it was");
    if (envAt.KEYS < envAt["ENV 0"] + 12 || envAt.BEAT < envAt["ENV 0"] + 12)
        fail("FILTER ENV: SOURCE KEYS or BEAT does not open the low-pass (12 dB or more over ENV 0 %)");
    if (envAt["TRACK 00"] > envAt["ENV 0"] + 1) fail("FILTER ENV: SOURCE TRACK 00 opened it with no note on track 00");
}

const dry = M.newFx(), levels = {}, heard = {};
for (const c of M.state.carts) {
    if (c.type === "FUSION") continue;
    c.slots.forEach((s, i) => {
        if (!s) return;
        const ref = c.id << 8 | i, { p, send } = fresh(dry);
        const note = c.type === "TB303" ? 36 : c.type === "SAMPLER" ? s.p.base : 60;   /* as LEVEL is measured */
        send(inst(ref));
        send({ t: "on", id: "k", time: .01, midi: note, ref, vol: 127, track: -1 });
        send({ t: "off", id: "k", time: 1.01 });
        const { L, K } = render(p, 1.5);
        (heard[c.type] = heard[c.type] || []).push(loudest(K));
        let ss = 0, pk = 0;
        for (let k = 441; k < 441 + SR / 2; k++) { if (!isFinite(L[k])) { fail(`${M.refText(ref)} ${s.name}: not finite`); return; } ss += L[k] * L[k]; pk = Math.max(pk, Math.abs(L[k])); }
        const db = 20 * Math.log10(Math.sqrt(ss / (SR / 2)) + 1e-9);
        (levels[c.type] = levels[c.type] || []).push(db);
        if (pk > .95) fail(`${M.refText(ref)} ${s.name}: peak ${pk.toFixed(2)}`);
        if (db < -65 && pk < .01) fail(`${M.refText(ref)} ${s.name}: silent (${db.toFixed(1)} dB, peak ${pk.toFixed(4)})`);
        if ((c.type === "DX7" || c.type === "TB303") && i < 4) {
            const { hz: f, periodic } = f0(L, 4000, 8192), cents = 1200 * Math.log2(f / 261.63);
            const octs = Math.round(cents / 1200), off = cents - octs * 1200;
            if (periodic > .8 && Math.abs(off) > 35) fail(`${M.refText(ref)} ${s.name}: pitch ${f.toFixed(1)} Hz is not a C`);
        }
        if (i < 2) wav(c.type + "_" + i, L);
    });
}
for (const e in levels) {
    const v = levels[e], avg = v.reduce((a, b) => a + b, 0) / v.length;
    console.log(`${e.padEnd(7)} ${String(v.length).padStart(3)} instruments, average ${avg.toFixed(1)} dB RMS, ${Math.min(...v).toFixed(1)} to ${Math.max(...v).toFixed(1)}`);
}
/* the instruments as their LEVELs were measured (on the device's engines: each instrument's loudest 300 ms
 * at -20 dB, LEVEL included): the engines here must agree, so each engine's median sits within 1 dB of
 * -20 (some sit lower: a LEVEL also keeps a loud peak under -6 dB) */
{
    const out = [];
    for (const e in heard) {
        const v = heard[e].slice().sort((a, b) => a - b), med = v[v.length >> 1];
        out.push(`${e} ${med.toFixed(1)}`);
        if (Math.abs(med + 20) > 1) fail(`${e}'s instruments heard at ${med.toFixed(1)} dB (median), not as their LEVELs say (-20 dB)`);
    }
    console.log("the instruments with their LEVELs, median (dB):", out.join(", "));
}
const full = structuredClone(M.state.songs[0].fx);
for (const k in full.on) full.on[k] = true;
const { p, send } = fresh(full);
/* the first instrument of each cartridge but FUSION, eight of them: every engine */
const refs = M.state.carts.filter(c => c.type !== "FUSION").map(c => c.id << 8 | c.slots.findIndex(Boolean)).slice(0, 8);
refs.forEach((r, i) => { send(inst(r)); send({ t: "on", id: "c" + i, time: .01 + i * .05, midi: 48 + i * 3, ref: r, vol: 110, track: i }); });
for (let i = 0; i < 8; i++) send({ t: "on", id: "d" + i, time: .5 + i * .06, midi: 60 + i * 2, ref: refs[i], vol: 110, track: i });
send({ t: "click", time: .2, accent: true, sound: 0, level: 60 });
const out = render(p, 3);
let pk = 0, bad = 0;
for (const x of [out.L, out.R]) for (const v of x) { if (!isFinite(v)) bad++; pk = Math.max(pk, Math.abs(v)); }
console.log(`16 voices, every effect on: peak ${pk.toFixed(3)}, ${p.stats ? p.stats.voices.length : 0} voices sounding at the end`);
if (bad) fail(bad + " non-finite samples through the chain");
if (pk > 1) fail("the output passes full scale");
wav("full_chain", out.L);

/* the pool past VOICES (specs/ui/screens/settings.md#voices), as the firmware's voices_test: at 8 voices, note 60
 * held, then 8 more keys at 0.5 s let go at 1 s: 60 is parked, unheard, and comes back once a voice is free, where
 * its envelope would be (within 1 dB of 60 alone over 2.5-3 s); let go while parked (0.8 s), it is gone */
{
    const rms = (x, a) => { let s = 0; for (let i = a; i < x.length; i++) s += x[i] * x[i]; return 10 * Math.log10(s / (x.length - a) + 1e-20); };
    const take = (ref, others, letGo) => {
        const { p, send } = fresh(dry);
        send({ t: "voices", n: 8 });
        send(inst(ref));
        send({ t: "on", id: "a", time: .01, midi: 60, ref, vol: 100, track: -1 });
        for (let k = 0; k < others; k++) send({ t: "on", id: "b" + k, time: .5, midi: 62 + k, ref, vol: 100, track: -1 });
        if (letGo) send({ t: "off", id: "a", time: .8 });
        for (let k = 0; k < others; k++) send({ t: "off", id: "b" + k, time: 1 });
        render(p, .9);
        p.post();
        const parked = p.stats.parked === (letGo ? 0 : 1) && !p.stats.voices.some(v => v.id === "a");
        const end = render(p, 2.1);
        p.post();
        const back = p.stats.parked === 0 && p.stats.voices.length === 1 && p.stats.voices[0].id === "a";
        return { db: rms(end.K, Math.round(1.6 * SR)), parked, back };
    };
    for (const type of ["DX7", "JUNO", "SAMPLER"]) {
        let ref = -1, alone = null;
        for (const c of M.state.carts) {
            if (c.type !== type || ref >= 0) continue;
            c.slots.forEach((s, i) => { if (s && ref < 0) { const r = take(c.id << 8 | i, 0, false); if (r.db > -45) { ref = c.id << 8 | i; alone = r; } } });
        }
        if (ref < 0) { fail(`${type}: no instrument that sustains, for the pool's check`); continue; }
        const taken = take(ref, 8, false), gone = take(ref, 8, true);
        console.log(`the pool at 8 voices, ${type} ${M.refText(ref)}: alone ${alone.db.toFixed(2)} dB, parked and back ${taken.db.toFixed(2)} dB, ` +
                    `let go while parked ${gone.db.toFixed(1)} dB`);
        if (!taken.parked) fail(`${type}: a 9th key does not park the oldest`);
        if (!taken.back) fail(`${type}: the parked note does not come back once a voice is free`);
        if (Math.abs(taken.db - alone.db) > 1) fail(`${type}: back from parking at ${taken.db.toFixed(2)} dB, not where its envelope would be`);
        if (!gone.parked || gone.db > -80) fail(`${type}: let go while parked, it is not gone`);
    }
    /* 20 keys held, as voices_test: below VOICES 16 the pool holds 16 notes, parked ones included (4 wait at 12, 8 at
     * 8) */
    {
        let ref = -1;
        for (const c of M.state.carts) if (c.type === "JUNO" && ref < 0) ref = c.id << 8 | c.slots.findIndex(Boolean);
        const held = n => {
            const { p, send } = fresh(dry);
            send({ t: "voices", n });
            send(inst(ref));
            for (let k = 0; k < 20; k++) send({ t: "on", id: "k" + k, time: .01 + k * .05, midi: 48 + 2 * k, ref, vol: 100, track: -1 });
            render(p, 1.5); p.post();
            return { heard: p.stats.voices.length, parked: p.stats.parked };
        };
        const a = held(12), b = held(8);
        console.log(`20 keys held: VOICES 12: ${a.heard} heard, ${a.parked} parked; VOICES 8: ${b.heard} heard, ${b.parked} parked`);
        if (a.heard !== 12 || a.parked !== 4) fail("20 keys at VOICES 12: not 12 heard and 4 parked");
        if (b.heard !== 8 || b.parked !== 8) fail("20 keys at VOICES 8: not 8 heard and 8 parked");
    }
    /* the keys first, however quiet: 8 quiet keys held at 8 voices, then a loud song note: it starts parked, every key
     * heard; 2 more keys park the 2 oldest keys; one heard key let go: a parked key comes back, not the song's note;
     * all let go, the song's note comes in */
    {
        let ref = -1;
        for (const c of M.state.carts) if (c.type === "JUNO" && ref < 0) ref = c.id << 8 | c.slots.findIndex(Boolean);
        const { p, send } = fresh(dry);
        send({ t: "voices", n: 8 });
        send(inst(ref));
        for (let k = 0; k < 8; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref, vol: 20, track: -1 });
        send({ t: "on", id: "s", time: .3, midi: 60, ref, vol: 127, track: 0 });
        render(p, .5); p.post();
        const keysOn = () => p.stats.voices.filter(v => v.track === -1).length, songOn = () => p.stats.voices.some(v => v.id === "s");
        const a = { keys: keysOn(), song: songOn(), parked: p.stats.parked };
        send({ t: "on", id: "k8", time: .6, midi: 80, ref, vol: 20, track: -1 });
        send({ t: "on", id: "k9", time: .6, midi: 82, ref, vol: 20, track: -1 });
        render(p, .3); p.post();
        const b = { keys: keysOn(), oldest: p.stats.voices.some(v => v.id === "k0" || v.id === "k1") };
        send({ t: "off", id: "k5", time: wk.currentTime });
        render(p, 2.5); p.post();
        const c = { keys: keysOn(), song: songOn(), back: p.stats.voices.some(v => v.id === "k0" || v.id === "k1") };
        for (let k = 0; k < 10; k++) send({ t: "off", id: "k" + k, time: wk.currentTime });
        render(p, 3); p.post();
        const d = songOn();
        console.log(`the keys first at 8 voices: 8 keys and a song note: ${a.keys} keys heard, song ${a.song ? "heard" : "parked"}; ` +
                    `10 keys: ${b.keys} heard; one let go: ${c.keys} keys heard, song ${c.song ? "heard" : "parked"}; all let go: song ${d ? "heard" : "not heard"}`);
        if (a.keys !== 8 || a.song || a.parked !== 1) fail("the keys first: a song note took a voice from a held key");
        if (b.keys !== 8 || b.oldest) fail("the keys first: 2 more keys do not park the 2 oldest");
        if (c.keys !== 8 || c.song || !c.back) fail("the keys first: a voice free, the song's note came back before the keys'");
        if (!d) fail("the keys first: all keys let go, the song's note does not come in");
    }
}

/* ----------------------------------------------- the songs and the loops, through the replay --- */
let cur = null;
const sent = {};
const post = m => { sent[m.t] = (sent[m.t] || 0) + 1; cur.send(m); };
const A = {
    time: () => wk.currentTime, instrument() {}, changed() {}, fxNow() {},
    noteOn(id, time, midi, ref, vol, track, o) {
        if (M.instType(ref) !== "FUSION") cur.send(inst(ref));
        post({ t: "on", id, time, midi, ref, vol, track, accent: !!o.accent, glide: !!o.glide, offset: o.offset || 0, params: o.params && o.params.length ? o.params : null,
               key: !o.song });
    },
    fire: (track, time, idp) => post({ t: "fire", track, time, id: idp ? idp + "~f" : undefined }),
    noteOff: (id, time) => post({ t: "off", id, time }), ctl: (id, time, bend, vol) => post({ t: "ctl", id, time, bend, vol }),
    flush: (prefix, time, at) => post({ t: "flush", prefix, time, at }), allOff: () => post({ t: "alloff" }),
    cutAt: (prefix, time) => { post({ t: "cutp", prefix, time }); post({ t: "killp", prefix, time: time + .004 }); },
    iparam: (track, code, value, time) => post({ t: "iparam", track, code, value, time }), iclear: (track, time) => post({ t: "iclear", track, time }),
    trackMix: (track, key, value, time) => post({ t: "tmix", track, key, value, time }), trackMute() {},
    click: (time, accent, sound, level) => post({ t: "click", time, accent, sound, level }),
    fxCmd: (code, value, time, deck) => post({ t: "fxcmd", code, value, time, deck }), fxReset: () => post({ t: "fxreset" }),
    fxEmptyAt: time => post({ t: "fxempty", time }),
    punchAt: (k, on, time, idp = "s") => post({ t: "tpunch", id: idp + "~", k, on, time }), punch: (k, on) => post({ t: "punch", k, on }),
    barAt: (time, deck, idp = "s") => post({ t: "tbar", id: idp + "~", time, deck }), fxBypass: on => post({ t: "bypass", on }), fxWatch: on => post({ t: "watch", on }),
    songFx: s => post({ t: "fx", setup: s.fx, mixer: s.mixer, base: s === M.state.loops ? 16 : 0, tempo: 120, gates: M.state.gates.map(g => g.steps) }),
    /* DJ's, as audio.js sends them */
    voices: n => post({ t: "voices", n }), dj: m => post(Object.assign({ t: "dj" }, m)), hush: on => post({ t: "hush", on }),
    djDeck: (deck, setup, mixer) => post({ t: "djdeck", deck, setup, mixer }), djReset: deck => post({ t: "djreset", deck }),
    djEcho: (deck, on) => post({ t: "djecho", deck, on }),
    djBrake: (deck, time, up, clear) => post({ t: "brake", deck, time, up: !!up, clear: !!clear }),
    /* PLAYBACK's K3 on the song's voices, SETTINGS' KEYS GAIN on the keys', as audio.js sends them */
    songGain: g => post({ t: "songgain", g }), keysGain: g => post({ t: "keysgain", g }), keysDry: on => post({ t: "keysdry", on }),
};
A.fxNow = () => {
    if (P.what !== "dj") return;
    post({ t: "fx", setup: M.state.dj.fx, mixer: null, base: 0, tempo: P.tempo(), gates: M.state.gates.map(g => g.steps) });
    P.djPost();
};
page.PTFM.audio = A;
page.PTFM.ui = { song: 0, pattern: 0, inst: 0x0100, screen: { name: "SONGS" }, muted: () => false, editing: () => false, set: k => M.state.settings[k],
                 toast() {}, pausedHook() {}, clamp: (v, a, b) => v < a ? a : v > b ? b : v };
vm.runInContext(fs.readFileSync(path.join(PG, "player.js"), "utf8"), page, { filename: "player.js" });
const P = page.PTFM.player;
function play(name, start, seconds = SONG_S) {
    cur = fresh(M.newFx());
    for (const k in sent) delete sent[k];
    start();
    const n = Math.round(seconds * SR / 128), L = new Float32Array(n * 128);
    let next = 0, most = 0, steals = 0, pos = 0, ss = 0, peak = 0, nan = 0;
    for (let b = 0; b < n; b++) {
        if (wk.currentTime >= next) { tickFn(); next += .02; }
        const l = new Float32Array(128), r = new Float32Array(128);
        cur.p.process([], [[l, r]]);
        L.set(l, b * 128);
        for (let i = 0; i < 128; i++) { if (!isFinite(l[i]) || !isFinite(r[i])) nan++; ss += l[i] * l[i]; peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i])); }
        wk.currentTime += 128 / SR;
        if (cur.p.stats) { most = Math.max(most, cur.p.stats.voices.length); steals = cur.p.stats.steals; }
        const h = P.at();
        if (h) pos = Math.max(pos, h.pos);
    }
    P.pause();
    const db = 20 * Math.log10(Math.sqrt(ss / L.length) + 1e-9);
    const s = Object.keys(sent).sort().map(k => k + " " + sent[k]).join(", ");
    console.log(`${name.padEnd(16)} ${db.toFixed(1)} dB RMS, peak ${peak.toFixed(2)}, most ${most} voices, ${steals} steals, to position ${pos + 1}: ${s}`);
    if (nan) fail(name + ": " + nan + " non-finite samples");
    if (peak > 1) fail(name + ": the output passes full scale");
    if (db < -40) fail(name + ": nearly silent");
    wav("song_" + name.replace(/ /g, "_"), L);
    return L;
}
M.state.settings.VOICES = 16;
M.state.songs.forEach((s, i) => play(s.name, () => { page.PTFM.ui.song = i; P.playSong(i); }));
page.PTFM.ui.screen.name = "LOOPS";
if (M.state.loops.rows) play("THE LOOPS", () => P.playLoops(0));
else console.log("THE LOOPS        empty: the demo disk has none");

/* ------------------------------------------- a punch-in command: TAPE STOP !08 at row 16 --- */
/* a note every other row on track 1; at 120 BPM, 6 ticks, 4 rows a beat a row is 0.125 s: the punch at
 * 2.05 s winds down over a beat, is silent for the next, and the mix comes back at row 24 (3.05 s) */
page.PTFM.ui.screen.name = "SONGS";
const tp = M.state.patterns.indexOf(null);
function punchSong(withPunch) {
    M.state.patterns[tp] = M.newPattern(tp, 2, 32);
    for (let r = 0; r < 32; r += 2) M.state.patterns[tp].cells[r * 16] = M.newCell(60 + (r & 7), 0x0100, 100);
    if (withPunch) { const c = M.newCell(null, null, null); c.cmds.push({ c: 0xD1, v: 8 }); M.state.patterns[tp].cells[16 * 16 + 1] = c; }
    M.state.songs.push(Object.assign(M.newSong(), { name: "PUNCH", tempo: 120, ticks: 6, rpb: 4, swing: 50, order: [tp] }));
    const i = M.state.songs.length - 1;
    P.kept.song[i] = { pos: 0, row: 0 };                    /* from the start each time */
    const L = play(withPunch ? "TAPE STOP !08" : "NO PUNCH-IN", () => { page.PTFM.ui.song = i; P.playSong(i); }, 4);
    M.state.songs.pop();
    return L;
}
const ac = (L, a, b) => {                                    /* the level without DC (a stopped tape holds one sample) */
    const s = L.subarray(Math.round(a * SR), Math.round(b * SR));
    const mean = s.reduce((x, y) => x + y, 0) / s.length;
    return Math.sqrt(s.reduce((x, y) => x + (y - mean) * (y - mean), 0) / s.length);
};
const plain = punchSong(false), stopped = punchSong(true);
M.state.patterns[tp] = null;
const quiet = 20 * Math.log10(ac(stopped, 2.6, 3.0) / ac(plain, 2.6, 3.0) + 1e-9);
const back = 20 * Math.log10(ac(stopped, 3.2, 3.6) / ac(plain, 3.2, 3.6) + 1e-9);
console.log(`TAPE STOP !08: the stopped beat ${quiet.toFixed(1)} dB against the plain song, after its rows ${back.toFixed(1)} dB`);
if (!(quiet < -30)) fail("TAPE STOP !08: the second beat is not silent");
if (Math.abs(back) > 1) fail("TAPE STOP !08: the mix does not come back after its 8 rows");

/* ------------------- PLAYBACK's K1: a skip lands, a clean start there (every note cut, the tails emptied) --- */
/* a held Juno-106 note in the first position, through the REVERB, an empty second: skipped to it, silence (no note,
 * no tail); not skipped, it holds */
{
    const jc = M.state.carts.find(c => c && c.type === "JUNO"), tp2 = M.state.patterns.indexOf(null, tp + 1);
    const rfx = M.newFx();
    rfx.on.REVERB = true;
    M.state.patterns[tp] = M.newPattern(tp, 2, 32);
    M.state.patterns[tp].cells[0] = M.newCell(57, jc.id << 8, 100);
    M.state.patterns[tp2] = M.newPattern(tp2, 2, 32);
    M.state.songs.push(Object.assign(M.newSong(), { name: "SKIP", tempo: 120, ticks: 6, rpb: 4, swing: 50, order: [tp, tp2], fx: rfx }));
    const i = M.state.songs.length - 1;
    P.kept.song[i] = { pos: 0, row: 0 };
    const held = play("NO SKIP", () => { page.PTFM.ui.song = i; P.playSong(i); }, 3);
    P.kept.song[i] = { pos: 0, row: 0 };
    cur = fresh(M.newFx());
    for (const k in sent) delete sent[k];
    page.PTFM.ui.song = i; P.playSong(i);
    const n = Math.round(3 * SR / 128), L = new Float32Array(n * 128);
    let next = 0;
    for (let b = 0; b < n; b++) {
        if (wk.currentTime >= next) { tickFn(); next += .02; }
        if (b === Math.round(1 * SR / 128)) P.skip(1);
        const l = new Float32Array(128), r = new Float32Array(128);
        cur.p.process([], [[l, r]]);
        L.set(l, b * 128);
        wk.currentTime += 128 / SR;
    }
    P.pause();
    const db = (x, a, b) => 20 * Math.log10(ac(x, a, b) + 1e-9);
    console.log(`PLAYBACK K1: a held note, 2-3 s: not skipped ${db(held, 2, 3).toFixed(1)} dB, skipped at 1 s ${db(L, 2, 3).toFixed(1)} dB`);
    if (!(db(held, 2, 3) > -60)) fail("PLAYBACK K1: the test's held note does not hold");
    if (!(db(L, 2, 3) < -80)) fail("PLAYBACK K1: a note of the position before, or its REVERB tail, rings on after a skip");
    M.state.songs.pop();
    M.state.patterns[tp] = M.state.patterns[tp2] = null;
}

/* ---------------------------- GATE locked to the bar, FX LIVE's BYPASS (specs/ui/screens/fx.md) --- */
{
    const setup = M.newFx();
    setup.on.GATE = true; setup.knobs.GATE[1] = 1;          /* RATE 1/16: a step 0.125 s at 120 BPM */
    const { p, send } = fresh(setup);
    render(p, .3);                                           /* the clock off any bar */
    send({ t: "bar", time: wk.currentTime });              /* a bar starts now: step 1 here */
    render(p, .01);
    const s0 = p.fx.step;
    render(p, .375);
    const s3 = p.fx.step;
    console.log(`GATE: a bar at ${(.3).toFixed(1)} s, then step ${s0 + 1}; 0.375 s on, step ${s3 + 1}`);
    if (s0 !== 0 || s3 !== 3) fail("GATE does not start its 16 steps at the bar's start");
    /* BYPASS: a 2 kHz tone through a 100 Hz low-pass, held off while BYPASS is */
    const fset = M.newFx();
    fset.on.FILTER = true; fset.knobs.FILTER[0] = -64; fset.on.LIMITER = false;
    const f = fresh(fset).p.fx, tr = Array.from({ length: 34 }, () => new Float32Array(128)), L = new Float32Array(128), R = new Float32Array(128);
    const level = (bypass) => {
        f.msg({ t: "bypass", on: bypass });
        let s = 0;
        for (let b = 0; b < 40; b++) {
            for (let i = 0; i < 128; i++) tr[0][i] = .3 * Math.sin(2 * Math.PI * 2000 * (b * 128 + i) / SR);
            f.process(tr, L, R, b * 128 / SR);
            if (b >= 20) for (let i = 0; i < 128; i++) s += L[i] * L[i];
        }
        return Math.sqrt(s / (20 * 128));
    };
    const cut = level(false), open = level(true), byp = 20 * Math.log10(open / cut);
    console.log(`BYPASS: the tone ${byp.toFixed(1)} dB louder with the chain off`);
    if (!(byp > 20)) fail("BYPASS does not take the chain off");
}

/* ------------------------------ DRIVE's and DUCK's levels, as the FM-1's (specs/ui/screens/fx.md) --- */
{
    const tr = () => Array.from({ length: 34 }, () => new Float32Array(128));
    /* DRIVE 25 %, TONE open: a quiet 1 kHz tone comes out x 1.172 (+1.38 dB): the drive x the make-up */
    const dset = M.newFx();
    dset.on.LIMITER = false; dset.knobs.DRIVE = [25, 100];
    const dLevel = on => {
        dset.on.DRIVE = on;
        const f = fresh(dset).p.fx, t = tr(), L = new Float32Array(128), R = new Float32Array(128);
        let s = 0;
        for (let b = 0; b < 60; b++) {
            for (let i = 0; i < 128; i++) t[0][i] = .01 * Math.sin(2 * Math.PI * 1000 * (b * 128 + i) / SR);
            f.process(t, L, R, b * 128 / SR);
            if (b >= 30) for (let i = 0; i < 128; i++) s += L[i] * L[i];
        }
        return Math.sqrt(s / (30 * 128));
    };
    const dg = 20 * Math.log10(dLevel(true) / dLevel(false));
    console.log(`DRIVE 25 %: a quiet tone ${dg >= 0 ? "+" : ""}${dg.toFixed(2)} dB`);
    if (Math.abs(dg - 1.38) > .1) fail(`DRIVE 25 %: a quiet tone ${dg.toFixed(2)} dB, not +1.38`);
    /* DUCK: track 00 a loud burst for 0.2 s (muted: a muted source still ducks), track 01 a steady level
     * ducking fully (DEPTH 100 %, DCK 7F); RELEASE 200 ms: silent under the burst, half way back 0.1 s after
     * it, whole after 0.2 s */
    const kset = M.newFx();
    kset.on.LIMITER = false; kset.on.DUCK = true; kset.knobs.DUCK = [0, 100, 200];
    const mixer = [{ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }, { lvl: 127, pan: 128, dck: 127, cho: 0, dly: 0, rev: 0 }];
    const f = fresh(kset).p.fx, t = tr(), L = new Float32Array(128), R = new Float32Array(128);
    f.msg({ t: "fx", setup: kset, mixer, base: 0, tempo: 120, gates: [] });
    f.msg({ t: "mute", track: 0, on: true });
    const env = [];
    for (let b = 0; b < 200; b++) {
        for (let i = 0; i < 128; i++) {
            const n = b * 128 + i;
            t[0][i] = n < .2 * SR ? .8 * Math.sin(2 * Math.PI * 60 * n / SR) : 0;
            t[1][i] = .1;                                    /* (a steady level: L shows its gain at once) */
        }
        f.process(t, L, R, b * 128 / SR);
        env.push(L[127] / (.1 * .5));                       /* (its gain: the mix takes a voice at half) */
    }
    const at = s => env[Math.round(s * SR / 128)];
    const under = at(.15), half = at(.3), back = at(.45);
    console.log(`DUCK: under the burst x${under.toFixed(2)}, 0.1 s after it x${half.toFixed(2)}, 0.25 s after it x${back.toFixed(2)}`);
    if (!(under < .05 && Math.abs(half - .5) < .05 && back > .99)) fail("DUCK does not follow its source as the FM-1 does (at once, then a straight fall over RELEASE)");
}

/* ------------------------------------- DJ: two decks, one pool, one mix (specs/ui/screens/dj.md) --- */
{
    /* the pool's priority, alone: a full pool, a note from either deck */
    const p = new Proc(), deckVoices = (left, right) => {
        p.pool = [];
        let age = 0;
        for (let k = 0; k < left; k++) p.pool.push({ on: true, track: k, age: ++age, rel: false, id: "s" + k });
        for (let k = 0; k < right; k++) p.pool.push({ on: true, track: 16 + k, age: ++age, rel: false, id: "d" + k });
        p.max = left + right;
    };
    const take = (track, xf, left, right) => {          /* (the voice taken fades out where it is: park "out") */
        deckVoices(left, right);
        p.dj = { left: 0, xf, sharp: false };
        const v = p.alloc({ track });
        if (!v) return "-";
        const t = p.pool.find(x => x.park === "out") || v;
        return t.track < 16 ? "L" : "R";
    };
    const a = take(0, 16, 4, 4), b = take(16, 16, 4, 4);
    const c = take(16, 0, 8, 0), d = take(16, 32, 8, 0);
    const e = take(0, 0, 0, 8);
    console.log(`DJ's pool, full: the middle, a left note takes ${a}, a right one ${b}; all left, the right faded out takes ${c}, ` +
                `faded in ${d}; all right, a left note heard takes ${e}`);
    if (a !== "R" || b !== "R") fail("DJ: in the middle the right deck does not give way");
    if (c !== "-") fail("DJ: a deck faded out takes a voice from the deck heard");
    if (d !== "L") fail("DJ: the left deck faded out does not give way");
    if (e !== "R") fail("DJ: the heard deck cannot take from the faded one");

    /* two loops on the decks, synced, through the replay: the mix, PREVIEW, the crossfader, BRAKE */
    const ix = n => M.state.songs.findIndex(s => s.name === n);
    const [L, R] = P.dj.sides;
    cur = fresh(M.newFx());
    P.djLoad(L, ix("TRANCE LOOP"));
    P.djLoad(R, ix("BREAKS LOOP"));
    P.dj.sync = true;
    const run = (seconds, at) => {
        const n = Math.round(seconds * SR / 128), out = { L: new Float32Array(n * 128), R: new Float32Array(n * 128), most: 0, nan: 0, peak: 0, decks: [0, 0] };
        let next = wk.currentTime;
        for (let b = 0; b < n; b++) {
            if (at) at(b * 128 / SR);
            if (wk.currentTime >= next) { tickFn(); next += .02; }
            const l = new Float32Array(128), r = new Float32Array(128);
            cur.p.process([], [[l, r]]);
            out.L.set(l, b * 128); out.R.set(r, b * 128);
            for (let i = 0; i < 128; i++) { if (!isFinite(l[i]) || !isFinite(r[i])) out.nan++; out.peak = Math.max(out.peak, Math.abs(l[i]), Math.abs(r[i])); }
            wk.currentTime += 128 / SR;
            for (const v of cur.p.pool) if (v.on && v.track < 32) out.decks[v.track >> 4] = 1;
            if (cur.p.stats) out.most = Math.max(out.most, cur.p.stats.voices.length);
        }
        return out;
    };
    const rms = (x, a = 0, b = x.length) => { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return 20 * Math.log10(Math.sqrt(s / (b - a)) + 1e-9); };
    P.djPlay(L);
    const lStart = L.r.t;
    let rStart = -1;
    const both = run(6, t => { if (rStart < 0 && t >= .3) { P.djPlay(R); rStart = R.r ? R.r.t : 0; } });
    const bar = lStart + 60 / P.djBpm(L) * 4;
    console.log(`DJ: two loops synced, ${rms(both.L).toFixed(1)} dB RMS, peak ${both.peak.toFixed(2)}, most ${both.most} voices, ` +
                `both decks sounding ${both.decks.join("")}; the right started at ${rStart.toFixed(3)} s (the left's bar line ${bar.toFixed(3)} s)`);
    if (both.nan) fail("DJ: " + both.nan + " non-finite samples");
    if (both.peak > 1) fail("DJ: the output passes full scale");
    if (rms(both.L) < -40) fail("DJ: two decks nearly silent");
    if (both.decks.join("") !== "11") fail("DJ: a deck plays no voices");
    if (Math.abs(rStart - bar) > .03) fail("DJ: the synced deck did not start on the main's bar line");
    /* SYNC holds the beats together, also as the main's tempo is turned (the right follows it) */
    const phase = d => { const h = P.djHeard(d); return ((h.row + (wk.currentTime - h.t) / h.dur) / h.rpb) % 1; };
    const apart = () => { const x = Math.abs(phase(L) - phase(R)); return Math.min(x, 1 - x); };
    const p0 = apart();
    P.djTempo(L, 60);                                          /* +6 BPM */
    run(4);
    const p1 = apart();
    console.log(`DJ: SYNC, the beats apart by ${p0.toFixed(3)} of a beat; the main 6 BPM faster, 4 s on: ${p1.toFixed(3)} (${P.djBpm(L).toFixed(1)} and ${P.djBpm(R).toFixed(1)} BPM)`);
    if (p0 > .02 || p1 > .02 || P.djBpm(R) !== P.djBpm(L)) fail("DJ: SYNC does not keep the right deck on the main's beats");
    /* the crossfader at the left's end and the left paused: the right deck alone, nothing else; then at the
     * right's end too: silence once the echoes and the reverb have rung out (its sends follow the crossfader) */
    P.djPause(L);
    P.djXf(32);
    run(4);
    P.djXf(-32);
    const gone = run(3);
    console.log(`DJ: the crossfader at the left's end, the left paused: ${rms(gone.L, Math.round(1.5 * SR)).toFixed(1)} dB`);
    if (rms(gone.L, Math.round(1.5 * SR)) > -70) fail("DJ: a deck faded out is still heard");
    /* BRAKE, held: the deck slows to a stop in 0.5 s and stands there silent, its place counting on; let go, it
     * plays on where it would have been */
    P.djXf(16);
    run(.5);
    P.djBrake(R);
    const br = run(1.5), standing = R.playing && !!R.brake && !!R.brake.held;
    P.djStopUp(R);
    const up = run(1);
    console.log(`DJ: BRAKE held, the right deck ${standing ? "stopped and standing" : "not standing"}, the last second ${rms(br.L, Math.round(.7 * SR)).toFixed(1)} dB; ` +
                `let go, ${rms(up.L, Math.round(.3 * SR)).toFixed(1)} dB`);
    if (!standing || rms(br.L, Math.round(.7 * SR)) > -50) fail("DJ: BRAKE held does not stop the deck and keep it silent there");
    if (!R.playing || R.brake || rms(up.L, Math.round(.3 * SR)) < -45) fail("DJ: BRAKE let go does not play on");
    P.djPause(L); P.djPause(R);
    /* SYNC at double: an 86 BPM main, a 172 BPM song on the right plays at 172, its beats on the main's halves */
    if (ix("HIP-HOP LOOP") >= 0 && ix("DNB LOOP") >= 0) {
        P.djLoad(L, ix("HIP-HOP LOOP")); P.djLoad(R, ix("DNB LOOP"));
        P.dj.sync = false; P.djSync();
        P.djPlay(L); run(.2); P.djPlay(R); run(6);
        const half = () => { const a = phase(L) * 2 % 1, b = phase(R), x = Math.abs(a - b); return Math.min(x, 1 - x); };
        console.log(`DJ: SYNC at ${P.dj.mult}x: ${P.djBpm(L).toFixed(1)} and ${P.djBpm(R).toFixed(1)} BPM, the right's beats ${half().toFixed(3)} of a beat off the main's halves`);
        if (P.dj.mult !== 2 || half() > .03) fail("DJ: SYNC's double does not keep the right on the main's half beats");
        P.djPause(L); P.djPause(R);
    }
    /* the LFO pads' swing, in the worklet (specs/ui/screens/dj.md#the-lfo-pads): what it reports each block */
    const watch = (seconds, f) => {
        const n = Math.round(seconds * SR / 128), out = [];
        let next = wk.currentTime;
        for (let b = 0; b < n; b++) {
            if (wk.currentTime >= next) { tickFn(); next += .02; }
            cur.p.process([], [[new Float32Array(128), new Float32Array(128)]]);
            wk.currentTime += 128 / SR;
            out.push({ t: wk.currentTime, v: f() });
        }
        return out;
    };
    const span = w => w.reduce((a, x) => [Math.min(a[0], x.v), Math.max(a[1], x.v)], [Infinity, -Infinity]);
    const fxw = cur.p.fx;
    P.djLoad(L, ix("TRANCE LOOP")); P.djLoad(R, ix("BREAKS LOOP"));
    P.dj.sync = true; P.djXf(16 - P.dj.xf);
    P.djPlay(L); run(.3);
    {
        /* the crossfader's: SINE, L TO R, from the middle, on the main's next row; a swing every two beats (1/2) */
        const F = P.dj.lfo.FADER;
        Object.assign(F, { shape: 0, range: 1, rev: false });
        const on = wk.currentTime;
        P.djLfoOn(F);
        const hz = P.djLfoHz(F), w = watch(3, () => cur.p.dj.xfNow), [lo, hi] = span(w);
        const first = w.find(x => Math.abs(x.v - 16) > .05), ups = w.filter((x, i) => i && w[i - 1].v < 16 && x.v >= 16).length;
        const x0 = cur.p.dj.xfNow;                       /* (read first: the message switching it off is a new DJ mix) */
        P.djLfoOn(F);
        const off = watch(.3, () => cur.p.dj.xfNow), jump = Math.abs(off[0].v - x0), back = off.find(x => Math.abs(x.v - 16) < .05);
        console.log(`DJ: the crossfader's LFO (SINE L TO R, ${hz.toFixed(3)} Hz): set off ${((first ? first.t : 0) - F.at).toFixed(3)} s from its step ` +
                    `(${(F.at - on).toFixed(3)} s after the press), swung ${lo.toFixed(1)} to ${hi.toFixed(1)}, ${ups} times up through the middle in 3 s; ` +
                    `off: ${jump.toFixed(2)} in the first block, home after ${back ? (back.t - off[0].t + 128 / SR).toFixed(3) : "never"} s`);
        if (!first || first.t < F.at || first.t > F.at + .02) fail("DJ: the crossfader's LFO does not set off on its step");
        if (lo > 1 || hi < 31) fail("DJ: the crossfader's LFO in L TO R does not swing end to end");
        if (Math.abs(ups - 3 * hz) > 1) fail("DJ: the crossfader's LFO does not swing at its speed");
        if (jump > 2 || !back || back.t - off[0].t > .25) fail("DJ: the crossfader's LFO switched off does not glide home within 0.2 s");
    }
    {
        /* the left filter's, its FILTER key on at LP -20: OFF TO HERE (at first), then SWING (to HP 20); with the key
         * off, the filter heard stays OFF while the swing runs on unheard (the preview); the key on again: heard
         * where the swing is, no restart */
        const lf = L.lfo;
        P.djFilter(L, -20);
        if (!L.filterOn) { P.djFilterSwitch(L); P.djFilterSettle(L); }   /* (on at first, at OFF: the knob heard) */
        lf.shape = 2;
        P.djLfoOn(lf);
        const [lo, hi] = span(watch(3, () => fxw.lfoNow.filt[L.i]));
        lf.range = 3; P.djMix();
        watch(.1, () => 0);
        const [slo, shi] = span(watch(3, () => fxw.lfoNow.filt[L.i]));
        P.djFilterSwitch(L); P.djFilterSettle(L);
        const offW = watch(1, () => fxw.lfoNow.filt[L.i]), rest = span(offW.slice(Math.round(.1 * SR / 128)));   /* (past the 15 ms glide) */
        const [plo, phi] = span(watch(2, () => fxw.lfoNow.prev[L.i]));
        P.djFilter(L, -10);                                     /* (the knob, the filter off: the preview follows the setting) */
        watch(.1, () => 0);
        const [qlo, qhi] = span(watch(2, () => fxw.lfoNow.prev[L.i]));
        P.djFilter(L, 10);
        watch(.1, () => 0);
        console.log(`DJ: the filter off, its knob to LP -30: the preview swings ${qlo.toFixed(1)} to ${qhi.toFixed(1)}`);
        if (Math.abs(qlo + 30) > .5 || Math.abs(qhi - 30) > .5) fail("DJ: the filter LFO's preview does not follow the filter's knob");
        P.djFilterSwitch(L); P.djFilterSettle(L);
        const back = watch(.1, () => [fxw.lfoNow.filt[L.i], fxw.lfoNow.prev[L.i]]), last = back[back.length - 1].v;
        console.log(`DJ: the left filter's LFO at LP -20: OFF TO HERE swung ${lo.toFixed(1)} to ${hi.toFixed(1)}, SWING ${slo.toFixed(1)} to ${shi.toFixed(1)}; ` +
                    `its FILTER key off: heard ${rest[0].toFixed(2)} to ${rest[1].toFixed(2)}, the preview ${plo.toFixed(1)} to ${phi.toFixed(1)}; ` +
                    `on again: heard ${last[0].toFixed(1)} where the swing is (${last[1].toFixed(1)})`);
        if (Math.abs(lo + 20) > .5 || Math.abs(hi) > .5) fail("DJ: a filter's LFO in OFF TO HERE does not swing between OFF and the filter");
        if (Math.abs(slo + 20) > .5 || Math.abs(shi - 20) > .5) fail("DJ: a filter's LFO in SWING does not swing to the filter mirrored");
        if (Math.abs(rest[0]) > .05 || Math.abs(rest[1]) > .05) fail("DJ: a filter's LFO is heard with its FILTER key off");
        if (Math.abs(plo + 20) > .5 || Math.abs(phi - 20) > .5) fail("DJ: a filter's LFO does not swing on unheard with its FILTER key off (the preview)");
        if (Math.abs(last[0] - last[1]) > .5) fail("DJ: the FILTER key on again does not bring the swing in where it is");
        P.djLfoOn(lf);
    }
    {
        /* the FX LFO: the REVERB's MIX between its setting (35 %) and 100 %; with DJ's REVERB off, nothing */
        const X = P.dj.lfo.FX, set = M.state.dj.fx.knobs.REVERB[3] / 100;
        Object.assign(X, { shape: 2, range: 0 });
        P.djLfoOn(X);
        const [lo, hi] = span(watch(3, () => fxw.lfoNow.mix));
        M.state.dj.fx.on.REVERB = false; A.fxNow();
        const offW = watch(.1, () => fxw.lfoNow.mix);
        M.state.dj.fx.on.REVERB = true; A.fxNow();
        console.log(`DJ: the FX LFO on the REVERB's MIX (set ${set.toFixed(2)}): swung ${lo.toFixed(2)} to ${hi.toFixed(2)}; the REVERB off: ${offW[offW.length - 1].v}`);
        if (Math.abs(lo - set) > .02 || hi < .98) fail("DJ: the FX LFO does not swing the REVERB's MIX between its setting and 100 %");
        if (offW[offW.length - 1].v !== null || fxw.lfoMix) fail("DJ: the FX LFO acts on an effect switched off");
        P.djLfoOn(X);
    }
    {
        /* the CUT held (HOME + REC): silence within ~12 ms, no note starting, the decks keeping their time; the tails
         * cut, so let go with nothing playing it stays silent (against the same without it: the reverb ringing on) */
        P.djPlay(R); run(1);
        const where = () => { const a = P.djState(R).at; return a.pos * 256 + a.row; }, pos0 = where();
        P.djHush(true);
        const hush = run(.5), voices = cur.p.pool.filter(v => v.on).length, moved = R.playing && where() > pos0;
        P.djHush(false);
        const heard = run(.5);
        /* (the decks' notes cut, the effects left ringing: all sound off would empty their tails too) */
        const notesOff = () => { A.cutAt("s", wk.currentTime); A.cutAt("d", wk.currentTime); };
        P.djPause(L); P.djPause(R); run(.3); notesOff();
        const tail = run(1.1);
        P.djPlay(L); P.djPlay(R); run(1);
        P.djPause(L); P.djPause(R); run(.3); notesOff(); P.djHush(true); run(.1); P.djHush(false);
        const after = run(1);
        const at = Math.round(.02 * SR);
        console.log(`DJ: the CUT held, ${rms(hush.L, at).toFixed(1)} dB after 20 ms, ${voices} voices, the decks ${moved ? "moving on" : "STOPPED"}; let go ${rms(heard.L, Math.round(.1 * SR)).toFixed(1)} dB; ` +
                    `the decks paused, their notes cut: the tail ${rms(tail.L, Math.round(.1 * SR)).toFixed(1)} dB, with the CUT held a moment ${rms(after.L, Math.round(.1 * SR)).toFixed(1)} dB`);
        if (rms(hush.L, at) > -90 || voices || !moved) fail("DJ: the CUT held is not silent, a note started, or the decks stopped");
        if (rms(heard.L, Math.round(.1 * SR)) < -45) fail("DJ: REC let go, the decks are not heard again");
        if (rms(tail.L, Math.round(.1 * SR)) < -75) fail("DJ: no effects' tail after a pause (the REC check needs one)");
        if (rms(after.L, Math.round(.1 * SR)) > -90) fail("DJ: REC does not cut the effects' tails");
        P.djPause(L); P.djPause(R);
    }
}
/* the effect under the FX page's cursor (specs/ui/screens/fx.md#the-fx-setup): another setup (a song's start) keeps it
 * as it plays, the others take the new one's; with none under the cursor, the new setup whole */
{
    const a = M.newFx(), b = M.newFx();
    a.knobs.FILTER[0] = -20; a.on.FILTER = true; a.knobs.REVERB[0] = 10;
    b.knobs.FILTER[0] = 30; b.on.FILTER = false; b.knobs.REVERB[0] = 77;
    const f = fresh(a).p.fx, setup = (s, id) => ({ t: "fx", setup: s, mixer: [], base: 0, tempo: 120, gates: [], id });
    f.msg(setup(a, 0));
    f.msg({ t: "mine", fx: "FILTER" });
    f.msg({ t: "fxreset" });
    f.msg(setup(b, 1));
    const kept = f.over["FILTER/0"] === -20 && f.overOn.FILTER === true && f.over["REVERB/0"] === undefined && f.setup.knobs.REVERB[0] === 77;
    f.msg({ t: "mine", fx: null });
    f.msg({ t: "fxreset" });
    f.msg(setup(a, 0));
    const whole = f.over["FILTER/0"] === undefined && f.overOn.FILTER === undefined && f.setup.knobs.FILTER[0] === -20;
    console.log(`FX under the cursor: a new setup keeps FILTER as it played ${kept ? "yes" : "NO"}, REVERB the new one's; none under it: the setup whole ${whole ? "yes" : "NO"}`);
    if (!kept) fail("FX: another setup does not keep the effect under the FX page's cursor as it plays");
    if (!whole) fail("FX: with no effect under the cursor, a new setup is not taken whole");
}
console.log(failed ? failed + " FAILED" : "ALL PASSED");
process.exit(Math.min(failed, 100));
