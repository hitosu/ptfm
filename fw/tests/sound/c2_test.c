/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The second core's hand-over (specs/second-core.md#when) on the PC: one session through player.h, made in whichever
 * mode the test platform's second core is in (C2_MODE: disk_store.c), each case's output in files of its own;
 * run_sound.sh runs it in every mode and compares modes 1-4 with mode 0, byte for byte. The cases, in this order:
 * DJ HEAVY's pair on two decks (first: mode 4's fallback lands in it), DJ's stops on the shared DELAY and REVERB with
 * a DJ FILTER and its LFO, 20 s of every song of the disk from its start, PUNCH IN with the punch-ins pressed and let
 * go at odd blocks, a song with the METRONOME on through a COUNT-IN, CUT pressed and let go at odd halves, the keys
 * DRY with notes, SYNC PULSE and SETTINGS' SECOND CORE switched at odd halves. A case's files: .lr every half as player_audio leaves it, read
 * once the next half is made (as the codec reads it: core 2 may still be on a half's last blocks when player_audio
 * returns), .usb USB audio's tap and .out player_out's blocks as they come, a hash a block (each counted to the case
 * that made it). Each case must be heard, every block must reach the taps once, and the blocks go to core 2 as the
 * mode has it (none in mode 0).
 *   c2_test DISK OUT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/plat.h"
#include "../../core/play/player.h"
#include "../../core/play/dj.h"
#include "../../core/fx/fx.h"
#include "../../core/store/store.h"
#include "../../core/spare.h"

int disk_load(const char *path);
extern uint32_t c2_posts;                /* (disk_store.c: the blocks handed over to core 2) */
extern uint32_t c2_voice_jobs, c2_lost;  /* (the voice jobs handed over; mode 5's one lost) */
static int v16, mode;                    /* C2_V16: the 16 voices' session; C2_MODE */
static int finish(void);

#define NCASE 48
static struct {
    char name[16];
    FILE *f[3];                          /* .lr, .usb, .out */
    uint64_t block0;                     /* the session's block it starts at */
    double sum;                          /* its halves' squares */
    uint64_t frames;
} cs[NCASE];
static int ncase, fails;
static const char *dir;
static uint64_t made;                    /* blocks made so far */
static uint64_t n_usb, n_out;            /* blocks USB audio's tap and player_out were given */
static int16_t buf[2][2 * 256];          /* the codec's two halves */
static uint32_t nh;                      /* halves made */
static int pend = -1, pend_frames;       /* the half made last: its case and frames (written once the next is made) */

static void begin(const char *name)
{
    static const char *const EXT[3] = {"lr", "usb", "out"};
    char path[512];
    int k;
    if (ncase == NCASE) {
        fprintf(stderr, "c2_test: more than %d cases\n", NCASE);
        exit(2);
    }
    snprintf(cs[ncase].name, sizeof cs[ncase].name, "%s", name);
    for (k = 0; k < 3; k++) {
        snprintf(path, sizeof path, "%s/%s.%s", dir, name, EXT[k]);
        if (!(cs[ncase].f[k] = fopen(path, "wb"))) {
            perror(path);
            exit(2);
        }
    }
    cs[ncase].block0 = made;
    ncase++;
}
static int case_of(uint64_t block)       /* the case that made a block (one given a half late: maybe the one before) */
{
    int c = ncase - 1;
    while (c > 0 && cs[c].block0 > block)
        c--;
    return c;
}
#define FNV0 14695981039346656037ull
static uint64_t fnv(uint64_t h, const void *p, size_t n)   /* (FNV-1a, 64 bits) */
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 1099511628211ull;
    return h;
}
static void taken(const char *tap, int n)
{
    if (n != FX_BLOCK) {
        fprintf(stderr, "c2_test: %s given %d frames\n", tap, n);
        exit(2);
    }
}
static void usb_tap(const int32_t *lr, const int16_t *click, int n)   /* .usb: the mix's hash, its clicks' in it */
{
    uint64_t h;
    taken("USB audio's tap", n);
    h = fnv(FNV0, lr, 2u * FX_BLOCK * sizeof *lr);
    h = click ? fnv(h, click, FX_BLOCK * sizeof *click) : ~h;
    fwrite(&h, sizeof h, 1, cs[case_of(n_usb++)].f[1]);
}
static void out_block(const int32_t *lr, int at, int n)   /* .out: its block in the half, silent or not, its hash */
{
    uint8_t where[2] = {(uint8_t)(at / FX_BLOCK), lr != 0};
    uint64_t h = 0;
    FILE *f = cs[case_of(n_out++)].f[2];
    taken("player_out", n);
    if (lr)
        h = fnv(FNV0, lr, 2u * FX_BLOCK * sizeof *lr);
    fwrite(where, 1, 2, f);
    fwrite(&h, sizeof h, 1, f);
}

