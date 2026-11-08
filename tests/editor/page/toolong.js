/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A sample too long for the FM-1 (specs/editor.md, Too long for the FM-1): a take of 31.2 s on the TABLE, its size
 * TOO LONG and its picture drawn; ▶ through the page's own player; a move to the FM-1 panel refused with its length;
 * its tab saying how long it is; TRIM making it fit, SAVE, and the move then taken; back over a reload. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    const id = await evaluate(`(() => {
        const n = Math.round(31.2 * 44100), x = new Float32Array(n);
        for (let i = 0; i < n; i++) x[i] = 0.4 * Math.sin(2 * Math.PI * 220 * i / 44100) * (i % 44100 < 22050 ? 1 : 0.2);
        const r = ED.cutting.addSamples(ED.app.st, "L", [{ snd: { rate: 44100, ch: [x], bits: 16 }, how: { chan: "mix", rate: 44100, bits: 16 }, name: "TAKE" }], "RECORDING");
        ED.app.commit(r.st, "TEST: A LONG TAKE", { ok: true, select: { side: "L", ids: r.ids } });
        return r.ids[0]; })()`);
    await sleep(300);
    const row = await evaluate(`(() => { const r = document.querySelector('.panel[data-side=L] [data-id="${id}"]'); return r ? [r.querySelector(".c-size").textContent, r.querySelector(".c-kind").textContent] : null; })()`);
    check(row && row[0] === "TOO LONG" && row[1] === "16-BIT · 44.1 KHZ · 31.20 S", "too long: the TABLE's row: " + JSON.stringify(row));
    const sel = await evaluate(`(() => { const s = document.querySelector(".panel[data-side=L] .selbar .sum"); return s && s.textContent; })()`);
    check(/TOO LONG FOR THE FM-1: 31\.2 S, 23\.8 S AT MOST/.test(sel || ""), "too long: picked, the bar says how long: " + sel);
    await shot("01-row");
    /* ▶: the page's own player */
    await evaluate(`ED.player.play(ED.app.st.L.find(i => i.id === "${id}")); 1`);
    await sleep(300);
    const own = await evaluate(`[ED.player.now && ED.player.now.job.kind, document.querySelector("#pbtn").textContent]`);
    await evaluate(`ED.player.stop(); 1`);
    check(own[0] === "own" && own[1] === "■", "too long: ▶ plays it through the page's own player: " + JSON.stringify(own));
    /* the move refused, with its length; the FM-1 panel as it was */
    const r0 = await evaluate(`ED.app.st.R.length`);
    const mv = await evaluate(`(() => { const ok = ED.files.transfer("L", ["${id}"]); return [ok, document.querySelector("#status").textContent, ED.app.st.R.length]; })()`);
    check(mv[0] === false && /^TOO LONG FOR THE FM-1: 31\.2 S, 23\.8 S AT MOST/.test(mv[1]) && mv[2] === r0, "too long: the move to the FM-1 panel refused: " + JSON.stringify(mv));
    /* its tab: how long it is, what makes it fit; a drag over the first third, TRIM, SAVE */
    await evaluate(`ED.tabs.open(ED.app.st.L.find(i => i.id === "${id}")); 1`);
    await sleep(400);
    const said = await evaluate(`(() => { const b = ED.tabs.current().body.querySelector(".sinfo .bad"); return b ? b.textContent : null; })()`);
    check(said && /TOO LONG FOR THE FM-1: 31\.2 S, 23\.8 S AT MOST/.test(said) && /MAKES IT FIT/.test(said), "too long: its tab says so: " + said);
    await shot("02-tab");
    await evaluate(`(() => { const cv = ED.tabs.current().body.querySelector(".wavebox canvas"), r = cv.getBoundingClientRect();
        const ev = (t, f) => cv.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: r.left + r.width * f, clientY: r.top + 20, pointerId: 1 }));
        ev("pointerdown", 0.01); ev("pointermove", 0.15); ev("pointermove", 0.3); ev("pointerup", 0.3); return 1; })()`);
    await sleep(200);
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "TRIM").click(); 1`);
    await sleep(300);
    const fit = await evaluate(`(() => { const d = ED.tabs.current().draft, e = ED.model.sampleEntry(d, ED.app.st.L);
        return [ED.model.isLong(d), e.length, !!ED.tabs.current().body.querySelector(".sinfo .bad")]; })()`);
    check(fit[0] === false && fit[1] > 8 * 44100 && fit[1] < 10 * 44100 && !fit[2], "too long: TRIM to about 9 s makes it fit: " + JSON.stringify(fit));
    await evaluate(`ED.tabs.current().saveBtn.click(); 1`);
    await sleep(300);
    const saved = await evaluate(`(() => { const it = ED.app.st.L.find(i => i.id === "${id}"); return [ED.model.isLong(it), it.recs.length, document.querySelector('.panel[data-side=L] [data-id="${id}"] .c-size').textContent]; })()`);
    check(saved[0] === false && saved[1] === 1 && / KB$/.test(saved[2]), "too long: SAVE: a sample on the TABLE, its size in KB: " + JSON.stringify(saved));
    const mv2 = await evaluate(`(() => { const ok = ED.files.transfer("L", ["${id}"]); return [ok, ED.app.st.R.some(i => i.kind === 0x11)]; })()`);
    check(mv2[0] === true && mv2[1], "too long: once it fits, the move is taken: " + JSON.stringify(mv2));
    await shot("03-fits");
    /* a long one over a reload: kept, its original back */
    await evaluate(`(() => {
        const n = Math.round(25 * 44100), x = new Float32Array(n);
        for (let i = 0; i < n; i++) x[i] = 0.3 * Math.sin(i / 9);
        const r = ED.cutting.addSamples(ED.app.st, "L", [{ snd: { rate: 44100, ch: [x], bits: 16 }, how: { chan: "mix", rate: 44100, bits: 16 }, name: "TAKE" }], "RECORDING");
        ED.app.commit(r.st, "TEST: ANOTHER", { ok: true }); return ED.keep.flush(); })()`);
    await api.send("Page.reload", {});
    await api.until("!!document.querySelector('#status') && /BACK AS YOU LEFT IT/.test(document.querySelector('#status').textContent)", 30);
    const back = await evaluate(`(() => { const L = ED.app.st.L, it = L[L.length - 1]; return [ED.model.isLong(it), it.long, !!(it.sound && it.sound.ch && it.sound.ch[0].length === ${Math.round(25 * 44100)})]; })()`);
    check(back[0] && back[1] === Math.round(25 * 44100) && back[2], "too long: over a reload, kept with its original: " + JSON.stringify(back));
};
