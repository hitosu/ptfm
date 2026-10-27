/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* fw/tests/ui_test.c's steps on the playground, press for press, its shots under the same names (those
 * screens.js has not), so fw/tests/ui_shots.js compares the firmware's screens with these:
 *   node tests/playground/harness.js tests/playground/mirror_ui.js   -> build/playground/ui_*.png */
"use strict";
module.exports = async t => {
    await t.combo("HOME", "LFO"); await t.tap("HOME");
    await t.homeKey(1);
    await t.tap("SEQ"); await t.sleep(2100); await t.tap("SEQ"); await t.sleep(2100);
    await t.tap("EDIT"); await t.turn("ALGORITHM", 3);
    await t.turn("K1", 1); t.shot("ui_songs_order_k1");
    await t.tap("HOME"); await t.sleep(2100);
    await t.tap("GLO"); await t.turn("SELECT", 1); await t.turn("K1", 5); t.shot("ui_song_props_tempo"); await t.tap("GLO");
    await t.tap("SAVE"); await t.sleep(1500);
    await t.tap("EDIT"); await t.turn("K1", 1); await t.tap("SEL"); t.shot("ui_songs_order_menu");
    await t.turn("SELECT", 10); await t.tap("SEL");     /* REVERT, the menu's last line while the song is unsaved */
    await t.tap("OCT+");                                /* REVERT */
    await t.tap("HOME"); await t.sleep(2100);
    await t.homeKey(8);
    await t.homeKey(1); await t.turn("ALGORITHM", 2); t.shot("ui_songs_alg");
    await t.tap("PLAY"); await t.sleep(1500); t.shot("ui_songs_playing");
    await t.homeKey(8); t.shot("ui_playback8");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("ui_playback4");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("ui_playback2");
    await t.tap("SEQ"); await t.sleep(2100); t.shot("ui_playback1");
    await t.tap("SEQ"); await t.sleep(2100);
    await t.tap("PLAY"); await t.sleep(300);            /* paused */
    await t.turn("SELECT", 2); await t.sleep(100);
    await t.tap("ARP"); await t.tap("ARP");
    await t.homeKey(20); await t.tap("SEQ"); await t.tap("SEQ");
    await t.homeKey(17); await t.turn("SELECT", 3); await t.turn("SELECT", -3);
    t.down("HOME"); await t.sleep(600); t.up("HOME"); await t.sleep(200);
    await t.tap("HOME"); await t.tap("HOME");
    t.down("SEL"); await t.sleep(600); t.shot("ui_sel_layer"); t.up("SEL"); await t.sleep(200);
    t.down("FX"); await t.sleep(600); t.keyDown(0); await t.sleep(30); t.shot("ui_fx_layer_punch");
    t.keyUp(0); await t.sleep(30); t.up("FX"); await t.sleep(200);
    await t.homeKey(0); await t.homeKey(0);             /* track 00 muted, and on again */
    t.down("HOME"); await t.sleep(30); await t.turn("SELECT", 1); t.up("HOME"); await t.sleep(200);
    await t.homeKey(17);
    await t.homeKey(1); await t.tap("GLO"); t.shot("ui_song_props"); await t.tap("GLO");
    await t.homeKey(20); await t.turn("SELECT", 5); await t.turn("K1", 1); t.shot("ui_settings_theme"); await t.tap("LFO");
    await t.turn("SELECT", 20); await t.turn("SELECT", -1);     /* TOOLS */
    await t.tap("EDIT"); await t.sleep(800);
    await t.turn("K1", 1); await t.tap("EDIT"); await t.sleep(800);
    await t.turn("K1", 1); await t.tap("EDIT"); t.shot("ui_clean_up"); await t.tap("OCT-");
    await t.turn("K1", -2); await t.turn("SELECT", -20); await t.turn("SELECT", 5);
    await t.turn("K1", 1); await t.combo("SAVE", "OCT-"); t.shot("ui_revert_question"); await t.tap("OCT-");
    await t.tap("LFO");
    /* SONGS' list menu: no MOVE TO FWS (FWS holds nothing of the user's), SELECT down to its last item */
    await t.homeKey(1);
    await t.tap("SEL"); await t.turn("SELECT", -12); await t.turn("SELECT", 12); t.shot("ui_songs_menu"); await t.tap("HOME");
    t.P.pause();
};