static void put(void)                    /* the half made before, finished now: into its case's file */
{
    const int16_t *b = buf[(nh - 1u) & 1u];
    int i;
    if (pend < 0)
        return;
    fwrite(b, 2u * sizeof *b, (size_t)pend_frames, cs[pend].f[0]);
    for (i = 0; i < 2 * pend_frames; i++)
        cs[pend].sum += (double)b[i] * b[i];
    cs[pend].frames += (uint64_t)pend_frames;
    pend = -1;
}
static void half(int frames)             /* a half made (frames: whole blocks, 256 at most) */
{
    player_audio(buf[nh & 1u], frames);
    put();
    pend = ncase - 1;
    pend_frames = frames;
    nh++;
    made += (uint64_t)frames / FX_BLOCK;
}
static void halves(int n)
{
    while (n-- > 0)
        half(256);
}
static void run(double seconds) { halves((int)(seconds * 44100 / 256)); }
static void blocks(int n)                /* n blocks on: whole halves, the rest a half of fewer (what follows lands on
                                          * an odd block) */
{
    for (; n >= 4; n -= 4)
        half(256);
    if (n)
        half(n * FX_BLOCK);
}

static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}
static player_state_t st(void)
{
    player_state_t s;
    player_state(&s, 0);
    return s;
}
static int key_voices(void)
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), i, c = 0;
    for (i = 0; i < n; i++)
        c += v[i].on && v[i].track == 0xFF;
    return c;
}
static void keys(int on, int base)       /* a chord on each engine's built-in instrument (and a sampler's) */
{
    static const pl_inst_t IN[5] = {{0xF0, 0x02}, {0xF8, 0x03}, {0xFA, 0x00}, {0xF0, 0x10}, {0x03, 0x08}};
    int e;
    for (e = 0; e < 5; e++)
        if (on)
            player_key_on((uint8_t)(base + 3 * e), IN[e], 100);
        else
            player_key_off((uint8_t)(base + 3 * e));
}

/* ---- 16 voices (C2_V16; specs/second-core.md#the-voices-on-core-2): VOICES 16, core 2's places (PL_C2_FROM on) on
 * core 2 through the voice job. Built with PL_ONE_CORE_16 and run in mode 0, one core renders all 16 in place: the
 * reference modes 1-3 and 6 must equal, but v16_switch (SECOND CORE off counts 16 as 12): the modes among themselves
 * there. Mode 4's fallback (at a half's start) and mode 5's (a voice job lost) only run on, the limit 12 after ---- */
static const pl_inst_t V16_IN[4] = {{0xF0, 0x02}, {0xF8, 0x03}, {0xF0, 0x10}, {0x03, 0x08}};   /* DX7, JUNO-106, DX7,
                                                                                            * a sampler's (03:08) */
