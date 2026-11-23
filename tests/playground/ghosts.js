/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's ghost notes (specs/ui/screens/settings.md#voices) without a browser: the worklet's pool
 * (playground/dsp-core.js) run as sound.js runs it, and the replay (player.js) on a stand-in for audio.js on the
 * worklet's clock. The firmware's cases of the pool (fw/tests/sound/voices_test.c, fw/tests/lanes_test.c) on the
 * playground's twin: the same scenarios, the same order, the counts per its own blocks (128 frames, where its rules
 * count per 64 frames: twice a block its returns, its budget, its fades). Then the songs and DJ's decks with
 * their notes made ghosts all the time against the same with every note in an engine, block by block (voices_test's
 * differential test), and the AUTOTEST's GHOST NOTES stretches (fw/web/autotest.js) on the twin's pool.
 *   node tests/playground/ghosts.js */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "../.."), PG = path.join(ROOT, "playground"), SR = 44100, BLK = 128, U = 64;

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
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage: m => { if (m.t === "stats") this.stats = m; }, onmessage: null }; } },
    registerProcessor: (n, c) => { Proc = c; },
};
vm.createContext(wk);
/* (the worklet's random numbers from a seed, set again at each fresh(): a DX7 LFO with no key sync starts at a random
 * phase, so the takes, which go by the notes' levels, are the same every run) */
