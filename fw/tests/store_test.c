/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The store's host tests (specs/files/store.md) on a fake flash: mounting, saving, the newest version,
 * deletes, the built-in numbers, power cuts at every flash operation of a save (the state before it or
 * after it, never a mix), records that fail their CRC, compaction under churn, a full store, other
 * firmwares' leftovers, audio blocks (samples and the old form's audio side by side); samplers of instruments only, the
 * old form never touched, CHECK STORAGE's entry check, the LAYOUT; the edit memory as pieces (a pattern's tracks, a
 * cartridge's slots, LOOPS'), REVERT of one thing, the save of a pattern too big for one part, room joined, hold, an
 * edit mode's copies, pieces past 64 KB (run_store.sh builds them with an edit memory of 96 KB too), LOOPS in parts,
 * its older version kept; LOOPS' stage, a piece no SAVE or REVERT takes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "store_plat.h"
#include "../core/plat.h"
#include "../core/store/store.h"
#include "../core/store/crc.h"
#include "../core/store/seed.h"
#include "../core/spare.h"
#include "../core/play/rec.h"

static int fails, checks;

/* the player seeding tells (linked with seed.c): nothing plays here */
void player_all_sound_off(void) {}
void player_store_changed(void) {}
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A payload says which object and version it is, then bytes from a generator seeded by both. */
static uint32_t len_of(int obj, int ver) { return 40 + (uint32_t)((obj * 977 + ver * 131) % 2900); }
static void fill(uint8_t *b, int obj, int ver)
{
    uint32_t n = len_of(obj, ver), x = (uint32_t)(obj * 7919 + ver * 104729 + 1);
    b[0] = (uint8_t)obj; b[1] = (uint8_t)ver; b[2] = (uint8_t)(ver >> 8); b[3] = 0x5A;
    for (uint32_t i = 4; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (uint8_t)(x >> 16); }
}
/* Which version of obj the store holds: -1 none, -2 wrong bytes. */
static int version_held(uint8_t kind, int obj)
{
    uint32_t len; uint8_t ver;
    const uint8_t *p = store_get(kind, (uint8_t)obj, 0, &len, &ver);
    if (!p) return -1;
    int v = p[1] | p[2] << 8;
    static uint8_t want[4096];
    fill(want, obj, v);
    if (len != len_of(obj, v) || p[0] != obj || memcmp(p, want, len)) return -2;
    return v;
}

static uint8_t bufs[STORE_SAVE_MAX][4096];
static int save_versions(uint8_t kind, const int *objs, const int *vers, int n)
{
    int r = store_save_begin();
    if (r) return r;
    for (int i = 0; i < n; i++) {
        fill(bufs[i], objs[i], vers[i]);
        if ((r = store_save_put(kind, (uint8_t)objs[i], 0, 1, bufs[i], len_of(objs[i], vers[i])))) return r;
    }
    return store_save_run();
}
static int save_one(uint8_t kind, int obj, int ver) { return save_versions(kind, &obj, &ver, 1); }

/* A pattern's payload (specs/files/patterns.md): its head (tracks, rows - 1, EDIT STEP), its track bits, then each
 * stored track's stream: cells[t] cells of a note (a cell byte 0x81: a note follows, one row skipped), its 0x00 last;
 * the notes from seed. Its length. */
static uint32_t pat(uint8_t *b, int ntrk, const int *cells, int seed)
{
    uint32_t at = 5, bits = 0;
    b[0] = (uint8_t)ntrk; b[1] = 63; b[2] = 1;
    for (int t = 0; t < ntrk; t++) {
        if (!cells[t]) continue;
        bits |= 1u << t;
        for (int c = 0; c < cells[t]; c++) { b[at++] = 0x81; b[at++] = (uint8_t)(36 + (t * 7 + c * 5 + seed) % 60); }
        b[at++] = 0;
    }
    b[3] = (uint8_t)bits; b[4] = (uint8_t)(bits >> 8);
    return at;
}
/* The same, denser: 128 rows, cells[t] cells (128 at most) of a note and a command (4 B: 0x88, the note, the command's
 * code and value) on rows 0, 1, 2, ... Its length. */
static uint32_t pat_dense(uint8_t *b, int ntrk, const int *cells, int seed)
{
    uint32_t at = 5, bits = 0;
    b[0] = (uint8_t)ntrk; b[1] = 127; b[2] = 1;
    for (int t = 0; t < ntrk; t++) {
        if (!cells[t]) continue;
        bits |= 1u << t;
        for (int c = 0; c < cells[t]; c++) {
            b[at++] = 0x88; b[at++] = (uint8_t)(36 + (t * 7 + c * 5 + seed) % 60); b[at++] = 0x0A; b[at++] = (uint8_t)c;
        }
        b[at++] = 0;
    }
    b[3] = (uint8_t)bits; b[4] = (uint8_t)(bits >> 8);
    return at;
}
static int save_pat(int num, const uint8_t *b, uint32_t len)
{
    int r = store_save_begin();
    if (r || (r = store_save_put(REC_PATTERN, (uint8_t)num, 0, 1, b, len))) return r;
    return store_save_run();
}
static int edits_saved(void)            /* SAVE of the edits, written all at once: its result */
{
    int r = store_edit_save(), st;
    if (r) return r;
    while ((st = store_save_status(0)) == STORE_BUSY) store_poll();
    return st;
}

static void t_crc(void)
{
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u, "CRC-32 of 123456789: %08X", crc32_update(0, "123456789", 9));
    uint32_t c = crc32_update(0, "1234", 4);
    CHECK(crc32_update(c, "56789", 5) == 0xCBF43926u, "CRC-32 in two parts");
}

static void t_empty(void)
{
    fake_reset();
    store_init();
    store_usage_t u;
    store_usage(&u);
    CHECK(store_count() == 0, "an empty flash holds nothing");
    CHECK(u.user_blocks == 103 && u.user_free_blocks == 103, "user area 103 blocks, all free: %u %u", u.user_blocks, u.user_free_blocks);
    CHECK(u.user_bytes == 412u * 1024, "user area 412 KB");
    CHECK(u.user_free == (103u - STORE_MIN_FREE - STORE_EDITOR_ROOM) * STORE_BLOCK,
          "free: the room for compaction and for the EDITOR's pattern counted as used: %u", u.user_free);
    CHECK(store_get(REC_SONG, 0, 0, 0, 0) == 0, "no song");
}

static void t_basic(void)
{
    fake_reset();
    store_init();
    int objs[3] = { 1, 2, 3 }, vers[3] = { 1, 1, 1 };
    CHECK(save_versions(REC_PATTERN, objs, vers, 3) == STORE_OK, "a save of three");
    for (int i = 1; i <= 3; i++) CHECK(version_held(REC_PATTERN, i) == 1, "pattern %d held", i);
    store_init();                                       /* power-cycle */
    for (int i = 1; i <= 3; i++) CHECK(version_held(REC_PATTERN, i) == 1, "pattern %d after a power-cycle", i);
    CHECK(save_one(REC_PATTERN, 2, 2) == STORE_OK, "a newer version");
    CHECK(version_held(REC_PATTERN, 2) == 2, "the newer wins");
    store_init();
    CHECK(version_held(REC_PATTERN, 2) == 2, "the newer wins after a power-cycle");
    /* the head goes on in the same block after a power-cycle */
    store_rec_t a, b;
    store_info(REC_PATTERN, 2, 0, &a);
    CHECK(save_one(REC_PATTERN, 9, 1) == STORE_OK, "one more");
    store_info(REC_PATTERN, 9, 0, &b);
    CHECK(b.addr == a.addr + STORE_HDR + ((a.len + 3) & ~3u), "the next record right after the last: %X %X", a.addr, b.addr);
    /* a delete */
    CHECK(store_save_begin() == STORE_OK && store_save_del(REC_PATTERN, 1, 0) == STORE_OK && store_save_run() == STORE_OK, "a delete");
    CHECK(version_held(REC_PATTERN, 1) == -1, "deleted");
    store_init();
    CHECK(version_held(REC_PATTERN, 1) == -1, "deleted after a power-cycle");
    uint8_t nums[16];
    int n = store_list(REC_PATTERN, nums, 16);
    CHECK(n == 4 && nums[0] == 2 && nums[1] == 3 && nums[2] == 9 && nums[3] == 0xF0, "the list: %d", n);
    /* patterns' parts list once */
    uint8_t part[10] = { 1 };
    CHECK(store_save_begin() == 0 && store_save_put(REC_PATTERN, 3, 1, 1, part, 10) == 0 && store_save_run() == 0, "a second part");
    n = store_list(REC_PATTERN, nums, 16);
    CHECK(n == 4, "parts list once: %d", n);
    store_usage_t u;
    store_usage(&u);
    int kp = 0;
    while (store_kinds[kp] != REC_PATTERN) kp++;
    CHECK(u.count[kp] == 4 && u.user_free_blocks == 101, "figures: %u patterns, %u free", u.count[kp], u.user_free_blocks);
}

static void t_builtin(void)
{
    fake_reset();
    store_init();
    uint32_t len; uint8_t ver;
    const uint8_t *p = store_get(REC_DX7, 0xF0, 0, &len, &ver);
    CHECK(p && len == 4 && p[3] == 0xF0 && ver == 1, "a built-in cartridge");
    CHECK(store_get(REC_PATTERN, 0xF0, 1, &len, 0) && len == 2, "a built-in pattern's part");
    uint8_t x[8] = { 0 };
    uint8_t nums[8];
    CHECK(store_list(REC_DX7, nums, 8) == 1 && nums[0] == 0xF0, "the built-ins listed");
    /* a built-in thing changed and saved: its copy in the user area found first, listed once; deleted (its change
     * dropped), the firmware's own is back */
    {
        uint8_t ch[4] = {9, 9, 9, 0xF0};
        store_rec_t r;
        CHECK(store_save_begin() == 0 && store_save_put(REC_DX7, 0xF0, 0, 1, ch, 4) == 0 && store_save_run() == 0,
              "a built-in's change saved");
        p = store_get(REC_DX7, 0xF0, 0, &len, 0);
        CHECK(p && p[0] == 9 && store_info(REC_DX7, 0xF0, 0, &r) && r.area == STORE_AREA_USER, "the change found first");
        CHECK(store_list(REC_DX7, nums, 8) == 1 && nums[0] == 0xF0, "listed once");
        store_init();
        p = store_get(REC_DX7, 0xF0, 0, &len, 0);
        CHECK(p && p[0] == 9, "the change after a mount");
        CHECK(store_edit_del(REC_DX7, 0xF0, 0) == 0, "its change deleted (an edit)");
        p = store_get(REC_DX7, 0xF0, 0, &len, 0);
        CHECK(p && len == 4 && p[0] != 9 && store_info(REC_DX7, 0xF0, 0, &r) && r.area == STORE_AREA_BUILTIN,
              "deleted, the firmware's own is back at once");
        CHECK(store_list(REC_DX7, nums, 8) == 1 && nums[0] == 0xF0, "still listed");
        CHECK(store_edit_save() == 0, "SAVE");
        while (store_save_status(0) == STORE_BUSY) store_poll();
        store_init();
        p = store_get(REC_DX7, 0xF0, 0, &len, 0);
        CHECK(p && len == 4 && p[3] == 0xF0 && p[0] != 9 && store_info(REC_DX7, 0xF0, 0, &r) &&
              r.area == STORE_AREA_BUILTIN, "saved and mounted: the firmware's own");
    }
    /* a cartridge under a built-in's number but of another kind (an earlier build's, whose built-in there was of
     * that kind) is left out at a mount: the firmware's own stays, listed once */
    {
        uint8_t j[4] = {7, 7, 7, 7};
        CHECK(store_save_begin() == 0 && store_save_put(REC_JUNO, 0xF0, 0, 1, j, 4) == 0 && store_save_run() == 0,
              "a JUNO-106 cartridge F0 written");
        store_init();
        CHECK(!store_get(REC_JUNO, 0xF0, 0, &len, 0) && store_list(REC_JUNO, nums, 8) == 0, "left out at the mount");
        p = store_get(REC_DX7, 0xF0, 0, &len, 0);
        CHECK(p && len == 4 && p[3] == 0xF0 && store_list(REC_DX7, nums, 8) == 1, "the built-in DX7 F0 as before");
    }
    CHECK(store_list(REC_SONG, nums, 8) == 0, "no songs");
    /* a record in the flash numbered F0 (written by something else) does not count */
    CHECK(store_save_begin() == 0 && store_save_put(REC_SONG, 0xF0, 0, 1, x, 8) == 0 && store_save_run() == 0, "a song F0 is fine");
    CHECK(store_get(REC_SONG, 0xF0, 0, &len, 0) != 0, "songs may use F0");
}

/* The cartridges share one number space: a cartridge of another kind put under a number deletes the one there, and
 * only it: a sample's number is apart from the cartridges' (sample 05 is no part of cartridge 05), and the old form's
 * audio under that number is never touched (specs/files/store.md#a-record) */
static void t_cart_kind(void)
{
    fake_reset();
    store_init();
    uint8_t c[90] = { 1 }, a[200] = { 2 };
    c[9] = 2;                                           /* (a sampler of version 2: its head, 2 instruments) */
    CHECK(store_save_begin() == 0 && store_save_put(REC_SAMPLER, 5, 0, 2, c, 90) == 0, "a sampler");
    CHECK(store_save_put(REC_SAMPLE, 5, 0, 1, a, 200) == 0 && store_save_put(REC_SAMPLE, 6, 0, 1, a, 200) == 0 &&
          store_save_put(REC_AUDIO_OLD, 5, 0, 1, a, 200) == 0, "samples 05 and 06, and old audio under 05");
    CHECK(store_save_run() == 0, "saved");
    CHECK(store_save_begin() == 0 && store_save_put(REC_DX7, 5, 0, 1, c, 90) == 0 && store_save_run() == 0, "a DX7 cartridge at 05");
    CHECK(!store_get(REC_SAMPLER, 5, 0, 0, 0) && store_get(REC_DX7, 5, 0, 0, 0), "the sampler deleted, the DX7 cartridge there");
    CHECK(store_get(REC_SAMPLE, 5, 0, 0, 0) && store_get(REC_SAMPLE, 6, 0, 0, 0) && store_get(REC_AUDIO_OLD, 5, 0, 0, 0),
          "the samples stay, and so does the old audio under 05");
    store_init();
    CHECK(store_get(REC_SAMPLE, 5, 0, 0, 0) && store_get(REC_AUDIO_OLD, 5, 0, 0, 0) && !store_get(REC_SAMPLER, 5, 0, 0, 0),
          "so after a power-cycle");
}

/* A power cut at every flash operation of a save, plain and torn: the state before or after, never a mix. */
static uint8_t base[1u << 20];
static void t_cuts(void)
{
    fake_reset();
    store_init();
    int objs[6] = { 1, 2, 3, 4, 5, 6 }, v1[6] = { 1, 1, 1, 1, 1, 1 }, v2[7] = { 2, 2, 2, 2, 2, 2, 1 };
    int objs2[7] = { 1, 2, 3, 4, 5, 6, 7 };
    CHECK(save_versions(REC_PATTERN, objs, v1, 6) == 0, "the library before");
    memcpy(base, fake_flash, sizeof base);
    fake_ops = 0;
    CHECK(save_versions(REC_PATTERN, objs2, v2, 7) == 0, "the save to cut");
    long total = fake_ops;
    CHECK(total > 10, "the save takes %ld operations", total);
    int mixes = 0, before = 0, after = 0;
    static int torn;
    static long k;                                      /* static: kept across the longjmp */
    for (torn = 0; torn < 2; torn++)
        for (k = 0; k <= total; k++) {
            memcpy(fake_flash, base, sizeof base);
            store_init();
            fake_ops = 0; cut_at = k; cut_torn = torn;
            if (!setjmp(cut_env)) save_versions(REC_PATTERN, objs2, v2, 7);
            cut_at = -1;
            long ops = fake_ops;
            store_init();                               /* power on */
            long spoils = fake_ops - ops;
            int old = 1, nw = 1;
            for (int i = 0; i < 7; i++) {
                int v = version_held(REC_PATTERN, objs2[i]);
                if (v != (i < 6 ? 1 : -1)) old = 0;
                if (v != v2[i]) nw = 0;
            }
            if (old) before++; else if (nw) after++; else mixes++;
            CHECK(old || nw, "cut at %ld%s: a mix", k, torn ? " (torn)" : "");
            CHECK(!nw || spoils == 0, "cut at %ld: the save had finished, yet power-on wrote", k);
            CHECK(spoils <= 7, "cut at %ld: power-on wrote %ld times (a byte a record at most)", k, spoils);
            /* and the store goes on: a save after it, a power-cycle (which writes nothing) */
            CHECK(save_one(REC_SONG, 1, 3) == 0, "a save after the cut at %ld", k);
            long o2 = fake_ops;
            store_init();
            CHECK(fake_ops == o2, "cut at %ld: a later power-on wrote to the flash", k);
            CHECK(version_held(REC_SONG, 1) == 3, "it holds after a power-cycle (cut %ld)", k);
            for (int i = 0; i < 7; i++) {
                int v = version_held(REC_PATTERN, objs2[i]);
                CHECK(v == (old ? (i < 6 ? 1 : -1) : v2[i]), "cut %ld: pattern %d changed after the next save", k, objs2[i]);
            }
        }
    printf("  power cuts: %ld operations x 2, before %d, after %d, mixed %d\n", total + 1, before, after, mixes);
}

/* A record that fails its CRC costs only itself. */
static void t_crc_fail(void)
{
    fake_reset();
    store_init();
    int objs[3] = { 1, 2, 3 }, v[3] = { 1, 1, 1 };
    save_versions(REC_SONG, objs, v, 3);
    store_rec_t r;
    store_info(REC_SONG, 2, 0, &r);
    fake_flash[r.addr + STORE_HDR + 7] ^= 0x10;         /* a bit of song 2's payload */
    store_init();
    store_usage_t u;
    store_usage(&u);
    CHECK(version_held(REC_SONG, 1) == 1 && version_held(REC_SONG, 2) == -1 && version_held(REC_SONG, 3) == 1,
          "only the bad record lost: %d %d %d", version_held(REC_SONG, 1), version_held(REC_SONG, 2), version_held(REC_SONG, 3));
    CHECK(u.bad == 1, "one bad record counted: %u", u.bad);
    /* song 2's header bad too (its length): the next record is found by 4-byte steps */
    store_info(REC_SONG, 3, 0, &r);
    uint32_t song3 = r.addr;
    store_info(REC_SONG, 1, 0, &r);
    uint32_t song2 = r.addr + STORE_HDR + ((r.len + 3) & ~3u);
    fake_flash[song2 + 5] ^= 0x0F;
    store_init();
    CHECK(version_held(REC_SONG, 1) == 1 && version_held(REC_SONG, 3) == 1, "songs 1 and 3 still there");
    /* the save's last record (song 3, flagged) bad: the whole save goes */
    fake_flash[song3] = 0x55;
    store_init();
    CHECK(version_held(REC_SONG, 1) == -1 && version_held(REC_SONG, 3) == -1, "a save without its last record is ignored");
}

/* Churn: versions saved again and again; compaction keeps the room; power-cycles between. */
static void t_churn(void)
{
    fake_reset();
    store_init();
    enum { NOBJ = 40 };
    int held[NOBJ];
    for (int i = 0; i < NOBJ; i++) held[i] = -1;
    uint32_t x = 12345;
    int saves = 0, full = 0;
    for (int round = 0; round < 3000; round++) {
        x = x * 1103515245u + 12345u;
        int n = 1 + (int)(x >> 16) % 4, objs[4], vers[4];
        for (int i = 0; i < n; i++) {
            x = x * 1103515245u + 12345u;
            objs[i] = (int)(x >> 16) % NOBJ;
            for (int j = 0; j < i; j++) if (objs[j] == objs[i]) objs[i] = (objs[i] + 1) % NOBJ;
            vers[i] = held[objs[i]] + 2;
        }
        int r = save_versions(REC_PATTERN, objs, vers, n);
        if (r == STORE_ERR_FULL) { full++; continue; }
        CHECK(r == 0, "save %d: %d", round, r);
        if (r) break;
        saves++;
        for (int i = 0; i < n; i++) held[objs[i]] = vers[i];
        if (round % 97 == 0) {
            store_init();
            for (int i = 0; i < NOBJ; i++) {
                int v = version_held(REC_PATTERN, i);
                CHECK(v == held[i], "round %d: pattern %d holds %d, not %d", round, i, v, held[i]);
            }
        }
    }
    store_init();
    for (int i = 0; i < NOBJ; i++) CHECK(version_held(REC_PATTERN, i) == held[i], "at the end: pattern %d", i);
    store_usage_t u;
    store_usage(&u);
    printf("  churn: %d saves, %d refused, %ld erases, %u free blocks, the mount %u walks\n", saves, full,
           fake_erases, u.user_free_blocks, u.mount_walks);
    CHECK(u.mount_walks == 1, "no cut: one walk at power-on (%u)", u.mount_walks);
    CHECK(full == 0, "nothing refused: 40 objects of < 3 KB fit easily");
}

