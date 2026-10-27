/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FILES (specs/editor.md, FILES): TABLE on the left, FM-1 on the right, the arrows between them. Each panel: its keys,
 * groups by kind, three views (DETAILS, LIST, ICONS), the filter, sorting, picking (click, Ctrl, Shift, the arrows,
 * Ctrl+A), the right-click menu, the selection bar, dragging between the panels and files dropped from the computer;
 * the FM-1 panel's header and space bar. The rules are model.js's.
 *   ED.files.build(), render(select), prefs() / setPrefs(p), selected(side), selectIds(side, ids), transfer(from, ids, o),
 *   askDelete(side, ids), item(side, id) (the panels' items and the FM-1 panel's built-ins and defaults) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, app = ED.app;
const h = U.h, $ = U.$, kb = U.kb, plural = U.plural, COL = U.COL;
const H2 = M.H2;
const SYS_KINDS = [K.SETTINGS, K.MIDIMAP, K.LOOPS, K.DJ];
const GROUPS = ["SONGS", "PATTERNS", "CARTRIDGES", "SAMPLERS", "SAMPLES", "SYSTEM"];
const ui = { active: "L", sel: { L: new Set(), R: new Set() }, anchor: { L: null, R: null }, focus: { L: null, R: null },
    view: { L: "details", R: "details" }, filter: { L: "", R: "" }, sort: { L: { k: "src", d: 1 }, R: { k: "num", d: 1 } },
    closed: { L: [], R: [] }, order: { L: [], R: [] }, show: "L", need: null, fresh: new Set(), users: { L: new Map(), R: new Map() } };
const P = {};
let drag = null;

/* ---- the panels' items: the FM-1 panel lists the firmware's built-ins and the system items at their defaults too ---- */
let viewCache = { st: null, R: null };
function viewItems(side) {
    const st = app.st;
    if (side === "L") return st.L;
    if (viewCache.st === st) return viewCache.R;
    const out = st.R.slice();
    const at = new Set(st.R.filter(i => i.num != null && M.numSpace(i.kind)).map(i => M.numSpace(i.kind) + i.num));
    const seen = new Set();
    for (const b of M.builtins) {
        const k = b.kind + ":" + b.num;
        if (seen.has(k) || at.has(M.numSpace(b.kind) + b.num)) continue;
        seen.add(k);
        const recs = M.builtins.filter(x => x.kind === b.kind && x.num === b.num).map(x => ({ sub: x.sub, ver: x.ver, payload: x.payload }));
        out.push({ id: "B:" + k, side: "R", kind: b.kind, num: b.num, recs, link: {}, fw: true });
    }
    for (const k of SYS_KINDS) if (!st.R.some(i => i.kind === k)) out.push({ id: "D:" + k, side: "R", kind: k, num: 0, recs: [], link: {}, isDefault: true });
    for (const k of [K.PATTERN, K.SONG]) if (!st.R.some(i => i.kind === k && i.num === M.FE))
        out.push({ id: "D:" + k, side: "R", kind: k, num: M.FE, recs: [], link: {}, isDefault: true });
    viewCache = { st, R: out };
    return out;
}
function item(side, id) { return viewItems(side).find(i => i.id === id) || null; }
const fixed = it => it.fw || it.isDefault;

/* ---- what a row shows ---- */
function numText(it, items) {
    if (it.side !== "R") return "";
    if (it.kind === K.AUDIO) { const o = items.find(x => x.id === it.owner); return (o ? H2(o.num) : "--") + ":" + H2(it.idx); }
    if (SYS_KINDS.includes(it.kind)) return "—";
    return H2(it.num);
}
function nameText(it, items) {
    if (it.isDefault) return M.nameOf(Object.assign({}, it, { recs: [{ payload: new Uint8Array(0), ver: 1 }] }), items);
    if (it.kind === K.PATTERN && it.side === "R" && it.num !== M.FE) return "";
    return M.nameOf(it, items);
}
function derived(it, items) {
    if (it.kind !== K.PATTERN || it.side !== "R" || it.num === M.FE || it.isDefault) return "";
    const p = M.patternPlace(it, items);
    if (p) return p;
    const fu = items.find(c => c.kind === K.FUSION && Object.values(c.link).includes(it.id));
    if (fu) return "FUSION · " + M.nameOf(fu, items);
    return "NOT IN A SONG";
}
function kindText(it, items) {
    if (it.isDefault) return "DEFAULT";
    try { return M.kindText(it, items); } catch (e) { return "CANNOT BE READ"; }
}
function sizeText(it) { return it.fw ? "IN FW" : it.isDefault ? "—" : kb(M.size(it)); }
function label(it, items) {
    if (it.isDefault) return nameText(it, items);
    return M.label(it, items);
}

/* ---- the FM-1 panel's header and space ---- */
function fm1Source() {
    const f = app.st.fm1, pd = M.pending(app.st);
    let src;
    if (f.from === "read") src = "READ " + f.when + (f.fw ? " · PT/FM " + f.fw : "");
    else if (f.from === "write") src = "WRITTEN " + f.when + (f.fw ? " · PT/FM " + f.fw : "");
    else if (f.from === "disk") src = "DISK " + f.name;
    else src = "EMPTY";
    return { src, pd };
}
function renderHead(side) {
    const p = P[side];
    if (side === "R") {
        const { src, pd } = fm1Source(), f = app.st.fm1;
        p.sub.textContent = "";
        p.sub.append(src, pd.count ? h("em", null, " · " + plural(pd.count, "CHANGE") + " NOT WRITTEN") : "");
        if (f.other) p.sub.append(h("span", { class: "warn" }, " · " + f.other));
        p.write.textContent = "";
        p.write.append("WRITE", pd.count ? h("span", { class: "badge" }, pd.count) : "");
        p.revert.disabled = !pd.count || f.from === "empty" && !app.st.base.length && !app.st.R.length;
        renderSpace();
    } else {
        const tot = app.st.L.reduce((s, it) => s + M.size(it), 0);
        p.sub.textContent = "KEPT IN THIS BROWSER";
        p.space.textContent = "";
        p.space.append(h("b", null, plural(app.st.L.length, "ITEM")), " · ", h("b", null, kb(tot)));
    }
}
function renderSpace() {
    const p = P.R, sp = M.space(app.st.R, app.st.fm1.layout), pc = n => (100 * n / sp.blocks) + "%";
    const need = ui.need && ui.need.until > Date.now() ? ui.need : null;
    /* (nothing kept free: when little is left, what that costs the FM-1's own saves is said) */
    const bar = h("div", { class: "bar", title: sp.used + " OF " + sp.blocks + " BLOCKS OF 4 KB USED; " + sp.reserve * 4 + " KB FREE ADVISED FOR THE FM-1'S OWN SAVES" },
        h("i", { class: "data", style: { width: pc(sp.dataBlocks) } }), h("i", { class: "audio", style: { width: pc(sp.audioBlocks) } }), h("i", { class: "free" }),
        need ? h("i", { class: "need", style: { left: pc(Math.max(0, sp.used)), width: pc(Math.min(need.blocks, sp.blocks)) } }) : null);
    const k = (c, t) => h("span", null, h("span", { class: "k", style: { background: c } }), t);
    const legend = need
        ? h("div", { class: "legend err" }, need.msg + " DELETE SOMETHING HERE, OR TRIM IT ON THE TABLE.")
        : h("div", { class: "legend" }, k(COL.lgrey, h("span", null, "DATA ", h("b", null, kb(sp.dataBlocks * 4096)))),
            k(COL.brown, h("span", null, "AUDIO ", h("b", null, kb(sp.audioBlocks * 4096)))),
            h("span", null, "FREE ", h("b", null, kb(Math.max(0, sp.free) * 4096))),
            sp.low ? h("span", { class: "warn" }, sp.low) : null);
    p.space.textContent = "";
    p.space.append(bar, legend);
}

