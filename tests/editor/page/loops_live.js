/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The LOOPS tab with its loops playing (specs/editor.md): THE NOTES across (a header of rows, each lane three lines,
 * the rows left to right); a MUTE, a mixer value, TEMPO and a set's SOLO heard at once with the loops playing on: each
 * sent to the preview as an edit on LOOPS (no new start, no new flash), the preview still playing, TEMPO's new BPM in
 * what plays. Checked through what the page sends its preview and the state it reads back. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await evaluate(`(() => {
        const R = ED.rec, L = R.unpackLoops(R.loopsV3([(() => { const b = new Uint8Array(72); b.set([120, 0, 6, 50, 0, 0, 0, 0]); return b; })()]));
        L.length = 16;
        const lp = (note, lanes) => ({ cart: 0xF8, slot: 0, colour: 0, mono: 0, muted: false, lanes, base: 16, repeat: 1, shift: 0, mixer: R.MIX_DEFAULT.slice(),
            cells: new Map([[R.cellKey(0, 0), { note, ins: -1, vol: -1, cmds: [] }], [R.cellKey(4, lanes - 1), { note: note + 7, ins: -1, vol: 0x40, cmds: [] }]]) });
        L.loops = [lp(48, 2), lp(60, 1)];
        const loops = R.packLoops(L).map((payload, sub) => ({ kind: 0x22, num: 0, sub, ver: 4, payload }));
        const set = R.packLSet({ name: "LIVE SET", solos: 0, loops: L }).map((payload, sub) => ({ kind: 0x24, num: 0xA0, sub, ver: 1, payload }));
        const st = ED.model.fromRecords(loops.concat(set), { side: "R" });
        ED.app.commit(Object.assign({}, ED.app.st, { R: st.items }), "LOOPS");
        const e = ED.player.engine, post = e.post.bind(e);
        window.LV = { sent: [], L,
            body: () => ED.tabs.current().body,
            open: kind => { ED.player.stop(); ED.tabs.open(ED.app.st.R.find(i => i.kind === kind)); return 1; },
            play: () => { LV.body().querySelector('button[title="PLAY ALL THE LOOPS"]').click(); return 1; },
            sends: () => LV.sent.splice(0) };
        e.post = m => { LV.sent.push(m.t + (m.how ? ":" + m.how + (m.how === "loops" ? "/" + m.soloed : "") : "") + (m.fn ? ":" + m.fn + "(" + m.args.join(",") + ")" : "")); post(m); };
        return 1; })()`);
    const step = async (name, js, ok) => {
        let r;
        try { r = await evaluate(js); } catch (e) { r = "FAILED " + e.message; }
        check(ok(r), "loops live: " + name + ": " + JSON.stringify(r));
        return r;
    };
    await evaluate("LV.open(0x22)");
    await api.until("!!LV.body().querySelector('.hgrid')", 10);
    await step("THE NOTES across: a header of 16 rows, two lanes of three lines, the notes in place", `(() => {
        const g = LV.body().querySelector(".hgrid"), rows = [...g.querySelectorAll(".hrow")], R = ED.rec;
        const cell = (row, r) => rows[row].querySelectorAll(".hc")[r].textContent;
        return [rows.length, rows[0].querySelectorAll(".hc").length, cell(1, 0) === R.noteName(48), cell(4, 4) === R.noteName(55),
            cell(5, 4), cell(2, 0), rows[1].querySelector(".hl").textContent, rows[4].querySelector(".hl").textContent, g.offsetHeight];
    })()`, r => r[0] === 7 && r[1] === 16 && r[2] && r[3] && r[4] === "40" && r[5] === "--" && r[6] === "LANE 1" && r[7] === "LANE 2" && r[8] < 200);
    await shot("01-notes-across");
    await evaluate("LV.play()");
    await api.until("!!(ED.player.state && ED.player.state.playing)", 15);
    await sleep(400);
    await evaluate("LV.sends()");
    const live = async (name, js) => {
        await evaluate(js);
        await sleep(500);
        return step(name + ": an edit on LOOPS, playing on", `(() => { const s = LV.sends(); return [s, !!(ED.player.state && ED.player.state.playing)]; })()`,
            r => Array.isArray(r[0]) && r[0].some(x => /^edits:loops\/|^call:ed_loops_changed/.test(x)) && !r[0].some(x => /^(play|flash|stop)/.test(x)) && r[1] === true);
    };
    await live("MUTE on loop 00", `(() => { const c = LV.body().querySelector('.lmixrow input[type="checkbox"][title="MUTED"]'); c.checked = true; c.dispatchEvent(new Event("change")); return 1; })()`);
    await live("LEVEL of loop 01", `(() => { const i = [...LV.body().querySelectorAll(".lmixrow")][2].querySelector('input[type="text"]'); i.value = "40"; i.dispatchEvent(new Event("change")); return 1; })()`);
    await live("TEMPO 150", `(() => { const i = LV.body().querySelector('input[aria-label="TEMPO"]'); i.value = "150"; i.dispatchEvent(new Event("change")); return 1; })()`);
    await step("TEMPO 150 in what plays", "ED.player.state.bpm", r => r === 150);
    await shot("02-loops-live");
    await evaluate("LV.open(0x24)");
    await api.until("!!LV.body().querySelector('.hgrid')", 10);
    await evaluate("LV.play()");
    await api.until("!!(ED.player.state && ED.player.state.playing)", 15);
    await sleep(400);
    await evaluate("LV.sends()");
    const solo = await live("SOLO on loop 01 of a set", `(() => { const c = [...LV.body().querySelectorAll('.lmixrow input[type="checkbox"]')].filter(x => /^SOLO/.test(x.title))[1]; c.checked = true; c.dispatchEvent(new Event("change")); return 1; })()`);
    check(Array.isArray(solo) && Array.isArray(solo[0]) && solo[0].some(x => /^call:ed_loops_changed\(0,0,2\)$|^edits:loops\/2$/.test(x)), "loops live: the set's solo sent as its bit: " + JSON.stringify(solo && solo[0]));
    await evaluate("ED.player.stop(); 1");
};
