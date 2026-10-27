/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The landing's screens (specs/site.md), made as fw/tests/manual_shots.c makes the manual's: all of fw/core on the PC
 * on the host tests' fake FM-1 (ui3_test.c, included whole), the demo disk laid into its flash, booted once; then each
 * screen in a process of its own (fork), from the same start: its script, its checks, the shot. A screen either has a
 * script of its own or is one of ui3's shots, taken from the run of the ui3 step that makes it.
 * Core 2 is taken as running (each half's jobs done at once), so VOICES 16 is 16, as on the FM-1.
 *
 *   shots DISK.ptfmdsk OUTDIR          OUTDIR/ID.ppm for every screen below (site/tools/shots.sh runs it) */
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define main ui3_main
#include "ui3_test.c"
#undef main

int plat_c2_half(void) { return 1; }
void plat_c2_post(void (*job)(uint32_t), uint32_t j) { job(j); }

typedef struct {
    const char *id;
    void (*make)(void);         /* a script of its own: ends on its checks, then the shot is taken as the id */
    void (*step)(void);         /* or a ui3 step, run whole ... */
    const char *ui3;            /* ... and this of its shots taken */
} sshot_t;

/* a moving picture (VISUAL) is taken as one whole frame drawn at once */
static int whole_frame;

/* the black keys' notes (panel/layout.js): OP1 1, OP2 3, OP3 5, OP4 8, OP5 10, PIT 15, GLO 17, MONO 20 */
enum { K_OP1 = 1, K_OP2 = 3, K_OP3 = 5, K_OP4 = 8, K_OP5 = 10, K_PIT = 15, K_GLO = 17, K_MONO = 20 };

static void go_to(int key, const char *name)
{
    home_key(key);
    char what[64];
    snprintf(what, sizeof what, "the screen is %s", name);
    check(screen_is(name), what);
}
static void slow_turn(int k, int clicks)               /* (a click at a time, too slow for the knobs' acceleration) */
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(700); }
}

/* the cartridges list at its end: every kind at once (SAMPLER, FUSION, DX7, JUNO-106, CHIP, TB-303) */
static void s_instruments_all(void) { go_to(K_OP3, "INSTRUMENTS"); tap(B_ENV); turn(K_SELECT, 16); }
/* VISUAL's VOICES at 16: MIDNIGHT RUN playing and a chord held on the keys over it */
static void s_visual_voices16(void)
{
    go_to(K_OP1, "SONGS");
    turn(K_SELECT, 2);
    tap(B_PLAY);
    run(5000);
    go_to(K_PIT, "VISUAL");
    turn(K_K2, 2);                                      /* (GAIN X4: the waves tall enough to see) */
    static const int chord[] = { 0, 4, 7, 9, 11, 14, 16, 19, 21, 23 };
    for (unsigned i = 0; i < sizeof chord / sizeof chord[0]; i++) key(chord[i], 1);
    run(900);
    whole_frame = 1;
}
static void s_settings_midi(void) { go_to(K_MONO, "SETTINGS"); tap(B_SEQ); run(300); }
static void s_status_storage(void) { tap(B_PLAY); run(3000); go_to(K_GLO, "STATUS"); slow_turn(K_SELECT, 2); run(400); }
static void s_songs_list(void) { go_to(K_OP1, "SONGS"); turn(K_SELECT, 2); }
/* the EDITOR on MIDNIGHT RUN's pattern 0D, the cursor a few rows down on a track with notes (the hero's screen) */
static void s_editor_grid(void)
{
    go_to(K_OP2, "EDITOR");
    tap(B_ENV);
    turn(K_SELECT, 0x0D);
    tap(B_ENV);
    check(screen_is("EDITOR") && !ui_overlay_count(), "the pattern list closed on the pattern picked");
    turn(K_SELECT, 4);
    turn(K_ALG, 2);
}

static const sshot_t SHOTS[] = {
    {"instruments-all", s_instruments_all, 0, 0},
    {"visual-voices16", s_visual_voices16, 0, 0},
    {"fx-live-punch", 0, test_fx, "ui3_fx_live_punch"},
    {"loops-full", 0, loops_as_playground, "28i_loops_full16"},
    {"dj-decks", 0, dj_as_playground, "33b_dj_decks"},
    {"settings-midi", s_settings_midi, 0, 0},
    {"status-storage", s_status_storage, 0, 0},
    {"songs-list", s_songs_list, 0, 0},
    {"editor-grid", s_editor_grid, 0, 0},
};
#define NSHOTS ((int)(sizeof SHOTS / sizeof SHOTS[0]))

/* in its own process: the screen's state, its checks, its PPM in dir */
static int take(const sshot_t *m, const char *dir)
{
    if (m->make) {
        out = dir;
        m->make();
        if (whole_frame) { run(40); g_invalidate(); ui_render(); ppm(m->id); }
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
    if (argc != 3) { fprintf(stderr, "usage: shots DISK.ptfmdsk OUTDIR\n"); return 2; }
    lay_disk(argv[1]);
    app_init();
    run(100);
    int bad = 0;
    for (int i = 0; i < NSHOTS; i++) {
        fflush(stdout);
        pid_t p = fork();
        if (p < 0) { perror("fork"); return 1; }
        if (!p) _exit(take(&SHOTS[i], argv[2]));
        int st = 0;
        waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL %s\n", SHOTS[i].id); bad++; }
    }
    printf(bad ? "shots: %d of %d FAILED\n" : "shots: %d screens\n", bad ? bad : NSHOTS, NSHOTS);
    return bad ? 1 : 0;
}