/* Churn with power cuts anywhere, compaction's saves included. */
static void t_churn_cuts(void)
{
    fake_reset();
    store_init();
    enum { NOBJ = 60 };
    int held[NOBJ];
    for (int i = 0; i < NOBJ; i++) held[i] = -1;
    uint32_t x = 777;
    int cuts = 0;
    for (int round = 0; round < 1500; round++) {
        x = x * 1103515245u + 12345u;
        int n = 1 + (int)(x >> 16) % 3, objs[3], vers[3];
        for (int i = 0; i < n; i++) {
            x = x * 1103515245u + 12345u;
            objs[i] = (int)(x >> 16) % NOBJ;
            for (int j = 0; j < i; j++) if (objs[j] == objs[i]) objs[i] = (objs[i] + 1) % NOBJ;
            vers[i] = held[objs[i]] + 2;
        }
        x = x * 1103515245u + 12345u;
        int cut = (x >> 16) % 5 == 0;
        fake_ops = 0;
        if (cut) { x = x * 1103515245u + 12345u; cut_at = (x >> 16) % 30; cut_torn = (x >> 20) & 1; }
        volatile int r = -99;
        if (!setjmp(cut_env)) r = save_versions(REC_PATTERN, objs, vers, n);
        int was_cut = cut && r == -99;
        cut_at = -1;
        if (was_cut) {
            cuts++;
            store_init();
            int all_new = 1, all_old = 1;
            for (int i = 0; i < n; i++) {
                int v = version_held(REC_PATTERN, objs[i]);
                if (v != vers[i]) all_new = 0;
                if (v != held[objs[i]]) all_old = 0;
            }
            CHECK(all_new || all_old, "round %d: a cut save came back mixed", round);
            if (all_new) for (int i = 0; i < n; i++) held[objs[i]] = vers[i];
        } else {
            CHECK(r == 0, "round %d: %d", round, r);
            for (int i = 0; i < n; i++) held[objs[i]] = vers[i];
        }
        for (int i = 0; i < NOBJ; i++)
            if (version_held(REC_PATTERN, i) != held[i]) {
                CHECK(0, "round %d: pattern %d holds %d, not %d", round, i, version_held(REC_PATTERN, i), held[i]);
                return;
            }
    }
    printf("  churn with cuts: 1500 saves, %d cut\n", cuts);
}

/* A full store: SAVE refuses while fewer than 2 free blocks would be left; nothing is lost. */
static uint8_t big[3500];
static int save_big(int obj)
{
    memset(big, obj, sizeof big);
    int r = store_save_begin();
    if (!r) r = store_save_put(REC_SONG, (uint8_t)obj, 0, 1, big, sizeof big);
    return r ? r : store_save_run();
}
static int big_held(int obj)
{
    uint32_t len;
    const uint8_t *p = store_get(REC_SONG, (uint8_t)obj, 0, &len, 0);
    if (!p) return 0;
    for (uint32_t i = 0; i < len; i++) if (p[i] != (uint8_t)obj) return -1;
    return len == sizeof big;
}
static void t_full(void)
{
    fake_reset();
    store_init();
    int obj = 0, r;
    while ((r = save_big(obj)) == STORE_OK && obj < 255) obj++;
    store_usage_t u;
    store_usage(&u);
    CHECK(r == STORE_ERR_FULL, "a full store refuses: %d after %d", r, obj);
    CHECK(u.user_free_blocks >= STORE_MIN_FREE + STORE_EDITOR_ROOM,
          "at least 10 blocks left free (compaction's 2, the EDITOR's 8): %u", u.user_free_blocks);
    CHECK(store_save_short() >= STORE_BLOCK && store_save_short() % STORE_BLOCK == 0 &&
          store_save_short() <= (uint32_t)STORE_BLOCK * (1 + STORE_MIN_FREE),
          "the refused save says what it lacked (blocks): %u", (unsigned)store_save_short());
    store_init();
    for (int i = 0; i < obj; i++) CHECK(big_held(i) == 1, "song %d kept", i);
    /* deleting makes room again */
    CHECK(store_save_begin() == 0, "begin");
    for (int i = 0; i < 20; i++) store_save_del(REC_SONG, (uint8_t)i, 0);
    CHECK(store_save_run() == 0, "deleting twenty");
    int more = 0;
    while (save_big(obj + more) == STORE_OK && obj + more < 255) more++;
    CHECK(more >= 18, "room again: %d more", more);
    store_init();
    for (int i = 20; i < obj + more; i++) CHECK(big_held(i) == 1, "song %d after", i);
    for (int i = 0; i < 20; i++) CHECK(big_held(i) == 0, "song %d stays deleted", i);
    printf("  full: %d songs of 3.5 KB, then %d more after deleting 20\n", obj, more);
}

/* Other firmwares' leftovers: blocks with no record of PT/FM's count as free, erased before use. */
static void t_leftovers(void)
{
    fake_reset();
    for (uint32_t a = STORE_U1_START; a < STORE_U1_START + 3 * 4096; a++) fake_flash[a] = (uint8_t)(a * 7 + 3);
    for (uint32_t a = 0x0FC000; a < 0x0FF000; a++) fake_flash[a] = (uint8_t)(a >> 3);
    store_init();
    store_usage_t u;
    store_usage(&u);
    CHECK(store_count() == 0, "nothing of PT/FM's");
    CHECK(u.user_free_blocks == 103, "every block free: %u", u.user_free_blocks);
    CHECK(save_one(REC_SONG, 4, 1) == 0, "a save over the leftovers");
    store_init();
    CHECK(version_held(REC_SONG, 4) == 1, "and it is read back");
    /* an erase cut short (a few bytes not 0xFF) is erased again before it is written */
    fake_reset();
    store_init();
    memset(&fake_flash[STORE_U1_START], 0xFF, 4096);
    fake_flash[STORE_U1_START + 3000] = 0x00;
    CHECK(save_one(REC_SONG, 4, 1) == 0, "a block not quite erased");
    store_init();
    CHECK(version_held(REC_SONG, 4) == 1, "read back");
}

/* Audio: records over consecutive blocks of their own, as the uploader writes them. (put_raw's flags: 1 its save's
 * last, 2 deleted; put_raw_v with the record's version.) */
static void put_raw_v(uint32_t addr, uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const uint8_t *p, uint32_t len,
                      uint32_t save, int flags)
{
    uint8_t h[16] = { kind, num, sub, ver, (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)flags,
                      (uint8_t)save, (uint8_t)(save >> 8), (uint8_t)(save >> 16), (uint8_t)(save >> 24) };
    uint32_t c = crc32_update(crc32_update(0, h, 12), p, len);
    h[12] = (uint8_t)c; h[13] = (uint8_t)(c >> 8); h[14] = (uint8_t)(c >> 16); h[15] = (uint8_t)(c >> 24);
    memcpy(&fake_flash[addr], h, 16);
    memcpy(&fake_flash[addr + 16], p, len);
}
static void put_raw(uint32_t addr, uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len, uint32_t save, int flags)
{
    put_raw_v(addr, kind, num, sub, 1, p, len, save, flags);
}
/* A sample's record (specs/files/instruments.md#samples) into b, n bytes: its entry (length `length`, rate 22050, coding
 * `coding`, a name), then its audio, whose head says coding `acoding` and length `alength` (the two agree in a sample
 * as it should be), then bytes from seed standing for the index and the blocks. Its length, n. */
static void bits_put(uint8_t *b, uint32_t at, uint32_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, at++)
        if (v >> i & 1u) b[at >> 3] |= (uint8_t)(1u << (at & 7u)); else b[at >> 3] &= (uint8_t)~(1u << (at & 7u));
}
static uint32_t sample_rec(uint8_t *b, uint32_t n, uint32_t length, int coding, uint32_t alength, int acoding, int seed)
{
    memset(b, 0, 16);
    bits_put(b, 0, length, 20);
    bits_put(b, 20, 22050, 18);
    bits_put(b, 38, (uint32_t)coding, 4);
    for (uint32_t i = 0; i < 10; i++) bits_put(b, 42 + 6 * i, 11 + i, 6);   /* (a name: its characters' codes) */
    b[16] = (uint8_t)acoding;
    b[17] = (uint8_t)alength; b[18] = (uint8_t)(alength >> 8); b[19] = (uint8_t)(alength >> 16);
    uint32_t x = (uint32_t)seed * 2654435761u + 1u;
    for (uint32_t i = 20; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (uint8_t)(x >> 16); }
    return n;
}
static void t_audio(void)
{
    fake_reset();
    static uint8_t a[10000];
    for (int i = 0; i < 10000; i++) a[i] = (uint8_t)(i * 13);
    uint32_t at = STORE_U1_START;
    put_raw(at, REC_AUDIO_OLD, 1, 0, a, 10000, 5, 0);   /* 2.5 blocks */
    at += 16 + 10000;
    put_raw(at, REC_AUDIO_OLD, 1, 1, a, 3000, 5, 0);
    at += 16 + 3000;
    put_raw(STORE_U1_START + 5 * 4096, REC_SAMPLER, 1, 0, a, 300, 5, 1);
    store_init();
    uint32_t len;
    const uint8_t *p = store_get(REC_AUDIO_OLD, 1, 0, &len, 0);
    CHECK(p && len == 10000 && !memcmp(p, a, 10000), "a sample over three blocks");
    p = store_get(REC_AUDIO_OLD, 1, 1, &len, 0);
    CHECK(p && len == 3000 && !memcmp(p, a, 3000), "the next sample, across a block line");
    CHECK(store_get(REC_SAMPLER, 1, 0, &len, 0) && len == 300, "the sampler");
    /* a save does not append into the audio's last block, nor into the sampler's block after it? (that is a record block: it may) */
    CHECK(save_one(REC_SONG, 1, 1) == 0, "a save");
    store_rec_t r;
    store_info(REC_SONG, 1, 0, &r);
    CHECK(r.addr >> 12 != (STORE_U1_START + 16 + 10000 + 16 + 2999) >> 12, "not in an audio block");
    /* churn: the audio never moves */
    for (int i = 0; i < 800; i++) save_one(REC_PATTERN, i % 30, i + 1);
    store_init();
    p = store_get(REC_AUDIO_OLD, 1, 0, &len, 0);
    CHECK(p == &fake_flash[STORE_U1_START + 16] && !memcmp(p, a, 10000), "the audio stays where it was");
    store_usage_t u;
    store_usage(&u);
    CHECK(u.user_audio == 16 + 10000 + 16 + 3000, "the audio counted: %u", u.user_audio);
}

/* Both kinds are audio to the store (specs/files/store.md#audio): samples (0x11) and the old form's audio (0x10), side
 * by side in one run of blocks as an uploader lays them, each crossing blocks: both mounted, read in place; their
 * blocks never appended to, never compacted, never written over by saves that fill the user area; the old audio
 * counted as audio, not as samples. */
static void t_samples_audio(void)
{
    fake_reset();
    static uint8_t s1[10000], s2[3000], o1[6000], o2[2500];
    uint32_t at = STORE_U1_START, a1, a2, a3, a4;
    sample_rec(s1, sizeof s1, 19000, 1, 19000, 1, 1);
    sample_rec(s2, sizeof s2, 4000, 2, 4000, 2, 2);
    for (uint32_t i = 0; i < sizeof o1; i++) o1[i] = (uint8_t)(i * 7);
    for (uint32_t i = 0; i < sizeof o2; i++) o2[i] = (uint8_t)(i * 3);
    put_raw(a1 = at, REC_SAMPLE, 0x23, 0, s1, sizeof s1, 5, 0);           /* 2.5 blocks */
    at += 16 + sizeof s1;
    put_raw(a2 = at, REC_AUDIO_OLD, 0x0C, 3, o1, sizeof o1, 5, 0);       /* across the next block line */
    at += 16 + sizeof o1;
    put_raw(a3 = at, REC_SAMPLE, 0x24, 0, s2, sizeof s2, 5, 0);
    at += 16 + sizeof s2;
    put_raw(a4 = at, REC_AUDIO_OLD, 0x0C, 4, o2, sizeof o2, 5, 0);
    at += 16 + ((sizeof o2 + 3u) & ~3u);
    uint32_t audio_end = at;                                            /* (the run's last block is not full) */
    put_raw(STORE_U2_START, REC_SONG, 1, 0, o1, 64, 5, 1);
    store_init();
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SAMPLE, 0x23, 0, &len, &ver);
    CHECK(p == &fake_flash[a1 + 16] && len == sizeof s1 && ver == 1 && !memcmp(p, s1, len), "a sample over three blocks, read in place");
    p = store_get(REC_SAMPLE, 0x24, 0, &len, 0);
    CHECK(p == &fake_flash[a3 + 16] && len == sizeof s2 && !memcmp(p, s2, len), "the next sample, after old audio");
    CHECK(store_get(REC_AUDIO_OLD, 0x0C, 3, &len, 0) == &fake_flash[a2 + 16] && len == sizeof o1 &&
          store_get(REC_AUDIO_OLD, 0x0C, 4, &len, 0) == &fake_flash[a4 + 16] && len == sizeof o2, "the old audio beside them, kept");
    store_usage_t u;
    store_usage(&u);
    int ks = 0, ko = 0, bad = u.bad;
    while (ks < STORE_NKINDS && store_kinds[ks] != REC_SAMPLE) ks++;
    while (ko < STORE_NKINDS && store_kinds[ko] != REC_AUDIO_OLD) ko++;
    CHECK(ks < STORE_NKINDS && ko < STORE_NKINDS && u.count[ks] == 2 && u.count[ko] == 2 && !bad,
          "counted: 2 samples, 2 old audio records, none bad (%d %d %d)", ks < STORE_NKINDS ? u.count[ks] : -1,
          ko < STORE_NKINDS ? u.count[ko] : -1, bad);
    CHECK(u.user_audio == 4 * 16u + sizeof s1 + sizeof o1 + sizeof s2 + ((sizeof o2 + 3u) & ~3u), "both kinds counted as audio: %u",
          u.user_audio);
    CHECK(store_is_audio(REC_SAMPLE) && store_is_audio(REC_AUDIO_OLD) && !store_is_audio(REC_SAMPLER) && !store_is_audio(REC_SONG),
          "store_is_audio: 0x10 and 0x11 alone");
    /* saves, compactions, a full user area: no record ever lands in the audio's blocks, nothing of it moves */
    int obj = 0, in_audio = 0;
    for (int i = 0; i < 600; i++) save_one(REC_PATTERN, i % 25, i + 1);
    while (save_big(obj) == STORE_OK && obj < 255) obj++;
    CHECK(obj > 20, "the user area filled around the audio: %d songs", obj);
    for (int i = 0; i < store_count(); i++) {
        store_rec_t r;
        store_entry(i, &r, 0, 0);
        if (!store_is_audio(r.kind) && r.addr >> 12 >= a1 >> 12 && r.addr >> 12 <= (audio_end - 1) >> 12) in_audio++;
    }
    CHECK(!in_audio, "no record written into the audio's blocks: %d", in_audio);
    store_init();
    CHECK(store_get(REC_SAMPLE, 0x23, 0, &len, 0) == &fake_flash[a1 + 16] && !memcmp(&fake_flash[a1 + 16], s1, sizeof s1) &&
          store_get(REC_SAMPLE, 0x24, 0, &len, 0) == &fake_flash[a3 + 16] && !memcmp(&fake_flash[a3 + 16], s2, sizeof s2) &&
          store_get(REC_AUDIO_OLD, 0x0C, 3, &len, 0) == &fake_flash[a2 + 16] && !memcmp(&fake_flash[a2 + 16], o1, sizeof o1) &&
          store_get(REC_AUDIO_OLD, 0x0C, 4, &len, 0) == &fake_flash[a4 + 16] && !memcmp(&fake_flash[a4 + 16], o2, sizeof o2),
          "after the user area filled and a power-cycle: both kinds where they were, whole");
    /* deleting songs makes room again; the audio stays */
    CHECK(store_save_begin() == 0, "begin");
    for (int i = 0; i < 10; i++) store_save_del(REC_SONG, (uint8_t)i, 0);
    CHECK(store_save_run() == STORE_OK && save_big(200) == STORE_OK && store_get(REC_SAMPLE, 0x23, 0, &len, 0) == &fake_flash[a1 + 16],
          "room again, the audio where it was");
}

/* A sampler cartridge holds instruments only (kind 0x04 version 2: its 10 B head, 40 B an instrument, no samples'
 * table): edited as pieces (its head and its slots), built as it was put, saved alone, no audio with it. Deleting it
 * leaves the samples; a sample's deletion is a whole record's, and goes first when storage is full. */
static uint32_t sampler_v2(uint8_t *b, int n, int seed)
{
    memset(b, 0, 10);
    b[0] = (uint8_t)seed; b[9] = (uint8_t)n;
    for (int s = 0; s < n; s++) {
        uint8_t *d = b + 10 + 40 * s;
        memset(d, 1 + (s + seed) % 40, 40);
        d[0] = (uint8_t)(0x20 + s);                     /* (SAMPLE: the sample's number) */
    }
    return 10u + 40u * (uint32_t)n;
}
static void t_sampler_v2(void)
{
    fake_reset();
    static uint8_t a[4096], b[4096], got[4096], s[6000];
    uint32_t la, lb, l;
    uint8_t v;
    sample_rec(s, sizeof s, 9000, 1, 9000, 1, 3);
    put_raw(STORE_U1_START, REC_SAMPLE, 0x05, 0, s, sizeof s, 5, 0);
    put_raw(STORE_U1_START + 16 + sizeof s, REC_SAMPLE, 0x06, 0, s, 200, 5, 1);
    store_init();
    la = sampler_v2(a, 3, 1);
    CHECK(store_edit_put(REC_SAMPLER, 5, 0, 2, a, la) == STORE_OK && store_piece(REC_SAMPLER, 5, STORE_PC_HEAD, &l) && l == 10 &&
          store_piece(REC_SAMPLER, 5, 2, &l) && l == 40, "a sampler of version 2 put: its head (10 B) and its slots, pieces");
    CHECK(store_edit_build(REC_SAMPLER, 5, 0, got, sizeof got, &l, &v) == 0 && l == la && v == 2 && !memcmp(got, a, la),
          "built as put, version 2");
    int n0 = store_count();
    CHECK(edits_saved() == STORE_OK && store_get(REC_SAMPLER, 5, 0, &l, &v) && l == la && v == 2 && store_count() == n0 + 1,
          "saved: the sampler's record alone, no audio with it");
    {
        store_key_t keys[STORE_MOVE_MAX];
        int nk = store_thing(REC_SAMPLER, 5, 0, keys, STORE_MOVE_MAX), ns = store_thing(REC_SAMPLE, 5, 0, keys + 8, 8);
        CHECK(nk == 1 && keys[0].kind == REC_SAMPLER && ns == 1 && keys[8].kind == REC_SAMPLE &&
              store_thing_bytes(keys, 1) == 16u + ((la + 3u) & ~3u), "the sampler as a thing: its record alone (sample 05 a thing "
              "of its own)");
    }
    memcpy(b, a, la);
    memset(b + 10 + 40, 0x55, 40);                      /* (slot 1 changed) */
    CHECK(store_edit_put(REC_SAMPLER, 5, 0, 2, b, la) == STORE_OK && store_piece(REC_SAMPLER, 5, 1, &l) && l == 40 &&
          !store_piece(REC_SAMPLER, 5, 0, 0) && !store_piece(REC_SAMPLER, 5, STORE_PC_HEAD, 0), "a slot changed: its piece alone");
    store_edit_forget(REC_SAMPLER, 5, 0);
    CHECK(!store_edit_count(), "REVERT: nothing unsaved");
    /* the device writes no other version of a sampler */
    lb = 11 + 40;
    memset(b, 0, lb);
    b[9] = 1; b[10] = 0;
    CHECK(store_edit_put(REC_SAMPLER, 8, 0, 1, b, lb) == STORE_ERR_STATE && !store_edit_count(), "a sampler of version 1 put: refused");
    CHECK(store_edit_piece(REC_SAMPLER, 5, 0, 1, a + 10, 40) == STORE_ERR_STATE && !store_edit_count(),
          "a piece of version 1 for a sampler: refused");
    /* DELETE of the sampler: its instruments only; samples 05 and 06 stay */
    CHECK(store_edit_del(REC_SAMPLER, 5, 0) == STORE_OK && store_edit_has(REC_SAMPLER, 5, 0) == 2 &&
          !store_edit_has(REC_SAMPLE, 5, 0) && store_get(REC_SAMPLE, 5, 0, &l, 0), "the sampler deleted: sample 05 untouched");
    CHECK(edits_saved() == STORE_OK && !store_get(REC_SAMPLER, 5, 0, 0, 0) && store_get(REC_SAMPLE, 5, 0, &l, 0) && l == sizeof s &&
          store_get(REC_SAMPLE, 6, 0, &l, 0) && l == 200, "saved: the samples stay");
    /* a sample deleted (an edit): a whole record's deletion; a sampler's REVERT never takes it back */
    la = sampler_v2(a, 2, 4);
    CHECK(store_edit_put(REC_SAMPLER, 6, 0, 2, a, la) == STORE_OK && store_edit_del(REC_SAMPLE, 6, 0) == STORE_OK &&
          store_edit_has(REC_SAMPLE, 6, 0) == 2 && !store_get(REC_SAMPLE, 6, 0, 0, 0), "sampler 06 made, sample 06 deleted (edits)");
    CHECK(store_edit_forget(REC_SAMPLER, 6, 0) == STORE_OK && store_edit_has(REC_SAMPLE, 6, 0) == 2 && store_edit_count() == 1,
          "REVERT of cartridge 06: sample 06's deletion stays");
    CHECK(edits_saved() == STORE_OK && !store_get(REC_SAMPLE, 6, 0, 0, 0) && store_get(REC_SAMPLE, 5, 0, 0, 0), "saved: sample 06 gone");
    store_init();
    CHECK(!store_get(REC_SAMPLE, 6, 0, 0, 0) && store_get(REC_SAMPLE, 5, 0, &l, 0) && l == sizeof s, "so after a power-cycle");
}

/* A sampler's empty slot in version 2 (specs/files/instruments.md#sample-instruments): SAMPLE (8 bits), START .. the
 * envelope fill bits 0-175, so the name's first character is at bit 176; an empty slot is that character 63, every other
 * bit 0. The store tells an empty slot by it (an empty slot is as none) and makes the empty slots a build fills in so,
 * the envelope's last bits (174-175) left alone. */