vm.runInContext("var rngSeed = 1; Math.random = () => (rngSeed = rngSeed * 16807 % 2147483647) / 2147483647;", wk);
const dsp = {};
dsp.window = dsp;
vm.createContext(dsp);
for (const f of ["dsp-core.js", "dsp-dx7.js", "dsp-synth.js", "dsp-chip-tables.js", "dsp-chip.js", "dsp-fx.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), dsp, { filename: f });
vm.runInContext("const E = {};\n" + dsp.PTFM.dsp.map(f => "(" + f.toString() + ")(E);").join("\n") + "\nregisterProcessor('ptfm', E.Proc); this.E = E;\n",
                wk, { filename: "worklet.js" });
const WE = wk.E;
/* the pool's ranks and states (dsp-core.js's): the list's drop, the ghosts */
const GR_LET = 0, GR_MUTED = 1, GR_SONG = 2, GR_KEY = 3, GHOST = 1, BACKING = 2, KEYS = WE.KEYS;
const dry = M.newFx();

let failed = 0, passed = 0;
const check = (what, ok) => { console.log((ok ? "PASS " : "FAIL ") + what); if (ok) passed++; else failed++; };
/* the returns' rules cases written before the FM-1's weights (the order, 8 a block, the tails' fades, the returns after a
 * mute, a solo or a VOICES change, the waits they count): run with the budget out of their way (it never stops a walk
 * there); every other case on the FM-1's own budget (the twin's BUDGET), as voices_test.c does */
const rules = (on) => { WE.budget = on ? 65536 : WE.BUDGET; };

/* a worklet from the start (VOICES n), the demo's samples in it */
function fresh(n = 12, setup = dry) {
    wk.currentTime = 0;
    wk.rngSeed = 1;
    const p = new Proc(), send = m => p.port.onmessage({ data: m });
    const list = [];
    M.state.samples.forEach((s, k) => { if (s) list.push({ key: k, data: M.audio[s.audio], rate: s.rate }); });
    send({ t: "samples", list });
    send({ t: "voices", n });
    send({ t: "master", v: .5 });
    send({ t: "fx", setup, mixer: M.state.songs[0xA0].mixer, base: 0, tempo: 120, gates: M.state.gates.map(g => g.steps) });
    return { p, send };
}
function inst(ref) {                                   /* (as audio.js sends an instrument) */
    const c = M.cart(ref >> 8), i = M.inst(ref);
    return { t: "inst", ref, engine: c.type, p: c.type === "SAMPLER" ? Object.assign({ key: M.sample(i.p.sample) ? i.p.sample : null }, i.p) : i.p,
             level: i.level };
}
/* n blocks of the worklet (its sound: the keys' track's and the mix's) */
function blocks(p, n = 1) {
    let pk = 0;
    for (let b = 0; b < n; b++) {
        const l = new Float32Array(BLK), r = new Float32Array(BLK);
        p.process([], [[l, r]]);
        for (let i = 0; i < BLK; i++) pk = Math.max(pk, Math.abs(l[i]));
        wk.currentTime += BLK / SR;
    }
    return pk;
}
const secs = s => Math.round(s * SR / BLK);            /* (the blocks in s seconds) */
const now = () => wk.currentTime;
/* note m started at once in engine c (fresh), or as a ghost from its start (c < 0): the firmware's test sets a pool by
 * hand so (its note_at); a key's unless m says otherwise */
function at(p, m, c) {
    p.now = p.frame;
    if (c >= 0) { const e = p.en[c]; e.kind = e.st = e.last = e.voice = e.wait = null; }
    p.alloc = function () { this.alC = c; this.alBorn = c < 0; return true; };
    try { p.noteOn(Object.assign({ vol: 127, time: now(), track: -1, key: true }, m)); } finally { delete p.alloc; }
    return p.byId.get(m.id);
}
/* the returns held off, as the firmware's test moves its block clock without the voices pass's returns; on again */
const noReturns = p => { p.returns = () => {}; };
const returnsOn = p => { delete p.returns; };
const heard = v => { v.heard = true; v.gLast = v.gMix = 1; };   /* (rendered, its gain full: a take fades it) */
const near = (a, b, t = 1e-6) => Math.abs(a - b) <= t * Math.max(1, Math.abs(b));
/* engine state a as b, as far as the forward work keeps it (fw/tests/sound/voices_test.c's env_same): the Juno-106's
 * envelope, LFO, delay, VCA and width; the sampler's envelope and its place; the CHIP's tables, envelopes, release and
 * frames */
const JUNO_KEPT = ["env", "envState", "lfoAcc", "lfoRising", "lfoPol", "holdAcc", "inHold", "rampAcc", "lfoAmp", "dac", "vcaT", "pw"];
const CHIP_KEPT = ["f_acc", "tp", "tc", "frames", "rel_c", "rlev", "evol", "e_acc", "e_pos", "e_r", "nx_e", "len_e", "s_acc",
                   "reg", "len_x", "sweeping", "sw_off", "dead", "gate", "lvol", "arp", "cents", "duty", "nrate", "nb", "silent"];
const same = (x, y) => Array.isArray(x) || ArrayBuffer.isView(x) ? x.length === y.length && x.every((e, i) => e === y[i]) : x === y;
function envSame(kind, a, b) {
    if (!a || !b) return false;
    if (kind === "JUNO") return JUNO_KEPT.every(k => a[k] === b[k] || (typeof a[k] === "number" && near(a[k], b[k], 1e-9)));
    if (kind === "SAMPLER")
        return !!a.dead === !!b.dead && a.env.level === b.env.level && a.env.ix === b.env.ix && a.env.st === b.env.st &&
               near(a.pos, b.pos, 1e-3) && a.dir === b.dir && a.looping === b.looping;
    if (a.dead || b.dead) return !!a.dead && !!b.dead;
    return CHIP_KEPT.every(k => same(a[k], b[k]));
}
const placeSame = (x, y) => !!x && !!y && !!x.ended === !!y.ended && (x.ended || (near(x.pos, y.pos, 1e-3) && x.dir === y.dir && x.looping === y.looping));
const ref = (cart, slot) => cart << 8 | slot;
const R = M.refText;
/* the instruments the firmware's voices_test picks on the demo disk (its own tests choose them by their sound; here by
 * number, so both run the same instruments) */
const DXS = ref(0xF0, 4), DX2 = ref(0xF0, 5), PAD = ref(0xF8, 1), SLOW = ref(0xF8, 0x1F), LONG = ref(0xF8, 0x13);
const LOOPS = ref(0xA2, 8), SHOT = ref(0xA0, 0), CHS = ref(0xF9, 0), VT = ref(0xF9, 0x18), QUICK = ref(0xF0, 0);
const JU0 = ref(0xF8, 0), TB0 = ref(0xFA, 0);          /* (the first of the built-in JUNO-106 and TB-303 cartridges) */
function rmsDb(x, a = 0, b = x.length) {
    let s = 0;
    for (let i = a; i < b; i++) s += x[i] * x[i];
    return 10 * Math.log10(s / (b - a) + 1e-20);
}

/* ---------------------------------------------------------------------------------------------- the pool --- */
/* the pool past VOICES, as voices_test's first cases, each engine: at 8 voices, note 60 of an instrument that sustains
 * held, 8 more keys of it at 0.5 s: 60 is taken, a ghost, unheard at 0.9 s. The 8 let go at 1 s: 60 comes back, alone
 * again at 2.5 s, where its envelope would be (within 1 dB of 60 never taken over 2.5-3 s). Let go: 60 of an instrument
 * still heard 0.7 s after its let-go let go at 0.8 s, a ghost, the 8 keys of an instrument whose release is over at once
 * (the takers) let go at 0.9 s: alone again at 1.5 s, back in its release (within 1 dB of 60 alone over 1.5-2 s). A
 * sampler back stands where the note never taken does, at the same frame (its place walked). The instruments are the
 * firmware's test's; each run starts the worklet afresh (the Juno-106's chorus and noise too, as the FM-1's test starts
 * them). Then a made sampler and a made CHIP whose level still moves there (2 dB from where it was 1 s before: a note
 * started again at its return, its time not worked, would sound as that) */
{
    const sameAt = (x, y) => x != null && y != null && near(x, y);   /* (a sampler's place, at the same frame) */
    const sine4 = new Int8Array(4 * SR);                /* (a made sampler's sample; its base note under 60: a step not whole) */
    for (let i = 0; i < sine4.length; i++) sine4[i] = Math.round(127 * Math.sin(2 * Math.PI * i / 100));
    const demo = r => ({ ref: r, msg: inst(r), sample: M.cart(r >> 8).type === "SAMPLER" });
    const MADE = {
        SAMPLER: { ref: 0x7E00, made: "a sampler made for it", sample: true,
                   msg: { t: "inst", ref: 0x7E00, engine: "SAMPLER", level: 0, p: { key: "sine4", sample: 0, start: 0, end: 4 * SR, loop: "FORWARD",
                          loopStart: 0, loopEnd: SR, base: 59, tune: 0, volume: 64, env: { rates: [99, 20, 99, 99], levels: [99, 50, 50, 0] } } } },
        CHIP: { ref: 0x7E01, made: `a CHIP made for it (${R(CHS)} with a decaying PSG envelope)`,
                msg: { t: "inst", ref: 0x7E01, engine: "CHIP", level: M.inst(CHS).level,
                       p: Object.assign(structuredClone(M.inst(CHS).p), { env: 3, psg_shape: 0, psg_period: 40000 }) } },
    };
    const nameOf = I => I.made || R(I.ref);
    /* the keys' track over the measure (at, 0.5 s) and 1 s before it; a ghost at 0.9 s; back alone at `at`; the place */
    const take = (I, others, letGo, Q = I) => {
        const { p, send } = fresh(8);
        if (I.made) send({ t: "samples", list: [{ key: "sine4", data: sine4, rate: SR }] });
        send(I.msg);
        if (Q !== I) send(Q.msg);
        send({ t: "on", id: "a", time: .02, midi: 60, ref: I.ref, vol: 100, track: -1 });
        for (let k = 0; k < others; k++) send({ t: "on", id: "b" + k, time: .52, midi: 62 + k, ref: Q.ref, vol: 100, track: -1 });
        if (letGo) send({ t: "off", id: "a", time: .82 });
        for (let k = 0; k < others; k++) send({ t: "off", id: "b" + k, time: letGo ? .92 : 1.02 });
        const at = letGo ? 1.52 : 2.52, K = [];
        const run = s => { for (let b = 0; b < secs(s); b++) { blocks(p); K.push(...p.tracks[KEYS]); } };
        run(.9);
        p.post();
        const ghost = !p.stats.voices.some(v => v.id === "a") && p.stats.ghostsAll === 1 && p.stats.ghosts === (letGo ? 0 : 1);
        run(at - .9);
        p.post();
        const back = p.stats.ghostsAll === 0 && p.stats.voices.length === 1 && p.stats.voices[0].id === "a" && p.stats.voices[0].rel === letGo;
        const a = p.vs.find(v => v.id === "a"), pos = a && a.e >= 0 && I.sample ? p.en[a.e].st.pos : null;
        const n0 = K.length;
        run(.5);
        return { db: rmsDb(K.slice(n0)), early: rmsDb(K.slice(Math.round((at - 1) * SR), Math.round((at - .5) * SR))), ghost, back, pos };
    };
    /* the firmware's: held, let go (an instrument heard 0.7 s after its let-go; the disk has no sampler's: one made for
     * it, the same sine, its release slow) */
    const PICK = { DX7: [ref(0xF0, 4), ref(0xF0, 2)], JUNO: [ref(0xF8, 1), ref(0xF8, 0x13)], SAMPLER: [ref(0xA2, 8), 0x7E02],
                   CHIP: [ref(0xF9, 0), ref(0xF9, 0x42)] };
    MADE[0x7E02] = { ref: 0x7E02, made: "a sampler made for it, its release slow", sample: true,
                     msg: { t: "inst", ref: 0x7E02, engine: "SAMPLER", level: 0, p: Object.assign({}, MADE.SAMPLER.msg.p, { env: { rates: [99, 99, 99, 20], levels: [99, 99, 99, 0] } }) } };
    const quick = demo(QUICK);
    {   /* (the takers: sounding held, silent 0.2 s after their let-go) */
        const { p, send } = fresh();
        send(quick.msg);
        send({ t: "on", id: "q", time: 0, midi: 60, ref: QUICK, vol: 100, track: -1 });
        send({ t: "off", id: "q", time: .3 });
        const K = [], run = s => { K.length = 0; for (let b = 0; b < secs(s); b++) { blocks(p); K.push(...p.tracks[KEYS]); } return rmsDb(K); };
        const held = run(.3);
        run(.2);
        const rest = run(.2);
        check(`the takers ${R(QUICK)}: sounding held (${held.toFixed(1)} dB), silent 0.2 s after their let-go (${rest.toFixed(1)} dB)`, held > -45 && rest < -100);
    }
    for (const type of ["DX7", "JUNO", "SAMPLER", "CHIP"]) {
        const held = demo(PICK[type][0]), lin = MADE[PICK[type][1]] || demo(PICK[type][1]);
        const alone = take(held, 0, false), taken = take(held, 8, false), aloneGo = take(lin, 0, true, quick), gone = take(lin, 8, true, quick);
        const twice = take(held, 0, false);
        console.log(`the pool at 8 voices, ${type} ${R(held.ref)}: alone ${alone.db.toFixed(2)} dB (twice ${twice.db.toFixed(2)}; 1 s before ` +
                    `${alone.early.toFixed(2)}), a ghost and back ${taken.db.toFixed(2)} dB; ${nameOf(lin)} let go at 0.8 s (the takers ` +
                    `${R(QUICK)}): alone ${aloneGo.db.toFixed(2)} dB, as a ghost ${gone.db.toFixed(2)} dB` +
                    (held.sample ? `; the place, held: alone ${alone.pos}, back ${taken.pos}` : "") +
                    (lin.sample ? `; let go: alone ${aloneGo.pos}, back ${gone.pos}` : ""));
        check(`${type}: a run alone sounds the same twice (each worklet afresh)`, Math.abs(twice.db - alone.db) < .01);
        check(`${type}: a 9th key takes the oldest's engine, a ghost, unheard`, taken.ghost);
        check(`${type}: once an engine is free it comes back`, taken.back);
        check(`${type}: where its envelope would be (within 1 dB)`, Math.abs(taken.db - alone.db) <= 1 && alone.db > -60);
        if (held.sample) check(`${type}: back where the note never taken is (its place)`, sameAt(taken.pos, alone.pos));
        check(`${type}: let go as a ghost, a ghost let go`, gone.ghost);
        check(`${type}: let go as a ghost, it comes back in its release`, gone.back);
        check(`${type}: let go as a ghost, back in its release (within 1 dB)`, Math.abs(gone.db - aloneGo.db) <= 1);
        if (lin.sample) check(`${type}: a ghost let go back where the note never taken is (its place)`, sameAt(gone.pos, aloneGo.pos));
    }
    for (const type of ["SAMPLER", "CHIP"]) {
        const I = MADE[type], alone = take(I, 0, false), taken = take(I, 8, false);
        console.log(`the pool at 8 voices, ${nameOf(I)}: alone ${alone.db.toFixed(2)} dB (1 s before ${alone.early.toFixed(2)}), a ghost and back ` +
                    `${taken.db.toFixed(2)} dB` + (I.sample ? `; the place: alone ${alone.pos}, back ${taken.pos}` : ""));
        check(`${nameOf(I)}: its level still moving at the measure (2 dB in 1 s)`, Math.abs(alone.early - alone.db) >= 2);
        check(`${nameOf(I)}: a ghost and back, where its envelope would be (within 1 dB)`, taken.ghost && taken.back && Math.abs(taken.db - alone.db) <= 1);
        if (I.sample) check(`${nameOf(I)}: back where the note never taken is (its place)`, sameAt(taken.pos, alone.pos));
    }
}

/* ------------------------------------------ voices_test's cases of the proposal's section 11, on the twin --- */
/* the list full, every rank (drop_ranks): 24 ghosts set by hand, of every rank (S a song's held, M a muted track's, K a
 * key's held, L let go 300, 100 or 50 FM-1 blocks ago); one let go before them all is not kept; then a key's held ghost
 * 24 times over: the one to go each time is the let go (longest ago first), the muted, the songs' held, then the keys'
 * held, each a ghost longest first; with the keys' held alone, a song's held or a muted one is not kept */
{
    const KIND = "SMKLKSKKSKLKMKKSKLKKMKSK", WANT = [10, 3, 17, 1, 12, 20, 0, 5, 8, 15, 22];
    const { p, send } = fresh();
    send(inst(DXS));
    blocks(p, 210);                                     /* (room for the ages: 400 FM-1 blocks) */
    send({ t: "mute", track: 0, on: true });
    let n = 0;
    const put = (kind, ago) => {
        const g = at(p, { id: "g" + n++, ref: DXS, midi: 40 + n % 40, track: kind === "K" ? -1 : kind === "M" ? 0 : 1, key: kind === "K" }, -1);
        if (kind === "L") { g.held = false; g.tOff = p.now - ago * U; }
        return g;
    };
    const g = [...KIND].map((k, i) => put(k, i === 3 ? 100 : i === 10 ? 300 : 50));
    p.now = p.frame;
    let refused = !p.room(GR_LET, p.now - 400 * U) && p.gl.length === 24, order = true, drops = 0;
    for (let i = 0; i < 24; i++) {
        if (!p.room(GR_KEY, -1)) { order = false; break; }
        const gone = g.findIndex(x => x && !p.vs.includes(x));
        if (gone >= 0) g[gone] = null;
        order = order && (drops < 11 ? gone === WANT[drops] : gone >= 0 && KIND[gone] === "K" && g.slice(0, gone).every((x, k) => KIND[k] !== "K" || !x));
        drops++;
        put("K");
        p.now = p.frame;
        if (drops === 11) refused = refused && !p.room(GR_SONG, -1) && !p.room(GR_MUTED, -1) && p.gl.length === 24;
    }
    console.log(`the list full, every rank: dropped in the order of section 6 ${order} (${drops} drops); not kept when below them all ${refused}`);
    check("the list full: let go (longest ago first), muted, songs' held, keys' held, each a ghost longest first", order && drops === 24 && refused);
}
/* returns into tails' engines as many a block as the block has fades left (fade_limit; the takes' fades first): VOICES
 * 12, the 12 engines release tails heard, 12 keys' held ghosts: 8 claim tails (fading out there, the ghosts' returns
 * waiting: 4 fades each 64 frames, 8 a block here), the other 4 wait; with 3 takes made in the block first, 5 */
rules(true);
{
    const got = [0, 0], still = [0, 0];
    let ok = true;
    for (const pre of [0, 1]) {
        const { p, send } = fresh(12);
        send(inst(DXS));
        for (let c = 0; c < 12; c++) { const v = at(p, { id: "t" + c, ref: DXS, midi: 70 + c }, c); p.release(v); heard(v); v.peak = .015; }
        const g = [];
        for (let k = 0; k < 12; k++) g.push(at(p, { id: "g" + k, ref: DXS, midi: 40 + k }, -1));
        p.ageSeen = p.age;                              /* (every note heard at its last block's end) */
        p.nfd = 0;
        for (let k = 0; pre && k < 3; k++) {            /* (three new keys' notes: each takes a tail, fading) */
            p.noteOn({ id: "n" + k, ref: DXS, midi: 90 + k, vol: 127, track: -1, key: true, time: now() });
            const v = p.byId.get("n" + k);
            ok = ok && !!v && v.e >= 0;
        }
        p.now = p.frame;
        p.returns();
        for (const e of p.en) got[pre] += e.fading > 0 && !!e.wait && e.wait.gh === BACKING ? 1 : 0;
        still[pre] = g.filter(x => x.gh === GHOST).length;
    }
    console.log(`returns into tails' engines at VOICES 12 (8 fades a block): ${got[0]} claimed, ${still[0]} waiting; with 3 takes' fades first: ` +
                `${got[1]} claimed, ${still[1]} waiting`);
    check("returns into tails' engines: as many a block as its fades left, the takes' first", ok && got[0] === 8 && still[0] === 4 && got[1] === 5 && still[1] === 7);
}
rules(false);
/* a take past the block's fades (take_past): VOICES 12, 12 keys held and heard; 10 more at one block's start: the
 * first 8 taken notes fade out in their engines in that block (4 each 64 frames), the other 2 are cut at once, all 10
 * go on as ghosts; the 10 new notes sound from that block */
{
    const { p, send } = fresh(12);
    send(inst(PAD));
    for (let k = 0; k < 12; k++) send({ t: "on", id: "k" + k, time: .01, midi: 36 + 2 * k, ref: PAD, vol: 100, track: -1, key: true });
    blocks(p, secs(.5));
    const s0 = p.steals, t = now();
    for (let k = 0; k < 10; k++) send({ t: "on", id: "n" + k, time: t, midi: 70 + 2 * k, ref: PAD, vol: 100, track: -1, key: true });
    blocks(p);
    const on = [...Array(10).keys()].every(k => { const v = p.byId.get("n" + k); return v && v.e >= 0; });
    p.post();
    console.log(`10 takes in a block at VOICES 12: ${p.nfd} fades, ${p.steals - s0} taken, ${p.stats.ghosts} ghosts, the new notes in their engines ${on}`);
    check("a take past the block's fades: cut at once (8 fades a block), each a ghost, the new notes sounding", p.nfd === 8 && p.steals - s0 === 10 &&
          p.stats.ghosts === 10 && on);
}
/* a note back, fading in, is not taken again (back_kept), pools set by hand at VOICES 8: the songs' held notes, engine
 * 3's back from a ghost (fading in) and the quietest: the next quietest goes; the keys' held, engine 0's the oldest and
 * fading in: the next oldest goes */
{
    const { p, send } = fresh(8);
    send(inst(DXS));
    const PEAK = [300, 200, 400, 10, 500, 500, 500, 500], t = [];
    for (let c = 0; c < 8; c++) { const v = at(p, { id: "s" + c, ref: DXS, midi: 60 + c, track: c, key: false }, c); heard(v); v.peak = PEAK[c] / 32768; t.push(v); }
    p.ageSeen = p.age;
    t[3].back = 2;
    const a = p.victim(-1, 9, false);
    for (const v of t) { v.track = KEYS; v.own = -1; v.back = 0; }
    t[0].back = 2;
    const b = p.victim(-1, -1, false);
    console.log(`a note fading back in: the songs' quietest taken instead (engine ${a ? a.e : -1}), the keys' oldest instead (engine ${b ? b.e : -1})`);
    check("a note back, fading in, is not taken again: the next of its kind goes", a === t[1] && b === t[1]);
}
/* a held ghost quiet at its return comes back all the same (quiet_held): a Juno-106 pad with a slow attack, a key's ghost
 * from its start with its level 0 (as a track's volume slid down), back a block on, under hearing; its level turned up:
 * it sounds */
{
    const { p, send } = fresh();
    send(inst(SLOW));
    const sp = M.inst(SLOW).p, slow = sp.env.a >= 50 && sp.env.s >= 100 && sp.vca.mode === "env";
    const g = at(p, { id: "k60", ref: SLOW, midi: 60 }, -1);
    g.vol = 0;
    blocks(p);
    const back = g.e >= 0 && g.held, under = back && p.quiet(g, p.en[g.e].st);
    send({ t: "ctl", id: "k60", time: now(), bend: 0, vol: 1 });
    const pk = blocks(p, 8);
    console.log(`a held ghost of ${R(SLOW)} (a slow attack ${slow}), its level 0, back a block on: back ${back}, under hearing then ${under}, its level up: ` +
                `its peak ${pk.toFixed(4)}`);
    check("a held ghost under hearing at its return comes back (never dropped for quiet), then rises", slow && back && under && pk > 0);
}
/* a release unmuted in time is heard (release_heard): a song's held note (track 0) muted at 1 s (a ghost), let go 0.1 s
 * on while muted (a ghost let go), heard again 0.3 s on, in its release: back in its release, its envelopes the note
 * never taken's (a key's beside it, let go at the same frame) */
rules(true);
{
    const { p, send } = fresh();
    send(inst(LONG));
    send({ t: "on", id: "s0", time: .01, midi: 60, ref: LONG, vol: 127, track: 0 });
    send({ t: "on", id: "k", time: .01, midi: 60, ref: LONG, vol: 127, track: -1, key: true });
    blocks(p, secs(1));
    const g = p.byId.get("s0"), v = p.byId.get("k");
    send({ t: "mute", track: 0, on: true });
    let kept = true;
    for (let b = 0; b < secs(.1); b++) { blocks(p); kept = kept && g.gh === GHOST && g.held; }
    send({ t: "off", id: "s0", time: now() });
    send({ t: "off", id: "k", time: now() });
    for (let b = 0; b < secs(.3); b++) { blocks(p); kept = kept && g.gh === GHOST && !g.held; }
    send({ t: "mute", track: 0, on: false });
    blocks(p);
    const back = g.e >= 0 && !g.held && p.vs.includes(g), same = back && v.e >= 0 && envSame("JUNO", p.en[g.e].st, p.en[v.e].st) && !p.quiet(v, p.en[v.e].st);
    console.log(`a song's note of ${R(LONG)} muted, let go while muted, heard again 0.3 s on: a ghost while muted ${kept}, back in its release ${back}, ` +
                `as the note never taken ${same}`);
    check("a release unmuted in time is heard: back in its release, as the note never taken", kept && back && same);
}
rules(false);
/* a held ghost's return takes its own track's tail first (own_tail): VOICES 8, the 8 engines songs' release tails heard,
 * track 1's in engine 5 (peak 300), track 2's in the others (peak 100 and up); a held ghost of track 1 comes back into
 * track 1's tail's engine; one of track 3 (none of its own) into the quietest */
{
    const { p, send } = fresh(8);
    send(inst(DXS));
    for (let c = 0; c < 8; c++) {
        const v = at(p, { id: "t" + c, ref: DXS, midi: 60 + c, track: c === 5 ? 1 : 2, key: false }, c);
        p.release(v);
        heard(v);
        v.peak = (c === 5 ? 300 : c === 2 ? 100 : 200 + 10 * c) / 32768;
    }
    p.ageSeen = p.age;
    const g1 = at(p, { id: "g1", ref: DXS, midi: 50, track: 1, key: false }, -1), g3 = at(p, { id: "g3", ref: DXS, midi: 52, track: 3, key: false }, -1);
    p.nfd = 0;
    p.now = p.frame;
    p.returns();
    const w1 = p.en.findIndex(e => e.wait === g1), w3 = p.en.findIndex(e => e.wait === g3);
    console.log(`held ghosts back into tails' engines: track 1's into engine ${w1} (its own track's tail: 5), track 3's into ${w3} (the quietest: 2)`);
    check("a return takes its own track's tail first, else the quietest", w1 === 5 && w3 === 2);
}
/* a sampler ghost let go comes back in its release, a long one (samp_long): A2:08 (it loops), made to ring by its note's
 * own list (LOOP START and LOOP END early, so a let-go plays on through the rest of the sample; R4 slow), held 1 s, let
 * go, back 0.5 s on: in its release, its envelope and place the note never taken's (a key's beside it), heard */
rules(true);
{
    const { p, send } = fresh();
    send(inst(LOOPS));
    const params = [{ c: 0x43, v: 4 }, { c: 0x44, v: 16 }, { c: 0x4B, v: 30 }];
    const v = at(p, { id: "v", ref: LOOPS, midi: 60, params }, 11), g = at(p, { id: "g", ref: LOOPS, midi: 60, params }, -1);
    noReturns(p);
    blocks(p, secs(1));
    send({ t: "off", id: "g", time: now() });
    send({ t: "off", id: "v", time: now() });
    blocks(p, secs(.5));
    const rings = v.e >= 0 && !v.ended && !p.quiet(v, p.en[v.e].st);
    returnsOn(p);
    blocks(p);
    const back = g.e >= 0 && !g.held, same = back && envSame("SAMPLER", p.en[g.e].st, p.en[v.e].st);
    console.log(`a sampler ghost of ${R(LOOPS)} (its loop early, R4 slow) let go after 1 s: its note never taken heard 0.5 s on ${rings}; back in its ` +
                `release ${back}, its envelope and place the note's ${same}`);
    check("a sampler ghost let go comes back in its release, as the note never taken", rings && back && same);
}
rules(false);
/* a ghost's sampler place, walked to the end of the block just made (as its return would find it): a copy */
const placeNow = (p, g) => {
    const s = p.sampOf(g), q = Object.assign({}, g.pl), n = p.frame - g.tPl;
    if (s && n > 0) WE.samp.go(q, WE.samp.facts(g.p, s), WE.samp.inc(g.p, s, g.midi, g.pb), n, g.held);
    return q;
};
/* a sampler ghost and the note never taken beside it, block by block from now: its place the note's each block (its
 * note's and its own end apart); the block the note ended in (by its place: past END), the block the ghost went in */
function follow(p, g, v, each) {
    let same = true, end = -1, gone = -1, byPlace = false;
    for (let b = 0; b < 40000; b++) {
        if (each) each(b);
        blocks(p);
        if (!p.vs.includes(g)) { gone = b; break; }
        if (end < 0 && v.e >= 0 && !v.ended) same = same && placeSame(placeNow(p, g), WE.samp.of(p.en[v.e].st));
        if (end < 0 && (v.ended || v.e < 0)) { end = b; byPlace = v.e >= 0 && placeNow(p, g).ended; }
    }
    return { same, end, gone, byPlace };
}
/* a sampler ghost slid (samp_slid: its pitch moved every block, -6 to +6 semitones and again): its place, walked at each
 * pitch it had, the note never taken's each block (the same pitches); its end from that same count: gone in the block
 * after the one the note ended in */
{
    const { p, send } = fresh();
    send(inst(SHOT));
    const v = at(p, { id: "v", ref: SHOT, midi: 60 }, 11), g = at(p, { id: "g", ref: SHOT, midi: 60 }, -1);
    noReturns(p);
    const r = follow(p, g, v, b => {
        const bend = ((b % 96) * 8 - 384) / 64;
        send({ t: "ctl", id: "g", time: now(), bend, vol: 1 });
        send({ t: "ctl", id: "v", time: now(), bend, vol: 1 });
    });
    console.log(`a sampler ghost of ${R(SHOT)} slid every block: its place the note never taken's ${r.same}; the note ended in block ${r.end} ` +
                `(by its place ${r.byPlace}), the ghost gone in block ${r.gone}`);
    check("a sampler ghost slid: its place and its end from one count, as the note never taken's", r.same && r.byPlace && r.end > 0 && r.gone === r.end + 1);
}
/* a sampler note born a ghost whose own list takes it out of its loop (born_unloop): A2:08 (it loops) with (a) LOOP OFF,
 * (b) LOOP START and LOOP END on one point (no room), as a key's ghost from its start; (c) a song's note with LOOP OFF
 * started in a song's held note's engine (VOICES 8, the other engines the keys'), taken in its block by a key's note:
 * born then from where it started. Each against the same note never taken (a key's beside it): its place each block,
 * gone in the block after the note ended (by its place: it plays on to END) */
{
    const WHAT = ["LOOP OFF", "a loop with no room", "LOOP OFF, its start taken"];
    const PAR = [[{ c: 0x42, v: 0 }], [{ c: 0x43, v: 128 }, { c: 0x44, v: 128 }], [{ c: 0x42, v: 0 }]];
    let ok = true;
    for (let m = 0; m < 3; m++) {
        const { p, send } = fresh(m < 2 ? 12 : 8);
        send(inst(LOOPS));
        send(inst(DXS));
        let g, born;
        if (m < 2) {
            g = at(p, { id: "g", ref: LOOPS, midi: 60, params: PAR[m] }, -1);
            born = !!g && g.gh === GHOST;
        } else {
            for (let c = 0; c < 7; c++) at(p, { id: "k" + c, ref: DXS, midi: 70 + c }, c);
            const s = at(p, { id: "s1", ref: DXS, midi: 80, track: 1, key: false }, 7);
            heard(s);
            p.ageSeen = p.age;
            p.now = p.frame;
            p.noteOn({ id: "g", ref: LOOPS, midi: 60, vol: 127, track: 0, params: PAR[m], time: now() });
            g = p.byId.get("g");
            const inE = !!g && g.e === 7;
            p.noteOn({ id: "k90", ref: DXS, midi: 60, vol: 127, track: -1, key: true, time: now() });
            const k = p.byId.get("k90");
            born = inE && g.gh === GHOST && g.held && g.e < 0 && !!k && k.e === 7;
        }
        const v = at(p, { id: "v", ref: LOOPS, midi: 60, params: PAR[m] }, 11);
        noReturns(p);
        const r = born ? follow(p, g, v) : { same: false, end: -1, gone: -1, byPlace: false };
        console.log(`a sampler note of ${R(LOOPS)} born a ghost, ${WHAT[m]}: its place the note never taken's ${r.same}; the note ended in block ${r.end} ` +
                    `(by its place ${r.byPlace}), the ghost gone in block ${r.gone}`);
        ok = ok && born && r.same && r.byPlace && r.end > 0 && r.gone === r.end + 1;
    }
    check("a sampler note born a ghost: out of its loop by its own list, as the note never taken (3 ways)", ok);
}
/* a CHIP ghost's return, by the commands it comes back with (chip_rebuilt: they act from its return, and it ends as its
 * voice would): a song's CHIP note of track 0 made a ghost, its track's command changed since (RELEASE 1, 8, 15 on F9:00,
 * whose own RELEASE gives it no release; on F9:18, whose own VOL table runs out at E, a VOL table running out later, at
 * E2, or sooner), its return begun in a fresh engine (its commands rebuilt, its note started there: its figures worked
 * out again from that note's start), its work in pieces (a step a block), or waiting. Each against the note never taken
 * (that command from its start, stepped by its own block step, as the firmware's test steps it). "Gone after the note":
 * in the block after the one the note never taken ended in; "within a frame": up to a frame of the instrument later,
 * never sooner, and exactly at its let-go plus the release's length a scratch note with its rebuilt list gives:
 *  -  0-2: RELEASE 1, 8, 15, let go as its note starts, its work in pieces: back as the note never taken, ending with it;
 *  -    3: E2's table, held, started 3 FM-1 blocks before E, its work waiting till 5 after E, then in pieces: as the note;
 *  - 4, 5: controls: let go before its return begins, or with its claim's note still to come: gone at once (no release);
 *  -    6: E2's table, started 3 before E, waiting till 10 after E, then given up (a key's note takes its engine, every
 *          other engine a key's): a ghost again, gone after the note (E2);
 *  -    7: the same with no command changed: gone after the note (E) while its work waits, never given up;
 *  -    8: RELEASE 1, its note started, waiting, let go 5 FM-1 blocks on, given up 5 more on: within a frame;
 *  -    9: RELEASE 1, its note started, waiting, given up held 5 blocks on, let go 5 more on: within a frame;
 *  -   10: a VOL table running out before E, its return begun after that: gone as its note starts (its end come);
 *  - 11-13: 6 given up by a command on its track (RELEASE 3), its track muted (a held ghost of track 1 below it in the
 *          list begun after it), VOICES 8 (its return in engine 11) */
{
    const C = WE.chip, CE = WE.engines.CHIP, NB = 1 << 22;   /* (a 64-frame block's frames, Q16: CHIP_N << 16) */
    const { p: q0 } = fresh();
    const built = (r, pc, pv) => { const m = inst(r); return pc == null ? m.p : q0.build(m, [[pc, pv]]); };
    const note = (r, pc, pv) => { const v = { midi: 60, bend: 0 }; CE.start(v, built(r, pc, pv), 60, {}, q0); return v; };
    const endOf = (r, pc, pv, held) => C.endBlocks(note(r, pc, pv), held);
    const frameBlocks = (r, pc, pv) => {                /* (the blocks a frame of it takes, rounded up) */
        const v = note(r, pc, pv);
        for (let i = 0; i < 64 && !v.dead; i++) { const a = v.f_acc; C.skip(v); if (v.f_acc < a + NB) return Math.ceil((a + NB - v.f_acc) / NB); }
        return 0;
    };
    const eOwn = endOf(VT, null, 0, true), fb = frameBlocks(CHS, 0x57, 1), rel = endOf(CHS, 0x57, 1, false);
    let tab = 0, tabs = 0, e2 = 0, es = 0;
    for (let x = 1; x < 64 && isFinite(eOwn); x++) {
        const e = endOf(VT, 0x60, x, true);
        if (!tab && isFinite(e) && e > eOwn + 40 && e < 4000) { tab = x; e2 = e; }
        if (!tabs && isFinite(e) && e > 10 && e + 40 < eOwn) { tabs = x; es = e; }
    }
    console.log(`${R(VT)} held: its own VOL table runs out in ${eOwn} FM-1 blocks, VOL TABLE ${tab} in ${e2}, VOL TABLE ${tabs} in ${es}; a frame of ` +
                `${R(CHS)} with RELEASE 1 ${fb} blocks at most, its release ${rel}`);
    const WHAT = ["RELEASE 1 since, let go as its note starts: as the note", "RELEASE 8 since, let go as its note starts: as the note",
                  "RELEASE 15 since, let go as its note starts: as the note", "a longer VOL table since, past its own end: as the note",
                  "let go before its return begins: gone at once (a control)", "let go, its claim's note to come: gone at once (a control)",
                  "waiting past its own end, given up by a note: after the note", "waiting, no command changed: gone after the note, at its end",
                  "waiting, let go, given up: gone within a frame of the note", "waiting, given up, let go: gone within a frame of the note",
                  "begun after a shorter VOL table's end: gone as it starts", "waiting past its end, given up by a command: after the note",
                  "waiting past its end, given up by a mute: after the note", "waiting past its end, given up by VOICES 8: after the note"];
    const live = v => CE.active(v);
    for (let m = 0; m < 14 && tab && tabs && fb; m++) {
        const vol = m === 3 || m === 6 || m === 7 || m >= 10, how = m === 11 ? 1 : m === 12 ? 2 : m === 13 ? 3 : 0;
        const gave = m === 6 || m === 8 || m === 9 || m >= 11, work = m < 4, keys = m === 6 || m === 8 || m === 9;
        const r = vol ? VT : CHS, pc = vol ? 0x60 : 0x57, pv = m === 10 ? tabs : vol ? tab : m === 1 ? 8 : m === 2 || m === 4 || m === 5 ? 15 : 1;
        const np = m === 7 ? 0 : 1, c0 = how === 3 ? 11 : 0;
        const { p, send } = fresh();
        send(inst(r));
        send(inst(DXS));
        let other = null;
        if (how === 2) other = at(p, { id: "s1.1", ref: DXS, midi: 50, track: 1, key: false }, -1);
        const g = at(p, { id: "s0.1", ref: r, midi: 60, track: 0, key: false }, -1);
        const v = note(r, np ? pc : null, pv), tOn = p.frame;   /* (the note never taken: its own steps, a block at a time) */
        for (let c = 1; keys && c < 12; c++) at(p, { id: "k" + c, ref: DXS, midi: 70 + c }, c);
        noReturns(p);
        const step = () => { C.skip(v); C.skip(v); };    /* (a playground block: two of its own) */
        const holdF = vol ? (m === 10 ? (es + eOwn) / 2 * U : g.tEnd - p.frame - 3 * U) : 689 * U;
        /* (its own end E, before its commands are rebuilt: the times counted from it) */
        let giveAt = vol && m !== 10 ? g.tEnd + 10 * U : Infinity, letAt = Infinity;
        const workAt = m === 3 ? g.tEnd + 5 * U : g.tOn;
        for (let b = 0; b < Math.floor(holdF / BLK); b++) { blocks(p); step(); }
        const ended = !live(v);
        p.tpar[0] = np ? [[pc, pv]] : [];               /* (the track's command since, its notes not turned by it) */
        p.now = p.frame;
        if (m !== 5) p.ready(g);
        p.en[c0].wait = g; p.en[c0].kind = null; g.gh = BACKING; g.done = 0;
        let started = false;
        if (m !== 4 && m !== 5) { p.forward(g, c0, { n: 1 }); started = p.en[c0].kind === "CHIP" && g.gh === BACKING; }
        else if (m === 4) p.backStop(g);
        if (m < 6 && m !== 3) { p.release(g); C.noteOff(v); }
        const at0 = p.frame;
        if (m === 8 || m === 9) { letAt = p.frame + (m === 8 ? 5 : 10) * U; giveAt = p.frame + (m === 8 ? 10 : 5) * U; }
        let early = false, back = false, same = false, ge = -1, re = -1, given = false, ghost = false, otherBack = false, exF = -1;
        for (let b = 0; b < 4000 && m !== 10; b++) {
            blocks(p); step();
            if (!p.vs.includes(g)) { early = !given; ge = b; break; }
            p.now = p.frame;
            if (p.frame >= letAt && g.held) { p.release(g); C.noteOff(v); exF = g.tOff + rel * U; letAt = Infinity; }
            if (gave && p.frame >= giveAt && !given) {
                if (how === 0) {                        /* (a key's note takes its engine) */
                    p.noteOn({ id: "k99", ref: DXS, midi: 60, vol: 127, track: -1, key: true, time: now() });
                    const k = p.byId.get("k99");
                    given = !!k && k.e === c0;
                } else if (how === 1) {
                    p.param({ track: 0, code: 0x57, value: 3 });
                    given = true;
                } else if (how === 2) {
                    send({ t: "mute", track: 0, on: true });
                    returnsOn(p); p.returns(); noReturns(p);
                    otherBack = other.e >= 0 || other.gh === BACKING;
                    send({ t: "mute", track: 0, on: false });
                    given = true;
                } else { send({ t: "voices", n: 8 }); given = true; }
                ghost = p.vs.includes(g) && g.gh === GHOST && p.en[c0].wait !== g;
                giveAt = Infinity;
            }
            if (work && p.frame >= workAt) {
                p.forward(g, c0, { n: 1 });
                if (g.e >= 0) { back = true; same = envSame("CHIP", p.en[g.e].st, v); break; }
            }
            if (re < 0 && !live(v)) re = b;
        }
        if (back) {                                     /* (both on, a block of their own at a time, to their ends) */
            const a = p.en[g.e].st;
            ge = re = -1;
            for (let b = 0; b < 12000 && (ge < 0 || re < 0); b++) {
                C.skip(a); C.skip(v);
                if (ge < 0 && !live(a)) ge = b;
                if (re < 0 && !live(v)) re = b;
            }
        }
        let good, says;
        if (m < 4) {
            good = started && !early && back && same && ge >= 0 && ge === re;
            says = `${back ? same ? "back as the note never taken" : "back, not as the note" : early ? "gone" : "not back"}; its engine and the note ` +
                   `never taken's end in blocks ${ge} and ${re} after that`;
        } else if (m < 6 || m === 10) {
            good = m === 10 ? !started && !p.vs.includes(g) && !p.en[c0].wait && ended : early && ge === 0;
            says = (m === 10 ? !p.vs.includes(g) : ge === 0) ? "gone at once" : "not gone";
        } else {
            /* (8, 9: in the first block from its let-go plus the rebuilt release's length on) */
            const ex = m === 8 || m === 9 ? Math.ceil((exF - at0) / BLK) : re + 1, d = ge - (re + 1);
            good = started && re >= 0 && ge >= 0 && (m === 7 ? early && !given : given && ghost && !early) &&
                   (m === 8 || m === 9 ? d >= 0 && d <= Math.ceil(fb * U / BLK) : d === 0) && ge === ex && (how !== 2 || otherBack);
            says = `${m === 7 ? (given ? "given up" : "never given up") : ghost ? "a ghost again" : "not a ghost"}, gone in block ${ge}, the note never ` +
                   `taken's voice freed in block ${re + 1} (${d >= 0 ? "+" : ""}${d}; by its figures ${ex})${how === 2 ? otherBack ? "; the ghost below begun" : "; the ghost below not begun" : ""}`;
        }
        console.log(`a CHIP return, ${WHAT[m]}: ${says}`);
        check(`a CHIP return, ${WHAT[m]}`, good);
    }
    if (!tab || !tabs || !fb) check("a CHIP instrument whose VOL table runs out, others of it running out later, sooner; a frame", false);
}
/* a key's held note whose engine has ended by itself (take_ended: a CHIP's VOL table run out; a one-shot sample past its
 * END), its voice not yet freed (the next block frees it), taken: VOICES 8 over it in engine 11, the list full of songs'
 * held ghosts (a key's held ranks above them). It goes, not a ghost: no ghost dropped for it, none listed due */
for (const [what, r] of [["CHIP", VT], ["one-shot sample", SHOT]]) {
    const { p, send } = fresh();
    send(inst(DXS));
    send(inst(r));
    const g = [];
    for (let i = 0; i < 24; i++) g.push(at(p, { id: "g" + i, ref: DXS, midi: 40 + i, track: 1, key: false }, -1));
    const v = at(p, { id: "k70", ref: r, midi: 60 }, 11);
    noReturns(p);
    let b = 0;
    for (; b < 40000 && !v.ended; b++) blocks(p);
    const ended = v.ended && v.e === 11;
    send({ t: "voices", n: 8 });
    const kept = g.filter(x => p.vs.includes(x) && x.gh === GHOST).length, due = p.gl.filter(x => x.tEnd <= p.frame).length;
    console.log(`a key's held ${what} note ended in its engine (in ${b} blocks), taken with the list full: ${p.vs.includes(v) ? "a ghost" : "gone"}; ` +
                `the list's ghosts kept ${kept} of 24, ${due} listed due`);
    check(`a held ${what} note ended in its engine, taken: gone, no ghost dropped`, ended && !p.vs.includes(v) && kept === 24 && p.gl.length === 24 && !due);
}
/* a sampler ghost whose end falls in the block it would come back in goes at its return (samp_edge): a one-shot's held
 * ghost: ready a block and more before its end, gone in the block its end falls in */
{
    const { p, send } = fresh();
    send(inst(SHOT));
    const g = at(p, { id: "g", ref: SHOT, midi: 60 }, -1), e = g.tEnd;
    noReturns(p);
    while (e - (p.frame + BLK) > BLK) blocks(p);
    p.now = p.frame;
    const two = p.ready(g) && g.tEnd === e;
    blocks(p);
    p.now = p.frame;
    const one = p.ready(g);
    console.log(`a one-shot's ghost of ${R(SHOT)}: ready more than a block before its end ${two}, gone in the block its end falls in ${!one && !p.vs.includes(g)}`);
    check("a sampler ghost ending in the block it would come back in goes at its return", isFinite(e) && two && !one && !p.vs.includes(g));
}
/* an instrument deleted by its return (inst_gone): a key's held ghost of a sampler, its instrument gone: dropped at its
 * return, its engine not started */
{
    const { p, send } = fresh();
    send(inst(LOOPS));
    const g = at(p, { id: "g", ref: LOOPS, midi: 60 }, -1);
    noReturns(p);
    blocks(p);
    send({ t: "inst", ref: LOOPS, gone: true });
    returnsOn(p);
    blocks(p);
    const ok = !p.vs.includes(g) && p.en[0].kind === null && !p.en[0].wait && !p.en[0].voice;
    console.log(`a ghost of ${R(LOOPS)}, its instrument deleted: gone at its return, no engine started ${ok}`);
    check("an instrument deleted: its ghost goes at its return", ok);
}
/* a ghost let go survives its track going A -> B -> A before its return (track_aba): a song's DX7 ghost of track 0 held
 * 1 s, let go, the track on another instrument, then on its own again: back; the same with the track left on the
 * other: gone */
rules(true);
{
    const run = aba => {
        const { p, send } = fresh();
        send(inst(DXS));
        send(inst(DX2));
        const g = at(p, { id: "s0", ref: DXS, midi: 60, track: 0, key: false }, -1);
        noReturns(p);
        blocks(p, secs(1));
        send({ t: "off", id: "s0", time: now() });
        send({ t: "iclear", track: 0, time: now(), ref: DX2 });
        if (aba) send({ t: "iclear", track: 0, time: now(), ref: DXS });
        returnsOn(p);
        blocks(p);
        return { back: g.e >= 0 && p.vs.includes(g), gone: !p.vs.includes(g) };
    };
    const a = run(true), b = run(false);
    console.log(`a song's ghost let go, its track A -> B -> A by its return: back ${a.back}; A -> B: gone ${b.gone}`);
    check("a ghost let go survives its track going A -> B -> A; on B at its return it goes", a.back && b.gone);
}
rules(false);
/* FILTER's envelope held by a ghost and let go by a one-shot's end (filter_held): a song's one-shot sampler note on track
 * 3 born a ghost: track 3 held in FILTER's sources while it lasts, not once it has ended by itself */
{
    const { p, send } = fresh();
    send(inst(SHOT));
    const g = at(p, { id: "s3", ref: SHOT, midi: 60, track: 3, key: false }, -1);
    noReturns(p);
    blocks(p);
    const held = p.heldT[3] === 1;
    for (let b = 0; b < 40000 && p.vs.includes(g); b++) blocks(p);
    blocks(p);
    const after = p.heldT[3] === 1;
    console.log(`FILTER's envelope: a one-shot's ghost on track 3 held there ${held}, its own end let it go ${!after}`);
    check("FILTER's envelope held by a ghost, let go at a one-shot ghost's own end", held && !after);
}
/* a FUSION layer's E9x restarting it while its ghost's return is under way (fusion_restart): A5:03 row 60's fourth layer
 * (a CHIP one with E93), its return under way in engine 3 (its note started there, its work to come), every other
 * engine a key's; the restart (the page's runner sends it as a note with the layer's id): the return given up, the
 * restart's note in that engine */
{
    const F = ref(0xA5, 3), fp = M.pattern(M.inst(F).p.pattern), c = fp && fp.cells[60 * 16 + 3];
    const lr = c ? (c.inst != null ? c.inst : M.trackInst(fp, 60, 3)) : null, retrig = !!c && c.cmds.some(x => x.c === 0xE9 || (x.c === 0x0E && x.v >> 4 === 9));
    if (!c || lr == null || M.instType(lr) !== "CHIP") check("FUSION A5:03 row 60: a CHIP layer (the case's)", false);
    else {
        const { p, send } = fresh();
        send(inst(DXS));
        send(inst(lr));
        for (let e = 0; e < 12; e++) if (e !== 3) at(p, { id: "k" + e, ref: DXS, midi: 40 + e }, e);
        const id = "s0.1/3", m = { id, ref: lr, midi: c.note, vol: 64, track: 0, key: false, fcell: { ref: F, pat: M.inst(F).p.pattern, row: 60, k: 3, note: c.note } };
        const g = at(p, m, -1);
        p.en[3].wait = g; p.en[3].kind = null; g.gh = BACKING; g.done = 0;
        p.now = p.frame;
        p.forward(g, 3, { n: 1 });
        const under = g.gh === BACKING && p.en[3].kind === "CHIP";
        p.noteOn(Object.assign({}, m, { retrig: true, time: now() }));
        const n = p.byId.get(id), ok = under && !p.vs.includes(g) && !!n && n !== g && n.e === 3 && !n.gh && !p.en[3].wait;
        console.log(`a FUSION layer's restart (${R(lr)}, E9x ${retrig}) while its ghost's return is under way (${under}): the ghost gone ${!p.vs.includes(g)}, ` +
                    `the restart in its engine ${!!n && n.e === 3}`);
        check("FUSION: E9x restarting a layer whose return is under way: given up, the new start in its engine", ok);
    }
}
/* DJ (dj_takes_return; a pool set by hand, VOICES 8, the right deck faded out): the left deck's held notes in engines 0-7
 * but 5, where a left deck's ghost's return is under way; a note of the right deck takes that engine (a return under
 * way is taken before any deck's rule: silent, as a free engine), the ghost a ghost still */
{
    const { p, send } = fresh(8);
    send(inst(DXS));
    p.dj = { on: true, left: 0, xf: 0, sharp: false, vol: [1, 1] };
    for (let c = 0; c < 8; c++) if (c !== 5) at(p, { id: "s" + c, ref: DXS, midi: 70 + c, track: c, key: false }, c);
    const g = at(p, { id: "g3", ref: DXS, midi: 60, track: 3, key: false }, -1);
    p.en[5].wait = g; p.en[5].kind = null; g.gh = BACKING; g.done = 0;
    p.now = p.frame;
    p.forward(g, 5, { n: 1 });
    const right = p.djHeard(1);
    p.noteOn({ id: "r19", ref: DXS, midi: 62, vol: 127, track: 19, time: now() });
    const v = p.byId.get("r19"), ok = !right && !!v && v.e === 5 && g.gh === GHOST && !p.en[5].wait;
    console.log(`DJ, the right deck faded out (heard ${right}): its note took the left deck's return under way (engine ${v ? v.e : -1}), the ghost a ghost ` +
                `${g.gh === GHOST}`);
    check("DJ: a faded-out deck's note takes the other deck's return under way (silent), the ghost a ghost", ok);
}
/* DJ's KEY changed on a deck (the firmware's pl_dj_pitch): every note of that deck, an older one ringing on and a ghost
 * too, on its track's own bend (0 from its note, then its latest note's ticks) plus the deck's; the other deck's notes
 * as they were */
{
    const { p, send } = fresh(8);
    send(inst(PAD));
    noReturns(p);
    p.dj = { on: true, left: 0, xf: 16, sharp: false, vol: [1, 1], key: [0, 0] };
    const old = at(p, { id: "o", ref: PAD, midi: 60, track: 2, key: false }, 0);
    send({ t: "ctl", id: "o", time: now(), bend: 1.5, vol: 1, track: 2 });
    blocks(p, 2);
    const lat = at(p, { id: "n", ref: PAD, midi: 64, track: 2, key: false }, 1);
    send({ t: "tbend", track: 2, time: now(), bend: 0 });
    send({ t: "ctl", id: "n", time: now(), bend: .25, vol: 1, track: 2 });
    const g = at(p, { id: "g", ref: PAD, midi: 55, track: 3, key: false }, -1);
    send({ t: "ctl", id: "g", time: now(), bend: .75, vol: 1, track: 3 });
    send({ t: "tbend", track: 3, time: now(), bend: 0 });   /* (a later note of track 3 began: its bend 0) */
    const oth = at(p, { id: "x", ref: PAD, midi: 50, track: 18, key: false }, 2);
    send({ t: "ctl", id: "x", time: now(), bend: .5, vol: 1, track: 18 });
    blocks(p, 2);
    const before = [old.bend, lat.bend, g.bend, oth.bend];
    p.dj.key = [3, 0];
    blocks(p, 1);
    const after = [old.bend, lat.bend, g.bend, oth.bend];
    console.log(`DJ's KEY +3 on the left deck: the older note ${before[0]} -> ${after[0]}, the latest ${before[1]} -> ${after[1]}, a ghost ` +
                `${before[2]} -> ${after[2]}, the right deck's ${before[3]} -> ${after[3]}`);
    check("DJ's KEY: every note of the deck on its track's own bend plus KEY (an older one's own bend dropped), the other deck's as it was",
          before.join() === "1.5,0.25,0.75,0.5" && after.join() === "3.25,3.25,3,0.5");
    returnsOn(p);
}
/* a sampler ghost's instrument edited while it waits (settings.md, a sampler ghost keeps its place: an edit counts from
 * the ghost's next walk or its return, never at the edit itself; voices_test's ghost_edit_walk): a key's held ghost of
 * A2:08 (looping); (a) LOOP OFF, (b) LOOP END on LOOP START (no room), then its pitch turned (its place walked then): out
 * of its loop at that walk, gone by END with no engine free; (c) LOOP OFF, its pitch turned, the instrument put back, then
 * an engine free: back out of its loop (left at the walk, for good); (d) LOOP OFF and put back with no walk between, then
 * an engine free: back in its loop */
rules(true);
{
    const WHAT = ["LOOP OFF, its pitch turned", "no room, its pitch turned", "LOOP OFF, its pitch turned, put back", "LOOP OFF and put back, no walk"];
    let ok = true;
    for (let k = 0; k < 4; k++) {
        const { p, send } = fresh();
        const was = inst(LOOPS), ed = structuredClone(was);
        if (k === 1) ed.p.loopEnd = ed.p.loopStart;
        else ed.p.loop = "OFF";
        send(was);
        const g = at(p, { id: "g", ref: LOOPS, midi: 60 }, -1);
        noReturns(p);
        blocks(p, 50);
        const before = g.pl.looping;
        send(ed);
        blocks(p);
        let walked = null, gone = false, back = false, looping = null;
        const kept = g.pl.looping;                      /* (the edit itself walks nothing) */
        if (k < 3) {
            send({ t: "ctl", id: "g", time: now(), bend: 1, vol: 1 });
            blocks(p);
            walked = g.pl.looping;
        }
        if (k >= 2) send(was);
        if (k < 2) {                                    /* (no engine free: on to its end) */
            for (let b = 0; b < 200000 && p.vs.includes(g); b++) blocks(p);
            gone = !p.vs.includes(g);
            ok = ok && walked === false && gone;
        } else {
            returnsOn(p);
            blocks(p);
            back = g.e >= 0 && p.vs.includes(g);
            looping = back ? p.en[g.e].st.looping : null;
            ok = ok && back && looping === (k === 3) && (k === 3 || walked === false);
        }
        ok = ok && before && kept;
        console.log(`a held sampler ghost of ${R(LOOPS)}, ${WHAT[k]}: in its loop after the edit ${kept}, after the walk ${walked}, gone by its end ${gone}, ` +
                    `back ${back}, looping ${looping}`);
    }
    check("a sampler ghost's instrument edited while it waits: out of its loop at its next walk (for good), not at the edit itself", ok);
}
/* the same while its return is under way (its note started in engine 0, the budget run out): its walks read its
 * instrument as it is now, as the firmware's (pl_v_samp), not the patch its return's note started with: LOOP OFF, then
 * its pitch turned: out of its loop there. And its instrument deleted before its return ends: no walk, no place set
 * there (the engine's as its forward work left it), as the firmware's; kept: walked to now */
{
    const under = (p, g) => {                           /* (its return begun in engine 0, not finished) */
        p.en[0].wait = g; p.en[0].kind = null; g.gh = BACKING; g.done = 0;
        p.now = p.frame;
        p.forward(g, 0, { n: 1 });
        return g.gh === BACKING && p.en[0].kind === "SAMPLER";
    };
    let { p, send } = fresh();
    const was = inst(LOOPS), ed = structuredClone(was);
    ed.p.loop = "OFF";
    send(was);
    let g = at(p, { id: "g", ref: LOOPS, midi: 60 }, -1);
    noReturns(p);
    blocks(p, 50);
    const begun = under(p, g), before = g.pl.looping;
    send(ed);
    blocks(p);
    const kept = g.pl.looping;
    send({ t: "ctl", id: "g", time: now(), bend: 1, vol: 1 });
    blocks(p);
    const walked = g.pl.looping, still = g.gh === BACKING;
    console.log(`a held sampler ghost of ${R(LOOPS)}, its return under way (${begun}), LOOP OFF: in its loop after the edit ${kept}, after a walk ` +
                `${walked}, under way still ${still}`);
    check("a sampler ghost's return under way: its walks read its instrument as it is now (LOOP OFF: out of its loop at the walk)",
          begun && before && kept && walked === false && still);
    const ends = [];
    for (const gone of [true, false]) {
        ({ p, send } = fresh());
        send(was);
        g = at(p, { id: "g", ref: LOOPS, midi: 60 }, -1);
        noReturns(p);
        blocks(p, 20);
        const b = under(p, g);
        blocks(p, 20);
        if (gone) send({ t: "inst", ref: LOOPS, gone: true });
        const t0 = g.tPl;
        p.now = p.frame;
        const done = p.forward(g, 0, { n: 1e9 });
        ends.push({ b, done, back: g.e === 0, walked: g.tPl !== t0 });
    }
    console.log(`its return ending with its instrument deleted: ${JSON.stringify(ends[0])}; kept: ${JSON.stringify(ends[1])}`);
    check("a sampler ghost's return ending with its instrument deleted: no walk, no place set; kept: walked to now",
          ends.every(x => x.b && x.done && x.back) && !ends[0].walked && ends[1].walked);
}
rules(false);
/* LEG onto a held ghost whose end has come, still listed in that block (commands.md, legato: only onto a note still
 * sounding; voices_test's leg_ghost_ended): a song's one-shot ghost on track 4; a LEG before its end: taken; once its end
 * has come (the block it is dropped in, before its ends): refused, the row a plain note */
{
    const run = late => {
        const { p, send } = fresh();
        send(inst(SHOT));
        const g = at(p, { id: "s4.1", ref: SHOT, midi: 60, track: 4, key: false }, -1);
        noReturns(p);
        if (late) while (p.frame < g.tEnd) blocks(p);
        const listed = p.gl.includes(g), due = g.tEnd <= p.frame;
        send({ t: "on", id: "s4.2", time: now(), midi: 62, ref: SHOT, vol: 127, track: 4, leg: "s4.1" });
        blocks(p);
        const n = p.byId.get("s4.2");
        return { taken: n === g, listed, due, plain: !!n && n !== g };
    };
    const a = run(false), b = run(true);
    console.log(`LEG on a held one-shot ghost of ${R(SHOT)} on track 4: taken before its end ${a.taken}; its end come (${b.due}), still listed (${b.listed}): ` +
                `${b.plain ? "refused, a plain note" : "taken"}`);
    check("LEG onto a held ghost whose end has come (still listed): refused, the row a plain note", a.taken && b.listed && b.due && b.plain);
}
/* ... and a LEG landing in the very block the ghost's end falls in: after its end frame, refused (the firmware's ghost
 * has ended by then on its block clock); before it, taken */
{
    const run = after => {
        const { p, send } = fresh();
        send(inst(SHOT));
        const g = at(p, { id: "s4.1", ref: SHOT, midi: 60, track: 4, key: false }, -1);
        noReturns(p);
        while (p.frame + BLK <= g.tEnd) blocks(p);
        const inside = g.tEnd > p.frame && g.tEnd < p.frame + BLK - 1;   /* (its end inside the block, not at its start) */
        const f = after ? g.tEnd : p.frame + Math.floor((g.tEnd - p.frame) / 2);
        send({ t: "on", id: "s4.2", time: now() + (f - p.frame) / SR, midi: 62, ref: SHOT, vol: 127, track: 4, leg: "s4.1" });
        const k = g.tEnd - p.frame;
        blocks(p);
        const n = p.byId.get("s4.2");
        return { inside, k, f: f - (p.frame - BLK), taken: n === g, plain: !!n && n !== g };
    };
    const a = run(false), b = run(true);
    console.log(`LEG in the block a one-shot ghost's end falls in (frame ${b.k} of it): at frame ${a.f} ${a.taken ? "taken" : "refused"}, at frame ${b.f} ` +
                `${b.plain ? "refused, a plain note" : "taken"}`);
    check("LEG in the block a ghost's end falls in: before its end taken, after it refused (a plain note)", a.inside && b.inside && a.taken && b.plain);
}
/* a sampler ghost's end in blocks that carry an event past their first frame (a ctl to another note at frame 64 of
 * every block, as a song's ticks land anywhere in a block): gone in the block after the one it ends in, as with none
 * (the firmware's ghost ends on its block clock, whatever its ticks carry) */
{
    const run = late => {
        const { p, send } = fresh(8);
        send(inst(SHOT)); send(inst(DXS));
        for (let k = 0; k < 8; k++) send({ t: "on", id: "k" + k, time: 0, midi: 40 + k, ref: DXS, vol: 100, track: -1, key: true });
        send({ t: "on", id: "g", time: .01, midi: 60, ref: SHOT, vol: 127, track: 0 });   /* (a song's note, every engine a key's: born a ghost) */
        blocks(p, 4);
        const g = p.gl.find(x => x.id === "g"), end = g ? g.tEnd : -1;
        let gone = -1;
        for (let b = 0; b < 4000 && g; b++) {
            if (late) send({ t: "ctl", id: "k0", time: now() + 64 / SR, bend: (b % 2) * .01, vol: 100 / 127 });
            blocks(p);
            if (!p.gl.includes(g)) { gone = (p.frame - BLK) / BLK; break; }
        }
        return { end, gone, born: !!g };
    };
    const a = run(false), b = run(true), blockOf = f => Math.floor((f - 1) / BLK);
    console.log(`a one-shot's ghost ending in block ${blockOf(a.end)}: gone in block ${a.gone} with no events, in block ${b.gone} with one at frame 64 of every block`);
    check("a sampler ghost's end: gone in the block after it, whatever frames the blocks' events fall on", a.born && b.born && a.gone === blockOf(a.end) + 1 &&
          b.gone === a.gone);
}
/* a sampler ghost's return under way when its instrument is edited (LOOP OFF): its note started in its engine before
 * the edit, its return finished after it walks its place by the instrument as it is then (the firmware's pl_forward):
 * back out of its loop; its engine's own copy of the instrument the one it started with */
{
    const { p, send } = fresh();
    const was = inst(LOOPS), ed = structuredClone(was);
    ed.p.loop = "OFF";
    send(was);
    const g = at(p, { id: "g", ref: LOOPS, midi: 60 }, -1);
    noReturns(p);
    blocks(p, 20);
    p.en[0].wait = g; p.en[0].kind = null; g.gh = BACKING; g.done = 0;
    p.now = p.frame;
    p.forward(g, 0, { n: 1 });
    const under = g.gh === BACKING && p.en[0].kind === "SAMPLER" && p.en[0].st.looping;
    send(ed);
    blocks(p, 2);
    p.now = p.frame;
    const back = p.forward(g, 0, { n: 1e9 }) === 1 && g.e === 0, st = back ? p.en[0].st : null;
    console.log(`a sampler return under way (${under}), its instrument edited to LOOP OFF, finished: back ${back}, looping ${st && st.looping}, its engine's ` +
                `copy LOOP ${st && st.p.loop}`);
    check("a sampler return under way, its instrument edited: its place walked by the instrument as it is at the return's end", under && back && !st.looping &&
          st.p.loop === "FORWARD");
}
/* a replay's notes cut (PLAYBACK's K1, a DJ deck's stop standing: the firmware's cut over a block): a held note's sound
 * fades out over the 64 frames from the cut's frame, as NOTE CUT's, and the note goes */
{
    const run = how => {
        const { p, send } = fresh();
        send(inst(PAD));
        send({ t: "on", id: "s0.1", time: .01, midi: 60, ref: PAD, vol: 127, track: 0 });
        blocks(p, secs(.3));
        const f = p.frame + 37, t = now() + 37 / SR, T = [];
        if (how === "cutp") { send({ t: "cutp", prefix: "s", time: t }); send({ t: "killp", prefix: "s", time: t + .004 }); }
        else send({ t: "cut", id: "s0.1", time: t });
        for (let b = 0; b < 3; b++) { blocks(p); T.push(...p.tracks[0]); }
        return { T: T.slice(37), gone: !p.vs.some(v => v.id === "s0.1") };
    };
    const a = run("cutp"), b = run("cut"), pk = (x, i, j) => Math.max(...x.slice(i, j).map(Math.abs));
    const same = a.T.every((x, i) => x === b.T[i]);
    console.log(`a replay's held note cut at frame 37 of a block: over the 64 frames from the cut the peak ${pk(a.T, 0, 64).toFixed(4)} (NOTE CUT's ` +
                `${pk(b.T, 0, 64).toFixed(4)}), after them ${pk(a.T, 64, 128).toFixed(4)}; as NOTE CUT's sample for sample ${same}; gone ${a.gone}`);
    check("a replay's notes cut: faded out over 64 frames as NOTE CUT's, then gone", pk(a.T, 0, 64) > 0 && pk(a.T, 64, 128) === 0 && same && a.gone);
}
/* the firmware's figures the twin takes: END turned to 0 by a command is one frame (a sampler note with it ends in its
 * first block); a position value lands on the firmware's frame (v x 1/256 of the length, worked out in whole frames: an
 * odd length); BRAKE's bend at its stand is four octaves down, and a spin up's first block's two semitones short of it
 * no more (from a sixteenth of the speed) */
{
    const { p, send } = fresh();
    send(inst(SHOT));
    send({ t: "on", id: "e", time: now(), midi: 60, ref: SHOT, vol: 127, track: -1, key: true, params: [{ c: 0x41, v: 0 }] });
    blocks(p);
    const v = p.vs.find(x => x.id === "e"), one = !v || v.ended;
    const L = 4 * SR + 1, pos = { key: "odd", end: L, loopStart: 0, loopEnd: L };
    p.samples.odd = { data: new Float32Array(L), rate: SR };
    WE.params.SAMPLER(pos, 0x43, 100, p);
    const want = (L >> 8) * 100 + ((L & 255) * 100 >> 8);
    console.log(`END 00 by a command: the note ${one ? "ended in its first block" : "still sounding"} (its END ${v ? p.en[v.e] && p.en[v.e].st ? p.en[v.e].st.end : "-" : "-"}); ` +
                `LOOP START 100 on ${L} frames: ${pos.loopStart} (the firmware's ${want})`);
    check("END 00 by a command plays one frame", one);
    check("a position command lands on the firmware's frame (an odd length)", pos.loopStart === want);
    p.dj = { on: true, left: 0, xf: 16, key: [0, 0] };
    send({ t: "brake", deck: 0, time: now(), up: false });
    blocks(p, secs(.6));
    const stood = p.shift[0];
    send({ t: "brake", deck: 0, time: now(), up: true });
    blocks(p, 2);
    const up = p.shift[0];
    p.dj = null;
    console.log(`BRAKE's bend at its stand ${stood.toFixed(2)} semitones, a spin up's first block ${up.toFixed(2)}`);
    check("BRAKE's bend at its stand -48 semitones, a spin up's first block -48", stood === -48 && up === -48);
}

/* a TB-303's next note within its NOTE CUT's fade (voices_test's tb_after_cut): the voice being cut is not taken again,
 * so the note takes an engine of its own and sounds, the cut one fading out and going */
{
    const { p, send } = fresh();
    send(inst(TB0));
    send({ t: "on", id: "s5tb", time: .01, midi: 36, ref: TB0, vol: 100, track: 5 });
    blocks(p, secs(.1));
    const t = now() + 10 / SR;
    send({ t: "cut", id: "s5tb", time: t });
    send({ t: "on", id: "s5tb", time: t + .0005, midi: 38, ref: TB0, vol: 100, track: 5 });
    blocks(p, 3);
    const w = p.byId.get("s5tb"), T = [];
    for (let b = 0; b < 4; b++) { blocks(p); T.push(...p.tracks[5]); }
    const pk = Math.max(...T.map(Math.abs)), cut = p.vs.filter(v => v.engine === "TB303").length;
    console.log(`a TB-303 note 0.5 ms after its NOTE CUT: ${w ? "midi " + w.midi + ", cutting " + !!w.cutting : "none"}, its track's peak after ${pk.toFixed(4)}, ` +
                `TB-303 voices ${cut}`);
    check("a TB-303 note within its NOTE CUT's fade: heard, in an engine of its own", w && w.midi === 38 && !w.cutting && pk > 0 && cut === 1);
}
/* ------------------------------------------------------- the pool's other cases (voices_test's, on the twin) --- */
const runFor = (p, seconds) => blocks(p, secs(seconds));
/* a TB-303 line above a smaller count (tb_above): track 5's 303 in engine 9 at VOICES 12 (the engines under it the
 * keys'), its note let go (a line's gate closes half a row on), engine 8's key cut, VOICES 8, the key in engine 3 cut:
 * its next note takes an engine under 8, the one before ringing out in 9 (or rung out by then: never taken again) */
{
    const jref = JU0, tref = TB0;
    const { p, send } = fresh(12);
    send(inst(jref)); send(inst(tref));
    for (let k = 0; k < 9; k++) send({ t: "on", id: "k" + k, time: .01, midi: 48 + k, ref: jref, vol: 100, track: -1, key: true });
    send({ t: "on", id: "s5tb", time: .05, midi: 36, ref: tref, vol: 100, track: 5 });
    runFor(p, .1);
    const v = p.byId.get("s5tb"), was = v ? v.e : -1;
    send({ t: "off", id: "s5tb", time: now() }); send({ t: "cut", id: "k8", time: now() });
    runFor(p, BLK / SR);
    send({ t: "voices", n: 8 });
    send({ t: "cut", id: "k3", time: now() });
    runFor(p, BLK / SR);
    send({ t: "on", id: "s5tb", time: now() + 1 / SR, midi: 38, ref: tref, vol: 100, track: 5 });
    runFor(p, BLK / SR);
    const w = p.byId.get("s5tb");
    console.log(`a TB-303 line in engine ${was} at VOICES 12, let go, VOICES 8: its next note in engine ${w ? w.e : "-"}, ` +
                `the one before in ${v ? v.e : "-"} (held ${v && v.held}; -1: rung out)`);
    check("a TB-303 line above a smaller count: its next note takes an engine within the count, the old one rings out",
          was === 9 && !!w && w !== v && w.e >= 0 && w.e < 8 && (v.e === 9 || v.e < 0) && !v.held);
}
/* LEG onto a one-shot that has ended by itself (leg_ended; commands.md, legato: only onto a note still sounding): a
 * song's note of a one-shot held on track 4, played to its end (its engine its own till the next block), a LEG note in
 * the next block: refused, the row a plain note, heard */
{
    const { p, send } = fresh();
    send(inst(SHOT));
    send({ t: "on", id: "s4.1", time: .005, midi: 60, ref: SHOT, vol: 127, track: 4 });
    let h = null;
    runFor(p, .01);
    for (let b = 0; b < 20000; b++) { h = p.vs.find(x => x.id === "s4.1"); if (!h || h.ended) break; runFor(p, BLK / SR); }
    const ended = !!h && h.ended && h.e >= 0;
    send({ t: "on", id: "s4.2", time: now(), midi: 62, ref: SHOT, vol: 127, track: 4, leg: "s4.1" });
    runFor(p, 2 * BLK / SR);
    const n = p.vs.find(x => x.id === "s4.2" && x.e >= 0 && !x.ended);
    console.log(`LEG on a held one-shot (${R(SHOT)}) ended by itself, its engine still its own (${ended}): ${n && n !== h ? "a plain note, sounding" : "taken by the ended note"}`);
    check("LEG onto a note that has ended by itself: refused, the row a plain note", ended && !!n && n !== h);
}
/* RECORD LIVE's tap (specs/ui/screens/loops.md#record-live): a note marked `tap` losing its engine is told once, one
 * given none is told, a ghost coming back tells nothing, and a note off a voice's own tells nothing */
{
    const jref = JU0;
    const session = () => {
        const s = fresh(16), said = [], post0 = s.p.port.postMessage;
        s.p.port.postMessage = m => { if (m.t === "tap") said.push(m); else post0.call(s.p.port, m); };
        s.send({ t: "voices", n: 2 });
        s.send(inst(jref));
        return Object.assign(s, { said });
    };
    const a = session();
    a.send({ t: "on", id: "s1", time: .01, midi: 60, ref: jref, vol: 100, track: 16, tap: true });
    a.send({ t: "on", id: "s2", time: .02, midi: 62, ref: jref, vol: 100, track: 16, tap: true });
    runFor(a.p, .1);
    check("the tap: a note with a voice tells nothing", !a.said.length);
    a.send({ t: "on", id: "s3", time: .2, midi: 64, ref: jref, vol: 100, track: 16, tap: true });
    runFor(a.p, .2);
    check("the tap: a note taken by another is told, once, at its time", a.said.length === 1 && a.said[0].lost && /^s[12]$/.test(a.said[0].id) &&
          Math.abs(a.said[0].time - .2) <= .01);
    a.send({ t: "off", id: "s3", time: .5 });
    runFor(a.p, 3);
    a.p.post();
    check("the tap: a ghost coming back tells nothing", a.said.length === 1 && a.p.stats.ghostsAll === 0);
    const b = session();
    b.send({ t: "on", id: "k1", time: .01, midi: 60, ref: jref, vol: 100, track: -1 });
    b.send({ t: "on", id: "k2", time: .02, midi: 62, ref: jref, vol: 100, track: -1 });
    b.send({ t: "on", id: "s1", time: .1, midi: 64, ref: jref, vol: 100, track: 16, tap: true });
    runFor(b.p, .2);
    check("the tap: a song's note finding every engine a key's is told as given none", b.said.length === 1 && b.said[0].none && b.said[0].id === "s1");
    const c = session();
    c.send({ t: "on", id: "s1", time: .01, midi: 60, ref: jref, vol: 100, track: 16, tap: true });
    c.send({ t: "on", id: "s2", time: .02, midi: 62, ref: jref, vol: 100, track: 16, tap: true });
    runFor(c.p, .1);
    c.send({ t: "voices", n: 1 });
    runFor(c.p, .1);
    check("the tap: a note made a ghost past a smaller count is told", c.said.length === 1 && c.said[0].lost);
}
/* 20 keys held, as voices_test: VOICES of them sound, the rest are ghosts (8 at VOICES 12, 12 at 8: up to 24 wait) */
{
    const jref = JU0;
    const held = n => {
        const { p, send } = fresh(16);
        send({ t: "voices", n });
        send(inst(jref));
        for (let k = 0; k < 20; k++) send({ t: "on", id: "k" + k, time: .01 + k * .05, midi: 48 + 2 * k, ref: jref, vol: 100, track: -1 });
        runFor(p, 1.5); p.post();
        return { heard: p.stats.voices.length, ghosts: p.stats.ghosts };
    };
    const a = held(12), b = held(8);
    console.log(`20 keys held: VOICES 12: ${a.heard} heard, ${a.ghosts} ghosts; VOICES 8: ${b.heard} heard, ${b.ghosts} ghosts`);
    check("20 keys at VOICES 12: 12 heard, 8 ghosts", a.heard === 12 && a.ghosts === 8);
    check("20 keys at VOICES 8: 8 heard, 12 ghosts", b.heard === 8 && b.ghosts === 12);
}
/* the keys first, however quiet: 8 quiet keys held at 8 voices, then a loud song note: it starts as a ghost, every key
 * heard; 2 more keys make the 2 oldest keys ghosts; one heard key let go: a key's ghost comes back into its tail's engine,
 * not the song's note; all let go, the song's note comes back */
{
    const jref = JU0;
    const { p, send } = fresh(16);
    send({ t: "voices", n: 8 });
    send(inst(jref));
    for (let k = 0; k < 8; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref: jref, vol: 20, track: -1 });
    send({ t: "on", id: "s", time: .3, midi: 60, ref: jref, vol: 127, track: 0 });
    runFor(p, .5); p.post();
    const keysOn = () => p.stats.voices.filter(v => v.track === -1).length, songOn = () => p.stats.voices.some(v => v.id === "s");
    const a = { keys: keysOn(), song: songOn(), ghosts: p.stats.ghosts };
    send({ t: "on", id: "k8", time: .6, midi: 80, ref: jref, vol: 20, track: -1 });
    send({ t: "on", id: "k9", time: .6, midi: 82, ref: jref, vol: 20, track: -1 });
    runFor(p, .3); p.post();
    const b = { keys: keysOn(), oldest: p.stats.voices.some(v => v.id === "k0" || v.id === "k1") };
    send({ t: "off", id: "k5", time: now() });
    runFor(p, 2.5); p.post();
    const c = { keys: keysOn(), song: songOn(), back: p.stats.voices.some(v => v.id === "k0" || v.id === "k1") };
    for (let k = 0; k < 10; k++) send({ t: "off", id: "k" + k, time: now() });
    runFor(p, 3); p.post();
    const d = songOn();
    console.log(`the keys first at 8 voices: 8 keys and a song note: ${a.keys} keys heard, song ${a.song ? "heard" : "a ghost"}; ` +
                `10 keys: ${b.keys} heard; one let go: ${c.keys} keys heard, song ${c.song ? "heard" : "a ghost"}; all let go: song ${d ? "heard" : "not heard"}`);
    check("the keys first: a song note takes no engine from a held key", a.keys === 8 && !a.song && a.ghosts === 1);
    check("the keys first: 2 more keys make the 2 oldest ghosts", b.keys === 8 && !b.oldest);
    check("the keys first: an engine free, a key's ghost comes back before the song's note", c.keys === 8 && !c.song && c.back);
    check("the keys first: all keys let go, the song's note comes in", d);
}
/* mutes make ghosts (specs/ui/screens/settings.md#muted-tracks): a song track's held note, its track muted, leaves its
 * engine at the end of its mute's block and is a ghost; a new note there starts as one; unmuted, both come back, the
 * held one where its envelope would be. DUCK's source's muted note and a TB-303's keep their engines */
{
    const juno = JU0, tb = TB0;
    const muted = (setup, mute) => {
        const { p, send } = fresh(16, setup);
        send(inst(juno)); send(inst(tb));
        send({ t: "on", id: "s0", time: .01, midi: 60, ref: juno, vol: 100, track: 0 });
        runFor(p, .3);
        if (mute) send({ t: "mute", track: 0, on: true });
        runFor(p, .1); p.post();
        const a = { voices: p.stats.voices.length, ghosts: p.stats.ghosts };
        send({ t: "on", id: "s1", time: now() + .01, midi: 64, ref: juno, vol: 100, track: 0 });
        send({ t: "on", id: "t1", time: now() + .01, midi: 48, ref: tb, vol: 100, track: 1 });
        if (mute) send({ t: "mute", track: 1, on: true });
        runFor(p, .1); p.post();
        const b = { voices: p.stats.voices.length, ghosts: p.stats.ghosts, tb: p.stats.voices.some(v => v.id === "t1") };
        if (mute) { send({ t: "mute", track: 0, on: false }); send({ t: "mute", track: 1, on: false }); }
        runFor(p, .02); p.post();
        const c = { voices: p.stats.voices.length, ghosts: p.stats.ghostsAll };
        const tr = [];
        for (let k = 0; k < 30; k++) { runFor(p, BLK / SR); tr.push(...p.tracks[0]); }
        return { a, b, c, db: rmsDb(tr, 1024, tr.length) };
    };
    const plain = muted(dry, false), m = muted(dry, true);
    const duck = structuredClone(dry);
    duck.on.DUCK = true; duck.knobs.DUCK[0] = 0;        /* (SOURCE: track 00) */
    const d = muted(duck, true);
    console.log(`a mute: the held note ${m.a.voices ? "kept its engine" : "a ghost"} (${m.a.ghosts} held ghosts); a new note on it ` +
                `${m.b.ghosts === 2 ? "a ghost" : "took an engine"}, a muted TB-303 ${m.b.tb ? "keeps its engine" : "a ghost"}; unmuted: ` +
                `${m.c.voices} sounding, ${m.c.ghosts} ghosts, ${m.db.toFixed(2)} dB against ${plain.db.toFixed(2)} never muted; ` +
                `DUCK's source muted: ${d.a.voices} sounding, ${d.a.ghosts} ghosts`);
    check("a mute: the muted track's held note becomes a ghost", m.a.voices === 0 && m.a.ghosts === 1);
    check("a mute: a muted track's new note starts as a ghost, a muted TB-303 keeps its engine", m.b.ghosts === 2 && m.b.tb && m.b.voices === 1);
    check("a mute: unmuted, its ghosts come back", m.c.voices === 3 && m.c.ghosts === 0);
    check("a mute: back from a mute, where its envelope would be (within 1 dB)", Math.abs(m.db - plain.db) <= 1);
    check("a mute: DUCK's source's muted note keeps its engine", d.a.voices === 1 && d.a.ghosts === 0);
}
/* another count: 16 keys held at VOICES 16, then 4: the 12 in the engines above it fade out and are ghosts; 16 again:
 * they come back, 8 a block at the most (4 each FM-1 block) */
rules(true);
{
    const jref = JU0;
    const { p, send } = fresh(16);
    send(inst(jref));
    for (let k = 0; k < 16; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref: jref, vol: 100, track: -1 });
    runFor(p, .5);
    send({ t: "voices", n: 4 });
    runFor(p, .05); p.post();
    const a = { voices: p.stats.voices.length, ghosts: p.stats.ghosts, low: p.stats.voices.every(v => v.eng < 4) };
    send({ t: "voices", n: 16 });
    runFor(p, BLK / SR); p.post();
    const b = p.stats.voices.length;
    runFor(p, .05); p.post();
    const c = { voices: p.stats.voices.length, ghosts: p.stats.ghostsAll };
    console.log(`another count: 16 keys at VOICES 4: ${a.voices} sounding, ${a.ghosts} ghosts; at 16 again: ${b} after a block, ${c.voices} after 50 ms`);
    check("another count: VOICES 4 makes ghosts of the notes above it", a.voices === 4 && a.ghosts === 12 && a.low);
    check("another count: VOICES 16 again brings them back, 8 a block", b === 12 && c.voices === 16 && c.ghosts === 0);
}
rules(false);
/* the FM-1's budget (BUDGET: 44 units of about 1 us an FM-1 block, each weight its work's mean measured there): the same
 * 16 keys, VOICES 4, then 16: a block (two FM-1 blocks) brings 2 back at the most, a return's checks and start
 * overdrawing an FM-1 block's budget, the rest of its work in the blocks after; all 12 back within 60 FM-1 blocks */
{
    const jref = JU0;
    const { p, send } = fresh(16);
    send(inst(jref));
    for (let k = 0; k < 16; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref: jref, vol: 100, track: -1 });
    runFor(p, .5);
    send({ t: "voices", n: 4 });
    runFor(p, .05); p.post();
    send({ t: "voices", n: 16 });
    let was = p.stats.voices.length, most = 0, all = -1;
    for (let b = 1; b <= 30 && all < 0; b++) {
        runFor(p, BLK / SR); p.post();
        const n = p.stats.voices.length;
        most = Math.max(most, n - was);
        was = n;
        if (n === 16) all = b;
    }
    console.log(`the FM-1's budget (${WE.BUDGET / 2} units an FM-1 block): VOICES 16 again, all 16 back in ${all} blocks, ${most} back in a block at most`);
    check("the FM-1's budget: 2 back a block at most (one each FM-1 block), all back within 60 FM-1 blocks", most <= 2 && all > 0);
}
/* the load guard (the firmware's pl_heavy): the same 16 keys, the first core's 10 counted for the half (10 x 4 x 64 =
 * 2,560 for a Juno-106's); VOICES 4, then 16 with every half heavy (the cap under any count): none comes back however
 * long; the cap back, all 16 back */
{
    const jref = JU0;
    const { p, send } = fresh(16);
    send(inst(jref));
    for (let k = 0; k < 16; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref: jref, vol: 100, track: -1 });
    runFor(p, .5);
    WE.loadCap = 2560;
    const at = !p.heavy();
    WE.loadCap = 2559;
    const over = p.heavy();
    send({ t: "voices", n: 4 });
    runFor(p, .05); p.post();
    WE.loadCap = -1;
    send({ t: "voices", n: 16 });
    runFor(p, .5); p.post();
    const held = p.stats.voices.length;
    WE.loadCap = WE.LOAD_CAP;
    let all = -1;
    for (let b = 1; b <= 400 && all < 0; b++) {         /* (their forward work: 0.55 s each, under the budget) */
        runFor(p, BLK / SR); p.post();
        if (p.stats.voices.length === 16) all = b;
    }
    console.log(`the load guard: every half heavy, ${held} sounding 0.5 s after VOICES 16; the cap back, all 16 in ${all} blocks`);
    check("the load guard: the first core's 10 Juno-106 notes counted 2,560 for the half; none back while every half is " +
          "heavy; the cap back, all back", at && over && held === 4 && all > 0);
}
/* the load guard's own split (the firmware's pl_heavy, voices_test's case): VOICES 12, 12 Juno-106 keys (768 a block).
 * With the second core running, the top 4 on its side (256 and its allowance, 128, under the rest's 512): the first
 * core's 8 counted, 2,048 for the half, the busier; without it all 12 on the first core, 3,072 */
{
    const jref = JU0;
    const { p, send } = fresh(12);
    send(inst(jref));
    for (let k = 0; k < 12; k++) send({ t: "on", id: "k" + k, time: .01 + k * .01, midi: 48 + 2 * k, ref: jref, vol: 100, track: -1 });
    runFor(p, .5);
    const at = (cap) => { WE.loadCap = cap; return p.heavy(); };
    const alone = !p.c2 && !at(3072) && at(3071);
    send({ t: "voices", n: 12, c2: true });
    const withC2 = p.c2 && !at(2048) && at(2047);
    WE.loadCap = WE.LOAD_CAP;
    p.post();
    console.log(`the load guard's split: 12 Juno-106 notes (${p.stats.voices.length} sounding), alone ${alone}, with the second core ${withC2}`);
    check("the load guard's split: VOICES 12 with the second core counts the first core's 8 (2,048, not 3,072 as alone)",
          alone && withC2 && p.stats.voices.length === 12);
}

