/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Seeding, the device's side (seed.h; the bytes: fw/web/seed.js). The uploader owns placement: the
 * device only checks that a block lies in a region it reported, and writes it: a block's first chunk
 * erases it (unless it is erased), every chunk is programmed as it comes, and BLOCK checks the block's
 * CRC read back from the flash. Messages are unpacked and packed in the store's scratch (claimed while
 * seeding uses it); ACKs have a small buffer of their own. */
#include <string.h>
#include "seed.h"
#include "store.h"
#include "builtin.h"
#include "crc.h"
#include "../plat.h"
#include "../play/player.h"

#ifndef PTFM_VERSION
#define PTFM_VERSION ""                 /* the build stamps it: the date and time, DEV after a dev build's */
#endif

#define PROTOCOL   1
#define LAYOUT_VER 1
#define MSG_DATA   512                  /* data bytes a message at most */
#define LAY_PART   (MSG_DATA - 8)       /* a LAYOUT message: total, offset, then this much */
#define TIMEOUT_MS 10000
#define IN_BYTES   520                  /* the scratch: an incoming message's data, unpacked, or one to send */
#define OUT_BYTES  600                  /* then a message packed: 6 + 74 x 8 at most */

enum { C_HELLO = 0x01, C_LAYOUT = 0x02, C_READ = 0x03, C_DATA = 0x04, C_BEGIN = 0x10, C_CHUNK = 0x11,
       C_BLOCK = 0x12, C_END = 0x13, C_ABORT = 0x1F, C_ACK = 0x20 };

/* the (kind, version) pairs this firmware reads (specs/files/store.md#a-record) */
static const uint8_t ACCEPTS[][2] = {
    { REC_DX7, 1 }, { REC_JUNO, 1 }, { REC_TB303, 1 }, { REC_SAMPLER, 1 }, { REC_FUSION, 1 }, { REC_CHIP, 1 },
    { REC_AUDIO, 1 }, { REC_PATTERN, 1 }, { REC_SONG, 1 }, { REC_LOOPS, 4 }, { REC_DJ, 1 },
    { REC_SETTINGS, 1 }, { REC_MIDIMAP, 1 }
};
#define NACCEPT ((int)(sizeof ACCEPTS / sizeof ACCEPTS[0]))

typedef char scratch_fits[IN_BYTES + OUT_BYTES <= STORE_SEED_SCRATCH ? 1 : -1];

static int active, lay_sending, out_pending, cur_block = -1;
static uint32_t last_ms, lay_total, lay_off, out_n;
static uint16_t blocks_total, blocks_done;
static int (*unsaved)(void);
static seed_info_t info;
static uint8_t *scr;                    /* the store's scratch while claimed, else 0 */
static uint8_t ackb[24];                /* an ACK, packed */
static const uint8_t *out;              /* the message waiting to go: ackb, or in the scratch */

void seed_init(void)
{
    active = lay_sending = out_pending = 0;
    cur_block = -1;
    blocks_total = blocks_done = 0;
    scr = 0;
    memset(&info, 0, sizeof info);
}

