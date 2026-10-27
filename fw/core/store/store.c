/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The store (specs/files/store.md): a log of records in the flash, no directory. The map in RAM says
 * where each object's newest version is; a save appends at the head; compaction frees blocks.
 *
 * A save is all or nothing: its last record carries flag bit 0, written last. A save without a valid last
 * record (a power cut while saving or seeding) is the newest at the next power-on, which spoils its records
 * so that no later mount takes them (store_mount); its number is never used again.
 *
 * Only the user area is read and written (specs/files/store.md#where-things-go): FWS holds the firmware and its
 * built-in things, never written while running; its free part is never read.
 *
 * RAM (specs/files/ram.md): the edit memory (67 KB: the pieces and their list), the map (8 B an object), one scratch
 * buffer shared by the mount, a save's list or the record a save builds, and seeding's messages (they never run
 * together), and a page and a few small tables. */
#include <string.h>
#include "store.h"
#include "builtin.h"
#include "crc.h"
#include "../plat.h"
/* (the platform's long-work and lock calls: no-ops where a platform has none, in plat_weak.c, never here) */

#define MAP_MAX   560                   /* objects, 8 B each (store.md: ~600 objects in a full store) */
#define F_LAST    1u
#define F_DEL     2u
#define NBLK      256                   /* 4 KB blocks in the 1 MiB flash */
#define ALIGN4(n) (((n) + 3u) & ~3u)
#define BLK(a)    ((a) >> 12)

/* A map entry, 8 B: kl = kind << 16 | num << 8 | sub, and the payload length's bits 12-19 in bits 24-31;
 * al = the header's address >> 2 (bits 0-17), the length's bits 0-11 << 18, deleted << 30. The version is
 * read from the record's header when asked for. */
typedef struct { uint32_t kl, al; } ent_t;

/* A record of a save being made (the save API's list, in the scratch): 12 B on the FM-1. */
typedef struct {
    uint8_t kind, num, sub, ver;
    const uint8_t *p;
    uint16_t lenf;                      /* the payload's length | 0x8000 deleted */
} stage_t;

#define L_DEL 0x8000u                   /* (a save's list: deleted) */
#define L_LEN(f) ((uint32_t)((f) & 0x3FFFu))

/* the scratch (STORE_SCRATCH_BYTES) holds the mount's saves (one a map entry), or a save's list, or the record the
   edits' save builds, or seeding's messages, or VISUAL's frames: never two at once */
typedef char scratch_mount[MAP_MAX * 4 <= STORE_SCRATCH_BYTES ? 1 : -1];
typedef char scratch_seed[STORE_SEED_SCRATCH <= STORE_SCRATCH_BYTES ? 1 : -1];
typedef char scratch_rec[STORE_MAX_REC <= STORE_SCRATCH_BYTES ? 1 : -1];
/* (an edit's offset: 16 bits in 4-byte units) */
typedef char emem_fits_offsets[STORE_EDIT_MAX % 4u == 0 && STORE_EDIT_MAX <= 0xFFFFu * 4u ? 1 : -1];

static PLAT_POOL ent_t map[MAP_MAX];
static int map_n;
/* the store's generation (store.h): up whenever what a reader may hold a pointer into can change: the map (a
 * record put, dropped, mounted), the edits' pieces (tell) */
static volatile uint32_t gen_n;
uint32_t store_gen(void) { return gen_n; }
static PLAT_POOL uint32_t scratch32[STORE_SCRATCH_BYTES / 4];
uint32_t store_ram_index(void) { return (uint32_t)(sizeof map + sizeof scratch32); }
static int claimed;                     /* seeding holds the scratch */
static uint8_t blk_live[NBLK];          /* live bytes in each block (headers, payloads, padding), in units of
                                           16 B rounded up: 0 none, 255 4,080 or more */
static uint8_t blk_audio[NBLK / 8];     /* a block holding live audio: never compacted nor appended to */
static uint8_t taken[NBLK / 8];         /* blocks a save being placed has opened (only while placing) */
static uint32_t last_save;              /* the newest complete save */
static uint32_t next_save;              /* the number the next save takes */
static uint32_t head;                   /* where the next record goes; 0: open the next free block */
static uint32_t head_from;              /* the block the search for a free one starts after */
static uint16_t n_bad, n_lost;
static int locked;
static uint8_t pg[256];                 /* the page being written */
static uint32_t pg_addr = 1;            /* its address; 1: no page held */
static uint32_t pg_lo, pg_hi;           /* the part of it written */
#define PEND_MAX 8
static struct { uint32_t keyd, addr, len; } pend[PEND_MAX];    /* records written, not yet in the map
                                                                   (wr_flush); keyd: the key | deleted << 24 */
static int npend;

const uint8_t store_kinds[STORE_NKINDS] = {
    REC_DX7, REC_JUNO, REC_TB303, REC_SAMPLER, REC_FUSION, REC_CHIP, REC_AUDIO, REC_PATTERN, REC_SONG, REC_LOOPS,
    REC_DJ, REC_SETTINGS, REC_MIDIMAP
};

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t key_of(uint8_t k, uint8_t n, uint8_t s) { return (uint32_t)k << 16 | (uint32_t)n << 8 | s; }
static uint32_t ent_key(const ent_t *e) { return e->kl & 0xFFFFFFu; }
static uint8_t ent_kind(const ent_t *e) { return (uint8_t)(e->kl >> 16); }
static uint8_t ent_num(const ent_t *e) { return (uint8_t)(e->kl >> 8); }
static uint8_t ent_sub(const ent_t *e) { return (uint8_t)e->kl; }
static uint32_t ent_addr(const ent_t *e) { return (e->al & 0x3FFFFu) << 2; }
static uint32_t ent_len(const ent_t *e) { return (e->kl >> 24) << 12 | (e->al >> 18 & 0xFFFu); }
static int ent_del(const ent_t *e) { return (int)(e->al >> 30 & 1u); }
static uint32_t ent_span(const ent_t *e) { return STORE_HDR + ALIGN4(ent_len(e)); }
static void ent_set(ent_t *e, uint32_t key, uint32_t addr, uint32_t len, int del)
{
    e->kl = key | (len >> 12) << 24;
    e->al = addr >> 2 | (len & 0xFFFu) << 18 | (uint32_t)(del ? 1 : 0) << 30;
}
/* the built-ins: builtin.c's, and those the firmware makes itself at start (store_builtin_extra: the keys' song, the
 * EDITOR's own pattern) */
#define BI_EXTRA_MAX 4
static builtin_rec_t bi_extra[BI_EXTRA_MAX];
static int bi_nextra;
void store_builtin_extra(const builtin_rec_t *r)
{
    int i = 0;
    while (i < bi_nextra && !(bi_extra[i].kind == r->kind && bi_extra[i].num == r->num && bi_extra[i].sub == r->sub)) i++;
    if (i == BI_EXTRA_MAX) return;
    bi_extra[i] = *r;                   /* (the same one again: replaced) */
    if (i == bi_nextra) bi_nextra++;
}
static int bi_count(void) { return builtin_count + bi_nextra; }
static const builtin_rec_t *bi_rec(int i) { return i < builtin_count ? &builtin_recs[i] : &bi_extra[i - builtin_count]; }
int store_builtin_count(void) { return bi_count(); }
const builtin_rec_t *store_builtin_at(int i) { return bi_rec(i); }
static int is_builtin(uint8_t kind, uint8_t num)
{
    if (num >= 0xF0 && ((kind >= REC_DX7 && kind <= REC_CART_LAST) || kind == REC_PATTERN)) return 1;
    for (int i = 0; i < bi_nextra; i++) if (kind == bi_extra[i].kind && num == bi_extra[i].num) return 1;
    return 0;
}
static int is_cart(uint8_t kind) { return kind >= REC_DX7 && kind <= REC_CART_LAST; }
/* the user area's two regions: the FM-1's; in the editor's WebAssembly the preview may take the whole flash instead
 * (store.h, store_preview_room: region 1 all of it, region 2 empty) */
#ifdef PTFM_EDITOR
static uint32_t U1_START = STORE_U1_START, U1_END = STORE_U1_END, U2_START = STORE_U2_START, U2_END = STORE_U2_END;
void store_preview_room(int on)
{
    U1_START = on ? STORE_P_START : STORE_U1_START;
    U1_END = on ? STORE_P_END : STORE_U1_END;
    U2_START = on ? STORE_P_END : STORE_U2_START;
    U2_END = on ? STORE_P_END : STORE_U2_END;
}
#else
#define U1_START STORE_U1_START
#define U1_END   STORE_U1_END
#define U2_START STORE_U2_START
#define U2_END   STORE_U2_END
#endif
static int in_user(uint32_t a)
{
    return (a >= U1_START && a < U1_END) || (a >= U2_START && a < U2_END);
}
static int blk_is_audio(uint32_t b) { return blk_audio[b >> 3] >> (b & 7) & 1; }

/* ---- reading the flash ---- */
static int rd(uint32_t addr, void *dst, uint32_t n)     /* the user area, read in place */
{
    const uint8_t *p = plat_flash_ptr(addr);
    if (p) { memcpy(dst, p, n); return 0; }
    return plat_flash_read(addr, dst, n);
}

static uint8_t ent_ver(const ent_t *e)
{
    uint8_t v = 0;
    rd(ent_addr(e) + 3, &v, 1);
    return v;
}

uint32_t store_crc_flash(uint32_t c, uint32_t addr, uint32_t n)
{
    const uint8_t *p = plat_flash_ptr(addr);
    if (p) return crc32_update(c, p, n);
    uint8_t buf[128];
    while (n) {
        uint32_t k = n < sizeof buf ? n : sizeof buf;
        if (plat_flash_read(addr, buf, k)) return ~c;     /* a read that failed never checks */
        c = crc32_update(c, buf, k);
        addr += k; n -= k;
    }
    return c;
}

int store_all_ff(uint32_t addr, uint32_t n)
{
    const uint8_t *p = plat_flash_ptr(addr);
    uint8_t buf[128];
    while (n) {
        uint32_t k = n < sizeof buf ? n : sizeof buf;
        const uint8_t *q = p;
        if (!q) { if (plat_flash_read(addr, buf, k)) return 0; q = buf; }
        for (uint32_t i = 0; i < k; i++) if (q[i] != 0xFF) return 0;
        addr += k; n -= k;
        if (p) p += k;
    }
    return 1;
}

/* ---- walking the records ---- */
/* Calls fn for every record whose header is plausible and whose CRC checks (store.md#power-on), from start to end.
 * A record that fails costs only itself: the walk tries each following 4-byte step. Kind 0xFF (erased flash) ends
 * a block's records. */
typedef void (*rec_fn)(uint32_t pos, const uint8_t *h, uint32_t len);
static int walk_count_bad;
static rec_fn walk_bad;                         /* CHECK STORAGE: a plausible record that fails its CRC */

static int plausible(const uint8_t *h, uint32_t pos, uint32_t end, uint32_t len)
{
    uint8_t k = h[0], fl = h[7];
    if (k == 0x00 || k >= 0xE0) return 0;               /* 0: no kind (spoilt); E0-EF disk only; FF erased */
    if (fl & ~(F_LAST | F_DEL)) return 0;
    if ((fl & F_DEL) && len) return 0;
    if (pos + STORE_HDR + len > end || pos + STORE_HDR + len < pos) return 0;
    if (k != REC_AUDIO && (pos & 0xFFFu) + STORE_HDR + len > STORE_BLOCK) return 0;
    return 1;
}

/* the first byte from a to end that is not 0xFF; end when there is none */
static uint32_t first_not_ff(uint32_t a, uint32_t end)
{
    uint8_t buf[64];
    while (a < end) {
        uint32_t k = end - a < sizeof buf ? end - a : sizeof buf;
        const uint8_t *q = plat_flash_ptr(a);
        if (!q) { if (plat_flash_read(a, buf, k)) return end; q = buf; }
        for (uint32_t i = 0; i < k; i++) if (q[i] != 0xFF) return a + i;
        a += k;
    }
    return end;
}

static void walk(uint32_t start, uint32_t end, rec_fn fn)
{
    uint32_t pos = start, blk = 0xFFFFFFFFu;
    int stepping = 0;
    while (pos + STORE_HDR <= end) {
        if (BLK(pos) != blk) { blk = BLK(pos); plat_idle(); }  /* (a walk takes a while: the main loop lives) */
        uint8_t h[STORE_HDR];
        if (rd(pos, h, STORE_HDR)) { pos = (pos | 0xFFFu) + 1; continue; }
        if (h[0] == 0xFF) {
            /* erased flash ends the block's records; while stepping past a bad record a 0xFF may be its
               data: then the rest of the block erased ends them, else the stepping goes on from the next
               byte that is not 0xFF (the run looked over once) */
            uint32_t be = (pos | 0xFFFu) + 1;
            if (!stepping) { pos = be; continue; }
            uint32_t q = first_not_ff(pos, be);
            if (q >= be) { pos = be; stepping = 0; continue; }
            pos = (q & ~3u) > pos ? (q & ~3u) : pos + 4;
            continue;
        }
        uint32_t len = h[4] | h[5] << 8 | (uint32_t)h[6] << 16;
        int ok = plausible(h, pos, end, len);
        if (ok) {
            uint32_t c = crc32_update(0, h, 12);
            if (len) c = store_crc_flash(c, pos + STORE_HDR, len);
            ok = c == le32(h + 12);
            if (!ok && walk_bad && !stepping) walk_bad(pos, h, len);
        }
        if (!ok) {
            if (!stepping && walk_count_bad && h[0]) n_bad++;   /* (kind 0: spoilt, or not a record at all) */
            stepping = 1;
            pos += 4;
            continue;
        }
        stepping = 0;
        fn(pos, h, len);
        pos += STORE_HDR + ALIGN4(len);
    }
}

static void walk_all(rec_fn fn)                 /* the user area, both its regions */
{
    walk(U1_START, U1_END, fn);
    walk(U2_START, U2_END, fn);
}

/* ---- the map ---- */
static int map_find(uint32_t key, int *at)
{
    int lo = 0, hi = map_n;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        uint32_t k = ent_key(&map[mid]);
        if (k == key) { if (at) *at = mid; return mid; }
        if (k < key) lo = mid + 1; else hi = mid;
    }
    if (at) *at = lo;
    return -1;
}

/* Puts (kind, num, sub) at addr; saves (mounting) holds each entry's save number: the newest wins, and in
 * one save a live record wins over a deleted one. Without saves (a save being written) it always wins. */