static void v16_keys(int on, int from, int to, int gap)   /* keys from..to on (DX7, Juno-106, DX7, sampler in turn) */
{
    int i;
    for (i = from; i <= to; i++) {
        if (on)
            player_key_on((uint8_t)(36 + 2 * i), V16_IN[i & 3], 100);
        else
            player_key_off((uint8_t)(36 + 2 * i));
        halves(gap);
    }
}
static void v16_session(void)
{
    static player_dj_t mix;
    uint8_t nums[256];
    int i, n, k;
    player_voice_count(16);

    begin("v16_keys");                   /* 16 keys held, started at odd halves, let go one at a time */
    halves(1);
    v16_keys(1, 0, 15, 3);
    halves(397);
    check("16 keys held: 16 voices sound", key_voices() == 16 || (mode == 4 || mode == 5));
    v16_keys(0, 0, 15, 5);
    run(2.0);

    begin("v16_park");                   /* 20 keys: the newest park the oldest; 6 let go, the parked back in */
    v16_keys(1, 0, 19, 1);
    halves(201);
    check("20 keys held: 16 sound, 4 parked", (key_voices() == 16 && st().parked == 4) || (mode == 4 || mode == 5));
    v16_keys(0, 4, 9, 7);
    for (k = 0; k < 100 && st().parked; k++)   /* (their tails ending free their voices for the parked) */
        halves(17);
    printf("  6 let go: the parked back after %d x 17 halves, %d parked, %d keys' voices\n", k, st().parked,
           key_voices());
    check("6 let go: the 4 parked back as their tails end", !st().parked || (mode == 4 || mode == 5));
    v16_keys(0, 0, 3, 1);
    v16_keys(0, 10, 19, 1);
    run(2.0);

    begin("v16_over");                   /* SPICE ROAD with 8 keys held over it */
    player_play_song(0x01, 0, 0);
    run(1.0);
    v16_keys(1, 0, 7, 11);
    run(6.0);
    v16_keys(0, 0, 7, 1);
    player_stop();
    player_all_sound_off();
    run(1.0);

    begin("v16_mutes");                  /* 10 keys held, SPICE ROAD's notes in the places past them (core 2's), its
                                          * tracks muted and heard again at odd halves: core 2's places whose mute
                                          * fades, rendered and mixed by core 1 that block */
    v16_keys(1, 0, PL_C2_FROM - 1, 1);
    player_play_song(0x01, 0, 0);
    run(2.0);
    for (k = 0, n = 0; k < 24; k++) {
        pl_voice_info_t v[PL_VOICES];
        int nv = player_voices_now(v, PL_VOICES), c;
        for (c = PL_C2_FROM; c < nv; c++)
            n += v[c].on && v[c].track != 0xFF;
        player_mutes(k & 1 ? 0u : 0x5555u << (k & 2 ? 1 : 0));
        halves(3 + 2 * (k % 5));
    }
    printf("  song voices in core 2's places at the mutes: %d\n", n);
    check("mutes at 16: song voices in core 2's places as their tracks fade", n > 0);
    player_mutes(0);
    v16_keys(0, 0, PL_C2_FROM - 1, 1);
    player_stop();
    player_all_sound_off();
    run(1.0);

    n = store_list(REC_SONG, nums, 256);  /* every song 10 s from its start (release tails past 12) */
    for (i = 0; i < n; i++) {
        char name[16];
        snprintf(name, sizeof name, "v16_song_%02X", nums[i]);
        begin(name);
        player_play_song(nums[i], 0, 0);
        run(10.0);
    }

    begin("v16_dj");                     /* DJ HEAVY's pair, as the 12 voices' session has it */
    player_dj(1, DJ_FX);
    mix.xf = 16;
    mix.reso = 30;
    mix.sends_filtered = 1;
    mix.sync = 1;
    mix.mix_set[0] = 50;
    mix.mix_set[1] = 30;
    mix.mix_set[2] = 35;
    for (i = 0; i < PL_NLFO; i++) {
        mix.lfo[i].shape = 2;
        mix.lfo[i].len = 6;
    }
    player_dj_set(&mix);
    player_deck_load(0, 0x00);
    player_deck_load(1, 0x02);
    player_deck_play(0, 1, 1, 0);
    run(1.0);
    player_deck_tempo(1, 65536, st().deck[0].bpm10);
    player_deck_arm(1, 1, 0);
    run(19.0);
    check("DJ HEAVY at 16: both decks playing", st().what == PL_DJ && st().deck[0].playing && st().deck[1].playing);
    player_dj(0, 0);
    player_stop();
    player_all_sound_off();
    run(1.0);

    begin("v16_cut");                    /* 16 keys held over TECHNO LOOP, CUT pressed and let go at odd halves */
    player_play_song(0x08, 1, 0);
    run(1.0);
    v16_keys(1, 0, 15, 1);
    for (k = 0; k < 5; k++) {
        player_hush(1);
        halves(1 + 13 * k);
        player_hush(0);
        halves(29 + 7 * k);
    }
    blocks(13);
    player_hush(1);
    blocks(23);
    player_hush(0);
    run(1.0);
    v16_keys(0, 0, 15, 1);
    player_stop();
    player_all_sound_off();
    run(1.0);

    begin("v16_switch");                 /* 16 keys held, SECOND CORE off and on at odd halves: 16 counted as 12 */
    v16_keys(1, 0, 15, 1);
    for (k = 0; k < 6; k++) {
        plat_c2_enable(0);
        halves(1 + 26 * k);
        plat_c2_enable(1);
        halves(37 + 14 * k);
    }
    v16_keys(0, 0, 15, 1);
    run(2.0);
}