/* ---- building ---- */
function seg(values, labels, get, set) {
    const box = h("div", { class: "seg" });
    const paint = () => [...box.children].forEach((b, i) => b.classList.toggle("on", values[i] === get()));
    values.forEach((v, i) => box.append(h("button", { type: "button", onclick: () => { set(v); paint(); } }, labels[i])));
    paint();
    box.paint = paint;
    return box;
}
function build() {
    const files = $("#files");
    $("#narrowsw").append(seg(["L", "R"], ["TABLE", "FM-1"], () => ui.show, v => { ui.show = v; files.dataset.show = v; }));
    P.L = buildPanel("L");
    const rail = h("div", { class: "rail" },
        h("button", { class: "btn", id: "toR", title: "COPY THE TABLE'S SELECTION TO THE FM-1, WITH WHAT IT USES (ALT+→)", onclick: () => transfer("L", [...ui.sel.L]) }, "→", h("small", null, "FM-1")),
        h("button", { class: "btn", id: "toL", title: "COPY THE FM-1'S SELECTION TO THE TABLE, WITH WHAT IT USES (ALT+←)", onclick: () => transfer("R", [...ui.sel.R]) }, "←", h("small", null, "TABLE")));
    P.R = buildPanel("R");
    files.append(P.L.el, rail, P.R.el);
}
function buildPanel(side) {
    const isR = side === "R", F = ED.fm1;
    const p = { side };
    p.sub = h("span", { class: "sub" });
    p.filter = h("input", { type: "search", placeholder: "FILTER", title: "KEEP WHAT HOLDS THIS IN ITS NAME, SOURCE, NUMBER OR KIND", "aria-label": "Filter",
        oninput: e => { ui.filter[side] = e.target.value; renderList(side); } });
    p.views = seg(["details", "list", "icons"], ["DETAILS", "LIST", "ICONS"], () => ui.view[side], v => { ui.view[side] = v; renderList(side); saveSoon(); });
    const fold = all => { ui.closed[side] = all ? GROUPS.slice() : []; renderList(side); saveSoon(); };
    p.folds = h("div", { class: "seg" },
        h("button", { type: "button", title: "FOLD EVERY GROUP", onclick: () => fold(true) }, "COLLAPSE ALL"),
        h("button", { type: "button", title: "OPEN EVERY GROUP", onclick: () => fold(false) }, "EXPAND ALL"));
    const tools = isR ? [
        p.read = h("button", { class: "btn", title: "CONNECT, READ WHAT THE FM-1 HOLDS INTO THIS PANEL, LET GO", onclick: () => F.read() }, "READ"),
        p.write = h("button", { class: "btn primary", title: "CONNECT, MAKE THE FM-1 HOLD WHAT THIS PANEL SHOWS, LET GO", onclick: () => F.write() }, "WRITE"),
        h("span", { class: "sep" }),
        h("button", { class: "btn", title: "PUT A DISK'S CONTENTS IN THIS PANEL, IN PLACE OF WHAT IS HERE", onclick: () => F.uploadDisk() }, "UPLOAD DISK"),
        h("button", { class: "btn", title: "SAVE THIS PANEL AS A DISK (.PTFMDSK): THE BACKUP", onclick: () => F.downloadDisk() }, "DOWNLOAD DISK"),
        h("span", { class: "sep" }),
        p.revert = h("button", { class: "btn", title: "PUT BACK WHAT THIS PANEL SHOWED AFTER THE LAST READ OR WRITE", onclick: () => F.revert() }, "REVERT"),
        h("button", { class: "btn danger", title: "EMPTY THIS PANEL (THE BUILT-IN INSTRUMENTS STAY)", onclick: () => deleteAll("R") }, "DELETE ALL"),
    ] : [
        h("button", { class: "btn primary", title: "ADD FILES: DISKS, MOD, S3M, XM, MIDI, DX7 .SYX, SOUNDS", onclick: () => $("#filepick").click() }, "+ ADD FILES"),
        h("button", { class: "btn", title: "MAKE A NEW SONG, PATTERN, CARTRIDGE, SAMPLER OR SYSTEM ITEM HERE", onclick: e => {
            const r = e.currentTarget.getBoundingClientRect();
            U.menu(r.left, r.bottom + 2, NEW_KINDS.map(x => x === "-" ? x : { label: x[1], fn: () => makeNew(x[0]) }));
        } }, "+ NEW ▾"),
        h("span", { class: "sep" }),
        h("button", { class: "btn danger", title: "EMPTY THE TABLE", onclick: () => deleteAll("L") }, "DELETE ALL"),
    ];
    p.space = h("div", { class: isR ? "space" : "tinfo" });
    p.list = h("div", { class: "list", tabIndex: 0, "data-side": side, "aria-label": isR ? "FM-1" : "TABLE" });
    p.selbar = h("div", { class: "selbar" });
    p.el = h("section", { class: "panel", "data-side": side, "aria-label": isR ? "FM-1" : "TABLE" },
        h("div", { class: "phead" }, h("h2", null, isR ? "FM-1" : "TABLE"), p.sub),
        h("div", { class: "ptools" }, tools, h("span", { class: "grow" }), p.filter, p.views, p.folds),
        p.space, p.list, p.selbar);
    wireList(p);
    wireDrop(p);
    return p;
}
function render(select) {
    if (select) { ui.sel[select.side] = new Set(select.ids); ui.focus[select.side] = select.ids[0] || null; ui.anchor[select.side] = select.ids[0] || null;
        for (const id of select.ids) ui.fresh.add(id); setTimeout(() => { for (const id of select.ids) ui.fresh.delete(id); }, 1900); }
    for (const side of ["L", "R"]) {
        const ids = new Set(viewItems(side).map(i => i.id));
        for (const id of [...ui.sel[side]]) if (!ids.has(id)) ui.sel[side].delete(id);
        renderHead(side);
        renderList(side);
    }
    if (select && select.ids[0]) {
        const el = P[select.side].list.querySelector(`[data-id="${CSS.escape(select.ids[0])}"]`);
        if (el) el.scrollIntoView({ block: "nearest" });
    }
}

