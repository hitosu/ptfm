/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* READ and WRITE against the virtual FM-1 (specs/editor.md, The FM-1: READ, WRITE and disks; Tests): the firmware's
 * own seeding code in its WebAssembly, through seed.js's session as with a real FM-1 over MIDI. A song moved to the FM-1
 * panel and written; READ back byte for byte, then again with only the changed records read; the plan (what stays,
 * what goes); CHANGED (a save on the FM-1 meanwhile: planned again); SAVE OR REVERT FIRST; a version the FM-1 does not
 * take; built-ins it lacks; another PT/FM version; DELETE ALL written; a backup put back exactly, FE included.
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

    /* ---- a backup put back exactly, FE included ---- */
    const fe = { id: M.newId(), side: "R", kind: K.PATTERN, num: 0xFE, orig: 0xFE, link: {},
        recs: [{ sub: 0, ver: 1, payload: R.packPattern({ tracks: 2, rows: 8, step: 1, cells: new Map([["1,1", { note: 64, ins: 0xF000, vol: -1, cmds: [] }]]) })[0] }] };
    fe.link = { cF0: M.BUILTIN };
    const settings = { id: M.newId(), side: "R", kind: K.SETTINGS, num: 0, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: (() => { const b = new Uint8Array(133); b.set(ED.defs.newSettings()); b[2] = 3; return b; })() }] };
    st = Object.assign({}, st, { R: st.R.concat([fe, settings]) });
    let feErr = null;
    try { res = await F.writeTo(ses, st, {}); } catch (e) { feErr = e; }
    check(!feErr && res.bad === 0, "the EDITOR'S PATTERN and the settings written" + (feErr ? ": " + feErr.message : ""));
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

    ses.close();
    done("device_test");
    process.exit(0);
})().catch(e => { console.log("FAIL: " + e.stack); process.exit(1); });
