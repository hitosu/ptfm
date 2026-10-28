/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The manual's screenshots (specs/manual.md section 3): all of fw/core on the PC on the host tests' fake FM-1
 * (ui3_test.c, included whole), the demo disk laid into its flash, booted once; then each figure in a process of its
 * own (fork), from the same start: its script, its checks, the shot. A figure either has a script of its own or is
 * one of ui3's shots, taken from the run of the ui3 step that makes it (the step's own checks hold).
 *
 *   manual_shots DISK.ptfmdsk OUTDIR [ID...]    OUTDIR/ID.ppm for each id (all when none); exit 1 if a check fails,
 *                                               2 for an unknown id
 *   manual_shots --list                         the ids, one a line */
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define main ui3_main
#include "ui3_test.c"
#undef main

typedef struct {
    const char *id;
    void (*make)(void);         /* a script of its own: ends on its checks, then the shot is taken as the id */
    void (*step)(void);         /* or a ui3 step, run whole ... */
    const char *ui3;            /* ... and this of its shots taken */
} mshot_t;

/* a moving picture (VISUAL) is taken as one whole frame drawn at once: ui3's shot compares the frame sent in parts with
 * the whole, which a picture moving between the two never passes */
static int whole_frame;
static void shot_whole(const char *name)
{
    run(40);
    g_invalidate();
    ui_render();
    ppm(name);
}

/* the black keys' notes (panel/layout.js): OP1 1, OP2 3, OP3 5, OP4 8, OP5 10, OP6 13, PIT 15, GLO 17, MONO 20, POLY 22 */
enum { K_OP1 = 1, K_OP2 = 3, K_OP3 = 5, K_OP4 = 8, K_OP5 = 10, K_PIT = 15, K_GLO = 17, K_MONO = 20, K_POLY = 22 };

static void go_to(int key, const char *name)
{
    home_key(key);
    char what[64];
    snprintf(what, sizeof what, "the figure's screen is %s", name);
    check(screen_is(name), what);
}
static void s_songs_list(void) { go_to(K_OP1, "SONGS"); turn(K_SELECT, 2); }
static void s_songs_order(void) { go_to(K_OP1, "SONGS"); turn(K_SELECT, 2); tap(B_EDIT); turn(K_ALG, 2); }

/* the EDITOR on a numbered pattern of the demo disk (MIDNIGHT RUN's 0D, picked in ENV's list), the cursor a few rows
 * down on a track with notes */
static void editor_on_pattern(void)
{
    go_to(K_OP2, "EDITOR");
    tap(B_ENV);
    turn(K_SELECT, 0x0D);
    tap(B_ENV);
    check(screen_is("EDITOR") && !ui_overlay_count(), "the pattern list closed on the pattern picked");
    turn(K_SELECT, 4);
    turn(K_ALG, 2);
}
static void s_editor_grid(void) { editor_on_pattern(); }
static void s_editor_1track(void) { editor_on_pattern(); tap(B_SEQ); tap(B_SEQ); tap(B_SEQ); run(2100); }
static void s_editor_note(void) { editor_on_pattern(); tap(B_EDIT); }

static void s_editor_list(void) { go_to(K_OP2, "EDITOR"); tap(B_ENV); turn(K_SELECT, 0x0D); }
static void s_editor_block(void)
{
    editor_on_pattern();
    press(B_SEL);                                       /* (SEL held + SELECT / ALGORITHM: marks) */
    turn(K_SELECT, 3);
    turn(K_ALG, 1);
    release(B_SEL);
    run(400);
}
static void s_editor_props(void) { editor_on_pattern(); tap(B_GLO); }
/* INSTRUMENTS: the built-in F0 at power-on; K1 turns the cartridges (F0 ... FA, then round to the disk's 01 ...) */
static void s_instr_browse(void) { go_to(K_OP3, "INSTRUMENTS"); turn(K_SELECT, 3); }
static void s_instr_dx7(void) { go_to(K_OP3, "INSTRUMENTS"); turn(K_SELECT, 3); tap(B_EDIT); }
static void slow_turn(int k, int clicks)               /* (a click at a time, too slow for the knobs' acceleration) */
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(700); }
}
static void s_instr_tb303(void) { go_to(K_OP3, "INSTRUMENTS"); turn(K_K1, 11); tap(B_EDIT); }
static void s_instr_sampler(void)
{
    go_to(K_OP3, "INSTRUMENTS");
    slow_turn(K_K1, -6);                                /* F0 back to the demo disk's 01 DRUM KIT */
    tap(B_EDIT);
}

