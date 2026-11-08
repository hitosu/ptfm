/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A cartridge's slots and what names them on a panel (specs/editor.md, The tabs: Many instruments at once): every
 * place a slot of the cartridge is named, a cell's (in full, or the slot alone under its track's last cartridge), a
 * loop's, a favourite's and the settings' current instrument; how many name a set of slots (the questions' "used"),
 * and every one of them moved by a map of places (a rearranging's SAVE). No DOM: Node runs it.
 *   ED.slotops.refs(items, side, cart) -> [{ id, r, at, slot }]   (cart: the cartridge's item; at: its slot byte)
 *   uses(items, side, cart, slots) -> how many name those slots
 *   remap(items, side, cart, map) -> { items, ids }   (map: Map old slot -> new slot; ids: the items rewritten) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND;

/* does cartridge number n, as item `it` names it, mean `cart`? (the FM-1 panel: by number; the TABLE: by its link) */
function names(it, side, n, cart) {
    if (side === "R") return n === cart.num;
    return it.link && it.link[M.lk("c", n)] === cart.id;
}
/* a pattern part's cells: each one naming an instrument, its cartridge number and where its slot byte is */
function cellSlots(b, out, r) {
    const mask = b[3] | b[4] << 8;
    let at = 5;
    for (let t = 0; t < 16; t++) {
        if (!(mask >> t & 1)) continue;
        let last = null;                                /* (the track's last cartridge, for a cell naming the slot alone) */
        for (;;) {
            if (at >= b.length) return;
            const head = b[at++];
            if (head === 0) break;
            if (head & 0x80) at++;
            const mode = (head >> 5) & 3;
            if (mode === 2) { if (last != null) out.push({ r, n: last, at }); at++; }
            else if (mode === 3) { last = b[at]; out.push({ r, n: b[at], at: at + 1 }); at += 2; }
            if (head & 0x10) at++;
            if (head & 0x08) {
                let n = 1;
                if (b[at] === 0xFF) { n = b[at + 1]; at += 2; }
                at += 2 * n;
            }
            if ((head & 7) === 7) at++;
        }
    }
}
function refs(items, side, cart) {
    const out = [];
    for (const it of items) {
        if (it.dim || it.id === cart.id) continue;
        const add = (r, n, at) => { const b = it.recs[r].payload; if (at < b.length && names(it, side, n, cart)) out.push({ id: it.id, r, at, slot: b[at] }); };
        if (it.kind === K.PATTERN) it.recs.forEach((rec, r) => { const c = []; cellSlots(rec.payload, c, r); for (const x of c) add(x.r, x.n, x.at); });
        else if (it.kind === K.LOOPS) { const b = it.recs[0].payload; for (let i = 0; i < b[5]; i++) add(0, b[80 + 12 * i], 81 + 12 * i); }
        else if (it.kind === K.FAVORITES) { const b = it.recs[0].payload; for (let i = 0; i < b[9]; i++) if (b[11 + 3 * i] !== 0xFF) add(0, b[10 + 3 * i], 11 + 3 * i); }
        else if (it.kind === K.SETTINGS) { const b = it.recs[0].payload; if (b.length > 123 && (b[123] & 1)) add(0, b[121], 122); }
    }
    return out;
}
function uses(items, side, cart, slots) {
    const s = slots instanceof Set ? slots : new Set(slots);
    return refs(items, side, cart).filter(x => s.has(x.slot)).length;
}
function remap(items, side, cart, map) {
    const byItem = new Map();
    for (const x of refs(items, side, cart)) {
        if (!map.has(x.slot) || map.get(x.slot) === x.slot) continue;
        if (!byItem.has(x.id)) byItem.set(x.id, []);
        byItem.get(x.id).push(x);
    }
    const ids = [...byItem.keys()];
    const out = items.map(it => {
        const xs = byItem.get(it.id);
        if (!xs) return it;
        const recs = it.recs.map(r => Object.assign({}, r, { payload: r.payload.slice() }));
        for (const x of xs) recs[x.r].payload[x.at] = map.get(x.slot);
        return Object.assign({}, it, { recs });
    });
    return { items: out, ids };
}

/* ---- a sampler's samples (specs/editor.md, The tabs: Cartridges): an instrument names its sample by number; the draft's
 * link says which item that number is (on the FM-1 panel the item's own number, on the TABLE a number of the sampler's
 * own); the samples a paste brings are `pending` in the draft, items added at SAVE.
 *   samplesOn(items, pending) -> the samples an instrument can name there [{ item }], by number on the FM-1 panel, by name
 *   sampleNamed(draft, items, num) -> the sample item the draft's number names, or null (missing)
 *   newInstrument(sample, num) -> an instrument playing the sample whole, named after it
 *   pointTo(draft, side, at, inst, sample) -> the draft with the instrument at `at` (a slot index) set and the link naming
 *     the sample
 *   replaceSample(draft, side, at, sample, items) -> the same, as a new instrument's values with the old name followed
 *   addInstruments(item, side, samples) -> { ok, item, msg }: samples dropped on a sampler get an instrument each
 *   usedSamples(draft, items) -> [{ num, item (null: missing), slots: [at] }] in the order they are first named
 *   equalSample(items, src) -> the sample of `items` equal to src (its whole record), or null
 *   bringSamples(items, draft, side, srcs, layout) -> { ok, bring, fresh, msg }, numberForMissing(items, draft, side, num):
 *     see PASTE below (items: the saved panel; draft: the tab's, its pending samples and its own references counted) */
const isSample = i => i.kind === K.SAMPLE && !i.dim && i.recs.length > 0;
function samplesOn(items, pending) {
    const out = items.filter(isSample).concat((pending || []).map(p => p.item).filter(isSample));
    const name = i => (M.sampleEntry(i, items) || {}).name || "";
    return out.sort((a, b) => a.num != null && b.num != null ? a.num - b.num : name(a) < name(b) ? -1 : name(a) > name(b) ? 1 : 0).map(item => ({ item }));
}
function sampleNamed(draft, items, num) {
    const id = draft.link[M.lk("m", num)];
    if (!id) return null;
    return items.find(i => i.id === id) || (draft.pending || []).map(p => p.item).find(i => i.id === id) || null;
}
function newInstrument(sample, num) {
    const e = M.sampleEntry(sample, []) || { name: "SAMPLE", length: 0 };
    return Object.assign(ED.defs.newSlot(K.SAMPLER, e.name), { sample: num, start: 0, end: e.length, loop: 0, loop_start: 0, loop_end: e.length });
}
/* the number the sampler's instruments give a sample: the item's own on the FM-1 panel, else one of the sampler's own */
function numberIn(draft, side, sample) {
    return side === "R" ? sample.num : M.numberFor(draft, "m", sample.id, "L", null, sample.orig);
}
function pointTo(draft, side, at, inst, sample) {
    const c = R.unpackSampler(draft.recs[0].payload);
    while (c.instruments.length <= at) c.instruments.push(null);
    const num = numberIn(draft, side, sample);
    c.instruments[at] = Object.assign({}, inst, { sample: num });
    return Object.assign({}, draft, { recs: [Object.assign({}, draft.recs[0], { payload: R.packSampler(c) })].concat(draft.recs.slice(1)),
        link: Object.assign({}, draft.link, { [M.lk("m", num)]: sample.id }) });
}
function replaceSample(draft, side, at, sample, items) {
    const c = R.unpackSampler(draft.recs[0].payload), old = c.instruments[at];
    const was = old && sampleNamed(draft, items, old.sample), wasName = was && (M.sampleEntry(was, items) || {}).name;
    const fresh = newInstrument(sample, 0);
    if (old && !(wasName != null && old.name === wasName)) fresh.name = old.name;    /* (its name follows the sample only when it was the old sample's) */
    return pointTo(draft, side, at, fresh, sample);
}
/* samples dropped on a sampler's item (specs/editor.md, Moving items): an instrument each at its first empty slots, each
 * playing its sample whole, named after it; 64 at most -> { ok, item, msg } */
function addInstruments(item, side, samples) {
    const c = R.unpackSampler(item.recs[0].payload);
    const free = [];
    for (let i = 0; i < 64; i++) if (!c.instruments[i]) free.push(i);
    if (samples.length > free.length) return { ok: false, msg: "A SAMPLER HOLDS 64 INSTRUMENTS: " + free.length + " FREE FOR " + samples.length + " SAMPLES. NOTHING MOVED" };
    let d = item;
    samples.forEach((s, k) => { d = pointTo(d, side, free[k], newInstrument(s, 0), s); });
    return { ok: true, item: d, at: free.slice(0, samples.length) };
}
function usedSamples(draft, items) {
    const c = R.unpackSampler(draft.recs[0].payload), out = [], by = new Map();
    c.instruments.forEach((s, at) => {
        if (!s) return;
        if (!by.has(s.sample)) { const o = { num: s.sample, item: sampleNamed(draft, items, s.sample), slots: [] }; by.set(s.sample, o); out.push(o); }
        by.get(s.sample).slots.push(at);
    });
    return out;
}
function equalSample(items, src) {
    const mine = M.hashes([src]).get(src.id), cand = items.filter(i => isSample(i) && i.recs[0].payload.length === src.recs[0].payload.length);
    if (!cand.length) return null;
    const h = M.hashes(cand);
    return cand.find(i => h.get(i.id) === mine) || null;
}

/* The samples a PASTE of sample instruments needs (specs/editor.md, Many instruments at once): the same sample already on
 * the panel (its whole record equal) is named; else it comes along as an item of its own, on the FM-1 panel at a free
 * number (passing over the numbers references name), on the TABLE with none. srcs: the sample items the copied
 * instruments named (null: gone from where it was copied). Refused, nothing brought, when no number is free or the room
 * is not there. -> { ok, bring: Map (a source's id -> the item the pasted instruments name), fresh: [items], msg } */
/* the panel as the tab's draft would leave it: the draft in its item's place (the numbers its instruments name count,
 * those of an earlier paste's orphan too), the samples a paste brought added */
function panelWith(items, draft) {
    const out = items.some(i => i.id === draft.id) ? items.map(i => i.id === draft.id ? draft : i) : items.concat([draft]);
    const have = new Set(out.map(i => i.id));
    return out.concat((draft.pending || []).map(p => p.item).filter(i => !have.has(i.id)));
}
function bringSamples(items, draft, side, srcs, layout) {
    const all = panelWith(items, draft), bring = new Map(), fresh = [];
    const busy = new Set([...M.taken(all).m, ...M.namedNothing(all)]);
    for (const src of srcs) {
        if (!src || bring.has(src.id)) continue;
        const same = equalSample(all.concat(fresh), src);
        if (same) { bring.set(src.id, same); continue; }
        let num = null;
        if (side === "R") {
            num = M.pickNum(busy, null, 256);
            if (num < 0) return { ok: false, msg: "NO FREE SAMPLE NUMBER: DELETE A SAMPLE FIRST. NOTHING PASTED" };
            busy.add(num);
        }
        const it = Object.assign({}, src, { id: M.newId(), side, num, link: {} });
        if (side === "R") { delete it.src; delete it.name; delete it.sound; delete it.entry; delete it.long; }   /* (no original on the FM-1 panel) */
        bring.set(src.id, it); fresh.push(it);
    }
    if (fresh.length && side === "R") {
        const sp = M.space(all.concat(fresh), layout);
        if (!sp.fits) return { ok: false, msg: "DOESN'T FIT: THE SAMPLES NEED " + sp.missing * 4 + " KB MORE ON THE FM-1. NOTHING PASTED." };
    }
    return { ok: true, bring, fresh };
}
/* the number a pasted instrument keeps when its sample is gone from where it was copied: its own, unless that number
 * names a sample here (a pasted instrument must never play an unrelated one), then one that names nothing */
function numberForMissing(items, draft, side, num) {
    const all = panelWith(items, draft), link = draft.link;
    const clash = side === "R" ? M.taken(all).m.has(num) : !!link[M.lk("m", num)];
    if (!clash) return num;
    const held = side === "R" ? new Set([...M.taken(all).m, ...M.namedNothing(all)])
        : new Set(Object.keys(link).filter(x => x[0] === "m").map(x => parseInt(x.slice(1), 16)));
    const n = M.pickNum(held, null, 256);
    return n >= 0 ? n : num;
}

ED.slotops = { refs, uses, remap, samplesOn, sampleNamed, newInstrument, pointTo, replaceSample, addInstruments, usedSamples, equalSample,
    bringSamples, numberForMissing };
})(globalThis.ED = globalThis.ED || {});
