/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The second core's hand-over (specs/second-core.md#when) on the PC: one session through player.h, made in whichever
 * mode the test platform's second core is in (C2_MODE: disk_store.c), each case's output in files of its own;
 * run_sound.sh runs the plain session (VOICES 12, the voices' split moving there too) in modes 0-4 and 6 and compares
 * modes 1-3 and 6 with one core's (built with PL_ONE_CORE_16: every voice rendered in place, the load guard counting as
 * with core 2), byte for byte, and so with the split forced (C2_SPLIT); mode 4 with mode 0 whose guard counts as mode
 * 4's (C2_GUARD) (modes 5 and 7 lose a voice job: they run in the 16 voices' session). The
 * cases, in this order: DJ HEAVY's pair on two decks (first: mode 4's fallback lands in it), DJ's stops on the shared
 * DELAY and REVERB with a DJ FILTER and its LFO, 20 s of every song of the disk from its start, PUNCH IN with the
 * punch-ins pressed and let go at odd blocks, a song with the METRONOME on through a COUNT-IN, CUT pressed and let go
 * at odd halves, the keys DRY with notes, SYNC PULSE, LOOPS with the stage over a muted loop and SETTINGS' SECOND CORE
 * switched at odd halves (the 16 voices' session: LOOPS with the stage too). A case's files: .lr every half as
 * player_audio leaves it, read once the next half is made (as the codec reads it: core 2 may still be on a half's last
 * blocks when player_audio returns), .usb USB audio's tap and .out player_out's blocks as they come, a hash a block
 * (each counted to the case that made it). Each case must be heard, every block must reach the taps once, and the
 * blocks go to core 2 as the mode has it (none in mode 0). Built with POOL_CHECK it includes the player, its pool's
 * links checked after every half (pool_check.h), and mode 7 (a voice job lost part way: disk_store.c) runs; without,
 * the player is an object of its own.
 *   c2_test DISK OUT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/plat.h"
#ifdef POOL_CHECK
#include "../../core/play/player.c"
#include "pool_check.h"
#endif
#include "../../core/play/player.h"
#include "../../core/play/dj.h"
#include "../../core/fx/fx.h"
#include "../../core/store/store.h"
#include "../../core/spare.h"

int disk_load(const char *path);
extern uint32_t c2_posts;                /* (disk_store.c: the blocks handed over to core 2) */
extern uint32_t c2_voice_jobs, c2_lost;  /* (the voice jobs handed over; mode 5's or 7's one lost) */
static int v16, mode;                    /* C2_V16: the 16 voices' session; C2_MODE */
static int finish(void);
#define LOST(m) ((m) == 4 || (m) == 5 || (m) == 7)   /* (modes whose core 2 stops: the limit 12 after) */
#ifdef POOL_CHECK
extern void (*c2_part)(void (*job)(uint32_t), uint32_t j);
static void c2_part_way(void (*job)(uint32_t), uint32_t j)   /* (mode 7: the voice job begun, its first half of voices
                                                             * rendered and mixed, then lost: not done) */
{
    uint8_t n = pl_v2.n;
    pl_v2.n = (uint8_t)(n / 2u);
    job(j);
    pl_v2.n = n;
    pl_v2.done = 0;
}
#endif

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
#ifdef POOL_CHECK
static uint32_t guard_n;                 /* (C2_GUARD: the load guard counting as with core 2 taking the voices' share for
                                          * the session's first guard_n - 1 halves, then as without: mode 4's, in mode 0) */
#endif
static void half(int frames)             /* a half made (frames: whole blocks, 256 at most) */
{
#ifdef POOL_CHECK
    if (guard_n)
        pl_guard_c2 = (int8_t)(nh + 1u < guard_n);
#endif
    player_audio(buf[nh & 1u], frames);
#ifdef POOL_CHECK
    pool_check(ncase ? cs[ncase - 1].name : "c2_test");
#endif
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
    static const pl_inst_t IN[5] = {{0xF0, 0x02}, {0xF8, 0x03}, {0xFA, 0x00}, {0xF0, 0x10}, {0xA2, 0x08}};
    int e;
    for (e = 0; e < 5; e++)
        if (on)
            player_key_on((uint8_t)(base + 3 * e), IN[e], 100);
        else
            player_key_off((uint8_t)(base + 3 * e));
}

/* ---- LOOPS with the stage (specs/ui/screens/loops.md#the-stage): three loops of four-note chords (DX7, JUNO-106,
 * DX7) over 16 rows, loop 00 muted and selected, the stage (a DX7 chord pattern) in its place on its track: the stage's
 * voices pass the mute by; then MONO, the lanes held, the keys on the stage, a jump, a solo, + NEW LOOP's track. The
 * loops' record and the stage put into the test's store by hand (disk_put, disk_stage), the source the test's own ---- */
void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);
void disk_stage(const uint8_t *p, uint32_t len);
static const uint8_t LOOP_IN[3][2] = {{0xF0, 0x02}, {0xF8, 0x03}, {0xF0, 0x10}};
static uint8_t lp_rec[512], lp_stage[256];
static rec_song_t lp_song0;
static int lp_src_song(rec_song_t *s)
{
    static const uint8_t ORDER0 = 0;
    memset(s, 0, sizeof *s);
    s->tempo = 150;
    s->ticks = 6;
    s->rpb = 4;
    s->swing = 50;
    s->gvol = 64;
    s->len = 1;
    s->order = &ORDER0;
    for (int t = 0; t < 16; t++) {
        s->mix[t][RM_LEVEL] = 0x7F;
        s->mix[t][RM_PAN] = (uint8_t)(0x40 + 0x20 * (t % 3));
        s->mix[t][RM_REVERB] = (uint8_t)(t == 0 ? 0x30 : 0x10);
        s->mix[t][RM_DUCK] = (uint8_t)(t == 1 ? 0x40 : 0);
    }
    s->fx = lp_song0.fx;                 /* (CITY LIGHTS' FX setup) */
    return 0;
}
static int lp_src_pattern(uint8_t num, rec_pat_t *pt)
{
    if (num)
        return -1;
    memset(pt, 0, sizeof *pt);
    pt->ntrk = 16;
    pt->rows = 16;
    pt->step = 1;
    return 0;
}
static int lp_src_inst(int track, pl_inst_t *in)
{
    if (track < 0 || track >= 3)
        return 0;
    in->cart = LOOP_IN[track][0];
    in->slot = LOOP_IN[track][1];
    return 1;
}
static const pl_source_t LP_SRC = {lp_src_song, lp_src_pattern, lp_src_inst, 1};
/* a stream: notes at (row, lane, note), in order, each with its volume, the stream's 0x00 after them */
static uint32_t lp_enc(uint8_t *o, const int (*c)[3], int n, int vol)
{
    uint32_t k = 0;
    int prev = -1;
    for (int i = 0; i < n; i++) {
        int s = 4 * c[i][0] + c[i][1], skip = s - prev - 1;
        o[k++] = (uint8_t)(0x80 | (c[i][2] ? 0x10 : 0) | (skip >= 7 ? 7 : skip));
        o[k++] = (uint8_t)(c[i][2] ? c[i][2] : REC_NOTE_OFF);
        if (c[i][2])
            o[k++] = (uint8_t)vol;
        if (skip >= 7)
            o[k++] = (uint8_t)skip;
        prev = s;
    }
    o[k++] = 0;
    return k;
}
static void loops_put(void)              /* the loops' record (version 4) and the stage */
{
    static const int CH[3][8][3] = {
        {{0, 0, 48}, {0, 1, 55}, {0, 2, 60}, {0, 3, 64}, {8, 0, 50}, {8, 1, 57}, {8, 2, 62}, {8, 3, 65}},
        {{0, 0, 36}, {0, 1, 43}, {0, 2, 48}, {0, 3, 52}, {8, 0, 38}, {8, 1, 45}, {8, 2, 50}, {8, 3, 53}},
        {{4, 0, 72}, {4, 1, 76}, {4, 2, 79}, {4, 3, 83}, {12, 0, 74}, {12, 1, 77}, {12, 2, 81}, {12, 3, 84}}};
    static const int ST[10][3] = {{0, 0, 67}, {0, 1, 71}, {0, 2, 74}, {2, 3, 79}, {6, 0, 0}, {8, 0, 69}, {8, 1, 72},
                                  {8, 2, 76}, {12, 1, 0}, {14, 3, 81}};
    uint32_t at, n;
    rec_song(0xA3, &lp_song0);
    memset(lp_rec, 0, sizeof lp_rec);
    lp_rec[0] = 150;                     /* TEMPO 150, 6 ticks, SWING 50, 1/16 */
    lp_rec[2] = 6;
    lp_rec[3] = 50;
    lp_rec[5] = 3;
    lp_rec[7] = 16;
    for (int i = 0; i < 3; i++) {
        uint8_t *e = lp_rec + REC_LOOPS_TABLE + REC_LOOPS_ENTRY * i;
        e[0] = LOOP_IN[i][0];
        e[1] = LOOP_IN[i][1];
        e[2] = (uint8_t)i;               /* (its colour; POLY, not muted: the mutes are the player's) */
        e[3] = 3;                        /* (4 lanes, SHIFT 0) */
        e[4] = 15;                       /* (its base 16 rows, once) */
        e[5] = 1;
        e[6] = 0x7F;
        e[7] = 0x80;
    }
    at = REC_LOOPS_TABLE + REC_LOOPS_ENTRY * 3;
    lp_rec[at] = 3;                      /* (the streams' payload: the loops, the rows - 1, 0, every loop's stream here) */
    lp_rec[at + 1] = 15;
    lp_rec[at + 3] = 7;
    n = at + 5;
    for (int i = 0; i < 3; i++)
        n += lp_enc(lp_rec + n, CH[i], 8, 0x60);
    disk_put(REC_LOOPS, 0, 0, lp_rec, n);
    disk_stage(lp_stage, lp_enc(lp_stage, ST, 10, 0x70));
    player_store_watch(STORE_STAGE, 0, 0);
}
static void lp_stage_set(int mono, int track, int silent)
{
    pl_lp_stage_t s = {{0xF0, 0x03}, (uint8_t)mono, (uint8_t)track, (uint8_t)silent, 0};
    player_lp_stage(&s);
}
static void loops_case(const char *name)
{
    begin(name);
    loops_put();
    lp_stage_set(0, 0, 0);               /* (loop 00 selected: silent, the stage in its place on its track) */
    player_lp_mutes(1, 0);               /* (loop 00 muted: the stage's voices pass it by) */
    player_play_source(PL_LOOPS, &LP_SRC, 0, 0);
    run(4.0);
    check("LOOPS with the stage: the loops playing", st().what == PL_LOOPS && st().playing);
    lp_stage_set(1, 0, 0);               /* MONO */
    halves(171);
    lp_stage_set(0, 0, 0);
    player_lp_hold(15, st().lp_pass, st().row);   /* (OVERWRITE from the line) */
    halves(97);
    player_lp_note(PL_LP_STAGE_KEYS, 0, 62, 0x7F, -1, 0, 0);
    player_lp_note(PL_LP_STAGE_KEYS, 1, 65, 0x7F, (st().row + 2) % 16, 0, 0);
    halves(53);
    player_lp_hold(15, 0, -1);
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    player_lp_off(PL_LP_STAGE_KEYS, 1, -1);
    halves(41);
    player_lp_jump(-5);
    halves(83);
    player_lp_mutes(1, 4);               /* (loop 02 soloed: the stage still heard) */
    halves(131);
    lp_stage_set(0, 3, 0xFF);            /* (+ NEW LOOP: the next loop's track, loop 00 heard again but muted) */
    halves(137);
    player_lp_mutes(0, 0);
    halves(61);
    player_stop();
    player_all_sound_off();
    disk_stage(0, 0);
    player_store_watch(STORE_STAGE, 0, 0);
    run(1.0);
}

