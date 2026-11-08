/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Cutting sounds (editor/js/slices.js; specs/editor.md, Recording and cutting sounds): the naming rule, TRIM
 * SILENCE's level, DETECT SLICES on a sound of known hits (each start 5 ms before its attack, the silent slices,
 * SENSITIVITY, starts closer than 30 ms one), slices from starts placed by hand, the joins' crossfades and a point
 * carried over them, and a kit's FUSION pattern.
 *   node tests/editor/slices_test.js */
"use strict";
const { ED, check, done } = require("./load.js");
const S = ED.slices, R = ED.rec;

/* ---- the naming rule ---- */
check(S.nameFor("KICK", []) === "KICK00" && S.nameFor("KICK", ["KICK00"]) === "KICK01", "naming: KICK -> KICK00, KICK01");
check(S.nameFor("TAKE00", ["TAKE00"]) === "TAKE01", "naming: TAKE00 -> TAKE01 (its digits read off)");
check(S.nameFor("DRUMLOOP 1", []) === "DRUMLOOP00", "naming: DRUMLOOP 1 -> DRUMLOOP00 (letters given up to the digits)");
check(S.nameFor("", []) === "SAMPLE00" && S.nameFor(null, ["SAMPLE00"]) === "SAMPLE01", "naming: no name counts as SAMPLE");
{
    const taken = Array.from({ length: 100 }, (_, i) => "HAT" + String(i).padStart(2, "0"));
    check(S.nameFor("HAT", taken) === "HAT100", "naming: past 99, three digits");
    const long = Array.from({ length: 100 }, (_, i) => "SNAREDRUM".slice(0, 8) + String(i).padStart(2, "0"));
    check(S.nameFor("SNAREDRUMS", long) === "SNAREDR100", "naming: three digits take a letter more");
}

/* ---- a sound of known hits: four 80 ms bursts at 0.2, 0.6, 1.0, 1.4 s in 1.8 s, silence (a faint noise) between ---- */
const rate = 44100, n = Math.round(1.8 * rate), x = new Float32Array(n);
let seed = 1;
const noise = () => { seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF; return seed / 0x7FFFFFFF * 2 - 1; };
for (let i = 0; i < n; i++) x[i] = noise() * 0.0005;                               /* -66 dB: under the silence level */
const hits = [0.2, 0.6, 1.0, 1.4];
for (const t of hits) {
    const a = Math.round(t * rate);
    for (let i = 0; i < Math.round(0.08 * rate); i++) x[a + i] = Math.sin(i * 0.3) * 0.8 * Math.exp(-i / (0.03 * rate));
}
{
    const [a, b] = S.trimSilence(x, rate);
    const pre = Math.round(0.005 * rate);
    check(Math.abs(a - (Math.round(0.2 * rate) - pre)) <= 2, `TRIM SILENCE: from 5 ms before the first sound (${a})`);
    check(b > Math.round(1.4 * rate) && b < Math.round(1.5 * rate), `TRIM SILENCE: to 5 ms after the last (${b})`);
}
{
    const sl = S.detect(x, rate, 0, n, 5), loud = sl.filter(s => !s.silent), quiet = sl.filter(s => s.silent);
    const w = Math.round(0.005 * rate);
    check(loud.length === 4, `DETECT SLICES: the four hits (${loud.length}: ${loud.map(s => (s.a / rate).toFixed(3)).join(" ")})`);
    check(loud.every((s, i) => Math.abs(s.a - (Math.round(hits[i] * rate) - w)) <= w), "each starts 5 ms before its attack (a window's step)");
    check(loud.every(s => s.b - s.a < 0.3 * rate), "each ends where its level drops and stays under for 100 ms");
    check(quiet.length >= 4 && sl[0].silent && sl[0].a === 0, "the silent slices between, and the part before the first start");
    check(sl.every((s, i) => !i || s.a === sl[i - 1].b) && sl[sl.length - 1].b === n, "the slices cover the whole sound, end to start");
    const soft = new Float32Array(x);                                                 /* a hit 18 dB under the others */
    for (let i = Math.round(0.6 * rate); i < Math.round(0.68 * rate); i++) soft[i] *= 0.125;
    const s1 = S.detect(soft, rate, 0, n, 1).filter(s => !s.silent).length, s10 = S.detect(soft, rate, 0, n, 10).filter(s => !s.silent).length;
    check(s10 === 4 && s1 <= 4, `SENSITIVITY: 10 finds the soft hit too (1: ${s1}, 10: ${s10})`);
    const dbl = new Float32Array(x);                                                   /* a flam 15 ms after the first hit */
    for (let i = 0; i < Math.round(0.02 * rate); i++) dbl[Math.round(0.215 * rate) + i] += 0.6;
    check(S.detect(dbl, rate, 0, n, 5).filter(s => !s.silent).length === 4, "starts closer than 30 ms count as one");
    const sel = S.detect(x, rate, Math.round(0.5 * rate), Math.round(1.2 * rate), 5).filter(s => !s.silent);
    check(sel.length === 2 && sel[0].a >= Math.round(0.5 * rate), "a selection cuts only its part");
    const hand = S.fromStarts(x, rate, 0, n, [Math.round(0.6 * rate), Math.round(1.0 * rate)]);
    check(hand.filter(s => !s.silent).length === 3 && !hand[0].silent, "from starts by hand: their ends found again; the part before not silent (a hit)");
    const fast = new Float32Array(n);                                                  /* short hits 120 ms apart */
    for (let i = 0; i < n; i++) fast[i] = noise() * 0.0005;
    for (const t of [0.2, 0.32, 0.44]) {
        const a = Math.round(t * rate);
        for (let i = 0; i < Math.round(0.04 * rate); i++) fast[a + i] = Math.sin(i * 0.3) * 0.8 * Math.exp(-i / (0.008 * rate));
    }
    const fl = S.detect(fast, rate, 0, n, 5).filter(s => !s.silent);
    check(fl.length === 3 && fl[0].b === fl[1].a && fl[1].b === fl[2].a && fl[2].b - fl[2].a < 0.1 * rate,
          `quiet for less than 100 ms: the slice ends at the next start, no silent slice between (${fl.map(s => (s.a / rate).toFixed(3) + "-" + (s.b / rate).toFixed(3)).join(" ")})`);
}