static void s_editor_4(void) { editor_on_pattern(); tap(B_SEQ); run(2100); }
static void s_editor_2(void) { editor_on_pattern(); tap(B_SEQ); tap(B_SEQ); run(2100); }
static void s_editor_menu(void) { editor_on_pattern(); tap(B_SEL); check(ui_overlay_count() > 0, "the cell's menu open"); }
static void s_songs_props(void) { go_to(K_OP1, "SONGS"); turn(K_SELECT, 2); tap(B_GLO); }
static void s_songs_menu(void) { go_to(K_OP1, "SONGS"); turn(K_SELECT, 2); tap(B_SEL); }
/* a cartridge by K1, from F0: -6 the disk's 01 DRUM KIT, -1 its 06 FUSION, +8 F8 JUNO-106, +9 F9 CHIPTUNE, +10 FA */
/* what a script checks after its presses (specs/manual.md section 3): the header's subpage, a word of the footer */
static int sub_is(const char *w)
{
    const ui_screen_t *s = ui_screen_now();
    const char *t = s && s->sub ? s->sub() : 0;
    return t && !strcmp(t, w);
}
static int foot_has(const char *w)
{
    uint8_t col;
    const ui_screen_t *s = ui_screen_now();
    const char *t = s && s->footer ? s->footer(&col) : 0;
    return t && strstr(t, w);
}
static void instr_cart(int clicks, int kind)           /* K1 from F0: the cartridge reached of the kind wanted */
{
    char what[80];
    go_to(K_OP3, "INSTRUMENTS");
    slow_turn(K_K1, clicks);
    snprintf(what, sizeof what, "INSTRUMENTS: K1 %d clicks on a cartridge of kind %02X (the current %04X)", clicks, kind,
             ui_cur.inst);
    check(screen_is("INSTRUMENTS") && ins_kind((uint8_t)(ui_cur.inst >> 8)) == kind, what);
}
static void instr_page(int clicks, int kind, int pages)
{
    instr_cart(clicks, kind);
    tap(B_EDIT);
    for (int i = 0; i < pages; i++) tap(B_SEQ);
    run(1300);
    check(sub_is("EDIT"), "INSTRUMENTS: EDIT, the edit mode");
}
static void s_instr_carts(void) { go_to(K_OP3, "INSTRUMENTS"); tap(B_ENV); }
static void s_dx7_env(void) { instr_page(0, REC_DX7, 1); }
static void s_dx7_pitch(void) { instr_page(0, REC_DX7, 2); }
static void s_dx7_voice(void) { instr_page(0, REC_DX7, 3); }
static void s_juno_browse(void) { instr_cart(8, REC_JUNO); }
static void s_juno_dco(void) { instr_page(8, REC_JUNO, 0); }
static void s_juno_filters(void) { instr_page(8, REC_JUNO, 1); }
static void s_juno_env(void) { instr_page(8, REC_JUNO, 2); }
static void s_juno_voice(void) { instr_page(8, REC_JUNO, 3); }
static void s_tb303_browse(void) { instr_cart(10, REC_TB303); }
static void s_chip_browse(void) { instr_cart(9, REC_CHIP); }
static void s_chip_tone(void) { instr_page(9, REC_CHIP, 0); }
static void s_chip_volume(void) { instr_page(9, REC_CHIP, 1); }
static void s_chip_tables(void) { instr_page(9, REC_CHIP, 2); }
static void s_chip_wave(void) { instr_page(9, REC_CHIP, 3); }
static void s_sampler_browse(void) { instr_cart(-6, REC_SAMPLER); }
static void s_sampler_env(void) { instr_page(-6, REC_SAMPLER, 1); }
static void s_fusion_browse(void) { instr_cart(-1, REC_FUSION); }
static void s_fusion_edit(void) { instr_page(-1, REC_FUSION, 0); }

