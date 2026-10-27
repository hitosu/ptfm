/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The tabs (specs/editor.md, The tabs): FILES first, always there; every item opens in a tab of its own (its kind,
 * number and name, and its panel). A tab edits a copy, the draft: SAVE puts it into the item on its panel and closes
 * the tab; ✕ with changes not saved asks SAVE, DISCARD or KEEP EDITING. A tab whose item was deleted says so and cannot
 * save; a built-in instrument's says that SAVE makes a changed copy; a system item at its default is made by SAVE.
 * A tab has its own UNDO until SAVE. The editors themselves are tab_*.js's: ED.tabs.kinds[name] = (tab, api) -> element.
 *   ED.tabs.open(it), show(id | "files"), close(tab), renderBar(), stateChanged(), unsavedOn(side), current() */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, app = ED.app;
const h = U.h, $ = U.$;
const tabs = [];
let shown = "files", seq = 1;
const kinds = {};
/* each tab's scrolling body's height as --edh on it (what a side list held in view is as tall as) */
const bodySize = typeof ResizeObserver !== "undefined"
    ? new ResizeObserver(es => { for (const e of es) e.target.style.setProperty("--edh", e.target.clientHeight + "px"); }) : null;

function editorOf(it) {
    if (it.kind === K.PATTERN) return "pattern";
    if (it.kind === K.SONG) return "song";
    if (it.kind === K.AUDIO) return "sample";
    if (M.isCart(it.kind)) return "cart";
    return { [K.SETTINGS]: "settings", [K.MIDIMAP]: "midimap", [K.LOOPS]: "loops", [K.DJ]: "dj" }[it.kind] || null;
}
/* a system item at its default: the records it starts from, the firmware's own (its keys' song and EDITOR's pattern
 * FE as its store answers them, DJ's setup and the loops' head as a new store makes them; the settings as SETTINGS
 * makes a new record, the MIDI map empty) */
function defaultRecs(it) {
    const v = R.VERSION[it.kind], fw = ED.pics && ED.pics.fw;
    const fromFw = n => n < 0 ? null : fw.mem().slice(fw.ex.ed_edit_buf(), fw.ex.ed_edit_buf() + (n & 0xFFFFFF));
    let b = null;
    if (fw && fw.ex.ed_record) {
        if (it.kind === K.SONG || it.kind === K.PATTERN) b = fromFw(fw.ex.ed_record(it.kind, it.num, 0));
        else if (it.kind === K.DJ) b = fromFw(fw.ex.ed_dj_default());
        else if (it.kind === K.LOOPS) b = fromFw(fw.ex.ed_loops_default());
    }
    if (it.kind === K.SETTINGS) { b = new Uint8Array(133); b.set(ED.defs.newSettings()); }
    if (it.kind === K.MIDIMAP) b = new Uint8Array(4);
    if (!b) throw new Error("the firmware's default is not here (the page's firmware did not start)");
    return [{ sub: 0, ver: v, payload: b }];
}

