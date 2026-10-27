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
        P.playSong(0); await t.sleep(300);
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
    /* a pattern's DELETE, YES: every FUSION slot that played it to no pattern (--), as ui3's test_plist_delete_fusion
     * (last: the DRUM KIT's pattern goes) */
    const fus = M.state.carts.find(c => c.type === "FUSION"), kit = fus && fus.slots.find(s => s && s.name === "DRUM KIT");
    const pat = kit ? kit.p.pattern : null;
    ok(pat != null && pat >= 0xE0 && pat < 0xF0, "pattern DELETE: the DRUM KIT plays one of the disk's patterns");
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
