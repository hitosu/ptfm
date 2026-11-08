/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* New samples and kits put on a panel (specs/editor.md, Recording and cutting sounds): SAVE AS A NEW SAMPLE, SAVE AS
 * SAMPLES and a take's SAVE make samples of their own beside the one cut (items of the panel: on the TABLE with no
 * number, on the FM-1 panel at free numbers), named by the naming rule; MAKE A KIT a sampler cartridge naming the
 * slices' samples and a FUSION cartridge with its pattern. The FM-1 panel's room is checked as a move's is.
 *   ED.cutting.sampleOf(snd, how, name, src, longOk) -> a sample item (its sound its original; too long: TOO LONG
 *     with longOk, else it throws)
 *   addSamples(st, side, list, src) -> { ok, st, ids, msg }   (list: [{ snd, how, name }]; on the TABLE a
 *     sample too long is kept TOO LONG)
 *   makeKit(st, side, from, list, base, src, opts) -> { ok, st, ids, msg } */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, S = ED.slices, H2 = M.H2, MAX = 64;   /* (a kit's slices) */

const namesOn = items => items.filter(x => x.kind === K.SAMPLE).map(x => (M.sampleEntry(x, items) || {}).name || "").filter(Boolean);
const tooLong = i => "SLICE " + H2(i) + " IS TOO LONG";

/* a sound as PT/FM's samples are, one channel: a stereo one's channels mixed (chan "l" / "r": that one alone, for an
 * original kept stereo from before), at once, so what lies on the TABLE goes to the FM-1 as it is */
function monoOf(snd, chan) {
    if (snd.ch.length < 2) return snd;
    const ch = chan === "l" ? snd.ch[0] : chan === "r" ? snd.ch[1] : S.mono(snd.ch);
    return Object.assign({}, snd, { ch: [ch], channels: 1 });
}
/* a sample from a sound at its own rate, mixed to one channel first, at the RATE and DEPTH asked; longOk: a loose one
 * on the TABLE, which may be longer than a sample can be (TOO LONG: its original, no record) */
function sampleOf(snd, how, name, src, longOk) {
    snd = monoOf(snd, how.chan);
    const len = snd.ch[0].length, cut = { start: 0, end: len, chan: "mix", rate: how.rate, bits: how.bits };
    const it = { id: M.newId(), kind: K.SAMPLE, num: null, orig: null, link: {}, src,
        sound: { rate: snd.rate, ch: snd.ch, bits: snd.bits, cut } };
    let s;
    try { s = ED.conv.soundToSample({ rate: snd.rate, channels: snd.ch.length, length: len, ch: snd.ch, bits: snd.bits }, Object.assign({ name }, cut)); }
    catch (e) {
        if (!e.tooLong || !longOk) throw e;
        const nm = R.cleanName(name, 10) || "SAMPLE";
        return M.asLong(Object.assign(it, { name: nm }), { name: nm, rate: how.rate, coding: how.bits === 16 ? 2 : 1 }, e.tooLong);
    }
    return Object.assign(it, { recs: [{ sub: 0, ver: R.VERSION[K.SAMPLE], payload: R.packSample(s.entry, s.audio) }], name: s.entry.name });
}
/* each one made and named, in order; null and why when one cannot be */
function makeAll(items, list, src, longOk) {
    const taken = namesOn(items), out = [];
    for (let i = 0; i < list.length; i++) {
        const z = list[i], name = S.nameFor(z.name, taken);
        let it;
        try { it = sampleOf(z.snd, z.how, name, src, longOk); }
        catch (e) { return { msg: list.length > 1 ? tooLong(i + 1) : "TOO LONG FOR A SAMPLE: KEEP A SHORTER PART, OR A LOWER RATE" }; }
        taken.push(name);
        out.push(it);
    }
    return { made: out };
}
const fitsOn = (st, side, next) => side !== "R" || M.space(next, st.fm1 && st.fm1.layout).fits;
const noRoom = "NO ROOM ON THE FM-1: DELETE SOMETHING THERE FIRST, OR WORK ON THE TABLE";

/* the new samples are items of their own: on the TABLE with no number, on the FM-1 panel at free numbers, the lowest
 * first, passing over the numbers references name */