static void map_put(uint32_t key, uint32_t addr, uint32_t len, int del, uint32_t save, uint32_t *saves)
{
    int at, i = map_find(key, &at);
    if (i >= 0) {
        if (saves) {
            if (save < saves[i]) return;
            if (save == saves[i] && del) return;
            saves[i] = save;
        }
    } else if (map_n >= MAP_MAX) {
        n_lost++;
        return;
    }
    uint32_t was = plat_lock();                 /* (the audio context reads the map: never a torn entry) */
    gen_n++;
    if (i < 0) {
        memmove(&map[at + 1], &map[at], (uint32_t)(map_n - at) * sizeof map[0]);
        if (saves) { memmove(&saves[at + 1], &saves[at], (uint32_t)(map_n - at) * sizeof saves[0]); saves[at] = save; }
        map_n++;
        i = at;
    }
    ent_set(&map[i], key, addr, len, del);
    plat_unlock(was);
}

static void map_drop(int i)
{
    uint32_t was = plat_lock();
    gen_n++;
    memmove(&map[i], &map[i + 1], (uint32_t)(map_n - i - 1) * sizeof map[0]);
    map_n--;
    plat_unlock(was);
}

static void blocks_count(void)
{
    memset(blk_live, 0, sizeof blk_live);
    memset(blk_audio, 0, sizeof blk_audio);
    for (int i = 0; i < map_n; i++) {
        uint32_t a = ent_addr(&map[i]), end = a + ent_span(&map[i]);
        while (a < end) {
            uint32_t b = BLK(a), be = (b + 1) << 12, n = (end < be ? end : be) - a;
            uint32_t u = blk_live[b] + (n + 15) / 16;
            blk_live[b] = (uint8_t)(u > 255 ? 255 : u);
            if (ent_kind(&map[i]) == REC_AUDIO && !ent_del(&map[i]))  /* (a deleted one is a header like any) */
                blk_audio[b >> 3] |= (uint8_t)(1u << (b & 7));
            a += n;
        }
    }
}

/* ---- mounting ----
 * A save counts only once its last record (flag bit 0) is there and checks. Whether it is can be told
 * only while that save is the newest: later, compaction may have erased an old save's last record (its
 * block freed once the record was dead) while others of its records live on. So the newest save, found
 * without a valid last record at power-on (a power cut while saving or seeding), has its records spoilt
 * there and then: their kind byte programmed to 0, so their CRC fails for good. That is the only write
 * a power-on ever makes, a 1-byte program a record, and only after such a cut. */
static uint32_t m_max, m_seen, m_last_end, m_ignore[4];
static int m_has_last, m_nignore, m_walks;
static uint32_t m_spoil;

static int ignored(uint32_t save)
{
    for (int i = 0; i < m_nignore; i++) if (m_ignore[i] == save) return 1;
    return 0;
}

static int builtin_has(uint8_t kind, uint8_t num);
static void on_mount(uint32_t pos, const uint8_t *h, uint32_t len)
{
    uint32_t save = le32(h + 8);
    if (save > m_seen) m_seen = save;
    if (ignored(save)) return;
    if (save > m_max) { m_max = save; m_has_last = 0; }
    if (save == m_max && (h[7] & F_LAST)) { m_has_last = 1; m_last_end = pos + STORE_HDR + ALIGN4(len); }
    /* (F0-FF too: a built-in thing changed and saved, found before the firmware's own; but a cartridge of a kind
     * the firmware has no built-in of under its number, an earlier build's, is left out: dead, so the compaction
     * drops it) */
    if (is_cart(h[0]) && h[1] >= 0xF0 && !builtin_has(h[0], h[1])) return;
    map_put(key_of(h[0], h[1], h[2]), pos, len, (h[7] & F_DEL) != 0, save, scratch32);
}

static void on_spoil(uint32_t pos, const uint8_t *h, uint32_t len)
{
    (void)len;
    if (le32(h + 8) == m_spoil) {
        static const uint8_t zero = 0;
        plat_flash_prog(pos, &zero, 1);
    }
}

void store_mount(void)
{
    pg_addr = 1;                                        /* no page half written */
    npend = 0;
    m_nignore = 0;
    m_walks = 0;
    for (;;) {
        gen_n++;                                        /* (the map is built anew) */
        map_n = 0; n_bad = 0; n_lost = 0;
        m_max = 0; m_seen = 0; m_has_last = 0; m_last_end = 0;
        walk_count_bad = 1;
        walk_all(on_mount);
        walk_count_bad = 0;
        m_walks++;
        if (!m_max || m_has_last || m_nignore >= 4) break;
        /* the newest save never finished: spoil its records, read again without them (and should a
           program fail, its number stays set aside, as it is never used again: next_save is past it) */
        m_spoil = m_max;
        walk_all(on_spoil);
        m_walks++;
        m_ignore[m_nignore++] = m_max;
    }
    last_save = m_max;
    next_save = m_seen + 1;
    memset(taken, 0, sizeof taken);
    blocks_count();
    head = 0;
    head_from = U2_END - STORE_BLOCK;                   /* the search then starts at region 1's first */
    if (m_last_end && in_user(m_last_end - 1)) {
        uint32_t b = BLK(m_last_end - 1);
        head_from = b << 12;
        if ((m_last_end & 0xFFFu) && !blk_is_audio(b) &&
            store_all_ff(m_last_end, ((b + 1) << 12) - m_last_end))
            head = m_last_end;
    }
}

enum { S_IDLE, S_STAGING, S_WRITING, S_RESULT };
static int s_state;

static void saving_none(void);
void store_init(void)
{
    locked = 0;
    claimed = 0;
    s_state = S_IDLE;
    saving_none();
    store_mount();
}

/* ---- reading ---- */
static const builtin_rec_t *builtin_find(uint8_t kind, uint8_t num, uint8_t sub)
{
    for (int i = 0; i < bi_count(); i++)
        if (bi_rec(i)->kind == kind && bi_rec(i)->num == num && bi_rec(i)->sub == sub)
            return bi_rec(i);
    return 0;
}

/* ---- the edit memory (store.h; specs/files/ram.md#editing-and-saving) ----
 * What differs from the flash, as pieces, each piece once. The pieces lie from emem's bottom up (to ptop), each in
 * a room of its length rounded up to 4; their list, 8 B an entry sorted by key, from its top down (from lbase). A
 * piece changed again is rewritten where it lies when it fits its room (what it leaves is free), else it goes to
 * the pieces' top and its old room is free; free room between pieces is joined when room runs short, moving pieces
 * down one at a time (squeeze). The audio context reads the list and the pieces: every change is made with it held
 * off (plat_lock) and told meanwhile to whoever reads them (the watch: the player puts its place on what changed).
 * An entry: kv = its key (kind << 17 | num << 9 | id: a track, a slot, a head, the samples' table, a whole record's
 * sub; PC_OBJ the thing's own) | its version << 25; its piece's offset in 4-byte units (a room starts on a multiple
 * of 4: 256 KB reach), the length | flags. Kinds with bit 7 set (K_SESS) are an edit mode's copies
 * (store_edit_session): never read as edits, never saved, never told. */
#define PC_OBJ 0x1FFu                   /* a thing's own entry: PF_DEL, its flash record left out */
#define K_SESS 0x80u
typedef struct { uint32_t kv; uint16_t off, lenf; } pc_t;
#define PF_DEL 0x8000u                  /* a whole record deleted; PC_OBJ: the flash's record left out */
#define PF_NONE 0x4000u                 /* (an edit mode's copy: there was no entry) */
#define PF_LEN(f) ((uint32_t)((f) & 0x1FFFu))
#define PK_MASK 0x1FFFFFFu
static uint32_t pkey(uint32_t kind, uint32_t num, uint32_t id) { return kind << 17 | num << 9 | id; }
static PLAT_POOL uint32_t emem32[STORE_EDIT_MAX / 4];
#define emem ((uint8_t *)emem32)
static uint32_t ptop;                           /* the pieces' top */
static uint32_t lbase = STORE_EDIT_MAX;         /* the list: lbase .. STORE_EDIT_MAX */
static uint32_t pused;                          /* the pieces' rooms: ptop - pused is free between them */
static uint32_t edit_moves;
static int edit_saving;                         /* the save under way is the edits' */
static uint8_t sess_on;                         /* an edit mode's copies are kept */
static uint32_t sess_of;                        /* ... of this thing's entries only (kind << 8 | num, + 1); 0 every one */
static store_watch_fn watch;
void store_set_watch(store_watch_fn fn) { watch = fn; }
uint32_t store_edit_moves(void) { return edit_moves; }

static int npc(void) { return (int)((STORE_EDIT_MAX - lbase) >> 3); }
static pc_t *pcs(void) { return (pc_t *)(void *)(emem + lbase); }
static uint32_t pc_key(const pc_t *e) { return e->kv & PK_MASK; }
static uint32_t pc_off(const pc_t *e) { return (uint32_t)e->off << 2; }        /* its piece's offset in bytes */
static uint8_t *pc_at(const pc_t *e) { return emem + pc_off(e); }
static int pc_lower(uint32_t key)               /* the first entry whose key is key or more */
{
    const pc_t *e = pcs();
    int lo = 0, hi = npc();
    while (lo < hi) {
        int m = (lo + hi) >> 1;
        if (pc_key(&e[m]) < key) lo = m + 1;
        else hi = m;
    }
    return lo;
}
static int pc_find(uint32_t key)
{
    int i = pc_lower(key);
    return i < npc() && pc_key(&pcs()[i]) == key ? i : -1;
}
static int piece_kind(uint8_t k) { return k == REC_PATTERN || k == REC_LOOPS || is_cart(k); }
static int is_sess(uint32_t key) { return (int)(key >> 24 & 1u); }      /* (kind bit 7) */
static int sess_wants(uint32_t key)             /* an edit mode keeps a copy of this entry */
{
    return sess_on && !is_sess(key) && (!sess_of || (key >> 9) + 1u == sess_of);
}
static void tell(uint32_t key)                  /* (the audio context held off) */
{
    gen_n++;                                    /* (first: the watch's readers see the change) */
    if (watch && !is_sess(key)) watch((uint8_t)(key >> 17), (uint8_t)(key >> 9), (uint16_t)(key & 0x1FFu));
}
static int obj_hidden(uint8_t kind, uint8_t num)
{
    int i = pc_find(pkey(kind, num, PC_OBJ));
    return i >= 0 && (pcs()[i].lenf & PF_DEL);
}

/* the free room between pieces joined: the lowest piece above the joined part moved down to it, in turn; each a
 * short step with the audio context held off, told (a pointer into a piece taken before is stale: edit_moves) */
static void squeeze(void)
{
    uint32_t at = 0;
    for (;;) {
        const pc_t *e = pcs();
        int n = npc(), b = -1;
        uint32_t bo = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++)
            if (PF_LEN(e[i].lenf) && pc_off(&e[i]) >= at && pc_off(&e[i]) < bo) { bo = pc_off(&e[i]); b = i; }
        if (b < 0) break;
        if (bo != at) {
            uint32_t was = plat_lock();
            memmove(emem + at, emem + bo, PF_LEN(e[b].lenf));
            pcs()[b].off = (uint16_t)(at >> 2);
            edit_moves++;
            tell(pc_key(&pcs()[b]));
            plat_unlock(was);
        }
        at += ALIGN4(PF_LEN(e[b].lenf));
    }
    ptop = at;
}
static uint32_t room_free(void) { return lbase - pused; }      /* (above the top and between the pieces) */
static uint8_t held;                            /* store_edit_hold: nothing rewritten in place, nothing moved */
static int room(uint32_t n)                     /* n bytes clear between the pieces' top and the list */
{
    if (lbase - ptop >= n) return 1;
    if (held || room_free() < n) return 0;
    squeeze();
    return lbase - ptop >= n;
}
void store_edit_hold(int on) { held = (uint8_t)(on != 0); }
int store_edit_reserve(uint32_t n) { return !locked && !edit_saving && room(n); }

static int sess_keep(uint32_t key);
/* The entry of key set: its version, these bytes (len, at most 4 KB, never lying in emem) and flags; written where
 * it lies when they fit its room, else at the pieces' top; told. STORE_ERR_FULL: nothing changed. */
static int pc_put(uint32_t key, uint8_t ver, const void *p, uint32_t len, uint32_t flags)
{
    if (sess_wants(key) && sess_keep(key)) return STORE_ERR_FULL;
    uint32_t a = ALIGN4(len);
    int i = pc_find(key);
    uint32_t had = i >= 0 ? ALIGN4(PF_LEN(pcs()[i].lenf)) : 0u;
    int move = a > had || (held && a);          /* (a new room at the top: it does not fit, or held: the old bytes
                                                 * stay where they are) */
    uint32_t need = (move ? a : 0u) + (i < 0 ? 8u : 0u);
    if (need && !room(need)) return STORE_ERR_FULL;
    i = pc_find(key);                           /* (squeezed: it may have moved) */
    uint32_t off = move ? ptop : i >= 0 ? pc_off(&pcs()[i]) : 0u;
    uint32_t was = plat_lock();
    if (len) memcpy(emem + off, p, len);
    if (move) {                                 /* (the old room free) */
        if (had) edit_moves++;
        ptop += a;
        pused = pused + a - had;
    } else {
        if (!held && i >= 0 && off + had == ptop) ptop = off + a;    /* (the top piece: the top comes down) */
        pused -= had - a;
    }
    if (i < 0) {
        i = pc_lower(key);
        memmove(emem + lbase - 8u, emem + lbase, 8u * (uint32_t)i);
        lbase -= 8u;
    }
    pc_t *e = &pcs()[i];
    e->kv = key | (uint32_t)(ver & 0x7Fu) << 25;
    e->off = (uint16_t)(len ? off >> 2 : 0u);
    e->lenf = (uint16_t)(len | flags);
    tell(key);
    plat_unlock(was);
    return STORE_OK;
}
static void pc_drop(uint32_t key)               /* the entry of key gone, its room free; told */
{
    if (sess_wants(key) && pc_find(key) >= 0) sess_keep(key);  /* (no room for the copy: gone all the same) */
    int i = pc_find(key);
    if (i < 0) return;
    uint32_t was = plat_lock();
    pc_t *e = &pcs()[i];
    uint32_t had = ALIGN4(PF_LEN(e->lenf));
    if (!held && had && pc_off(e) + had == ptop) ptop = pc_off(e);
    pused -= had;
    memmove(emem + lbase + 8u, emem + lbase, 8u * (uint32_t)i);
    lbase += 8u;
    edit_moves++;
    tell(key);
    plat_unlock(was);
}
static void obj_drop(uint8_t kind, uint8_t num) /* every entry of a thing gone */
{
    int i;
    while ((i = pc_lower(pkey(kind, num, 0))) < npc() && pc_key(&pcs()[i]) <= pkey(kind, num, PC_OBJ))
        pc_drop(pc_key(&pcs()[i]));
}

