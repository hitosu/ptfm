/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The tabs: each kind edited through its own controls and saved, its records read back with the change; the FX
 * chain reordered by dragging; CLOSE with changes asks; a tab whose item is deleted says so. */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    await api.demo();
    await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.transfer("L", [s.id]); return 1; })()`);
    /* helpers in the page: a control found by its label, set as a person would; SAVE */
    await evaluate(`window.TT = {
        save() { const t = ED.tabs.current(); t.saveBtn.click(); return !ED.tabs.list.includes(t); },
        field(label) { return [...ED.tabs.current().body.querySelectorAll(".field")].find(x => x.querySelector("label").textContent === label); },
        slider(label, v) { const i = TT.field(label).querySelector("input[type=range]"); i.value = v; i.dispatchEvent(new Event("input")); i.dispatchEvent(new Event("change")); return +i.value; },
        choice(label, v) { const s = TT.field(label).querySelector("select"); s.value = String(v); s.dispatchEvent(new Event("change")); return s.value; },
        button(text) { const b = [...ED.tabs.current().body.querySelectorAll("button")].find(x => x.textContent === text); b.click(); return !!b; }
    }; 1`);
    const step = async (name, js, ok) => {
        let r;
        try { r = await evaluate(js); } catch (e) { r = "FAILED " + e.message; }
        check(ok(r), "tabs: " + name + ": " + JSON.stringify(r));
    };
    await step("pattern: a cell picked, the computer's Z writes F-3", `(() => {
        const p = ED.app.st.R.find(i => i.kind === 0x20); ED.tabs.open(p);
        const g = ED.tabs.current().body.querySelector(".pgrid"), c = g.querySelector('.pc[data-r="1"][data-t="0"]');
        c.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, pointerType: "mouse" })); g.dispatchEvent(new PointerEvent("pointerup", { bubbles: true }));
        g.dispatchEvent(new KeyboardEvent("keydown", { key: "z", bubbles: true }));
        const saved = TT.save(), q = ED.rec.unpackPattern(ED.app.st.R.find(i => i.id === p.id).recs.map(r => r.payload));
        return [saved, ED.rec.noteName((q.cells.get("1,0") || {}).note || 0)];
    })()`, r => r[0] === true && r[1] === "F-3");
    await step("song: TEMPO and AT THE END", `(() => {
        const s = ED.app.st.R.find(i => i.kind === 0x21); ED.tabs.open(s);
        TT.slider("TEMPO", 140); TT.choice("AT THE END", 0);
        const saved = TT.save(), x = ED.rec.unpackSong(ED.app.st.R.find(i => i.id === s.id).recs[0].payload);
        return [saved, x.tempo, x.flags & 1];
    })()`, r => r[0] === true && r[1] === 140 && r[2] === 0);
    /* the FX chain: the first effect dragged after the third */
    await step("song: the FX chain's first effect dragged after its third", `(() => {
        const s = ED.app.st.R.find(i => i.kind === 0x21); ED.tabs.open(s);
        const before = ED.rec.fxChain(ED.rec.unpackSong(s.recs[0].payload).fx);
        const chips = [...ED.tabs.current().body.querySelectorAll(".chain .fxchip[draggable=true]")];
        const dt = new DataTransfer(), r = chips[2].getBoundingClientRect(), at = { bubbles: true, cancelable: true, dataTransfer: dt, clientX: r.right - 2, clientY: r.top + 4 };
        chips[0].dispatchEvent(new DragEvent("dragstart", at));
        chips[2].dispatchEvent(new DragEvent("dragover", at));
        const marked = chips[2].classList.contains("drop-after");
        chips[2].dispatchEvent(new DragEvent("drop", at));
        chips[0].dispatchEvent(new DragEvent("dragend", at));
        const saved = TT.save(), after = ED.rec.fxChain(ED.rec.unpackSong(ED.app.st.R.find(i => i.id === s.id).recs[0].payload).fx);
        const want = before.slice(1, 3).concat([before[0]], before.slice(3));
        return [saved, marked, after.join(), want.join()];
    })()`, r => r[0] === true && r[1] === true && r[2] === r[3]);
    await step("sampler on the TABLE: an instrument's LEVEL", `(() => {
        const c = ED.app.st.L.find(i => i.kind === 0x04); ED.tabs.open(c);
        TT.slider("LEVEL", -12);
        const saved = TT.save(), x = ED.rec.unpackSampler(ED.app.st.L.find(i => i.id === c.id).recs[0].payload);
        return [saved, x.instruments[0].level];
    })()`, r => r[0] === true && r[1] === -12);
    await shot("01-sampler");
    await step("sample on the FM-1 panel: DEPTH 16 bits, its entry in its sampler", `(() => {
        const s = ED.app.st.R.find(i => i.kind === 0x10); ED.tabs.open(s);
        TT.choice("DEPTH", 1);
        const saved = TT.save(), m = ED.model.byIdMap(ED.app.st.R), e = ED.model.sampleEntry(m.get(s.id), m);
        return [saved, e.coding, !!m.get(s.id).entry, ED.rec.ptrDecode(m.get(s.id).recs[0].payload).bits];
    })()`, r => r[0] === true && r[1] === 2 && r[2] === false && r[3] === 16);
    await step("a built-in instrument: SAVE makes a changed copy at its number", `(() => {
        const b = ED.files.viewItems("R").find(i => i.fw && i.kind === 0x02); ED.tabs.open(b);
        TT.slider("LEVEL", 6);
        const saved = TT.save(), it = ED.app.st.R.find(i => i.kind === 0x02 && i.num === b.num);
        return [saved, !!it && !!it.builtin, it ? ED.rec.unpackCart(2, it.recs[0].payload).slots[0].level : null];
    })()`, r => r[0] === true && r[1] === true && r[2] === 6);
    await step("the changed built-in: the panel's references to its number name it, the pattern tab lists it", `(() => {
        const it = ED.app.st.R.find(i => i.kind === 0x02 && i.builtin), k = ED.model.lk("c", it.num);
        const users = ED.app.st.R.filter(i => k in i.link);
        const p = users.find(i => i.kind === 0x20) || ED.app.st.R.find(i => i.kind === 0x20); ED.tabs.open(p);
        const g = ED.tabs.current().body.querySelector(".pgrid"), c = g.querySelector('.pc[data-r="0"][data-t="0"]');
        c.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, pointerType: "mouse" })); g.dispatchEvent(new PointerEvent("pointerup", { bubbles: true }));
        const sel = [...ED.tabs.current().body.querySelectorAll(".insp select")].find(s => /^NO INSTRUMENT/.test(s.options[0].textContent));
        const keys = [...sel.options].map(o => o.value.split("|")[0]);
        ED.tabs.close(ED.tabs.current());
        return [users.length, users.every(u => u.link[k] === it.id), keys.includes(it.id), keys.includes("B" + it.num)];
    })()`, r => r[0] > 0 && r[1] === true && r[2] === true && r[3] === false);
    await step("SETTINGS from its default: THEME", `(() => {
        const d = ED.files.viewItems("R").find(i => i.isDefault && i.kind === 0x30); ED.tabs.open(d);
        TT.choice("THEME", 3);
        const saved = TT.save(), it = ED.app.st.R.find(i => i.kind === 0x30);
        return [saved, it && it.recs[0].payload[2], it && it.recs[0].payload.length];
    })()`, r => r[0] === true && r[1] === 3 && r[2] === 133);
    await step("MIDI MAP from its default: a mapping added", `(() => {
        const d = ED.files.viewItems("R").find(i => i.isDefault && i.kind === 0x31); ED.tabs.open(d);
        TT.button("+ ADD A MAPPING");
        const saved = TT.save(), it = ED.app.st.R.find(i => i.kind === 0x31);
        return [saved, it ? ED.rec.unpackMidiMap(it.recs[0].payload).maps.length : 0];
    })()`, r => r[0] === true && r[1] === 1);
    await step("DJ SETUP from its default: the CROSSFADER", `(() => {
        const d = ED.files.viewItems("R").find(i => i.isDefault && i.kind === 0x23); ED.tabs.open(d);
        TT.choice("CROSSFADER", 1);
        const saved = TT.save(), it = ED.app.st.R.find(i => i.kind === 0x23);
        return [saved, it && it.recs[0].payload[1]];
    })()`, r => r[0] === true && r[1] === 1);
    await step("CLOSE with changes asks; DISCARD keeps the item as it was", `(() => {
        const s = ED.app.st.R.find(i => i.kind === 0x21); ED.tabs.open(s); TT.slider("SWING", 60);
        ED.tabs.close(ED.tabs.current()); const q = document.querySelector("#dlg h3").textContent;
        [...document.querySelectorAll("#dlg .btn")].find(b => b.textContent === "DISCARD").click();
        return [q, ED.tabs.list.length, ED.rec.unpackSong(ED.app.st.R.find(i => i.id === s.id).recs[0].payload).swing];
    })()`, r => r[0] === "SAVE THE CHANGES TO SONG 00 PUNCH IN?" && r[1] === 0 && r[2] === 50);
    await step("a tab whose item is deleted: said, SAVE off", `(() => {
        const c = ED.app.st.L.find(i => i.kind === 0x01 || i.kind === 0x05); ED.tabs.open(c); ED.tabs.show("files");
        const r = ED.model.remove(ED.app.st, "L", [c.id], false); ED.app.commit(r.st, "DELETED");
        const t = ED.tabs.list.find(x => x.itemId === c.id); ED.tabs.show(t.id);
        return [!!t.notes.querySelector(".banner.bad"), t.saveBtn.disabled];
    })()`, r => r[0] === true && r[1] === true);
    await shot("02-deleted");
    await step("LOOPS from its default opens", `(() => { const d = ED.files.viewItems("R").find(i => i.isDefault && i.kind === 0x22); const t = ED.tabs.open(d); return !!t && !t.body.querySelector(".banner.bad"); })()`, r => r === true);
    await step("what WRITE would send: every group changed", `[ED.model.pending(ED.app.st).count, [...new Set(ED.app.st.R.map(i => ED.model.groupOf(i)))].sort().join()]`,
        r => r[0] > 5 && r[1] === "CARTRIDGES,PATTERNS,SAMPLERS,SAMPLES,SONGS,SYSTEM");
};