/* ------------------------------------------ the lanes' ghosts (fw/tests/lanes_test.c's), at the worklet --- */
/* (the notes as the page's lanes send them: a lane's note `lane`, on its loop's track 16 + i; LEG a note naming the held
 * one, a glide its pitch a tick at a time, REC's hand-over the notes moved to the loop's track, a mute or a solo each
 * loop's track muted) */
/* section 2's rule on the lanes' ghosts (t_lane_ghosts): the keys holding every engine (VOICES 8), loop 00 a JUNO-106
 * note and LEG at row 1, loop 01 a DX7 note and a 3xx glide at row 1, both ghosts from their start; LEG moves loop 00's
 * ghost there at once (its pitch, its JUNO-106 key), the glide loop 01's a tick at a time, as on sounding lanes; the keys
 * let go, both back at their pitch and key. A ghost let go whose id a new note takes (the page's ids never come round;
 * were one to, the older note takes a name of its own) is no lane's note now: LEG and the glide pass it by. A stage's
 * held note taken by the keys (a held ghost) goes on as the loop's at REC's hand-over, back as the loop's */
rules(true);
{
    const JU = ref(0xF8, 0), DX = ref(0xF0, 0), ST = ref(0xF0, 3);
    const { p, send } = fresh(8);
    for (const r of [JU, DX, ST, DXS]) send(inst(r));
    const t = () => now();
    for (let k = 0; k < 8; k++) send({ t: "on", id: "k" + k, time: t(), midi: 72 + k, ref: DXS, vol: 100, track: -1, key: true });
    blocks(p, 2);
    send({ t: "on", id: "l0.1", time: t(), midi: 60, ref: JU, vol: 127, track: 16, lane: true });
    send({ t: "on", id: "l1.1", time: t(), midi: 60, ref: DX, vol: 127, track: 17, lane: true });
    blocks(p, 2);
    const a = p.byId.get("l0.1"), b = p.byId.get("l1.1");
    check("the keys holding every engine: loops 00's and 01's notes ghosts", !!a && !!b && a.e < 0 && b.e < 0 && a.gh === GHOST && b.gh === GHOST);
    send({ t: "on", id: "l0.2", time: t(), midi: 64, ref: JU, vol: 127, track: 16, lane: true, leg: "l0.1" });
    send({ t: "ctl", id: "l1.1", time: t(), bend: 2, vol: 1 });
    blocks(p);
    check(`LEG on a lane's ghost: its pitch and key there at once (bend ${a.bend}, key ${a.jkey})`, p.byId.get("l0.2") === a && a.held && a.midi === 60 &&
          a.bend === 4 && a.jkey === 64 && a.e < 0);
    check(`a glide on a lane's ghost: a tick on, 2 semitones (its bend ${b.bend})`, b.bend === 2 && b.e < 0);
    send({ t: "ctl", id: "l1.1", time: t(), bend: 4, vol: 1 });
    blocks(p);
    check("... there, a ghost still", b.bend === 4 && b.e < 0);
    for (let k = 0; k < 8; k++) send({ t: "off", id: "k" + k, time: t() });
    blocks(p, 2);
    const sa = a.e >= 0 ? p.en[a.e].st : null, sb = b.e >= 0 ? p.en[b.e].st : null;
    check("the keys let go: both back at their pitch and key", !!sa && !!sb && sa.pitch88 === 64 << 8 && sa.bend === 4 && sb.bend === 4);
    /* (a ghost let go of loop 00, then a new note under its id: the older one named anew; LEG and a glide reach the new) */
    for (let k = 0; k < 8; k++) send({ t: "on", id: "j" + k, time: t(), midi: 72 + k, ref: DXS, vol: 100, track: -1, key: true });
    send({ t: "on", id: "l0.3", time: t(), midi: 60, ref: JU, vol: 127, track: 16, lane: true });
    blocks(p, 2);
    const h = p.byId.get("l0.3");
    send({ t: "off", id: "l0.3", time: t() });
    blocks(p);
    send({ t: "on", id: "l0.3", time: t(), midi: 62, ref: JU, vol: 127, track: 16, lane: true });
    blocks(p);
    const n = p.byId.get("l0.3");
    send({ t: "on", id: "l0.4", time: t(), midi: 67, ref: JU, vol: 127, track: 16, lane: true, leg: "l0.3" });
    send({ t: "ctl", id: "l0.4", time: t(), bend: 1, vol: 1 });
    blocks(p);
    check(`LEG and a glide pass by a ghost let go whose id a new note took (the lane's note's bend ${n ? n.bend : "-"}, the ghost's ${h ? h.bend : "-"})`,
          !!h && !!n && h !== n && h.gh === GHOST && !h.held && h.id !== "l0.3" && p.byId.get("l0.4") === n && h.bend === 0 && h.jkey === 60 && n.jkey === 67);
    for (let k = 0; k < 8; k++) send({ t: "off", id: "j" + k, time: t() });
}
{   /* MOVE: the stage's held note a ghost, handed over by REC */
    const ST = ref(0xF0, 3);
    const { p, send } = fresh(8);
    send(inst(ST)); send(inst(DXS));
    send({ t: "on", id: "t0", time: now(), midi: 60, ref: ST, vol: 127, track: WE.STAGE, lane: true });
    blocks(p, 2);
    for (let k = 0; k < 8; k++) send({ t: "on", id: "k" + k, time: now(), midi: 48 + k, ref: DXS, vol: 100, track: -1, key: true });
    blocks(p, 2);
    const s = p.byId.get("t0"), ghost = !!s && s.gh === GHOST && s.held && s.track === WE.STAGE;
    check("a stage's held note taken by the keys: a held ghost", ghost);
    send({ t: "retrack", ids: ["t0"], track: 16 });
    blocks(p);
    check("MOVE (REC's hand-over): the held ghost goes on as loop 00's", ghost && s.gh === GHOST && s.held && s.track === 16 && s.own === 16);
    for (let k = 0; k < 8; k++) send({ t: "off", id: "k" + k, time: now() });
    blocks(p, 2);
    check("... the keys let go: back as the loop's", ghost && s.e >= 0 && s.held && s.track === 16);
}
rules(false);
/* the list full of a solo's ghosts when it ends (t_solo_full): VOICES 8, eight loops of a JUNO-106 pad chord of four held
 * (32 notes: 8 sound, 24 ghosts, the list full); loop 07 soloed (every other loop's track muted): every other loop's
 * notes ghosts, the 4 past the 24 lost, loop 07's in engines; the solo ended: none lost then, 4 back in the block (the
 * engines free), the rest waiting, held ghosts */