/* ---- an edit mode's copies (store.h) ----
 * The entry of key as it was before the mode's first change of it, kept under its kind | K_SESS (PF_NONE: there
 * was none). Its bytes are copied once the room for them is clear (pc_put then never squeezes them away). */
static int sess_keep(uint32_t key)
{
    uint32_t sk = key | K_SESS << 17;
    if (pc_find(sk) >= 0) return 0;
    int i = pc_find(key);
    if (i < 0) return pc_put(sk, 0, 0, 0, PF_NONE);
    uint32_t len = PF_LEN(pcs()[i].lenf);
    if (!room(ALIGN4(len) + 8u)) return STORE_ERR_FULL;
    i = pc_find(key);
    const pc_t *e = &pcs()[i];
    return pc_put(sk, (uint8_t)(e->kv >> 25), pc_at(e), len, e->lenf & PF_DEL);
}
static void sess_none(void)                     /* the copies dropped */
{
    int i;
    while ((i = pc_lower(K_SESS << 17)) < npc()) pc_drop(pc_key(&pcs()[i]));
}
void store_edit_session(int on)
{
    sess_on = 0;
    sess_of = 0;
    sess_none();
    sess_on = (uint8_t)(on != 0);
}
void store_edit_session_of(uint8_t kind, uint8_t num)
{
    store_edit_session(1);
    sess_of = ((uint32_t)kind << 8 | num) + 1u;
}
int store_edit_session_drop(void)
{
    int changed = 0, i;
    if (locked || edit_saving) return 0;        /* (a save under way writes the changes it found: the copies stay) */
    sess_on = 0;
    sess_of = 0;
    while ((i = pc_lower(K_SESS << 17)) < npc()) {
        pc_t c = pcs()[i];
        uint32_t sk = pc_key(&c), key = sk & ~(K_SESS << 17), len = PF_LEN(c.lenf);
        int k = pc_find(key);
        if (c.lenf & PF_NONE) {
            if (k >= 0) { pc_drop(key); changed = 1; }
        } else if (k < 0 || pcs()[k].kv != c.kv - (sk - key) || pcs()[k].lenf != c.lenf ||
                   memcmp(pc_at(&pcs()[k]), pc_at(&c), len)) {
            uint8_t *b = (uint8_t *)scratch32; /* (the copy's bytes out of emem first: putting may move them) */
            memcpy(b, pc_at(&c), len);
            pc_drop(sk);
            pc_put(key, (uint8_t)(c.kv >> 25), b, len, c.lenf & PF_DEL);
            changed = 1;
            continue;
        }
        pc_drop(sk);
    }
    return changed;
}

/* every edit gone (a SAVE has written them, or REVERT ALL), the copies too; each thing told once */
static void edits_none(void)
{
    sess_on = 0;
    sess_of = 0;
    while (npc()) {
        uint32_t k = pc_key(&pcs()[0]), was = plat_lock();
        lbase += 8u;                            /* (the first entry: nothing else moves) */
        if (!is_sess(k) && (!piece_kind((uint8_t)(k >> 17)) || !npc() || pc_key(&pcs()[0]) >> 9 != k >> 9))
            tell(piece_kind((uint8_t)(k >> 17)) ? (k | PC_OBJ) : k);   /* (a thing's last entry gone) */
        plat_unlock(was);
    }
    ptop = pused = 0;
    edit_moves++;
}
void store_edit_revert(void)
{
    if (edit_saving) return;
    edits_none();
}

/* a built-in thing (F0-FF) as the firmware has it: its changed copy, edited or saved, comes first; deleted, the
 * firmware's own is back */
static const uint8_t *builtin_get(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver)
{
    const builtin_rec_t *b = builtin_find(kind, num, sub);
    if (!b) return 0;
    if (len) *len = b->len;
    if (ver) *ver = b->ver;
    return b->data;
}
/* the flash's record (the user area in place, or the firmware's built-in one), the record the edits lie over: a
 * pattern's, a cartridge's or LOOPS' left out while their thing's entry says so (a built-in thing's own then) */
static const uint8_t *flash_rec(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver, int hide)
{
    int bi = is_builtin(kind, num);
    if (hide && piece_kind(kind) && obj_hidden(kind, num)) return bi ? builtin_get(kind, num, sub, len, ver) : 0;
    int i = map_find(key_of(kind, num, sub), 0);
    if (i < 0 || ent_del(&map[i])) return bi ? builtin_get(kind, num, sub, len, ver) : 0;
    const uint8_t *p = plat_flash_ptr(ent_addr(&map[i]) + STORE_HDR);
    if (!p) return 0;                                   /* (the user area always reads in place) */
    if (len) *len = ent_len(&map[i]);
    if (ver) *ver = p[3 - (int)STORE_HDR];             /* the header's byte 3, right before the payload */
    return p;
}
static const uint8_t *flash_get(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver)
{
    return flash_rec(kind, num, sub, len, ver, 1);
}
const uint8_t *store_get(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver)
{
    if (locked) return 0;
    if (!piece_kind(kind)) {                            /* a whole record: the edited one first */
        int i = pc_find(pkey(kind, num, sub));
        if (i >= 0) {
            const pc_t *e = &pcs()[i];
            if (e->lenf & PF_DEL) return 0;
            if (len) *len = PF_LEN(e->lenf);
            if (ver) *ver = (uint8_t)(e->kv >> 25);
            return pc_at(e);
        }
    }
    return flash_get(kind, num, sub, len, ver);
}
const uint8_t *store_get_saved(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver)
{
    return locked ? 0 : flash_rec(kind, num, sub, len, ver, 0);
}
const uint8_t *store_piece(uint8_t kind, uint8_t num, uint16_t id, uint32_t *len)
{
    if (locked) return 0;
    int i = pc_find(pkey(kind, num, id));
    if (i < 0 || (pcs()[i].lenf & PF_DEL)) return 0;
    if (len) *len = PF_LEN(pcs()[i].lenf);
    return pc_at(&pcs()[i]);
}
int store_edit_obj(uint8_t kind, uint8_t num)
{
    int i = pc_lower(pkey(kind, num, 0));
    if (i >= npc() || pc_key(&pcs()[i]) > pkey(kind, num, PC_OBJ)) return 0;
    return STORE_OBJ_EDITED | (obj_hidden(kind, num) ? STORE_OBJ_HIDDEN : 0);
}

static int obj_live(uint8_t kind, uint8_t num);
int store_info(uint8_t kind, uint8_t num, uint8_t sub, store_rec_t *r)
{
    memset(r, 0, sizeof *r);
    r->kind = kind; r->num = num; r->sub = sub;
    int bi = is_builtin(kind, num), i;
    if (piece_kind(kind) && store_edit_has(kind, num, 0) == 2) {  /* deleted by an edit: a built-in one's own is back */
        const builtin_rec_t *b = bi ? builtin_find(kind, num, sub) : 0;
        if (!b) return 0;
        r->ver = b->ver; r->area = STORE_AREA_BUILTIN; r->len = b->len;
        return 1;
    }
    if (piece_kind(kind) ? store_edit_obj(kind, num) != 0 : pc_find(pkey(kind, num, sub)) >= 0) {
        r->area = STORE_AREA_EDIT;                      /* (edited: the user area's, as a SAVE puts it there) */
        if (!piece_kind(kind)) return store_get(kind, num, sub, &r->len, &r->ver) != 0;
        r->ver = (uint8_t)(pcs()[pc_lower(pkey(kind, num, 0))].kv >> 25);
        return obj_live(kind, num);
    }
    i = map_find(key_of(kind, num, sub), 0);
    if (i < 0 || ent_del(&map[i])) {                    /* (a built-in thing unchanged, or its change deleted) */
        const builtin_rec_t *b = bi ? builtin_find(kind, num, sub) : 0;
        if (!b) return 0;
        r->ver = b->ver; r->area = STORE_AREA_BUILTIN; r->len = b->len;
        return 1;
    }
    r->ver = ent_ver(&map[i]);
    r->addr = ent_addr(&map[i]);
    r->area = STORE_AREA_USER;
    r->len = ent_len(&map[i]);
    return 1;
}

int store_read(uint8_t kind, uint8_t num, uint8_t sub, uint32_t off, void *dst, uint32_t n)
{
    store_rec_t r;
    if (locked || !store_info(kind, num, sub, &r) || off > r.len || n > r.len - off) return -1;
    if (r.area == STORE_AREA_EDIT) {                    /* (a thing of pieces: not one record in RAM) */
        if (piece_kind(kind)) return -1;
        memcpy(dst, store_get(kind, num, sub, 0, 0) + off, n);
        return 0;
    }
    if (r.area == STORE_AREA_BUILTIN) {
        memcpy(dst, builtin_find(kind, num, sub)->data + off, n);
        return 0;
    }
    return rd(r.addr + STORE_HDR + off, dst, n);
}

static int builtin_has(uint8_t kind, uint8_t num)       /* the firmware has one (a pattern: any part) */
{
    if (!is_builtin(kind, num)) return 0;
    for (int i = 0; i < bi_count(); i++)
        if (bi_rec(i)->kind == kind && bi_rec(i)->num == num && (kind == REC_PATTERN || !bi_rec(i)->sub))
            return 1;
    return 0;
}
/* (kind, num) live once the edits are counted: a pattern any of its parts; a built-in one always (its change
 * deleted, the firmware's own is back) */
static int obj_live(uint8_t kind, uint8_t num)
{
    if (!piece_kind(kind)) {
        int i = pc_find(pkey(kind, num, 0));
        if (i >= 0) return !(pcs()[i].lenf & PF_DEL);
        i = map_find(key_of(kind, num, 0), 0);
        return i >= 0 && !ent_del(&map[i]);
    }
    if (store_piece(kind, num, STORE_PC_HEAD, 0) || builtin_has(kind, num)) return 1;
    if (obj_hidden(kind, num)) return 0;
    int at;
    map_find(key_of(kind, num, 0), &at);
    for (int i = at; i < map_n && ent_kind(&map[i]) == kind && ent_num(&map[i]) == num; i++)
        if (!ent_del(&map[i]) && (kind == REC_PATTERN || !ent_sub(&map[i]))) return 1;
    return 0;
}

int store_list(uint8_t kind, uint8_t *nums, int max)
{
    uint8_t cand[32];                           /* the numbers to look at: saved, edited, built in */
    memset(cand, 0, sizeof cand);
    for (int i = 0; i < map_n; i++)
        if (ent_kind(&map[i]) == kind && !ent_del(&map[i]) && (kind == REC_PATTERN || !ent_sub(&map[i])))
            cand[ent_num(&map[i]) >> 3] |= (uint8_t)(1u << (ent_num(&map[i]) & 7));
    for (int i = pc_lower(pkey(kind, 0, 0)); i < npc() && pc_key(&pcs()[i]) >> 17 == kind; i++) {
        uint32_t k = pc_key(&pcs()[i]);
        if (piece_kind(kind) || !(k & 0x1FFu)) cand[k >> 12 & 31u] |= (uint8_t)(1u << (k >> 9 & 7u));
    }
    for (int i = 0; i < bi_count(); i++)
        if (bi_rec(i)->kind == kind) cand[bi_rec(i)->num >> 3] |= (uint8_t)(1u << (bi_rec(i)->num & 7));
    int n = 0;
    for (int num = 0; num <= 0xFF && n < max; num++)
        if ((cand[num >> 3] >> (num & 7) & 1) && obj_live(kind, (uint8_t)num)) nums[n++] = (uint8_t)num;
    return n;
}

/* ---- editing (store.h) ---- */
static const uint8_t ZERO1[1] = {0};    /* an empty track's stream */
static const uint8_t SLOT_SZ[REC_CART_LAST + 1] = {0, 112, 28, 24, 40, 16, 28};      /* a slot's bytes by cartridge kind */
static const uint16_t NAME_AT[REC_CART_LAST + 1] = {0, 784, 123, 42, 174, 8, 121};   /* its name's first character (6 bits) */
#define LOOPS_VER 4u                    /* LOOPS' record (specs/files/loops.md): the version read; its loop table, 12 B
                                         * a loop, then with loops a pattern payload's bytes and the streams */
#define LOOPS_TABLE 80u
#define LOOPS_ENTRY 12u
static uint32_t loops_head(uint32_t n) { return LOOPS_TABLE + LOOPS_ENTRY * n; }    /* its bytes before the streams */
static uint32_t cart_head(uint8_t kind) { return kind == REC_SAMPLER ? 11u : 10u; }
static int slot_empty(uint8_t kind, const uint8_t *s)
{
    uint32_t b = NAME_AT[kind];
    return ((uint32_t)(s[b >> 3] | s[(b >> 3) + 1u] << 8) >> (b & 7u) & 63u) == 63u;
}
static void slot_clear(uint8_t kind, uint8_t *s)        /* an empty slot: 0s, its name's first character 63 */
{
    uint32_t b = NAME_AT[kind];
    memset(s, 0, SLOT_SZ[kind]);
    s[b >> 3] |= (uint8_t)(63u << (b & 7u));
    s[(b >> 3) + 1u] |= (uint8_t)(63u >> (8u - (b & 7u)));
}
/* a track's stream at s (specs/files/patterns.md): its length with its 0x00; 0 when it runs past e */
static uint32_t strm_len(const uint8_t *s, const uint8_t *e)
{
    const uint8_t *q = s;
    while (q < e && *q) {
        uint32_t h = *q, a = h >> 5 & 3u;
        const uint8_t *n = q + 1u + (h >> 7) + (a == 3u ? 2u : a == 2u ? 1u : 0u) + (h >> 4 & 1u);
        if (h & 0x08u) {
            if (n + 1 >= e) return 0;
            n += n[0] == 0xFFu ? 2u + 2u * n[1] : 2u;
        }
        n += (h & 7u) == 7u;
        if (n > e) return 0;
        q = n;
    }
    return q < e ? (uint32_t)(q - s) + 1u : 0u;
}
uint32_t store_track_kept(const uint8_t *s, uint32_t n, uint32_t rows)
{
    const uint8_t *q = s, *e = s + n - 1u;              /* (e: its 0x00) */
    uint32_t row = 0;                                   /* the first row the next cell can be on */
    while (q < e && *q) {
        uint32_t h = *q, a = h >> 5 & 3u;
        const uint8_t *c = q + 1u + (h >> 7) + (a == 3u ? 2u : a == 2u ? 1u : 0u) + (h >> 4 & 1u);
        if (h & 0x08u) c += c[0] == 0xFFu ? 2u + 2u * c[1] : 2u;
        row += (h & 7u) == 7u ? *c++ : h & 7u;          /* (the cell's row) */
        if (row >= rows || c > e) break;
        row++;
        q = c;
    }
    return (uint32_t)(q - s) + 1u;
}
/* track t's stream among a pattern payload's (its streams from q, its track bits): 1 found */
static int track_in(const uint8_t *q, const uint8_t *e, uint32_t bits, uint32_t t, const uint8_t **p, uint32_t *len)
{
    for (uint32_t k = 0; k < 16u && q < e; k++) {
        if (!(bits >> k & 1u)) continue;
        uint32_t n = strm_len(q, e);
        if (!n) return 0;
        if (k == t) { *p = q; *len = n; return 1; }
        q += n;
    }
    return 0;
}