int main(int argc, char **argv)
{
    static player_dj_t mix;
    static const int CUTS[][2] = {{1, 37}, {3, 41}, {171, 63}, {2, 1}, {5, 29}, {1, 1}, {7, 53}};   /* (on, off) */
    static const int PULSE[][2] = {{1, 57}, {2, 41}, {0, 13}, {1, 3}, {0, 1}, {2, 89}, {0, 31}};   /* (side, halves) */
    player_state_t s;
    uint8_t nums[256];
    int n, i, k;
    if (argc != 3 || disk_load(argv[1])) {
        fprintf(stderr, "c2_test DISK OUT\n");
        return 2;
    }
    dir = argv[2];
    player_init();
    player_usb = usb_tap;
    player_out = out_block;
    dj_record_default(DJ_REC);
    player_dj_mem(spare_borrow(player_dj_mem_size()));   /* (lent as dj.c has it lent: the spare zone's top) */
    mode = getenv("C2_MODE") ? atoi(getenv("C2_MODE")) : 0;
    if ((v16 = getenv("C2_V16") != 0)) {
        v16_session();
        return finish();
    }

    /* ---- DJ HEAVY: PUNCH IN and MIDNIGHT RUN from their second patterns, SYNC on, the crossfader in the middle, no
     * DJ FILTER or LFO, DJ's own setup (DELAY, REVERB, LIMITER on; CHORUS off) ---- */
    begin("dj_heavy");
    player_dj(1, DJ_FX);
    mix.xf = 16;
    mix.reso = 30;
    mix.sends_filtered = 1;
    mix.sync = 1;
    mix.mix_set[0] = 50;
    mix.mix_set[1] = 30;
    mix.mix_set[2] = 35;
    for (i = 0; i < PL_NLFO; i++) {
        mix.lfo[i].shape = 2;
        mix.lfo[i].len = 6;
    }
    player_dj_set(&mix);
    player_deck_load(0, 0x00);           /* PUNCH IN */
    player_deck_load(1, 0x02);           /* MIDNIGHT RUN */
    player_deck_play(0, 1, 1, 0);
    run(1.0);
    player_deck_tempo(1, 65536, st().deck[0].bpm10);
    player_deck_arm(1, 1, 0);            /* (it starts on the main's next bar line) */
    run(19.0);
    s = st();
    check("DJ HEAVY: both decks playing", s.what == PL_DJ && s.deck[0].playing && s.deck[1].playing);

    /* ---- DJ's stops on the shared effects (ECHO OUT: the DELAY run, REVERB OUT: the REVERB), BRAKE, a DJ FILTER
     * swung by its LFO, KEY on the right ---- */
    begin("dj_stops");
    mix.filt[0] = -40;
    mix.filt_on[0] = 1;
    mix.lfo[PL_LFO_FILT_L] = (pl_lfo_t){1, 1, 1, 1, 0, 7};   /* (SQUARE, OFF TO FULL, 1/4) */
    mix.key[1] = 3;
    player_dj_set(&mix);
    halves(57);
    player_deck_stop(0, PL_STOP_ECHO, 1, 0);
    halves(131);
    player_deck_stop(0, PL_STOP_ECHO, 0, 0);
    halves(77);
    player_deck_stop(1, PL_STOP_REVERB, 1, 0);
    halves(149);
    player_deck_stop(1, PL_STOP_REVERB, 0, 0);
    halves(93);
    player_deck_stop(0, PL_STOP_BRAKE, 1, 0);
    halves(61);
    player_deck_stop(0, PL_STOP_BRAKE, 0, 0);
    halves(119);
    s = st();
    check("DJ's stops let go: both decks playing", s.what == PL_DJ && s.deck[0].playing && s.deck[1].playing);

    /* ---- every song of the disk, 20 s from its start ---- */
    n = store_list(REC_SONG, nums, 256);
    for (i = 0; i < n; i++) {
        char name[16];
        snprintf(name, sizeof name, "song_%02X", nums[i]);
        begin(name);
        player_play_song(nums[i], 0, 0);
        run(20.0);
    }
    check("the disk's songs: 18", n == 18);

    /* ---- PUNCH IN (its own punch-ins too) with the 16 punch-ins pressed and let go at odd blocks ---- */
    begin("punch");
    player_play_song(0x00, 0, 0);
    blocks(301);
    for (k = 0; k < 16; k++) {
        player_punch(k, 1);
        blocks(333 + 26 * k);
        player_punch(k, 0);
        blocks(211 + 14 * k);
    }

    /* ---- CITY LIGHTS with the METRONOME on; paused, a COUNT-IN of 2 bars, its pattern looping after it ---- */
    begin("metronome");
    player_play_song(0x03, 0, 0);
    player_fx_cmd(0xAE, FX_METRONOME);   /* (FX ON: the METRONOME, the song's own BEEP at 60 %) */
    run(4.0);
    player_pause();
    halves(37);
    s = st();
    player_count_in(2, s.bpm10);
    player_play_pattern(s.pat, 0);       /* (held till the count-in's last beat is over) */
    halves(9);
    s = st();
    check("the COUNT-IN under way, the pattern waiting", s.count_beats == 8 && !s.playing);
    run(8.0);
    s = st();
    check("... the pattern playing after it, the METRONOME on", s.playing && s.what == PL_LOOP && !s.count_beats);
    run(4.0);

    /* ---- TECHNO LOOP's loop (a note every beat, DELAY and REVERB), CUT pressed and let go at odd halves: let go in
     * its fade, just after it, held long, pressed again at once; then at odd blocks ---- */
    begin("cut");
    player_play_song(0x08, 1, 0);
    run(2.0);
    for (k = 0; k < (int)(sizeof CUTS / sizeof CUTS[0]); k++) {
        player_hush(1);
        halves(CUTS[k][0]);
        if (k == 2)
            check("CUT held: hushed, the song playing on", st().hushed && st().playing);
        player_hush(0);
        halves(CUTS[k][1]);
    }
    blocks(13);
    player_hush(1);
    blocks(23);
    player_hush(0);
    run(2.0);
    check("CUT let go: heard again", !st().hushed && st().playing);

    /* ---- the keys DRY: chords on each engine over SPICE ROAD, DRY switched while they ring; then over silence, a
     * FUSION instrument's layers and a Juno-106's chorus on lines of their own ---- */
    begin("keys_dry");
    player_play_song(0x01, 0, 0);
    run(1.0);
    player_keys_dry(1);
    keys(1, 55);
    halves(171);
    check("the keys DRY over a song: their voices sound", key_voices() >= 5);
    player_keys_dry(0);
    halves(37);
    player_keys_dry(1);
    keys(0, 55);
    halves(53);
    keys(1, 60);
    halves(91);
    keys(0, 60);
    player_keys_dry(0);
    halves(1);
    player_keys_dry(1);
    halves(77);
    player_stop();
    player_all_sound_off();
    run(1.0);
    player_key_on(57, (pl_inst_t){0x06, 0}, 127);   /* (the disk's FUSION's first: SPACE PAD) */
    player_key_on(64, (pl_inst_t){0xF8, 0x03}, 100);
    halves(201);
    check("the keys DRY over silence: FUSION's layers and a Juno-106 sound", key_voices() >= 3);
    player_key_off(57);
    player_key_off(64);
    run(3.0);
    player_keys_dry(0);

    /* ---- SYNC PULSE switched at odd halves over ACID TEST: on the left, on the right, off; then at odd blocks ---- */
    begin("sync_pulse");
    player_play_song(0x04, 0, 0);
    run(1.0);
    for (k = 0; k < (int)(sizeof PULSE / sizeof PULSE[0]); k++) {
        player_sync_pulse(PULSE[k][0]);
        halves(PULSE[k][1]);
    }
    blocks(7);
    player_sync_pulse(1);
    blocks(301);
    player_sync_pulse(0);
    run(1.0);

    /* ---- SETTINGS' SECOND CORE switched off and on at odd halves over MIDNIGHT RUN (each from the next half) ---- */
    begin("c2_switch");
    player_play_song(0x02, 0, 0);
    run(1.0);
    for (k = 0; k < 6; k++) {
        plat_c2_enable(0);
        halves(1 + 26 * k);
        plat_c2_enable(1);
        halves(37 + 14 * k);
    }

    return finish();
}

