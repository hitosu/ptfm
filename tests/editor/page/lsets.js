/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A LOOPS set on the page (specs/editor.md): on the FM-1 panel in a group of its own, LOOPS SETS, its line its number
 * and name; its tab LOOPS' own with a SOLO column (a mute and a solo saved into it, its name kept); F2 renames it, the
 * name written into its record's first 12 bytes, its loops untouched. */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    await evaluate(`(() => {
        const R = ED.rec, L = R.unpackLoops(R.loopsV3([(() => { const b = new Uint8Array(72); b.set([125, 0, 6, 50, 0, 0, 0, 0]); return b; })()]));
        L.length = 16;
        L.loops = [{ cart: 0xF8, slot: 0, colour: 0, mono: 0, muted: false, lanes: 1, base: 16, repeat: 1, shift: 0, mixer: R.MIX_DEFAULT.slice(),
            cells: new Map([[R.cellKey(0, 0), { note: 60, ins: -1, vol: -1, cmds: [] }]]) }];
        const parts = R.packLoops(L), p0 = new Uint8Array(R.LSET_PRE + parts[0].length);
        p0.set(R.packName("SUNDAY JAM", 16), 0);
        p0.set(parts[0], R.LSET_PRE);
        const recs = [p0].concat(parts.slice(1)).map((payload, sub) => ({ kind: 0x24, num: 3, sub, ver: 1, payload }));
        const it = ED.model.fromRecords(recs, { side: "R" }).items[0];
        ED.app.commit(Object.assign({}, ED.app.st, { R: ED.app.st.R.concat([it]) }), "A SET");
        window.SET = { id: it.id, loops: [...parts[0]].join() };
        return 1; })()`);
    const shown = await evaluate(`(() => {
        const p = document.querySelector(".panel[data-side=R]"), g = p.querySelector('.ghead[data-g="LOOPS SETS"]');
        const row = p.querySelector('[data-id="' + CSS.escape(window.SET.id) + '"]');
        return [!!g, g ? g.querySelector(".gcount").textContent : "", row ? row.textContent : ""]; })()`);
    check(shown[0] && shown[1] === "1" && /03/.test(shown[2]) && /SUNDAY JAM/.test(shown[2]),
          "loops sets: on the FM-1 panel in its group LOOPS SETS, its line 03 SUNDAY JAM (" + JSON.stringify(shown) + ")");
    await shot("01-group");
    /* its tab, LOOPS' own (a double click on its line, as EDIT IN A TAB) with a SOLO column: loop 00 muted and soloed
     * there, then SAVE: its record's loop table and solos written, its name kept */
    const tabs0 = await evaluate("document.querySelectorAll('.tab').length");
    await evaluate(`(() => { const row = document.querySelector('.panel[data-side=R] [data-id="' + CSS.escape(window.SET.id) + '"]');
        row.dispatchEvent(new MouseEvent("dblclick", { bubbles: true })); return 1; })()`);
    const tabs1 = await evaluate("document.querySelectorAll('.tab').length");
    const head = await evaluate(`(() => [...document.querySelectorAll('.lmixrow.mixhead span')].map(s => s.textContent).join(","))()`);
    check(tabs1 === tabs0 + 1 && /MUTE,SOLO,LEVEL/.test(head), "loops sets: opening one opens its tab, the mixer's MUTE and SOLO columns (" +
          tabs0 + " -> " + tabs1 + "; " + head + ")");
    await shot("01b-tab");
    const saved = await evaluate(`(() => {
        const rows = [...document.querySelectorAll('.lmixrow:not(.mixhead)')], box = rows[0].querySelectorAll('input[type=checkbox]');
        box[0].checked = true; box[0].dispatchEvent(new Event("change", { bubbles: true }));
        const box2 = document.querySelectorAll('.lmixrow:not(.mixhead)')[0].querySelectorAll('input[type=checkbox]');
        box2[1].checked = true; box2[1].dispatchEvent(new Event("change", { bubbles: true }));
        ED.tabs.save(ED.tabs.current());
        const it = ED.app.st.R.find(i => i.kind === 0x24 && i.num === 3), S = ED.rec.unpackLSet(it.recs.map(r => r.payload));
        return [S.loops.loops[0].muted, S.solos, S.name]; })()`);
    check(saved[0] === true && saved[1] === 1 && saved[2] === "SUNDAY JAM", "loops sets: MUTE and SOLO saved into the set (" + JSON.stringify(saved) + ")");
    await evaluate("(() => { ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.show('files'); return 1; })()");
    await evaluate(`(() => { window.SET.loops = [...ED.app.st.R.find(i => i.kind === 0x24 && i.num === 3).recs[0].payload.subarray(ED.rec.LSET_PRE)].join(); return 1; })()`);
    /* F2: its rename, the name into bytes 0-11 */
    const renamed = await evaluate(`(async () => {
        ED.files.selectIds("R", [window.SET.id]);
        const list = document.querySelector(".panel[data-side=R] .list");
        list.focus();
        list.dispatchEvent(new KeyboardEvent("keydown", { key: "F2", bubbles: true, cancelable: true }));
        const inp = document.activeElement;
        if (!inp || inp.tagName !== "INPUT") return ["no input"];
        inp.value = "RAINY DAY";
        inp.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
        const it = ED.app.st.R.find(i => i.kind === 0x24 && i.num === 3), b = it.recs[0].payload;
        return [ED.model.nameOf(it, ED.app.st.R), ED.rec.unpackName(b.subarray(0, 12), 16), [...b.subarray(ED.rec.LSET_PRE)].join() === window.SET.loops]; })()`);
    check(renamed[0] === "RAINY DAY" && renamed[1] === "RAINY DAY" && renamed[2] === true,
          "loops sets: F2 renames it, the name in its first 12 bytes, its loops untouched (" + JSON.stringify(renamed) + ")");
    await shot("02-renamed");
};