/* LOOPS' part sub in the flash, in the version read (another is as none here, and kept in the flash as it is) */
static const uint8_t *loops_part(uint8_t num, uint32_t sub, uint32_t *len)
{
    uint8_t v = 0;
    const uint8_t *q = flash_get(REC_LOOPS, num, (uint8_t)sub, len, &v);
    return q && v == LOOPS_VER ? q : 0;
}
/* The flash's piece (what an edit's piece lies over): 1 with its bytes (a track with no stream: a lone 0x00; a slot or
 * a table not stored: none, len 0), 0 when the flash has no such thing (or it cannot be read now) */
static int flash_piece(uint8_t kind, uint8_t num, uint32_t id, const uint8_t **p, uint32_t *len, uint8_t *ver)
{
    const uint8_t *q;
    uint32_t l;
    *p = 0;
    *len = 0;
    if (kind == REC_PATTERN) {
        int got = 0;
        for (uint32_t s = 0; s < 16u; s++) {
            if (!(q = flash_get(REC_PATTERN, num, (uint8_t)s, &l, ver)) || l < 5u) continue;
            if (!got && id == STORE_PC_HEAD) { *p = q; *len = 3; return 1; }
            got = 1;
            if (id < 16u && track_in(q + 5, q + l, q[3] | (uint32_t)q[4] << 8, id, p, len)) return 1;
        }
        if (got && id < 16u) { *p = ZERO1; *len = 1; }
        return got;
    }
    if (kind == REC_LOOPS) {                            /* (part 0: the head, then the payload; the others the payload) */
        if (!(q = loops_part(num, 0, &l))) return 0;
        uint32_t n = l > 5u ? q[5] : 0u, h = loops_head(n);
        if (ver) *ver = LOOPS_VER;
        if (id == STORE_PC_HEAD) { *p = q; *len = h < l ? h : l; return 1; }
        if (n && h + 5u <= l) {
            if (id == STORE_PC_PHEAD) { *p = q + h; *len = 3; return 1; }
            for (uint32_t s = 0; id < 16u && s < 16u; s++) {   /* (a loop's stream: in whichever part holds it) */
                if (s && !(q = loops_part(num, s, &l))) continue;
                if (s) h = 0;
                if (h + 5u <= l && track_in(q + h + 5u, q + l, q[h + 3u] | (uint32_t)q[h + 4u] << 8, id, p, len)) return 1;
            }
        }
        if (id < 16u) { *p = ZERO1; *len = 1; }
        return 1;
    }
    if (!(q = flash_get(kind, num, 0, &l, ver))) return 0;
    uint32_t hl = cart_head(kind), sz = SLOT_SZ[kind], n = l >= 10u ? q[9] : 0u, ns = kind == REC_SAMPLER && l > 10u ? q[10] : 0u;
    if (id == STORE_PC_HEAD) { *p = q; *len = hl <= l ? hl : l; }
    else if (id == STORE_PC_TABLE) { if (hl + sz * n + 16u * ns <= l) { *p = q + hl + sz * n; *len = 16u * ns; } }
    else if (id < n && hl + sz * (id + 1u) <= l) { *p = q + hl + sz * id; *len = sz; }
    return 1;
}
/* the flash has (kind, num, sub) but it cannot be read (a guard: the user area always reads in place) */
static int flash_lost(uint8_t kind, uint8_t num, uint8_t sub)
{
    int i = piece_kind(kind) && obj_hidden(kind, num) ? -1 : map_find(key_of(kind, num, sub), 0);
    return i >= 0 && !ent_del(&map[i]) && !flash_get(kind, num, sub, 0, 0);
}
/* a piece the same as the flash's: it goes, and nothing is unsaved by it (an empty slot is as none) */
static int as_flash(uint8_t kind, uint8_t num, uint32_t id, uint8_t ver, const uint8_t *b, uint32_t len)
{
    const uint8_t *q;
    uint32_t ql;
    uint8_t qv = 0;
    if (flash_piece(kind, num, id, &q, &ql, &qv) != 1)      /* (nothing there: an empty track or slot is as none) */
        return is_cart(kind) ? id < 0x100u && len == SLOT_SZ[kind] && slot_empty(kind, b) : id < 16u && len == 1u && !b[0];
    if (!q) return is_cart(kind) && id < 0x100u ? len == SLOT_SZ[kind] && slot_empty(kind, b) : !len && id == STORE_PC_TABLE;
    return qv == ver && ql == len && !memcmp(q, b, len);
}
static const void *outside(const void *p, uint32_t len)    /* (bytes from the edit memory itself: moved out first,
                                                             * as putting may move them) */
{
    const uint8_t *b = (const uint8_t *)p;
    if (b >= emem && b < emem + STORE_EDIT_MAX && len <= STORE_SCRATCH_BYTES) {
        memmove(scratch32, b, len);
        return scratch32;
    }
    return p;
}

int store_edit_piece(uint8_t kind, uint8_t num, uint16_t id, uint8_t ver, const void *p, uint32_t len)
{
    if (locked || edit_saving || !piece_kind(kind) || id >= PC_OBJ) return STORE_ERR_STATE;
    if (STORE_HDR + len > STORE_MAX_REC || (kind == REC_LOOPS && id < 16u && len > STORE_LOOP_STREAM_MAX))
        return STORE_ERR_BIG;
    p = outside(p, len);
    if (as_flash(kind, num, id, ver, p, len)) {
        pc_drop(pkey(kind, num, id));
        return STORE_OK;
    }
    return pc_put(pkey(kind, num, id), ver, p, len, 0);
}

/* a thing deleted: its pieces gone, its flash record left out (if it has one: a built-in thing's own is back) */
static int obj_delete(uint8_t kind, uint8_t num)
{
    int at, saved = 0;
    uint8_t v = 1;
    obj_drop(kind, num);
    map_find(key_of(kind, num, 0), &at);
    for (int i = at; i < map_n && ent_kind(&map[i]) == kind && ent_num(&map[i]) == num && !saved; i++)
        if (!ent_del(&map[i]) && (kind == REC_PATTERN || !ent_sub(&map[i]))) { saved = 1; v = ent_ver(&map[i]); }
    return saved ? pc_put(pkey(kind, num, PC_OBJ), v, 0, 0, PF_DEL) : STORE_OK;
}
/* the six cartridge kinds share one number: kind's there means another kind's goes (a sampler's samples with it) */
static int cart_other(uint8_t kind, uint8_t num)
{
    for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) if (k != kind && obj_live(k, num) && !builtin_has(k, num)) return 1;
    return 0;
}
static void cart_claim(uint8_t kind, uint8_t num)
{
    for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) {
        if (k == kind || !obj_live(k, num) || builtin_has(k, num)) continue;
        obj_delete(k, num);
        if (k == REC_SAMPLER)
            for (int j = 0; j < map_n; j++)
                if (ent_kind(&map[j]) == REC_AUDIO && ent_num(&map[j]) == num && !ent_del(&map[j]))
                    pc_put(pkey(REC_AUDIO, num, ent_sub(&map[j])), ent_ver(&map[j]), 0, 0, PF_DEL);
    }
}

/* A whole record laid out as its pieces (store_edit_put), in turn: piece j's id, bytes and length (bytes NULL: that
 * piece goes); 0 past the last. A pattern: its head, its 16 tracks (all in this one part); LOOPS: its head, then with
 * loops its streams' head and 16 streams (all in this one part; without loops, those go); a cartridge: its head, its
 * slots, a sampler's table. */
typedef struct {
    uint8_t kind;
    const uint8_t *b;
    uint32_t len, h, n;                 /* the head's bytes; a cartridge's slots */
    const uint8_t *trk[16];             /* a pattern's streams (NULL: none) */
    uint16_t tl[16];
} split_t;
/* 0: its layout holds (LOOPS': version 4's alone) */
static int split_init(split_t *s, uint8_t kind, uint8_t ver, const uint8_t *b, uint32_t len)
{
    uint32_t at = 0;
    memset(s, 0, sizeof *s);
    s->kind = kind; s->b = b; s->len = len;
    if (is_cart(kind)) {
        s->h = cart_head(kind);
        if (len < s->h) return -1;
        s->n = b[9];
        return s->h + SLOT_SZ[kind] * s->n + (kind == REC_SAMPLER ? 16u * b[10] : 0u) == len ? 0 : -1;
    }
    if (kind == REC_LOOPS) {
        if (ver != LOOPS_VER || len < loops_head(0)) return -1;
        s->n = b[5];
        s->h = loops_head(s->n);
        if (!s->n) return s->h <= len ? 0 : -1;
        at = s->h;
    }
    if (at + 5u > len) return -1;                       /* a pattern payload: its head, its tracks' streams */
    const uint8_t *q = b + at + 5u, *e = b + len;
    uint32_t bits = b[at + 3u] | (uint32_t)b[at + 4u] << 8;
    for (uint32_t t = 0; t < 16u; t++) {
        if (!(bits >> t & 1u)) continue;
        uint32_t n = strm_len(q, e);
        if (!n) return -1;
        s->trk[t] = q;
        s->tl[t] = (uint16_t)n;
        q += n;
    }
    return 0;
}
static int split_at(const split_t *s, uint32_t j, uint32_t *id, const uint8_t **p, uint32_t *l)
{
    if (is_cart(s->kind)) {
        uint32_t sz = SLOT_SZ[s->kind];
        if (!j) { *id = STORE_PC_HEAD; *p = s->b; *l = s->h; return 1; }
        if (j <= s->n) { *id = j - 1u; *p = s->b + s->h + sz * (j - 1u); *l = sz; return 1; }
        if (j == s->n + 1u && s->kind == REC_SAMPLER) { *id = STORE_PC_TABLE; *p = s->b + s->h + sz * s->n; *l = 16u * s->b[10]; return 1; }
        return 0;
    }
    uint32_t at = 0;
    if (s->kind == REC_LOOPS) {
        if (!j) { *id = STORE_PC_HEAD; *p = s->b; *l = s->h; return 1; }
        j--;
        at = s->h;
        if (!s->n) {                                    /* (no loops: no pattern) */
            if (j > 16u) return 0;
            *id = j ? j - 1u : STORE_PC_PHEAD;
            *p = 0;
            return 1;
        }
    }
    if (!j) { *id = s->kind == REC_LOOPS ? STORE_PC_PHEAD : STORE_PC_HEAD; *p = s->b + at; *l = 3; return 1; }
    if (j > 16u) return 0;
    *id = j - 1u;
    *p = s->trk[j - 1u] ? s->trk[j - 1u] : ZERO1;
    *l = s->trk[j - 1u] ? s->tl[j - 1u] : 1u;
    return 1;
}
static int put_split(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const uint8_t *b, uint32_t len)
{
    split_t s;
    uint32_t need = 0, id, l, j;
    const uint8_t *p;
    if (sub || split_init(&s, kind, ver, b, len)) return STORE_ERR_STATE;
    for (j = 0; kind == REC_LOOPS && j < 16u; j++) if (s.tl[j] > STORE_LOOP_STREAM_MAX) return STORE_ERR_BIG;
    int claim = is_cart(kind) && cart_other(kind, num);
    for (j = 0; split_at(&s, j, &id, &p, &l); j++) {    /* the room for every piece kept, first: all or nothing */
        if (!p || as_flash(kind, num, id, ver, p, l)) continue;
        int i = pc_find(pkey(kind, num, id));
        uint32_t had = i >= 0 ? ALIGN4(PF_LEN(pcs()[i].lenf)) : 0u;
        need += (ALIGN4(l) > had ? ALIGN4(l) : 0u) + (i < 0 ? 8u : 0u);
        if (sess_wants(pkey(kind, num, id)) && pc_find(pkey(kind | K_SESS, num, id)) < 0) need += had + 8u;
    }
    if (claim) need += 8u * 66u;                        /* (another kind's deleted: its entry, a sampler's samples') */
    if (need > room_free()) return STORE_ERR_FULL;
    if (claim) cart_claim(kind, num);
    for (j = 0; split_at(&s, j, &id, &p, &l); j++) {
        int r;
        if (!p || as_flash(kind, num, id, ver, p, l)) pc_drop(pkey(kind, num, id));
        else if ((r = pc_put(pkey(kind, num, id), ver, p, l, 0)) != STORE_OK) return r;
    }
    if (is_cart(kind)) {                                /* slots past the last one stored: their pieces go */
        int i;
        while ((i = pc_lower(pkey(kind, num, s.n))) < npc() && pc_key(&pcs()[i]) < pkey(kind, num, 0x100u))
            pc_drop(pc_key(&pcs()[i]));
    }
    return STORE_OK;
}

int store_edit_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len)
{
    if (locked || edit_saving) return STORE_ERR_STATE;
    if (STORE_HDR + len > STORE_MAX_REC) return STORE_ERR_BIG;
    payload = outside(payload, len);
    if (piece_kind(kind)) return put_split(kind, num, sub, ver, payload, len);
    const uint8_t *q;
    uint32_t ql;
    uint8_t qv;
    if ((q = flash_get(kind, num, sub, &ql, &qv)) && qv == ver && ql == len && !memcmp(q, payload, len)) {
        pc_drop(pkey(kind, num, sub));                  /* back as saved */
        return STORE_OK;
    }
    return pc_put(pkey(kind, num, sub), ver, payload, len, 0);
}

int store_edit_del(uint8_t kind, uint8_t num, uint8_t sub)
{
    if (locked || edit_saving) return STORE_ERR_STATE;
    if (piece_kind(kind)) return obj_delete(kind, num);
    int i = map_find(key_of(kind, num, sub), 0);
    if (i < 0 || ent_del(&map[i])) {                    /* nothing saved: only the edit goes */
        pc_drop(pkey(kind, num, sub));
        return STORE_OK;
    }
    return pc_put(pkey(kind, num, sub), ent_ver(&map[i]), 0, 0, PF_DEL);
}

