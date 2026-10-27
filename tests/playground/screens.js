/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Every screen and the main overlays as PNGs (2x), to look at:
 *   node tests/playground/harness.js tests/playground/screens.js   -> build/playground/*.png */
"use strict";
module.exports = async t => {
    const { U } = t;
    t.shot("01_status");
    await t.combo("HOME", "LFO"); t.shot("02_help_status"); await t.tap("HOME");
    await t.homeKey(1); t.shot("03_songs");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("03b_songs_next"); await t.tap("SEQ"); await t.tap("SEQ");   /* REPEAT SONG (past its toast), round to ONE PLAY */
    await t.tap("EDIT"); await t.turn("ALGORITHM", 3); t.shot("04_songs_order"); await t.tap("HOME");
    /* the cell of the pool with the most commands, to show them */
    let best = { n: 0, k: -1, p: 0 };
    for (const p of t.M.state.patterns) if (p) p.cells.forEach((c, k) => { if (c && c.cmds.length > best.n && (k & 15) < p.tracks) best = { n: c.cmds.length, k, p: p.n }; });
    await t.homeKey(3);
    U.pattern = best.p; U.editor.row = best.k >> 4; U.editor.track = best.k & 15; U.editor.left = 0;
    t.shot("05_editor8");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("06_editor4");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("07_editor2");
    await t.turn("K1", 1); await t.sleep(1300); t.shot("07b_editor2_cmd2");
    await t.turn("K1", -2); await t.sleep(1300); t.shot("07c_editor2_vol");
    await t.turn("K1", 1);
    await t.tap("SEQ"); await t.sleep(2100); t.shot("07d_editor1");
    await t.tap("SEQ");
    await t.tap("EDIT"); await t.sleep(1300); t.shot("08_note_editor"); await t.tap("EDIT");
    await t.combo("HOME", "LFO"); t.shot("09_help_editor"); await t.tap("HOME");
    await t.tap("ENV"); t.shot("10_pattern_list"); await t.tap("HOME");
    t.down("SEL"); await t.sleep(20); await t.turn("SELECT", 3); await t.turn("ALGORITHM", 2); t.up("SEL"); await t.sleep(150);
    t.shot("11_block"); await t.tap("SEL"); t.shot("12_block_menu"); await t.tap("HOME"); await t.tap("HOME");
    /* each kind's first cartridge in the user area, wherever the demo disk puts it: a built-in one (in FWS)
     * cloned into the user area first (the shots as the firmware's test takes them) */
    for (const ty of ["DX7", "JUNO", "TB303", "FUSION"]) {
        const c = t.M.state.carts.find(x => x.type === ty);
        if (!c.rom || t.M.state.carts.some(x => x.type === ty && !x.rom)) continue;
        const n = structuredClone(c);
        n.id = t.M.freeCart(); delete n.rom; delete n.builtin;
        t.M.state.carts.push(n); t.M.state.carts.sort((a, b) => a.id - b.id);
    }
    const first = type => t.M.state.carts.find(c => c.type === type && !c.rom).id << 8;
    await t.homeKey(5); U.setInst(first("DX7")); await t.sleep(50); t.shot("13_instr_dx7");
    await t.tap("EDIT"); t.shot("14_instr_dx7_edit");
    await t.turn("ALGORITHM", 8); await t.tap("SEL"); t.shot("14b_name_entry"); await t.tap("HOME"); await t.tap("EDIT");
    U.setInst(first("JUNO")); await t.sleep(50); t.shot("15_instr_juno");
    await t.tap("EDIT"); t.shot("16_instr_juno_edit"); await t.tap("EDIT");
    U.setInst(first("TB303")); await t.sleep(50); t.shot("17_instr_tb303");
    await t.tap("EDIT"); t.shot("17b_instr_tb303_edit"); await t.tap("EDIT");
    U.setInst(0xF900); await t.sleep(50); t.shot("17c_instr_chip");   /* (CHIPTUNE, built in: looked at, not changed) */
    await t.tap("EDIT"); t.shot("17d_instr_chip_edit"); await t.turn("ALGORITHM", 2); t.shot("17e_instr_chip_tables");
    await t.turn("ALGORITHM", 1); t.shot("17f_instr_chip_wave"); await t.tap("EDIT");
    U.setInst(first("SAMPLER")); await t.sleep(50); t.shot("18_instr_sampler");
    await t.tap("EDIT"); t.shot("19_instr_sampler_edit"); await t.tap("SEQ"); t.shot("19b_instr_sampler_env"); await t.tap("EDIT");
    U.setInst(first("FUSION")); await t.sleep(50); t.shot("20_instr_fusion");
    await t.tap("EDIT"); t.shot("20b_instr_fusion_edit");
    await t.turn("SELECT", 2); await t.tap("SEL"); t.shot("20bb_fusion_menu"); await t.tap("HOME");
    await t.turn("SELECT", -2); await t.tap("SEL"); await t.sleep(50); t.shot("20c_fusion_editor");
    await t.tap("HOME"); await t.tap("HOME");
    U.setInst(0x0100); U.setInst(first("FUSION") | t.M.cart(first("FUSION") >> 8).slots.findIndex(s => !s));
    await t.tap("EDIT"); t.shot("20d_fusion_fill"); await t.tap("HOME");
    await t.tap("ENV"); t.shot("21_cartridges"); await t.tap("HOME");
    await t.homeKey(8); t.shot("22_playback");
    await t.tap("FX"); t.shot("23_fx_live"); await t.tap("SEQ"); t.shot("23b_fx_edit");
    for (let i = 0; i < 12; i++) { await t.turn("SELECT", 1); t.shot("23c_fx_pic_" + String(i).padStart(2, "0")); }   /* (each item's
                                                                     * picture: fw/tests/ui3_test.c the same) */
    await t.turn("SELECT", -12);
    await t.tap("SEQ"); t.shot("24_mixer");
    /* the KEYS column (the third page): K1 on its LVL, +3 dB; DRY darkens DCK and the sends (fw/tests/ui3_test.c the
     * same); then back */
    await t.turn("ALGORITHM", 20); await t.turn("SELECT", -6); await t.turn("K1", 3); t.shot("24b_mixer_keys");
    U.changeSetting("KEYS", "DRY"); t.shot("24c_mixer_keys_dry"); U.changeSetting("KEYS", "THROUGH FX");
    await t.turn("K1", -3); await t.turn("ALGORITHM", -20); await t.sleep(1300);
    await t.tap("SEQ");
    await t.homeKey(15); t.shot("25_visual");
    await t.homeKey(20); t.shot("26_settings");
    /* the MIDI page, with three mappings as LEARN would make them; LEARN's box at its second step */
    t.M.state.midi.push({ ch: 0, msg: "CC", num: 74, ctl: 2, kind: 1 }, { ch: 9, msg: "NOTE", num: 36, ctl: 20, kind: 5 },
                        { ch: 1, msg: "NOTE", num: 60, ctl: 22, kind: 4 });
    await t.tap("SEQ"); t.shot("26b_settings_midi");
    await t.turn("SELECT", 7); await t.tap("EDIT"); await t.turn("K1", 1); t.shot("26c_midi_learn");   /* (LEARN) */
    await t.tap("HOME"); t.M.state.midi.length = 0; await t.turn("SELECT", -9); await t.tap("SEQ");
    U.setInst(first("JUNO"));                           /* a sound for the loops (not FUSION's empty slot) */
    await t.homeKey(22); t.shot("27_loops_empty");
    /* LOOPS (specs/ui/screens/loops.md): the first loop recording (REC, PLAY, a chord and notes); closed by REC in its
     * first bar; a second loop, a MONO line, recorded over it as the loops play; paused: the pattern recorder (its
     * preview stopped), armed on + NEW LOOP, CONTROL mode with a solo, GLO, SEL, HOME's combination; 16 loops */
    const P = t.P, LS = () => t.M.state.loops, sc = () => U.screen.state();
    await t.tap("REC"); t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    t.keyDown(0); t.keyDown(4); t.keyDown(7); await t.sleep(500); t.keyUp(0); t.keyUp(4); t.keyUp(7); await t.sleep(60);
    for (const k of [11, 9]) { t.keyDown(k); await t.sleep(220); t.keyUp(k); await t.sleep(30); }
    t.shot("28_loops_first"); await t.tap("REC");
    await t.tap("SEQ"); await t.key(20); await t.tap("SEQ");      /* (CONTROL's MONO: the next loop a line) */
    await t.tap("REC");
    for (let n = 0; n < 200 && !P.lpRecording(); n++) await t.sleep(20);
    for (const k of [14, 16, 18, 21]) { t.keyDown(k); await t.sleep(280); t.keyUp(k); await t.sleep(60); }
    for (let n = 0; n < 200 && sc().take; n++) await t.sleep(20);
    await t.sleep(500); t.shot("28b_loops_two");
    P.pause(); await t.sleep(100);
    await t.tap("HOME"); t.shot("31c_home_loops");
    await t.key(U.WHITE[0]); t.shot("31d_home_loops_muted");    /* a white key acted: only its line left */
    await t.key(U.WHITE[0]); await t.tap("HOME");
    /* the pattern recorder: ARP (its preview stopped by ARP again), steps on two rows */
    await t.tap("ARP"); await t.tap("ARP");
    for (const k of [0, 4, 8, 12]) await t.key(U.WHITE[k]);
    await t.turn("SELECT", 1); await t.turn("K3", 7);
    for (const k of [2, 6, 10, 14, 15]) await t.key(U.WHITE[k]);
    t.shot("28c_loops_pattern"); await t.tap("HOME");
    await t.tap("REC"); t.shot("28d_loops_armed"); await t.tap("REC");   /* (on + NEW LOOP, paused: a note or PLAY starts it) */
    await t.tap("SEQ"); await t.key(17); await t.key(U.WHITE[1]); await t.sleep(2100); t.shot("28e_loops_control");
    await t.key(U.WHITE[1]); await t.key(17); await t.tap("SEQ"); await t.sleep(2100);
    await t.tap("GLO"); t.shot("28f_loops_glo"); await t.tap("GLO");
    await t.turn("SELECT", -1); await t.tap("SEL"); t.shot("28g_loops_sel"); await t.tap("HOME");
    {                                                             /* 16 loops: the two, again and again, colour by colour */
        const was = structuredClone(LS());
        t.M.state.loops.list = Array.from({ length: 16 }, (_, i) => Object.assign(structuredClone(was.list[i % 2]), { col: i % 12, muted: i === 5 }));
        for (let i = 0; i < 16; i++) t.M.state.loops.mixer[i] = structuredClone(was.mixer[i % 2]);
        await t.turn("SELECT", 20); t.shot("28i_loops_full16");
        t.M.state.loops = was; P.mutesChanged(); await t.turn("SELECT", -20);
    }
    {                                                             /* RECORD LIVE recording, the loops' second pass (the song dropped after):
                                                                   * SEL's RECORD LIVE, its panel's REC, PLAY */
        const songs = t.M.state.songs.length, pats = t.M.state.patterns.slice();
        await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf("RECORD LIVE"); await t.tap("SEL");
        await t.tap("REC"); await t.tap("PLAY");
        for (let n = 0; n < 300 && U.screen.liveRow() < LS().rows; n++) await t.sleep(20);
        await t.sleep(300); t.shot("28h_loops_song");
        P.pause(); await t.sleep(100);
        t.M.state.songs.length = songs; t.M.state.patterns = pats; t.M.settle(); t.M.version++;
    }
    {                                                             /* the RECORD LIVE panel over two loops, as fw/tests/ui3_test.c's
                                                                   * test_loops_live makes them (F0:00 and F0:01, the second X4) */
        const was = LS(), inst = U.inst, row = P.kept.loops.row, cells = (base, notes) => {
            const a = new Array(base * 4).fill(null);
            for (const [r, n] of notes) a[r * 4] = n === t.M.OFF ? t.M.newCell(t.M.OFF, null, null) : t.M.newCell(n, null, 100);
            return a;
        };
        const two = [{ inst: 0xF000, col: 0, mono: false, lanes: 1, base: 16, repeats: 1, muted: false, soloed: false,
                       cells: cells(16, [[0, 60], [4, t.M.OFF], [8, 62], [12, t.M.OFF]]) },
                     { inst: 0xF001, col: 1, mono: false, lanes: 1, base: 4, repeats: 4, muted: false, soloed: false,
                       cells: cells(4, [[0, 64], [2, t.M.OFF]]) }];
        t.M.state.loops = Object.assign(structuredClone(was), { list: two, rows: 16, tempo: 30, quantize: "ROW", nextMono: false, sel: 0 });
        U.setInst(0xF000); P.mutesChanged(); P.kept.loops.row = 0;   /* (paused at their start) */
        await t.tap("SEL"); const m = U.top(); m.sel = m.items.indexOf("RECORD LIVE"); await t.tap("SEL");
        t.shot("ui3_loops_record_live"); await t.tap("HOME");
        t.M.state.loops = was; U.setInst(inst); P.mutesChanged(); P.kept.loops.row = row;
    }
    U.pattern = t.M.state.patterns.find(p => p && !p.rom).n;      /* (not the FUSION's pattern, in ROM, from 20c) */
    await t.homeKey(3);
    await t.tap("SEL"); const mp = U.top(); mp.sel = mp.items.indexOf("RECORD PATTERN"); await t.tap("SEL");
    await t.turn("K3", -4); await t.turn("K4", 1); t.shot("28d_editor_pattern");
    await t.tap("HOME"); U.recPat.size = 16; U.recPat.round = true; U.recPat.steps.fill(false);
    await t.homeKey(17);
    for (let i = 1; i < 6; i++) { await t.turn("SELECT", 1); t.shot("29_status_" + i); }
    t.down("HOME"); await t.sleep(700); t.shot("30_home_layer"); t.up("HOME"); await t.sleep(200);
    /* HOME's combination, tapped open: on the EDITOR, then its white keys switched to solo */
    await t.homeKey(3); await t.tap("HOME"); t.shot("31_home_combo");
    await t.tap("SEQ"); t.shot("31b_home_solo"); await t.tap("HOME");
    /* the count-in's box over the EDITOR (1 BAR, its second beat), and armed (KEY) */
    if (!U.editor.rec) await t.tap("REC");
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(60 / t.P.tempo() * 1000 + 200); t.shot("32_count_in");
    t.down("PLAY"); await t.sleep(30); t.up("PLAY"); await t.sleep(100);
    t.M.state.settings["COUNT-IN"] = "KEY";
    t.down("PLAY"); await t.sleep(40); t.up("PLAY"); await t.sleep(150); t.shot("32b_take_armed");
    await t.tap("HOME"); await t.tap("REC");
    t.M.state.settings["COUNT-IN"] = "1 BAR";
    /* DJ: empty; two loops on the decks, synced, a LOOP on the left, the right filtered; HOME's combination;
     * its properties; FX on DJ's own setup; the LFO pads; PLAY's pads held (CUE + PLAY, MOVE, BRAKE); the help */
    const song = name => t.M.state.songs.findIndex(s => s.name === name);
    await t.homeKey(10); t.shot("33_dj_empty");
    /* (the black keys: OP4 / OP5 LOAD, OP1 / OP6 PLAY, OP3 the left's FILTER, the blank key the pads' mode; SYNC on
     * and the filters on at OFF at first). fw/tests/ui3_test.c's dj_as_playground does the same, for ui_shots.js */
    await t.turn("SELECT", song("TRANCE LOOP") - U.screen.listAt()); await t.key(8);
    await t.turn("SELECT", song("BREAKS LOOP") - U.screen.listAt()); await t.key(10);
    await t.key(1); await t.key(13);
    await t.sleep(2600);
    await t.key(25);                                              /* LOOPS: the right's PATTERN, the left's 1 BAR */
    await t.key(U.WHITE[8]); t.keyDown(U.WHITE[3]); await t.sleep(40); t.keyUp(U.WHITE[3]);
    await t.turn("K3", -12); await t.turn("K2", 20); await t.key(5); await t.turn("ALGORITHM", 12); await t.sleep(1300);
    t.P.djKey(t.P.dj.sides[0], 2); await t.homeKey(U.WHITE[9]);   /* (the left's KEY +2, the right's track 01 muted; VOLUMEs -6, +3) */
    await t.tap("ENV"); await t.turn("K2", -6); await t.turn("K3", 3); await t.tap("ENV");
    t.shot("33b_dj_decks");                                       /* (the filters: the right's LP on, the left's HP set, off) */
    t.P.djKey(t.P.dj.sides[0], 0); await t.homeKey(U.WHITE[9]); t.P.djVol(t.P.dj.sides[0], 0); t.P.djVol(t.P.dj.sides[1], 0);
    await t.tap("HOME"); t.shot("33c_dj_home"); await t.tap("HOME");
    await t.tap("GLO"); t.shot("33d_dj_props");
    await t.tap("GLO");
    await t.tap("FX"); t.shot("33e_dj_fx_live"); await t.tap("SEQ"); t.shot("33f_dj_fx_edit"); await t.tap("SEQ"); await t.tap("FX");
    /* the LFO pads: the crossfader's L TO R, the right filter's, the FX LFO on; then an on / off pad held, ALG its
     * speed (LFO held first: its layer, the pads' map; let go, the LFO pads) */
    t.down("LFO"); await t.sleep(700); t.shot("33fa_dj_lfo_layer"); t.up("LFO"); await t.sleep(200);
    await t.key(U.WHITE[0]); await t.key(U.WHITE[2]); await t.key(U.WHITE[8]); await t.key(U.WHITE[12]); await t.sleep(1300);
    t.shot("33g_dj_lfo");
    t.keyDown(U.WHITE[8]); await t.sleep(30); await t.turn("ALGORITHM", -2); t.shot("33h_dj_lfo_speed"); t.keyUp(U.WHITE[8]); await t.sleep(40);
    await t.key(U.WHITE[15]); await t.tap("LFO"); await t.sleep(1300);
    /* PLAY's pads, the left deck's: CUE held, then OP1 (let go, it plays on); MOVE held, its cursor 2 bars on; BRAKE held */
    await t.key(25);
    t.keyDown(U.WHITE[1]); await t.sleep(200); await t.key(1); t.shot("33i_dj_cue_play"); t.keyUp(U.WHITE[1]); await t.sleep(300);
    t.keyDown(U.WHITE[3]); await t.sleep(30); await t.turn("K1", 2); t.shot("33j_dj_move"); t.keyUp(U.WHITE[3]); await t.sleep(800);
    t.keyDown(U.WHITE[4]); await t.sleep(900); t.shot("33k_dj_brake"); t.keyUp(U.WHITE[4]); await t.sleep(300);
    /* the help's four pages of its own */
    await t.combo("HOME", "LFO");
    for (let k = 0; k < 4; k++) { t.shot("33" + "lmno"[k] + "_dj_help" + (k + 1)); await t.turn("SELECT", 1); }
    await t.tap("HOME");
    t.P.pause();
};
