/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The host tests' store: the records of a disk file (specs/files/disk.md) in memory, and the firmware's
 * built-in instruments (builtin.c) for cartridges and patterns F0-FF, through store.h's readers; and the rest of
 * the platform the sound needs (plat.h): the clock, the spare zone's room, the second core. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/plat.h"
#include "../../core/store/store.h"
#include "../../core/store/builtin.h"

typedef struct {
    uint8_t kind, num, sub, ver;
    uint32_t len;
    const uint8_t *p;
} rec_t;
static rec_t *recs;
static int nrecs;
static uint8_t *disk;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int disk_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint32_t files, i, at;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    disk = malloc((size_t)n);
    if (!disk || fread(disk, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (n < 16 || memcmp(disk, "PTFMDISK", 8) || disk[8] != 1)
        return -1;
    files = le32(disk + 12);
    recs = calloc(files + 1u, sizeof *recs);
    for (i = 0, at = 16; i < files && at + 12u <= (uint32_t)n; i++) {
        rec_t *r = &recs[nrecs++];
        r->kind = disk[at];
        r->num = disk[at + 1];
        r->sub = disk[at + 2];
        r->ver = disk[at + 3];
        r->len = le32(disk + at + 4);
        r->p = disk + at + 12;
        at += 12u + r->len;
        at = (at + 3u) & ~3u;
    }
    return 0;
}

/* a record made by the test itself (the reference instruments), taking the place of the disk's */
void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len)
{
    int i;
    for (i = 0; i < nrecs; i++)
        if (recs[i].kind == kind && recs[i].num == num && recs[i].sub == sub)
            break;
    if (i == nrecs) {
        recs = realloc(recs, (size_t)(nrecs + 1) * sizeof *recs);
        nrecs++;
    }
    recs[i].kind = kind;
    recs[i].num = num;
    recs[i].sub = sub;
    recs[i].ver = 1;
    recs[i].len = len;
    recs[i].p = p;
}

static int builtin_kind(uint8_t kind) { return (kind >= REC_DX7 && kind <= REC_CART_LAST) || kind == REC_PATTERN; }

const uint8_t *store_get(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver)
{
    int i;
    if (num >= 0xF0u && builtin_kind(kind)) {
        for (i = 0; i < builtin_count; i++)
            if (builtin_recs[i].kind == kind && builtin_recs[i].num == num && builtin_recs[i].sub == sub) {
                *len = builtin_recs[i].len;
                *ver = builtin_recs[i].ver;
                return builtin_recs[i].data;
            }
        return 0;
    }
    for (i = 0; i < nrecs; i++)
        if (recs[i].kind == kind && recs[i].num == num && recs[i].sub == sub) {
            *len = recs[i].len;
            *ver = recs[i].ver;
            return recs[i].p;
        }
    return 0;
}

int store_list(uint8_t kind, uint8_t *nums, int max)
{
    int n = 0, v;
    for (v = 0; v < 256 && n < max; v++) {
        uint32_t len;
        uint8_t ver;
        int s;
        for (s = 0; s < (kind == REC_PATTERN ? 16 : 1); s++)
            if (store_get(kind, (uint8_t)v, (uint8_t)s, &len, &ver)) {
                nums[n++] = (uint8_t)v;
                break;
            }
    }
    return n;
}

void store_init(void) {}
void store_poll(void) {}

uint32_t store_gen(void) { return 0; }  /* (nothing here changes under a reader: the disk is read once) */
/* the spare zone (spare.c, which DJ's second deck borrows from): its own array alone here */
uint32_t plat_spare_room(void) { return 0; }

/* the edit memory: nothing edited here (every record read as the disk has it) */
void store_set_watch(store_watch_fn fn) { (void)fn; }
int store_edit_obj(uint8_t kind, uint8_t num) { (void)kind; (void)num; return 0; }
const uint8_t *store_piece(uint8_t kind, uint8_t num, uint16_t id, uint32_t *len)
{
    (void)kind; (void)num; (void)id; (void)len;
    return 0;
}

/* the platform's clock for the costs' figures (plat.h): the PC's */
#include <time.h>
uint32_t plat_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u);
}
uint32_t plat_ms(void) { return plat_us() / 1000u; }   /* (SYNC EXTERNAL's clock: no clock comes here) */

/* the second core (plat.h, specs/second-core.md), as the test's mode C2_MODE has it (read once; 0 without it):
 *   0  core 1 runs every block itself (plat_c2_half 0);
 *   1  core 2 runs each block's job at once, as it is handed over;
 *   2  a block late: a job runs right after the next one is handed over (block j's after block j + 1 was made up
 *      to its effects), a half's last at the next half's start;
 *   3  a half late: a half's jobs all at the next half's start;
 *   4  as 1, then from the 500th half on core 2 stopped (a fallback: plat_c2_half 0 for good);
 *   5  as 1, then from the 300th half on the first voice job handed over (its argument PLAT_C2_VOICE_JOB) never runs
 *      and core 2 stops there (a fallback in the middle of a half: what is handed over after runs at once, as the
 *      FM-1's plat_c2_post does, and plat_c2_half is 0 for good);
 *   6  a second thread: every job runs there, in order, as core 2 runs them, while this one goes on (plat_c2_wait
 *      and plat_c2_half wait for it to finish what it has).
 * The jobs keep their order in every mode: what changes is what core 1 has done meanwhile, so a datum both cores
 * touch in one half makes the sound differ from mode 0's. plat_c2_wait runs what still waits; a job handed over in a
 * half core 2 did not take, or more than a half's blocks waiting, stops the test. */
