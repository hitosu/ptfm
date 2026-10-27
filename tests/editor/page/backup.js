/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* WRITE's BACKUP on the virtual FM-1: a song written; the page made not to know the FM-1 (no READ or WRITE behind its
 * panel), a pattern changed and written (the FM-1's old one is the demo disk's, on the TABLE already: nothing copied);
 * changed again and written (the FM-1 as the last WRITE left it: no backup at all); unknown again, changed and written
 * (the FM-1's version is new to the TABLE: it lands there, its source BACKUP and the time); UNDO takes the backup back. */
module.exports = async api => {
    const { evaluate, check, shot, until } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.transfer("L", [s.id]); return 1; })()`);
    const write = async () => {
        evaluate("ED.fm1.write(); 1");
        const d = await until("(() => { const d = document.querySelector('#dlg'); return d && !d.hidden && /WRITE TO THE FM-1|NOTHING TO WRITE/.test(d.innerText) && d.innerText.replace(/\\n+/g, ' | '); })()", 60);
        await api.button("WRITE") || await api.button("OK");
        const s = await until("(() => { const s = document.querySelector('#status').textContent; return /WRITTEN TO|NOTHING TO WRITE|REFUSED|COULD NOT/.test(s) && s; })()", 120);
        return { dialog: d || "", status: s || "" };
    };
    const change = n => evaluate(`(() => {
        const R = ED.rec, st = ED.app.st, pat = st.R.find(i => i.kind === 0x20 && i.num === 0), p = R.unpackPattern(pat.recs.map(x => x.payload));
        p.cells.set("${n},0", { note: 60 + ${n}, ins: -1, vol: 0x40, cmds: [] });
        ED.app.commit(ED.model.replaceItem(st, "R", Object.assign({}, pat, { recs: R.packPattern(p).map((payload, i) => ({ sub: i, ver: 1, payload })) })), "CHANGED"); return 1; })()`);
    const backups = () => evaluate(`ED.app.st.L.filter(i => /^BACKUP /.test(i.src || "")).map(i => i.kind + " " + i.src)`);
    let w = await write();
    check(/WRITTEN TO THE VIRTUAL FM-1: 14 RECORDS, 0 REMOVED$/.test(w.status) && !/BACKUP/.test(w.dialog), "backup: the first write, nothing to back up: " + w.status);
    const n0 = await evaluate("ED.app.st.L.length");
    const unknown = () => evaluate("ED.app.set(Object.assign({}, ED.app.st, { base: [] })); 1");
    await unknown();
    await change(3);
    w = await write();
    check(/BACKUP FIRST: WHAT THIS WRITE REPLACES OR REMOVES ON THE FM-1 \(1 RECORD\)/.test(w.dialog) && /BACKUP: THE TABLE HOLDS ALL OF IT ALREADY/.test(w.status) &&
        await evaluate("ED.app.st.L.length") === n0, "backup: the FM-1's old pattern is the demo disk's: nothing copied: " + w.status);
    await change(4);
    w = await write();
    check(/NO BACKUP: WHAT THIS WRITE REPLACES OR REMOVES ON THE FM-1 IS AS THE LAST READ OR WRITE LEFT IT/.test(w.dialog) && !/BACKUP FIRST/.test(w.dialog) &&
        !/BACKUP/.test(w.status) && await evaluate("ED.app.st.L.length") === n0, "backup: the FM-1 as the last WRITE left it: none: " + w.status);
    await unknown();
    await change(5);
    w = await write();
    const b = await backups();
    check(/BACKUP \d+ [A-Z]{3} \d\d:\d\d: 1 ITEM ON THE TABLE/.test(w.status) && b.length === 1 && /^32 BACKUP \d+ [A-Z]{3} \d\d:\d\d$/.test(b[0]),
        "backup: the FM-1's changed pattern lands on the TABLE alone, stamped: " + w.status + " · " + b);
    await shot("01-backup");
    await evaluate("ED.app.undo(); 1");
    check((await backups()).length === 0 && await evaluate("ED.app.st.L.length") === n0, "backup: UNDO takes it back");
    /* READ, DELETE ALL, something new moved there, WRITE: the FM-1's records were all seen by the READ: no backup */
    await unknown();
    await evaluate("ED.fm1.read().then(() => 1)");
    await evaluate(`(() => { ED.app.commit(ED.model.deleteAll(ED.app.st, "R"), "EMPTY");
        const s = ED.app.st.L.filter(i => i.kind === 0x21)[3]; ED.files.transfer("L", [s.id]); return 1; })()`);
    w = await write();
    check(/NO BACKUP: /.test(w.dialog) && /REMOVED FROM THE FM-1/.test(w.dialog) && !/BACKUP FIRST/.test(w.dialog) && /WRITTEN TO THE VIRTUAL FM-1: \d+ RECORDS, [1-9]\d* REMOVED$/.test(w.status) &&
        (await backups()).length === 0, "backup: READ, DELETE ALL, a song moved, WRITE: none: " + w.status);
    await shot("02-read-delete-all");
};
