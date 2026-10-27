/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* WRITE to an FM-1 that reads only an older version of the loops (the virtual FM-1's answer changed for it here): the
 * panel's loops, one soloed, converted down, SOLO named, WRITE ANYWAY; CANCEL writes nothing. */
module.exports = async api => {
    const { evaluate, check, shot, until } = api;
    await api.fresh();
    await evaluate(`(() => {
        const R = ED.rec, L = R.unpackLoops(R.loopsV3([(() => { const b = new Uint8Array(72); b.set([125, 0, 6, 50, 0, 0, 0, 0]); return b; })()]));
        L.length = 16;
        L.loops = [{ cart: 0xF8, slot: 0, colour: 0, mono: 0, muted: false, soloed: true, lanes: 1, base: 16, repeat: 1, mixer: R.MIX_DEFAULT.slice(),
            cells: new Map([[R.cellKey(0, 0), { note: 60, ins: -1, vol: -1, cmds: [] }]]) }];
        const it = ED.model.fromRecords(R.packLoops(L).map((payload, sub) => ({ kind: 0x22, num: 0, sub, ver: 4, payload })), { side: "R" }).items[0];
        ED.app.commit(Object.assign({}, ED.app.st, { R: [it] }), "LOOPS");
        const connect = ED.fm1.connect;
        ED.fm1.connect = async () => { const c = await connect(); const hello = c.session.hello.bind(c.session);
            c.session.hello = async () => { const l = await hello(); l.accepts = l.accepts.map(a => a.kind === 0x22 ? { kind: 0x22, ver: 3 } : a); return l; };
            return c; };
        ED.fm1.write();
        return 1; })()`);
    const d = await until("(() => { const d = document.querySelector('#dlg'); return d && !d.hidden && /WRITE TO THE FM-1/.test(d.innerText) && d.innerText.replace(/\\n+/g, ' | '); })()", 60);
    check(/CONVERTED FOR THE FM-1'S VERSION \| LOOPS 00: VERSION 4 WRITTEN AS VERSION 3 · NOT KEPT: SOLO/.test(d || "") && /WRITE ANYWAY \| CANCEL$/.test(d || ""),
        "down: the loops converted for the FM-1, SOLO named, WRITE ANYWAY: " + d);
    await shot("01-dialog");
    await api.button("CANCEL");
    const s = await until("(() => { const s = document.querySelector('#status').textContent; return /NOTHING WRITTEN/.test(s) && s; })()", 20);
    check(!!s, "down: CANCEL writes nothing");
};
