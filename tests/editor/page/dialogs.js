/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The dialogs: DELETE of an item something uses (what uses it, to the end, in the same dialog; DELETE ALL; UNDO),
 * DELETE with ALSO WHAT ONLY IT USES, the whole TABLE moved to the FM-1 panel, a settings item replacing the panel's
 * (REPLACE, UNDO). */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.transfer("L", [s.id]); return 1; })()`);
    /* a pattern the song plays */
    await evaluate(`(() => { const p = ED.app.st.R.find(i => i.kind === 0x20); ED.files.askDelete("R", [p.id]); return 1; })()`);
    let d = await api.dialog();
    check(/^DELETE PATTERN 00 AND WHAT USES IT\?/.test(d) && /WHAT USES IT, AND WHAT USES THAT, GOES TOO: 1 SONG\./.test(d) &&
        /SONG 00 PUNCH IN USES PATTERN 00/.test(d) && /DELETE ALL \| CANCEL$/.test(d), "dialogs: a used pattern, its song named, DELETE ALL: " + d);
    await shot("01-used");
    await api.button("CANCEL");
    /* a sample an instrument plays, on the TABLE: its sampler, the patterns that play it, the songs that play those */
    const chain = await evaluate(`(() => {
        const L = ED.app.st.L, s = L.find(x => x.kind === 0x10 && x.owner && ED.model.usersAll(L, [x.id]).ids.length >= 3);
        if (!s) return null;
        const us = ED.model.usersAll(L, [s.id]);
        ED.files.askDelete("L", [s.id]);
        return { id: s.id, users: us.ids, kinds: [...new Set(us.ids.map(id => L.find(i => i.id === id).kind))].sort((a, b) => a - b) };
    })()`);
    d = await api.dialog();
    check(chain && chain.kinds.includes(0x04) && chain.kinds.includes(0x20) && chain.kinds.includes(0x21) &&
        /AND WHAT USES IT\?/.test(d) && /\d+ SONGS/.test(d), "dialogs: a played sample: its sampler, patterns and songs in one dialog: " + d.slice(0, 200));
    await shot("02-chain");
    await api.button("DELETE ALL");
    const left = await evaluate(`${JSON.stringify(chain ? [chain.id].concat(chain.users) : [])}.filter(id => ED.app.st.L.some(i => i.id === id)).length`);
    await evaluate("ED.app.undo(); 1");
    const back = await evaluate(`${JSON.stringify(chain ? [chain.id].concat(chain.users) : [])}.every(id => ED.app.st.L.some(i => i.id === id))`);
    check(left === 0 && back, "dialogs: DELETE ALL took all " + (chain ? chain.users.length + 1 : 0) + " (left " + left + "); UNDO brought them back");
    /* the song: nothing uses it; what only it uses offered */
    await evaluate(`(() => { const s = ED.app.st.R.find(i => i.kind === 0x21); ED.files.askDelete("R", [s.id]); return 1; })()`);
    d = await api.dialog();
    check(/^DELETE SONG 00 PUNCH IN\?/.test(d) && /ALSO WHAT ONLY IT USES/.test(d) && /ALSO GOES: 6 PATTERNS, 1 SAMPLER, 6 SAMPLES · FREES \d+ KB/.test(d) &&
        /DELETE \| CANCEL$/.test(d), "dialogs: a song nothing uses: ALSO WHAT ONLY IT USES: " + d);
    await shot("03-delete");
    await api.button("CANCEL");
    /* the whole TABLE to the FM-1 */
    await evaluate(`(() => { const ids = ED.app.st.L.filter(i => !i.owner).map(i => i.id); ED.files.transfer("L", ids); return 1; })()`);
    const st = await api.status();
    check(/^→ FM-1: \d+ ITEMS · \+ \d+ SAMPLES · \d+ ITEMS? ALREADY THERE/.test(st), "dialogs: the whole TABLE moved, what is there already reused: " + st);
    /* a settings item on the TABLE over the FM-1 panel's */
    const first = await evaluate(`(() => {
        const mk = v => { const b = new Uint8Array(133); b.set(ED.defs.newSettings()); b[2] = v; return { id: ED.model.newId(), side: "L", kind: 0x30, num: null, orig: 0, link: {}, recs: [{ sub: 0, ver: 1, payload: b }], src: "MINE.PTFMDSK" }; };
        const a = mk(1), b = mk(2);
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([a, b]) }), "TWO SETTINGS");
        ED.files.transfer("L", [a.id]);
        const first = document.querySelector("#status").textContent;
        ED.files.transfer("L", [b.id]);
        return first;
    })()`);
    d = await api.dialog();
    check(/→ FM-1: SETTINGS/.test(first) && /^REPLACE ON THE FM-1 PANEL\?/.test(d) && /REPLACE \| CANCEL$/.test(d), "dialogs: settings moved, then a second asks to replace: " + d);
    await shot("04-replace");
    await api.button("REPLACE");
    const theme = () => evaluate(`ED.app.st.R.find(i => i.kind === 0x30).recs[0].payload[2]`);
    const t2 = await theme();
    await evaluate("ED.app.undo(); 1");
    const t1 = await theme();
    check(t2 === 2 && t1 === 1, "dialogs: REPLACE took the second's THEME (" + t2 + "), UNDO the first's back (" + t1 + ")");
    /* the right-click menu */
    const menu = await evaluate(`(() => { const row = document.querySelector(".panel[data-side=R] [data-id]");
        row.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, clientX: 900, clientY: 300 }));
        const m = document.querySelector(".menu"); const t = m ? [...m.querySelectorAll("button")].map(b => b.textContent).join(" | ") : ""; ED.ui.closeMenu(); return t; })()`);
    check(/DELETE/.test(menu) && /TABLE/.test(menu), "dialogs: an item's right-click menu: " + menu);
};
