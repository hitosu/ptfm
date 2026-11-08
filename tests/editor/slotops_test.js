/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A cartridge's slots and what names them (editor/js/slotops.js): on the demo disk moved to the FM-1 panel, every
 * cell naming a sampler's slot found (in full and the slot alone), the uses of a set of slots counted, and a swap of
 * two places rewriting exactly those cells (each pattern decoded again: the same notes, the instruments swapped).
 *   node tests/editor/slotops_test.js */
"use strict";
const { ED, S, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model, O = ED.slotops;

const disk = S.parseData(demo());
const T = M.fromRecords(disk.files, { side: "L", src: "DEMO.PTFMDSK" });
const r = M.transfer({ L: T.items, R: [], base: [] }, "L", T.items.filter(i => !i.owner).map(i => i.id), {});
const Rr = r.st.R;
/* the cells of every pattern naming cartridge num, decoded: [pattern id, row, track, slot] */
const cellsOf = (items, num) => {
    const out = [];
    for (const p of items.filter(i => i.kind === K.PATTERN)) {
        const pat = R.unpackPattern(p.recs.map(x => x.payload));
        for (const [key, c] of pat.cells) if (c.ins >= 0 && (c.ins >> 8) === num) out.push(p.id + "|" + key + "|" + (c.ins & 255));
    }
    return out.sort();
};
const cart = Rr.filter(i => i.kind === K.SAMPLER).map(c => ({ c, n: cellsOf(Rr, c.num).length })).sort((a, b) => b.n - a.n)[0].c;
const before = cellsOf(Rr, cart.num), refs = O.refs(Rr, "R", cart);
check(before.length > 0 && refs.length > 0 && refs.length <= before.length,
      `the places naming ${M.H2(cart.num)}'s slots found (${refs.length} bytes; the decoded cells playing it, those naming the last one included: ${before.length})`);
const used = new Set(before.map(s => +s.split("|").pop()));
const [a, b] = [...used].slice(0, 2);
check(O.uses(Rr, "R", cart, [a]) === refs.filter(x => x.slot === a).length && O.uses(Rr, "R", cart, [a]) > 0, `the uses of slot ${M.H2(a)} counted`);
if (b != null) {
    const m = O.remap(Rr, "R", cart, new Map([[a, b], [b, a]]));
    const after = cellsOf(m.items, cart.num), swap = s => { const p = s.split("|"), v = +p.pop(); return p.concat([v === a ? b : v === b ? a : v]).join("|"); };
    check(after.join() === before.map(swap).sort().join() && m.ids.length > 0, `a swap of ${M.H2(a)} and ${M.H2(b)}: every cell naming them swapped (${m.ids.length} items rewritten)`);
    const notes = items => items.filter(i => i.kind === K.PATTERN).map(p => [...R.unpackPattern(p.recs.map(x => x.payload)).cells.values()].map(c => c.note).join()).join("/");
    check(notes(m.items) === notes(Rr), "the notes as they were");
}

/* ---- a sampler's samples (the sampler tab, specs/editor.md The tabs: Cartridges): any sample on the panel by number or
 * name, REPLACE SAMPLE, CREATE, THE SAMPLES IT USES, PASTE bringing an equal sample or a new one ---- */
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const smps = Rr.filter(i => i.kind === K.SAMPLE), entryOf = (it, items) => M.sampleEntry(it, items);
const sampler = Rr.filter(i => i.kind === K.SAMPLER).sort((x, y) => y.recs[0].payload[9] - x.recs[0].payload[9])[0];
const insts = R.unpackSampler(sampler.recs[0].payload).instruments;
const on = O.samplesOn(Rr, []);
check(on.length === smps.length && on.every((x, i) => i === 0 || x.item.num > on[i - 1].item.num), `every sample on the panel is offered, by number (${on.length})`);
const named0 = O.sampleNamed(sampler, Rr, insts[0].sample);
check(named0 && named0.kind === K.SAMPLE && named0.num === insts[0].sample, `an instrument's number names its sample item (${M.H2(insts[0].sample)} ${named0 && entryOf(named0, Rr).name})`);
check(O.sampleNamed(sampler, Rr, 250) === null, "a number no link names: no sample (missing)");
const usedS = O.usedSamples(sampler, Rr);
check(usedS.reduce((n, u) => n + u.slots.length, 0) === insts.filter(Boolean).length && usedS.every(u => u.item), `THE SAMPLES IT USES: each sample once, its slots (${usedS.length} samples, ${insts.filter(Boolean).length} instruments)`);

/* REPLACE SAMPLE: every value a new instrument's, the name following when it was the old sample's, the others untouched */
const other = smps.find(s => s.id !== named0.id);
{
    const c0 = R.unpackSampler(sampler.recs[0].payload);
    const k = c0.instruments.findIndex(Boolean);
    c0.instruments[k] = Object.assign({}, c0.instruments[k], { name: entryOf(named0, Rr).name, level: 20, tune: 100, loop: 1, loop_start: 5, loop_end: 9, base_note: 40, start: 3, volume: 10 });
    const d0 = Object.assign({}, sampler, { recs: [Object.assign({}, sampler.recs[0], { payload: R.packSampler(c0) })] });
    const d1 = O.replaceSample(d0, "R", k, other, Rr), c1 = R.unpackSampler(d1.recs[0].payload), q = c1.instruments[k], e = entryOf(other, Rr);
    check(q.sample === other.num && d1.link[M.lk("m", other.num)] === other.id, "REPLACE SAMPLE: the instrument names the new sample, the link too");
    check(q.name === e.name && q.level === 0 && q.tune === 0x80 && q.loop === 0 && q.start === 0 && q.end === e.length && q.base_note === 60 && q.volume === 64,
        "REPLACE SAMPLE: every value a new instrument's (START 0, END its length, LOOP OFF, BASE NOTE C-4, TUNE none, VOLUME 64), the name following the old sample's");
    check(c1.instruments.every((x, i) => i === k || (x && c0.instruments[i] ? eq(x.raw, c0.instruments[i].raw) : x === c0.instruments[i])), "REPLACE SAMPLE: the other instruments as they were");
    const c2 = R.unpackSampler(sampler.recs[0].payload);
    c2.instruments[k] = Object.assign({}, c2.instruments[k], { name: "MY SOUND" });
    const d2 = Object.assign({}, sampler, { recs: [Object.assign({}, sampler.recs[0], { payload: R.packSampler(c2) })] });
    check(R.unpackSampler(O.replaceSample(d2, "R", k, other, Rr).recs[0].payload).instruments[k].name === "MY SOUND", "REPLACE SAMPLE: a name of its own stays");
}
/* CREATE on an empty slot: a new instrument playing the sample whole, named after it; DELETE of one leaves the sample */
{
    const at = insts.length, ni = O.newInstrument(other, 0), e = entryOf(other, Rr);
    const d1 = O.pointTo(sampler, "R", at, ni, other), c1 = R.unpackSampler(d1.recs[0].payload);
    check(c1.instruments.length === at + 1 && c1.instruments[at].sample === other.num && c1.instruments[at].name === e.name && c1.instruments[at].end === e.length,
        "CREATE: a new instrument at the empty slot plays the sample whole, named after it");
    const gone = Object.assign({}, d1, { recs: [Object.assign({}, d1.recs[0], { payload: R.packSampler(Object.assign({}, c1, { instruments: c1.instruments.map((x, i) => i === at ? null : x) })) })] });
    const st = M.replaceItem({ L: [], R: Rr, base: [] }, "R", gone);
    check(st.R.filter(i => i.kind === K.SAMPLE).length === smps.length && st.R.find(i => i.id === other.id), "DELETE of the instrument: its sample stays");
}
/* samples dropped on a sampler's item: an instrument each at its first empty slots, whole, named after it; 64 at most */
{
    const three = smps.slice(0, 3), r = O.addInstruments(sampler, "R", three), c1 = R.unpackSampler(r.item.recs[0].payload);
    check(r.ok && r.at.join() === [insts.length, insts.length + 1, insts.length + 2].join() && three.every((s, k) => c1.instruments[insts.length + k].sample === s.num
        && c1.instruments[insts.length + k].name === entryOf(s, Rr).name && c1.instruments[insts.length + k].end === entryOf(s, Rr).length),
        "samples dropped on a sampler: an instrument each at its first empty slots, playing the sample whole, named after it");
    const full = Object.assign({}, sampler, { recs: [Object.assign({}, sampler.recs[0], { payload: R.packSampler({ name: "FULL", instruments: Array.from({ length: 63 }, (_, i) => O.newInstrument(smps[0], 0)) }) })] });
    const r2 = O.addInstruments(full, "R", smps.slice(0, 2));
    check(!r2.ok && /64 INSTRUMENTS/.test(r2.msg), "64 at most: " + r2.msg);
}
/* on the TABLE the sampler's numbers are its own (the link names the sample item) */
{
    const Ls = M.transfer({ L: [], R: Rr, base: [] }, "R", [sampler.id], {}).st.L;
    const ls = Ls.find(i => i.kind === K.SAMPLER), lsmp = Ls.filter(i => i.kind === K.SAMPLE);
    const pick = lsmp.find(s => !Object.values(ls.link).includes(s.id)) || lsmp[0];
    const d1 = O.pointTo(ls, "L", 0, O.newInstrument(pick, 0), pick), c1 = R.unpackSampler(d1.recs[0].payload);
    check(O.sampleNamed(d1, Ls, c1.instruments[0].sample) === pick, "on the TABLE: the number is the sampler's own, its link names the sample");
    check(pick.num == null, "on the TABLE a sample has no number");
}
/* the same sample (the whole record) is equal; the same audio under another name is another sample */
{
    const copy = Object.assign({}, other, { id: M.newId() });
    check(O.equalSample(Rr, copy) === other, "an equal sample is found by its whole record");
    const s = R.unpackSample(other.recs[0].payload), renamed = Object.assign({}, other, { id: M.newId(), recs: [{ sub: 0, ver: 1, payload: R.packSample(Object.assign({}, s.entry, { name: "OTHERNAME" }), s.audio) }] });
    check(O.equalSample(Rr, renamed) === null, "the same audio under another name: not equal (two samples)");
}
/* PASTE: an equal sample named, else brought at a free number passing over the numbers references name, none free:
 * refused; not enough room: refused */
{
    const fromTable = Ls => Ls.filter(i => i.kind === K.SAMPLE);
    const tbl = M.transfer({ L: [], R: Rr, base: [] }, "R", [sampler.id], {}).st.L, tsm = fromTable(tbl);
    const r1 = O.bringSamples(Rr, sampler, "R", [tsm[0], tsm[0], null], null);
    check(r1.ok && r1.fresh.length === 0 && r1.bring.get(tsm[0].id) && r1.bring.get(tsm[0].id).side === "R", "PASTE: a sample the panel holds already is named, nothing brought");
    const odd = R.packSample({ length: 8, rate: 8000, coding: 1, name: "ODD ONE" }, R.ptrEncode(Int8Array.from([1, 2, 3, 4, 5, 6, 7, 8]), 8));
    const mk = p => ({ id: M.newId(), side: "L", kind: K.SAMPLE, num: null, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: p }], src: "X" });
    const a = mk(odd), b = mk(R.packSample({ length: 8, rate: 8000, coding: 1, name: "ODD TWO" }, R.ptrEncode(Int8Array.from([8, 7, 6, 5, 4, 3, 2, 1]), 8)));
    const taken = new Set(Rr.filter(i => i.kind === K.SAMPLE).map(i => i.num));
    let low = 0; while (taken.has(low)) low++;
    // a reference to nothing at the lowest free number: passed over
    const ghost = Object.assign({}, sampler, { id: M.newId(), num: Rr.filter(i => i.kind === K.SAMPLER).length + 100 });
    const gc = R.unpackSampler(ghost.recs[0].payload); gc.instruments[0] = Object.assign({}, gc.instruments[0], { sample: low });
    ghost.recs = [Object.assign({}, ghost.recs[0], { payload: R.packSampler(gc) })]; ghost.link = {};
    const r2 = O.bringSamples(Rr.concat([ghost]), sampler, "R", [a, b, a], null);
    check(r2.ok && r2.fresh.length === 2 && r2.fresh[0].num === low + 1 && r2.fresh[1].num === low + 2 && r2.bring.get(a.id) === r2.fresh[0],
        `PASTE: new samples at free numbers (${r2.fresh.map(x => M.H2(x.num))}), the number a reference names (${M.H2(low)}) passed over, one sample once`);
    check(r2.fresh.every(x => x.side === "R" && !x.src && !x.sound), "PASTE: on the FM-1 panel the sample carries no original");
    const rt = O.bringSamples(Rr, sampler, "L", [a], null);
    check(rt.ok && rt.fresh[0].num === null && rt.fresh[0].side === "L", "PASTE on the TABLE: the sample comes with no number");
    const full = Rr.filter(i => i.kind !== K.SAMPLE);
    for (let n = 0; n < 256; n++) full.push({ id: M.newId(), side: "R", kind: K.SAMPLE, num: n, orig: n, link: {}, recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 1, rate: 8000, coding: 1, name: "S" + n }, R.ptrEncode(Int8Array.from([n & 127]), 8)) }] });
    const r3 = O.bringSamples(full, sampler, "R", [a], null);
    check(!r3.ok && /^NO FREE SAMPLE NUMBER/.test(r3.msg), "PASTE with all 256 numbers taken: NO FREE SAMPLE NUMBER, nothing pasted: " + r3.msg);
    const r4 = O.bringSamples(Rr, sampler, "R", [a], { regions: [{ start: 0x93000, end: 0x94000, area: "user", inPlace: true }], reserve: 0 });
    check(!r4.ok && /^DOESN'T FIT/.test(r4.msg), "PASTE that does not fit the FM-1: refused: " + r4.msg);
    // an instrument whose sample is gone: its number never names an unrelated sample
    const n0 = smps[0].num, nm = O.numberForMissing(Rr, sampler, "R", n0);
    check(nm !== n0 && !taken.has(nm), `PASTE of an instrument whose sample is gone: ${M.H2(n0)} names a sample here, so it takes ${M.H2(nm)}, which names nothing`);
    check(O.numberForMissing(Rr, sampler, "R", 250) === 250, "a number no sample holds is kept");
    /* two PASTEs in one draft (nothing saved between): the first leaves an orphan naming `low` (its sample gone, the number
     * free then); the second, bringing a sample, must not take `low` (after SAVE the orphan would play it), and a number
     * the first brought is not given again */
    {
        const orphan = O.numberForMissing(Rr, sampler, "R", low);
        check(orphan === low, "the first PASTE's instrument whose sample is gone keeps its free number " + M.H2(low));
        const c = R.unpackSampler(sampler.recs[0].payload), at = c.instruments.length;
        c.instruments[at] = Object.assign({}, O.newInstrument(smps[0], 0), { sample: low });
        const draft1 = Object.assign({}, sampler, { recs: [Object.assign({}, sampler.recs[0], { payload: R.packSampler(c) })] });
        const p2 = O.bringSamples(Rr, draft1, "R", [a], null);
        check(p2.ok && p2.fresh[0].num !== low && p2.fresh[0].num === low + 1, `the second PASTE (the draft's orphan names ${M.H2(low)}) brings its sample at ${M.H2(p2.fresh[0].num)}, not ${M.H2(low)}`);
        const draft2 = Object.assign({}, draft1, { pending: [{ from: a.id, item: p2.fresh[0] }] });
        const p3 = O.bringSamples(Rr, draft2, "R", [b], null);
        check(p3.ok && p3.fresh[0].num !== low && p3.fresh[0].num !== p2.fresh[0].num, `a later PASTE does not reuse the number the first brought (${M.H2(p3.fresh[0].num)})`);
        const clash = O.numberForMissing(Rr, draft2, "R", p2.fresh[0].num);
        check(clash !== p2.fresh[0].num && clash !== low, "an instrument of a missing sample never takes a number a pending sample holds");
        /* UNDO / CLOSE drop the draft: the saved item keeps no pending sample (`pruned`), the panel none */
        const kept = M.pruned(draft2);
        check(!kept.pending && !Rr.some(i => i.id === p2.fresh[0].id), "a draft's brought sample is not in the panel (UNDO and CLOSE drop it with the draft; SAVE's pruned strips it)");
    }
}
/* the sample's tab: the instruments of every sampler naming it follow a change on SAVE (START / END where the same sound now is) */
{
    const O2 = ED.optimize, victim = smps.find(s => O2.playersOf(s, Rr).length > 0);
    const e = entryOf(victim, Rr), len = e.length;
    // a second sampler naming the same sample, its instrument playing it whole
    const second = { id: M.newId(), side: "R", kind: K.SAMPLER, num: M.pickNum(M.taken(Rr).c, null), orig: null, link: {}, src: "X",
        recs: [{ sub: 0, ver: 2, payload: R.packSampler({ name: "TWO", instruments: [O.newInstrument(victim, 0)] }) }] };
    const sec = O.pointTo(second, "R", 0, O.newInstrument(victim, 0), victim);
    const st0 = { L: [], R: Rr.concat([sec]), base: [] };
    const players = O2.playersOf(victim, st0.R);
    check(new Set(players.map(p => p.item.id)).size >= 2, `PLAYED BY: the instruments of every sampler naming it (${new Set(players.map(p => p.item.id)).size} samplers, ${players.length} instruments)`);
    const cut0 = { start: 0, end: len, chan: "mix", rate: e.rate, bits: 8 }, cut = { start: 0, end: len >> 1, chan: "mix", rate: e.rate, bits: 8 };
    // the sample made shorter (the tab's SAVE puts it in the panel, then follow)
    const half = R.unpackSample(victim.recs[0].payload);
    const shorter = Object.assign({}, victim, { recs: [{ sub: 0, ver: 1, payload: R.packSample(Object.assign({}, half.entry, { length: len >> 1 }), half.audio) }] });
    const st1 = O2.follow(M.replaceItem(st0, "R", shorter), "R", victim.id, cut, cut0, e.rate);
    const ends = O2.playersOf(shorter, st1.R).map(p => p.inst.end);
    check(ends.length === players.length && ends.every(x => x <= (len >> 1)) && ends.some(x => x === (len >> 1)), `a cut on SAVE: every sampler's instruments END follow (${ends.slice(0, 4)} of ${len >> 1})`);
}
done("slotops");
