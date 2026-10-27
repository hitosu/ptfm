/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A sample's OPTIMIZE: its three levels with what each saves; SMALLEST picked and saved: the sample smaller, its
 * instruments' START, END and loop where the same sound now is, their LEVEL up by the 6 dB it lowered. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    /* a sample of a sampler on the TABLE that an instrument plays from its start to its end */
    const pick = await evaluate(`(() => {
        const L = ED.app.st.L, R = ED.rec;
        for (const o of L.filter(i => i.kind === 0x04)) {
            const s = R.unpackSampler(o.recs[0].payload);
            for (const it of L.filter(i => i.owner === o.id)) {
                const e = s.samples[it.idx], ins = s.instruments.map((x, k) => [x, k]).filter(([x]) => x && x.sample === it.idx);
                if (e && ins.length && ins.every(([x]) => x.start === 0 && x.end === e.length)) return { id: it.id, owner: o.id, idx: it.idx, length: e.length, rate: e.rate,
                    ins: ins.map(([x, k]) => ({ k, level: x.level, end: x.end, loop: x.loop })) };
            }
        }
        return null;
    })()`);
    check(!!pick, "optimize: a sample its instruments play whole: " + JSON.stringify(pick && { idx: pick.idx, length: pick.length, rate: pick.rate, ins: pick.ins.length }));
    if (!pick) return;
    await evaluate(`ED.tabs.open(ED.app.st.L.find(i => i.id === ${JSON.stringify(pick.id)})); 1`);
    await sleep(300);
    const before = await evaluate(`ED.model.size(ED.tabs.current().draft)`);
    const menu = await evaluate(`(() => { const b = [...ED.tabs.current().body.querySelectorAll("button")].find(x => x.textContent.startsWith("OPTIMIZE")); b.click();
        const m = document.querySelector(".menu"); return m ? [...m.querySelectorAll("button")].map(x => [x.textContent, x.disabled]) : null; })()`);
    check(menu && menu.length === 3 && /^LOSSLESS · /.test(menu[0][0]) && /^SMALLER · /.test(menu[1][0]) && /^SMALLEST · .*6 DB LOWER/.test(menu[2][0]) && !menu[2][1],
        "optimize: the menu, each level with what it saves: " + JSON.stringify(menu && menu.map(x => x[0])));
    await shot("01-menu");
    await evaluate(`[...document.querySelectorAll(".menu button")][2].click(); 1`);
    await sleep(300);
    const after = await evaluate(`ED.model.size(ED.tabs.current().draft)`);
    check(after < before, "optimize: SMALLEST put in the draft: " + before + " -> " + after + " B");
    await shot("02-smallest");
    const saved = await evaluate(`(() => { const t = ED.tabs.current(); t.saveBtn.click(); const R = ED.rec, L = ED.app.st.L;
        const o = L.find(i => i.id === ${JSON.stringify(pick.owner)}), s = R.unpackSampler(o.recs[0].payload), e = s.samples[${pick.idx}];
        return { open: ED.tabs.list.includes(t), length: e.length, rate: e.rate, ins: ${JSON.stringify(pick.ins.map(x => x.k))}.map(k => ({ level: s.instruments[k].level, start: s.instruments[k].start, end: s.instruments[k].end })) }; })()`);
    check(!saved.open && saved.ins.every((x, i) => x.level === Math.min(127, pick.ins[i].level + 12) && x.start === 0 && Math.abs(x.end - saved.length) <= 1),
        "optimize: saved: its instruments' LEVEL up 6 dB, END at the sample's new end (" + JSON.stringify(saved) + ")");
    /* OPTIMIZE SAMPLES from the TABLE's menu: a sampler picked, the dialog's three levels, SMALLER applied, UNDO back */
    const many = await evaluate(`(() => {
        const L = ED.app.st.L, c = L.filter(i => i.kind === 0x04).find(o => L.filter(s => s.owner === o.id).length > 1);
        ED.files.selectIds("L", [c.id]);
        const row = document.querySelector('.panel[data-side=L] [data-id="' + c.id + '"]'), r = row.getBoundingClientRect();
        row.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: r.left + 20, clientY: r.top + 5 }));
        const b = [...document.querySelectorAll(".menu button")].find(x => /^OPTIMIZE /.test(x.textContent));
        const label = b && b.textContent, off = b && b.disabled;
        if (b) b.click();
        return { id: c.id, label, off, n: L.filter(s => s.owner === c.id).length };
    })()`);
    check(many.label === "OPTIMIZE " + many.n + " SAMPLES…" && !many.off, "optimize: the TABLE's menu offers " + JSON.stringify(many.label));
    await sleep(600);
    const dlg = await evaluate(`(() => { const d = document.querySelector("#dlg .dlg"); return d ? { title: d.querySelector("h3").textContent,
        levels: [...d.querySelectorAll("label.sw")].map(x => [x.textContent, x.querySelector("input").checked, x.querySelector("input").disabled]) } : null; })()`);
    check(dlg && dlg.title === "OPTIMIZE " + many.n + " SAMPLES" && dlg.levels.length === 3 && /^LOSSLESS · /.test(dlg.levels[0][0]) && /^SMALLEST · .*→/.test(dlg.levels[2][0]) && !dlg.levels[2][2],
        "optimize: the dialog, each level with what it saves: " + JSON.stringify(dlg && dlg.levels.map(x => x[0].slice(0, 60))));
    await shot("03-many");
    const done1 = await evaluate(`(() => {
        const size = () => ED.app.st.L.filter(s => s.owner === ${JSON.stringify(many.id)}).reduce((a, s) => a + ED.model.size(s), 0), before = size();
        const lv = [...document.querySelectorAll("#dlg label.sw input")]; lv[2].click(); lv[2].dispatchEvent(new Event("change"));
        [...document.querySelectorAll("#dlg .btn")].find(b => b.textContent === "OPTIMIZE").click();
        const after = size(), msg = document.querySelector("#status").textContent;
        ED.app.undo();
        return { before, after, back: size(), msg };
    })()`);
    check(done1.after < done1.before && done1.back === done1.before && /^OPTIMIZED \d+ SAMPLES? \(SMALLEST\)/.test(done1.msg),
        "optimize: SMALLEST applied to the sampler's samples, one step: " + JSON.stringify(done1));
};
