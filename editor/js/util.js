/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Small helpers the editor's scripts share: hex, sizes, the browser's storage, files in and out, the DOM. */
(function (ED) {
"use strict";

const hex2 = v => (v >>> 0).toString(16).toUpperCase().padStart(2, "0");
const kb = n => n >= 1024 * 100 ? Math.round(n / 1024) + " KB" : n >= 1024 ? (n / 1024).toFixed(1) + " KB" : n + " B";
const align4 = n => (n + 3) & ~3;
const recSize = len => 16 + align4(len);                 /* a record in the flash: its header and payload */
const crc32 = (b, c) => globalThis.PTFM_seed.crc32(b, c);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

/* the browser's storage: one database, a key-value store */
function idb(mode, fn) {
    return new Promise((resolve, reject) => {
        if (typeof indexedDB === "undefined") { resolve(null); return; }
        const rq = indexedDB.open("ptfm-editor", 1);
        rq.onupgradeneeded = () => rq.result.createObjectStore("kv");
        rq.onerror = () => resolve(null);
        rq.onsuccess = () => {
            const db = rq.result, tx = db.transaction("kv", mode), r = fn(tx.objectStore("kv"));
            tx.oncomplete = () => { db.close(); resolve(r && "result" in r ? r.result : null); };
            tx.onerror = () => { db.close(); reject(tx.error); };
        };
    });
}
const idbGet = key => idb("readonly", s => s.get(key)).catch(() => null);
const idbPut = (key, value) => idb("readwrite", s => s.put(value, key)).catch(() => null);

/* a file to save: the page offers it through a link it clicks (pages opened from the file system can) */
function saveFile(bytes, name, type) {
    const blob = new Blob([bytes], { type: type || "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
}
function pickFiles(accept, multiple) {
    return new Promise(resolve => {
        const i = document.createElement("input");
        i.type = "file";
        if (accept) i.accept = accept;
        i.multiple = !!multiple;
        i.onchange = () => resolve([...i.files]);
        i.click();
    });
}
const readFile = f => f.arrayBuffer().then(b => new Uint8Array(b));

/* the DOM: h("div.cls#id", {attrs, on: {click}}, children...) */
function h(tag, attrs, ...kids) {
    const m = /^([a-z0-9]+)((?:[.#][\w-]+)*)$/i.exec(tag);
    const el = document.createElement(m ? m[1] : tag);
    if (m && m[2]) for (const part of m[2].match(/[.#][\w-]+/g)) {
        if (part[0] === ".") el.classList.add(part.slice(1)); else el.id = part.slice(1);
    }
    if (attrs) for (const k in attrs) {
        const v = attrs[k];
        if (v == null || v === false) continue;
        if (k === "on") for (const e in v) el.addEventListener(e, v[e]);
        else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
        else if (k === "text") el.textContent = v;
        else if (k === "html") el.innerHTML = v;
        else if (k in el && typeof v !== "string") el[k] = v;
        else el.setAttribute(k, v === true ? "" : v);
    }
    for (const k of kids.flat()) if (k != null && k !== false) el.append(k instanceof Node ? k : String(k));
    return el;
}
const $ = id => document.getElementById(id);
function clear(el) { while (el.firstChild) el.firstChild.remove(); return el; }

/* a base64 text (the page's inlined data) to bytes */
function b64(s) {
    const bin = atob(s), b = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) b[i] = bin.charCodeAt(i);
    return b;
}

ED.util = { hex2, kb, align4, recSize, crc32, clamp, idbGet, idbPut, saveFile, pickFiles, readFile, h, $, clear, b64 };
})(globalThis.ED = globalThis.ED || {});