/* ---- the list ---- */
function sortKey(side, k, items) {
    switch (k) {
        case "num": return it => SYS_KINDS.includes(it.kind) ? 9e6 + SYS_KINDS.indexOf(it.kind) : it.kind === K.AUDIO
            ? ((items.find(o => o.id === it.owner) || { num: 999 }).num * 256 + it.idx) : (it.num != null ? it.num : 999) * 256;
        case "src": return it => (it.src || "") + "\u0001" + (it.orig != null ? H2(it.orig) : "") + (it.kind === K.AUDIO ? H2(it.idx || 0) : "") + nameText(it, items);
        case "name": return it => nameText(it, items) || derived(it, items);
        case "kind": return it => kindText(it, items);
        case "size": return it => it.fw || it.isDefault ? -1 : M.size(it);
        case "deps": return it => fixed(it) ? -1 : M.sizeWith(items, it.id);
        case "used": return it => (ui.users[side].get(it.id) || []).length;
    }
    return () => 0;
}
function cmp(a, b) { return typeof a === "number" && typeof b === "number" ? a - b : String(a).localeCompare(String(b)); }
function renderList(side) {
    const p = P[side], list = p.list, scroll = list.scrollTop, all = viewItems(side), real = app.st[side];
    ui.users[side] = M.users(real);
    const pend = side === "R" ? M.pending(app.st).ids : new Set();
    const f = ui.filter[side].trim().toUpperCase();
    let items = all;
    if (f) items = items.filter(it => [nameText(it, real), it.src || "", kindText(it, real), numText(it, real), derived(it, real)].join(" ").toUpperCase().includes(f));
    const { k, d } = ui.sort[side], key = sortKey(side, k, real);
    const keys = new Map(items.map(it => [it.id, key(it)]));
    items = items.slice().sort((a, b) => d * cmp(keys.get(a.id), keys.get(b.id)));
    list.textContent = "";
    ui.order[side] = [];
    const view = ui.view[side];
    if (view === "details") {
        const cols = [["", ""], side === "R" ? ["num", "NO."] : ["src", "FROM"], ["name", "NAME"], ["kind", "KIND", "c-kind"], ["size", "SIZE", "c-size"],
            ["deps", "+ USES", "c-deps"], ["used", "USED", "c-used"], ["", ""]];
        list.append(h("div", { class: "colhead" }, cols.map(([ck, t, cls]) => h("span", { class: (cls || "") + (ck === k ? " on" : ""), title: ck ? "SORT" : null,
            style: cls === "c-size" || cls === "c-deps" || cls === "c-used" ? "text-align:right" : null,
            onclick: ck ? () => { ui.sort[side] = { k: ck, d: ui.sort[side].k === ck ? -ui.sort[side].d : 1 }; renderList(side); saveSoon(); } : null },
            t + (ck === k ? (d > 0 ? " ▴" : " ▾") : "")))));
    }
    let any = false;
    for (const g of GROUPS) {
        const gi = items.filter(it => M.groupOf(it) === g);
        if (!gi.length) continue;
        any = true;
        const closed = ui.closed[side].includes(g);
        const tot = gi.reduce((s, it) => s + (fixed(it) ? 0 : M.size(it)), 0);
        list.append(h("div", { class: "ghead", "data-g": g, "data-act": "fold", title: (closed ? "OPEN " : "FOLD ") + g + " · CTRL+CLICK: PICK ALL OF IT" },
            h("button", { class: "caret", "aria-label": closed ? "OPEN" : "CLOSE", "aria-expanded": closed ? "false" : "true" }, closed ? "▸" : "▾"),
            h("span", { class: "glabel" }, g), h("span", { class: "gcount" }, gi.length),
            h("span", { class: "gline" }), h("span", { class: "gsize" }, kb(tot))));
        if (closed) continue;
        if (view === "details") for (const it of gi) { list.append(rowEl(side, it, pend, real)); ui.order[side].push(it.id); }
        else if (view === "list") list.append(h("div", { class: "lgrid" }, gi.map(it => { ui.order[side].push(it.id); return lrowEl(side, it, real); })));
        else { const tl = h("div", { class: "tiles" }); list.append(tl); for (const it of gi) { ui.order[side].push(it.id); tl.append(tileEl(side, it, real)); } }
    }
    if (!any) list.append(emptyEl(side, !!f));
    list.scrollTop = scroll;
    renderSelbar(side);
}
const canPlay = it => ED.player && ED.player.canPlay(it);
const isPlaying = it => ED.player && ED.player.isPlaying(it);
function rowEl(side, it, pend, real) {
    const sel = ui.sel[side].has(it.id), used = (ui.users[side].get(it.id) || []).length;
    const deps = fixed(it) ? 0 : M.sizeWith(real, it.id), own = fixed(it) ? 0 : M.size(it);
    const cls = "row" + (sel ? " sel" : "") + (ui.focus[side] === it.id ? " focus" : "") + (it.isDefault || it.dim ? " dim" : "") + (ui.fresh.has(it.id) ? " fresh" : "");
    const nm = nameText(it, real), der = derived(it, real);
    const missing = !fixed(it) && Object.values(it.link).some(t => t === null);
    return h("div", { class: cls, draggable: !fixed(it) && !it.dim, "data-id": it.id },
        h("span", { class: "c-ic", html: U.icon(it) }),
        side === "R" ? h("span", { class: "c-num", style: { color: U.colourOf(it) } }, numText(it, real)) : h("span", { class: "c-src", title: it.src || "" }, it.src || ""),
        h("span", { class: "c-name" }, pend.has(it.id) ? h("span", { class: "pend", title: "NOT WRITTEN TO THE FM-1 YET" }) : null,
            nm ? h("span", { class: "nm" }, nm) : null, der ? h("span", { class: "der" }, der) : null,
            it.fw ? h("span", { class: "tag" }, "BUILT-IN") : null,
            it.builtin ? h("span", { class: "tag chg" }, side === "R" ? "CHANGED" : "CHANGED " + H2(it.orig)) : null,
            it.isDefault ? h("span", { class: "tag" }, "DEFAULT") : null,
            it.dim ? h("span", { class: "tag bad" }, "NOT WRITTEN") : null,
            missing ? h("span", { class: "tag bad", title: "IT NAMES SOMETHING THAT IS NOT HERE" }, "MISSING") : null),
        h("span", { class: "c-kind" }, kindText(it, real)),
        h("span", { class: "c-size" }, sizeText(it)),
        h("span", { class: "c-deps", title: "WITH WHAT IT USES" }, deps && deps !== own ? kb(deps) : ""),
        h("span", { class: "c-used", title: used ? "USED BY " + used : "USED BY NOTHING" }, used ? String(used) : ""),
        h("span", { class: "c-act" }, canPlay(it) ? h("button", { class: "act", "data-act": "play", title: "PLAY (SPACE)" }, isPlaying(it) ? "■" : "▶") : null,
            !it.dim ? h("button", { class: "act ed", "data-act": "edit", title: "EDIT IN A TAB (ENTER)" }, "✎") : null));
}
function lrowEl(side, it, real) {
    const sel = ui.sel[side].has(it.id);
    return h("div", { class: "lrow" + (sel ? " sel" : "") + (ui.focus[side] === it.id ? " focus" : "") + (ui.fresh.has(it.id) ? " fresh" : ""), "data-id": it.id,
        draggable: !fixed(it) && !it.dim, title: label(it, real) },
        h("span", { html: U.icon(it) }), side === "R" ? h("span", { class: "mono", style: { color: U.colourOf(it) } }, numText(it, real)) : null,
        h("span", { class: "nm" }, nameText(it, real) || derived(it, real)));
}
function tileEl(side, it, real) {
    const sel = ui.sel[side].has(it.id);
    const thumb = h("div", { class: "thumb" });
    if (!fixed(it) && !it.dim && (it.kind === K.PATTERN || it.kind === K.AUDIO || it.kind === K.SONG)) {
        const c = h("canvas", { width: 212, height: 112 });
        thumb.append(c);
        requestAnimationFrame(() => drawThumb(c, it, side, real));
    } else thumb.innerHTML = U.icon(it, true);
    const n = numText(it, real);
    return h("div", { class: "tile" + (sel ? " sel" : "") + (ui.focus[side] === it.id ? " focus" : "") + (ui.fresh.has(it.id) ? " fresh" : ""), "data-id": it.id,
        draggable: !fixed(it) && !it.dim, title: label(it, real) },
        thumb, h("div", { class: "tn" }, side === "R" && n && n !== "—" ? h("span", { class: "mono" }, n + " ") : null, nameText(it, real) || derived(it, real)),
        h("div", { class: "ts" }, it.fw ? "BUILT-IN" : it.isDefault ? "DEFAULT" : kb(M.size(it))));
}
function emptyEl(side, filtered) {
    if (filtered) return h("div", { class: "empty" }, "NOTHING MATCHES THE FILTER.");
    if (side === "L") return h("div", { class: "empty" }, h("b", null, "THE TABLE IS EMPTY"), h("br"),
        "DROP FILES HERE: DISKS (.PTFMDSK), MOD, S3M, XM, MIDI, DX7 .SYX, SOUNDS.", h("br"),
        h("button", { class: "linkbtn", onclick: () => $("#filepick").click() }, "ADD FILES"));
    return h("div", { class: "empty" }, "NOTHING HERE.");
}