int store_edit_forget(uint8_t kind, uint8_t num, uint8_t sub)
{
    if (locked || edit_saving) return STORE_ERR_STATE;
    if (!piece_kind(kind)) pc_drop(pkey(kind, num, sub));
    else if (!is_cart(kind)) obj_drop(kind, num);
    else {
        int i;
        for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) obj_drop(k, num);
        while ((i = pc_lower(pkey(REC_AUDIO, num, 0))) < npc() && pc_key(&pcs()[i]) <= pkey(REC_AUDIO, num, 0x1FFu))
            pc_drop(pc_key(&pcs()[i]));
    }
    return STORE_OK;
}
int store_edit_forget_piece(uint8_t kind, uint8_t num, uint16_t id)
{
    if (locked || edit_saving) return STORE_ERR_STATE;
    pc_drop(pkey(kind, num, id));
    return STORE_OK;
}

int store_edit_count(void)
{
    const pc_t *e = pcs();
    int n = 0, c = npc();
    uint32_t last = 0xFFFFFFFFu;
    for (int i = 0; i < c; i++) {                       /* (a thing of pieces counts once) */
        uint32_t k = pc_key(&e[i]), g;
        if (is_sess(k)) break;
        g = piece_kind((uint8_t)(k >> 17)) ? k >> 9 : k;
        if (g != last) n++;
        last = g;
    }
    return n;
}
int store_edit_has(uint8_t kind, uint8_t num, uint8_t sub)
{
    if (piece_kind(kind)) {
        if (!store_edit_obj(kind, num)) return 0;
        return obj_hidden(kind, num) && !store_piece(kind, num, STORE_PC_HEAD, 0) ? 2 : 1;
    }
    int i = pc_find(pkey(kind, num, sub));
    return i < 0 ? 0 : (pcs()[i].lenf & PF_DEL) ? 2 : 1;
}
uint32_t store_edit_used(void) { return pused + 8u * (uint32_t)npc(); }
uint32_t store_edit_room(void) { return room_free(); }
uint32_t store_edit_top(void) { return ptop + 8u * (uint32_t)npc(); }
uint32_t store_edit_limit(void) { return STORE_EDIT_MAX; }

/* ---- the records as the edits make them (the save's, store_edit_build) ---- */
/* a cartridge into b (at most max): its length; 0 none, too big, or its flash record not readable now */
static uint32_t build_cart(uint8_t kind, uint8_t num, uint8_t *b, uint32_t max)
{
    uint32_t fl = 0, hl = cart_head(kind), sz = SLOT_SZ[kind], hlen = 0, n, ns, fn, at, l, s;
    uint8_t fv;
    const uint8_t *f = flash_get(kind, num, 0, &fl, &fv), *h = store_piece(kind, num, STORE_PC_HEAD, &hlen), *q;
    if (!f && flash_lost(kind, num, 0)) return 0;
    if (!h) { h = f; hlen = fl; }
    if (!h || hlen < 10u) return 0;
    n = h[9];
    ns = kind == REC_SAMPLER && hlen > 10u ? h[10] : 0u;
    fn = f && fl >= 10u ? f[9] : 0u;
    at = hl + sz * n + 16u * ns;
    if (at > max) return 0;
    memset(b, 0, hl);
    memcpy(b, h, hlen < hl ? hlen : hl);
    for (s = 0; s < n; s++) {
        uint8_t *d = b + hl + sz * s;
        if ((q = store_piece(kind, num, (uint16_t)s, &l)) && l == sz) memcpy(d, q, sz);
        else if (f && s < fn && hl + sz * (s + 1u) <= fl) memcpy(d, f + hl + sz * s, sz);
        else slot_clear(kind, d);
    }
    if (ns) {                                           /* a sampler's samples' table */
        uint8_t *d = b + hl + sz * n;
        uint32_t fns = f && fl > 10u ? f[10] : 0u;
        memset(d, 0, 16u * ns);
        if ((q = store_piece(kind, num, STORE_PC_TABLE, &l))) memcpy(d, q, l < 16u * ns ? l : 16u * ns);
        else if (f && hl + sz * fn + 16u * fns <= fl) memcpy(d, f + hl + sz * fn, 16u * (fns < ns ? fns : ns));
    }
    return at;
}
/* ---- free blocks and placing ---- */
#define NUSER ((int)(((U1_END - U1_START) + (U2_END - U2_START)) / STORE_BLOCK))

static uint32_t user_block(int i)               /* the i-th block of the user area, in address order */
{
    uint32_t n1 = (U1_END - U1_START) / STORE_BLOCK;
    return (uint32_t)i < n1 ? U1_START + (uint32_t)i * STORE_BLOCK
                            : U2_START + ((uint32_t)i - n1) * STORE_BLOCK;
}
static int user_index(uint32_t a)
{
    if (a >= U1_START && a < U1_END) return (int)((a - U1_START) / STORE_BLOCK);
    if (a >= U2_START && a < U2_END)
        return (int)((U1_END - U1_START) / STORE_BLOCK + (a - U2_START) / STORE_BLOCK);
    return -1;
}

static int blk_free(uint32_t a, uint32_t open)
{
    uint32_t b = BLK(a);
    return !blk_live[b] && !blk_is_audio(b) && !(taken[b >> 3] >> (b & 7) & 1) && !(open && BLK(open) == b);
}
static int count_free(void)
{
    int n = 0;
    for (int i = 0; i < NUSER; i++) if (blk_free(user_block(i), head)) n++;
    return n;
}

/* The placing head: pl_head (0: none open), pl_from (the search for a free block starts after it). A
 * record that opens a block finds it erased (prep_block) before it is written. */
static uint32_t pl_head, pl_from;

static void place_start(void)
{
    memset(taken, 0, sizeof taken);
    pl_head = head;
    pl_from = head ? head : head_from;
}

static int prep_block(uint32_t a)               /* a block about to be written: erased, checked */
{
    if (store_all_ff(a, STORE_BLOCK)) return 0;
    if (plat_flash_erase(a)) return -1;
    return store_all_ff(a, STORE_BLOCK) ? 0 : -1;
}

static int place(uint32_t size, uint32_t *addr)
{
    if (pl_head && (pl_head & 0xFFFu) + size <= STORE_BLOCK) {
        *addr = pl_head;
    } else {
        int from = user_index(pl_from), i, k;
        if (from < 0) from = NUSER - 1;
        for (k = 1; k <= NUSER; k++) {
            i = (from + k) % NUSER;
            if (blk_free(user_block(i), pl_head)) break;
        }
        if (k > NUSER) return -1;
        uint32_t a = user_block(i);
        taken[BLK(a) >> 3] |= (uint8_t)(1u << (BLK(a) & 7));
        if (prep_block(a)) return -1;           /* an erase: ~17 ms */
        pl_from = a;
        *addr = a;
    }
    pl_head = *addr + size;
    if (!(pl_head & 0xFFFu)) { pl_from = pl_head - STORE_BLOCK; pl_head = 0; }
    return 0;
}

/* How many blocks records of these sizes would open from the head as it stands (a size of 0: a record left out). */
static int blocks_needed(uint32_t (*size_of)(int), int n)
{
    int k = 0;
    uint32_t h = head;
    for (int i = 0; i < n; i++) {
        uint32_t s = size_of(i);
        if (!s) continue;
        if (!h || (h & 0xFFFu) + s > STORE_BLOCK) { k++; h = 0x1000u; }     /* a new block's start */
        h += s;
        if (!(h & 0xFFFu)) h = 0;
    }
    return k;
}

/* ---- writing ---- */
/* A record goes into the map only once its last page is programmed (and read back): until then the audio
 * context, which reads in place, would find unwritten bytes there. The records written since the last page
 * went out wait in pend; more than PEND_MAX of them program the page early. */
static void pend_apply(void)
{
    for (int i = 0; i < npend; i++)
        map_put(pend[i].keyd & 0xFFFFFFu, pend[i].addr, pend[i].len, (int)(pend[i].keyd >> 24), 0, 0);
    npend = 0;
}

static int wr_flush(void)
{
    if (pg_addr != 1) {
        uint32_t a = pg_addr + pg_lo, n = pg_hi - pg_lo;
        pg_addr = 1;
        if (n) {
            if (plat_flash_prog(a, pg + pg_lo, n)) { npend = 0; return -1; }
            uint8_t back[64];                   /* read back in pieces: the stack stays small */
            for (uint32_t o = 0; o < n; o += sizeof back) {
                uint32_t k = n - o < sizeof back ? n - o : sizeof back;
                if (rd(a + o, back, k) || memcmp(back, pg + pg_lo + o, k)) { npend = 0; return -1; }
            }
        }
    }
    pend_apply();
    return 0;
}

static int pend_add(uint32_t key, uint32_t addr, uint32_t len, int del)
{
    if (npend == PEND_MAX && wr_flush()) return -1;
    pend[npend].keyd = key | (uint32_t)(del ? 1 : 0) << 24;
    pend[npend].addr = addr;
    pend[npend].len = len;
    npend++;
    return 0;
}

static int wr_bytes(uint32_t addr, const uint8_t *src, uint32_t n)
{
    while (n) {
        uint32_t page = addr & ~255u, off = addr & 255u, k = 256 - off;
        if (k > n) k = n;
        if (page != pg_addr) {
            if (wr_flush()) return -1;
            pg_addr = page; memset(pg, 0xFF, sizeof pg); pg_lo = 256; pg_hi = 0;
        }
        memcpy(pg + off, src, k);
        if (off < pg_lo) pg_lo = off;
        if (off + k > pg_hi) pg_hi = off + k;
        addr += k; src += k; n -= k;
    }
    return 0;
}

/* Places one record, writes it, and puts it in the map (the map changes record by record: a save that
 * fails remounts, and nothing reads the map while a save is written but the UI, SAVE waiting for a pause). */
static int wr_record(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, int del, int last, uint32_t save,
                     const uint8_t *payload, uint32_t len)
{
    uint32_t addr;
    if (del) len = 0;
    if (place(STORE_HDR + ALIGN4(len), &addr)) return -1;
    uint8_t h[STORE_HDR];
    h[0] = kind; h[1] = num; h[2] = sub; h[3] = ver;
    h[4] = (uint8_t)len; h[5] = (uint8_t)(len >> 8); h[6] = (uint8_t)(len >> 16);
    h[7] = (uint8_t)((last ? F_LAST : 0) | (del ? F_DEL : 0));
    put32(h + 8, save);
    uint32_t c = crc32_update(0, h, 12);
    if (len) c = crc32_update(c, payload, len);
    put32(h + 12, c);
    if (wr_bytes(addr, h, STORE_HDR) || (len && wr_bytes(addr + STORE_HDR, payload, len))) return -1;
    return pend_add(key_of(kind, num, sub), addr, len, del);
}

/* ---- the save being made: the save API's list (in the scratch), or the edit memory's ---- */
#define stg ((stage_t *)scratch32)
#define STAGE_CAP ((int)((sizeof scratch32) / sizeof(stage_t)) < STORE_SAVE_MAX ? \
                   (int)((sizeof scratch32) / sizeof(stage_t)) : STORE_SAVE_MAX)
static int n_stg;
static int s_result, s_next, s_n;
static uint32_t s_save;

typedef struct { uint8_t kind, num, sub, ver, del; const uint8_t *p; uint32_t len; } rec_t;

/* ---- the edits' save (store_edit_save): their records, in the list's order ----
 * Each thing edited makes its records: a whole record its own; a cartridge one, built from the flash's and its
 * pieces; a pattern or LOOPS its parts (its streams laid into as many as they need, in order, as the editor lays a
 * pattern out; LOOPS' part 0 its head first), then the deletes of the parts the flash has past them; a deleted thing
 * the deletes of its records. A record is built in the scratch as it is written; a pattern's or LOOPS' streams are
 * read from where they lay when its first record came up (its parts in the flash are written over one at a time). */
#define PART_MAX (STORE_MAX_REC - STORE_HDR)
enum { FROM_FLASH = 1, FROM_PTR = 2 };
static int sv_at = -1;                  /* the save's record at hand (-1: none yet) */
static struct {
    int obj, end;                       /* its thing's entries in the list: [obj, end) */
    uint32_t k, n;                      /* the thing's record at hand, and how many it makes */
    uint8_t kind, num, ver, exists;
    uint32_t len;                       /* a cartridge's record */
    uint8_t ntrk, rows1, step, np;      /* a pattern's head (LOOPS': its streams' head, ntrk its loops), its parts */
    uint16_t hl;                        /* LOOPS' head: its bytes (0: none), from where */
    uint8_t hfrom;
    uint32_t haddr;
    const uint8_t *hptr;
    uint16_t dels;                      /* the parts the flash has that go (bit s) */
    uint16_t tlen[16];                  /* each track's stream (with its 0x00; 0: not stored), its part, where */
    uint8_t part[16], from[16];
    uint32_t addr[16];
    const uint8_t *ptr[16];
} sv;
static int sv_bad;                      /* a record in the map not readable (a guard, flash_lost's): not saved */

static uint32_t bitcount(uint32_t v)
{
    uint32_t n = 0;
    for (; v; v &= v - 1u) n++;
    return n;
}
static uint8_t flash_ver(uint8_t kind, uint8_t num, uint8_t sub)
{
    int i = map_find(key_of(kind, num, sub), 0);
    return i >= 0 ? ent_ver(&map[i]) : 1u;
}
static uint8_t obj_ver(uint8_t kind, uint8_t num)       /* the version its pieces have, else the flash's */
{
    int i = pc_find(pkey(kind, num, STORE_PC_HEAD));
    uint8_t v = 1;
    if (i >= 0) return (uint8_t)(pcs()[i].kv >> 25);
    return flash_get(kind, num, 0, 0, &v) ? v : 1u;
}
/* a pattern's or LOOPS' parts (sv.kind, sv.num): the flash's (LOOPS' in the version read alone: another is as none,
 * left in the flash unless LOOPS is saved or deleted), the pieces over them, the streams laid into parts in order */
