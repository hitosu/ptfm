/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Samples made smaller (specs/editor.md, The tabs: SAMPLE's OPTIMIZE; FILES: OPTIMIZE SAMPLES), shared by the SAMPLE tab
 * and the TABLE's menu: a sample's source (a sound's own audio, or the sample decoded), what its sampler's instruments
 * play of it, OPTIMIZE's levels, and a new cut put in a panel with its sampler following it (the entry; the
 * instruments' START, END and loop where the same sound now is; their LEVEL up by what was lowered).
 *   ED.optimize.source(it, items), context(it, items, src) -> { src, snd, cut0, used, players, owner, entry }
 *   remake(it, ctx, cut) -> the sample made again from its source (an owned one's entry in it.entry till follow)
 *   follow(st, side, id, cut, cut0, rate) -> st: an owned sample's entry into its sampler, the instruments following
 *   samplesOf(items, ids) -> the samples picked, a sampler's with it; LEVELS
 *   plan(items, ids, level) -> { results, picked, before, after, failed }; apply(st, side, plan) -> st (one step) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND;
const LEVELS = ["lossless", "smaller", "smallest"];

/* the source every change starts from: the sound's own audio, or the sample's decoded */
function source(it, items) {
    if (it.sound) return it.sound;
    const e = M.sampleEntry(it, items), d = R.ptrDecode(it.recs[0].payload), full = d.bits === 16 ? 32768 : 128;
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
    const owner = it.owner ? items.find(x => x.id === it.owner) || null : null;
    const players = owner ? R.unpackSampler(owner.recs[0].payload).instruments.filter(s => s && s.sample === it.idx) : [];
    const used = players.length ? {
        first: Math.floor(Math.min(...players.map(s => toSnd(Math.min(s.start, s.loop ? s.loop_start : s.start), cut0, snd.rate)))),
        last: Math.ceil(Math.max(...players.map(s => toSnd(Math.max(s.end, s.loop ? s.loop_end : 0), cut0, snd.rate)))) } : null;
    const entry = M.sampleEntry(it, items) || { name: "SAMPLE", rate: cut0.rate, length: 0, coding: 1 };
    return { src, snd, cut0, used, players, owner, entry };
}
function remake(it, ctx, c) {
    const s = ED.conv.soundToSample(ctx.snd, Object.assign({ name: ctx.entry.name }, c));
    const n = Object.assign({}, it, { recs: [{ sub: 0, ver: it.recs[0].ver, payload: s.audio }],
        entry: M.packedEntry(Object.assign({}, ctx.entry, s.entry, { name: ctx.entry.name })) });
    if (!ctx.src.own) n.sound = Object.assign({}, it.sound || ctx.src, { cut: c });
    return n;
}
/* an owned sample's entry lives in its sampler: written there, and its instruments follow the change: their START,
 * END and loop where the same sound now is (a cut, a rate), their LEVEL up by what OPTIMIZE lowered */
function follow(st, side, id, cut, cut0, rate) {
    const items = st[side], cur = items.find(x => x.id === id), o = cur && cur.owner && items.find(x => x.id === cur.owner);
    if (!o || !cur.entry) return st;
    let b = o.recs[0].payload.slice(), s = R.unpackSampler(b);
    b.set(cur.entry, 11 + s.instruments.length * R.SAMPLER_SLOT + cur.idx * R.SAMPLE_ENTRY);
    const lost = (cut.gain || 0) - (cut0.gain || 0), len = R.unpackSampler(M.samplerBytes("", [], [cur.entry])).samples[0].length;
    const moved = cut.start !== cut0.start || cut.rate !== cut0.rate || cut.end !== cut0.end;
    if (moved || lost) {
        s = R.unpackSampler(b);
        const at = v => Math.max(0, Math.min(len, fromSnd(toSnd(v, cut0, rate), cut, rate)));
        s.instruments = s.instruments.map(x => !x || x.sample !== cur.idx ? x : Object.assign({}, x, moved ? { start: at(x.start), end: at(x.end),
            loop_start: at(x.loop_start), loop_end: at(x.loop_end) } : {}, lost ? { level: Math.max(-128, Math.min(127, x.level - 2 * lost)) } : {}));
        b = R.packSampler(s);
    }
    const c2 = Object.assign({}, cur); delete c2.entry;
    const st2 = M.replaceItem(st, side, Object.assign({}, o, { recs: [Object.assign({}, o.recs[0], { payload: b })] }));
    return M.replaceItem(st2, side, c2);
}

/* ---- many at once (the TABLE's OPTIMIZE SAMPLES) ---- */
/* the samples picked, and a picked sampler's; none a page cannot read */
function samplesOf(items, ids) {
    const sel = new Set(ids), out = [];
    for (const it of items) if (it.kind === K.AUDIO && !it.dim && (sel.has(it.id) || (it.owner && sel.has(it.owner)))) out.push(it);
    return out;
}
/* each sample at a level, made from its source as its tab's OPTIMIZE makes it; only what comes out smaller is kept */
function plan(items, ids, level) {
    const picked = samplesOf(items, ids), results = [];
    let before = 0, after = 0, failed = 0;
    for (const it of picked) {
        const now = M.size(it);
        before += now;
        let n = null, ctx = null, cut = null;
        try {
            ctx = context(it, items);
            cut = ED.conv.optimize(ctx.snd, ctx.cut0, ctx.used, ctx.players.length > 0)[level];
            n = remake(it, ctx, cut);
        } catch (e) { failed++; }
        if (n && M.size(n) < now) { results.push({ id: it.id, item: n, cut, cut0: ctx.cut0, rate: ctx.snd.rate }); after += M.size(n); }
        else after += now;
    }
    return { level, picked: picked.length, results, before, after, failed };
}
function apply(st, side, p) {
    for (const r of p.results) {
        if (!st[side].some(x => x.id === r.id)) continue;
        st = M.replaceItem(st, side, M.pruned(r.item));
        st = follow(st, side, r.id, r.cut, r.cut0, r.rate);
    }
    return st;
}

ED.optimize = { LEVELS, source, context, remake, follow, samplesOf, plan, apply };
})(globalThis.ED = globalThis.ED || {});
