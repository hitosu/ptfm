// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* UPDATE REFERENCES? (specs/ui/saving.md#references; fw/tests/ui3_test.c's test_refs_save): YES makes the change with
 * its references, pauses playback and writes everything pending as one save, UPDATING n REFERENCES, then REFERENCES
 * UPDATED, SAVED once written, nothing unsaved after; a used slot SWAPped with the next one, then back; CHECK REFERENCES
 * and the loops (test_refs_loops). Run by the harness:
 *   node tests/playground/harness.js tests/playground/refs_save.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    let ref = -1;
    for (const c of M.state.carts) {
        if (c.id > 0xEF || ref >= 0) continue;
        for (let k = 0; k + 1 < c.size && ref < 0; k++) if (c.slots[k] && M.instUses(c.id << 8 | k)) ref = c.id << 8 | k;
    }
    ok(ref >= 0, "UPDATE REFERENCES?: the demo disk has a used instrument in the user area");
    if (ref < 0) return;
    /* the loops are references too (specs/files/references.md): LOOPS' own loop and a set's naming it, a use each */
    const loopOf = r => ({ inst: r, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted: false, soloed: false, cells: new Array(64).fill(null), shift: 0 });
    const loops0 = structuredClone(M.state.loops);
    U.edit(() => { const j = M.state.loops; j.list = [loopOf(ref)]; j.rows = 16; M.state.lsets[7] = { n: 7, name: "REFS", loops: structuredClone(j) }; });
    const lpRefs = () => M.state.loops.list[0].inst + "," + M.state.lsets[7].loops.list[0].inst, two = r => r + "," + r;
    const u0 = M.instUses(ref), u1 = M.instUses(ref + 1);
    async function swap(what, item = "SWAP", at = ref, dir = 1, n = u0 + u1) {   /* (MOVE: floated to the next free slot) */
        P.playSong(0xA0); await t.sleep(300);
        const played = P.playing;
        U.setInst(at);
        await t.homeKey(5);                                     /* (INSTRUMENTS) */
        await t.tap("SEL");
        const m = U.top();
        if (!m || !m.items || m.items.indexOf(item) < 0) { ok(false, "the slot menu has " + item); return; }
        m.sel = m.items.indexOf(item); await t.tap("SEL");
        await t.turn("SELECT", dir);
        await t.tap("SEL");                                     /* put down: the question */
        const asked = t.top() === "question" && U.top().lines[1] === "UPDATE REFERENCES?";
        await t.tap("OCT+");                                    /* YES */
        const bar = t.top() === "progress" && U.top().text === "UPDATING " + n + " REFERENCES";
        const early = U.toastNow() === "REFERENCES UPDATED, SAVED", paused = !P.playing;
        for (let i = 0; i < 40 && t.top() === "progress"; i++) await t.sleep(100);
        ok(asked && played && bar && paused && !early && U.toastNow() === "REFERENCES UPDATED, SAVED" && !M.unsaved,
           "UPDATE REFERENCES? YES (" + what + "): asked " + asked + ", UPDATING " + (u0 + u1) + " REFERENCES " + bar +
           ", playback paused " + paused + " (played " + played + "), the toast only once written (" + U.toastNow() +
           ", early " + early + "), nothing unsaved (" + M.unsaved + ")");
        await t.tap("HOME");
    }
    await swap("SWAP");
    ok(M.instUses(ref + 1) === u0 && M.instUses(ref) === u1, "... the references along with the slots");
    ok(lpRefs() === two(ref + 1), "... LOOPS' loop and the set's with them (" + lpRefs() + ")");
    await swap("SWAP back");
    ok(M.instUses(ref) === u0 && M.instUses(ref + 1) === u1, "... and back");
    ok(lpRefs() === two(ref), "... the loops back too");
    /* a slot's MOVE (to the next free slot) and back: LOOPS' loop and the set's follow */
    const c0 = M.cart(ref >> 8);
    let fr = (ref & 255) + 1;
    while (fr < c0.size && c0.slots[fr]) fr++;
    if (fr < c0.size) {
        await swap("MOVE", "MOVE", ref, 1, u0);
        ok(!c0.slots[ref & 255] && lpRefs() === two(ref >> 8 << 8 | fr) && M.instUses(ref >> 8 << 8 | fr) === u0,
           "a slot's MOVE answered YES: LOOPS' loop and the set's follow it (" + lpRefs() + ")");
        await swap("MOVE back", "MOVE", ref >> 8 << 8 | fr, -1, u0);
        ok(lpRefs() === two(ref) && M.instUses(ref) === u0, "... and back");
    } else ok(false, "a free slot after " + M.refText(ref));
    /* a sampler's MOVE in the cartridge list (specs/ui/screens/instruments.md): as any cartridge's, its instruments only (its
     * samples, records of their own, stay where they are): with a change of its own not saved (no SAVE IT FIRST), YES
     * moves it with its references and saves everything pending as one save; its instruments keep naming their samples */
    const sc = M.cart(0xA0), s0 = sc.slots.findIndex(Boolean), kr = 0xA000 | s0;   /* (the loops on its first instrument) */
    U.edit(() => { M.state.loops.list[0].inst = kr; M.state.lsets[7].loops.list[0].inst = kr; });
    const cartUses = id => (M.cart(id) || { slots: [] }).slots.reduce((a, s, k) => a + (s ? M.instUses(id << 8 | k) : 0), 0);
    const named = id => (M.cart(id) || { slots: [] }).slots.map(s => s ? s.p.sample : -1).join();
    const uses1 = cartUses(0xA0), names1 = named(0xA0), to = 0xA6, name1 = sc.name;   /* (the next free one up) */
    async function moveCart(from, clicks, what) {
        U.setInst(from << 8);
        await t.homeKey(5);
        await t.tap("ENV");
        U.top().sel = M.cartIndex(from);
        await t.tap("SEL");
        const m = U.top();
        if (!m || !m.items || m.items.indexOf("MOVE") < 0) { ok(false, "the cartridge's menu has MOVE"); return; }
        m.sel = m.items.indexOf("MOVE"); await t.tap("SEL");
        await t.turn("SELECT", clicks);
        await t.tap("SEL");                                     /* put down: the question */
        const asked = t.top() === "question" && U.top().lines[1] === "UPDATE REFERENCES?";
        await t.tap("OCT+");                                    /* YES */
        const bar = t.top() === "progress" && U.top().text === "UPDATING " + uses1 + " REFERENCES";
        for (let i = 0; i < 40 && t.top() === "progress"; i++) await t.sleep(100);
        const at = from === 0xA0 ? to : 0xA0;
        ok(asked && bar && !!M.cart(at) && !M.cart(from) && named(at) === names1 && cartUses(at) === uses1 && !M.unsaved &&
           U.toastNow() === "REFERENCES UPDATED, SAVED", "a sampler's MOVE (" + what + "): asked " + asked + ", UPDATING " + uses1 +
           " REFERENCES " + bar + ", moved with its references, its instruments on their samples, nothing unsaved (" + M.unsaved +
           ", " + U.toastNow() + ")");
        if (t.top()) await t.tap("HOME");
    }
    ok(uses1 > 0 && sc.type === "SAMPLER", "a sampler's MOVE: the demo disk's DRUM KIT is used (" + uses1 + " times)");
    U.edit(() => { sc.name = "MOVED KIT"; });                 /* (a change of its own, not saved) */
    await moveCart(0xA0, 1, "to " + M.hex(to));
    ok(lpRefs() === two(to << 8 | s0), "... LOOPS' loop and the set's with it, by cartridge, the slot kept (" + lpRefs() + ")");
    await moveCart(to, -1, "back to A0");
    ok(lpRefs() === two(kr), "... and back");
    U.edit(() => { M.cart(0xA0).name = name1; M.state.loops = loops0; M.state.lsets[7] = null; });   /* (the loops as they were) */
    M.save();
    /* CHECK REFERENCES and the loops (ui3's test_refs_loops): LOOPS' loop and a set's naming an instrument deleted are
     * listed after everything else and counted; REVERT ALL, and nothing is missing */
    {
        const runTool = async name => {
            await t.homeKey(20); await t.turn("SELECT", -M.SETTINGS.length); await t.turn("SELECT", M.SETTINGS.findIndex(s => s.k === "TOOLS"));
            U.pickTool(name); await t.tap("EDIT");
        };
        let c = null, k = -1;                                   /* (an instrument of the user area's nothing uses) */
        for (const x of M.state.carts) {
            if (c || x.id > 0xEF || x.type === "FAVORITES") continue;
            k = x.slots.findIndex((sl, j) => sl && !M.instUses(x.id << 8 | j));
            if (k >= 0) c = x;
        }
        ok(!!c && !M.state.lsets[7], "(an instrument nothing uses, no set 07)");
        const gone = c ? c.id << 8 | k : 0;
        for (let i = 0; i < 4 && t.top(); i++) await t.tap("HOME");
        if (c) U.edit(() => { const j = M.state.loops; j.list = [loopOf(gone)]; M.state.lsets[7] = { n: 7, name: "", loops: structuredClone(j) }; c.slots[k] = null; });
        await runTool("CHECK REFERENCES"); await t.sleep(900);
        const ls = t.top() === "list" ? U.top() : null, want = ["LOOP 00: " + M.refText(gone), "SET 07 LOOP 00: " + M.refText(gone)];
        ok(!!ls && ls.title === "MISSING REFERENCES: 2" && ls.lines.join() === want.join(), "CHECK REFERENCES: LOOPS' loop and a set's " +
           "naming an instrument deleted (" + (ls ? ls.title + ", " + ls.lines.join(", ") : t.top()) + ")");
        t.shot("ui3_check_refs_loops");
        if (t.top()) await t.tap("HOME");
        M.revert();                                             /* (REVERT ALL) */
        await runTool("CHECK REFERENCES"); await t.sleep(900);
        ok(!t.top() && U.toastNow() === "NO MISSING REFERENCES" && !!M.inst(gone) && !M.state.lsets[7],
           "... REVERT ALL: the instrument and the loops as they were, nothing missing");
    }
    /* YES with the edit memory nearly full (ui3's test_refs_full): a slot's MOVE, a SWAP, a cartridge's MOVE and its
     * DELETE refused whole (MEMORY FOR EDITS IS FULL): no slot, pattern, favourite or loop changed, nothing saved */
    {
        const W = [["a slot's MOVE", "MOVE", false, 1], ["a SWAP", "SWAP", false, 1], ["a cartridge's MOVE", "MOVE", true, 1],
                   ["a cartridge's DELETE", "DELETE", true, 0]];
        M.state.junk = "";
        M.settle();
        M.state.junk = "x".repeat(Math.max(0, (M.EDITS_MAX - 300 - M.editsUsed - 12) * 4));   /* (an edit nothing reads, ~300 B left) */
        M.settle();
        const left = M.EDITS_MAX - M.editsUsed, J = () => JSON.stringify([M.state.carts, M.state.patterns, M.state.loops, M.state.lsets]);
        const state0 = J(), unsaved0 = M.unsaved, u0 = M.instUses(0xA000);
        let saved = false;
        const save0 = U.saveRefs;
        U.saveRefs = n => { saved = true; save0(n); };
        for (const [what, item, list, dir] of W) {
            U.setInst(0xA000);
            await t.homeKey(5);
            if (list) { await t.tap("ENV"); U.top().sel = M.cartIndex(0xA0); }
            await t.tap("SEL");
            const m = U.top(), has = !!(m && m.items && m.items.indexOf(item) >= 0);
            if (has) { m.sel = m.items.indexOf(item); await t.tap("SEL"); }
            if (dir) { await t.turn("SELECT", dir); await t.tap("SEL"); }
            const asked = t.top() === "question" && U.top().lines[1] === "UPDATE REFERENCES?";
            await t.tap("OCT+");                                /* YES */
            await t.sleep(300);
            const full = t.top() === "question" && U.top().lines[0] === "MEMORY FOR EDITS IS FULL";
            ok(has && asked && full && J() === state0 && M.unsaved === unsaved0 && !saved && M.instUses(0xA000) === u0,
               "UPDATE REFERENCES? YES, the edit memory nearly full (" + left + " B left): " + what + " of A0:00 refused whole: nothing " +
               "changed, nothing saved (item " + has + ", asked " + asked + ", full " + full + ", saved " + saved + ")");
            for (let i = 0; i < 5 && U.top(); i++) await t.tap("HOME");
        }
        U.saveRefs = save0;
        delete M.state.junk;
        M.settle();
    }
    /* a MOVE answered YES of an instrument many patterns play goes through when the tracks that name it fit, though the
     * whole patterns would not (ui3's test_refs_fits): the edit memory left between the two */
    {
        const cb = c => c ? 2 + (c.note != null) + (c.inst != null) * 2 + (c.vol != null) + c.cmds.length * 2 : 0;
        let whole = 0, np = 0;
        for (const p of M.state.patterns) {
            if (!p || !p.cells.some(c => c && c.inst === 0xA000)) continue;
            np++;
            for (let tr = 0; tr < p.tracks; tr++) { let b = 1; for (let r = 0; r < p.rows; r++) b += cb(p.cells[r * 16 + tr]); whole += M.pieceRoom(b); }
        }
        const c0 = M.cart(0xA0), u0 = M.instUses(0xA000);
        let fr = 1;
        while (fr < c0.size && c0.slots[fr]) fr++;
        M.state.junk = "";
        M.settle();
        M.state.junk = "x".repeat(Math.max(0, (M.EDITS_MAX - (whole - 1024) - M.editsUsed - 12) * 4));
        M.settle();
        const left = M.EDITS_MAX - M.editsUsed;
        await swap("MOVE, the tracks that name it fitting", "MOVE", 0xA000, 1, u0);
        ok(np > 1 && left < whole && !c0.slots[0] && M.instUses(0xA000 | fr) === u0,
           "UPDATE REFERENCES? YES, " + np + " patterns playing A0:00: the whole patterns " + whole + " B, " + left + " left: the MOVE goes through");
        delete M.state.junk;
        M.save();
        await swap("MOVE back", "MOVE", 0xA000 | fr, -1, u0);
        ok(!!c0.slots[0] && M.instUses(0xA000) === u0, "... and back");
    }
    /* a pattern's DELETE, YES: every FUSION slot that played it to no pattern (--), as ui3's test_plist_delete_fusion
     * (last: the DRUM KIT's pattern goes) */
    const fus = M.state.carts.find(c => c.type === "FUSION"), kit = fus && fus.slots.find(s => s && s.name === "DRUM KIT");
    const pat = kit ? kit.p.pattern : null;
    ok(pat != null && pat >= 0xA0 && pat <= 0xA5, "pattern DELETE: the DRUM KIT plays one of the disk's patterns");
    if (pat == null) return;
    await t.homeKey(3);
    U.patternList("TEST", pat, () => {}, U.B.ENV);              /* (opened by ENV: SEL then opens the list's menu) */
    await t.tap("SEL");
    const m = U.top();
    ok(m && m.items && m.items.indexOf("DELETE") >= 0, "pattern DELETE: the pattern list's menu has DELETE");
    m.sel = m.items.indexOf("DELETE"); await t.tap("SEL");
    const asked = t.top() === "question";
    await t.tap("OCT+");                                        /* YES */
    for (let i = 0; i < 40 && t.top() === "progress"; i++) await t.sleep(100);
    ok(asked && !M.state.patterns[pat] && kit.p.pattern == null,
       "pattern DELETE, YES: the FUSION slot that played it set to no pattern (asked " + asked + "; " + M.hex(pat) +
       " gone " + !M.state.patterns[pat] + "; the slot " + kit.p.pattern + "; " + U.toastNow() + ")");
};
