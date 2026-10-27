/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The page's frame (specs/editor.md, The page): its state and UNDO, the header's tabs, the status line, dialogs,
 * menus, the kinds' icons and colours. The panels are files.js's, the tabs tabs.js's.
 *   ED.app.st          { L, R, base (the FM-1 panel after the last READ or WRITE), fm1 { from, when, name, fw, lastSave,
 *                      layout } }: replaced, never changed in place
 *   ED.app.commit(st, what, o)   a step to UNDO: the state replaced, kept in the browser, the page drawn again
 *   ED.ui.h, status, busy, toast, dialog, closeDialog, menu, icon, colourOf, kb, plural, nowLabel */
(function (ED) {
"use strict";
const M = ED.model, K = ED.rec.KIND;
const $ = (s, r) => (r || document).querySelector(s);
const PROPS = { value: 1, checked: 1, disabled: 1, selected: 1, hidden: 1, draggable: 1, tabIndex: 1, title: 1, min: 1, max: 1,
    step: 1, type: 1, placeholder: 1, maxLength: 1, id: 1, multiple: 1, accept: 1, width: 1, height: 1 };
/* h("div", { class, style, onclick, ... }, children...) */
function h(tag, props, ...kids) {
    const e = document.createElement(tag);
    if (props) for (const k of Object.keys(props)) {
        const v = props[k];
        if (v == null || v === false) continue;
        if (k === "class") e.className = v;
        else if (k === "style") { if (typeof v === "string") e.style.cssText = v; else Object.assign(e.style, v); }
        else if (k === "html") e.innerHTML = v;
        else if (k.startsWith("on") && typeof v === "function") e.addEventListener(k.slice(2), v);
        else if (k in PROPS) e[k] = v;
        else e.setAttribute(k, v === true ? "" : v);
    }
    for (const c of kids.flat(Infinity)) if (c != null && c !== false) e.append(c instanceof Node ? c : String(c));
    return e;
}
function kb(b) { if (b < 1000) return b + " B"; if (b < 1048576) return (b / 1024).toFixed(b < 10240 ? 1 : 0) + " KB"; return (b / 1048576).toFixed(2) + " MB"; }
const plural = (n, one, many) => n + " " + (n === 1 ? one : many || one + "S");
const MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"];
function nowLabel() {
    const d = new Date();
    return d.getDate() + " " + MONTHS[d.getMonth()] + " " + String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const uniq = a => [...new Set(a.filter(x => x != null))];

/* ---- colours (specs/ui/colours.md: the engines') and icons ---- */
const COL = { white: "#F4F4EC", lgrey: "#B4B4AC", dgrey: "#5C5C5C", yellow: "#F0E074", lblue: "#7C8CF8", lgreen: "#84E884",
    lcyan: "#84E8E8", cyan: "#3AA8A8", lred: "#F87C74", lmagenta: "#E884E8", brown: "#C87C34", blueD: "#161B3E" };
const ENG_COL = { [K.DX7]: COL.lred, [K.JUNO]: COL.lcyan, [K.TB303]: COL.lgreen, [K.CHIP]: COL.lblue, [K.FUSION]: COL.lmagenta,
    [K.SAMPLER]: COL.yellow };
function colourOf(it) {
    if (it.isDefault) return COL.dgrey;
    if (ENG_COL[it.kind]) return ENG_COL[it.kind];
    if (it.kind === K.AUDIO) return COL.yellow;
    if (it.kind === K.SONG) return it.side === "R" && it.num === M.FE ? COL.lcyan : COL.white;
    if (it.kind === K.PATTERN) return it.side === "R" && it.num === M.FE ? COL.yellow : COL.lgrey;
    return COL.lcyan;
}
function icon(it, big) {
    const c = colourOf(it), w = big ? 2 : 1.5;
    const fe = it.side === "R" && it.num === M.FE;
    const k = it.kind === K.SAMPLER ? "sampler" : M.isCart(it.kind) ? "cart" : it.kind === K.AUDIO ? "sample" :
        it.kind === K.SONG ? (fe ? "keysong" : "song") : it.kind === K.PATTERN ? "pattern" :
        { [K.SETTINGS]: "settings", [K.MIDIMAP]: "midimap", [K.LOOPS]: "loops", [K.DJ]: "dj" }[it.kind];
    switch (k) {
        case "song": return `<svg viewBox="0 0 16 16"><rect x="2" y="3" width="12" height="2" fill="${c}"/><rect x="2" y="7" width="8" height="2" fill="${c}"/><rect x="2" y="11" width="10" height="2" fill="${c}"/></svg>`;
        case "pattern": return `<svg viewBox="0 0 16 16"><g fill="${c}">${[3, 7, 11].map(y => [2, 7, 12].map(x => `<rect x="${x}" y="${y}" width="3" height="2"/>`).join("")).join("")}</g></svg>`;
        case "cart": return `<svg viewBox="0 0 16 16"><path d="M2 2.5h12v9h-2v2H4v-2H2z" fill="none" stroke="${c}" stroke-width="${w}"/><rect x="5" y="5" width="6" height="3" fill="${c}"/></svg>`;
        case "sampler": return `<svg viewBox="0 0 16 16"><path d="M2 2.5h12v9h-2v2H4v-2H2z" fill="none" stroke="${c}" stroke-width="${w}"/><path d="M4 7h1.5l1-2.5 1.5 5 1.5-4 1 1.5H12" fill="none" stroke="${c}" stroke-width="1.2"/></svg>`;
        case "sample": return `<svg viewBox="0 0 16 16"><path d="M1 8h2l1.5-5 2 10 2-8 1.5 5 1.5-3 1 1h2.5" fill="none" stroke="${c}" stroke-width="${w}"/></svg>`;
        case "settings": return `<svg viewBox="0 0 16 16"><g fill="${c}"><rect x="3" y="2" width="1.5" height="12"/><rect x="7.25" y="2" width="1.5" height="12"/><rect x="11.5" y="2" width="1.5" height="12"/><rect x="1.5" y="9" width="4.5" height="2.5"/><rect x="5.75" y="4" width="4.5" height="2.5"/><rect x="10" y="7" width="4.5" height="2.5"/></g></svg>`;
        case "midimap": return `<svg viewBox="0 0 16 16"><circle cx="8" cy="8" r="6" fill="none" stroke="${c}" stroke-width="1.5"/><g fill="${c}"><rect x="4" y="7" width="2" height="2"/><rect x="10" y="7" width="2" height="2"/><rect x="5" y="4.5" width="2" height="2"/><rect x="9" y="4.5" width="2" height="2"/><rect x="7" y="3.5" width="2" height="2"/></g></svg>`;
        case "loops": return `<svg viewBox="0 0 16 16"><path d="M13 8a5 5 0 1 1-2-4" fill="none" stroke="${c}" stroke-width="1.5"/><path d="M9.5 2.5h3.5V6" fill="none" stroke="${c}" stroke-width="1.5"/></svg>`;
        case "dj": return `<svg viewBox="0 0 16 16"><circle cx="4.5" cy="8" r="3.2" fill="none" stroke="${c}" stroke-width="1.5"/><circle cx="11.5" cy="8" r="3.2" fill="none" stroke="${c}" stroke-width="1.5"/><rect x="4" y="7.5" width="1" height="1" fill="${c}"/><rect x="11" y="7.5" width="1" height="1" fill="${c}"/></svg>`;
        case "keysong": return `<svg viewBox="0 0 16 16"><g fill="${c}"><rect x="2" y="3" width="2" height="10"/><rect x="5" y="3" width="2" height="10"/><rect x="8" y="3" width="2" height="10"/><rect x="11" y="3" width="2" height="10"/></g></svg>`;
    }
    return "";
}

/* ---- the status line, toasts, dialogs, menus ---- */
function status(msg, o) {
    o = o || {};
    const st = $("#status");
    st.className = "status" + (o.err ? " err" : "") + (o.ok ? " ok" : "");
    st.textContent = "";
    st.append(h("span", { class: "smsg", title: msg }, msg));
    if (o.undo) st.append(h("button", { class: "linkbtn", onclick: () => app.undo() }, "UNDO"));
}
function busy(msg, frac) {
    const st = $("#status");
    st.className = "status";
    st.textContent = "";
    st.append(h("div", { class: "busybar" }, h("i", { style: { width: (100 * clamp(frac, 0, 1)) + "%" } })), h("span", { class: "smsg" }, msg));
}
let toastT = 0;
function toast(msg) {
    document.querySelectorAll(".toast").forEach(e => e.remove());
    const t = h("div", { class: "toast", role: "status" }, msg);
    document.body.append(t);
    clearTimeout(toastT);
    toastT = setTimeout(() => t.remove(), 3200);
}
/* buttons: [{ label, cls, onClick, keep }] (keep: the dialog stays open) */
function dialog(o) {
    const wrap = $("#dlg");
    wrap.textContent = "";
    const box = h("div", { class: "dlg", role: "dialog", "aria-modal": "true", "aria-label": o.title }, h("h3", null, o.title), o.body || null,
        h("div", { class: "acts" }, o.buttons.map(b => h("button", { class: "btn " + (b.cls || ""), onclick: () => { if (!b.keep) closeDialog(); if (b.onClick) b.onClick(); } }, b.label))));
    wrap.append(box);
    wrap.hidden = false;
    wrap.onkeydown = e => { if (e.key === "Escape") { closeDialog(); if (o.onCancel) o.onCancel(); } e.stopPropagation(); };
    const first = box.querySelector(".btn.primary") || box.querySelector(".btn");
    if (first) first.focus();
}
function closeDialog() { const w = $("#dlg"); w.hidden = true; w.textContent = ""; }
const dialogOpen = () => !$("#dlg").hidden;
let menuEl = null;
function closeMenu() { if (menuEl) { menuEl.remove(); menuEl = null; } }
/* entries: [{ label, fn, off, cls, key } | "-"] */
function menu(x, y, entries) {
    closeMenu();
    menuEl = h("div", { class: "menu", role: "menu" }, entries.map(e => e === "-" ? h("hr") :
        h("button", { class: e.cls || "", disabled: !!e.off, onclick: () => { closeMenu(); e.fn(); } }, e.label, e.key ? h("kbd", null, e.key) : null)));
    document.body.append(menuEl);
    const r = menuEl.getBoundingClientRect();
    menuEl.style.left = Math.max(4, Math.min(x, innerWidth - r.width - 8)) + "px";
    menuEl.style.top = Math.max(4, Math.min(y, innerHeight - r.height - 8)) + "px";
}

/* ---- the state and UNDO ---- */
const emptyFm1 = () => ({ from: "empty", when: null, name: null, fw: null, lastSave: null, layout: null });
const app = {
    st: { L: [], R: [], base: [], fm1: emptyFm1() },
    hist: new M.History(100),
    commit(st, what, o) {
        o = o || {};
        this.hist.push(this.st, what);
        this.st = st;
        this.changed(o);
        if (what && !o.quiet) status(what, { undo: true, ok: !!o.ok });
    },
    /* the state replaced without a step (a READ's or WRITE's base) */
    set(st, o) { this.st = st; this.changed(o || {}); },
    changed(o) {
        ED.keep.save(this.saved());
        if (ED.files) ED.files.render(o.select);
        renderTop();
        if (ED.tabs) ED.tabs.stateChanged();
    },
    saved() { return { v: 1, st: this.st, ui: ED.files ? ED.files.prefs() : null }; },
    /* UNDO and REDO bring the panels back, never what the FM-1 is known to hold (the last READ's or WRITE's base, its
     * save number, its layout): WRITE is not taken back */
    keepTruth(st) {
        const cur = this.st.fm1 || {}, f = Object.assign({}, st.fm1);
        for (const k of ["lastSave", "layout", "baseInfo", "fw", "other"]) f[k] = cur[k];
        return Object.assign({}, st, { base: this.st.base, fm1: f });
    },
    undo() {
        const e = this.hist.undo(this.st);
        if (!e) { status("NOTHING TO UNDO"); return; }
        this.st = this.keepTruth(e.st);
        this.changed({});
        status("UNDONE: " + (e.what || "").split(" · ")[0]);
    },
    redo() {
        const e = this.hist.redo(this.st);
        if (!e) { status("NOTHING TO REDO"); return; }
        this.st = this.keepTruth(e.st);
        this.changed({});
        status("REDONE: " + (e.what || "").split(" · ")[0]);
    },
    emptyFm1
};

function renderTop() {
    $("#undo").disabled = !app.hist.canUndo;
    $("#redo").disabled = !app.hist.canRedo;
    if (ED.tabs) ED.tabs.renderBar();
}

ED.app = app;
ED.ui = { h, $, kb, plural, nowLabel, clamp, uniq, COL, ENG_COL, colourOf, icon, status, busy, toast, dialog, closeDialog,
    dialogOpen, menu, closeMenu, renderTop };
})(globalThis.ED = globalThis.ED || {});
