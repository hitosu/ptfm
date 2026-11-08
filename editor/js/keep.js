/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Kept in the browser (specs/editor.md, Kept in the browser): both panels and the page's own data beside them in
 * IndexedDB, written a moment after each change; the originals (a sound's own audio, from a drop or a take) in a store
 * of their own beside them, each under an id of its own, written once and dropped once no item names it; one page at a
 * time (the Web Locks API: the page opened again while it is open elsewhere waits, and says so, until the other one is
 * closed).
 *   ED.keep.load() -> the saved state or null (the originals put back); ED.keep.save(state) (soon after);
 *   ED.keep.original(ch) -> Promise of true once that original is kept (false: the browser could not keep it);
 *   ED.keep.lock(onWait) -> Promise once this page is the one */
(function (ED) {
"use strict";
const DB = "ptfm-editor-beta", STORE = "kv", KEY = "state", ORIG = "orig:", SIDES = ["L", "R", "base"];

function open() {
    return new Promise(resolve => {
        if (typeof indexedDB === "undefined") { resolve(null); return; }
        let rq;
        try { rq = indexedDB.open(DB, 1); } catch (e) { resolve(null); return; }
        rq.onupgradeneeded = () => rq.result.createObjectStore(STORE);
        rq.onerror = () => resolve(null);
        rq.onsuccess = () => resolve(rq.result);
    });
}
/* fn(store) -> the requests whose results come back, in a list (one request: its result alone) */
function run(mode, fn) {
    return open().then(db => new Promise(resolve => {
        if (!db) { resolve(null); return; }
        try {
            const tx = db.transaction(STORE, mode), rq = fn(tx.objectStore(STORE));
            tx.oncomplete = () => { db.close(); resolve(Array.isArray(rq) ? rq.map(q => q.result) : rq && "result" in rq ? rq.result : null); };
            tx.onerror = tx.onabort = () => { db.close(); resolve(null); };
        } catch (e) { db.close(); resolve(null); }
    }));
}

/* the originals: a sound's channels (the array of them) -> its id; the ids the store holds; those original() wrote,
 * kept until a state names them (a save of a state from before cannot drop them; one never named goes after a reload) */
const ids = new WeakMap();
let stored = new Set(), fresh = new Set(), idN = 0;
function idOf(ch) {
    let id = ids.get(ch);
    if (!id) { id = Date.now().toString(36) + "-" + (idN++).toString(36); ids.set(ch, id); }
    return id;
}
/* the state as kept: each item's original named by its id, its channels apart */
function split(st) {
    const out = Object.assign({}, st), need = new Map();
    for (const side of SIDES) if (Array.isArray(st[side])) out[side] = st[side].map(it => {
        const s = it.sound;
        if (!s || !s.ch) return it;
        const id = idOf(s.ch), s2 = Object.assign({}, s);
        need.set(id, s.ch);
        delete s2.ch;
        s2.oid = id;
        return Object.assign({}, it, { sound: s2 });
    });
    return { out, need };
}
/* an original an older page kept stereo: one channel now, as its CHANNELS had it (the left, the right, or the two
 * mixed: the sample made from it sounds the same), since PT/FM's samples are one channel */
function monoCh(ch, chan) {
    if (chan === "l") return ch[0];
    if (chan === "r") return ch[1];
    const n = ch[0].length, out = new Float32Array(n), k = 1 / ch.length;
    for (const c of ch) for (let i = 0; i < n; i++) out[i] += c[i] * k;
    return out;
}
/* the originals put back into the items naming them (one lost: the sample as its record has it; a sample too long,
 * which has no record, goes with it); a stereo one made one channel, under a new id (written, and the stereo one
 * dropped, at the next change kept) */
function join(saved, keys, vals) {
    const by = new Map(), mixed = new Map();
    (keys || []).forEach((k, i) => by.set(String(k).slice(ORIG.length), vals[i]));
    stored = new Set(by.keys());
    if (!saved || !saved.st) return saved || null;
    for (const side of SIDES) {
        if (!Array.isArray(saved.st[side])) continue;
        for (const it of saved.st[side]) {
            const s = it.sound;
            if (!s || s.ch || !s.oid) continue;
            const o = by.get(s.oid);
            if (o && o.ch && o.ch.length > 1) {
                const chan = s.cut && s.cut.chan || "mix", key = s.oid + ":" + chan;
                if (!mixed.has(key)) mixed.set(key, [monoCh(o.ch, chan)]);
                s.ch = mixed.get(key);
                if (s.cut) s.cut = Object.assign({}, s.cut, { chan: "mix" });
                delete s.oid;
            } else if (o && o.ch) { s.ch = o.ch; ids.set(o.ch, s.oid); delete s.oid; }
            else delete it.sound;
        }
        saved.st[side] = saved.st[side].filter(it => !(it.long > 0 && !it.sound));
    }
    return saved;
}

let timer = 0, pending = null, saving = Promise.resolve();
function save(st) {
    pending = st;
    clearTimeout(timer);
    timer = setTimeout(flush, 400);
}
/* one transaction: the originals not yet kept, the state, the originals no longer named dropped */
function flush() {
    if (!pending) return saving;
    const st = pending;
    pending = null;
    saving = saving.then(() => {
        const { out, need } = split(st.st || {});
        const rec = Object.assign({}, st, { st: out });
        return run("readwrite", s => {
            for (const [id, ch] of need) if (!stored.has(id)) s.put({ ch }, ORIG + id);
            for (const id of stored) if (!need.has(id) && !fresh.has(id)) s.delete(ORIG + id);
            return s.put(rec, KEY);
        }).then(r => {
            if (r != null) {
                for (const id of need.keys()) fresh.delete(id);
                stored = new Set([...need.keys(), ...fresh]);
            }
            return r;
        });
    }).then(r => {
        if (r == null && typeof indexedDB !== "undefined" && api.onError) api.onError();     /* (full, or storage off) */
        return r;
    });
    return saving;
}
/* an original written now (a take's SAVE writes it first) */
function original(ch) {
    const id = idOf(ch);
    if (stored.has(id)) return Promise.resolve(true);
    saving = saving.then(() => run("readwrite", s => s.put({ ch }, ORIG + id))).then(r => {
        if (r == null) return false;
        stored.add(id);
        fresh.add(id);
        return true;
    });
    return saving;
}
/* what an older page kept: its samplers and their samples in the old form, and its loose samples, converted once as the
 * state loads (the next save keeps the new form; the TABLE, the FM-1 panel and its base each in their own frame) ->
 * the state, `.converted` true when anything changed */
function upgrade(saved) {
    if (!saved || !saved.st || !ED.model || !ED.model.upgradeKept) return saved;
    const isOldForm = i => i.kind === 0x10 || (i.kind === 0x04 && !i.older && i.recs && i.recs.length && i.recs[0].ver === 1);
    for (const side of SIDES) {
        if (!Array.isArray(saved.st[side])) continue;
        const was = saved.st[side];
        let next;
        /* the base is what the FM-1 panel was at its last READ or WRITE: when its old-form items are the panel's own, the
         * very same objects, it takes the panel's conversion (the same samples, the same numbers), so an unwritten edit
         * is the only difference between the two and the pending changes are only that */
        if (side === "base" && saved.converted && Array.isArray(saved.st.R) && saved.old && was.some(isOldForm) &&
            was.filter(isOldForm).every(i => saved.old.R.includes(i))) {
            const conv = new Map(saved.st.R.map(i => [i.id, i])), orig = new Set(saved.old.R.map(i => i.id));
            next = was.filter(i => !(isOldForm(i) && !conv.has(i.id))).map(i => (isOldForm(i) && conv.get(i.id)) || i)
                .concat(saved.st.R.filter(i => !orig.has(i.id) && i.kind === 0x11 && !was.some(x => x.id === i.id)));
        } else next = ED.model.upgradeKept(was, side === "L" ? "L" : "R");
        if (side === "R") saved.old = { R: was };
        if (next !== was) { saved.st[side] = next; saved.converted = true; }
    }
    delete saved.old;
    return saved;
}
function load() {
    if (typeof IDBKeyRange === "undefined") return Promise.resolve(null);
    const range = IDBKeyRange.bound(ORIG, ORIG + String.fromCharCode(0xffff));
    return run("readonly", s => [s.get(KEY), s.getAllKeys(range), s.getAll(range)])
        .then(r => r ? upgrade(join(r[0], r[1], r[2])) : null);
}

function lock(onWait) {
    const L = typeof navigator !== "undefined" && navigator.locks;
    if (!L) return Promise.resolve(true);
    return new Promise(resolve => {
        L.request(DB, { ifAvailable: true }, lk => {
            if (lk) { resolve(true); return new Promise(() => {}); }
            if (onWait) onWait();
            L.request(DB, () => { resolve(true); return new Promise(() => {}); });
            return null;
        });
    });
}

if (typeof addEventListener !== "undefined") addEventListener("pagehide", () => { flush(); });
const api = { load, save, flush, original, lock, onError: null, join, upgrade /* (the tests) */ };
ED.keep = api;
})(globalThis.ED = globalThis.ED || {});
