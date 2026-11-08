// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The playground's user area kept in the browser (specs/panel.md#the-page): a new one starts clean (the built-ins
 * alone), DEMO DISK writes the demo disk into it (refused with changes not saved), SAVE keeps what it holds, a power
 * cycle (a reload) starts from what was kept, the changes not saved gone; ERASE DATA drops it. A store in memory
 * stands in for IndexedDB. Run by the harness:
 *   node tests/playground/harness.js tests/playground/storage.js */
"use strict";
module.exports = async t => {
    const { M, ok } = t;
    let kept = null;
    M.useStore({ get: async () => kept, put: async v => { kept = structuredClone(v); }, del: async () => { kept = null; } });

    await M.eraseKept();
    ok(!(await M.startUp()), "nothing kept: a new user area");
    const S = M.state;
    ok(!S.songs.some(Boolean) && S.carts.every(c => c.builtin) && S.carts.length > 0 &&
       S.patterns.every(p => !p || p.builtin || p.n === M.EDITOR_PATTERN) && !M.unsaved,
       "clean: no song, the built-in cartridges and patterns alone (" + S.carts.length + " cartridges)");

    ok(M.demoDisk() && M.songList().length === 18 && !M.unsaved, "DEMO DISK: the demo disk's 18 songs, nothing unsaved");
    await M.kept();
    ok(kept && kept.songs.filter(Boolean).length === 18, "... and kept in the browser");

    const name = M.state.songs[0xA0].name;
    M.edit(() => { M.state.songs[0xA0].name = "KEPT"; });
    ok(M.unsaved === 1 && !M.demoDisk() && M.state.songs[0xA0].name === "KEPT", "DEMO DISK refused while a change is not saved");
    M.save();
    await M.kept();
    ok(kept.songs[0xA0].name === "KEPT", "SAVE keeps the change in the browser");

    M.edit(() => { M.state.songs[0xA0].name = "NOT SAVED"; });
    ok(await M.startUp() && M.state.songs[0xA0].name === "KEPT" && !M.unsaved, "a power cycle: what was kept, the change not saved gone");

    /* LOOPS' mutes are saved, its solos never (specs/files/loops.md): a solo leaves nothing unsaved, a REVERT keeps it,
     * a power cycle drops it */
    const loop = (muted, soloed) => ({ inst: 0xF000, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted, soloed,
                                       cells: new Array(64).fill(null) });
    M.edit(() => { M.state.loops.rows = 16; M.state.loops.list = [loop(true, false), loop(false, false)]; });
    M.save();
    M.edit(() => { M.state.loops.list[1].soloed = true; });
    ok(!M.unsaved, "LOOPS: a solo leaves nothing unsaved");
    M.edit(() => { M.state.songs[0xA0].name = "NOT SAVED"; });
    M.revert();
    ok(M.state.loops.list[1].soloed && M.state.songs[0xA0].name === "KEPT", "REVERT ALL: the solo kept (nothing saved holds it)");
    M.save();
    await M.kept();
    ok(!kept.loops.list[1].soloed && kept.loops.list[0].muted, "SAVE: the mute kept in the browser, the solo not");
    ok(await M.startUp() && M.state.loops.list[0].muted && !M.state.loops.list.some(l => l.soloed),
       "a power cycle: the loops' mute as saved, no solo");
    M.edit(() => { M.state.loops = M.newLoops(); });
    M.save();

    /* a user area kept in the older form (each sampler holding its samples' table, an instrument naming its place in it:
     * specs/files/instruments.md#the-old-form) is converted as it loads, as the emulator converts the flash it keeps
     * (specs/emulator.md): each sample a record of its own, numbered the lowest free first, sampler by sampler in number
     * order, in each in its table's order, the same sample once (a clone's samples are its source's); an instrument naming
     * an emptied entry an empty slot; nothing unsaved */
    {
        const now = structuredClone(M.state), old = structuredClone(M.state);
        const tables = {};
        for (const c of old.carts) if (c.type === "SAMPLER") {
            const table = [...new Set(c.slots.filter(Boolean).map(s => s.p.sample))].sort((a, b) => a - b);
            tables[c.id] = table;
            c.samples = table.map(n => (old.samples || [])[n]);
            for (const s of c.slots) if (s) s.p.sample = table.indexOf(s.p.sample);
        }
        const kit = old.carts.find(c => c.id === 0xA0), clone = structuredClone(kit);
        Object.assign(clone, { id: 0x10, name: "KIT 2" });
        clone.samples = clone.samples || [];
        clone.samples.push(Object.assign({}, clone.samples[0], { name: "(DELETED)", length: 0, deleted: true }));
        clone.slots[0x0B] = structuredClone(kit.slots[0]);
        clone.slots[0x0B].p.sample = clone.samples.length - 1;   /* (an emptied entry) */
        old.carts.push(clone);
        old.carts.sort(M.cartCmp);
        delete old.samples;
        delete old.older;
        kept = old;
        const was = await M.startUp(), S2 = M.state, n2 = (S2.samples || []).filter(Boolean).length;
        /* (the same samples in the same order, from 00 as the conversion numbers them) */
        const sameSamples = !!S2.samples && !!now.samples && JSON.stringify(now.samples.filter(Boolean)) === JSON.stringify(S2.samples.filter(Boolean));
        const named = id => (M.cart(id) || { slots: [] }).slots.map(s => s ? s.p.sample : -1).join();
        const sameNames = Object.keys(tables).every(id => named(+id) === now.carts.find(c => c.id === +id).slots.map(s => s ? s.p.sample - 0xA0 : -1).join());
        const cl = M.cart(0x10);
        ok(was && n2 === 33 && sameSamples && sameNames && !!cl && !cl.samples && S2.carts.every(c => !c.samples) &&
           named(0x10).split(",").slice(0, 10).join() === named(0xA0).split(",").slice(0, 10).join() && !cl.slots[0x0B] &&
           Array.isArray(S2.older) && !S2.older.length && !M.unsaved,
           "a user area kept in the older form: converted as it loads (33 samples, numbered as the demo disk's, the samplers " +
           "naming them, a clone's merged, an emptied entry's instrument an empty slot; nothing unsaved)");
        ok(M.demoDisk(), "(the demo disk again)");
        await M.kept();
    }

    await M.eraseKept();
    ok(kept === null && !(await M.startUp()) && !M.songList().length, "ERASE DATA: nothing kept, a new user area again");
    M.demoDisk();
    M.edit(() => { M.state.songs[0xA0].name = name; });
    M.save();
};