rules(true);
{
    const { p, send } = fresh(8);
    send(inst(PAD));
    for (let i = 0; i < 8; i++) for (let l = 0; l < 4; l++)
        send({ t: "on", id: `l${i}.${l}`, time: now(), midi: 36 + 5 * i + l, ref: PAD, vol: 127, track: 16 + i, lane: true });
    blocks(p, 2);
    const heldN = () => p.vs.filter(v => v.track >= 16 && v.track < 24 && v.held).length;
    p.post();
    const held0 = heldN(), v0 = p.stats.voices.length, g0 = p.gl.length;
    for (let i = 0; i < 7; i++) send({ t: "mute", track: 16 + i, on: true });
    blocks(p, 2);
    const loop7 = [0, 1, 2, 3].every(l => { const v = p.byId.get(`l7.${l}`); return v && v.e >= 0; });
    const held1 = heldN(), g1 = p.gl.length;
    for (let i = 0; i < 7; i++) send({ t: "mute", track: 16 + i, on: false });
    blocks(p);
    p.post();
    const held2 = heldN(), g2 = p.gl.length, v2 = p.stats.voices.length;
    console.log(`a solo's ghosts past the list: 32 notes at VOICES 8: ${v0} heard, ${g0} ghosts (${held0} held); loop 07 soloed: ${held1} held, ${g1} ` +
                `ghosts, loop 07's in engines ${loop7}; the solo ended: ${held2} held, ${g2} ghosts, ${v2} heard`);
    check("32 notes at VOICES 8: 8 heard, 24 ghosts", held0 === 32 && v0 === 8 && g0 === 24);
    check("loop 07 soloed: its notes heard, the others' ghosts, the list full, 4 past it lost", g1 === 24 && loop7 && held1 === 28);
    check("the solo ended: none lost then, 4 back in the engines free", held2 === 28 && g2 === 20 && v2 === 8);
}
rules(false);
/* DUCK's source (loop 01) muted keeps its engines and DUCK follows it; a change of the source acts as a mute on the old
 * source's muted notes and an unmute on the new one's muted ghosts; BYPASS (no DUCK in force: no source) the same; DUCK
 * off, a mute again (t_mute_duck) */