static void sv_parts(void)
{
    int lp = sv.kind == REC_LOOPS;
    uint32_t hl, l, a, s, t, w, n = 0, used, part = 0;
    const uint8_t *h = store_piece(sv.kind, sv.num, STORE_PC_HEAD, &hl), *p, *q, *e;
    int got = 0, hid = obj_hidden(sv.kind, sv.num);
    memset(sv.tlen, 0, sizeof sv.tlen);
    memset(sv.from, 0, sizeof sv.from);
    sv.dels = 0;
    sv.np = 0;
    sv.ver = 1;
    sv.hl = 0;
    sv.ntrk = sv.rows1 = sv.step = 0;
    for (s = 0; s < 16u; s++) {                     /* the flash's parts: the head, each track's stream */
        uint8_t v = 1;
        int mi = map_find(key_of(sv.kind, sv.num, (uint8_t)s), 0), live = mi >= 0 && !ent_del(&map[mi]);
        if (live) sv.dels |= (uint16_t)(1u << s);
        if (hid && !h) continue;                    /* (deleted: only its parts' deletes) */
        if (!(p = flash_get(sv.kind, sv.num, (uint8_t)s, &l, &v))) {
            if (live && !hid) sv_bad = 1;
            continue;
        }
        a = live && !hid ? ent_addr(&map[mi]) + STORE_HDR : 0u;
        w = 0;                                      /* (where the pattern payload's bytes start) */
        if (lp) {                                   /* (LOOPS: part 0 its head first) */
            if (v != LOOPS_VER || (s && !sv.hl)) continue;
            if (!s) {
                n = l > 5u ? p[5] : 0u;
                if ((w = loops_head(n)) > l) continue;
                sv.hl = (uint16_t)w;
                sv.hfrom = a ? FROM_FLASH : FROM_PTR;
                sv.haddr = a;
                sv.hptr = p;
            }
        }
        if (w + 5u > l) continue;
        if (!got) { got = 1; sv.ntrk = p[w]; sv.rows1 = p[w + 1u]; sv.step = p[w + 2u]; sv.ver = v; }
        for (t = 0, q = p + w + 5u, e = p + l; t < 16u && q < e; t++) {
            uint32_t k;
            if (!((p[w + 3u] | (uint32_t)p[w + 4u] << 8) >> t & 1u)) continue;
            if (!(k = strm_len(q, e))) break;
            sv.tlen[t] = (uint16_t)k;
            sv.from[t] = a ? FROM_FLASH : FROM_PTR; /* (a built-in one's bytes stay put) */
            sv.addr[t] = a + (uint32_t)(q - p);
            sv.ptr[t] = q;
            q += k;
        }
    }
    if (lp) {                                       /* LOOPS: its head, its streams' head (the pieces first), its loops */
        const uint8_t *ph = store_piece(REC_LOOPS, sv.num, STORE_PC_PHEAD, &l);
        if (h && hl >= loops_head(0)) {
            sv.hl = (uint16_t)hl;
            sv.hfrom = FROM_PTR;
            sv.hptr = h;
            n = h[5];
        }
        if (ph && l >= 3u) { sv.ntrk = ph[0]; sv.rows1 = ph[1]; sv.step = ph[2]; }
        else if (!got && sv.hl) sv.rows1 = (uint8_t)(sv.hptr[7] ? sv.hptr[7] - 1u : 0u);   /* (none: its length) */
        got = sv.hl != 0;
        sv.ntrk = (uint8_t)(n > 16u ? 16u : n);    /* (a stream a loop) */
        sv.ver = LOOPS_VER;
        if (!got && !hid) sv.dels = 0;              /* (none in the version read: another left as it is) */
    } else {
        if (h && hl >= 3u) { got = 1; sv.ntrk = h[0]; sv.rows1 = h[1]; sv.step = h[2]; }
        sv.ntrk = sv.ntrk < 1u ? 1u : sv.ntrk > 16u ? 16u : sv.ntrk;
    }
    if (sv.rows1 > PAT_ROWS_MAX - 1u) sv.rows1 = (uint8_t)(PAT_ROWS_MAX - 1u);   /* (a record of more: its first 128) */
    for (t = 0; t < 16u; t++) {                     /* the pieces over them */
        if ((q = store_piece(sv.kind, sv.num, (uint16_t)t, &l))) {
            sv.tlen[t] = (uint16_t)l;
            sv.from[t] = FROM_PTR;
            sv.ptr[t] = q;
        }
        if (t < sv.ntrk && sv.tlen[t] > 1u)         /* (a record of more rows: its first 128, a loop's 512; sv_part
                                                     * ending it) */
            sv.tlen[t] = (uint16_t)store_track_kept(sv.ptr[t], sv.tlen[t], lp ? STORE_LOOP_ROWS : PAT_ROWS_MAX);
        if (t >= sv.ntrk || sv.tlen[t] <= 1u) sv.tlen[t] = 0;    /* (past its tracks, or empty: not stored) */
    }
    sv.exists = (uint8_t)got;
    if (got) {
        for (t = 0, used = sv.hl + 5u; t < sv.ntrk; t++) {
            if (!sv.tlen[t]) continue;
            if (used + sv.tlen[t] > PART_MAX && used > (part ? 5u : sv.hl + 5u)) { part++; used = 5; }
            sv.part[t] = (uint8_t)part;
            used += sv.tlen[t];
        }
        sv.np = (uint8_t)(part + 1u);
        sv.dels &= (uint16_t)~((1u << sv.np) - 1u);
    }
    sv.n = sv.np + bitcount(sv.dels);
}
static uint32_t sv_part(uint8_t *b, uint32_t k, int build)  /* part k of the pattern or LOOPS at hand (into b): its
                                                             * length; 0 a read failed */
{
    uint32_t w = k ? 0u : sv.hl, at = w + 5u, mask = 0, t;     /* (LOOPS' part 0: its head first) */
    if (build && w) {
        if (sv.hfrom == FROM_FLASH) { if (rd(sv.haddr, b, w)) return 0; }
        else memcpy(b, sv.hptr, w);
    }
    if (sv.kind == REC_LOOPS && !sv.ntrk) return w;            /* (no loops: its head alone) */
    for (t = 0; t < sv.ntrk; t++) {
        if (!sv.tlen[t] || sv.part[t] != k) continue;
        if (build) {
            if (sv.from[t] == FROM_FLASH) { if (rd(sv.addr[t], b + at, sv.tlen[t])) return 0; }
            else memcpy(b + at, sv.ptr[t], sv.tlen[t]);
            b[at + sv.tlen[t] - 1u] = 0;            /* (its end: already there, but where rows past the last kept
                                                     * were left out) */
        }
        mask |= 1u << t;
        at += sv.tlen[t];
    }
    if (build) {
        b[w] = sv.ntrk; b[w + 1u] = sv.rows1; b[w + 2u] = sv.step;
        b[w + 3u] = (uint8_t)mask; b[w + 4u] = (uint8_t)(mask >> 8);
    }
    return at;
}
static void sv_one(void)                /* a cartridge: one record (built here for its length) */
{
    uint8_t *b = (uint8_t *)scratch32;
    int gone = obj_hidden(sv.kind, sv.num) && !store_piece(sv.kind, sv.num, STORE_PC_HEAD, 0);
    sv.len = gone ? 0u : build_cart(sv.kind, sv.num, b, STORE_SCRATCH_BYTES);
    sv.exists = (uint8_t)(sv.len != 0);
    sv.ver = obj_ver(sv.kind, sv.num);
    sv.n = 1;
    if (sv.exists) return;
    if (!gone && (obj_live(sv.kind, sv.num) || flash_lost(sv.kind, sv.num, 0))) sv_bad = 1;   /* (there: not built) */
    int mi = map_find(key_of(sv.kind, sv.num, 0), 0);
    sv.n = mi >= 0 && !ent_del(&map[mi]) ? 1u : 0u;  /* (deleted: the flash's goes) */
}
static void sv_enter(int obj)
{
    const pc_t *e = pcs();
    int c = npc(), j = obj + 1;
    uint32_t key = pc_key(&e[obj]);
    sv.obj = obj;
    sv.k = 0;
    sv.kind = (uint8_t)(key >> 17);
    sv.num = (uint8_t)(key >> 9);
    if (!piece_kind(sv.kind)) { sv.end = obj + 1; sv.n = 1; return; }
    while (j < c && pc_key(&e[j]) >> 9 == key >> 9) j++;
    sv.end = j;
    if (sv.kind == REC_PATTERN || sv.kind == REC_LOOPS) sv_parts();
    else sv_one();
}
/* ---- deletions first (store.md#the-room-for-the-editors-pattern) ----
 * An edits' SAVE that does not fit writes its deletions first, as a save of their own, then the rest once
 * compaction has freed their blocks. Only deletions no edit could be made from go first: a thing deleted whole whose
 * kind (the cartridges' kinds and the samples' audio: one) has no new thing among the edits (one the flash does not
 * hold, or one made anew: what a MOVE or a CLONE makes), the EDITOR's pattern and the keys' song apart (never moved).
 * A power cut between the two leaves the deletions saved and the rest not. */
static int sv_dels;                     /* the save under way: those deletions only */
static int then_rest;                   /* ... and the edits' save follows it */
static void saving_none(void) { edit_saving = 0; sv_dels = 0; then_rest = 0; }   /* (power-on: no save under way) */
static int fam(uint8_t kind) { return is_cart(kind) || kind == REC_AUDIO ? 1 : (int)kind; }
static int saved_live(uint8_t kind, uint8_t num, uint8_t sub)
{
    int i = map_find(key_of(kind, num, sub), 0);
    return i >= 0 && !ent_del(&map[i]);
}
static int whole_del(int obj, int end)  /* the thing of entries [obj, end) deleted whole */
{
    const pc_t *e = &pcs()[obj];
    uint32_t k = pc_key(e);
    if (!piece_kind((uint8_t)(k >> 17))) return (e->lenf & PF_DEL) != 0;
    return end == obj + 1 && (k & 0x1FFu) == PC_OBJ && (e->lenf & PF_DEL);
}
static int no_new_of(uint8_t kind)      /* no new thing of kind's among the edits */
{
    const pc_t *e = pcs();
    for (int i = 0, c = npc(); i < c; i++) {
        uint32_t k = pc_key(&e[i]), id = k & 0x1FFu;
        uint8_t ki = (uint8_t)(k >> 17), ni = (uint8_t)(k >> 9);
        if (is_sess(k) || fam(ki) != fam(kind)) continue;
        if (ni == STORE_EDITOR_PAT && (ki == REC_PATTERN || ki == REC_SONG)) continue;
        if (!piece_kind(ki)) { if (!(e[i].lenf & PF_DEL) && !saved_live(ki, ni, (uint8_t)id)) return 0; }
        else if (id != PC_OBJ && (obj_hidden(ki, ni) || !saved_live(ki, ni, 0))) return 0;
    }
    return 1;
}
static void dels_drop(void)             /* written: the deletion edits of what the flash no longer holds go */
{
    for (int i = 0; i < npc(); ) {
        uint32_t k = pc_key(&pcs()[i]), id = k & 0x1FFu;
        uint8_t ki = (uint8_t)(k >> 17), ni = (uint8_t)(k >> 9);
        int end = i + 1;
        while (piece_kind(ki) && !is_sess(k) && end < npc() && pc_key(&pcs()[end]) >> 9 == k >> 9) end++;
        if (!is_sess(k) && whole_del(i, end) && !saved_live(ki, ni, piece_kind(ki) ? 0 : (uint8_t)id)) {
            pc_drop(k);                                 /* (the list closes up: i again) */
            continue;
        }
        i = end;
    }
}

static void sv_first(int obj)           /* the first thing from obj that makes a record (none: sv.obj = npc()) */
{
    while (obj < npc() && !is_sess(pc_key(&pcs()[obj]))) {
        sv_enter(obj);
        if (sv.n && (!sv_dels || (whole_del(sv.obj, sv.end) && no_new_of(sv.kind)))) return;
        obj = sv.end;
    }
    sv.obj = npc();
    sv.n = 0;
}
static void sv_seek(int r)              /* to the save's record r (from the first when it is before the one at hand) */
{
    if (sv_at < 0 || r < sv_at) { sv_at = 0; sv_first(0); }
    while (sv_at < r && sv.obj < npc()) {
        sv_at++;
        if (++sv.k >= sv.n) sv_first(sv.end);
    }
}
static int sv_count(void)
{
    int n = 0;
    sv_bad = 0;
    for (sv_first(0); sv.obj < npc(); sv_first(sv.end)) n += (int)sv.n;
    sv_at = -1;
    return n;
}
/* the record at hand; build: its bytes made (in the scratch; p NULL when a read failed), else only its length */
static void sv_rec(rec_t *r, int build)
{
    uint8_t *b = (uint8_t *)scratch32;
    memset(r, 0, sizeof *r);
    r->kind = sv.kind; r->num = sv.num; r->ver = sv.ver;
    if (!piece_kind(sv.kind)) {
        const pc_t *e = &pcs()[sv.obj];
        r->sub = (uint8_t)(pc_key(e) & 0xFFu);
        r->ver = (uint8_t)(e->kv >> 25);
        r->del = (uint8_t)((e->lenf & PF_DEL) != 0);
        r->len = PF_LEN(e->lenf);
        r->p = pc_at(e);
        return;
    }
    int parts = sv.kind == REC_PATTERN || sv.kind == REC_LOOPS;
    if (parts && sv.exists && sv.k < sv.np) {
        r->sub = (uint8_t)sv.k;
        r->len = sv_part(b, sv.k, 0);
        if (build) r->p = sv_part(b, sv.k, 1) == r->len ? b : 0;   /* (p NULL: a read failed, the save fails) */
        return;
    }
    if (parts) {                                    /* the k-th of the flash's parts that go */
        uint32_t d = sv.dels, k = sv.k - (sv.exists ? sv.np : 0u), s = 0;
        while (k--) d &= d - 1u;
        while (s < 15u && !(d >> s & 1u)) s++;
        r->sub = (uint8_t)s;
        r->del = 1;
        r->ver = flash_ver(sv.kind, sv.num, (uint8_t)s);
        return;
    }
    if (!sv.exists) {
        r->del = 1;
        r->ver = flash_ver(sv.kind, sv.num, 0);
        return;
    }
    r->len = sv.len;
    if (build)                                      /* (again: the scratch may have served another since) */
        r->p = build_cart(sv.kind, sv.num, b, STORE_SCRATCH_BYTES) == sv.len ? b : 0;
}

int store_edit_build(uint8_t kind, uint8_t num, uint8_t sub, uint8_t *dst, uint32_t max, uint32_t *len, uint8_t *ver)
{
    uint32_t l = 0;
    if (locked || edit_saving) return -1;
    if (!piece_kind(kind)) {
        const uint8_t *p = store_get(kind, num, sub, &l, ver);
        if (!p) return -1;
        if (l > max) return -2;
        memcpy(dst, p, l);
        *len = l;
        return 0;
    }
    if (!obj_live(kind, num)) return -1;
    if (kind == REC_PATTERN || kind == REC_LOOPS) {
        sv.kind = kind;
        sv.num = num;
        sv_parts();
        sv_at = -1;
        if (!sv.exists) return -1;
        if (sv.np > 1u || (l = sv_part(dst, 0, 0)) > max) return -2;
        l = sv_part(dst, 0, 1);
    } else l = build_cart(kind, num, dst, max);
    if (!l) return -2;
    *len = l;
    *ver = obj_ver(kind, num);
    return 0;
}

