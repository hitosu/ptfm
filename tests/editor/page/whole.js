/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* WRITE to an FM-1 where the panel fits only emptied (the virtual FM-1's answer changed for it here: every block of
 * its user area also holding a deleted record's header): the dialog says the whole user area is written anew, WRITE
 * writes it, and READ finds exactly the panel. */
module.exports = async api => {
    const { evaluate, check, shot, until } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`(() => {
        const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.transfer("L", [s.id]);
        const connect = ED.fm1.connect;
        ED.fm1.connect = async () => { const c = await connect(); const hello = c.session.hello.bind(c.session);
            c.session.hello = async () => { const l = await hello(), extra = [];
                for (const g of l.regions) if (g.area === "user") for (let a = g.start; a + 4096 <= g.end; a += 4096)
                    extra.push({ kind: 0x21, num: 0x7E, sub: 0, ver: 1, len: 0, crc: 0, addr: a, deleted: true, label: "SONG 7E.0" });
                l.records = l.records.concat(extra); return l; };
            return c; };
        ED.fm1.write();
        return 1; })()`);
    const d = await until("(() => { const d = document.querySelector('#dlg'); return d && !d.hidden && /WRITE TO THE FM-1/.test(d.innerText) && d.innerText.replace(/\\n+/g, ' | '); })()", 60);
    check(/THE WHOLE USER AREA IS WRITTEN ANEW/.test(d || "") && /THE OTHER \d+ BLOCKS ERASED/.test(d || "") && /WRITE \| CANCEL$/.test(d || ""),
        "whole: the dialog says the whole user area is written anew: " + d);
    await shot("01-dialog");
    await api.button("WRITE");
    const s = await until("(() => { const s = document.querySelector('#status').textContent; return /WRITTEN TO|REFUSED|COULD NOT/.test(s) && s; })()", 120);
    check(/WRITTEN TO THE VIRTUAL FM-1: 14 RECORDS, 0 REMOVED · THE WHOLE USER AREA WRITTEN ANEW$/.test(s || ""), "whole: written, the status line says so: " + s);
    await shot("02-written");
    /* READ (the virtual FM-1 as it is): exactly the panel */
    const same = await evaluate(`(async () => {
        const before = ED.model.toRecords(ED.app.st.R).map(r => r.kind + "." + r.num + "." + r.sub + "." + PTFM_seed.crc32(r.payload)).sort().join();
        await ED.fm1.read();
        const after = ED.model.toRecords(ED.app.st.R).map(r => r.kind + "." + r.num + "." + r.sub + "." + PTFM_seed.crc32(r.payload)).sort().join();
        return before === after && after.length > 0; })()`);
    check(same === true, "whole: READ after it finds exactly the panel");
};
