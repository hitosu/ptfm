/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Kept in the browser (specs/editor.md, Kept in the browser): both panels and the page's own data beside them in
 * IndexedDB, written a moment after each change; one page at a time (the Web Locks API: the page opened again while
 * it is open elsewhere waits, and says so, until the other one is closed).
 *   ED.keep.load() -> the saved state or null; ED.keep.save(state) (soon after); ED.keep.lock(onWait) -> Promise once
 *   this page is the one */
(function (ED) {
"use strict";
const DB = "ptfm-editor-beta", STORE = "kv", KEY = "state";

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
function run(mode, fn) {
    return open().then(db => new Promise(resolve => {
        if (!db) { resolve(null); return; }
        let out = null;
        try {
            const tx = db.transaction(STORE, mode), rq = fn(tx.objectStore(STORE));
            tx.oncomplete = () => { db.close(); resolve(rq && "result" in rq ? rq.result : out); };
            tx.onerror = tx.onabort = () => { db.close(); resolve(null); };
        } catch (e) { db.close(); resolve(null); }
    }));
}

let timer = 0, pending = null, saving = Promise.resolve();
function save(st) {
    pending = st;
    clearTimeout(timer);
    timer = setTimeout(flush, 400);
}
function flush() {
    if (!pending) return saving;
    const st = pending;
    pending = null;
    saving = saving.then(() => run("readwrite", s => s.put(st, KEY))).then(r => {
        if (r == null && typeof indexedDB !== "undefined" && api.onError) api.onError();     /* (full, or storage off) */
        return r;
    });
    return saving;
}
const load = () => run("readonly", s => s.get(KEY));

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
const api = { load, save, flush, lock, onError: null };
ED.keep = api;
})(globalThis.ED = globalThis.ED || {});
