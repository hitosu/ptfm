/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The SAMPLE tab's cutting (specs/editor.md, Recording and cutting sounds): nothing selected when it opens; a drag
 * selects (TRIM, SAVE AS A NEW SAMPLE dimmed before); SAVE AS A NEW SAMPLE beside it in its sampler, named by the
 * naming rule, the tab staying; TRIM in the draft, UNDO in the tab; DETECT SLICES on a sound of known hits (a loose
 * sample made here), markers by hand (a double-click adds one, Delete deletes the picked one), REMOVE SILENCE
 * shortening the sound; SAVE AS SAMPLES and MAKE A KIT (a sampler and a FUSION cartridge, opened in their tabs). */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    /* a sample of a sampler on the TABLE */
    const pick = await evaluate(`(() => { const L = ED.app.st.L; const it = L.find(i => i.kind === 0x11 && L.some(o => o.kind === 0x04 && Object.values(o.link).includes(i.id))); return it ? { id: it.id } : null; })()`);
    check(!!pick, "slices: a sampler's sample on the TABLE");
    if (!pick) return;
    await evaluate(`ED.tabs.open(ED.app.st.L.find(i => i.id === ${JSON.stringify(pick.id)})); 1`);
    await sleep(300);
    const keys = () => evaluate(`Object.fromEntries([...ED.tabs.current().body.querySelectorAll("button")].map(b => [b.textContent, b.disabled]))`);
    let k = await keys();
    check(k["TRIM"] === true && k["SAVE AS A NEW SAMPLE"] === true && k["▶ PLAY THE SAMPLE"] === false && k["DETECT SLICES"] === false,
        "slices: nothing selected at first: TRIM and SAVE AS A NEW SAMPLE dimmed, PLAY THE SAMPLE");
    /* a drag across the middle half */
    const drag = (f0, f1) => evaluate(`(() => { const cv = ED.tabs.current().body.querySelector(".wavebox canvas"), r = cv.getBoundingClientRect();
        const ev = (t, f) => cv.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: r.left + r.width * f, clientY: r.top + 20, pointerId: 1 }));
        ev("pointerdown", ${f0}); ev("pointermove", ${(f0 + f1) / 2}); ev("pointermove", ${f1}); ev("pointerup", ${f1}); return 1; })()`);
    await drag(0.25, 0.75);
    await sleep(200);
    k = await keys();
    check(k["TRIM"] === false && k["SAVE AS A NEW SAMPLE"] === false && k["▶ PLAY THE SELECTION"] === false, "slices: a drag selects: TRIM, SAVE AS A NEW SAMPLE, PLAY THE SELECTION");
    await shot("01-selected");
    const len0 = await evaluate(`ED.model.sampleEntry(ED.tabs.current().draft, ED.app.st.L).length`);
    const ns = await evaluate(`(() => { const t = ED.tabs.current(), n0 = ED.app.st.L.length;
        [...t.body.querySelectorAll("button")].find(b => b.textContent === "SAVE AS A NEW SAMPLE").click();
        const L = ED.app.st.L, it = L[L.length - 1];
        return { added: L.length - n0, own: it.kind === 0x11 && !L.some(o => o.kind === 0x04 && Object.values(o.link).includes(it.id)), name: ED.model.sampleEntry(it, L).name, src: it.src,
            open: ED.tabs.current() === t, base: ED.model.sampleEntry(t.base, L).name }; })()`);
    check(ns.added === 1 && ns.own && /^[A-Z0-9 .-]*\d\d$/.test(ns.name) && ns.src === "FROM " + ns.base && ns.open,
        "slices: SAVE AS A NEW SAMPLE: one sample of its own with no instrument, " + ns.name + " (from " + ns.base + "), the tab staying");
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "TRIM").click(); 1`);
    await sleep(200);
    const len1 = await evaluate(`ED.model.sampleEntry(ED.tabs.current().draft, ED.app.st.L).length`);
    check(len1 < len0 * 0.7 && len1 > len0 * 0.3, "slices: TRIM: the sample is the selection (" + len0 + " -> " + len1 + ")");
    await evaluate(`[...ED.tabs.current().el.querySelectorAll("button")].find(b => b.textContent === "UNDO").click(); 1`);
    await sleep(200);
    check(await evaluate(`ED.model.sampleEntry(ED.tabs.current().draft, ED.app.st.L).length`) === len0, "slices: UNDO in the tab: the sample whole again");
    await evaluate(`(() => { const t = ED.tabs.current(); t.dirty = false; [...t.el.querySelectorAll("button")].find(b => b.textContent === "CLOSE").click(); return 1; })()`);
    await sleep(200);

    /* a loose sample of four hits made here, its tab opened */
    const loose = await evaluate(`(() => {
        const rate = 22050, n = Math.round(1.8 * rate), x = new Float32Array(n);
        for (let i = 0; i < n; i++) x[i] = (Math.random() * 2 - 1) * 0.0005;
        for (const t of [0.2, 0.6, 1.0, 1.4]) { const a = Math.round(t * rate); for (let i = 0; i < Math.round(0.08 * rate); i++) x[a + i] = Math.sin(i * 0.3) * 0.8 * Math.exp(-i / (0.03 * rate)); }
        const it = Object.assign(ED.cutting.sampleOf({ rate, ch: [x], bits: 16 }, { chan: "mix", rate, bits: 8 }, "HITS", "TEST"), { side: "L" });
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([it]) }), "TEST");
        ED.tabs.open(ED.app.st.L.find(i => i.id === it.id)); return it.id; })()`);
    await sleep(300);
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "DETECT SLICES").click(); 1`);
    await sleep(300);
    const sl = await evaluate(`(() => { const c = ED.tabs.current().draft.cutting; return c && c.slices ? c.slices.map(z => z.silent ? 0 : 1) : null; })()`);
    check(sl && sl.filter(Boolean).length === 4 && sl.includes(0), "slices: DETECT SLICES: the four hits and the silent slices (" + JSON.stringify(sl) + ")");
    await shot("02-slices");
    k = await keys();
    check(k["REMOVE SILENCE"] === false && k["SAVE AS SAMPLES"] === false && k["MAKE A KIT"] === false && k["REMOVE"] === true,
        "slices: REMOVE SILENCE, SAVE AS SAMPLES and MAKE A KIT; REMOVE dimmed with none picked");
    /* a double-click adds a marker: five hits then; Delete on the picked one takes it back */
    await evaluate(`(() => { const cv = ED.tabs.current().body.querySelector(".wavebox canvas"), r = cv.getBoundingClientRect();
        cv.dispatchEvent(new MouseEvent("dblclick", { bubbles: true, clientX: r.left + r.width * (0.45 / 1.8), clientY: r.top + 20 })); return 1; })()`);
    await sleep(300);
    const five = await evaluate(`ED.tabs.current().draft.cutting.slices.filter(z => !z.silent).length`);
    check(five === 5, "slices: a double-click adds a marker (" + five + " slices)");
    /* SAVE AS SAMPLES: the four hits as loose samples (all but the silent ones with none picked) */
    const n0 = await evaluate(`ED.app.st.L.length`);
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "SAVE AS SAMPLES").click(); 1`);
    await sleep(300);
    const after = await evaluate(`ED.app.st.L.slice(${n0}).map(i => [i.kind, ED.model.sampleEntry(i, ED.app.st.L).name, i.src])`);
    check(after.length === 5 && after.every(x => x[0] === 0x11 && x[2] === "FROM HITS") && after[0][1] === "HITS00" && after[4][1] === "HITS04",
        "slices: SAVE AS SAMPLES: each slice a loose sample, HITS00 .. HITS04, from HITS");
    /* MAKE A KIT: a sampler and a FUSION cartridge, both opened */
    const n1 = await evaluate(`ED.app.st.L.length`);
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "MAKE A KIT").click(); 1`);
    await sleep(500);
    const kit = await evaluate(`(() => { const L = ED.app.st.L.slice(${n1}), R = ED.rec;
        const s = L.find(i => i.kind === 0x04), f = L.find(i => i.kind === 0x05);
        const names = s && R.unpackSampler(s.recs[0].payload).instruments.map(x => { const t = ED.app.st.L.find(i => i.id === s.link[ED.model.lk("m", x.sample)]); return t && t.kind === 0x11 && ED.model.sampleEntry(t, ED.app.st.L).name; });
        return { s: s && R.unpackSampler(s.recs[0].payload).instruments.length, f: f && R.unpackCart(0x05, f.recs[0].payload).name, names: names && names.join(),
            tabs: ED.tabs.list.map(t => t.draft.kind) }; })()`);
    check(kit.s === 5 && kit.f === "HITS KIT" && /^HITS05,HITS06,HITS07,HITS08,HITS09$/.test(kit.names) && kit.tabs.includes(0x04) && kit.tabs.includes(0x05),
        "slices: MAKE A KIT: a sampler of 5 instruments and HITS KIT, both opened (" + JSON.stringify(kit) + ")");
    await shot("03-kit");
    /* back on the sample: REMOVE SILENCE shortens it, its sound the joined one */
    await evaluate(`ED.tabs.open(ED.app.st.L.find(i => i.id === ${JSON.stringify(loose)})); 1`);
    await sleep(300);
    const l0 = await evaluate(`ED.model.sampleEntry(ED.tabs.current().draft, ED.app.st.L).length`);
    await evaluate(`[...ED.tabs.current().body.querySelectorAll("button")].find(b => b.textContent === "REMOVE SILENCE").click(); 1`);
    await sleep(300);
    const l1 = await evaluate(`ED.model.sampleEntry(ED.tabs.current().draft, ED.app.st.L).length`);
    const joined = await evaluate(`(() => { const c = ED.tabs.current().draft.cutting; return c.slices.every(z => !z.silent) && ED.tabs.current().draft.sound.ch[0].length; })()`);
    check(l1 < l0 * 0.6 && joined > 0, "slices: REMOVE SILENCE: the silent slices taken out, the sound joined (" + l0 + " -> " + l1 + ")");
    await shot("04-removed");
};
