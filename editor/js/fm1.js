/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The FM-1 (specs/editor.md, The FM-1: READ, WRITE and disks): no connection kept. READ and WRITE each find the FM-1
 * among the computer's MIDI ports, talk the seeding protocol (fw/web/seed.js) for as long as they run, and let it go.
 * UPLOAD DISK puts a disk in the FM-1 panel, DOWNLOAD DISK saves the panel as one, REVERT puts back what it showed after
 * the last READ or WRITE.
 *   ED.fm1.read(), write(), uploadDisk(), downloadDisk(), revert()
 *   ED.fm1.readFrom(session, st) / planWrite(layout, st, heldBytes?) / writeTo(session, st, o): the same without the page
 *   (the tests)
 *   ED.fm1.connect: async () -> { session, close(), name }: Web MIDI, or the virtual FM-1 (the page's own firmware, its
 *   flash in this page only) when the page is opened with ?virtual, or what a test puts there */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, S = globalThis.PTFM_seed;
const HDR = 16, READ_MAX = 504;
const U = () => ED.ui, app = () => ED.app;
const pageVersion = () => (globalThis.PTFM_ED_WASM && PTFM_ED_WASM.version) || "DEV";

/* ---- finding the FM-1 ---- */
async function connectMidi() {
    if (typeof navigator === "undefined" || !navigator.requestMIDIAccess) throw new Error("THIS BROWSER HAS NO MIDI FOR WEB PAGES: OPEN THE PAGE IN CHROME OR EDGE");
    let access;
    try { access = await navigator.requestMIDIAccess({ sysex: true }); }
    catch (e) { throw new Error("MIDI WAS NOT ALLOWED: LET THE PAGE USE MIDI DEVICES (THE BROWSER'S PERMISSION) AND TRY AGAIN"); }
    const pick = list => {
        const all = [...list.values()].filter(p => /FM-1/i.test(p.name || "") && p.state !== "disconnected");
        return all.find(p => /PT\/FM/i.test(p.name)) || all[0] || null;
    };
    const input = pick(access.inputs), output = pick(access.outputs);
    if (!input || !output) throw new Error("NO FM-1 FOUND: CONNECT IT BY USB AND SWITCH IT ON (CLOSE OTHER PAGES OR PROGRAMS USING IT)");
    const session = S.session(S.midiTransport(input, output), { timeout: 6000 });
    return { session, name: "THE FM-1", close() { session.close(); try { input.close(); output.close(); } catch (e) { /* (closed already) */ } } };
}
let virtual = null;
async function connectVirtual() {
    if (!virtual) {
        const wasm = Uint8Array.from(atob(PTFM_ED_WASM.wasm), c => c.charCodeAt(0));
        virtual = await new ED.Firmware(await WebAssembly.compile(wasm)).start(null);
    }
    const session = S.session(virtual.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
    return { session, name: "THE VIRTUAL FM-1", close() { session.close(); }, fw: virtual };
}
const api = {
    connect: async () => (typeof location !== "undefined" && /[?&]virtual\b/.test(location.search) ? connectVirtual() : connectMidi())
};

/* why HELLO failed, in the status line's words */
function helloError(e) {
    if (e && e.code === S.RESULT.UNSAVED) return "THE FM-1 HAS EDITS NOT SAVED: SAVE OR REVERT FIRST ON THE FM-1";
    if (e && e.timeout) return "THE FM-1 DID NOT ANSWER AS PT/FM: IT RUNS ANOTHER FIRMWARE OR THE USB RESCUE (INSTALL PT/FM FIRST)";
    return (e && e.message ? e.message : String(e)).toUpperCase();
}
function otherVersion(layout) {
    const fw = (layout.fwText || "").trim(), pg = pageVersion();
    return fw && pg && fw !== pg ? "THE FM-1 RUNS PT/FM " + fw + ", THIS PAGE " + pg : null;
}

/* ---- READ ---- */
const crcCache = new WeakMap();
function crcOf(payload) { let c = crcCache.get(payload); if (c === undefined) { c = S.crc32(payload); crcCache.set(payload, c); } return c; }
/* the FM-1's records read back (only those not already known with the same CRC), as items of the FM-1 panel */
async function readFrom(session, st, onProgress) {
    const layout = await session.hello();
    const known = new Map();
    for (const r of M.toRecords(st.base || [])) known.set(r.kind + "." + r.num + "." + r.sub + "." + r.ver + "." + r.payload.length + "." + crcOf(r.payload), r.payload);
    const live = layout.records.filter(r => !r.deleted);
    const todo = live.filter(r => !known.has(r.kind + "." + r.num + "." + r.sub + "." + r.ver + "." + r.len + "." + r.crc));
    const total = todo.reduce((n, r) => n + HDR + r.len, 0);
    let done = 0;
    const recs = [];
    for (const r of live) {
        const k = r.kind + "." + r.num + "." + r.sub + "." + r.ver + "." + r.len + "." + r.crc;
        if (known.has(k)) { recs.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: known.get(k) }); continue; }
        recs.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: await readRecord(session, r, n => { done += n; if (onProgress) onProgress(done, total); }) });
    }
    const items = keepIds(M.fromRecords(recs, { side: "R" }).items, st.base || []);
    const fm1 = { from: "read", when: null, name: null, fw: (layout.fwText || "").trim() || null, lastSave: layout.lastSave,
        layout: slimLayout(layout), other: otherVersion(layout) };
    return { items, fm1, layout, read: todo.length, kept: live.length - todo.length };
}
/* one of the layout's records read back and checked (its header's CRC over the bytes); tick(n) after each piece */
async function readRecord(session, r, tick) {
    const rec = new Uint8Array(HDR + r.len);
    for (let o = 0; o < rec.length; o += READ_MAX) {
        const n = Math.min(READ_MAX, rec.length - o);
        rec.set(await session.read(r.addr + o, n), o);
        if (tick) tick(n);
    }
    const len = rec[4] | rec[5] << 8 | rec[6] << 16;
    const crc = (rec[12] | rec[13] << 8 | rec[14] << 16 | rec[15] << 24) >>> 0;
    if (rec[0] !== r.kind || rec[1] !== r.num || rec[2] !== r.sub || len !== r.len || S.crc32(rec.subarray(HDR), S.crc32(rec.subarray(0, 12))) !== crc)
        throw new Error(r.label + " READ BACK WRONG: TRY AGAIN");
    return rec.slice(HDR);
}
/* items equal to the panel's last ones keep their ids (open tabs, picks stay on them) */
function keepIds(items, base) {
    const key = (it, m) => it.kind + "." + (it.kind === K.AUDIO ? (m.get(it.owner) || {}).num + "." + it.idx : it.num);
    const bm = M.byIdMap(base), nm = M.byIdMap(items), old = new Map(base.map(b => [key(b, bm), b]));
    const ren = new Map();
    for (const it of items) { const b = old.get(key(it, nm)); if (b && !items.some(x => x.id === b.id)) ren.set(it.id, b.id); }
    if (!ren.size) return items;
    return items.map(it => {
        const c = Object.assign({}, it, { id: ren.get(it.id) || it.id });
        if (c.owner) c.owner = ren.get(c.owner) || c.owner;
        const link = {};
        for (const k in it.link) link[k] = ren.get(it.link[k]) || it.link[k];
        c.link = link;
        return c;
    });
}
function slimLayout(L) { return { regions: L.regions, reserve: L.reserve, builtins: L.builtins, accepts: L.accepts, fwText: L.fwText }; }