function addSamples(st, side, list, src) {
    const items = st[side];
    const m = makeAll(items, list, src, side === "L");
    if (!m.made) return { ok: false, msg: m.msg };
    for (const it of m.made) it.side = side;
    if (side === "R") {
        const busy = new Set([...M.taken(items).m, ...M.namedNothing(items)]);
        /* (none saved when not all fit: the words say how many numbers it needs; with none free at all, the spec's) */
        const free = 256 - busy.size;
        if (!free) return { ok: false, msg: "ALL 256 SAMPLE NUMBERS USED" };
        if (m.made.length > free) return { ok: false, msg: "NOT ENOUGH SAMPLE NUMBERS: " + m.made.length + " NEEDED, " + free + " FREE" };
        for (const it of m.made) {
            const n = M.pickNum(busy, null, 256);
            if (n < 0) return { ok: false, msg: "ALL 256 SAMPLE NUMBERS USED" };
            busy.add(n);
            it.num = n;
        }
    }
    const next = items.concat(m.made);
    if (!fitsOn(st, side, next)) return { ok: false, msg: noRoom };
    return { ok: true, st: Object.assign({}, st, { [side]: next }), ids: m.made.map(x => x.id) };
}

/* MAKE A KIT: a sampler named after the sample (slice n its sample and instrument n - 1, played whole at C-4), a
 * FUSION cartridge (the name cut to 8, then " KIT") whose slot 00 is the kit, its pattern a key a slice */
function makeKit(st, side, from, list, base, src) {
    if (list.length > MAX) return { ok: false, msg: "64 SLICES AT MOST IN A KIT" };
    if (!list.length) return { ok: false, msg: "NO SLICE TO MAKE A KIT OF" };
    const items = st[side], m = makeAll(items, list, src);
    if (!m.made) return { ok: false, msg: m.msg };
    const name = String(base || "SAMPLE").toUpperCase().slice(0, 10).trim() || "SAMPLE";
    /* (slice n is sample n and instrument n - 1, in the kit's own frame: a move numbers them on the FM-1 panel) */
    const insts = m.made.map((it, i) => ED.slotops.newInstrument(it, i));
    const id = M.newId, sam = { id: id(), side: "L", kind: K.SAMPLER, num: null, orig: null, link: {}, src, name,
        recs: [{ sub: 0, ver: R.VERSION[K.SAMPLER], payload: R.packSampler({ name, instruments: insts }) }] };
    m.made.forEach((it, i) => { Object.assign(it, { side: "L", orig: i }); sam.link[M.lk("m", i)] = it.id; });
    const kitName = name.slice(0, 8).trim() + " KIT";
    const pat = { id: id(), side: "L", kind: K.PATTERN, num: null, orig: null, link: { [M.lk("c", 0)]: sam.id }, src, name: kitName,
        recs: R.packPattern(S.kitPattern(m.made.length, 0)).map((payload, sub) => ({ sub, ver: R.VERSION[K.PATTERN], payload })) };
    const fus = { id: id(), side: "L", kind: K.FUSION, num: null, orig: null, link: { [M.lk("p", 0)]: pat.id }, src, name: kitName,
        recs: [{ sub: 0, ver: R.VERSION[K.FUSION], payload: R.packCart(K.FUSION, { name: kitName,
            slots: [Object.assign(ED.defs.newSlot(K.FUSION, name), { name, level: 0, pattern: 0 })] }) }] };
    const made = [sam].concat(m.made, [pat, fus]);
    if (side === "L") return { ok: true, st: Object.assign({}, st, { L: st.L.concat(made) }), ids: [sam.id, fus.id] };
    /* the FM-1 panel: placed as a move from the TABLE places them (numbers, room), the TABLE left as it was */
    const r = M.transfer(Object.assign({}, st, { L: made }), "L", [fus.id, sam.id], { layout: st.fm1 && st.fm1.layout });
    if (!r.ok) return { ok: false, msg: r.msg };
    return { ok: true, st: Object.assign({}, r.st, { L: st.L }), ids: r.ids || [] };
}

ED.cutting = { monoOf, sampleOf, addSamples, makeKit };
})(globalThis.ED = globalThis.ED || {});