function find(side, itemId) { return tabs.find(t => t.side === side && t.itemId === itemId); }
function open(it) {
    let t = find(it.side, it.id);
    if (t) { show(t.id); return t; }
    const ed = editorOf(it);
    if (!ed || !kinds[ed]) { U.status("NO EDITOR FOR THIS KIND YET", { err: true }); return null; }
    let draft = it;
    if (it.isDefault) {
        try { draft = Object.assign({}, it, { recs: defaultRecs(it) }); }
        catch (e) { U.status(String(e.message || e).toUpperCase(), { err: true }); return null; }
    }
    t = { id: "t" + (seq++), side: it.side, itemId: it.id, ed, base: it, draft, dirty: false, deleted: false, undo: [], redo: [] };
    tabs.push(t);
    build(t);
    show(t.id);
    return t;
}
function build(t) {
    const it = t.draft, items = app.st[t.side];
    const SYSW = { [K.SETTINGS]: "SETTINGS", [K.MIDIMAP]: "MIDI MAP", [K.LOOPS]: "LOOPS", [K.DJ]: "DJ SETUP" };
    const kindWord = SYSW[it.kind] || (it.kind === K.PATTERN ? "PATTERN" : it.kind === K.SONG ? "SONG" : it.kind === K.AUDIO ? "SAMPLE" : M.ENGINE[it.kind] || "");
    const num = ED.files.numText(t.base, items);
    t.nameEl = h("div", { class: "edname" });
    t.saveBtn = h("button", { class: "btn primary", onclick: () => save(t) }, "SAVE");
    t.notes = h("div");
    t.body = h("div", { class: "edbody" });
    if (bodySize) bodySize.observe(t.body);
    t.el = h("section", { class: "edview", "data-tab": t.id },
        h("div", { class: "edhead" }, h("span", { class: "edic", html: U.icon(t.base, true) }),
            h("div", null, h("div", { class: "edkind" }, kindWord + (num && num !== "—" ? " " + num : "") + " · " + (t.side === "R" ? "FM-1" : "TABLE")), t.nameEl),
            h("span", { class: "grow" }),
            h("button", { class: "btn small", title: "UNDO IN THIS TAB (CTRL+Z)", onclick: () => tabUndo(t) }, "UNDO"),
            h("button", { class: "btn small", title: "REDO IN THIS TAB (CTRL+Y)", onclick: () => tabRedo(t) }, "REDO"),
            t.saveBtn, h("button", { class: "btn", onclick: () => close(t) }, "CLOSE")),
        t.notes, t.body);
    $("#main").append(t.el);
    renderTab(t);
}
const api = t => ({
    get draft() { return t.draft; },
    get side() { return t.side; },
    get st() { return app.st; },
    get tab() { return t; },
    /* a change: the new draft (the same id), with a words for the tab's UNDO */
    set(draft, what) {
        t.undo.push(t.draft); t.redo = [];
        if (t.undo.length > 200) t.undo.shift();
        t.draft = draft; t.dirty = true;
        paintHead(t);
        renderBar();
        if (ED.player) ED.player.update(draft, stWith(t));
    },
    /* the state with the draft in place of its item (the preview, the pictures) */
    stWith: () => stWith(t),
    refresh: () => renderTab(t)
});
function stWith(t) {
    const st = app.st, items = st[t.side];
    if (items.some(i => i.id === t.itemId)) return M.replaceItem(st, t.side, t.draft);
    return Object.assign({}, st, { [t.side]: items.concat([Object.assign({}, t.draft, { id: t.itemId })]) });
}
function renderTab(t) {
    paintHead(t);
    const sc = t.body.scrollTop;
    t.body.textContent = "";
    try { t.body.append(kinds[t.ed](t, api(t))); }
    catch (e) { t.body.append(h("div", { class: "banner bad" }, "THIS ITEM CANNOT BE EDITED HERE: " + String(e.message || e).toUpperCase())); console.error(e); }
    t.body.scrollTop = sc;
}
function paintHead(t) {
    const items = app.st[t.side];
    t.nameEl.textContent = ED.files.nameText(t.draft, items) || ED.files.label(t.draft, items);
    t.notes.textContent = "";
    if (t.deleted) t.notes.append(h("div", { class: "banner bad" }, "THIS ITEM WAS DELETED FROM ITS PANEL: THIS TAB CANNOT SAVE. CLOSE IT, OR UNDO THE DELETE."));
    else if (t.base.fw) t.notes.append(h("div", { class: "banner" }, "A BUILT-IN INSTRUMENT: SAVE MAKES A CHANGED COPY ON THE FM-1 PANEL (DELETING IT LATER BRINGS THE FIRMWARE'S OWN BACK)."));
    else if (t.base.isDefault) t.notes.append(h("div", { class: "banner" }, "THE FIRMWARE'S DEFAULT: SAVE MAKES IT THE FM-1 PANEL'S OWN."));
    else if (t.base.builtin && t.side === "R") t.notes.append(h("div", { class: "banner" }, "A CHANGED BUILT-IN INSTRUMENT."));
    t.saveBtn.disabled = t.deleted || !t.dirty;
}
function tabUndo(t) { const d = t.undo.pop(); if (!d) return; t.redo.push(t.draft); t.draft = d; t.dirty = t.undo.length > 0 || t.draft !== t.base; renderTab(t); renderBar(); if (ED.player) ED.player.update(t.draft, stWith(t)); }
function tabRedo(t) { const d = t.redo.pop(); if (!d) return; t.undo.push(t.draft); t.draft = d; t.dirty = true; renderTab(t); renderBar(); if (ED.player) ED.player.update(t.draft, stWith(t)); }
function save(t, anyway) {
    if (t.deleted || !t.dirty) return false;
    const st = app.st, items = st[t.side];
    const cur = items.find(i => i.id === t.itemId);
    /* changed under the tab (another tab's SAVE, a move): asked before it is replaced */
    if (cur && cur !== t.base && !anyway) {
        U.dialog({ title: ED.files.label(cur, items) + " CHANGED SINCE THIS TAB OPENED", body: h("p", null, "SAVE REPLACES IT WITH THIS TAB'S VERSION; THE OTHER CHANGE IS LOST (UNDO BRINGS IT BACK)."),
            buttons: [{ label: "SAVE ANYWAY", cls: "primary danger", onClick: () => save(t, true) }, { label: "CANCEL" }] });
        return false;
    }
    let next;
    if (cur) next = M.replaceItem(st, t.side, M.pruned(t.draft));
    else {
        const it = Object.assign({}, t.draft, { id: M.newId() });
        delete it.fw; delete it.isDefault;
        if (t.base.fw) it.builtin = true;
        /* the panel holds one at that key meanwhile (a move, UPLOAD DISK): this takes its place */
        const SYS = [K.SETTINGS, K.MIDIMAP, K.LOOPS, K.DJ];
        const there = items.find(i => i.kind === it.kind && (SYS.includes(it.kind) || i.num === it.num));
        if (there) it.id = there.id;
        next = there ? M.replaceItem(st, t.side, M.pruned(it)) : Object.assign({}, st, { [t.side]: items.concat([M.pruned(it)]) });
    }
    let more = "";
    if (t.onSave) { const r = t.onSave(next); next = r.st; more = r.what ? " · " + r.what : ""; }
    if (t.side === "R") next = Object.assign({}, next, { R: M.linkChanged(next.R) });
    app.commit(next, "SAVED " + ED.files.label(t.draft, items) + more + (t.side === "R" ? " · THE FM-1 CHANGES WITH WRITE" : ""));
    drop(t);
    return true;
}
function drop(t) {
    const i = tabs.indexOf(t);
    if (i < 0) return;
    if (t.cleanup) t.cleanup();                     /* (its keyboard's notes still on) */
    if (bodySize) bodySize.unobserve(t.body);
    tabs.splice(i, 1);
    t.el.remove();
    if (shown === t.id) show(tabs[i] ? tabs[i].id : tabs[i - 1] ? tabs[i - 1].id : "files");
    else renderBar();
}
function close(t) {
    if (!t.dirty || t.deleted) { drop(t); return; }
    show(t.id);
    U.dialog({ title: "SAVE THE CHANGES TO " + ED.files.label(t.draft, app.st[t.side]) + "?", body: h("p", null, "THIS TAB HAS CHANGES NOT SAVED."),
        buttons: [{ label: "SAVE", cls: "primary", onClick: () => save(t) }, { label: "DISCARD", cls: "danger", onClick: () => drop(t) }, { label: "KEEP EDITING" }] });
}
function show(id) {
    shown = id;
    $("#files").hidden = id !== "files";
    for (const t of tabs) t.el.hidden = t.id !== id;
    renderBar();
    if (id === "files") ED.files.render();
}
function renderBar() {
    const bar = $("#tabs");
    bar.textContent = "";
    bar.append(h("button", { class: "tab" + (shown === "files" ? " on" : ""), onclick: () => show("files") }, "FILES"));
    for (const t of tabs) {
        const items = app.st[t.side];
        const nm = ED.files.nameText(t.draft, items) || ED.files.label(t.draft, items);
        bar.append(h("div", { class: "tab" + (shown === t.id ? " on" : ""), role: "button", tabIndex: 0, title: ED.files.label(t.draft, items),
            onclick: e => { if (!e.target.closest(".x")) show(t.id); }, onauxclick: e => { if (e.button === 1) close(t); } },
            h("span", { class: "ic", html: U.icon(t.base) }), nm, h("span", { class: "side" }, t.side === "R" ? "FM-1" : "TABLE"),
            t.dirty ? h("span", { class: "dot", title: "CHANGES NOT SAVED" }) : null,
            h("button", { class: "x", title: "CLOSE", onclick: () => close(t) }, "✕")));
    }
}
/* the panels changed (a move, a delete, UNDO): a tab whose item is gone says so; one not changed follows its item */
function stateChanged() {
    for (const t of tabs) {
        const it = ED.files.item(t.side, t.itemId);
        const was = t.deleted;
        t.deleted = !it && !t.base.fw && !t.base.isDefault;
        /* (a system item at its default is made again whenever the panels change: the same default, the tab keeps the
         * records it made for it) */
        if (it && !t.dirty && it !== t.base && it.isDefault && t.base.isDefault) { t.base = it; t.draft = Object.assign({}, it, { recs: t.draft.recs }); }
        else if (it && !t.dirty && it !== t.base) { t.base = it; t.draft = it; renderTab(t); }
        else if (was !== t.deleted) paintHead(t);
    }
    renderBar();
}
/* ---- the tabs' controls: styled sliders, lists and fields, every value in the FM-1's words ---- */
function field(label, ctl, out) { return h("div", { class: "field" }, h("label", null, label), ctl, out || h("span")); }
/* a slider: get(), set(v) on release (live(v) while it moves), fmt(v) the words; acc: the filled part's colour; def:
 * its default, which a double-click puts back */
