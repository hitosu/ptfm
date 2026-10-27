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
    ok(!S.songs.length && S.carts.every(c => c.builtin) && S.carts.length > 0 &&
       S.patterns.every(p => !p || p.builtin || p.n === M.EDITOR_PATTERN) && !M.unsaved,
       "clean: no song, the built-in cartridges and patterns alone (" + S.carts.length + " cartridges)");

    ok(M.demoDisk() && M.state.songs.length === 18 && !M.unsaved, "DEMO DISK: the demo disk's 18 songs, nothing unsaved");
    await M.kept();
    ok(kept && kept.songs.length === 18, "... and kept in the browser");

    const name = M.state.songs[0].name;
    M.edit(() => { M.state.songs[0].name = "KEPT"; });
    ok(M.unsaved === 1 && !M.demoDisk() && M.state.songs[0].name === "KEPT", "DEMO DISK refused while a change is not saved");
    M.save();
    await M.kept();
    ok(kept.songs[0].name === "KEPT", "SAVE keeps the change in the browser");

    M.edit(() => { M.state.songs[0].name = "NOT SAVED"; });
    ok(await M.startUp() && M.state.songs[0].name === "KEPT" && !M.unsaved, "a power cycle: what was kept, the change not saved gone");

    /* LOOPS' mutes are saved, its solos never (specs/files/loops.md): a solo leaves nothing unsaved, a REVERT keeps it,
     * a power cycle drops it */
    const loop = (muted, soloed) => ({ inst: 0xF000, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted, soloed,
                                       cells: new Array(64).fill(null) });
    M.edit(() => { M.state.loops.rows = 16; M.state.loops.list = [loop(true, false), loop(false, false)]; });
    M.save();
    M.edit(() => { M.state.loops.list[1].soloed = true; });
    ok(!M.unsaved, "LOOPS: a solo leaves nothing unsaved");
    M.edit(() => { M.state.songs[0].name = "NOT SAVED"; });
    M.revert();
    ok(M.state.loops.list[1].soloed && M.state.songs[0].name === "KEPT", "REVERT ALL: the solo kept (nothing saved holds it)");
    M.save();
    await M.kept();
    ok(!kept.loops.list[1].soloed && kept.loops.list[0].muted, "SAVE: the mute kept in the browser, the solo not");
    ok(await M.startUp() && M.state.loops.list[0].muted && !M.state.loops.list.some(l => l.soloed),
       "a power cycle: the loops' mute as saved, no solo");
    M.edit(() => { M.state.loops = M.newLoops(); });
    M.save();

    await M.eraseKept();
    ok(kept === null && !(await M.startUp()) && !M.state.songs.length, "ERASE DATA: nothing kept, a new user area again");
    M.demoDisk();
    M.edit(() => { M.state.songs[0].name = name; });
    M.save();
};
