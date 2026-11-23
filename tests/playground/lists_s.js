// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* Menus, properties, the hold layers, SETTINGS' MIDI page and the song and cartridge lists in FONT_S
 * (specs/ui/dialogs.md#rows-in-s): a menu of more than 11 items scrolling with the first shown as its table says, an item
 * at its 26-character limit beside a group's mark, BACK's rule in its own row, a property of two lines selected at the
 * list's bottom shown whole, one of 27 characters on one line, the panel's height kept at the list's end, FX's KEYS,
 * EVERYWHERE on two lines, the layers in S or wholly in T at their 28 characters, the cartridge list's FONT_T figures on
 * its names' baseline, the song list and its TIME past 99:59, the MIDI page's mappings on two lines and its empty line.
 * The firmware's own checks, step for step: fw/tests/ui3_test.c (test_lists_s) and fw/tests/midi_test.c (the MIDI page).
 * Run by the harness:
 *   node tests/playground/harness.js tests/playground/lists_s.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t, D = t.PTFM.draw, Y0 = U.BODY_Y, hold = U.set("HOLD DELAY") + 100;
    /* a text as drawn: the frame's rows y..y+12 (F.S's; y..y+10 F.T's) over the text's width from x, its ink against the
     * text drawn alone on black, pixel for pixel, some of it inked (fw/tests/text_probe.h's text_as; the rows below left
     * out: a highlight's foot) */
    function textAs(x, y, s, col, f = D.F.S) {
        t.frame();
        const fb = D.fb, w = s.length * f.w, h = f.h === 16 ? 13 : 11, frame = fb.slice();
        D.px(0, 0, col);
        const ink = fb[0];
        D.rect(x, y, w, h, D.BLACK);
        D.text(x, y, f, s, col);
        const alone = fb.slice();
        fb.set(frame);
        let same = true, inked = 0;
        for (let j = 0; j < h; j++) for (let i = x; i < x + w && i < 240; i++) {
            const a = alone[(y + j) * 240 + i] === ink, b = frame[(y + j) * 240 + i] === ink;
            inked += a;
            if (a !== b) same = false;
        }
        return same && inked > 8;
    }
    const textT = (x, y, s, col) => textAs(x, y, s, col, D.F.T);
    const right = (x, s, f = D.F.S) => x - D.tw(f, s);
    /* pixels of a colour in x0..x1-1, y0..y1-1 (the firmware's count_px) */
    function countPx(x0, y0, x1, y1, col) {
        t.frame();
        const fb = D.fb, keep = fb[0];
        D.px(0, 0, col);
        const ink = fb[0];
        fb[0] = keep;
        let n = 0;
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) n += fb[y * 240 + x] === ink;
        return n;
    }
    /* the rows of y0..y1-1 holding ink (not black) in x0..x1-1: [first, last] */
    function inkRows(x0, x1, y0, y1) {
        t.frame();
        const fb = D.fb, keep = fb[0];
        D.px(0, 0, D.BLACK);
        const black = fb[0];
        fb[0] = keep;
        let a = -1, b = -1;
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (fb[y * 240 + x] !== black) { if (a < 0) a = y; b = y; }
        return [a, b];
    }

    /* ---- a menu of 14: 11 show, the first max(0, min(sel - 5, items - 11)); item i at 10, 39 + 16 i (SETTINGS' menu's key,
     * as the firmware's test takes it) ---- */
    U.go("STATUS"); await t.sleep(40);
    const ITEMS = ["NEW", "CLONE", "MOVE", "DELETE", "RENAME", "REVERT", "COPY", "CUT", "PASTE", "PASTE MIX", "CLEAR", "SWAP",
                   "INIT", "MARK"];
    U.menu("settings", "A MENU OF 14", ITEMS, () => {});
    await t.turn("SELECT", -20);
    await t.turn("SELECT", 13);
    ok(textAs(10, Y0 + 22, ITEMS[3], D.LGREY) && textAs(10, Y0 + 22 + 16 * 10, ITEMS[13], D.WHITE) && textAs(8, Y0 + 5, "A MENU OF 14", D.CYAN),
       "a menu of 14, its last highlighted: the first shown is the fourth (DELETE), the last at the bottom, in S at 10");
    await t.turn("SELECT", -7);
    ok(textAs(10, Y0 + 22, ITEMS[1], D.LGREY) && textAs(10, Y0 + 22 + 16 * 5, ITEMS[6], D.WHITE),
       "the seventh highlighted: the first shown the second (CLONE), the highlight 5 down");
    t.shot("lists_s_menu_14");
    await t.tap("HOME");
    /* an item of 26 characters, a group's mark after it */
    const LONG = "AUTOMATIC REPEAT PROCEDURE";
    U.menu("settings", "A LONG ITEM", [LONG, { group: "ROWS", items: ["INSERT ROW", "DELETE ROW"] }], () => {});
    await t.turn("SELECT", -20);
    ok(LONG.length === 26 && textAs(10, Y0 + 22, LONG, D.WHITE) && textAs(222, Y0 + 22 + 16, ">", D.LGREY),
       "an item of 26 characters drawn whole in S (to x 218), a group's > at 222");
    t.shot("lists_s_menu_26");
    /* the group opened, the first item after BACK highlighted: BACK's rule at 6, 53, in BACK's own row */
    await t.turn("SELECT", 1);
    await t.tap("SEL");
    ok(t.top() === "menu" && textAs(26, Y0 + 22, "BACK", D.CYAN) && textAs(10, Y0 + 22 + 16, "INSERT ROW", D.WHITE) &&
       countPx(6, 53, 234, 54, D.DGREY) === 228 && countPx(3, 53, 6, 54, D.DGREY) + countPx(234, 53, 237, 54, D.DGREY) === 0,
       "a group opened, its first item highlighted: BACK's rule at y 53 whole (228 px from x 6), the item's bar under it");
    t.shot("lists_s_group_back");
    await t.tap("HOME");

    /* ---- properties: 12 rows, the last of two lines, selected at the bottom: both lines; LENGTH's row 27, one line ---- */
    const NAMES = ["TEMPO", "SWING", "LENGTH", "GRID", "QUANTIZE", "MONO LEGATO", "COUNT-IN", "VIEW", "K1 TURNS", "K2 TURNS",
                   "K3 TURNS", "NEW LOOP REVERB"];
    const VALUE = "A LONGER VALUE", V27 = "NINETEEN CHARACTERS";   /* (15 + 2 + 14: two lines; 6 + 2 + 19 = 27: one) */
    U.props("SOME PROPERTIES", NAMES.map((name, i) => ({ name, get: () => i === 11 ? VALUE : i === 2 ? V27 : "ONE", change() {} })));
    await t.turn("SELECT", 11);
    /* the rows from the third (9 of one line, then the last's two: 11 in view) */
    ok(textAs(10, Y0 + 23, NAMES[2], D.CYAN) && textAs(10, Y0 + 23 + 16 * 9, NAMES[11], D.WHITE) &&
       textAs(right(230, VALUE), Y0 + 23 + 16 * 10, VALUE, D.WHITE),
       "a property of two lines selected at the list's bottom: its name, then its value right-aligned under it, both in view");
    ok(textAs(right(230, V27), Y0 + 23, V27, D.LGREY) && NAMES[2].length + 2 + V27.length === 27,
       "a property of 27 characters (LENGTH  NINETEEN CHARACTERS): one line, its value on its name's line");
    t.shot("lists_s_props_bottom");
    await t.turn("SELECT", -11);
    ok(textAs(10, Y0 + 23, NAMES[0], D.WHITE) && textAs(10, Y0 + 23 + 16 * 10, NAMES[10], D.CYAN) &&
       countPx(4, Y0 + 2 + 11 * 16 + 23, 236, Y0 + 2 + 11 * 16 + 24, D.LGREY) > 200,
       "the first selected: the first row on top, 11 rows of one line in view (the last row below them), the panel as tall");
    await t.tap("HOME");
    /* the panel's height the rows' lines (at most those in view) however it scrolls: a row of 2 then 10 of one line, the
     * last selected: the first row out of view, the panel still 11 lines tall */
    const N2 = [NAMES[11]].concat(NAMES.slice(2, 12));
    U.props("SOME PROPERTIES", N2.map((name, i) => ({ name, get: () => i ? "ONE" : VALUE, change() {} })));
    await t.turn("SELECT", 10);
    ok(textAs(10, Y0 + 23 + 16 * 9, NAMES[11], D.WHITE) && countPx(4, 218, 236, 219, D.LGREY) > 200 && countPx(4, 202, 236, 203, D.LGREY) < 100,
       "a two-line row scrolled out of view at the list's end: the panel keeps its 11 lines (its foot at 218, not 202)");
    t.shot("lists_s_props_end");
    await t.tap("HOME");
    /* FX's KEYS, EVERYWHERE on MIDNIGHT RUN's setup, the song played last (ROUTING, then it: the last row; the firmware's
     * test finds it so after its earlier tests), KEYS THROUGH FX (two lines), under a title of two lines (rows from 56) */
    const P = t.P, keysWas = M.state.settings.KEYS;
    P.playSong(M.songNums()[M.songList().findIndex(s => s.name === "MIDNIGHT RUN")]);
    await t.sleep(300);
    P.pause();
    M.state.settings.KEYS = "THROUGH FX";
    U.go("FX"); await t.sleep(40);
    await t.tap("GLO");
    const keysRow = textAs(10, Y0 + 39, "TEMPO", D.WHITE) ? 3 : 1;     /* (THE KEYS' would have TEMPO, SWING first) */
    await t.turn("SELECT", keysRow);
    ok(keysRow === 1 && t.top() === "props" && textAs(8, Y0 + 5, "FX PROPERTIES FOR", D.CYAN) && textAs(8, Y0 + 21, "MIDNIGHT RUN", D.WHITE) &&
       textAs(10, Y0 + 39 + 16 * keysRow, "KEYS, EVERYWHERE", D.WHITE) && textAs(right(230, "THROUGH FX"), Y0 + 39 + 16 * (keysRow + 1), "THROUGH FX", D.WHITE),
       "FX's properties for MIDNIGHT RUN: KEYS, EVERYWHERE selected, the last row (" + keysRow + " of 1): its name, then THROUGH FX right-aligned under it");
    t.shot("lists_s_fx_props");
    await t.tap("GLO");
    M.state.settings.KEYS = keysWas;

    /* ---- the hold layers: in S when every line fits (SAVE's; a name being typed: its one line of 28), else wholly in T
     * (SEL's on the EDITOR's grid, 31, and on INSTRUMENTS browsing, 29); the keys whole ---- */
    U.go("STATUS"); await t.sleep(40);
    t.down("SAVE"); await t.sleep(hold);
    ok(textAs(8, 167 + 5, "HOLD ", D.CYAN) && textAs(8, 167 + 21, "OCT+", D.YELLOW) && textAs(8, 167 + 37, "OCT-", D.YELLOW) &&
       textAs(40, 167 + 37, ": REVERT ALL", D.LGREY),
       "SAVE held: its layer in S (2 lines: the panel at 167, 58 high), OCT+: SAVE, OCT-: REVERT ALL");
    t.shot("lists_s_save_layer");
    t.up("SAVE"); await t.sleep(200);
    U.textEntry("A NAME", "ABC", 10, U.B.EDIT, () => {});
    t.down("SEL"); await t.sleep(hold);
    ok(t.top() === "text" && textAs(8, 183 + 21, "SELECT IN A NAME", D.YELLOW) && textAs(8 + 16 * 8, 183 + 21, ": NEXT GROUP", D.LGREY),
       "SEL held in a name being typed: its one line of 28 characters, SELECT IN A NAME: NEXT GROUP, in S (the panel at 183)");
    t.shot("lists_s_name_sel_layer");
    t.up("SEL"); await t.sleep(200);
    await t.tap("HOME");                                /* (the name: nothing made) */
    const at = n => 225 - (12 * n + 26) + 19;           /* (in T: the first line of a layer of n) */
    U.go("EDITOR"); await t.sleep(40);
    t.down("SEL"); await t.sleep(hold);
    ok([3, 4].some(n => textT(8, at(n), "SELECT, ALGORITHM", D.YELLOW) && textT(8 + 17 * 6, at(n), ": MARK A BLOCK", D.LGREY)),
       "SEL held on the EDITOR's grid: its layer in T (a line past 28 characters), SELECT, ALGORITHM: MARK A BLOCK whole");
    t.shot("lists_s_editor_sel_layer");
    t.up("SEL"); await t.sleep(200);
    U.go("INSTRUMENTS"); await t.sleep(40);
    t.down("SEL"); await t.sleep(hold);
    ok([3, 4].some(n => textT(8, at(n), "SELECT, ALGORITHM", D.YELLOW) && textT(8 + 17 * 6, at(n), ": MARK SLOTS", D.LGREY)),
       "SEL held on INSTRUMENTS browsing: a line of 29 characters, SELECT, ALGORITHM: MARK SLOTS, sends the layer to T");
    t.shot("lists_s_instr_sel_layer");
    t.up("SEL"); await t.sleep(200);

    /* ---- the cartridge list: the names in S, the figures in T on their baseline; measured on F0 KEYS/ORGANS's line ---- */
    await t.tap("ENV");
    await t.turn("SELECT", -40);                        /* (the first line highlighted) */
    const f0 = M.state.carts.findIndex(c => c.id === 0xF0), ry = Y0 + 34 + 16 * f0;
    const nm = inkRows(32, 128, ry, ry + 16), ty = inkRows(134, 190, ry, ry + 16), sl = inkRows(192, 234, ry, ry + 16);
    ok(t.top() === "list" && f0 > 0 && f0 < 10 && textAs(32, ry, "KEYS/ORGANS", D.LGREY) && nm[1] === ty[1] && ty[1] === sl[1] &&
       nm[1] - nm[0] === 9 && ty[1] - ty[0] === 7 && nm[0] === ry + 2,
       "the cartridge list: F0's line (" + f0 + "), its name in S (rows 2-11), its type and slots in T ending on the same row, its " +
       "baseline (" + nm + " / " + ty + " / " + sl + ")");
    t.shot("lists_s_cartridges");
    await t.tap("HOME");

    /* ---- the song list (ENV on STATUS): 18 songs, 10 in view; the last highlighted, the first shown the ninth ---- */
    const songs = M.songList(), ns = songs.length, last = songs[ns - 1], bpm = String(M.bpm(last));
    U.go("STATUS"); await t.sleep(40);
    await t.tap("ENV");
    await t.turn("SELECT", 40);
    ok(t.top() === "list" && ns === 18 && textAs(32, Y0 + 34 + 16 * 9, last.name, D.WHITE) && textT(196 - bpm.length * 6, Y0 + 34 + 16 * 9 + 2, bpm, D.WHITE) &&
       textAs(32, Y0 + 34, songs[ns - 10].name, D.LGREY) && countPx(4, Y0 + 33 + 16 * 9, 236, Y0 + 34 + 16 * 9, D.WHITE) > 200,
       "the song list, its last of " + ns + " highlighted: " + last.name + " at the bottom (its tempo " + bpm + " in T), " +
       songs[ns - 10].name + " first, the bar on line 9");
    t.shot("lists_s_song_list");
    await t.tap("HOME");
    /* a song past 99:59: its TIME in hours and minutes (the first song made long: TEMPO 3, 12 ticks a row) */
    const s0 = songs[0], was = { tempo: s0.tempo, ticks: s0.ticks };
    U.edit(() => { s0.tempo = 3; s0.ticks = 12; });
    const secs = Math.round(U.songSeconds(s0)), tm = U.fmtListTime(U.songSeconds(s0));
    await t.tap("ENV");
    await t.turn("SELECT", -40);
    ok(t.top() === "list" && secs >= 6000 && tm.includes("H") && tm.length <= 5 && textT(202, Y0 + 36, tm, D.WHITE),
       "a song of " + secs + " s in the list: its TIME " + tm + ", hours and minutes, whole at 202");
    t.shot("lists_s_song_long");
    await t.tap("HOME");
    U.edit(() => { s0.tempo = was.tempo; s0.ticks = was.ticks; });
    const f = U.fmtListTime;
    ok(f(5999) === "99:59" && f(6000) === "1H40" && f(6060) === "1H41" && f(359999) === "99H59" && f(360000) === "99H+",
       "the list's TIME: 5999 s " + f(5999) + ", 6000 " + f(6000) + ", 6060 " + f(6060) + ", 359999 " + f(359999) + ", 360000 " + f(360000));

    /* ---- SETTINGS' MIDI page: empty, NO MAPPINGS: EDIT ON LEARN whole after LEARN; then a mapping two lines, the FM-1's
     * control, then the MIDI control and its kind (fw/tests/midi_test.c the same) ---- */
    const keep = M.state.midi.slice();
    M.state.midi.length = 0;
    U.go("SETTINGS"); await t.sleep(40);
    if (U.screen.sub() !== "MIDI") await t.tap("SEQ");
    await t.turn("SELECT", -20);
    const items = M.MIDI_SETTINGS.length;               /* (its items, MIDI CONTROLLER and LEARN: 8) */
    await t.turn("SELECT", items - 1);                  /* (LEARN) */
    ok(textAs(4, Y0 + 2 + 16 * (items + 1), "NO MAPPINGS: EDIT ON LEARN", D.DGREY),   /* (+ 1: CONTROLLER AFTERTOUCH's
                                                        * value on a line of its own) */
       "the MIDI page with no mappings: NO MAPPINGS: EDIT ON LEARN after LEARN, in S, whole (26 characters, to x 212)");
    t.shot("lists_s_midi_empty");
    const MAPS = [{ ch: 0, msg: "CC", num: 20, ctl: 3, kind: 1 }, { ch: 0, msg: "CC", num: 21, ctl: 4, kind: 1 },
                  { ch: 0, msg: "CC", num: 22, ctl: 5, kind: 1 }, { ch: 9, msg: "NOTE", num: 36, ctl: 20, kind: 4 },
                  { ch: 0, msg: "CC", num: 50, ctl: 22, kind: 5 }, { ch: 0, msg: "CC", num: 7, ctl: 7, kind: 0 },
                  { ch: 0, msg: "CC", num: 74, ctl: 0, kind: 0 }];
    M.state.midi.push(...MAPS);
    await t.turn("SELECT", 1);                          /* the first mapping */
    const two = (k, y) => {
        const mp = MAPS[k], src = (mp.msg === "NOTE" ? "NOTE " + mp.num : "CC " + mp.num) + " CH " + (mp.ch + 1) + "  " + M.MIDI_KINDS[mp.kind];
        return textAs(right(236, src), y + 16, src, D.WHITE);
    };
    /* (selected: the 8th row, kept 5 lines down: rows 2 ... from the top, the mapping at line 5) */
    ok(textAs(4, Y0 + 2 + 16 * 5, "K2", D.WHITE) && two(0, Y0 + 2 + 16 * 5),
       "the MIDI page: the first mapping selected, K2 on its first line, CC 20 CH 1  RELATIVE right-aligned on its second");
    t.shot("lists_s_midi_first");
    await t.turn("SELECT", 20);                         /* the last */
    ok(textAs(4, Y0 + 2 + 16 * 10, "SELECT", D.WHITE) && two(6, Y0 + 2 + 16 * 10),
       "the last mapping selected at the list's bottom: both its lines in view (SELECT, CC 74 CH 1  ABSOLUTE)");
    t.shot("lists_s_midi_last");
    M.state.midi.length = 0;
    M.state.midi.push(...keep);
    await t.turn("SELECT", -40);
    if (U.screen.sub() === "MIDI") await t.tap("SEQ");
};
