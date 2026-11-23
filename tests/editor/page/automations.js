/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* AUTOMATIONS on the page (specs/editor.md, The tabs): the FM-1 panel's at its default (DEFAULT, dark grey, in SYSTEM);
 * two files on the TABLE; the first's tab: its 16 slots, a slot's events as a table in words, the row the FM-1 skips
 * grey; a knob's clicks, a gap, a time (the row taking its place), INSERT, DELETE of a range, a control changed and the
 * slot's screen, UNDO and REDO in the tab, every change coded as the firmware codes it; two slots marked and copied,
 * SAVE; the second's tab: PASTE past 0F refused, over a slot holding one asked (KEEP, then REPLACE), the parts pasted
 * byte for byte; a press and its release at +0 grey (one pass of the FM-1's); moved to the FM-1 panel, in place of its
 * default; every slot emptied there and saved: the default again. */
module.exports = async api => {
    const { evaluate, check, shot } = api;
    await api.fresh();
    const def = await evaluate(`(() => { const row = document.querySelector('.panel[data-side=R] [data-id="D:50"]'); return [row ? row.textContent : ""]; })()`);
    check(/AUTOMATIONS/.test(def[0]) && /DEFAULT/.test(def[0]), "automations: the FM-1 panel lists it at its default (" + JSON.stringify(def[0]) + ")");
    /* its tab from the default: 16 empty slots; INSERT makes slot 00's first event; SAVE makes the record; UNDO the default again */
    const fromDef = await evaluate(`(() => {
        document.querySelector('.panel[data-side=R] [data-id="D:50"]').dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
        const t = ED.tabs.current(), b = t.body, out = [t.kindWord, [...b.querySelectorAll(".autoslot.empty")].length, b.querySelector(".slotpage .hint").textContent];
        [...b.querySelectorAll("button")].find(x => x.textContent === "INSERT").click();
        const s = ED.rec.unpackAuto(t.draft.recs)[0];
        out.push(s.screen, s.events.map(ED.rec.autoEventText).join(), [...t.body.querySelectorAll(".atable .arow")].map(r => r.textContent).join());
        t.saveBtn.click();
        const it = ED.app.st.R.find(i => i.kind === 0x32);
        out.push(it ? it.recs.map(r => r.sub + ":" + [...r.payload].join(",")).join() : "", !!document.querySelector('.panel[data-side=R] [data-id="D:50"]'));
        ED.app.undo();
        out.push(!ED.app.st.R.some(i => i.kind === 0x32) && !!document.querySelector('.panel[data-side=R] [data-id="D:50"]'));
        return out; })()`);
    check(fromDef[0] === "AUTOMATIONS" && fromDef[1] === 16 && /AN EMPTY SLOT/.test(fromDef[2]), "automations: the default's tab: 16 empty slots (" + fromDef[2] + ")");
    check(fromDef[3] === 0 && fromDef[4] === "KEY F-3 DOWN" && fromDef[5] === "0:00.000+0KEY F-3 DOWN" && fromDef[6] === "0:0,1,0,0,0" && fromDef[7] === false && fromDef[8] === true,
        "automations: INSERT on an empty slot, KEY F-3 DOWN at 0 on STATUS; SAVE makes the record (" + fromDef[6] + "), UNDO the default again");
    /* two files on the TABLE: A (slot 00 on LOOPS, 03 on SONGS), B (slot 01 on FX) */
    await evaluate(`(() => {
        const R = ED.rec, P = (t, id) => ({ t, k: "press", id }), U = (t, id) => ({ t, k: "release", id }), N = (t, id, c) => ({ t, k: "knob", id, c });
        window.AU = { s0: [P(0, 7), N(0, 2, 2), U(250, 7), P(1000, 30), U(1180, 30), U(1500, 8)], s3: [P(0, 39), U(100, 39)], b1: [N(0, 0, -3)] };
        const mk = (slots, src) => ED.model.fromRecords(R.packAuto(slots).map(r => Object.assign({ kind: 0x32, num: 0 }, r)), { side: "L", src }).items[0];
        const a = mk([{ screen: 7, events: AU.s0 }, null, null, { screen: 1, events: AU.s3 }], "A.PTFMDSK"), b = mk([null, { screen: 6, events: AU.b1 }], "B.PTFMDSK");
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([a, b]) }), "TWO FILES");
        AU.a = a.id; AU.b = b.id;
        window.TA = {
            tab: () => ED.tabs.current(),
            body: () => ED.tabs.current().body,
            slot: (i, o) => { ED.tabs.current().body.querySelector('.autoslot[data-slot="' + i + '"]').dispatchEvent(new MouseEvent("click", Object.assign({ bubbles: true }, o || {}))); },
            row: (i, o) => { ED.tabs.current().body.querySelectorAll(".atable .arow")[i].dispatchEvent(new MouseEvent("click", Object.assign({ bubbles: true }, o || {}))); },
            rows: () => [...ED.tabs.current().body.querySelectorAll(".atable .arow")].map(r => (r.classList.contains("skip") ? "~" : "") + r.textContent),
            set: (label, v) => { const e = ED.tabs.current().body.querySelector('.aedit [aria-label="' + label + '"]'); e.value = String(v); e.dispatchEvent(new Event("change", { bubbles: true })); return e.value; },
            button: text => { const b = [...ED.tabs.current().body.querySelectorAll("button")].find(x => x.textContent === text); if (b) b.click(); return !!b; },
            screen: v => { const s = ED.tabs.current().body.querySelector('select[aria-label="SCREEN"]'); s.value = String(v); s.dispatchEvent(new Event("change")); },
            slots: () => ED.rec.unpackAuto(ED.tabs.current().draft.recs),
            parts: (recs, i) => recs.filter(r => r.sub >> 4 === i).map(r => (r.sub & 15) + ":" + [...r.payload].join(",")).join(" ")
        };
        return 1; })()`);
    const listed = await evaluate(`(() => { const row = document.querySelector('.panel[data-side=L] [data-id="' + CSS.escape(AU.a) + '"]'); return row ? row.textContent : ""; })()`);
    check(/AUTOMATIONS/.test(listed) && /2 OF 16 SLOTS/.test(listed), "automations: on the TABLE, 2 OF 16 SLOTS (" + listed + ")");
    await shot("01-panels");
    /* A's tab */
    await evaluate(`(() => { document.querySelector('.panel[data-side=L] [data-id="' + CSS.escape(AU.a) + '"]').dispatchEvent(new MouseEvent("dblclick", { bubbles: true })); return 1; })()`);
    const open = await evaluate(`(() => {
        const b = TA.body(), sl = [...b.querySelectorAll(".autoslot:not(.head)")].map(r => r.textContent);
        return [TA.tab().kindWord, sl.length, sl[0], sl[1], sl[3], TA.rows()]; })()`);
    check(open[0] === "AUTOMATIONS" && open[1] === 16 && /^00LOOPS0:016$/.test(open[2]) && /^01EMPTY$/.test(open[3]) && /^03SONGS0:002$/.test(open[4]),
        "automations: its tab, the 16 slots, each its screen, length and events (" + JSON.stringify(open.slice(0, 5)) + ")");
    const rows0 = open[5];
    check(rows0.length === 6 && rows0[0] === "0:00.000+0KEY C-4 DOWN" && rows0[1] === "0:00.000+0K1 +2" && rows0[2] === "0:00.250+250KEY C-4 UP" &&
        rows0[3] === "0:01.000+750SEL DOWN" && rows0[4] === "0:01.180+180SEL UP" && /^~0:01\.500\+320KEY C#4 UPNOT DOWN: THE FM-1 SKIPS IT$/.test(rows0[5]),
        "automations: slot 00's events in words, the one the FM-1 skips grey, saying why (" + rows0.join(" | ") + ")");
    await shot("02-tab");
    /* the edits, each coded as the firmware codes it */
    const ed = await evaluate(`(() => {
        const out = [];
        TA.row(1); TA.set("DOES", -5); out.push(ED.rec.autoEventText(TA.slots()[0].events[1]));
        TA.row(2); TA.set("MS SINCE THE LAST", 400); out.push(TA.slots()[0].events.map(e => e.t).join(","));
        TA.row(4); TA.set("TIME", "0:00.100"); const e1 = TA.slots()[0].events; out.push(e1.map(e => e.t + " " + ED.rec.autoEventText(e)).join(", "), TA.tab().row);
        TA.button("INSERT"); const e2 = TA.slots()[0].events; out.push(e2.length, ED.rec.autoEventText(e2[3]) + " " + e2[3].t, TA.tab().row);
        TA.row(5); TA.row(6, { shiftKey: true }); out.push(TA.button("DELETE 2")); out.push(TA.slots()[0].events.length);
        TA.row(0); TA.set("CONTROL", "p40"); out.push(TA.rows().join(" | "));
        TA.screen(8); out.push(TA.slots()[0].screen);
        ED.tabs.undo(TA.tab()); out.push(TA.slots()[0].screen);
        ED.tabs.redo(TA.tab()); out.push(TA.slots()[0].screen);
        const s = TA.slots()[0], want = ED.rec.packAutoSlot(s.screen, s.events);
        out.push(TA.parts(TA.tab().draft.recs, 0) === want.map((p, k) => k + ":" + [...p].join(",")).join(" "));
        return out; })()`);
    check(ed[0] === "K1 -5", "automations: a knob's clicks changed: " + ed[0]);
    check(ed[1] === "0,0,400,1150,1330,1650", "automations: a gap of 400 ms, the rows after it moving with it: " + ed[1]);
    check(ed[2] === "0 KEY C-4 DOWN, 0 K1 -5, 100 SEL UP, 400 KEY C-4 UP, 1150 SEL DOWN, 1650 KEY C#4 UP" && ed[3] === 2,
        "automations: a time typed, the row taking its place in time, picked there: " + ed[2] + " (row " + ed[3] + ")");
    check(ed[4] === 7 && ed[5] === "SEL UP 100" && ed[6] === 3, "automations: INSERT puts a copy right after it, at its time: " + ed[5]);
    check(ed[7] === true && ed[8] === 5, "automations: two rows picked (Shift) and deleted: " + ed[8] + " left");
    check(/^0:00\.000\+0REC DOWN \| 0:00\.000\+0K1 -5 \| ~0:00\.100\+100SEL UPNOT DOWN/.test(ed[9]), "automations: a control changed, the rows the FM-1 skips following: " + ed[9]);
    check(ed[10] === 8 && ed[11] === 7 && ed[12] === 8, "automations: the slot's screen DJ, UNDO and REDO in the tab: " + ed.slice(10, 13).join(","));
    check(ed[13] === true, "automations: the slot's parts coded as the firmware codes them");
    await shot("03-edited");
    /* two slots marked, COPY; SAVE */
    const cp = await evaluate(`(() => {
        TA.slot(0); TA.slot(3, { ctrlKey: true });
        const marked = [...TA.body().querySelectorAll(".autoslot.on")].map(r => r.dataset.slot).join();
        TA.button("COPY");
        const st = document.querySelector("#status").textContent;
        AU.recs = TA.tab().draft.recs;
        TA.tab().saveBtn.click();
        const it = ED.app.st.L.find(i => i.id === AU.a);
        return [marked, st, !ED.tabs.list.length, TA.parts(it.recs, 0) === TA.parts(AU.recs, 0)]; })()`);
    check(cp[0] === "0,3" && /COPIED 2 \(00, 03\)/.test(cp[1]) && cp[2] && cp[3], "automations: slots 00 and 03 marked and copied, the tab saved (" + JSON.stringify(cp) + ")");
    /* B's tab: PASTE past 0F refused; over slot 01, which holds one: asked, KEEP, then REPLACE */
    const ps = await evaluate(`(async () => {
        const out = [];
        ED.tabs.open(ED.app.st.L.find(i => i.id === AU.b));
        TA.slot(15); TA.button("PASTE"); out.push(document.querySelector("#status").textContent, TA.slots().filter(Boolean).length);
        TA.slot(1); TA.button("PASTE");
        const dlg = document.querySelector("#dlg"); out.push(dlg && !dlg.hidden ? dlg.innerText.replace(/\\n+/g, " | ") : "");
        [...dlg.querySelectorAll(".btn")].find(x => x.textContent === "KEEP").click();
        out.push(TA.slots()[1].events.length, TA.tab().dirty);
        TA.button("PASTE");
        [...document.querySelectorAll("#dlg .btn")].find(x => x.textContent === "REPLACE").click();
        const d = TA.tab().draft.recs, a = ED.app.st.L.find(i => i.id === AU.a).recs;
        const strip = s => s.replace(/^\\d+:/, "").replace(/ \\d+:/g, " ");
        out.push(strip(TA.parts(d, 1)) === strip(TA.parts(a, 0)), strip(TA.parts(d, 2)) === strip(TA.parts(a, 3)), d.map(r => r.sub.toString(16)).join());
        out.push([...TA.body().querySelectorAll(".autoslot:not(.head)")].slice(1, 3).map(r => r.textContent).join(" | "));
        TA.slot(2);
        return out; })()`);
    check(/2 COPIED, 1 SLOT FROM HERE: NOTHING PASTED/.test(ps[0]) && ps[1] === 1, "automations: PASTE at 0F with 2 copied: " + ps[0]);
    check(/REPLACE 1 AUTOMATION\?/.test(ps[2]) && /SLOT 01 HOLDS ONE/.test(ps[2]) && ps[3] === 1 && ps[4] === false, "automations: PASTE over slot 01 asks first; KEEP leaves it (" + ps[2] + ")");
    check(ps[5] === true && ps[6] === true && ps[7] === "10,20", "automations: REPLACE: slots 01 and 02 hold 00's and 03's parts byte for byte, each its screen (subs " + ps[7] + ")");
    check(/^01DJ0:005$/.test(ps[8].split(" | ")[0]) && /^02SONGS0:002$/.test(ps[8].split(" | ")[1]), "automations: the slots listed after the paste: " + ps[8]);
    await shot("04-pasted");
    /* PLAY UP moved to +0, in the pass of PLAY DOWN: both grey, cancelled; UNDO */
    const cancel = await evaluate(`(() => {
        TA.row(1); TA.set("MS SINCE THE LAST", 0);
        const rows = TA.rows();
        ED.tabs.undo(TA.tab());
        return [rows.join(" | "), TA.rows().join(" | ")]; })()`);
    check(/^~0:00\.000\+0PLAY DOWNCANCELLED IN ONE PASS \(\+0\): THE FM-1 NEVER SEES IT \| ~0:00\.000\+0PLAY UPCANCELLED IN ONE PASS/.test(cancel[0]) && !/~/.test(cancel[1]),
        "automations: a press and its release at +0, one pass of the FM-1's: both grey, CANCELLED IN ONE PASS (" + cancel[0] + "); UNDO");
    /* B saved, onto the FM-1 panel: in place of its default */
    const mv = await evaluate(`(() => {
        TA.tab().saveBtn.click();
        ED.files.transfer("L", [AU.b]);
        const p = document.querySelector(".panel[data-side=R]"), it = ED.app.st.R.find(i => i.kind === 0x32);
        const row = it && p.querySelector('[data-id="' + CSS.escape(it.id) + '"]');
        return [!!it, !p.querySelector('[data-id="D:50"]'), row ? row.textContent : "", it ? it.recs.map(r => r.sub.toString(16)).join() : ""]; })()`);
    check(mv[0] && mv[1] && /AUTOMATIONS/.test(mv[2]) && /2 OF 16 SLOTS/.test(mv[2]) && mv[3] === "10,20", "automations: moved to the FM-1 panel in place of its default (" + JSON.stringify(mv) + ")");
    await shot("05-fm1");
    /* its tab on the FM-1 panel: every row of both slots deleted, SAVE: the panel's DEFAULT again, a change to write */
    const emptied = await evaluate(`(() => {
        document.querySelector('.panel[data-side=R] [data-id="' + CSS.escape(ED.app.st.R.find(i => i.kind === 0x32).id) + '"]').dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
        for (const s of [1, 2]) {
            TA.slot(s);
            TA.body().querySelector(".atable").dispatchEvent(new KeyboardEvent("keydown", { key: "a", ctrlKey: true, bubbles: true }));
            TA.body().querySelector(".atable").dispatchEvent(new KeyboardEvent("keydown", { key: "Delete", bubbles: true }));
        }
        const left = TA.tab().draft.recs.length;
        TA.tab().saveBtn.click();
        const row = document.querySelector('.panel[data-side=R] [data-id="D:50"]');
        return [left, ED.app.st.R.some(i => i.kind === 0x32), row ? row.textContent : "", ED.model.pending(ED.app.st).count]; })()`);
    check(emptied[0] === 0 && emptied[1] === false && /AUTOMATIONS/.test(emptied[2]) && /DEFAULT/.test(emptied[2]) && emptied[3] === 0,
        "automations: every slot emptied and saved on the FM-1 panel: its DEFAULT again (" + JSON.stringify(emptied) + ")");
};
