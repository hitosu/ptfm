/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Samples made smaller (specs/editor.md, The tabs: SAMPLE's OPTIMIZE; FILES: OPTIMIZE SAMPLES), shared by the SAMPLE tab
 * and the TABLE's menu: a sample's source (a sound's own audio, or the sample decoded), what its sampler's instruments
 * play of it, OPTIMIZE's levels, and a new cut put in a panel with its sampler following it (the entry; the
 * instruments' START, END and loop where the same sound now is; their LEVEL up by what was lowered).
 *   ED.optimize.source(it, items), context(it, items, src) -> { src, snd, cut0, used, players, playersAt, entry }
 *   remake(it, ctx, cut) -> the sample made again from its source (its record holds the entry and the audio)
 *   follow(st, side, id, cut, cut0, rate) -> st: the instruments of every sampler naming the sample following the change
 *   samplesOf(items, ids) -> the samples picked, a sampler's with it; LEVELS
 *   plan(items, ids, level) -> { results, picked, before, after, failed, fits (samples too long that now fit) };
 *   apply(st, side, plan) -> st (one step) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND;
const LEVELS = ["lossless", "smaller", "smallest"];

/* the instruments of every sampler on the panel that name the sample: [{ item (the sampler), at (the slot), inst }] */
function playersOf(it, items) {
    const out = [];
    for (const o of items) {
        if (o.kind !== K.SAMPLER || o.dim) continue;
        for (const f of M.refs(o)) {
            if (f.sp !== "m" || o.link[M.lk(f.sp, f.num)] !== it.id) continue;
            const inst = R.unpackSamplerInst(o.recs[0].payload.subarray(f.at, f.at + R.SAMPLER_SLOT), 2);
            if (inst) out.push({ item: o, at: (f.at - 10) / R.SAMPLER_SLOT, inst });
        }
    }
    return out;
}
/* the source every change starts from: the sound's own audio, or the sample's decoded */
function source(it, items) {
    if (it.sound) return it.sound;
    const e = M.sampleEntry(it, items), d = R.ptrDecode(R.unpackSample(it.recs[0].payload).audio), full = d.bits === 16 ? 32768 : 128;
    const ch = new Float32Array(d.samples.length);
    for (let i = 0; i < ch.length; i++) ch[i] = d.samples[i] / full;
    return { rate: e ? e.rate : 22050, ch: [ch], bits: d.bits, own: true, cut: { start: 0, end: ch.length, chan: "mix", rate: e ? e.rate : 22050, bits: d.bits } };
}
const toSnd = (v, c, rate) => c.start + v * rate / c.rate, fromSnd = (f, c, rate) => Math.round((f - c.start) * c.rate / rate);
/* the cut it has now, and what its sampler's instruments that play it use of the sound (in the sound's frames) */
function context(it, items, src) {
    src = src || source(it, items);
    const snd = { rate: src.rate, channels: src.ch.length, length: src.ch[0].length, ch: src.ch, bits: src.bits };
    const cut0 = src.cut || { start: 0, end: snd.length, chan: "mix", rate: src.rate, bits: 8 };
    const playersAt = playersOf(it, items), players = playersAt.map(p => p.inst);
    const used = players.length ? {
        first: Math.floor(Math.min(...players.map(s => toSnd(Math.min(s.start, s.loop ? s.loop_start : s.start), cut0, snd.rate)))),
        last: Math.ceil(Math.max(...players.map(s => toSnd(Math.max(s.end, s.loop ? s.loop_end : 0), cut0, snd.rate)))) } : null;
    const entry = M.sampleEntry(it, items) || { name: "SAMPLE", rate: cut0.rate, length: 0, coding: 1 };
    return { src, snd, cut0, used, players, playersAt, entry };
}
/* (a sample on the TABLE with an original and no instrument playing it may come out longer than a sample can be: TOO
 * LONG, its cut kept; any other is refused, the error said) */
function remake(it, ctx, c) {
    let s;
    try { s = ED.conv.soundToSample(ctx.snd, Object.assign({ name: ctx.entry.name }, c)); }
    catch (e) {
        if (!e.tooLong || it.side !== "L" || (ctx.players && ctx.players.length) || ctx.src.own) throw e;
        return M.asLong(Object.assign({}, it, { sound: Object.assign({}, it.sound || ctx.src, { cut: c }) }),
            Object.assign({}, ctx.entry, { rate: c.rate, coding: c.bits === 16 ? 2 : 1 }), e.tooLong);
    }
    /* (the record holds the entry and the audio: the sample made again is whole in itself) */
    const n = Object.assign({}, it, { recs: [{ sub: 0, ver: R.VERSION[K.SAMPLE],
        payload: R.packSample(Object.assign({}, ctx.entry, s.entry, { name: ctx.entry.name }), s.audio) }] });
    delete n.long; delete n.entry;
    if (!ctx.src.own) n.sound = Object.assign({}, it.sound || ctx.src, { cut: c });
    return n;
}
/* the sample as it is in its panel now (a new cut put there): the instruments of every sampler that name it follow the
 * change: their START, END and loop where the same sound now is (a cut, a rate), their LEVEL up by what OPTIMIZE lowered */
function follow(st, side, id, cut, cut0, rate, map) {
    const items = st[side], cur = items.find(x => x.id === id);
    if (!cur || cur.dim || !cur.recs.length) return st;
    const lost = (cut.gain || 0) - (cut0.gain || 0), len = M.sampleEntry(cur, items).length;
    const moved = !!map || cut.start !== cut0.start || cut.rate !== cut0.rate || cut.end !== cut0.end;
    if (!moved && !lost) return st;
    /* (map: the joins REMOVE made, a point of the sound as it was to where it is now: in a removed part, its join) */
    const at = v => Math.max(0, Math.min(len, fromSnd(map ? map(toSnd(v, cut0, rate)) : toSnd(v, cut0, rate), cut, rate)));
    const by = new Map();                           /* sampler id -> its slots naming the sample */
    for (const p of playersOf(cur, items)) { if (!by.has(p.item.id)) by.set(p.item.id, []); by.get(p.item.id).push(p.at); }
    for (const [oid, slots] of by) {
        const o = st[side].find(x => x.id === oid), s = R.unpackSampler(o.recs[0].payload);
        s.instruments = s.instruments.map((x, k) => {
            if (!x || !slots.includes(k)) return x;
            const y = Object.assign({}, x, moved ? { start: at(x.start), end: at(x.end), loop_start: at(x.loop_start), loop_end: at(x.loop_end) } : {},
                lost ? { level: Math.max(-128, Math.min(127, x.level - 2 * lost)) } : {});
            if (y.loop && y.loop_end <= y.loop_start) y.loop = 0;      /* (a loop left with no length: OFF) */
            return y;
        });
        st = M.replaceItem(st, side, Object.assign({}, o, { recs: [Object.assign({}, o.recs[0], { payload: R.packSampler(s) })] }));
    }
    return st;
}

/* ---- many at once (the TABLE's OPTIMIZE SAMPLES) ---- */
/* the samples picked, and a picked sampler's; none a page cannot read */
function samplesOf(items, ids) {
    const sel = new Set(ids), by = new Map(items.map(i => [i.id, i])), out = [], seen = new Set();
    const add = it => { if (it && it.kind === K.SAMPLE && !it.dim && !seen.has(it.id)) { seen.add(it.id); out.push(it); } };
    for (const it of items) if (sel.has(it.id)) { add(it); if (it.kind === K.SAMPLER && !it.dim) for (const t of Object.values(it.link)) add(by.get(t)); }
    return out;
}
/* each sample at a level, made from its source as its tab's OPTIMIZE makes it; only what comes out smaller is kept (a
 * sample too long that comes out fitting counts as smaller: fits, its size in before and after alike) */
function plan(items, ids, level) {
    const picked = samplesOf(items, ids), results = [];
    let before = 0, after = 0, failed = 0, fits = 0;
    for (const it of picked) {
        const now = M.size(it), long = M.isLong(it);
        let n = null, ctx = null, cut = null;
        try {
            ctx = context(it, items);
            cut = ED.conv.optimize(ctx.snd, ctx.cut0, ctx.used, ctx.players.length > 0)[level];
            n = remake(it, ctx, cut);
        } catch (e) { failed++; }
        if (n && (long ? !M.isLong(n) : M.size(n) < now)) {
            results.push({ id: it.id, item: n, cut, cut0: ctx.cut0, rate: ctx.snd.rate });
            before += long ? M.size(n) : now; after += M.size(n);
            if (long) fits++;
        } else { before += now; after += now; }
    }
    return { level, picked: picked.length, results, before, after, failed, fits };
}
function apply(st, side, p) {
    for (const r of p.results) {
        if (!st[side].some(x => x.id === r.id)) continue;
        st = M.replaceItem(st, side, M.pruned(r.item));
        st = follow(st, side, r.id, r.cut, r.cut0, r.rate);
    }
    return st;
}

ED.optimize = { LEVELS, source, context, playersOf, remake, follow, samplesOf, plan, apply };
})(globalThis.ED = globalThis.ED || {});
