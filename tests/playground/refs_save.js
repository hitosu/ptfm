// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* UPDATE REFERENCES? (specs/ui/saving.md#references; fw/tests/ui3_test.c's test_refs_save): YES makes the change with
 * its references, pauses playback and writes everything pending as one save, UPDATING n REFERENCES, then REFERENCES
 * UPDATED, SAVED once written, nothing unsaved after; a used slot SWAPped with the next one, then back. Run by the
 * harness:
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
    const u0 = M.instUses(ref), u1 = M.instUses(ref + 1);
    async function swap(what) {
        P.playSong(0xA0); await t.sleep(300);
        const played = P.playing;
        U.setInst(ref);
        await t.homeKey(5);                                     /* (INSTRUMENTS) */
        await t.tap("SEL");
        const m = U.top();
        if (!m || !m.items || m.items.indexOf("SWAP") < 0) { ok(false, "the slot menu has SWAP"); return; }
        m.sel = m.items.indexOf("SWAP"); await t.tap("SEL");
        await t.turn("SELECT", 1);
        await t.tap("SEL");                                     /* put down: the question */
        const asked = t.top() === "question" && U.top().lines[1] === "UPDATE REFERENCES?";
        await t.tap("OCT+");                                    /* YES */
        const bar = t.top() === "progress" && U.top().text === "UPDATING " + (u0 + u1) + " REFERENCES";
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
    await swap("SWAP back");
    ok(M.instUses(ref) === u0 && M.instUses(ref + 1) === u1, "... and back");
    /* a sampler's MOVE in the cartridge list (specs/ui/screens/instruments.md): as any cartridge's, its instruments only (its
     * samples, records of their own, stay where they are): with a change of its own not saved (no SAVE IT FIRST), YES
     * moves it with its references and saves everything pending as one save; its instruments keep naming their samples */
    const sc = M.cart(0xA0), cartUses = id => (M.cart(id) || { slots: [] }).slots.reduce((a, s, k) => a + (s ? M.instUses(id << 8 | k) : 0), 0);
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
    await moveCart(to, -1, "back to A0");
    U.edit(() => { M.cart(0xA0).name = name1; });
    M.save();
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
