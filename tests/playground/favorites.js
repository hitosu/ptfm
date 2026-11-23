/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FAVORITES (specs/ui/screens/instruments.md#favorites; fw/tests/ui3_test.c's test_favorites, whose shots these match by
 * name): ADD TO FAVORITES and REMOVE FROM FAVORITES (the one item, its label both ways, single and marked; its toasts;
 * REMOVE taking every slot of FB naming it), SEL + GLO there and back (and refused in edit mode, NO FAVORITES YET),
 * browsing (the place a favourite, the instrument its source; PRESETS skipping the empty and missing ones; an empty or
 * missing one keeps the instrument), a favourite's edit mode (K1, PRESETS, SEL + PRESETS doing nothing, HOME drops, EDIT
 * keeps, an empty one starts at the instrument), REMOVE, MOVE asking nothing, PASTE's refusals, NEW CARTRIDGE's kinds
 * and no CLONE of FB, the lists' order, CHECK REFERENCES, the references' rewrite, FB emptied as the firmware's own,
 * FAVORITES FULL, FB alone used (an earlier PT/FM's FAVORITES cartridge beside it; FB deleted). Run by the harness:
 *   node tests/playground/harness.js tests/playground/favorites.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t;
    const FB = M.cart(0xFB), src = i => { const f = FB.slots[i]; return f ? f.src : -1; };
    const used = () => FB.slots.filter(Boolean).length;
    const selGlo = async () => { t.down("SEL"); await t.sleep(30); t.down("GLO"); await t.sleep(30); t.up("GLO"); await t.sleep(30); t.up("SEL"); await t.sleep(300); };
    /* the slot menu's item picked (one not in the menu: nothing picked, the menu closed) */
    const pick = async item => {
        await t.tap("SEL"); await t.sleep(200);
        const m = U.top();
        if (!m || m.kind !== "menu") return false;
        if (!m.items.includes(item) && !m.top.some(x => x.group && x.items.includes(item))) { await t.tap("HOME"); return false; }
        m.goto(item);
        await t.tap("SEL"); await t.sleep(200);
        return true;
    };
    /* the slot menu's FAVORITES item: 1 ADD TO FAVORITES, 2 REMOVE FROM FAVORITES, 3 both, 0 none (the menu closed again) */
    const favItem = async () => {
        await t.tap("SEL"); await t.sleep(200);
        const m = U.top(), its = m && m.kind === "menu" ? m.items : [];
        await t.tap("HOME"); await t.sleep(200);
        return (its.includes("ADD TO FAVORITES") ? 1 : 0) | (its.includes("REMOVE FROM FAVORITES") ? 2 : 0);
    };
    const mark = async n => { await pick("MARK"); await t.turn("SELECT", n); };
    ok(M.state.carts[0].id === 0xFB && FB.type === "FAVORITES" && FB.name === "FAVORITES" && FB.builtin,
       "FAVORITES: the built-in FB, empty, first in the lists' order");
    U.setInst(0xF000);
    await t.homeKey(5);
    ok(U.screen.name === "INSTRUMENTS", "FAVORITES: on INSTRUMENTS");
    await selGlo();
    ok(U.place === 0xF000 && U.toastNow() === "NO FAVORITES YET", "SEL + GLO with no favourite: NO FAVORITES YET, no switch");
    ok(await favItem() === 1, "an instrument not in FB: the menu's item reads ADD TO FAVORITES (no REMOVE)");
    await pick("ADD TO FAVORITES");
    ok(src(0) === 0xF000 && U.toastNow() === "ADDED TO FB:00", "ADD TO FAVORITES: into FB's first empty slot, ADDED TO FB:00 (" + U.toastNow() + ")");
    const inFb = await favItem();
    ok(inFb === 2, "... in FB now: the item reads REMOVE FROM FAVORITES, not ADD (" + inFb + ")");
    await t.sleep(2500);                                    /* (the toast gone: the header as the firmware's shot has it) */
    await t.tap("SEL"); await t.sleep(200);
    U.top().goto("REMOVE FROM FAVORITES");
    t.shot("fav_menu_remove");
    await t.tap("HOME"); await t.sleep(200);
    /* REMOVE FROM FAVORITES: every slot of FB naming it emptied, no question; FB as built in again */
    U.edit(() => { FB.slots[0x10] = { src: 0xF000 }; });    /* (a second favourite naming it) */
    await pick("REMOVE FROM FAVORITES");
    ok(src(0) < 0 && src(0x10) < 0 && t.top() !== "question" && U.toastNow() === "REMOVED FROM FAVORITES",
       "REMOVE FROM FAVORITES: every slot of FB naming it emptied, no question (" + U.toastNow() + ")");
    ok(!M.builtinChanged("cart", FB) && await favItem() === 1, "... FB emptied: as built in again (no change of it left); the item reads ADD again");
    await pick("ADD TO FAVORITES");
    await mark(3);                                          /* MARK 00-03: 00 there, 01-03 added */
    ok(await favItem() === 1, "marked slots, one of them in FB, the others not: ADD TO FAVORITES");
    await pick("ADD TO FAVORITES");
    ok(src(1) === 0xF001 && src(2) === 0xF002 && src(3) === 0xF003 && src(4) < 0 && U.toastNow() === "ADDED TO FB:01",
       "marked slots: each its slot, the one there skipped (" + U.toastNow() + ")");
    U.setInst(0xF000);
    await mark(3);                                          /* MARK 00-03 again: all in FB */
    const allIn = await favItem();
    await t.turn("SELECT", 1);                              /* (00-04: 04 not) */
    const oneOut = await favItem();
    t.frame();
    ok(allIn === 2 && oneOut === 1 && t.leds[3] === 1,
       "marked slots all in FB: REMOVE FROM FAVORITES (" + allIn + "); one more not in FB: ADD again (" + oneOut + "; marked " + (t.leds[3] === 1) + ")");
    await t.tap("HOME");                                    /* (the mark dropped) */
    /* in edit mode too: REMOVE on an instrument FB holds, ADD on one it does not */
    U.setInst(0xF001);
    await t.tap("EDIT");
    const edIn = U.screen.editMode() === 1 ? await favItem() : -1;
    await t.tap("HOME");
    U.setInst(0xF005);
    await t.tap("EDIT");
    const edOut = U.screen.editMode() === 1 ? await favItem() : -1;
    await t.tap("HOME");
    ok(edIn === 2 && edOut === 1 && !U.screen.editMode(),
       "in edit mode: REMOVE FROM FAVORITES on F0:01, in FB (" + edIn + "); ADD TO FAVORITES on F0:05 (" + edOut + ")");
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
    t.down("SEL"); await t.sleep(30); await t.turn("PRESETS", -1); t.up("SEL"); await t.sleep(300);
    ok(U.place === 0xFB01 && src(1) === 0xF101 && U.inst === 0xF101 && U.screen.editMode() === 2 && !U.top(),
       "SEL + PRESETS in a favourite's edit mode: nothing, the place, the favourite and the instrument as they were (" +
       M.refText(U.place) + " " + M.refText(src(1)) + " " + M.refText(U.inst) + ", list " + !!U.top() + ")");
    t.down("SEL"); await t.sleep(30); await t.turn("PRESETS", 1); t.up("SEL"); await t.sleep(300);
    ok(U.place === 0xFB01 && src(1) === 0xF101 && U.screen.editMode() === 2, "... turned on: nothing too");
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
    /* NEW CARTRIDGE (the slots' menu) and the cartridge list's NEW: no FAVORITES kind; no CLONE of FB (a built-in's other
     * than it has one) */
    await t.tap("SEL"); await t.sleep(200);
    U.top().level = null;
    U.top().sel = U.top().top.findIndex(x => x.group === "NEW CARTRIDGE");
    await t.tap("SEL");
    ok(t.top() === "menu" && U.top().items.join() === "BACK,DX7,JUNO-106,TB-303,CHIP,SAMPLER,FUSION",
       "NEW CARTRIDGE's kinds: no FAVORITES (" + (U.top() && U.top().items.join()) + ")");
    while (U.top()) await t.tap("HOME");
    {   /* DELETE CARTRIDGE (the slots' menu): none on a built-in one (FB here); on the user's, the cartridge on screen
         * deleted as the cartridge list's DELETE deletes it (unused: no question), the place on the list's first */
        await t.tap("SEL"); await t.sleep(200);
        const onFb = U.top() && U.top().top.includes("DELETE CARTRIDGE");
        U.top().level = null;
        U.top().sel = U.top().top.findIndex(x => x.group === "NEW CARTRIDGE");
        await t.tap("SEL");
        U.top().sel = U.top().items.indexOf("DX7");
        await t.tap("SEL"); await t.sleep(200);
        const made = U.inst >> 8, was = !!M.cart(made);
        await t.tap("SEL"); await t.sleep(200);
        U.top().level = null;                               /* (it opens where it was used last: NEW CARTRIDGE's group) */
        U.top().sel = U.top().top.indexOf("DELETE CARTRIDGE");
        await t.tap("SEL"); await t.sleep(200);
        ok(!onFb && made < 0xF0 && was && !M.cart(made) && !U.top() && U.place >> 8 === M.state.carts[0].id,
           "DELETE CARTRIDGE: none on FB; a new cartridge " + M.hex(made) + " deleted, unused: no question; the place on the list's first, " + M.hex(U.place >> 8));
        U.setInst(0xFB08);
    }
    await t.tap("ENV"); await t.tap("SEL");                 /* the cartridge list, at FB */
    const fbMenu = t.top() === "menu" ? U.top().items.join() : "";
    ok(fbMenu.startsWith("NEW,") && !fbMenu.includes("CLONE") && fbMenu.includes("RENAME"), "the cartridge list's menu on FB: no CLONE (" + fbMenu + ")");
    U.top().sel = U.top().top.findIndex(x => x.group === "NEW");
    await t.tap("SEL");
    ok(t.top() === "menu" && U.top().items.join() === "BACK,DX7,JUNO-106,TB-303,CHIP,SAMPLER,FUSION",
       "the cartridge list's NEW: no FAVORITES (" + (U.top() && U.top().items.join()) + ")");
    await t.tap("HOME");
    await t.turn("SELECT", 1);                              /* (the next cartridge: CLONE there) */
    await t.tap("SEL");
    ok(t.top() === "menu" && U.top().items.includes("CLONE"), "... on another cartridge: CLONE");
    while (U.top()) await t.tap("HOME");
    M.revertThing("cart", 0xFB);
    ok(!M.cart(0xFB).slots.some(Boolean) && !M.builtinChanged("cart", M.cart(0xFB)), "(its REVERT: nothing of the test left)");
    /* FB emptied by REMOVE is the firmware's own again */
    U.setInst(0xF000);
    await t.homeKey(5);
    await pick("ADD TO FAVORITES");
    U.setInst(0xFB00);
    await pick("REMOVE");
    ok(!M.builtinChanged("cart", M.cart(0xFB)), "FB emptied: the firmware's own again");
    while (U.top()) await t.tap("HOME");
    /* FAVORITES FULL: FB with 2 empty slots, 3 marked instruments not in it: none added; 2 fit; REMOVE of them marked */
    const fb = M.cart(0xFB);
    U.edit(() => { for (let s = 0; s < 126; s++) fb.slots[s] = { src: 0xF100 }; });
    const usedNow = () => M.cart(0xFB).slots.filter(Boolean).length, srcNow = i => { const f = M.cart(0xFB).slots[i]; return f ? f.src : -1; };
    U.setInst(0xF000);
    await mark(2);
    await pick("ADD TO FAVORITES");
    ok(usedNow() === 126 && U.toastNow() === "FAVORITES FULL", "FAVORITES FULL: 3 marked, 2 empty slots in FB: none added (" + usedNow() + " used; " + U.toastNow() + ")");
    U.setInst(0xF000);
    await mark(1);
    await pick("ADD TO FAVORITES");
    ok(srcNow(126) === 0xF000 && srcNow(127) === 0xF001 && U.toastNow() === "ADDED TO FB:7E", "... 2 marked: both added, FB full (" + U.toastNow() + ")");
    U.setInst(0xF000);
    await mark(1);
    await pick("REMOVE FROM FAVORITES");
    ok(srcNow(126) < 0 && srcNow(127) < 0 && usedNow() === 126 && U.toastNow() === "REMOVED FROM FAVORITES",
       "... REMOVE FROM FAVORITES on them marked: each one's slot emptied, the others kept (" + usedNow() + " used; " + U.toastNow() + ")");
    M.revertThing("cart", 0xFB);
    /* empty slots among the marked ones are left out: a cartridge of the test's own with sounds at 00 and 03 only; FB
     * with gaps (00 and 02 used): the two land in 01 and 03 */
    const gc = M.freeCart();
    U.edit(() => {
        const slots = new Array(M.SIZES.DX7).fill(null);
        slots[0] = M.init("DX7"); slots[3] = M.init("DX7");
        M.state.carts.push({ id: gc, name: "GAPS", type: "DX7", size: M.SIZES.DX7, slots });
        M.state.carts.sort(M.cartCmp);
        const f = M.cart(0xFB);
        f.slots[0] = { src: 0xF100 }; f.slots[2] = { src: 0xF100 };
    });
    U.setInst(gc << 8 | 1);
    await mark(1);                                          /* (01-02: both empty) */
    const none = await favItem();
    await t.tap("HOME");
    U.setInst(gc << 8);
    await mark(3);                                          /* (00-03: 01 and 02 empty) */
    const some = await favItem();
    await pick("ADD TO FAVORITES");
    ok(!none && some === 1 && srcNow(1) === gc << 8 && srcNow(3) === (gc << 8 | 3) && usedNow() === 4 && U.toastNow() === "ADDED TO FB:01",
       "marked slots all empty: no item (" + none + "); 00-03 with 01, 02 empty: ADD (" + some + "), the two into FB's gaps 01 and 03 (" +
       usedNow() + " used; " + U.toastNow() + ")");
    U.setInst(gc << 8);
    await mark(3);
    const allMarked = await favItem();
    await pick("REMOVE FROM FAVORITES");
    ok(allMarked === 2 && srcNow(1) < 0 && srcNow(3) < 0 && srcNow(0) >= 0 && srcNow(2) >= 0 && usedNow() === 2,
       "... 00-03 again, the empty ones left out: REMOVE FROM FAVORITES (" + allMarked + "), FB's 01 and 03 emptied (" + usedNow() + " used)");
    M.revertThing("cart", 0xFB);
    M.revertThing("cart", gc);
    /* FB alone: an earlier PT/FM's FAVORITES cartridge with a favourite beside an empty FB (SEL + GLO: NO FAVORITES YET);
     * FB deleted (taken out of the cartridges, as a deletion leaves them): ADD TO FAVORITES not listed, SEL + GLO and GLO
     * + PRESETS NO FAVORITES YET */
    const mine = M.freeCart();
    U.edit(() => {
        const slots = new Array(128).fill(null);
        slots[0] = { src: 0xF005 };
        M.state.carts.push({ id: mine, name: "MY FAVORITES", type: "FAVORITES", size: 128, slots });
        M.state.carts.sort(M.cartCmp);
    });
    U.setInst(0xF000);
    await selGlo();
    ok(M.isFav(mine) && U.place === 0xF000 && U.toastNow() === "NO FAVORITES YET",
       "FB empty, an earlier PT/FM's FAVORITES cartridge (" + M.hex(mine) + ") with a favourite: SEL + GLO NO FAVORITES YET, no switch (" + M.refText(U.place) + ")");
    const fbWas = M.cart(0xFB);
    M.state.carts.splice(M.cartIndex(0xFB), 1);
    await selGlo();
    const sg = U.place === 0xF000 && U.toastNow() === "NO FAVORITES YET";
    const fi = await favItem();
    await t.homeKey(1);                                     /* (SONGS: GLO + PRESETS works there) */
    const was = U.inst;
    t.down("GLO"); await t.sleep(30); await t.turn("PRESETS", 1);
    const gp = U.inst === was && U.toastNow() === "NO FAVORITES YET";
    t.up("GLO"); await t.sleep(300);
    ok(!M.cart(0xFB) && sg && !fi && gp, "FB deleted: SEL + GLO NO FAVORITES YET (" + sg + "), ADD TO FAVORITES not listed (" + fi +
       "), GLO + PRESETS NO FAVORITES YET (" + gp + ")");
    while (U.top()) await t.tap("HOME");
    M.state.carts.push(fbWas);
    M.state.carts.sort(M.cartCmp);
    M.revertThing("cart", mine);
    ok(M.isFav(0xFB) && !M.cart(mine) && !M.builtinChanged("cart", M.cart(0xFB)), "(their REVERTs: FB there again, nothing of the test left)");
    await t.homeKey(1);
    U.setInst(0xF000);
};
