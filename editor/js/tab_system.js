/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The system items' tabs (specs/editor.md, The tabs): SETTINGS (every setting, the 16 GATE patterns of your own as rows
 * of steps), MIDI MAP (each mapping's channel, message, number, FM-1 control and kind), LOOPS (each loop's instrument,
 * POLY or MONO, lanes and repeats; the loops' FX setup), DJ SETUP (the crossfader, the filter, the FX LFO, the decks,
 * DJ's own FX setup). Ranges and words are specs/files/'s. */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, D = ED.defs, h = U.h, H2 = M.H2;
const withPayload = (d, b) => Object.assign({}, d, { recs: [Object.assign({}, d.recs[0], { payload: b })].concat(d.recs.slice(1)) });

/* ---- SETTINGS ---- */
T.kinds.settings = function (t, api) {
    const b0 = api.draft.recs[0].payload;
    const edit = (fn, what) => { const b = b0.slice(); fn(b); api.set(withPayload(api.draft, b), what); api.refresh(); };
    const ctl = s => s.list ? T.choice(s.n, s.list, () => s.get(b0), v => edit(b => s.set(b, v), s.n + " " + s.list[v]))
        : T.slider(s.n, { min: s.min, max: s.max, step: s.step || 1, def: s.def, get: () => s.get(b0), fmt: v => (s.tenths ? (v / 10).toFixed(1) : s.hex ? H2(v) : (s.min < 0 && v > 0 ? "+" : "") + v) + (s.unit || ""),
            set: v => edit(b => s.set(b, v), s.n) });
    const gates = h("div", { class: "gates" });
    for (let g = 0; g < 16; g++) {
        const steps = D.gateGet(b0, g);
        gates.append(h("span", null, H2(g)));
        steps.forEach((v, k) => gates.append(h("button", { class: "v" + v, title: D.GATE_STEPS[v] + " (CLICK FOR THE NEXT)", onclick: () => edit(b => { const s2 = D.gateGet(b, g); s2[k] = (s2[k] + 1) % 5; D.gateSet(b, g, s2); }, "GATE PATTERN " + H2(g)) },
            v === 0 ? "·" : v === 1 ? "■" : D.GATE_STEPS[v])));
    }
    return h("div", { class: "cards" }, T.card("SETTINGS", ...D.SETTINGS.map(ctl)), T.card("MIDI", ...D.MIDI_SETTINGS.map(ctl)),
        h("section", { class: "card wide" }, h("h4", null, "YOUR 16 GATE PATTERNS", h("span", { class: "grow" })), h("p", { class: "hint" }, "EACH STEP: OFF, ON, ×2, ×3, ×4 (A CLICK TURNS IT ON)."), gates));
};

/* ---- MIDI MAP ---- */
T.kinds.midimap = function (t, api) {
    const m = R.unpackMidiMap(api.draft.recs[0].payload);
    const save = (maps, what) => { api.set(withPayload(api.draft, R.packMidiMap({ maps })), what); api.refresh(); };
    const rows = h("div");
    rows.append(h("div", { class: "maprow head" }, h("span", null, "CHANNEL"), h("span", null, "MESSAGE"), h("span", null, "NUMBER"), h("span", null, "FM-1 CONTROL"), h("span", null, "KIND"), h("span")));
    m.maps.forEach((x, i) => {
        const upd = (f, v) => { const maps = m.maps.map(y => Object.assign({}, y)); maps[i][f] = v; save(maps, "MAPPING " + (i + 1)); };
        rows.append(h("div", { class: "maprow" },
            h("select", { onchange: e => upd("ch", +e.target.value) }, Array.from({ length: 16 }, (_, c) => h("option", { value: c, selected: c === x.ch }, String(c + 1)))),
            h("select", { onchange: e => upd("note", +e.target.value) }, ["CC", "NOTE"].map((n, k) => h("option", { value: k, selected: k === x.note }, n))),
            h("input", { type: "number", min: 0, max: 127, value: x.num, onchange: e => upd("num", Math.max(0, Math.min(127, +e.target.value || 0))) }),
            h("select", { onchange: e => upd("control", +e.target.value) }, D.CONTROLS.map((n, k) => h("option", { value: k, selected: k === x.control }, n))),
            h("select", { onchange: e => upd("type", +e.target.value) }, D.MAP_KINDS.map((n, k) => h("option", { value: k, selected: k === x.type }, n))),
            h("button", { class: "btn small danger", title: "REMOVE", onclick: () => save(m.maps.filter((_, k) => k !== i), "MAPPING REMOVED") }, "✕")));
    });
    return T.wideCard("THE MIDI MAP · " + U.plural(m.maps.length, "MAPPING"), rows,
        h("button", { class: "btn small", disabled: m.maps.length >= 64, onclick: () => save(m.maps.concat([{ ch: 0, note: 0, num: 1, control: 2, type: 0 }]), "MAPPING ADDED") }, "+ ADD A MAPPING"));
};

/* ---- LOOPS ---- */
T.kinds.loops = function (t, api) {
    const side = t.side, L = R.unpackLoops(api.draft.recs.map(r => r.payload));
    const save = (fn, what, link) => {
        const L2 = R.unpackLoops(api.draft.recs.map(r => r.payload));
        fn(L2);
        const parts = R.packLoops(L2);
        api.set(M.pruned(Object.assign({}, api.draft, { recs: parts.map((payload, i) => ({ sub: i, ver: api.draft.recs[0].ver, payload })), link: link || api.draft.link })), what);
        api.refresh();
    };
    const items = api.st[side];
    const carts = [];
    /* (on the FM-1 panel a built-in's number names the changed one it holds) */
    const changedAt = n => side === "R" && n >= 0xF0 ? items.find(c => M.isCart(c.kind) && !c.dim && c.num === n) : null;
    for (const b of M.builtins) if (M.isCart(b.kind) && !carts.some(c => c.num === b.num)) {
        const ch = changedAt(b.num);
        carts.push(ch ? { key: ch.id, num: b.num, kind: ch.kind, payload: ch.recs[0].payload, label: H2(b.num), item: ch }
            : { key: "B" + b.num, num: b.num, kind: b.kind, payload: b.payload, label: H2(b.num) });
    }
    for (const c of items) if (M.isCart(c.kind) && !c.dim && !carts.some(z => z.key === c.id)) carts.push({ key: c.id, num: c.num, kind: c.kind, payload: c.recs[0].payload, label: side === "R" ? H2(c.num) : "--", item: c });
    const rows = h("div");
    rows.append(h("div", { class: "looprow maprow head" }, h("span", null, "LOOP"), h("span", null, "INSTRUMENT"), h("span", null, "POLY / MONO"), h("span", null, "LANES"), h("span", null, "REPEATS"), h("span")));
    L.loops.forEach((lp, i) => {
        const t2 = api.draft.link[M.lk("c", lp.cart)];
        const ch = t2 === M.BUILTIN || t2 == null ? changedAt(lp.cart) : null;
        const curKey = ch ? ch.id : t2 === M.BUILTIN || (t2 == null && lp.cart >= 0xF0) ? "B" + lp.cart : t2;
        const sel = h("select", { onchange: e => {
            const [k, s] = e.target.value.split("|"), c = carts.find(z => z.key === k);
            if (!c) return;
            const target = c.item ? c.item.id : M.BUILTIN, num = M.numberFor(api.draft, "c", target, side, c.num, c.item && c.item.orig);
            save(L2 => { L2.loops[i].cart = num; L2.loops[i].slot = +s; L2.loops[i].raw[0] = num; L2.loops[i].raw[1] = +s; }, "LOOP " + (i + 1) + " INSTRUMENT",
                Object.assign({}, api.draft.link, { [M.lk("c", num)]: target }));
        } });
        for (const c of carts) {
            let names = [];
            try { names = c.kind === K.SAMPLER ? R.unpackSampler(c.payload).instruments.map(z => z && z.name) : R.unpackCart(c.kind, c.payload).slots.map(z => z && z.name); } catch (e) { /* (none) */ }
            const g = h("optgroup", { label: c.label + " " + R.unpackName(c.payload.subarray(0, 9), 12) });
            names.forEach((n, k) => { if (n != null) g.append(h("option", { value: c.key + "|" + k, selected: c.key === curKey && k === lp.slot }, c.label + ":" + H2(k) + " " + n)); });
            sel.append(g);
        }
        rows.append(h("div", { class: "looprow" }, h("span", { class: "mono" }, H2(i)), sel,
            h("select", { onchange: e => save(L2 => { L2.loops[i].mono = +e.target.value; }, "LOOP " + (i + 1) + (e.target.value === "1" ? " MONO" : " POLY")) },
                ["POLY", "MONO"].map((n, k) => h("option", { value: k, selected: k === lp.mono }, n))),
            h("input", { type: "number", min: 1, max: 4, value: lp.lanes, onchange: e => save(L2 => { L2.loops[i].lanes = Math.max(1, Math.min(4, +e.target.value || 1)); }, "LOOP " + (i + 1) + " LANES") }),
            h("input", { type: "number", min: 1, max: 255, value: lp.repeat, title: "REPEATS, 1-255", onchange: e => save(L2 => { L2.loops[i].repeat = Math.max(1, Math.min(255, +e.target.value || 1)); }, "LOOP " + (i + 1) + " REPEATS") }),
            h("button", { class: "btn small", title: "PLAY ALL THE LOOPS", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶")));
    });
    t.fxPicture = (e, fx) => ED.pics.fx(e, fx, { tempo: L.tempo, ticks: L.ticks, rpb: 4 });
    /* a loop's notes in a grid: its base's rows down, its lanes across (shown; recorded on the FM-1) */
    t.loop = Math.min(t.loop || 0, Math.max(0, L.loops.length - 1));
    const lp = L.loops[t.loop];
    let notes = null;
    if (lp) {
        let html = '<div class="ph"><span class="pr"> </span>';
        for (let ln = 0; ln < lp.lanes; ln++) html += `<span class="pth" style="width:calc(13ch + 14px)">LANE ${ln + 1}</span>`;
        html += "</div>";
        for (let r = 0; r < lp.base; r++) {
            html += `<div class="prow${r % 4 === 0 ? " beat" : ""}"><span class="pr">${H2(r)}</span>`;
            for (let ln = 0; ln < lp.lanes; ln++) {
                const c = lp.cells.get(R.cellKey(r, ln));
                const nt = c && c.note ? R.noteName(c.note) : '<span class="e">---</span>';
                const vol = c && c.vol >= 0 ? `<span class="v">${H2(c.vol)}</span>` : '<span class="e">--</span>';
                const cm = c && c.cmds && c.cmds.length ? `<span class="x">${ED.cmds.text(c.cmds[0])}</span>` : '<span class="e">...</span>';
                html += `<span class="pc" style="width:calc(13ch + 14px)">${nt} ${vol} ${cm}</span>`;
            }
            html += "</div>";
        }
        const grid = h("div", { class: "pgrid", style: "max-height:420px" });
        grid.innerHTML = html;
        notes = h("section", { class: "card wide" }, h("h4", null, "THE NOTES", h("span", { class: "grow" })),
            h("div", { class: "seg", style: "margin-bottom:8px;flex-wrap:wrap" }, L.loops.map((_, i) => h("button", { class: i === t.loop ? "on" : "", onclick: () => { t.loop = i; api.refresh(); } }, "LOOP " + H2(i)))),
            h("p", { class: "hint" }, U.plural(lp.base, "ROW") + " × " + U.plural(lp.lanes, "LANE") + (lp.repeat ? ", " + lp.repeat + " REPEATS" : "") + " · RECORDED ON THE FM-1'S LOOPS SCREEN"), grid);
    }
    return h("div", { class: "cards" }, notes,
        T.wideCard("THE LOOPS · " + U.plural(L.loops.length, "LOOP"), L.loops.length ? rows : h("p", { class: "hint" }, "NO LOOPS: THEY ARE RECORDED ON THE FM-1'S LOOPS SCREEN."),
            T.slider("TEMPO", { min: 3, max: 600, def: 120, get: () => L.tempo, set: v => save(L2 => { L2.tempo = v; }, "TEMPO " + v) }),
            T.slider("TICKS A ROW", { min: 1, max: 31, def: 6, get: () => L.ticks, set: v => save(L2 => { L2.ticks = v; }, "TICKS A ROW " + v) }),
            T.slider("SWING", { min: 50, max: 75, def: 50, get: () => L.swing, fmt: v => v + " %", set: v => save(L2 => { L2.swing = v; }, "SWING " + v + " %") })),
        T.card("THE LOOPS' FX SETUP", T.fxSetup(L.fx, (fx, what) => save(L2 => { L2.fx = fx; }, what), t)));
};

/* ---- DJ SETUP ---- */
T.kinds.dj = function (t, api) {
    const b0 = api.draft.recs[0].payload, side = t.side, items = api.st[side];
    const edit = (fn, what) => { const b = b0.slice(); fn(b); api.set(withPayload(api.draft, b), what); api.refresh(); };
    const deck = (at, name) => {
        const song = b0[at] === 0xFF ? null : api.draft.link[M.lk("s", b0[at])];
        const it = song && items.find(x => x.id === song);
        return T.card(name, h("p", null, b0[at] === 0xFF ? "NO SONG ON IT." : "SONG " + H2(b0[at]) + (it ? " " + M.nameOf(it, items) : " (NOT ON THIS PANEL: THE DECK IS EMPTY ON THE FM-1)")),
            T.slider("TEMPO", { min: 300, max: 3000, get: () => b0[at + 5] | b0[at + 6] << 8, fmt: v => (v / 10).toFixed(1) + " BPM", set: v => edit(b => { b[at + 5] = v & 255; b[at + 6] = v >> 8; }, name + " TEMPO") }),
            T.slider("KEY", { min: -6, max: 6, def: 0, get: () => (b0[at + 7] << 24) >> 24, fmt: v => (v > 0 ? "+" : "") + v, set: v => edit(b => { b[at + 7] = v & 255; }, name + " KEY") }),
            T.slider("VOLUME", { min: -25, max: 6, def: 0, get: () => (b0[at + 8] << 24) >> 24, fmt: v => v === -25 ? "OFF" : (v > 0 ? "+" : "") + v + " DB", set: v => edit(b => { b[at + 8] = v & 255; }, name + " VOLUME") }),
            T.slider("DJ FILTER", { min: -64, max: 63, def: 0, get: () => (b0[at + 9] << 24) >> 24, fmt: v => v === 0 ? "OFF" : v < 0 ? "LP " + (-v) : "HP " + v, set: v => edit(b => { b[at + 9] = v & 255; }, name + " DJ FILTER") }),
            b0[at] !== 0xFF ? h("button", { class: "btn small danger", onclick: () => edit(b => { b.fill(0, at, at + 24); b[at] = 0xFF; }, name + " EMPTIED") }, "EMPTY THE DECK") : null);
    };
    t.fxPicture = (e, fx) => ED.pics.fx(e, fx, null);
    return h("div", { class: "cards" },
        T.card("DJ",
            T.choice("CROSSFADER", ["SMOOTH", "SHARP"], () => b0[1] & 1, v => edit(b => { b[1] = v; }, "CROSSFADER")),
            T.slider("FILTER RESONANCE", { min: 0, max: 100, def: 30, get: () => b0[2], fmt: v => v + " %", set: v => edit(b => { b[2] = v; }, "FILTER RESONANCE " + v + " %") }),
            T.choice("FILTER THE SENDS", ["ON", "OFF"], () => b0[3] & 1, v => edit(b => { b[3] = v; }, "FILTER THE SENDS")),
            T.choice("FX LFO", ["REVERB MIX", "DELAY MIX", "CHORUS MIX"], () => Math.min(2, b0[4]), v => edit(b => { b[4] = v; }, "FX LFO")),
            T.slider("THE CROSSFADER'S PLACE", { min: 0, max: 32, def: 0, get: () => b0[5], set: v => edit(b => { b[5] = v; }, "THE CROSSFADER'S PLACE") }),
            T.choice("SYNC", ["OFF", "ON"], () => b0[6] & 1, v => edit(b => { b[6] = (b[6] & ~1) | v; }, "SYNC"))),
        deck(88, "THE LEFT DECK"), deck(112, "THE RIGHT DECK"),
        T.card("DJ'S OWN FX SETUP", T.fxSetup(b0.subarray(8, 72), (fx, what) => edit(b => { b.set(fx, 8); }, what), t, { noRouting: true })));
};
})(globalThis.ED = globalThis.ED || {});
