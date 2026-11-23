/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A FAVORITES cartridge on the page (specs/editor.md: Items, The tabs, Moving items): + NEW's FAVORITES makes MY
 * FAVORITES and opens its tab, 128 empty slots; a slot's source picked (a cartridge, then an instrument) names it, by its
 * cartridge's and its own name on the TABLE; SWAP DOWN moves it; REMOVE empties it; SAVE keeps it. The FM-1 holds one
 * FAVORITES, FB: an item moved onto the FM-1 panel replaces FB's slots (nothing asked while FB is empty; then REPLACE
 * or CANCEL), three moved at once put the bottom-most on (asked), DELETE of FB gives the firmware's own back, FB moved
 * to the TABLE is an item of its own, a disk's UPLOAD puts FB and an earlier PT/FM's FAVORITES back as they are. */
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

    /* ---- the FM-1 holds one FAVORITES, FB (specs/editor.md, Moving items): a FAVORITES item moved onto the FM-1 panel
     * replaces FB's slots (asked first once FB holds favourites); of several, the bottom-most on the TABLE; a disk's
     * UPLOAD puts FB and an earlier PT/FM's FAVORITES back as they are; FB moved to the TABLE an item of its own ---- */
    api.status = () => evaluate("(document.querySelector('#status .smsg') || {}).textContent || ''");
    await evaluate(`ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.show('files'); 1`);
    await evaluate(`(() => {
        const M = ED.model, R = ED.rec, L = ED.app.st.L, smp = L.filter(i => i.kind === 0x04);
        const mk = (name, k) => ({ id: M.newId(), side: "L", kind: 7, num: null, orig: null, src: "TEST", name, link: { [M.lk("c", 0x10 + k)]: smp[k].id },
            recs: [{ sub: 0, ver: 1, payload: R.packCart(7, { name, slots: [{ cart: 0x10 + k, slot: 0 }] }) }] });
        window.favs = [mk("FIRST", 0), mk("SECOND", 1), mk("THIRD", 2)];
        ED.app.commit(Object.assign({}, ED.app.st, { L: L.concat(window.favs) }), "THREE FAVORITES");
        return 1; })()`);
    await sleep(200);
    const fbOn = `(() => { const f = ED.app.st.R.filter(i => i.kind === 7); return f.length === 1 && f[0].num === 0xFB && f[0].builtin ?
        ED.rec.unpackCart(7, f[0].recs[0].payload).name + " " + ED.rec.unpackCart(7, f[0].recs[0].payload).slots.filter(Boolean).length + " " +
        (ED.app.st.R.find(i => i.id === f[0].link[ED.model.lk("c", ED.rec.unpackCart(7, f[0].recs[0].payload).slots[0].cart)]) || {}).num : "NO FB: " + f.length; })()`;
    const moveL = ids => evaluate(`(() => { const ids = ${ids}; ED.files.selectIds("L", ids); ED.files.transfer("L", ids); return 1; })()`);
    /* onto FB as built in (empty): no question */
    await moveL("[window.favs[0].id]");
    const d0 = await api.dialog(), s1 = await api.status(), f1 = await evaluate(fbOn);
    check(!d0 && /^→ FM-1: FIRST · .*FB FAVORITES IS NOW FIRST$/.test(s1) && /^FAVORITES 1 \d+$/.test(f1) &&
          await evaluate(`!ED.files.viewItems("R").some(i => i.fw && i.kind === 7)`),
          "favorites: FIRST onto the FM-1 panel, FB empty: FB holds its favourite (its cartridge along), nothing asked, FB listed once, CHANGED: " + s1 + " / " + f1);
    await shot("02-fb-first");
    /* onto FB holding favourites: the question; CANCEL keeps FB as it was, REPLACE puts SECOND's on */
    await moveL("[window.favs[1].id]");
    const q = await api.dialog();
    check(/REPLACE ON THE FM-1 PANEL\?/.test(q) && /FB FAVORITES HOLDS 1 FAVORITE: IT GIVES WAY TO 1 FAVORITE FROM SECOND/.test(q),
          "favorites: SECOND onto FB holding FIRST's: asked first: " + q);
    await shot("03-replace-fb");
    await api.button("CANCEL");
    check(await evaluate(fbOn) === f1, "favorites: CANCEL: FB as it was");
    await moveL("[window.favs[1].id]");
    await api.button("REPLACE");
    const s2 = await api.status(), f2 = await evaluate(fbOn);
    check(/FB FAVORITES IS NOW SECOND$/.test(s2) && /^FAVORITES 1 \d+$/.test(f2) && f2 !== f1,
          "favorites: REPLACE: FB holds SECOND's favourite (" + f2 + "): " + s2);
    /* several at once: the bottom-most on the TABLE kept, the others not put on */
    const bottom = await evaluate(`(() => { const rows = [...document.querySelectorAll(".panel[data-side=L] [data-id]")].map(r => r.getAttribute("data-id"));
        const ids = window.favs.map(f => f.id).filter(id => rows.includes(id)).sort((a, b) => rows.indexOf(a) - rows.indexOf(b));
        return ED.model.nameOf(window.favs.find(f => f.id === ids[ids.length - 1]), []); })()`);
    await moveL("[window.favs[2].id, window.favs[0].id, window.favs[1].id]");
    const q3 = await api.dialog();
    await api.button("REPLACE");
    const s3 = await api.status();
    check(new RegExp("FB FAVORITES HOLDS 1 FAVORITE: IT GIVES WAY TO 1 FAVORITE FROM " + bottom + "; 2 OTHER FAVORITES NOT PUT ON").test(q3),
          "favorites: three moved at once onto FB holding SECOND's: asked first: " + q3);
    check(bottom === "THIRD" && new RegExp("FB FAVORITES IS NOW " + bottom + " · 2 OTHER FAVORITES NOT PUT ON$").test(s3) &&
          await evaluate(`ED.app.st.R.filter(i => i.kind === 7).length`) === 1,
          "favorites: three moved at once: the bottom-most (" + bottom + ") on FB, the other two not put on: " + s3);
    /* DELETE of FB on the FM-1 panel: BACK TO BUILT-IN, FB empty again (the firmware's own) */
    await evaluate(`(() => { const fb = ED.app.st.R.find(i => i.kind === 7); ED.files.askDelete("R", [fb.id]); return 1; })()`);
    const qd = await api.dialog();
    await api.button("DELETE");
    check(/A CHANGED BUILT-IN GOES BACK TO THE FIRMWARE'S OWN/.test(qd) && await evaluate(`ED.app.st.R.filter(i => i.kind === 7).length`) === 0 &&
          await evaluate(`ED.files.viewItems("R").some(i => i.fw && i.kind === 7 && i.num === 0xFB)`),
          "favorites: DELETE of FB on the FM-1 panel: back to the firmware's own, empty: " + qd);
    await moveL("[window.favs[2].id]");                     /* (THIRD on again, for FB to the TABLE below) */
    await shot("04-three-moved");
    /* FB moved to the TABLE: a FAVORITES item of its own, named FAVORITES */
    await evaluate(`(() => { const fb = ED.app.st.R.find(i => i.kind === 7); ED.files.selectIds("R", [fb.id]); ED.files.transfer("R", [fb.id]); return 1; })()`);
    const s4 = await api.status();
    check(await evaluate(`ED.app.st.L.filter(i => i.kind === 7 && !i.builtin && ED.model.nameOf(i, ED.app.st.L) === "FAVORITES").length`) === 1 &&
          /^← TABLE: FAVORITES · A COPY OF BUILT-IN FB/.test(s4), "favorites: FB moved to the TABLE: an item of its own, named FAVORITES: " + s4);
    /* a disk's UPLOAD puts the disk back as it is: a backup holding FB and an earlier PT/FM's 07 gives both */
    await evaluate(`(() => {
        const R = ED.rec, mk = (num, name, slot) => ({ id: ED.model.newId(), side: "R", kind: 7, num, orig: num, link: {}, builtin: num >= 0xF0,
            recs: [{ sub: 0, ver: 1, payload: R.packCart(7, { name, slots: [{ cart: 0xF0, slot }] }) }] });
        const bytes = ED.fm1.diskOf([mk(0xFB, "FAVORITES", 1), mk(0x07, "MY FAVORITES", 2)]);
        ED.fm1.uploadDisk(new File([bytes], "favs.ptfmdsk"));
        return 1; })()`);
    const s5 = await api.until(`(() => { const s = (document.querySelector('#status .smsg') || {}).textContent || ''; return /UPLOAD DISK/.test(s) && s; })()`, 20);
    const f5 = await evaluate(`ED.app.st.R.filter(i => i.kind === 7).map(f => ED.model.H2(f.num) + " " + ED.rec.unpackCart(7, f.recs[0].payload).name +
        " " + ED.rec.unpackCart(7, f.recs[0].payload).slots[0].slot).sort().join(", ")`);
    check(f5 === "07 MY FAVORITES 2, FB FAVORITES 1" && /^UPLOAD DISK: FAVS\.PTFMDSK IN THE FM-1 PANEL, 2 ITEMS · NOTHING CHANGES/.test(s5),
          "favorites: a disk's UPLOAD puts back FB and an earlier PT/FM's 07 as the disk holds them (" + f5 + "): " + s5);
    await shot("05-disk-upload");
};