/* ---- WRITE ---- */
const recKey = r => r.kind << 16 | r.num << 8 | r.sub;
/* the plan: the panel's records the FM-1 should hold, planned by the uploader's own planner; what it says first.
 * heldBytes: the bytes of the FM-1's records the page cannot read (recKey.crc -> payload), which a whole rewrite
 * writes back as they are: read by writeTo when a plan asks for them (readHeld) */
function planWrite(layout, st, heldBytes) {
    const out = { problems: [], warnings: [], newer: false, plan: null, missingBuiltins: [], converted: [] };
    const items = st.R, dims = items.filter(i => i.dim);
    const recs = M.toRecords(items.filter(i => !i.dim), items);
    const fileOf = r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, len: r.payload.length, crc: crcOf(r.payload), payload: r.payload,
        label: S.describe(r) });
    let files = recs.map(fileOf);
    const acc = new Set(layout.accepts.map(a => a.kind << 8 | a.ver));
    /* an object of a version the FM-1 does not take, where it takes an older one this page goes down to: converted
     * (WRITE writes that), what the older version cannot hold named */
    const odd = new Map();
    for (const f of files) if (!acc.has(f.kind << 8 | f.ver)) { const k = f.kind + "." + f.num; if (!odd.has(k)) odd.set(k, []); odd.get(k).push(f); }
    for (const g of odd.values()) {
        const f0 = g[0], name = S.describe(f0).replace(/\.\d+$/, "");
        const to = layout.accepts.filter(a => a.kind === f0.kind && a.ver < f0.ver && R.canDowngrade(f0.kind, f0.ver, a.ver)).map(a => a.ver).sort((a, b) => b - a)[0];
        if (to == null) continue;
        let d;
        try { d = R.downgrade(g, to); }
        catch (e) { out.problems.push(name + " DOES NOT FIT VERSION " + to + ", THE ONE THE FM-1 READS: " + String(e.message || e).toUpperCase()); continue; }
        files = files.filter(f => !g.includes(f)).concat(d.recs.map(fileOf));
        out.converted.push({ name, from: f0.ver, to, lost: d.lost });
    }
    out.files = files;
    /* (kind, version) pairs the FM-1 does not take, each named */
    const bad = new Map();
    for (const f of files) if (!acc.has(f.kind << 8 | f.ver)) bad.set(f.kind << 8 | f.ver, (S.KIND_NAME[f.kind] || "KIND " + f.kind) + " VERSION " + f.ver);
    for (const t of bad.values()) out.problems.push("THE FM-1 DOES NOT TAKE " + t + ": UPDATE ITS FIRMWARE");
    /* built-ins the FM-1's layout does not list */
    const has = new Set(layout.builtins.map(b => M.numSpace(b.kind) + b.num));
    const users = new Map();
    for (const it of items) for (const f of M.refs(it)) {
        if (it.link[M.lk(f.sp, f.num)] !== M.BUILTIN || has.has(f.sp + f.num)) continue;
        if (!users.has(f.num)) users.set(f.num, []);
        users.get(f.num).push(it);
    }
    for (const [num, us] of users) out.missingBuiltins.push(M.label(us[0], items).split(" ")[0] + (us.length > 1 ? "S " : " ") +
        [...new Set(us.map(u => u.num != null ? M.H2(u.num) : ""))].join(", ") + " USE" + (us.length > 1 ? "" : "S") + " " + M.H2(num) + ", WHICH THE FM-1 DOES NOT HAVE");
    /* what the page cannot read (shown dim) is never written: where the FM-1 holds a record at its key, that record
     * stays exactly as it is (planned as kept, with the FM-1's own version, length and CRC); else nothing goes there */
    const dimKeys = new Set(M.toRecords(dims, items).map(recKey));
    const held = layout.records.filter(x => !x.deleted && dimKeys.has(recKey(x)));
    const keepAsIs = held.map(x => ({ kind: x.kind, num: x.num, sub: x.sub, ver: x.ver, len: x.len, crc: x.crc, payload: null, label: x.label }));
    /* (the FM-1's reserve not kept: planned to its last block; fewer free blocks than advised are said instead) */
    const lay = Object.assign({}, layout, { reserve: 0, accepts: layout.accepts.concat(held.map(x => ({ kind: x.kind, ver: x.ver }))) });
    let P = S.plan(lay, { files: files.concat(keepAsIs), problems: [] });
    /* it does not fit around what stays on the FM-1 (records kept where they are, blocks a deleted record's header still
     * holds) but fits the FM-1 emptied: the whole user area written anew (the uploader's planClean), every record of the
     * panel's, and what the page cannot read written back as the FM-1 holds it (its bytes read first: readHeld) */
    if (!P.ok) {
        const bytes = heldBytes || new Map(), bk = x => recKey(x) + "." + x.crc;
        const back = keepAsIs.map(f => Object.assign({}, f, { payload: bytes.get(bk(f)) || new Uint8Array(f.len) }));
        const C = S.planClean(lay, { files: files.concat(back), problems: [] });
        const unread = held.filter(x => !bytes.has(bk(x)));
        if (C.ok && unread.length) out.readHeld = unread;
        else if (C.ok) {
            /* (what the FM-1 holds and the panel does not: erased with the rest, named as removed) */
            const keys = new Set(C.write.map(recKey));
            C.del = layout.records.filter(x => !x.deleted && !keys.has(recKey(x)))
                .map(x => ({ kind: x.kind, num: x.num, sub: x.sub, ver: x.ver, len: 0, del: true, label: x.label }));
            P = C;
        }
    }
    if (P.ok && !P.upToDate) { const low = M.lowSpace(P.space.after, layout.reserve); if (low) out.warnings.push(low); }
    P.del = P.del.filter(d => !dimKeys.has(recKey(d)));
    if (P.write.some(f => !f.payload)) P.problems.push("a record this page cannot read would be written");    /* (never: kept as it is) */
    out.plan = P;
    if (!P.ok) for (const p of P.problems) if (!out.problems.includes(p)) out.problems.push(p.toUpperCase());
    out.newer = st.fm1.lastSave != null && layout.lastSave > st.fm1.lastSave;
    /* BACKUP's: what the FM-1 holds that the write replaces or removes (no record of the panel's at its key with its
     * CRC; one the page cannot read stays as it is), unless the last READ or WRITE saw it just so: the panel held it
     * then, and what was done with it since was chosen (a READ, DELETE ALL and a WRITE back nothing up). Only what
     * changed on the FM-1 since is backed up. */
    const stays = new Set(files.concat(keepAsIs).map(f => recKey(f) + "." + f.crc));
    const seen = new Set(M.toRecords(st.base || []).map(r => recKey(r) + "." + crcOf(r.payload)));
    const goes = layout.records.filter(x => !x.deleted && !stays.has(recKey(x) + "." + x.crc));
    out.backup = goes.filter(x => !seen.has(recKey(x) + "." + x.crc));
    out.seenGone = goes.length - out.backup.length;
    return out;
}
/* BACKUP before a WRITE: the FM-1's items now (its records: the panel's bytes where it holds the same, the rest read),
 * and which of them the write replaces or removes. -> { items (numbered as on the FM-1), ids, read } */