static void v2_empty(uint8_t *s) { memset(s, 0, 40); s[22] = 0x3F; }
static void t_sampler_v2_slots(void)
{
    fake_reset();
    store_init();
    static uint8_t a[4096], got[4096];
    uint8_t e[40], h[10], s6[40], v;
    uint32_t la, l;
    v2_empty(e);
    la = sampler_v2(a, 4, 1);
    memcpy(a + 10 + 40 * 2, e, 40);                     /* slot 2 empty */
    memset(a + 10 + 40, 0, 40);                         /* slot 1: the envelope's last bits set, its name's first */
    a[10 + 40 + 21] = 0xC0;                             /* character 15 (not empty) */
    a[10 + 40 + 22] = 0x0F;
    CHECK(store_edit_put(REC_SAMPLER, 0x12, 0, 2, a, la) == STORE_OK && !store_piece(REC_SAMPLER, 0x12, 2, 0),
          "v2 slots: an empty slot (character 63 at bit 176) is as none: no piece");
    CHECK(store_piece(REC_SAMPLER, 0x12, 1, &l) && l == 40, "v2 slots: a used one with the envelope's bits 174-175 set is kept");
    CHECK(store_edit_build(REC_SAMPLER, 0x12, 0, got, sizeof got, &l, &v) == 0 && l == la && !memcmp(got, a, la),
          "v2 slots: built as put, the empty slot as a v2 empty slot");
    CHECK(edits_saved() == STORE_OK && store_get(REC_SAMPLER, 0x12, 0, &l, &v) && l == la && !memcmp(store_get(REC_SAMPLER, 0x12, 0, 0, 0), a, la),
          "v2 slots: saved as put");
    /* slots added past the stored ones with a gap: the build fills the gap with v2 empty slots */
    memcpy(h, a, 10);
    h[9] = 7;
    memset(s6, 0x21, 40);
    CHECK(store_edit_piece(REC_SAMPLER, 0x12, STORE_PC_HEAD, 2, h, 10) == STORE_OK &&
          store_edit_piece(REC_SAMPLER, 0x12, 6, 2, s6, 40) == STORE_OK, "v2 slots: 7 slots, slot 6 put");
    CHECK(store_edit_build(REC_SAMPLER, 0x12, 0, got, sizeof got, &l, &v) == 0 && l == 10u + 7u * 40u &&
          !memcmp(got, h, 10) && !memcmp(got + 10, a + 10, la - 10) && !memcmp(got + 10 + 40 * 4, e, 40) && !memcmp(got + 10 + 40 * 5, e, 40) &&
          !memcmp(got + 10 + 40 * 6, s6, 40), "v2 slots: the gap's slots 4 and 5 empty: 63 at bit 176, bits 174-175 zero");
    store_edit_forget(REC_SAMPLER, 0x12, 0);
    /* an empty slot put past those stored is as none: nothing unsaved */
    CHECK(store_edit_piece(REC_SAMPLER, 0x12, 9, 2, e, 40) == STORE_OK && !store_edit_count(),
          "v2 slots: an empty slot put past those stored: nothing unsaved");
}

/* Deletions first (specs/files/store.md#the-room-for-the-editors-pattern): a sample's deletion always qualifies (the
 * FM-1 makes no samples), even with a cartridge made anew among the changes; it goes in a save of its own, then the
 * rest once compaction has freed its blocks */
static void t_sample_dels_first(void)
{
    fake_reset();
    static uint8_t s[12000], a[4096];
    sample_rec(s, sizeof s, 20000, 1, 20000, 1, 7);
    put_raw(STORE_U1_START, REC_SAMPLE, 0x30, 0, s, sizeof s, 1, 1);  /* (3 blocks of its own) */
    store_init();
    int obj = 0;
    while (save_big(obj) == STORE_OK && obj < 255) obj++;
    uint32_t la = sampler_v2(a, 64, 2), l;                             /* a new sampler: ~2.6 KB, it needs a block */
    CHECK(store_edit_put(REC_SAMPLER, 0x40, 0, 2, a, la) == STORE_OK && store_edit_del(REC_SAMPLE, 0x30, 0) == STORE_OK,
          "storage full: a new sampler, a sample deleted (edits)");
    int r = edits_saved();
    CHECK(r == STORE_OK && !store_edit_count() && !store_get(REC_SAMPLE, 0x30, 0, 0, 0) && store_get(REC_SAMPLER, 0x40, 0, &l, 0) &&
          l == la, "one SAVE: the sample's deletion first, its blocks freed, then the new sampler (%d)", r);
    uint32_t sv_del = 0, sv_new = 0;
    for (int i = 0; i < store_count(); i++) {
        store_rec_t e;
        uint32_t sv;
        store_entry(i, &e, &sv, 0);
        if (e.kind == REC_SAMPLE && e.num == 0x30 && e.deleted) sv_del = sv;
        if (e.kind == REC_SAMPLER && e.num == 0x40) sv_new = sv;
    }
    CHECK(sv_del && sv_new > sv_del, "two saves: the deletion's %u, then the sampler's %u", sv_del, sv_new);
    store_init();
    CHECK(!store_get(REC_SAMPLE, 0x30, 0, 0, 0) && store_get(REC_SAMPLER, 0x40, 0, 0, 0), "so after a power-cycle");
}

/* A sample deleted with the instruments that use it, in two samplers, storage full (deletions first: the sample's
 * deletion a save of its own, the samplers' changes the next: specs/ui/saving.md#storage-full), as the FM-1's DELETE
 * makes the edits; that SAVE cut by a power cut at every flash operation, plain and torn. At each power-on the samplers
 * read whole and the sample is there whole or not at all; its instruments name it as before (cut before the deletion),
 * name it gone (cut between the two saves: a missing sample, grey and silent) or are empty with it gone (both written);
 * never the samplers' change without the deletion, never one sampler's without the other's */
static uint8_t sdc_base[1u << 20];
static uint8_t sdc_a[4096], sdc_b[4096];                /* the samplers as the edits leave them */
static uint32_t sdc_la, sdc_lb;
static int sdc_inst(uint8_t cart, int slot)     /* the sample an instrument names, -1 an empty slot, -2 no sampler */
{
    rec_cart_t c;
    rec_sinst_t si;
    if (rec_cart(cart, &c) || c.kind != REC_SAMPLER) return -2;
    const uint8_t *x = rec_slot(&c, (uint8_t)slot);
    if (!x) return -1;
    rec_sampler_inst(x, &si);
    return si.sample;
}
static int sdc_state(void)              /* 0 before, 1 the deletion alone, 2 both; -1 anything else */
{
    rec_sample_t sa;
    uint32_t len;
    int has30 = !rec_sample(0x30, &sa), named = 0, empty = 0;
    if (!has30 && store_get(REC_SAMPLE, 0x30, 0, &len, 0)) return -1;  /* (there, yet not read: in part) */
    if (rec_sample(0x31, &sa) || sdc_inst(0x40, 1) != 0x31 || sdc_inst(0x41, 2) != 0x31 || sdc_inst(0x41, 63) != 0x31)
        return -1;                                      /* (the others as they were) */
    static const uint8_t AT[3][2] = {{0x40, 0}, {0x41, 0}, {0x41, 1}};    /* (the instruments on sample 30) */
    for (int k = 0; k < 3; k++) {
        int s = sdc_inst(AT[k][0], AT[k][1]);
        if (s == -2) return -1;
        named += s == 0x30;
        empty += s == -1;
    }
    return has30 && named == 3 ? 0 : !has30 && named == 3 ? 1 : !has30 && empty == 3 ? 2 : -1;
}
static int sdc_edits(void)
{
    int r = store_edit_del(REC_SAMPLE, 0x30, 0);
    if (!r) r = store_edit_put(REC_SAMPLER, 0x40, 0, 2, sdc_a, sdc_la);
    if (!r) r = store_edit_put(REC_SAMPLER, 0x41, 0, 2, sdc_b, sdc_lb);
    return r;
}
static void t_sample_dels_cuts(void)
{
    fake_reset();
    static uint8_t s30[12000], s31[300], a[4096], b[4096];
    sample_rec(s30, sizeof s30, 20000, 1, 20000, 1, 9);
    sample_rec(s31, sizeof s31, 500, 1, 500, 1, 4);
    put_raw(STORE_U1_START, REC_SAMPLE, 0x30, 0, s30, sizeof s30, 1, 0);         /* (3 blocks of its own) */
    put_raw(STORE_U1_START + 3 * 4096, REC_SAMPLE, 0x31, 0, s31, sizeof s31, 1, 1);
    store_init();
    sdc_la = sampler_v2(a, 64, 3);                      /* (64 instruments each: ~2.6 KB, a block each) */
    sdc_lb = sampler_v2(b, 64, 5);
    for (int s = 0; s < 64; s++) { a[10 + 40 * s] = s < 1 ? 0x30 : 0x31; b[10 + 40 * s] = s < 2 ? 0x30 : 0x31; }
    CHECK(store_save_begin() == STORE_OK && store_save_put(REC_SAMPLER, 0x40, 0, 2, a, sdc_la) == STORE_OK &&
          store_save_put(REC_SAMPLER, 0x41, 0, 2, b, sdc_lb) == STORE_OK && store_save_run() == STORE_OK,
          "two samplers saved, three instruments on sample 30");
    int obj = 0;
    while (save_big(obj) == STORE_OK && obj < 255) obj++;
    memcpy(sdc_a, a, sdc_la);
    memcpy(sdc_b, b, sdc_lb);
    uint8_t *e[3] = {sdc_a + 10, sdc_b + 10, sdc_b + 50};  /* (their slots emptied: 0s, 63 the name's first character) */
    for (int k = 0; k < 3; k++) { memset(e[k], 0, 40); bits_put(e[k], 176, 63, 6); }
    memcpy(sdc_base, fake_flash, sizeof sdc_base);
    CHECK(sdc_state() == 0, "storage full: the samplers whole, sample 30 there (%d)", sdc_state());
    fake_ops = 0;
    CHECK(sdc_edits() == STORE_OK && edits_saved() == STORE_OK && sdc_state() == 2,
          "the SAVE to cut: the sample and its instruments deleted (%d)", sdc_state());
    uint32_t sv_del = 0, sv_a = 0, sv_b = 0;
    for (int i = 0; i < store_count(); i++) {
        store_rec_t r;
        uint32_t sv;
        store_entry(i, &r, &sv, 0);
        if (r.kind == REC_SAMPLE && r.num == 0x30 && r.deleted) sv_del = sv;
        if (r.kind == REC_SAMPLER && r.num == 0x40) sv_a = sv;
        if (r.kind == REC_SAMPLER && r.num == 0x41) sv_b = sv;
    }
    CHECK(sv_del && sv_a > sv_del && sv_b == sv_a, "deletions first: the sample's deletion saved %u, then both samplers in "
          "one save %u, %u", sv_del, sv_a, sv_b);
    long total = fake_ops;
    static int torn, n[3], bad;
    static long k;                                      /* static: kept across the longjmp */
    n[0] = n[1] = n[2] = bad = 0;
    for (torn = 0; torn < 2; torn++)
        for (k = 0; k <= total; k++) {
            memcpy(fake_flash, sdc_base, sizeof sdc_base);
            store_init();
            store_edit_revert();
            sdc_edits();
            fake_ops = 0; cut_at = k; cut_torn = torn;
            if (!setjmp(cut_env)) edits_saved();
            cut_at = -1;
            store_init();                               /* power on: the edits in RAM gone */
            store_edit_revert();
            int st = sdc_state();
            if (st >= 0) n[st]++;
            else { bad++; CHECK(0, "a sample's deletion cut at %ld%s: a sampler or the sample broken, or a mix", k, torn ? " (torn)" : ""); }
        }
    printf("  a sample's deletion cut: %ld operations x 2, before %d, the deletion alone (instruments naming it gone) %d, "
           "both %d\n", total + 1, n[0], n[1], n[2]);
    CHECK(!bad && n[0] && n[1] && n[2], "a sample's deletion cut at every operation: before %d, the deletion alone %d, both %d",
          n[0], n[1], n[2]);
}

/* The old form (specs/files/instruments.md#the-old-form): an old sampler (kind 0x04 version 1) and its audio (kind 0x10)
 * are never touched by the device: the old sampler's number stays taken (no NEW, CLONE or MOVE lands there: the store
 * refuses any cartridge there), the device deletes neither, and compaction moves the sampler byte for byte */
static void t_old_form(void)
{
    fake_reset();
    static uint8_t a[4096], old[11 + 2 * 40 + 2 * 16], o1[5000];
    uint8_t nums[16];
    uint32_t l;
    memset(old, 0x21, sizeof old);
    old[9] = 2; old[10] = 2;
    for (uint32_t i = 0; i < sizeof o1; i++) o1[i] = (uint8_t)(i * 5);
    put_raw(STORE_U1_START, REC_AUDIO_OLD, 7, 0, o1, sizeof o1, 4, 0);
    put_raw(STORE_U1_START + 16 + sizeof o1, REC_AUDIO_OLD, 7, 1, o1, 900, 4, 0);
    put_raw(STORE_U1_START + 3 * 4096, REC_SAMPLER, 7, 0, old, sizeof old, 4, 1);
    memset(a, 0x33, 50);                                /* a sampler of a version this firmware does not read at all */
    put_raw_v(STORE_U1_START + 3 * 4096 + 16 + ((sizeof old + 3u) & ~3u), REC_SAMPLER, 9, 0, 3, a, 50, 4, 0);
    store_init();
    uint32_t la = sampler_v2(a, 1, 1);
    CHECK(store_get(REC_SAMPLER, 7, 0, &l, 0) && l == sizeof old, "an old sampler 07 and its audio");
    CHECK(rec_cart_taken(7) && !rec_cart_taken(8), "its number taken (rec_cart_taken), 08 free");
    CHECK(store_edit_put(REC_DX7, 7, 0, 1, a, 10) == STORE_ERR_STATE && store_edit_put(REC_SAMPLER, 7, 0, 2, a, la) == STORE_ERR_STATE &&
          store_edit_put(REC_FAV, 7, 0, 1, a, 10) == STORE_ERR_STATE && !store_edit_count(),
          "no cartridge lands at 07 (NEW, CLONE, MOVE: a DX7, a sampler, FAVORITES): refused, nothing changed");
    CHECK(store_edit_piece(REC_SAMPLER, 7, 0, 2, a + 10, 40) == STORE_ERR_STATE &&
          store_edit_piece(REC_DX7, 7, STORE_PC_HEAD, 1, a, 10) == STORE_ERR_STATE && !store_edit_count(), "nor a piece of one");
    CHECK(store_edit_del(REC_SAMPLER, 7, 0) == STORE_ERR_STATE && store_edit_del(REC_AUDIO_OLD, 7, 0) == STORE_ERR_STATE &&
          store_edit_put(REC_AUDIO_OLD, 7, 1, 1, a, 100) == STORE_ERR_STATE && !store_edit_count(),
          "the device deletes neither the old sampler nor its audio, nor writes old audio");
    CHECK(store_save_begin() == STORE_OK && store_save_put(REC_DX7, 7, 0, 1, a, 10) == STORE_ERR_STATE, "the save API: refused too");
    store_save_cancel();
    CHECK(store_list(REC_SAMPLER, nums, 16) == 2 && nums[0] == 7 && nums[1] == 9, "listed by the store as sampler records (kept)");
    /* compaction moves the old sampler as it is; the audio never moves */
    for (int i = 0; i < 1500; i++) save_one(REC_PATTERN, i % 40, i + 1);
    store_init();
    const uint8_t *p = store_get(REC_SAMPLER, 7, 0, &l, 0);
    uint8_t ver = 0;
    store_get(REC_SAMPLER, 7, 0, 0, &ver);
    CHECK(p && l == sizeof old && ver == 1 && !memcmp(p, old, l) && store_get(REC_AUDIO_OLD, 7, 0, &l, 0) == &fake_flash[STORE_U1_START + 16] &&
          store_get(REC_AUDIO_OLD, 7, 1, &l, 0) && l == 900, "after 1,500 saves: the old sampler whole, version 1; its audio where it was");
    CHECK(rec_cart_taken(7) && store_edit_put(REC_JUNO, 7, 0, 1, a, 10) == STORE_ERR_STATE, "its number still taken");
    CHECK(rec_cart_taken(9) && store_edit_put(REC_DX7, 9, 0, 1, a, 10) == STORE_ERR_STATE &&
          store_edit_del(REC_SAMPLER, 9, 0) == STORE_ERR_STATE && store_get(REC_SAMPLER, 9, 0, &l, &ver) && l == 50 && ver == 3,
          "a sampler of version 3 (a newer firmware's): kept, its number taken, never deleted");
}

/* CHECK STORAGE's entry check (specs/ui/screens/settings.md): a sample whose entry and audio head differ (its coding or
 * its length) is listed, ENTRY AND AUDIO DIFFER, though its CRC checks; store_sample_ok tells the same. */
static void t_check_samples(void)
{
    fake_reset();
    static uint8_t s1[3000], s2[3000], s3[3000], s4[19];
    uint32_t at = STORE_U1_START;
    sample_rec(s1, sizeof s1, 5000, 1, 5000, 1, 1);    /* agrees */
    sample_rec(s2, sizeof s2, 5000, 1, 4999, 1, 2);    /* lengths differ */
    sample_rec(s3, sizeof s3, 5000, 2, 5000, 1, 3);    /* codings differ */
    memcpy(s4, s1, sizeof s4);                          /* (too short for its audio's head) */
    put_raw(at, REC_SAMPLE, 0x21, 0, s1, sizeof s1, 5, 0); at += 16 + sizeof s1;
    put_raw(at, REC_SAMPLE, 0x23, 0, s2, sizeof s2, 5, 0); at += 16 + sizeof s2;
    put_raw(at, REC_SAMPLE, 0x24, 0, s3, sizeof s3, 5, 0); at += 16 + sizeof s3;
    put_raw(at, REC_SAMPLE, 0x25, 0, s4, sizeof s4, 5, 1);
    store_init();
    CHECK(store_sample_ok(s1, sizeof s1) && !store_sample_ok(s2, sizeof s2) && !store_sample_ok(s3, sizeof s3) &&
          !store_sample_ok(s4, sizeof s4), "store_sample_ok: the entry against the audio's head");
    store_problem_t pr[8];
    int n = store_check(pr, 8), d23 = 0, d24 = 0, d25 = 0, other = 0;
    for (int i = 0; i < n && i < 8; i++) {
        if (pr[i].kind == REC_SAMPLE && pr[i].what == STORE_DIFFER && !pr[i].sub) {
            d23 += pr[i].num == 0x23; d24 += pr[i].num == 0x24; d25 += pr[i].num == 0x25;
        } else other++;
    }
    CHECK(n == 3 && d23 == 1 && d24 == 1 && d25 == 1 && !other, "CHECK STORAGE: samples 23, 24 and 25 listed, entry and audio differ (%d)", n);
    /* one damaged where it is read: listed once, DAMAGED (not twice) */
    fake_flash[STORE_U1_START + 16 + sizeof s1 + 16 + 100] ^= 0x40;
    n = store_check(pr, 8);
    int dam = 0;
    for (int i = 0; i < n && i < 8; i++) dam += pr[i].num == 0x23;
    CHECK(n == 3 && dam == 1, "sample 23 damaged: listed once (%d, %d)", n, dam);
    /* the old form's audio is never read: never listed */
    fake_reset();
    put_raw(STORE_U1_START, REC_AUDIO_OLD, 0x0C, 0, s1, 2000, 5, 1);
    store_init();
    fake_flash[STORE_U1_START + 16 + 10] ^= 1;
    CHECK(store_check(pr, 8) == 0, "old audio damaged: not listed (this firmware never reads it)");
}

/* Seeding's LAYOUT (specs/files/blob.md#seeding; its bytes: fw/web/seed.js): bytes 6-7 the records the store can
 * list (its map's room, 560); the accepted pairs the new form's, sampler version 2 and samples, not the old form's */
static uint8_t lay[16384];
static uint32_t lay_n, lay_total;
static void lay_catch(const uint8_t *m, uint32_t n)
{
    static uint8_t d[1024];
    uint32_t k = 0;
    if (n < 6 || m[4] != 0x02) return;                  /* (LAYOUT messages only) */
    for (uint32_t i = 5; i + 1 < n; ) {                 /* 7 bytes in 8 */
        uint8_t top = m[i++];
        for (uint32_t j = 0; j < 7 && i + 1 < n; j++, i++) d[k++] = (uint8_t)(m[i] | (top >> j & 1) << 7);
    }
    if (k < 8) return;
    uint32_t total = d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24, off = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
    if (off + k - 8 <= sizeof lay) memcpy(lay + off, d + 8, k - 8);
    lay_total = total;
    lay_n += k - 8;
}
static void t_layout(void)
{
    fake_reset();
    store_edit_revert();                                /* (no edit left by a test before: the device would refuse) */
    store_init();
    seed_init();
    static const uint8_t hello[] = { 0xF0, 0x7D, 0x50, 0x54, 0x01, 0x00, 0x01, 0xF7 };
    lay_n = lay_total = 0;
    fake_sysex = lay_catch;
    seed_sysex(hello, sizeof hello);
    for (int i = 0; i < 16; i++) seed_poll();
    fake_sysex = 0;
    CHECK(lay_total && lay_n == lay_total && !memcmp(lay, "PTLY", 4), "the LAYOUT: %u of %u bytes", lay_n, lay_total);
    CHECK((lay[6] | lay[7] << 8) == STORE_MAP_MAX && STORE_MAP_MAX == 560, "its bytes 6-7: the map's room, %u", lay[6] | lay[7] << 8);
    uint32_t nreg = lay[20], nacc = lay[21], o = 36 + 12 * nreg;
    int s2 = 0, smp = 0, s1 = 0, old = 0;
    for (uint32_t i = 0; i < nacc; i++, o += 2) {
        s2 += lay[o] == REC_SAMPLER && lay[o + 1] == 2; s1 += lay[o] == REC_SAMPLER && lay[o + 1] == 1;
        smp += lay[o] == REC_SAMPLE && lay[o + 1] == 1; old += lay[o] == REC_AUDIO_OLD;
    }
    CHECK(nacc == 14 && s2 && smp && !s1 && !old, "it takes samplers of version 2 and samples, not the old form (%u pairs)", nacc);
}

