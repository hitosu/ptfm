/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FAVORITES (specs/ui/screens/instruments.md#favorites; fw/tests/ui3_test.c's test_favorites, whose shots these match by
 * name): ADD TO FAVORITES and its toasts, SEL + GLO there and back (and refused in edit mode, NO FAVORITES YET), browsing
 * (the place a favourite, the instrument its source; PRESETS skipping the empty and missing ones; an empty or missing one
 * keeps the instrument), a favourite's edit mode (K1, PRESETS, HOME drops, EDIT keeps, an empty one starts at the
 * instrument), REMOVE, MOVE asking nothing, PASTE's refusals, NEW CARTRIDGE, the lists' order, CHECK REFERENCES, the
 * references' rewrite, FB emptied as the firmware's own. Run by the harness:
 *   node tests/playground/harness.js tests/playground/favorites.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t;
    const FB = M.cart(0xFB), src = i => { const f = FB.slots[i]; return f ? f.src : -1; };
    const selGlo = async () => { t.down("SEL"); await t.sleep(30); t.down("GLO"); await t.sleep(30); t.up("GLO"); await t.sleep(30); t.up("SEL"); await t.sleep(300); };
    const pick = async item => {
        await t.tap("SEL"); await t.sleep(200);
        const m = U.top();
        if (!m || m.kind !== "menu") return false;
        m.goto(item);
        await t.tap("SEL"); await t.sleep(200);
        return true;
    };
    ok(M.state.carts[0].id === 0xFB && FB.type === "FAVORITES" && FB.name === "FAVORITES" && FB.builtin,
       "FAVORITES: the built-in FB, empty, first in the lists' order");
    U.setInst(0xF000);
    await t.homeKey(5);
    ok(U.screen.name === "INSTRUMENTS", "FAVORITES: on INSTRUMENTS");
    await selGlo();
    ok(U.place === 0xF000 && U.toastNow() === "NO FAVORITES YET", "SEL + GLO with no favourite: NO FAVORITES YET, no switch");
    await pick("ADD TO FAVORITES");
    ok(src(0) === 0xF000 && U.toastNow() === "ADDED TO FB:00", "ADD TO FAVORITES: into FB's first empty slot, ADDED TO FB:00 (" + U.toastNow() + ")");
    await pick("ADD TO FAVORITES");
    ok(src(1) < 0 && U.toastNow() === "ALREADY IN FB:00", "... again: ALREADY IN FB:00, nothing added");
    await pick("MARK");
    await t.turn("SELECT", 3);
    await pick("ADD TO FAVORITES");
    ok(src(1) === 0xF001 && src(2) === 0xF002 && src(3) === 0xF003 && src(4) < 0 && U.toastNow() === "ADDED TO FB:01",
       "marked slots: each its slot, the one there skipped (" + U.toastNow() + ")");
    /* references: a favourite is a use (the questions count it) */
    ok(M.instUses(0xF003) >= 1, "a favourite counts as a use");
    /* SEL + GLO: to FB at its first favourite, the instrument its source; again: back */
    U.setInst(0xF005);
    await selGlo();
    ok(U.place === 0xFB00 && U.inst === 0xF000 && U.knobNow() === "CARTRIDGE FB FAVORITES",
       "SEL + GLO: to FB, at its first favourite; the instrument its source; the header names the cartridge (" + U.knobNow() + ")");
    t.shot("fav_browse");
    await t.turn("PRESETS", 1);
    ok(U.place === 0xFB01 && U.inst === 0xF001, "PRESETS: the next favourite, its source the instrument");
    await selGlo();
    ok(U.place === 0xF005 && U.inst === 0xF005, "SEL + GLO again: back to the place before");
    await selGlo();
    ok(U.place === 0xFB01, "... and to FB again at the favourite last used there");
    /* GLO held + PRESETS: a favourite once (the place stays), its list while GLO is held, GLO's tap not done; refused on
     * INSTRUMENTS (GLO blinks); GLO's layer says it, and GLO let go after it opens nothing */
    await selGlo();                                         /* (back: F0:05) */
    t.down("GLO"); await t.sleep(30); await t.turn("PRESETS", 1); t.up("GLO"); await t.sleep(300);
    ok(U.place === 0xF005 && U.inst === 0xF005 && !U.top(), "GLO + PRESETS refused on INSTRUMENTS: nothing picked, no list");
    await t.homeKey(1);                                     /* SONGS */
    t.down("GLO"); await t.sleep(30);
    await t.turn("PRESETS", 1);
    ok(U.place === 0xF005 && U.inst === 0xF002 && t.top() === "list" && (U.knobNow() || "").startsWith("PRESETS F0:02 "),
       "GLO + PRESETS: FB:02 (after FB:01) once, the place kept, the list, the header (" + M.refText(U.place) + " " +
       M.refText(U.inst) + " " + U.knobNow() + ")");
    t.shot("fav_glo_list");
    await t.sleep(1500); t.frame();
    ok(t.top() === "list", "... the list stays while GLO is held");
    await t.turn("PRESETS", -2);
    ok(U.inst === 0xF000 && U.place === 0xF005, "... two back: FB:00's source");
    t.up("GLO"); await t.sleep(300);
    ok(!U.top() && U.inst === 0xF000, "GLO let go: the list gone, no properties (its tap not done), the source kept");
    await t.turn("PRESETS", 1);
    await t.sleep(1200); t.frame();
    ok(U.place === 0xF006 && U.inst === 0xF006, "PRESETS alone: on from the place as it was (F0:05 -> F0:06)");
    await selGlo();
    ok(U.place === 0xFB00 && U.inst === 0xF000, "SEL + GLO: to the favourite GLO + PRESETS used last");
    await selGlo();
    ok(U.place === 0xF006, "... and back");
    t.down("GLO"); await t.sleep(1500); t.frame();          /* (held past the hold delay: its layer) */
    t.shot("fav_glo_layer");
    t.up("GLO"); await t.sleep(300);
    ok(!U.top(), "GLO held past the hold delay (its layer) and let go: nothing opens");
    await t.tap("GLO"); await t.sleep(300);
    ok(!!U.top(), "GLO's tap: the song's properties, as ever");
    for (let i = 0; i < 4 && U.top(); i++) await t.tap("HOME");
    await t.homeKey(5);                                     /* (INSTRUMENTS again) */
    U.setInst(0xFB01);
    /* an empty and a missing favourite keep the instrument; PRESETS skips them */
    U.edit(() => { FB.slots[5] = { src: 0xF07F }; });
    U.setInst(0xFB05);
    ok(U.place === 0xFB05 && U.inst === 0xF001, "a missing favourite: the place, the instrument stays");
    t.shot("fav_missing");
    U.setInst(0xFB04);
    ok(U.place === 0xFB04 && U.inst === 0xF001, "an empty favourite: the same");
    t.shot("fav_empty");
    U.setInst(0xFB03);
    await t.turn("PRESETS", 1);
    ok(U.place === 0xFB00, "PRESETS skips the empty and missing ones (wrapping to FB:00)");
    /* a favourite's edit mode */
    U.setInst(0xFB01);
    await t.tap("EDIT");
    ok(U.screen.editMode() === 2, "EDIT on a favourite: its edit mode");
    t.shot("fav_edit");
    await t.turn("K1", 1);
    ok(src(1) === 0xF100 && U.inst === 0xF100 && U.place === 0xFB01 && t.top() === "list",
       "K1: the next cartridge with an instrument, at its first, the list over the screen (" + M.refText(src(1)) + ")");
    t.shot("fav_edit_k1");
    await t.sleep(1200); t.frame();
    ok(!U.top(), "... its cartridge list gone a second after the last click");
    await t.turn("PRESETS", 1);
    ok(src(1) === 0xF101 && U.inst === 0xF101, "PRESETS: that cartridge's next instrument");
    await selGlo();
    ok(U.place === 0xFB01 && U.screen.editMode() === 2, "SEL + GLO refused in edit mode");
    t.down("SEL"); await t.sleep(30); await t.turn("PRESETS", -1); t.up("SEL"); await t.sleep(300);   /* (FB first: back past it) */
    ok(M.cart(U.place >> 8).type === "FAVORITES" && U.screen.editMode() === 2,
       "SEL + PRESETS in a favourite's edit mode: the FAVORITES cartridges only (" + M.refText(U.place) + ")");
    for (let i = 0; i < 4 && U.top(); i++) await t.tap("HOME");
    U.setInst(0xFB01);
    await t.sleep(1200); t.frame();                         /* (PRESETS' instrument list gone by itself) */
    await t.tap("HOME");
    ok(!U.screen.editMode() && src(1) === 0xF001 && U.inst === 0xF001, "HOME drops it: the favourite and instrument as before");
    await t.tap("EDIT");
    await t.turn("PRESETS", 1);
    await t.sleep(1200); t.frame();                         /* (PRESETS' instrument list gone by itself) */
    await t.tap("EDIT");
    ok(!U.screen.editMode() && src(1) === 0xF002 && U.inst === 0xF002, "EDIT keeps it (" + U.screen.editMode() + " " +
       M.refText(src(1)) + " " + M.refText(U.inst) + " " + U.screen.name + ")");
    const c1 = M.state.carts[1];                            /* (the cartridge after FB in the lists' order) */
    U.setInst(c1.id << 8 | U.cartFirst(c1));
    await t.tap("EDIT");
    t.down("SEL"); await t.sleep(30); await t.turn("PRESETS", -1); t.up("SEL"); await t.sleep(300);
    ok(M.cart(U.place >> 8).type !== "FAVORITES" && U.screen.editMode() === 1,
       "SEL + PRESETS in an instrument's edit mode: past the FAVORITES cartridges (" + M.refText(U.place) + ")");
    for (let i = 0; i < 4 && U.top(); i++) await t.tap("HOME");
    await t.tap("HOME");
    U.setInst(0xFB01);                                      /* (the favourite again, its source F0:02 the instrument) */
    U.setInst(0xFB06);
    await t.tap("EDIT");
    ok(src(6) === 0xF002, "EDIT on an empty favourite: it starts at the current instrument");
    await t.tap("EDIT");
    /* REMOVE, MOVE asking nothing, PASTE's refusals */
    await pick("REMOVE");
    ok(src(6) < 0 && U.inst === 0xF002, "REMOVE: the favourite goes, the instrument stays");
    U.setInst(0xFB00);
    await pick("MOVE");
    await t.turn("SELECT", 1);
    await t.tap("SEL");
    ok(t.top() !== "question" && src(0) < 0 && src(4) === 0xF000, "MOVE in FAVORITES asks nothing");
    U.setInst(0xFB01);
    await pick("COPY");
    U.setInst(0xF00A);
    await pick("PASTE");
    ok(U.toastNow() === "COPIED FROM FAVORITES: NOT HERE", "a favourite pasted into a DX7 cartridge: refused");
    await pick("COPY");
    U.setInst(0xFB08);
    await pick("PASTE");
    ok(U.toastNow() === "COPIED FROM DX7: NOT HERE" && src(8) < 0, "an instrument pasted into FAVORITES: refused");
    /* NEW CARTRIDGE: FAVORITES, named MY FAVORITES, opened; then ADD TO FAVORITES a group of both */
    await pick("FAVORITES");
    const c2 = M.cart(U.place >> 8);
    ok(c2 && c2.type === "FAVORITES" && c2.name === "MY FAVORITES" && c2.id < 0xF0 && U.toastNow() === "CARTRIDGE " + M.hex(c2.id) + " MADE",
       "NEW CARTRIDGE, FAVORITES: made and opened (" + U.toastNow() + ")");
    ok(M.state.carts[0].id === 0xFB && M.state.carts[1] === c2, "the lists' order: FB, then the user's FAVORITES, then the rest");
    U.setInst(0xF00A);
    await t.tap("SEL");
    U.top().level = null;                                   /* (it reopens in the group last used: NEW CARTRIDGE's) */
    U.top().sel = U.top().top.findIndex(x => x.group === "ADD TO FAVORITES");
    await t.tap("SEL");
    ok(t.top() === "menu" && U.top().level && U.top().items.join() === "BACK,FB FAVORITES," + M.hex(c2.id) + " MY FAVORITES",
       "with two FAVORITES cartridges, ADD TO FAVORITES is a group: " + (U.top() && U.top().items.join()));
    t.shot("fav_add_group");
    while (U.top()) await t.tap("HOME");
    M.revertThing("cart", c2.id);
    M.revertThing("cart", 0xFB);
    ok(!M.cart(c2.id) && !M.cart(0xFB).slots.some(Boolean), "(their REVERTs: nothing of the test left)");
    /* FB emptied by REMOVE is the firmware's own again */
    U.setInst(0xF000);
    await t.homeKey(5);
    await pick("ADD TO FAVORITES");
    U.setInst(0xFB00);
    await pick("REMOVE");
    ok(!M.builtinChanged("cart", M.cart(0xFB)), "FB emptied: the firmware's own again");
    while (U.top()) await t.tap("HOME");
    await t.homeKey(1);
    U.setInst(0xF000);
};