async function backupItems(session, layout, st, w, onProgress) {
    const need = new Set(w.backup.map(x => recKey(x) + "." + x.crc));
    const panel = new Map(w.files.map(f => [recKey(f) + "." + f.crc, f.payload]));     /* (as written: converted down too) */
    const total = w.backup.reduce((n, x) => n + HDR + x.len, 0);
    let done = 0;
    const recs = [];
    for (const x of layout.records) {
        if (x.deleted) continue;
        const k = recKey(x) + "." + x.crc;
        const payload = need.has(k) ? await readRecord(session, x, n => { done += n; if (onProgress) onProgress(done, total); }) : panel.get(k);
        if (payload) recs.push({ kind: x.kind, num: x.num, sub: x.sub, ver: x.ver, payload });   /* (else one the page cannot read: left) */
    }
    const items = M.fromRecords(recs, { side: "R" }).items, m = M.byIdMap(items);
    const at = it => it.kind === K.AUDIO ? K.AUDIO << 16 | (m.get(it.owner) || {}).num << 8 | it.idx : null;
    const ids = items.filter(it => w.backup.some(x => x.kind === K.AUDIO ? at(it) === recKey(x) : it.kind === x.kind && it.num === x.num)).map(it => it.id);
    return { items, ids, read: w.backup.length };
}
/* the FM-1's items WRITE removes, by name: from what the panel held after the last READ, else the records' own labels */
function removedNames(P, st) {
    const base = st.base || [], bm = M.byIdMap(base), out = new Set();
    for (const d of P.del) {
        const b = base.find(it => it.kind === d.kind && (it.kind === K.AUDIO ? (bm.get(it.owner) || {}).num === d.num && it.idx === d.sub : it.num === d.num));
        out.add(b ? M.label(b, base) : S.describe(d).replace(/\.\d+$/, ""));
    }
    return [...out];
}
/* the panel's items WRITE writes, by name */
function writtenNames(P, st) {
    const items = st.R, m = M.byIdMap(items), at = new Map();
    for (const it of items) {
        const num = it.kind === K.AUDIO ? (m.get(it.owner) || it).num : it.num;
        for (const r of it.recs) at.set(it.kind << 16 | num << 8 | (it.kind === K.AUDIO ? it.idx : r.sub), it);
    }
    const out = new Set();
    for (const f of P.write) { const it = at.get(recKey(f)); out.add(it ? M.label(it, items) : S.describe(f)); }
    return [...out];
}
async function writeTo(session, st, o) {
    o = o || {};
    for (let tries = 0; ; tries++) {
        const layout = await session.hello();
        let w = planWrite(layout, st);
        /* a whole rewrite keeping what the page cannot read: those records read first, then planned again */
        if (w.readHeld) {
            const bytes = new Map();
            for (const x of w.readHeld) bytes.set(recKey(x) + "." + x.crc, await readRecord(session, x));
            w = planWrite(layout, st, bytes);
        }
        if (layout.unsaved) w.problems.unshift("THE FM-1 HAS EDITS NOT SAVED: SAVE OR REVERT FIRST ON THE FM-1");
        if (w.problems.length) { const e = new Error(w.problems.join(" · ")); e.problems = w.problems; throw e; }
        if (w.plan.upToDate) return { upToDate: true, layout, lastSave: layout.lastSave };
        if (o.confirm && !(await o.confirm(w, layout))) return { cancelled: true };
        /* BACKUP: what it replaces or removes, read and handed over before anything is written */
        if (o.backup && w.backup.length) await o.backup(await backupItems(session, layout, st, w, o.onBackup));
        try {
            const r = await session.write(w.plan, o.onProgress);
            return Object.assign(r, { layout, plan: w.plan });
        } catch (e) {
            /* (the FM-1 saved meanwhile: planned again and asked again, its newer save said first) */
            if (e && e.code === S.RESULT.CHANGED && tries < 2) continue;
            throw e;
        }
    }
}