/* ---- 16 voices (C2_V16; specs/second-core.md#the-voices-on-core-2): VOICES 16, the places from each block's split on
 * on core 2 through the voice job. Built with PL_ONE_CORE_16 and run in mode 0, one core renders all 16 in place: the
 * reference modes 1-3 and 6 must equal, but v16_switch (SECOND CORE off counts 16 as 12): the modes among themselves
 * there. Mode 4's fallback (at a half's start), mode 5's (a voice job never begun, lost) and mode 7's (one begun, lost)
 * only run on, the limit 12 after, their own checks ---- */
static pl_tap_t tq[64];                  /* (RECORD LIVE's tap while the 16 keys are held) */
static const pl_inst_t V16_IN[4] = {{0xF0, 0x02}, {0xF8, 0x03}, {0xF0, 0x10}, {0xA2, 0x08}};   /* DX7, JUNO-106, DX7,
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
#ifdef POOL_CHECK
static uint32_t lost_n;                  /* (mode 7: the notes of the job lost, each told to the tap as lost) */
/* (modes 5 and 7, 16 keys held, a half after the voice job was lost: was, each engine's voice before; the job's places
 * those its split handed over, pl_v2.place, from the split up, all 16 sounding. A job never begun left its engines'
 * state: its places under 13 sound on, as every place before the split, 13-16's notes ghosts in their engines' order. A
 * begun one: its notes ghosts made last in their engines' order, the ones made last back first, each into the first of
 * its places under 13 free (a return under way there or none yet in the rest); its engines' state given up (kind 255:
 * a note there starts it from zero), as places 13-16, unused at the limit 12, still show; the places before the split
 * untouched. The count 12 and no fade left over either way; the tap told each held note that lost its engine, PT_LOST
 * on the keys' lane) */
