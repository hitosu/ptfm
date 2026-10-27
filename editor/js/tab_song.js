/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The SONG tab (specs/editor.md, The tabs: SONG; specs/files/songs.md), the KEYS' SONG too: NAME, TEMPO, TICKS A ROW,
 * ROWS A BEAT, SWING, AT THE END; the ORDER (each position's pattern from a list, play from here, move, remove, add);
 * TRIM (tick the positions to keep; REMOVE WHAT IS NO LONGER USED, on at first, says what goes and frees on SAVE); the
 * MIXER (each track's LEVEL, PAN, DUCK, SENDS); the FX SETUP (ED.tabs.fxSetup, shared with LOOPS and DJ SETUP). */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, D = ED.defs, h = U.h, H2 = M.H2;
const NEW = R.newSong("");                          /* (a new song's values: the sliders' double-click) */

function setSong(api, d, s, link, what) {
    const it = Object.assign({}, d, { recs: [{ sub: 0, ver: d.recs[0].ver, payload: R.packSong(s) }], link: link || d.link });
    api.set(M.pruned(Object.assign({}, it, { link: Object.assign({}, it.link) })), what);
}
function patternLabel(p, items) {
    return p.side === "R" ? "PATTERN " + H2(p.num) + (M.patternPlace(p, items) ? " · " + M.patternPlace(p, items) : "") : M.nameOf(p, items);
}

T.kinds.song = function (t, api) {
    const side = t.side, d = api.draft, s = R.unpackSong(d.recs[0].payload), items = api.st[side];
    const set = (fn, what) => { const s2 = R.unpackSong(api.draft.recs[0].payload); fn(s2); setSong(api, api.draft, s2, null, what); api.refresh(); };
    const main = T.card("THE SONG",
        side === "R" && t.base.num === M.FE ? null : T.text("NAME", () => s.name, v => set(x => { x.name = v; }, "NAME " + v), 16),
        T.slider("TEMPO", { min: 3, max: 600, def: NEW.tempo, get: () => s.tempo, set: v => set(x => { x.tempo = v; }, "TEMPO " + v) }),
        T.slider("TICKS A ROW", { min: 1, max: 31, def: NEW.ticks, get: () => s.ticks, set: v => set(x => { x.ticks = v; }, "TICKS A ROW " + v) }),
        T.slider("ROWS A BEAT", { min: 1, max: 16, def: NEW.rpb, get: () => s.rpb, set: v => set(x => { x.rpb = v; }, "ROWS A BEAT " + v) }),
        T.slider("SWING", { min: 50, max: 75, def: NEW.swing, get: () => s.swing, fmt: v => v + " %", set: v => set(x => { x.swing = v; }, "SWING " + v + " %") }),
        T.choice("AT THE END", ["LOOP", "STOP"], () => s.flags & 1, v => set(x => { x.flags = (x.flags & ~1) | v; }, v ? "AT THE END STOP" : "AT THE END LOOP")),
        T.choice("SLIDES", ["AMIGA PERIODS", "LINEAR"], () => s.flags >> 1 & 1, v => set(x => { x.flags = (x.flags & ~2) | v << 1; }, "SLIDES")),
        T.slider("GLOBAL VOLUME", { min: 0, max: 64, def: NEW.gvol, get: () => s.gvol, set: v => set(x => { x.gvol = v; }, "GLOBAL VOLUME " + v) }));

    /* ---- the ORDER and TRIM ---- */
    const pats = items.filter(p => p.kind === K.PATTERN && !p.dim && !(side === "R" && p.num === M.FE));
    const trim = t.trim = t.trim || { on: false, keep: null, removeUnused: true };
    const order = h("div", { class: "order" });
    const posTarget = n => api.draft.link[M.lk("p", n)];
    s.positions.forEach((n, i) => {
        const target = posTarget(n);
        const sel = h("select", { onchange: e => {
            const p = pats.find(x => x.id === e.target.value);
            if (!p) return;
            const num = M.numberFor(api.draft, "p", p.id, side, p.num, p.orig);
            const link = Object.assign({}, api.draft.link, { [M.lk("p", num)]: p.id });
            const s2 = R.unpackSong(api.draft.recs[0].payload);
            s2.positions[i] = num;
            setSong(api, api.draft, s2, link, "POSITION " + H2(i));
            api.refresh();
        } }, pats.map(p => h("option", { value: p.id, selected: p.id === target }, patternLabel(p, items))),
            !pats.some(p => p.id === target) ? h("option", { value: "", selected: true }, "PATTERN " + H2(n) + " (NOT HERE)") : null);
        const keep = trim.keep ? trim.keep[i] : true;
        order.append(h("div", { class: "orow" + (trim.on && !keep ? " off" : "") },
            trim.on ? h("input", { type: "checkbox", checked: keep, title: "KEEP THIS POSITION", onchange: e => { trim.keep[i] = e.target.checked; api.refresh(); } }) : null,
            h("span", { class: "mono", style: "width:28px" }, H2(i)), sel,
            h("button", { class: "btn small", title: "EDIT ITS PATTERN IN A TAB", disabled: !pats.some(p => p.id === target),
                onclick: () => { const p = pats.find(x => x.id === target); if (p) ED.tabs.open(p); } }, "✎"),
            h("button", { class: "btn small", title: "PLAY FROM HERE", onclick: () => ED.player.play(api.draft, { st: api.stWith(), pos: i }) }, "▶"),
            h("button", { class: "btn small", title: "UP", disabled: i === 0, onclick: () => set(x => { [x.positions[i - 1], x.positions[i]] = [x.positions[i], x.positions[i - 1]]; }, "POSITION MOVED") }, "↑"),
            h("button", { class: "btn small", title: "DOWN", disabled: i === s.positions.length - 1, onclick: () => set(x => { [x.positions[i + 1], x.positions[i]] = [x.positions[i], x.positions[i + 1]]; }, "POSITION MOVED") }, "↓"),
            h("button", { class: "btn small danger", title: "REMOVE THIS POSITION", disabled: s.positions.length < 2, onclick: () => set(x => { x.positions.splice(i, 1); }, "POSITION REMOVED") }, "✕")));
    });
    const trimInfo = h("div", { class: "box", hidden: !trim.on });
    const goes = () => {
        if (!trim.on || !trim.keep) return { ids: new Set(), bytes: 0 };
        const kept = new Set(s.positions.filter((_, i) => trim.keep[i]).map(n => posTarget(n)));
        const dropped = [...new Set(s.positions.map(n => posTarget(n)).filter(id => id && !kept.has(id)))];
        if (!trim.removeUnused) return { ids: new Set(), bytes: 0, dropped };
        const others = items.filter(x => x.id !== t.itemId);
        const usedElse = id => others.some(x => M.hardUses(x, M.byIdMap(items)).includes(id));
        const orphans = dropped.filter(id => !usedElse(id));
        const st2 = { [side]: items.map(x => x.id === t.itemId ? Object.assign({}, x, { link: {} }) : x) };
        const ex = orphans.length ? M.exclusive(st2[side], orphans) : new Set();
        const all = new Set(orphans.concat([...ex]));
        let bytes = 0;
        for (const id of all) { const x = items.find(y => y.id === id); if (x) bytes += M.size(x); }
        return { ids: all, bytes, dropped };
    };
    const paintTrim = () => {
        const g = goes();
        trimInfo.textContent = g.ids.size ? "ON SAVE, ALSO GOES: " + U.plural(g.ids.size, "ITEM") + " NOTHING ELSE USES, " + U.kb(g.bytes) : "NOTHING ELSE GOES ON SAVE";
    };
    if (trim.on) paintTrim();
    const trimBar = h("div", { class: "rowbtns" },
        h("button", { class: "btn small" + (trim.on ? " on" : ""), onclick: () => { trim.on = !trim.on; trim.keep = trim.on ? s.positions.map(() => true) : null; api.refresh(); } }, "TRIM"),
        trim.on ? h("label", { class: "octrow", style: "cursor:pointer" }, h("input", { type: "checkbox", checked: trim.removeUnused, onchange: e => { trim.removeUnused = e.target.checked; paintTrim(); } }), "REMOVE WHAT IS NO LONGER USED") : null,
        trim.on ? h("button", { class: "btn small primary", onclick: () => {
            const g = goes();
            const s2 = R.unpackSong(api.draft.recs[0].payload);
            s2.positions = s2.positions.filter((_, i) => trim.keep[i]);
            if (!s2.positions.length) { U.status("KEEP ONE POSITION AT LEAST", { err: true }); return; }
            /* on SAVE, of what TRIM dropped, only what nothing uses then goes (with what only it uses): an UNDO in the
             * tab, or the pattern put back in the order, keeps it */
            const dropped = g.dropped || [];
            t.onSave = dropped.length && trim.removeUnused ? st => {
                const us = M.users(st[side]);
                const orphans = dropped.filter(id => st[side].some(x => x.id === id) && !(us.get(id) || []).length);
                if (!orphans.length) return { st };
                const r = M.remove(st, side, orphans, true);
                return { st: r.st, what: U.plural(r.gone.size, "ITEM") + " NO LONGER USED REMOVED, " + U.kb(r.bytes) };
            } : null;
            trim.on = false; trim.keep = null;
            setSong(api, api.draft, s2, null, "TRIMMED TO " + U.plural(s2.positions.length, "POSITION"));
            api.refresh();
        } }, "TRIM NOW") : null);
    const orderCard = T.card("THE ORDER", order, h("div", { class: "rowbtns", style: "margin-top:8px" },
        h("button", { class: "btn small", disabled: s.positions.length >= 256 || !pats.length, onclick: () => set(x => { x.positions.push(x.positions[x.positions.length - 1]); }, "POSITION ADDED") }, "+ ADD A POSITION"),
        h("button", { class: "btn small", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶ PLAY THE SONG")),
        h("div", { class: "fxbox" }, trimBar, trimInfo));

    /* ---- the MIXER ---- */
    let ntrk = 1;
    for (const n of s.positions) { const p = items.find(x => x.id === posTarget(n)); if (p) ntrk = Math.max(ntrk, p.recs[0].payload[0]); }
    const mix = h("div");
    const MIXW = ["LEVEL", "PAN", "DUCK", "CHO", "DLY", "REV"], MIXT = ["LEVEL", "PAN", "DUCK", "CHORUS SEND", "DELAY SEND", "REVERB SEND"];
    mix.append(h("div", { class: "mixrow mixhead" }, h("span", null, "TR"), ...MIXW.map((f, k) => h("span", { title: MIXT[k] }, f))));
    for (let tr = 0; tr < ntrk; tr++) {
        mix.append(h("div", { class: "mixrow" }, h("span", { class: "mono" }, H2(tr)), ...R.MIX_FIELDS.map((f, k) => {
            const i = h("input", { type: "text", value: H2(s.mixer[tr][k]), maxLength: 2, style: "width:100%;font-family:var(--mono)", title: f.toUpperCase() + " (HEX)" });
            i.addEventListener("change", () => { const v = parseInt(i.value, 16); if (isNaN(v)) return; set(x => { x.mixer[tr][k] = Math.max(0, Math.min(k === 1 ? 255 : 127, v)); }, "TRACK " + H2(tr) + " " + f.toUpperCase()); });
            return i;
        })));
    }
    const mixCard = T.card("THE MIXER", h("p", { class: "hint" }, "LEVEL 7F FULL · PAN 00 LEFT, 80 CENTRE, FF RIGHT · DUCK AND THE SENDS 00-7F"), mix);
    t.fxPicture = (e, fx) => ED.pics.fx(e, fx, { tempo: s.tempo, ticks: s.ticks, rpb: s.rpb });
    if (!ED.pics.available()) ED.pics.ready().then(() => { if (t.el.isConnected) api.refresh(); }).catch(() => {});
    const fxCard = T.card("THE FX SETUP", T.fxSetup(s.fx, (fx, what) => set(x => { x.fx = fx; }, what), t));
    return h("div", { class: "cards" }, main, orderCard, mixCard, fxCard);
};

/* ---- an FX setup's 64 bytes (songs.md#the-fx-setup): ROUTING, the chain's order, each effect on or off, its knobs ---- */
T.fxSetup = function (fxIn, onChange, t, o) {
    o = o || {};
    const fx = fxIn.slice();
    const box = h("div");
    const st = t.fxv = t.fxv || { pick: 6 };
    const change = what => onChange(fx.slice(), what);
    const NAMES = D.FX_NAMES;
    const isOn = n => !!((fx[7] | fx[8] << 8) >> n & 1);
    if (!o.noRouting) box.append(T.choice("ROUTING", ["GLOBAL", "SENDS"], () => fx[0] & 1, v => { fx[0] = v; change("ROUTING " + (v ? "SENDS" : "GLOBAL")); }));
    const chain = R.fxChain(fx);
    /* the chain's order by dragging a chip before or after another (◀ ▶ on each do the same a step at a time, by touch
     * too); the drag carries the chip's place in a type of its own, so FILES' drops never take it */
    const DRAG = "application/x-ptfm-fx-chain";
    const marks = () => box.querySelectorAll(".fxchip.drop-before, .fxchip.drop-after, .fxchip.dragging").forEach(x => x.classList.remove("drop-before", "drop-after", "dragging"));
    const after = (e, chip) => { const r = chip.getBoundingClientRect(); return e.clientX > r.left + r.width / 2; };
    const dragChip = (chip, i) => {
        chip.draggable = true;
        chip.addEventListener("dragstart", e => { e.dataTransfer.setData(DRAG, String(i)); e.dataTransfer.effectAllowed = "move"; chip.classList.add("dragging"); });
        chip.addEventListener("dragover", e => {
            if (![...e.dataTransfer.types].includes(DRAG)) return;
            e.preventDefault();
            e.dataTransfer.dropEffect = "move";
            const a = after(e, chip);
            chip.classList.toggle("drop-after", a); chip.classList.toggle("drop-before", !a);
        });
        chip.addEventListener("dragleave", () => chip.classList.remove("drop-before", "drop-after"));
        chip.addEventListener("drop", e => {
            if (![...e.dataTransfer.types].includes(DRAG)) return;
            e.preventDefault();
            const from = +e.dataTransfer.getData(DRAG);
            let to = i + (after(e, chip) ? 1 : 0);
            marks();
            if (from === to || from === to - 1 || !(from >= 0 && from < chain.length)) return;   /* (dropped where it is) */
            const c = chain.slice(), [m] = c.splice(from, 1);
            if (from < to) to--;
            c.splice(to, 0, m);
            R.setFxChain(fx, c);
            change("THE CHAIN'S ORDER: " + NAMES[m] + " MOVED");
        });
        chip.addEventListener("dragend", marks);
    };
    const row = h("div", { class: "chain" });
    chain.forEach((n, i) => {
        const chip = h("div", { class: "fxchip" + (st.pick === n ? " pick" : "") + (n < 10 && isOn(n) ? " lit" : "") + (n < 10 && !isOn(n) ? " off" : ""), onclick: () => { st.pick = n; t.refreshFx && t.refreshFx(); } },
            n < 10 ? h("button", { title: isOn(n) ? "ON: CLICK FOR OFF" : "OFF: CLICK FOR ON", onclick: e => { e.stopPropagation(); R.setFxOn(fx, (fx[7] | fx[8] << 8) ^ (1 << n)); change(NAMES[n] + (isOn(n) ? " ON" : " OFF")); } }, isOn(n) ? "●" : "○") : null,
            NAMES[n] || "?",
            h("button", { title: "EARLIER IN THE CHAIN", disabled: i === 0, onclick: e => { e.stopPropagation(); const c = chain.slice(); [c[i - 1], c[i]] = [c[i], c[i - 1]]; R.setFxChain(fx, c); change("THE CHAIN'S ORDER"); } }, "◀"),
            h("button", { title: "LATER IN THE CHAIN", disabled: i === chain.length - 1, onclick: e => { e.stopPropagation(); const c = chain.slice(); [c[i + 1], c[i]] = [c[i], c[i + 1]]; R.setFxChain(fx, c); change("THE CHAIN'S ORDER"); } }, "▶"));
        dragChip(chip, i);
        row.append(chip);
        if (i < chain.length - 1) row.append(h("span", { class: "fxarrow" }, "→"));
    });
    for (const n of [8, 9]) row.append(h("div", { class: "fxchip" + (st.pick === n ? " pick" : "") + (isOn(n) ? " lit" : " off"), onclick: () => { st.pick = n; t.refreshFx && t.refreshFx(); } },
        h("button", { onclick: e => { e.stopPropagation(); R.setFxOn(fx, (fx[7] | fx[8] << 8) ^ (1 << n)); change(NAMES[n] + (isOn(n) ? " ON" : " OFF")); } }, isOn(n) ? "●" : "○"), NAMES[n]));
    box.append(row);
    const knobs = h("div", { class: "fxbox" });
    const paintKnobs = () => {
        knobs.textContent = "";
        const e = D.FX.find(x => x.num === st.pick);
        if (st.pick === 9) {
            knobs.append(h("div", { class: "fxname" }, "METRONOME"),
                T.slider("TEMPO", { min: 0, max: 300, def: 0, get: () => fx[9] | fx[10] << 8, fmt: v => v < 30 ? "FOLLOW" : String(v), set: v => { const x = v < 30 ? 0 : v; fx[9] = x & 255; fx[10] = x >> 8; change("METRONOME TEMPO"); } }),
                T.slider("BEATS A BAR", { min: 1, max: 16, def: 4, get: () => fx[11] || 4, set: v => { fx[11] = v; change("METRONOME BEATS A BAR " + v); } }),
                T.choice("SOUND", D.METRONOME_SOUNDS, () => fx[12], v => { fx[12] = v; change("METRONOME SOUND"); }),
                T.slider("LEVEL", { min: 0, max: 100, def: 60, get: () => fx[13], fmt: v => v + " %", set: v => { fx[13] = v; change("METRONOME LEVEL " + v + " %"); } }));
        } else if (e) {
            knobs.append(h("div", { class: "fxname" }, e.name + (isOn(e.num) ? "" : " · OFF")));
            let pic = t.fxPicture ? t.fxPicture(e.num, fx) : null;
            if (pic) knobs.append(pic);
            /* while a knob moves, the picture drawn again from what it would make */
            const live = (at, v) => {
                if (!pic) return;
                const f2 = fx.slice(); f2[at] = v;
                const np = t.fxPicture(e.num, f2);
                if (np) { pic.replaceWith(np); pic = np; }
            };
            for (const k of e.knobs) {
                const at = 16 + (k.code - 0x90);
                knobs.append(T.slider(k.n, { min: 0, max: D.knobSteps(k), def: D.knobStep(k, k.def), get: () => Math.min(fx[at], D.knobSteps(k)), fmt: v => D.knobText(k, v), set: v => { fx[at] = v; change(e.name + " " + k.n + " " + D.knobText(k, v)); },
                    live: v => live(at, v) }));
            }
        } else knobs.append(h("p", { class: "hint" }, "◆ SENDS: WHERE THE TRACKS' SENDS ENTER THE CHAIN (ROUTING SENDS)."));
    };
    paintKnobs();
    t.refreshFx = () => { const nb = T.fxSetup(fx, onChange, t, o); box.replaceWith(nb); };
    box.append(knobs);
    return box;
};
})(globalThis.ED = globalThis.ED || {});