rules(true);
{
    const { p, send } = fresh(12);
    send(inst(PAD));
    send({ t: "fx", setup: M.state.loops.fx, mixer: M.state.loops.mixer, base: 16, tempo: 120, gates: M.state.gates.map(g => g.steps) });
    for (let i = 0; i < 3; i++) for (let l = 0; l < 4; l++)
        send({ t: "on", id: `l${i}.${l}`, time: now(), midi: 48 + 12 * i + 3 * l, ref: PAD, vol: 127, track: 16 + i, lane: true });
    blocks(p, 2);
    const fx = (code, value) => send({ t: "fxcmd", code, value, time: now() });
    const lanes = (i, ghosts) => [0, 1, 2, 3].filter(l => { const v = p.byId.get(`l${i}.${l}`); return v && (ghosts ? v.held && v.e < 0 && v.gh === GHOST : v.e >= 0); }).length;
    fx(0xAA, 1); fx(0xAB, 100); fx(0xAE, 8);            /* (DUCK's SOURCE loop 01, DEPTH 100, on: DUCK the chain's 9th) */
    blocks(p, 4);
    const on = p.fx.duckSrc === 17;
    send({ t: "mute", track: 17, on: true });
    blocks(p, 4);
    const r = [];
    r.push(["DUCK's source muted: its notes keep their engines", on && lanes(1, false) === 4]);
    fx(0xAA, 2); blocks(p, 2);
    r.push(["the source now loop 02: loop 01's muted notes ghosts", lanes(1, true) === 4 && lanes(2, false) === 4]);
    send({ t: "mute", track: 18, on: true }); blocks(p, 2);
    r.push(["loop 02 muted too: the source's notes keep their engines", lanes(1, true) === 4 && lanes(2, false) === 4]);
    fx(0xAA, 1); blocks(p, 2);
    r.push(["the source loop 01 again: its ghosts back, loop 02's ghosts", lanes(1, false) === 4 && lanes(2, true) === 4]);
    send({ t: "bypass", on: true }); blocks(p, 2);
    r.push(["BYPASS: no source, loop 01's muted notes ghosts too", lanes(1, true) === 4 && lanes(2, true) === 4]);
    send({ t: "bypass", on: false }); blocks(p, 2);
    r.push(["BYPASS off: the source's ghosts back", lanes(1, false) === 4 && lanes(2, true) === 4]);
    fx(0xAF, 8); blocks(p, 2);
    r.push(["DUCK off: the muted loops' notes all ghosts", lanes(1, true) === 4 && lanes(2, true) === 4 && lanes(0, false) === 4]);
    for (const [what, ok] of r) check(what, ok);
}
rules(false);