static void src_rec(int i, rec_t *r)            /* the i-th record of the save under way (its length) */
{
    if (edit_saving) {
        sv_seek(i);
        sv_rec(r, 0);
    } else {
        const stage_t *s = &stg[i];
        r->kind = s->kind; r->num = s->num; r->sub = s->sub; r->ver = s->ver;
        r->del = (uint8_t)((s->lenf & L_DEL) != 0); r->len = L_LEN(s->lenf); r->p = s->p;
    }
}
static uint32_t src_size(int i) { rec_t r; src_rec(i, &r); return STORE_HDR + ALIGN4(r.del ? 0 : r.len); }

int store_save_begin(void)
{
    if (locked || claimed || s_state == S_WRITING) return STORE_ERR_STATE;
    n_stg = 0;
    s_state = S_STAGING;
    return STORE_OK;
}

static int stage(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, int del, const void *p, uint32_t len)
{
    for (int i = 0; i < n_stg; i++)
        if (stg[i].kind == kind && stg[i].num == num && stg[i].sub == sub) {
            stg[i].ver = ver; stg[i].p = p; stg[i].lenf = (uint16_t)(del ? L_DEL : len);
            return STORE_OK;
        }
    if (n_stg >= STAGE_CAP) return STORE_ERR_MANY;
    stage_t *s = &stg[n_stg++];
    s->kind = kind; s->num = num; s->sub = sub; s->ver = ver; s->p = p; s->lenf = (uint16_t)(del ? L_DEL : len);
    return STORE_OK;
}

static int staged(uint8_t kind, uint8_t num, uint8_t sub)
{
    for (int i = 0; i < n_stg; i++)
        if (stg[i].kind == kind && stg[i].num == num && stg[i].sub == sub) return 1;
    return 0;
}

int store_save_del(uint8_t kind, uint8_t num, uint8_t sub)
{
    if (s_state != S_STAGING) return STORE_ERR_STATE;
    int i = map_find(key_of(kind, num, sub), 0);
    if (i < 0 || ent_del(&map[i])) {                    /* nothing there: only drop a put of it */
        for (int j = 0; j < n_stg; j++)
            if (stg[j].kind == kind && stg[j].num == num && stg[j].sub == sub) {
                stg[j] = stg[--n_stg];
                break;
            }
        return STORE_OK;
    }
    return stage(kind, num, sub, ent_ver(&map[i]), 1, 0, 0);
}

int store_save_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len)
{
    if (s_state != S_STAGING) return STORE_ERR_STATE;
    if (STORE_HDR + len > STORE_MAX_REC) return STORE_ERR_BIG;
    int r = stage(kind, num, sub, ver, 0, payload, len);
    if (r || !is_cart(kind)) return r;
    /* the six cartridge kinds share one number space: another kind's cartridge there goes */
    for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) {
        if (k == kind || staged(k, num, 0)) continue;
        int i = map_find(key_of(k, num, 0), 0);
        if (i < 0 || ent_del(&map[i])) continue;
        if ((r = stage(k, num, 0, ent_ver(&map[i]), 1, 0, 0))) return r;
        if (k == REC_SAMPLER)
            for (int j = 0; j < map_n; j++)
                if (ent_kind(&map[j]) == REC_AUDIO && ent_num(&map[j]) == num && !ent_del(&map[j]) &&
                    !staged(REC_AUDIO, num, ent_sub(&map[j])))
                    if ((r = stage(REC_AUDIO, num, ent_sub(&map[j]), ent_ver(&map[j]), 1, 0, 0))) return r;
    }
    return STORE_OK;
}

void store_save_cancel(void)
{
    if (s_state == S_STAGING) { n_stg = 0; s_state = S_IDLE; }
}

/* ---- compaction (store.md#garbage-and-compaction) ---- */
/* A deleted record of the block being compacted goes along only while an older copy of its object is left
 * somewhere: one walk finds that out for all of them. While it runs, such an entry carries C_ASK (bit 31 of
 * al, unused otherwise); an older copy found clears it. */
#define C_ASK 0x80000000u
static uint32_t c_block;
static void on_older(uint32_t pos, const uint8_t *h, uint32_t len)
{
    (void)len;
    if (BLK(pos) == c_block || (h[7] & F_DEL)) return;
    int i = map_find(key_of(h[0], h[1], h[2]), 0);
    if (i >= 0 && (map[i].al & C_ASK) && ent_addr(&map[i]) != pos) map[i].al &= ~C_ASK;
}

/* The live records of the user block with the fewest live bytes, written again at the head as a save of
 * their own; the block is then free. A deleted record goes along only while an older copy is left. */
static int compact_one(void)
{
    uint32_t best = 0, best_live = 256;
    for (int i = 0; i < NUSER; i++) {
        uint32_t a = user_block(i), b = BLK(a);
        if (!blk_live[b] || blk_is_audio(b) || (head && BLK(head) == b)) continue;
        if (blk_live[b] < best_live) { best_live = blk_live[b]; best = a; }
    }
    if (!best || best_live > 252) return 0;             /* nothing with room to gain (4,032 B live or more) */
    c_block = BLK(best);
    int asks = 0;
    for (int i = 0; i < map_n; i++)
        if (BLK(ent_addr(&map[i])) == c_block && ent_del(&map[i])) { map[i].al |= C_ASK; asks++; }
    if (asks) walk_all(on_older);                       /* (one walk, however many deleted records) */
    int n = 0;
    for (int i = map_n - 1; i >= 0; i--) {
        ent_t *e = &map[i];
        if (BLK(ent_addr(e)) != c_block) continue;
        if (e->al & C_ASK) { map_drop(i); continue; }   /* no older copy left: no need for it */
        if (!ent_del(e) && !plat_flash_ptr(ent_addr(e) + STORE_HDR)) return 0;   /* the user area reads in place */
        n++;
    }
    if (!n) { blocks_count(); return 1; }               /* only deleted records no longer needed */
    place_start();
    uint32_t save = next_save++;
    int k = 0;
    for (int i = 0; i < map_n && k < n; i++) {
        ent_t *e = &map[i];
        uint32_t a = ent_addr(e);
        if (BLK(a) != c_block) continue;
        uint8_t h[4];
        rd(a, h, 4);
        k++;
        if (wr_record(h[0], h[1], h[2], h[3], ent_del(e), k == n, save,
                      ent_del(e) ? 0 : plat_flash_ptr(a + STORE_HDR), ent_len(e))) {
            pg_addr = 1; store_mount(); return 0;
        }
    }
    if (wr_flush()) { store_mount(); return 0; }
    last_save = save;
    head = pl_head; head_from = pl_from;
    memset(taken, 0, sizeof taken);
    blocks_count();
    return 1;
}

static uint32_t save_short;             /* the last save that did not fit: the bytes it was short by */
uint32_t store_save_short(void) { return save_short; }
/* The room a save needs (store.md#the-room-for-the-editors-pattern): all its records must leave compaction's
 * STORE_MIN_FREE blocks; the blocks what it writes takes, the EDITOR's pattern and deletes apart (they may use the
 * EDITOR's room: deleting is how room is made), must leave STORE_EDITOR_ROOM more (records that fit the open block
 * take none). Apart means laid out without them: where they lie among the rest in the log can cost the rest a block,
 * and that block is theirs, from the EDITOR's room; the floor is counted on the save as written. The blocks it lacks;
 * 0 or less it fits. */
static uint32_t src_size_other(int i)
{
    rec_t r;
    src_rec(i, &r);
    if (r.del || (r.kind == REC_PATTERN && r.num == STORE_EDITOR_PAT)) return 0;
    return STORE_HDR + ALIGN4(r.len);
}
static int room_lack(int n)
{
    int f = count_free(), all = blocks_needed(src_size, n) + STORE_MIN_FREE - f, no = blocks_needed(src_size_other, n);
    int other = no ? no + STORE_MIN_FREE + (int)STORE_EDITOR_ROOM - f : 0;
    return all > other ? all : other;
}
static int make_room(int n)
{
    int stuck = 0, lack;
    for (int tries = 0; tries < 64; tries++) {
        int before = count_free();
        if (room_lack(n) <= 0) return STORE_OK;
        plat_idle();                                    /* (compaction takes a while: the main loop lives) */
        if (!compact_one()) break;
        if (count_free() <= before) { if (++stuck >= 4) break; } else stuck = 0;
    }
    if ((lack = room_lack(n)) <= 0) return STORE_OK;
    save_short = (uint32_t)lack * STORE_BLOCK;
    return STORE_ERR_FULL;
}

static int commit(int n)
{
    s_n = n;
    if (!n) { s_state = S_RESULT; s_result = STORE_OK; return STORE_OK; }
    int r = make_room(n);
    if (r) { s_state = S_IDLE; n_stg = 0; return r; }
    place_start();
    s_save = next_save++;
    s_state = S_WRITING; s_next = 0;
    return STORE_OK;
}

int store_save_commit(void)
{
    if (s_state != S_STAGING || locked) return STORE_ERR_STATE;
    edit_saving = 0;
    return commit(n_stg);
}

static int edits_commit(void);
static void save_step(void)
{
    rec_t r;
    if (edit_saving) {                                  /* (the edits' records built as they are written) */
        sv_seek(s_next);
        sv_rec(&r, 1);
    } else src_rec(s_next, &r);
    int fail = (!r.del && r.len && !r.p) ||
               wr_record(r.kind, r.num, r.sub, r.ver, r.del, s_next == s_n - 1, s_save, r.p, r.len);
    s_next++;
    if (!fail && s_next == s_n) fail = wr_flush();
    if (!fail && s_next == s_n) {
        last_save = s_save;
        head = pl_head; head_from = pl_from;
        memset(taken, 0, sizeof taken);
        blocks_count();
        s_state = S_RESULT; s_result = STORE_OK; n_stg = 0;
        if (edit_saving && sv_dels) {                   /* the deletions written: theirs go, then the rest */
            dels_drop();
            sv_dels = 0;
            edit_saving = 0;
            if (then_rest) {
                then_rest = 0;
                int r = edits_commit();                 /* (compaction runs first: make_room) */
                if (r) { s_state = S_RESULT; s_result = r; }
            }
        } else if (edit_saving) { edits_none(); edit_saving = 0; }  /* saved: the flash holds them now */
        return;
    }
    if (fail) {
        pg_addr = 1;
        store_mount();                                  /* the save has no last record: the map as it was */
        s_state = S_RESULT; s_result = STORE_ERR_FLASH; n_stg = 0;
        edit_saving = 0;                                /* the edits stay, to be saved again */
        sv_dels = 0;
        then_rest = 0;
    }
}

static int edits_commit(void)                   /* the edits' save started (or nothing to write: done) */
{
    edit_saving = 1;
    int n = sv_count();
    if (sv_bad) { edit_saving = 0; return STORE_ERR_STATE; }
    int r = commit(n);
    if (r == STORE_ERR_FULL) {                          /* (deletions first: their own save, then this again) */
        uint32_t short_by = save_short;
        sv_dels = 1;
        int nd = sv_count();
        if (nd && commit(nd) == STORE_OK) { then_rest = 1; r = STORE_OK; }
        else { sv_dels = 0; save_short = short_by; }
    }
    sv_at = -1;                                        /* (enumerated again as they are written) */
    if (r || s_state != S_WRITING) {
        edit_saving = 0;
        if (!r) edits_none();                           /* nothing to write: nothing left unsaved */
    }
    return r;
}
int store_edit_save(void)
{
    if (locked || claimed || s_state == S_WRITING) return STORE_ERR_STATE;
    if (s_state == S_STAGING) store_save_cancel();
    sv_dels = 0;
    then_rest = 0;
    return edits_commit();
}

void store_poll(void)
{
    if (s_state == S_WRITING && !locked) save_step();
}

int store_save_status(uint8_t *percent)
{
    if (s_state == S_WRITING) {
        if (percent) *percent = (uint8_t)(s_n ? s_next * 100 / s_n : 100);
        return STORE_BUSY;
    }
    if (percent) *percent = 100;
    if (s_state == S_RESULT) { s_state = S_IDLE; return s_result; }
    return STORE_OK;
}

int store_save_run(void)
{
    int r = store_save_commit();
    if (r) return r;
    while (s_state == S_WRITING) save_step();
    return store_save_status(0);
}

/* ---- the figures ---- */
void store_usage(store_usage_t *u)
{
    memset(u, 0, sizeof *u);
    u->user_blocks = (uint16_t)NUSER;
    u->user_bytes = (uint32_t)NUSER * STORE_BLOCK;
    for (int i = 0; i < NUSER; i++) {
        uint32_t a = user_block(i), b = BLK(a);
        u->user_records += (uint32_t)blk_live[b] * 16;     /* (to 16 B) */
        if (blk_free(a, head)) u->user_free_blocks++;
    }
    {                                                   /* (compaction's blocks and the EDITOR's room counted as used) */
        uint32_t f = (uint32_t)u->user_free_blocks * STORE_BLOCK + (head ? STORE_BLOCK - (head & 0xFFFu) : 0);
        uint32_t keep = (STORE_MIN_FREE + STORE_EDITOR_ROOM) * STORE_BLOCK;
        u->user_free = f > keep ? f - keep : 0;
    }
    u->fw_end = plat_fw_end();
    for (int i = 0; i < map_n; i++) {
        const ent_t *e = &map[i];
        if (ent_del(e)) continue;
        if (ent_kind(e) == REC_AUDIO) u->user_audio += ent_span(e);
        for (int k = 0; k < STORE_NKINDS; k++)
            if (store_kinds[k] == ent_kind(e)) { u->count[k]++; u->bytes[k] += ent_len(e); }
    }
    u->bad = n_bad;
    u->lost = n_lost;
    u->last_save = last_save;
    u->mount_walks = (uint8_t)m_walks;
}

/* ---- things, a sampler's CLONE and MOVE, CHECK STORAGE (store.h) ---- */
static int live_at(uint32_t key, int *i)
{
    int k = map_find(key, 0);
    if (i) *i = k;
    return k >= 0 && !ent_del(&map[k]);
}

static int add_key(store_key_t *keys, int n, int max, uint8_t kind, uint8_t num, uint8_t sub)
{
    if (n < max) { keys[n].kind = kind; keys[n].num = num; keys[n].sub = sub; }
    return n + 1;
}