/* A deleted object never comes back, whatever compaction does. */
static void t_deleted_stays(void)
{
    fake_reset();
    store_init();
    for (int i = 0; i < 10; i++) save_one(REC_SONG, i, 1);
    CHECK(store_save_begin() == 0 && store_save_del(REC_SONG, 3, 0) == 0 && store_save_run() == 0, "a delete");
    for (int i = 0; i < 2000; i++) save_one(REC_PATTERN, i % 50, i + 1);
    store_init();
    CHECK(version_held(REC_SONG, 3) == -1, "song 3 stays deleted");
    for (int i = 0; i < 10; i++) if (i != 3) CHECK(version_held(REC_SONG, i) == 1, "song %d kept", i);
}

/* A sample deleted stays deleted: its audio never moves, so its delete must outlive the compactions while an older
 * copy is left in an audio block; the sample beside it and the sampler are kept. */
/* What counts as damaged (specs/files/store.md#power-on): what CHECK STORAGE names, a record failing its CRC that
 * mattered (the one in use, one newer than it, or one with none in use), and nothing else. A deleted sample keeps its
 * bytes in its blocks, free now: a save into its middle block leaves its header with a payload changed (older than its
 * deletion) and its tail at the next block's start (no record at all); neither counts, at the mount nor in CHECK
 * STORAGE. A record in use torn still counts, once, in both. */
static void t_bad_count(void)
{
    fake_reset();
    static uint8_t s4[2000], s5[10000], s6[3000], c[90];
    uint8_t p[64];
    for (int i = 0; i < 64; i++) p[i] = (uint8_t)(i * 5 + 1);
    sample_rec(s4, sizeof s4, 3000, 1, 3000, 1, 4);
    sample_rec(s5, sizeof s5, 15000, 1, 15000, 1, 5);
    sample_rec(s6, sizeof s6, 4000, 1, 4000, 1, 6);
    sampler_v2(c, 2, 1);
    uint32_t a4 = STORE_U1_START, a5 = a4 + 16 + sizeof s4, a6 = a5 + 16 + sizeof s5;   /* (an uploaded disk's audio run) */
    put_raw(a4, REC_SAMPLE, 4, 0, s4, sizeof s4, 5, 0);
    put_raw(a5, REC_SAMPLE, 5, 0, s5, sizeof s5, 5, 0);                 /* over three blocks: the first, the second, the third */
    put_raw(a6, REC_SAMPLE, 6, 0, s6, sizeof s6, 5, 0);
    put_raw_v(STORE_U1_START + 4 * STORE_BLOCK, REC_SAMPLER, 5, 0, 2, c, 90, 5, 1);
    CHECK(a5 >> 12 == STORE_U1_START >> 12 && (a6 - 1) >> 12 == (STORE_U1_START >> 12) + 2, "sample 05 over three blocks");
    put_raw(STORE_U1_START + 5 * STORE_BLOCK, REC_SAMPLE, 5, 0, p, 0, 6, 2 | 1);   /* sample 05 deleted */
    uint32_t mid = STORE_U1_START + STORE_BLOCK;                        /* then a save into its middle block (free now) */
    memset(&fake_flash[mid], 0xFF, STORE_BLOCK);
    put_raw(mid, REC_SONG, 9, 0, p, 64, 7, 1);
    store_init();
    store_usage_t u;
    store_usage(&u);
    store_problem_t pr[8];
    int n = store_check(pr, 8);
    CHECK(store_get(REC_SAMPLE, 4, 0, 0, 0) && store_get(REC_SAMPLE, 6, 0, 0, 0) && !store_get(REC_SAMPLE, 5, 0, 0, 0) &&
          store_get(REC_SONG, 9, 0, 0, 0), "samples 04 and 06 and song 09 there, 05 deleted");
    CHECK(u.bad == 0 && n == 0, "what a deleted sample leaves counts as nothing damaged: the mount %u, CHECK STORAGE %d", u.bad, n);
    /* a record in use torn (a song the device saves, then a payload bit gone): counted once, at the mount and in CHECK
     * STORAGE alike */
    CHECK(save_one(REC_SONG, 10, 1) == 0 && version_held(REC_SONG, 10) == 1, "song 10 saved on the device");
    store_rec_t r;
    store_info(REC_SONG, 10, 0, &r);
    fake_flash[r.addr + STORE_HDR + 3] ^= 0x20;
    store_init();
    store_usage(&u);
    n = store_check(pr, 8);
    CHECK(u.bad == 1 && n == 1 && pr[0].kind == REC_SONG && pr[0].num == 10, "a torn record: the mount %u, CHECK STORAGE %d (%02X %02X)",
          u.bad, n, n ? pr[0].kind : 0, n ? pr[0].num : 0);
    /* song 10 saved again: the torn copy is older than the one in use now, and costs nothing */
    CHECK(save_one(REC_SONG, 10, 2) == 0 && version_held(REC_SONG, 10) == 2, "song 10 saved again");
    store_init();
    store_usage(&u);
    CHECK(u.bad == 0 && store_check(pr, 8) == 0, "the torn copy older than the one in use: nothing counted (%u)", u.bad);
}

static void t_audio_delete_stays(void)
{
    fake_reset();
    static uint8_t a[6000], c[90];
    uint32_t len;
    sample_rec(a, sizeof a, 11000, 1, 11000, 1, 9);
    sampler_v2(c, 2, 1);
    put_raw(STORE_U1_START, REC_SAMPLE, 5, 0, a, 6000, 5, 0);         /* (as an uploaded disk lays them: t_audio) */
    put_raw(STORE_U1_START + 16 + 6000, REC_SAMPLE, 6, 0, a, 3000, 5, 0);
    put_raw_v(STORE_U1_START + 3 * 4096, REC_SAMPLER, 5, 0, 2, c, 90, 5, 1);
    store_init();
    CHECK(store_get(REC_SAMPLE, 5, 0, 0, 0) && store_get(REC_SAMPLE, 6, 0, 0, 0), "a sampler and two samples");
    CHECK(store_edit_del(REC_SAMPLE, 5, 0) == 0 && edits_saved() == 0, "sample 05 deleted");
    for (int i = 0; i < 2000; i++) save_one(REC_PATTERN, i % 50, i + 1);
    store_init();
    CHECK(!store_get(REC_SAMPLE, 5, 0, 0, 0), "the deleted sample stays deleted through the compactions");
    const uint8_t *p = store_get(REC_SAMPLE, 6, 0, &len, 0);
    CHECK(p && len == 3000 && !memcmp(p, a, 3000) && store_get(REC_SAMPLER, 5, 0, 0, 0), "the other sample and the sampler kept");
}

/* An FM-1 written by a firmware that kept the user's things in FWS's free part (0x060000-0x092000 here, after the
 * fake firmware's end): those records count for nothing and are never read; the user area mounts as ever, and
 * nothing of the store reads the flash below the user area (specs/files/store.md#power-on). What such a move left in
 * the user area too: under the move's save number a deleted record for each thing moved, and its older copies spoilt
 * (kind 0), or, a move cut short while spoiling, one still whole. They hide those things for good; compaction lets
 * the deleted records go (no older copy left that the user area could show). */
static int entries_of(uint8_t kind, uint8_t num)       /* the map's entries for a thing, deleted ones too */
{
    store_rec_t r;
    int n = 0;
    for (int i = 0; store_entry(i, &r, 0, 0); i++) n += r.kind == kind && r.num == num;
    return n;
}
static void t_old_store(void)
{
    fake_reset();
    uint8_t p[64];
    static uint8_t b[4096];
    for (int i = 0; i < 64; i++) p[i] = (uint8_t)i;
    int cells[16] = { 5, 3 };
    uint32_t lp = pat(b, 2, cells, 1), at = STORE_U1_START + STORE_BLOCK;
    put_raw(0x085000, REC_SONG, 7, 0, p, 64, 3, 0);                    /* moved there by the old firmware */
    put_raw(0x086000, REC_PATTERN, 0x30, 0, b, lp, 4, 0);
    put_raw(0x087000, REC_AUDIO_OLD, 2, 0, p, 64, 4, 0);
    uint32_t x = 99;                                                    /* and an install's padding between them */
    for (uint32_t a = 0x060000; a < 0x085000; a++) { x = x * 1103515245u + 12345u; fake_flash[a] = (uint8_t)(x >> 16); }
    put_raw(STORE_U1_START, REC_SONG, 2, 0, p, 64, 5, 1);               /* the user area's */
    put_raw(at, REC_SONG, 7, 0, p, 64, 1, 1);                           /* song 07 before its move, spoilt */
    fake_flash[at] = 0;
    at += 16 + 64;
    put_raw(at, REC_PATTERN, 0x30, 0, b, lp, 2, 1);                     /* pattern 30 before its move, left whole */
    at += 16 + ((lp + 3) & ~3u);
    put_raw(at, REC_SONG, 7, 0, p, 0, 3, 2 | 1);                        /* the moves' deleted records */
    put_raw(at + 16, REC_PATTERN, 0x30, 0, p, 0, 4, 2);
    put_raw(at + 32, REC_AUDIO_OLD, 2, 0, p, 0, 4, 2 | 1);
    fake_low_reads = 0;
    store_init();
    uint8_t nums[8];
    store_usage_t u;
    store_usage(&u);
    CHECK(!store_get(REC_SONG, 7, 0, 0, 0) && !store_get(REC_PATTERN, 0x30, 0, 0, 0) && !store_get(REC_AUDIO_OLD, 2, 0, 0, 0),
          "FWS's free part: nothing found there, nor the older copies the user area has");
    CHECK(store_list(REC_SONG, nums, 8) == 1 && nums[0] == 2, "the songs: the user area's alone");
    CHECK(store_list(REC_PATTERN, nums, 8) == 1 && nums[0] == 0xF0, "the patterns: the built-in one alone");
    int live = 0;
    for (int k = 0; k < STORE_NKINDS; k++) live += u.count[k];
    CHECK(store_count() == 4 && live == 1 && u.bad == 0, "one record mounted and the moves' 3 deletes, none bad: %d, %d, %u",
          store_count(), live, u.bad);
    store_problem_t pr[4];
    CHECK(store_check(pr, 4) == 0, "CHECK STORAGE: nothing wrong");
    CHECK(fake_low_reads == 0, "nothing read below the user area: %ld reads", fake_low_reads);
    CHECK(save_one(REC_SONG, 7, 2) == 0 && version_held(REC_SONG, 7) == 2, "a song 07 saved anew: in the user area");
    store_init();
    CHECK(version_held(REC_SONG, 7) == 2 && fake_low_reads == 0, "after a power-cycle too, nothing read below it");
    int obj = 0x40;
    while (save_big(obj) == STORE_OK && obj < 0xE0) obj++;             /* (the flash filled: compaction) */
    CHECK(!entries_of(REC_PATTERN, 0x30) && !entries_of(REC_AUDIO_OLD, 2),
          "compaction: the moves' deletes let go, their block freed (an audio one among them too)");
    store_init();
    uint32_t len;
    CHECK(!store_get(REC_PATTERN, 0x30, 0, 0, 0) && !store_get(REC_AUDIO_OLD, 2, 0, 0, 0) && version_held(REC_SONG, 7) == 2 &&
          store_get(REC_SONG, 2, 0, &len, 0) && len == 64, "after a power-cycle: nothing back");
}

/* The EDITOR's own pattern FE (specs/files/store.md#the-room-for-the-editors-pattern): 8 blocks kept for it on top
 * of compaction's 2. Any other save that would leave fewer than 10 free is refused; FE's own may use the 8; a full
 * FE rewritten with only 2 free asks as storage full, its edit kept; deletes make room and are let through. */
static int put_full_pattern(uint8_t num, int seed)     /* 16 tracks of 128 cells of 15 B, as pieces: ~30 KB */
{
    static uint8_t t[128 * 15 + 1];
    uint8_t head[3] = { 16, 127, 1 };
    int r = store_edit_piece(REC_PATTERN, num, STORE_PC_HEAD, 1, head, 3);
    for (int k = 0; k < 16 && !r; k++) {
        uint32_t at = 0;
        for (int c = 0; c < 128; c++) {                 /* a note, an instrument, a volume, four commands */
            t[at++] = 0xF8; t[at++] = (uint8_t)(36 + (k + c + seed) % 60);
            t[at++] = 1; t[at++] = (uint8_t)c; t[at++] = 0x40;
            t[at++] = 0xFF; t[at++] = 4;
            for (int m = 0; m < 4; m++) { t[at++] = (uint8_t)(0x10 + m); t[at++] = (uint8_t)(c + m + seed); }
        }
        t[at++] = 0;
        r = store_edit_piece(REC_PATTERN, num, (uint16_t)k, 1, t, at);
    }
    return r;
}
static int free_blocks(void) { store_usage_t u; store_usage(&u); return u.user_free_blocks; }
static void t_editor_room(void)
{
    fake_reset();
    store_init();
    CHECK(put_full_pattern(0x20, 1) == 0 && edits_saved() == STORE_OK, "a full pattern 20 saved (8 blocks)");
    int obj = 0;
    while (save_big(obj) == STORE_OK && obj < 200) obj++;
    CHECK(free_blocks() == STORE_MIN_FREE + STORE_EDITOR_ROOM, "filled to 10 free blocks: %d (%d songs)", free_blocks(), obj);
    static uint8_t b[4096];
    int cells[16] = { 125, 125, 125, 125, 125, 125, 125, 125 };
    CHECK(store_edit_put(REC_PATTERN, 0x10, 0, 1, b, pat_dense(b, 8, cells, 3)) == 0, "a pattern of ~4 KB edited");
    CHECK(edits_saved() == STORE_ERR_FULL && store_edit_count() == 1, "its SAVE refused, the edit kept: 10 free are the room");
    store_edit_forget(REC_PATTERN, 0x10, 0);
    CHECK(put_full_pattern(0xFE, 2) == 0 && edits_saved() == STORE_OK, "the EDITOR's pattern at its largest: saved");
    CHECK(free_blocks() == STORE_MIN_FREE, "it took the room: %d free", free_blocks());
    uint32_t len;
    CHECK(store_get(REC_PATTERN, 0xFE, 7, &len, 0) != 0, "in 8 parts");
    uint8_t one[3] = { 0x81, 60, 0 };                   /* track 00 a note: the whole pattern written again */
    const uint8_t *t0 = store_piece(REC_PATTERN, 0xFE, 0, 0);
    CHECK(!t0 && store_edit_piece(REC_PATTERN, 0xFE, 0, 1, one, sizeof one) == 0, "a note of FE changed");
    CHECK(edits_saved() == STORE_ERR_FULL && store_edit_count() == 1, "its SAVE asks as storage full, the edit kept");
    /* pattern 20 deleted, that save first: deletes are let through, and free its 8 blocks */
    CHECK(store_save_begin() == 0, "begin");
    for (int s = 0; s < 8; s++) store_save_del(REC_PATTERN, 0x20, (uint8_t)s);
    CHECK(store_save_run() == STORE_OK, "pattern 20's deletes saved with 2 free");
    CHECK(edits_saved() == STORE_OK && store_edit_count() == 0, "then FE's SAVE goes through");
    store_init();
    const uint8_t *q = store_get(REC_PATTERN, 0xFE, 0, &len, 0);
    CHECK(q && q[0] == 16 && q[5] == 0x81 && q[6] == 60, "after a power-cycle: FE as saved");
    CHECK(free_blocks() == STORE_MIN_FREE + STORE_EDITOR_ROOM && save_big(obj) == STORE_ERR_FULL,
          "the old copy's blocks free again: 10, the room, so a song is refused: %d", free_blocks());
}

/* The same corner as the UI meets it: FE at its largest, 2 free, FE changed again; a pattern deleted as an edit
 * (DELETE, CLEAN UP), then one SAVE. The deletion is saved first, on its own, then the rest once compaction has
 * freed its blocks (store.md#the-room-for-the-editors-pattern: "once something is deleted SAVE writes them"). A
 * pattern MOVEd (put at a new number, deleted at the old) is never split so: refused, nothing written. */
static void t_editor_room_delete(void)
{
    fake_reset();
    store_init();
    CHECK(put_full_pattern(0x20, 1) == 0 && edits_saved() == STORE_OK, "split: a full pattern 20 saved");
    int obj = 0;
    while (save_big(obj) == STORE_OK && obj < 200) obj++;
    CHECK(put_full_pattern(0xFE, 2) == 0 && edits_saved() == STORE_OK && free_blocks() == STORE_MIN_FREE, "split: FE saved full, 2 free");
    uint8_t one[3] = { 0x81, 61, 0 };
    CHECK(store_edit_piece(REC_PATTERN, 0xFE, 0, 1, one, sizeof one) == 0, "split: a note of FE changed");
    static uint8_t b[4096];
    int cells[16] = { 4 };
    uint32_t n21 = pat(b, 1, cells, 1);
    /* a MOVE: 20's copy put at 21, 20 deleted: one save or none */
    CHECK(store_edit_put(REC_PATTERN, 0x21, 0, 1, b, n21) == 0 && store_edit_del(REC_PATTERN, 0x20, 0) == 0, "split: a MOVE edited");
    CHECK(edits_saved() == STORE_ERR_FULL && store_edit_count() == 3, "split: a MOVE's delete never goes first: refused, all kept");
    uint32_t len;
    store_edit_revert();                                /* power-cycle: the edits in RAM gone */
    store_init();
    CHECK(store_get(REC_PATTERN, 0x20, 0, &len, 0) && !store_get(REC_PATTERN, 0x21, 0, &len, 0), "split: after a power-cycle, 20 as saved, no 21");
    /* a plain DELETE: saved first, then FE */
    CHECK(store_edit_piece(REC_PATTERN, 0xFE, 0, 1, one, sizeof one) == 0 && store_edit_del(REC_PATTERN, 0x20, 0) == 0,
          "split: FE changed, pattern 20 deleted (edits)");
    CHECK(edits_saved() == STORE_OK && store_edit_count() == 0, "split: one SAVE writes both (the deletion first)");
    store_edit_revert();
    store_init();
    const uint8_t *q = store_get(REC_PATTERN, 0xFE, 0, &len, 0);
    CHECK(q && q[5] == 0x81 && q[6] == 61 && !store_get(REC_PATTERN, 0x20, 0, &len, 0), "split: after a power-cycle: FE as saved, 20 gone");
}

/* STATUS' USER FLASH MAP (specs/ui/screens/status.md): the user area KB by KB, what each holds. Checked against the
 * map's entries KB by KB (the kind with most live bytes), free blocks FREE, the open block's tail OPEN, and an
 * older copy's KB in a block still in use taking its block's kind, not FREE. SAMPLES are the samples' (0x11) alone:
 * the old form's audio (0x10) is OTHER. */
static int kb_kind(uint8_t kind)
{
    return kind == REC_SAMPLE ? STORE_KB_SAMPLES : kind <= REC_CART_LAST ? STORE_KB_INSTRUMENTS : kind == REC_PATTERN ?
           STORE_KB_PATTERNS : kind == REC_SONG ? STORE_KB_SONGS : kind == REC_LOOPS ? STORE_KB_LOOPS : STORE_KB_OTHER;
}
static int kb_index(uint32_t a)                 /* a user-area address's KB in the map's order, -1 outside */
{
    if (a >= STORE_U1_START && a < STORE_U1_END) return (int)((a - STORE_U1_START) >> 10);
    if (a >= STORE_U2_START && a < STORE_U2_END) return (int)(((STORE_U1_END - STORE_U1_START) + (a - STORE_U2_START)) >> 10);
    return -1;
}
static void t_kb_map(void)
{
    static uint8_t kb[512], live[512];
    static uint32_t by[512][8];
    fake_reset();
    store_init();
    int n = store_kb_map(kb, 512);
    int nuser = (int)(((STORE_U1_END - STORE_U1_START) + (STORE_U2_END - STORE_U2_START)) >> 10), all_free = 1;
    for (int i = 0; i < n; i++) if (kb[i] != STORE_KB_FREE) all_free = 0;
    CHECK(n == nuser && all_free, "kb map: an empty user area, %d KB all free", n);
    static uint8_t b[6000];
    int cells[16] = { 40, 30 };
    memset(b, 7, sizeof b);
    int r0 = store_save_begin(), r1 = store_save_put(REC_SAMPLE, 3, 0, 1, b, 4000), r2 = store_save_put(REC_DX7, 3, 0, 1, b, 3000),
        r3 = store_save_put(REC_SONG, 2, 0, 1, b, 200), r4 = store_save_put(REC_LOOPS, 0, 0, 3, b, 500),
        r5 = store_save_put(REC_SETTINGS, 0, 0, 1, b, 128), r7 = store_save_put(REC_AUDIO_OLD, 3, 0, 1, b, 4000),
        r6 = store_save_run();
    CHECK(!r0 && !r1 && !r2 && !r3 && !r4 && !r5 && !r7 && r6 == STORE_OK,
          "kb map: a sample, a cartridge, a song, the loops, the settings, old audio saved (%d %d %d %d %d %d %d %d)", r0, r1, r2,
          r3, r4, r5, r7, r6);
    uint32_t pl = pat(b, 2, cells, 1);
    CHECK(store_save_begin() == 0 && store_save_put(REC_PATTERN, 5, 0, 1, b, pl) == 0 && store_save_run() == STORE_OK,
          "kb map: a pattern saved");
    pl = pat(b, 2, cells, 2);
    CHECK(store_save_begin() == 0 && store_save_put(REC_PATTERN, 5, 0, 1, b, pl) == 0 && store_save_run() == STORE_OK,
          "kb map: the pattern saved again (its older copy dead)");
    memset(by, 0, sizeof by);
    memset(live, 0, sizeof live);
    for (int i = 0; i < store_count(); i++) {
        store_rec_t r;
        uint32_t sv, crc;
        if (!store_entry(i, &r, &sv, &crc)) continue;
        uint32_t a = r.addr, end = a + STORE_HDR + (((r.deleted ? 0 : r.len) + 3u) & ~3u);
        for (; a < end; a = (a | 1023u) + 1) {
            int k = kb_index(a);
            uint32_t ke = (a | 1023u) + 1, nb = (end < ke ? end : ke) - a;
            if (k >= 0) { by[k][r.deleted ? STORE_KB_OTHER : kb_kind(r.kind)] += nb; live[k] = 1; }
        }
    }
    n = store_kb_map(kb, 512);
    int wrong = 0, used = 0, open = 0, freeb = 0, older = 0;
    for (int i = 0; i < n; i++) {
        if (live[i]) {
            int best = 0;
            for (int k = 1; k < 8; k++) if (by[i][k] > by[i][best]) best = k;
            if (kb[i] != best) wrong++;
            used++;
        } else if (kb[i] == STORE_KB_OPEN) open++;
        else if (kb[i] == STORE_KB_FREE) freeb++;
        else older++;                                   /* (no live byte, not free: an older copy's) */
    }
    CHECK(!wrong && used >= 9, "kb map: each KB of live records the kind with most bytes in it (%d KB, %d wrong)", used, wrong);
    CHECK(open > 0 && freeb % 4 == 0 && freeb + open + used + older == n, "kb map: the open block's tail OPEN (%d), whole blocks FREE (%d)", open, freeb);
    store_usage_t u;
    store_usage(&u);
    CHECK(freeb == u.user_free_blocks * 4, "kb map: FREE squares are the free blocks: %d KB, %d blocks", freeb, u.user_free_blocks);
    int k5 = 0, ks = 0;
    for (int i = 0; i < n; i++) { if (kb[i] == STORE_KB_PATTERNS) k5++; if (kb[i] == STORE_KB_SAMPLES) ks++; }
    CHECK(older == 0 || k5 > 0, "kb map: an older copy's KB takes its block's kind (%d such)", older);
    CHECK(ks == 4, "kb map: the sample's 4 KB SAMPLES, the old audio's not (%d)", ks);
}

