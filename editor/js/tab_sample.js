/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The SAMPLE tab (specs/editor.md, The tabs: SAMPLE; Recording and cutting sounds): the sample's waveform as the draft
 * has it, with a selection dragged across it (nothing selected at first): PLAY THE SELECTION, TRIM, TRIM SILENCE, WHOLE
 * SAMPLE, SAVE AS A NEW SAMPLE; the slices (DETECT SLICES, SENSITIVITY, markers dragged, added and deleted, a slice
 * picked by a click, each with its ▶; REMOVE SILENCE, REMOVE, SAVE AS SAMPLES, MAKE A KIT); OPTIMIZE (LOSSLESS,
 * SMALLER, SMALLEST: ED.conv.optimize); RATE (the proposed one marked); DEPTH (8 or 16 bits); the original's (one
 * channel: a sound is mixed to one as it comes) and the result's size; the instruments that play it, which follow a
 * change on SAVE (their points where the same sound now is, a point in a removed part at its join, a loop left with no
 * length OFF; their LEVEL for a level lowered). A sample made from a sound keeps its original and every change starts
 * from it; REMOVE and REMOVE SILENCE remake the original, joined; any other sample starts from its own audio, so on
 * the FM-1 panel a cut is final once written. Selections, slices and markers are the tab's alone (t.cutting), in the
 * frames of the tab's source (t.src). */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, O = ED.optimize, S = ED.slices, h = U.h, H2 = M.H2;
const RATES = [8000, 11025, 16000, 22050, 32000, 44100, 48000];

