/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A FAVORITES cartridge's tab (specs/editor.md, The tabs: A FAVORITES cartridge; specs/files/instruments.md#favorites):
 * its name and its 128 slots, each naming an instrument of another cartridge: picked (a cartridge, then one of its
 * instruments), removed, moved and swapped; ▶ plays the source through the preview. On the FM-1 panel a favourite reads
 * as the FM-1 lists it (07 0B:05 BRASS); on the TABLE, which has no numbers, by its source's name and its source's
 * cartridge's name; a source that is gone reads MISSING, grey. A slot's source is a reference (the item's link), so
 * moves and renumbering follow it as any cartridge's. */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, h = U.h, H2 = M.H2, SLOTS = 128;

T.kinds.favorites = function (t, api) {
    const side = t.side, d = api.draft, items = api.st[side], byId = M.byIdMap(items);
    const c = R.unpackCart(K.FAVORITES, d.recs[0].payload);
    while (c.slots.length < SLOTS) c.slots.push(null);
    t.slot = t.slot != null ? t.slot : 0;
    /* a slot's source: the cartridge item its number names (the link), its instrument's name, or MISSING */
    const cartOf = num => {
        const id = d.link[M.lk("c", num)];
        if (id === M.BUILTIN || (id == null && side === "R")) return items.find(i => i.num === num && M.isCart(i.kind) && i.kind !== K.FAVORITES) ||
            (M.builtinAt("c", num) ? { builtinNum: num, kind: M.builtinAt("c", num).kind, recs: [M.builtinAt("c", num)] } : null);
        return id ? byId.get(id) || null : null;
    };
    const instName = (ci, slot) => {
        try {
            if (!ci || ci.kind === K.FAVORITES) return null;
            if (ci.kind === K.SAMPLER) { const s = R.unpackSampler(ci.recs[0].payload).instruments[slot]; return s ? s.name : null; }
            const s = R.unpackCart(ci.kind, ci.recs[0].payload).slots[slot]; return s ? s.name : null;
        } catch (e) { return null; }
    };
    const cartName = ci => { try { return ci.kind === K.SAMPLER ? R.unpackSampler(ci.recs[0].payload).name : R.unpackCart(ci.kind, ci.recs[0].payload).name; } catch (e) { return "?"; } };
    const write = (fn, what, link) => {
        const c2 = R.unpackCart(K.FAVORITES, api.draft.recs[0].payload);
        while (c2.slots.length < SLOTS) c2.slots.push(null);
        fn(c2.slots);
        const payload = R.packCart(K.FAVORITES, { name: c2.name, slots: c2.slots });
        api.set(Object.assign({}, api.draft, { recs: [{ sub: 0, ver: api.draft.recs[0].ver, payload }], link: link || api.draft.link }), what);
        api.refresh();
    };
    /* the cartridges a source can come from: every cartridge with an instrument but FAVORITES (the built-in ones on the
     * FM-1 panel too), in the lists' order */
    const sources = items.filter(i => M.isCart(i.kind) && i.kind !== K.FAVORITES && !i.dim && !i.owner).map(i => ({ item: i, num: i.num }))
        .concat(side === "R" ? M.builtins.filter(b => R.isCart(b.kind) && b.kind !== K.FAVORITES && !items.some(i => i.num === b.num && M.isCart(i.kind)))
            .map(b => ({ item: { kind: b.kind, recs: [b], builtinNum: b.num }, num: b.num })) : [])
        .sort((a, b) => (a.num == null ? 0 : a.num) - (b.num == null ? 0 : b.num));
    /* the number a source's cartridge takes in this item's own numbering: on the FM-1 panel its own; on the TABLE a
     * number of the item's link (M.numberFor) */
    function pick(slot, src, inst) {
        const link = Object.assign({}, api.draft.link);
        let num;
        if (side === "R") num = src.num;
        else {
            num = M.numberFor(api.draft, "c", src.item.id, side, null, null);
            link[M.lk("c", num)] = src.item.id;
        }
        write(s => { s[slot] = { cart: num, slot: inst }; }, "FAVORITE " + H2(slot) + ": " + (instName(src.item, inst) || ""), link);
    }

    /* ---- the list ---- */
    const label = (fv) => {
        if (!fv) return ["----------", U.COL.dgrey, ""];
        const ci = cartOf(fv.cart), nm = instName(ci, fv.slot);
        if (!nm) return ["MISSING", U.COL.dgrey, side === "R" ? H2(fv.cart) + ":" + H2(fv.slot) : ""];
        return [nm, U.ENG_COL[ci.kind] || U.COL.lgrey, side === "R" ? H2(fv.cart) + ":" + H2(fv.slot) : cartName(ci)];
    };
    const rows = h("div", { class: "slots" }, h("h5", null, "FAVORITES · " + c.slots.filter(Boolean).length + "/128"));
    c.slots.forEach((fv, i) => {
        const [nm, col, where] = label(fv);
        rows.append(h("div", { class: "slotrow" + (i === t.slot ? " on" : "") + (fv ? "" : " empty"), onclick: () => { t.slot = i; api.refresh(); } },
            h("span", { class: "mono" }, H2(i)), h("span", { style: "color:" + col }, (where ? where + " " : "") + nm), h("span")));
    });

    /* ---- the slot picked: its source ---- */
    const fv = c.slots[t.slot], [nm, , where] = label(fv);
    const cartSel = h("select", { onchange: e => { t.srcCart = +e.target.value; api.refresh(); } },
        h("option", { value: -1 }, "A CARTRIDGE…"),
        sources.map((s, k) => h("option", { value: k, selected: t.srcCart === k }, (s.num != null ? H2(s.num) + " " : "") + cartName(s.item) + " · " + (M.ENGINE[s.item.kind] || ""))));
    const src = t.srcCart != null && t.srcCart >= 0 ? sources[t.srcCart] : null;
    const instSel = h("select", { disabled: !src, onchange: e => { if (src && +e.target.value >= 0) pick(t.slot, src, +e.target.value); } },
        h("option", { value: -1 }, "AN INSTRUMENT…"),
        src ? (src.item.kind === K.SAMPLER ? R.unpackSampler(src.item.recs[0].payload).instruments : R.unpackCart(src.item.kind, src.item.recs[0].payload).slots)
            .map((s, k) => s ? h("option", { value: k }, H2(k) + " " + s.name) : null).filter(Boolean) : []);
    const btn = (lab, fn, on) => h("button", { class: "btn small", onclick: fn, disabled: !on }, lab);
    const swapWith = dir => {
        const i = t.slot, j = i + dir;
        if (j < 0 || j >= SLOTS) return;
        t.slot = j;                                     /* (the cursor goes with it) */
        write(s => { const x = s[i]; s[i] = s[j]; s[j] = x; }, "SWAP " + H2(i) + " " + H2(j));
    };
    const card = T.card("FAVORITE " + H2(t.slot),
        h("div", { class: "hint" }, fv ? "SOURCE " + (where ? where + " " : "") + nm : "AN EMPTY SLOT: PICK AN INSTRUMENT"),
        T.field("CARTRIDGE", cartSel), T.field("INSTRUMENT", instSel),
        h("div", { class: "rowbtns", style: "margin-top:8px" },
            btn("▶ PLAY IT", () => { const ci = fv && cartOf(fv.cart); if (ci && !ci.builtinNum && ci.id) ED.player.toggle(ci, { st: api.stWith(), slot: fv.slot }); }, !!(fv && nm !== "MISSING")),
            btn("REMOVE", () => write(s => { s[t.slot] = null; }, "REMOVE " + H2(t.slot)), !!fv),
            btn("▲ SWAP UP", () => swapWith(-1), t.slot > 0),
            btn("▼ SWAP DOWN", () => swapWith(1), t.slot < SLOTS - 1)));
    const name = T.card("THE CARTRIDGE", T.text("NAME", () => c.name || "", v => {
        const c2 = R.unpackCart(K.FAVORITES, api.draft.recs[0].payload);
        const payload = R.packCart(K.FAVORITES, { name: v, slots: c2.slots });
        api.set(Object.assign({}, api.draft, { recs: [{ sub: 0, ver: api.draft.recs[0].ver, payload }], name: v }), "NAME " + v); api.refresh();
    }, 12));
    return h("div", { class: "cartwrap" }, rows, h("div", { class: "cards" }, name, card));
};
})(globalThis.ED = globalThis.ED || {});