function slider(label, o) {
    const fmt = o.fmt || (v => String(v));
    const out = h("output", null, fmt(o.get()));
    const i = h("input", { type: "range", min: o.min, max: o.max, step: o.step || 1, value: o.get(), "aria-label": label,
        title: o.def != null ? "DOUBLE-CLICK: THE DEFAULT, " + fmt(o.def) : null });
    const paint = () => i.style.setProperty("--p", (100 * (+i.value - o.min) / Math.max(1, o.max - o.min)) + "%");
    i.addEventListener("input", () => { out.textContent = fmt(+i.value); paint(); if (o.live) o.live(+i.value); });
    i.addEventListener("change", () => o.set(+i.value));
    if (o.def != null) i.addEventListener("dblclick", () => {
        const v = Math.max(o.min, Math.min(o.max, o.def));
        i.value = v; out.textContent = fmt(v); paint();
        o.set(v);
    });
    if (o.acc) i.style.setProperty("--acc", o.acc);
    paint();
    return field(label, i, out);
}
/* the defaults of a new instrument of each engine (a slider's double-click) */
const slotDefaults = {};
function slotDefault(kind) { if (!slotDefaults[kind]) slotDefaults[kind] = ED.defs.newSlot(kind, ""); return slotDefaults[kind]; }
function choice(label, values, get, set) {
    const s = h("select", { "aria-label": label, onchange: e => set(+e.target.value) },
        values.map((t, i) => h("option", { value: i, selected: i === get() }, t)));
    return field(label, s);
}
/* a name's field, as wide as the name may be long (capitals: about 1.3 of a digit's width each) */
function text(label, get, set, max) {
    const i = h("input", { type: "text", class: "name", value: get(), maxLength: max, "aria-label": label });
    if (max) i.style.width = "calc(" + Math.ceil(max * 1.3) + "ch + 16px)";
    i.addEventListener("change", () => set(R.cleanName(i.value, max)));
    return h("div", { class: "field two" }, h("label", null, label), i);
}
function card(title, ...kids) { return h("section", { class: "card" }, h("h4", null, title, h("span", { class: "grow" })), ...kids); }
function wideCard(title, ...kids) { const c = card(title, ...kids); c.classList.add("wide"); return c; }
/* a card with the FM-1's pictures of what its controls change beside them, held in view while the controls scroll
 * (above them when the card is narrow) */
function picSection(title, pics, ...kids) {
    const c = card(title, h("div", { class: "psplit" }, h("div", { class: "pcol" }, pics), h("div", { class: "ccol" }, ...kids)));
    c.classList.add("psec");
    return c;
}

const unsavedOn = side => tabs.some(t => t.side === side && t.dirty && !t.deleted);
const current = () => tabs.find(t => t.id === shown) || null;
if (typeof addEventListener !== "undefined") addEventListener("beforeunload", e => { if (tabs.some(t => t.dirty && !t.deleted)) { e.preventDefault(); e.returnValue = ""; } });

ED.tabs = { kinds, open, show, close, save, renderBar, stateChanged, unsavedOn, current, editorOf, defaultRecs, undo: tabUndo, redo: tabRedo,
    field, slider, slotDefault, choice, text, card, wideCard, picSection,
    get list() { return tabs; } };
})(globalThis.ED = globalThis.ED || {});