/* ---- the ICONS view's pictures: a pattern's notes in its engines' colours, a sample's waveform, a song's positions ---- */
const peaksCache = new WeakMap();
function peaks(it) {
    if (peaksCache.has(it.recs[0])) return peaksCache.get(it.recs[0]);
    let out = null;
    try {
        const d = R.ptrDecode(it.recs[0].payload), s = d.samples, n = 106, full = d.bits === 16 ? 32768 : 128;
        out = new Float32Array(n);
        for (let i = 0; i < n; i++) {
            let m = 0;
            const a = Math.floor(i * s.length / n), b = Math.floor((i + 1) * s.length / n);
            for (let j = a; j < b; j++) { const v = Math.abs(s[j]); if (v > m) m = v; }
            out[i] = m / full;
        }
    } catch (e) { out = new Float32Array(0); }
    peaksCache.set(it.recs[0], out);
    return out;
}
function engineOf(side, it, num) {
    const t = it.link[M.lk("c", num)];
    if (t === M.BUILTIN || (t == null && num >= 0xF0)) { const b = M.builtinAt("c", num); return b ? b.kind : null; }
    const ti = t && app.st[side].find(x => x.id === t);
    return ti ? ti.kind : null;
}
function drawThumb(c, it, side, real) {
    const g = c.getContext("2d"), W = c.width, Hh = c.height;
    g.fillStyle = "#000"; g.fillRect(0, 0, W, Hh);
    if (it.kind === K.AUDIO) {
        const pk = peaks(it), w = W / Math.max(1, pk.length);
        g.fillStyle = COL.yellow;
        for (let i = 0; i < pk.length; i++) { const hh = Math.max(1, pk[i] * (Hh - 8)); g.fillRect(i * w, (Hh - hh) / 2, Math.max(1, w - 0.5), hh); }
        return;
    }
    if (it.kind === K.SONG) {
        const b = it.recs[0].payload, n = b[20] + 1, w = W / Math.max(1, n);
        for (let i = 0; i < n; i++) {
            const t = it.link[M.lk("p", b[21 + i])], p = t && real.find(x => x.id === t);
            let dens = 0.2;
            if (p) { try { const pp = R.unpackPattern(p.recs.map(r => r.payload)); dens = Math.min(1, pp.cells.size / (pp.tracks * pp.rows * 0.35)); } catch (e) { /* (drawn plain) */ } }
            g.fillStyle = p ? COL.lgrey : COL.lred;
            const hh = 10 + dens * (Hh - 24);
            g.fillRect(i * w + 1, Hh - 6 - hh, Math.max(1, w - 2), hh);
        }
        return;
    }
    let pp;
    try { pp = R.unpackPattern(it.recs.map(r => r.payload)); } catch (e) { return; }
    const cw = W / pp.tracks, rh = Hh / pp.rows, last = {};
    const keys = [...pp.cells.keys()].map(k => k.split(",").map(Number)).sort((a, b) => a[0] - b[0]);
    for (const [r, t] of keys) {
        const x = pp.cells.get(r + "," + t);
        if (x.ins >= 0) last[t] = x.ins >> 8;
        if (x.note < 12) continue;
        g.fillStyle = U.ENG_COL[engineOf(side, it, last[t])] || COL.lgrey;
        g.fillRect(t * cw + 1, r * rh, Math.max(1, cw - 2), Math.max(1, rh));
    }
}