/* That SAVE cut by a power cut at every flash operation, plain and torn: before it, the deletion alone, or both;
 * never FE's change without the deletion, and never a part of either */
static uint8_t split_base[1u << 20];
static int fe_note(void)                        /* track 00's first note of FE as the flash holds it (-1: none) */
{
    uint32_t len;
    const uint8_t *q = store_get(REC_PATTERN, 0xFE, 0, &len, 0);
    return q && len > 6 && q[5] == 0x81 ? q[6] : -1;
}
static void t_split_cuts(void)
{
    fake_reset();
    store_init();
    put_full_pattern(0x20, 1);
    edits_saved();
    int obj = 0;
    while (save_big(obj) == STORE_OK && obj < 200) obj++;
    put_full_pattern(0xFE, 2);
    edits_saved();
    memcpy(split_base, fake_flash, sizeof split_base);
    int note0 = fe_note();
    static const uint8_t one[3] = { 0x81, 61, 0 };
    store_edit_piece(REC_PATTERN, 0xFE, 0, 1, one, sizeof one);
    store_edit_del(REC_PATTERN, 0x20, 0);
    fake_ops = 0;
    CHECK(edits_saved() == STORE_OK, "split cuts: the SAVE to cut");
    long total = fake_ops;
    static int torn, before, mid, after, bad;
    static long k;                                      /* static: kept across the longjmp */
    uint32_t len;
    before = mid = after = bad = 0;
    for (torn = 0; torn < 2; torn++)
        for (k = 0; k <= total; k++) {
            memcpy(fake_flash, split_base, sizeof split_base);
            store_init();
            store_edit_revert();
            store_edit_piece(REC_PATTERN, 0xFE, 0, 1, one, sizeof one);
            store_edit_del(REC_PATTERN, 0x20, 0);
            fake_ops = 0; cut_at = k; cut_torn = torn;
            if (!setjmp(cut_env)) edits_saved();
            cut_at = -1;
            store_init();                               /* power on: the edits in RAM gone */
            store_edit_revert();
            int has20 = store_get(REC_PATTERN, 0x20, 0, &len, 0) != 0, n = fe_note();
            if (has20 && n == note0) before++;
            else if (!has20 && n == note0) mid++;
            else if (!has20 && n == 61) after++;
            else { bad++; CHECK(0, "split cut at %ld%s: a mix (20 %d, FE's note %d)", k, torn ? " (torn)" : "", has20, n); }
        }
    printf("  split cuts: %ld operations x 2, before %d, the deletion alone %d, both %d\n", total + 1, before, mid, after);
    CHECK(!bad && before && mid && after, "split cuts: %ld operations x 2: before %d, the deletion alone %d, both %d", total + 1, before, mid, after);
}

/* The spare zone (fw/core/spare.h; specs/files/ram.md#the-edit-memory-and-the-spare-zone): one borrow at a time
 * from its top, never refused while it fits. */
static void t_spare(void)
{
    uint8_t *z = spare_borrow(13116);
    CHECK(z != 0 && spare_size() >= SPARE_OWN + 8192u, "DJ's 13,116 B had (the zone %u B)", spare_size());
    CHECK(z == spare_zone() + spare_size() - ((13116u + 3u) & ~3u), "at the zone's very top");
    CHECK(!spare_borrow(64), "one borrow at a time");
    spare_return();
    CHECK(spare_borrow(spare_size()) != 0, "the whole zone once given back");
    spare_return();
    CHECK(!spare_borrow(spare_size() + 4), "more than the zone: refused");
}

/* Compacting a block of many deleted records takes one walk, not one a record: the watchdog (~8 s on the
 * FM-1, fed by plat_idle at every block a walk enters) never starves; and none of them comes back. */
static void t_compact_deletes(void)
{
    fake_reset();
    store_init();
    static uint8_t small[20];
    store_save_begin();
    for (int i = 0; i < 70; i++) { small[0] = (uint8_t)i; store_save_put(REC_SONG, (uint8_t)i, 0, 1, small, 20); }
    CHECK(store_save_run() == 0, "70 songs");
    store_save_begin();
    for (int i = 0; i < 70; i++) store_save_put(REC_SONG, (uint8_t)i, 0, 1, small, 20);
    CHECK(store_save_run() == 0, "70 songs again (older copies left behind)");
    store_save_begin();
    for (int i = 0; i < 70; i++) store_save_del(REC_SONG, (uint8_t)i, 0);
    CHECK(store_save_run() == 0, "70 deletes, in a block of their own");
    long most = 0;
    int obj = 0, r;
    do {                                                /* the flash filled: compaction takes the deletes' block first */
        long before = fake_idles;
        r = save_big(100 + obj++);
        if (fake_idles - before > most) most = fake_idles - before;
    } while (r == STORE_OK && obj < 150);
    long walk = 103;                                    /* (the user area's blocks) */
    CHECK(most > walk / 2, "a compaction walked (%ld blocks entered)", most);
    CHECK(most <= 3 * walk, "at most a few walks in one save: %ld blocks entered (a walk is %ld)", most, walk);
    store_init();
    int back = 0;
    for (int i = 0; i < 70; i++) if (store_get(REC_SONG, (uint8_t)i, 0, 0, 0)) back++;
    CHECK(back == 0, "no deleted song came back: %d", back);
    printf("  70 deletes compacted: %ld blocks entered in the busiest save (a walk: %ld)\n", most, walk);
}

/* The map holds a record only once all its bytes are programmed, the locks balance, and a borrow never
 * moves the edits. */
static int map_sound(void)                      /* every entry's record in the flash, whole */
{
    for (int i = 0; i < store_count(); i++) {
        store_rec_t r;
        store_entry(i, &r, 0, 0);
        uint8_t h[16];
        if (plat_flash_read(r.addr, h, 16)) return 0;
        uint32_t c = crc32_update(0, h, 12);
        if (r.len) c = store_crc_flash(c, r.addr + 16, r.len);
        if (h[0] != r.kind || c != (uint32_t)(h[12] | h[13] << 8 | h[14] << 16 | (uint32_t)h[15] << 24)) return 0;
    }
    return 1;
}
static void t_map_after_flush(void)
{
    fake_reset();
    store_init();
    static uint8_t b[4096];
    for (int i = 0; i < 12; i++) {
        int cells[16] = { 3 + i, 0, 9, i };
        CHECK(store_edit_put(REC_PATTERN, (uint8_t)i, 0, 1, b, pat(b, 4, cells, i)) == STORE_OK, "pattern %d edited", i);
    }
    for (int i = 0; i < 30; i++) { uint8_t s[24] = { (uint8_t)i }; store_edit_put(REC_SONG, (uint8_t)i, 0, 1, s, 24); }
    fake_locks = 0;
    CHECK(store_edit_save() == 0, "SAVE");
    int sound = 1, steps = 0;
    while (store_save_status(0) == STORE_BUSY) {
        store_poll();
        steps++;
        if (!map_sound()) sound = 0;
    }
    CHECK(sound, "after every pass of the save (%d), the map's records all in the flash, whole", steps);
    CHECK(fake_locks > 0 && fake_lock_depth == 0, "the map changed under the lock, balanced: %ld, depth %d", fake_locks, fake_lock_depth);
    for (int i = 0; i < 12; i++) {
        int cells[16] = { 3 + i, 0, 9, i };
        uint32_t len, l2 = pat(b, 4, cells, i);
        uint8_t ver;
        const uint8_t *p = store_get(REC_PATTERN, (uint8_t)i, 0, &len, &ver);
        CHECK(p && len == l2 && !memcmp(p, b, l2) && store_edit_obj(REC_PATTERN, (uint8_t)i) == 0,
              "pattern %d saved as one part, its pieces gone", i);
    }
    /* the borrow: the spare zone, the edits never in its way */
    store_edit_revert();
    int k = 0;
    while (store_edit_put(REC_SONG, (uint8_t)(40 + k), 0, 1, b, 4000) == STORE_OK) k++;   /* the edit memory full */
    uint32_t mv = store_edit_moves();
    void *m = spare_borrow(23284);
    CHECK(m != 0 && store_edit_moves() == mv, "a borrow with the edit memory full (%d songs of 4 KB): had, nothing moved", k);
    spare_return();
    CHECK(fake_lock_depth == 0, "the locks balance");
    store_edit_revert();
}

/* Stepping past other firmwares' leftovers: a long run of 0xFF in a block looked over once, not at every
 * step (it was quadratic). */
static void t_ff_runs(void)
{
    fake_reset();
    for (uint32_t a = STORE_U1_START; a < STORE_U2_END; a += 4096) {
        if (a >= STORE_U1_END && a < STORE_U2_START) continue;
        for (int i = 0; i < 16; i++) fake_flash[a + i] = (uint8_t)(0x31 + i);     /* not a record */
        fake_flash[a + 4095] = 0x00;                    /* the rest 0xFF but its last byte */
    }
    clock_t t0 = clock();
    store_init();
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    store_usage_t u;
    store_usage(&u);
    CHECK(store_count() == 0 && u.user_free_blocks == 103, "the leftovers: nothing of PT/FM's, every block free");
    CHECK(ms < 250, "and the mount quick: %.0f ms", ms);
    printf("  leftovers with long runs of 0xFF: mounted in %.0f ms\n", ms);
}

/* The edit memory: changes in RAM read first, SAVE writes them as one save, REVERT drops them. */
static void t_edits(void)
{
    fake_reset();
    store_init();
    int objs[2] = { 1, 2 }, v1[2] = { 1, 1 };
    save_versions(REC_SONG, objs, v1, 2);
    static uint8_t b5[4096], b3[4096];
    fill(b5, 1, 5);
    fill(b3, 3, 1);
    CHECK(store_edit_put(REC_SONG, 1, 0, 1, b5, len_of(1, 5)) == 0, "an edit");
    CHECK(version_held(REC_SONG, 1) == 5, "the edit read first");
    store_rec_t r;
    CHECK(store_info(REC_SONG, 1, 0, &r) && r.area == STORE_AREA_EDIT && r.len == len_of(1, 5), "store_info: the edit");
    uint8_t part[8];
    CHECK(store_read(REC_SONG, 1, 0, 4, part, 8) == 0 && !memcmp(part, b5 + 4, 8), "store_read: the edit");
    CHECK(store_edit_put(REC_SONG, 3, 0, 1, b3, len_of(3, 1)) == 0, "a new song, edited");
    CHECK(store_edit_del(REC_SONG, 2, 0) == 0, "a delete, edited");
    CHECK(store_edit_del(REC_SONG, 4, 0) == 0 && store_edit_count() == 3, "deleting what is not there changes nothing");
    CHECK(version_held(REC_SONG, 2) == -1 && store_edit_has(REC_SONG, 2, 0) == 2, "song 2 gone in the edits");
    uint8_t nums[8];
    int n = store_list(REC_SONG, nums, 8);
    CHECK(n == 2 && nums[0] == 1 && nums[1] == 3, "the list with the edits: %d", n);
    {
        uint32_t el = 0;
        uint8_t ev, empty[10] = { 0 }, got[64];
        CHECK(store_edit_put(REC_DX7, 0xF0, 0, 1, empty, 10) == STORE_OK &&
              store_edit_build(REC_DX7, 0xF0, 0, got, sizeof got, &el, &ev) == 0 && el == 10,
              "a built-in instrument is edited as any (built with its edit)");
    }
    store_edit_revert();
    CHECK(store_edit_count() == 0 && version_held(REC_SONG, 1) == 1 && version_held(REC_SONG, 2) == 1 &&
          version_held(REC_SONG, 3) == -1, "REVERT: as saved");
    /* SAVE */
    store_edit_put(REC_SONG, 1, 0, 1, b5, len_of(1, 5));
    store_edit_put(REC_SONG, 3, 0, 1, b3, len_of(3, 1));
    store_edit_del(REC_SONG, 2, 0);
    CHECK(store_edit_save() == 0, "SAVE starts");
    uint8_t pc = 0;
    int st, steps = 0;
    while ((st = store_save_status(&pc)) == STORE_BUSY) { store_poll(); steps++; }
    CHECK(st == STORE_OK && steps > 0, "SAVE done after %d passes", steps);
    CHECK(store_edit_count() == 0, "nothing left unsaved");
    store_info(REC_SONG, 1, 0, &r);
    CHECK(r.area == STORE_AREA_USER && version_held(REC_SONG, 1) == 5 && version_held(REC_SONG, 3) == 1 &&
          version_held(REC_SONG, 2) == -1, "saved into the flash");
    store_init();
    CHECK(version_held(REC_SONG, 1) == 5 && version_held(REC_SONG, 3) == 1 && version_held(REC_SONG, 2) == -1,
          "saved, after a power-cycle");
    /* a pattern of two parts deleted: the thing whole, both parts gone from the list */
    {
        static uint8_t p0[4096];
        int cells[16] = { 2, 3 };
        uint32_t l0 = pat(p0, 2, cells, 0);
        store_save_begin(); store_save_put(REC_PATTERN, 7, 0, 1, p0, l0); store_save_put(REC_PATTERN, 7, 1, 1, p0, l0);
        store_save_run();
        n = store_list(REC_PATTERN, nums, 8);
        CHECK(n == 2 && nums[0] == 7, "a pattern of two parts listed");
        CHECK(store_edit_del(REC_PATTERN, 7, 1) == STORE_OK && store_edit_has(REC_PATTERN, 7, 0) == 2, "deleted (any part: the whole)");
        n = store_list(REC_PATTERN, nums, 8);
        CHECK(n == 1 && nums[0] == 0xF0, "deleted: not listed");
        CHECK(edits_saved() == STORE_OK && !store_info(REC_PATTERN, 7, 0, &r) && !store_info(REC_PATTERN, 7, 1, &r),
              "saved: both parts gone from the flash");
    }
    /* full, and the room back */
    static uint8_t big4[4000];
    int k = 0;
    while (store_edit_put(REC_SONG, (uint8_t)k, 0, 1, big4, sizeof big4) == 0) { k++; big4[0]++; }
    CHECK(k == (int)(STORE_EDIT_MAX / (8 + 4000)), "the edit memory holds %d of 4 KB", k);
    CHECK(store_edit_room() < 4008, "and says so: %u left", store_edit_room());
    store_edit_revert();
    /* a record put again and again, larger and smaller: its old rooms joined when room runs short */
    uint32_t mv = store_edit_moves();
    int bad = 0;
    for (int i = 0; i < 60; i++) {
        big4[0] = (uint8_t)i;
        if (store_edit_put(REC_SONG, (uint8_t)(i % 6), 0, 1, big4, i & 1 ? 4000 : 3000 + 100 * (uint32_t)(i % 7))) bad++;
    }
    CHECK(!bad && store_edit_count() == 6 && store_edit_moves() > mv, "60 puts of 6 songs, sizes changing: none refused (moves %u)",
          store_edit_moves() - mv);
    for (int i = 54; i < 60; i++) {
        uint32_t len;
        const uint8_t *p = store_get(REC_SONG, (uint8_t)(i % 6), 0, &len, 0);
        CHECK(p && p[0] == (uint8_t)i && len == (i & 1 ? 4000u : 3000u + 100u * (uint32_t)(i % 7)), "song %d as last put", i % 6);
    }
    store_edit_revert();
}

/* The edit memory's pieces (specs/files/ram.md#editing-and-saving): a pattern, a cartridge and LOOPS kept as the
 * pieces that differ from the flash; a change made again rewritten where it lies; back as saved, nothing unsaved;
 * every change under the lock and told to the watch; REVERT of one thing; the save of a pattern too big for one
 * part; room joined (squeeze); hold; an edit mode's copies. */
