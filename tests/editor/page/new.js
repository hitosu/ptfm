/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* + NEW on the TABLE: its menu; each kind made, opened in its tab, its records read back; a new song moved to the FM-1
 * panel with its pattern, numbered, its reference kept. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    const menu = await evaluate(`(() => { const b = [...document.querySelectorAll(".panel[data-side=L] .ptools button")].find(x => x.textContent.startsWith("+ NEW")); b.click();
        const m = document.querySelector(".menu"); return m ? [...m.querySelectorAll("button")].map(x => x.textContent).join(" | ") : ""; })()`);
    check(menu === "SONG (WITH AN EMPTY PATTERN) | PATTERN | DX7 CARTRIDGE | JUNO-106 CARTRIDGE | TB-303 CARTRIDGE | CHIP CARTRIDGE | FUSION CARTRIDGE | " +
        "SAMPLER (DROP SOUNDS ON IT) | SETTINGS | MIDI MAP | LOOPS | DJ SETUP", "new: the menu: " + menu);
    await shot("01-menu");
    await evaluate("ED.ui.closeMenu(); 1");
    const want = { 0x21: "NEW SONG · 1 · 1", 0x20: "8 x 64", 1: "NEW DX7 · 1", 2: "NEW JUNO-106 · 1", 3: "NEW TB-303 · 1", 4: "NEW SAMPLER · 0", 5: "NEW FUSION · 1",
        6: "NEW CHIP · 1", 0x30: "133 B", 0x31: "4 B", 0x22: "80 B", 0x23: "136 B" };
    for (const k of Object.keys(want).map(Number)) {
        await evaluate(`ED.files.makeNew(${k}).then(() => 1)`);
        await sleep(200);
        const got = await evaluate(`(() => {
            const t = ED.tabs.current(), it = t && t.base, R = ED.rec;
            if (!it || it.kind !== ${k}) return "no tab for it";
            const p = it.recs.map(r => r.payload);
            if (${k} === 0x21) { const s = R.unpackSong(p[0]); return s.name + " · " + s.positions.length + " · " + Object.keys(it.link).length; }
            if (${k} === 0x20) { const q = R.unpackPattern(p); return q.tracks + " x " + q.rows; }
            if (${k} === 4) { const s = R.unpackSampler(p[0]); return s.name + " · " + s.instruments.filter(Boolean).length; }
            if (${k} <= 6) { const c = R.unpackCart(${k}, p[0]); return c.name + " · " + c.slots.filter(Boolean).length; }
            return p[0].length + " B";
        })()`);
        check(got === want[k], "new: kind " + k.toString(16) + " made and opened: " + got);
    }
    await shot("02-tab");
    check(await evaluate("ED.app.st.L.length === 13 && ED.app.st.L.every(i => i.src === 'NEW')"), "new: 13 items on the TABLE, their source NEW");
    const moved = await evaluate(`(() => { ED.tabs.list.slice().forEach(t => ED.tabs.close(t));
        const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.transfer("L", [s.id]);
        const R = ED.app.st.R, rs = R.find(i => i.kind === 0x21), rp = rs && R.find(i => i.id === rs.link[ED.model.lk("p", ED.rec.unpackSong(rs.recs[0].payload).positions[0])]);
        return [document.querySelector("#status").textContent, rs && rs.num, rp && rp.num]; })()`);
    check(/SONG 00 NEW SONG · \+ 1 PATTERN/.test(moved[0]) && moved[1] === 0 && moved[2] === 0, "new: a new song moved with its pattern: " + JSON.stringify(moved));
};