static void v16_lost(const uint8_t *was)
{
    uint32_t i, n = pl_v2.n, k = n ? pl_v2.place[0] : PL_ENGINES, back = 0;
    int ok = pl_nv == PL_VOICES_C1 && !pl_nfd && n && k + n == PL_ENGINES;
    lost_n = n;
    for (i = 0; i < (mode == 5 ? PL_VOICES_C1 : k); i++)
        ok &= pl_e[i].voice == was[i];
    if (mode == 5) {
        ok &= pl_gn == PL_ENGINES - PL_VOICES_C1;
        for (i = 0; ok && i < pl_gn; i++)
            ok &= pl_gl[i] == was[PL_VOICES_C1 + i] && pl_v[pl_gl[i]].held;
    } else {
        for (i = PL_VOICES_C1; i < PL_ENGINES; i++)
            ok &= pl_e[i].voice == PL_NO_VOICE && pl_e[i].kind == 255;
        while (k + back < PL_VOICES_C1 && pl_e[k + back].voice != PL_NO_VOICE)
            back++;
        for (i = 0; i < back; i++)
            ok &= pl_e[k + i].voice == was[PL_ENGINES - 1u - i];
        for (i = k + back; i < PL_VOICES_C1; i++)
            ok &= pl_e[i].voice == PL_NO_VOICE;
        ok &= pl_gn == n - back;
        for (i = 0; ok && i < pl_gn; i++)
            ok &= pl_gl[i] == was[k + i] && pl_v[pl_gl[i]].held;
    }
    printf("  the voice job lost (%s): places %u-16, %u ghosts, %u back, the count %u\n", mode == 7 ? "begun" : "never begun",
           (unsigned)k + 1u, (unsigned)pl_gn, (unsigned)back, (unsigned)pl_nv);
    check(mode == 7 ? "a job begun, lost: its notes ghosts made last, the last back first into its places, 13-16 zeroed"
                    : "a job never begun, lost: its places under 13 sound on, 13-16's notes ghosts in order", ok);
}
#endif
/* (the tap's events of the 16 keys held: each key's note told, and in modes 5 and 7 the notes losing their engines) */
static void v16_tapped(void)
{
    pl_tap_t e;
#ifdef POOL_CHECK
    int notes = 0, lost = 0, after = 0, want = mode == 5 ? 4 : mode == 7 ? (int)lost_n : 0;
#else
    int notes = 0, lost = 0, after = 0, want = mode == 5 ? 4 : 0;
#endif
    while (player_tap_peek(&e)) {
        player_tap_next();
        if (e.lane == PT_KEY && e.note == PT_LOST) {
            lost++;
        } else if (e.lane == PT_KEY && e.note >= 12u) {
            notes++;
            after += lost > 0;
        }
    }
    player_tap(0, 0);
    printf("  the tap, 16 keys held: %d notes told, %d lost, %d notes told after a loss\n", notes, lost, after);
    check("RECORD LIVE's tap: the keys' notes, each lost one told, none coming back", notes == 16 && lost == want &&
          !after && !player_tap_lost());
}
static void v16_session(void)
{
    static player_dj_t mix;
    uint8_t nums[256];
    int i, n, k;
    player_voice_count(16);

    begin("v16_keys");                   /* 16 keys held, started at odd halves, let go one at a time; the tap on while
                                          * they are held (modes 5 and 7: the voice job lost then) */
    halves(1);
    player_tap(tq, 64);
    v16_keys(1, 0, 15, 3);
    for (k = 0; k < 397; k++) {
        uint32_t lost = c2_lost;
#ifdef POOL_CHECK
        uint8_t was[PL_ENGINES];
        for (i = 0; i < (int)PL_ENGINES; i++)
            was[i] = pl_e[i].voice;
#endif
        halves(1);
        if (c2_lost != lost) {           /* (a half on: what came back in it too) */
            halves(1);
            k++;
#ifdef POOL_CHECK
            v16_lost(was);
#endif
        }
    }
    check("16 keys held: 16 voices sound", key_voices() == 16 || LOST(mode));
    v16_tapped();
    v16_keys(0, 0, 15, 5);
    run(2.0);

    begin("v16_ghost");                  /* 20 keys: the newest take the oldest's engines (ghosts); 6 let go, the ghosts
                                          * back in (into their tails' engines, each tail fading out) */
    v16_keys(1, 0, 19, 1);
    halves(201);
    check("20 keys held: 16 sound, 4 ghosts", (key_voices() == 16 && st().ghosts == 4) || LOST(mode));
    v16_keys(0, 4, 9, 7);
    for (k = 0; k < 100 && st().ghosts; k++)
        halves(17);
    printf("  6 let go: the ghosts back after %d x 17 halves, %d ghosts, %d keys' voices\n", k, st().ghosts,
           key_voices());
    check("6 let go: the 4 ghosts back into their tails' engines", !st().ghosts || LOST(mode));
    v16_keys(0, 0, 3, 1);
    v16_keys(0, 10, 19, 1);
    run(2.0);

    begin("v16_over");                   /* SPICE ROAD with 8 keys held over it */
    player_play_song(0xA1, 0, 0);
    run(1.0);
    v16_keys(1, 0, 7, 11);
    run(6.0);
    v16_keys(0, 0, 7, 1);
    player_stop();
    player_all_sound_off();
    run(1.0);

    begin("v16_mutes");                  /* 10 keys held, SPICE ROAD's notes in the places past them (the top places,
                                          * core 2's from the split on), its tracks muted and heard again at odd halves:
                                          * core 2's places whose mute fades, rendered and mixed by core 1 that block */
    v16_keys(1, 0, 9, 1);
    player_play_song(0xA1, 0, 0);
    run(2.0);
    for (k = 0, n = 0; k < 24; k++) {
        pl_voice_info_t v[PL_VOICES];
        int nv = player_voices_now(v, PL_VOICES), c;
        for (c = 10; c < nv; c++)
            n += v[c].on && v[c].track != 0xFF;
        player_mutes(k & 1 ? 0u : 0x5555u << (k & 2 ? 1 : 0));
        halves(3 + 2 * (k % 5));
    }
    printf("  song voices in the top 6 places at the mutes: %d\n", n);
    check("mutes at 16: song voices in the top places as their tracks fade", n > 0);
    player_mutes(0);
    v16_keys(0, 0, 9, 1);
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
    player_deck_load(0, 0xA0);
    player_deck_load(1, 0xA2);
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
    player_play_song(0xA8, 1, 0);
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

    loops_case("v16_loops");             /* (LOOPS with the stage: its voices in core 2's places too) */

    begin("v16_count");                  /* 16 keys held, VOICES 8, 16, 12, 16: at 8 the notes of engines 9-16 fade out
                                          * there in one block and go on as ghosts in their engines' order; at 16 they
                                          * come back as the block's budget lets them, 4 a block at most, the one made
                                          * last first, into the first free engines (a return under way holding its
                                          * engine into the next block), all back within 6 blocks */
    v16_keys(1, 0, 15, 1);
    halves(53);
    {
        int fell, rose = 1, s[7], g[7];
#ifdef POOL_CHECK
        uint8_t was[PL_ENGINES];
        uint32_t b0;
        for (i = 0; i < (int)PL_ENGINES; i++)
            was[i] = pl_e[i].voice;
        b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
#endif
        player_voice_count(8);
        blocks(1);
        s[0] = key_voices();
        g[0] = st().ghosts;
        fell = s[0] == 8 && g[0] == 8;
#ifdef POOL_CHECK
        fell &= pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3] - b0 == 16u && pl_gn == 8;
        for (i = 0; fell && i < 8; i++)
            fell &= pl_gl[i] == was[8 + i];
#endif
        halves(37);
        player_voice_count(16);
        int n = s[0], all = -1;
        for (i = 1; i < 7; i++) s[i] = s[0], g[i] = g[0];
        for (i = 1; i <= 60 && all < 0; i++) {   /* (the FM-1's budget: about a return begun a block) */
            blocks(1);
            int sv = key_voices(), gv = st().ghosts;
            if (i < 7) { s[i] = sv; g[i] = gv; }
            rose &= sv + gv == 16 && sv >= n && sv - n <= 4;
            n = sv;
            if (n == 16) all = i;
        }
        rose &= all > 0;
#ifdef POOL_CHECK
        for (i = 0; rose && i < 8; i++)
            rose &= pl_e[8 + i].voice == was[15 - i];
#endif
        printf("  16 keys, VOICES 8: %d sound, %d ghosts; 16 again: %d %d %d %d %d %d sound a block on and the next, "
               "all 16 in %d blocks\n", s[0], g[0], s[1], s[2], s[3], s[4], s[5], s[6], all);
        check("VOICES 8: 8 ghosts, their fades in their engines in one block, in order", fell || LOST(mode));
        check("VOICES 16 again: back 4 a block at most, the one made last first, all within 60 blocks", rose || LOST(mode));
    }
    halves(41);
    player_voice_count(12);
    halves(29);
    check("VOICES 12: 12 sound, 4 ghosts", (key_voices() == 12 && st().ghosts == 4) || LOST(mode));
    player_voice_count(16);
    halves(31);
    check("VOICES 16: 16 sound", key_voices() == 16 || LOST(mode));
    v16_keys(0, 0, 15, 1);
    run(2.0);

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
#ifdef POOL_CHECK
    c2_part = c2_part_way;
