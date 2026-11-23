/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The system items' tabs (specs/editor.md, The tabs): SETTINGS (every setting, the 16 GATE patterns of your own as rows
 * of steps), MIDI MAP (each mapping's channel, message, number, FM-1 control and kind), LOOPS and a LOOPS set (each
 * loop's instrument, POLY or MONO, lanes, repeats and SHIFT, its mute, a set's solo, its mix, its place, DELETE; the
 * setup, a new loop's defaults, the loops' FX setup; the notes, the length and a cut shown), DJ SETUP (the crossfader,
 * the filter, the FX LFO, the decks, DJ's own FX setup), AUTOMATIONS (the 16 slots, a slot's events as a table, edited,
 * inserted and deleted; slots copied into another file's tab). Ranges and words are specs/files/'s. */
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

/* ---- LOOPS, and a LOOPS set (the same, with its solos) ---- */
T.kinds.loops = function (t, api) {
    const side = t.side, isSet = api.draft.kind === K.LSET;
    const read = () => { const p = api.draft.recs.map(r => r.payload); return isSet ? R.unpackLSet(p) : { loops: R.unpackLoops(p) }; };
    const S = read(), L = S.loops;
    const save = (fn, what, link) => {                  /* (fn(loops, the set or { loops })) */
        const S2 = read();
        fn(S2.loops, S2);
        const parts = isSet ? R.packLSet(S2) : R.packLoops(S2.loops);
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
    rows.append(h("div", { class: "looprow maprow head" }, h("span", null, "LOOP"), h("span", null, "INSTRUMENT"), h("span", null, "POLY / MONO"), h("span", null, "LANES"), h("span", null, "REPEATS"), h("span", { title: "ROWS THE LOOP PLAYS LATER (+) OR EARLIER (-), ROUND THE LENGTH" }, "SHIFT"), h("span")));
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
            lp.cut ? h("span", { class: "mono", title: "CUT: THE LOOP WRAPS AT ROW " + lp.cut + " (CUT ON THE FM-1'S LOOPS SCREEN)" }, "CUT " + lp.cut)
                : h("input", { type: "number", min: 1, max: 255, value: lp.repeat, title: "REPEATS, 1-255", onchange: e => save(L2 => { L2.loops[i].repeat = Math.max(1, Math.min(255, +e.target.value || 1)); }, "LOOP " + (i + 1) + " REPEATS") }),
            h("input", { type: "text", class: "mono", size: 4, value: D.shiftText(lp.shift, L.length), title: "SHIFT: +3 PLAYS 3 ROWS LATER, -3 EARLIER; 0 TO " + (L.length - 1) + " OR SIGNED",
                onchange: e => {
                    const v = D.shiftParse(e.target.value, L.length);
                    if (v === null) { api.refresh(); return; }
                    if (v === lp.shift) { e.target.value = D.shiftText(lp.shift, L.length); return; }
                    save(L2 => { L2.loops[i].shift = v; }, "LOOP " + (i + 1) + " SHIFT " + D.shiftText(v, L.length));
                } }),
            h("button", { class: "btn small", title: "PLAY ALL THE LOOPS", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶")));
    });
    t.fxPicture = (e, fx) => ED.pics.fx(e, fx, { tempo: L.tempo, ticks: L.ticks, rpb: 4 });
    /* the loops' mixer: each loop's mute (a set's solo too), its LEVEL, PAN, DUCK and sends (hex, as the FM-1 shows them),
     * its place in the list (▲ ▼: the selection and a set's solo go with it) and DELETE (the loops after it move up);
     * then NEW, a new loop's defaults: its LEVEL and sends (PAN and DUCK centre and off, as LOOPS' properties keep them),
     * stored once one is set */
    const MIXW = ["LEVEL", "PAN", "DUCK", "CHO", "DLY", "REV"], MIXT = ["LEVEL", "PAN", "DUCK", "CHORUS SEND", "DELAY SEND", "REVERB SEND"];
    const effDef = L2 => L2.head[79] & 0x80 ? L2.mix.slice() : [0x40, 0x80, 0, 0x7F, 0x7F, 0x7F], def = effDef(L);
    const swapBits = (m, i, j) => (m & ~(1 << i | 1 << j)) | (m >> j & 1) << i | (m >> i & 1) << j;
    const move = (L2, S2, i, j) => {
        [L2.loops[i], L2.loops[j]] = [L2.loops[j], L2.loops[i]];
        if (L2.selected === i) L2.selected = j; else if (L2.selected === j) L2.selected = i;
        if (isSet) S2.solos = swapBits(S2.solos, i, j);
    };
    const del = (L2, S2, i) => {
        L2.loops.splice(i, 1);
        if (L2.selected > i) L2.selected--;
        if (isSet) S2.solos = (S2.solos & ((1 << i) - 1)) | (S2.solos >> (i + 1)) << i;
    };
    const hexIn = (v, max, set, title) => {
        const i = h("input", { type: "text", class: "mono", value: H2(v), maxLength: 2, title });
        i.addEventListener("change", () => { const x = parseInt(i.value, 16); if (isNaN(x)) { i.value = H2(v); return; } set(Math.max(0, Math.min(max, x))); });
        return i;
    };
    const mixer = h("div");
    mixer.append(h("div", { class: "lmixrow mixhead" }, h("span", null, "LOOP"), h("span", null, "MUTE"), h("span", null, isSet ? "SOLO" : ""),
        ...MIXW.map((f, k) => h("span", { title: MIXT[k] }, f)), h("span")));
    L.loops.forEach((lp, i) => {
        const n = "LOOP " + H2(i);
        mixer.append(h("div", { class: "lmixrow" }, h("span", { class: "mono" }, H2(i)),
            h("input", { type: "checkbox", checked: lp.muted, title: "MUTED", onchange: e => save(L2 => { L2.loops[i].muted = e.target.checked; }, n + (e.target.checked ? " MUTED" : " ON")) }),
            isSet ? h("input", { type: "checkbox", checked: !!(S.solos >> i & 1), title: "SOLO (A SET KEEPS ITS SOLOS)",
                onchange: e => save((L2, S2) => { S2.solos = e.target.checked ? S2.solos | 1 << i : S2.solos & ~(1 << i); }, n + (e.target.checked ? " SOLO" : " SOLO OFF")) }) : h("span"),
            ...R.MIX_FIELDS.map((f, k) => hexIn(lp.mixer[k], k === 1 ? 255 : 127, v => save(L2 => { L2.loops[i].mixer[k] = v; }, n + " " + MIXT[k]), MIXT[k] + " (HEX)")),
            h("span", { class: "lmixbtns" },
                h("button", { class: "btn small", disabled: !i, title: "UP THE LIST", onclick: () => save((L2, S2) => move(L2, S2, i, i - 1), n + " MOVED UP") }, "▲"),
                h("button", { class: "btn small", disabled: i === L.loops.length - 1, title: "DOWN THE LIST", onclick: () => save((L2, S2) => move(L2, S2, i, i + 1), n + " MOVED DOWN") }, "▼"),
                h("button", { class: "btn small danger", title: "DELETE THE LOOP (THE ONES AFTER IT MOVE UP)", onclick: () => save((L2, S2) => del(L2, S2, i), n + " DELETED") }, "✕"))));
    });
    mixer.append(h("div", { class: "lmixrow" }, h("span", { class: "mono", title: "A NEW LOOP'S DEFAULTS: THE MIX IT STARTS FROM" }, "NEW"), h("span"), h("span"),
        ...R.MIX_FIELDS.map((f, k) => k === 1 || k === 2 ? h("span", { class: "mono" }, H2(def[k]))
            : hexIn(def[k], 127, v => save(L2 => { L2.mix = effDef(L2); L2.mix[k] = v; L2.head[79] |= 0x80; }, "A NEW LOOP'S " + MIXT[k]), "A NEW LOOP'S " + MIXT[k] + " (HEX)")),
        h("span")));
    const JOBN = ["LEVEL", "PAN", "DUCK", "CHORUS SEND", "DELAY SEND", "REVERB SEND", "TEMPO", "VIEW"];
    /* a loop's notes across: its base's rows left to right, each lane three lines (its notes, their volumes, their first
     * commands) under the rows' numbers, a beat every 4 rows (shown; recorded on the FM-1) */
    t.loop = Math.min(t.loop || 0, Math.max(0, L.loops.length - 1));
    const lp = L.loops[t.loop];
    let notes = null;
    if (lp) {
        const col = r => `<span class="hc${r % 4 === 0 ? " beat" : ""}">`;
        let html = '<div class="hrow hhead"><span class="hl">ROW</span>';
        for (let r = 0; r < lp.base; r++) html += col(r) + (r % 4 === 0 ? H2(r) : "") + "</span>";
        html += "</div>";
        for (let ln = 0; ln < lp.lanes; ln++) {
            let nt = "", vol = "", cm = "";
            for (let r = 0; r < lp.base; r++) {
                const c = lp.cells.get(R.cellKey(r, ln));
                nt += col(r) + (c && c.note ? R.noteName(c.note) : '<span class="e">---</span>') + "</span>";
                vol += col(r) + (c && c.vol >= 0 ? `<span class="v">${H2(c.vol)}</span>` : '<span class="e">--</span>') + "</span>";
                cm += col(r) + (c && c.cmds && c.cmds.length ? `<span class="x">${ED.cmds.text(c.cmds[0])}</span>` : '<span class="e">...</span>') + "</span>";
            }
            html += `<div class="hrow hlane"><span class="hl">LANE ${ln + 1}</span>${nt}</div>` +
                `<div class="hrow"><span class="hl"></span>${vol}</div><div class="hrow"><span class="hl"></span>${cm}</div>`;
        }
        const grid = h("div", { class: "pgrid hgrid" });
        grid.innerHTML = html;
        notes = h("section", { class: "card wide" }, h("h4", null, "THE NOTES", h("span", { class: "grow" })),
            h("div", { class: "seg", style: "margin-bottom:8px;flex-wrap:wrap" }, L.loops.map((_, i) => h("button", { class: i === t.loop ? "on" : "", onclick: () => { t.loop = i; api.refresh(); } }, "LOOP " + H2(i)))),
            h("p", { class: "hint" }, U.plural(lp.base, "ROW") + " × " + U.plural(lp.lanes, "LANE") + (lp.cut ? ", CUT AT ROW " + lp.cut + ": IT WRAPS THERE"
                : lp.repeat ? ", " + lp.repeat + " REPEATS" : "") + " · RECORDED ON THE FM-1'S LOOPS SCREEN"),
            h("p", { class: "hint shifthint" }, D.shiftHint(lp.shift, L.length) + " (THE NOTES ARE SHOWN AS STORED)"), grid);
    }
    return h("div", { class: "cards" }, notes,
        T.wideCard("THE LOOPS · " + U.plural(L.loops.length, "LOOP") + " · THE LENGTH " + U.plural(L.length, "ROW"),
            L.loops.length ? rows : h("p", { class: "hint" }, "NO LOOPS: THEY ARE RECORDED ON THE FM-1'S LOOPS SCREEN."),
            T.slider("TEMPO", { min: 3, max: 600, def: 120, get: () => L.tempo, set: v => save(L2 => { L2.tempo = v; }, "TEMPO " + v) }),
            T.slider("TICKS A ROW", { min: 1, max: 31, def: 6, get: () => L.ticks, set: v => save(L2 => { L2.ticks = v; }, "TICKS A ROW " + v) }),
            T.slider("SWING", { min: 50, max: 75, def: 50, get: () => L.swing, fmt: v => v + " %", set: v => save(L2 => { L2.swing = v; }, "SWING " + v + " %") }),
            T.choice("QUANTIZE", ["ROW", "2 ROWS", "3 ROWS", "4 ROWS", "REAL TIME"], () => L.quantize > 4 ? 0 : L.quantize, v => save(L2 => { L2.quantize = v; }, "QUANTIZE")),
            T.choice("MONO LEGATO", ["GLIDE", "JUMP", "LEGATO"], () => L.legato, v => save(L2 => { L2.legato = v; }, "MONO LEGATO")),
            T.choice("COUNT-IN", ["OFF", "1 BAR", "2 BARS"], () => L.countIn === 3 ? 0 : L.countIn, v => save(L2 => { L2.countIn = v; }, "COUNT-IN")),
            T.choice("THE KEYS", ["POLY", "MONO"], () => L.nextMono ? 1 : 0, v => save(L2 => { L2.nextMono = v; }, "THE KEYS")),
            T.choice("VIEW", ["1 BAR A SCREEN", "2 BARS A SCREEN", "4 BARS A SCREEN"], () => Math.max(0, [1, 2, 4].indexOf(L.view)), v => save(L2 => { L2.view = [1, 2, 4][v]; }, "VIEW")),
            ...[0, 1, 2, 3].map(k => T.choice("K" + (k + 1) + " TURNS", JOBN, () => L.jobs[k], v => save(L2 => { L2.jobs = L2.jobs.slice(); L2.jobs[k] = v; }, "K" + (k + 1) + " TURNS " + JOBN[v])))),
        T.wideCard("THE LOOPS' MIXER", h("p", { class: "hint" }, "LEVEL 7F FULL · PAN 00 LEFT, 80 CENTRE, FF RIGHT · DUCK AND THE SENDS 00-7F" +
            (isSet ? " · SOLO: WHILE ANY LOOP IS SOLOED ONLY THOSE SOUND" : "")), mixer),
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

/* ---- AUTOMATIONS (specs/editor.md, The tabs): the 16 slots in a list (each its screen, length and events), the slot
 * shown: its screen, its events as a table (the time from the start, the ms since the last, the event in words; a row
 * the FM-1 skips grey, saying why), the picked row's time, gap, control and what it does edited, rows inserted and
 * deleted; each change writes the slot's parts anew as the firmware writes them. Slots marked, COPY and PASTE between
 * the files' tabs (the page's clipboard, the slots' parts as they are), asking before replacing. No preview. ---- */
let autoClip = null;                                /* [{ parts: [{ part, payload }] }], while the page is open */
T.kinds.auto = function (t, api) {
    const slots = R.unpackAuto(api.draft.recs), N = R.AUTO_SLOTS;
    const range = (a, b) => Array.from({ length: b - a + 1 }, (_, k) => a + k);
    const say = (m, err) => U.status(m, err ? { err: true } : { ok: true });
    if (t.aslot == null) t.aslot = Math.max(0, slots.findIndex(Boolean));
    if (!t.marks) t.marks = new Set([t.aslot]);
    const s = slots[t.aslot], ev = s ? s.events : [];
    if (t.row == null || t.row >= ev.length) t.row = Math.max(0, Math.min(t.row || 0, ev.length - 1));
    t.rows = new Set([...(t.rows || [])].filter(i => i < ev.length));
    if (!t.rows.size && ev.length) t.rows.add(t.row);
    const screenText = n => n < 0 ? "?" : R.AUTO_SCREENS[n] || "WHERE IT IS";
    const holds = i => !!slots[i] && slots[i].screen >= 0;   /* (as the FM-1 counts them: by part 0; without it, EMPTY there) */
    const slotName = i => "SLOT " + H2(i);
    /* a slot's events written anew (its parts as the firmware writes them; none: the slot empty) */
    const putSlot = (i, screen, events, what, pick) => {
        let parts;
        try { parts = R.packAutoSlot(screen, events); } catch (e) { say(String(e.message || e), true); api.refresh(); return false; }
        api.set(Object.assign({}, api.draft, { recs: R.autoWithSlot(api.draft.recs, i, parts) }), what);
        if (pick) { t.row = pick[pick.length - 1]; t.rows = new Set(pick); }
        api.refresh();
        return true;
    };
    const screenOf = () => s && s.screen >= 0 ? s.screen : 0;
    const edit = (events, what, pick) => putSlot(t.aslot, screenOf(), events, slotName(t.aslot) + " · " + what, pick);

    /* ---- the slots: a click picks one and marks it alone, Shift the range to it, Ctrl (a touch) adds or takes one ---- */
    const pickSlot = (i, e) => {
        if (e && e.shiftKey) t.marks = new Set(range(Math.min(t.aslot, i), Math.max(t.aslot, i)));
        else if (e && (e.ctrlKey || e.metaKey || e.pointerType === "touch")) { if (t.marks.has(i) && t.marks.size > 1) t.marks.delete(i); else t.marks.add(i); }
        else t.marks = new Set([i]);
        if (i !== t.aslot) { t.aslot = i; t.row = 0; t.rows = null; t.evTop = 0; }
        t.focus = "slots";
        api.refresh();
    };
    const copy = () => {
        const picked = [...t.marks].sort((a, b) => a - b).filter(holds);
        if (!picked.length) { say("NOTHING TO COPY: MARK A SLOT HOLDING ONE", true); return; }
        autoClip = picked.map(i => ({ parts: api.draft.recs.filter(r => r.sub >> 4 === i).map(r => ({ part: r.sub & 15, payload: r.payload })) }));
        say("COPIED " + picked.length + " (" + picked.map(H2).join(", ") + ")");
    };
    /* PASTE: the copied slots in order from the slot picked on, each with its screen; past 0F nothing; slots holding one asked */
    const paste = () => {
        if (!autoClip || !autoClip.length) { say("NOTHING COPIED", true); return; }
        const from = t.aslot, k = autoClip.length;
        if (from + k > N) { say(k + " COPIED, " + U.plural(N - from, "SLOT") + " FROM HERE: NOTHING PASTED", true); return; }
        const targets = range(from, from + k - 1), held = targets.filter(holds), clip = autoClip;
        const go = () => {
            let recs = api.draft.recs;
            targets.forEach((i, j) => {
                recs = recs.filter(r => r.sub >> 4 !== i).concat(clip[j].parts.map(p => ({ sub: i * 16 + p.part, ver: R.VERSION[K.AUTO], payload: p.payload })));
            });
            recs.sort((a, b) => a.sub - b.sub);
            api.set(Object.assign({}, api.draft, { recs }), "PASTED " + k);
            t.marks = new Set(targets); t.row = 0; t.rows = null; t.evTop = 0;
            say("PASTED " + k + " INTO " + H2(from) + (k > 1 ? "-" + H2(from + k - 1) : ""));
            api.refresh();
        };
        if (held.length) U.dialog({ title: "REPLACE " + U.plural(held.length, "AUTOMATION") + "?",
            body: h("p", null, (held.length > 1 ? "SLOTS " : "SLOT ") + held.map(H2).join(", ") + (held.length > 1 ? " HOLD ONE EACH" : " HOLDS ONE") + ": THE PASTED ONES TAKE THEIR PLACE."),
            buttons: [{ label: "REPLACE", cls: "primary danger", onClick: go }, { label: "KEEP" }] });
        else go();
    };
    const list = h("div", { class: "slots", tabIndex: 0, onscroll: () => { t.slTop = list.scrollTop; } },
        h("h5", null, "THE SLOTS · " + range(0, N - 1).filter(holds).length + " OF " + N + " HOLD ONE"),
        h("div", { class: "slotrow autoslot head" }, h("span"), h("span", null, "SCREEN"), h("span", { class: "r" }, "LENGTH"), h("span", { class: "r" }, "EVENTS")));
    for (let i = 0; i < N; i++) {
        const x = slots[i], held = holds(i);
        list.append(h("div", { class: "slotrow autoslot" + (t.marks.has(i) ? " on" : "") + (i === t.aslot ? " cur" : "") + (held && x.events.length ? "" : " empty"),
            "data-slot": i, title: x && x.bad ? x.bad : null, onclick: e => pickSlot(i, e) },
            h("span", { class: "mono" }, H2(i)), (held ? screenText(x.screen) : "EMPTY") + (x && x.bad ? " !" : ""),
            h("span", { class: "mono r" }, held ? R.autoLength(x.events.length ? x.events[x.events.length - 1].t : 0) : ""),
            h("span", { class: "mono r" }, held ? String(x.events.length) : "")));
    }
    list.addEventListener("keydown", e => {
        const ctl = e.ctrlKey || e.metaKey, k = e.key.toLowerCase();
        if (!(k === "arrowup" || k === "arrowdown" || k === "escape" || (ctl && (k === "a" || k === "c" || k === "v")))) return;
        t.focus = "slots";
        if (k === "arrowup" || k === "arrowdown") {
            const i = Math.max(0, Math.min(N - 1, t.aslot + (k === "arrowup" ? -1 : 1)));
            if (e.shiftKey) { t.marks.add(i); if (i !== t.aslot) { t.aslot = i; t.row = 0; t.rows = null; t.evTop = 0; } t.focus = "slots"; api.refresh(); } else pickSlot(i, null);
        } else if (ctl && k === "a") { t.marks = new Set(range(0, N - 1).filter(i => slots[i])); if (!t.marks.size) t.marks.add(t.aslot); api.refresh(); }
        else if (k === "escape") { t.marks = new Set([t.aslot]); api.refresh(); }
        else if (ctl && k === "c") { copy(); t.focus = null; }
        else if (ctl && k === "v") paste();
        e.preventDefault(); e.stopPropagation();
    });
    queueMicrotask(() => {
        if (!list.isConnected) return;
        if (t.slTop) list.scrollTop = t.slTop;
        if (t.focus === "slots") { list.focus(); t.focus = null; }
    });
    const btn = (label, fn, title) => h("button", { class: "btn small", title, onclick: fn }, label);
    const top = T.card("AUTOMATIONS",
        h("p", { class: "hint", style: "margin-top:0" }, "EACH SLOT THE FM-1'S KEYS, BUTTONS AND KNOBS AS THEY WERE WORKED, AT THEIR TIMES: RECORDED AND PLAYED ON THE FM-1'S AUTOMATIONS SCREEN (HOME + OP6). NO PREVIEW: AN AUTOMATION PLAYS THE FM-1'S CONTROLS."),
        h("div", { class: "rowbtns", style: "margin-top:8px" }, h("span", { class: "hint" }, U.plural(t.marks.size, "SLOT") + " MARKED"),
            btn("COPY", copy, "CTRL+C: THE MARKED SLOTS HOLDING ONE, INTO THE PAGE'S CLIPBOARD"),
            btn("PASTE", paste, "CTRL+V: THE COPIED SLOTS, FROM THE SLOT PICKED ON (IN ANY AUTOMATIONS TAB)")));

    /* ---- the slot shown ---- */
    const page = h("div", { class: "slotpage" });
    const card = T.card(slotName(t.aslot) + (s && s.events.length ? " · " + screenText(s.screen) : ""));
    page.append(card);
    if (s && s.bad) card.append(h("div", { class: "banner bad" }, "IT DOES NOT READ WHOLE: " + s.bad + ". " + (s.events.length ? "THE EVENTS THAT READ ARE SHOWN; " : "") + "A CHANGE WRITES IT ANEW FROM THEM."));
    if (s && s.events.length) {
        /* its screen: HOME's round, or a number past it (WHERE IT IS: kept as read until another is picked) */
        const where = R.AUTO_SCREENS.length;
        card.append(T.field("SCREEN", h("select", { "aria-label": "SCREEN", title: "THE SCREEN IT WAS RECORDED ON: IT PLAYS THERE (WHERE IT IS: ON THE SCREEN AUTOMATIONS WAS OPENED FROM)",
            onchange: e => { const v = +e.target.value, sc = v < where ? v : s.screen >= where ? s.screen : R.AUTO_WHERE;
                putSlot(t.aslot, sc, ev, slotName(t.aslot) + " · SCREEN " + screenText(sc)); } },
            R.AUTO_SCREENS.concat(["WHERE IT IS"]).map((n, k) => h("option", { value: k, selected: k === Math.min(s.screen, where) }, n)))));
    }
    const scan = R.autoScan(ev);
    const gap = i => ev[i].t - (i ? ev[i - 1].t : 0);
    /* the picked row's edits */
    const insert = () => {
        if (!ev.length) { putSlot(t.aslot, screenOf(), [{ t: 0, k: "press", id: 0 }], slotName(t.aslot) + " · ITS FIRST EVENT", [0]); return; }
        const at = t.row + 1, e2 = ev.slice();
        e2.splice(at, 0, Object.assign({}, ev[t.row]));
        edit(e2, "ROW INSERTED", [at]);
    };
    const del = () => {
        if (!t.rows.size) return;
        const gone = [...t.rows].sort((a, b) => a - b), e2 = ev.filter((_, i) => !t.rows.has(i));
        edit(e2, U.plural(gone.length, "ROW") + " DELETED", e2.length ? [Math.min(gone[0], e2.length - 1)] : []);
    };
    const setTime = (i, v) => {
        const e2 = ev.map((e, k) => k === i ? Object.assign({}, e, { t: v }) : e), moved = e2[i];
        e2.sort((a, b) => a.t - b.t);                /* (stable: rows of the same time keep their order) */
        edit(e2, "TIME " + R.autoTime(v), [e2.indexOf(moved)]);
    };
    const setGap = (i, v) => {
        const d = v - gap(i);
        edit(ev.map((e, k) => k >= i ? Object.assign({}, e, { t: e.t + d }) : e), "+" + v + " MS", [i]);
    };
    const setEvent = (i, e, what) => edit(ev.map((x, k) => k === i ? Object.assign({ t: x.t }, e) : x), what, [i]);
    const rowEditor = () => {
        const i = t.row, e = ev[i], box = h("div", { class: "aedit" });
        if (!e) {
            box.append(btn("INSERT", insert, "THE SLOT'S FIRST EVENT: KEY F-3 DOWN AT 0:00.000, RECORDED ON " + screenText(screenOf())));
            return box;
        }
        const timeIn = h("input", { type: "text", class: "mono", value: R.autoTime(e.t), "aria-label": "TIME", title: "THE TIME FROM THE START: 0:12.345, OR IN SECONDS" });
        timeIn.addEventListener("change", () => {
            const v = R.autoParseTime(timeIn.value);
            if (v == null) { say("A TIME AS 0:12.345, OR IN SECONDS", true); timeIn.value = R.autoTime(e.t); return; }
            if (v !== e.t) setTime(i, v);
        });
        const gapIn = h("input", { type: "number", min: 0, max: R.AUTO_GAP_MAX, value: gap(i), "aria-label": "MS SINCE THE LAST", title: "MS SINCE THE EVENT BEFORE: THE ROWS AFTER IT MOVE WITH IT" });
        gapIn.addEventListener("change", () => {
            const v = Math.round(+gapIn.value);
            if (!(v >= 0 && v <= R.AUTO_GAP_MAX)) { say("0 TO " + R.AUTO_GAP_MAX + " MS SINCE THE LAST", true); gapIn.value = gap(i); return; }
            if (v !== gap(i)) setGap(i, v);
        });
        /* its control: the keys, the buttons (pressed or let go), the knobs (turned) */
        const knob = e.k === "knob", cur = (knob ? "n" : "p") + e.id;
        const ctl = h("select", { "aria-label": "CONTROL", onchange: () => {
            const v = ctl.value, id = +v.slice(1);
            const ne = v[0] === "n" ? { k: "knob", id, c: knob ? e.c || 1 : 1 } : { k: knob ? "press" : e.k, id };
            setEvent(i, ne, R.autoEventText(ne));
        } });
        const opt = (v, label) => h("option", { value: v, selected: v === cur }, label);
        const keys = h("optgroup", { label: "KEYS" }), buttons = h("optgroup", { label: "BUTTONS" }), knobs = h("optgroup", { label: "KNOBS" });
        for (let k = 0; k < R.AUTO_KEYS; k++) keys.append(opt("p" + k, R.autoControl("press", k)));
        R.AUTO_BUTTONS.forEach((n, k) => buttons.append(opt("p" + (R.AUTO_KEYS + k), n)));
        R.AUTO_KNOBS.forEach((n, k) => knobs.append(opt("n" + k, n)));
        if (!R.autoControl(e.k, e.id)) ctl.append(opt(cur, "KEPT FOR LATER " + (knob ? "KNOB " : "") + e.id));
        ctl.append(keys, buttons, knobs);
        /* what it does: DOWN or UP; a knob's clicks, never 0 */
        const clicks = [-8, -7, -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7];
        if (knob && !clicks.includes(e.c)) clicks.unshift(e.c);
        const does = h("select", { "aria-label": "DOES", onchange: () => {
            const v = does.value, ne = knob ? { k: "knob", id: e.id, c: +v } : { k: v, id: e.id };
            setEvent(i, ne, R.autoEventText(ne));
        } }, knob ? clicks.map(c => h("option", { value: c, selected: c === e.c }, (c > 0 ? "+" : "") + c + (Math.abs(c) === 1 ? " CLICK" : " CLICKS")))
            : [["press", "DOWN"], ["release", "UP"]].map(([v, n]) => h("option", { value: v, selected: v === e.k }, n)));
        box.append(h("label", null, "TIME"), timeIn, h("label", null, "MS SINCE THE LAST"), gapIn, h("label", null, "EVENT"), ctl, does,
            btn("INSERT", insert, "A COPY OF THIS ROW RIGHT AFTER IT, AT ITS TIME"),
            h("button", { class: "btn small danger", title: "DELETE: THE PICKED ROWS GO, THE OTHERS KEEP THEIR TIMES", onclick: del }, t.rows.size > 1 ? "DELETE " + t.rows.size : "DELETE"));
        return box;
    };
    let editorEl = rowEditor();
    if (s && s.events.length) {
        const parts = api.draft.recs.filter(r => r.sub >> 4 === t.aslot);
        card.append(h("p", { class: "hint", style: "margin-top:0" }, U.plural(ev.length, "EVENT") + " · " + R.autoLength(ev[ev.length - 1].t) + " · " +
            U.plural(parts.length, "PART") + ", " + U.kb(parts.reduce((n, r) => n + r.payload.length, 0))));
    } else card.append(h("p", { class: "hint", style: "margin-top:0" }, "AN EMPTY SLOT: AN AUTOMATION IS RECORDED ON THE FM-1, PASTED HERE, OR MADE HERE EVENT BY EVENT (INSERT)."));
    card.append(editorEl);
    if (ev.length) {
        const esc = x => String(x).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/"/g, "&quot;");
        let html = "";
        ev.forEach((e, i) => {
            const w = scan.why[i];
            html += '<div class="arow' + (t.rows.has(i) ? " sel" : "") + (i === t.row ? " cur" : "") + (w ? " skip" : "") + '" data-i="' + i + '"' + (w ? ' title="' + esc(w) + '"' : "") + "><span>" +
                R.autoTime(e.t) + "</span><span>+" + gap(i) + "</span><span>" + esc(R.autoEventText(e)) + (w ? "<i>" + esc(w) + "</i>" : "") + "</span></div>";
        });
        const table = h("div", { class: "atable", tabIndex: 0, "aria-label": "THE EVENTS", onscroll: () => { t.evTop = table.scrollTop; } });
        table.innerHTML = html;
        /* (a pick drawn in place: the rows' marks and the editor, the table kept as it is) */
        const paintPick = () => {
            for (const el of table.querySelectorAll(".arow.sel, .arow.cur")) el.classList.remove("sel", "cur");
            for (const i of t.rows) table.children[i].classList.add("sel");
            const c = table.children[t.row];
            if (c) { c.classList.add("cur"); c.scrollIntoView({ block: "nearest" }); }
            const ne = rowEditor();
            editorEl.replaceWith(ne);
            editorEl = ne;
        };
        table.addEventListener("click", e => {
            const r = e.target.closest(".arow");
            if (!r) return;
            const i = +r.dataset.i;
            if (e.shiftKey) t.rows = new Set(range(Math.min(t.row, i), Math.max(t.row, i)));
            else if (e.ctrlKey || e.metaKey) { if (t.rows.has(i) && t.rows.size > 1) t.rows.delete(i); else t.rows.add(i); }
            else t.rows = new Set([i]);
            t.row = i;
            paintPick();
        });
        table.addEventListener("keydown", e => {
            const k = e.key.toLowerCase();
            if (k === "arrowup" || k === "arrowdown") {
                const i = Math.max(0, Math.min(ev.length - 1, t.row + (k === "arrowup" ? -1 : 1)));
                if (e.shiftKey) t.rows.add(i); else t.rows = new Set([i]);
                t.row = i;
                paintPick();
            } else if ((e.ctrlKey || e.metaKey) && k === "a") { t.rows = new Set(range(0, ev.length - 1)); paintPick(); }
            else if (k === "delete" || k === "backspace") { if (t.rows.size) { t.focus = "table"; del(); } }
            else return;
            e.preventDefault(); e.stopPropagation();
        });
        queueMicrotask(() => {
            if (!table.isConnected) return;
            table.scrollTop = t.evTop || 0;
            if (t.focus === "table") { table.focus(); t.focus = null; }
        });
        card.append(h("div", { class: "arow ahead" }, h("span", null, "TIME"), h("span", null, "+MS"), h("span", null, "EVENT")), table,
            h("p", { class: "hint" }, scan.held.length ? "STILL DOWN AT THE END (THE FM-1 LETS GO OF IT THERE): " + scan.held.map(id => R.autoControl("press", id) || String(id)).join(", ")
                : "NOTHING IS STILL DOWN AT THE END."));
    }
    return h("div", null, top, h("div", { class: "cartwrap", style: "margin-top:12px" }, list, page));
};
})(globalThis.ED = globalThis.ED || {});
