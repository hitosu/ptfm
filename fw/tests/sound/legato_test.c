/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LEG (specs/files/commands.md#legato): C-4 held, then E-4 with LEG four rows on. On DX7, JUNO-106, CHIP and a sampler
 * the held note moves to E-4 with no new attack: no note started, the same voice going on (its envelope, a sample's
 * place), the track's pitch E-4 (a JUNO-106's filter following the new key). Where LEG may not play legato it changes
 * nothing, sample for sample against the same song without it: the note let go before (===), another instrument,
 * FUSION, a TB-303 (its slide is SLD's), and a start from the LEG's row (the first note heard has its attack).
 *   legato_test DISK */
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int triggers;
#define PL_TEST_TRIGGER(t, note, in) (void)(t), (void)(note), (void)(in), triggers++
#include "../../core/play/player.c"

int disk_load(const char *path);
void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);

enum { ROWS = 16, HALVES = 200 };        /* (a row 120 ms: 20.7 halves; 1.16 s) */
typedef struct { int row, note, cart, slot, vol, ncmd; uint8_t cmd[2][2]; } cell_t;

static uint8_t pats[3][256], songs[3][100];   /* (disk_put keeps the bytes in place: one each) */
static int16_t audio[HALVES][512];

/* pattern 0x40 + k, one track of the cells given (in row order), and song 0x40 + k playing it once */
static void make(int k, const cell_t *c, int n)
{
    int at = 5, row = 0, i;
    uint8_t *pat = pats[k], *song = songs[k];
    memset(pat, 0, 256);
    pat[0] = 1;
    pat[1] = ROWS - 1;
    pat[3] = 1;
    for (i = 0; i < n; i++) {
        int skip = c[i].row - row;
        pat[at++] = (uint8_t)((c[i].note ? 0x80 : 0) | (c[i].cart >= 0 ? 0x60 : 0) | (c[i].vol >= 0 ? 0x10 : 0) |
                              (c[i].ncmd ? 0x08 : 0) | (skip < 7 ? skip : 7));
        if (skip >= 7)                   /* (7: the skip in a byte of its own) */
            pat[at++] = (uint8_t)skip;
        if (c[i].note)
            pat[at++] = (uint8_t)c[i].note;
        if (c[i].cart >= 0) {
            pat[at++] = (uint8_t)c[i].cart;
            pat[at++] = (uint8_t)c[i].slot;
        }
        if (c[i].vol >= 0)
            pat[at++] = (uint8_t)c[i].vol;
        if (c[i].ncmd == 1) {
            pat[at++] = c[i].cmd[0][0];
            pat[at++] = c[i].cmd[0][1];
        } else if (c[i].ncmd > 1) {
            int j;
            pat[at++] = 0xFF;
            pat[at++] = (uint8_t)c[i].ncmd;
            for (j = 0; j < c[i].ncmd; j++) {
                pat[at++] = c[i].cmd[j][0];
                pat[at++] = c[i].cmd[j][1];
            }
        }
        row = c[i].row + 1;
    }
    pat[at++] = 0;
    disk_put(REC_PATTERN, (uint8_t)(0x40 + k), 0, pat, (uint32_t)at);
    memset(song, 0, 100);
    song[12] = 125;
    song[14] = 6;
    song[15] = 4;
    song[16] = 50;
    song[17] = 2 | 1;                    /* linear slides, STOP at the end */
    song[19] = 64;
    song[21] = (uint8_t)(0x40 + k);
    song[24] = 0xF7;
    memset(song + 25, 0xFF, 5);
    song[30] = 0x80;
    disk_put(REC_SONG, (uint8_t)(0x40 + k), 0, song, 23 + 64 + 8);
}

/* the track's latest note's voice (-1 none) */
static int voice_of_track(void)
{
    int i;
    for (i = 0; i < (int)PL_NV; i++)
        if (pl_v[i].use == PV_SONG && pl_v[i].who == 0 && pl_v[i].gen == pl_ch[0].gen)
            return i;
    return -1;
}