/* ---- picking, keys, the menu ---- */
function select(side, id, e) {
    ui.active = side;
    const sel = ui.sel[side], add = e && (e.ctrlKey || e.metaKey);
    if (e && e.shiftKey && ui.anchor[side]) {
        const o = ui.order[side], a = o.indexOf(ui.anchor[side]), b = o.indexOf(id);
        if (!add) sel.clear();
        if (a >= 0 && b >= 0) for (let i = Math.min(a, b); i <= Math.max(a, b); i++) sel.add(o[i]);
    } else if (add) { if (sel.has(id)) sel.delete(id); else sel.add(id); ui.anchor[side] = id; }
    else { sel.clear(); sel.add(id); ui.anchor[side] = id; }
    ui.focus[side] = id;
    renderList(side);
}
function selectIds(side, ids) {
    const have = new Set(viewItems(side).map(i => i.id));
    ids = ids.filter(id => have.has(id));
    ui.sel[side] = new Set(ids);
    ui.focus[side] = ids[0] || null;
    ui.anchor[side] = ids[0] || null;
    ui.active = side;
    if (ui.show !== side) { ui.show = side; $("#files").dataset.show = side; $("#narrowsw").firstChild.paint(); }
    for (const g of new Set(ids.map(id => M.groupOf(item(side, id))))) ui.closed[side] = ui.closed[side].filter(x => x !== g);
    renderList(side);
    const el = ids[0] && P[side].list.querySelector(`[data-id="${CSS.escape(ids[0])}"]`);
    if (el) el.scrollIntoView({ block: "nearest" });
}
function openTab(it) { if (ED.tabs && it && !it.dim) ED.tabs.open(it); }
function play(it) { if (ED.player && it) ED.player.toggle(it); }
function wireList(p) {
    const side = p.side, list = p.list;
    list.addEventListener("click", e => {
        const a = e.target.closest("[data-act]");
        if (a) {
            const act = a.dataset.act;
            /* a group's header: a click folds or opens it; Ctrl or Shift + click adds all its items to those picked */
            if (act === "fold") {
                const g = a.closest(".ghead").dataset.g;
                if (e.ctrlKey || e.metaKey || e.shiftKey) {
                    viewItems(side).filter(i => M.groupOf(i) === g).forEach(i => ui.sel[side].add(i.id));
                    ui.active = side;
                    renderList(side);
                } else { ui.closed[side] = ui.closed[side].includes(g) ? ui.closed[side].filter(x => x !== g) : ui.closed[side].concat([g]); renderList(side); saveSoon(); }
                return;
            }
            const it = item(side, a.closest("[data-id]").dataset.id);
            if (act === "play") play(it);
            if (act === "edit") openTab(it);
            return;
        }
        const row = e.target.closest("[data-id]");
        if (row) select(side, row.dataset.id, e);
        else if (e.target === list) { ui.sel[side].clear(); renderList(side); }
    });
    list.addEventListener("dblclick", e => { const row = e.target.closest("[data-id]"); if (row && !e.target.closest("[data-act]")) openTab(item(side, row.dataset.id)); });
    list.addEventListener("contextmenu", e => {
        const row = e.target.closest("[data-id]");
        if (!row) return;
        e.preventDefault();
        if (!ui.sel[side].has(row.dataset.id)) select(side, row.dataset.id);
        showMenu(e.clientX, e.clientY, side);
    });
    list.addEventListener("focus", () => { ui.active = side; });
    list.addEventListener("keydown", e => {
        const o = ui.order[side];
        let i = o.indexOf(ui.focus[side]);
        const it = ui.focus[side] && item(side, ui.focus[side]);
        if (e.key === "ArrowDown" || e.key === "ArrowUp") {
            e.preventDefault();
            i = U.clamp(i + (e.key === "ArrowDown" ? 1 : -1), 0, o.length - 1);
            if (o[i]) {
                select(side, o[i], e.shiftKey ? { shiftKey: true } : null);
                const el = list.querySelector(`[data-id="${CSS.escape(o[i])}"]`);
                if (el) el.scrollIntoView({ block: "nearest" });
            }
        } else if (e.key === "Enter" && it) { e.preventDefault(); openTab(it); }
        else if (e.key === " " && it) { e.preventDefault(); play(it); }
        else if ((e.key === "Delete" || e.key === "Backspace") && ui.sel[side].size) { e.preventDefault(); askDelete(side, [...ui.sel[side]]); }
        else if (e.key === "F2" && it) { e.preventDefault(); startRename(side, it.id); }
        else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "a") { e.preventDefault(); ui.sel[side] = new Set(o); renderList(side); }
        else if (e.key === "ArrowRight" && side === "L" && e.altKey) { e.preventDefault(); transfer("L", [...ui.sel.L]); }
        else if (e.key === "ArrowLeft" && side === "R" && e.altKey) { e.preventDefault(); transfer("R", [...ui.sel.R]); }
    });
    list.addEventListener("dragstart", e => {
        const row = e.target.closest("[data-id]");
        if (!row) return;
        if (!ui.sel[side].has(row.dataset.id)) select(side, row.dataset.id);
        drag = { side, ids: [...ui.sel[side]] };
        e.dataTransfer.effectAllowed = "copy";
        e.dataTransfer.setData("text/plain", drag.ids.map(id => label(item(side, id), app.st[side])).join("\n"));
    });
    list.addEventListener("dragend", () => { drag = null; clearDrop(); });
}
function clearDrop() { document.querySelectorAll(".drop, .drophere").forEach(e => e.classList.remove("drop", "drophere")); }
function wireDrop(p) {
    const side = p.side, el = p.el;
    el.addEventListener("dragover", e => {
        const files = e.dataTransfer && [...e.dataTransfer.types].includes("Files");
        if ((drag && drag.side !== side) || files) {
            e.preventDefault();
            e.dataTransfer.dropEffect = "copy";
            el.classList.add("drop");
            document.querySelectorAll(".drophere").forEach(x => x.classList.remove("drophere"));
            if (drag && side === "R" && drag.ids.some(id => { const x = item(drag.side, id); return x && x.kind === K.AUDIO; })) {
                const row = e.target.closest("[data-id]"), it = row && item(side, row.dataset.id);
                if (it && it.kind === K.SAMPLER && !fixed(it)) row.classList.add("drophere");
            }
        }
    });
    el.addEventListener("dragleave", e => { if (!el.contains(e.relatedTarget)) el.classList.remove("drop"); });
    el.addEventListener("drop", e => {
        e.preventDefault();
        const row = e.target.closest("[data-id]"), it = row && item(side, row.dataset.id);
        clearDrop();
        if (e.dataTransfer.files && e.dataTransfer.files.length) {
            if (side === "L") ED.drops.add([...e.dataTransfer.files]);
            else dropOnFm1([...e.dataTransfer.files]);
            return;
        }
        if (drag && drag.side !== side) {
            const target = side === "R" && it && it.kind === K.SAMPLER && !fixed(it) ? it.id : null;
            transfer(drag.side, drag.ids, { target });
        }
        drag = null;
    });
}
/* a disk dropped on the FM-1 panel: UPLOAD DISK; anything else goes on the TABLE */
async function dropOnFm1(files) {
    const isDisk = async f => { const b = new Uint8Array(await f.slice(0, 8).arrayBuffer()); const m = String.fromCharCode(...b); return m === "PTFMDISK" || m === "PTFMDATA"; };
    const disks = [];
    for (const f of files) if (await isDisk(f)) disks.push(f);
    if (disks.length !== 1 || files.length !== 1) {
        U.status(disks.length > 1 ? "ONE DISK AT A TIME ON THE FM-1 PANEL: IT TAKES THE PANEL'S PLACE (UPLOAD DISK)"
            : "ONLY A DISK GOES ON THE FM-1 PANEL (UPLOAD DISK): DROP OTHER FILES ON THE TABLE, THEN MOVE WHAT YOU WANT", { err: true });
        return;
    }
    ED.fm1.uploadDisk(disks[0]);
}
function renderSelbar(side) {
    const p = P[side], real = app.st[side], ids = [...ui.sel[side]].filter(id => item(side, id));
    p.selbar.textContent = "";
    const moveBtn = h("button", { class: "btn small", onclick: () => transfer(side, ids), disabled: !ids.length }, side === "L" ? "TO FM-1 →" : "← TO TABLE");
    if (!ids.length) {
        p.selbar.append(h("span", { class: "dim" }, side === "L"
            ? "DROP FILES HERE · DRAG ITEMS TO THE FM-1 OR USE → · DOUBLE-CLICK OPENS A TAB · RIGHT-CLICK FOR MORE"
            : "PICK TO PLAY, EDIT, DELETE OR COPY TO THE TABLE · DROP SAMPLES ON A SAMPLER TO ADD THEM TO IT · DROP A DISK HERE: UPLOAD DISK"));
        return;
    }
    const items = ids.map(id => item(side, id)), own = items.reduce((s, it) => s + (fixed(it) ? 0 : M.size(it)), 0);
    let all = 0;
    const m = M.byIdMap(real);
    for (const id of M.closure(real, ids)) all += M.size(m.get(id));
    const one = items.length === 1 ? items[0] : null;
    p.selbar.append(h("span", { class: "sum" }, one ? label(one, real) : plural(items.length, "ITEM") + " PICKED",
        h("span", null, " · " + (one && one.fw ? "IN FW" : kb(own)) + (all > own ? " · " + kb(all) + " WITH WHAT " + (one ? "IT USES" : "THEY USE") : ""))));
    p.selbar.append(h("span", { class: "acts" },
        one && canPlay(one) ? h("button", { class: "btn small", onclick: () => play(one) }, isPlaying(one) ? "■ STOP" : "▶ PLAY") : null,
        one && !one.dim ? h("button", { class: "btn small", onclick: () => openTab(one) }, "✎ EDIT") : null,
        h("button", { class: "btn small danger", onclick: () => askDelete(side, ids) }, "DELETE"), moveBtn));
    if (one) {
        const usesIds = fixed(one) ? [] : U.uniq(Object.values(one.link).filter(t => t && t !== M.BUILTIN));
        const bi = fixed(one) ? [] : U.uniq(M.refs(one).filter(f => one.link[M.lk(f.sp, f.num)] === M.BUILTIN).map(f => f.num));
        const uses = usesIds.map(id => item(side, id)).filter(Boolean), by = (ui.users[side].get(one.id) || []).map(id => item(side, id)).filter(Boolean);
        const refs = h("div", { class: "refs" });
        const link = it => h("span", { class: "ref", onclick: () => selectIds(side, [it.id]) }, label(it, real));
        if (uses.length || bi.length) {
            refs.append("USES ");
            uses.slice(0, 8).forEach((u, i) => { if (i) refs.append(", "); refs.append(link(u)); });
            if (uses.length > 8) refs.append(" AND " + (uses.length - 8) + " MORE");
            if (bi.length) refs.append((uses.length ? ", " : "") + "BUILT-IN " + bi.map(H2).join(" "));
        }
        if (by.length) {
            refs.append(uses.length || bi.length ? " · USED BY " : "USED BY ");
            by.slice(0, 6).forEach((u, i) => { if (i) refs.append(", "); refs.append(link(u)); });
            if (by.length > 6) refs.append(" AND " + (by.length - 6) + " MORE");
        }
        if (!uses.length && !bi.length && !by.length) refs.append(one.fw ? "IN EVERY PT/FM'S FIRMWARE" : one.isDefault ? "THE FIRMWARE'S DEFAULT: NOTHING SAVED" : "USES NOTHING · USED BY NOTHING");
        p.selbar.append(refs);
    }
}
function showMenu(x, y, side) {
    const ids = [...ui.sel[side]], one = ids.length === 1 ? item(side, ids[0]) : null, real = app.st[side];
    const nSamples = side === "L" ? ED.optimize.samplesOf(real, ids).length : 0;
    U.menu(x, y, [
        { label: one && isPlaying(one) ? "STOP" : "PLAY", fn: () => play(one), off: !one || !canPlay(one), key: "SPACE" },
        { label: "EDIT IN A TAB", fn: () => openTab(one), off: !one || one.dim, key: "ENTER" },
        { label: "RENAME", fn: () => startRename(side, one.id), off: !one || !canRename(one), key: "F2" },
        "-",
        { label: side === "L" ? "COPY TO THE FM-1  →" : "←  COPY TO THE TABLE", fn: () => transfer(side, ids), key: side === "L" ? "ALT+→" : "ALT+←" },
        { label: "SELECT WHAT IT USES", fn: () => selectIds(side, [...M.closure(real, ids)].filter(i => !ids.includes(i))), off: !ids.length },
        { label: "SELECT WHAT USES IT", fn: () => selectIds(side, U.uniq(ids.flatMap(i => ui.users[side].get(i) || []))), off: !ids.some(i => (ui.users[side].get(i) || []).length) },
        side === "L" ? "-" : null,
        side === "L" ? { label: "OPTIMIZE " + (nSamples ? plural(nSamples, "SAMPLE") : "SAMPLES") + "…", fn: () => askOptimize(side, ids), off: !nSamples } : null,
        "-",
        { label: "DELETE", fn: () => askDelete(side, ids), cls: "danger", key: "DEL" }].filter(Boolean));
}
document.addEventListener("mousedown", e => { if (!e.target.closest(".menu")) U.closeMenu(); });

