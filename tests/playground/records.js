// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The list of records (specs/files/store.md#the-list-of-records, specs/ui/saving.md#storage-full; fw/tests/ui3_test.c's
 * test_save_list): the FM-1 lists 560 records at most. The demo disk's 121, then cartridges, patterns and songs made up to
 * 560, saved; a sampler's MOVE answered YES (its new number a record more, the old one's deletion unable to go first, as a
 * cartridge is new) refused whole: TOO MANY RECORDS TO SAVE, 1 MORE THAN THE FM-1 LISTS, the edits kept, nothing saved;
 * SAVE asks the same; two songs deleted, SAVE writes their deletions first, then the rest. At 560 again: a song deleted
 * and two patterns made, the song's deletion saved first and the rest refused; a pattern deleted beside them refused
 * whole (that deletion cannot go first), saved once a pattern less is made. The one-of-a-kind records: DJ's stand the
 * save puts in counted before it; the EDITOR's pattern alone saved (the settings record keeps it); DJ's setup written,
 * then back at its values: still a record. A built-in cartridge changed and saved, then BACK TO BUILT-IN with a new song
 * at 560: its deletion goes first. A set of 16 loops of 128 rows, a note every 2 rows: 2 parts (the firmware's loops
 * test stores the same set in 2), so 1 over at 559. Run by the harness:
 *   node tests/playground/harness.js tests/playground/records.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t;
    const over = () => M.recordsOver(U.keepChanged());
    const asking = () => t.top() === "question" ? U.top().lines.join(" / ") : "";
    const LIST = "TOO MANY RECORDS TO SAVE / 1 MORE THAN THE FM-1 LISTS / DELETE SOMETHING, THEN SAVE";
    async function save() {                                     /* SAVE's tap, then its end (a toast or a question) */
        await t.tap("SAVE");
        await t.sleep(100);
        for (let i = 0; i < 40 && t.top() === "progress"; i++) await t.sleep(100);
    }
    ok(M.recordKeys(M.state, U.inst).size === 121, "the demo disk's records: " + M.recordKeys(M.state, U.inst).size +
       " (5 samplers, FUSION, 33 samples, 64 patterns, 18 songs; the built-ins none)");
    /* filled to 560: cartridges 00-9F (FUSION's, emptied), patterns at every free number but EE and EF (empty), songs from
     * 00 (A0's copies) */
    const A5 = M.cart(0xA5), song = M.state.songs[0xA0];
    U.setInst(0xA000);                                          /* (kept by the first SAVE: the settings record a key) */
    U.edit(() => {
        for (let id = 0; id < 0xA0; id++) M.state.carts.push(Object.assign(structuredClone(A5), { id, name: "JUNK", slots: A5.slots.map(() => null) }));
        M.state.carts.sort(M.cartCmp);
        for (let n = 0; n < 0xEE; n++) if (!M.state.patterns[n]) M.state.patterns[n] = M.newPattern(n, 1, 16);
        for (let n = 0; over() < 0; n++) if (!M.state.songs[n]) M.state.songs[n] = Object.assign(structuredClone(song), { name: "JUNK " + M.hex(n) });
    });
    await save();
    ok(!over() && !M.unsaved && U.toastNow() === "SAVED" && !asking(), "560 records: saved (" + U.toastNow() + ", " + (over() + 560) +
       " listed, " + M.unsaved + " unsaved)");
    /* a sampler's MOVE in the cartridge list, YES: refused whole */
    U.setInst(0xA000);
    await t.homeKey(5);
    await t.tap("ENV");
    U.top().sel = M.cartIndex(0xA0);
    await t.tap("SEL");
    let m = U.top();
    const has = !!(m && m.items && m.items.indexOf("MOVE") >= 0);
    if (has) { m.sel = m.items.indexOf("MOVE"); await t.tap("SEL"); }
    await t.turn("SELECT", 1);
    await t.tap("SEL");                                         /* put down: the question */
    const refs = t.top() === "question" && U.top().lines[1] === "UPDATE REFERENCES?";
    await t.tap("OCT+");                                        /* YES */
    await t.sleep(300);
    ok(has && refs && asking() === LIST && !!M.cart(0xA6) && !M.cart(0xA0) && M.unsaved > 0 && !!M.savedThing("cart", 0xA0) &&
       !M.savedThing("cart", 0xA6), "at 560, a sampler's MOVE answered YES (to A6): refused whole: " + asking() + "; the edits kept (" +
       M.unsaved + " unsaved), nothing saved (A0 saved " + !!M.savedThing("cart", 0xA0) + ", A6 " + !!M.savedThing("cart", 0xA6) + ")");
    t.shot("records_full");
    await t.tap("OCT-");                                        /* BACK */
    await save();
    ok(asking() === LIST && M.unsaved > 0 && !M.savedThing("cart", 0xA6), "... SAVE asks the same, nothing saved");
    await t.tap("HOME");
    for (let i = 0; i < 4 && U.top(); i++) await t.tap("HOME");
    /* two songs deleted: their deletions written first (no new song among the changes), then the rest */
    U.edit(() => { M.state.songs[0] = null; M.state.songs[1] = null; });
    await save();
    ok(!asking() && U.toastNow() === "SAVED" && !M.unsaved && !!M.savedThing("cart", 0xA6) && !M.savedThing("cart", 0xA0) &&
       !M.savedThing("song", 0) && over() === -2, "two songs deleted: SAVE writes it (" + U.toastNow() + "; " + (over() + 560) +
       " live; the FM-1 lists the deletions too, till a save at the limit reclaims them), the cartridge at A6");
    /* 560 again; a song deleted and two patterns made: the song's deletion saved first, the rest 1 over */
    U.edit(() => { M.state.songs[0] = structuredClone(song); M.state.songs[1] = structuredClone(song); });
    await save();
    ok(!over() && !M.unsaved, "the two songs again: 560 listed (" + (over() + 560) + ")");
    U.edit(() => { M.state.songs[2] = null; M.state.patterns[0xEE] = M.newPattern(0xEE, 1, 16); M.state.patterns[0xEF] = M.newPattern(0xEF, 1, 16); });
    await save();
    ok(asking() === LIST && !M.savedThing("song", 2) && !M.savedThing("pattern", 0xEE) && M.unsaved === 2,
       "a song deleted, patterns EE and EF made: the deletion saved first (song 02 saved " + !!M.savedThing("song", 2) +
       "), the rest refused: " + asking() + " (" + M.unsaved + " unsaved)");
    await t.tap("HOME");
    /* pattern 00 deleted beside them: no deletion of a pattern goes first while patterns are made: refused whole */
    U.edit(() => { M.state.patterns[0] = null; });
    await save();
    ok(asking() === LIST && !!M.savedThing("pattern", 0) && M.unsaved === 3, "pattern 00 deleted beside them: refused whole (" +
       asking() + "), nothing saved (00 saved " + !!M.savedThing("pattern", 0) + ")");
    await t.tap("HOME");
    U.edit(() => { M.state.patterns[0xEF] = null; });
    await save();
    ok(!asking() && U.toastNow() === "SAVED" && !M.unsaved && !M.savedThing("pattern", 0) && !!M.savedThing("pattern", 0xEE) &&
       over() === -1, "one pattern made, not two: saved (" + U.toastNow() + ", " + (over() + 560) + " live; the FM-1 lists " +
       "00's deletion too)");
    /* the one-of-a-kind records: DJ's stand the save would put in counted before it (no DJ record yet) */
    ok(M.recordsOver(false, { moved: true }) === M.recordsOver(false) + 1, "DJ's decks moved, no DJ record yet: the save's DJ record " +
       "counted before it (" + M.recordsOver(false, { moved: true }) + ", " + M.recordsOver(false) + ")");
    /* the EDITOR's pattern changed alone: the settings record keeps it, so SAVE saves */
    U.pattern = 0x01;
    await save();
    ok(U.toastNow() === "SAVED" && U.savedPat === 0x01 && !U.keepChanged(), "the EDITOR's pattern changed alone: SAVE writes it (" +
       U.toastNow() + ")");
    /* DJ's setup written, then back at its values: still a record, as the FM-1 keeps one once written */
    const reso = M.state.dj.reso;
    U.edit(() => { M.state.dj.reso = reso + 1; });
    await save();
    const o1 = over();
    U.edit(() => { M.state.dj.reso = reso; });
    await save();
    ok(o1 === 0 && over() === 0 && U.toastNow() === "SAVED", "DJ's setup saved (" + (o1 + 560) + " listed), then back at its values: " +
       "its record still listed (" + (over() + 560) + ")");
    U.edit(() => { M.state.songs[0x70] = structuredClone(song); });
    await save();
    ok(asking() === LIST, "... so a new song at 560 is 1 over: " + asking());
    await t.tap("HOME");
    U.edit(() => { M.state.songs[0x70] = null; });
    /* a built-in cartridge changed and saved; then BACK TO BUILT-IN with a new song at 560: the built-in's deletion goes
     * first (no new cartridge), then the rest */
    U.edit(() => { M.state.songs[0] = null; });
    await save();
    const f0 = M.cart(0xF0).name;
    U.edit(() => { M.cart(0xF0).name = "CHANGED"; });
    await save();
    const o2 = over();
    U.edit(() => { M.state.carts[M.cartIndex(0xF0)] = M.builtinOriginal("cart", 0xF0); M.state.songs[0x70] = structuredClone(song); });
    await save();
    ok(o2 === 0 && !asking() && U.toastNow() === "SAVED" && (M.savedThing("cart", 0xF0) || {}).name === f0 && !!M.savedThing("song", 0x70) &&
       !over(), "F0 changed and saved (" + (o2 + 560) + " listed), then BACK TO BUILT-IN and a new song: F0's deletion first, the rest " +
       "saved (" + (asking() || U.toastNow()) + ", " + (over() + 560) + " listed)");
    /* a set of 16 loops of 128 rows, a note with its volume every 2 rows: 256 B a stream as the FM-1 encodes it, so 2
     * parts (14 streams beside the head in part 0); at 559 it is 1 over */
    U.edit(() => { M.state.songs[0x70] = null; });
    await save();
    const loopOf = i => ({ inst: 0xF000 | i, col: 0, mono: false, lanes: 1, base: 128, repeats: 1, muted: false, soloed: false, shift: 0,
                           cells: Array.from({ length: 512 }, (_, k) => k % 8 ? null : { note: 60, vol: 127, cmds: [] }) });
    U.edit(() => { M.state.lsets[0x10] = { n: 0x10, name: "BIG", loops: Object.assign(M.newLoops(), { rows: 128, list: Array.from({ length: 16 }, (_, i) => loopOf(i)) }) }; });
    const sparts = [...M.recordKeys(M.state)].filter(k => k.startsWith("36.16.")).length;
    await save();
    ok(M.streamOf(M.state.lsets[0x10].loops.list[0].cells) === 256 && sparts === 2 && asking() === LIST,
       "a set of 16 loops of 128 rows, a note every 2 rows: streams of " + M.streamOf(M.state.lsets[0x10].loops.list[0].cells) + " B, " +
       sparts + " parts; at 559: " + asking());
    await t.tap("HOME");
};