static const uint8_t ZERO[1] = { 0 };   /* an empty track's stream */
static int w_calls, w_unlocked, w_stage;
static uint32_t w_last, w_stage_len;
static const uint8_t *w_stage_at;               /* the stage as a reader told by the watch alone sees it */
static void watch_fn(uint8_t kind, uint8_t num, uint16_t id)
{
    w_calls++;
    if (fake_lock_depth <= 0) w_unlocked++;
    if (kind == STORE_STAGE) { w_stage++; w_stage_len = 0; w_stage_at = store_stage(&w_stage_len); }
    w_last = (uint32_t)kind << 16 | (uint32_t)num << 9 | id;
}
static void t_pieces(void)
{
    fake_reset();
    store_init();
    store_set_watch(watch_fn);
    static uint8_t a[8192], b[8192], got[4096];
    uint32_t l, la, lb;
    uint8_t v;
    int c1[16] = { 4, 6, 0, 3 }, c2[16] = { 4, 7, 0, 3 };
    la = pat(a, 4, c1, 0);
    CHECK(save_pat(5, a, la) == STORE_OK, "a pattern saved");
    /* a cell changed: one track's piece */
    lb = pat(b, 4, c2, 0);
    w_calls = w_unlocked = 0;
    CHECK(store_edit_put(REC_PATTERN, 5, 0, 1, b, lb) == STORE_OK && store_edit_count() == 1, "a cell added to track 1");
    const uint8_t *t1 = store_piece(REC_PATTERN, 5, 1, &l);
    CHECK(t1 && l == 7u * 2u + 1u && !store_piece(REC_PATTERN, 5, 0, 0) && !store_piece(REC_PATTERN, 5, 3, 0) &&
          !store_piece(REC_PATTERN, 5, STORE_PC_HEAD, 0), "only track 1 is a piece (%u B)", l);
    CHECK(w_calls == 1 && !w_unlocked && w_last == ((uint32_t)REC_PATTERN << 16 | 5u << 9 | 1u),
          "the watch told of it, under the lock (%d calls)", w_calls);
    uint32_t used = store_edit_used();
    CHECK(used == 16u + 8u, "it takes its room and its entry: %u B", used);
    b[5 + 4 * 2 + 1 + 3] ^= 1;                          /* (track 1's second note) */
    CHECK(store_edit_put(REC_PATTERN, 5, 0, 1, b, lb) == STORE_OK && store_edit_used() == used &&
          store_piece(REC_PATTERN, 5, 1, 0) == t1 && store_edit_count() == 1, "changed again: rewritten where it lies, nothing more");
    CHECK(store_edit_build(REC_PATTERN, 5, 0, got, sizeof got, &l, &v) == 0 && l == lb && !memcmp(got, b, lb) && v == 1,
          "built as put");
    CHECK(!memcmp(store_get(REC_PATTERN, 5, 0, &l, 0), a, la), "store_get: the flash's record under it");
    CHECK(store_edit_put(REC_PATTERN, 5, 0, 1, a, la) == STORE_OK && store_edit_count() == 0 && store_edit_used() == 0,
          "back as saved: nothing unsaved");
    /* the head when the size changes; one piece; REVERT of the thing */
    lb = pat(b, 6, c1, 0);
    CHECK(store_edit_put(REC_PATTERN, 5, 0, 1, b, lb) == STORE_OK && store_piece(REC_PATTERN, 5, STORE_PC_HEAD, &l) && l == 3 &&
          !store_piece(REC_PATTERN, 5, 0, 0), "6 tracks: only the head is a piece");
    uint8_t one[3] = { 0x81, 40, 0 };
    CHECK(store_edit_piece(REC_PATTERN, 5, 9, 1, one, 3) == STORE_OK && store_edit_build(REC_PATTERN, 5, 0, got, sizeof got, &l, &v) == 0 &&
          got[0] == 6 && (got[3] | got[4] << 8) == 0x0B, "a piece of track 9 alone: past the 6 tracks, not stored");
    store_edit_put(REC_SONG, 1, 0, 1, one, 3);
    CHECK(store_edit_count() == 2 && store_edit_forget(REC_PATTERN, 5, 0) == STORE_OK && store_edit_count() == 1 &&
          !store_edit_obj(REC_PATTERN, 5) && store_get(REC_SONG, 1, 0, 0, 0), "REVERT of the pattern: the song's edit stays");
    store_edit_revert();
    CHECK(w_unlocked == 0, "every change told under the lock");
    /* deleted, then made anew */
    CHECK(store_edit_del(REC_PATTERN, 5, 0) == STORE_OK && store_edit_has(REC_PATTERN, 5, 0) == 2 &&
          store_edit_build(REC_PATTERN, 5, 0, got, sizeof got, &l, &v) == -1, "deleted: nothing to build");
    lb = pat(b, 2, c2, 3);
    CHECK(store_edit_put(REC_PATTERN, 5, 0, 1, b, lb) == STORE_OK && store_edit_has(REC_PATTERN, 5, 0) == 1 &&
          store_edit_build(REC_PATTERN, 5, 0, got, sizeof got, &l, &v) == 0 && l == lb && !memcmp(got, b, lb),
          "made anew: none of the old one's tracks under it");
    CHECK(edits_saved() == STORE_OK && !memcmp(store_get(REC_PATTERN, 5, 0, &l, 0), b, lb) && l == lb && !store_edit_count(),
          "saved: the new one");
    /* a pattern too big for one part: put as pieces, saved as two parts; smaller again, the second part deleted */
    {
        int big[16];
        for (int t = 0; t < 16; t++) big[t] = 100;
        uint32_t lbig = pat_dense(b, 16, big, 1);       /* 16 streams of 401 B */
        CHECK(store_edit_put(REC_PATTERN, 9, 0, 1, b, lbig) == STORE_ERR_BIG, "over 4 KB as one record: refused");
        int ok = store_edit_piece(REC_PATTERN, 9, STORE_PC_HEAD, 1, b, 3) == STORE_OK;
        for (int t = 0; t < 16; t++) ok &= store_edit_piece(REC_PATTERN, 9, (uint16_t)t, 1, b + 5 + 401 * t, 401) == STORE_OK;
        CHECK(ok && store_edit_build(REC_PATTERN, 9, 0, got, sizeof got, &l, &v) == -2, "as pieces; too big to build as one");
        CHECK(edits_saved() == STORE_OK, "saved");
        for (int pass = 0; pass < 2; pass++) {
            const uint8_t *p0 = store_get(REC_PATTERN, 9, 0, &l, 0), *p1;
            uint32_t l1;
            p1 = store_get(REC_PATTERN, 9, 1, &l1, 0);
            CHECK(p0 && p1 && !store_get(REC_PATTERN, 9, 2, 0, 0) && p0[0] == 16 && (p0[3] | p0[4] << 8) == 0x03FF &&
                  (p1[3] | p1[4] << 8) == 0xFC00 && l == 5 + 401 * 10 && l1 == 5 + 401 * 6 &&
                  !memcmp(p0 + 5, b + 5, 401 * 10) && !memcmp(p1 + 5, b + 5 + 401 * 10, 401 * 6),
                  "%s: two parts, tracks 0-9 and 10-15, every stream as put", pass ? "after a power cycle" : "saved");
            store_init();
        }
        ok = 1;
        for (int t = 10; t < 16; t++) ok &= store_edit_piece(REC_PATTERN, 9, (uint16_t)t, 1, ZERO, 1) == STORE_OK;
        CHECK(ok && store_edit_count() == 1 && edits_saved() == STORE_OK && store_get(REC_PATTERN, 9, 0, &l, 0) &&
              l == 5 + 401 * 10 && !store_get(REC_PATTERN, 9, 1, 0, 0), "tracks 10-15 emptied: one part, the second deleted");
    }
    /* room joined: freed rooms between pieces moved down into when the top runs short */
    {
        static uint8_t s[3000];
        int k = 0;
        while (k < 30) {
            s[0] = (uint8_t)k;
            if (store_edit_put(REC_SONG, (uint8_t)(100 + k), 0, 1, s, sizeof s) != STORE_OK) break;
            k++;
        }
        for (int i = 0; i < k; i += 2) store_edit_forget(REC_SONG, (uint8_t)(100 + i), 0);
        uint32_t mv = store_edit_moves();
        int added = 0;
        for (int i = 0; i < k / 2; i++, added++) {
            s[0] = (uint8_t)(200 + i);
            if (store_edit_put(REC_SONG, (uint8_t)(150 + i), 0, 1, s, sizeof s) != STORE_OK) break;
        }
        int right = 1;
        for (int i = 1; i < k; i += 2) { const uint8_t *p = store_get(REC_SONG, (uint8_t)(100 + i), 0, &l, 0); right &= p && p[0] == i && l == 3000; }
        for (int i = 0; i < added; i++) { const uint8_t *p = store_get(REC_SONG, (uint8_t)(150 + i), 0, &l, 0); right &= p && p[0] == 200 + i; }
        CHECK(added == k / 2 && store_edit_moves() > mv && right && !w_unlocked,
              "%d songs of 3 KB, every other dropped, as many put again: room joined (%u moves), every one intact", k, store_edit_moves() - mv);
        store_edit_revert();
    }
    /* hold: rewrites go to the top, the old bytes left readable, nothing moves */
    {
        int cells[16] = { 5, 5 };
        la = pat(a, 2, cells, 0);
        save_pat(6, a, la);
        lb = pat(b, 2, cells, 9);
        store_edit_put(REC_PATTERN, 6, 0, 1, b, lb);
        const uint8_t *o0 = store_piece(REC_PATTERN, 6, 0, 0);
        uint8_t keep[11];
        memcpy(keep, o0, 11);
        CHECK(store_edit_reserve(64), "room reserved");
        uint32_t mv = store_edit_moves();
        store_edit_hold(1);
        lb = pat(b, 2, cells, 20);
        store_edit_put(REC_PATTERN, 6, 0, 1, b, lb);
        const uint8_t *n0 = store_piece(REC_PATTERN, 6, 0, 0);
        CHECK(n0 && n0 != o0 && !memcmp(o0, keep, 11), "held: track 0 rewritten at the top, its old bytes still there");
        store_edit_hold(0);
        CHECK(store_edit_moves() > mv, "the moves count it (a pointer taken before is stale)");
        store_edit_revert();
    }
    /* an edit mode's copies: HOME puts every piece back, EDIT keeps the changes */
    {
        int cells[16] = { 5, 5 };
        lb = pat(b, 2, cells, 30);
        store_edit_put(REC_PATTERN, 6, 0, 1, b, lb);
        store_edit_session(1);
        uint8_t s1[3] = { 1, 2, 3 };
        static uint8_t want[4096];
        uint32_t lw = pat(want, 2, cells, 30);
        lb = pat(b, 3, cells, 31);
        store_edit_put(REC_PATTERN, 6, 0, 1, b, lb);
        store_edit_put(REC_SONG, 3, 0, 1, s1, 3);
        CHECK(store_edit_count() == 2, "in the mode: two things changed");
        int dr = store_edit_session_drop(), n = store_edit_count();
        CHECK(dr == 1 && n == 1 && !store_get(REC_SONG, 3, 0, 0, 0), "HOME: the mode's changes dropped (%d, %d left)", dr, n);
        int br = store_edit_build(REC_PATTERN, 6, 0, got, sizeof got, &l, &v);
        CHECK(br == 0 && l == lw && !memcmp(got, want, lw), "... the pattern as before the mode, its earlier change kept "
              "(%d, %u B of %u, head %d %d)", br, l, lw, got[0], got[3]);
        store_edit_session(1);
        store_edit_put(REC_SONG, 3, 0, 1, s1, 3);
        store_edit_session(0);
        CHECK(store_edit_count() == 2 && store_get(REC_SONG, 3, 0, 0, 0) && store_edit_session_drop() == 0,
              "EDIT: the changes kept, no copies left");
        store_edit_revert();
    }
    /* a save under way: HOME's drop is refused, as every other change is (the save writes the changes it found) */
    {
        int cells[16] = { 5, 5 }, st;
        static uint8_t want[4096];
        store_edit_session(1);
        lb = pat(b, 2, cells, 40);
        store_edit_put(REC_PATTERN, 6, 0, 1, b, lb);
        uint32_t lw = pat(want, 2, cells, 40);
        int sr = store_edit_save(), dr = store_edit_session_drop();
        while ((st = store_save_status(0)) == STORE_BUSY) store_poll();
        int br = store_edit_build(REC_PATTERN, 6, 0, got, sizeof got, &l, &v);
        CHECK(sr == 0 && dr == 0 && st == STORE_OK && !store_edit_count() && br == 0 && l == lw && !memcmp(got, want, lw),
              "a save under way: the mode's drop refused, the change saved (%d %d %d, %d left, %d, %u B of %u)", sr, dr,
              st, store_edit_count(), br, l, lw);
        CHECK(save_pat(6, a, la) == 0, "... the pattern saved back as before");
    }
    store_set_watch(0);
}

/* Pieces past 64 KB when the edit memory is larger (an entry keeps its piece's offset in 4-byte units; run_store.sh
 * builds these tests at 96 KB too): songs and a pattern's tracks fill it, every fifth song dropped, the pattern and
 * some songs put again larger (to the top: room joined, pieces moved down), every one read back as put, saved and
 * mounted again. */
#define FAR 0x10000u                            /* (an offset 16 bits of bytes cannot hold) */
#define FAR_ROOM (FAR + 16384u)                 /* (enough past it for drops and puts there to move pieces down: the
                                                 * 96 KB build; the firmware's own size passes FAR by less) */
static int far_of(const uint8_t *p, const uint8_t *base) { return p && (uint32_t)(p - base) >= FAR; }
static void t_edits_far(void)
{
    fake_reset();
    store_init();
    store_edit_revert();
    static uint8_t b[4096], pa[4096], pb[4096], got[4096];
    static const uint8_t *was[256];
    static int ver[256];
    int c1[16], c2[16], n, r = STORE_OK, ok = 1, pat_in = 0, far = 0;
    for (int t = 0; t < 16; t++) { c1[t] = 100; c2[t] = 120; }
    uint32_t la = pat_dense(pa, 8, c1, 3), lb = pat_dense(pb, 8, c2, 4), l;      /* (tracks of 401 B, then 481) */
    uint8_t v;
    const uint8_t *base = 0;
    /* songs to 12 KB short of full, the pattern, songs till full */
    for (n = 0; n < 0xE0; n++) {
        if (!pat_in && store_edit_top() + 12288u > STORE_EDIT_MAX) {
            pat_in = store_edit_put(REC_PATTERN, 0x50, 0, 1, pa, la) == STORE_OK;
            if (!pat_in) break;
        }
        fill(b, n, 1);
        if ((r = store_edit_put(REC_SONG, (uint8_t)n, 0, 1, b, len_of(n, 1))) != STORE_OK) break;
        ver[n] = 1;
        if (!n) base = store_get(REC_SONG, 0, 0, 0, 0);    /* (the first piece: the edit memory's bottom) */
    }
    for (int i = 0; i < n; i++) {
        ok &= version_held(REC_SONG, i) == 1;
        far += far_of(store_get(REC_SONG, (uint8_t)i, 0, 0, 0), base);
    }
    for (int t = 0; t < 8; t++) far += far_of(store_piece(REC_PATTERN, 0x50, (uint16_t)t, 0), base);
    CHECK(pat_in && r == STORE_ERR_FULL && ok && store_edit_top() > STORE_EDIT_MAX - 4096u,
          "%d songs and a pattern fill the edit memory (%u B of %u), every song as put", n, store_edit_top(), STORE_EDIT_MAX);
    CHECK(store_edit_build(REC_PATTERN, 0x50, 0, got, sizeof got, &l, &v) == 0 && l == la && !memcmp(got, pa, la),
          "the pattern built as put");
    if (STORE_EDIT_MAX >= FAR_ROOM) CHECK(far > 8, "%d pieces past 64 KB", far);
    /* every fifth song dropped (the top one kept: the larger tracks cannot all go above it), the pattern's tracks and
     * every seventh song left put again larger */
    for (int i = 0; i < n; i++) was[i] = store_get(REC_SONG, (uint8_t)i, 0, 0, 0);
    for (int i = 0; i < n - 1; i += 5) { store_edit_forget(REC_SONG, (uint8_t)i, 0); ver[i] = -1; }
    uint32_t mv = store_edit_moves();
    int again = 0, moved = 0, moved_far = 0;
    CHECK(store_edit_put(REC_PATTERN, 0x50, 0, 1, pb, lb) == STORE_OK, "the pattern's tracks larger: put");
    ok = 1;
    for (int i = 1; i < n; i += 7) {
        if (ver[i] < 0 || len_of(i, 2) <= len_of(i, 1)) continue;
        fill(b, i, 2);
        ok &= store_edit_put(REC_SONG, (uint8_t)i, 0, 1, b, len_of(i, 2)) == STORE_OK;
        ver[i] = 2;
        again++;
    }
    far = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = store_get(REC_SONG, (uint8_t)i, 0, 0, 0);
        if (ver[i] == 1 && p != was[i]) { moved++; moved_far += far_of(was[i], base); }
        ok &= version_held(REC_SONG, i) == ver[i];
        far += far_of(p, base);
    }
    for (int t = 0; t < 8; t++) far += far_of(store_piece(REC_PATTERN, 0x50, (uint16_t)t, 0), base);
    CHECK(ok && again > 0 && moved > 0 && store_edit_moves() > mv, "a fifth dropped, the pattern and %d songs put again "
          "larger: room joined, %d songs moved down, every song as put", again, moved);
    CHECK(store_edit_build(REC_PATTERN, 0x50, 0, got, sizeof got, &l, &v) == 0 && l == lb && !memcmp(got, pb, lb),
          "the pattern built as put again");
    if (STORE_EDIT_MAX >= FAR_ROOM)
        CHECK(moved_far > 0 && far > 0, "%d moved down from past 64 KB, %d pieces lie past it", moved_far, far);
    /* saved, mounted again */
    CHECK(edits_saved() == STORE_OK && store_edit_count() == 0, "saved: nothing unsaved");
    store_init();
    ok = 1;
    for (int i = 0; i < n; i++) ok &= version_held(REC_SONG, i) == ver[i];
    const uint8_t *q = store_get(REC_PATTERN, 0x50, 0, &l, 0);
    CHECK(ok && q && l == lb && !memcmp(q, pb, lb) && !store_get(REC_PATTERN, 0x50, 1, 0, 0),
          "mounted again: every song and the pattern as saved");
}

/* A cartridge record (specs/files/instruments.md): its head (10 B; byte 9 the slots), n slots each filled with one
 * value from seed (a name never empty). Its length. */
static uint32_t cart(uint8_t *b, uint8_t kind, int n, int seed)
{
    static const uint32_t SZ[6] = { 0, 112, 28, 24, 40, 16 };
    memset(b, 0, 10);
    b[0] = (uint8_t)seed; b[9] = (uint8_t)n;
    for (int s = 0; s < n; s++) memset(b + 10 + SZ[kind] * (uint32_t)s, 1 + (s + seed) % 40, SZ[kind]);
    return 10 + SZ[kind] * (uint32_t)n;
}
static void t_cart_pieces(void)
{
    fake_reset();
    store_init();
    static uint8_t a[8192], b[8192], got[8192];
    uint32_t la, lb, l;
    uint8_t v, nums[8];
    la = cart(a, REC_DX7, 32, 0);
    store_save_begin(); store_save_put(REC_DX7, 3, 0, 1, a, la); store_save_run();
    memcpy(b, a, la);
    memset(b + 10 + 112 * 3, 77, 112);                  /* slot 3 */
    CHECK(store_edit_put(REC_DX7, 3, 0, 1, b, la) == STORE_OK && store_piece(REC_DX7, 3, 3, &l) && l == 112 &&
          !store_piece(REC_DX7, 3, 2, 0) && !store_piece(REC_DX7, 3, STORE_PC_HEAD, 0) && store_edit_used() == 112 + 8,
          "a slot changed: its piece alone (%u B)", store_edit_used());
    CHECK(store_edit_build(REC_DX7, 3, 0, got, sizeof got, &l, &v) == 0 && l == la && !memcmp(got, b, la), "built as put");
    CHECK(store_edit_forget_piece(REC_DX7, 3, 3) == STORE_OK && !store_edit_count(), "the slot's REVERT: nothing unsaved");
    lb = cart(b, REC_DX7, 30, 0);
    CHECK(store_edit_put(REC_DX7, 3, 0, 1, b, lb) == STORE_OK && store_piece(REC_DX7, 3, STORE_PC_HEAD, 0) &&
          !store_piece(REC_DX7, 3, 0, 0) && store_edit_build(REC_DX7, 3, 0, got, sizeof got, &l, &v) == 0 && l == lb &&
          !memcmp(got, b, lb), "30 slots: the head a piece, built so");
    CHECK(edits_saved() == STORE_OK && store_get(REC_DX7, 3, 0, &l, 0) && l == lb, "saved as 30 slots");
    /* another kind at the number: the first goes, in the same save */
    lb = cart(b, REC_JUNO, 128, 5);
    CHECK(store_edit_put(REC_JUNO, 3, 0, 1, b, lb) == STORE_OK && store_edit_has(REC_DX7, 3, 0) == 2 &&
          store_list(REC_DX7, nums, 8) == 1 && nums[0] == 0xF0 && store_list(REC_JUNO, nums, 8) == 1 && nums[0] == 3,
          "a JUNO-106 cartridge at 03: the DX7 one deleted");
    CHECK(edits_saved() == STORE_OK && !store_get(REC_DX7, 3, 0, 0, 0) && store_get(REC_JUNO, 3, 0, &l, 0) && l == lb,
          "saved: the JUNO-106 one only");
    /* REVERT of a cartridge: every kind at its number */
    lb = cart(b, REC_TB303, 4, 9);
    store_edit_put(REC_TB303, 3, 0, 1, b, lb);
    CHECK(store_edit_count() == 2 && store_edit_forget(REC_TB303, 3, 0) == STORE_OK && !store_edit_count() &&
          store_list(REC_JUNO, nums, 8) == 1, "REVERT of cartridge 03: the TB-303 one and the JUNO-106's delete gone");
    /* a sampler (version 2, instruments only) and samples 04-06: claimed by a DX7 one, the sampler alone deleted */
    lb = cart(b, REC_SAMPLER, 2, 1);
    store_save_begin();
    store_save_put(REC_SAMPLER, 4, 0, 2, b, lb);
    for (int s = 4; s < 7; s++) store_save_put(REC_SAMPLE, (uint8_t)s, 0, 1, a, 600);
    CHECK(store_save_run() == STORE_OK, "a sampler, samples 04-06");
    b[10 + 40 + 5] = 0x41;                              /* (its second slot) */
    CHECK(store_edit_put(REC_SAMPLER, 4, 0, 2, b, lb) == STORE_OK && store_piece(REC_SAMPLER, 4, 1, &l) && l == 40 &&
          !store_piece(REC_SAMPLER, 4, STORE_PC_HEAD, 0), "its slot changed: a piece");
    la = cart(a, REC_DX7, 1, 2);
    CHECK(store_edit_put(REC_DX7, 4, 0, 1, a, la) == STORE_OK && store_edit_has(REC_SAMPLER, 4, 0) == 2 &&
          !store_edit_has(REC_SAMPLE, 4, 0) && !store_edit_has(REC_SAMPLE, 5, 0), "a DX7 cartridge at 04: the sampler deleted, no sample");
    CHECK(edits_saved() == STORE_OK && !store_get(REC_SAMPLER, 4, 0, 0, 0) && store_get(REC_SAMPLE, 4, 0, &l, 0) && l == 600 &&
          store_get(REC_SAMPLE, 6, 0, 0, 0) && store_get(REC_DX7, 4, 0, 0, 0), "saved so: the samples stay");
}

/* LOOPS (specs/files/loops.md), version 4: its head (the record before the streams: 80 B and 12 a loop), its streams'
 * head and each loop's stream, as pieces */
static uint32_t loops_head(uint8_t *b, int n, int seed)        /* TEMPO 120, n loops of 128 rows: its length */
{
    memset(b, 0, 80 + 12 * (uint32_t)n);
    b[0] = 120; b[2] = 6; b[3] = 50; b[5] = (uint8_t)n; b[7] = n ? 128 : 0;
    for (int i = 0; i < n; i++) { b[80 + 12 * i] = 0xF0; b[81 + 12 * i] = (uint8_t)(seed + i); b[84 + 12 * i] = 127; b[85 + 12 * i] = 1; }
    return 80 + 12 * (uint32_t)n;
}
static uint32_t loops_rec(uint8_t *b, int n, int seed)
{
    uint32_t h = loops_head(b, n, seed);
    if (!n) return h;
    int cells[16] = { 0 };
    for (int i = 0; i < n; i++) cells[i] = 3 + i + seed;
    return h + pat(b + h, n, cells, seed);
}
static void t_loops_pieces(void)
{
    fake_reset();
    store_init();
    static uint8_t a[4096], b[4096], got[4096];
    uint32_t la, lb, l;
    uint8_t v;
    la = loops_rec(a, 3, 0);
    store_save_begin(); store_save_put(REC_LOOPS, 0, 0, 4, a, la); store_save_run();
    memcpy(b, a, la);
    b[80 + 36 + 5 + 7 + 1] ^= 1;                        /* (loop 1's first note) */
    CHECK(store_edit_put(REC_LOOPS, 0, 0, 4, b, la) == STORE_OK && store_piece(REC_LOOPS, 0, 1, &l) && l == 9 &&
          !store_piece(REC_LOOPS, 0, 0, 0) && !store_piece(REC_LOOPS, 0, STORE_PC_HEAD, 0) &&
          !store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0), "a note changed: loop 1's stream alone");
    CHECK(store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == 0 && l == la && !memcmp(got, b, la) && v == 4,
          "built as put");
    CHECK(edits_saved() == STORE_OK && store_get(REC_LOOPS, 0, 0, &l, 0) && l == la && !memcmp(store_get(REC_LOOPS, 0, 0, 0, 0), b, la),
          "saved as built");
    lb = loops_rec(b, 0, 0);
    CHECK(store_edit_put(REC_LOOPS, 0, 0, 4, b, lb) == STORE_OK && store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == 0 &&
          l == 80 && edits_saved() == STORE_OK && store_get(REC_LOOPS, 0, 0, &l, 0) && l == 80, "every loop deleted: its head alone");
    lb = loops_rec(b, 5, 2);
    CHECK(store_edit_put(REC_LOOPS, 0, 0, 4, b, lb) == STORE_OK && store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == 0 &&
          l == lb && !memcmp(got, b, lb) && edits_saved() == STORE_OK && !memcmp(store_get(REC_LOOPS, 0, 0, &l, 0), b, lb),
          "5 loops again: built and saved");
}