/* PLAYBACK following MIDNIGHT RUN, started on SONGS and played a few seconds */
static void playback_playing(void)
{
    go_to(K_OP1, "SONGS");
    turn(K_SELECT, 2);
    tap(B_PLAY);
    run(4000);
    go_to(K_OP4, "PLAYBACK");
    player_state_t st;
    player_state(&st, 0);
    check(st.what == PL_SONG && st.playing, "the song plays on into PLAYBACK");
}
static void s_playback_view(void) { playback_playing(); turn(K_ALG, 2); run(500); }
static void s_playback_1track(void) { playback_playing(); turn(K_ALG, 2); tap(B_SEQ); tap(B_SEQ); tap(B_SEQ); run(2100); }
static void s_playback_songs(void) { playback_playing(); tap(B_ENV); }
static void s_playback_glo(void) { playback_playing(); tap(B_GLO); }

/* VISUAL: MIDNIGHT RUN playing a while, then the view (SEQ from VOICES), frozen (GLO's FREEZE) so the frame drawn
 * whole equals the one sent in parts */
static void visual_view(int n)
{
    go_to(K_OP1, "SONGS");
    turn(K_SELECT, 2);
    tap(B_PLAY);
    run(6000);
    go_to(K_PIT, "VISUAL");
    for (int i = 0; i < n; i++) tap(B_SEQ);
    if (n <= 1) turn(K_K2, 2);                          /* (VOICES, SCOPE: GAIN X4, the waves tall enough to see) */
    run(n == 5 ? 2500 : n == 2 ? 9000 : 1400);         /* (FOMA: on its way a while, before any corner; WAVE: its
                                                         * seconds across the screen filled; the knob's readout gone) */
    whole_frame = 1;                                    /* (not frozen: no FROZEN in its footer) */
}
static void s_visual_voices(void) { visual_view(0); }
static void s_visual_scope(void) { visual_view(1); }
static void s_visual_wave(void) { visual_view(2); }
static void s_visual_spectrum(void) { visual_view(3); }
static void s_visual_levels(void) { visual_view(4); }
static void s_visual_foma(void)                         /* (no corner hit yet: none of what is left to find) */
{
    visual_view(5);
    check(screen_is("VISUAL") && !foot_has("SCORE"), "VISUAL's FOMA: no corner hit yet, so no CORNER! and no SCORE:");
}
static void s_visual_options(void) { tap(B_PLAY); run(500); go_to(K_PIT, "VISUAL"); tap(B_GLO); }
/* STATUS' pages (SELECT, a step at a time), a song playing for the figures */
static void status_page(int n) { tap(B_PLAY); run(3000); go_to(K_GLO, "STATUS"); slow_turn(K_SELECT, n); run(400); }
static void s_status_ptfm(void) { status_page(0); }
static void s_status_perf(void) { status_page(1); }
static void s_status_storage(void) { status_page(2); }
static void s_status_flash(void) { status_page(3); }
static void s_status_ram(void) { status_page(4); }
static void s_status_device(void) { status_page(5); }
/* SETTINGS: the list's items in order (OUTPUT 0 ... TUNING 20, TOOLS 21, ABOUT 22) */
static void s_settings_top(void) { go_to(K_MONO, "SETTINGS"); }
/* (SETTINGS' items by name, S_*: their places move as items come) */
static void s_settings_bottom(void) { go_to(K_MONO, "SETTINGS"); slow_turn(K_SELECT, S_POWER_ON); }
static void s_settings_midi(void) { go_to(K_MONO, "SETTINGS"); tap(B_SEQ); run(300); }
static void s_settings_about(void) { go_to(K_MONO, "SETTINGS"); slow_turn(K_SELECT, S_ABOUT); tap(B_EDIT); }
static void s_settings_cleanup(void)
{
    go_to(K_MONO, "SETTINGS");
    slow_turn(K_SELECT, S_TOOLS);
    slow_turn(K_K1, 2);                                 /* CHECK REFERENCES, CHECK STORAGE, CLEAN UP */
    tap(B_EDIT);
    run(1500);
}
static void s_settings_factory_reset(void)              /* its first question (nothing erased: the shot is taken there) */
{
    go_to(K_MONO, "SETTINGS");
    slow_turn(K_SELECT, S_TOOLS);
    slow_turn(K_K1, 5);                                 /* ... REVERT ALL, FACTORY RESET */
    tap(B_EDIT);
    run(400);
    check(!strcmp(asking(), "FACTORY RESET?"), "SETTINGS: FACTORY RESET asks first");
}

