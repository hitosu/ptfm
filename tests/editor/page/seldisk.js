/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DOWNLOAD AS DISK in the TABLE's selection bar: a song picked and the key clicked; the file the page offers caught (its
 * link's click held back, its name and bytes kept), the status line, the panels unchanged and no step of UNDO; the disk
 * put in the FM-1 panel (UPLOAD DISK) equal to the move to an empty FM-1 panel, dropped on the TABLE equal by hash to what
 * was picked; two songs picked: ptfm-selection and the date; a version the page cannot read: refused, no file. */
module.exports = async api => {
    const { evaluate, check, shot, until } = api;
    await api.fresh();
    await api.demo();
    /* (the status line's words, without the UNDO beside them) */
    api.status = () => evaluate("(document.querySelector('#status .smsg') || {}).textContent || ''");
    /* the page's downloads caught: a link's click held back, its name and its file's bytes kept */
    await evaluate(`(() => {
        window.caught = [];
        const blobs = new Map(), make = URL.createObjectURL.bind(URL);
        URL.createObjectURL = b => { const u = make(b); blobs.set(u, b); return u; };
        document.addEventListener("click", e => {
            const a = e.target.closest && e.target.closest("a[download]");
            if (!a) return;
            e.preventDefault();
            const got = { name: a.download, bytes: null };
            window.caught.push(got);
            blobs.get(a.href).arrayBuffer().then(x => { got.bytes = new Uint8Array(x); });
        }, true);
        return 1; })()`);
    const key = side => evaluate(`(() => { const b = [...document.querySelectorAll(".panel[data-side=${side}] .selbar button")].find(x => x.textContent === "DOWNLOAD AS DISK");
        if (b) b.click(); return !!b; })()`);
    await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21 && ED.model.nameOf(i) === "PUNCH IN"); window.song = s.id;
        ED.files.selectIds("L", [s.id]); window.st0 = ED.app.st; window.h0 = ED.app.hist.back.length; return 1; })()`);
    check(await key("L"), "seldisk: the TABLE's selection bar has DOWNLOAD AS DISK");
    const name = await until("window.caught.length === 1 && window.caught[0].bytes && window.caught[0].name", 10);
    const st = await api.status();
    check(name === "punch-in.ptfmdsk" && /^DOWNLOAD AS DISK: punch-in\.ptfmdsk, 14 ITEMS, [\d.]+ KB/.test(st), "seldisk: the file offered, " + name + ": " + st);
    /* (the file's bytes are the selection's disk, byte for byte) */
    const sameBytes = i => evaluate(`(() => { const a = window.caught[${i}].bytes, d = ED.fm1.selectionDisk(ED.app.st.L, ED.files.selected("L").map(x => x.id));
        return d.ok && a.length === d.bytes.length && a.every((x, j) => x === d.bytes[j]); })()`);
    check(await sameBytes(0), "seldisk: the file's bytes are the selection's disk, byte for byte");
    check(await evaluate("ED.app.st === window.st0 && ED.app.hist.back.length === window.h0 && ED.app.st.R.length === 0"),
        "seldisk: the panels as they were, no step of UNDO");
    await evaluate(`ED.files.selectIds("R", []); 1`);
    check(!await key("R"), "seldisk: no DOWNLOAD AS DISK in the FM-1 panel's selection bar (its DOWNLOAD DISK saves the panel)");
    await shot("01-downloaded");
    /* its disk put in the FM-1 panel: the move to an empty FM-1 panel; dropped on the TABLE: what was picked, by hash */
    const trip = await evaluate(`(async () => {
        const M = ED.model, b = window.caught[0].bytes, L = ED.app.st.L;
        const sig = items => { const hs = M.hashes(items); return items.map(i => i.kind + "." + i.num + "." + hs.get(i.id)).sort().join(); };
        const mv = M.transfer({ L, R: [], base: [] }, "L", [window.song], {});
        await ED.fm1.uploadDisk(new File([b], "punch-in.ptfmdsk"));
        const th = M.hashes(L), picked = [...M.closure(L, [window.song])].map(id => L.find(i => i.id === id).kind + "." + th.get(id)).sort().join();
        window.n0 = L.length; window.picked = picked;
        await ED.drops.add([new File([b], "punch-in.ptfmdsk")]);
        return [sig(ED.app.st.R) === sig(mv.st.R), ED.app.st.R.length, mv.st.R.length];
    })()`);
    check(trip[0] && trip[1] === 14, "seldisk: UPLOAD DISK of it: the same items, numbers and hashes as the move to an empty FM-1 panel (" + trip.slice(1) + ")");
    const back = await until(`ED.app.st.L.length > window.n0 && (() => { const M = ED.model, L = ED.app.st.L, add = L.slice(window.n0), hs = M.hashes(L);
        return [add.map(i => i.kind + "." + hs.get(i.id)).sort().join() === window.picked, add.length]; })()`, 20);
    check(back && back[0] && back[1] === 14, "seldisk: dropped on the TABLE: the song picked and what it uses, equal by hash (" + back + ")");
    /* two songs picked: ptfm-selection and the date */
    await evaluate(`(() => { const s = ED.app.st.L.filter(i => i.kind === 0x21).slice(0, 2).map(i => i.id); ED.files.selectIds("L", s); return 1; })()`);
    await key("L");
    const two = await until("window.caught.length === 2 && window.caught[1].bytes && window.caught[1].name", 10);
    const today = await evaluate(`(() => { const d = new Date(); return "ptfm-selection-" + d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0") + ".ptfmdsk"; })()`);
    check(two === today && new RegExp("^DOWNLOAD AS DISK: " + today.replace(/\./g, "\\.") + ", \\d+ ITEMS, ").test(await api.status()), "seldisk: two songs picked: " + two + ": " + await api.status());
    check(await sameBytes(1), "seldisk: two songs: the file's bytes are the selection's disk, byte for byte");
    /* a version this page cannot read: refused with the move's words, no file */
    await evaluate(`(() => { const it = ED.model.fromRecords([{ kind: 0x20, num: 2, sub: 0, ver: 9, payload: new Uint8Array([1, 3, 1, 0, 0]) }], { side: "L", src: "ODD.PTFMDSK" }).items[0];
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([it]) }), "AN ODD ONE"); ED.files.selectIds("L", [it.id]); return 1; })()`);
    await key("L");
    const no = await api.status();
    check(no === "A VERSION THIS PAGE CANNOT READ STAYS WHERE IT IS · NO DISK SAVED" && await evaluate("document.querySelector('#status').classList.contains('err') && window.caught.length === 2"),
        "seldisk: refused, no file: " + no);
    await shot("02-refused");
};
