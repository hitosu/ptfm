/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The two panels' rules (specs/editor.md: Items, references, hashes; Moving items; DELETE and DELETE ALL; UNDO), on
 * the demo disk: items from records, moves with what they use, reuse by hash, numbers kept and given, every reference
 * rewritten, loose samples into a sampler, the system items replaced, changed built-ins, refusal when it does not fit,
 * DELETE refused and ALSO WHAT ONLY IT USES, DELETE ALL, the space as the planner places it, what WRITE would change,
 * UNDO.
 *
 *   node tests/editor/model_test.js [WASM]       (the built-in instruments from the editor's WebAssembly) */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model;
const H2 = M.H2;
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const key = r => r.kind + "." + r.num + "." + r.sub;

(async () => {
    const wasmFile = process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm");
    const fw = await new ED.Firmware(await WebAssembly.compile(fs.readFileSync(wasmFile))).start(null);
    const bi = fw.builtins();
    M.setBuiltins(bi);
    check(bi.length > 5 && bi.every(b => b.num >= 0xF0), "the firmware's built-in instruments: " + bi.map(b => H2(b.num)).join(" "));

    /* ---- items from a disk ---- */
    const disk = S.parseData(demo());
    const T = M.fromRecords(disk.files, { side: "L", src: "DEMO.PTFMDSK" });
    const L0 = T.items;
    const of = (items, kind) => items.filter(i => i.kind === kind);
    check(of(L0, K.SONG).length === 18, "18 songs on the TABLE: " + of(L0, K.SONG).length);
    check(L0.every(i => i.num === null && i.side === "L"), "no numbers on the TABLE");
    check(of(L0, K.PATTERN).every(p => /^DEMO P[0-9A-F]{2}$/.test(p.name)), "patterns named from their source: " + of(L0, K.PATTERN)[0].name);
    const samples = of(L0, K.AUDIO), samplers = of(L0, K.SAMPLER);
    check(samples.length > 30 && samples.every(s => samplers.some(c => c.id === s.owner)), samples.length + " samples, each in its sampler");
    check(T.skipped.length === 0, "nothing left out of the demo disk: " + T.skipped.map(r => r.label).join(", "));
    const song0 = of(L0, K.SONG).find(s => M.nameOf(s) === "PUNCH IN") || of(L0, K.SONG)[0];
    const pos = M.refs(song0).filter(f => f.sp === "p");
    check(pos.length > 0 && pos.every(f => L0.some(i => i.id === song0.link[M.lk("p", f.num)])), "the song's positions link to its patterns");
    const usesB = L0.some(i => Object.values(i.link).includes(M.BUILTIN));
    check(usesB, "the disk's patterns use built-in instruments (linked as the firmware's)");
    check(L0.every(i => Object.values(i.link).every(t => t !== null)), "no reference to nothing on the demo disk");

    /* ---- hashes: equal content hashes equal, whatever its numbers ---- */
    const hL = M.hashes(L0);
    check(new Set(L0.map(i => hL.get(i.id))).size >= L0.length - 2, "hashes tell the items apart");

    /* ---- the whole disk to an empty FM-1 panel: numbers kept, records byte for byte ---- */
    let st = { L: L0, R: [], base: [] };
    let r = M.transfer(st, "L", L0.filter(i => !i.owner).map(i => i.id), {});
    check(r.ok, "the whole disk moves: " + r.msg);
    st = r.st;
    const recs = M.toRecords(st.R);
    const dk = new Map(disk.files.map(f => [key(f), f]));
    check(recs.length === disk.files.length && recs.every(x => dk.has(key(x)) && eq(dk.get(key(x)).payload, x.payload)),
        "the FM-1 panel holds the disk's records exactly (numbers kept): " + recs.length + " of " + disk.files.length);
    const sp = M.space(st.R);
    const ses = S.session(fw.transport()), lay = await ses.hello();
    ses.close();
    const P = S.plan(lay, disk);
    check(sp.blocks === 103 && sp.used === P.space.need && sp.free === P.space.after && sp.fits,
        "the demo disk takes " + sp.used + " of 103 blocks, as the uploader plans it on an empty FM-1 (" + P.space.need + "): " + JSON.stringify(sp));
    const spL = M.space(st.R, lay);
    check(spL.used === sp.used && spL.reserve === lay.reserve, "the same counted on the FM-1's own layout");
    const hR = M.hashes(st.R);
    const rSong = st.R.find(i => i.kind === K.SONG && M.nameOf(i) === M.nameOf(song0));
    check(hR.get(rSong.id) === hL.get(song0.id), "a song's hash is the same on both panels");

    /* moved again: everything is there already */
    r = M.transfer(st, "L", [song0.id], {});
    check(r.ok && r.st.R.length === st.R.length && /ALREADY THERE/.test(r.msg) && /NOTHING NEW/.test(r.msg), "moved again: reused, " + r.msg);

    /* ---- numbers taken: the song's pattern goes elsewhere, its positions rewritten ---- */
    const p0 = st.L.find(i => i.id === song0.link[M.lk("p", pos[0].num)]);
    const emptyR = { L: st.L, R: [], base: [] };
    /* a pattern of another source at the same number first */
    const other = Object.assign({}, of(L0, K.PATTERN).find(p => p.orig !== p0.orig), { id: M.newId(), orig: p0.orig });
    other.recs = other.recs.map(x => Object.assign({}, x, { payload: x.payload.slice() }));
    other.recs[0].payload[0] = Math.max(1, other.recs[0].payload[0]);
    const otherCell = other.recs[0].payload;
    otherCell[2] ^= 1;                              /* (its EDIT STEP changed: another pattern by content) */
    let s2 = { L: st.L.concat([other]), R: [], base: [] };
    r = M.transfer(s2, "L", [other.id], {});
    check(r.ok && r.st.R[0].num === p0.orig, "the other pattern takes number " + H2(p0.orig));
    r = M.transfer(r.st, "L", [song0.id], {});
    check(r.ok && /IS NOW/.test(r.msg), "the song's pattern takes another number: " + r.msg);
    const rs = r.st.R.find(i => i.kind === K.SONG);
    const rp = r.st.R.find(i => i.kind === K.PATTERN && M.hashes(r.st.R).get(i.id) === M.hashes(st.L).get(p0.id));
    check(rp && rp.num !== p0.orig && rs.recs[0].payload[21 + pos[0].at - 21] === rp.num, "its positions name the new number " + (rp && H2(rp.num)));
    check(rs.link[M.lk("p", rp.num)] === rp.id, "and link to it");
    /* the patterns' cells name the cartridges' numbers on the FM-1 panel */
    for (const it of r.st.R) for (const f of M.refs(it)) {
        const t = it.link[M.lk(f.sp, f.num)];
        if (t && t !== M.BUILTIN) { const ti = r.st.R.find(x => x.id === t); if (!ti || ti.num !== f.num) { check(false, M.label(it) + " names " + H2(f.num) + " for " + (ti && M.label(ti))); break; } }
    }
    check(true, "every reference on the FM-1 panel names its target's number");

    /* ---- the FM-1 panel back to the TABLE: numbers remembered, source FM-1 ---- */
    const back = M.transfer({ L: [], R: st.R, base: [] }, "R", [rSong.id], { stamp: "FM-1 26 OCT 03:40" });
    check(back.ok && back.st.L.every(i => i.num === null && i.orig != null && i.src === "FM-1 26 OCT 03:40"), "back on the TABLE: no numbers, each remembers its own and its source");
    check(back.st.L.some(i => i.kind === K.PATTERN && /· POS/.test(i.name)), "patterns named by where they played: " + back.st.L.find(i => i.kind === K.PATTERN).name);
    const hb = M.hashes(back.st.L);
    check(hb.get(back.ids[0]) === hR.get(rSong.id), "the same hash after the trip");

    /* ---- loose samples: a new sampler, or into the one dropped on ---- */
    const audio = R.ptrEncode(Array.from({ length: 2000 }, (_, i) => Math.round(80 * Math.sin(i / 7))), 8);
    const entry = M.packedEntry({ length: 2000, rate: 22050, coding: 1, name: "SINE" });
    const loose = { id: M.newId(), side: "L", kind: K.AUDIO, num: null, orig: null, link: {}, owner: null, entry,
        recs: [{ sub: 0, ver: 1, payload: audio }], name: "SINE", src: "SINE.WAV" };
    let s3 = { L: [loose], R: [], base: [] };
    r = M.transfer(s3, "L", [loose.id], {});
    const ns = r.st && r.st.R.find(i => i.kind === K.SAMPLER);
    check(r.ok && ns && /NEW SAMPLER/.test(r.msg), "a loose sample makes a new sampler: " + r.msg);
    check(ns && R.unpackSampler(ns.recs[0].payload).samples[0].name === "SINE" && R.unpackSampler(ns.recs[0].payload).instruments.length === 0,
        "the sample in it, no instrument made");
    const smp = r.st.R.find(i => i.kind === K.AUDIO);
    check(smp && smp.owner === ns.id && smp.idx === 0 && M.toRecords(r.st.R).some(x => x.kind === K.AUDIO && x.num === ns.num && x.sub === 0), "its audio filed under the sampler's number");
    r = M.transfer(r.st, "L", [loose.id], {});
    check(r.ok && /ALREADY THERE/.test(r.msg) === false, "moved again with no target: another new sampler (" + r.msg + ")");
    /* onto a sampler of the demo disk */
    const host = st.R.find(i => i.kind === K.SAMPLER);
    const hostN = R.unpackSampler(host.recs[0].payload).samples.length;
    r = M.transfer({ L: [loose], R: st.R, base: [] }, "L", [loose.id], { target: host.id });
    const h2 = r.st && r.st.R.find(i => i.id === host.id);
    check(r.ok && R.unpackSampler(h2.recs[0].payload).samples.length === hostN + 1, "dropped on " + M.label(host) + ": it joins it (" + r.msg + ")");
    check(r.st.R.some(i => i.kind === K.AUDIO && i.owner === host.id && i.idx === hostN), "at its next free place, " + H2(hostN));
    const r2 = M.transfer(r.st, "L", [loose.id], { target: host.id });
    check(r2.ok && r2.st.R.length === r.st.R.length && /ALREADY THERE/.test(r2.msg), "the same sample again: already there");

    /* ---- the system items: one of each on the FM-1, replaced ---- */
    const set1 = new Uint8Array(133); set1[0] = 1;
    const set2 = set1.slice(); set2[0] = 2;
    const mk = (b, src) => ({ id: M.newId(), side: "L", kind: K.SETTINGS, num: null, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: b }], src });
    const a1 = mk(set1, "A"), a2 = mk(set2, "B");
    let s4 = { L: [a1, a2], R: [], base: [] };
    r = M.transfer(s4, "L", [a1.id], {});
    check(r.ok && r.st.R.filter(i => i.kind === K.SETTINGS).length === 1, "settings to the FM-1 panel");
    r = M.transfer(r.st, "L", [a2.id], {});
    check(r.ok && r.st.R.filter(i => i.kind === K.SETTINGS).length === 1 && r.st.R[0].recs[0].payload[0] === 2 && r.replaced.length === 1,
        "another settings replaces it: " + r.msg);
    r = M.transfer(r.st, "L", [a2.id], {});
    check(r.ok && /ALREADY THERE/.test(r.msg), "the same settings: already there");

    /* ---- the EDITOR's pattern: a plain pattern on the TABLE, a free number back ---- */
    const fe = { id: M.newId(), side: "R", kind: K.PATTERN, num: 0xFE, orig: 0xFE, link: {}, recs: [{ sub: 0, ver: 1, payload: new Uint8Array([4, 63, 1, 0, 0]) }] };
    check(M.nameOf(fe) === "EDITOR'S PATTERN" && M.groupOf(fe) === "SYSTEM", "FE on the FM-1 panel: the EDITOR'S PATTERN, in SYSTEM");
    r = M.transfer({ L: [], R: [fe], base: [] }, "R", [fe.id], {});
    const feL = r.st.L[0];
    check(r.ok && M.nameOf(feL) === "EDITOR FE" && M.groupOf(feL) === "PATTERNS", "on the TABLE: EDITOR FE, a pattern");
    r = M.transfer({ L: [feL], R: [], base: [] }, "L", [feL.id], {});
    check(r.ok && r.st.R[0].num === 0, "moved back: a free number, " + H2(r.st.R[0].num));

    /* ---- a changed built-in: on its number when free there, else another ---- */
    const b0 = bi.find(b => b.kind === K.DX7);
    const ch = b0.payload.slice(); ch[0] ^= 1;
    const cb = M.fromRecords([{ kind: b0.kind, num: b0.num, sub: 0, ver: b0.ver, payload: ch }], { side: "L", src: "X" }).items[0];
    check(cb.builtin && cb.orig === b0.num, "a changed built-in on the TABLE remembers " + H2(b0.num));
    r = M.transfer({ L: [cb], R: [], base: [] }, "L", [cb.id], {});
    check(r.ok && r.st.R[0].num === b0.num && r.st.R[0].builtin, "on the FM-1 panel it lands on " + H2(b0.num));
    const ch2 = ch.slice(); ch2[0] ^= 2;
    const cb2 = M.fromRecords([{ kind: b0.kind, num: b0.num, sub: 0, ver: b0.ver, payload: ch2 }], { side: "L", src: "Y" }).items[0];
    const r3 = M.transfer({ L: [cb2], R: r.st.R, base: [] }, "L", [cb2.id], {});
    check(r3.ok && r3.st.R.length === 2 && r3.st.R[1].num < 0xF0 && !r3.st.R[1].builtin, "another change of it: a free number, " + H2(r3.st.R[1].num));
    /* deleting a changed built-in: the firmware's own again, references to it name the firmware's */
    const user = { id: M.newId(), side: "R", kind: K.PATTERN, num: 0x10, link: { ["c" + H2(b0.num)]: r.st.R[0].id },
        recs: [{ sub: 0, ver: 1, payload: R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map([["0,0", { note: 60, ins: b0.num << 8, vol: -1, cmds: [] }]]) })[0] }] };
    const s5 = { L: [], R: [r.st.R[0], user], base: [] };
    check(M.blockers(s5.R, [r.st.R[0].id]).length === 0, "a changed built-in a pattern uses: DELETE goes on (the pattern plays the firmware's own then)");
    check(!M.exclusive(s5.R, [user.id]).has(r.st.R[0].id), "ALSO WHAT ONLY IT USES never takes a changed built-in back");
    const d5 = M.remove(s5, "R", [r.st.R[0].id], false);
    check(d5.st.R.length === 1 && d5.st.R[0].link["c" + H2(b0.num)] === M.BUILTIN, "deleted: the pattern names the firmware's own");
    /* landed again on its number by a move: the panel's references to that number name it, as on the FM-1 */
    const r6 = M.transfer({ L: [cb], R: d5.st.R, base: [] }, "L", [cb.id], {});
    const cb6 = r6.ok && r6.st.R.find(i => i.builtin), u6 = r6.ok && r6.st.R.find(i => i.kind === K.PATTERN);
    check(cb6 && cb6.num === b0.num && u6.link["c" + H2(b0.num)] === cb6.id && M.closure(r6.st.R, [u6.id]).has(cb6.id),
        "a changed built-in landing on its number: the panel's pattern names it (and takes it with it)");
    check(d5.st.R[0].link["c" + H2(b0.num)] === M.BUILTIN, "the panel before the move left as it was");
    check(M.linkChanged(r6.st.R) === r6.st.R, "nothing more to relink: the same array");

    /* ---- refused when it does not fit ---- */
    const big = R.ptrEncode(Array.from({ length: 300000 }, (_, i) => (i * 7919) % 255 - 127), 8);
    const bigS = { id: M.newId(), side: "L", kind: K.AUDIO, num: null, orig: null, link: {}, owner: null,
        entry: M.packedEntry({ length: 300000, rate: 44100, coding: 1, name: "NOISE" }), recs: [{ sub: 0, ver: 1, payload: big }], name: "NOISE" };
    r = M.transfer({ L: [bigS], R: st.R, base: [] }, "L", [bigS.id], {});
    check(!r.ok && /DOESN'T FIT: SAMPLE NOISE NEEDS \d+ KB MORE/.test(r.msg) && r.need > 0, "too big: " + r.msg);
    /* the reserve is advice, not kept: fewer free blocks than it fit and are said; what the planner cannot place does not */
    {
        const reg = n => ({ regions: [{ start: 0x93000, end: 0x93000 + n * 4096, area: "user", inPlace: true }], reserve: 16 });
        const used = M.space(st.R, reg(103)).used, s1 = M.space(st.R, reg(used + 5)), s2 = M.space(st.R, reg(used - 1));
        check(s1.fits && s1.free === 5 && /^ONLY 20 KB FREE ON THE FM-1: IT CAN'T SAVE NEW THINGS/.test(s1.low || "") && !s2.fits && s2.missing >= 1,
            "the reserve not kept: 5 blocks left fit, said (" + s1.low + "); one block short does not fit (" + s2.missing + " missing)");
        check(/NOT EVEN A DELETE/.test(M.lowSpace(2, 16)) && /LESS THAN THE 64 KB ADVISED/.test(M.lowSpace(12, 16)) && M.lowSpace(16, 16) === null,
            "what few free blocks cost: under 3 nothing saved, under 10 nothing new, under 16 less than advised");
    }

    /* ---- DELETE ---- */
    const R0 = st.R;
    const rPat = R0.find(i => i.id === rSong.link[M.lk("p", M.refs(rSong)[0].num)]);
    const bl = M.blockers(R0, [rPat.id]);
    check(bl.length >= 1 && bl.some(([u]) => u === rSong.id), "a pattern the song plays: refused (" + bl.length + " users)");
    const ex = M.exclusive(R0, [rSong.id]);
    check(ex.has(rPat.id) || R0.filter(s => s.kind === K.SONG).some(s => s.id !== rSong.id && Object.values(s.link).includes(rPat.id)),
        "ALSO WHAT ONLY IT USES takes its patterns: " + ex.size + " items");
    check([...ex].every(id => { const it = R0.find(i => i.id === id); return it.kind !== K.SETTINGS; }), "never the system items");
    const d1 = M.remove(st, "R", [rSong.id], true);
    check(d1.st.R.length === R0.length - 1 - ex.size && d1.bytes > 0, "deleted with what only it used: " + d1.gone.size + " items, " + d1.bytes + " B");
    const d0 = M.remove(st, "R", [rSong.id], false);
    check(d0.st.R.length === R0.length - 1, "deleted alone: its patterns stay");
    /* a sampler goes with its samples; a sample an instrument plays is refused; one no instrument plays goes alone */
    const smpl = R0.find(i => i.kind === K.SAMPLER && R0.filter(s => s.owner === i.id).length > 1);
    const its = R0.filter(s => s.owner === smpl.id);
    const played = new Set(R.unpackSampler(smpl.recs[0].payload).instruments.filter(Boolean).map(x => x.sample));
    const pl = its.find(s => played.has(s.idx));
    check(pl && M.blockers(R0, [pl.id]).some(([u]) => u === smpl.id), "a sample an instrument plays: its sampler uses it");
    /* DELETE takes what uses it, and what uses that, to the end: nothing left that uses the set */
    const ua = M.usersAll(R0, [pl.id]);
    check(ua.ids.includes(smpl.id) && ua.ids.length >= 1 && M.blockers(R0, [pl.id].concat(ua.ids)).length === 0
        && ua.pairs.every(([u, t]) => ua.ids.includes(u) && (t === pl.id || ua.ids.includes(t) || R0.find(i => i.id === t).owner === smpl.id)),
        "what uses a played sample, to the end: " + ua.ids.map(id => M.oneOf(R0.find(i => i.id === id))).join(", "));
    check(M.usersAll(R0, [rSong.id]).ids.length === 0, "a song nothing uses: nothing goes with it");
    const unplayed = { L: [], R: [ns, smp], base: [] };
    const du = M.remove(unplayed, "R", [smp.id], false);
    check(du.st.R.length === 1 && R.unpackSampler(du.st.R[0].recs[0].payload).samples.length === 0, "a sample no instrument plays goes alone, its entry with it");
    const ds = M.remove({ L: [], R: R0.filter(i => i.id === smpl.id || i.owner === smpl.id), base: [] }, "R", [smpl.id], false);
    check(ds.st.R.length === 0, "a sampler goes with its " + its.length + " samples");

    /* ---- DELETE ALL ---- */
    check(M.deleteAll(st, "L").L.length === 0 && M.deleteAll(st, "R").R.length === 0, "DELETE ALL empties a panel");

    /* ---- what WRITE would change ---- */
    let pd = M.pending(st);
    check(pd.count === st.R.length && pd.removed.length === 0, "against an empty FM-1 everything is to write: " + pd.count);
    st = Object.assign({}, st, { base: st.R });
    pd = M.pending(st);
    check(pd.count === 0, "after a WRITE nothing");
    const dd = M.remove(st, "R", [rSong.id], false).st;
    pd = M.pending(dd);
    check(pd.count === 1 && pd.removed.length === 1, "a song deleted: one change, removed");

    /* ---- the EDITOR'S PATTERN on the FM-1 panel does not stand in the way of a move ---- */
    {
        const withFe = { L: L0, R: [fe], base: [] };
        const r1 = M.transfer(withFe, "L", [song0.id], {});
        check(r1.ok && M.space(r1.st.R).fits, "a move to a panel holding FE: " + r1.msg);
    }
    /* ---- equal items within one move are one item there ---- */
    {
        const twin = M.fromRecords(disk.files, { side: "L", src: "COPY.PTFMDSK" }).items;
        const s2 = twin.find(i => i.kind === K.SONG && M.nameOf(i) === M.nameOf(song0));
        const r2 = M.transfer({ L: L0.concat(twin), R: [], base: [] }, "L", [song0.id, s2.id], {});
        const once = M.transfer({ L: L0, R: [], base: [] }, "L", [song0.id], {});
        check(r2.ok && r2.st.R.length === once.st.R.length, "the same song from two disks in one move: " + r2.st.R.length + " items, as one song's " + once.st.R.length);
        /* two settings in one move: the FM-1 keeps one */
        const r3 = M.transfer({ L: [a1, a2], R: [], base: [] }, "L", [a1.id, a2.id], {});
        check(r3.ok && r3.st.R.filter(i => i.kind === K.SETTINGS).length === 1 && /KEEPS ONE OF EACH/.test(r3.msg), "two settings in one move: one record (" + r3.msg + ")");
    }
    /* ---- what the page cannot read: kept, counted, never moved, never in the way ---- */
    {
        const smpr = disk.files.find(f => f.kind === K.SAMPLER), au = disk.files.filter(f => f.kind === K.AUDIO && f.num === smpr.num);
        const odd = [Object.assign({}, smpr, { ver: 9 })].concat(au, [{ kind: 0x40, num: 0, sub: 0, ver: 1, payload: Uint8Array.of(1, 2, 3) }]);
        const dR = M.fromRecords(odd, { side: "R" }).items;
        check(dR.length === au.length + 2 && dR.every(i => i.dim), "a sampler of another version: it and its samples kept dim; a kind this page does not know kept dim on the FM-1 panel");
        let fine = true;
        try { M.space(dR); M.users(dR); M.hashes(dR); dR.forEach(i => M.nameOf(i, dR)); } catch (e) { fine = false; }
        check(fine, "dim items break neither the space, the uses, the hashes nor the names");
        const dL = M.fromRecords(odd, { side: "L" }).items;
        check(!dL.some(i => i.kind === 0x40), "on the TABLE a kind the FM-1 cannot hold is left out");
        /* a song using a pattern of another version: the song moves, the pattern stays */
        const pf = disk.files.filter(f => f.kind === K.PATTERN && f.num === M.refs(song0)[0].num);
        const sf = disk.files.find(f => f.kind === K.SONG && f.num === song0.orig);
        const mix = M.fromRecords([sf].concat(pf.map(f => Object.assign({}, f, { ver: 9 }))), { side: "L" }).items;
        const sg = mix.find(i => i.kind === K.SONG);
        const r4 = M.transfer({ L: mix, R: [], base: [] }, "L", [sg.id], {});
        check(r4.ok && r4.st.R.length === 1 && !r4.st.R.some(i => i.dim), "a song moves, its pattern of another version stays where it is");
    }
    /* ---- a damaged pattern does not hang the page; a song's position FF names nothing ---- */
    {
        const pt = disk.files.find(f => f.kind === K.PATTERN);
        const cut = M.fromRecords([Object.assign({}, pt, { payload: pt.payload.subarray(0, 9) })], { side: "L" }).items[0];
        check(Array.isArray(M.refs(cut)), "a pattern cut short: its references read to where it ends");
        const sFF = R.newSong("FF"); sFF.positions = [0xFF];
        const fs = M.fromRecords([{ kind: K.SONG, num: 1, sub: 0, ver: 1, payload: R.packSong(sFF) }], { side: "R" }).items[0];
        check(M.refs(fs).length === 0, "a song position FF: no pattern, nothing missing");
    }
    /* ---- a tab's draft of a sample carries its own entry ---- */
    {
        const o = st.R.find(i => i.kind === K.AUDIO);
        const draft = Object.assign({}, o, { entry: M.packedEntry({ length: 123, rate: 11025, coding: 1, name: "CUT" }) });
        const e2 = M.sampleEntry(draft, st.R);
        check(e2.length === 123 && e2.rate === 11025, "a sample's draft: its own entry, not its sampler's");
    }

    /* ---- UNDO ---- */
    const hist = new M.History(3);
    let cur = { L: [], R: [] };
    for (let i = 1; i <= 5; i++) { hist.push(cur, "STEP " + i); cur = { L: new Array(i), R: [] }; }
    check(hist.back.length === 3, "100 steps at most (3 here)");
    let e = hist.undo(cur); cur = e.st;
    check(cur.L.length === 4 && hist.canRedo, "UNDO");
    e = hist.redo(cur); cur = e.st;
    check(cur.L.length === 5, "REDO");

    done("model_test");
})().catch(e => { console.log("FAIL: " + e.stack); process.exit(1); });