int store_thing(uint8_t kind, uint8_t num, uint8_t sub, store_key_t *keys, int max)
{
    int n = 0, at;
    if (is_builtin(kind, num)) return 0;
    if (is_cart(kind)) {
        for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) {
            if (!live_at(key_of(k, num, 0), 0)) continue;
            n = add_key(keys, n, max, k, num, 0);
            if (k != REC_SAMPLER) continue;
            map_find(key_of(REC_AUDIO, num, 0), &at);
            for (int i = at; i < map_n && ent_kind(&map[i]) == REC_AUDIO && ent_num(&map[i]) == num; i++)
                if (!ent_del(&map[i])) n = add_key(keys, n, max, REC_AUDIO, num, ent_sub(&map[i]));
        }
    } else if (kind == REC_PATTERN || kind == REC_LOOPS) {     /* (every part) */
        map_find(key_of(kind, num, 0), &at);
        for (int i = at; i < map_n && ent_kind(&map[i]) == kind && ent_num(&map[i]) == num; i++)
            if (!ent_del(&map[i])) n = add_key(keys, n, max, kind, num, ent_sub(&map[i]));
    } else if (live_at(key_of(kind, num, sub), 0)) {
        n = add_key(keys, n, max, kind, num, sub);
    }
    return n < max ? n : max;
}

uint32_t store_thing_bytes(const store_key_t *keys, int n)
{
    uint32_t b = 0;
    int i;
    for (int k = 0; k < n; k++)
        if (live_at(key_of(keys[k].kind, keys[k].num, keys[k].sub), &i)) b += ent_span(&map[i]);
    return b;
}

/* A copy's records (a sampler's CLONE or MOVE), in the scratch while it runs: where each comes from and goes */
typedef struct {
    uint8_t kind, num, sub, aud, ver;
    uint32_t span, src, at;
} mv_t;
#define MV ((mv_t *)scratch32)
typedef char mv_fits[STORE_MOVE_MAX * sizeof(mv_t) <= STORE_SCRATCH_BYTES ? 1 : -1];
static int mv_n;

/* the user area's blocks the copy needs: the records but audio from the head, and a run of blocks of their own for
 * the audio; 1 when they fit with the reserve and compaction's two blocks left (the reserve covers the EDITOR's
 * room), with the audio run's start (0 none needed) */
static uint32_t mv_size_nonaudio(int k)
{
    int j = -1;
    for (int i = 0; i < mv_n; i++) if (!MV[i].aud && ++j == k) return MV[i].span;
    return 0;
}
static int mv_user_room(uint32_t *run_at)
{
    int nna = 0;
    uint32_t audio = 0;
    for (int i = 0; i < mv_n; i++) { if (MV[i].aud) audio += MV[i].span; else nna++; }
    int need = blocks_needed(mv_size_nonaudio, nna), nab = (int)((audio + STORE_BLOCK - 1) / STORE_BLOCK);
    *run_at = 0;
    if (count_free() - need - nab < STORE_RESERVE + STORE_MIN_FREE) return 0;
    if (!nab) return 1;
    /* a run of nab free blocks in one region, the head's block not among them */
    for (int reg = 0; reg < 2; reg++) {
        uint32_t s = reg ? U2_START : U1_START, e = reg ? U2_END : U1_END, run = 0;
        for (uint32_t a = s; a < e; a += STORE_BLOCK) {
            run = blk_free(a, head) ? run + 1 : 0;
            if ((int)run == nab) { *run_at = a - (uint32_t)(nab - 1) * STORE_BLOCK; return 1; }
        }
    }
    return 0;
}

/* one record copied to addr (header made again: this save, its flags), its payload read from src's, 64 B at a time;
 * then in the map */
static int wr_copy(uint32_t addr, const mv_t *m, int last, uint32_t save)
{
    uint32_t len;
    uint8_t h[STORE_HDR], sh[8];
    if (rd(m->src, sh, 8)) return -1;                   /* the payload's true length, from its header */
    len = sh[4] | sh[5] << 8 | (uint32_t)sh[6] << 16;
    h[0] = m->kind; h[1] = m->num; h[2] = m->sub; h[3] = m->ver;
    h[4] = (uint8_t)len; h[5] = (uint8_t)(len >> 8); h[6] = (uint8_t)(len >> 16);
    h[7] = (uint8_t)(last ? F_LAST : 0);
    put32(h + 8, save);
    uint32_t c = crc32_update(0, h, 12);
    if (len) c = store_crc_flash(c, m->src + STORE_HDR, len);
    put32(h + 12, c);
    if (wr_bytes(addr, h, STORE_HDR)) return -1;
    for (uint32_t o = 0; o < len; o += 64) {
        uint8_t b[64];
        uint32_t k = len - o < 64 ? len - o : 64;
        if (rd(m->src + STORE_HDR + o, b, k) || wr_bytes(addr + STORE_HDR + o, b, k)) return -1;
    }
    return pend_add(key_of(m->kind, m->num, m->sub), addr, len, 0);
}

static int move_fail(void)
{
    pg_addr = 1;
    store_mount();                                      /* the save has no last record: as it was */
    return STORE_ERR_FLASH;
}

/* CLONE (and MOVE) of a sampler cartridge with samples: its audio copied under the new number, over a run of free
 * blocks of its own; a MOVE's deleted records for the old number; then the cartridge's record from RAM, the save's
 * last */
int store_clone_sampler(uint8_t from, uint8_t to, uint8_t ver, const uint8_t *rec, uint32_t len, int move)
{
    if (locked || store_saving()) return STORE_ERR_STATE;
    if (STORE_HDR + ALIGN4(len) > STORE_MAX_REC) return STORE_ERR_BIG;
    for (int s = 0; s < 64; s++) {                      /* (an edit waiting at the new number would shadow it; a
                                                         * move's old number: its edits would bring it back) */
        int k = s >= REC_DX7 && s <= REC_CART_LAST;
        if (store_edit_has(REC_AUDIO, to, (uint8_t)s) || (k && store_edit_has((uint8_t)s, to, 0))) return STORE_ERR_STATE;
        if (move && (store_edit_has(REC_AUDIO, from, (uint8_t)s) || (k && store_edit_has((uint8_t)s, from, 0))))
            return STORE_ERR_STATE;
    }
    if (!store_scratch_claim(1)) return STORE_ERR_STATE;
    int r = STORE_OK, na = 0;
    mv_n = 0;
    for (int s = 0; s < 64 && mv_n < STORE_MOVE_MAX - 2; s++) {
        int i;
        if (store_edit_has(REC_AUDIO, from, (uint8_t)s) || !live_at(key_of(REC_AUDIO, from, (uint8_t)s), &i)) continue;
        mv_t *m = &MV[mv_n++];                          /* (a sample deleted by an edit is not copied) */
        m->kind = REC_AUDIO; m->num = to; m->sub = (uint8_t)s; m->aud = 1;
        m->span = ent_span(&map[i]);
        m->src = ent_addr(&map[i]);
        m->ver = ent_ver(&map[i]);
        na++;
    }
    if (move) {                                         /* the deleted records' room, counted as one */
        mv_t *d = &MV[mv_n++];
        memset(d, 0, sizeof *d);
        d->span = (uint32_t)(na + 1) * STORE_HDR;
    }
    mv_t *c = &MV[mv_n++];                              /* the record: its room counted, written from rec */
    memset(c, 0, sizeof *c);
    c->kind = REC_SAMPLER; c->num = to; c->span = STORE_HDR + ALIGN4(len);
    uint32_t run, at;
    if (!mv_user_room(&run)) { r = STORE_ERR_FULL; goto out; }
    uint32_t save = next_save++;
    place_start();
    if (run) {                                          /* the audio's blocks: taken, erased as entered */
        uint32_t audio = 0;
        for (int k = 0; k < mv_n; k++) if (MV[k].aud) audio += MV[k].span;
        for (uint32_t a = run; a < run + audio; a += STORE_BLOCK) {
            taken[BLK(a) >> 3] |= (uint8_t)(1u << (BLK(a) & 7));
            if (prep_block(a & ~0xFFFu)) { r = move_fail(); goto out; }
        }
    }
    at = run;
    for (int k = 0; k < mv_n; k++) {
        if (!MV[k].aud) continue;
        if (wr_copy(at, &MV[k], 0, save)) { r = move_fail(); goto out; }
        at += MV[k].span;
    }
    for (int k = 0; move && k <= mv_n; k++) {           /* a move: the old number's audio and record deleted */
        if (k < mv_n && !MV[k].aud) continue;
        if (k < mv_n ? wr_record(REC_AUDIO, from, MV[k].sub, MV[k].ver, 1, 0, save, 0, 0)
                     : wr_record(REC_SAMPLER, from, 0, ver, 1, 0, save, 0, 0)) { r = move_fail(); goto out; }
    }
    if (wr_record(REC_SAMPLER, to, 0, ver, 0, 1, save, rec, len) || wr_flush()) { r = move_fail(); goto out; }
    last_save = save;
    head = pl_head; head_from = pl_from;
    memset(taken, 0, sizeof taken);
    blocks_count();
out:
    store_scratch_claim(0);
    return r;
}

/* CHECK STORAGE: every record walked with its CRC checked; a record that fails counts when it mattered: the one in
 * use (damaged), or newer than the one in use (fell back), or with none in use (missing); an older copy failing
 * loses nothing */
static store_problem_t *ck_out;
static int ck_max, ck_n;
static void on_nothing(uint32_t pos, const uint8_t *h, uint32_t len) { (void)pos; (void)h; (void)len; }
static void on_check_bad(uint32_t pos, const uint8_t *h, uint32_t len)
{
    (void)len;
    uint32_t key = key_of(h[0], h[1], h[2]), save = le32(h + 8);
    int i = map_find(key, 0), what;
    if (i >= 0 && ent_addr(&map[i]) == pos) {
        what = STORE_DAMAGED;
    } else {
        uint8_t b[4];
        uint32_t ms = i >= 0 && !rd(ent_addr(&map[i]) + 8, b, 4) ? le32(b) : 0;
        if (i >= 0 && save <= ms) return;
        what = i >= 0 && !ent_del(&map[i]) ? STORE_FELL_BACK : STORE_MISSING;
    }
    int shown = ck_n < ck_max ? ck_n : ck_max;
    for (int k = 0; k < shown; k++)
        if (ck_out[k].kind == h[0] && ck_out[k].num == h[1] && ck_out[k].sub == h[2]) return;
    if (ck_n < ck_max) {
        store_problem_t *p = &ck_out[ck_n];
        p->kind = h[0]; p->num = h[1]; p->sub = h[2];
        p->area = STORE_AREA_USER;
        p->what = (uint8_t)what;
    }
    ck_n++;
}

int store_check(store_problem_t *out, int max)
{
    if (locked) return 0;
    ck_out = out; ck_max = max; ck_n = 0;
    walk_bad = on_check_bad;
    walk_all(on_nothing);
    walk_bad = 0;
    return ck_n;
}
/* ---- for seeding ---- */
void store_lock(int on) { locked = on; }
int store_saving(void) { return s_state == S_STAGING || s_state == S_WRITING; }
/* STATUS' USER FLASH MAP: the user area KB by KB (store.h). A block at a time: the bytes of the entries in it, KB by KB
 * and kind by kind (a deleted record's header OTHER) */
static int kb_of(const ent_t *e)
{
    uint8_t k = ent_kind(e);
    return ent_del(e) ? STORE_KB_OTHER : k == REC_AUDIO ? STORE_KB_SAMPLES : is_cart(k) ? STORE_KB_INSTRUMENTS :
           k == REC_PATTERN ? STORE_KB_PATTERNS : k == REC_SONG ? STORE_KB_SONGS : k == REC_LOOPS ? STORE_KB_LOOPS :
           STORE_KB_OTHER;
}
int store_kb_map(uint8_t *kb, int max)
{
    int n = 0;
    for (int i = 0; i < NUSER && n < max; i++) {
        uint32_t a = user_block(i);
        uint16_t by[4][8];
        uint32_t tot[8];
        memset(by, 0, sizeof by);
        memset(tot, 0, sizeof tot);
        int free = blk_free(a, head);
        if (!free)
            for (int j = 0; j < map_n; j++) {
                uint32_t s = ent_addr(&map[j]), e = s + ent_span(&map[j]);
                if (e <= a || s >= a + STORE_BLOCK) continue;
                if (s < a) s = a;
                if (e > a + STORE_BLOCK) e = a + STORE_BLOCK;
                int k = kb_of(&map[j]);
                for (uint32_t p = s; p < e; ) {
                    uint32_t q = (p | 1023u) + 1;
                    if (q > e) q = e;
                    by[(p - a) >> 10][k] = (uint16_t)(by[(p - a) >> 10][k] + (q - p));
                    tot[k] += q - p;
                    p = q;
                }
            }
        int bk = STORE_KB_OTHER;                        /* the block's own kind: an older copy's KB takes it */
        for (int k = STORE_KB_SAMPLES; k <= STORE_KB_OTHER; k++) if (tot[k] > tot[bk]) bk = k;
        uint32_t open = head && BLK(head) == BLK(a) ? head - a : STORE_BLOCK;
        for (uint32_t q = 0; q < 4 && n < max; q++) {
            int best = 0;
            for (int k = STORE_KB_SAMPLES; k <= STORE_KB_OTHER; k++) if (by[q][k] > by[q][best]) best = k;
            kb[n++] = (uint8_t)(free ? STORE_KB_FREE : best ? best : q * 1024u >= open ? STORE_KB_OPEN : bk);
        }
    }
    return n;
}
int store_count(void) { return map_n; }
uint32_t store_last_save(void) { return next_save - 1; }     /* the highest seen: a new save goes after it */

uint8_t *store_scratch(void) { return (uint8_t *)scratch32; }

uint8_t *store_scratch_claim(int on)
{
    if (!on) { claimed = 0; return 0; }
    if (claimed || s_state == S_STAGING || s_state == S_WRITING) return 0;  /* another's, or a save's list */
    claimed = 1;
    return (uint8_t *)scratch32;
}

int store_entry(int i, store_rec_t *r, uint32_t *save, uint32_t *crc)
{
    if (i < 0 || i >= map_n) return 0;
    const ent_t *e = &map[i];
    r->kind = ent_kind(e); r->num = ent_num(e); r->sub = ent_sub(e);
    r->addr = ent_addr(e);
    r->ver = ent_ver(e);
    r->area = STORE_AREA_USER;
    r->deleted = (uint8_t)ent_del(e);
    r->len = ent_len(e);
    if (save) { uint8_t b[4]; *save = rd(r->addr + 8, b, 4) ? 0 : le32(b); }
    if (crc) *crc = r->len ? store_crc_flash(0, r->addr + STORE_HDR, r->len) : 0;
    return 1;
}
