/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* SET NUMBER on the FM-1 panel (specs/editor.md, Numbers on the FM-1 panel): F4 and the right-click menu; the dialog saying what
 * each number would do, its key off for a number refused; a free number and a held one, what names the cartridge following;
 * UNDO; a built-in, the TABLE and several picked without it; a tab open and clean following, one with changes refusing. */
module.exports = async api => {
    const { evaluate, check, shot, sleep, until } = api;
    const A0 = 0xA0, A1 = 0xA1, A2 = 0xA2, A5 = 0xA5;    /* (the demo disk's DRUM KIT, PERCUSSION, BAND/ORCH, FUSION) */
    await api.fresh();
    await api.demo();
    await evaluate(`(() => { const r = ED.model.transfer(ED.app.st, "L", ED.app.st.L.map(i => i.id), {}); ED.app.commit(r.st, r.msg); ED.tabs.show("files"); return 1; })()`);
    await until("ED.app.st.R.length > 100", 20);
    const cart = n => `ED.app.st.R.find(i => ED.model.isCart(i.kind) && i.num === ${n})`;
    const named = n => evaluate(`ED.app.st.R.reduce((c, i) => c + ED.model.refs(i).filter(f => f.sp === "c" && f.num === ${n}).length, 0)`);
    const pick = n => evaluate(`(() => { ED.files.selectIds("R", [${cart(n)}.id]); document.querySelector(".panel[data-side=R] .list").focus(); return 1; })()`);
    const key = k => evaluate(`document.querySelector(".panel[data-side=R] .list").dispatchEvent(new KeyboardEvent("keydown", { key: "${k}", bubbles: true })); 1`);
    const type = v => evaluate(`(() => { const i = document.querySelector("#dlg input.num"); i.value = "${v}"; i.dispatchEvent(new Event("input", { bubbles: true })); return 1; })()`);
    const say = () => evaluate(`document.querySelector("#dlg .numsay").textContent`);
    const off = () => evaluate(`document.querySelector("#dlg .btn.primary").disabled`);
    const status = () => evaluate("(document.querySelector('#status .smsg') || {}).textContent || ''");
    const n1 = await named(A0), n2 = await named(A1);
    check(n1 > 0 && n2 > 0, "renumber: the samplers A0 and A1 are named: " + n1 + ", " + n2);

    /* F4 opens it on the number it has */
    await pick(A0);
    await key("F4");
    await sleep(100);
    check(await evaluate(`document.activeElement === document.querySelector("#dlg input.num") && document.activeElement.value === "A0"`), "renumber: F4: the dialog, its field holding A0");
    check(/^A NUMBER FOR /.test(await api.dialog()), "renumber: its title: " + await api.dialog());
    check(await off() && /A0 ALREADY$/.test(await say()), "renumber: its own number: the key off: " + await say());
    await type("F3");
    check(await off() && /^F0-FF ARE THE BUILT-IN/.test(await say()), "renumber: a built-in number refused: " + await say());
    await type("2");
    check(await off() && /00 TO EF/.test(await say()), "renumber: one digit: " + await say());
    await type("zz");
    check(await off() && /00 TO EF/.test(await say()), "renumber: not hex: " + await say());
    await type("a1");
    check(!await off() && (await say()).startsWith("A1 HOLDS PERCUSSION: THE TWO SWAP NUMBERS"), "renumber: a held number, small letters too: " + await say());
    await shot("01-held");
    await type("2B");
    check(!await off() && (await say()).startsWith("2B IS FREE ·"), "renumber: a free number: " + await say());
    await shot("02-free");

    /* SET NUMBER: at once, what names it following, the row and the selection there */
    const steps = await evaluate("ED.app.hist.back.length");
    await api.button("SET NUMBER");
    await sleep(200);
    check(await evaluate(`document.querySelector("#dlg").hidden`), "renumber: the dialog closes");
    check(await named(0x2B) === n1 && await named(A0) === 0, "renumber: every reference follows to 2B: " + await named(0x2B));
    check(/^DRUM KIT IS NOW 2B · \d+ REFERENCES FOLLOW · THE FM-1 CHANGES WITH WRITE/.test(await status()), "renumber: the status line: " + await status());
    check(await evaluate("ED.app.hist.back.length") === steps + 1, "renumber: one step of UNDO");
    check(await evaluate(`ED.files.selected("R").length === 1 && ED.files.selected("R")[0].num === 0x2B`), "renumber: still picked");
    await shot("03-set");

    /* a tab open and clean follows: its head, its lists */
    await evaluate(`(() => { ED.tabs.open(${cart(0x2B)}); return 1; })()`);
    await sleep(300);
    check(/ 2B · FM-1$/.test(await evaluate(`ED.tabs.current().kindEl.textContent`)), "renumber: its tab's head: " + await evaluate(`ED.tabs.current().kindEl.textContent`));
    await evaluate(`(() => { const r = ED.model.renumber(ED.app.st, ${cart(0x2B)}.id, 0x2C); ED.app.commit(r.st, r.msg); return 1; })()`);
    await sleep(300);
    check(/ 2C · FM-1$/.test(await evaluate(`ED.tabs.current().kindEl.textContent`)), "renumber: the tab follows: " + await evaluate(`ED.tabs.current().kindEl.textContent`));

    /* a clean tab that names another cartridge draws again too: its lists show the numbers */
    await evaluate(`(() => { const p = ED.app.st.R.find(i => i.kind === 0x20 && ED.model.refs(i).every(f => f.sp !== "c" || f.num !== ${A5}));
        const t = ED.tabs.open(p); window.patTab = t.id; window.patRecs = p.recs; t.body.firstElementChild.dataset.old = "1"; return 1; })()`);
    await sleep(300);
    await evaluate(`(() => { const r = ED.model.renumber(ED.app.st, ${cart(A5)}.id, 0x30); ED.app.commit(r.st, r.msg); return 1; })()`);
    await sleep(300);
    check(await evaluate(`(() => { const t = ED.tabs.current(); return t.id === window.patTab && t.draft.recs === window.patRecs && !t.body.querySelector("[data-old]"); })()`),
        "renumber: a clean tab on another item draws again, its item unchanged");

    /* a tab with changes not saved: refused, nothing changes */
    await evaluate(`(() => { const t = ED.tabs.current(); t.dirty = true; ED.tabs.show("files"); return 1; })()`);
    await pick(A1);
    await key("F4");
    await sleep(100);
    check(await evaluate(`document.querySelector("#dlg").hidden`) && /^SAVE OR CLOSE THE FM-1 PANEL'S OPEN TABS FIRST/.test(await status()), "renumber: a tab with changes: refused: " + await status());
    await evaluate(`(() => { ED.tabs.list.forEach(t => { t.dirty = false; }); ED.tabs.list.slice().forEach(x => ED.tabs.close(x)); ED.tabs.show("files"); return 1; })()`);

    /* Enter in the field sets it; held: the two swap */
    await pick(A1);
    await key("F4");
    await sleep(100);
    await type("A2");
    await evaluate(`document.querySelector("#dlg input.num").dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true })); 1`);
    await sleep(200);
    check(await evaluate(`${cart(A2)}.num === ${A2} && ED.model.nameOf(${cart(A2)}, ED.app.st.R) === "PERCUSSION" && ED.model.nameOf(${cart(A1)}, ED.app.st.R) === "BAND/ORCH"`), "renumber: Enter: PERCUSSION and BAND/ORCH swapped");
    check(await named(A2) === n2, "renumber: PERCUSSION's references follow it");

    /* UNDO: back as it was */
    await evaluate(`(() => { const b = [...document.querySelectorAll("#status button")].find(x => x.textContent === "UNDO"); if (b) b.click(); return !!b; })()`);
    await sleep(200);
    check(await named(A1) === n2 && await evaluate(`ED.model.nameOf(${cart(A1)}, ED.app.st.R) === "PERCUSSION"`), "renumber: UNDO: back as it was");

    /* the right-click menu: on the FM-1 panel, for one cartridge of the user's own */
    const menu = (n, k) => evaluate(`(() => { ED.files.selectIds("R", ${n}); ED.files.showMenu(10, 10, "${k || "R"}");
        const e = [...document.querySelectorAll(".menu button")].find(b => b.textContent.includes("SET NUMBER")); const on = e ? !e.disabled : null; ED.ui.closeMenu(); return on; })()`);
    check(await menu(`[${cart(A1)}.id]`) === true, "renumber: the menu offers SET NUMBER");
    check(await menu(`ED.app.st.R.filter(i => i.kind === 0x21).slice(0, 1).map(i => i.id)`) === false, "renumber: not on a song");
    check(await menu(`[${cart(A1)}.id, ${cart(A2)}.id]`) === false, "renumber: not on two picked");
    check(await evaluate(`(() => { ED.files.selectIds("L", ED.app.st.L.filter(i => i.kind === 4).slice(0, 1).map(i => i.id)); ED.files.showMenu(10, 10, "L");
        const on = [...document.querySelectorAll(".menu button")].some(b => b.textContent.includes("SET NUMBER")); ED.ui.closeMenu(); return on; })()`) === false, "renumber: not on the TABLE");
};