/* ---- OPTIMIZE SAMPLES (the TABLE): the samples picked, a picked sampler's with them, at one level, as each one's
 * tab makes it; what every level saves shown first; one step to UNDO ---- */
const OPT_LEVELS = [["lossless", "LOSSLESS", "NOTHING HEARD CHANGES"], ["smaller", "SMALLER", "EACH AT ITS PROPOSED RATE"],
    ["smallest", "SMALLEST", "8 BITS, A SOFTER TOP; 6 DB LOWER WHERE INSTRUMENTS PLAY IT (THEIR LEVEL UP 6 DB)"]];
let optLevel = 1;
function askOptimize(side, ids) {
    const real = app.st[side], O = ED.optimize, n = O.samplesOf(real, ids).length;
    if (!n) return;
    U.busy("OPTIMIZE: TRYING " + plural(n, "SAMPLE") + " AT EVERY LEVEL…", 0.5);
    setTimeout(() => {
        const plans = OPT_LEVELS.map(([l]) => O.plan(real, ids, l));
        U.status("");
        const line = (p, i) => {
            const [, name, what] = OPT_LEVELS[i], saved = p.before - p.after;
            return h("label", { class: "sw" }, h("input", { type: "radio", name: "optlv", checked: i === optLevel, disabled: !p.results.length,
                onchange: () => { optLevel = i; } }),
                h("span", null, h("b", null, name), " · " + (p.results.length ? kb(p.before) + " → " + kb(p.after) + " (−" + kb(saved) + "), " +
                    p.results.length + " OF " + p.picked + " SMALLER" : "NOTHING SAVED"), h("br"), h("span", { class: "dim" }, what)));
        };
        if (!plans[optLevel].results.length) optLevel = plans.findIndex(p => p.results.length);
        const failed = plans.reduce((a, p) => Math.max(a, p.failed), 0);
        U.dialog({ title: "OPTIMIZE " + plural(n, "SAMPLE"), body: h("div", null,
            h("p", null, "EACH SAMPLE MADE AGAIN AS ITS TAB'S OPTIMIZE MAKES IT: ITS INSTRUMENTS FOLLOW (START, END, LOOP, LEVEL). A SAMPLE NOTHING WOULD SAVE ON STAYS AS IT IS."),
            plans.map(line),
            failed ? h("p", { class: "dim" }, plural(failed, "SAMPLE") + " COULD NOT BE MADE AGAIN: LEFT AS THEY ARE.") : null),
            buttons: optLevel < 0 ? [{ label: "CLOSE", cls: "primary" }] : [
                { label: "OPTIMIZE", cls: "primary", onClick: () => {
                    const p = plans[optLevel];
                    app.commit(O.apply(app.st, side, p), "OPTIMIZED " + plural(p.results.length, "SAMPLE") + " (" + OPT_LEVELS[optLevel][1] + "): −" + kb(p.before - p.after));
                } },
                { label: "CANCEL" }] });
        if (optLevel < 0) optLevel = 1;
    }, 30);
}

