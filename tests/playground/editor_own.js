// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* The EDITOR's own pattern (specs/ui/screens/editor.md#the-editors-own-pattern), as fw/tests/ui3_test.c's
 * test_editor_own_*: pattern FE, never listed, the EDITOR's to start from; SEL's NEW PATTERN, LOAD A PATTERN, STORE
 * IN A PATTERN; the warning before anything replaces it; no number shown on it; REVERT ALL bringing it back. And
 * FWS without the user's things (specs/ui/saving.md#fws): no MOVE TO FWS anywhere, STATUS' STORAGE the user area
 * alone; patterns of 128 rows at most. Run by the harness:
 *   node tests/playground/harness.js tests/playground/editor_own.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok } = t;
    const OWN = M.EDITOR_PATTERN, J = JSON.stringify;
    const errs = [];
    const step = async (what, fn) => { try { await fn(); } catch (e) { errs.push(what + ": " + String(e && e.stack || e).split("\n").slice(0, 2).join(" ")); } };
    const own = () => M.pattern(OWN);
    const holds = () => M.patternHolds(OWN);
    const asking = () => (t.top() === "question" ? U.top().lines[0] : "");
    const ownNote = () => U.edit(() => { own().cells[0] = M.newCell(60, null, null); });
    const menuPick = async item => {                           /* the EDITOR's SEL menu (the cell's): that item */
        await t.tap("SEL");
        const m = U.top(), i = m && m.items ? m.items.indexOf(item) : -1;
        ok(t.top() === "menu" && i >= 0, "the cell's menu has " + item + (m && m.items ? " (" + m.items.filter(x => typeof x === "string").join() + ")" : ""));
        if (i < 0) { await t.tap("HOME"); return false; }
        m.sel = i;
        await t.tap("SEL");
        return true;
    };
    const ownNew = async () => {                               /* NEW PATTERN, its warning answered YES */
        await t.homeKey(3);
        await menuPick("NEW PATTERN");
        if (asking()) await t.tap("OCT+");
    };
    const sameAs = (a, b) => { const p = M.pattern(a), q = M.pattern(b);
        return !!p && !!q && p.tracks === q.tracks && p.rows === q.rows && p.step === q.step && J(p.cells) === J(q.cells); };
    const nums = () => M.patternNums();
    const withNotes = () => nums().find(n => n <= M.USER_LAST && M.patternHolds(n));

    /* ---- a fresh start: the EDITOR on its own pattern ---- */
    await step("fresh", async () => {
        await t.homeKey(3);
        t.frame();
        ok(U.screen.name === "EDITOR" && U.pattern === OWN && !U.screen.sub(), "the EDITOR opens on its own pattern, the header EDITOR alone");
        ok(own() && own().tracks === 8 && own().rows === 64 && !holds(), "... empty, 8 x 64");
        ok(!nums().includes(OWN) && M.state.patterns.filter(Boolean).some(p => p.n === OWN), "... never listed (M.patternNums)");
        t.shot("pg_editor_own_fresh");
        await t.tap("REC");
        await t.key(7);
        await t.key(9);
        await t.tap("REC");
        ok(holds() && M.unsavedThing("pattern", OWN), "REC and the keys write into it (an edit)");
    });

    /* ---- NEW PATTERN, LOAD A PATTERN, STORE IN A PATTERN ---- */
    await step("menu", async () => {
        const PN = withNotes();
        ok(PN != null, "a pattern of the user's with notes");
        const pWas = M.unsavedThing("pattern", PN);
        await ownNew();
        ok(U.pattern === OWN && !holds() && own().tracks === 8 && own().rows === 64, "NEW PATTERN: its own, emptied, 8 x 64");
        await menuPick("LOAD A PATTERN");
        ok(t.top() === "list" && U.top().title === "LOAD INTO THE EDITOR", "LOAD A PATTERN: the list LOAD INTO THE EDITOR");
        U.top().sel = nums().indexOf(PN);
        t.shot("pg_editor_own_load_list");
        await t.tap("SEL");
        ok(U.pattern === OWN && sameAs(OWN, PN) && U.toastNow() === "PATTERN " + M.hex(PN) + " LOADED",
           "LOAD: " + M.hex(PN) + " copied into its own, the EDITOR on it (" + U.toastNow() + ")");
        ok(M.unsavedThing("pattern", PN) === pWas, "LOAD: the pattern picked not changed");
        const f = M.freePattern();
        await menuPick("STORE IN A PATTERN");
        ok(t.top() === "list" && U.top().title === "STORE THE PATTERN IN" && U.top().sel === 0, "STORE: the list STORE THE PATTERN IN, + NEW PATTERN first");
        t.shot("pg_editor_own_store_list");
        await t.tap("SEL");
        ok(U.pattern === f && sameAs(f, PN) && !holds() && U.toastNow() === "STORED IN PATTERN " + M.hex(f),
           "STORE, + NEW PATTERN: a new number, the EDITOR on it, its own emptied (" + U.toastNow() + ")");
        U.edit(() => { M.pattern(f).cells[0] = M.newCell(0x30, null, null); });
        const was = J(M.pattern(PN).cells[0]);
        const storeOver = async () => {
            await menuPick("STORE IN A PATTERN");
            U.top().sel = 1 + nums().filter(n => n !== f).indexOf(PN);
            await t.tap("SEL");
        };
        await storeOver();
        ok(asking() === "PATTERN " + M.hex(PN) + " WILL BE REPLACED" && U.top().lines[1] === "DO YOU WANT TO CONTINUE?", "STORE over notes asks first: " + asking());
        t.shot("pg_editor_own_replace");
        await t.tap("OCT-");
        ok(U.pattern === f && J(M.pattern(PN).cells[0]) === was, "STORE: NO keeps it, the EDITOR where it was");
        await storeOver();
        await t.tap("OCT+");
        ok(U.pattern === PN && M.pattern(PN).cells[0] && M.pattern(PN).cells[0].note === 0x30 && M.pattern(f).cells[0].note === 0x30 &&
           U.toastNow() === "STORED IN PATTERN " + M.hex(PN), "STORE: YES replaces it, the EDITOR on it, the one it came from kept");
        M.revertThing("pattern", f);
        if (!pWas) M.revertThing("pattern", PN);
    });

    /* ---- the warning: with notes in its own pattern, every way to another asks first ---- */
    const asked = (way) => ok(asking() === "EDITOR CHANGES WILL BE LOST" && U.top().lines[1] === "DO YOU WANT TO CONTINUE?",
                              "the warning: " + way + " asks EDITOR CHANGES WILL BE LOST (" + asking() + ")");
    await step("warning", async () => {
        await ownNew(); ownNote();
        await t.tap("ENV"); await t.tap("ENV");
        asked("ENV's pick");
        t.shot("pg_editor_own_warning");
        await t.tap("OCT-");
        ok(U.pattern === OWN && holds(), "NO stays on its own, its notes kept");
        await t.tap("ENV"); await t.tap("ENV");
        await t.tap("OCT+");
        ok(U.pattern === nums()[0] && !holds(), "YES opens the one picked, its own emptied");
        await t.tap("ENV"); await t.tap("ENV");
        ok(!asking() && U.pattern === nums()[0], "an empty one never asks");
        /* SONGS' EDIT PATTERN */
        await ownNew(); ownNote();
        await t.homeKey(1);
        await t.tap("EDIT");
        await t.tap("SEL");
        const m = U.top(); m.sel = m.items.indexOf("EDIT PATTERN"); await t.tap("SEL");
        asked("SONGS' EDIT PATTERN");
        await t.tap("OCT+");
        ok(U.screen.name === "EDITOR" && U.pattern !== OWN && !holds(), "SONGS' EDIT PATTERN, YES: the EDITOR on the position's pattern");
        await t.tap("HOME");
        if (U.screen.name === "SONGS") await t.tap("HOME");
        /* SONGS' ENV list */
        await ownNew(); ownNote();
        await t.homeKey(1);
        await t.tap("ENV"); await t.tap("ENV");
        asked("SONGS' ENV pick");
        await t.tap("OCT+");
        ok(U.pattern !== OWN && !holds() && /^PATTERN [0-9A-F]{2} FOR THE EDITOR$/.test(U.toastNow() || ""), "SONGS' ENV pick, YES: " + U.toastNow());
        /* PLAYBACK's EDIT */
        await ownNew(); ownNote();
        await t.homeKey(8);
        await t.tap("EDIT");
        asked("PLAYBACK's EDIT");
        await t.tap("OCT-");
        ok(U.pattern === OWN && holds(), "PLAYBACK's EDIT, NO: nothing changed");
        /* a FUSION slot's PATTERN (the demo disk's FUSION) */
        const FB = M.state.carts.find(c => c.type === "FUSION");
        await t.homeKey(5);
        U.setInst(FB.id << 8);
        await t.tap("EDIT");
        await t.tap("SEL");                                     /* (on the edit page's PATTERN line: straight there) */
        asked("a FUSION slot's PATTERN");
        await t.tap("OCT+");
        ok(U.screen.name === "EDITOR" && U.pattern === FB.slots[0].p.pattern && U.pattern !== OWN && !holds(),
           "a FUSION slot's PATTERN, YES: the EDITOR on its pattern");
        await t.tap("HOME"); await t.tap("HOME");
    });

    /* ---- no number on its own pattern ---- */
    await step("texts", async () => {
        await ownNew(); ownNote();
        t.frame();
        ok(!U.screen.sub(), "the header EDITOR, no number");
        await t.tap("GLO");
        ok(t.top() === "props" && U.top().title === "PATTERN PROPERTIES" && !U.top().items.some(i => i.name === "USED BY"),
           "GLO: PATTERN PROPERTIES, no USED BY (" + (U.top() && U.top().title) + ")");
        t.shot("pg_editor_own_props");
        await t.tap("GLO");
        await t.tap("EDIT");
        t.shot("pg_editor_own_where");
        await t.tap("EDIT");
        await menuPick("REVERT");
        ok(asking() === "REVERT THE PATTERN TO SAVED?", "REVERT THE PATTERN TO SAVED? (" + asking() + ")");
        await t.tap("OCT-");
        await t.tap("PLAY");
        await t.sleep(400);
        await t.homeKey(8);
        t.frame();
        const pg = U.screen.page(), ft = U.screen.footer();
        ok(pg === "EDITOR" && ft && /^ROW [0-9A-F]{2}$/.test(ft[0]), "PLAYBACK while its own plays: the header EDITOR, the footer " + (ft && ft[0]));
        t.shot("pg_editor_own_playback");
        P.pauseAll();
        await t.homeKey(3);
    });

    /* ---- never listed, never taken ---- */
    await step("hidden", async () => {
        ok(M.state.keys && own(), "the keys' song FE and the EDITOR's pattern FE side by side");
        ok(M.freePattern() <= M.USER_LAST, "NEW, CLONE, MOVE never take FE");
        const FB = M.state.carts.find(c => c.type === "FUSION"), k = FB.slots.findIndex(Boolean);
        U.setInst(FB.id << 8 | k);
        await t.homeKey(5);
        await t.tap("EDIT");
        let seen = false;
        for (let i = 0; i < 60; i++) { await t.turn("K1", 1); if (M.cart(FB.id).slots[k].p.pattern === OWN) seen = true; }
        await t.tap("HOME");
        ok(!seen, "FUSION's PATTERN knob never shows FE");
        M.revertThing("cart", FB.id);
        await t.homeKey(1);
        await t.tap("EDIT");
        seen = false;
        for (let i = 0; i < 60; i++) { await t.turn("K1", 1); if (M.state.songs[U.song].order.includes(OWN)) seen = true; }
        await t.tap("HOME");
        ok(!seen, "a song position's K1 PATTERN never shows FE");
        M.revertThing("song", U.song);
    });

    /* ---- REVERT ALL brings a saved one back while the EDITOR is elsewhere: the next switch asks ---- */
    await step("revert all", async () => {
        await ownNew(); ownNote();
        M.save();
        await t.tap("ENV"); await t.tap("ENV"); await t.tap("OCT+");
        const on = U.pattern;
        ok(on !== OWN && !holds(), "the EDITOR on another, its own emptied (an edit)");
        M.revert();
        ok(U.pattern === on && holds(), "REVERT ALL: its own has its notes again, the EDITOR stays");
        await t.tap("ENV"); await t.tap("ENV");
        asked("the next pick after REVERT ALL");
        await t.tap("OCT+");
        M.save();
    });

    /* ---- FWS holds nothing of the user's; 128 rows ---- */
    await step("fws and rows", async () => {
        const menus = [];
        const look = async () => { await t.tap("SEL"); if (U.top() && U.top().items) menus.push(U.top().title + ": " + U.top().items.filter(x => typeof x === "string").join()); await t.tap("HOME"); };
        await t.homeKey(1); await look();
        await t.homeKey(3); await t.tap("ENV"); await look(); await t.tap("HOME");
        await t.homeKey(5); await t.tap("ENV"); await look(); await t.tap("HOME");
        ok(!menus.some(m => /FWS|USER AREA/.test(m)), "no MOVE TO FWS, FILL FWS, MOVE TO USER AREA, IN FWS: " + menus.join(" | "));
        const st = M.storage();
        ok(st.romFree === undefined && st.inRom === undefined, "the storage figures: the user area alone");
        await t.homeKey(17);                                    /* STATUS, its STORAGE page */
        await t.turn("SELECT", -3); await t.turn("SELECT", 2);
        t.shot("pg_status_storage");
        await t.homeKey(3);
        await ownNew();
        await t.tap("GLO");
        U.top().sel = U.top().items.findIndex(i => i.name === "ROWS");
        t.down("SEL"); await t.turn("K1", 3); const r112 = own().rows;
        await t.turn("K1", 2); t.up("SEL"); await t.sleep(50);
        await t.turn("K1", 1);
        await t.tap("GLO");
        ok(r112 === 112 && own().rows === 128, "GLO's ROWS stops at 128 (SEL held: " + r112 + ", then " + own().rows + ")");
        M.revertThing("pattern", OWN);
    });

    /* ---- STATUS' USER FLASH MAP and RAM MAP (specs/ui/screens/status.md): a 4 x 4 square a KB, 48 a line from (0, 33) ---- */
    await step("status maps", async () => {
        const squares = () => { let n = 0; while (n < 1024 && t.px((n % 48) * 5 + 1, 33 + Math.floor(n / 48) * 5 + 1)) n++; return n; };
        await t.homeKey(17);
        await t.turn("SELECT", -6); await t.turn("SELECT", 3);
        t.shot("pg_status_flash_map");
        ok(U.screen.sub() === "USER FLASH MAP" && squares() === 412, "STATUS -> USER FLASH MAP: 412 squares (" + squares() + ")");
        await t.turn("SELECT", 1);
        t.shot("pg_status_ram_map");
        ok(U.screen.sub() === "RAM MAP" && squares() === 512, "STATUS -> RAM MAP: 512 squares (" + squares() + ")");
        await t.turn("SELECT", 1);
        ok(U.screen.sub() === "DEVICE", "then DEVICE, the last");
    });

    for (const e of errs) ok(false, "no error in " + e);
};