/* FX from STATUS: LIVE at first; SEQ to EDIT; THE KEYS' chain: GATE 0, FILTER 1, LO-FI 2, CHORUS 3, DELAY 4,
 * REVERB 5, DRIVE 6, LIMITER 7, then DUCK 8, METRONOME 9 */
static void fx_edit(int effect, const char *name)       /* FX's EDIT page on the effect named (the footer names it) */
{
    char what[64];
    tap(B_FX);
    tap(B_SEQ);
    slow_turn(K_SELECT, effect);
    run(400);
    snprintf(what, sizeof what, "FX: EDIT on %s", name);
    check(screen_is("FX") && sub_is("EDIT") && foot_has(name), what);
}
static void s_fx_held(void) { tap(B_PLAY); run(1000); press(B_FX); run(800); }
static void s_fx_reverb(void) { fx_edit(5, "REVERB"); }
static void s_fx_mixer(void)                            /* MIDNIGHT RUN's mixer, playing */
{
    go_to(K_OP1, "SONGS");
    turn(K_SELECT, 2);
    tap(B_PLAY);
    run(2000);
    tap(B_FX); tap(B_SEQ); tap(B_SEQ);
    run(400);
    check(screen_is("FX") && sub_is("MIXER"), "FX: the MIXER");
}
static void s_fx_metronome(void) { fx_edit(9, "METRONOME"); }
/* each effect on EDIT, switched on (OCT+), its picture drawn; the header's toast gone (2.1 s) */
static void fx_on(int effect, const char *name) { fx_edit(effect, name); tap(B_OCTP); run(2100); }
static void s_fx_gate(void) { fx_on(0, "GATE"); }
static void s_fx_filter(void) { fx_on(1, "FILTER"); slow_turn(K_K1, -12); run(2100); }   /* (CUT a low-pass) */
static void s_fx_lofi(void) { fx_on(2, "LO-FI"); }
static void s_fx_chorus(void) { fx_on(3, "CHORUS"); }
static void s_fx_delay(void) { fx_on(4, "DELAY"); }
static void s_fx_drive(void) { fx_on(6, "DRIVE"); }
static void s_fx_limiter(void) { fx_edit(7, "LIMITER"); run(2100); }   /* (on already, in every setup) */
static void s_fx_duck(void) { fx_on(8, "DUCK"); }
static void s_fx_filter_env(void) { fx_edit(1, "FILTER"); turn(K_ALG, 1); run(1300); }
static void s_fx_props(void) { fx_edit(0, "GATE"); tap(B_GLO); }

