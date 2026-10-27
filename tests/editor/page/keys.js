/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The instrument tabs' keys and side list, and small things of FILES and the tabs: the cartridge's instrument list held
 * in view, its scroll kept over a redraw; MIDI asked for once at a time when the list is opened (never on focus; the
 * browser's request held open here, as a question not answered yet); where the tabs play from kept by the page (another
 * tab, every tab closed, a reload); the computer's keys playing the tab shown only; a group's header folding it, Ctrl +
 * click picking it; a song position's ✎; a cartridge's NAME and its button on one row. */
module.exports = async api => {
    const { evaluate, check, shot, sleep, send } = api;
    await api.fresh();
    await evaluate("try { localStorage.removeItem('ptfm-editor-keys-from'); } catch (e) {} 1");
    await api.demo();
    const helpers = `window.KK = {
        open(k) { const it = ED.app.st.L.find(i => i.kind === k) || ED.files.viewItems("R").find(i => i.kind === k); ED.tabs.open(it); return !!it; },
        body() { return ED.tabs.current().body; },
        sel() { return KK.body().querySelector("select.keysfrom"); },
        asks: 0
    };
    navigator.requestMIDIAccess = () => { KK.asks++; return new Promise(() => {}); }; 1`;
    await evaluate(helpers);
    /* the side list */
    await evaluate("KK.open(2)");
    await api.until("!!KK.body().querySelector('.slots')", 10);
    await evaluate("KK.body().scrollTop = KK.body().scrollHeight; 1");
    await sleep(300);
    const side = await evaluate(`(() => { const b = KK.body().getBoundingClientRect(), s = KK.body().querySelector(".slots").getBoundingClientRect();
        return [Math.round(s.top - b.top), Math.round(b.bottom - s.bottom)]; })()`);
    check(side[0] >= 0 && side[0] <= 12 && side[1] >= 0, "keys: the instrument list held in view at the page's bottom (" + side + " px from the body's top / bottom)");
    await shot("01-side-list");
    const kept = await evaluate(`(async () => { const s = KK.body().querySelector(".slots"); s.scrollTop = 1500; s.dispatchEvent(new Event("scroll"));
        const was = s.scrollTop; [...s.querySelectorAll(".slotrow")][60].click(); await new Promise(r => setTimeout(r, 50));
        const s2 = KK.body().querySelector(".slots"); return [s2 !== s, was, s2.scrollTop, s2.querySelector(".slotrow.on .mono").textContent]; })()`);
    check(kept[0] && kept[1] > 0 && kept[2] === kept[1] && kept[3] === "3C", "keys: the list's scroll kept over a redraw: " + JSON.stringify(kept));
    /* MIDI asked for once at a time, never on focus */
    const asks = await evaluate(`(() => { const s = KK.sel(), a0 = KK.asks;
        s.dispatchEvent(new FocusEvent("focus")); s.dispatchEvent(new FocusEvent("blur")); s.dispatchEvent(new FocusEvent("focus"));
        const onFocus = KK.asks - a0;
        s.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true })); s.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
        s.dispatchEvent(new KeyboardEvent("keydown", { key: "Tab", bubbles: true }));
        return [onFocus, KK.asks - a0 - onFocus]; })()`);
    check(asks[0] === 0 && asks[1] === 1, "keys: MIDI asked on focus " + asks[0] + " times, on two openings at once " + asks[1]);
    /* the VIRTUAL MIDI KEYBOARD picked in one tab: another tab, the keys, every tab closed, a reload */
    await evaluate(`(() => { const s = KK.sel(); s.value = "pc"; s.dispatchEvent(new Event("change")); return 1; })()`);
    await evaluate("KK.open(1)");
    check(await evaluate("KK.sel().value") === "pc" && /^VIRTUAL MIDI KEYBOARD:/.test(await evaluate("KK.body().querySelector('.keyshint').textContent")),
        "keys: the choice the same in another instrument tab");
    const keys = await evaluate(`(() => { const dx = ED.tabs.current(), ju = ED.tabs.list.find(t => t !== dx && t.held);
        document.body.dispatchEvent(new KeyboardEvent("keydown", { key: "z", bubbles: true }));
        const on = [[...dx.held].join(), ju ? [...ju.held].join() : ""], lit = !!KK.body().querySelector('.piano [data-n="53"].on');
        document.body.dispatchEvent(new KeyboardEvent("keyup", { key: "z", bubbles: true }));
        return [on[0], on[1], lit, dx.held.size]; })()`);
    check(keys[0] === "53" && keys[1] === "" && keys[2] && keys[3] === 0, "keys: Z plays F3 in the tab shown only, lit, off at its key up: " + JSON.stringify(keys));
    await evaluate("ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); KK.open(6); 1");
    check(await evaluate("KK.sel().value") === "pc", "keys: the choice kept with every tab closed");
    await send("Page.reload", {});
    await api.until("!!document.querySelector('#status') && /BACK AS YOU LEFT IT/.test(document.querySelector('#status').textContent)", 30);
    await evaluate(helpers);
    await evaluate("ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); KK.open(3); 1");
    check(await evaluate("KK.sel().value") === "pc", "keys: the choice kept over a reload");
    await evaluate("try { localStorage.removeItem('ptfm-editor-keys-from'); } catch (e) {} 1");
    /* FILES: a group's header folds it, picking nothing; Ctrl + click picks all of it */
    await evaluate("ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.show('files'); 1");
    const g = await evaluate(`(() => {
        const L = document.querySelector(".ghead[data-g='SONGS']").closest(".list");
        const head = () => L.querySelector(".ghead[data-g='SONGS']"), rows = () => L.querySelectorAll("[data-id]").length, picked = () => ED.files.selected("L").length;
        const r0 = rows(); head().querySelector(".glabel").click();
        const r1 = rows(), p1 = picked(); head().querySelector(".gsize").click();
        const r2 = rows(); head().querySelector(".glabel").dispatchEvent(new MouseEvent("click", { bubbles: true, ctrlKey: true }));
        return [r0, r1, p1, r2, picked(), rows()]; })()`);
    check(g[1] === g[0] - 18 && g[2] === 0 && g[3] === g[0] && g[4] === 18 && g[5] === g[0], "keys: a group's header folds and opens it, Ctrl + click picks its 18 songs: " + JSON.stringify(g));
    /* a song position's ✎ opens its pattern */
    const pen = await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.tabs.open(s);
        const b = [...ED.tabs.current().body.querySelectorAll(".orow button")].filter(x => x.textContent === "✎")[1]; b.click();
        return [ED.tabs.current().base.kind, ED.tabs.list.length]; })()`);
    check(pen[0] === 0x20 && pen[1] === 2, "keys: a song position's ✎ opens its pattern in a tab");
    /* a cartridge's NAME as wide as 12 capitals, its button on the same row */
    await evaluate("ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); KK.open(1); 1");
    const row = await evaluate(`(() => { const r = KK.body().querySelector(".namerow"), i = r.querySelector("input"), b = r.querySelector("button");
        return [Math.round(i.getBoundingClientRect().width), Math.abs(b.getBoundingClientRect().top - i.getBoundingClientRect().top) < 12]; })()`);
    check(row[0] > 100 && row[0] < 200 && row[1], "keys: the cartridge's NAME " + row[0] + " px wide, ▶ PLAY EVERY INSTRUMENT on its row");
    await shot("02-name-row");
};
