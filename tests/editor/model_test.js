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
    const samples = of(L0, K.SAMPLE), samplers = of(L0, K.SAMPLER);
    check(samples.length > 30 && samples.every(s => !s.owner && s.recs.length === 1 && M.groupOf(s) === "SAMPLES" && M.nameOf(s, L0) !== "SAMPLE"),
        samples.length + " samples, each an item of its own (named from its entry)");
    check(of(L0, K.AUDIO).length === 0 && samplers.length > 3 && samplers.every(c => !c.dim && M.kindText(c, L0).startsWith("SAMPLER · ")),
        samplers.length + " samplers, none holding samples: " + M.kindText(samplers[0], L0));
    check(samplers.every(c => M.refs(c).every(f => f.sp === "m" && L0.some(i => i.id === c.link[M.lk("m", f.num)] && i.kind === K.SAMPLE))),
        "every instrument names a sample the disk holds (linked)");
    check(M.size(samplers[0]) === 16 + ((samplers[0].recs[0].payload.length + 3) & ~3), "a sampler's size is its instruments' alone");
    {   /* its row's kind: its instruments, and the samples they name (specs/editor.md, DETAILS) */
        const c = samplers[0], ins = R.unpackSampler(c.recs[0].payload).instruments.filter(Boolean);
        const n = new Set(ins.map(s => s.sample)).size;
        check(M.kindText(c, L0) === "SAMPLER · " + ins.length + " INSTR · " + n + " SMP" && n > 0,
            "a sampler's row: its instruments and the samples they name: " + M.kindText(c, L0));
    }
    {   /* a sample whose entry and audio differ (another tool's record, CRC good): read as missing, as the FM-1 reads it
         * (specs/files/instruments.md#samples): listed dim and kept as it is, its number taken, the instruments naming it
         * a reference to nothing; not moved */
        const one = samples[0], p = Uint8Array.from(one.recs[0].payload);
        const e = R.unpackSample(p).entry, odd = R.packSample(Object.assign({}, e, { length: e.length + 1 }), p.subarray(16));
        const smp = c => R.unpackSampler(c.recs[0].payload).instruments.filter(Boolean);
        const user = samplers.find(c => smp(c).some(s => c.link[M.lk("m", s.sample)] === one.id));
        const num = smp(user).find(s => user.link[M.lk("m", s.sample)] === one.id).sample;
        const recs = [{ kind: K.SAMPLE, num, sub: 0, ver: 1, payload: odd }, { kind: K.SAMPLER, num: 0x20, sub: 0, ver: 2, payload: user.recs[0].payload }];
        const fr = M.fromRecords(recs, { side: "R" }).items, si = fr.find(i => i.kind === K.SAMPLE), ci = fr.find(i => i.kind === K.SAMPLER);
        check(si && si.dim && si.differ && M.kindText(si, fr) === "SAMPLE · ENTRY AND AUDIO DIFFER" && M.nameOf(si, fr) === "SAMPLE " + H2(num) &&
            ci.link[M.lk("m", num)] === null && M.namedNothing(fr).has(num) &&
            M.freeSampleNumbers(fr) === 256 - new Set(smp(user).map(s => s.sample)).size,
            "a sample whose entry and audio differ: dim, '" + (si && M.kindText(si, fr)) + "', its instruments name nothing, its number taken");
        const mv = M.transfer({ L: [], R: fr, base: [] }, "R", [si.id], {});
        check(!mv.ok && mv.msg === "A SAMPLE WHOSE ENTRY AND AUDIO DIFFER STAYS WHERE IT IS: THE FM-1 READS IT AS MISSING", "not moved: " + mv.msg);
        check(M.toRecords(fr).some(r => r.kind === K.SAMPLE && r.num === num && eq(r.payload, odd)), "written back as it is");
    }
    check(M.sizeWith(L0, samplers[0].id) > M.size(samplers[0]), "with what it uses, the samples it names are counted");
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
    const entry = { length: 2000, rate: 22050, coding: 1, name: "SINE" };
    const loose = { id: M.newId(), side: "L", kind: K.SAMPLE, num: null, orig: null, link: {},
        recs: [{ sub: 0, ver: 1, payload: R.packSample(entry, audio) }], name: "SINE", src: "SINE.WAV" };
    let s3 = { L: [loose], R: [], base: [] };
    r = M.transfer(s3, "L", [loose.id], {});
    const smp = r.st && r.st.R.find(i => i.kind === K.SAMPLE);
    check(r.ok && smp && r.st.R.length === 1 && smp.num === 0 && !/SAMPLER/.test(r.msg), "a sample alone is an item of its own on the FM-1 panel, number 00: " + r.msg);
    check(M.label(smp, r.st.R) === "SAMPLE 00 SINE" && M.toRecords(r.st.R).length === 1 && M.toRecords(r.st.R)[0].kind === K.SAMPLE && M.toRecords(r.st.R)[0].num === 0,
        "its record is kind 0x11 at its number: " + M.label(smp, r.st.R));
    r = M.transfer(r.st, "L", [loose.id], {});
    check(r.ok && /ALREADY THERE/.test(r.msg) && r.st.R.length === 1, "moved again: the same sample is already there (" + r.msg + ")");
    /* a sampler of the demo disk moved with a sample at its number already: the sample takes another number, the instruments follow */
    const host = L0.find(i => i.kind === K.SAMPLER);
    const hostRefs = M.refs(host);
    const first = hostRefs[0].num;
    const squat = Object.assign({}, loose, { id: M.newId(), orig: null, num: first, side: "R", name: "SQUAT" });
    squat.recs = [{ sub: 0, ver: 1, payload: R.packSample(Object.assign({}, entry, { name: "SQUAT" }), audio) }];
    r = M.transfer({ L: L0, R: [squat], base: [] }, "L", [host.id], {});
    const hR1 = r.ok && r.st.R.find(i => i.kind === K.SAMPLER);
    check(r.ok && hR1 && M.refs(hR1).every(f => { const t = r.st.R.find(i => i.id === hR1.link[M.lk("m", f.num)]); return t && t.kind === K.SAMPLE && t.num === f.num; }),
        "a sampler moved: its samples come, every instrument names its sample's number there (" + r.msg + ")");
    check(r.ok && M.refs(hR1).length === hostRefs.length && !M.refs(hR1).some(f => f.num === first) && /IS NOW/.test(r.msg) && r.st.R.find(i => i.id === squat.id).num === first,
        "the number taken by another sample: the one moved takes another, the other keeps its own");
    check(r.ok && r.st.R.filter(i => i.kind === K.SAMPLE).length === 1 + new Set(M.refs(host).map(f => f.num)).size, "its samples, once each, beside the sample already there");
    /* numbers references still name are passed over: an instrument naming 17 with no sample there */
    {
        const sm0 = R.unpackSampler(host.recs[0].payload);
        const ghost = Object.assign({}, host, { id: M.newId(), side: "R", num: 0x30, orig: 0x30 });
        const gp = host.recs[0].payload.slice(); gp[10] = 0x11;                    /* its first instrument names sample 11, which nothing holds */
        ghost.recs = [{ sub: 0, ver: 2, payload: gp }]; ghost.link = {};
        const gR = M.fromRecords(M.toRecords([ghost]), { side: "R" }).items;
        check(gR.length === 1 && gR[0].link[M.lk("m", 0x11)] === null, "an instrument naming a missing sample: a reference to nothing");
        const wantEleven = Object.assign({}, loose, { id: M.newId(), orig: 0x11 });
        const rr = M.transfer({ L: [wantEleven], R: gR, base: [] }, "L", [wantEleven.id], {});
        const got = rr.ok && rr.st.R.find(i => i.kind === K.SAMPLE);
        check(got && got.num !== 0x11 && got.num === 0, "a sample whose number 11 a reference names: passed over, it takes 00 (" + rr.msg + ")");
        const withN = M.pending({ R: gR.concat([got]), base: [] });
        check(M.namedNothing(gR).has(0x11) && sm0.instruments.length > 0 && withN.count === 2, "the numbers named by nothing are listed");
        /* no free sample number */
        const full = [];
        for (let n = 0; n < 256; n++) full.push({ id: M.newId(), side: "R", kind: K.SAMPLE, num: n, orig: n, link: {}, recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 8, rate: 8000, coding: 1, name: "F" + n }, audio.slice(0, 20)) }] });
        const nf = M.transfer({ L: [loose], R: full, base: [] }, "L", [loose.id], {});
        check(!nf.ok && nf.msg === "NOT ENOUGH SAMPLE NUMBERS: 1 NEEDED, 0 FREE", "256 samples: " + nf.msg);
        /* a sampler of 64 samples to a panel with 200 samples: 64 needed, 56 free */
        const part = full.slice(0, 200);
        const big64 = L0.find(i => i.kind === K.SAMPLER && M.sizeWith(L0, i.id) > 0 && new Set(M.refs(i).map(f => f.num)).size > 1);
        const needN = M.closure(L0, [big64.id]), nS = [...needN].filter(id => L0.find(i => i.id === id).kind === K.SAMPLE).length;
        const part2 = full.slice(0, 256 - nS + 1);
        const nm = M.transfer({ L: L0, R: part2, base: [] }, "L", [big64.id], {});
        check(!nm.ok && nm.msg === "NOT ENOUGH SAMPLE NUMBERS: " + nS + " NEEDED, " + (nS - 1) + " FREE" && part.length === 200,
            "a sampler needing " + nS + " samples moved to a panel with " + (nS - 1) + " numbers free: " + nm.msg);
    }

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
    const r3b = M.transfer({ L: [cb2], R: r3.st.R, base: [] }, "L", [cb2.id], {});
    check(r3b.ok && r3b.st.R.length === 2 && /ALREADY THERE/.test(r3b.msg), "moved again: the change that took a free number is already there (" + r3b.msg + ")");
    {   /* ...and a pattern using it, moved twice: the second time all of it is there */
        const pu = { id: M.newId(), side: "L", kind: K.PATTERN, num: null, orig: 0x10, link: { ["c" + H2(b0.num)]: cb2.id }, name: "USES CB2",
            recs: R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map([["0,0", { note: 60, ins: b0.num << 8, vol: -1, cmds: [] }]]) }).map((payload, sub) => ({ sub, ver: R.VERSION[K.PATTERN], payload })) };
        const u1 = M.transfer({ L: [cb2, pu], R: r.st.R, base: [] }, "L", [pu.id], {});
        const u2 = u1.ok && M.transfer({ L: [cb2, pu], R: u1.st.R, base: [] }, "L", [pu.id], {});
        check(u2 && u2.ok && u2.st.R.length === u1.st.R.length && /ALREADY THERE/.test(u2.msg) && /NOTHING NEW/.test(u2.msg),
            "a pattern using that change, moved twice: the second time all of it is already there (" + (u2 && u2.msg) + ")");
    }
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
    const bigS = { id: M.newId(), side: "L", kind: K.SAMPLE, num: null, orig: null, link: {},
        recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 300000, rate: 44100, coding: 1, name: "NOISE" }, big) }], name: "NOISE" };
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
    /* a sample an instrument names: DELETE takes the instruments naming it, in every sampler, not the sampler */
    const smpl = R0.find(i => i.kind === K.SAMPLER && new Set(M.refs(i).map(f => f.num)).size > 1);
    const pl = R0.find(i => i.id === smpl.link[M.lk("m", M.refs(smpl)[0].num)]);
    check(pl && pl.kind === K.SAMPLE && M.users(R0).get(pl.id).includes(smpl.id), "a sample's users are the samplers whose instruments name it");
    check(M.blockers(R0, [pl.id]).length === 0 && M.usersAll(R0, [pl.id]).ids.length === 0, "DELETE of a sample is never refused, nor takes a sampler along");
    const named = M.instrumentsNaming(R0, [pl.id], false);
    const namedBy = R0.filter(i => i.kind === K.SAMPLER && Object.values(i.link).includes(pl.id));
    const nInst = namedBy.reduce((n, i) => n + M.refs(i).filter(f => i.link[M.lk("m", f.num)] === pl.id).length, 0);
    check(named.instruments === nInst && named.instruments >= 1 && named.samplers.length === namedBy.length,
        "the page's question counts the instruments: " + named.instruments + " in " + named.samplers.length + " samplers");
    const dp = M.remove(st, "R", [pl.id], false);
    const after = dp.st.R.find(i => i.id === smpl.id);
    check(dp.st.R.length === R0.length - 1 && !dp.st.R.some(i => i.id === pl.id), "the sample goes alone");
    check(M.refs(after).every(f => after.link[M.lk("m", f.num)] !== pl.id) && !(M.lk("m", M.refs(smpl)[0].num) in after.link && after.link[M.lk("m", M.refs(smpl)[0].num)] === pl.id) &&
        R.unpackSampler(after.recs[0].payload).instruments.length === R.unpackSampler(smpl.recs[0].payload).instruments.length &&
        R.unpackSampler(after.recs[0].payload).instruments.filter(Boolean).length === R.unpackSampler(smpl.recs[0].payload).instruments.filter(Boolean).length - M.refs(smpl).filter(f => smpl.link[M.lk("m", f.num)] === pl.id).length,
        "its instruments in the sampler become empty slots (the slots kept), the sampler stays");
    check(namedBy.every(i => dp.st.R.some(x => x.id === i.id)), "every sampler naming it stays");
    check(M.usersAll(R0, [rSong.id]).ids.length === 0, "a song nothing uses: nothing goes with it");
    /* on the TABLE, whose items have no numbers, a sample's DELETE takes what uses it as before: the samplers, and what uses those */
    {
        const sL = L0.find(i => i.id === L0.find(c => c.kind === K.SAMPLER && Object.keys(c.link).length).link[M.lk("m", M.refs(L0.find(c => c.kind === K.SAMPLER && Object.keys(c.link).length))[0].num)]);
        const us = M.usersAll(L0, [sL.id]);
        check(M.blockers(L0, [sL.id]).length > 0 && us.ids.some(id => L0.find(i => i.id === id).kind === K.SAMPLER) && M.blockers(L0, [sL.id].concat(us.ids)).length === 0 &&
            M.instrumentsNaming(L0, [sL.id], false).instruments === 0,
            "on the TABLE a sample's DELETE takes its samplers and what uses those, to the end (" + us.ids.map(id => M.oneOf(L0.find(i => i.id === id))).join(", ") + ")");
    }
    /* a sampler alone: its samples stay unless ALSO WHAT ONLY IT USES; shared samples go only when no other sampler names them */
    const ds = M.remove(st, "R", [smpl.id], false);
    check(ds.st.R.length === R0.length - 1 && ds.st.R.filter(i => i.kind === K.SAMPLE).length === R0.filter(i => i.kind === K.SAMPLE).length, "a sampler goes alone: its samples stay");
    const exs = M.exclusive(R0, [smpl.id]);
    const sharedSample = [...new Set(M.refs(smpl).map(f => smpl.link[M.lk("m", f.num)]))].find(id => (M.users(R0).get(id) || []).some(u => u !== smpl.id));
    check([...exs].every(id => R0.find(i => i.id === id).kind === K.SAMPLE && (M.users(R0).get(id) || []).every(u => u === smpl.id)) &&
        (!sharedSample || !exs.has(sharedSample)), "ALSO WHAT ONLY IT USES: the samples no other sampler names (" + exs.size + "), never a shared one");
    const dsx = M.remove(st, "R", [smpl.id], true);
    check(dsx.st.R.length === R0.length - 1 - exs.size, "and DELETE with it takes exactly those");
    /* a sampler holding the only users of a sample, several samplers sharing one */
    {
        const A = R0.filter(i => i.kind === K.SAMPLER);
        const sharedBy = new Map();
        for (const a of A) for (const id of new Set(M.refs(a).map(f => a.link[M.lk("m", f.num)]))) sharedBy.set(id, (sharedBy.get(id) || []).concat(a.id));
        const shared = [...sharedBy].find(([, v]) => v.length > 1);
        check(!shared || (M.instrumentsNaming(R0, [shared[0]], false).samplers.length === shared[1].length), "a sample two samplers name: both lose their instruments");
    }

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
        const smpr = disk.files.find(f => f.kind === K.SAMPLER), au = disk.files.filter(f => f.kind === K.SAMPLE).slice(0, 2);
        const odd = [Object.assign({}, smpr, { ver: 9 })].concat(au, [{ kind: 0x40, num: 0, sub: 0, ver: 1, payload: Uint8Array.of(1, 2, 3) }]);
        const dR = M.fromRecords(odd, { side: "R" }).items;
        check(dR.length === au.length + 2 && dR.filter(i => i.dim).length === 2 && dR.filter(i => i.kind === K.SAMPLE).every(i => !i.dim),
            "a sampler of another version and a kind this page does not know are kept dim on the FM-1 panel; the samples, records of their own, are read");
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
        const o = st.R.find(i => i.kind === K.SAMPLE);
        const draft = Object.assign({}, o, { entry: M.packedEntry({ length: 123, rate: 11025, coding: 1, name: "CUT" }) });
        const e2 = M.sampleEntry(draft, st.R);
        check(e2.length === 123 && e2.rate === 11025 && M.sampleEntry(o, st.R).name === M.nameOf(o, st.R), "a sample's draft: its own entry, not its record's");
    }

    /* ---- the old form: converted as it loads (a disk, a backup, a READ, the kept panels), once ---- */
    {
        const sams = disk.files.filter(f => f.kind === K.SAMPLER), smps = disk.files.filter(f => f.kind === K.SAMPLE);
        const dn = R.downgrade(sams.concat(smps), 1);
        const oldDisk = disk.files.filter(f => f.kind !== K.SAMPLER && f.kind !== K.SAMPLE).concat(dn.recs);
        check(oldDisk.some(f => f.kind === K.AUDIO) && oldDisk.filter(f => f.kind === K.SAMPLER).every(f => f.ver === 1), "an old-form disk made from the demo disk: " + dn.recs.length + " old records");
        for (const side of ["L", "R"]) {
            const ld = M.fromRecords(oldDisk, { side, src: "OLD.PTFMDSK" });
            const its = ld.items, cv = ld.conversion;
            const ss = its.filter(i => i.kind === K.SAMPLER), sm = its.filter(i => i.kind === K.SAMPLE);
            check(cv && cv.converted.length === sams.length && cv.older.length === 0 && cv.dropped === 0 && cv.samples === sm.length && !its.some(i => i.kind === K.AUDIO || i.dim),
                side + ": " + sams.length + " old samplers converted as the disk loads, " + sm.length + " samples of their own (" + JSON.stringify(cv) + ")");
            check(ss.every(c => M.refs(c).every(f => { const t = its.find(i => i.id === c.link[M.lk("m", f.num)]); return t && t.kind === K.SAMPLE; })), side + ": every instrument names its sample (linked)");
            if (side === "R") {
                check(ss.map(c => c.num).join() === sams.map(c => c.num).join() && sm.map(i => i.num).sort((a, b) => a - b).every((n, i) => n === i),
                    "R: the samplers keep their numbers; the samples take the lowest free numbers, 00 upward");
                check(its.filter(i => i.kind !== K.SAMPLER && i.kind !== K.SAMPLE).length === L0.filter(i => i.kind !== K.SAMPLER && i.kind !== K.SAMPLE).length, "R: the songs, patterns and cartridges are untouched");
            }
            if (side === "R") {
                const again = M.fromRecords(M.toRecords(its), { side });
                check(again.conversion === null && again.items.length === its.length, "R: converted records load again with nothing to convert (once)");
            }
        }
        /* a sampler with the same sample as another's: one sample, both naming it */
        const R2 = M.fromRecords(oldDisk, { side: "R" }).items;
        const names = new Map();
        for (const c of R2.filter(i => i.kind === K.SAMPLER)) for (const id of new Set(M.refs(c).map(f => c.link[M.lk("m", f.num)]))) names.set(id, (names.get(id) || 0) + 1);
        check(new Set(names.values()).size >= 1 && R2.filter(i => i.kind === K.SAMPLE).every(i => names.has(i.id)), "every converted sample is named by an instrument");

        /* past 256 samples: whole samplers while they fit; the rest OLDER FORM, listed with their size, not played or edited, DELETE takes them */
        const audioOf = k => R.ptrEncode(Array.from({ length: 16 }, (_, i) => (k * 7 + i) % 120 - 60), 8);
        const mk = (num, from) => {
            const insts = [Object.assign(ED.defs.newSlot(K.SAMPLER, "I" + num), { sample: 0, start: 0, end: 16, base_note: 60 })];
            const entries = Array.from({ length: 64 }, (_, k) => ({ length: 16, rate: 8000, coding: 1, name: "S" + (from + k) }));
            return [{ kind: K.SAMPLER, num, sub: 0, ver: 1, payload: R.packSampler({ name: "BIG " + num, instruments: insts, samples: entries }) }]
                .concat(entries.map((_, k) => ({ kind: K.AUDIO, num, sub: k, ver: 1, payload: audioOf(from + k) })));
        };
        const big = [0, 1, 2, 3, 4].flatMap(n => mk(n, n * 64));
        const bg = M.fromRecords(big, { side: "R" });
        const older = bg.items.filter(i => i.older), listed = bg.items.filter(i => i.kind === K.SAMPLER);
        check(bg.conversion.converted.length === 4 && bg.conversion.older.join() === "4" && bg.conversion.samples === 256 && older.length === 1 && older[0].num === 4 && older[0].dim,
            "320 samples in 5 samplers: 4 converted (256 samples), the 5th OLDER FORM (" + JSON.stringify(bg.conversion) + ")");
        check(M.kindText(older[0], bg.items) === "SAMPLER · OLDER FORM · 64 SAMPLES NEED NUMBERS" && M.nameOf(older[0], bg.items) === "BIG 4" &&
            bg.items.filter(i => i.kind === K.AUDIO).length === 64 && listed.length === 5,
            "it is listed with its name and how many numbers it needs, its 64 audio records kept under it (" + M.kindText(older[0], bg.items) + ")");
        check(/1 SAMPLER LEFT IN THE OLDER FORM: PAST 256 SAMPLES, 64 SAMPLES NEED NUMBERS, 0 FREE/.test(ED.fm1.conversionText(bg.conversion)),
            "the status words say how many numbers it needs, and how many are free: " + ED.fm1.conversionText(bg.conversion));
        check(M.sizeWith(bg.items, older[0].id) > 64 * 16 && M.size(older[0]) < M.sizeWith(bg.items, older[0].id), "its size with its audio is counted");
        const mvO = M.transfer({ L: [], R: bg.items, base: [] }, "R", [older[0].id], {});
        check(!mvO.ok && mvO.msg === "A SAMPLER IN THE OLDER FORM STAYS WHERE IT IS: 64 SAMPLES NEED NUMBERS, 0 FREE: DELETE SAMPLES, OR IT, TO FREE NUMBERS",
            "not moved, saying how many numbers it needs: " + mvO.msg);
        const delO = M.remove({ L: [], R: bg.items, base: [] }, "R", [older[0].id], false);
        check(delO.st.R.length === bg.items.length - 65 && !delO.st.R.some(i => i.kind === K.AUDIO) && M.blockers(bg.items, [older[0].id]).length === 0,
            "DELETE takes it with its old audio (65 records), nothing stands in the way");
        check(M.toRecords(bg.items).filter(r => r.kind === K.AUDIO).length === 64 && M.toRecords(bg.items).some(r => r.kind === K.SAMPLER && r.num === 4 && r.ver === 1),
            "kept as records exactly as they were (old sampler at 04, its 64 audio records)");
        /* the TABLE has no numbers: no cap, no OLDER FORM; every sample an item, whole samplers */
        {
            const tl = M.fromRecords(big, { side: "L", src: "BIG.PTFMDSK" });
            const ts = tl.items.filter(i => i.kind === K.SAMPLER), tm = tl.items.filter(i => i.kind === K.SAMPLE);
            check(ts.length === 5 && ts.every(i => !i.older && !i.dim) && tm.length === 320 && !tl.items.some(i => i.kind === K.AUDIO || i.older || i.dim) &&
                tl.conversion.older.length === 0 && tl.conversion.samples === 320 && ts.every(c => M.refs(c).every(f => tl.items.some(x => x.id === c.link[M.lk("m", f.num)]))),
                "a disk of 320 distinct samples on the TABLE: 5 samplers converted whole, 320 samples, no OLDER FORM (" + ts.length + " samplers, " + tm.length + " samples)");
            const olds = [0, 1, 2, 3, 4, 5].flatMap(n => mk(n, n * 64));
            const k = olds.filter(r => r.kind === K.SAMPLER).map(r => ({ id: "k" + r.num, side: "L", kind: K.SAMPLER, num: null, orig: r.num, link: {}, recs: [{ sub: 0, ver: 1, payload: r.payload }] }))
                .concat(olds.filter(r => r.kind === K.AUDIO).map(r => ({ id: "ka" + r.num + "." + r.sub, side: "L", kind: K.AUDIO, num: null, orig: r.num, link: {}, owner: "k" + r.num, idx: r.sub, recs: [{ sub: r.sub, ver: 1, payload: r.payload }] })));
            const ku = M.upgradeKept(k, "L");
            check(ku.filter(i => i.kind === K.SAMPLER).length === 6 && ku.filter(i => i.kind === K.SAMPLER).every(i => !i.older && !i.dim) && ku.filter(i => i.kind === K.SAMPLE).length === 384 &&
                !ku.some(i => i.kind === K.AUDIO), "a kept TABLE of 6 old samplers x 64 distinct samples: no OLDER FORM item, 384 samples");
            /* moved to the FM-1 panel it brings the samples its instruments name (one each here), not the 378 nobody names */
            const mv = M.transfer({ L: ku, R: [], base: [] }, "L", ku.filter(i => i.kind === K.SAMPLER).map(i => i.id), {});
            check(mv.ok && mv.st.R.filter(i => i.kind === K.SAMPLE).length === 6, "...and moved to the FM-1 panel, only the samples its instruments name come: " + mv.msg);
        }
        /* numbers of an older sampler are taken: NEW / a move never lands on 04 */
        check(M.taken(bg.items).c.has(4), "the older sampler's number is taken");
        /* the room: the panel's space counts the old records as the FM-1 holds them */
        check(M.space(bg.items).bytes > M.space(delO.st.R).bytes, "the space counts the older sampler's records");
    }

    /* ---- what an older page kept in the browser: converted once at the first load ---- */
    {
        const audioOf = k => R.ptrEncode(Array.from({ length: 16 }, (_, i) => (k * 5 + i) % 100 - 50), 8);
        const entry = (n, k) => ({ length: 16, rate: 8000, coding: 1, name: n + k });
        const oldItems = (side, base) => {
            const out = [];
            [0, 1].forEach(n => {
                const entries = [0, 1, 2].map(k => entry("A", n * 10 + k)), slot = Object.assign(ED.defs.newSlot(K.SAMPLER, "I" + n), { sample: 1, start: 0, end: 16 });
                const smp = { id: base + "s" + n, side, kind: K.SAMPLER, num: side === "R" ? n + 3 : null, orig: n + 3, link: {}, src: "OLD",
                    recs: [{ sub: 0, ver: 1, payload: R.packSampler({ name: "OLD" + n, instruments: [slot], samples: entries }) }] };
                out.push(smp);
                entries.forEach((e, k) => out.push({ id: base + "a" + n + k, side, kind: K.AUDIO, num: side === "R" ? n + 3 : null, orig: n + 3, link: {}, owner: smp.id, idx: k,
                    recs: [{ sub: k, ver: 1, payload: n === 1 && k === 1 ? audioOf(1) : audioOf(n * 10 + k) }] }));
            });
            if (side === "L") out.push({ id: base + "loose", side, kind: K.AUDIO, num: null, orig: null, link: {}, owner: null, entry: M.packedEntry(entry("L", 0)),
                recs: [{ sub: 0, ver: 1, payload: audioOf(99) }], name: "L0", src: "SINE.WAV" });
            return out;
        };
        const nR = M.upgradeKept(oldItems("R", "r"), "R");
        const smR = nR.filter(i => i.kind === K.SAMPLE), samR = nR.filter(i => i.kind === K.SAMPLER);
        check(samR.length === 2 && samR.map(i => i.id).join() === "rs0,rs1" && samR.every(i => i.recs[0].ver === 2 && !i.dim) && !nR.some(i => i.kind === K.AUDIO) &&
            samR.map(i => i.num).join() === "3,4",
            "the FM-1 panel kept in the old form: the samplers keep their ids and numbers, now version 2, no old audio left");
        check(smR.length === 6 && smR.every(i => i.num != null && i.side === "R") && smR.map(i => i.num).sort((a, b) => a - b).join() === "0,1,2,3,4,5", "its 6 samples are items of their own at 00-05 (" + smR.map(i => i.num).join() + ")");
        check(samR.every(c => M.refs(c).length === 1 && nR.some(i => i.id === c.link[M.lk("m", M.refs(c)[0].num)] && i.kind === K.SAMPLE)), "each instrument is linked to its sample");
        check(M.upgradeKept(nR, "R") === nR, "converted once: the second load finds nothing to convert (the same array)");
        const nL = M.upgradeKept(oldItems("L", "l"), "L");
        const loose = nL.find(i => i.id === "lloose");
        check(loose && loose.kind === K.SAMPLE && !loose.entry && M.sampleEntry(loose, nL).name === "L0" && loose.src === "SINE.WAV" && R.unpackSample(loose.recs[0].payload).audio.length === audioOf(99).length,
            "the TABLE kept: a loose sound becomes a sample, its name, source and audio kept");
        check(nL.filter(i => i.kind === K.SAMPLE).length === 7 && nL.every(i => i.kind !== K.SAMPLE || i.num === null), "the TABLE's samples have no numbers (7 with the loose one)");
        /* the browser's store: base too, through the keep module */
        require("../../editor/js/keep.js");
        const saved = { v: 1, st: { L: oldItems("L", "x"), R: oldItems("R", "y"), base: oldItems("R", "z") } };
        const up = ED.keep.upgrade(saved);
        check(up.converted === true && ["L", "R", "base"].every(s => up.st[s].every(i => i.kind !== K.AUDIO)) && up.st.base.filter(i => i.kind === K.SAMPLE).length === 6,
            "ED.keep.upgrade: the TABLE, the FM-1 panel and its base converted together");
        check(ED.keep.upgrade({ v: 1, st: { L: [], R: [], base: [] } }).converted === undefined, "a state with nothing old: untouched");
        /* a panel with an unwritten edit over its base: converted alike, so the only pending change is the edit */
        {
            const same = oldItems("R", "w"), extra = { id: "wnew", side: "R", kind: K.PATTERN, num: 0x40, orig: 0x40, link: {}, recs: [{ sub: 0, ver: 1, payload: R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map() })[0] }] };
            const u2 = ED.keep.upgrade({ v: 1, st: { L: [], R: same.concat([extra]), base: same } });
            check(u2.converted === true && u2.st.base.filter(i => i.kind === K.SAMPLE).length === 6 && u2.st.base.every(b => u2.st.R.some(x => x.id === b.id)) &&
                M.pending({ R: u2.st.R, base: u2.st.base }).count === 1 && !("old" in u2),
                "a kept panel with one unwritten edit: its base converted like the panel, one pending change (" + M.pending({ R: u2.st.R, base: u2.st.base }).count + ")");
        }
    }

    /* ---- OPTIMIZE: the instruments of every sampler naming a sample follow it ---- */
    {
        const O = ED.optimize;
        const base = L0.filter(i => i.kind === K.SAMPLER)[0];
        const twin = Object.assign({}, base, { id: M.newId() });
        const items = L0.concat([twin]);
        const smp = items.find(i => i.id === base.link[M.lk("m", M.refs(base)[0].num)]);
        const pl = O.context(smp, items).playersAt;
        check(pl.length >= 2 && new Set(pl.map(p => p.item.id)).size === 2, "a sample two samplers name: both players found (" + pl.length + ")");
        const p = O.plan(items, [smp.id], "smallest"), st1 = O.apply({ L: items, R: [], base: [] }, "L", p);
        const a = R.unpackSampler(st1.L.find(i => i.id === base.id).recs[0].payload).instruments, b = R.unpackSampler(st1.L.find(i => i.id === twin.id).recs[0].payload).instruments;
        const k = (M.refs(base)[0].at - 10) / R.SAMPLER_SLOT;
        const was = R.unpackSampler(base.recs[0].payload).instruments[k];
        check(p.results.length === 1 && a[k].end === b[k].end && a[k].start === b[k].start && a[k].level === b[k].level && (a[k].end !== was.end || a[k].level !== was.level || p.results[0].cut0 === p.results[0].cut),
            "OPTIMIZE applied: the instruments in both samplers follow the new cut (END " + was.end + " -> " + a[k].end + "/" + b[k].end + ")");
    }

    /* ---- a built-in cartridge moved to the TABLE: a copy, a cartridge of its own (specs/editor.md, Moving items) ---- */
    const sig = items => { const hs = M.hashes(items); return items.map(i => i.kind + "." + i.num + "." + hs.get(i.id)).sort().join(); };
    const sameRecs = (a, b) => { const s = x => x.slice().sort((p, q) => key(p) < key(q) ? -1 : key(p) > key(q) ? 1 : 0);
        const x = s(a), y = s(b); return x.length === y.length && x.every((r, i) => key(r) === key(y[i]) && r.ver === y[i].ver && eq(r.payload, y[i].payload)); };
    const f0 = bi.find(b => b.kind === K.DX7);
    const chg = f0.payload.slice(); chg[0] ^= 1;
    const pat1 = cart => R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map([["0,0", { note: 60, ins: cart << 8, vol: -1, cmds: [] }]]) });
    const songAt = (name, pos) => { const s = R.newSong(name); s.positions = [pos]; return R.packSong(s); };
    /* the FM-1 panel holding the built-in changed, a pattern 10 playing it and a song 03 playing the pattern */
    const Rset = M.fromRecords([{ kind: f0.kind, num: f0.num, sub: 0, ver: f0.ver, payload: chg }]
        .concat(pat1(f0.num).map((payload, sub) => ({ kind: K.PATTERN, num: 0x10, sub, ver: R.VERSION[K.PATTERN], payload })),
            [{ kind: K.SONG, num: 3, sub: 0, ver: R.VERSION[K.SONG], payload: songAt("USES IT", 0x10) }]), { side: "R" }).items;
    const cR = Rset.find(i => i.builtin), pR = Rset.find(i => i.kind === K.PATTERN), sR = Rset.find(i => i.kind === K.SONG);
    check(cR && cR.num === f0.num && pR.link[M.lk("c", f0.num)] === cR.id && sR.link[M.lk("p", 0x10)] === pR.id, "an FM-1 panel with a changed " + H2(f0.num) + " a song uses");
    let cp = null, dc = null;
    {
        /* the FM-1 panel lists the firmware's own as files.js does: an item of no record of the panel's, marked fw */
        const fw0 = { id: "B:" + f0.kind + ":" + f0.num, side: "R", kind: f0.kind, num: f0.num, link: {}, fw: true,
            recs: bi.filter(x => x.kind === f0.kind && x.num === f0.num).map(x => ({ sub: x.sub, ver: x.ver, payload: x.payload })) };
        const nm = M.nameOf(fw0, []);
        const c1 = M.transfer({ L: [], R: [], base: [] }, "R", [fw0.id], { builtins: [fw0], stamp: "FM-1 4 NOV 15:00" });
        cp = c1.ok ? c1.st.L[0] : null;
        check(c1.ok && c1.st.L.length === 1 && cp.kind === K.DX7 && !cp.builtin && !cp.fw && cp.orig === null && cp.num === null && M.nameOf(cp, c1.st.L) === nm &&
            eq(cp.recs[0].payload, f0.payload) && cp.src === "FM-1 4 NOV 15:00" && c1.msg === "← TABLE: " + nm + " · A COPY OF BUILT-IN " + H2(f0.num),
            "a built-in picked and moved to the TABLE: a plain cartridge of its kind, its contents and name, no number to remember (" + c1.msg + ")");
        const c2 = cp && M.transfer(c1.st, "R", [fw0.id], { builtins: [fw0] });
        check(c2 && c2.ok && c2.st.L.length === 1 && /1 ITEM ALREADY THERE/.test(c2.msg) && !/A COPY OF/.test(c2.msg), "moved twice: one copy, the move's own ALREADY THERE (" + (c2 && c2.msg) + ")");
        const c3 = cp && M.transfer(c1.st, "L", [cp.id], {});
        const back = c3 && c3.ok && c3.st.R[0];
        check(back && c3.st.R.length === 1 && back.num === 0 && !back.builtin && M.builtinAt("c", f0.num) && !M.taken(c3.st.R).c.has(f0.num),
            "the copy moved back: a new cartridge at 00 beside the built-in " + H2(f0.num) + " (" + (c3 && c3.msg) + ")");
        check(!M.transfer({ L: [], R: [], base: [] }, "R", [fw0.id], {}).ok, "the model moves no built-in nobody asked to copy");
        /* a changed one picked itself: a copy with its changes, no number */
        const d1 = M.transfer({ L: [], R: Rset, base: [] }, "R", [cR.id], { builtins: [cR] });
        dc = d1.ok ? d1.st.L[0] : null;
        check(dc && d1.st.L.length === 1 && !dc.builtin && dc.orig === null && eq(dc.recs[0].payload, chg) && /A COPY OF BUILT-IN/.test(d1.msg),
            "a changed built-in picked and moved: a copy with its changes, no number to remember (" + d1.msg + ")");
        /* its copy moved back beside it: two cartridges */
        const d2 = dc && M.transfer({ L: d1.st.L, R: Rset, base: [] }, "L", [dc.id], {});
        const dn = d2 && d2.ok ? d2.st.R.filter(i => i.kind === K.DX7) : [];
        check(dn.length === 2 && dn.some(i => i.num === f0.num && i.builtin) && dn.some(i => i.num === 0 && !i.builtin) && !/ALREADY THERE/.test(d2.msg),
            "its copy moved back beside it: two cartridges, " + H2(f0.num) + " CHANGED and 00 (" + (d2 && d2.msg) + ")");
        /* a song using it moved to the TABLE beside the copy: its changed built-in still remembers its number */
        const d3 = M.transfer({ L: d1.st.L, R: Rset, base: [] }, "R", [sR.id], {});
        const kept = d3.ok ? d3.st.L.filter(i => i.kind === K.DX7) : [];
        check(kept.length === 2 && kept.some(i => i.builtin && i.orig === f0.num) && kept.some(i => !i.builtin && i.orig === null),
            "a song using the changed built-in, moved to the TABLE beside the copy: its built-in still remembers " + H2(f0.num) + " (" + d3.msg + ")");
        /* both picked in one move: the built-in picked becomes a copy, the song's stays itself */
        const d4 = M.transfer({ L: [], R: Rset, base: [] }, "R", [sR.id, cR.id], { builtins: [cR] });
        const both = d4.ok ? d4.st.L.filter(i => i.kind === K.DX7) : [], own = both.find(i => i.builtin);
        check(both.length === 2 && own && own.orig === f0.num && both.some(i => !i.builtin && i.orig === null) &&
            d4.st.L.find(i => i.kind === K.PATTERN).link[M.lk("c", f0.num)] === own.id,
            "a song and its changed built-in picked together: the copy, and the song's own still naming " + H2(f0.num) + " (" + d4.msg + ")");
        /* a backup (nothing picked by hand) keeps a changed built-in remembering its number */
        const bk = M.transfer({ L: [], R: Rset, base: [] }, "R", [cR.id], { stamp: "BACKUP 4 NOV 15:00" });
        check(bk.ok && bk.st.L[0].builtin && bk.st.L[0].orig === f0.num, "a changed built-in moved with no built-ins asked to copy (a backup): it remembers its number");
        /* the song's changed built-in back to an FM-1 panel holding only the copy (the firmware's own at its number): it lands
         * on its number, not on the copy */
        if (d2 && d2.ok) {
            const sL = d3.st.L.find(i => i.kind === K.SONG), R00 = d2.st.R.filter(i => i.kind === K.DX7 && !i.builtin);
            const d5 = M.transfer({ L: d3.st.L, R: R00, base: [] }, "L", [sL.id], {});
            const p5 = d5.ok && d5.st.R.find(i => i.kind === K.PATTERN), c5 = d5.ok && d5.st.R.find(i => i.builtin);
            const n5 = p5 && R.unpackPattern(p5.recs.map(x => x.payload)).cells.get("0,0").ins >> 8;
            check(c5 && c5.num === f0.num && n5 === f0.num && p5.link[M.lk("c", f0.num)] === c5.id && d5.st.R.filter(i => i.kind === K.DX7).length === 2,
                "that song moved to an FM-1 panel holding only the copy: its changed built-in lands on " + H2(f0.num) + ", not on the copy (" + d5.msg + ")");
        }
        /* a TABLE song pointing at the copy, moved to the FM-1 panel: it names the copy's new number, not the built-in's */
        for (const [copy, Rnow, where] of [[cp, [], "an empty FM-1 panel"], [dc, Rset, "one holding the changed built-in it copies"]]) {
            if (!copy) { check(false, "a TABLE song pointing at a copy: no copy made"); continue; }
            const tp = { id: M.newId(), side: "L", kind: K.PATTERN, num: null, orig: 5, link: { [M.lk("c", 5)]: copy.id }, name: "USES THE COPY",
                recs: pat1(5).map((payload, sub) => ({ sub, ver: R.VERSION[K.PATTERN], payload })) };
            const ts = { id: M.newId(), side: "L", kind: K.SONG, num: null, orig: 1, link: { [M.lk("p", 5)]: tp.id }, recs: [{ sub: 0, ver: R.VERSION[K.SONG], payload: songAt("THE COPY", 5) }] };
            const m = M.transfer({ L: [copy, tp, ts], R: Rnow, base: [] }, "L", [ts.id], {});
            const mp = m.ok && m.st.R.find(i => i.kind === K.PATTERN && i.id !== pR.id), n = mp && R.unpackPattern(mp.recs.map(x => x.payload)).cells.get("0,0").ins >> 8;
            const mc = mp && m.st.R.find(i => i.id === mp.link[M.lk("c", n)]);
            check(mc && mc.kind === K.DX7 && !mc.builtin && n < 0xF0 && n === mc.num && m.st.R.filter(i => i.kind === K.DX7).length === Rnow.filter(i => i.kind === K.DX7).length + 1,
                "a TABLE song pointing at the copy, moved to " + where + ": it names the copy's number " + (mc ? H2(mc.num) : "?") + ", not the built-in's (" + m.msg + ")");
        }
    }

    /* ---- DOWNLOAD AS DISK of the TABLE's selection (specs/editor.md, The FM-1: READ, WRITE and disks) ---- */
    {
        const F = ED.fm1, day = new Date(2026, 10, 4);
        check(typeof F.selectionDisk === "function", "the TABLE's selection as a disk: ED.fm1.selectionDisk");
        const pick = (L, ids) => typeof F.selectionDisk === "function" ? F.selectionDisk(L, ids, { date: day }) : { ok: false, msg: "NONE" };
        const one = pick(L0, [song0.id]);
        const mv = M.transfer({ L: L0, R: [], base: [] }, "L", [song0.id], {});
        check(one.ok && M.nameOf(song0, L0) === "PUNCH IN" && one.name === "punch-in.ptfmdsk" && one.items.length === 14 && one.items.length === mv.st.R.length &&
            S.parseData(one.bytes).files.length === M.toRecords(mv.st.R).length,
            "one song picked: " + one.name + ", " + (one.items || []).length + " items with what it uses, as the move to an empty FM-1 panel (" + (one.msg || "") + ")");
        if (one.ok) {
            const up = F.diskItems(one.bytes, "R", "PUNCH-IN.PTFMDSK");
            check(up.damaged === 0 && sig(up.items) === sig(mv.st.R) && sameRecs(M.toRecords(up.items), M.toRecords(mv.st.R)),
                "its disk put in an empty FM-1 panel (UPLOAD DISK): the same items, numbers and hashes as the move, record for record");
            check(M.toRecords(up.items).every(r => !(M.isCart(r.kind) && r.num >= 0xF0)) && up.items.some(i => Object.values(i.link).includes(M.BUILTIN)),
                "the built-ins left out, the references to them kept");
            const dropped = F.diskItems(one.bytes, "L", "PUNCH-IN.PTFMDSK").items, th = M.hashes(L0), dh = M.hashes(dropped);
            const picked = [...M.closure(L0, [song0.id])];
            check(dropped.length === picked.length && dropped.map(i => i.kind + "." + dh.get(i.id)).sort().join() === picked.map(id => L0.find(i => i.id === id).kind + "." + th.get(id)).sort().join(),
                "dropped on the TABLE: the song picked and what it uses, equal by hash (" + dropped.length + " items)");
        }
        /* several picked: numbered as the move numbers them (the number remembered when free, else the lowest free) */
        const s1 = of(L0, K.SONG).find(s => s.id !== song0.id);
        const other = Object.assign({}, s1, { id: M.newId(), orig: song0.orig, recs: [Object.assign({}, s1.recs[0], { payload: s1.recs[0].payload.slice() })] });
        other.recs[0].payload.set(R.packName("OTHER SONG", 16), 0);
        const two = pick(L0.concat([other]), [song0.id, other.id]);
        const songs = two.ok ? F.diskItems(two.bytes, "R", "X").items.filter(i => i.kind === K.SONG) : [];
        check(two.ok && two.name === "ptfm-selection-2026-11-04.ptfmdsk" && songs.length === 2 &&
            songs.map(s => s.num).sort((a, b) => a - b).join() === [song0.orig, song0.orig === 0 ? 1 : 0].sort((a, b) => a - b).join() &&
            songs.every(s => M.refs(s).length && M.refs(s).every(f => s.link[M.lk(f.sp, f.num)])),
            "two songs remembering one number: " + two.name + ", numbers " + songs.map(s => H2(s.num)).join(" ") + ", their positions naming their patterns there");
        const pp = of(L0, K.PATTERN)[0], onePat = pick(L0, [pp.id]);
        check(onePat.ok && onePat.name === pp.name.toLowerCase().replace(/[^a-z0-9]+/g, "-") + ".ptfmdsk" && /^demo-p[0-9a-f]{2}\.ptfmdsk$/.test(onePat.name),
            "a pattern picked: its TABLE name as the file's (" + onePat.name + ")");
        /* a changed built-in a song uses goes along, at its number */
        {
            const tl = M.transfer({ L: [], R: Rset, base: [] }, "R", [sR.id], {}).st.L;
            const d = pick(tl, [tl.find(i => i.kind === K.SONG).id]);
            const its = d.ok ? F.diskItems(d.bytes, "R", "X").items : [];
            const c = its.find(i => i.kind === K.DX7);
            check(d.ok && d.name === "uses-it.ptfmdsk" && its.length === 3 && c && c.builtin && c.num === f0.num && eq(c.recs[0].payload, chg) &&
                its.find(i => i.kind === K.PATTERN).link[M.lk("c", f0.num)] === c.id,
                "a song using a changed built-in: the disk holds it at " + H2(f0.num) + ", its pattern naming it (" + d.name + ")");
        }
        /* one system item of a kind: the first, the others said */
        const sys = pick([a1, a2], [a1.id, a2.id]);
        check(sys.ok && sys.items.length === 1 && sys.said === "THE FM-1 KEEPS ONE OF EACH: SETTINGS FROM B LEFT OUT", "two settings picked: one on the disk (" + sys.said + ")");
        /* what a move refuses, with its words */
        const no = pick(L0, []);
        check(!no.ok && no.msg === "PICK SOMETHING TO DOWNLOAD FIRST", "nothing picked: " + no.msg);
        const dimP = M.fromRecords([{ kind: K.PATTERN, num: 2, sub: 0, ver: 9, payload: new Uint8Array([1, 3, 1, 0, 0]) }], { side: "L" }).items[0];
        const nd = pick([dimP], [dimP.id]);
        check(dimP.dim && !nd.ok && nd.msg === "A VERSION THIS PAGE CANNOT READ STAYS WHERE IT IS · NO DISK SAVED", "a version this page cannot read: " + nd.msg);
        const lg = M.asLong(Object.assign({}, loose, { id: M.newId(), sound: { rate: 44100, ch: [new Float32Array(4)], cut: { start: 0, end: 4, rate: 44100 } } }), entry, 1376256);
        const nl = pick([lg, loose], [lg.id]);
        check(!nl.ok && nl.msg === "TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST: NO DISK SAVED", "a sample too long: " + nl.msg);
        const many = Array.from({ length: 257 }, (_, n) => ({ id: M.newId(), side: "L", kind: K.SAMPLE, num: null, orig: null, link: {},
            recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 8, rate: 8000, coding: 1, name: "M" + n }, audio.slice(0, 20)) }] }));
        const nn = pick(many, many.map(i => i.id));
        check(!nn.ok && nn.msg === "NOT ENOUGH SAMPLE NUMBERS: 257 NEEDED, 256 FREE · NO DISK SAVED", "257 samples: " + nn.msg);
        const huge = { id: M.newId(), side: "L", kind: K.SAMPLE, num: null, orig: null, link: {}, name: "NOISE",
            recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 480000, rate: 44100, coding: 1, name: "NOISE" }, R.ptrEncode(Array.from({ length: 480000 }, (_, i) => ((i * 2654435761) >>> 24) - 128), 8)) }] };
        const nf = pick([huge], [huge.id]);
        check(!nf.ok && /^DOESN'T FIT: SAMPLE NOISE NEEDS \d+ KB MORE ON THE FM-1\. NO DISK SAVED\.$/.test(nf.msg), "what does not fit an empty FM-1: " + nf.msg);
    }

    /* ---- a cartridge's number on the FM-1 panel: a free one, or swapped with what holds it, every reference following ---- */
    {
        const C1 = 0xA0, C2 = 0xA1, C6 = 0xA5;               /* (the demo disk's DRUM KIT, PERCUSSION, FUSION) */
        const kitL = L0.find(i => i.orig === C1 && M.isCart(i.kind)), percL = L0.find(i => i.orig === C2 && M.isCart(i.kind));
        const mk = (kind, payload, link, o) => Object.assign({ id: M.newId(), side: "L", kind, num: null, orig: null, src: "TEST", link,
            recs: [{ sub: 0, ver: R.VERSION[kind], payload }] }, o);
        /* what else names a cartridge: a favourite, a loop's table, the settings' current instrument */
        const fav = mk(K.FAVORITES, R.packCart(K.FAVORITES, { name: "MINE", slots: [{ cart: C1, slot: 0 }, { cart: C2, slot: 0 }] }),
            { [M.lk("c", C1)]: kitL.id, [M.lk("c", C2)]: percL.id });
        const lp = new Uint8Array(92); lp[5] = 1; lp[80] = C2;
        const loops = mk(K.LOOPS, lp, { [M.lk("c", C2)]: percL.id });
        const sb = new Uint8Array(133); sb.set(ED.defs.newSettings()); sb[121] = C1; sb[123] |= 1;
        const sett = mk(K.SETTINGS, sb, { [M.lk("c", C1)]: kitL.id }, { orig: 0 });
        const mv = M.transfer({ L: L0.concat([fav, loops, sett]), R: [], base: [], fm1: {} }, "L", L0.map(i => i.id).concat([fav.id, loops.id, sett.id]), {});
        check(mv.ok, "the demo disk, a favourite, loops and settings on the FM-1 panel: " + mv.msg);
        const p = Object.assign({}, mv.st, { base: mv.st.R });
        const cart = (items, n) => items.find(i => M.isCart(i.kind) && i.num === n);
        const kit = cart(p.R, C1), perc = cart(p.R, C2), fus = cart(p.R, C6);
        check(kit && perc && fus && kit.kind === K.SAMPLER && fus.kind === K.FUSION, "A0 and A1 samplers, A5 FUSION");
        const of2 = (items, kind) => items.find(i => i.kind === kind);
        const named = (items, n) => items.reduce((c, i) => c + M.refs(i).filter(f => f.sp === "c" && f.num === n).length, 0);
        const byId = items => new Map(items.map(i => [i.id, i]));
        const sameUse = (a, b) => { const x = M.users(a), y = M.users(b);
            return a.every(i => [...(x.get(i.id) || [])].sort().join() === [...(y.get(i.id) || [])].sort().join()); };
        const sameHash = (a, b) => { const x = M.hashes(a), y = M.hashes(b); return a.every(i => x.get(i.id) === y.get(i.id)); };
        /* every number a reference holds finds, through its link, the cartridge at that number */
        const linked = items => { const m = byId(items);
            return items.every(i => M.refs(i).every(f => f.sp !== "c" || f.num >= M.FIRST_FW || m.get(i.link[M.lk("c", f.num)]).num === f.num)); };
        const n1 = named(p.R, C1), n2 = named(p.R, C2);
        check(n1 > 2 && n2 > 2 && linked(p.R), "the samplers are named: " + n1 + " and " + n2 + " references");

        /* to a free number */
        let r = M.renumber(p, kit.id, 0x20);
        check(r.ok && cart(r.st.R, 0x20) && cart(r.st.R, 0x20).id === kit.id && !cart(r.st.R, C1), "A0 taken to the free 20");
        check(named(r.st.R, C1) === 0 && named(r.st.R, 0x20) === n1 && named(r.st.R, C2) === n2, "every reference follows, the others stay");
        check(linked(r.st.R) && sameHash(p.R, r.st.R) && sameUse(p.R, r.st.R), "the same items, using the same things, by their hashes");
        check(r.msg === `DRUM KIT IS NOW 20 · ${n1} REFERENCES FOLLOW · THE FM-1 CHANGES WITH WRITE`, "the status line: " + r.msg);
        check(r.plan === `20 IS FREE · ${n1} REFERENCES FOLLOW`, "the dialog's line: " + r.plan);
        check(of2(r.st.R, K.SETTINGS).recs[0].payload[121] === 0x20, "the settings' current instrument follows");
        check(of2(r.st.R, K.FAVORITES).recs[0].payload[10] === 0x20 && of2(r.st.R, K.FAVORITES).recs[0].payload[13] === C2, "a favourite follows, the other stays");
        check(of2(r.st.R, K.LOOPS).recs[0].payload[80] === C2, "the loops name another: they stay");
        check(kit.num === C1 && named(p.R, C1) === n1 && kit.recs[0].payload === cart(r.st.R, 0x20).recs[0].payload, "the state before as it was (UNDO), a cartridge's own bytes shared");
        const pe = M.pending(r.st);
        check(pe.ids.has(kit.id) && pe.removed.some(b => b.id === kit.id) && pe.ids.has(of2(r.st.R, K.SETTINGS).id), "WRITE: it and what names it pending, its old number removed");
        const rc = M.toRecords(r.st.R);
        check(rc.some(x => x.kind === K.SAMPLER && x.num === 0x20) && !rc.some(x => x.kind === K.SAMPLER && x.num === C1), "its record under the new number");

        /* to a number held: the two swap, of one kind or of two */
        r = M.renumber(p, kit.id, C2);
        check(r.ok && cart(r.st.R, C2).id === kit.id && cart(r.st.R, C1).id === perc.id, "A0 to the held A1: the two swap");
        check(named(r.st.R, C1) === n2 && named(r.st.R, C2) === n1 && linked(r.st.R) && sameHash(p.R, r.st.R) && sameUse(p.R, r.st.R), "each one's references follow it");
        check(of2(r.st.R, K.LOOPS).recs[0].payload[80] === C1, "the loops follow theirs");
        check(r.msg === `DRUM KIT IS NOW A1, PERCUSSION A0 · ${n1 + n2} REFERENCES FOLLOW · THE FM-1 CHANGES WITH WRITE`, "the status line: " + r.msg);
        check(r.plan === `A1 HOLDS PERCUSSION: THE TWO SWAP NUMBERS · ${n1 + n2} REFERENCES FOLLOW`, "the dialog's line: " + r.plan);
        r = M.renumber(p, fus.id, C1);
        check(r.ok && cart(r.st.R, C1).kind === K.FUSION && cart(r.st.R, C6).kind === K.SAMPLER && linked(r.st.R) && sameHash(p.R, r.st.R), "two kinds swap: one number space");
        const nm = (it, n) => M.refs(it).filter(f => f.sp === "m" && f.num === n).length;
        check(p.R.every(i => { const j = byId(r.st.R).get(i.id); return i.kind !== K.SAMPLER || M.refs(i).every(f => nm(j, f.num) === nm(i, f.num)); }), "the sample numbers untouched");

        /* a number references still name, with nothing there: they go to the place left, still grey, as MOVE on the FM-1 */
        const hole = Object.assign({}, p, { R: p.R.filter(i => i.id !== perc.id) });
        r = M.renumber(hole, kit.id, C2);
        check(r.ok && named(r.st.R, C2) === n1 && named(r.st.R, C1) === n2 && !cart(r.st.R, C1), "onto a place named by nothing: its references go to the place left");
        check(r.st.R.every(i => M.refs(i).every(f => f.sp !== "c" || f.num !== C1 || !byId(r.st.R).has(i.link[M.lk("c", C1)]))), "and stay naming nothing");

        /* what is refused */
        const no = (st, id, n, re, what) => { const x = M.renumber(st, id, n); check(!x.ok && !x.st && re.test(x.msg), what + ": " + x.msg); };
        no(p, kit.id, 0xF0, /^F0-FF ARE THE BUILT-IN INSTRUMENTS'/, "a built-in number");
        no(p, kit.id, 0x100, /00 TO EF/, "past a byte");
        no(p, kit.id, NaN, /00 TO EF/, "not a number");
        no(p, kit.id, C1, /^DRUM KIT IS A0 ALREADY$/, "its own number");
        no(p, p.R.find(i => i.kind === K.SONG).id, 0x20, /CARTRIDGE/, "a song");
        const changed = Object.assign({}, kit, { id: M.newId(), num: 0xF3, builtin: true });
        check(!M.canRenumber(changed) && M.canRenumber(kit), "a changed built-in keeps its number");
        const withDim = Object.assign({}, p, { R: p.R.map(i => i.id === p.R.find(x => x.kind === K.PATTERN).id ? Object.assign({}, i, { dim: true }) : i) });
        no(withDim, kit.id, 0x20, /CANNOT READ/, "a pattern the page cannot read could not follow");
        const dimHolder = Object.assign({}, p, { R: p.R.map(i => i.id === perc.id ? Object.assign({}, i, { dim: true }) : i) });
        no(dimHolder, kit.id, C2, /CANNOT READ/, "a cartridge the page cannot read holds the number");
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