/* ------------------------------------------------------------------------- through the replay (player.js) --- */
/* a stand-in for audio.js on the worklet's clock: what it sends the worklet, as it sends it (an instrument once a run, a
 * FUSION pattern's cells with a layer's first note) */
let cur = null, sentInst = new Set(), sentPat = new Set(), idSeq = new Map();
const seqOf = id => idSeq.has(id) ? "n" + idSeq.get(id) : id;
const post = m => cur.send(m);
const ensure = ref => { if (sentInst.has(ref) || M.instType(ref) === "FUSION" || !M.inst(ref)) return; sentInst.add(ref); post(inst(ref)); };
const fpat = num => {
    if (sentPat.has(num)) return;
    sentPat.add(num);
    const pt = M.pattern(num), cells = [];
    if (pt) for (let r = 0; r < pt.rows; r++) for (let k = 0; k < 16; k++) {
        const c = pt.cells[r * 16 + k], ins = c && c.inst != null ? c.inst : (c && k < pt.tracks ? M.trackInst(pt, r, k) : null);
        cells[r * 16 + k] = c && c.note != null && c.note !== M.OFF && ins != null ? [c.note, ins] : null;
    }
    post({ t: "fpat", pat: num, cells });
};
const A = {
    time: () => wk.currentTime, instrument: ref => ensure(ref), changed() {}, instMsg: inst,
    noteOn(id, time, midi, ref, vol, track, o) {
        for (const k of [id.split("/")[0], id]) if (!idSeq.has(k)) idSeq.set(k, idSeq.size);   /* (the run's notes in the
                                                  * order sent, a FUSION note's before its layers: the ids' own count goes on) */
        ensure(ref);
        if (o.fcell) fpat(o.fcell.pat);
        post({ t: "on", id, time, midi, ref, vol, track, accent: !!o.accent, glide: !!o.glide, offset: o.offset || 0, params: o.params && o.params.length ? o.params : null,
               asEdited: !!o.asEdited, key: !o.song, leg: o.leg || null, tap: !!o.tap, lane: !!o.lane, fcell: o.fcell || null, retrig: !!o.retrig });
    },
    cut: (id, time) => post({ t: "cut", id, time }), tapped() {},
    fire: (track, time, idp) => post({ t: "fire", track, time, id: idp ? idp + "~f" : undefined }),
    noteOff: (id, time) => post({ t: "off", id, time }), ctl: (id, time, bend, vol, track) => post({ t: "ctl", id, time, bend, vol, track }),
    trackBend: (track, time, bend) => post({ t: "tbend", track, time, bend }),
    flush: (prefix, time, at) => post({ t: "flush", prefix, time, at: at != null ? at : null }),
    flushIds: (ids, time) => post({ t: "flushids", ids, time }), stageCol: track => post({ t: "stagecol", track }),
    retrack: (ids, track) => post({ t: "retrack", ids, track }),
    cutAt: (prefix, time) => { post({ t: "cutp", prefix, time }); post({ t: "killp", prefix, time: time + .004 }); },
    allOff: () => post({ t: "alloff" }), hush: on => post({ t: "hush", on }),
    iparam: (track, code, value, time) => post({ t: "iparam", track, code, value, time }),
    iclear(track, time, r) {
        const fus = r != null && M.instType(r) === "FUSION" && M.inst(r) ? M.inst(r).p.pattern : null;
        if (fus != null) fpat(fus);
        post({ t: "iclear", track, time, ref: r, fpat: fus });
    },
    trackMix: (track, key, value, time) => post({ t: "tmix", track, key, value, time }), trackMute: (track, on) => post({ t: "mute", track, on }),
    click: (time, accent, sound, level) => post({ t: "click", time, accent, sound, level }), punch: (k, on) => post({ t: "punch", k, on }),
    punchAt: (k, on, time, idp = "s") => post({ t: "tpunch", id: idp + "~", k, on, time }),
    barAt: (time, deck, idp = "s") => post({ t: "tbar", id: idp + "~", time, deck }), fxBypass: on => post({ t: "bypass", on }), fxWatch: on => post({ t: "watch", on }),
    fxCmd: (code, value, time, deck) => post({ t: "fxcmd", code, value, time, deck }), fxReset: () => post({ t: "fxreset" }),
    fxEmptyAt: time => post({ t: "fxempty", time }), pulseSideNow: () => 0, pulse() {},
    songFx: s => { if (s) post({ t: "fx", setup: s.fx, mixer: s.mixer, base: s === M.state.loops ? 16 : 0, tempo: 120, gates: M.state.gates.map(g => g.steps) }); },
    voices: () => post({ t: "voices", n: Math.min(16, +M.state.settings.VOICES) }), dj: m => post(Object.assign({ t: "dj" }, m)),
    djDeck: (deck, setup, mixer) => post({ t: "djdeck", deck, setup, mixer }), djReset: deck => post({ t: "djreset", deck }),
    djEcho: (deck, on, fx = "DELAY") => post({ t: "djecho", deck, on, fx }),
    djBrake: (deck, time, up, clear, fast = 1, dur = .5) => post({ t: "brake", deck, time, up: !!up, clear: !!clear, fast, dur }),
    songGain: g => post({ t: "songgain", g }), keysGain: g => post({ t: "keysgain", g }), keysDry: on => post({ t: "keysdry", on }),
};
A.fxNow = () => {
    if (P.what !== "dj") return;
    post({ t: "fx", setup: M.state.dj.fx, mixer: null, base: 0, tempo: P.tempo(), gates: M.state.gates.map(g => g.steps) });
    P.djPost();
};
page.PTFM.audio = A;
page.PTFM.ui = { song: 0xA0, pattern: 0xA6, inst: 0xA000, screen: { name: "SONGS" }, screens: {}, B: {}, muted: () => false, editing: () => false,
                 set: k => M.state.settings[k], toast() {}, refuse() {}, pausedHook() {}, clamp: (v, a, b) => v < a ? a : v > b ? b : v };
vm.runInContext(fs.readFileSync(path.join(PG, "player.js"), "utf8"), page, { filename: "player.js" });
const P = page.PTFM.player;
/* a fresh worklet for the replay (VOICES n) */
function replay(n) {
    M.state.settings.VOICES = n;
    cur = fresh(n, M.newFx());
    sentInst = new Set(); sentPat = new Set(); idSeq = new Map();
    nextTick = 0;
    return cur.p;
}
let nextTick = 0;
/* a block of the replay: its ticks due on the worklet's clock (every 20 ms), then the worklet's block */
function step(p) {
    if (wk.currentTime >= nextTick) { tickFn(); nextTick = wk.currentTime + .02 - (wk.currentTime - nextTick) % .02; }
    blocks(p);
}
const song = (i, from = 0) => { page.PTFM.ui.song = i; P.kept.song[i] = { pos: from, row: 0 }; nextTick = wk.currentTime; P.playSong(i); };

/* a song's stop (song_stopped): MIDNIGHT RUN at VOICES 8 with 8 keys held (its notes ghosts), stopped: every note of it
 * let go at once, each of its held ghosts (taken down before the stop) a ghost let go at the stop's frame; the keys let
 * go, they come back in their release or go; 10.5 s on, none left */
{
    const p = replay(8);
    song(0xA2);
    for (let b = 0; b < secs(1); b++) step(p);
    ensure(PAD);
    for (let k = 0; k < 8; k++) post({ t: "on", id: "k" + k, time: now(), midi: 48 + 2 * k, ref: PAD, vol: 100, track: -1, key: true });
    for (let b = 0; b < secs(1); b++) step(p);
    const held = p.gl.filter(g => g.track < 32 && g.held);
    P.pause();
    const at = p.frame;
    step(p);
    const let_ = p.vs.filter(v => v.track < 32).every(v => !v.held), still = held.filter(g => p.vs.includes(g));
    const when = still.every(g => !g.held && g.tOff === at);
    for (let k = 0; k < 8; k++) post({ t: "off", id: "k" + k, time: now() });
    for (let b = 0; b < secs(10.5); b++) step(p);
    const none = !p.vs.some(v => v.track < 32);
    console.log(`MIDNIGHT RUN under 8 keys, stopped: ${held.length} held ghosts of it (${still.length} still there a block on, each let go at the stop's ` +
                `frame ${when}); a block on every note of it let go ${let_}; 10.5 s on, none left ${none}`);
    check("a song's stop: its held ghosts let go at the stop's frame, gone or back in their release later", held.length > 0 && still.length > 0 && when && let_ && none);
}
/* songs of the test's own: pattern cells [track, row, note, instrument, vol, commands], `ntrk` tracks of `rows`; the song
 * TEMPO 125, 6 ticks a row, STOP at its end, playing the pattern `pos` times; its index (taken away by unmake) */
const made = [];
function makeSong(name, ntrk, rows, cells, pos = 1) {
    const n = M.state.patterns.indexOf(null, 0x10), pt = M.newPattern(n, ntrk, rows);
    for (const [k, r, note, ins, vol, cmds] of cells) {
        const c = M.newCell(note, ins == null ? null : ins, vol == null ? null : vol);
        for (const [cc, v] of cmds || []) c.cmds.push({ c: cc, v });
        pt.cells[r * 16 + k] = c;
    }
    M.state.patterns[n] = pt;
    const s = M.state.songs.indexOf(null, 0x10);
    M.state.songs[s] = Object.assign(M.newSong(), { name, tempo: 125, ticks: 6, rpb: 4, swing: 50, atEnd: "STOP", order: Array(pos).fill(n) });
    made.push([n, s]);
    return s;
}
const unmake = () => { for (const [n, s] of made) { M.state.patterns[n] = null; M.state.songs[s] = null; } made.length = 0; };
/* DJ's pass-by in a two-deck session (dj_pass_by: songs of the test's own on the decks, chords of a Juno-106 pad held),
 * VOICES 4: the left's four notes; the right's four 0.25 s on (both heard, the right the lower in the mix: the first
 * takes a left one's engine, the others their own deck's: ghosts); the left's fifth at row 16 (it takes the right's
 * last: its ghost made last); the crossfader full left (the right faded out); at row 32 the left's track 1 let go: the
 * right's held ghosts, first in the order, find none of their deck's to take and are passed by, the left's comes back
 * into its tail's engine. Throughout, a faded-out deck's note or ghost never takes an engine the heard deck's note held
 * in the block before */
{
    const lc = [0, 1, 2, 3].map(k => [k, 0, 48 + 3 * k, PAD, 0x7F]).concat([[4, 16, 60, PAD, 0x7F], [1, 32, M.OFF]]);
    const si = makeSong("PASS LEFT", 5, 64, lc), sj = makeSong("PASS RIGHT", 4, 64, [0, 1, 2, 3].map(k => [k, 0, 62 + 4 * k, PAD, 0x7F]));
    const p = replay(4), [Ld, Rd] = P.dj.sides, sync = P.dj.sync;
    P.dj.sync = false;
    P.djLoad(Ld, si); P.djLoad(Rd, sj);
    P.dj.xf = 16;
    const run = s => { for (let b = 0; b < secs(s); b++) step(p); };
    run(.05);
    P.djPlay(Ld, true);
    run(.25);
    P.djPlay(Rd, true);
    run(2.25);
    P.dj.xf = Ld === P.dj.sides[0] ? 0 : 32;
    P.djMix();
    run(.1);
    const left = Ld.base >> 4, right = Rd.base >> 4, deckOf = v => !v ? 3 : v.track < 32 ? v.track >> 4 : 2;
    const hg = p.gl.filter(g => g.track < 32 && g.held), gl = hg.filter(g => deckOf(g) === left).length, gr = hg.filter(g => deckOf(g) === right).length;
    const last = hg.length ? deckOf(hg[hg.length - 1]) : -1;
    /* (the note let go at row 32 a heard one of the left's, its tail the engine its ghost takes: which of the left's notes
     * the right's first took goes by their levels, so the row's note off is put on the first left track still heard) */
    const lp = M.pattern(M.state.songs[si].order[0]), heardT = [0, 1, 2, 3].find(k => p.vs.some(v => v.track === Ld.base + k && v.e >= 0 && v.held));
    lp.cells[32 * 16 + 1] = null;
    if (heardT != null) lp.cells[32 * 16 + heardT] = M.newCell(M.OFF, null, null);
    let taken = 0, lefts = 0, rights = 0, waits = 0;
    const N = Math.round(3300 * U / BLK);
    for (let b = 0; b < N; b++) {
        const had = [], before = p.gl.slice();
        for (let c = 0; c < p.max; c++) had.push(deckOf(p.note(c)));
        step(p);
        for (let c = 0; c < p.max; c++) taken += had[c] === left && deckOf(p.note(c)) === right ? 1 : 0;
        for (const g of before) if (g.e >= 0 && g.track < 32) { if (deckOf(g) === left) lefts++; else rights++; }
        waits += p.gl.some(g => g.held && deckOf(g) === right) ? 1 : 0;
    }
    console.log(`DJ, the right deck faded out at VOICES 4 (held ghosts then: the left's ${gl}, the right's ${gr}, the one made last the ` +
                `${last === right ? "right" : "left"}'s): the left deck's engines taken by the right's ${taken}; returns: the left's ${lefts}, the right's ` +
                `${rights}; the right's held ghosts waiting in ${waits} blocks of ${N}; the right deck heard ${p.djHeard(right)}`);
    check("DJ: a faded-out deck takes none of the heard deck's engines; the heard deck's ghosts come back past its", gl > 0 && gr > 0 && last === right &&
          !taken && lefts >= 1 && !rights && waits === N && !p.djHeard(right));
    P.djPause(Ld); P.djPause(Rd);
    P.dj.sync = sync; P.dj.xf = 0;
    unmake();
}