static const mshot_t SHOTS[] = {
    {"fx-live", 0, test_fx, "23_fx_live"},
    {"fx-edit", 0, test_fx, "23b_fx_edit"},
    {"fx-mixer", s_fx_mixer, 0, 0},
    {"fx-live-filter", 0, test_fx, "ui3_fx_live_filter"},
    {"fx-live-punch", 0, test_fx, "ui3_fx_live_punch"},
    {"fx-live-bypass", 0, test_fx, "ui3_fx_live_bypass"},
    {"fx-gate-steps", 0, test_fx, "ui3_fx_gate_edit"},
    {"fx-held", s_fx_held, 0, 0},
    {"fx-reverb", s_fx_reverb, 0, 0},
    {"fx-gate", s_fx_gate, 0, 0},
    {"fx-filter", s_fx_filter, 0, 0},
    {"fx-lofi", s_fx_lofi, 0, 0},
    {"fx-chorus", s_fx_chorus, 0, 0},
    {"fx-delay", s_fx_delay, 0, 0},
    {"fx-drive", s_fx_drive, 0, 0},
    {"fx-limiter", s_fx_limiter, 0, 0},
    {"fx-duck", s_fx_duck, 0, 0},
    {"fx-metronome", s_fx_metronome, 0, 0},
    {"fx-filter-env", s_fx_filter_env, 0, 0},
    {"fx-props", s_fx_props, 0, 0},
    {"loops-empty", 0, loops_as_playground, "27_loops_empty"},
    {"loops-first", 0, loops_as_playground, "28_loops_first"},
    {"loops-two", 0, loops_as_playground, "28b_loops_two"},
    {"loops-home", 0, loops_as_playground, "31c_home_loops"},
    {"loops-home-muted", 0, loops_as_playground, "31d_home_loops_muted"},
    {"loops-recorder", 0, loops_as_playground, "28c_loops_pattern"},
    {"loops-armed", 0, loops_as_playground, "28d_loops_armed"},
    {"loops-props", 0, loops_as_playground, "28f_loops_glo"},
    {"loops-menu", 0, loops_as_playground, "28g_loops_sel"},
    {"loops-full", 0, loops_as_playground, "28i_loops_full16"},
    {"loops-song", 0, loops_as_playground, "28h_loops_song"},
    {"loops-save-to", 0, test_loops, "ui3_loops_save_to"},
    {"loops-record-live", 0, test_loops, "ui3_loops_record_live"},
    {"loops-repeats", 0, test_loops, "ui3_loops_repeats"},
    {"visual-voices", s_visual_voices, 0, 0},
    {"visual-scope", s_visual_scope, 0, 0},
    {"visual-wave", s_visual_wave, 0, 0},
    {"visual-spectrum", s_visual_spectrum, 0, 0},
    {"visual-levels", s_visual_levels, 0, 0},
    {"visual-foma", s_visual_foma, 0, 0},
    {"visual-options", s_visual_options, 0, 0},
    {"status-ptfm", s_status_ptfm, 0, 0},
    {"status-performance", s_status_perf, 0, 0},
    {"status-storage", s_status_storage, 0, 0},
    {"status-flash", s_status_flash, 0, 0},
    {"status-ram", s_status_ram, 0, 0},
    {"status-device", s_status_device, 0, 0},
    {"settings-list", s_settings_top, 0, 0},
    {"settings-list-end", s_settings_bottom, 0, 0},
    {"settings-midi", s_settings_midi, 0, 0},
    {"settings-about", s_settings_about, 0, 0},
    {"settings-cleanup", s_settings_cleanup, 0, 0},
    {"settings-factory-reset", s_settings_factory_reset, 0, 0},
    {"dj-empty", 0, dj_as_playground, "33_dj_empty"},
    {"dj-decks", 0, dj_as_playground, "33b_dj_decks"},
    {"dj-home", 0, dj_as_playground, "33c_dj_home"},
    {"dj-props", 0, dj_as_playground, "33d_dj_props"},
    {"dj-fx-live", 0, dj_as_playground, "33e_dj_fx_live"},
    {"dj-fx-edit", 0, dj_as_playground, "33f_dj_fx_edit"},
    {"dj-lfo-layer", 0, dj_as_playground, "33fa_dj_lfo_layer"},
    {"dj-lfo", 0, dj_as_playground, "33g_dj_lfo"},
    {"dj-lfo-speed", 0, dj_as_playground, "33h_dj_lfo_speed"},
    {"dj-cue-play", 0, dj_as_playground, "33i_dj_cue_play"},
    {"dj-move", 0, dj_as_playground, "33j_dj_move"},
    {"dj-brake", 0, dj_as_playground, "33k_dj_brake"},
    {"playback-view", s_playback_view, 0, 0},
    {"playback-1track", s_playback_1track, 0, 0},
    {"playback-songs", s_playback_songs, 0, 0},
    {"playback-glo", s_playback_glo, 0, 0},
    {"editor-4tracks", s_editor_4, 0, 0},
    {"editor-2tracks", s_editor_2, 0, 0},
    {"editor-menu", s_editor_menu, 0, 0},
    {"songs-props", s_songs_props, 0, 0},
    {"songs-menu", s_songs_menu, 0, 0},
    {"instruments-carts", s_instr_carts, 0, 0},
    {"instruments-dx7-envelopes", s_dx7_env, 0, 0},
    {"instruments-dx7-pitch", s_dx7_pitch, 0, 0},
    {"instruments-dx7-voice", s_dx7_voice, 0, 0},
    {"instruments-juno-browse", s_juno_browse, 0, 0},
    {"instruments-juno-dco", s_juno_dco, 0, 0},
    {"instruments-juno-filters", s_juno_filters, 0, 0},
    {"instruments-juno-envelope", s_juno_env, 0, 0},
    {"instruments-juno-voice", s_juno_voice, 0, 0},
    {"instruments-tb303-browse", s_tb303_browse, 0, 0},
    {"instruments-chip-browse", s_chip_browse, 0, 0},
    {"instruments-chip-tone", s_chip_tone, 0, 0},
    {"instruments-chip-volume", s_chip_volume, 0, 0},
    {"instruments-chip-tables", s_chip_tables, 0, 0},
    {"instruments-chip-wave", s_chip_wave, 0, 0},
    {"instruments-sampler-browse", s_sampler_browse, 0, 0},
    {"instruments-sampler-envelope", s_sampler_env, 0, 0},
    {"instruments-fusion-browse", s_fusion_browse, 0, 0},
    {"instruments-fusion-edit", s_fusion_edit, 0, 0},
    {"editor-grid", s_editor_grid, 0, 0},
    {"editor-1track", s_editor_1track, 0, 0},
    {"editor-note", s_editor_note, 0, 0},
    {"editor-list", s_editor_list, 0, 0},
    {"editor-block", s_editor_block, 0, 0},
    {"editor-props", s_editor_props, 0, 0},
    {"instruments-browse", s_instr_browse, 0, 0},
    {"instruments-edit-dx7", s_instr_dx7, 0, 0},
    {"instruments-edit-tb303", s_instr_tb303, 0, 0},
    {"instruments-edit-sampler", s_instr_sampler, 0, 0},
    {"loops-control", 0, loops_as_playground, "28e_loops_control"},
    {"songs-list", s_songs_list, 0, 0},
    {"songs-order", s_songs_order, 0, 0},
};
#define NSHOTS ((int)(sizeof SHOTS / sizeof SHOTS[0]))

