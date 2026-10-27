/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The cartridge tabs (specs/editor.md, The tabs: Cartridges): the cartridge's name and its instruments in a list; an
 * instrument's page: NAME, LEVEL (AUTO LEVEL as the FM-1 measures it), its engine's parameters as the FM-1's EDIT pages
 * name them, each picture of the FM-1's own (ED.pics) beside the parameters it draws; DX7, Juno-106, TB-303, CHIP (a
 * section a page of the FM-1's: TONE, VOLUME, TABLES, WAVE), a FUSION slot (its pattern, its
 * LEVEL), a sampler's instrument (its sample, START, END, the loop, BASE NOTE, TUNE, VOLUME, the envelope) and the
 * sampler's samples (each with NEW INSTRUMENT FOR IT). Every instrument page has a keyboard of the FM-1's 27 keys,
 * played by mouse, touch, the computer's keys or a MIDI keyboard picked from the computer's inputs. */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, D = ED.defs, h = U.h, H2 = M.H2;
const SLOT_MAX = { [K.DX7]: 32, [K.JUNO]: 128, [K.TB303]: 128, [K.FUSION]: 64, [K.CHIP]: 128, [K.SAMPLER]: 64 };

function decode(d) { return d.kind === K.SAMPLER ? R.unpackSampler(d.recs[0].payload) : R.unpackCart(d.kind, d.recs[0].payload); }
function encode(d, c, link) {
    const payload = d.kind === K.SAMPLER ? R.packSampler(c) : R.packCart(d.kind, c);
    return Object.assign({}, d, { recs: [{ sub: 0, ver: d.recs[0].ver, payload }], link: link || d.link });
}
const slotsKey = c => c.instruments ? "instruments" : "slots";
const lvText = v => (v > 0 ? "+" : v < 0 ? "-" : "") + (Math.abs(v) / 2).toFixed(1) + " DB";