#endif
    player_init();
    player_usb = usb_tap;
    player_out = out_block;
    dj_record_default(DJ_REC);
    player_dj_mem(spare_borrow(player_dj_mem_size()));   /* (lent as dj.c has it lent: the spare zone's top) */
    mode = getenv("C2_MODE") ? atoi(getenv("C2_MODE")) : 0;
#ifdef POOL_CHECK
    if (getenv("C2_SPLIT"))              /* (the block's split forced, player.c's SPLIT_*: 1 fixed as before it moved, 2 every
                                          * voice core 1's, 3 the top 10 core 2's, 4 a place at random each block) */
        pl_split_how = (uint8_t)atoi(getenv("C2_SPLIT"));
    if (getenv("C2_GUARD"))
        guard_n = (uint32_t)atoi(getenv("C2_GUARD"));
#endif
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
    player_deck_load(0, 0xA0);           /* PUNCH IN */
    player_deck_load(1, 0xA2);           /* MIDNIGHT RUN */
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
    player_play_song(0xA0, 0, 0);
    blocks(301);
    for (k = 0; k < 16; k++) {
        player_punch(k, 1);
        blocks(333 + 26 * k);
        player_punch(k, 0);
        blocks(211 + 14 * k);
    }

    /* ---- CITY LIGHTS with the METRONOME on; paused, a COUNT-IN of 2 bars, its pattern looping after it ---- */
    begin("metronome");
    player_play_song(0xA3, 0, 0);
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
    player_play_song(0xA8, 1, 0);
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
    player_play_song(0xA1, 0, 0);
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
    player_key_on(57, (pl_inst_t){0xA5, 0}, 127);   /* (the disk's FUSION's first: SPACE PAD) */
    player_key_on(64, (pl_inst_t){0xF8, 0x03}, 100);
    halves(201);
    check("the keys DRY over silence: FUSION's layers and a Juno-106 sound", key_voices() >= 3);
    player_key_off(57);
    player_key_off(64);
    run(3.0);
    player_keys_dry(0);

    /* ---- SYNC PULSE switched at odd halves over ACID TEST: on the left, on the right, off; then at odd blocks ---- */
    begin("sync_pulse");
    player_play_song(0xA4, 0, 0);
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

    loops_case("loops_stage");           /* ---- LOOPS with the stage (above) ---- */

    /* ---- SETTINGS' SECOND CORE switched off and on at odd halves over MIDNIGHT RUN (each from the next half) ---- */
    begin("c2_switch");
    player_play_song(0xA2, 0, 0);
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
#ifdef POOL_CHECK
    int none = pl_split_how == SPLIT_CORE1 || (!v16 && pl_split_how == SPLIT_FIXED);   /* (no voice job: forced so) */