/* ---- renaming: a name in the item's record (songs, cartridges, samples); a pattern's on the TABLE only ---- */
function canRename(it) {
    if (fixed(it) || it.dim || SYS_KINDS.includes(it.kind)) return false;
    if (it.kind === K.PATTERN) return it.side === "L";
    return true;
}
const NAME_W = it => it.kind === K.SONG ? 16 : M.isCart(it.kind) ? 12 : it.kind === K.AUDIO ? 10 : 16;
function renamed(it, v, items) {
    if (it.kind === K.PATTERN) return Object.assign({}, it, { name: v });
    if (it.kind === K.SONG || M.isCart(it.kind)) {
        const b = it.recs[0].payload.slice();
        b.set(R.packName(v, NAME_W(it)), 0);
        return Object.assign({}, it, { recs: [Object.assign({}, it.recs[0], { payload: b })].concat(it.recs.slice(1)) });
    }
    if (it.kind === K.AUDIO) {
        if (!it.owner) {
            const e = M.sampleEntry(it, items);
            return Object.assign({}, it, { entry: M.packedEntry(Object.assign({}, e, { name: v })), name: v });
        }
        return null;                                /* (its name is its sampler's: renamed there) */
    }
    return null;
}
function startRename(side, id) {
    const it = item(side, id);
    if (!it || !canRename(it)) return;
    if (ui.view[side] !== "details") { ui.view[side] = "details"; P[side].views.paint(); renderList(side); }
    const cell = P[side].list.querySelector(`.row[data-id="${CSS.escape(id)}"] .c-name`);
    if (!cell) return;
    const max = NAME_W(it), real = app.st[side];
    const inp = h("input", { type: "text", value: nameText(it, real), maxLength: max });
    cell.textContent = "";
    cell.append(inp);
    inp.focus(); inp.select();
    let done = false;
    const finish = ok => {
        if (done) return;
        done = true;
        const v = R.cleanName(inp.value, max);
        if (ok && v && v !== nameText(it, real)) {
            let n = renamed(it, v, real), st = app.st;
            if (!n && it.kind === K.AUDIO && it.owner) {
                /* an owned sample: its entry in its sampler */
                const o = real.find(x => x.id === it.owner), b = o.recs[0].payload, s = R.unpackSampler(b);
                const at = 11 + s.instruments.length * R.SAMPLER_SLOT + it.idx * R.SAMPLE_ENTRY;
                const nb = b.slice();
                nb.set(M.packedEntry(Object.assign({}, s.samples[it.idx], { name: v })), at);
                st = M.replaceItem(st, side, Object.assign({}, o, { recs: [Object.assign({}, o.recs[0], { payload: nb })] }));
            } else if (n) st = M.replaceItem(st, side, n);
            app.commit(st, "RENAMED TO " + v);
        } else render();
        P[side].list.focus();
    };
    inp.addEventListener("keydown", e => { if (e.key === "Enter") finish(true); if (e.key === "Escape") finish(false); e.stopPropagation(); });
    inp.addEventListener("blur", () => finish(true));
}

/* ---- moving ---- */
function transfer(from, ids, o) {
    o = o || {};
    const to = from === "L" ? "R" : "L";
    const all = ids.map(id => item(from, id)).filter(Boolean);
    const fixedOnes = all.filter(fixed);
    ids = all.filter(it => !fixed(it)).map(it => it.id);
    if (!ids.length) {
        U.status(fixedOnes.length ? (fixedOnes[0].fw ? "BUILT-IN INSTRUMENTS ARE IN EVERY PT/FM'S FIRMWARE: NOTHING TO MOVE" : "A DEFAULT HAS NOTHING IN IT TO MOVE") : "PICK SOMETHING TO MOVE FIRST", { err: !!fixedOnes.length });
        return false;
    }
    const stamp = "FM-1 " + U.nowLabel();
    const r = M.transfer(app.st, from, ids, { target: o.target, layout: app.st.fm1.layout, stamp });
    if (!r.ok) {
        if (r.need) {
            ui.need = { blocks: Math.ceil(r.need / 4096), msg: r.msg.replace(/ NOTHING MOVED\.$/, "."), until: Date.now() + 6000 };
            renderSpace();
            setTimeout(renderSpace, 6100);
        }
        U.status(r.msg, { err: true });
        return false;
    }
    const go = () => app.commit(r.st, r.msg, { ok: true, select: { side: to, ids: r.ids } });
    const rep = (r.replaced || []).map(id => app.st[to].find(x => x.id === id)).filter(Boolean);
    if (rep.length) {
        U.dialog({ title: "REPLACE ON THE FM-1 PANEL?", body: h("div", null,
            h("ul", null, rep.map(x => h("li", null, M.nameOf(x, app.st[to]) + ": THE ONE YOU MOVE TAKES THE PLACE OF THIS PANEL'S"))),
            h("p", { class: "dim" }, "THE FM-1 KEEPS ONE OF EACH. NOTHING CHANGES ON THE FM-1 UNTIL WRITE; UNDO TAKES IT BACK.")),
            buttons: [{ label: "REPLACE", cls: "primary", onClick: go }, { label: "CANCEL" }] });
    } else go();
    return true;
}