/* ---- joins ---- */
{
    const a = new Float32Array(1000).fill(1), j = S.join([a], 1000, [[0, 100], [300, 400], [600, 1000]]);
    const F = 5;
    check(j.length === 100 + 100 + 400 - 2 * F, `REMOVE: the parts kept joined, a 5 ms crossfade each (${j.length})`);
    check(j.ch[0].every(v => Math.abs(v - 1) < 1e-6), "a crossfade of the same level keeps it");
    check(j.map(50) === 50 && j.map(350) === 100 - F + 50 && j.map(200) === 100 - F + F, "a point carried: inside a part, and a removed one to its join");
    const st = new Float32Array(200), b = new Float32Array(200);
    for (let i = 0; i < 200; i++) { st[i] = i < 100 ? 1 : -1; b[i] = 0.5; }
    const j2 = S.join([st, b], 1000, [[0, 100], [100, 200]]);
    check(j2.ch.length === 2 && j2.ch[0][95] < 1 && j2.ch[0][95] > 0.5 && j2.ch[0][99] < 0 && j2.ch[1][97] === 0.5,
          "the channels kept, the join faded from one part into the next");
}

/* ---- a kit's FUSION pattern ---- */
{
    const p = S.kitPattern(3, 0), c = p.cells.get(R.cellKey(0x3D, 0));
    check(p.rows === 128 && p.tracks === 1 && p.step === 0 && p.cells.size === 3, "kit: 128 rows, one track, EDIT STEP 0");
    check(c && c.note === 60 && c.ins === 1 && c.cmds.length === 1 && c.cmds[0][0] === 0x4B && c.cmds[0][1] === 0,
          "kit: slice 2 on row 3D at C-4, its instrument, R4 0");
    check(R.unpackPattern(R.packPattern(p)).cells.get(R.cellKey(0x3C, 0)).note === 60, "kit: the pattern packs and reads back");
}
/* ---- new samples and kits on the panels (editor/js/cutting.js) ---- */
{
    const M = ED.model, K = R.KIND, C = ED.cutting;
    const snd = { rate: 44100, ch: [x.slice(Math.round(0.2 * rate), Math.round(0.3 * rate))], bits: 16 };
    const how = { chan: "mix", rate: 44100, bits: 16 };
    const loose = C.sampleOf(snd, how, "KICK", "NEW");
    let st = { L: [Object.assign(loose, { side: "L" })], R: [], base: [], fm1: {} };
    let r = C.addSamples(st, "L", [{ snd, how, name: "KICK" }], "FROM KICK");
    const nw = r.ok && r.st.L.find(i => i.id === r.ids[0]);
    check(nw && !nw.owner && M.sampleEntry(nw, r.st.L).name === "KICK00" && nw.src === "FROM KICK" && nw.sound && nw.sound.rate === 44100 && nw.num === null,
          "SAVE AS A NEW SAMPLE on the TABLE: a sample of its own, KICK00 beside KICK, its original kept");
    /* on the FM-1 panel: moved there, then new samples of their own at the next free numbers */
    const m1 = M.transfer(st, "L", [loose.id], {});
    const inR = m1.st.R.find(i => i.kind === K.SAMPLE);
    r = C.addSamples(m1.st, "R", [{ snd, how, name: "KICK00" },{ snd, how, name: "KICK00" }], "FROM KICK00");
    const newR = r.ok && r.ids.map(id => r.st.R.find(i => i.id === id));
    check(r.ok && newR.length === 2 && newR.map(i => i.num).join() === "1,2" && newR.map(i => M.sampleEntry(i, r.st.R).name).join() === "KICK00,KICK01" &&
          r.st.R.filter(i => i.kind === K.SAMPLER).length === 0,
          "SAVE AS SAMPLES on the FM-1 panel: samples of their own at its next free numbers 01, 02, named in turn, no sampler made");
    check(r.ok && M.toRecords(r.st.R).filter(x => x.kind === K.SAMPLE).map(x => x.num).join() === "0,1,2", "their records at those numbers");
    /* a number references name is passed over, 256 samples are the most */
    {
        const ghost = { id: M.newId(), side: "R", kind: K.SAMPLER, num: 0x20, orig: 0x20, link: {},
            recs: [{ sub: 0, ver: 2, payload: R.packSampler({ name: "GHOST", instruments: [Object.assign(ED.defs.newSlot(K.SAMPLER, "I"), { sample: 1 })] }) }] };
        const withGhost = M.fromRecords(M.toRecords(m1.st.R.concat([ghost])), { side: "R" }).items;
        const rg = C.addSamples(Object.assign({}, m1.st, { R: withGhost }), "R", [{ snd, how, name: "G" }], "X");
        check(rg.ok && rg.st.R.find(i => i.id === rg.ids[0]).num === 2, "SAVE AS SAMPLES: 01 is named by an instrument with no sample, passed over: the sample takes 02");
        const flood = C.addSamples(m1.st, "R", Array.from({ length: 256 }, () => ({ snd: { rate: 8000, ch: [new Float32Array(10)], bits: 8 }, how: { chan: "mix", rate: 8000, bits: 8 }, name: "Z" })), "X");
        check(!flood.ok && /^NOT ENOUGH SAMPLE NUMBERS: 256 NEEDED, \d+ FREE$/.test(flood.msg), "256 samples to a panel holding some: none saved, saying how many it needs: " + flood.msg);
        const mk = n => ({ snd: { rate: 8000, ch: [new Float32Array(10)], bits: 8 }, how: { chan: "mix", rate: 8000, bits: 8 }, name: "Z" + n });
        const fill = C.addSamples(Object.assign({}, m1.st, { R: [] }), "R", Array.from({ length: 256 }, (_, n) => mk(n)), "X");
        check(fill.ok && fill.st.R.length === 256, "256 samples fill an empty panel's numbers");
        const last = C.addSamples(fill.st, "R", [mk(999)], "X");
        check(!last.ok && last.msg === "ALL 256 SAMPLE NUMBERS USED", "all 256 numbers used: " + last.msg);
    }
    const huge = { rate: 44100, ch: [new Float32Array(S.MAX_FRAMES + 10)], bits: 16 };
    const big = C.addSamples(m1.st, "R", [{ snd: huge, how, name: "A" },{ snd, how, name: "B" }], "X");
    check(!big.ok && big.msg === "SLICE 01 IS TOO LONG", "a slice too long for a sampler: none saved, " + big.msg);
    const one = C.addSamples(m1.st, "R", [{ snd: huge, how, name: "A" }], "X");
    check(!one.ok && /^TOO LONG FOR A SAMPLE/.test(one.msg), "a new sample too long for a sampler: refused, " + one.msg);
    const bigL = C.addSamples(st, "L", [{ snd: huge, how, name: "A" }, { snd, how, name: "B" }], "X");
    const bl = bigL.ok && bigL.ids.map(id => bigL.st.L.find(i => i.id === id));
    check(bl && M.isLong(bl[0]) && !M.isLong(bl[1]) && bl[0].long === S.MAX_FRAMES + 10,
          "SAVE AS SAMPLES from a loose one on the TABLE: the slice too long kept TOO LONG, the other a sample");
    const kitL = C.makeKit(st, "L", loose, [{ snd: huge, how, name: "A" }, { snd, how, name: "B" }], "A", "X");
    check(!kitL.ok && kitL.msg === "SLICE 01 IS TOO LONG", "MAKE A KIT on the TABLE: a slice too long refused, " + kitL.msg);
    /* MAKE A KIT on the TABLE, then on the FM-1 panel */
    const parts = [0, 1, 2].map(i => ({ snd, how, name: "KICK" }));
    r = C.makeKit(st, "L", loose, parts, "KICK", "KIT FROM KICK");
    const kS = r.ok && r.st.L.find(i => i.kind === K.SAMPLER), kF = r.ok && r.st.L.find(i => i.kind === K.FUSION), kP = r.ok && r.st.L.find(i => i.kind === K.PATTERN);
    const ks = kS && R.unpackSampler(kS.recs[0].payload);
    const kSamples = kS && ks.instruments.map(s => r.st.L.find(i => i.id === kS.link[M.lk("m", s.sample)]));
    check(ks && ks.name === "KICK" && ks.instruments.length === 3 && r.st.L.filter(i => i.kind === K.SAMPLE).length === 4 && kSamples.every(Boolean) &&
          ks.instruments.every((s, i) => s.sample === i && s.start === 0 && s.end === M.sampleEntry(kSamples[i], r.st.L).length && !s.loop && s.base_note === 60 &&
          s.name === M.sampleEntry(kSamples[i], r.st.L).name),
          "MAKE A KIT: a sampler named after the sample, instrument n - 1 naming slice n's sample of its own, played whole at C-4");
    const kf = kF && R.unpackCart(K.FUSION, kF.recs[0].payload);
    check(kf && kf.name === "KICK KIT" && kf.slots[0].name === "KICK" && kF.link[M.lk("p", 0)] === kP.id && kP.link[M.lk("c", 0)] === kS.id,
          "MAKE A KIT: a FUSION cartridge KICK KIT, its slot the kit playing the pattern, the pattern naming the sampler");
    check(kP && R.unpackPattern(kP.recs.map(x => x.payload)).cells.size === 3, "MAKE A KIT: its pattern a key a slice");
    r = C.makeKit(m1.st, "R", inR, parts, "KICK", "KIT FROM KICK");
    const rF = r.ok && r.st.R.find(i => i.kind === K.FUSION), rP = rF && r.st.R.find(i => i.kind === K.PATTERN && M.refs(rF).some(f => f.num === i.num));
    const rS = r.ok && r.st.R.find(i => i.kind === K.SAMPLER);
    check(r.ok && rF && rF.num != null && rP && rP.num < 0xF0 && r.st.L.length === m1.st.L.length,
          "MAKE A KIT on the FM-1 panel: numbers given as a move gives them, the TABLE as it was (" + (r.msg || "") + ")");
    check(rS && M.refs(rS).length === 3 && M.refs(rS).every(f => { const t = r.st.R.find(i => i.id === rS.link[M.lk("m", f.num)]); return t && t.kind === K.SAMPLE && t.num === f.num; }) &&
          r.st.R.filter(i => i.kind === K.SAMPLE).length === 4, "MAKE A KIT on the FM-1 panel: the kit's three samples numbered beside the one there, its instruments naming those numbers");
    const tooMany = C.makeKit(st, "L", loose, Array.from({ length: 65 }, () => ({ snd, how, name: "X" })), "X", "X");
    check(!tooMany.ok && tooMany.msg === "64 SLICES AT MOST IN A KIT", "65 slices: " + tooMany.msg);
}
/* ---- TOO LONG (specs/editor.md, Too long for the FM-1): a loose sample on the TABLE longer than a sample can be ---- */
(async () => {
    const M = ED.model, K = R.KIND, C = ED.cutting, O = ED.optimize;
    /* a take of 31.2 s, stereo, 44.1 kHz: a 200 Hz tone, kept as a 16-bit sample at 44.1 kHz as the recorder keeps it */
    const n31 = Math.round(31.2 * 44100), l = new Float32Array(n31), r2 = new Float32Array(n31);
    for (let i = 0; i < n31; i++) { l[i] = 0.5 * Math.sin(2 * Math.PI * 200 * i / 44100); r2[i] = l[i] * 0.5; }
    const take = { rate: 44100, ch: [l, r2], bits: 16 }, how = { chan: "mix", rate: 44100, bits: 16 };
    let st = { L: [], R: [], base: [], fm1: {} };
    const t0 = C.addSamples(st, "L", [{ snd: take, how, name: "TAKE" }], "RECORDING");
    const it = t0.ok && t0.st.L.find(i => i.id === t0.ids[0]);
    const mixOk = it && it.sound.ch.length === 1 && [0, 1234, n31 - 1].every(i => Math.abs(it.sound.ch[0][i] - (l[i] + r2[i]) / 2) < 1e-6);
    check(it && M.isLong(it) && it.long === n31 && !it.recs.length && M.size(it) === 0 && mixOk,
          "TOO LONG: a take of 31.2 s kept whole, its original and no record; a stereo sound mixed to one channel as it comes");
    check(it && M.nameOf(it, t0.st.L) === "TAKE00" && M.kindText(it, t0.st.L) === "16-BIT · 44.1 KHZ · 31.20 S",
          "TOO LONG: named by the naming rule, its kind its length (" + (it && M.kindText(it, t0.st.L)) + ")");
    check(it && M.longText(it) === "TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST", "TOO LONG: the words, " + (it && M.longText(it)));
    st = t0.st;
    /* its hash its original's: another take of other audio differs, the same one again is equal */
    const other = C.sampleOf({ rate: 44100, ch: [r2], bits: 16 }, how, "TAKE00", "RECORDING", true);
    const same = Object.assign({}, it, { id: "same" });
    const hs = M.hashes([it, Object.assign(other, { side: "L" }), same]);
    check(hs.get(it.id) !== hs.get(other.id) && hs.get(it.id) === hs.get("same"), "TOO LONG: its hash its original's");
    /* moves refused, and everything picked with it */
    const mv = M.transfer(st, "L", [it.id], {});
    check(!mv.ok && mv.msg === "TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST: NOTHING MOVED", "TOO LONG: a move refused, " + mv.msg);
    const kick = Object.assign(C.sampleOf({ rate: 44100, ch: [l.slice(0, 4410)], bits: 16 }, how, "KICK", "NEW"), { side: "L" });
    const st2 = Object.assign({}, st, { L: st.L.concat([kick]) });
    const mv2 = M.transfer(st2, "L", [kick.id, it.id], {});
    check(!mv2.ok && mv2.msg === "TAKE00 IS TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST: NOTHING MOVED", "TOO LONG: picked with another, " + mv2.msg);
    const m1 = M.transfer(st2, "L", [kick.id], {});
    /* the tab's changes: TRIM to 10 s fits, WHOLE SAMPLE back is TOO LONG again; a sampler's sample is refused */
    const ctx = O.context(it, st.L);
    const trimmed = O.remake(it, ctx, Object.assign({}, it.sound.cut, { start: 0, end: 441000 }));
    const te = M.sampleEntry(trimmed, st.L);
    check(!M.isLong(trimmed) && trimmed.recs.length === 1 && te.length === 441000 && te.rate === 44100 && te.coding === 2 && te.name === "TAKE00",
          "TOO LONG: TRIM to 10 s makes it a sample (its name kept)");
    const whole = O.remake(trimmed, O.context(trimmed, st.L), Object.assign({}, trimmed.sound.cut, { start: 0, end: n31 }));
    check(M.isLong(whole) && whole.long === n31 && !whole.recs.length && M.nameOf(whole, st.L) === "TAKE00", "TOO LONG: WHOLE SAMPLE back makes it TOO LONG again");
    const lower = O.remake(it, ctx, Object.assign({}, it.sound.cut, { rate: 22050 }));
    check(!M.isLong(lower) && M.sampleEntry(lower, st.L).length === Math.floor(n31 / 2), "TOO LONG: a lower RATE makes it fit (22.05 kHz: 15.6 s of frames)");
    const inS = m1.st.R.find(i => i.kind === K.SAMPLE), ctxS = O.context(inS, m1.st.R);
    let threw = null;
    const longSnd = Object.assign({}, ctxS.snd, { rate: 44100, length: n31, channels: 1, ch: [l] });
    try { O.remake(inS, Object.assign({}, ctxS, { snd: longSnd }), { start: 0, end: n31, chan: "mix", rate: 44100, bits: 16 }); }
    catch (e) { threw = e; }
    check(threw && threw.tooLong === n31, "TOO LONG: a sample on the FM-1 panel made too long is refused");
    /* OPTIMIZE SAMPLES: the proposed rate makes it fit, counted as smaller */
    const p = O.plan(st.L, [it.id], "smaller");
    const pi = p.results[0] && p.results[0].item;
    check(p.results.length === 1 && p.fits === 1 && pi && !M.isLong(pi) && p.before === p.after && M.sampleEntry(pi, st.L).rate < 44100,
          "TOO LONG: OPTIMIZE SAMPLES at SMALLER makes it fit, counted as smaller (" + (pi && M.sampleEntry(pi, st.L).rate) + " HZ)");
    const pl = O.plan(st.L, [it.id], "lossless");
    check(pl.results.length === 0 && pl.fits === 0, "TOO LONG: OPTIMIZE at LOSSLESS still too long: left as it is");
    const ap = O.apply(st, "L", p), ai = ap.L.find(i => i.id === it.id);
    check(ai && !M.isLong(ai) && ai.recs.length === 1 && ai.sound && ai.sound.ch === it.sound.ch, "TOO LONG: OPTIMIZE applied: a sample, its original kept");
    /* a sound dropped on the TABLE (a WAV of white noise: its proposed rate 44.1 kHz, too long at 8 bits) */
    const wav = (() => {
        const n = n31, b = new Uint8Array(44 + n * 2), v = new DataView(b.buffer);
        const s = (o, t) => { for (let i = 0; i < t.length; i++) b[o + i] = t.charCodeAt(i); };
        s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVE"); s(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
        v.setUint16(22, 1, true); v.setUint32(24, 44100, true); v.setUint32(28, 88200, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
        s(36, "data"); v.setUint32(40, n * 2, true);
        let z = 7;
        for (let i = 0; i < n; i++) { z = (z * 1103515245 + 12345) & 0x7FFFFFFF; v.setInt16(44 + i * 2, (z >> 15) - 32768, true); }
        return b;
    })();
    const d = await ED.drops.convert("NOISE.WAV", wav);
    const di = d.items[0];
    check(d.items.length === 1 && M.isLong(di) && di.side === "L" && di.sound && M.nameOf(di, d.items) === "NOISE" && / TOO LONG FOR THE FM-1: 31\.2 S/.test(d.what),
          "TOO LONG: a dropped sound longer than a sample can be is kept so, not refused (" + d.what + ")");
    /* kept in the browser: an original lost takes a sample too long with it (the others keep their records) */
    require("../../editor/js/keep.js");
    const saved = { st: { L: [Object.assign({}, it, { sound: { oid: "gone", cut: it.sound.cut, rate: 44100 } }),
        Object.assign({}, kick, { sound: { oid: "gone2", cut: kick.sound.cut, rate: 44100 } })], R: [], base: [] } };
    const j = ED.keep.join(saved, [], []);
    check(j.st.L.length === 1 && j.st.L[0].id === kick.id && !j.st.L[0].sound && j.st.L[0].recs.length === 1,
          "TOO LONG: its original lost, it goes; a sample with a record stays as its record has it");
    done("slices");
})().catch(e => { check(false, "TOO LONG: " + (e && e.stack || e)); done("slices"); });