#else
    int none = 0;
#endif
    if (!v16) {                          /* (handed over: all but CUT's and SECOND CORE off's in modes 1-3 and 6, the
                                          * first 499 halves' in mode 4, none in mode 0; the voice jobs at VOICES 12 too,
                                          * the split moving there, mode 4's in its first halves) */
        printf("  %u voice jobs, %u of %llu blocks handed over to core 2\n", (unsigned)c2_voice_jobs,
               (unsigned)(c2_posts - c2_voice_jobs), (unsigned long long)made);
        check("the blocks handed over to core 2 as its mode has it",
              mode == 0 ? !c2_posts : mode == 4 ? c2_posts && c2_posts - c2_voice_jobs <= 4u * 500u :
              c2_posts - c2_voice_jobs > made * 9u / 10u);
        check("the voice jobs at VOICES 12 as the mode has it (the split moves there too)",
              mode == 0 || none ? !c2_voice_jobs : mode == 4 ? c2_voice_jobs && c2_voice_jobs <= 4u * 500u :
              c2_voice_jobs > 1000u);
    } else {                             /* (16 voices: voice jobs in modes 1-3, 5, 6 and mode 4's first halves; none
                                          * in one core's reference) */
        printf("  %u voice jobs, %u of %llu jobs handed over to core 2\n", (unsigned)c2_voice_jobs, (unsigned)c2_posts,
               (unsigned long long)made);
        check("the voice jobs handed over as the mode has it", mode == 0 || none ? !c2_voice_jobs :
              c2_voice_jobs > 1000u);
        if (mode == 5 || mode == 7)
            check("modes 5 and 7: the voice job lost, core 2 stopped, the limit 12", c2_lost == 1 &&
                  st().voice_count == 12);
        else
            check("the limit in force 16 at the end", st().voice_count == 16 || mode == 4);
    }
    printf("c2_test: %d cases, %.1f s, %s\n", ncase, secs, fails ? "WRONG" : "each heard");
    return fails != 0;
}