/* ---- the page's keys ---- */
let busyNow = false;
async function withFm1(what, fn) {
    if (busyNow) return;
    busyNow = true;
    const u = U();
    u.busy(what + ": LOOKING FOR THE FM-1", 0);
    let c = null;
    try {
        c = await api.connect();
        await fn(c);
    } catch (e) {
        u.status(e.problems ? what + " REFUSED: " + e.message : helloError(e), { err: true });
    } finally {
        if (c) c.close();
        busyNow = false;
    }
}
async function read() {
    if (ED.tabs && ED.tabs.unsavedOn("R")) { U().status("SAVE OR CLOSE THE FM-1 PANEL'S OPEN TABS FIRST: READ REPLACES WHAT THEY EDIT", { err: true }); return; }
    await withFm1("READ", async c => {
        const u = U(), a = app();
        const r = await readFrom(c.session, a.st, (d, t) => u.busy("READ: " + u.kb(d) + " OF " + u.kb(t), t ? d / t : 1));
        r.fm1.when = u.nowLabel();
        const st = Object.assign({}, a.st, { R: r.items, base: r.items, fm1: Object.assign({}, r.fm1, { baseInfo: Object.assign({}, r.fm1) }) });
        a.commit(st, "READ FROM " + c.name + ": " + u.plural(r.items.length, "ITEM") + " (" + r.read + " RECORDS READ, " + r.kept + " ALREADY KNOWN)" +
            (r.fm1.other ? " · " + r.fm1.other : ""), { ok: true });
    });
}
function confirmDialog(w, layout, st) {
    const u = U(), h = u.h;
    return new Promise(resolve => {
        const P = w.plan, gone = removedNames(P, st);
        const bytes = P.write.reduce((n, f) => n + f.len, 0);
        const body = h("div", null,
            w.newer ? h("p", { class: "box", style: "color:var(--lred)" }, "THE FM-1 SAVED SOMETHING AFTER THIS PANEL'S LAST READ OR WRITE: A WRITE NOW REPLACES IT. READ FIRST TO SEE IT.") : null,
            w.missingBuiltins.length ? h("div", null, h("div", { class: "lbl" }, "BUILT-INS THE FM-1 DOES NOT HAVE"), h("ul", null, w.missingBuiltins.map(t => h("li", { class: "warn" }, t)))) : null,
            otherVersion(layout) ? h("p", { class: "dim" }, otherVersion(layout)) : null,
            !P.upToDate && w.warnings.length ? h("ul", null, w.warnings.map(t => h("li", { class: "warn" }, t))) : null,
            !P.upToDate && w.converted.length ? h("div", null, h("div", { class: "lbl" }, "CONVERTED FOR THE FM-1'S VERSION"), h("ul", null, w.converted.map(c =>
                h("li", { class: c.lost.length ? "warn" : null }, c.name + ": VERSION " + c.from + " WRITTEN AS VERSION " + c.to +
                    (c.lost.length ? " · NOT KEPT: " + c.lost.join(", ") : " · NOTHING LOST"))))) : null,
            P.clean ? h("p", { class: "box" }, "THIS PANEL DOES NOT FIT AROUND WHAT STAYS ON THE FM-1, BUT IT FITS THE FM-1 EMPTIED: " +
                "THE WHOLE USER AREA IS WRITTEN ANEW, WHICH TAKES LONGER. AFTER IT THE FM-1 HOLDS EXACTLY THIS PANEL, AS AFTER ANY WRITE.") : null,
            P.upToDate ? h("p", null, "THE FM-1 ALREADY HOLDS EXACTLY THIS PANEL: NOTHING TO WRITE.") :
                h("p", null, "WRITES " + u.plural(P.write.length, "RECORD") + " (" + u.kb(bytes) + ") INTO " + u.plural(P.space.need, "BLOCK") + "; " +
                    (P.clean ? "THE OTHER " + u.plural(P.space.erase, "BLOCK") + " ERASED. " : P.keep.length + " ALREADY THERE STAY. ") + P.space.after + " BLOCKS FREE AFTER."),
            P.upToDate ? null : (names => h("div", null, h("div", { class: "lbl" }, "WRITTEN"), h("ul", null, names.slice(0, 24).map(t => h("li", null, t)),
                names.length > 24 ? h("li", null, "AND " + (names.length - 24) + " MORE") : null)))(writtenNames(P, st)),
            gone.length ? h("div", null, h("div", { class: "lbl" }, "REMOVED FROM THE FM-1"), h("ul", null, gone.map(t => h("li", { class: "rm" }, t)))) : null,
            !P.upToDate && w.backup.length ? h("p", null, "BACKUP FIRST: WHAT THIS WRITE REPLACES OR REMOVES ON THE FM-1 (" + u.plural(w.backup.length, "RECORD") +
                ") IS READ AND PUT ON THE TABLE, WITH WHAT IT USES; WHAT THE TABLE HOLDS ALREADY IS NOT COPIED AGAIN." +
                (w.seenGone ? " THE REST IT REPLACES OR REMOVES IS AS THE LAST READ OR WRITE LEFT IT: NOT BACKED UP." : "")) :
            !P.upToDate && w.seenGone ? h("p", { class: "dim" }, "NO BACKUP: WHAT THIS WRITE REPLACES OR REMOVES ON THE FM-1 IS AS THE LAST READ OR WRITE LEFT IT.") : null,
            P.upToDate ? null : h("p", { class: "dim" }, "THE FM-1'S SOUND STOPS WHILE IT WRITES. KEEP IT CONNECTED."));
        const buttons = P.upToDate ? [{ label: "OK", cls: "primary", onClick: () => resolve(false) }] :
            w.newer ? [{ label: "READ FIRST", cls: "primary", onClick: () => { resolve(false); setTimeout(read, 0); } }, { label: "WRITE ANYWAY", cls: "danger", onClick: () => resolve(true) }, { label: "CANCEL", onClick: () => resolve(false) }] :
            w.missingBuiltins.length || w.converted.some(c => c.lost.length) || w.warnings.length ? [{ label: "WRITE ANYWAY", cls: "primary danger", onClick: () => resolve(true) }, { label: "CANCEL", onClick: () => resolve(false) }] :
            [{ label: "WRITE", cls: "primary", onClick: () => resolve(true) }, { label: "CANCEL", onClick: () => resolve(false) }];
        u.dialog({ title: P.upToDate ? "NOTHING TO WRITE" : "WRITE TO THE FM-1?", body, buttons, onCancel: () => resolve(false) });
    });
}
async function write() {
    if (ED.tabs && ED.tabs.unsavedOn("R")) { U().status("SAVE OR CLOSE THE FM-1 PANEL'S OPEN TABS FIRST: WRITE SENDS WHAT IS SAVED", { err: true }); return; }
    await withFm1("WRITE", async c => {
        const u = U(), a = app(), st = a.st;
        let backedUp = "";
        const r = await writeTo(c.session, st, { confirm: (w, layout) => { u.busy("WRITE: WAITING FOR YOUR ANSWER", 0); return confirmDialog(w, layout, st); },
            onProgress: p => u.busy("WRITE: BLOCK " + p.done + " OF " + p.total, p.done / p.total),
            onBackup: (d, t) => u.busy("WRITE: BACKUP, " + u.kb(d) + " OF " + u.kb(t), t ? d / t : 1),
            backup: b => { backedUp = backup(b); } });
        if (r.cancelled) { u.status("NOTHING WRITTEN"); return; }
        const fm1 = { from: "write", when: u.nowLabel(), name: null, fw: (r.layout.fwText || "").trim() || null, lastSave: r.lastSave,
            layout: slimLayout(r.layout), other: otherVersion(r.layout) };
        fm1.baseInfo = Object.assign({}, fm1);
        /* not a step to UNDO: the FM-1 has changed; the history goes on from here. What the FM-1 holds now is what
         * was written (st, taken at the start), whatever changed on the page meanwhile */
        a.set(Object.assign({}, a.st, { base: st.R, fm1 }));
        u.status(r.upToDate ? "NOTHING TO WRITE: THE FM-1 ALREADY HOLDS EXACTLY THIS PANEL" : "WRITTEN TO " + c.name + ": " + u.plural(r.plan.write.length, "RECORD") + ", " +
            u.plural(r.plan.del.length, "REMOVED", "REMOVED") + (r.plan.clean ? " · THE WHOLE USER AREA WRITTEN ANEW" : "") + backedUp + (r.bad ? " · " + r.bad + " BAD RECORDS: WRITE AGAIN" : ""), { ok: !r.bad, err: !!r.bad });
    });
}
/* BACKUP: the FM-1's items a WRITE replaces or removes, with what they use, onto the TABLE as a move would take them:
 * what the TABLE holds already (the same content) is not copied again, only referred to. Its own step (UNDO takes it
 * back); a backup that cannot be made stops the WRITE before anything is written. -> the status line's words */
