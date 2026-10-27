/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The PATTERN tab (specs/editor.md, The tabs: PATTERN), made for the mouse and the finger: the grid as the FM-1's
 * (tracks across, rows down, the beat's rows lighter, the playing row marked; FULL, NOTE + INSTRUMENT, NOTES), a click
 * or a tap picks a cell, a drag marks a block (on a touch screen with MARK BY DRAG on); the cell's panel (a keyboard of
 * the FM-1's 27 keys, NOTE OFF, NO NOTE, the instrument by name, the volume, up to four commands with their words);
 * COPY, CUT, PASTE, CLEAR, TRANSPOSE, INSERT ROW, DELETE ROW, the EDIT STEP; the computer's keys as the FM-1's. Also
 * the EDITOR'S PATTERN. */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, h = U.h, H2 = M.H2, C = ED.cmds;
const clamp = U.clamp;
/* the computer's keys as the FM-1's (F3 up): whites Z X C V B N M , . / and Q W E R T Y, blacks S D F H J L ; ' 3 4 6 */
const KEYMAP = { z: 53, s: 54, x: 55, d: 56, c: 57, f: 58, v: 59, b: 60, h: 61, n: 62, j: 63, m: 64, ",": 65, l: 66, ".": 67, ";": 68, "/": 69,
    "'": 70, q: 71, w: 72, 3: 73, e: 74, 4: 75, r: 76, t: 77, 6: 78, y: 79 };

function decode(d) { return R.unpackPattern(d.recs.map(r => r.payload)); }
function encode(d, p, link) {
    const parts = R.packPattern(p);
    return Object.assign({}, d, { recs: parts.map((payload, i) => ({ sub: i, ver: d.recs[0] ? d.recs[0].ver : R.VERSION[K.PATTERN], payload })), link: link || d.link });
}
/* the cartridges an instrument is picked from: the built-ins, then the panel's own */
function cartList(side, st) {
    const out = [];
    const seen = new Set();
    for (const b of M.builtins) {
        if (!M.isCart(b.kind) || seen.has(b.num)) continue;
        seen.add(b.num);
        /* (on the FM-1 panel the number names the changed one it holds) */
        const changed = side === "R" && st.R.find(i => M.isCart(i.kind) && i.num === b.num);
        out.push(changed ? { key: changed.id, num: b.num, kind: changed.kind, payload: changed.recs[0].payload, label: H2(b.num), item: changed }
            : { key: "B" + b.num, num: b.num, kind: b.kind, payload: b.payload, label: H2(b.num) });
    }
    for (const c of st[side]) if (M.isCart(c.kind) && !c.dim && !(side === "R" && c.num >= 0xF0))
        out.push({ key: c.id, num: c.num, kind: c.kind, payload: c.recs[0].payload, label: side === "R" ? H2(c.num) : "--", item: c });
    return out;
}
function slotNames(kind, payload) {
    try {
        if (kind === K.SAMPLER) return R.unpackSampler(payload).instruments.map(s => s && s.name);
        return R.unpackCart(kind, payload).slots.map(s => s && s.name);
    } catch (e) { return []; }
}

ED.tabs.kinds.pattern = function (t, api) {
    const side = t.side;
    const pv = t.pv = t.pv || { cur: { r: 0, t: 0 }, a: { r: 0, t: 0 }, b: { r: 0, t: 0 }, oct: 4, view: "full", block: false, ins: null };
    let p = decode(api.draft);
    pv.step = pv.step != null ? pv.step : p.step;
    const grid = h("div", { class: "pgrid", tabIndex: 0, "aria-label": "THE PATTERN'S GRID" });
    const insp = h("div", { class: "insp" });
    const info = h("span", { class: "info" });
    const carts = () => cartList(side, api.st);
    /* what a cell's instrument is: the cartridge (its kind, its label) by the draft's link */
    function cartOf(num) {
        const d = api.draft;
        let t2 = d.link[M.lk("c", num)];
        if (side === "R" && num >= 0xF0 && (t2 === M.BUILTIN || t2 == null)) {
            const ch = api.st.R.find(x => M.isCart(x.kind) && x.num === num);
            if (ch) t2 = ch.id;
        }
        if (t2 === M.BUILTIN || (t2 == null && num >= 0xF0)) { const b = M.builtinAt("c", num); return b ? { kind: b.kind, label: H2(num), key: "B" + num, payload: b.payload } : null; }
        const it = t2 && api.st[side].find(x => x.id === t2);
        return it ? { kind: it.kind, label: side === "R" ? H2(it.num) : "--", key: it.id, payload: it.recs[0].payload } : null;
    }
    /* the number a cartridge has in the draft's records (a new one given on the TABLE) */
    function numFor(c) {
        if (c.key[0] === "B" && !c.item) return c.num;
        if (side === "R") return c.num;
        const d = api.draft;
        for (const k in d.link) if (k[0] === "c" && d.link[k] === c.key) return parseInt(k.slice(1), 16);
        const used = new Set(Object.keys(d.link).filter(k => k[0] === "c").map(k => parseInt(k.slice(1), 16)));
        let n = c.item && c.item.orig != null && c.item.orig < 0xF0 && !used.has(c.item.orig) ? c.item.orig : 0;
        while (used.has(n)) n++;
        return n;
    }
    function rpb() {
        const s = api.st[side].find(x => x.kind === K.SONG && Object.values(x.link).includes(t.itemId));
        return s ? Math.max(1, s.recs[0].payload[15]) : 4;
    }
    const rect = () => ({ r0: Math.min(pv.a.r, pv.b.r), r1: Math.max(pv.a.r, pv.b.r), t0: Math.min(pv.a.t, pv.b.t), t1: Math.max(pv.a.t, pv.b.t) });
    const key = (r, tt) => r + "," + tt;
    const cellAt = (r, tt) => p.cells.get(key(r, tt)) || null;
    function lastIns(r, tt) { for (let rr = r; rr >= 0; rr--) { const x = cellAt(rr, tt); if (x && x.ins >= 0) return x.ins; } return -1; }
    function engineAt(r, tt) { const ins = lastIns(r, tt); const c = ins >= 0 ? cartOf(ins >> 8) : null; return c ? c.kind : null; }
    /* a change: the cells changed, encoded, the draft set */
    function commit(what, link) {
        api.set(encode(api.draft, p, link), what);
        draw(); drawInsp();
    }
    function mutate(fn, what) {
        const cells = new Map(p.cells);
        fn(cells);
        for (const [k, c] of cells) if (!c.note && c.ins < 0 && c.vol < 0 && !(c.cmds && c.cmds.length)) cells.delete(k);
        p = Object.assign({}, p, { cells });
        commit(what);
    }
    const editCell = (cells, r, tt) => { const c = Object.assign({ note: 0, ins: -1, vol: -1, cmds: [] }, cells.get(key(r, tt)) || {}); c.cmds = (c.cmds || []).map(x => x.slice()); cells.set(key(r, tt), c); return c; };

    /* ---- the toolbar ---- */
    const tf = h("input", { type: "number", min: 1, max: 16, value: p.tracks, title: "TRACKS, 1-16" });
    const rf = h("input", { type: "number", min: 1, max: 128, value: p.rows, title: "ROWS, 1-128" });
    const resize = () => {
        const nt = clamp(+tf.value || 1, 1, 16), nr = clamp(+rf.value || 1, 1, 128);
        if (nt === p.tracks && nr === p.rows) return;
        const cells = new Map();
        for (const [k, c] of p.cells) { const [r, tt] = k.split(",").map(Number); if (r < nr && tt < nt) cells.set(k, c); }
        p = Object.assign({}, p, { tracks: nt, rows: nr, cells });
        pv.cur = { r: Math.min(pv.cur.r, nr - 1), t: Math.min(pv.cur.t, nt - 1) }; pv.a = Object.assign({}, pv.cur); pv.b = Object.assign({}, pv.cur);
        commit("SIZE " + nt + " TRACKS × " + nr + " ROWS");
    };
    tf.addEventListener("change", resize); rf.addEventListener("change", resize);
    const stepSel = h("select", { title: "THE EDIT STEP: ROWS A NOTE MOVES ON", onchange: e => { pv.step = +e.target.value; } },
        Array.from({ length: 17 }, (_, i) => h("option", { value: i, selected: i === pv.step }, String(i))));
    const views = h("div", { class: "seg" }, ["full", "noteins", "notes"].map((v, i) => h("button", { class: pv.view === v ? "on" : "", onclick: e => {
        pv.view = v; [...e.currentTarget.parentNode.children].forEach(b => b.classList.toggle("on", b === e.currentTarget)); draw(); } }, ["FULL", "NOTE + INSTRUMENT", "NOTES"][i])));
    const playBtn = h("button", { class: "btn small", title: "PLAY THE PATTERN AS IT IS NOW (SPACE)", onclick: () => play() }, "▶ PLAY");
    const tools = h("div", { class: "ptb" },
        info, views,
        h("span", { class: "info" }, "TRACKS"), tf, h("span", { class: "info" }, "ROWS"), rf,
        h("span", { class: "info" }, "STEP"), stepSel,
        playBtn,
        h("button", { class: "btn small", onclick: () => doCopy(false) }, "COPY"), h("button", { class: "btn small", onclick: () => doCopy(true) }, "CUT"),
        h("button", { class: "btn small", onclick: doPaste }, "PASTE"), h("button", { class: "btn small", onclick: doClear }, "CLEAR"),
        h("button", { class: "btn small", onclick: () => transpose(-12) }, "−12"), h("button", { class: "btn small", onclick: () => transpose(-1) }, "−1"),
        h("button", { class: "btn small", onclick: () => transpose(1) }, "+1"), h("button", { class: "btn small", onclick: () => transpose(12) }, "+12"),
        h("button", { class: "btn small", onclick: () => shiftRows(1) }, "INSERT ROW"), h("button", { class: "btn small", onclick: () => shiftRows(-1) }, "DELETE ROW"),
        h("button", { class: "btn small" + (pv.block ? " on" : ""), title: "ON A TOUCH SCREEN, A DRAG MARKS A BLOCK INSTEAD OF SCROLLING",
            onclick: e => { pv.block = !pv.block; e.currentTarget.classList.toggle("on", pv.block); grid.classList.toggle("block", pv.block); } }, "MARK BY DRAG"));
    grid.classList.toggle("block", pv.block);

    /* ---- the grid ---- */
    function trackEngine(tt) {
        const cnt = {};
        for (let r = 0; r < p.rows; r++) { const x = cellAt(r, tt); if (x && x.ins >= 0) { const c = cartOf(x.ins >> 8); if (c) cnt[c.kind] = (cnt[c.kind] || 0) + 1; } }
        const k = Object.keys(cnt).sort((a, b) => cnt[b] - cnt[a])[0];
        return k ? +k : null;
    }
    function draw() {
        info.textContent = p.tracks + " TRACKS × " + p.rows + " ROWS";
        const sel = rect(), beat = rpb(), last = {};
        const w = pv.view === "full" ? "17ch" : pv.view === "noteins" ? "9ch" : "3ch";
        let html = '<div class="ph"><span class="pr"> </span>';
        for (let tt = 0; tt < p.tracks; tt++) {
            const e = trackEngine(tt);
            html += `<span class="pth" style="width:calc(${w} + 14px);border-bottom-color:${e ? U.ENG_COL[e] : "transparent"}">${H2(tt)}${e && pv.view !== "notes" ? " " + M.ENGINE[e] : ""}</span>`;
        }
        html += "</div>";
        for (let r = 0; r < p.rows; r++) {
            html += `<div class="prow${r % beat === 0 ? " beat" : ""}" data-r="${r}"><span class="pr">${H2(r)}</span>`;
            for (let tt = 0; tt < p.tracks; tt++) {
                const x = cellAt(r, tt);
                if (x && x.ins >= 0) last[tt] = x.ins;
                const c = last[tt] != null ? cartOf(last[tt] >> 8) : null, col = c ? U.ENG_COL[c.kind] : U.COL.white;
                const inS = r >= sel.r0 && r <= sel.r1 && tt >= sel.t0 && tt <= sel.t1;
                const cls = "pc" + (inS ? " sel" : "") + (pv.cur.r === r && pv.cur.t === tt ? " cur" : "");
                const nt = x && x.note ? `<span style="color:${x.note === 1 ? U.COL.lgrey : col}">${R.noteName(x.note)}</span>` : '<span class="e">---</span>';
                const ci = x && x.ins >= 0 ? cartOf(x.ins >> 8) : null;
                const ins = x && x.ins >= 0 ? `<span class="i"${ci ? "" : ' style="color:var(--lred)"'}>${ci ? ci.label : H2(x.ins >> 8)}:${H2(x.ins & 255)}</span>` : '<span class="e">--:--</span>';
                const vol = x && x.vol >= 0 ? `<span class="v">${H2(x.vol)}</span>` : '<span class="e">--</span>';
                const cm = x && x.cmds && x.cmds.length ? `<span class="x">${C.text(x.cmds[0])}${x.cmds.length > 1 ? "+" : " "}</span>` : '<span class="e">... </span>';
                const inner = pv.view === "full" ? `${nt} ${ins} ${vol} ${cm}` : pv.view === "noteins" ? `${nt} ${ins}` : nt;
                html += `<span class="${cls}" data-r="${r}" data-t="${tt}">${inner}</span>`;
            }
            html += "</div>";
        }
        const sc = [grid.scrollTop, grid.scrollLeft];
        grid.innerHTML = html;
        [grid.scrollTop, grid.scrollLeft] = sc;
        playRow = -1;
    }
    function paintSel() {
        const s = rect();
        grid.querySelectorAll(".pc").forEach(e => {
            const r = +e.dataset.r, tt = +e.dataset.t;
            e.classList.toggle("sel", r >= s.r0 && r <= s.r1 && tt >= s.t0 && tt <= s.t1);
            e.classList.toggle("cur", r === pv.cur.r && tt === pv.cur.t);
        });
    }
    /* the playing row, while the preview plays this pattern */
    let playRow = -1;
    const timer = setInterval(() => {
        if (!grid.isConnected) { clearInterval(timer); return; }
        const pl = ED.player, now = pl.now, st = pl.state;
        const row = now && now.it.id === t.itemId && st && st.playing && now.job.kind === "pattern" ? st.row : -1;
        if (row === playRow) return;
        const old = grid.querySelector(".prow.play");
        if (old) old.classList.remove("play");
        playRow = row;
        if (row >= 0) { const e = grid.querySelector(`.prow[data-r="${row}"]`); if (e) e.classList.add("play"); }
    }, 60);

    /* ---- the cell's panel ---- */
    function drawInsp() {
        const { r, t: tt } = pv.cur, x = cellAt(r, tt);
        insp.textContent = "";
        insp.append(h("div", { class: "where" }, "ROW " + H2(r) + " · TRACK " + H2(tt)));
        insp.append(h("div", { class: "grp" }, h("h4", null, "NOTE ", h("span", { class: "dim" }, x && x.note ? R.noteName(x.note) : "NONE")),
            piano(pv.oct, n => writeNote(n, false), x && x.note >= 12 ? x.note : null),
            h("div", { class: "octrow" }, h("button", { class: "btn small", onclick: () => { pv.oct = clamp(pv.oct - 1, 1, 7); drawInsp(); } }, "◀ OCTAVE"),
                h("span", { class: "mono" }, R.noteName(53 + (pv.oct - 4) * 12) + "–" + R.noteName(79 + (pv.oct - 4) * 12)),
                h("button", { class: "btn small", onclick: () => { pv.oct = clamp(pv.oct + 1, 1, 7); drawInsp(); } }, "OCTAVE ▶"), h("span", { class: "grow" }),
                h("button", { class: "btn small", onclick: () => writeNote(1, false) }, "NOTE OFF"),
                h("button", { class: "btn small", onclick: () => mutate(cells => { editCell(cells, r, tt).note = 0; }, "NO NOTE") }, "NO NOTE"))));
        /* the instrument by name */
        const sel = h("select", { onchange: e => setIns(e.target.value) });
        sel.append(h("option", { value: "" }, "NO INSTRUMENT (THE TRACK'S LAST)"));
        const cur = x && x.ins >= 0 ? cartOf(x.ins >> 8) : null, curKey = cur ? cur.key + "|" + (x.ins & 255) : "";
        for (const c of carts()) {
            const names = slotNames(c.kind, c.payload);
            const g = h("optgroup", { label: c.label + " " + R.unpackName(c.payload.subarray(0, 9), 12) + " · " + M.ENGINE[c.kind] });
            names.forEach((n, i) => { if (n != null) g.append(h("option", { value: c.key + "|" + i, selected: curKey === c.key + "|" + i }, c.label + ":" + H2(i) + " " + n)); });
            sel.append(g);
        }
        if (x && x.ins >= 0 && !cur) sel.append(h("option", { value: "?", selected: true }, H2(x.ins >> 8) + ":" + H2(x.ins & 255) + " (NOT HERE)"));
        insp.append(h("div", { class: "grp" }, h("h4", null, "INSTRUMENT"), sel));
        /* the volume */
        const vv = x && x.vol >= 0 ? x.vol : 0x7F;
        const out = h("output", null, H2(vv));
        const rng = h("input", { type: "range", min: 0, max: 127, value: vv, disabled: !(x && x.vol >= 0) });
        rng.style.setProperty("--p", (100 * vv / 127) + "%");
        rng.addEventListener("input", () => { out.textContent = H2(+rng.value); rng.style.setProperty("--p", (100 * +rng.value / 127) + "%"); });
        rng.addEventListener("change", () => mutate(cells => { editCell(cells, r, tt).vol = +rng.value; }, "VOLUME " + H2(+rng.value)));
        rng.title = "DOUBLE-CLICK: THE DEFAULT, 7F";
        rng.addEventListener("dblclick", () => { if (!rng.disabled) mutate(cells => { editCell(cells, r, tt).vol = 0x7F; }, "VOLUME 7F"); });
        insp.append(h("div", { class: "grp" }, h("h4", null, "VOLUME"),
            h("label", { class: "octrow", style: "cursor:pointer" }, h("input", { type: "checkbox", checked: !!(x && x.vol >= 0),
                onchange: e => mutate(cells => { editCell(cells, r, tt).vol = e.target.checked ? 0x7F : -1; }, e.target.checked ? "VOLUME SET" : "NO VOLUME") }), "SET", h("span", { class: "grow" }), out), rng));
        /* up to four commands */
        const eng = C.engineName(engineAt(r, tt));
        const ents = C.entries(eng);
        const box = h("div", { class: "grp" }, h("h4", null, "COMMANDS"));
        const cmds = x && x.cmds ? x.cmds : [];
        for (let k = 0; k <= Math.min(3, cmds.length); k++) {
            const cmd = cmds[k], e = cmd ? C.entryOf(cmd, eng) : null;
            const s2 = h("select", { onchange: ev => {
                const id = ev.target.value;
                mutate(cells => {
                    const c = editCell(cells, r, tt);
                    if (!id) c.cmds.splice(k, 1);
                    else { const en = ents.find(z => z.id === id); const v = c.cmds[k] ? C.valueOf(C.entryOf(c.cmds[k], eng), c.cmds[k]) : 0; c.cmds[k] = C.make(en, v); }
                }, id ? "COMMAND" : "COMMAND REMOVED");
            } });
            s2.append(h("option", { value: "" }, "NO COMMAND"));
            const groups = new Map();
            for (const en of ents) { if (!groups.has(en.group)) groups.set(en.group, h("optgroup", { label: en.group })); groups.get(en.group).append(h("option", { value: en.id, selected: e && e.id === en.id }, en.name)); }
            for (const g of groups.values()) s2.append(g);
            const num = h("input", { type: "text", value: cmd ? (e && e.digits === 1 ? (C.valueOf(e, cmd)).toString(16).toUpperCase() : H2(cmd[1])) : "", disabled: !cmd || (e && e.digits === 0),
                maxLength: 2, title: "THE VALUE IN HEX", style: "width:44px;font-family:var(--mono)" });
            num.addEventListener("change", () => {
                const v = parseInt(num.value, 16);
                if (isNaN(v)) return;
                mutate(cells => { const c = editCell(cells, r, tt); c.cmds[k] = e ? C.make(e, v) : [c.cmds[k][0], clamp(v, 0, 255)]; }, "COMMAND VALUE");
            });
            box.append(h("div", { class: "cmdrow" }, s2, num, h("div", { class: "words" }, cmd ? C.text(cmd) + " · " + C.words(cmd, eng) : "")));
        }
        insp.append(box, h("button", { class: "btn small danger", onclick: () => mutate(cells => { cells.delete(key(r, tt)); }, "CELL CLEARED") }, "CLEAR THE CELL"),
            h("p", { class: "hint" }, "KEYS: THE ARROWS MOVE · SHIFT MARKS A BLOCK · Z S X D C … WRITE NOTES AS THE FM-1'S KEYS (F3 UP) · = A NOTE OFF · DELETE CLEARS · CTRL+C X V · SPACE PLAYS"));
    }
    function setIns(v) {
        const { r, t: tt } = pv.cur;
        if (v === "?") return;
        if (!v) { mutate(cells => { editCell(cells, r, tt).ins = -1; }, "NO INSTRUMENT"); return; }
        const [ck, s] = v.split("|"), c = carts().find(z => z.key === ck);
        if (!c) return;
        const n = numFor(c), link = Object.assign({}, api.draft.link, { [M.lk("c", n)]: c.key[0] === "B" && !c.item ? M.BUILTIN : c.key });
        pv.ins = { n, s: +s };
        const cells = new Map(p.cells);
        editCell(cells, r, tt).ins = n << 8 | +s;
        p = Object.assign({}, p, { cells });
        commit("INSTRUMENT " + c.label + ":" + H2(+s), link);
    }
    function writeNote(n, advance) {
        const { r, t: tt } = pv.cur;
        mutate(cells => {
            const c = editCell(cells, r, tt);
            c.note = n;
            if (n >= 12 && c.ins < 0 && pv.ins && lastIns(r, tt) < 0) c.ins = pv.ins.n << 8 | pv.ins.s;
        }, "NOTE " + R.noteName(n));
        if (n >= 12) audition(n, r, tt);
        if (advance && pv.step) { pv.cur = { r: Math.min(p.rows - 1, pv.cur.r + pv.step), t: pv.cur.t }; pv.a = Object.assign({}, pv.cur); pv.b = Object.assign({}, pv.cur); paintSel(); drawInsp(); scrollCur(); }
    }
    /* the note heard on its track's instrument */
    function audition(n, r, tt) {
        const ins = lastIns(r, tt);
        if (ins < 0) return;
        const c = cartOf(ins >> 8);
        if (!c) return;
        const it = c.key[0] === "B" && c.key.length < 6 ? ED.files.item("R", "B:" + c.kind + ":" + (ins >> 8)) || { id: "B:" + c.kind + ":" + (ins >> 8), side: "R", kind: c.kind, num: ins >> 8, fw: true, recs: [{ payload: c.payload }], link: {} }
            : api.st[side].find(x => x.id === c.key);
        if (!it) return;
        ED.player.key(true, n, it, ins & 255).then(() => setTimeout(() => ED.player.key(false, n), 300)).catch(() => {});
    }
    let clip = null;
    function doCopy(cut) {
        const s = rect();
        clip = [];
        for (let r = s.r0; r <= s.r1; r++) { const row = []; for (let tt = s.t0; tt <= s.t1; tt++) { const x = cellAt(r, tt); row.push(x ? Object.assign({}, x, { cmds: (x.cmds || []).map(a => a.slice()) }) : null); } clip.push(row); }
        U.status((cut ? "CUT " : "COPIED ") + U.plural(s.r1 - s.r0 + 1, "ROW") + " × " + U.plural(s.t1 - s.t0 + 1, "TRACK"));
        if (cut) doClear();
    }
    function doPaste() {
        if (!clip) { U.status("COPY A BLOCK FIRST"); return; }
        mutate(cells => clip.forEach((row, i) => row.forEach((x, j) => {
            const r = pv.cur.r + i, tt = pv.cur.t + j;
            if (r >= p.rows || tt >= p.tracks) return;
            if (x) cells.set(key(r, tt), Object.assign({}, x, { cmds: x.cmds.map(a => a.slice()) })); else cells.delete(key(r, tt));
        })), "PASTED");
    }
    function doClear() { const s = rect(); mutate(cells => { for (let r = s.r0; r <= s.r1; r++) for (let tt = s.t0; tt <= s.t1; tt++) cells.delete(key(r, tt)); }, "CLEARED"); }
    function transpose(k) {
        const s = rect();
        mutate(cells => { for (let r = s.r0; r <= s.r1; r++) for (let tt = s.t0; tt <= s.t1; tt++) { const x = cells.get(key(r, tt)); if (x && x.note >= 12) editCell(cells, r, tt).note = clamp(x.note + k, 12, 127); } },
            "TRANSPOSED " + (k > 0 ? "+" : "") + k);
    }
    function shiftRows(dir) {
        const s = rect();
        mutate(cells => {
            for (let tt = s.t0; tt <= s.t1; tt++) {
                const col = [];
                for (let r = 0; r < p.rows; r++) col.push(cells.get(key(r, tt)) || null);
                if (dir > 0) { col.splice(pv.cur.r, 0, null); col.length = p.rows; } else { col.splice(pv.cur.r, 1); col.push(null); }
                col.forEach((x, r) => { if (x) cells.set(key(r, tt), x); else cells.delete(key(r, tt)); });
            }
        }, dir > 0 ? "ROW INSERTED" : "ROW DELETED");
    }
    function play() { ED.player.toggle(api.draft, { st: api.stWith() }); }
    function scrollCur() { const ce = grid.querySelector(`.pc[data-r="${pv.cur.r}"][data-t="${pv.cur.t}"]`); if (ce) ce.scrollIntoView({ block: "nearest", inline: "nearest" }); }

    /* ---- pointing and keys ---- */
    let dragging = false;
    grid.addEventListener("pointerdown", e => {
        const c = e.target.closest(".pc");
        if (!c) return;
        grid.focus({ preventScroll: true });
        const q = { r: +c.dataset.r, t: +c.dataset.t };
        if (e.shiftKey) pv.b = q; else { pv.a = q; pv.b = q; }
        pv.cur = q;
        dragging = e.pointerType !== "touch" || pv.block;
        if (dragging) try { grid.setPointerCapture(e.pointerId); } catch (err) { /* (a pointer already gone) */ }
        paintSel(); drawInsp();
    });
    grid.addEventListener("pointermove", e => {
        if (!dragging) return;
        const el = document.elementFromPoint(e.clientX, e.clientY), c = el && el.closest(".pc");
        if (c && grid.contains(c)) { const q = { r: +c.dataset.r, t: +c.dataset.t }; if (q.r !== pv.b.r || q.t !== pv.b.t) { pv.b = q; paintSel(); } }
    });
    grid.addEventListener("pointerup", () => { dragging = false; });
    grid.addEventListener("keydown", e => {
        const k = e.key;
        let { r, t: tt } = pv.cur;
        if ((e.ctrlKey || e.metaKey) && /^[cxv]$/i.test(k)) { e.preventDefault(); if (k.toLowerCase() === "v") doPaste(); else doCopy(k.toLowerCase() === "x"); return; }
        if (e.ctrlKey || e.metaKey || e.altKey) return;
        if (k === "Delete" || k === "Backspace") { e.preventDefault(); doClear(); return; }
        if (k === " ") { e.preventDefault(); play(); return; }
        if (k === "=") { e.preventDefault(); writeNote(1, true); return; }
        const n = KEYMAP[k.toLowerCase()];
        if (n && k.length === 1) { e.preventDefault(); writeNote(n + (pv.oct - 4) * 12, true); return; }
        if (k === "ArrowDown") r++; else if (k === "ArrowUp") r--; else if (k === "ArrowRight") tt++; else if (k === "ArrowLeft") tt--;
        else if (k === "PageDown") r += 16; else if (k === "PageUp") r -= 16; else if (k === "Home") r = 0; else if (k === "End") r = p.rows - 1;
        else if (k === "Tab") tt += e.shiftKey ? -1 : 1;
        else return;
        e.preventDefault();
        pv.cur = { r: clamp(r, 0, p.rows - 1), t: clamp(tt, 0, p.tracks - 1) };
        if (e.shiftKey && k !== "Tab") pv.b = Object.assign({}, pv.cur); else { pv.a = Object.assign({}, pv.cur); pv.b = Object.assign({}, pv.cur); }
        paintSel(); drawInsp(); scrollCur();
    });
    draw(); drawInsp();
    return h("div", { class: "edview", style: "height:auto;min-height:100%" }, tools, h("div", { class: "pwrap" }, grid, insp));
};

/* a keyboard of the FM-1's 27 keys from the octave's F (shared with the instrument tabs) */
function piano(oct, onNote, lit, onUp) {
    const base = 53 + (oct - 4) * 12, box = h("div", { class: "piano" });
    const whites = [], blacks = [];
    for (let i = 0; i < 27; i++) { const n = base + i; ([1, 3, 6, 8, 10].includes(n % 12) ? blacks : whites).push(n); }
    const ww = 100 / whites.length;
    whites.forEach(n => box.append(h("div", { class: "w" + (lit === n ? " on" : ""), "data-n": n }, n % 12 === 0 ? h("span", null, "C" + (Math.floor(n / 12) - 1)) : null)));
    blacks.forEach(n => { const wi = whites.filter(w => w < n).length; box.append(h("div", { class: "b" + (lit === n ? " on" : ""), "data-n": n, style: { left: `calc(${wi * ww}% - ${ww * 0.3}%)`, width: ww * 0.6 + "%" } })); });
    let down = null;
    box.addEventListener("pointerdown", e => { const k = e.target.closest("[data-n]"); if (k) { e.preventDefault(); down = +k.dataset.n; k.classList.add("on"); onNote(down); } });
    const up = () => { if (down == null) return; const k = box.querySelector(`[data-n="${down}"]`); if (k && lit !== down) k.classList.remove("on"); if (onUp) onUp(down); down = null; };
    box.addEventListener("pointerup", up);
    box.addEventListener("pointerleave", up);
    return box;
}
ED.tabs.piano = piano;
ED.tabs.KEYMAP = KEYMAP;
ED.tabs.kinds.patternDecode = decode;
})(globalThis.ED = globalThis.ED || {});
