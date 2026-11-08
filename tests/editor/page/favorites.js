/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A FAVORITES cartridge on the page (specs/editor.md: Items, The tabs): + NEW's FAVORITES makes MY FAVORITES and opens
 * its tab, 128 empty slots; a slot's source picked (a cartridge, then an instrument) names it, by its cartridge's and
 * its own name on the TABLE; SWAP DOWN moves it; REMOVE empties it; SAVE keeps it. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`ED.files.makeNew(0x07); 1`);
    await sleep(400);
    const t0 = await evaluate(`(() => { const t = ED.tabs.current(); return t ? { kind: t.draft.kind, name: ED.rec.unpackCart(7, t.draft.recs[0].payload).name,
        rows: t.body.querySelectorAll(".slotrow").length } : null; })()`);
    check(t0 && t0.kind === 7 && t0.name === "MY FAVORITES" && t0.rows === 128, "favorites: + NEW makes MY FAVORITES, its tab of 128 slots: " + JSON.stringify(t0));
    /* pick a source: the first cartridge, its first instrument */
    await evaluate(`(() => { const s = ED.tabs.current().body.querySelectorAll("select"); s[0].value = "0"; s[0].dispatchEvent(new Event("change")); return 1; })()`);
    await sleep(200);
    await evaluate(`(() => { const s = ED.tabs.current().body.querySelectorAll("select")[1]; s.value = s.options[1].value; s.dispatchEvent(new Event("change")); return 1; })()`);
    await sleep(200);
    const one = await evaluate(`(() => { const t = ED.tabs.current(), c = ED.rec.unpackCart(7, t.draft.recs[0].payload), r = t.body.querySelector(".slotrow");
        return { slot: c.slots[0], linked: !!t.draft.link[ED.model.lk("c", c.slots[0] ? c.slots[0].cart : 0)], row: r.textContent }; })()`);
    check(one.slot && one.linked && !/----------/.test(one.row), "favorites: a source picked: the slot names it, linked; listed by its names (" + one.row + ")");
    await shot("01-picked");
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "▼ SWAP DOWN").click(); 1`);
    await sleep(200);
    const sw = await evaluate(`ED.rec.unpackCart(7, ED.tabs.current().draft.recs[0].payload).slots.map(s => !!s)`);
    check(!sw[0] && sw[1], "favorites: SWAP DOWN moves it to slot 01");
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "REMOVE").click(); 1`);
    await sleep(200);
    check(await evaluate(`ED.rec.unpackCart(7, ED.tabs.current().draft.recs[0].payload).slots.filter(Boolean).length`) === 0, "favorites: REMOVE empties it");
    await evaluate(`ED.tabs.current().saveBtn.click(); 1`);
    await sleep(200);
    check(await evaluate(`ED.app.st.L.some(i => i.kind === 7 && i.name === "MY FAVORITES")`), "favorites: SAVE keeps it on the TABLE");
};