function backup(b) {
    const u = U(), a = app(), stamp = "BACKUP " + u.nowLabel();
    const r = M.transfer({ L: a.st.L, R: b.items, base: [], fm1: {} }, "R", b.ids, { stamp });
    if (!r.ok) throw new Error("THE BACKUP COULD NOT BE MADE, NOTHING WRITTEN: " + r.msg);
    const added = r.st.L.filter(it => !a.st.L.some(x => x.id === it.id));
    if (!added.length) return " · BACKUP: THE TABLE HOLDS ALL OF IT ALREADY";
    a.commit(Object.assign({}, a.st, { L: r.st.L }), stamp + " ON THE TABLE: " + u.plural(added.length, "ITEM") + " THE WRITE REPLACES OR REMOVES, WITH WHAT THEY USE");
    return " · " + stamp + ": " + u.plural(added.length, "ITEM") + " ON THE TABLE";
}

/* ---- disks ---- */
function pickDisk() {
    return new Promise(resolve => {
        const i = document.getElementById("diskpick");
        i.value = "";
        i.onchange = () => resolve(i.files[0] || null);
        i.click();
    });
}
/* a disk's items; a file whose CRC does not match its bytes is left out (damaged), and said */
function diskItems(bytes, side, src) {
    const d = S.parseData(bytes);
    if (!d.kind) throw new Error("NOT A PT/FM DISK");
    const good = d.files.filter(f => S.crc32(f.payload) === f.crc);
    const r = M.fromRecords(good, { side, src });
    return { items: r.items, skipped: r.skipped, problems: d.problems, damaged: d.files.length - good.length };
}
/* UPLOAD DISK: a disk picked, or the one dropped on the FM-1 panel (file) */
async function uploadDisk(file) {
    const u = U(), a = app();
    const f = file || await pickDisk();
    if (!f) return;
    try {
        if (ED.tabs && ED.tabs.unsavedOn("R")) throw new Error("SAVE OR CLOSE THE FM-1 PANEL'S OPEN TABS FIRST: THE DISK TAKES THE PANEL'S PLACE");
        const r = diskItems(new Uint8Array(await f.arrayBuffer()), "R", f.name.toUpperCase());
        const dims = r.items.filter(i => i.dim).length;
        const st = Object.assign({}, a.st, { R: r.items, fm1: Object.assign({}, a.st.fm1, { from: "disk", name: f.name.toUpperCase() }) });
        a.commit(st, "UPLOAD DISK: " + f.name.toUpperCase() + " IN THE FM-1 PANEL, " + u.plural(r.items.length, "ITEM") +
            (dims ? " (" + dims + " OF A VERSION THIS PAGE CANNOT READ: KEPT, NOT WRITTEN)" : "") +
            (r.damaged ? " · " + u.plural(r.damaged, "DAMAGED RECORD") + " LEFT OUT" : "") + " · NOTHING CHANGES ON THE FM-1 UNTIL WRITE", { ok: !r.damaged, err: !!r.damaged });
    } catch (e) { u.status("UPLOAD DISK: " + (e.message || e).toUpperCase(), { err: true }); }
}
function diskOf(items) {
    return S.makeDisk(M.toRecords(items).map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: r.payload })));
}
function downloadDisk() {
    const u = U(), a = app();
    const d = new Date(), name = "ptfm-" + d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0") + ".ptfmdsk";
    const bytes = diskOf(a.st.R);
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = name;
    document.body.append(link);
    link.click();
    setTimeout(() => { URL.revokeObjectURL(link.href); link.remove(); }, 2000);
    u.status("DOWNLOAD DISK: " + name + ", " + u.plural(a.st.R.length, "ITEM") + ", " + u.kb(bytes.length), { ok: true });
}
function revert() {
    const u = U(), a = app();
    if (!M.pending(a.st).count) { u.status("NOTHING TO REVERT: THE PANEL IS AS AFTER THE LAST READ OR WRITE"); return; }
    const info = a.st.fm1.baseInfo || app().emptyFm1();
    a.commit(Object.assign({}, a.st, { R: a.st.base, fm1: Object.assign({}, info, { baseInfo: info }) }), "REVERTED: THE FM-1 PANEL AS AFTER THE LAST READ OR WRITE");
}

Object.assign(api, { read, write, uploadDisk, downloadDisk, revert, readFrom, planWrite, writeTo, removedNames, diskItems, diskOf,
    otherVersion, helloError, connectVirtual });
ED.fm1 = api;
})(globalThis.ED = globalThis.ED || {});
