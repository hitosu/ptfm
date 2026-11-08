/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* READ and WRITE against the virtual FM-1 (specs/editor.md, The FM-1: READ, WRITE and disks; Tests): the firmware's
 * own seeding code in its WebAssembly, through seed.js's session as with a real FM-1 over MIDI. A song moved to the FM-1
 * panel and written; READ back byte for byte, then again with only the changed records read; the plan (what stays,
 * what goes); CHANGED (a save on the FM-1 meanwhile: planned again); SAVE OR REVERT FIRST; a version the FM-1 does not
 * take; built-ins it lacks; another PT/FM version; a panel that fits only the FM-1 emptied written as the whole user
 * area anew (what the page cannot read written back as it was); DELETE ALL written; a backup put back exactly, FE
 * included.
 *
 *   node tests/editor/device_test.js [WASM] */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model, F = ED.fm1;
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const key = r => r.kind + "." + r.num + "." + r.sub;
const sameRecs = (a, b) => {
    const m = new Map(a.map(r => [key(r), r]));
    return a.length === b.length && b.every(r => m.has(key(r)) && eq(m.get(key(r)).payload, r.payload));
};
const empty = () => ({ L: [], R: [], base: [], fm1: { from: "empty", lastSave: null } });

(async () => {
    const wasm = await WebAssembly.compile(fs.readFileSync(process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm")));
    const fw = await new ED.Firmware(wasm).start(null);
    M.setBuiltins(fw.builtins());
    const ses = S.session(fw.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
    const L = M.fromRecords(S.parseData(demo()).files, { side: "L", src: "DEMO.PTFMDSK" }).items;

    /* ---- a song moved and written ---- */
    let st = Object.assign(empty(), { L });
    const song = L.find(i => i.kind === K.SONG);
    let r = M.transfer(st, "L", [song.id], {});
    st = r.st;
    const w0 = F.planWrite(await ses.hello(), st);
    check(w0.problems.length === 0 && w0.plan.ok && w0.plan.del.length === 0 && w0.plan.write.length === M.toRecords(st.R).length,
        "the plan: write " + w0.plan.write.length + " records, nothing to remove");
    let res = await F.writeTo(ses, st, {});
    check(res.bad === 0 && res.records === M.toRecords(st.R).length, "written: " + JSON.stringify({ records: res.records, bad: res.bad, lastSave: res.lastSave }));
    st = Object.assign({}, st, { base: st.R, fm1: { from: "write", lastSave: res.lastSave } });

    /* ---- READ: everything, then only what changed ---- */
    let rd = await F.readFrom(ses, empty());
    check(sameRecs(M.toRecords(rd.items), M.toRecords(st.R)) && rd.read === M.toRecords(st.R).length, "READ back byte for byte, audio included: " + rd.read + " records read");
    check(rd.fm1.lastSave === res.lastSave && rd.fm1.layout && rd.fm1.layout.reserve === 16, "the panel's source: the FM-1's last save and layout");
    rd = await F.readFrom(ses, st);
    check(rd.read === 0 && rd.kept === M.toRecords(st.R).length, "READ again: nothing read, every record already known (" + rd.kept + ")");
    check(rd.items.every(i => st.R.some(x => x.id === i.id)), "the items keep their ids (open tabs stay on them)");
    const w1 = F.planWrite(await ses.hello(), st);
    check(w1.plan.upToDate, "WRITE again: nothing to write");

    /* ---- one pattern changed: only it written, only it read ---- */
    const pat = st.R.find(i => i.kind === K.PATTERN);
    const p = R.unpackPattern(pat.recs.map(x => x.payload));
    p.cells.set("0,0", { note: 61, ins: -1, vol: 0x40, cmds: [[0x0C, 0x20]] });
    const pat2 = Object.assign({}, pat, { recs: R.packPattern(p).map((payload, i) => ({ sub: i, ver: 1, payload })) });
    st = M.replaceItem(st, "R", pat2);
    check(M.pending(st).count === 1, "one change not written");
    const w2 = F.planWrite(await ses.hello(), st);
    check(w2.plan.write.length === pat2.recs.length && w2.plan.keep.length === M.toRecords(st.R).length - pat2.recs.length, "the plan: the pattern written, the rest kept where it is");
    res = await F.writeTo(ses, st, {});
    st = Object.assign({}, st, { base: st.R, fm1: { from: "write", lastSave: res.lastSave } });
    rd = await F.readFrom(ses, Object.assign({}, st, { base: st.base.map(i => i.id === pat2.id ? pat : i) }));
    check(rd.read === pat2.recs.length, "READ: only the changed pattern read again (" + rd.read + ")");

    /* ---- BACKUP: what a WRITE replaces, read first, onto the TABLE as a move takes it; nothing twice ---- */
    {
        const p3 = R.unpackPattern(pat2.recs.map(x => x.payload));
        p3.cells.set("1,0", { note: 62, ins: -1, vol: 0x30, cmds: [] });
        const pat3 = Object.assign({}, pat2, { recs: R.packPattern(p3).map((payload, i) => ({ sub: i, ver: 1, payload })) });
        let st3 = M.replaceItem(st, "R", pat3);
        /* the FM-1 as the last WRITE (or READ) left it: what the write replaces was seen, nothing backed up */
        const w0 = F.planWrite(await ses.hello(), st3);
        check(w0.backup.length === 0 && w0.seenGone === pat2.recs.length,
            "WRITE's BACKUP: the pattern it replaces is as the last WRITE left it: nothing backed up (" + w0.seenGone + " seen)");
        const wAll = F.planWrite(await ses.hello(), Object.assign({}, st, { R: [] }));
        check(wAll.backup.length === 0 && wAll.seenGone === (await ses.hello()).records.filter(x => !x.deleted).length,
            "a READ, DELETE ALL and a WRITE: nothing backed up (" + wAll.seenGone + " records removed, all seen)");
        /* changed on the FM-1 since (the last READ knew the older pattern): that one is backed up */
        st3 = Object.assign({}, st3, { base: st.base.map(i => i.id === pat2.id ? pat : i) });
        const w = F.planWrite(await ses.hello(), st3);
        check(w.backup.length === pat2.recs.length && w.backup.every(x => x.kind === K.PATTERN && x.num === pat2.num),
            "WRITE's BACKUP: the FM-1's pattern it replaces, changed since the last READ, only (" + w.backup.length + " records)");
        let got = null;
        res = await F.writeTo(ses, st3, { backup: b => { got = b; } });
        const old = got && got.items.find(i => got.ids.includes(i.id));
        check(res.bad === 0 && got && got.ids.length === 1 && old && sameRecs(M.toRecords([old]), M.toRecords([pat2])),
            "the backup read before the write: the FM-1's old pattern, byte for byte");
        const t1 = M.transfer({ L: [], R: got.items, base: [], fm1: {} }, "R", got.ids, { stamp: "BACKUP 26 OCT 11:50" });
        const t2 = M.transfer({ L: t1.st.L, R: got.items, base: [], fm1: {} }, "R", got.ids, { stamp: "BACKUP 26 OCT 11:51" });
        check(t1.ok && t1.st.L.some(i => i.kind === K.PATTERN && i.src === "BACKUP 26 OCT 11:50") && t2.ok && t2.st.L.length === t1.st.L.length,
            "onto the TABLE with what it uses (" + t1.st.L.length + " items), stamped; a second backup of the same copies nothing");
        st = Object.assign({}, st3, { base: st3.R, fm1: { from: "write", lastSave: res.lastSave } });
    }

    /* ---- CHANGED: the FM-1 saved while the page asked; the page plans again ---- */
    const other = M.transfer(st, "L", [L.filter(i => i.kind === K.SONG)[1].id], {}).st;
    let confirms = 0, newerSaid = false;
    res = await F.writeTo(ses, other, { confirm: async w => {
        confirms++;
        if (confirms === 2) { newerSaid = w.newer; return true; }
        /* (meanwhile someone else's save: the first state written again with a deleted record, a new save number) */
        const third = M.remove(st, "R", [st.R.find(i => i.kind === K.SONG).id], false).st;
        await F.writeTo(ses, third, {});           /* (the same transport: one session listens at a time) */
        return true;
    } });
    check(confirms === 2 && newerSaid && res.bad === 0, "refused as CHANGED: planned again, asked again with the FM-1's newer save said first, written");
    rd = await F.readFrom(ses, empty());
    check(sameRecs(M.toRecords(rd.items), M.toRecords(other.R)), "the FM-1 holds exactly the panel after it");
    st = Object.assign({}, other, { base: other.R, fm1: { from: "write", lastSave: res.lastSave } });

    /* ---- the newer save is said first ---- */
    const later = M.remove(st, "R", [st.R.find(i => i.kind === K.SONG).id], false).st;
    await F.writeTo(ses, later, {});
    const w3 = F.planWrite(await ses.hello(), st);
    check(w3.newer, "a save on the FM-1 after the panel's last WRITE: said before writing");

    /* ---- SAVE OR REVERT FIRST ---- */
    const sp = R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map() })[0];
    fw.mem().set(sp, fw.ex.ed_edit_buf());
    fw.ex.ed_edit_put(K.PATTERN, 0x30, 0, 1, sp.length);
    let err = null;
    try { await F.writeTo(ses, st, {}); } catch (e) { err = e; }
    check(err && /SAVE OR REVERT FIRST/.test(err.message), "edits not saved on the FM-1: " + (err && err.message));
    fw.ex.ed_remount();

    /* ---- a version the FM-1 does not take; built-ins it lacks; another version ---- */
    const odd = Object.assign({}, st.R.find(i => i.kind === K.SONG), { id: M.newId() });
    odd.recs = [Object.assign({}, odd.recs[0], { ver: 9 })];
    const w4 = F.planWrite(await ses.hello(), M.replaceItem(st, "R", Object.assign({}, odd, { id: st.R.find(i => i.kind === K.SONG).id })));
    check(w4.problems.some(t => /DOES NOT TAKE SONG VERSION 9/.test(t)), "a version the FM-1 does not take: " + w4.problems[0]);
    const lay = await ses.hello();
    /* an FM-1 that reads an older version of the loops: WRITE converts them down, what it cannot hold named */
    {
        const head = new Uint8Array(72); head.set([125, 0, 6, 50, 0, 0, 0, 0]); head[9] = 0x0F;
        const loops = M.fromRecords(R.loopsV3([head]).map((payload, sub) => ({ kind: K.LOOPS, num: 0, sub, ver: 4, payload })), { side: "R" }).items[0];
        const stL = Object.assign({}, st, { R: st.R.filter(i => i.kind !== K.LOOPS).concat([loops]) });
        const old = Object.assign({}, lay, { accepts: lay.accepts.map(a => a.kind === K.LOOPS ? { kind: K.LOOPS, ver: 3 } : a) });
        const w = F.planWrite(old, stL);
        const lw = w.plan.write.filter(f => f.kind === K.LOOPS);
        check(!w.problems.length && w.converted.length === 1 && w.converted[0].from === 4 && w.converted[0].to === 3 && !w.converted[0].lost.length &&
            lw.length === 1 && lw[0].ver === 3 && R.loopsV3([lw[0].payload]).every((p, i) => p.length === loops.recs[i].payload.length),
            "the FM-1 reads loops version 3: written as version 3, nothing lost (" + JSON.stringify(w.converted) + ")");
        const none = Object.assign({}, lay, { accepts: lay.accepts.filter(a => a.kind !== K.LOOPS) });
        check(F.planWrite(none, stL).problems.some(t => /DOES NOT TAKE LOOPS VERSION 4/.test(t)), "no version of the loops it reads: refused as before");
    }
    const usesB = st.R.find(i => Object.values(i.link).includes(M.BUILTIN));
    const bnum = M.refs(usesB).find(f => usesB.link[M.lk(f.sp, f.num)] === M.BUILTIN).num;
    const w5 = F.planWrite(Object.assign({}, lay, { builtins: lay.builtins.filter(b => b.num !== bnum) }), st);
    check(w5.missingBuiltins.length === 1 && new RegExp("USES? " + M.H2(bnum) + ", WHICH THE FM-1 DOES NOT HAVE").test(w5.missingBuiltins[0]), "built-ins the FM-1 lacks: " + w5.missingBuiltins[0]);
    check(F.otherVersion(Object.assign({}, lay, { fwText: "0.1" })) && !F.otherVersion(Object.assign({}, lay, { fwText: globalThis.PTFM_ED_WASM ? PTFM_ED_WASM.version : "" })) === !globalThis.PTFM_ED_WASM,
        "another PT/FM version said: " + F.otherVersion(Object.assign({}, lay, { fwText: "0.1" })));

    /* ---- a record of another version at a key the FM-1 holds: never written, the FM-1's left as it is ---- */
    {
        rd = await F.readFrom(ses, empty());
        const held = rd.items.find(i => i.kind === K.SONG);
        const odd = Object.assign({}, held, { id: M.newId(), dim: true, recs: [{ sub: 0, ver: 9, payload: Uint8Array.of(9, 9, 9) }] });
        const w = F.planWrite(await ses.hello(), Object.assign({}, st, { R: rd.items.map(i => i.id === held.id ? odd : i) }));
        check(w.plan.ok && w.plan.upToDate, "a dim item where the FM-1 holds a record: nothing written, nothing removed");
        const disk2 = Object.assign({}, odd, { num: 0x77 });
        const w2 = F.planWrite(await ses.hello(), Object.assign({}, st, { R: rd.items.concat([disk2]) }));
        check(w2.plan.ok && w2.plan.upToDate, "a dim item at a free key: not written");
    }

    /* ---- it fits only the FM-1 emptied: the whole user area written anew ---- */
    {
        /* the FM-1 as it says it is, every block of its user area also holding a deleted record's header (which no
         * plan may write over), so nothing fits around what is there */
        const pinAll = l => {
            const extra = [];
            for (const g of l.regions) if (g.area === "user")
                for (let a = g.start; a + 4096 <= g.end; a += 4096)
                    extra.push({ kind: K.SONG, num: 0x7E, sub: 0, ver: 1, len: 0, crc: 0, addr: a, deleted: true, label: "SONG 7E.0" });
            return Object.assign({}, l, { records: l.records.concat(extra) });
        };
        const pinned = { hello: async () => pinAll(await ses.hello()), write: (P, cb) => ses.write(P, cb), read: (a, n) => ses.read(a, n) };
        const w = F.planWrite(await pinned.hello(), st);
        check(!w.problems.length && w.plan.ok && w.plan.clean && w.plan.del.length === 0 && w.plan.write.length === M.toRecords(st.R).length,
            "the panel fits only the FM-1 emptied: the whole user area planned anew, every record of the panel's (" + w.plan.write.length + ")");
        res = await F.writeTo(pinned, st, {});
        rd = await F.readFrom(ses, empty());
        check(res.bad === 0 && res.plan.clean && sameRecs(M.toRecords(rd.items), M.toRecords(st.R)), "written anew: the FM-1 holds exactly the panel");
        st = Object.assign({}, st, { base: st.R, fm1: { from: "write", lastSave: res.lastSave } });

        /* the FM-1 holding what the panel lacks: named as removed */
        const gone = st.R.find(i => i.kind === K.SONG);
        const fewer = M.remove(st, "R", [gone.id], false).st;
        const wg = F.planWrite(await pinned.hello(), fewer);
        check(wg.plan.ok && wg.plan.clean && wg.plan.del.length === 1 && wg.plan.del[0].kind === K.SONG && wg.plan.del[0].num === gone.num,
            "written anew, what the panel lacks named as removed: " + F.removedNames(wg.plan, st).join(", "));

        /* a record the page cannot read, held by the FM-1: read first, written back byte for byte */
        const song = rd.items.find(i => i.kind === K.SONG);
        const odd = Object.assign({}, song, { id: M.newId(), dim: true, recs: [{ sub: 0, ver: 9, payload: Uint8Array.of(9, 9, 9) }] });
        const pat = rd.items.find(i => i.kind === K.PATTERN);
        const pp = R.unpackPattern(pat.recs.map(x => x.payload));
        pp.cells.set("2,0", { note: 63, ins: -1, vol: 0x20, cmds: [] });
        const pat4 = Object.assign({}, pat, { recs: R.packPattern(pp).map((payload, i) => ({ sub: i, ver: 1, payload })) });
        const stD = Object.assign({}, st, { R: rd.items.map(i => i.id === song.id ? odd : i.id === pat.id ? pat4 : i) });
        const wd = F.planWrite(await pinned.hello(), stD);
        check(wd.readHeld && wd.readHeld.length === 1 && wd.readHeld[0].kind === K.SONG && wd.readHeld[0].num === song.num,
            "written anew with a record the page cannot read: that one to read first");
        res = await F.writeTo(pinned, stD, {});
        const after = await F.readFrom(ses, empty());
        const songAfter = after.items.find(i => i.kind === K.SONG && i.num === song.num);
        const patAfter = after.items.find(i => i.kind === K.PATTERN && i.num === pat.num);
        check(res.bad === 0 && res.plan.clean && songAfter && sameRecs(M.toRecords([songAfter]), M.toRecords([song])) &&
            patAfter && sameRecs(M.toRecords([patAfter]), M.toRecords([pat4])),
            "written anew: the record the page cannot read back as it was, the changed pattern written");
        st = Object.assign({}, st, { R: after.items, base: after.items, fm1: { from: "write", lastSave: res.lastSave } });
    }

    /* ---- samples are records of their own: a sampler and its samples written, read back byte for byte ---- */
    {
        /* (an FM-1 of its own, so that this block reads only what it wrote) */
        const fw2 = await new ED.Firmware(wasm).start(null), ses2 = S.session(fw2.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
        const smpr = L.find(i => i.kind === K.SAMPLER);
        const mv = M.transfer(Object.assign(empty(), { L }), "L", [smpr.id], {});
        const sSt = Object.assign({}, mv.st, { R: mv.st.R });
        const nSmp = sSt.R.filter(i => i.kind === K.SAMPLE).length;
        const wS = F.planWrite(await ses2.hello(), sSt);
        const res2 = await F.writeTo(ses2, Object.assign({}, sSt, { fm1: { from: "empty", lastSave: null } }), {});
        const rd2 = await F.readFrom(ses2, empty());
        check(nSmp > 0 && res2.bad === 0 && !wS.rewritesSamples && sameRecs(M.toRecords(rd2.items), M.toRecords(sSt.R)) &&
            rd2.items.filter(i => i.kind === K.SAMPLE).length === nSmp && !rd2.conversion,
            "a sampler and its " + nSmp + " samples: written as records of kind 0x04 version 2 and 0x11, read back byte for byte, nothing converted");
        const linked = rd2.items.filter(i => i.kind === K.SAMPLER).every(i => M.refs(i).every(f => rd2.items.some(x => x.id === i.link[M.lk("m", f.num)] && x.num === f.num)));
        check(linked, "READ links every instrument to its sample's number");
        ses2.close();
    }

    /* ---- big, small, big: 12 songs written (what they use with them, samples among it), then one song, then the 12
     * again with the EDITOR'S PATTERN and the settings: the samples the small WRITE deleted leave their bytes in their
     * freed blocks, which the big one writes in part; that is no damaged record (specs/files/store.md#power-on), so every
     * mount counts none, and a READ lists every record ---- */
    {
        const fw3 = await new ED.Firmware(wasm).start(null), ses3 = S.session(fw3.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
        const songs = L.filter(i => i.kind === K.SONG), bads = [];
        let mv = M.transfer(Object.assign(empty(), { L }), "L", songs.slice(0, 12).map(i => i.id), {});
        let w = await F.writeTo(ses3, mv.st, {});
        bads.push(w.records + ":" + w.bad);
        mv = M.transfer(Object.assign(empty(), { L }), "L", [songs[0].id], {});
        w = await F.writeTo(ses3, mv.st, {});
        bads.push(w.records + ":" + w.bad);
        mv = M.transfer(Object.assign(empty(), { L }), "L", songs.slice(0, 12).map(i => i.id), {});
        const fe3 = { id: M.newId(), side: "R", kind: K.PATTERN, num: 0xFE, orig: 0xFE, link: { cF0: M.BUILTIN },
            recs: [{ sub: 0, ver: 1, payload: R.packPattern({ tracks: 2, rows: 8, step: 1, cells: new Map([["1,1", { note: 64, ins: 0xF000, vol: -1, cmds: [] }]]) })[0] }] };
        const set3 = { id: M.newId(), side: "R", kind: K.SETTINGS, num: 0, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: (() => { const b = new Uint8Array(133); b.set(ED.defs.newSettings()); b[2] = 3; return b; })() }] };
        const big = mv.st.R.concat([fe3, set3]);
        w = await F.writeTo(ses3, Object.assign({}, mv.st, { R: big }), {});
        bads.push(w.records + ":" + w.bad);
        const rd3 = await F.readFrom(ses3, empty());
        check(bads.every(x => x.endsWith(":0")) && sameRecs(M.toRecords(rd3.items), M.toRecords(big)),
            "big, small, big written: each mount counts nothing damaged, READ gives every record (records:bad " + bads.join(" ") + ")");
        ses3.close();
    }

    /* ---- an FM-1 holding the old form: READ converts as it loads; the first WRITE rewrites every sample, says so, and
     * backs the old records up; WRITE to an FM-1 that reads only the old form converts down, NOT KEPT named ---- */
    {
        const lay0 = await ses.hello();
        const d = S.parseData(demo());
        const sams = d.files.filter(f => f.kind === K.SAMPLER).slice(0, 3), smps = d.files.filter(f => f.kind === K.SAMPLE);
        const dn = R.downgrade(sams.concat(smps), 1);
        const oldRecs = dn.recs;
        /* a fake FM-1: the old records at blocks of their own, readable as the protocol reads them */
        const mem = new Map(), recs = [];
        let next = 0x93000;
        oldRecs.forEach(r => {
            const addr = next, b = new Uint8Array(16 + r.payload.length);
            next += Math.ceil(b.length / 4096) * 4096;
            b[0] = r.kind; b[1] = r.num; b[2] = r.sub; b[3] = r.ver;
            b[4] = r.payload.length & 255; b[5] = r.payload.length >> 8 & 255; b[6] = r.payload.length >> 16;
            b.set(r.payload, 16);
            const crc = S.crc32(b.subarray(16), S.crc32(b.subarray(0, 12)));
            b[12] = crc & 255; b[13] = crc >>> 8 & 255; b[14] = crc >>> 16 & 255; b[15] = crc >>> 24;
            mem.set(addr, b);
            recs.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, len: r.payload.length, crc: S.crc32(r.payload), addr, deleted: false, label: S.describe(r) });
        });
        const layOld = Object.assign({}, lay0, { records: recs });
        const fake = { hello: async () => layOld, read: async (a, n) => { for (const [at, b] of mem) if (a >= at && a < at + b.length) return b.slice(a - at, a - at + n); throw new Error("no record at " + a); } };
        const ro = await F.readFrom(fake, empty());
        const cv = ro.conversion;
        check(cv && cv.converted.length === 3 && cv.older.length === 0 && cv.samples === ro.items.filter(i => i.kind === K.SAMPLE).length && cv.samples > 0 &&
            ro.items.filter(i => i.kind === K.SAMPLER).every(i => !i.dim && i.recs[0].ver === 2) && !ro.items.some(i => i.kind === K.AUDIO),
            "READ of an FM-1 holding the old form: " + cv.converted.length + " samplers converted, " + cv.samples + " samples of their own (" + F.conversionText(cv).trim() + ")");
        check(ro.items.filter(i => i.kind === K.SAMPLER).map(i => i.num).join() === sams.map(s => s.num).join(), "the samplers keep their numbers");
        const stO = Object.assign(empty(), { R: ro.items, base: ro.items, fm1: ro.fm1 });
        check(M.pending(stO).count === 0, "(the panel and its base are the converted records: nothing pending by the page's count)");
        const wO = F.planWrite(layOld, stO);
        check(wO.problems.length === 0 && wO.plan.ok && !wO.plan.upToDate && wO.rewritesSamples === oldRecs.length && wO.plan.del.length === oldRecs.length - 3 &&
            wO.plan.write.length === M.toRecords(ro.items).length,
            "the first WRITE: every sample rewritten (" + wO.plan.write.length + " records), the old audio removed (" + wO.plan.del.length + "), the 3 old samplers replaced in place, and said (" + wO.rewritesSamples + " old records)");
        check(wO.backup.length === oldRecs.length && wO.seenGone === 0, "the old records are never 'seen already': all " + wO.backup.length + " in the BACKUP offered first");
        /* the old records loaded as a backup would: converted as they load */
        const loaded = M.fromRecords(oldRecs.map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: r.payload })), { side: "R" });
        check(loaded.conversion && loaded.conversion.converted.length === 3, "a backup of the old records converts as it loads too");
        /* the BACKUP offered first: the old records read from the FM-1, converted as they load, put on the TABLE with their samples */
        {
            let got = null, wrote = null;
            const hybrid = { hello: async () => layOld, read: fake.read, write: async P => { wrote = P; return { records: P.write.length, bad: 0, lastSave: 5 }; } };
            const rw = await F.writeTo(hybrid, stO, { backup: b => { got = b; } });
            check(rw.bad === 0 && wrote && wrote.write.length === M.toRecords(ro.items).length && got && got.read === oldRecs.length,
                "WRITE: the backup read before anything is written (" + (got && got.read) + " old records), then the rewrite sent");
            const tb = M.transfer({ L: [], R: got.items, base: [], fm1: {} }, "R", got.ids, { stamp: "BACKUP 4 NOV 05:00" });
            check(tb.ok && got.ids.length === 3 && tb.st.L.filter(i => i.kind === K.SAMPLER).length === 3 && tb.st.L.filter(i => i.kind === K.SAMPLE).length === ro.items.filter(i => i.kind === K.SAMPLE).length &&
                tb.st.L.every(i => i.src === "BACKUP 4 NOV 05:00"),
                "the backup lands on the TABLE: the 3 samplers with their " + tb.st.L.filter(i => i.kind === K.SAMPLE).length + " samples, stamped");
        }
        /* once written the old records are gone and the plan is up to date */
        const noOld = Object.assign({}, lay0);
        const wN = F.planWrite(noOld, Object.assign({}, stO, { base: [] }));
        check(!wN.rewritesSamples, "an FM-1 with no old records: no rewrite said");
        /* an FM-1 that reads the old form only: the samplers go down, NOT KEPT named */
        const onlyOld = Object.assign({}, lay0, { accepts: lay0.accepts.filter(a => a.kind !== K.SAMPLE && a.kind !== K.SAMPLER).concat([{ kind: K.SAMPLER, ver: 1 }, { kind: K.AUDIO, ver: 1 }]) });
        const mv = M.transfer(Object.assign(empty(), { L }), "L", L.filter(i => i.kind === K.SAMPLER).slice(0, 4).map(i => i.id).concat(L.filter(i => i.kind === K.SAMPLE).slice(-2).map(i => i.id)), {});
        const wD = F.planWrite(onlyOld, mv.st);
        const cvd = wD.converted.find(c => c.name === "SAMPLERS");
        const wf = wD.plan.write.filter(f => f.kind === K.SAMPLER || f.kind === K.AUDIO || f.kind === K.SAMPLE);
        check(!wD.problems.length && cvd && cvd.from === 2 && cvd.to === 1 && wf.every(f => f.kind !== K.SAMPLE && f.ver === 1) && wf.some(f => f.kind === K.AUDIO),
            "WRITE to an FM-1 that reads the old form: samplers down to version 1 with their audio, no sample record sent (" + wf.length + " records; " + JSON.stringify(cvd && cvd.lost) + ")");
        const names = M.toRecords(mv.st.R).filter(r => r.kind === K.SAMPLER).map(r => R.unpackSampler(r.payload));
        check(wf.filter(f => f.kind === K.SAMPLER).length === names.length, "each sampler an old sampler under its own number");
        /* NOT KEPT: a sample no instrument names (not sent), a sample two samplers name (a copy in each, counted twice) */
        {
            const aud = R.ptrEncode(Array.from({ length: 64 }, (_, i) => (i * 3) % 100 - 50), 8), mkS = (n, name) => ({ id: M.newId(), side: "R", kind: K.SAMPLE, num: n, orig: n, link: {},
                recs: [{ sub: 0, ver: 1, payload: R.packSample({ length: 64, rate: 8000, coding: 1, name }, aud) }] });
            const slot = n => Object.assign(ED.defs.newSlot(K.SAMPLER, "I"), { sample: n, start: 0, end: 64 });
            const S0 = mkS(0, "SHARED"), S1 = mkS(1, "ALONE"), A = { id: M.newId(), side: "R", kind: K.SAMPLER, num: 1, orig: 1, link: {},
                recs: [{ sub: 0, ver: 2, payload: R.packSampler({ name: "A", instruments: [slot(0)] }) }] };
            const B = Object.assign({}, A, { id: M.newId(), num: 2, recs: [{ sub: 0, ver: 2, payload: R.packSampler({ name: "B", instruments: [slot(0)] }) }] });
            for (const s of [A, B]) s.link[M.lk("m", 0)] = S0.id;
            const wK = F.planWrite(onlyOld, Object.assign(empty(), { R: [S0, S1, A, B] }));
            const cK = wK.converted.find(c => c.name === "SAMPLERS");
            const audio = wK.plan.write.filter(f => f.kind === K.AUDIO);
            check(cK && cK.lost.join("|") === "1 SAMPLE NO INSTRUMENT NAMES (NOT SENT)|1 SAMPLE SEVERAL SAMPLERS NAME (A COPY IN EACH)" && audio.length === 2 && audio.every(f => f.num !== undefined) &&
                !wK.plan.write.some(f => f.kind === K.SAMPLE),
                "NOT KEPT: the unnamed sample not sent, the shared sample a copy in each sampler: " + JSON.stringify(cK && cK.lost));
        }
        /* it fits only the FM-1 emptied: the whole user area written anew, the first WRITE's rewrite said */
        {
            const pin = l => {
                const extra = [];
                for (const g of l.regions) if (g.area === "user")
                    for (let a = g.start; a + 4096 <= g.end; a += 4096) extra.push({ kind: K.SONG, num: 0x7E, sub: 0, ver: 1, len: 0, crc: 0, addr: a, deleted: true, label: "SONG 7E.0" });
                return Object.assign({}, l, { records: l.records.concat(extra) });
            };
            const wC = F.planWrite(pin(layOld), stO);
            check(!wC.problems.length && wC.plan.ok && wC.plan.clean && wC.rewritesSamples === oldRecs.length && wC.plan.write.length === M.toRecords(ro.items).length,
                "the first WRITE of a converted panel that fits only the FM-1 emptied: the whole user area written anew, the rewrite still said (" + wC.rewritesSamples + " old records)");
        }
        const noSmp = Object.assign({}, lay0, { accepts: lay0.accepts.filter(a => a.kind !== K.SAMPLE && a.kind !== K.SAMPLER) });
        check(F.planWrite(noSmp, mv.st).problems.some(t => /DOES NOT TAKE (SAMPLER|SAMPLE)/.test(t)), "no version of the sampler it reads: refused as before");
    }

    /* ---- a backup put back exactly, FE included ---- */
    const fe = { id: M.newId(), side: "R", kind: K.PATTERN, num: 0xFE, orig: 0xFE, link: {},
        recs: [{ sub: 0, ver: 1, payload: R.packPattern({ tracks: 2, rows: 8, step: 1, cells: new Map([["1,1", { note: 64, ins: 0xF000, vol: -1, cmds: [] }]]) })[0] }] };
    fe.link = { cF0: M.BUILTIN };
    const settings = { id: M.newId(), side: "R", kind: K.SETTINGS, num: 0, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: (() => { const b = new Uint8Array(133); b.set(ED.defs.newSettings()); b[2] = 3; return b; })() }] };
    st = Object.assign({}, st, { R: st.R.concat([fe, settings]) });
    let feErr = null;
    try { res = await F.writeTo(ses, st, {}); } catch (e) { feErr = e; }
    check(!feErr && res.bad === 0, "the EDITOR'S PATTERN and the settings written" + (feErr ? ": " + feErr.message : "") + (res ? " " + JSON.stringify({ bad: res.bad, records: res.records }) : ""));
    rd = await F.readFrom(ses, empty());
    check(sameRecs(M.toRecords(rd.items), M.toRecords(st.R)), "read back with them");
    const disk = F.diskOf(rd.items);
    const back = F.diskItems(disk, "R", "BACKUP.PTFMDSK");
    check(sameRecs(M.toRecords(back.items), M.toRecords(rd.items)) && back.items.some(i => i.num === 0xFE && i.kind === K.PATTERN), "DOWNLOAD DISK then UPLOAD DISK: the same records, FE included");
    res = await F.writeTo(ses, Object.assign({}, st, { R: [] }), {});
    rd = await F.readFrom(ses, empty());
    check(rd.items.length === 0, "DELETE ALL written: the FM-1's user area empty");
    res = await F.writeTo(ses, Object.assign({}, st, { R: back.items }), {});
    rd = await F.readFrom(ses, empty());
    check(sameRecs(M.toRecords(rd.items), M.toRecords(back.items)), "the backup put back exactly");

    /* ---- a cartridge's number changed: WRITE names nothing as removed, READ brings it back byte for byte ---- */
    {
        const fw5 = await new ED.Firmware(wasm).start(null), ses5 = S.session(fw5.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
        const mv = M.transfer(Object.assign(empty(), { L }), "L", L.filter(i => i.kind === K.SONG).slice(0, 3).map(i => i.id).concat(L.filter(i => i.kind === K.FUSION).map(i => i.id)), {});
        const w5 = await F.writeTo(ses5, Object.assign({}, mv.st, { fm1: { from: "empty", lastSave: null } }), {});
        const s5 = Object.assign({}, mv.st, { base: mv.st.R, fm1: { from: "write", lastSave: w5.lastSave } });
        const fus = s5.R.find(i => i.kind === K.FUSION), smp = s5.R.find(i => i.kind === K.SAMPLER);
        const sw = M.renumber(s5, fus.id, smp.num);              /* (two kinds swapping their numbers) */
        check(sw.ok, "a sampler and FUSION swap their numbers: " + sw.msg);
        const pl = F.planWrite(await ses5.hello(), sw.st);
        check(pl.problems.length === 0 && pl.plan.ok && pl.plan.del.length > 0 && F.removedNames(pl.plan, sw.st).length === 0,
            "the plan: their old records go, and nothing is named as removed: " + F.removedNames(pl.plan, sw.st).join(", "));
        const w6 = await F.writeTo(ses5, sw.st, {});
        const rd5 = await F.readFrom(ses5, empty());
        check(w6.bad === 0 && sameRecs(M.toRecords(rd5.items), M.toRecords(sw.st.R)), "written and read back byte for byte, " + rd5.items.length + " items");
        const free = M.renumber(Object.assign({}, sw.st, { base: sw.st.R }), fus.id, 0x40);
        check(free.ok, "to a free number: " + free.msg);
        const w7 = await F.writeTo(ses5, Object.assign({}, free.st, { fm1: { from: "write", lastSave: w6.lastSave } }), {});
        const rd6 = await F.readFrom(ses5, empty());
        check(w7.bad === 0 && sameRecs(M.toRecords(rd6.items), M.toRecords(free.st.R)) && rd6.items.some(i => i.kind === K.FUSION && i.num === 0x40),
            "the FM-1 holds it at 40, what names it following");
        ses5.close();
    }

    ses.close();
    done("device_test");
    process.exit(0);
})().catch(e => { console.log("FAIL: " + e.stack); process.exit(1); });