/* ------------------------------------------------------- section 2's rule, block by block (voices_test's dv_*) --- */
/* a song voice's control part as the rules leave it (what a ghost keeps as a sounding note does), times from the run's
 * start, and what its engine got of it: its pitch, key and level there, a sampler's place (a ghost's walked to the
 * block's end, as its return would find it). A song's older note whose return has begun has its track's commands as they
 * were at its return (a Juno-106's patch copy and a FUSION layer's own kept): its list and a sampler's place not compared */
function dvOf(p, v, t0, st) {
    if (v.gh) st.ghosted.add(v);
    if (st.ghosted.has(v) && (v.gh === BACKING || v.e >= 0)) st.back.add(v);
    const older = st.back.has(v) && p.latest[v.track] !== (v.id || "").split("/")[0], own = !older || v.engine === "JUNO" || !!v.fcell;
    const d = { key: seqOf(v.pid || v.id) + "@" + (v.tOn - t0), held: v.held, tOff: v.held ? -1 : v.tOff - t0, midi: v.midi, jkey: v.jkey,
                bend: p.bendOf(v), vol: v.vol, gain: v.gain, par: own ? JSON.stringify(v.engine === "JUNO" ? v.p : v.par) : null,
                inEng: v.e >= 0, ePitch: null, eKey: null, place: null, own, engine: v.engine };
    if (v.e >= 0) {
        const s = p.en[v.e].st;
        d.ePitch = s.bend; d.eKey = v.engine === "JUNO" ? s.pitch88 : null;
        if (v.engine === "SAMPLER" && !s.dead && WE.engines.SAMPLER.active(s))   /* (past its END or under its start: ended
                                                  * by its place, as a ghost's walk has it, its render's end to come) */
            d.place = Object.assign(WE.samp.of(s), { ended: !s.looping && (s.dir > 0 ? s.pos >= s.end - 1 : s.pos < 0) });
    } else if (v.engine === "SAMPLER" && v.gh && v.pl && p.sampOf(v)) d.place = placeNow(p, v);
    return d;
}
function dvDiff(a, b) {                                 /* (the reference's a, the run's b: what differs, null none) */
    if (a.held !== b.held || a.tOff !== b.tOff) return "held or let go";
    if (a.midi !== b.midi || a.jkey !== b.jkey) return "note or key";
    if (!near(a.bend, b.bend, 1e-9)) return "pitch";
    if (!near(a.vol, b.vol, 1e-9) || a.gain !== b.gain) return "level";
    if (b.own && a.par !== b.par) return "parameters";
    if (a.inEng && b.inEng && (a.eKey !== b.eKey || ((b.own || b.engine !== "SAMPLER") && !near(a.ePitch, b.ePitch, 1e-9)))) return "its engine's pitch or key";
    if (b.own && a.place && b.place && !placeSame(a.place, b.place)) return "a sampler's place";
    return null;
}
const dvMiss = [0, 0];
/* a run of driver d from a fresh worklet (how 0: the reference, VOICES 12, every note in an engine, its blocks kept; how
 * 1: twelve keys held on the takers from before it starts (every engine a key's: its notes born ghosts, followed by every
 * rule), let go at d.freeAt; how 2: VOICES 4 (its notes taking each other's engines, ghosts and back all the time); how
 * 3: its tracks muted and heard again every 75 blocks (150 of the FM-1's). Each block's song voices against the
 * reference's: every held note there a twin here and the reverse, every note let go in both the same; FILTER's held
 * tracks and the tracks' latest notes the same. The first difference printed */
function dvRun(d, how, ref) {
    const NAMES = ["the reference", "the keys holding every engine", "VOICES 4", "mutes"];
    const p = replay(how === 2 ? 4 : 12);
    if (d.prep) d.prep();
    for (let b = 0; b < secs(.05); b++) step(p);
    ensure(QUICK);
    if (how === 1) for (let k = 0; k < 12; k++) post({ t: "on", id: "q" + k, time: now(), midi: 36 + 3 * k, ref: QUICK, vol: 100, track: -1, key: true });
    for (let b = 0; b < secs(.2); b++) step(p);
    const t0 = p.frame, s0 = p.steals, st = { ghosted: new Set(), back: new Set() }, out = { seen: 0, most: 0, backs: 0, steals: 0, blocks: 0, gone: false, bad: null };
    const blocksOut = [];
    for (let b = 0; b < 12000; b++) {
        if (how === 1 && b === d.freeAt) for (let k = 0; k < 12; k++) post({ t: "off", id: "q" + k, time: now() });
        d.act(b);
        if (how === 3 && b % 75 === 37) d.mute(b % 150 === 37);
        const was = new Set(p.gl);
        step(p);
        const gh = p.gl.filter(g => g.track < 32).length;
        out.seen += gh > 0 ? 1 : 0;
        out.most = Math.max(out.most, gh);
        for (const g of was) if (g.track < 32 && g.e >= 0) out.backs++;
        const vs = p.vs.filter(v => v.track < 32 && !v.cutting);
        if (b === d.cutAt + d.cutWait)                  /* (the cut done: every note of it from before gone, its ghosts at once) */
            out.gone = !vs.some(v => (d.cutMem < 0 || v.track >> 4 === d.cutMem()) && v.tOn - t0 < d.cutAt * BLK);
        const blk = { v: vs.map(v => dvOf(p, v, t0, st)), held: Array.from(p.heldT.slice(0, 32)).join(""),
                      latest: JSON.stringify(Object.keys(p.latest).filter(k => +k < 32).sort().map(k => [k, seqOf(p.latest[k])])) };
        if (!how) {
            blocksOut.push(blk);
            if (!P.playing && !blk.v.length && b > d.cutAt + d.cutWait + 200) break;
            continue;
        }
        const r = ref[b];
        if (!out.bad && r) {
            if (r.held !== blk.held || r.latest !== blk.latest) out.bad = `block ${b}: FILTER's held tracks ${blk.held} (${r.held}) or the tracks' latest notes differ`;
            const used = new Set();
            for (const a of r.v) {
                if (out.bad) break;
                const j = blk.v.findIndex((x, k) => !used.has(k) && x.key === a.key);
                if (j < 0) { if (a.held) out.bad = `block ${b}: ${a.key} held there, not here`; else dvMiss[0]++; continue; }
                used.add(j);
                const why = dvDiff(a, blk.v[j]);
                if (why) out.bad = `block ${b}: ${a.key}: its ${why} differ (bend ${blk.v[j].bend} ${a.bend}, held ${blk.v[j].held} ${a.held}, in an engine ` +
                                   `${blk.v[j].inEng} ${a.inEng}, a sampler's place ${JSON.stringify(blk.v[j].place)} ${JSON.stringify(a.place)})`;
            }
            blk.v.forEach((x, k) => { if (!out.bad && !used.has(k)) { if (x.held) out.bad = `block ${b}: ${x.key} held here, not in the reference`; else dvMiss[1]++; } });
            if (out.bad) console.log(`  ${NAMES[how]}, ${out.bad}`);
        }
        if (b + 1 >= ref.length) break;
    }
    out.blocks = how ? ref.length : blocksOut.length;
    out.steals = p.steals - s0;
    for (let k = 0; k < 12; k++) post({ t: "off", id: "q" + k, time: now() });
    d.mute(false);
    if (d.end) d.end();
    P.pause();
    return how ? out : Object.assign(out, { ref: blocksOut });
}
{
    const OFF = M.OFF, c = (k, r, note, ins, cmds) => [k, r, note, ins, ins != null ? 0x7F : null, cmds];
    const t0 = [c(0, 0, 60, DXS), c(0, 4, OFF), c(0, 5, null, null, [[0x0A, 0x04]]), c(0, 8, 64, DXS), c(0, 10, 67, null, [[0xD3, 0]]),
                c(0, 12, 72, null, [[0x03, 0x08]]), c(0, 13, null, null, [[0x03, 0]]), c(0, 16, null, null, [[0x01, 0x04]]), c(0, 18, null, null, [[0x04, 0x46]]),
                c(0, 20, null, null, [[0x48, 30]]), c(0, 22, null, null, [[0x0C, 0x20]]), c(0, 24, null, null, [[0x0E, 0xC3]]), c(0, 26, 62),
                c(0, 28, null, null, [[0x14, 0x03]]), c(0, 32, 60, null, [[0x00, 0x37]]), c(0, 36, null, null, [[0x0A, 0x40]]), c(0, 40, 65),
                c(0, 42, null, null, [[0x02, 0x03], [0x49, 40]]), c(0, 48, OFF), c(0, 49, null, null, [[0x0A, 0x08]]), c(0, 56, 60)];
    const t1 = k => [c(k, 0, 48, PAD), c(k, 8, 50, null, [[0xD3, 0]]), c(k, 12, 55, null, [[0x03, 0x06]]), c(k, 14, null, null, [[0x03, 0]]),
                     c(k, 16, null, null, [[0x45, 40]]), c(k, 18, null, null, [[0x4E, 90]]), c(k, 20, OFF), c(k, 21, null, null, [[0x0A, 0x04]]), c(k, 24, 48),
                     c(k, 30, null, null, [[0x04, 0x38]]), c(k, 34, 53, null, [[0xD3, 0]]), c(k, 40, OFF), c(k, 44, 52)];
    const t2 = [c(2, 0, 60, LOOPS), c(2, 6, 72, null, [[0x03, 0x10]]), c(2, 7, null, null, [[0x03, 0]]), c(2, 10, null, null, [[0x03, 0]]),
                c(2, 14, null, null, [[0x02, 0x08]]), c(2, 16, null, null, [[0x01, 0x08]]), c(2, 20, null, null, [[0x45, 0xA0]]), c(2, 24, 62, null, [[0x09, 0x02]]),
                c(2, 28, null, null, [[0x04, 0x68]]), c(2, 30, null, null, [[0x42, 0]]), c(2, 32, 67, null, [[0xD3, 0]]), c(2, 36, OFF),
                c(2, 37, null, null, [[0x0A, 0x02]]), c(2, 44, 60), c(2, 46, 64, null, [[0x03, 0x20]]), c(2, 48, null, null, [[0x03, 0]])];
    const t3 = [c(3, 0, 60, CHS), c(3, 10, 65, null, [[0xD3, 0]]), c(3, 14, null, null, [[0x00, 0x47]]), c(3, 18, null, null, [[0x57, 5]]), c(3, 22, OFF),
                c(3, 23, null, null, [[0x0A, 0x06]]), c(3, 30, 60), c(3, 34, null, null, [[0x54, 8]]), c(3, 36, 67, null, [[0x03, 0x10]]),
                c(3, 38, null, null, [[0x03, 0]]), c(3, 44, OFF)];
    const t4 = [c(4, 0, 60, DXS), c(4, 2, OFF), c(4, 4, 62, DX2), c(4, 6, OFF), c(4, 8, 64, DXS), c(4, 10, OFF), c(4, 12, 65, DX2), c(4, 16, 67, DXS),
                c(4, 20, OFF), c(4, 28, 60, DX2), c(4, 30, OFF), c(4, 32, 62, DXS), c(4, 36, OFF)];
    const f0 = [c(1, 0, 60, ref(0xA5, 3)), c(1, 12, OFF), c(1, 16, 60), c(1, 18, null, null, [[0x0A, 0x02]]), c(1, 28, 72), c(1, 40, OFF), c(1, 44, 60),
                c(1, 56, OFF)];
    const five = makeSong("FIVE TRACKS", 5, 64, [...t0, ...t1(1), ...t2, ...t3, ...t4], 2), fus = makeSong("JUNO AND FUSION", 2, 64, [...t1(0), ...f0], 2);
    let muteOn = false;
    const songDrv = n => ({ act: b => { if (!b) song(n); else if (b === 2250) P.skip(1); },
                            mute: on => { muteOn = on; page.PTFM.ui.muted = t => muteOn && t < 6; P.mutesChanged(); },
                            cutAt: 2250, cutWait: 45, cutMem: -1, freeAt: 1500 });
    const ix = name => M.state.songs.findIndex(s => s && s.name === name);
    const [Ld, Rd] = P.dj.sides;
    const djDrv = {
        prep() { P.dj.xf = 16; P.djLoad(Ld, ix("BREAKS LOOP")); P.djLoad(Rd, ix("WORLD LOOP")); P.djMix(); },
        act(b) {
            if (!b) { P.djPlay(Ld); P.djPlay(Rd); }
            else if (b === 750) P.djKey(Ld, 3);
            else if (b === 1150) P.djKey(Rd, -4);
            else if (b === 1750) P.djBrake(Ld);
            else if (b === 2000 || b === 3500) P.djStopUp(Ld);
            else if (b === 2500) P.djBackspin(Rd);
            else if (b === 2900 || b === 4100) P.djStopUp(Rd);
            else if (b === 3250) P.djEchoOut(Ld, "DELAY");
            else if (b === 3900) P.djEchoOut(Rd, "REVERB");
            else if (b === 4500) { P.djPause(Ld); P.djPause(Rd); }
        },
        mute(on) {
            for (let t = 0; t < 16; t++) { Ld.mutes[t] = on && t < 4; Rd.mutes[t] = on && [0, 1, 4, 5].includes(t); }
            if (P.what === "dj") P.mutesChanged();
        },
        end() { P.djPause(Ld); P.djPause(Rd); P.dj.xf = 0; },
        /* (BRAKE's stand 0.5 s after its press, at block 1922; the page's replay sees it at its next 20 ms tick) */
        cutAt: 1923, cutWait: 8, cutMem: () => Ld.base >> 4, freeAt: 3000,
    };
    const S = [["a song of five tracks", songDrv(five)], ["a song of a Juno-106 and FUSION A5:03", songDrv(fus)], ["DJ: BREAKS LOOP and WORLD LOOP, KEY, the stops", djDrv]];
    const ok = [true, true, true, true];
    for (const [what, d] of S) {
        dvMiss[0] = dvMiss[1] = 0;
        const r0 = dvRun(d, 0), r = [r0];
        for (let how = 1; how < 4; how++) r.push(dvRun(d, how, r0.ref));
        console.log(`  (${what}: note-blocks let go with no twin, not compared: ${dvMiss[0]} of the reference's, ${dvMiss[1]} of the runs')`);
        console.log(`${what} (${r0.blocks} blocks): the reference ${r0.steals} notes taken; the keys holding every engine: ghosts in ${r[1].seen} blocks ` +
                    `(${r[1].most} at most), ${r[1].backs} back; VOICES 4: ${r[2].steals} taken, ghosts in ${r[2].seen} blocks, ${r[2].backs} back; mutes: ` +
                    `ghosts in ${r[3].seen} blocks, ${r[3].backs} back; the cut (K1's restart, BRAKE's end) left none of its notes ${r.map(x => x.gone).join(" ")}`);
        ok[0] = ok[0] && !r0.steals && !r0.seen && r0.gone;
        ok[1] = ok[1] && !r[1].bad && r[1].seen > 1000 && r[1].backs > 0 && r[1].gone;
        ok[2] = ok[2] && !r[2].bad && r[2].steals > 0 && r[2].seen > 0 && r[2].backs > 0 && r[2].gone;
        ok[3] = ok[3] && !r[3].bad && r[3].seen > 0 && r[3].backs > 0 && r[3].gone;
    }
    page.PTFM.ui.muted = () => false;
    check("section 2's rule: the songs and the decks alone, every note in an engine (the reference)", ok[0]);
    check("... the keys holding every engine: every ghost followed as its note, block by block", ok[1]);
    check("... at VOICES 4, notes taken and back: the same, block by block", ok[2]);
    check("... muted and heard again: the same, block by block", ok[3]);
    unmake();
}