static int fails;
static void check(const char *what, int ok)
{
    if (!ok) {
        printf("legato: FAILED: %s\n", what);
        fails++;
    }
}

/* song k from row `row`: rendered into audio[] */
static void play(int k, int row)
{
    int h;
    player_init();
    player_play_song((uint8_t)(0x40 + k), 0, (uint8_t)row);
    for (h = 0; h < HALVES; h++)
        player_audio(audio[h], 256);
}

/* LEG onto a held note of instrument (cart, slot) */
static void legato(const char *name, int cart, int slot, int engine)
{
    const cell_t c[3] = {{0, 60, cart, slot, 0x7F, 0, {{0}}}, {4, 64, -1, 0, -1, 1, {{0xD3, 0}}},
                         {12, REC_NOTE_OFF, -1, 0, -1, 0, {{0}}}};
    char what[96];
    int h, v0 = -1, age0 = 0, gen0 = 0, trig0 = 0;
    int32_t pos0 = 0;
    make(0, c, 3);
    player_init();
    triggers = 0;
    player_play_song(0x40, 0, 0);
    for (h = 0; h < HALVES; h++) {
        player_audio(audio[h], 256);
        if (h == 80) {                   /* (row 3, the note held) */
            v0 = voice_of_track();
            if (v0 < 0)
                break;
            age0 = pl_v[v0].age;
            gen0 = pl_ch[0].gen;
            trig0 = triggers;
            pos0 = pl_v[v0].u.sm.pos;
        }
        if (h == 90)                     /* (row 4 started: E-4 legato) */
            break;
    }
    snprintf(what, sizeof what, "%s: C-4 sounding before the LEG row", name);
    check(what, v0 >= 0 && pl_v[v0].engine == (uint8_t)engine && trig0 == 1);
    if (v0 < 0)
        return;
    snprintf(what, sizeof what, "%s: no note started at the LEG row (triggers %d, gen %d -> %d)", name, triggers, gen0,
             pl_ch[0].gen);
    check(what, triggers == trig0 && pl_ch[0].gen == gen0);
    snprintf(what, sizeof what, "%s: the same voice goes on, held (age %d -> %d)", name, age0, pl_v[v0].age);
    check(what, voice_of_track() == v0 && (int)pl_v[v0].age == age0 && pl_v[v0].held && pl_v[v0].use == PV_SONG);
    snprintf(what, sizeof what, "%s: the track's note E-4, its bend 4 semitones (note %d, bend %d)", name,
             pl_ch[0].note, (int)pl_ch[0].bend);
    check(what, pl_ch[0].note == 64 && pl_ch[0].bend == 4 * 64);
    if (engine == EN_DX7) {
        snprintf(what, sizeof what, "%s: the voice bent to E-4", name);
        check(what, pl_v[v0].u.dx.bend == 256 * 21845 + 256 / 3);
    }
    if (engine == EN_JUNO) {
        snprintf(what, sizeof what, "%s: the VCF's key follow on E-4 (%d)", name, pl_v[v0].u.ju.pitch88 >> 8);
        check(what, pl_v[v0].u.ju.pitch88 == 64 << 8 && pl_v[v0].u.ju.bend == 256 * 21845 + 256 / 3);
    }
    if (engine == EN_SAMP) {
        snprintf(what, sizeof what, "%s: the sample plays on from its place (%d -> %d)", name, (int)pos0,
                 (int)pl_v[v0].u.sm.pos);
        check(what, pl_v[v0].u.sm.pos > pos0 && pl_v[v0].u.sm.on);
    }
}

/* song a and song b (b: a's cells with LEG added) render the same from row `row` (differ: they do not, the check's
 * own control), each from the same state (a forked child renders a into shared memory; the parent b, then waits for it
 * and compares) */
