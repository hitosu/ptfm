/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* ← of a built-in cartridge: the FM-1 panel's built-in DX7 picked, ← TO TABLE in its selection bar: a cartridge of its own
 * on the TABLE (named as the built-in, no number, no CHANGED tag), the status line calling it a copy; again: ALREADY THERE;
 * the built-in JUNO-106 dragged to the TABLE: its copy; the DX7's copy moved back (→): a new cartridge at 00 beside the
 * built-in; the built-in changed on the FM-1 panel, picked and moved: a copy with its changes, back at a free number
 * beside it; a song on the TABLE pointing at the first copy names 00 on the FM-1 panel; ← of a default and DELETE of a
 * built-in still refused. */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    /* (the status line's words, without the UNDO beside them) */
    api.status = () => evaluate("(document.querySelector('#status .smsg') || {}).textContent || ''");
    const bar = side => `[...document.querySelectorAll(".panel[data-side=${side}] .selbar button")]`;
    const click = (side, label) => evaluate(`(() => { const b = ${bar(side)}.find(x => x.textContent === ${JSON.stringify(label)}); if (b) b.click(); return !!b; })()`);
    const fw = await evaluate(`(() => { const b = ED.files.viewItems("R").find(i => i.fw && i.kind === 1); window.fw0 = b.id; window.fwName = ED.model.nameOf(b, []);
        window.fwNum = b.num; window.fwRec = b.recs[0]; return b.id + " " + window.fwName; })()`);
    const F0 = await evaluate("ED.model.H2(window.fwNum)");
    await evaluate(`ED.files.selectIds("R", [window.fw0]); 1`);
    check(await click("R", "← TO TABLE"), "builtin: the built-in's selection bar has ← TO TABLE (" + fw + ")");
    const c1 = await evaluate(`(() => { const L = ED.app.st.L, c = L[0]; window.copy = c && c.id;
        return L.length === 1 && c.kind === 1 && !c.builtin && c.orig == null && c.num == null && ED.model.nameOf(c, L) === window.fwName &&
            c.recs[0].payload.length === window.fwRec.payload.length && c.recs[0].payload.every((v, i) => v === window.fwRec.payload[i]) &&
            !document.querySelector(".panel[data-side=L] .row .tag"); })()`);
    const s1 = await api.status();
    check(c1 && s1 === "← TABLE: " + fw.split(" ").slice(1).join(" ") + " · A COPY OF BUILT-IN " + F0, "builtin: a cartridge of its own on the TABLE, named as the built-in: " + s1);
    await shot("01-copied");
    await click("R", "← TO TABLE");
    const s2 = await api.status();
    check(/1 ITEM ALREADY THERE/.test(s2) && await evaluate("ED.app.st.L.length") === 1, "builtin: moved again: one copy: " + s2);
    /* the built-in JUNO-106 dragged to the TABLE */
    const dragged = await evaluate(`(() => { const b = ED.files.viewItems("R").find(i => i.fw && i.kind === 2);
        const row = document.querySelector('.panel[data-side=R] [data-id="' + CSS.escape(b.id) + '"]'), dt = new DataTransfer();
        row.dispatchEvent(new DragEvent("dragstart", { bubbles: true, dataTransfer: dt }));
        document.querySelector(".panel[data-side=L]").dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: dt }));
        const j = ED.app.st.L.filter(i => i.kind === 2);
        return [row.draggable, j.length, j.length === 1 && !j[0].builtin && j[0].orig == null]; })()`);
    check(dragged[0] && dragged[1] === 1 && dragged[2], "builtin: the built-in JUNO-106 dragged to the TABLE: its copy (" + dragged + ")");
    /* the DX7's copy moved back: a new cartridge at 00, the built-in beside it */
    await evaluate(`ED.files.selectIds("L", [window.copy]); 1`);
    await click("L", "TO FM-1 →");
    const s3 = await api.status();
    const two = await evaluate(`ED.files.viewItems("R").filter(i => i.kind === 1 && (i.num === 0 || i.num === window.fwNum)).map(i => ED.model.H2(i.num) + (i.fw ? " BUILT-IN" : "")).sort().join(", ")`);
    check(two === "00, " + F0 + " BUILT-IN" && await evaluate("ED.app.st.R.length === 1 && !ED.app.st.R[0].builtin"), "builtin: the copy moved back: " + two + " (" + s3 + ")");
    /* the built-in changed on the FM-1 panel (as its tab's SAVE makes it), then picked and moved: a copy with its changes */
    await evaluate(`(() => { const p = window.fwRec.payload.slice(); p[0] ^= 1; window.changed = p;
        const it = { id: ED.model.newId(), side: "R", kind: 1, num: window.fwNum, orig: window.fwNum, link: {}, builtin: true, recs: [{ sub: 0, ver: window.fwRec.ver, payload: p }] };
        ED.app.commit(Object.assign({}, ED.app.st, { R: ED.app.st.R.concat([it]) }), "CHANGED"); ED.files.selectIds("R", [it.id]); return 1; })()`);
    await click("R", "← TO TABLE");
    const s4 = await api.status();
    const c4 = await evaluate(`(() => { const c = ED.app.st.L.filter(i => i.kind === 1 && i.id !== window.copy); window.copy2 = c[0] && c[0].id;
        return c.length === 1 && !c[0].builtin && c[0].orig == null && c[0].recs[0].payload.every((v, i) => v === window.changed[i]); })()`);
    check(c4 && /A COPY OF BUILT-IN /.test(s4), "builtin: the changed built-in picked: a copy with its changes: " + s4);
    await evaluate(`ED.files.selectIds("L", [window.copy2]); 1`);
    await click("L", "TO FM-1 →");
    const three = await evaluate(`ED.app.st.R.filter(i => i.kind === 1).map(i => ED.model.H2(i.num) + (i.builtin ? " CHANGED" : "")).sort().join(", ")`);
    check(three === "00, 01, " + F0 + " CHANGED", "builtin: its copy moved back beside it: " + three + " (" + await api.status() + ")");
    /* a song on the TABLE pointing at the first copy: on the FM-1 panel it names 00, the copy's number there */
    const named = await evaluate(`(() => {
        const M = ED.model, R = ED.rec;
        const pat = { id: M.newId(), side: "L", kind: 0x20, num: null, orig: 5, link: { c05: window.copy }, name: "USES THE COPY", src: "NEW",
            recs: R.packPattern({ tracks: 1, rows: 4, step: 1, cells: new Map([["0,0", { note: 60, ins: 5 << 8, vol: -1, cmds: [] }]]) }).map((payload, sub) => ({ sub, ver: R.VERSION[0x20], payload })) };
        const s = R.newSong("THE COPY"); s.positions = [5];
        const song = { id: M.newId(), side: "L", kind: 0x21, num: null, orig: 1, link: { p05: pat.id }, src: "NEW", recs: [{ sub: 0, ver: R.VERSION[0x21], payload: R.packSong(s) }] };
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([pat, song]) }), "A SONG");
        ED.files.selectIds("L", [song.id]);
        [...document.querySelectorAll(".panel[data-side=L] .selbar button")].find(x => x.textContent === "TO FM-1 →").click();
        const p = ED.app.st.R.find(i => i.kind === 0x20), n = R.unpackPattern(p.recs.map(x => x.payload)).cells.get("0,0").ins >> 8;
        return M.H2(n) + " " + (ED.app.st.R.find(i => i.id === p.link[M.lk("c", n)]) || {}).num; })()`);
    check(named === "00 0", "builtin: a TABLE song pointing at the copy names the copy's number on the FM-1 panel: " + named + " (" + await api.status() + ")");
    await shot("02-side-by-side");
    /* still refused: ← of a default, DELETE of a built-in */
    await evaluate(`ED.files.selectIds("R", ["D:48"]); 1`);
    await click("R", "← TO TABLE");
    check(await api.status() === "A DEFAULT HAS NOTHING IN IT TO MOVE", "builtin: ← of a default: " + await api.status());
    await evaluate(`ED.files.askDelete("R", [ED.files.viewItems("R").find(i => i.fw).id]); 1`);
    check(await api.status() === "BUILT-IN INSTRUMENTS LIVE IN THE FIRMWARE: NOTHING TO DELETE", "builtin: DELETE of a built-in: " + await api.status());
};
