/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (the mark's block first and last, the hashes, the staging's record only) */
/* The update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   3. with a host that answers hashes (ldr_hashed: our install page), asks
 *      the CRC-32 of each block of the app area and compares it with the
 *      block in the flash, so only the blocks that differ are fetched;
 *   4. fetches the block holding the mark (the slot's last: LDR_MARK_BLK) and,
 *      when it is not the flash's (another build), erases it first, so the
 *      start check (fw/stub) finds no mark until the end; then writes ONLY the
 *      app area [0x4000, 0x93000), block by block, skipping blocks already
 *      equal, and verifies each one; the flash head (SPL, isd_config) is never
 *      written;
 *   5. checks the CRC16 the package stores for flash.bin against everything it
 *      was served (or, for a block found equal by its hash, read from the
 *      flash; the head too, which it never writes; a mismatch: up to two more
 *      passes, rewriting what differs, the mark's block left erased), writes
 *      the mark's block, asks 0xF0000000 ("success"), then drops the update
 *      record (RAM, and a flash copy an older firmware left in the staging)
 *      and resets: the start check finds the new mark.
 * A cut anywhere in 4 leaves no mark that fits: at the next power-on the start
 * check starts this loader again (kept in the staging) and the host resumes.
 *
 * Hooks from the platform (loader.c, tests/ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_flash_known()     the flash chip is the known 1 MiB part (else nothing is
 *                         written: the update records go and the old firmware starts again)
 *   ldr_hashed()          the host answers hash reads (0xD0000000 | offset: the CRC-32 of 4 KB there)
 *   ldr_progress(phase, done, total)   LDR_PH_CHECK / LDR_PH_WRITE, blocks
 * plus the ota.c hooks (frames, time, idle). */
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI 0x93000u
#define LDR_NBLK ((LDR_APP_HI - LDR_APP_LO) / 0x1000u)
#define LDR_MARK_BLK 0x92000u                   /* the mark's block (fw/stub/stub_core.h: STUB_MARK_FLASH) */
#define LDR_REC_BLK 0xE4000u                    /* the staging's last block: an older firmware's flash copy of the
                                                 * update record (at its last 256 B) */