T.kinds.sample = function (t, api) {
    const side = t.side, items = api.st[side], d = api.draft;
    /* the source: the draft's original (REMOVE's joined one too, UNDO taking it back), else the audio as the tab opened it */
    const src = d.sound || (t.srcOwn = t.srcOwn || O.source(t.base, items));
    const cut = Object.assign({}, src.cut || { start: 0, end: src.ch[0].length, chan: "mix", rate: src.rate, bits: 8 }, d.sound ? d.sound.cut : t.cut || {});
    const snd = { rate: src.rate, channels: src.ch.length, length: src.ch[0].length, ch: src.ch, bits: src.bits };
    const entry = M.sampleEntry(d, items) || { name: "SAMPLE", rate: cut.rate, length: 0, coding: 1 };
    const proposed = (() => { try { return ED.conv.suggestRate(snd); } catch (e) { return src.rate; } })();
    /* the tab's own, in the source's frames: the slices, SENSITIVITY and the joins REMOVE made, in the draft (so the
     * tab's UNDO takes them back, never stored: M.pruned); the selection and the slices picked, in the tab */
    const cs = Object.assign({ slices: null, sens: 5, maps: [] }, d.cutting || {});
    const ui = t.cutUi = t.cutUi || { sel: null, picked: new Set() };
    if (ui.sel) { ui.sel = [Math.max(cut.start, ui.sel[0]), Math.min(cut.end, ui.sel[1])]; if (ui.sel[1] - ui.sel[0] < 2) ui.sel = null; }
    if (cs.slices) cs.slices = cs.slices.filter(z => z.a >= cut.start && z.b <= cut.end);
    ui.picked = new Set([...ui.picked].filter(i => cs.slices && i < cs.slices.length));  /* (an UNDO's fewer slices) */
    /* the markers inside a part kept (TRIM, TRIM SILENCE): those across its ends cut at them */
    const keptIn = c => cs.slices ? cs.slices.filter(z => z.b > c.start && z.a < c.end).map(z => ({ a: Math.max(c.start, z.a), b: Math.min(c.end, z.b), silent: z.silent })) : null;

    /* a change: the sample made again from its source (nsrc: a new source, REMOVE's joined sound); more: the
     * slices' own changes with it */
    function redo(c, what, nsrc, more) {
        let n;
        const s2 = nsrc || src;
        try { n = O.remake(api.draft, { src: s2, snd: nsrc ? { rate: s2.rate, channels: s2.ch.length, length: s2.ch[0].length, ch: s2.ch, bits: s2.bits } : snd, entry }, c); }
        catch (e) { U.status(String(e.message || e).toUpperCase(), { err: true }); return false; }
        if (nsrc) n.sound = Object.assign({}, s2, { cut: c });
        else if (src.own) t.cut = c;
        if (more) n.cutting = Object.assign({}, cs, more);
        api.set(n, what);
        api.refresh();
        return true;
    }
    /* the slices changed alone (markers, DETECT SLICES): a step of the tab's UNDO, which by itself asks nothing at ✕ */
    function setSlices(more, what) {
        const was = t.dirty;
        api.set(Object.assign({}, api.draft, { cutting: Object.assign({}, cs, more) }), what);
        t.dirty = was;
        ui.picked = new Set();
        api.refresh();
    }
    /* the cut the item had when the tab opened, and what its sampler's instruments that play it use of the sound */
    const src0 = t.base.sound || (t.srcOwn = t.srcOwn || O.source(t.base, items));
    const cut0 = src0.cut || cut;
    const { players, playersAt, used } = O.context(t.base, items, src0);
    /* the instruments of every sampler on the panel that name the sample follow a change on SAVE (their points where the
     * same sound now is, and the joins REMOVE made: a point carried over each) */
    t.onSave = st => {
        const map = cs.maps.length ? f => cs.maps.reduce((v, m) => m(v), f) : null;
        const out = { st: O.follow(st, side, t.itemId, cut, cut0, src0.rate, map) };
        if (players.length && (map || cut.start !== cut0.start || cut.end !== cut0.end || cut.rate !== cut0.rate))
            out.what = "ITS INSTRUMENTS FOLLOW: " + players.map(p => p.name).join(", ");
        return out;
    };
    /* OPTIMIZE: its three levels made, their sizes shown, the one picked put in the draft (heard before SAVE) */
    const optimize = btn => {
        const lv = ED.conv.optimize(snd, cut, used, players.length > 0);
        const size = c => { try { return ED.conv.soundToSample(snd, Object.assign({ name: entry.name }, c)).audio.length + 16; } catch (e) { return null; } };
        const long = M.isLong(api.draft), now = M.size(api.draft), r = btn.getBoundingClientRect();
        /* (a sample too long: a level that makes it fit counts as smaller) */
        const saved = n => long ? "FITS THE FM-1" : n < now ? "−" + U.kb(now - n) : "NOTHING SAVED";
        const line = (name, c, what) => {
            const n = size(c);
            return { label: name + " · " + (n == null ? "TOO LONG" : U.kb(n) + " (" + saved(n) + ")") + " · " + what,
                off: n == null || (!long && n >= now), fn: () => redo(c, "OPTIMIZE: " + name) };
        };
        U.menu(r.left, r.bottom + 2, [
            line("LOSSLESS", lv.lossless, "NOTHING HEARD CHANGES"),
            line("SMALLER", lv.smaller, "THE PROPOSED RATE, " + lv.smaller.rate + " HZ"),
            line("SMALLEST", lv.smallest, "8 BITS, A SOFTER TOP" + (lv.smallest.gain ? ", 6 DB LOWER (ITS INSTRUMENTS' LEVEL UP 6 DB)" : ""))]);
    };

    /* ---- playing a part: the sample made from it, through the preview, as the FM-1 would (one the preview cannot
     * hold: the source's part as it is, through the page's own player) ---- */
    function play(a, b) {
        const own = (x, y, it) => () => ED.player.playOwn(src, x, y, entry.name || "SAMPLE", it);
        if (a == null) return ED.player.toggle(api.draft, { st: api.stWith(), own: own(cut.start, cut.end, api.draft) });
        let tmp;
        try { tmp = O.remake(api.draft, { src, snd, entry }, Object.assign({}, cut, { start: a, end: b })); }
        catch (e) { U.status(String(e.message || e).toUpperCase(), { err: true }); return; }
        ED.player.toggle(tmp, { st: M.replaceItem(api.stWith(), side, tmp), own: own(a, b, tmp) });
    }
    /* a part as a sound of its own (the source's channels at its rate) */
    const part = (a, b) => ({ rate: src.rate, ch: src.ch.map(c => c.slice(a, b)), bits: src.bits });
    const how = () => ({ chan: cut.chan, rate: cut.rate, bits: cut.bits });
    const panelItem = () => api.st[side].find(i => i.id === t.itemId) || t.base;
    const busy = id => { const x = ED.tabs.find(side, id); return !!(x && x.dirty); };
    function saveNew(list, what) {
        const r = ED.cutting.addSamples(api.st, side, list, "FROM " + (entry.name || "SAMPLE"));
        if (!r.ok) { U.status(r.msg, { err: true }); return; }
        const names = r.ids.map(id => { const it = r.st[side].find(i => i.id === id); return ((M.sampleEntry(it, r.st[side]) || {}).name || "?") + (M.isLong(it) ? " (TOO LONG FOR THE FM-1)" : ""); });
        ED.app.commit(r.st, what + ": " + names.join(", ") + (side === "R" ? " · THE FM-1 CHANGES WITH WRITE" : ""), { ok: true });
        api.refresh();
    }
    const slicesToKeep = () => {
        if (!cs.slices) return [];
        const picked = cs.slices.filter((z, i) => ui.picked.has(i));
        return picked.length ? picked : cs.slices.filter(z => !z.silent);
    };
    function makeKit() {
        const list = slicesToKeep().map(z => ({ snd: part(z.a, z.b), how: how(), name: entry.name || "SAMPLE" }));
        const r = ED.cutting.makeKit(api.st, side, panelItem(), list, entry.name || "SAMPLE", "KIT FROM " + (entry.name || "SAMPLE"));
        if (!r.ok) { U.status(r.msg, { err: true }); return; }
        ED.app.commit(r.st, "A KIT OF " + list.length + " SLICES: A SAMPLER AND A FUSION CARTRIDGE" + (side === "R" ? " · THE FM-1 CHANGES WITH WRITE" : ""), { ok: true });
        for (const id of r.ids) { const it = r.st[side].find(i => i.id === id); if (it && (it.kind === K.SAMPLER || it.kind === K.FUSION)) ED.tabs.open(it); }
    }
    /* REMOVE, REMOVE SILENCE: the parts kept joined from the source's own frames, the joined sound the new original */
    function remove(drop, what) {
        if (!cs.slices || !drop.length) return;
        const gone = new Set(drop.map(z => z.a + ":" + z.b)), keep = [];
        let at = cut.start;
        for (const z of cs.slices) { if (gone.has(z.a + ":" + z.b)) { if (z.a > at) keep.push([at, z.a]); at = z.b; } }
        if (cut.end > at) keep.push([at, cut.end]);
        if (!keep.length) { U.status("NOTHING WOULD BE LEFT OF THE SAMPLE", { err: true }); return; }
        const j = S.join(src.ch, src.rate, keep), nsrc = { rate: src.rate, ch: j.ch, bits: src.bits };
        const c = Object.assign({}, cut, { start: 0, end: j.length });
        const left = cs.slices.filter(z => !gone.has(z.a + ":" + z.b)).map(z => ({ a: j.map(z.a), b: j.map(z.b), silent: z.silent })).filter(z => z.b > z.a);
        ui.picked = new Set(); ui.sel = null;
        redo(c, what, nsrc, { slices: left, maps: cs.maps.concat([j.map]) });
    }

    /* ---- the waveform: the sample as the draft has it (the source's frames cut.start..cut.end) ---- */
    const cv = h("canvas", { width: 1200, height: 220, tabindex: 0 });
    const v0 = cut.start, v1 = Math.max(cut.start + 1, cut.end), span = v1 - v0;
    const wave = h("div", { class: "wavebox" }, cv);
    const X = f => (f - v0) / span * cv.width, F = x => Math.round(v0 + x / cv.width * span);
    function drawWave() {
        const g = cv.getContext("2d"), W = cv.width, Hh = cv.height;
        g.fillStyle = "#000"; g.fillRect(0, 0, W, Hh);
        if (sl) sl.forEach((z, i) => {
            g.fillStyle = ui.picked.has(i) ? "#24305c" : z.silent ? "#0c0c0c" : i % 2 ? "#141414" : "#1c1c1c";
            g.fillRect(X(z.a), 0, X(z.b) - X(z.a), Hh);
        });
        if (ui.sel) { g.fillStyle = "#3a3418"; g.fillRect(X(ui.sel[0]), 0, X(ui.sel[1]) - X(ui.sel[0]), Hh); }
        const dim = f => sl && sl.some(z => z.silent && f >= z.a && f < z.b);
        /* one lane, as the sample is: PT/FM's samples are one channel (a sound is mixed to one as it comes; the channels
         * mixed here too, should a source still have more) */
        const lane = snd.ch, k = 1 / lane.length;
        g.fillStyle = U.COL.yellow;
        for (let x = 0; x < W; x++) {
            const i0 = F(x), i1 = Math.max(i0 + 1, F(x + 1));
            let mn = 1, mx = -1;
            for (let i = i0; i < i1; i += Math.max(1, (i1 - i0) >> 6)) {
                let v = 0;
                for (const d of lane) v += d[i] || 0;
                v *= k;
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            g.globalAlpha = dim(i0) ? 0.3 : 1;
            g.fillRect(x, (1 - mx) * Hh / 2, 1, Math.max(1, (mx - mn) * Hh / 2));
        }
        g.globalAlpha = 1;
        if (sl) {
            g.font = "12px sans-serif";
            sl.forEach((z, i) => {
                g.fillStyle = U.COL.white; g.fillRect(X(z.a), 0, 2, Hh);
                if (!z.silent) { g.fillStyle = U.COL.lgrey; g.fillText(H2(num(i)), X(z.a) + 4, 14); }
            });
        }
        if (ui.sel) { g.fillStyle = U.COL.yellow; g.fillRect(X(ui.sel[0]), 0, 2, Hh); g.fillRect(X(ui.sel[1]) - 2, 0, 2, Hh); }
    }
    let sl = cs.slices ? cs.slices.slice() : null;      /* (as drawn: a marker dragged moves here first) */
    const num = i => sl.slice(0, i + 1).filter(z => !z.silent).length;     /* (the slices numbered from 01, the silent ones not) */
    const at = e => { const r = cv.getBoundingClientRect(); return Math.max(v0, Math.min(v1, F((e.clientX - r.left) / r.width * cv.width))); };
    const near = () => Math.max(1, span / 120);
    let drag = null;
    cv.addEventListener("pointerdown", e => {
        const p = at(e), mk = sl ? sl.findIndex(z => !z.silent && Math.abs(z.a - p) < near() && z.a > v0) : -1;
        if (mk >= 0) drag = { kind: "marker", i: mk };
        else if (ui.sel && Math.abs(p - ui.sel[0]) < near()) drag = { kind: "start" };
        else if (ui.sel && Math.abs(p - ui.sel[1]) < near()) drag = { kind: "end" };
        else drag = { kind: "new", a: p, moved: false, ctrl: e.ctrlKey || e.shiftKey || e.pointerType === "touch" };
        try { cv.setPointerCapture(e.pointerId); } catch (err) { /* (a pointer already gone) */ }
    });
    cv.addEventListener("pointermove", e => {
        if (!drag) return;
        const p = at(e);
        if (drag.kind === "marker") { sl[drag.i] = Object.assign({}, sl[drag.i], { a: p }); drag.moved = true; }
        else if (drag.kind === "start" && ui.sel) ui.sel = [Math.min(p, ui.sel[1] - 1), ui.sel[1]];
        else if (drag.kind === "end" && ui.sel) ui.sel = [ui.sel[0], Math.max(p, ui.sel[0] + 1)];
        else if (drag.kind === "new" && Math.abs(p - drag.a) > near()) { drag.moved = true; ui.sel = [Math.min(drag.a, p), Math.max(drag.a, p)]; }
        drawWave(); pos.textContent = posText();
    });
    cv.addEventListener("pointerup", e => {
        if (!drag) return;
        const dr = drag; drag = null;
        if (dr.kind === "marker" && dr.moved) hand(sl.filter(z => !z.silent).map(z => z.a), "A MARKER MOVED");
        else if (dr.kind === "new" && !dr.moved && sl) {
            const p = at(e), i = sl.findIndex(z => p >= z.a && p < z.b), pk = ui.picked;
            if (i >= 0) { if (!dr.ctrl) ui.picked = new Set(pk.has(i) && pk.size === 1 ? [] : [i]); else if (pk.has(i)) pk.delete(i); else pk.add(i); }
            api.refresh();
        } else if (dr.kind !== "marker") api.refresh();  /* (the keys follow the selection) */
    });
    cv.addEventListener("dblclick", e => { if (sl) hand(sl.filter(z => !z.silent).map(z => z.a).concat([at(e)]), "A SLICE ADDED"); });
    cv.addEventListener("keydown", e => { if ((e.key === "Delete" || e.key === "Backspace") && sl && ui.picked.size) { e.preventDefault(); delMarker(); } });
    /* markers by hand: the slices found again from their starts (each slice's end and the silent ones) */
    const fromStarts = starts => S.fromStarts(S.mono(src.ch, 0, src.ch[0].length), src.rate, v0, v1, starts);
    const hand = (starts, what) => setSlices({ slices: fromStarts(starts) }, what);
    function delMarker() {
        const drop = new Set([...ui.picked].map(i => cs.slices[i].a));
        hand(cs.slices.filter(z => !z.silent && !drop.has(z.a)).map(z => z.a), "A MARKER DELETED");
    }
    /* DETECT SLICES (and SENSITIVITY moved): the selection, or the whole sample; markers by hand given up */
    function detect(sv) {
        const a = ui.sel ? ui.sel[0] : v0, b = ui.sel ? ui.sel[1] : v1, s = sv || cs.sens;
        const found = S.detect(S.mono(src.ch, 0, src.ch[0].length), src.rate, a, b, s);
        setSlices({ slices: found, sens: s }, "DETECT SLICES");
        U.status("SLICES: " + found.filter(z => !z.silent).length + " FOUND (" + found.filter(z => z.silent).length + " SILENT)", { ok: true });
    }
    const posText = () => ui.sel ? "SELECTED " + (ui.sel[0] / src.rate).toFixed(3) + " – " + (ui.sel[1] / src.rate).toFixed(3) + " S · " + ((ui.sel[1] - ui.sel[0]) / src.rate).toFixed(3) + " S"
        : "NOTHING SELECTED: DRAG ACROSS THE WAVE · THE SAMPLE " + (span / src.rate).toFixed(3) + " S";
    const pos = h("div", { class: "hint" }, posText());
    requestAnimationFrame(drawWave);

    /* ---- the keys ---- */
    const btn = (label, fn, o) => h("button", Object.assign({ class: "btn small", onclick: fn }, o || {}), label);
    const off = c => c ? {} : { disabled: true };
    const cutKeys = h("div", { class: "rowbtns", style: "margin-top:8px" },
        btn(ui.sel ? "▶ PLAY THE SELECTION" : "▶ PLAY THE SAMPLE", () => ui.sel ? play(ui.sel[0], ui.sel[1]) : play()),
        btn("TRIM", () => {                             /* (the markers inside the part kept stay) */
            const c = Object.assign({}, cut, { start: ui.sel[0], end: ui.sel[1] });
            ui.sel = null; ui.picked = new Set(); redo(c, "TRIM", null, { slices: keptIn(c) });
        }, off(ui.sel)),
        btn("TRIM SILENCE", () => {
            const [a, b] = S.trimSilence(S.mono(src.ch, v0, v1), src.rate);
            const c = Object.assign({}, cut, { start: v0 + a, end: v0 + b });
            ui.sel = null; ui.picked = new Set(); redo(c, "TRIM SILENCE", null, { slices: keptIn(c) });
        }),
        btn("WHOLE SAMPLE", () => { ui.sel = [0, snd.length]; redo(Object.assign({}, cut, { start: 0, end: snd.length }), "WHOLE SAMPLE"); }),
        btn("SAVE AS A NEW SAMPLE", () => saveNew([{ snd: part(ui.sel[0], ui.sel[1]), how: how(), name: entry.name || "SAMPLE" }], "A NEW SAMPLE"), off(ui.sel)),
        h("button", { class: "btn small primary", title: "MAKE IT TAKE LESS ROOM: LOSSLESS, SMALLER OR SMALLEST, EACH WITH WHAT IT SAVES", onclick: e => optimize(e.currentTarget) }, "OPTIMIZE ▾"));
    const sens = h("select", { onchange: e => { const v = +e.target.value; if (cs.slices) detect(v); else setSlices({ sens: v }, "SENSITIVITY " + v); } },
        Array.from({ length: 10 }, (_, i) => h("option", { value: i + 1, selected: cs.sens === i + 1 }, String(i + 1))));
    const has = !!(cs.slices && cs.slices.length), loud = has && cs.slices.some(z => !z.silent);
    const sliceKeys = h("div", { class: "rowbtns" },
        btn("DETECT SLICES", () => detect()), h("label", { class: "hint" }, "SENSITIVITY "), sens,
        btn("ADD A SLICE", () => hand((cs.slices || []).filter(z => !z.silent).map(z => z.a).concat([ui.sel[0]]), "A SLICE ADDED"), off(ui.sel && has)),
        btn("DELETE THE MARKER", delMarker, off(has && ui.picked.size)),
        btn("REMOVE SILENCE", () => remove(cs.slices.filter(z => z.silent), "REMOVE SILENCE"), off(has && cs.slices.some(z => z.silent))),
        btn("REMOVE", () => remove(cs.slices.filter((z, i) => ui.picked.has(i)), "REMOVE"), off(has && ui.picked.size)),
        btn("SAVE AS SAMPLES", () => saveNew(slicesToKeep().map(z => ({ snd: part(z.a, z.b), how: how(), name: entry.name || "SAMPLE" })), "SAMPLES FROM THE SLICES"), off(loud)),
        btn("MAKE A KIT", makeKit, off(loud)));
    const sliceList = h("div", { class: "slicelist" });
    function paintSlices() {
        sliceList.textContent = "";
        if (!cs.slices) { sliceList.append(h("div", { class: "hint" }, "DETECT SLICES FINDS WHERE THE SOUNDS START; A DOUBLE-CLICK ADDS A MARKER, A DRAG MOVES ONE, A CLICK PICKS A SLICE (CTRL, SHIFT: SEVERAL)")); return; }
        cs.slices.forEach((z, i) => {
            if (z.silent) return;
            sliceList.append(h("span", { class: "slice" + (ui.picked.has(i) ? " on" : "") },
                btn("▶ " + H2(num(i)), () => play(z.a, z.b)), " " + ((z.b - z.a) / src.rate).toFixed(2) + " S"));
        });
    }
    paintSlices();

    /* ---- the choices ---- */
    const rates = RATES.filter(r => r <= Math.max(snd.rate, cut.rate)).concat(RATES.includes(cut.rate) ? [] : [cut.rate]).sort((a, b) => a - b);
    const ctl = T.card("THE SAMPLE",
        T.text("NAME", () => entry.name || "", v => {
            /* the name is in the sample's entry, the record's first 16 bytes (a sample too long has no record: its draft's entry) */
            const n = Object.assign({}, api.draft, { name: v });
            if (n.recs.length) { const s = R.unpackSample(n.recs[0].payload); n.recs = [Object.assign({}, n.recs[0], { payload: R.packSample(Object.assign({}, s.entry, { name: v }), s.audio) })]; }
            else n.entry = M.packedEntry(Object.assign({}, entry, { name: v }));
            api.set(n, "NAME " + v); api.refresh();
        }, 10),
        T.field("RATE", h("select", { onchange: e => redo(Object.assign({}, cut, { rate: +e.target.value }), "RATE " + e.target.value + " HZ") },
            rates.map(r => h("option", { value: r, selected: r === cut.rate }, r + " HZ" + (r === proposed ? " · PROPOSED" : "") + (r === snd.rate ? " · THE SOURCE'S" : ""))))),
        T.choice("DEPTH", ["8 BITS", "16 BITS"], () => cut.bits === 16 ? 1 : 0, v => redo(Object.assign({}, cut, { bits: v ? 16 : 8 }), "DEPTH " + (v ? 16 : 8) + " BITS")));
    const origBytes = snd.length * snd.channels * Math.ceil((src.bits || 16) / 8);
    /* PLAYED BY: the instruments of every sampler on the panel that name it (the sampler's number and the slot, or its name on the TABLE) */
    const playedBy = playersAt.map(p => (side === "R" ? H2(p.item.num) : M.nameOf(p.item, items)) + ":" + H2(p.at) + " " + p.inst.name);
    const info = T.card("SIZES AND USES", h("div", { class: "sinfo" },
        h("div", null, h("b", null, src.own ? "ITS AUDIO" : "THE ORIGINAL"), U.kb(origBytes) + " · " + snd.rate + " HZ · " + snd.channels + (snd.channels > 1 ? " CHANNELS" : " CHANNEL") + (src.own ? " (NO ORIGINAL: A CUT IS FINAL ONCE SAVED)" : "")),
        M.isLong(d)
            ? h("div", { class: "bad" }, h("b", null, "THE RESULT"), M.longText(d) + " · " + entry.rate + " HZ · " + (entry.coding === 2 ? 16 : 8) +
                " BITS · TRIM, REMOVE, REMOVE SILENCE, A LOWER RATE OR OPTIMIZE MAKES IT FIT; TILL THEN IT STAYS ON THE TABLE")
            : h("div", null, h("b", null, "THE RESULT"), U.kb(M.size(api.draft)) + " AS STORED · " + entry.rate + " HZ · " + (entry.coding === 2 ? 16 : 8) + " BITS · " + (entry.rate ? (entry.length / entry.rate).toFixed(2) : "?") + " S"),
        h("div", null, h("b", null, "PLAYED BY"), playedBy.length ? playedBy.join(", ") : "NO INSTRUMENT YET")));
    const slicesCard = T.card("SLICES", sliceKeys, sliceList);
    return h("div", null, h("div", { class: "card", style: "margin-bottom:12px" }, wave, pos, cutKeys), h("div", { class: "cards" }, ctl, slicesCard, info));
};
})(globalThis.ED = globalThis.ED || {});
