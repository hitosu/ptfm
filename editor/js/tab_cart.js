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
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, D = ED.defs, S = ED.slotops, h = U.h, H2 = M.H2;
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
    /* ---- many at once (specs/editor.md, The tabs: Many instruments at once): every slot to the cartridge's size;
     * the selection t.sel, the slot shown t.slot (the last picked); COPY, CUT, PASTE, REPLACE, DELETE; rearranging,
     * the references following the places at SAVE (the draft's map of places, d.moved) ---- */
    const n = SLOT_MAX[kind];
    if (!t.sel || ![...t.sel].every(i => i < n)) t.sel = new Set([t.slot]);
    const ops = slotOps(t, api, kind, n);
    for (let i = 0; i < n; i++) {
        const s = list[i];
        const row = h("div", { class: "slotrow" + (t.sel.has(i) ? " on" : "") + (s ? "" : " empty") + (t.floating ? " float" : ""), draggable: !!s,
            onclick: e => ops.click(i, e) },
            h("span", { class: "mono" }, H2(i)), s ? s.name : "EMPTY",
            s && ED.player.canPlay(api.draft) ? h("button", { class: "act", title: "PLAY ITS ARPEGGIO", onclick: e => { e.stopPropagation(); ED.player.play(api.draft, { st: api.stWith(), slot: i }); } }, "▶") : h("span"));
        row.addEventListener("dragstart", e => { t.dragFrom = t.sel.has(i) ? [...t.sel].sort((a, b) => a - b) : [i]; e.dataTransfer.setData("text/plain", String(i)); });
        row.addEventListener("dragover", e => e.preventDefault());
        row.addEventListener("drop", e => { e.preventDefault(); if (t.dragFrom) ops.moveTo(t.dragFrom, i); t.dragFrom = null; });
        slots.append(row);
    }
    slots.tabIndex = 0;
    slots.addEventListener("keydown", ops.key);
    const top = T.card("THE CARTRIDGE", h("div", { class: "namerow" }, T.text("NAME", () => c.name, v => set(x => { x.name = v; }, "NAME " + v), 12),
        h("button", { class: "btn small", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶ PLAY EVERY INSTRUMENT")),
        ops.bar());

    /* ---- an instrument's page ---- */
    const s = list[t.slot];
    const page = h("div", { class: "slotpage" });
    if (!s) {
        const have = kind === K.SAMPLER ? S.samplesOn(api.st[side], api.draft.pending).length : 0;
        page.append(T.card("INSTRUMENT " + H2(t.slot), h("p", null, "AN EMPTY PLACE."),
            kind === K.FUSION ? null : kind === K.SAMPLER
                /* (an instrument needs a sample: CREATE picks one of any on the panel, a new instrument playing it whole) */
                ? h("button", { class: "btn primary", disabled: !have, title: have ? "PICK A SAMPLE: A NEW INSTRUMENT PLAYS IT WHOLE" : "NO SAMPLES ON THIS PANEL", onclick: () => pickSample(t, api, "CREATE", null) }, "CREATE")
                : h("button", { class: "btn primary", onclick: () => set(x => { const l = x[slotsKey(x)]; while (l.length < t.slot) l.push(null); l[t.slot] = D.newSlot(kind, "NEW " + (M.ENGINE[kind] || "")); }, "INSTRUMENT " + H2(t.slot) + " MADE") }, "NEW INSTRUMENT HERE")));
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
        /* the sample the instrument names, as the draft's link has it (none: the number names nothing) */
        const items = api.st[t.side], named = S.sampleNamed(api.draft, items, s.sample);
        const e = named ? M.sampleEntry(named, items) : null;
        const max = e ? e.length : 1 << 20;
        const pts = [["start", "START"], ["end", "END"], ["loop_start", "LOOP START"], ["loop_end", "LOOP END"]].map(([f, n]) =>
            T.slider(n, { min: 0, max, def: f === "end" || f === "loop_end" ? max : 0, get: () => Math.min(max, s[f]), fmt: v => e && e.rate ? (v / e.rate).toFixed(3) + " S" : String(v), acc: col, set: v => upd(f, v, n),
                live: v => live(x => mutSlot(x, f, v)) }));
        /* SAMPLE: any sample on the panel (the other values stay); REPLACE SAMPLE: the same pick, every value back to a new
         * instrument's */
        const all = S.samplesOn(items, api.draft.pending);
        const smp = h("select", { "aria-label": "Sample", onchange: ev => {
            const pick = all.find(x => x.item.id === ev.target.value);
            if (!pick) return;
            const q = decode(api.draft).instruments[t.slot];
            api.set(S.pointTo(api.draft, t.side, t.slot, q, pick.item), "SAMPLE " + sampleText(pick.item, items));
            api.refresh();
        } }, named ? null : h("option", { value: "", selected: true }, "THE SAMPLE IS MISSING: " + H2(s.sample)),
            all.map(x => h("option", { value: x.item.id, selected: x.item.id === (named && named.id) }, sampleText(x.item, items))));
        const replaceBtn = h("button", { class: "btn small", disabled: !all.length, title: "PICK A SAMPLE: THE INSTRUMENT PLAYS IT AS A NEW ONE WOULD", onclick: () => pickSample(t, api, "REPLACE SAMPLE", t.slot) }, "REPLACE SAMPLE");
        const mutEnv = (x, f, val) => { const q = x.instruments[t.slot]; x.instruments[t.slot] = Object.assign({}, q, { env: Object.assign({}, q.env, { [f]: val }) }); };
        return [T.picSection("THE SAMPLE INSTRUMENT", [pic("EDIT", W.EDIT).el, details()], T.field("SAMPLE", smp), h("div", { class: "rowbtns", style: "margin-bottom:6px" }, replaceBtn), ...pts, ...sl(mutSlot, D.SAMPLER_INST),
            h("h5", null, "THE ENVELOPE"), ...sl(mutEnv, D.ENV, s.env, "", nd.env))];
    }
    return [];
}
/* ---- many instruments at once (specs/editor.md, The tabs: Many instruments at once) ---- */
/* the page's clipboard of contents, while it is open: what the instruments are, not where they were, with what they
 * need (a sample instrument's sample, a FUSION slot's pattern) */
let clip = null;
function slotOps(t, api, kind, n) {
    const side = t.side, range = (a, b) => Array.from({ length: b - a + 1 }, (_, k) => a + k);
    const items = () => api.st[side], cartItem = () => items().find(i => i.id === t.itemId) || t.base;
    const cur = () => decode(api.draft), listOf = c => { const l = c[slotsKey(c)]; while (l.length < n) l.push(null); return l; };
    const sorted = () => [...t.sel].sort((a, b) => a - b);
    const places = () => api.draft.moved || range(0, n - 1);   /* P[o]: where what the references name o is now */
    const origOf = p => { const o = places().indexOf(p); return o < 0 ? p : o; };
    const usesAt = ps => ED.slotops.uses(items(), side, cartItem(), new Set(ps.map(origOf)));
    const say = (m, err) => U.status(m, err ? { err: true } : { ok: true });
    const put = (c2, what, more) => { api.set(Object.assign(encode(api.draft, c2), more || {}), what); api.refresh(); };
    const pruneTail = c2 => { const l = c2[slotsKey(c2)]; while (l.length && !l[l.length - 1]) l.pop(); return c2; };
    /* the sample a sampler's instrument plays, as its item on this panel */
    const sampleItem = (s) => kind === K.SAMPLER && s ? S.sampleNamed(api.draft, items(), s.sample) : null;
    function copy(cut) {
        const c = cur(), l = listOf(c), picked = sorted().filter(i => l[i]);
        if (!picked.length) { say("NOTHING TO COPY: PICK AN INSTRUMENT", true); return; }
        clip = { kind, side, from: t.itemId, slots: picked.map(i => Object.assign({}, l[i])),
            samples: picked.map(i => sampleItem(l[i])),
            patterns: picked.map(i => kind === K.FUSION ? api.draft.link[M.lk("p", l[i].pattern)] || null : null),
            patNums: picked.map(i => kind === K.FUSION ? l[i].pattern : null) };
        if (cut) del(true); else say("COPIED " + picked.length);
    }
    /* PASTE adds into empty slots (those no reference names first); REPLACE puts them from the first slot picked on */
    function paste(replace) {
        if (!clip || !clip.slots.length) { say("NOTHING COPIED", true); return; }
        if (clip.kind !== kind) { say("COPIED FROM " + (M.ENGINE[clip.kind] || "?") + ": NOT HERE", true); return; }
        const c2 = cur(), l = listOf(c2), k = clip.slots.length;
        let targets;
        if (replace) {
            const from = sorted()[0];
            if (from + k > n) { say(k + " COPIED, " + (n - from) + " SLOTS FROM HERE: NOTHING REPLACED", true); return; }
            targets = range(from, from + k - 1);
        } else {
            const empty = range(0, n - 1).filter(i => !l[i]), free = empty.filter(i => usesAt([i]) === 0), named = empty.filter(i => usesAt([i]) > 0);
            if (empty.length < k) { say(k + " COPIED, " + empty.length + " EMPTY SLOTS: NOTHING PASTED", true); return; }
            targets = free.concat(named).slice(0, k).sort((a, b) => a - b);
        }
        /* what comes along: a sample (the same sound already there, else at the first free sample, added at SAVE); a
         * FUSION slot's pattern (on this panel) */
        const pending = (api.draft.pending || []).slice(), link = Object.assign({}, api.draft.link);
        /* (the samples: the same one already on this panel (its whole record equal) is named; else it comes along as an item
         * of its own, on the FM-1 panel at a free number, passing over the numbers references name; none free, or no room
         * for them there: nothing pasted) */
        let bring = null;                           /* clip sample id -> the item the pasted instruments name */
        if (kind === K.SAMPLER) {
            const r = S.bringSamples(items(), api.draft, side, clip.samples, ED.app.st.fm1 && ED.app.st.fm1.layout);
            if (!r.ok) { say(r.msg, true); return; }
            bring = r.bring;
            for (const it of r.fresh) pending.push({ from: it.id, item: it });
        }
        for (let j = 0; j < k; j++) {
            const s = Object.assign({}, clip.slots[j]);
            if (kind === K.SAMPLER) {
                const src = clip.samples[j], got = src && bring.get(src.id);
                if (got) {
                    const num = side === "R" ? got.num : M.numberFor(Object.assign({}, api.draft, { link }), "m", got.id, "L", null, got.orig);
                    s.sample = num; link[M.lk("m", num)] = got.id;
                } else s.sample = S.numberForMissing(items(), Object.assign({}, api.draft, { pending, link }), side, s.sample);   /* (its sample is gone: it names nothing, as before) */
            } else if (kind === K.FUSION && clip.patterns[j] != null && clip.patNums[j] !== 0xFF) {
                const pid = clip.patterns[j], p = items().find(i => i.id === pid);
                if (!p) { say("ITS PATTERN IS NOT ON THIS PANEL: MOVE THE FUSION CARTRIDGE OVER FIRST", true); return; }
                const num = side === "R" ? p.num : M.numberFor(Object.assign({}, api.draft, { link }), "p", pid, side, null, null);
                link[M.lk("p", num)] = pid;
                s.pattern = num;
            }
            l[targets[j]] = s;
        }
        const usedOver = replace ? targets.filter(i => listOf(cur())[i]) : [], count = usesAt(usedOver), grey = replace ? 0 : usesAt(targets);
        const go = () => {
            put(pruneTail(c2), (replace ? "REPLACED " : "PASTED ") + k, { link, pending });
            t.sel = new Set(targets); t.slot = targets[0];
            say((replace ? "REPLACED " : "PASTED ") + k + (grey ? " · " + grey + " GREY REFERENCES PLAY AGAIN" : ""));
        };
        if (count) U.dialog({ title: "REPLACE " + usedOver.length + " USED INSTRUMENT" + (usedOver.length > 1 ? "S" : "") + "? USED " + count + " TIMES",
            body: h("p", null, "WHAT PLAYS THEM PLAYS THE NEW ONES."), buttons: [{ label: "REPLACE", cls: "primary danger", onClick: go }, { label: "KEEP" }] });
        else go();
    }
    /* DELETE empties the slots picked; what named them keeps naming those places (grey) */
    function del(quiet) {
        const c2 = cur(), l = listOf(c2), picked = sorted().filter(i => l[i]);
        if (!picked.length) return;
        const count = usesAt(picked);
        const go = () => { for (const i of picked) l[i] = null; put(pruneTail(c2), "DELETED " + picked.length); say((quiet ? "CUT " : "DELETED ") + picked.length); };
        if (count) U.dialog({ title: picked.length + " INSTRUMENT" + (picked.length > 1 ? "S ARE" : " IS") + " USED " + count + " TIMES",
            body: h("p", null, "DELETE ANYWAY? WHAT PLAYS THEM KEEPS NAMING THOSE PLACES, GREY AND SILENT."),
            buttons: [{ label: "DELETE", cls: "primary danger", onClick: go }, { label: "KEEP" }] });
        else go();
    }
    /* places swapped: the instruments and the map of places (a move onto an empty slot is a swap with it) */
    function swapIn(c2, P, a, b) {
        const l = listOf(c2), x = l[a]; l[a] = l[b]; l[b] = x;
        const oa = P.indexOf(a), ob = P.indexOf(b);
        if (oa >= 0) P[oa] = b;
        if (ob >= 0) P[ob] = a;
    }
    function moveTo(from, to) {
        const c2 = cur(), l = listOf(c2), P = places().slice(), delta = to - from[0], set = new Set(from);
        if (!delta) return;
        if (from.length === 1) swapIn(c2, P, from[0], to);
        else {
            const tg = from.map(f => f + delta);
            if (tg.some(x => x < 0 || x >= n || (l[x] && !set.has(x)))) { say("NOTHING MOVED: THE SLOTS THERE ARE NOT EMPTY", true); return; }
            for (const f of delta > 0 ? from.slice().reverse() : from) swapIn(c2, P, f, f + delta);
        }
        t.sel = new Set(from.map(f => f + delta)); t.slot = to;
        put(pruneTail(c2), "MOVED TO " + H2(to), { moved: P });
    }
    function step(dir) {
        const c2 = cur(), P = places().slice(), sel = sorted();
        if ((dir < 0 && sel[0] === 0) || (dir > 0 && sel[sel.length - 1] === n - 1)) return;
        for (const i of dir < 0 ? sel : sel.slice().reverse()) swapIn(c2, P, i, i + dir);
        t.sel = new Set(sel.map(i => i + dir)); t.slot += dir;
        put(pruneTail(c2), dir < 0 ? "MOVED UP" : "MOVED DOWN", { moved: P });
    }
    function click(i, e) {
        if (t.floating) { t.floating = false; moveTo(sorted(), i); return; }
        if (e.shiftKey) t.sel = new Set(range(Math.min(t.slot, i), Math.max(t.slot, i)));
        else if (e.ctrlKey || e.metaKey || e.pointerType === "touch") { if (t.sel.has(i) && t.sel.size > 1) t.sel.delete(i); else t.sel.add(i); }
        else t.sel = new Set([i]);
        t.slot = i;
        api.refresh();
    }
    function key(e) {
        const ctl = e.ctrlKey || e.metaKey, k = e.key.toLowerCase();
        let done = true;
        if (e.altKey && (k === "arrowup" || k === "arrowdown")) step(k === "arrowup" ? -1 : 1);
        else if (k === "arrowup" || k === "arrowdown") {
            const i = Math.max(0, Math.min(n - 1, t.slot + (k === "arrowup" ? -1 : 1)));
            if (e.shiftKey) t.sel.add(i); else t.sel = new Set([i]);
            t.slot = i; api.refresh();
        } else if (ctl && k === "a") { const l = listOf(cur()); t.sel = new Set(range(0, n - 1).filter(i => l[i])); if (!t.sel.size) t.sel.add(t.slot); api.refresh(); }
        else if (k === "escape") { t.sel = new Set([t.slot]); t.floating = false; api.refresh(); }
        else if (ctl && k === "c") copy(false);
        else if (ctl && k === "x") copy(true);
        else if (ctl && k === "v") paste(e.shiftKey);
        else if (k === "delete" || k === "backspace") del(false);
        else done = false;
        if (done) { e.preventDefault(); e.stopPropagation(); }
    }
    /* SAVE: the references follow the places (cells, the loops, the favourites, the settings' current instrument), the
     * samples that came along added; refused while a tab with changes is open on an item it would rewrite */
    t.onSave = st => {
        let list = st[side];
        const what = [], P = api.draft.moved;
        if (P && P.some((p, o) => p !== o)) {
            const cart = list.find(i => i.id === t.itemId) || cartItem();
            const r = ED.slotops.remap(list, side, cart, new Map(P.map((p, o) => [o, p]).filter(([o, p]) => o !== p)));
            const busy = r.ids.map(id => ED.tabs.find(side, id)).find(x => x && x !== t && x.dirty);
            if (busy) return { refuse: "SAVE OR CLOSE " + ED.files.label(busy.base, list) + "'S TAB FIRST" };
            list = r.items;
            const kinds = r.ids.map(id => list.find(i => i.id === id).kind);
            const np = kinds.filter(x => x === K.PATTERN).length, nf = kinds.filter(x => x === K.FAVORITES).length;
            const parts = [np ? np + " PATTERN" + (np > 1 ? "S" : "") : "", kinds.includes(K.LOOPS) ? "THE LOOPS" : "", nf ? nf + " FAVORITES CARTRIDGE" + (nf > 1 ? "S" : "") : "",
                kinds.includes(K.SETTINGS) ? "THE CURRENT INSTRUMENT" : ""].filter(Boolean);
            if (parts.length) what.push(parts.join(", ") + " FOLLOW");
            const dim = list.filter(i => i.dim && [K.PATTERN, K.SONG, K.LOOPS, K.FAVORITES].includes(i.kind)).length;
            if (dim) what.push(dim + " RECORD" + (dim > 1 ? "S" : "") + " THE PAGE CANNOT READ DO" + (dim > 1 ? "" : "ES") + " NOT FOLLOW");
        }
        /* the samples a paste brought, still named by the saved sampler: items of their own now (the sampler's link already
         * names them by id); on the FM-1 panel at the number the paste gave them, which must still be free */
        const saved = list.find(i => i.id === t.itemId), named = new Set(saved ? Object.values(saved.link) : []);
        const bringing = (api.draft.pending || []).map(p => p.item).filter(it => named.has(it.id));
        if (side === "R") {
            const tk = M.taken(list);
            const clash = bringing.find(it => tk.m.has(it.num));
            if (clash) return { refuse: "SAMPLE " + H2(clash.num) + " IS TAKEN ON THE FM-1 PANEL NOW: PASTE THE INSTRUMENTS AGAIN" };
            /* (nor may another sampler name that number as nothing now: it would play the new sample; and the room) */
            const nothing = M.namedNothing(list.filter(i => i.id !== t.itemId));
            const ghost = bringing.find(it => nothing.has(it.num));
            if (ghost) return { refuse: "SAMPLE " + H2(ghost.num) + " IS NAMED BY AN INSTRUMENT WITH NO SAMPLE NOW: PASTE THE INSTRUMENTS AGAIN" };
            if (bringing.length) {
                const sp = M.space(list.concat(bringing), ED.app.st.fm1 && ED.app.st.fm1.layout);
                if (!sp.fits) return { refuse: "DOESN'T FIT: THE SAMPLES NEED " + sp.missing * 4 + " KB MORE ON THE FM-1. NOTHING SAVED." };
            }
        }
        if (bringing.length) list = list.concat(bringing);
        if (bringing.length) what.push(bringing.length + " SAMPLE" + (bringing.length > 1 ? "S" : "") + " ADDED");
        return { st: Object.assign({}, st, { [side]: list }), what: what.join(" · ") };
    };
    const btn = (label, fn, title) => h("button", { class: "btn small", title, onclick: fn }, label);
    return { click, key, moveTo, bar: () => h("div", { class: "rowbtns", style: "margin-top:8px" },
        h("span", { class: "hint" }, t.sel.size + " PICKED"),
        btn("SELECT ALL", () => key({ key: "a", ctrlKey: true, preventDefault() {}, stopPropagation() {} }), "CTRL+A"),
        btn("SELECT NONE", () => key({ key: "Escape", preventDefault() {}, stopPropagation() {} }), "ESC"),
        btn("COPY", () => copy(false), "CTRL+C"), btn("CUT", () => copy(true), "CTRL+X"),
        btn("PASTE", () => paste(false), "CTRL+V: INTO EMPTY SLOTS"), btn("REPLACE", () => paste(true), "CTRL+SHIFT+V: FROM THE FIRST SLOT PICKED ON"),
        btn("DELETE", () => del(false), "DELETE"),
        btn(t.floating ? "MOVE TO: TAP A SLOT" : "MOVE TO", () => { t.floating = !t.floating; api.refresh(); }, "THE SELECTION FLOATS: A TAP ON A SLOT PUTS IT THERE"),
        btn("↑", () => step(-1), "ALT+↑"), btn("↓", () => step(1), "ALT+↓")) };
}

/* a sample in a list: its number and name on the FM-1 panel (23 KICK), its name on the TABLE */
function sampleText(it, items) {
    const e = M.sampleEntry(it, items);
    return (it.num != null ? H2(it.num) + " " : "") + (e ? e.name : "?");
}
/* CREATE (at null: the tab's empty slot) and REPLACE SAMPLE (at: the instrument's slot): a pick of any sample on the
 * panel; a new instrument there plays it whole, named after it, REPLACE SAMPLE's every value a new one's, the name
 * following when it was the old sample's */
function pickSample(t, api, what, at) {
    const items = api.st[t.side], all = S.samplesOn(items, api.draft.pending);
    if (!all.length) { U.status("NO SAMPLES ON THIS PANEL: DROP OR MOVE SOME ON FIRST", { err: true }); return; }
    const d = api.draft, c = R.unpackSampler(d.recs[0].payload), slot = at == null ? t.slot : at;
    const cur = at != null && c.instruments[slot] ? S.sampleNamed(d, items, c.instruments[slot].sample) : null;
    const sel = h("select", { "aria-label": "Sample" }, all.map(x => h("option", { value: x.item.id, selected: cur ? x.item.id === cur.id : false }, sampleText(x.item, items))));
    const go = () => {
        const pick = all.find(x => x.item.id === sel.value);
        if (!pick) return;
        const next = at == null ? S.pointTo(api.draft, t.side, slot, S.newInstrument(pick.item, 0), pick.item) : S.replaceSample(api.draft, t.side, slot, pick.item, items);
        api.set(next, what + " " + H2(slot) + ": " + sampleText(pick.item, items));
        t.sel = new Set([slot]); t.slot = slot;
        api.refresh();
    };
    U.dialog({ title: what + ": PICK A SAMPLE", body: h("div", null, sel,
            h("p", { class: "hint" }, at == null ? "A NEW INSTRUMENT PLAYS IT WHOLE, NAMED AFTER IT." : "THE INSTRUMENT PLAYS IT AS A NEW ONE WOULD: EVERY VALUE BACK TO A NEW INSTRUMENT'S.")),
        buttons: [{ label: what, cls: "primary", onClick: go }, { label: "CANCEL" }] });
}
/* THE SAMPLES IT USES: each sample the sampler's instruments name, with NEW INSTRUMENT FOR IT at the first empty slot */
function samplesCard(t, api, c, set) {
    const items = api.st[t.side], used = S.usedSamples(api.draft, items);
    const rows = used.map(u => {
        const e = u.item && M.sampleEntry(u.item, items);
        if (!e) return h("div", { class: "orow" }, h("span", { class: "mono", style: "width:28px" }, H2(u.num)), h("span", { class: "grow bad" }, "THE SAMPLE IS MISSING"),
            h("span", { class: "dim mono" }, u.slots.length + (u.slots.length > 1 ? " INSTRUMENTS" : " INSTRUMENT")));
        return h("div", { class: "orow" }, h("span", { class: "mono", style: "width:28px" }, u.item.num != null ? H2(u.item.num) : "--"), h("span", { class: "grow" }, e.name),
            h("span", { class: "dim mono" }, (e.rate / 1000).toFixed(1) + " KHZ · " + (e.rate ? (e.length / e.rate).toFixed(2) : "?") + " S"),
            h("button", { class: "btn small", onclick: () => {
                const k = c.instruments.findIndex(z => !z);
                const at = k < 0 ? c.instruments.length : k;
                if (at >= 64) { U.status("THE SAMPLER IS FULL: 64 INSTRUMENTS", { err: true }); return; }
                api.set(S.pointTo(api.draft, t.side, at, S.newInstrument(u.item, 0), u.item), "NEW INSTRUMENT FOR " + e.name);
                t.sel = new Set([at]); t.slot = at;
                api.refresh();
            } }, "NEW INSTRUMENT FOR IT"));
    });
    return T.card("THE SAMPLES IT USES", rows.length ? rows : h("p", { class: "hint" }, "NO SAMPLES YET: CREATE AN INSTRUMENT ON AN EMPTY SLOT, OR MOVE A SAMPLER OVER IN FILES."));
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
