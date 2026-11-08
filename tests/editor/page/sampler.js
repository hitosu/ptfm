/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A sampler's tab on the independent samples (specs/editor.md, The tabs: Cartridges, Many instruments at once): its
 * instruments name any sample on their panel (SAMPLE lists them by number and name), REPLACE SAMPLE (every value a new
 * instrument's), CREATE on an empty slot, DELETE (the instrument goes, its sample stays; one step of the tab's UNDO), THE
 * SAMPLES IT USES with NEW INSTRUMENT FOR IT; PASTE bringing a sample the panel lacks (added at SAVE, at a free number) and
 * refused with none free; the sample tab's PLAYED BY across samplers. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`(() => { const r = ED.model.transfer(ED.app.st, "L", ED.app.st.L.map(i => i.id), {}); ED.app.commit(r.st, "TEST"); return 1; })()`);
    await sleep(300);
    const pick = await evaluate(`(() => { const R = ED.app.st.R, S = R.filter(i => i.kind === 0x04).sort((a, b) => b.recs[0].payload[9] - a.recs[0].payload[9])[0];
        return { id: S.id, n: ED.rec.unpackSampler(S.recs[0].payload).instruments.length, samples: R.filter(i => i.kind === 0x11).length }; })()`);
    const tab = () => `ED.tabs.current()`;
    const draft = () => evaluate(`(() => { const c = ED.rec.unpackSampler(${tab()}.draft.recs[0].payload); return { n: c.instruments.filter(Boolean).length,
        len: c.instruments.length, inst: c.instruments.map(x => x && { name: x.name, sample: x.sample, level: x.level, start: x.start, end: x.end, loop: x.loop, base: x.base_note, tune: x.tune }) }; })()`);
    const btn = l => evaluate(`(() => { const b = [...${tab()}.body.querySelectorAll("button")].find(x => x.textContent === ${JSON.stringify(l)}); if (b && !b.disabled) b.click(); return !!b && !b.disabled; })()`);
    const row = i => `${tab()}.body.querySelectorAll(".slots .slotrow")[${i}]`;
    await evaluate(`ED.tabs.open(ED.app.st.R.find(i => i.id === ${JSON.stringify(pick.id)})); 1`);
    await sleep(400);

    /* the instrument's page: SAMPLE lists every sample on the panel, by number and name */
    const list = await evaluate(`(() => { const f = [...${tab()}.body.querySelectorAll(".field")].find(x => x.querySelector("label").textContent === "SAMPLE");
        const s = f.querySelector("select"); return { n: s.options.length, first: s.options[0].textContent, picked: s.options[s.selectedIndex].textContent, repl: [...${tab()}.body.querySelectorAll("button")].some(b => b.textContent === "REPLACE SAMPLE") }; })()`);
    check(list.n === pick.samples && /^[0-9A-F]{2} \S/.test(list.first) && /^[0-9A-F]{2} \S/.test(list.picked) && list.repl,
        "sampler: SAMPLE offers every sample on the panel, by number and name (" + JSON.stringify(list) + ")");
    const d0 = await draft();
    const other = await evaluate(`(() => { const R = ED.app.st.R, c = ED.rec.unpackSampler(${tab()}.draft.recs[0].payload), at = ${tab()}.slot;
        const o = R.find(i => i.kind === 0x11 && i.num !== c.instruments[at].sample); return { id: o.id, num: o.num, name: ED.model.sampleEntry(o, R).name }; })()`);
    const slot0 = await evaluate(`${tab()}.slot`);
    /* SAMPLE: another sample, the other values stay */
    await evaluate(`(() => { const f = [...${tab()}.body.querySelectorAll(".field")].find(x => x.querySelector("label").textContent === "SAMPLE"), s = f.querySelector("select");
        s.value = ${JSON.stringify(other.id)}; s.dispatchEvent(new Event("change")); return 1; })()`);
    let d1 = await draft();
    check(d1.inst[slot0].sample === other.num && d1.inst[slot0].level === d0.inst[slot0].level && d1.inst[slot0].tune === d0.inst[slot0].tune && d1.inst[slot0].name === d0.inst[slot0].name,
        "sampler: SAMPLE picks any sample on the panel, the instrument's other values stay (" + other.num + ")");
    /* the tab's UNDO button takes it back, as the user would */
    await evaluate(`[...${tab()}.el.querySelectorAll("button")].find(b => b.textContent === "UNDO").click(); 1`); await sleep(200);
    const dBack = await draft();
    check(dBack.inst[slot0].sample === d0.inst[slot0].sample, "sampler: UNDO (the button) takes the SAMPLE change back");
    /* REPLACE SAMPLE: a pick; every value a new instrument's, the name following when it was the old sample's */
    await btn("REPLACE SAMPLE"); await sleep(200);
    const dlg = await api.dialog();
    check(/^REPLACE SAMPLE: PICK A SAMPLE/.test(dlg), "sampler: REPLACE SAMPLE opens a pick of the panel's samples: " + dlg.slice(0, 60));
    await evaluate(`(() => { const s = document.querySelector("#dlg select"); s.value = ${JSON.stringify(other.id)}; s.dispatchEvent(new Event("change")); return 1; })()`);
    await api.button("REPLACE SAMPLE"); await sleep(200);
    d1 = await draft();
    const q = d1.inst[slot0];
    check(q.sample === other.num && q.name === other.name && q.level === 0 && q.tune === 128 && q.loop === 0 && q.start === 0 && q.base === 60,
        "sampler: REPLACE SAMPLE: the new sample, every value a new instrument's, named after it: " + JSON.stringify(q));
    await shot("01-replaced");

    /* DELETE: the instrument goes, the sample stays; the tab's UNDO brings it back */
    const samplesBefore = await evaluate(`ED.app.st.R.filter(i => i.kind === 0x11).length`);
    await evaluate(`${row(slot0)}.click(); 1`); await sleep(100);
    await btn("DELETE"); await sleep(200);
    if (/DELETE ANYWAY/.test(await api.dialog())) await api.button("DELETE");
    await sleep(200);
    const d2 = await draft();
    check(d2.inst[slot0] === null && d2.n === d1.n - 1, "sampler: DELETE: the instrument goes (" + d1.n + " -> " + d2.n + ")");
    await evaluate(`[...${tab()}.el.querySelectorAll("button")].find(b => b.textContent === "UNDO").click(); 1`); await sleep(200);
    const d3 = await draft();
    check(d3.n === d1.n && d3.inst[slot0] && d3.inst[slot0].sample === other.num, "sampler: one UNDO in the tab brings the instrument back");
    await btn("DELETE"); await sleep(200);
    if (/DELETE ANYWAY/.test(await api.dialog())) await api.button("DELETE");
    await sleep(200);

    /* CREATE on an empty slot: a pick of any sample, a new instrument playing it whole, named after it */
    const emptyAt = await evaluate(`${tab()}.body.querySelectorAll(".slots .slotrow.empty")[0] ? [...${tab()}.body.querySelectorAll(".slots .slotrow")].findIndex(r => r.classList.contains("empty")) : -1`);
    await evaluate(`${row(emptyAt)}.click(); 1`); await sleep(150);
    check(await btn("CREATE"), "sampler: an empty slot offers CREATE");
    await sleep(200);
    await evaluate(`(() => { const s = document.querySelector("#dlg select"); s.value = ${JSON.stringify(other.id)}; s.dispatchEvent(new Event("change")); return 1; })()`);
    await api.button("CREATE"); await sleep(200);
    const d4 = await draft(), e = d4.inst[emptyAt];
    const len = await evaluate(`ED.model.sampleEntry(ED.app.st.R.find(i => i.id === ${JSON.stringify(other.id)}), ED.app.st.R).length`);
    check(e && e.sample === other.num && e.name === other.name && e.start === 0 && e.end === len && e.loop === 0,
        "sampler: CREATE: a new instrument plays the sample whole, named after it: " + JSON.stringify(e));

    /* THE SAMPLES IT USES: each sample once, NEW INSTRUMENT FOR IT at the first empty slot */
    const card = await evaluate(`(() => { const c = [...${tab()}.body.querySelectorAll(".card")].find(x => x.querySelector("h4") && x.querySelector("h4").textContent.startsWith("THE SAMPLES IT USES"));
        return c ? { rows: c.querySelectorAll(".orow").length, btns: c.querySelectorAll("button").length } : null; })()`);
    const distinct = await evaluate(`new Set(ED.rec.unpackSampler(${tab()}.draft.recs[0].payload).instruments.filter(Boolean).map(x => x.sample)).size`);
    check(card && card.rows === distinct && card.btns === distinct, "sampler: THE SAMPLES IT USES: each sample once, NEW INSTRUMENT FOR IT on each (" + JSON.stringify(card) + " of " + distinct + ")");
    const before = await draft();
    await evaluate(`${tab()}.body.querySelector(".orow button").click(); 1`); await sleep(200);
    const after = await draft();
    check(after.n === before.n + 1, "sampler: NEW INSTRUMENT FOR IT adds an instrument (" + before.n + " -> " + after.n + ")");
    await shot("02-uses");
    // SAVE: the samples stay (DELETE touched none), nothing else changed on the panel
    await evaluate(`${tab()}.saveBtn.click(); 1`); await sleep(200);
    check(await evaluate(`ED.app.st.R.filter(i => i.kind === 0x11).length`) === samplesBefore, "sampler: SAVE: the panel's samples are as they were (" + samplesBefore + ")");

    /* PASTE: copy two instruments, take their sample off the panel, paste into a new sampler: the sample comes along
     * (added at SAVE at a free number); the same sample already there is only named */
    await evaluate(`ED.tabs.open(ED.app.st.R.find(i => i.id === ${JSON.stringify(pick.id)})); 1`); await sleep(300);
    const gone = await evaluate(`(() => { const c = ED.rec.unpackSampler(${tab()}.draft.recs[0].payload), it = ED.app.st.R, at = c.instruments.findIndex(Boolean);
        const smp = it.find(i => i.id === ${tab()}.draft.link[ED.model.lk("m", c.instruments[at].sample)]);
        return { at, id: smp.id, num: smp.num, name: ED.model.sampleEntry(smp, it).name }; })()`);
    await evaluate(`${row(gone.at)}.click(); 1`); await sleep(100);
    await btn("COPY"); await sleep(100);
    await evaluate(`(() => { const t = ${tab()}; t.dirty = false; t.closeAnyway = true; return 1; })()`);
    await evaluate(`ED.tabs.close(${tab()}); 1`); await sleep(200);
    // a new empty sampler on the FM-1 panel
    const fresh = await evaluate(`(() => { const R = ED.app.st.R, n = ED.model.pickNum(ED.model.taken(R).c, null);
        const it = { id: ED.model.newId(), side: "R", kind: 0x04, num: n, orig: null, link: {}, src: "TEST", recs: [{ sub: 0, ver: 2, payload: ED.rec.packSampler({ name: "PASTED", instruments: [] }) }] };
        ED.app.commit(Object.assign({}, ED.app.st, { R: R.concat([it]) }), "TEST"); ED.tabs.open(ED.app.st.R.find(i => i.id === it.id)); return it.id; })()`);
    await sleep(300);
    await evaluate(`${row(0)}.click(); 1`); await sleep(100);
    await btn("PASTE"); await sleep(250);
    const p1 = await draft();
    const named = await evaluate(`(() => { const d = ${tab()}.draft, c = ED.rec.unpackSampler(d.recs[0].payload), it = ED.app.st.R.find(i => i.id === d.link[ED.model.lk("m", c.instruments[0].sample)]);
        return it ? { id: it.id, pending: (d.pending || []).length } : null; })()`);
    check(p1.n === 1 && named && named.id === gone.id && named.pending === 0, "sampler: PASTE: the sample the panel holds is only named, nothing copied");
    // take the sample off the panel (its instruments go with it), paste again into a second new sampler
    await evaluate(`(() => { ED.tabs.list.slice().forEach(t => { t.dirty = false; ED.tabs.close(t); }); const r = ED.model.remove(ED.app.st, "R", [${JSON.stringify(gone.id)}], false); ED.app.commit(r.st, "TEST"); return 1; })()`);
    await sleep(200);
    const second = await evaluate(`(() => { const R = ED.app.st.R, n = ED.model.pickNum(ED.model.taken(R).c, null);
        const it = { id: ED.model.newId(), side: "R", kind: 0x04, num: n, orig: null, link: {}, src: "TEST", recs: [{ sub: 0, ver: 2, payload: ED.rec.packSampler({ name: "PASTED2", instruments: [] }) }] };
        ED.app.commit(Object.assign({}, ED.app.st, { R: R.concat([it]) }), "TEST"); ED.tabs.open(ED.app.st.R.find(i => i.id === it.id)); return it.id; })()`);
    await sleep(300);
    await evaluate(`${row(0)}.click(); 1`); await sleep(100);
    await btn("PASTE"); await sleep(250);
    const p2 = await evaluate(`(() => { const d = ${tab()}.draft, c = ED.rec.unpackSampler(d.recs[0].payload), x = c.instruments[0];
        const it = (d.pending || []).map(p => p.item).find(i => i.id === d.link[ED.model.lk("m", x.sample)]);
        return { n: c.instruments.filter(Boolean).length, pending: (d.pending || []).length, num: it && it.num, name: it && ED.model.sampleEntry(it, []).name, at: x.sample }; })()`);
    check(p2.n === 1 && p2.pending === 1 && p2.num === 0 && p2.at === 0 && p2.name === gone.name,
        "sampler: PASTE of an instrument whose sample is not on the panel: it comes along at the smallest free number (" + JSON.stringify(p2) + ")");
    // UNDO (the button) drops the brought sample with the draft: none pending, none on the panel; REDO brings it back
    const countAt = `ED.app.st.R.filter(i => i.kind === 0x11 && i.num === 0).length`;
    await evaluate(`[...${tab()}.el.querySelectorAll("button")].find(b => b.textContent === "UNDO").click(); 1`); await sleep(200);
    const undone = await evaluate(`({ pending: (${tab()}.draft.pending || []).length, onPanel: ${countAt}, n: ED.rec.unpackSampler(${tab()}.draft.recs[0].payload).instruments.length })`);
    check(undone.pending === 0 && undone.onPanel === 0 && undone.n === 0, "sampler: UNDO after a PASTE that brought a sample: no sample pending or left on the panel: " + JSON.stringify(undone));
    await evaluate(`[...${tab()}.el.querySelectorAll("button")].find(b => b.textContent === "REDO").click(); 1`); await sleep(200);
    await evaluate(`${tab()}.saveBtn.click(); 1`); await sleep(250);
    const saved =await evaluate(`(() => { const R = ED.app.st.R, s = R.find(i => i.id === ${JSON.stringify(second)}), c = ED.rec.unpackSampler(s.recs[0].payload), it = R.find(i => i.id === s.link[ED.model.lk("m", c.instruments[0].sample)]);
        return { has: !!it && it.kind === 0x11, num: it && it.num, taken: R.filter(i => i.kind === 0x11 && i.num === 0).length, st: document.querySelector("#status").textContent }; })()`);
    check(saved.has && saved.num === 0 && saved.taken === 1 && /1 SAMPLE ADDED/.test(saved.st), "sampler: SAVE adds the sample as an item of its own: " + JSON.stringify(saved));
    // none free: all 256 numbers taken, the paste refused, nothing pasted
    await evaluate(`(() => { ED.tabs.list.slice().forEach(t => { t.dirty = false; ED.tabs.close(t); });
        const R = ED.app.st.R, taken = new Set(R.filter(i => i.kind === 0x11).map(i => i.num)), add = [];
        for (let n = 0; n < 256; n++) if (!taken.has(n)) add.push({ id: ED.model.newId(), side: "R", kind: 0x11, num: n, orig: n, link: {}, src: "TEST", recs: [{ sub: 0, ver: 1,
            payload: ED.rec.packSample({ length: 1, rate: 8000, coding: 1, name: "F" + n }, ED.rec.ptrEncode(Int8Array.from([n & 127]), 8)) }] });
        const it = { id: ED.model.newId(), side: "R", kind: 0x04, num: ED.model.pickNum(ED.model.taken(R).c, null), orig: null, link: {}, src: "TEST", recs: [{ sub: 0, ver: 2, payload: ED.rec.packSampler({ name: "FULL", instruments: [] }) }] };
        ED.app.commit(Object.assign({}, ED.app.st, { R: R.concat(add, [it]) }), "TEST"); window.FULL = it.id; return add.length; })()`);
    // the clip holds an instrument of a sample the panel lacks: copy from a TABLE sampler, whose samples differ in name
    await evaluate(`(() => { const L = ED.app.st.L; const o = L.find(i => i.kind === 0x04);
        const s = L.find(i => i.id === Object.values(o.link)[0]), r = ED.rec.unpackSample(s.recs[0].payload);
        const odd = Object.assign({}, s, { id: ED.model.newId(), recs: [{ sub: 0, ver: 1, payload: ED.rec.packSample(Object.assign({}, r.entry, { name: "ODDONE" }), r.audio) }] });
        const c = ED.rec.unpackSampler(o.recs[0].payload); c.instruments = [c.instruments.find(Boolean)];
        const so = Object.assign({}, o, { id: ED.model.newId(), recs: [{ sub: 0, ver: 2, payload: ED.rec.packSampler(c) }], link: { [ED.model.lk("m", c.instruments[0].sample)]: odd.id } });
        ED.app.commit(Object.assign({}, ED.app.st, { L: L.concat([odd, so]) }), "TEST"); window.ODD = so.id; return 1; })()`);
    await evaluate(`ED.tabs.open(ED.app.st.L.find(i => i.id === window.ODD)); 1`); await sleep(300);
    await evaluate(`${row(0)}.click(); 1`); await sleep(100);
    await btn("COPY"); await sleep(100);
    await evaluate(`(() => { ED.tabs.list.slice().forEach(t => { t.dirty = false; ED.tabs.close(t); }); ED.tabs.open(ED.app.st.R.find(i => i.id === window.FULL)); return 1; })()`); await sleep(300);
    await evaluate(`${row(0)}.click(); 1`); await sleep(100);
    await btn("PASTE"); await sleep(250);
    const st = await evaluate(`document.querySelector("#status").textContent`);
    const none = await draft();
    check(/^NO FREE SAMPLE NUMBER/.test(st) && none.n === 0, "sampler: PASTE with all 256 sample numbers taken: " + st);
    await shot("03-full");

    /* samples dropped on a sampler's item: an instrument each at its first empty slots, each playing its sample whole, named
     * after it; the samples move to the FM-1 panel with it (64 at most) */
    await evaluate(`(() => { ED.tabs.list.slice().forEach(t => { t.dirty = false; ED.tabs.close(t); });
        const R = ED.app.st.R, drop = R.filter(i => i.kind === 0x11 && /^F\\d+$/.test(ED.model.sampleEntry(i, R).name)), r = ED.model.remove(ED.app.st, "R", drop.map(i => i.id), false); ED.app.commit(r.st, "TEST"); return drop.length; })()`);
    const dropped = await evaluate(`(() => { const L = ED.app.st.L, R0 = ED.app.st.R, target = R0.find(i => i.id === window.FULL);
        const sm = L.filter(i => i.kind === 0x11).slice(0, 3), n0 = ED.rec.unpackSampler(target.recs[0].payload).instruments.length;
        const ok = ED.files.transfer("L", sm.map(i => i.id), { target: target.id });
        const R = ED.app.st.R, t = R.find(i => i.id === window.FULL), c = ED.rec.unpackSampler(t.recs[0].payload);
        const names = c.instruments.filter(Boolean).map(x => x.name), want = sm.map(i => ED.model.sampleEntry(i, L).name);
        const whole = c.instruments.filter(Boolean).every(x => x.start === 0 && x.loop === 0 && x.base_note === 60);
        return { ok, n0, n: c.instruments.filter(Boolean).length, same: names.join() === want.join(), whole, named: c.instruments.filter(Boolean).every(x => ED.model.sampleEntry(R.find(i => i.id === t.link[ED.model.lk("m", x.sample)]) || sm[0], R)) }; })()`);
    check(dropped.ok && dropped.n === 3 && dropped.same && dropped.whole && dropped.named, "sampler: samples dropped on a sampler's item: an instrument each, whole, named after it: " + JSON.stringify(dropped));

    /* the sample's tab: PLAYED BY lists the instruments of every sampler naming it */
    await evaluate(`(() => { ED.tabs.list.slice().forEach(t => { t.dirty = false; ED.tabs.close(t); }); return 1; })()`);
    const two = await evaluate(`(() => { const L = ED.app.st.L, o = L.find(i => i.kind === 0x04 && Object.keys(i.link).length), id = Object.values(o.link)[0], key = Object.keys(o.link)[0];
        const c = ED.rec.unpackSampler(o.recs[0].payload).instruments.find(x => x && x.sample === parseInt(key.slice(1), 16));
        const second = Object.assign({}, o, { id: ED.model.newId(), recs: [{ sub: 0, ver: 2, payload: ED.rec.packSampler({ name: "TWO", instruments: [Object.assign({}, c, { name: "SECOND" })] }) }], link: { [key]: id } });
        ED.app.commit(Object.assign({}, ED.app.st, { L: L.concat([second]) }), "TEST");
        ED.tabs.open(ED.app.st.L.find(i => i.id === id));
        const info = [...ED.tabs.current().body.querySelectorAll(".sinfo div")].find(d => d.querySelector("b") && d.querySelector("b").textContent === "PLAYED BY");
        return { text: info.textContent, first: ED.model.nameOf(o, L) }; })()`);
    check(/SECOND/.test(two.text) && two.text.split(",").length >= 2, "sampler: the sample's PLAYED BY lists the instruments of both samplers: " + two.text.slice(0, 120));
    await shot("04-playedby");
};
