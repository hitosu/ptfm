/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The LOOPS tab's SHIFT (specs/editor.md): the length shown, each loop's SHIFT shown signed the nearer way round, an
 * entry of `-3` saved as the stored 13 over 16 rows, the hint saying how it plays, an untouched SHIFT past the length
 * kept byte for byte, an entry that is not a SHIFT refused. */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    await evaluate(`(() => {
        const R = ED.rec, L = R.unpackLoops(R.loopsV3([(() => { const b = new Uint8Array(72); b.set([125, 0, 6, 50, 0, 0, 0, 0]); return b; })()]));
        L.length = 16;
        const lp = shift => ({ cart: 0xF8, slot: 0, colour: 0, mono: 0, muted: false, lanes: 1, base: 16, repeat: 1, shift, mixer: R.MIX_DEFAULT.slice(),
            cells: new Map([[R.cellKey(0, 0), { note: 60, ins: -1, vol: -1, cmds: [] }]]) });
        L.loops = [lp(3), lp(12)];
        const it = ED.model.fromRecords(R.packLoops(L).map((payload, sub) => ({ kind: 0x22, num: 0, sub, ver: 4, payload })), { side: "R" }).items[0];
        ED.app.commit(Object.assign({}, ED.app.st, { R: [it] }), "LOOPS");
        window.LT = { it, inputs: () => [...ED.tabs.current().body.querySelectorAll(".looprow input.mono")],
            hint: () => (ED.tabs.current().body.querySelector(".shifthint") || {}).textContent };
        ED.tabs.open(ED.app.st.R.find(i => i.kind === 0x22));
        return 1; })()`);
    const step = async (name, js, ok) => {
        let r;
        try { r = await evaluate(js); } catch (e) { r = "FAILED " + e.message; }
        check(ok(r), "loops shift: " + name + ": " + JSON.stringify(r));
    };
    await step("shown signed, the length and the hint", `(() => [LT.inputs().map(i => i.value).join(), LT.hint(),
        /THE LENGTH 16 ROWS/.test(ED.tabs.current().body.textContent)])()`,
        r => r[0] === "+3,-4" && /^SHIFT \+3: PLAYS 3 ROWS LATER/.test(r[1]) && r[2] === true);
    await step("-3 entered on loop 0: saved as 13", `(() => {
        const i = LT.inputs()[0]; i.value = "-3"; i.dispatchEvent(new Event("change"));
        const t = ED.tabs.current(), d = ED.rec.unpackLoops(t.draft.recs.map(r => r.payload));
        return [d.loops.map(l => l.shift).join(), LT.inputs().map(x => x.value).join(), LT.hint()];
    })()`, r => r[0] === "13,12" && r[1] === "-3,-4" && /^SHIFT -3: PLAYS 3 ROWS EARLIER/.test(r[2]));
    await step("a refused entry changes nothing", `(() => {
        const i = LT.inputs()[1]; i.value = "99"; i.dispatchEvent(new Event("change"));
        const d = ED.rec.unpackLoops(ED.tabs.current().draft.recs.map(r => r.payload));
        return [d.loops.map(l => l.shift).join(), LT.inputs().map(x => x.value).join()];
    })()`, r => r[0] === "13,12" && r[1] === "-3,-4");
    await shot("01-shift");
};