/* ---- DELETE ---- */
function askDelete(side, ids) {
    const real = app.st[side];
    const all = ids.map(id => item(side, id)).filter(Boolean);
    const fw = all.filter(fixed);
    ids = all.filter(it => !fixed(it)).map(it => it.id);
    if (!ids.length) {
        if (fw.length) U.status(fw[0].fw ? "BUILT-IN INSTRUMENTS LIVE IN THE FIRMWARE: NOTHING TO DELETE" : "A DEFAULT HAS NOTHING TO DELETE", { err: true });
        return;
    }
    /* what uses it, and what uses that, to the end, goes with it: named here (each a link to it), deleted with it */
    const us = M.usersAll(real, ids);
    const asked = ids.length;
    ids = ids.concat(us.ids);
    const extra = M.exclusive(real, ids);
    const base = M.remove(app.st, side, ids, false), withExtra = M.remove(app.st, side, ids, true);
    let also = true;
    const sum = h("div", { class: "box" });
    const describe = gone => {
        const cnt = {};
        for (const id of gone) { const it = real.find(x => x.id === id); if (!it) continue; const k = M.oneOf(it); cnt[k] = (cnt[k] || 0) + 1; }
        return Object.entries(cnt).map(([k, n]) => plural(n, k)).join(", ");
    };
    const paint = () => { sum.textContent = also && extra.size ? "ALSO GOES: " + describe(extra) + " · FREES " + kb(withExtra.bytes) : "FREES " + kb(base.bytes); };
    paint();
    const items = ids.map(id => item(side, id)), mine = items.slice(0, asked);
    const back = items.filter(it => it.builtin && it.side === "R");
    const sys = side === "R" ? us.ids.map(id => item(side, id)).filter(it => M.groupOf(it) === "SYSTEM") : [];
    const ref = id => h("span", { class: "ref", onclick: () => { U.closeDialog(); selectIds(side, [id]); } }, label(item(side, id), real));
    U.dialog({ title: "DELETE " + (mine.length === 1 ? label(mine[0], real) : plural(mine.length, "ITEM")) + (us.ids.length ? " AND WHAT USES IT?" : "?"), body: h("div", null,
        mine.length > 1 ? h("ul", null, mine.slice(0, 8).map(it => h("li", null, label(it, real))), mine.length > 8 ? h("li", null, "AND " + (mine.length - 8) + " MORE") : null) : null,
        us.ids.length ? h("p", null, "IT IS USED. WHAT USES IT, AND WHAT USES THAT, GOES TOO: " + describe(us.ids) + ".") : null,
        us.ids.length ? h("ul", null, us.pairs.slice(0, 12).map(([u, id]) => h("li", null, ref(u), " USES ", label(item(side, id), real))),
            us.pairs.length > 12 ? h("li", null, "AND " + (us.pairs.length - 12) + " MORE") : null) : null,
        sys.length ? h("p", null, sys.map(it => label(it, real)).join(", ") + " GO" + (sys.length === 1 ? "ES" : "") + " BACK TO " + (sys.length === 1 ? "ITS DEFAULT" : "THEIR DEFAULTS") + ".") : null,
        extra.size ? h("label", { class: "sw" }, h("input", { type: "checkbox", checked: true, onchange: e => { also = e.target.checked; paint(); } }), "ALSO WHAT ONLY IT USES") : null,
        sum,
        back.length ? h("p", null, "A CHANGED BUILT-IN GOES BACK TO THE FIRMWARE'S OWN (BACK TO BUILT-IN).") : null,
        side === "R" ? h("p", { class: "dim" }, "NOTHING CHANGES ON THE FM-1 UNTIL WRITE.") : null),
        buttons: [{ label: us.ids.length ? "DELETE ALL" : "DELETE", cls: "primary danger", onClick: () => {
            const r = also ? withExtra : base;
            app.commit(r.st, "DELETED " + describe(r.gone) + " · " + kb(r.bytes) + " · " + (side === "R" ? "THE FM-1 CHANGES WITH WRITE" : "FROM THE TABLE"));
            ui.sel[side].clear();
        } }, { label: "CANCEL" }] });
}
function deleteAll(side) {
    if (side === "L") {
        if (!app.st.L.length) { U.status("THE TABLE IS ALREADY EMPTY"); return; }
        U.dialog({ title: "DELETE ALL FROM THE TABLE?", body: h("p", null, "EVERYTHING ON THE TABLE GOES: " + plural(app.st.L.length, "ITEM") + ", " + kb(app.st.L.reduce((s, i) => s + M.size(i), 0)) + ". UNDO BRINGS IT BACK."),
            buttons: [{ label: "DELETE ALL", cls: "primary danger", onClick: () => { ui.sel.L.clear(); app.commit(M.deleteAll(app.st, "L"), "THE TABLE IS EMPTY"); } }, { label: "CANCEL" }] });
    } else {
        U.dialog({ title: "EMPTY THE FM-1 PANEL?", body: h("div", null,
            h("p", null, "EVERYTHING GOES BUT THE BUILT-IN INSTRUMENTS; CHANGED BUILT-INS GO BACK TO THE FIRMWARE'S. SETTINGS, MIDI MAP, LOOPS AND DJ SETUP GO BACK TO THEIR DEFAULTS."),
            h("p", { class: "dim" }, "THE FM-1 ITSELF CHANGES ONLY WITH WRITE, WHICH THEN EMPTIES ITS USER AREA. UNDO BRINGS THE PANEL BACK.")),
            buttons: [{ label: "DELETE ALL", cls: "primary danger", onClick: () => { ui.sel.R.clear(); app.commit(M.deleteAll(app.st, "R"), "THE FM-1 PANEL IS EMPTY: A WRITE WOULD EMPTY THE FM-1"); } }, { label: "CANCEL" }] });
    }
}

/* ---- + NEW on the TABLE: an item of each kind as the FM-1 makes a new one, opened in its tab ---- */
const NEW_KINDS = [[K.SONG, "SONG (WITH AN EMPTY PATTERN)"], [K.PATTERN, "PATTERN"], "-",
    [K.DX7, "DX7 CARTRIDGE"], [K.JUNO, "JUNO-106 CARTRIDGE"], [K.TB303, "TB-303 CARTRIDGE"], [K.CHIP, "CHIP CARTRIDGE"],
    [K.FUSION, "FUSION CARTRIDGE"], [K.SAMPLER, "SAMPLER (DROP SOUNDS ON IT)"], "-",
    [K.SETTINGS, "SETTINGS"], [K.MIDIMAP, "MIDI MAP"], [K.LOOPS, "LOOPS"], [K.DJ, "DJ SETUP"]];
async function makeNew(kind) {
    const mk = (k, recs, more) => Object.assign({ id: M.newId(), side: "L", kind: k, num: null, orig: null, link: {}, recs, src: "NEW" }, more);
    const one = (k, payload) => [{ sub: 0, ver: R.VERSION[k], payload }];
    let made;
    try {
        if (kind === K.PATTERN || kind === K.SONG) {
            /* (the FM-1's NEW PATTERN: 8 tracks of 64 rows, empty) */
            const parts = R.packPattern({ tracks: 8, rows: 64, step: 1, cells: new Map() });
            const pat = mk(K.PATTERN, parts.map((payload, sub) => ({ sub, ver: R.VERSION[K.PATTERN], payload })), { name: "NEW PATTERN" });
            made = [pat];
            if (kind === K.SONG) made.unshift(mk(K.SONG, one(K.SONG, R.packSong(R.newSong("NEW SONG"))), { link: { [M.lk("p", 0)]: pat.id } }));
        } else if (kind === K.SAMPLER) made = [mk(kind, one(kind, M.samplerBytes("NEW SAMPLER", [], [])))];
        else if (M.isCart(kind)) {
            const name = "NEW " + M.ENGINE[kind];
            made = [mk(kind, one(kind, R.packCart(kind, { name, slots: [ED.defs.newSlot(kind, name)] })))];
        } else {
            await ED.pics.ready();                  /* (the system items' defaults: the firmware's own) */
            made = [mk(kind, ED.tabs.defaultRecs({ kind, num: 0 }))];
        }
    } catch (e) { U.status("CANNOT MAKE IT: " + String(e.message || e).toUpperCase(), { err: true }); return; }
    app.commit(Object.assign({}, app.st, { L: app.st.L.concat(made) }), "NEW ON THE TABLE: " + made.map(it => label(it, app.st.L.concat(made))).join(", "));
    ui.sel.L = new Set([made[0].id]);
    ED.tabs.open(made[0]);
}

/* ---- the page's own choices, kept with the panels ---- */
let saveT = 0;
function saveSoon() { clearTimeout(saveT); saveT = setTimeout(() => ED.keep.save(app.saved()), 300); }
function prefs() { return { view: ui.view, sort: ui.sort, closed: ui.closed }; }
function setPrefs(p) {
    if (!p) return;
    for (const side of ["L", "R"]) {
        if (p.view && p.view[side]) ui.view[side] = p.view[side];
        if (p.sort && p.sort[side]) ui.sort[side] = p.sort[side];
        if (p.closed && Array.isArray(p.closed[side])) ui.closed[side] = p.closed[side];
        if (P[side]) P[side].views.paint();
    }
}
const selected = side => [...ui.sel[side]].map(id => item(side, id)).filter(Boolean);
const active = () => ui.active;

ED.files = { build, render, renderSpace, prefs, setPrefs, selected, selectIds, transfer, askDelete, makeNew, item, viewItems, active,
    label, nameText, kindText, numText, fixed };
})(globalThis.ED = globalThis.ED || {});