#define LDR_HASH_AT 0xD0000000u                 /* a hash read: the CRC-32 of the package's 4 KB at the offset */
enum { LDR_PH_CHECK = 1, LDR_PH_WRITE = 2 };

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static int ldr_flash_known(void);
static int ldr_hashed(void);
static void ldr_progress(uint32_t phase, uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
static int ldr_chip_key(uint32_t *key)
{
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

/* CRC-32 (zlib's), the hashes' */
static uint32_t ldr_crc_t[256];
static uint32_t ldr_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i, k;
    if (!ldr_crc_t[1])
        for (i = 0; i < 256u; i++) {
            for (c = i, k = 0; k < 8u; k++)
                c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            ldr_crc_t[i] = c;
        }
    c = 0xFFFFFFFFu;
    while (n--)
        c = ldr_crc_t[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

#define LDR_EBADPKG (-12)                       /* flash.bin as served does not match the package's CRC16 */
#define LDR_ENOFIN (-13)                        /* no "success" from the host */
#define LDR_EFLASH (-14)                        /* another flash chip: nothing written, the records dropped */

/* the update records go: the RAM one (the boot then starts the firmware), and a flash copy an older firmware's
 * install left in the staging's last block, unless the loader kept there reaches into it */
static void ldr_records_drop(void)
{
    uint8_t r[80], h[32];
    ldr_record_clear();
    if (ldr_fread(LDR_REC_BLK + 0xF00u, r, 80) || ldr_fread(LDR_REC_BLK - 0x4000u, h, 32))
        return;
    if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0) &&
        !(ota_crc16(h + 2, 30, 0) == ota_rd16(h) && ota_rd32(h + 8) > 0x4000u))
        ldr_erase(LDR_REC_BLK);
}

static int ldr_block_write(uint32_t s, const uint8_t *sec, uint8_t *cur)   /* erase, program, verify: twice at most */
{
    uint32_t tries, k, p;
    for (tries = 0; tries < 2u; tries++) {
        for (k = 0; k < 0x1000u && cur[k] == 0xFFu; k++)
            ;
        if ((tries || k < 0x1000u) && ldr_erase(s))     /* (a block blank already, as the mark's: no erase) */
            return -9;
        for (p = 16u; p-- > 0u;)                         /* (its pages last to first: the mark's block's page 0, */
            if (ldr_prog(s + p * 256u, sec + p * 256u, 256))   /* the mark's, written last) */
                return -10;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (ota_memeq(sec, cur, 0x1000u))
            return 0;
    }
    return -11;                                          /* verify failed twice */
}

static int ldr_pass(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000], mark[0x1000], same[LDR_NBLK];
    uint32_t i, k, fl_off, fl_size, ota_off, ota_len, key, s, n, want, crc = 0, b, mb, mark_new;
    uint32_t hashed = (uint32_t)ldr_hashed();
    int rc;
    /* 1. UFW header + entry list (ota.c) */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_size, &ota_off, &ota_len, &want)) != 0)
        return rc;
    if (!fl_off || fl_size < LDR_APP_HI)
        return -3;
    /* 2. the package's app area must decrypt with this chip's key */
    if ((rc = ldr_chip_key(&key)) != 0)
        return -40 + rc;
    if (ota_read(fl_off + LDR_APP_LO, sec, 32))
        return -5;
    ldr_sfc(sec, 32, 0, key);
    if (ota_crc16(sec + 2, 30, 0) != ota_rd16(sec))
        return -6;                                       /* package for another chip key */
    if (!ldr_flash_known()) {                            /* an unknown flash: a firmware written there could */
        ldr_records_drop();                              /* not be updated again: keep the one there is */
        return LDR_EFLASH;
    }
    /* the head of flash.bin is never written, but it is part of the package's CRC */
    for (s = 0; s < LDR_APP_LO; s += 512u) {
        if (ota_read(fl_off + s, sec, 512))
            return -7;
        crc = ota_crc16(sec, 512, crc);
    }
    /* 3. which blocks are equal already, by their hashes (a host that answers them) */
    for (b = 0; b < LDR_NBLK; b++)
        same[b] = 0;
    if (hashed)
        for (s = LDR_APP_LO, b = 0; s < LDR_APP_HI; s += 0x1000u, b++) {
            if (ota_read(LDR_HASH_AT | (fl_off + s), sec, 4))
                return -15;
            if (ldr_fread(s, cur, 0x1000u))
                return -8;
            same[b] = (uint8_t)(ldr_crc32(cur, 0x1000u) == ota_rd32(sec));
            ldr_progress(LDR_PH_CHECK, b + 1u, LDR_NBLK);
        }
    /* 4. the mark's block: fetched first, erased first when it is not the flash's (another build), written last,
     *    once all the rest adds up; then the app area block by block */
    mb = (LDR_MARK_BLK - LDR_APP_LO) >> 12;
    mark_new = 0;
    if (!same[mb]) {
        for (k = 0; k < 0x1000u; k += 512u)
            if (ota_read(fl_off + LDR_MARK_BLK + k, mark + k, 512))
                return -7;
        if (ldr_fread(LDR_MARK_BLK, cur, 0x1000u))
            return -8;
        mark_new = !ota_memeq(mark, cur, 0x1000u);
        for (k = 0; k < 0x1000u && cur[k] == 0xFFu; k++)
            ;
        if (mark_new && k < 0x1000u && ldr_erase(LDR_MARK_BLK))
            return -9;
    }
    for (s = LDR_APP_LO, b = 0; s < LDR_APP_HI; s += 0x1000u, b++) {
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (same[b]) {                                   /* equal by its hash: the flash's bytes are the package's */
            crc = ota_crc16(cur, 0x1000u, crc);
        } else if (b == mb) {                            /* (fetched above; written once all adds up) */
            crc = ota_crc16(mark, 0x1000u, crc);
        } else {
            for (k = 0; k < 0x1000u; k += 512u)
                if (ota_read(fl_off + s + k, sec + k, 512))
                    return -7;
            crc = ota_crc16(sec, 0x1000u, crc);
            if (!ota_memeq(sec, cur, 0x1000u) && (rc = ldr_block_write(s, sec, cur)) != 0)
                return rc;
        }
        ldr_progress(LDR_PH_WRITE, b + 1u, LDR_NBLK);
    }
    for (s = LDR_APP_HI; s < fl_size; s += n) {          /* (a flash.bin longer than the app area) */
        n = fl_size - s < 512u ? fl_size - s : 512u;
        if (ota_read(fl_off + s, sec, n))
            return -7;
        crc = ota_crc16(sec, n, crc);
    }
    /* 5. finish: the whole package arrived as it was built (else the mark's block stays erased: the start check
     *    refuses the body, this loader starts again), the mark, the host confirms, the record goes, the SPL boots
     *    the app */
    if (crc != want)
        return LDR_EBADPKG;
    if (mark_new) {
        if (ldr_fread(LDR_MARK_BLK, cur, 0x1000u))
            return -8;
        if ((rc = ldr_block_write(LDR_MARK_BLK, mark, cur)) != 0)
            return rc;
        ldr_progress(LDR_PH_WRITE, LDR_NBLK, LDR_NBLK);
    }
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, sec, 8))
            break;
    if (i == 4u)
        return LDR_ENOFIN;
    ldr_records_drop();
    return 0;
}

/* a pass that does not add up (a byte damaged on the way) is run again: the host answers any read, so it
 * just serves the image once more and the blocks that differ are rewritten; a package that is damaged
 * itself fails every pass and the record stays */
static int ldr_session(void)
{
    int rc = LDR_EBADPKG;
    uint32_t pass;
    for (pass = 0; pass < 3u && rc == LDR_EBADPKG; pass++)
        rc = ldr_pass();
    return rc;
}