/* ------------------------------------------------- the AUTOTEST's GHOST NOTES (fw/web/autotest.js), on the twin --- */
/* VOICES 8, LOOPS at 120 BPM, a bar of 16 rows, five POLY loops of 4 lanes each a chord of four held over the bar (two
 * JUNO-106 pads, a DX7 pad, a sampler instrument that loops, a CHIP pad), a sixth MONO, a TB-303 line in sixteenths: 20
 * held notes against the 7 engines left. Then its stretches, each counted from the twin's pool in windows of a second,
 * with the scenario's checks on them (the firmware's debug figures': made, back into free engines or tails' ones, held
 * ghosts dropped, the budget, the oldest note back). The keys are the FM-1's (F3 = 53) */
{
    const PADS = [ref(0xF8, 0x31), ref(0xF8, 0x35), ref(0xF4, 0x06), ref(0xA2, 0x08), ref(0xF9, 0x43)], TB = ref(0xFA, 0), EP = ref(0xF0, 0), GDX = ref(0xF4, 0);
    const CHORDS = [[0, 4, 7, 11], [2, 5, 9, 12], [4, 7, 11, 14], [5, 9, 12, 16], [7, 11, 14, 18]], SPREAD = [0, 4, 7, 11, 14, 2, 9, 16, 5, 12, 19, 21, 24, 18, 23, 26];
    const F3 = 53, WIN = secs(1), wAt = x => Math.round(x * WIN), was = M.state.loops;   /* (a step's block: x windows on) */
    const loops = M.newLoops();
    Object.assign(loops, { tempo: 120, rows: 16, quantize: "ROW" });
    PADS.forEach((r, i) => {
        const L = { inst: r, col: i, mono: false, lanes: 4, base: 16, repeats: 1, muted: false, soloed: false, shift: 0, cells: new Array(64).fill(null) };
        CHORDS[i].forEach((n, l) => { L.cells[l] = M.newCell(F3 + n, null, 100); });
        loops.list.push(L);
    });
    const tb = { inst: TB, col: 5, mono: true, lanes: 1, base: 16, repeats: 1, muted: false, soloed: false, shift: 0, cells: new Array(64).fill(null) };
    for (let r = 0; r < 16; r++) tb.cells[r * 4] = M.newCell(F3 + (r % 4 === 2 ? 12 : 0), null, 100);
    loops.list.push(tb);
    M.state.loops = loops;
    const p = replay(8);
    /* the figures, as the debug build counts them, from the twin's pool */
    let F = null;
    const fresh0 = () => ({ made: 0, madeMuted: 0, madeCount: 0, madeTb: 0, back: 0, tails: 0, heldDropped: 0, backMuted: 0, mostBack: 0, units: 0,
                            oldest: 0, oldestHeld: false, oldestKey: false });
    let inRoom = false, inCount = false, inMute = false, nBack = 0;
    const wrap = (k, f) => { const o = p[k].bind(p); p[k] = f(o); };
    wrap("ghAdd", o => v => { F.made++; if (inMute) F.madeMuted++; if (inCount) F.madeCount++; if (v.engine === "TB303") F.madeTb++; o(v); });
    wrap("room", o => (r, off) => { inRoom = true; try { return o(r, off); } finally { inRoom = false; } });
    wrap("free", o => v => { if (inRoom && v.held) F.heldDropped++; o(v); });
    wrap("count", o => n => { inCount = true; try { o(n); } finally { inCount = false; } });
    wrap("muteGhost", o => v => { inMute = true; try { o(v); } finally { inMute = false; } });
    wrap("forward", o => (g, c, bud) => {
        const n = o(g, c, bud);
        if (n) {
            F.back++; nBack++;
            if (p.mutedV(g)) F.backMuted++;
            const age = p.now - g.tOn;
            if (age > F.oldest) { F.oldest = age; F.oldestHeld = g.held; F.oldestKey = g.track === KEYS; }
        }
        return n;
    });
    let inRet = false;
    wrap("returns", o => () => { nBack = 0; inRet = true; try { o(); } finally { inRet = false; } F.units = Math.max(F.units, WE.BUDGET - p.bud.n); F.mostBack = Math.max(F.mostBack, nBack); });
    wrap("give", o => (v, c, most) => { const f = o(v, c, most); if (inRet && f) F.tails++; return f; });   /* (a return claiming a tail) */
    /* a stretch of `s` seconds, act(b) at each of its blocks (at[b]: what to do at the block of second x, b === at(x)):
     * its windows' figures and the pool's state at each window's end (the loops' tracks with a note in an engine, with a
     * held ghost; the songs' notes and the keys' in engines; the keys' held ghosts) */
    const stretch = (s, act) => {
        const W = [];
        for (let b = 0, n = secs(s); b < n; b++) {
            if (b % WIN === 0) { F = fresh0(); W.push(F); }
            if (act) act(b);
            step(p);
            if (b % WIN === WIN - 1 || b === n - 1) {
                let on = 0, gh = 0;
                for (const v of p.vs) if (v.track >= 16 && v.track < 22) { if (v.e >= 0 && v.held) on |= 1 << (v.track - 16); else if (v.gh && v.held) gh |= 1 << (v.track - 16); }
                F.now = { tracksOn: on, tracks: gh, songsOn: p.vs.filter(v => v.track < 32 && v.e >= 0).length, keysOn: p.vs.filter(v => v.track === KEYS && v.e >= 0).length,
                          keys: p.gl.filter(g => g.track === KEYS && g.held).length };
            }
        }
        return W;
    };
    const sum = (W, k) => W.reduce((a, f) => a + f[k], 0), most = (W, k) => Math.max(0, ...W.map(f => f[k]));
    const figs = (name, W) => console.log(`${name}: made ${sum(W, "made")} (muted ${sum(W, "madeMuted")}, past a smaller count ${sum(W, "madeCount")}), back ` +
        `${sum(W, "back")} (into tails' ${sum(W, "tails")}), held ghosts dropped ${sum(W, "heldDropped")}, TB-303 notes made ghosts ${sum(W, "madeTb")}, the ` +
        `most back in a block ${most(W, "mostBack")}, the budget's most ${most(W, "units")} units, the oldest back ${(most(W, "oldest") / SR).toFixed(1)} s`);
    /* (the budget, a ghost's checks (a sampler's its own) and the most one return's last piece can pass it by: its start,
     * a step less a unit, the steps its forward work can overshoot (the firmware's figures': the DX7 8, the Juno-106 1),
     * a sampler's seek) */
    const OVER = { DX7: 8, JUNO: 1, SAMPLER: 0, CHIP: 0 };
    const BOUND = WE.BUDGET - 1 + Math.max(...Object.keys(OVER).map(e => (e === "SAMPLER" ? WE.READY_S : WE.READY) + WE.COST[e][0] +
                                                                       WE.COST[e][1] - 1 + OVER[e] * WE.COST[e][1] +
                                                                       (e === "SAMPLER" ? WE.SEEK : 0)));
    P.playLoops(0);
    stretch(3);                                         /* (the loops going round: every note there) */
    /* OVER THE COUNT: all six playing */
    let W = stretch(20);
    figs("OVER THE COUNT", W);
    check(`OVER THE COUNT: ghosts made in every window (${W.filter(f => f.made).length} of ${W.length})`, W.every(f => f.made > 0));
    check(`OVER THE COUNT: no held ghost dropped (${sum(W, "heldDropped")}), no TB-303 note made a ghost (${sum(W, "madeTb")})`, !sum(W, "heldDropped") && !sum(W, "madeTb"));
    check(`OVER THE COUNT: returns into tails' engines in every window (${W.filter(f => f.tails).length} of ${W.length})`, W.every(f => f.tails > 0));
    /* MUTES: each pad loop muted in turn 1.5 s, then on again, 4 s apart; a window more after the last's next bar line */
    const mute = (i, on) => { M.state.loops.list[i].muted = on; P.mutesChanged(); };
    W = stretch(16 + 2 + 2, b => { for (let i = 0; i < 5; i++) { if (b === wAt(4 * i)) mute(i, true); if (b === wAt(4 * i + 1.5)) mute(i, false); } });
    figs("MUTES", W);
    const tr = i => 1 << i, okM = [], heardM = [];
    for (let i = 0; i < 5; i++) {                       /* (the window inside its mute: its first; heard before: the two before it) */
        const inMuteW = W[4 * i], after = W.slice(4 * i + 2, 4 * i + 4), before = i ? W.slice(4 * i - 2, 4 * i) : [];
        okM.push([`MUTES: loop ${i} muted, its notes all ghosts`, !!inMuteW && !(inMuteW.now.tracksOn & tr(i)) && !!(inMuteW.now.tracks & tr(i))]);
        okM.push([`MUTES: loop ${i} muted, the engines it freed taken by the other loops' ghosts (${inMuteW ? inMuteW.now.songsOn : "-"} notes in engines)`,
                  !!inMuteW && inMuteW.now.songsOn >= 7 && inMuteW.back > 0]);
        if (before.some(f => f.now.tracksOn & tr(i))) {
            heardM.push(i);
            okM.push([`MUTES: loop ${i} on again, a note of it back in an engine by its next bar line and a window more`, after.some(f => f.now.tracksOn & tr(i))]);
        }
    }
    for (const [what, ok] of okM) check(what, ok);
    check(`MUTES: ghosts made by the mutes ${sum(W, "madeMuted")}, back while muted ${sum(W, "backMuted")}`, sum(W, "madeMuted") > 0 && !sum(W, "backMuted"));
    console.log(`MUTES: loops heard before their mute and again after: ${heardM.join(" ") || "none"}`);
    /* SOLO AND BACK: loop 00 soloed 5 s, then all on */
    W = stretch(10, b => { if (b === 0 || b === wAt(5)) { M.state.loops.list[0].soloed = !b; P.mutesChanged(); } });
    figs("SOLO AND BACK", W);
    const solo = W.slice(1, 5);
    check(`SOLO AND BACK: loop 00 soloed, no other pad loop's note in an engine, its own there, no TB-303 ghost (${solo.length} windows)`,
          solo.every(f => !(f.now.tracksOn & 0x1E) && f.now.tracksOn & 1) && !sum(W, "madeTb"));
    check(`SOLO AND BACK: no held ghost dropped before the let-go and the muted ones (${sum(W, "heldDropped")})`, !sum(W, "heldDropped"));
    check(`SOLO AND BACK: the budget's most in a block ${most(W, "units")} within its limit ${BOUND}`, most(W, "units") <= BOUND);
    /* VOICES CHANGED: VOICES 16, 8, 12, 16, 4 s each, the loops playing */
    const voices = n => { M.state.settings.VOICES = n; post({ t: "voices", n }); };
    W = stretch(16, b => { for (const [k, n] of [[0, 16], [4, 8], [8, 12], [12, 16]]) if (b === wAt(k)) voices(n); });
    figs("VOICES CHANGED", W);
    const part = (a, b) => W.slice(a, b);
    check(`VOICES CHANGED: at 8, ghosts made past the count (${sum(part(4, 8), "madeCount")})`, sum(part(4, 8), "madeCount") > 0);
    for (const [what, a, b, n] of [["8 to 16", 0, 4, 16], ["8 to 12", 8, 12, 12], ["12 to 16", 12, 16, 16]]) {
        const x = part(a, b), inE = Math.max(0, ...x.map(f => f.now.songsOn));
        check(`VOICES CHANGED: ${what}, ${sum(x, "back")} back, the most in a block ${most(x, "mostBack")} (8 at most)` + (n === 16 ? `, ${inE} in the engines` : ""),
              sum(x, "back") > 0 && most(x, "mostBack") <= 8 && (n !== 16 || inE > 12));
    }
    P.pause();
    M.state.loops = was;
    /* THE KEYS ON TOP: SONGS, VOICES 8, MIDNIGHT RUN from position 03, twelve keys held 5 s on a DX7, then let go */
    voices(8);
    ensure(GDX);
    song(0xA2, 3);
    stretch(1);
    W = stretch(10, b => {
        if (b === 0) for (let k = 0; k < 12; k++) post({ t: "on", id: "g" + k, time: now(), midi: F3 + SPREAD[k], ref: GDX, vol: 100, track: -1, key: true });
        if (b === wAt(5)) for (let k = 0; k < 12; k++) post({ t: "off", id: "g" + k, time: now() });
    });
    figs("THE KEYS ON TOP", W);
    const held = W.slice(1, 5), after = W.slice(5);
    check(`THE KEYS ON TOP: while held, the keys' 8 in the engines and 4 ghosts, the song's none (${held.map(f => `${f.now.keysOn}/${f.now.keys}/${f.now.songsOn}`).join(" ")})`,
          held.every(f => f.now.keysOn === 8 && f.now.keys === 4 && !f.now.songsOn));
    check(`THE KEYS ON TOP: let go, the song's ghosts back (${sum(after, "back")}), 8 a block at most`, sum(after, "back") > 0 && most(after, "mostBack") <= 8 &&
          after.some(f => f.now.songsOn));
    P.pause();
    /* A LONG NOTE: nothing playing, VOICES 8, the keys on EL PIANO: one key held; at 20 s eight more, 3 s; the first let
     * go at 28 s */
    ensure(EP);
    W = stretch(30, b => {
        const k = x => b === wAt(x);
        if (k(0)) post({ t: "on", id: "e0", time: now(), midi: F3 + SPREAD[0], ref: EP, vol: 100, track: -1, key: true });
        if (k(20)) for (let i = 1; i < 9; i++) post({ t: "on", id: "e" + i, time: now(), midi: F3 + SPREAD[i], ref: EP, vol: 100, track: -1, key: true });
        if (k(23)) for (let i = 1; i < 9; i++) post({ t: "off", id: "e" + i, time: now() });
        if (k(28)) post({ t: "off", id: "e0", time: now() });
    });
    figs("A LONG NOTE", W);
    const old = most(W, "oldest") / SR, of = W.find(f => f.oldest === most(W, "oldest"));
    check(`A LONG NOTE: the oldest note back ${old.toFixed(1)} s old, ${of && of.oldestHeld ? "held" : "let go"}, ${of && of.oldestKey ? "a key's" : "not a key's"} (over 20 s, held, a key's)`,
          old > 20 && !!of && of.oldestHeld && of.oldestKey);
}

/* AN ENGINE'S LAST NOTE: the next note of the same engine in it goes on from its state, as the FM-1's pl_eng_start keeps
 * it (dx7_note_on, juno_note_on): the DX7's LFO phase (without its KEY SYNC) and its sample-and-hold, its operators'
 * phases without OSC KEY SYNC; the Juno-106's LFO, coupling capacitors and HPF, its filter where its note rang out by
 * itself (not cut) on a patch that does not self-oscillate. Another engine's note between, or a ghost's return: from
 * zero */
{
    const { p, send } = fresh(12);
    noReturns(p);
    const clone = x => JSON.parse(JSON.stringify(x));
    const into = (m, c) => {                            /* (note m into engine c as the pool's alloc would: its last kept) */
        p.now = p.frame;
        p.alloc = function () { this.alC = c; this.alBorn = false; return true; };
        try { p.noteOn(Object.assign({ vol: 127, time: now(), track: -1, key: true }, m)); } finally { delete p.alloc; }
        return p.byId.get(m.id);
    };
    const dxFree = clone(inst(DXS).p), dxSync = clone(inst(DXS).p);
    dxFree.osc_key_sync = false; dxFree.lfo.key_sync = false; dxFree.lfo.wave = "sample&hold"; dxFree.lfo.speed = 99;
    dxSync.lfo.key_sync = false;
    send(Object.assign(inst(DXS), { ref: 0xEF00, p: dxFree }));
    send(Object.assign(inst(DXS), { ref: 0xEF01, p: dxSync }));
    let a = at(p, { id: "a", ref: 0xEF00, midi: 60 }, 0);
    blocks(p, 40);
    let A = p.en[0].st;
    const ph = A.lfo.ph, rs = A.lfo.rs, ops = A.ops.map(o => o.ph);
    p.give(a, 0, 0); p.free(a);                          /* (its sound cut there, as a take's) */
    const last = p.en[0].last;
    let b = into({ id: "b", ref: 0xEF00, midi: 67 }, 0), B = p.en[0].st;
    check(`AN ENGINE'S LAST NOTE: a DX7 note goes on from the cut one's LFO (phase ${B.lfo.ph.toFixed(3)} = ${ph.toFixed(3)}, ` +
          `S&H ${B.lfo.rs} = ${rs}) and, without OSC KEY SYNC, its operators' phases`,
          !!last && last.kind === "DX7" && last.cut && p.en[0].last === null && ph > 1 && rs !== 0 &&
          B.lfo.ph === ph && B.lfo.rs === rs && B.ops.every((o, i) => o.ph === ops[i]) && ops.some(x => x !== 0));
    blocks(p, 10);
    const ph2 = p.en[0].st.lfo.ph;
    p.give(b, 0, 0); p.free(b);
    b = into({ id: "c", ref: 0xEF01, midi: 62 }, 0); B = p.en[0].st;
    check(`AN ENGINE'S LAST NOTE: with OSC KEY SYNC, its operators from 0, the LFO on (${B.lfo.ph.toFixed(3)})`,
          B.ops.every(o => o.ph === 0) && B.lfo.ph === ph2);
    p.give(b, 0, 0); p.free(b);
    send(inst(PAD));
    b = into({ id: "j", ref: PAD, midi: 60 }, 0);
    blocks(p, 4);
    p.give(b, 0, 0); p.free(b);
    b = into({ id: "d", ref: 0xEF00, midi: 60 }, 0); B = p.en[0].st;
    check("AN ENGINE'S LAST NOTE: a DX7 note after another engine's: from zero (its LFO, its S&H, its operators)",
          B.lfo.ph === 0 && B.lfo.rs === 0 && B.ops.every(o => o.ph === 0));
    p.give(b, 0, 0); p.free(b);
    /* the Juno-106: PAD (RES 0, VCA GATE: its note ends a tick after its let-go) */
    a = at(p, { id: "ja", ref: PAD, midi: 48 }, 1);
    blocks(p, 30);
    p.release(a);
    let n = 0;
    while (p.en[1].voice === a && n++ < 1000) blocks(p, 1);   /* (its envelope at 0: ~0.6 s) */
    A = p.en[1].last && p.en[1].last.st;
    const JK = ["lfoAcc", "lfoRising", "lfoPol", "dc", "hp0", "hp1"], S = ["s0", "s1", "s2", "s3"], kept = A && [...JK, ...S].map(k => A[k]);
    b = into({ id: "jb", ref: PAD, midi: 55 }, 1); B = p.en[1].st;
    check(`AN ENGINE'S LAST NOTE: a Juno-106 note after one that ended by itself (${n} blocks): its LFO, DC, HPF and filter go on`,
          !!A && !p.en[1].last && p.en[1].kind === "JUNO" && !!A.dc && S.some(k => A[k]) &&
          [...JK, ...S].every((k, i) => B[k] === kept[i]) && B.ph === 0 && B.vca === 0 && B.env > 0);
    blocks(p, 20);
    A = p.en[1].st;
    const kept2 = JK.map(k => A[k]);
    p.give(b, 1, 0); p.free(b);                          /* (cut: juno_cut's filter at zero) */
    b = into({ id: "jc", ref: PAD, midi: 50 }, 1); B = p.en[1].st;
    check("AN ENGINE'S LAST NOTE: a Juno-106 note after a cut one: its LFO, DC and HPF go on, its filter from zero",
          JK.every((k, i) => B[k] === kept2[i]) && S.every(k => B[k] === 0));
    p.give(b, 1, 0); p.free(b);
    p.en[1].wait = { gh: BACKING }; p.en[1].kind = "JUNO";   /* (a ghost's return there, given up: kind 255) */
    p.backStop(p.en[1].wait);
    b = into({ id: "jd", ref: PAD, midi: 50 }, 1); B = p.en[1].st;
    check("AN ENGINE'S LAST NOTE: after a return given up there: from zero", B.dc === 0 && B.lfoAcc === 0 && B.lfoPol === 0);
    returnsOn(p);
}

console.log(`ghosts: ${passed} checks passed, ${failed} failed`);
console.log(failed ? failed + " FAILED" : "ALL PASSED");
process.exit(failed ? 1 : 0);