/* LOOPS past 4 KB (specs/files/loops.md#the-record): 16 loops edited as pieces, saved in parts (part 0 its head and the
 * streams that fit, each further part the streams' head with its own bits and streams), mounted again, a stream found
 * in whichever part holds it; a smaller one saved: the parts past it gone; a loop's stream kept to 512 rows, refused
 * past 3,800 B; a record of version 3 kept in the flash but read as none. */
static uint32_t loop_stream(uint8_t *b, int cells, int seed)  /* a note a row from row 0 (2 B a cell), then its 0x00 */
{
    for (int c = 0; c < cells; c++) { b[2 * c] = 0x80; b[2 * c + 1] = (uint8_t)(24 + (c * 7 + seed * 5) % 72); }
    b[2 * cells] = 0;
    return 2u * (uint32_t)cells + 1u;
}
static uint8_t lp_st[16][1300];
static uint32_t lp_sl[16];
static int loops_part_is(uint32_t s, const uint8_t *hd, uint32_t bits)  /* LOOPS' part s in the flash: version 4, (part
                                                                         * 0) the head hd, these streams as lp_st */
{
    uint32_t l, hl = s ? 0u : 80u + 12u * hd[5], at = hl + 5u;
    uint8_t v;
    const uint8_t *p = store_get(REC_LOOPS, 0, (uint8_t)s, &l, &v);
    if (!p || v != 4 || l < at || (hl && memcmp(p, hd, hl)) || p[hl] != hd[5] || (uint32_t)(p[hl + 3] | p[hl + 4] << 8) != bits)
        return 0;
    for (int t = 0; t < 16; t++) {
        if (!(bits >> t & 1u)) continue;
        if (at + lp_sl[t] > l || memcmp(p + at, lp_st[t], lp_sl[t])) return 0;
        at += lp_sl[t];
    }
    return at == l;
}
static void t_loops_parts(void)
{
    fake_reset();
    store_init();
    static uint8_t hd[272], b[4096], got[4096], v3[256], cut[1025];
    const uint8_t *q;
    uint32_t hl = loops_head(hd, 16, 1), l;
    uint8_t ph[3] = { 16, 127, 0 }, v;
    int ok = store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, 4, hd, hl) == STORE_OK &&
             store_edit_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 4, ph, 3) == STORE_OK;
    for (int t = 0; t < 16; t++) {                      /* (300 rows each: past a pattern's 128) */
        lp_sl[t] = loop_stream(lp_st[t], 300, t);
        ok &= store_edit_piece(REC_LOOPS, 0, (uint16_t)t, 4, lp_st[t], lp_sl[t]) == STORE_OK;
    }
    for (int t = 0; t < 16; t++) { q = store_piece(REC_LOOPS, 0, (uint16_t)t, &l); ok &= q && l == lp_sl[t] && !memcmp(q, lp_st[t], l); }
    CHECK(ok && store_edit_count() == 1, "16 loops of 601 B as pieces: each read back as put");
    CHECK(store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == -2, "too big to build as one part");
    CHECK(edits_saved() == STORE_OK && !store_edit_count(), "saved");
    for (int pass = 0; pass < 2; pass++) {
        CHECK(loops_part_is(0, hd, 0x003F) && loops_part_is(1, hd, 0x0FC0) && loops_part_is(2, hd, 0xF000) &&
              !store_get(REC_LOOPS, 0, 3, 0, 0), "%s: three parts, loops 0-5 after the head, 6-11, 12-15, every stream as put",
              pass ? "after a power cycle" : "saved");
        store_init();
    }
    store_key_t keys[8];
    CHECK(store_thing(REC_LOOPS, 0, 0, keys, 8) == 3 && keys[0].sub == 0 && keys[2].sub == 2, "the thing: its three parts");
    /* a stream as the flash has it in its third part: nothing unsaved; another: saved in its part */
    CHECK(store_edit_piece(REC_LOOPS, 0, 13, 4, lp_st[13], lp_sl[13]) == STORE_OK && !store_edit_count(),
          "loop 13's stream as saved (part 2): no edit");
    lp_sl[13] = loop_stream(lp_st[13], 280, 99);
    CHECK(store_edit_piece(REC_LOOPS, 0, 13, 4, lp_st[13], lp_sl[13]) == STORE_OK && store_edit_count() == 1 &&
          edits_saved() == STORE_OK && loops_part_is(0, hd, 0x003F) && loops_part_is(1, hd, 0x0FC0) &&
          loops_part_is(2, hd, 0xF000), "loop 13 changed: saved in its part, the rest as it was");
    memset(got, 0, sizeof got);
    CHECK(store_edit_piece(REC_LOOPS, 0, 1, 4, got, STORE_LOOP_STREAM_MAX + 1u) == STORE_ERR_BIG && !store_edit_count(),
          "a stream over 3,800 B refused");
    /* a smaller LOOPS: one part, the parts past it deleted */
    uint32_t lb = loops_rec(b, 3, 4);
    CHECK(store_edit_put(REC_LOOPS, 0, 0, 4, b, lb) == STORE_OK && store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == 0 &&
          l == lb && !memcmp(got, b, lb) && v == 4, "3 loops put whole: built as put");
    CHECK(edits_saved() == STORE_OK && (q = store_get(REC_LOOPS, 0, 0, &l, &v)) && l == lb && !memcmp(q, b, lb) && v == 4 &&
          !store_get(REC_LOOPS, 0, 1, 0, 0) && !store_get(REC_LOOPS, 0, 2, 0, 0), "saved: one part, parts 1 and 2 gone");
    /* a loop's stream of 600 rows: saved as its first 512 (a pattern track's would keep 128) */
    l = loop_stream(lp_st[0], 600, 7);
    memcpy(cut, lp_st[0], 1024);
    cut[1024] = 0;
    CHECK(store_edit_piece(REC_LOOPS, 0, 2, 4, lp_st[0], l) == STORE_OK && edits_saved() == STORE_OK &&
          store_get(REC_LOOPS, 0, 0, &l, 0) && l == lb - 19u + 1025u &&
          store_edit_piece(REC_LOOPS, 0, 2, 4, cut, 1025) == STORE_OK && !store_edit_count(),
          "a stream of 600 rows saved as its first 512 (%u B)", l);
    /* version 3 (72 B and 10 a loop, then one 16-track pattern): kept in the flash, read as none */
    fake_reset();
    store_init();
    int c3[16] = { 5 };
    store_rec_t r;
    memset(v3, 0, 82);
    v3[0] = 120; v3[2] = 6; v3[5] = 1; v3[72] = 0xF0;
    uint32_t l3 = 82 + pat(v3 + 82, 16, c3, 2);
    CHECK(!store_save_begin() && !store_save_put(REC_LOOPS, 0, 0, 3, v3, l3) && store_save_run() == STORE_OK &&
          (q = store_get(REC_LOOPS, 0, 0, &l, &v)) && v == 3 && l == l3 &&
          store_info(REC_LOOPS, 0, 0, &r) && r.area == STORE_AREA_USER && r.ver == 3 &&
          store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == -1, "version 3 in the flash: there, built as none");
    CHECK(store_edit_piece(REC_LOOPS, 0, 0, 4, v3 + 82 + 5, 11) == STORE_OK && store_piece(REC_LOOPS, 0, 0, 0),
          "... no piece lies over it (its stream as an edit is one)");
    CHECK(edits_saved() == STORE_OK && (q = store_get(REC_LOOPS, 0, 0, &l, &v)) && v == 3 && l == l3 && !memcmp(q, v3, l3),
          "that stream alone saved (no head of version 4): the record of version 3 left as it is");
    CHECK(store_edit_put(REC_SONG, 1, 0, 1, v3, 10) == STORE_OK && edits_saved() == STORE_OK &&
          (q = store_get(REC_LOOPS, 0, 0, &l, &v)) && v == 3 && !memcmp(q, v3, l3), "a song saved: it stays");
    lb = loops_head(b, 0, 0);
    CHECK(store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, 4, b, lb) == STORE_OK && edits_saved() == STORE_OK &&
          (q = store_get(REC_LOOPS, 0, 0, &l, &v)) && v == 4 && l == 80 && !memcmp(q, b, 80),
          "LOOPS saved: version 4 in its place");
}

/* LOOPS' stage (specs/files/ram.md#the-stage-in-the-edit-memory): one piece under a key of its own (kind 0xFF), a
 * loop's stream of at most 3,800 B, read wherever it lies. No edit: not counted, not saved, not LOOPS'; LOOPS' REVERT
 * and an edit mode's start, EDIT and HOME leave it; a SAVE (one with nothing to write, one with the stage at its
 * 3,800 B, one that compacts first) and REVERT ALL keep it where it lies, byte for byte, nothing told of it, the edits
 * made after them fitting as before, the next squeeze moving it down; a squeeze moves it, told, its reader following;
 * a full edit memory refuses a change, a stream past 3,800 B too, and a change while a save is written or seeding
 * runs; no record's entry point reaches its key. */
static void stage_bytes(uint8_t *b, uint32_t len, int seed)    /* len bytes, the last its 0x00 */
{
    for (uint32_t i = 0; i + 1u < len; i++) b[i] = (uint8_t)(1u + (i * 7u + (uint32_t)seed * 13u) % 250u);
    if (len) b[len - 1u] = 0;
}
static int stage_is(const uint8_t *b, uint32_t len)            /* the stage holds these bytes */
{
    uint32_t l = 0;
    const uint8_t *p = store_stage(&l);
    return p && l == len && !memcmp(p, b, len);
}
static int songs_fit(uint32_t size, int from)   /* songs of size B put from number `from` on till refused: how many */
{
    static uint8_t s[4096];
    int n = 0;
    while (n < 64) {
        s[0] = (uint8_t)n;
        if (store_edit_put(REC_SONG, (uint8_t)(from + n), 0, 1, s, size) != STORE_OK) break;
        n++;
    }
    return n;
}
static void songs_gone(int from, int n) { for (int k = 0; k < n; k++) store_edit_forget(REC_SONG, (uint8_t)(from + k), 0); }
/* the edit memory filled beside a stage of ls B put alone into an empty one: songs of 4 KB, then of 100 B (n4, n1) */
static void stage_ref(uint32_t ls, int *n4, int *n1)
{
    static uint8_t st[4096];
    store_stage_drop();
    store_edit_revert();
    stage_bytes(st, ls, 99);
    store_stage_put(st, ls);
    *n4 = songs_fit(4000, 0x40);
    *n1 = songs_fit(100, 0x80);
    songs_gone(0x40, *n4);
    songs_gone(0x80, *n1);
    store_stage_drop();
    store_edit_revert();
}
/* after a SAVE or REVERT ALL that kept the stage (st, ls) at p: the edits made after it fit as before (n4, n1), the
 * squeeze that joins the room under it moving it down, told, its bytes the same where it lies now */
static void stage_after(const char *what, const uint8_t *p, const uint8_t *st, uint32_t ls, int n4, int n1)
{
    w_stage_at = p;
    int ws = w_stage, k4 = songs_fit(4000, 0x40), k1 = songs_fit(100, 0x80);
    CHECK(k4 == n4 && k1 == n1 && store_edit_room() < 108 && stage_is(st, ls) && store_stage(0) < p && w_stage > ws &&
          !w_unlocked, "%s: the edits after it fit as before (%d + %d songs, %d + %d beside it alone), the squeeze moved it "
          "down, told", what, k4, k1, n4, n1);
    CHECK(w_stage_at == store_stage(0) && w_stage_len == ls, "%s: inside the watch, the stage already where it lies now",
          what);
    songs_gone(0x40, k4);
    songs_gone(0x80, k1);
}
/* a SAVE (or REVERT ALL) of edits lying under the stage */
static void stage_kept(uint32_t ls, int seed, int revert)
{
    static uint8_t st[4096], b[4096];
    char what[64];
    int cells[16] = { 40, 30, 20 }, n4, n1;
    snprintf(what, sizeof what, "%s, the stage %u B", revert ? "REVERT ALL" : "SAVE", ls);
    stage_ref(ls, &n4, &n1);
    stage_bytes(st, ls, seed);
    uint32_t lp = pat(b, 3, cells, seed);
    CHECK(store_edit_put(REC_PATTERN, 0x30, 0, 1, b, lp) == STORE_OK && store_edit_put(REC_SONG, 0x31, 0, 1, b, 3000) == STORE_OK &&
          store_stage_put(st, ls) == STORE_OK && store_edit_count() == 2, "%s: two changes, the stage above them", what);
    const uint8_t *p = store_stage(0);
    uint32_t last = store_last_save();
    int ws = w_stage, r = STORE_OK;
    if (revert) store_edit_revert();
    else r = edits_saved();
    CHECK(r == STORE_OK && !store_edit_count() && store_last_save() == last + (revert ? 0u : 1u) && store_stage(0) == p &&
          stage_is(st, ls) && w_stage == ws && store_edit_used() == ((ls + 3u) & ~3u) + 8u,
          "%s: the changes %s, the stage kept where it lies, byte for byte, nothing told of it", what,
          revert ? "dropped" : "saved");
    if (!revert) {
        uint32_t l;
        const uint8_t *q = store_get(REC_SONG, 0x31, 0, &l, 0);
        CHECK(q && l == 3000 && !memcmp(q, b, 3000), "%s: the song in the flash", what);
    }
    stage_after(what, p, st, ls, n4, n1);
    store_stage_drop();
    store_edit_revert();
}
/* the user area worn so that a save compacts first: each block a small song still live beside a big one's older copy */
static void worn(void)
{
    static uint8_t sm[100], bg[3900];
    for (int k = 0; k < 120; k++) {
        sm[0] = bg[0] = (uint8_t)k;
        if (store_save_begin() || store_save_put(REC_SONG, (uint8_t)(1 + k), 0, 1, sm, sizeof sm) ||
            store_save_put(REC_SONG, 0xC0, 0, 1, bg, sizeof bg) || store_save_run() != STORE_OK)
            break;
    }
}
static void t_stage(void)
{
    fake_reset();
    store_init();
    store_edit_revert();
    store_set_watch(watch_fn);
    static uint8_t st[4096], b[4096], got[4096];
    uint32_t l, ls, mv;
    uint8_t v;
    const uint8_t *p, *q;
    int s;
    /* a piece of its own, no edit */
    CHECK(!store_stage(&l) && !store_stage_room(), "no stage at first");
    ls = 1200;
    stage_bytes(st, ls, 1);
    w_calls = w_unlocked = w_stage = 0;
    CHECK(store_stage_put(st, ls) == STORE_OK && stage_is(st, ls), "the stage put: read back as put");
    CHECK(!store_edit_count() && store_edit_used() == 1200u + 8u && store_stage_room() == 1200u + 8u,
          "no change counted; it takes its room and its entry (%u B)", store_edit_used());
    CHECK(w_stage == 1 && w_calls == 1 && !w_unlocked && w_last == (uint32_t)STORE_STAGE << 16,
          "the watch told of it, as the stage, under the lock (%d calls)", w_calls);
    CHECK(!store_edit_obj(REC_LOOPS, 0) && !store_edit_has(REC_LOOPS, 0, 0) &&
          store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == -1, "not LOOPS'");
    p = store_stage(0);
    stage_bytes(st, ls, 2);
    CHECK(store_stage_put(st, ls) == STORE_OK && store_stage(0) == p && stage_is(st, ls) && w_stage == 2,
          "changed: rewritten where it lies, told");
    mv = store_edit_moves();
    ls = 1301;
    stage_bytes(st, ls, 3);
    CHECK(store_stage_put(st, ls) == STORE_OK && store_stage(0) == p && stage_is(st, ls) && store_edit_moves() == mv &&
          store_edit_used() == 1304u + 8u, "longer, the top piece: grown where it lies (%u B)", store_edit_used());
    memset(b, 7, sizeof b);
    store_edit_put(REC_SONG, 1, 0, 1, b, 500);
    ls = 1500;
    stage_bytes(st, ls, 4);
    CHECK(store_stage_put(st, ls) == STORE_OK && store_stage(0) != p && stage_is(st, ls) && store_edit_moves() > mv &&
          store_edit_used() == 1500u + 8u + 500u + 8u, "longer, a song above it: moved to the top, its old room free (%u B)",
          store_edit_used());
    /* refused, nothing changed */
    stage_bytes(b, STORE_LOOP_STREAM_MAX + 1u, 5);
    CHECK(store_stage_put(b, STORE_LOOP_STREAM_MAX + 1u) == STORE_ERR_BIG && stage_is(st, ls),
          "a stream past 3,800 B refused (THE STAGE IS FULL), the stage as it was");
    CHECK(store_edit_piece(STORE_STAGE, 0, 0, 4, b, 10) == STORE_ERR_STATE &&
          store_edit_put(STORE_STAGE, 0, 0, 4, b, 10) == STORE_ERR_STATE && store_edit_del(STORE_STAGE, 0, 0) == STORE_ERR_STATE &&
          store_edit_forget(STORE_STAGE, 0, 0) == STORE_ERR_STATE && store_edit_forget_piece(STORE_STAGE, 0, 0) == STORE_ERR_STATE &&
          store_edit_put(REC_SONG | 0x80u, 1, 0, 1, b, 10) == STORE_ERR_STATE && stage_is(st, ls) && store_edit_count() == 1,
          "no record's entry point reaches its key, nor an edit mode's copies'");
    store_lock(1);
    CHECK(store_stage_put(b, 100) == STORE_ERR_STATE && store_stage_drop() == STORE_ERR_STATE && stage_is(st, ls),
          "while seeding: a change refused (STAGE NOT WRITTEN: STORAGE BUSY)");
    store_lock(0);
    p = store_stage(0);
    CHECK(store_edit_save() == STORE_OK && store_saving() && store_stage_put(b, 100) == STORE_ERR_STATE &&
          store_stage_drop() == STORE_ERR_STATE && stage_is(st, ls), "while a save is written: a change refused (STORE_ERR_STATE)");
    while ((s = store_save_status(0)) == STORE_BUSY) store_poll();
    CHECK(s == STORE_OK && !store_edit_count() && store_stage(0) == p && stage_is(st, ls) && store_edit_used() == 1500u + 8u &&
          (q = store_get(REC_SONG, 1, 0, &l, 0)) && l == 500, "the save written: the song saved, the stage as it was");
    /* dropped */
    w_stage = 0;
    w_stage_at = p;
    CHECK(store_stage_drop() == STORE_OK && !store_stage(&l) && !store_stage_room() && !store_edit_used() && w_stage == 1 &&
          !w_stage_at, "dropped: no stage, its room free, told (none already inside the watch)");
    CHECK(store_stage_drop() == STORE_OK && w_stage == 1, "dropped again: nothing");
    CHECK(store_stage_put(ZERO, 1) == STORE_OK && !store_stage(&l) && !store_edit_used(), "a lone 0x00 (no cell): no piece");
    stage_bytes(st, STORE_LOOP_STREAM_MAX, 6);
    CHECK(store_stage_put(st, STORE_LOOP_STREAM_MAX) == STORE_OK && stage_is(st, STORE_LOOP_STREAM_MAX) &&
          store_stage_put(st, 0) == STORE_OK && !store_stage(&l), "3,800 B taken; none: dropped");
    /* a full edit memory refuses a change */
    store_edit_revert();
    songs_fit(4000, 0x40);
    songs_fit(100, 0x80);
    stage_bytes(st, 200, 7);
    CHECK(store_edit_room() < 108 && store_stage_put(st, 200) == STORE_ERR_FULL && !store_stage(&l),
          "a full edit memory: a new stage refused (%u B left)", store_edit_room());
    store_edit_revert();
    ls = 1000;
    stage_bytes(st, ls, 8);
    store_stage_put(st, ls);
    songs_fit(4000, 0x40);
    songs_fit(100, 0x80);
    stage_bytes(b, 1200, 9);
    CHECK(store_edit_room() < 108 && store_stage_put(b, 1200) == STORE_ERR_FULL && stage_is(st, ls),
          "... a longer stage refused, the stage as it was");
    stage_bytes(st, ls, 10);
    CHECK(store_stage_put(st, ls) == STORE_OK && stage_is(st, ls), "... one as long fits its room");
    store_edit_revert();
    CHECK(stage_is(st, ls) && store_edit_used() == 1000u + 8u, "REVERT ALL: the songs gone, the stage kept");
    store_stage_drop();
    /* store_stage_check: what a put would answer, nothing written; the pieces' top grows by what it adds alone */
    {
        int k = 0;
        CHECK(store_stage_check(100) == STORE_OK && store_stage_check(STORE_LOOP_STREAM_MAX) == STORE_OK &&
              store_stage_check(STORE_LOOP_STREAM_MAX + 1u) == STORE_ERR_BIG && !store_stage(&l) && !store_edit_used(),
              "check: 100 B and 3,800 B taken, past it refused, nothing written");
        store_lock(1);
        CHECK(store_stage_check(100) == STORE_ERR_STATE, "check while seeding: refused");
        store_lock(0);
        while (store_edit_room() >= 3000u + 4008u && k < 64) { memset(b, k, 4000); store_edit_put(REC_SONG, (uint8_t)(0x40 + k), 0, 1, b, 4000); k++; }
        while (store_edit_room() >= 3000u + 108u && k < 128) { memset(b, k, 100); store_edit_put(REC_SONG, (uint8_t)(0x40 + k), 0, 1, b, 100); k++; }
        stage_bytes(st, 2000, 21);
        CHECK(store_stage_put(st, 2000) == STORE_OK && store_edit_room() < 1100u, "a stage of 2,000 B on top, %u B left",
              store_edit_room());
        p = store_stage(0);
        stage_bytes(st, 2900, 22);
        CHECK(store_stage_check(2900) == STORE_OK && store_stage_put(st, 2900) == STORE_OK && store_stage(0) == p &&
              stage_is(st, 2900), "the top 900 B longer than the room it had, less than its whole: taken, grown where it lies");
        stage_bytes(b, 3200, 23);
        CHECK(store_edit_room() < 300u && store_stage_check(3200) == STORE_ERR_FULL && store_stage_put(b, 3200) == STORE_ERR_FULL &&
              stage_is(st, 2900), "300 B more than the room left: refused by both, the stage as it was");
        memset(b, 1, 20);
        store_edit_put(REC_SONG, 0xC0, 0, 1, b, 20);
        stage_bytes(b, 2904, 24);
        CHECK(store_stage_check(2904) == STORE_ERR_FULL && store_stage_put(b, 2904) == STORE_ERR_FULL && stage_is(st, 2900) &&
              store_stage_check(2900) == STORE_OK, "a song above it: 4 B longer needs its whole room at the top, refused by "
              "both; as long, taken");
        store_edit_revert();
        store_stage_drop();
    }
    /* an edit mode's start, EDIT and HOME leave it; changed in the mode, no copy of it is kept */
    {
        uint8_t s1[3] = { 1, 2, 3 };
        ls = 900;
        stage_bytes(st, ls, 11);
        store_stage_put(st, ls);
        store_edit_put(REC_SONG, 3, 0, 1, s1, 3);
        store_edit_session(1);
        CHECK(stage_is(st, ls), "an edit mode's start: the stage kept");
        store_edit_put(REC_SONG, 3, 0, 1, b, 50);
        ls = 950;
        stage_bytes(st, ls, 12);
        store_stage_put(st, ls);
        int dr = store_edit_session_drop();
        CHECK(dr == 1 && stage_is(st, ls) && store_edit_count() == 1 && (q = store_get(REC_SONG, 3, 0, &l, 0)) && l == 3,
              "HOME: the song back as before the mode, the stage as last put (no copy of it)");
        store_edit_session(1);
        ls = 960;
        stage_bytes(st, ls, 13);
        store_stage_put(st, ls);
        CHECK(store_edit_session_drop() == 0 && stage_is(st, ls), "HOME with the stage alone changed: nothing put back");
        store_edit_session_of(REC_SONG, 3);
        store_edit_put(REC_SONG, 3, 0, 1, b, 60);
        store_edit_session(0);
        CHECK(stage_is(st, ls) && (q = store_get(REC_SONG, 3, 0, &l, 0)) && l == 60 && store_edit_session_drop() == 0,
              "EDIT: the change kept, the stage too");
        store_edit_revert();
        store_stage_drop();
    }
    /* LOOPS' REVERT and LOOPS saved leave it */
    {
        uint32_t la = loops_rec(b, 3, 0);
        store_save_begin(); store_save_put(REC_LOOPS, 0, 0, 4, b, la); store_save_run();
        ls = 700;
        stage_bytes(st, ls, 14);
        store_stage_put(st, ls);
        b[80 + 36 + 5 + 7 + 1] ^= 1;                    /* (loop 1's first note) */
        CHECK(store_edit_put(REC_LOOPS, 0, 0, 4, b, la) == STORE_OK && store_edit_count() == 1 &&
              store_edit_build(REC_LOOPS, 0, 0, got, sizeof got, &l, &v) == 0 && l == la && !memcmp(got, b, la),
              "LOOPS changed beside it: one change, LOOPS built as put");
        CHECK(store_edit_forget(REC_LOOPS, 0, 0) == STORE_OK && !store_edit_count() && !store_edit_obj(REC_LOOPS, 0) &&
              stage_is(st, ls), "LOOPS' REVERT: its pieces gone, the stage kept");
        store_edit_put(REC_LOOPS, 0, 0, 4, b, la);
        CHECK(edits_saved() == STORE_OK && (q = store_get(REC_LOOPS, 0, 0, &l, 0)) && l == la && !memcmp(q, b, la) &&
              stage_is(st, ls), "LOOPS saved as built, the stage kept");
        store_stage_drop();
    }
    /* a SAVE with nothing to write: nothing written, nothing of the stage's in the flash */
    {
        ls = 2000;
        stage_bytes(st, ls, 15);
        store_stage_put(st, ls);
        p = store_stage(0);
        uint32_t last = store_last_save();
        int nrec = store_count(), ws = w_stage, theirs = 0;
        CHECK(edits_saved() == STORE_OK && store_last_save() == last && store_count() == nrec && store_stage(0) == p &&
              stage_is(st, ls) && w_stage == ws && store_edit_used() == 2000u + 8u,
              "SAVE with the stage alone: nothing written, the stage where it lies, nothing told of it");
        for (int i = 0; i < store_count(); i++) {
            store_rec_t r;
            uint32_t sv, crc;
            if (store_entry(i, &r, &sv, &crc) && (r.kind & 0x7Fu) == (STORE_STAGE & 0x7Fu)) theirs++;
        }
        CHECK(!theirs, "no record of its kind in the flash");
        store_stage_drop();
    }
    /* SAVE and REVERT ALL keep it, also at its 3,800 B; the edits after fit as before */
    stage_kept(1200, 16, 0);
    stage_kept(STORE_LOOP_STREAM_MAX, 17, 0);
    stage_kept(1200, 18, 1);
    stage_kept(STORE_LOOP_STREAM_MAX, 19, 1);
    /* a SAVE that compacts first */
    {
        int n4, n1;
        fake_reset();
        store_init();
        stage_ref(STORE_LOOP_STREAM_MAX, &n4, &n1);
        worn();
        stage_bytes(st, STORE_LOOP_STREAM_MAX, 20);
        memset(b, 0x33, 3900);
        CHECK(store_edit_put(REC_SONG, 0xC1, 0, 1, b, 3900) == STORE_OK && store_stage_put(st, STORE_LOOP_STREAM_MAX) == STORE_OK,
              "a worn store: a song of 3.9 KB changed, the stage at its 3,800 B");
        p = store_stage(0);
        uint32_t last = store_last_save();
        int ws = w_stage;
        CHECK(edits_saved() == STORE_OK && store_last_save() > last + 1u && (q = store_get(REC_SONG, 0xC1, 0, &l, 0)) &&
              l == 3900 && !memcmp(q, b, 3900) && store_stage(0) == p && stage_is(st, STORE_LOOP_STREAM_MAX) &&
              w_stage == ws && !store_edit_count(), "a SAVE that compacts first (%u saves): the song saved, the stage kept "
              "where it lies, byte for byte", store_last_save() - last);
        printf("  the stage: kept through a SAVE that compacted first (%u saves), %d + %d songs beside it after\n",
               store_last_save() - last, n4, n1);
        stage_after("a SAVE that compacts", p, st, STORE_LOOP_STREAM_MAX, n4, n1);
        store_stage_drop();
        store_edit_revert();
    }
    CHECK(!w_unlocked && !store_stage(&l) && !store_edit_used(), "every change told under the lock; none left");
    store_set_watch(0);
}

