/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Many instruments at once in a cartridge's tab (specs/editor.md, The tabs: Many instruments at once): the list to the
 * cartridge's size; Shift-click picks a range; COPY, then PASTE into empty slots (nothing replaced); another type's tab
 * refuses the paste; DELETE of used instruments asks (DELETE or KEEP); on the FM-1 panel two used instruments swapped
 * (dropped on each other) and saved: every cell naming them follows, the status line says what followed. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    /* the demo disk moved to the FM-1 panel: its patterns name its cartridges by number */
    await evaluate(`(() => { const r = ED.model.transfer(ED.app.st, "L", ED.app.st.L.filter(i => !i.owner).map(i => i.id), {}); ED.app.commit(r.st, "TEST"); return 1; })()`);
    await sleep(300);
    /* a sampler of the FM-1 panel the most cells name */
    const pick = await evaluate(`(() => { const R = ED.app.st.R, O = ED.slotops;
        const s = R.filter(i => i.kind === 0x04).map(c => ({ c, n: O.refs(R, "R", c).length })).sort((a, b) => b.n - a.n)[0];
        const used = [...new Set(O.refs(R, "R", s.c).map(x => x.slot))];
        return { id: s.c.id, num: s.c.num, used: used.slice(0, 2), n: ED.rec.unpackSampler(s.c.recs[0].payload).instruments.length }; })()`);
    await evaluate(`ED.tabs.open(ED.app.st.R.find(i => i.id === ${JSON.stringify(pick.id)})); 1`);
    await sleep(400);
    const rows = await evaluate(`ED.tabs.current().body.querySelectorAll(".slots .slotrow").length`);
    check(rows === 64, "slots: the list shows every slot to the sampler's 64 (" + rows + ")");
    /* Shift-click a range of two used slots, COPY, then PASTE: into two empty slots */
    const row = i => `ED.tabs.current().body.querySelectorAll(".slots .slotrow")[${i}]`;
    await evaluate(`${row(0)}.click(); 1`); await sleep(100);
    await evaluate(`${row(1)}.dispatchEvent(new MouseEvent("click", { bubbles: true, shiftKey: true })); 1`); await sleep(100);
    const picked = await evaluate(`ED.tabs.current().sel.size`);
    check(picked === 2, "slots: Shift-click picks the range (" + picked + ")");
    const btn = l => evaluate(`(() => { const b = [...ED.tabs.current().body.querySelectorAll("button")].find(x => x.textContent === ${JSON.stringify(l)}); if (b) b.click(); return !!b; })()`);
    await btn("COPY"); await sleep(100);
    const before = await evaluate(`ED.rec.unpackSampler(ED.tabs.current().draft.recs[0].payload).instruments.filter(Boolean).length`);
    await btn("PASTE"); await sleep(200);
    const after = await evaluate(`ED.rec.unpackSampler(ED.tabs.current().draft.recs[0].payload).instruments.filter(Boolean).length`);
    check(after === before + 2, "slots: PASTE adds the two into empty slots (" + before + " -> " + after + ")");
    await shot("01-pasted");
    /* DELETE of a used instrument asks */
    await evaluate(`${row(pick.used[0])}.click(); 1`); await sleep(100);
    await btn("DELETE"); await sleep(200);
    const dlg = await api.dialog();
    check(/IS USED \d+ TIMES|ARE USED \d+ TIMES/.test(dlg) && /DELETE ANYWAY/.test(dlg), "slots: DELETE of a used instrument asks: " + dlg.slice(0, 80));
    await api.button("KEEP"); await sleep(100);
    /* the two used instruments swapped by a drop and saved: the cells follow */
    const cells = `(() => { const R = ED.app.st.R, O = ED.slotops, c = R.find(i => i.id === ${JSON.stringify(pick.id)});
        const r = O.refs(R, "R", c); return [r.filter(x => x.slot === ${pick.used[0]}).length, r.filter(x => x.slot === ${pick.used[1]}).length]; })()`;
    const c0 = await evaluate(cells);
    await evaluate(`(() => { const rs = ED.tabs.current().body.querySelectorAll(".slots .slotrow"), a = rs[${pick.used[0]}], b = rs[${pick.used[1]}];
        const dt = new DataTransfer(); a.dispatchEvent(new DragEvent("dragstart", { bubbles: true, dataTransfer: dt }));
        b.dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: dt })); return 1; })()`);
    await sleep(200);
    await evaluate(`ED.tabs.current().saveBtn.click(); 1`);
    await sleep(300);
    const c1 = await evaluate(cells), st = await api.status();
    check(c1[0] === c0[1] && c1[1] === c0[0] && /FOLLOW/.test(st), "slots: swapped and saved: the cells naming them follow (" + JSON.stringify([c0, c1]) + "), " + st);
    /* another type's tab refuses the paste */
    await evaluate(`ED.tabs.open(ED.app.st.R.find(i => i.kind === 0x05)); 1`); await sleep(300);
    await btn("PASTE"); await sleep(100);
    check(/^COPIED FROM SAMPLER: NOT HERE/.test(await api.status()), "slots: a FUSION tab refuses a sampler's copy: " + await api.status());
};