void seed_set_unsaved(int (*fn)(void)) { unsaved = fn; }
int seed_active(void) { return active; }
void seed_info(seed_info_t *s)
{
    info.active = (uint8_t)active;
    info.done = blocks_done;
    info.total = blocks_total;
    *s = info;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t g16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t g32(const uint8_t *p) { return g16(p) | g16(p + 2) << 16; }

static void release(void)               /* the scratch back, once nothing of seeding's is in it */
{
    if (scr && !lay_sending && !(out_pending && out != ackb)) { store_scratch_claim(0); scr = 0; }
}

/* ---- messages ---- */
static uint32_t pack(uint8_t *o, uint8_t cmd, const uint8_t *d, uint32_t n)
{
    uint32_t k = 0;
    o[k++] = 0xF0; o[k++] = 0x7D; o[k++] = 0x50; o[k++] = 0x54; o[k++] = cmd;
    for (uint32_t i = 0; i < n; i += 7) {
        uint32_t m = n - i < 7 ? n - i : 7;
        uint8_t top = 0;
        for (uint32_t j = 0; j < m; j++) if (d[i + j] & 0x80) top |= (uint8_t)(1u << j);
        o[k++] = top;
        for (uint32_t j = 0; j < m; j++) o[k++] = d[i + j] & 0x7F;
    }
    o[k++] = 0xF7;
    return k;
}

static void send_now(const uint8_t *m, uint32_t n)
{
    out = m;
    out_n = n;
    out_pending = plat_sysex_send(m, n) != 0;
}

static void send_big(uint8_t cmd, uint32_t n)       /* a LAYOUT or DATA message: its data at scr[0..n) */
{
    send_now(scr + IN_BYTES, pack(scr + IN_BYTES, cmd, scr, n));
}

static void ack(uint8_t cmd, uint32_t block, uint8_t result, const uint8_t *extra, uint32_t n)
{
    uint8_t d[12];
    d[0] = cmd; put16(d + 1, block); d[3] = result;
    if (n > sizeof d - 4) n = sizeof d - 4;
    if (n) memcpy(d + 4, extra, n);
    send_now(ackb, pack(ackb, C_ACK, d, 4 + n));
}

/* ---- the layout ---- */
static uint8_t *ly_dst;
static uint32_t ly_pos, ly_lo, ly_hi;
static void E8(uint32_t b) { if (ly_pos >= ly_lo && ly_pos < ly_hi) ly_dst[ly_pos - ly_lo] = (uint8_t)b; ly_pos++; }
static void E16(uint32_t v) { E8(v); E8(v >> 8); }
static void E32(uint32_t v) { E16(v); E16(v >> 16); }
static void Ereg(uint32_t s, uint32_t e, int area, int in_place) { E32(s); E32(e); E8((uint32_t)area); E8((uint32_t)in_place); E16(0); }

/* Writes the layout's bytes lo .. hi into dst; returns its whole length. A record's payload CRC is reckoned
 * when its bytes are in the window. */
static uint32_t layout(uint8_t *dst, uint32_t lo, uint32_t hi)
{
    ly_dst = dst; ly_pos = 0; ly_lo = lo; ly_hi = hi;
    int nreg = 2;                                       /* the user area's (FWS is never written) */
    int nrec = store_count();
    E8('P'); E8('T'); E8('L'); E8('Y'); E8(LAYOUT_VER); E8(PROTOCOL); E16(0);
    E32(0); E32(plat_fw_end()); E16(MSG_DATA); E16(STORE_RESERVE);
    E8((uint32_t)nreg); E8(NACCEPT); E16((uint32_t)store_builtin_count());
    E32(store_last_save()); E32((uint32_t)nrec);
    E8(store_edit_count() || (unsaved && unsaved()) ? 1 : 0); E8(0); E16(0);
    Ereg(STORE_U1_START, STORE_U1_END, 0, 1);
    Ereg(STORE_U2_START, STORE_U2_END, 0, 1);
    for (int i = 0; i < NACCEPT; i++) { E8(ACCEPTS[i][0]); E8(ACCEPTS[i][1]); }
    for (int i = 0; i < store_builtin_count(); i++) {       /* (the firmware's own extras too: the EDITOR's pattern) */
        const builtin_rec_t *b = store_builtin_at(i);
        E8(b->kind); E8(b->num); E8(b->sub); E8(b->ver);
    }
    for (int i = 0; i < nrec; i++) {
        store_rec_t r;
        uint32_t save = 0, crc = 0;
        if (ly_pos + 24 > lo && ly_pos < hi) store_entry(i, &r, &save, &crc);
        else memset(&r, 0, sizeof r);                   /* outside the window: only its length counts */
        E8(r.kind); E8(r.num); E8(r.sub); E8(r.ver);
        E32(crc); E32(r.addr); E32(r.len); E32(save);
        E8(r.deleted ? 2u : 0u); E8(0); E16(0);
    }
    {                                                   /* the firmware's version, as text */
        static const char v[] = PTFM_VERSION;
        uint32_t n = sizeof v - 1 < 64 ? sizeof v - 1 : 64;
        E8(n);
        for (uint32_t i = 0; i < n; i++) E8((uint8_t)v[i]);
    }
    return ly_pos;
}

static void layout_part(void)
{
    uint32_t n = lay_total - lay_off < LAY_PART ? lay_total - lay_off : LAY_PART;
    put32(scr, lay_total); put32(scr + 4, lay_off);
    layout(scr + 8, lay_off, lay_off + n);
    lay_off += n;
    if (lay_off >= lay_total) lay_sending = 0;
    send_big(C_LAYOUT, 8 + n);
}

/* ---- the end ---- */
static void finish(uint8_t cmd)
{
    uint8_t ex[8];
    store_lock(0);
    store_mount();                                      /* (it uses the scratch: nothing of ours is left there) */
    /* the player told before it runs again (the audio is silent while active): nothing it held is valid */
    player_all_sound_off();
    player_store_changed();
    active = 0;
    cur_block = -1;
    store_usage_t u;
    store_usage(&u);
    uint32_t live = 0;
    for (int k = 0; k < STORE_NKINDS; k++) live += u.count[k];
    info.ended = 1;
    info.records = (uint16_t)live;
    info.bad = u.bad;
    info.result = SEED_OK;
    put16(ex, live); put16(ex + 2, u.bad); put32(ex + 4, store_last_save());
    if (cmd) ack(cmd, 0, SEED_OK, ex, 8);
}

static int in_user(uint32_t a, uint32_t n)      /* within a region the layout reported: the user area */
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END && a + n >= a) ||
           (a >= STORE_U2_START && a + n <= STORE_U2_END && a + n >= a);
}
static int block_ok(uint32_t b) { return b <= 0xFF && in_user(b << 12, STORE_BLOCK); }