static int find(const char *id)
{
    for (int i = 0; i < NSHOTS; i++)
        if (!strcmp(SHOTS[i].id, id)) return i;
    return -1;
}

/* in its own process: the figure's state, its checks, its PPM in dir */
static int take(const mshot_t *m, const char *dir)
{
    if (m->make) {
        out = dir;
        m->make();
        if (whole_frame) shot_whole(m->id);
        else shot(m->id);
    } else {
        char tmp[512], from[600], to[600];
        snprintf(tmp, sizeof tmp, "%s/.ui3-%s", dir, m->id);
        mkdir(tmp, 0777);
        out = tmp;
        m->step();
        snprintf(from, sizeof from, "%s/%s.ppm", tmp, m->ui3);
        snprintf(to, sizeof to, "%s/%s.ppm", dir, m->id);
        check(rename(from, to) == 0, "the ui3 step made its shot");
        char cmd[700];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
        if (system(cmd)) fails++;
    }
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < NSHOTS; i++) puts(SHOTS[i].id);
        return 0;
    }
    if (argc < 3) { fprintf(stderr, "usage: manual_shots DISK.ptfmdsk OUTDIR [ID...] | --list\n"); return 2; }
    for (int a = 3; a < argc; a++)
        if (find(argv[a]) < 0) { fprintf(stderr, "manual_shots: no figure %s\n", argv[a]); return 2; }
    lay_disk(argv[1]);
    app_init();
    run(100);
    int bad = 0, n = 0;
    for (int i = 0; i < NSHOTS; i++) {
        int want = argc == 3;
        for (int a = 3; a < argc && !want; a++) want = !strcmp(argv[a], SHOTS[i].id);
        if (!want) continue;
        fflush(stdout);
        pid_t p = fork();
        if (p < 0) { perror("fork"); return 1; }
        if (!p) _exit(take(&SHOTS[i], argv[2]));
        int st = 0;
        waitpid(p, &st, 0);
        n++;
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL figure %s\n", SHOTS[i].id); bad++; }
    }
    printf(bad ? "manual_shots: %d of %d figures FAILED\n" : "manual_shots: %d figures\n", bad ? bad : n, n);
    return bad ? 1 : 0;
}