/* the session's end: every case's files closed, its loudness, the checks of the whole */
static int finish(void)
{
    int i, k, quiet = 0;
    double secs = 0;
    plat_c2_wait();                     /* (the last half: what core 2 still has of it) */
    put();
    for (i = 0; i < ncase; i++) {
        double ms = cs[i].sum / (2.0 * (double)(cs[i].frames ? cs[i].frames : 1)), db;
        db = 10 * log10(ms / (32768.0 * 32768.0) + 1e-20);
        printf("  %-12s %5.1f s, %6.1f dB RMS\n", cs[i].name, cs[i].frames / 44100.0, db);
        quiet += db < -60;
        secs += cs[i].frames / 44100.0;
        for (k = 0; k < 3; k++)
            fclose(cs[i].f[k]);
    }
    check("every case heard (above -60 dB RMS)", !quiet);
    check("every block given to USB audio's tap and to player_out once", n_usb == made && n_out == made);
    if (!v16) {                          /* (handed over: all but CUT's and SECOND CORE off's in modes 1-3 and 6, the
                                          * first 499 halves' in mode 4, none in mode 0) */
        printf("  %u of %llu blocks handed over to core 2\n", (unsigned)c2_posts, (unsigned long long)made);
        check("the blocks handed over to core 2 as its mode has it",
              mode == 0 ? !c2_posts : mode == 4 ? c2_posts && c2_posts <= 4u * 500u : c2_posts > made * 9u / 10u);
    } else {                             /* (16 voices: voice jobs in modes 1-3, 5, 6 and mode 4's first halves; none
                                          * in one core's reference) */
        printf("  %u voice jobs, %u of %llu jobs handed over to core 2\n", (unsigned)c2_voice_jobs, (unsigned)c2_posts,
               (unsigned long long)made);
        check("the voice jobs handed over as the mode has it", mode == 0 ? !c2_voice_jobs : c2_voice_jobs > 1000u);
        if (mode == 5)
            check("mode 5: the voice job lost, core 2 stopped, the limit 12", c2_lost == 1 && st().voice_count == 12);
        else
            check("the limit in force 16 at the end", st().voice_count == 16 || mode == 4);
    }
    printf("c2_test: %d cases, %.1f s, %s\n", ncase, secs, fails ? "WRONG" : "each heard");
    return fails != 0;
}