static int open_block(uint32_t b)               /* a block's first chunk: erased unless it is */
{
    uint32_t a = b << 12;
    cur_block = (int)b;
    if (store_all_ff(a, STORE_BLOCK)) return 0;
    if (plat_flash_erase(a) || !store_all_ff(a, STORE_BLOCK)) { cur_block = -1; return -1; }
    return 0;
}

static int program(uint32_t a, const uint8_t *d, uint32_t n)    /* within one block, page by page */
{
    while (n) {
        uint32_t k = 256 - (a & 255u);
        if (k > n) k = n;
        int blank = 1;
        for (uint32_t i = 0; i < k; i++) if (d[i] != 0xFF) { blank = 0; break; }
        if (!blank && plat_flash_prog(a, d, k)) return -1;
        a += k; d += k; n -= k;
    }
    return 0;
}

static uint8_t refuse(void)                     /* why seeding cannot start now, or SEED_OK */
{
    if (store_saving()) return SEED_BUSY;
    if (store_edit_count() || (unsaved && unsaved())) return SEED_UNSAVED;
    return SEED_OK;
}

/* ---- the commands ---- */
static void command(uint8_t cmd, const uint8_t *in, uint32_t len)
{
    uint8_t r;
    switch (cmd) {
    case C_HELLO:
        if (active) finish(0);
        info.refused = 0;
        if (len < 1) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); return; }
        if (in[0] != PROTOCOL) { ack(cmd, 0, SEED_VERSION, 0, 0); return; }
        if ((r = refuse())) { info.refused = 1; info.result = (int8_t)r; ack(cmd, 0, r, 0, 0); return; }
        lay_total = layout(scr, 0, 0);
        lay_off = 0;
        lay_sending = 1;
        if (!out_pending) layout_part();
        return;
    case C_READ: {                                      /* the library read back: the editor page's */
        if (len < 6) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); return; }
        uint32_t a = g32(in), k = g16(in + 4);
        if (k > LAY_PART || !in_user(a, k)) { ack(cmd, 0, SEED_BAD_BLOCK, 0, 0); return; }
        if (active) { ack(cmd, 0, SEED_BUSY, 0, 0); return; }
        put32(scr, a);                                  /* (the request is read: its bytes may go) */
        if (plat_flash_read(a, scr + 4, k)) { ack(cmd, 0, SEED_FLASH, 0, 0); return; }
        send_big(C_DATA, 4 + k);
        return;
    }
    case C_BEGIN:
        if (len < 6) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); return; }
        if ((r = refuse())) { info.refused = 1; info.result = (int8_t)r; ack(cmd, 0, r, 0, 0); return; }
        /* the plan was made on the layout: a save since (its number taken) and it is stale */
        if (g32(in) != store_last_save() + 1) { info.result = SEED_CHANGED; ack(cmd, 0, SEED_CHANGED, 0, 0); return; }
        lay_sending = 0;
        player_all_sound_off();                         /* (the audio goes silent with active: its voices let go) */
        active = 1;
        store_lock(1);
        blocks_total = (uint16_t)g16(in + 4);
        blocks_done = 0;
        cur_block = -1;
        info.refused = 0;
        info.result = SEED_OK;
        ack(cmd, 0, SEED_OK, 0, 0);
        return;
    case C_CHUNK: {
        if (len < 4) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); return; }
        uint32_t b = g16(in), off = g16(in + 2), k = len - 4;
        if (!active) { ack(cmd, b, SEED_NOT_BEGUN, 0, 0); return; }
        if (!block_ok(b)) { ack(cmd, b, SEED_BAD_BLOCK, 0, 0); return; }
        if (off + k > STORE_BLOCK) { ack(cmd, b, SEED_BAD_MSG, 0, 0); return; }
        if (((int)b != cur_block && open_block(b)) || program((b << 12) + off, in + 4, k)) {
            info.result = SEED_FLASH;
            ack(cmd, b, SEED_FLASH, 0, 0);
            return;
        }
        ack(cmd, b, SEED_OK, 0, 0);
        return;
    }
    case C_BLOCK: {
        if (len < 6) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); return; }
        uint32_t b = g16(in), crc = g32(in + 2);
        if (!active) { ack(cmd, b, SEED_NOT_BEGUN, 0, 0); return; }
        if (!block_ok(b)) { ack(cmd, b, SEED_BAD_BLOCK, 0, 0); return; }
        if ((int)b != cur_block && open_block(b)) r = SEED_FLASH;          /* a block of only 0xFF */
        else r = store_crc_flash(0, b << 12, STORE_BLOCK) == crc ? SEED_OK : SEED_BAD_CRC;
        cur_block = -1;                                 /* the next chunk for it starts it again */
        if (r == SEED_OK) blocks_done++;
        info.result = (int8_t)r;
        ack(cmd, b, r, 0, 0);
        return;
    }
    case C_END:
        if (!active) { ack(cmd, 0, SEED_NOT_BEGUN, 0, 0); return; }
        finish(cmd);
        return;
    case C_ABORT:
        if (active) finish(cmd);
        else ack(cmd, 0, SEED_OK, 0, 0);
        return;
    }
}