T.kinds.cart = function (t, api) {
    const side = t.side, d = api.draft, kind = d.kind;
    const c = decode(d), list = c[slotsKey(c)];
    t.slot = t.slot != null ? t.slot : Math.max(0, list.findIndex(Boolean));
    t.keyOn = null;                                 /* (an empty place plays nothing: its keyboard sets it again) */
    const draftWith = (fn, link) => { const c2 = decode(api.draft); fn(c2); return encode(api.draft, c2, link); };
    const set = (fn, what, link) => { api.set(draftWith(fn, link), what); api.refresh(); };
    /* ---- the FM-1's own pictures of the instrument (ED.pics), each beside the controls it draws ---- */
    const figs = [];
    const paint = (f, draft) => {
        const c = ED.pics.available() ? f.draw(draft) : null, old = f.el.querySelector("canvas");
        if (c) { if (old) old.replaceWith(c); else f.el.append(c); }
        f.el.hidden = !c;
    };
    /* pic(caption, what, page, group): a figure of one of the slot's pictures (group a function: read at each drawing);
     * .el the figure, .repaint() draws it again */
    const pic = (cap, what, page, group) => {
        const f = { el: h("figure", null, h("figcaption", null, cap)),
            draw: draft => ED.pics.instrument(draft, api.stWith(), t.slot, what, page || 0, typeof group === "function" ? group() : group || 0) };
        f.repaint = () => paint(f, api.draft);
        figs.push(f);
        f.repaint();
        return f;
    };
    /* while a slider moves, the pictures drawn again from what it would make (nothing changed yet) */
    const live = fn => { const d2 = draftWith(fn); for (const f of figs) paint(f, d2); };
    const col = U.ENG_COL[kind];

    /* ---- the list ---- */
    const slots = h("div", { class: "slots", onscroll: () => { t.slotsTop = slots.scrollTop; } },
        h("h5", null, (M.ENGINE[kind] || "") + " · " + list.filter(Boolean).length + " INSTRUMENTS"));
    /* (where the list was scrolled kept over the tab's redraws; at first the picked instrument in view) */
    queueMicrotask(() => {
        if (!slots.isConnected) return;
        if (t.slotsTop != null) { slots.scrollTop = t.slotsTop; return; }
        const r = slots.querySelector(".slotrow.on");
        if (r && r.offsetTop + r.offsetHeight > slots.clientHeight) slots.scrollTop = r.offsetTop - slots.clientHeight / 2;
    });
    const n = Math.min(SLOT_MAX[kind], Math.max(list.length + 1, 1));
    for (let i = 0; i < n; i++) {
        const s = list[i];
        slots.append(h("div", { class: "slotrow" + (i === t.slot ? " on" : "") + (s ? "" : " empty"), onclick: () => { t.slot = i; api.refresh(); } },
            h("span", { class: "mono" }, H2(i)), s ? s.name : "EMPTY",
            s && ED.player.canPlay(api.draft) ? h("button", { class: "act", title: "PLAY ITS ARPEGGIO", onclick: e => { e.stopPropagation(); ED.player.play(api.draft, { st: api.stWith(), slot: i }); } }, "▶") : h("span")));
    }
    const top = T.card("THE CARTRIDGE", h("div", { class: "namerow" }, T.text("NAME", () => c.name, v => set(x => { x.name = v; }, "NAME " + v), 12),
        h("button", { class: "btn small", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶ PLAY EVERY INSTRUMENT")));

    /* ---- an instrument's page ---- */
    const s = list[t.slot];
    const page = h("div", { class: "slotpage" });
    if (!s) {
        page.append(T.card("INSTRUMENT " + H2(t.slot), h("p", null, "AN EMPTY PLACE."),
            kind === K.FUSION || kind === K.SAMPLER && !c.samples.some(Boolean) ? null :
            h("button", { class: "btn primary", onclick: () => set(x => { const l = x[slotsKey(x)]; while (l.length < t.slot) l.push(null); l[t.slot] = D.newSlot(kind, "NEW " + (M.ENGINE[kind] || "")); }, "INSTRUMENT " + H2(t.slot) + " MADE") }, "NEW INSTRUMENT HERE")));
    } else {
        const mutSlot = (x, f, v) => { x[slotsKey(x)][t.slot] = Object.assign({}, x[slotsKey(x)][t.slot], { [f]: v }); };
        const upd = (f, v, what) => set(x => mutSlot(x, f, v), what);
        page.append(T.card("INSTRUMENT " + H2(t.slot),
            T.text("NAME", () => s.name, v => upd("name", v, "NAME " + v), 10),
            T.slider("LEVEL", { min: -128, max: 127, def: 0, get: () => s.level, fmt: lvText, acc: col, set: v => upd("level", v, "LEVEL " + lvText(v)), live: v => live(x => mutSlot(x, "level", v)) }),
            h("div", { class: "rowbtns" },
                h("button", { class: "btn small", title: "MEASURE IT AS THE FM-1'S AUTO LEVEL DOES", onclick: () => autoLevel(t, api, upd) }, "AUTO LEVEL"),
                h("button", { class: "btn small danger", onclick: () => set(x => { x[slotsKey(x)][t.slot] = null; }, "INSTRUMENT " + H2(t.slot) + " EMPTIED") }, "EMPTY THIS PLACE"))));
        page.append(...params(t, api, kind, s, c, { mutSlot, upd, set, live, pic }));
        page.append(keyboard(t, api));
        if (!ED.pics.available()) ED.pics.ready().then(() => { if (t.el.isConnected) api.refresh(); }).catch(() => {});
    }
    if (kind === K.SAMPLER) page.append(samplesCard(t, api, c, set));
    return h("div", null, top, h("div", { class: "cartwrap", style: "margin-top:12px" }, slots, page));
};

/* ---- each engine's parameters, as the FM-1's EDIT pages name them, in sections with the pictures they change ---- */
function params(t, api, kind, s, c, o) {
    const { mutSlot, upd, set, live, pic } = o;
    const col = U.ENG_COL[kind], W = ED.pics.WHAT;
    const details = () => pic("AS INSTRUMENTS SHOWS IT", W.DETAILS).el;
    /* sliders for a parameter list: mut(cart, field, value) changes the decoded cartridge, scope the values shown */
    /* defs: the values a new instrument of the engine has there (a double-click's) */
    const nd = T.slotDefault(kind);
    const sl = (mut, list, scope, prefix, defs) => list.map(p => T.slider(p.n, { min: 0, max: p.max, get: () => Math.min(p.max, scope ? scope[p.f] : s[p.f]), fmt: p.fmt || String, acc: col,
        def: (defs || nd)[p.f],
        set: v => set(x => mut(x, p.f, v), (prefix || "") + p.n + " " + (p.fmt ? p.fmt(v) : v)), live: v => live(x => mut(x, p.f, v)) }));
    /* (the DX7's edit picture: the algorithm and the picked operator's envelope, beside every parameter) */
    if (kind === K.DX7) {
        const v = s.voice;
        const mutVoice = (x, f, val) => { const q = x.slots[t.slot]; x.slots[t.slot] = Object.assign({}, q, { voice: Object.assign({}, q.voice, { [f]: val }) }); };
        t.op = t.op != null ? t.op : 0;
        const tabs = h("div", { class: "seg", style: "margin-bottom:8px" }, [0, 1, 2, 3, 4, 5].map(i => h("button", { class: t.op === i ? "on" : "", onclick: () => { t.op = i; api.refresh(); } }, "OP" + (i + 1))));
        const oi = 5 - t.op;                         /* (OP6 first in the record, as Yamaha's packed voice) */
        const mutOp = (x, f, val) => { const q = x.slots[t.slot], ops = q.ops.slice(); ops[oi] = Object.assign({}, ops[oi], { [f]: val }); x.slots[t.slot] = Object.assign({}, q, { ops }); };
        return [T.picSection("THE DX7", [pic("EDIT: OPERATOR " + (t.op + 1), W.EDIT, 0, t.op).el, details()],
            h("h5", null, "THE VOICE AND THE LFO"), ...sl(mutVoice, D.DX7_VOICE, v, "", nd.voice),
            h("h5", null, "OPERATOR " + (t.op + 1)), tabs, ...sl(mutOp, D.DX7_OP, s.ops[oi], "OP" + (t.op + 1) + " ", nd.ops[oi]))];
    }
    if (kind === K.JUNO) return [T.picSection("THE JUNO-106", [details(), pic("EDIT", W.EDIT).el], ...sl(mutSlot, D.JUNO))];
    if (kind === K.TB303) return [T.picSection("THE TB-303", [details(), pic("EDIT", W.EDIT).el], ...sl(mutSlot, D.TB303))];
    /* CHIP: a section a page of the FM-1's (TONE, VOLUME, TABLES, WAVE) with that page's picture; TABLES' shows the
     * table last touched, as the FM-1's cursor picks it (its rows: VOL, ARP, PITCH, VIBRATO, DUTY, NOISE) */
    if (kind === K.CHIP) {
        const isTable = p => /^(tab|spd|vib)_/.test(p.f);
        const of = (g, tables) => D.CHIP.filter(p => p.grp === g && isTable(p) === !!tables);
        const groups = names => names.flatMap(g => [h("h5", null, g), ...sl(mutSlot, of(g))]);
        const ROWS = ["VOL", "ARP", "PITCH", "VIBRATO", "DUTY", "NOISE"];
        t.chipRow = t.chipRow || 0;
        const tabCap = () => "EDIT: TABLES · " + ROWS[t.chipRow];
        const tables = pic(tabCap(), W.EDIT, 2, () => t.chipRow);
        const rows = ROWS.map((g, row) => {
            const box = h("div", { class: "grp" + (row === t.chipRow ? " pick" : "") }, h("h5", null, g), ...sl(mutSlot, of(g, true)));
            const touch = () => {
                if (t.chipRow === row) return;
                t.chipRow = row;
                rows.forEach((b, i) => b.classList.toggle("pick", i === row));
                tables.el.querySelector("figcaption").textContent = tabCap();
                tables.repaint();
            };
            box.addEventListener("pointerdown", touch);
            box.addEventListener("focusin", touch);
            return box;
        });
        return [T.picSection("TONE", [pic("EDIT: TONE", W.EDIT, 0).el, details()], ...groups(["OSC", "NOISE", "CLOCK", "SWEEP"])),
            T.picSection("VOLUME", [pic("EDIT: VOLUME", W.EDIT, 1).el], ...groups(["VOLUME", "PSG ENV"])),
            T.picSection("TABLES", [tables.el], ...rows),
            T.picSection("WAVE", [pic("EDIT: WAVE", W.EDIT, 3).el], ...sl(mutSlot, of("TABLE")))];
    }
    if (kind === K.FUSION) {
        const items = api.st[t.side], pats = items.filter(p => p.kind === K.PATTERN && !p.dim);
        const cur = s.pattern === 0xFF ? null : api.draft.link[M.lk("p", s.pattern)];
        const sel = h("select", { onchange: e => {
            if (!e.target.value) { upd("pattern", 0xFF, "NO PATTERN"); return; }
            const p = pats.find(x => x.id === e.target.value), num = M.numberFor(api.draft, "p", p.id, t.side, p.num, p.orig);
            set(x => { x.slots[t.slot] = Object.assign({}, x.slots[t.slot], { pattern: num }); }, "PATTERN", Object.assign({}, api.draft.link, { [M.lk("p", num)]: p.id }));
        } }, h("option", { value: "" }, "NO PATTERN"), pats.map(p => h("option", { value: p.id, selected: p.id === cur }, p.side === "R" ? "PATTERN " + H2(p.num) : M.nameOf(p, items))));
        return [T.picSection("THE FUSION SLOT", [pic("EDIT", W.EDIT).el, details()], T.field("PATTERN", sel))];
    }
    if (kind === K.SAMPLER) {
        const e = c.samples[s.sample];
        const max = e ? e.length : 1 << 20;
        const pts = [["start", "START"], ["end", "END"], ["loop_start", "LOOP START"], ["loop_end", "LOOP END"]].map(([f, n]) =>
            T.slider(n, { min: 0, max, def: f === "end" || f === "loop_end" ? max : 0, get: () => Math.min(max, s[f]), fmt: v => e && e.rate ? (v / e.rate).toFixed(3) + " S" : String(v), acc: col, set: v => upd(f, v, n),
                live: v => live(x => mutSlot(x, f, v)) }));
        const smp = h("select", { onchange: ev => upd("sample", +ev.target.value, "SAMPLE " + H2(+ev.target.value)) },
            c.samples.map((x, i) => x ? h("option", { value: i, selected: i === s.sample }, H2(i) + " " + x.name) : null));
        const mutEnv = (x, f, val) => { const q = x.instruments[t.slot]; x.instruments[t.slot] = Object.assign({}, q, { env: Object.assign({}, q.env, { [f]: val }) }); };
        return [T.picSection("THE SAMPLE INSTRUMENT", [pic("EDIT", W.EDIT).el, details()], T.field("SAMPLE", smp), ...pts, ...sl(mutSlot, D.SAMPLER_INST),
            h("h5", null, "THE ENVELOPE"), ...sl(mutEnv, D.ENV, s.env, "", nd.env))];
    }
    return [];
}
/* a sampler's samples, each with NEW INSTRUMENT FOR IT */
function samplesCard(t, api, c, set) {
    const rows = c.samples.map((e, i) => e ? h("div", { class: "orow" }, h("span", { class: "mono", style: "width:28px" }, H2(i)), h("span", { class: "grow" }, e.name),
        h("span", { class: "dim mono" }, (e.rate / 1000).toFixed(1) + " KHZ · " + (e.rate ? (e.length / e.rate).toFixed(2) : "?") + " S"),
        h("button", { class: "btn small", onclick: () => set(x => {
            let k = x.instruments.findIndex(z => !z);
            if (k < 0) k = x.instruments.length;
            if (k >= 64) return;
            x.instruments[k] = Object.assign(D.newSlot(K.SAMPLER, e.name), { sample: i, start: 0, end: e.length, loop_start: 0, loop_end: e.length });
            t.slot = k;
        }, "NEW INSTRUMENT FOR " + e.name) }, "NEW INSTRUMENT FOR IT")) : null);
    return T.card("ITS SAMPLES", rows.some(Boolean) ? rows : h("p", { class: "hint" }, "NO SAMPLES: MOVE SOUNDS ONTO THIS SAMPLER IN FILES."));
}
/* AUTO LEVEL: the firmware's own measure on the page's instance, the cartridge (and its samples) loaded there */
async function autoLevel(t, api, upd) {
    try {
        const fw = await ED.pics.ready();
        const p = ED.player.plan(api.draft, api.stWith(), { slot: t.slot });
        ED.pics.image(p.recs);
        const num = api.draft.fw ? api.draft.num : (p.job.events.find(x => x[1] === "ed_key_on") || { 2: [0, 0] })[2][1];
        const v = fw.level(num, t.slot);
        if (v == null) { U.status("AUTO LEVEL: IT MADE NO SOUND", { err: true }); return; }
        upd("level", Math.max(-128, Math.min(127, v)), "AUTO LEVEL " + lvText(v));
    } catch (e) { U.status("AUTO LEVEL: " + String(e.message || e).toUpperCase(), { err: true }); }
}

/* ---- the keyboard: the FM-1's 27 keys, the computer's keys, a MIDI keyboard ---- */
/* Where the instrument tabs are played from is the page's, one choice for every tab, kept in this browser (FROM.id: ""
 * none, "pc" the VIRTUAL MIDI KEYBOARD, else a MIDI input, found again by its id or else its name). One set of
 * listeners plays the tab shown (its t.keyOn / t.keyOff); a note goes off in the tab it went on in. MIDI is asked for
 * when the list is opened, one question at a time (not on focus: the browser's question takes the focus away and gives
 * it back); a browser that already allows it is not asked. */
const PC = "pc", FROM_KEY = "ptfm-editor-keys-from";
const isTyping = e => e.target.closest && e.target.closest("input, select, textarea, [contenteditable]");
const FROM = (() => {
    try { const v = JSON.parse(localStorage.getItem(FROM_KEY)); if (v && typeof v.id === "string") return { id: v.id, name: String(v.name || "") }; }
    catch (e) { /* (none kept: no MIDI keyboard) */ }
    return { id: "", name: "" };
})();
let midi = null, asking = null, port = null;
const going = new Map();                            /* a note on -> the tab it plays in */
const pcGoing = new Map();                          /* a computer key down -> its note */
function shownKeys() { const t = ED.tabs.current(); return t && t.keyOn && t.el.isConnected ? t : null; }
function noteOn(n) { const t = shownKeys(); if (!t || going.has(n)) return; going.set(n, t); t.keyOn(n); }
function noteOff(n) { const t = going.get(n); if (!t) return; going.delete(n); t.keyOff(n); }
function midiMessage(e) {
    const [st, a, b] = e.data;
    if ((st & 0xF0) === 0x90 && b) noteOn(a); else if ((st & 0xF0) === 0x80 || ((st & 0xF0) === 0x90 && !b)) noteOff(a);
}
const portNamed = () => !midi || !FROM.id || FROM.id === PC ? null
    : midi.inputs.get(FROM.id) || [...midi.inputs.values()].find(p => p.name === FROM.name) || null;
/* the MIDI input listened to: the one picked, when it is there (again after it is plugged in) */
function listen() {
    const p = portNamed();
    if (p === port) return;
    if (port) port.removeEventListener("midimessage", midiMessage);
    port = p;
    if (port) { port.addEventListener("midimessage", midiMessage); port.open().catch(() => {}); }
}
function askMidi() {
    if (midi) return Promise.resolve(midi);
    if (!asking) asking = navigator.requestMIDIAccess().then(a => {
        midi = a;
        a.addEventListener("statechange", () => { listen(); paintLists(); });
        listen(); paintLists();
        return a;
    }).finally(() => { asking = null; });
    return asking;
}
/* MIDI allowed already (a READ, an earlier visit): taken with no question, so the kept MIDI keyboard plays at once */
function midiIfAllowed() {
    if (midi || asking || !navigator.requestMIDIAccess || !navigator.permissions) return;
    navigator.permissions.query({ name: "midi" }).then(s => { if (s.state === "granted") askMidi().catch(() => {}); }).catch(() => {});
}
/* a keyboard's list as the choice now is: none, the VIRTUAL MIDI KEYBOARD, the MIDI inputs there now, and the one
 * picked while it is not there (or MIDI not asked for yet) */
function fillList(s) {
    while (s.options.length > 2) s.remove(2);
    if (midi) for (const p of midi.inputs.values()) s.append(h("option", { value: p.id }, p.name));
    const p = portNamed(), id = p ? p.id : FROM.id;
    if (id && id !== PC && !p) s.append(h("option", { value: id }, FROM.name + (midi ? " (NOT CONNECTED)" : "")));
    s.value = id;
}
function paintLists() {
    for (const s of document.querySelectorAll("select.keysfrom")) fillList(s);
    for (const e of document.querySelectorAll(".keyshint")) e.textContent = hintText();
}
const hintText = () => FROM.id === PC ? "VIRTUAL MIDI KEYBOARD: Z S X D C … PLAY FROM F3 (Q W E … ABOVE), [ AND ] THE OCTAVE · ANYWHERE ON THIS TAB BUT IN A FIELD"
    : "CLICK THE KEYS, OR CLICK HERE AND PLAY THE COMPUTER'S KEYS: Z S X D C … FROM F3";
function pick(id, name) {
    FROM.id = id; FROM.name = name || "";
    try { localStorage.setItem(FROM_KEY, JSON.stringify(FROM)); } catch (e) { /* (kept for this page only) */ }
    listen(); paintLists();
}
/* the computer's keys while the VIRTUAL MIDI KEYBOARD is picked: anywhere on an instrument tab but in a field */
if (typeof document !== "undefined") {
    document.addEventListener("keydown", e => {
        const t = shownKeys();
        if (FROM.id !== PC || !t || ED.ui.dialogOpen() || isTyping(e) || e.ctrlKey || e.metaKey || e.altKey) return;
        if (e.key === "[" || e.key === "]") { e.preventDefault(); if (!e.repeat) t.octave(e.key === "]" ? 1 : -1); return; }
        const k = e.key.toLowerCase(), n = T.KEYMAP[k];
        if (!n) return;
        e.preventDefault();
        if (e.repeat || pcGoing.has(k)) return;
        const note = n + (t.oct - 4) * 12;
        pcGoing.set(k, note);
        noteOn(note);
    });
    document.addEventListener("keyup", e => {
        const k = e.key.toLowerCase();
        if (!pcGoing.has(k)) return;
        noteOff(pcGoing.get(k));
        pcGoing.delete(k);
    });
}
function keyboard(t, api) {
    t.oct = t.oct || 4;
    const held = t.held = t.held || new Set();       /* (kept over the tab's redraws: a note on goes off after one) */
    const lit = (n, on) => { const k = pianoBox.querySelector(`[data-n="${n}"]`); if (k) k.classList.toggle("on", on); };
    const on = n => {
        if (held.has(n)) return;
        held.add(n); lit(n, true);
        ED.player.key(true, n, api.draft, t.slot, api.stWith()).catch(e => U.status(String(e.message || e).toUpperCase(), { err: true }));
    };
    const off = n => { if (n == null || !held.has(n)) return; held.delete(n); lit(n, false); ED.player.key(false, n); };
    t.keyOn = on; t.keyOff = off;
    const pianoBox = h("div");
    const octText = h("span", { class: "mono" });
    const paint = () => {
        pianoBox.textContent = "";
        pianoBox.append(T.piano(t.oct, on, null, off));
        for (const n of held) lit(n, true);
        octText.textContent = R.noteName(53 + (t.oct - 4) * 12) + "–" + R.noteName(79 + (t.oct - 4) * 12);
    };
    t.octave = d => { for (const n of [...held]) off(n); t.oct = Math.max(1, Math.min(7, t.oct + d)); paint(); };
    paint();
    /* where it is played from (the page's choice, above): no MIDI keyboard, the VIRTUAL MIDI KEYBOARD (the computer's keys
     * anywhere on the tab), or a MIDI keyboard from the computer's inputs */
    const midiSel = h("select", { class: "keysfrom", title: "PLAY IT FROM: THE COMPUTER'S KEYS (VIRTUAL MIDI KEYBOARD) OR A MIDI KEYBOARD", "aria-label": "Play it from" },
        h("option", { value: "" }, "NO MIDI KEYBOARD"),
        h("option", { value: PC }, "VIRTUAL MIDI KEYBOARD: THE PC'S KEYS"));
    const ask = () => {
        if (midi || !navigator.requestMIDIAccess) return;
        askMidi().catch(() => U.status("MIDI WAS NOT ALLOWED: THE VIRTUAL MIDI KEYBOARD STILL PLAYS", { err: true }));
    };
    fillList(midiSel);
    midiSel.addEventListener("pointerdown", ask);
    midiSel.addEventListener("keydown", e => { if (e.key !== "Tab" && e.key !== "Shift") ask(); });
    midiSel.addEventListener("change", () => {
        const v = midiSel.value, p = midi && midi.inputs.get(v);
        pick(v, p ? p.name : v === FROM.id ? FROM.name : "");
        if (v === PC) midiSel.blur();                  /* (the keys are the tab's now, not the list's) */
    });
    const hint = h("p", { class: "hint keyshint" }, hintText());
    const box = h("div", { class: "kbd", tabIndex: 0 },
        h("div", { class: "octrow" }, h("b", { class: "edkind" }, "PLAY IT"), h("span", { class: "grow" }),
            h("button", { class: "btn small", onclick: () => t.octave(-1) }, "◀ OCTAVE"), octText,
            h("button", { class: "btn small", onclick: () => t.octave(1) }, "OCTAVE ▶"), midiSel),
        pianoBox, hint);
    /* (the box's own keys: when the VIRTUAL MIDI KEYBOARD is not on, which takes them everywhere) */
    box.addEventListener("keydown", e => {
        if (FROM.id === PC || isTyping(e)) return;
        const n = T.KEYMAP[e.key.toLowerCase()];
        if (n && !e.repeat && !e.ctrlKey && !e.metaKey) { e.preventDefault(); on(n + (t.oct - 4) * 12); }
    });
    box.addEventListener("keyup", e => { if (FROM.id === PC) return; const n = T.KEYMAP[e.key.toLowerCase()]; if (n) off(n + (t.oct - 4) * 12); });
    /* the tab closed: its notes still on go off */
    t.cleanup = () => { for (const [n, x] of [...going]) if (x === t) noteOff(n); };
    midiIfAllowed();
    return box;
}
})(globalThis.ED = globalThis.ED || {});