/* A pattern has at most 128 rows (specs/files/patterns.md#size): a record of more, made before that rule, reads as its
 * first 128 (rec.c's walk and size), and its next save keeps those: rows - 1 written as 127, each track's cells past
 * row 127 left out, the tracks taken from the flash as they were and those put whole as an edit alike. */
static void t_rows_cap(void)
{
    fake_reset();
    store_init();
    static uint8_t b[4096];
    int cells[16] = { 100, 100 };                       /* (a note every other row: rows 1, 3, ... 199) */
    uint32_t len = pat(b, 2, cells, 4), whole = len;
    b[1] = 199;                                         /* 200 rows */
    CHECK(save_pat(0x40, b, len) == STORE_OK, "a pattern of 200 rows saved (as an older firmware could)");
    rec_pat_t p;
    uint8_t ntrk;
    uint16_t rows;
    CHECK(!rec_pattern(0x40, &p) && p.rows == PAT_ROWS_MAX, "it reads as 128 rows: %u", p.rows);
    CHECK(!rec_pattern_size(0x40, &ntrk, &rows) && rows == PAT_ROWS_MAX, "its size says 128 rows: %u", rows);
    uint8_t t0[3] = { 0x81, 50, 0 };
    CHECK(store_edit_piece(REC_PATTERN, 0x40, 0, 1, t0, 3) == 0 && edits_saved() == STORE_OK, "a track edited, saved");
    const uint8_t *q = store_get(REC_PATTERN, 0x40, 0, &len, 0);
    CHECK(q && q[1] == PAT_ROWS_MAX - 1, "the save writes rows - 1 as 127: %d", q ? q[1] : -1);
    /* track 01, not edited, from the flash: its 64 notes on rows 1-127 as they were, then its 0x00 (129 B) */
    CHECK(q && len == 5u + 3u + 129u && !memcmp(q + 8, b + 5 + 201, 128) && q[8 + 128] == 0,
          "the track not edited keeps its cells on rows 0-127 alone: %u B", len);
    CHECK(store_edit_put(REC_PATTERN, 0x41, 0, 1, b, whole) == 0 && edits_saved() == STORE_OK &&
          (q = store_get(REC_PATTERN, 0x41, 0, &len, 0)) && q[1] == PAT_ROWS_MAX - 1 && len == 5u + 2u * 129u &&
          !memcmp(q + 5, b + 5, 128) && q[5 + 128] == 0 && !memcmp(q + 5 + 129, b + 5 + 201, 128) && q[5 + 257] == 0,
          "a record of 200 rows put whole as an edit: saved as its first 128 rows, %u B", q ? len : 0);
}

/* Built-ins the firmware makes at start (store_builtin_extra): the keys' song FE and the EDITOR's pattern FE side by
 * side, each read, listed and changed as the firmware's own are; registering one again replaces it. Last: the
 * extras stay for good. */
static void t_builtin_extras(void)
{
    static const uint8_t song[8] = { 'K', 'E', 'Y', 'S', 1, 2, 3, 4 };
    static const uint8_t epat[5] = { 8, 63, 1, 0, 0 };
    const builtin_rec_t s = { REC_SONG, 0xFE, 0, 1, sizeof song, song }, p = { REC_PATTERN, 0xFE, 0, 1, sizeof epat, epat };
    fake_reset();
    store_builtin_extra(&s);
    store_builtin_extra(&p);
    store_builtin_extra(&s);                            /* (again: the same one) */
    store_init();
    uint32_t len;
    uint8_t nums[8];
    store_rec_t r;
    const uint8_t *q = store_get(REC_SONG, 0xFE, 0, &len, 0);
    CHECK(q == song && len == 8, "the keys' song: the firmware's");
    q = store_get(REC_PATTERN, 0xFE, 0, &len, 0);
    CHECK(q == epat && len == 5 && store_info(REC_PATTERN, 0xFE, 0, &r) && r.area == STORE_AREA_BUILTIN,
          "the EDITOR's pattern: the firmware's, beside it");
    CHECK(store_list(REC_SONG, nums, 8) == 0, "the keys' song never listed (a whole record built in)");
    CHECK(store_list(REC_PATTERN, nums, 8) == 2 && nums[0] == 0xF0 && nums[1] == 0xFE, "the patterns: F0 and FE");
    static uint8_t b[4096];
    int cells[16] = { 4, 4 };
    uint32_t lp = pat(b, 2, cells, 9);
    uint8_t s2[8] = { 'K', 'E', 'Y', 'S', 9, 9, 9, 9 };
    CHECK(store_edit_put(REC_PATTERN, 0xFE, 0, 1, b, lp) == 0 && store_edit_put(REC_SONG, 0xFE, 0, 1, s2, 8) == 0 &&
          edits_saved() == STORE_OK, "both changed and saved");
    store_init();
    q = store_get(REC_PATTERN, 0xFE, 0, &len, 0);
    CHECK(q && q != epat && len == lp && !memcmp(q, b, lp), "the pattern's change found first");
    q = store_get(REC_SONG, 0xFE, 0, &len, 0);
    CHECK(q && q != song && q[4] == 9, "the song's change found first");
    CHECK(store_edit_del(REC_PATTERN, 0xFE, 0) == 0 && store_edit_del(REC_SONG, 0xFE, 0) == 0 && edits_saved() == STORE_OK,
          "both changes deleted and saved");
    store_init();
    CHECK(store_get(REC_PATTERN, 0xFE, 0, &len, 0) == epat && store_get(REC_SONG, 0xFE, 0, &len, 0) == song,
          "the firmware's own back, both");
}

/* FAVORITES (specs/files/instruments.md#favorites, store.md#power-on): kind 0x07, a cartridge of 3-byte references
 * held as any cartridge's slots; read by rec_fav, never an instrument (rec_slot); the built-in FB edited as any built-in
 * (its change deleted, the firmware's own back); another cartridge kind under a FAVORITES record's number wins at a mount,
 * the FAVORITES record's deletion the next SAVE's */
static void t_favorites(void)
{
    fake_reset();
    store_init();
    uint8_t b[16 + 3 * 8], nums[8];
    uint32_t l;
    rec_cart_t c;
    memset(b, 0, sizeof b);
    b[0] = 9; b[9] = 3;
    const uint8_t sl[9] = { 0x0B, 0x05, 0, 0, 0xFF, 0, 0xF0, 0x03, 0 };    /* 0B:05, empty, F0:03 */
    memcpy(b + 10, sl, 9);
    CHECK(store_edit_put(REC_FAV, 0x10, 0, 1, b, 19) == STORE_OK && store_piece(REC_FAV, 0x10, 0, &l) && l == 3,
          "a FAVORITES cartridge: its slots, 3 B pieces");
    CHECK(!rec_cart(0x10, &c) && c.kind == REC_FAV && c.nslots == 3 && rec_fav(&c, 0) && rec_fav(&c, 0)[1] == 5 &&
          !rec_fav(&c, 1) && rec_fav(&c, 2)[0] == 0xF0 && !rec_slot(&c, 0), "read: its favourites, an empty one, no instrument");
    CHECK(edits_saved() == STORE_OK && store_get(REC_FAV, 0x10, 0, &l, 0) && l == 19, "saved, 19 B");
    store_init();
    CHECK(!rec_cart(0x10, &c) && c.kind == REC_FAV && rec_fav(&c, 2) && store_list(REC_FAV, nums, 8) == 2 &&
          nums[0] == 0x10 && nums[1] == 0xFB, "mounted: listed with the built-in FB");
    /* an empty slot is as none: putting one where the flash has none changes nothing */
    {
        const uint8_t e[3] = { 0, 0xFF, 0 };
        CHECK(store_edit_piece(REC_FAV, 0x10, 5, 1, e, 3) == STORE_OK && !store_edit_count(), "an empty favourite: nothing unsaved");
    }
    /* the built-in FB: a change saved in the user area; deleted, the firmware's own (empty) is back */
    {
        store_rec_t r;
        b[9] = 1;
        CHECK(store_edit_put(REC_FAV, 0xFB, 0, 1, b, 13) == STORE_OK && edits_saved() == STORE_OK &&
              store_info(REC_FAV, 0xFB, 0, &r) && r.area == STORE_AREA_USER, "FB changed and saved: the user area's");
        CHECK(store_edit_del(REC_FAV, 0xFB, 0) == STORE_OK && edits_saved() == STORE_OK && store_info(REC_FAV, 0xFB, 0, &r) &&
              r.area == STORE_AREA_BUILTIN && store_get(REC_FAV, 0xFB, 0, &l, 0) && l == 10, "emptied: its record deleted, the firmware's own");
    }
    /* an older firmware's NEW over a FAVORITES record it kept: a DX7 cartridge at 10 too, saved later */
    {
        uint8_t d[10 + 112];
        memset(d, 0, sizeof d);
        d[9] = 1;
        d[10 + 98] = 1;                                 /* (its name not empty: bit 784's 6 bits) */
        CHECK(store_save_begin() == 0, "an older firmware's save");
        store_save_put(REC_DX7, 0x10, 0, 1, d, sizeof d);
        store_save_run();
        /* (store_save_put deletes the other kind in the same save, as this firmware's always does: put it back by hand,
         * as an older firmware leaves it, the FAVORITES record the newer save) */
        store_rec_t r;
        CHECK(store_info(REC_DX7, 0x10, 0, &r) && r.area == STORE_AREA_USER, "the DX7 cartridge written");
        store_usage_t u;
        store_usage(&u);
        put_raw(0x0A0000, REC_FAV, 0x10, 0, b, 19, u.last_save + 1, 1);
        store_init();
        CHECK(!rec_cart(0x10, &c) && c.kind == REC_DX7, "mounted: the DX7 cartridge wins");
        CHECK(store_edit_has(REC_FAV, 0x10, 0) == 2 && store_list(REC_FAV, nums, 8) == 1 && nums[0] == 0xFB,
              "the FAVORITES record left out by an edit, not listed");
        CHECK(edits_saved() == STORE_OK, "the next SAVE");
        store_init();
        CHECK(!store_edit_count() && !store_get(REC_FAV, 0x10, 0, 0, 0) && store_get(REC_DX7, 0x10, 0, 0, 0),
              "its deletion saved: the DX7 cartridge alone, nothing unsaved");
    }
}

/* SETTINGS' FACTORY RESET (seed_wipe; specs/ui/screens/settings.md): refused while a save is made; else every block of
 * the user area erased, a block a poll (one already erased left as it is), the seeding's commands answered busy
 * meanwhile, the restart asked once at the end; the next start finds nothing */
static int acks, ack_result;
static void wipe_acks(const uint8_t *m, uint32_t n)
{
    if (n >= 11 && m[4] == 0x20) { acks++; ack_result = m[9] | (m[5] >> 3 & 1) << 7; }   /* (ACK: cmd, block, result) */
}
static int user_block_used(uint32_t a)
{
    for (uint32_t i = 0; i < STORE_BLOCK; i++) if (fake_flash[a + i] != 0xFF) return 1;
    return 0;
}
static void t_factory_reset(void)
{
    fake_reset();
    store_init();
    seed_init();
    int objs[3] = { 1, 2, 3 }, vers[3] = { 1, 1, 1 };
    CHECK(save_versions(REC_PATTERN, objs, vers, 3) == STORE_OK, "factory reset: three patterns saved");
    CHECK(store_save_begin() == STORE_OK && store_saving() && seed_wipe() == -1 && !seed_active(),
          "factory reset: refused while a save is being made");
    store_save_cancel();
    int used = 0;
    for (uint32_t a = STORE_U1_START; a < STORE_U2_END; a += STORE_BLOCK)
        if ((a < STORE_U1_END || a >= STORE_U2_START) && user_block_used(a)) used++;
    long e0 = fake_erases, r0 = fake_restarts;
    CHECK(used > 0 && seed_wipe() == 0 && seed_wiping() && seed_active(), "factory reset: started (%d blocks in use)", used);
    CHECK(seed_wipe() == -1, "factory reset: a second one refused while it runs");
    static const uint8_t hello[] = { 0xF0, 0x7D, 0x50, 0x54, 0x01, 0x00, 0x01, 0xF7 };
    fake_sysex = wipe_acks;
    acks = 0;
    seed_sysex(hello, sizeof hello);
    CHECK(acks == 1 && ack_result == SEED_BUSY, "factory reset: a seeding's HELLO answered busy (%d, %d)", acks, ack_result);
    fake_sysex_refused = 1;                             /* (another HELLO while USB takes nothing: its answer waits, and
                                                         * must not hold the erase up) */
    seed_sysex(hello, sizeof hello);
    int polls = 0;
    while (fake_restarts == r0 && polls < 1000) { seed_poll(); polls++; }
    seed_info_t si;
    seed_info(&si);
    int left = 0;
    for (uint32_t a = STORE_U1_START; a < STORE_U2_END; a += STORE_BLOCK)
        if ((a < STORE_U1_END || a >= STORE_U2_START) && user_block_used(a)) left++;
    CHECK(fake_restarts == r0 + 1 && polls == 104 && si.done == 103 && si.total == 103 && !left && fake_erases - e0 == used,
          "factory reset: 103 blocks, a poll each, then the restart, though USB took nothing (polls %d, %u of %u, %d left, "
          "%ld erased of %d in use)", polls, si.done, si.total, left, fake_erases - e0, used);
    fake_sysex_refused = 0;
    for (int i = 0; i < 10; i++) seed_poll();
    CHECK(fake_restarts == r0 + 1 && seed_active(), "factory reset: the restart asked once; meanwhile it waits");
    fake_sysex = 0;
    seed_init();                                        /* the restart */
    store_init();
    CHECK(version_held(REC_PATTERN, 1) == -1 && version_held(REC_PATTERN, 3) == -1 && !seed_active(),
          "factory reset: the next start finds nothing");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    t_crc();
    t_empty();
    t_basic();
    t_builtin();
    t_cart_kind();
    t_cuts();
    t_crc_fail();
    t_churn();
    t_churn_cuts();
    t_full();
    t_leftovers();
    t_audio();
    t_samples_audio();
    t_sampler_v2();
    t_sampler_v2_slots();
    t_sample_dels_first();
    t_sample_dels_cuts();
    t_old_form();
    t_check_samples();
    t_layout();
    t_deleted_stays();
    t_audio_delete_stays();
    t_bad_count();
    t_old_store();
    t_compact_deletes();
    t_map_after_flush();
    t_ff_runs();
    t_edits();
    t_pieces();
    t_edits_far();
    t_cart_pieces();
    t_loops_pieces();
    t_loops_parts();
    t_stage();
    t_editor_room();
    t_editor_room_delete();
    t_split_cuts();
    t_kb_map();
    t_spare();
    t_rows_cap();
    t_factory_reset();
    t_favorites();
    t_builtin_extras();                                 /* (last: its extras stay) */
    printf("store: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