static void same(const char *what, const cell_t *a, int na, const cell_t *b, int nb, int row, int differ)
{
    int16_t (*ref)[512] = mmap(0, sizeof audio, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    char w[120];
    pid_t child;
    make(1, a, na);
    make(2, b, nb);
    fflush(stdout);
    child = fork();
    if (!child) {
        play(1, row);
        memcpy(ref, audio, sizeof audio);
        _exit(0);
    }
    play(2, row);
    waitpid(child, 0, 0);
    snprintf(w, sizeof w, differ ? "%s: LEG did not change the sound" : "%s: LEG changed the sound", what);
    check(w, !memcmp(ref, audio, sizeof audio) == !differ);
    munmap(ref, sizeof audio);
}

int main(int argc, char **argv)
{
    if (argc < 2 || disk_load(argv[1])) {
        printf("legato_test DISK\n");
        return 2;
    }
    legato("DX7", 0xF0, 0, EN_DX7);
    legato("JUNO-106", 0xF8, 0, EN_JUNO);
    legato("CHIP", 0xF9, 0, EN_CHIP);
    legato("sampler", 0x03, 0, EN_SAMP);
    {   /* LEG where it plays a plain note: the same as without it */
        const cell_t off0[3] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {2, REC_NOTE_OFF, -1, 0, -1, 0, {{0}}},
                                {4, 64, -1, 0, -1, 0, {{0}}}};
        const cell_t off1[3] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {2, REC_NOTE_OFF, -1, 0, -1, 0, {{0}}},
                                {4, 64, -1, 0, -1, 1, {{0xD3, 0}}}};
        const cell_t oth0[2] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {4, 64, 0xF0, 1, -1, 0, {{0}}}};
        const cell_t oth1[2] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {4, 64, 0xF0, 1, -1, 1, {{0xD3, 0}}}};
        const cell_t fus0[2] = {{0, 60, 0x06, 0, 0x7F, 0, {{0}}}, {4, 64, -1, 0, -1, 0, {{0}}}};
        const cell_t fus1[2] = {{0, 60, 0x06, 0, 0x7F, 0, {{0}}}, {4, 64, -1, 0, -1, 1, {{0xD3, 0}}}};
        const cell_t tbs0[2] = {{0, 48, 0xFA, 0, 0x7F, 1, {{0xC1, 0}}}, {4, 52, -1, 0, -1, 0, {{0}}}};
        const cell_t tbs1[2] = {{0, 48, 0xFA, 0, 0x7F, 1, {{0xC1, 0}}}, {4, 52, -1, 0, -1, 1, {{0xD3, 0}}}};
        const cell_t tb0[2] = {{0, 48, 0xFA, 0, 0x7F, 0, {{0}}}, {4, 52, -1, 0, -1, 0, {{0}}}};
        const cell_t tb1[2] = {{0, 48, 0xFA, 0, 0x7F, 0, {{0}}}, {4, 52, -1, 0, -1, 1, {{0xD3, 0}}}};
        const cell_t dx0[2] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {4, 64, -1, 0, -1, 0, {{0}}}};
        const cell_t dx1[2] = {{0, 60, 0xF0, 0, 0x7F, 0, {{0}}}, {4, 64, -1, 0, -1, 1, {{0xD3, 0}}}};
        same("a held DX7 note (the control)", dx0, 2, dx1, 2, 0, 1);
        same("the note let go (===) before", off0, 3, off1, 3, 0, 0);
        same("another instrument", oth0, 2, oth1, 2, 0, 0);
        same("FUSION", fus0, 2, fus1, 2, 0, 0);
        same("a TB-303 after SLD", tbs0, 2, tbs1, 2, 0, 0);
        same("a TB-303 without SLD", tb0, 2, tb1, 2, 0, 0);
        same("a start from the LEG's row", dx0, 2, dx1, 2, 4, 0);
    }
    printf("legato: %s\n", fails ? "FAILED" : "LEG plays legato on DX7, JUNO-106, CHIP and a sampler; elsewhere a "
                                              "plain note, as without it");
    return fails != 0;
}
