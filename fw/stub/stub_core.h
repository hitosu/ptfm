/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The start check, its portable part (specs/hardware.md#updates-and-rescue): the slot's layout, the firmware's mark,
 * the box the start check, the firmware and the update loader share in RAM, and the decision. stub.c runs it on the
 * FM-1 at every start, before the firmware; fw/tests/stub_test.c on the PC.
 *
 * The slot (app.bin, from flash 0x4120, in place at 0x02000120; STUB_SLOT_LEN bytes, the package's slot):
 *   0x02000120 .. 0x02001000   the start check (STUB_LEN bytes, the same in every build: stub.lock)
 *   0x02001000 ..              the firmware (the body), its free part filled with 0xFF
 *   the slot's last 32 bytes   the mark: the body's length and CRC-32, written last by the update loader (the
 *                              block holding it is erased first), so a body not whole has no mark that fits it
 * The decision: the firmware starts when OCT- is not held, the firmware did not ask for update mode, the mark checks
 * out and the body's CRC-32 is the mark's; else the update loader kept in the staging runs (update mode), once; a
 * second time without the firmware starting between (the loader did not come up), or with no loader kept, the chip's
 * own USB mode. */
#ifndef STUB_CORE_H
#define STUB_CORE_H
#include <stdint.h>

#define STUB_XIP 0x02000120u                     /* the slot in place (flash 0x4120) */
#define STUB_SLOT_LEN 0x8DFBCu                   /* the slot's bytes (fm1pkg_make.APP_SLOT: build.py checks) */
#define STUB_LEN 0xEE0u                          /* the start check's room: flash 0x4120 .. 0x5000 */
#define STUB_APP_XIP (STUB_XIP + STUB_LEN)       /* 0x02001000: the firmware's first byte (its entry) */
#define STUB_MARK_LEN 32u
#define STUB_BODY_LEN (STUB_SLOT_LEN - STUB_LEN - STUB_MARK_LEN)
#define STUB_MARK_XIP (STUB_XIP + STUB_SLOT_LEN - STUB_MARK_LEN)
#define STUB_MARK_FLASH (0x4120u + STUB_SLOT_LEN - STUB_MARK_LEN)   /* 0x920BC: in the slot's last block */
#define STUB_MARK_MAGIC 0x4D465450u              /* "PTFM" */
#define STUB_BOX_ADDR 0x01C7FCF0u                /* the RAM records' last 16 B (app.ld's NOINIT ends below) */
#define STUB_BOX_MAGIC 0x31425453u               /* "STB1" */
#define STUB_LOADER_AT 0x000E0000u               /* the update loader kept: the staging (the firmware's today) */
#define STUB_LOADER_MAX 0x5000u                  /* the staging's bytes */
/* where the start check looks for a loader kept, in this order: the flash's end (PT/FM's own, 12 KB at 0x0FC000; a
 * bigger one staged from 0x0FA000), then the staging between the user area's regions (0x0E0000: older firmwares',
 * and PT/FM's until the staging moves); the first whole head wins */
#define STUB_LOADER_PLACES {0x000FC000u, 0x000FA000u, STUB_LOADER_AT}
#define STUB_NPLACES 3u

typedef struct {                                 /* the mark, little-endian, 32 B */
    uint32_t magic;                              /* STUB_MARK_MAGIC */
    uint32_t len;                                /* the body's bytes: STUB_BODY_LEN */
    uint32_t crc;                                /* the body's CRC-32 (zlib's) */
    uint32_t build;                              /* the image's CRC-32 as built (the debug part names it too) */
    uint32_t r0, r1;                             /* 0xFFFFFFFF */
    uint32_t self;                               /* the CRC-32 of the 24 bytes above */
    uint16_t pad;                                /* 0xFFFF */
    uint16_t fix;                                /* makes app.bin's CRC16 the same in every build (the slot's first
                                                  * block, its directory's CRCs, then equal too) */
} stub_mark_t;

typedef struct {                                 /* the box, at STUB_BOX_ADDR, kept across restarts; RAM may keep it
                                                  * through a power-off too: set anew at every power-on (stub_power_on) */
    uint32_t magic;                              /* STUB_BOX_MAGIC, else set anew */
    uint8_t req;                                 /* the firmware asks: 1 update mode (its boot guard), cleared */
    uint8_t tries;                               /* the update loader started since the firmware last did */
    uint8_t last;                                /* the last decision: STUB_APP, STUB_LOADER, STUB_ROM */
    uint8_t why;                                 /* its reasons: STUB_W_* */
    uint32_t check_us;                           /* the body's CRC-32 took (0: not read) */
    uint16_t starts;                             /* starts counted since the power-on */
    uint8_t p3;                                  /* the chip's reset reasons as this start read them (P33's: bit 0
                                                  * power-on, 2 the watchdog, 6 a software reset): the firmware takes
                                                  * them from here too, should a read of them clear them */
    uint8_t pad;
} stub_box_t;
typedef char stub_box_16[sizeof(stub_box_t) == 16u ? 1 : -1];

enum { STUB_APP = 1, STUB_LOADER = 2, STUB_ROM = 3 };
enum {
    STUB_W_OCT = 1,                              /* OCT- held alone at the start */
    STUB_W_REQ = 2,                              /* the firmware asked */
    STUB_W_MARK = 4,                             /* no mark that checks out */
    STUB_W_BODY = 8,                             /* the body's CRC-32 is not the mark's */
    STUB_W_NOLDR = 16,                           /* no update loader kept */
    STUB_W_AGAIN = 32                            /* the loader started once already without the firmware between */
};

/* CRC-32, zlib's (reflected 0xEDB88320, from 0xFFFFFFFF, inverted at the end); t: 256 words, stub_crc_table */
static inline void stub_crc_table(uint32_t *t)
{
    uint32_t i, k, c;
    for (i = 0; i < 256u; i++) {
        for (c = i, k = 0; k < 8u; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
}
static inline uint32_t stub_crc32(const uint32_t *t, uint32_t c, const uint8_t *p, uint32_t n)
{
    c = ~c;
    while (n--)
        c = t[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static inline uint32_t stub_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* the mark's 32 bytes check out: its magic, the body's length, its own CRC-32 */
static inline int stub_mark_ok(const uint32_t *t, const uint8_t *m)
{
    return stub_rd32(m) == STUB_MARK_MAGIC && stub_rd32(m + 4) == STUB_BODY_LEN &&
           stub_rd32(m + 24) == stub_crc32(t, 0, m, 24);
}

static inline uint32_t stub_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t stub_crc16(const uint8_t *p, uint32_t n)   /* poly 0x1021, init 0 (the package's) */
{
    uint32_t c = 0, i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8u; i++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}

/* a power-on, by the chip's reset reasons (P33's: bit 0 power-on, 2 the watchdog, 6 a software reset), as the
 * firmware's boot guard reads them: RAM can outlast a short power-off, so the box's magic alone does not tell */
static inline int stub_power_on(uint32_t p3) { return (p3 & 1u) && !(p3 & (4u | 0x40u)); }

/* the 32 bytes read at `at` are a loader's head staged there ("LOADER.BIN", its CRC16, its own address, a length the
 * staging holds) */
static inline int stub_loader_ok(const uint8_t *h, uint32_t at)
{
    static const char NAME[11] = "LOADER.BIN";
    uint32_t i, len = stub_rd32(h + 8);
    if (stub_crc16(h + 2, 30) != stub_rd16(h) || stub_rd32(h + 4) != at || h[12] != 0x41u ||
        len < 0x100u || len > STUB_LOADER_MAX || at + len > 0x000FF000u)
        return 0;
    for (i = 0; i < 11u; i++)
        if (h[16 + i] != (uint8_t)NAME[i])
            return 0;
    return 1;
}

/* what to start. why: what was seen (STUB_W_OCT, _REQ, _MARK, _BODY); loader: one is kept; tries: the box's.
 * Returns STUB_APP, STUB_LOADER or STUB_ROM, and the reasons in *out */
static inline int stub_decide(uint32_t why, int loader, uint32_t tries, uint32_t *out)
{
    if (!(why & (STUB_W_OCT | STUB_W_REQ | STUB_W_MARK | STUB_W_BODY))) {
        *out = 0;
        return STUB_APP;
    }
    if (!loader)
        why |= STUB_W_NOLDR;
    if (tries)
        why |= STUB_W_AGAIN;
    *out = why;
    return loader && !tries ? STUB_LOADER : STUB_ROM;
}

/* the update record that runs the loader kept at `at` (as the firmware's install does: ota.c) */
static inline void stub_parm(uint8_t *p, uint32_t at)   /* 112 bytes */
{
    static const char ID[13] = "ota-FM-1_015";
    uint32_t i;
    for (i = 0; i < 112u; i++)
        p[i] = 0;
    p[2] = 0x0D; p[3] = 0x5A;
    p[4] = 0x01; p[5] = 0x5A;
    p[6] = 0x41; p[7] = 0x54;
    for (i = 0; i < 12u; i++)
        p[8 + i] = (uint8_t)ID[i];
    p[72] = (uint8_t)at; p[73] = (uint8_t)(at >> 8);
    p[74] = (uint8_t)(at >> 16); p[75] = (uint8_t)(at >> 24);
    i = stub_crc16(p + 2, 78);
    p[0] = (uint8_t)i;
    p[1] = (uint8_t)(i >> 8);
}
#endif