#define C2_QUEUE (PLAT_HALF / 64u)      /* (a half's blocks) */
static int c2_mode = -1;
static uint8_t c2_take;                 /* this half's answer */
static uint8_t c2_on = 1, c2_forced;    /* SETTINGS' SECOND CORE, the debug build's 38 */
static uint8_t c2_dropped;              /* mode 5: the voice job dropped, core 2 stopped */
static uint32_t c2_halves;
static struct { void (*job)(uint32_t); uint32_t j; } c2_q[C2_QUEUE];
static uint32_t c2_n;
uint32_t c2_posts;                      /* blocks handed over, counted (c2_test: the player does hand them over) */
uint32_t c2_voice_jobs, c2_lost;        /* voice jobs handed over; mode 5's one dropped */

/* mode 6's second thread: a ring of jobs, run in order there (the mutex orders memory between the two, as the chip's
 * sync does between the cores) */
static pthread_t c2_th;
static pthread_mutex_t c2_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c2_cv = PTHREAD_COND_INITIALIZER, c2_cv_done = PTHREAD_COND_INITIALIZER;
static uint32_t c2_head, c2_tail, c2_started;
static void *c2_thread(void *arg)
{
    (void)arg;
    for (;;) {
        void (*job)(uint32_t);
        uint32_t j;
        pthread_mutex_lock(&c2_mx);
        while (c2_tail == c2_head)
            pthread_cond_wait(&c2_cv, &c2_mx);
        job = c2_q[c2_tail % C2_QUEUE].job;
        j = c2_q[c2_tail % C2_QUEUE].j;
        pthread_mutex_unlock(&c2_mx);
        job(j);
        pthread_mutex_lock(&c2_mx);
        c2_tail++;
        pthread_cond_signal(&c2_cv_done);
        pthread_mutex_unlock(&c2_mx);
    }
    return 0;
}

static void c2_run(void)                /* every job waiting, in order */
{
    uint32_t i, n = c2_n;
    if (c2_mode == 6) {                 /* (the thread finishes what it has) */
        pthread_mutex_lock(&c2_mx);
        while (c2_tail != c2_head)
            pthread_cond_wait(&c2_cv_done, &c2_mx);
        pthread_mutex_unlock(&c2_mx);
        return;
    }
    c2_n = 0;
    for (i = 0; i < n; i++)
        c2_q[i].job(c2_q[i].j);
}

int plat_c2_half(void)
{
    if (c2_mode < 0) {
        const char *m = getenv("C2_MODE");
        c2_mode = m ? atoi(m) : 0;
        if (c2_mode < 0 || c2_mode > 6) {
            fprintf(stderr, "C2_MODE %s: 0-6\n", m);
            exit(2);
        }
        if (c2_mode == 6 && pthread_create(&c2_th, 0, c2_thread, 0)) {
            fprintf(stderr, "C2_MODE 6: no thread\n");
            exit(2);
        }
    }
    c2_run();                           /* (core 2 finished the half before: first thing) */
    if ((c2_mode == 4 || c2_mode == 5) && c2_halves < 500u)
        c2_halves++;
    c2_take = (uint8_t)(c2_mode && c2_on && !c2_forced && !(c2_mode == 4 && c2_halves >= 500u) && !c2_dropped);
    return c2_take;
}

void plat_c2_post(void (*job)(uint32_t), uint32_t j)
{
    if (j == PLAT_C2_VOICE_JOB)
        c2_voice_jobs++;
    if (c2_dropped) {                   /* (mode 5: core 2 stopped in this half: at once, as the FM-1's) */
        job(j);
        return;
    }
    if (c2_mode == 5 && c2_halves >= 300u && j == PLAT_C2_VOICE_JOB) {
        c2_dropped = 1;                 /* (never run: the player finds it not done) */
        c2_lost++;
        return;
    }
    if (c2_mode == 6) {
        pthread_mutex_lock(&c2_mx);
        if (!c2_take || c2_head - c2_tail == C2_QUEUE) {
            fprintf(stderr, "plat_c2_post: job %u handed over %s\n", (unsigned)j,
                    c2_take ? "with a half's blocks waiting" : "in a half core 2 does not take");
            abort();
        }
        c2_q[c2_head % C2_QUEUE].job = job;
        c2_q[c2_head % C2_QUEUE].j = j;
        c2_head++;
        c2_started++;
        c2_posts++;
        pthread_cond_signal(&c2_cv);
        pthread_mutex_unlock(&c2_mx);
        return;
    }
    if (!c2_take || c2_n == C2_QUEUE) {
        fprintf(stderr, "plat_c2_post: block %u handed over %s\n", (unsigned)j,
                c2_take ? "with a half's blocks waiting" : "in a half core 2 does not take");
        abort();
    }
    c2_q[c2_n].job = job;
    c2_q[c2_n].j = j;
    c2_n++;
    c2_posts++;
    if (c2_mode == 1 || c2_mode == 4 || c2_mode == 5)
        c2_run();
    else if (c2_mode == 2 && c2_n == 2u) {   /* (the block before's, now that this one is handed over) */
        c2_q[0].job(c2_q[0].j);
        c2_q[0] = c2_q[1];
        c2_n = 1;
    }
}

void plat_c2_enable(int on) { c2_on = (uint8_t)(on != 0); }
void plat_c2_force_off(int off) { c2_forced = (uint8_t)(off != 0); }
void plat_c2_wait(void) { c2_run(); }