void seed_sysex(const uint8_t *msg, uint32_t n)
{
    if (n < 6 || msg[0] != 0xF0 || msg[1] != 0x7D || msg[2] != 0x50 || msg[3] != 0x54 || msg[n - 1] != 0xF7)
        return;
    uint8_t cmd = msg[4];
    if (cmd != C_HELLO && cmd != C_READ && cmd != C_BEGIN && cmd != C_CHUNK && cmd != C_BLOCK && cmd != C_END &&
        cmd != C_ABORT)
        return;                                         /* another PT/FM protocol's, or the device's own */
    last_ms = plat_ms();
    if (!scr) scr = store_scratch_claim(1);
    if (!scr) { ack(cmd, 0, SEED_BUSY, 0, 0); return; } /* a save is being made */
    /* the payload, 7 bytes in 8 */
    uint32_t len = 0;
    for (uint32_t i = 5; i < n - 1; ) {
        uint8_t top = msg[i++];
        for (uint32_t j = 0; j < 7 && i < n - 1; j++, i++) {
            if (len >= IN_BYTES || (msg[i] & 0x80)) { ack(cmd, 0, SEED_BAD_MSG, 0, 0); release(); return; }
            scr[len++] = (uint8_t)(msg[i] | (top >> j & 1) << 7);
        }
    }
    command(cmd, scr, len);
    release();                                          /* (kept while a LAYOUT or DATA waits in it) */
}

void seed_poll(void)
{
    if (out_pending) {
        if (plat_sysex_send(out, out_n)) return;
        out_pending = 0;
    }
    if (lay_sending) { layout_part(); return; }
    release();
    if (active && plat_ms() - last_ms > TIMEOUT_MS) finish(0);    /* 10 s without a message */
}
