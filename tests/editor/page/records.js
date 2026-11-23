/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* WRITE past the FM-1's list of records (specs/editor.md, WRITE; specs/files/store.md#the-list-of-records): a disk of 561
 * records (the demo disk's 121, cartridges, patterns and songs made of its own) uploaded to the FM-1 panel goes in all
 * the same; WRITE refuses it before anything is written, the status line saying why; with a song less (560) it is
 * written. */
"use strict";
const fs = require("fs");
module.exports = async api => {
    const { evaluate, check, shot, until } = api;
    const S = require(api.file("fw/web/seed.js"));
    const files = S.parseData(new Uint8Array(fs.readFileSync(api.file("dist/firmware/demo.ptfmdsk")))).files;
    const cart = k => k >= 1 && k <= 7, key = (kind, num) => (cart(kind) ? "C" : kind) + "." + num;
    const has = new Set(files.map(f => key(f.kind, f.num))), made = [];
    const fus = files.find(f => f.kind === 5), song = files.find(f => f.kind === 0x21 && f.num === 0xA0);
    const pat = files.filter(f => f.kind === 0x20).reduce((a, f) => f.payload.length < a.payload.length ? f : a);
    const add = (f, num) => { if (!has.has(key(f.kind, num))) { has.add(key(f.kind, num)); made.push(Object.assign({}, f, { num })); } };
    for (let n = 0; n <= 0xEF; n++) { add(fus, n); add(pat, n); }
    for (let n = 0; files.length + made.length < 561; n++) add(song, n);
    const disk = S.makeDisk(files.concat(made).map(f => ({ kind: f.kind, num: f.num, sub: f.sub, ver: f.ver, payload: f.payload })));
    await api.fresh();
    const lastSave = `(async () => { const c = await ED.fm1.connect(); try { return (await c.session.hello()).lastSave; } finally { c.close(); } })()`;
    const live = `(async () => { const c = await ED.fm1.connect(); try { return (await c.session.hello()).records.filter(r => !r.deleted).length; } finally { c.close(); } })()`;
    const msg = "((document.querySelector('#status .smsg') || document.querySelector('#status')).textContent)";
    const before = await evaluate(lastSave);
    await evaluate(`(() => { const b = Uint8Array.from(atob(${JSON.stringify(Buffer.from(disk).toString("base64"))}), c => c.charCodeAt(0));
        ED.fm1.uploadDisk(new File([b], "big.ptfmdsk")); return 1; })()`);
    const up = await until(`(() => { const s = ${msg}; return /UPLOAD DISK/.test(s) && s; })()`, 30);
    const n = await evaluate("ED.model.toRecords(ED.app.st.R).length");
    check(n === 561 && /^UPLOAD DISK: BIG\.PTFMDSK IN THE FM-1 PANEL, \d+ ITEMS/.test(up || ""), "records: a disk of 561 records in the FM-1 panel (" + n + "): " + up);
    await evaluate("ED.fm1.write(); 1");
    const s = await until(`(() => { const s = ${msg}; return /REFUSED|WRITTEN TO|COULD NOT/.test(s) && s; })()`, 60);
    const after = await evaluate(lastSave);
    check(/WRITE REFUSED: IT DOES NOT FIT IN THE FM-1'S LIST OF RECORDS: 561 RECORDS, 560 AT MOST$/.test(s || "") && after === before &&
          !await evaluate("!!document.querySelector('#dlg') && !document.querySelector('#dlg').hidden"),
          "records: WRITE refused before anything is written (the FM-1's last save " + after + ", " + before + " before), no dialog: " + s);
    await shot("01-refused");
    /* a song less: 560, written */
    await evaluate("ED.app.set(Object.assign({}, ED.app.st, { R: ED.app.st.R.filter(i => !(i.kind === 0x21 && i.num === 0)) })); ED.fm1.write(); 1");
    const d = await until("(() => { const d = document.querySelector('#dlg'); return d && !d.hidden && /WRITE TO THE FM-1/.test(d.innerText) && d.innerText.replace(/\\n+/g, ' | '); })()", 60);
    const go = await evaluate(`(() => { const b = [...document.querySelectorAll("#dlg .btn")].find(x => x.textContent === "WRITE ANYWAY" || x.textContent === "WRITE");
        if (b) b.click(); return b ? b.textContent : ""; })()`);
    const w = await until(`(() => { const s = ${msg}; return s !== ${JSON.stringify(s)} && /WRITTEN TO|REFUSED|COULD NOT/.test(s) && s; })()`, 180);
    const n2 = await evaluate(live);
    check(!!d && !!go && /WRITTEN TO THE VIRTUAL FM-1: \d+ RECORDS/.test(w || "") && n2 === 560, "records: a song less, 560: written (" + go +
          "; the FM-1 holds " + n2 + "): " + w);
    await shot("02-written");
};
