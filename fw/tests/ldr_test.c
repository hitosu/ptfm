/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (the paths: PT/FM's fw/; the hashes, the mark's order, the power cuts) */
/* Host test of the update loader (fw/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc
 * Checked: the install, the head never written, the update records dropped, a damaged byte, a damaged package,
 * no "success", another flash chip; with a host that answers hashes (PT/FM's pages) the same, and only the blocks
 * that differ fetched; the block holding the mark erased first and written last; and a power cut after every erase
 * and every page program: the flash then holds a complete mark only over the body it belongs to (what the start
 * check needs: fw/stub), and a new session finishes the install. The flash is compared as stored, never decoded. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define FW_ID "ota-FM-1_901"
#define FW_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, fetches, hash_reads, erases, progs, bad_range, record_cleared,
    f0_asked;
static uint32_t corrupt_at, corrupt_once, no_finish;   /* the host serves a damaged byte / never says "success" */
static uint32_t answer_hashes;                         /* the host answers 0xD0000000 | offset reads */
static uint32_t slot_lo, slot_fetches;                 /* (flash.bin's offset; 512-B reads of the slot's blocks) */
static uint32_t first_erase, last_written, first_block_erased;   /* (the order: the mark's block first and last) */
static uint32_t last_page;                             /* (the last page programmed: the mark's, 0x92000) */
static uint32_t cut_after, ops;                        /* a power cut after the cut_after-th erase or program */
static jmp_buf cut;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static uint32_t host_crc32(const uint8_t *p, uint32_t n)      /* zlib's, as the pages compute it */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xF0000000u && no_finish) {
        f0_asked++;
        return 0;                                    /* no reply */
    }
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else if ((addr & 0xF0000000u) == 0xD0000000u) {
        uint32_t off = addr & 0x0FFFFFFFu, c;
        if (!answer_hashes) { printf("a hash read from a loader not told the host answers them\n"); exit(1); }
        if (len != 4 || off + 4096 > logical_len) { printf("a bad hash read %#x+%u\n", addr, len); exit(1); }
        hash_reads++;
        c = host_crc32(logical + off, 4096);
        memcpy(m + 14, &c, 4);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        fetches++;
        slot_fetches += len == 512 && addr >= slot_lo + 0x4000u && addr < slot_lo + 0x93000u;
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && addr <= corrupt_at && corrupt_at < addr + len) {
            m[14 + corrupt_at - addr] ^= 0x08;       /* (the frame's own checksum is fine: only the CRC sees it) */
            if (corrupt_once)
                corrupt_at = 0;
        }
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../device/ota.c"

static void op(void)                                 /* one flash operation done: the power may go now */
{
    if (cut_after && ++ops == cut_after)
        longjmp(cut, 1);
}
static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (!((off >= 0x4000u && off < 0x93000u) || off == 0xE4000u) || (off & 0xFFFu)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    if (!erases++) first_erase = off;
    first_block_erased |= off == 0x4000u;
    memset(nor + off, 0xFF, 0x1000);
    op();
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > 0x93000u || ((off & 0xFFu) + n) > 256u) bad_range++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    progs++;
    last_written = off & ~0xFFFu;
    last_page = off;
    op();
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
#include "../hal/fm1_flash_id.h"
static uint32_t flash_id = FL_JEDEC_PUYA;           /* the simulated chip's JEDEC ID, judged as loader.c does */
static int ldr_flash_known(void) { return fl_jedec_ok(flash_id); }
static int ldr_hashed(void) { return (int)answer_hashes; }
static void ldr_progress(uint32_t phase, uint32_t done, uint32_t total) { (void)phase; (void)done; (void)total; }
#include "../loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static int check(const char *what, int ok) { printf("%-64s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static void put_record(void)                         /* a valid update record at 0xE4F00 (an older firmware's) */
{
    uint8_t r[112] = {0};
    r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
    ota_wr16(r, ota_crc16(r + 2, 78, 0));
    memcpy(nor + 0xE4F00, r, sizeof r);
}

static const uint8_t *oldf, *newf;                   /* the two flash.bin images (the slot from 0x4000) */
#define MARK_BLK 0x92000u
/* the start check's view, as stored: a complete mark's block (OLD's or NEW's) only over that version's whole body */
static int mark_fits(void)
{
    const uint8_t *v = !memcmp(nor + MARK_BLK, newf + MARK_BLK, 0x1000) ? newf :
                       !memcmp(nor + MARK_BLK, oldf + MARK_BLK, 0x1000) ? oldf : 0;
    return !v || !memcmp(nor + 0x4000, v + 0x4000, MARK_BLK - 0x4000);
}

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo, h, total_ops, cuts = 0, cut_bad = 0, resume_bad = 0;
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    oldf = old + ofo;
    newf = logical + nfo;
    slot_lo = nfo;
    for (h = 0; h < 2; h++) {
        const char *how = h ? " (hashes)" : "";
        char what[96];
        answer_hashes = h;
        /* device: OLD installed, plus a valid update record at 0xE4F00 */
        memset(nor, 0xFF, sizeof nor);
        memcpy(nor, oldf, 0x93000);
        memcpy(head, nor, sizeof head);
        put_record();
        requests = fetches = hash_reads = erases = progs = record_cleared = f0_asked = bad_range = 0;
        first_block_erased = 0;
        rc = ldr_session();
        printf("  rc %d, %u requests (%u fetches, %u hashes), %u block erases%s\n", rc, requests, fetches, hash_reads,
               erases, how);
        snprintf(what, sizeof what, "install completes%s", how);
        bad += check(what, rc == 0);
        bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000));
        bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
        bad += check("finish asked (0xF0000000)", f0_asked >= 1);
        bad += check("update record cleared (RAM + flash)", record_cleared && nor[0xE4F06] == 0xFF);
        bad += check("no write outside the allowed windows", bad_range == 0);
        if (memcmp(oldf + MARK_BLK, newf + MARK_BLK, 0x1000)) {
            bad += check("the mark's block erased first", first_erase == MARK_BLK);
            bad += check("... and written last, the page holding the mark the very last", last_written == MARK_BLK &&
                         last_page == MARK_BLK);
        }
        bad += check(memcmp(oldf + 0x4000, newf + 0x4000, 0x1000) ? "the slot's first block written (it differs)" :
                     "the slot's first block not written (equal: the start check's)",
                     first_block_erased == (memcmp(oldf + 0x4000, newf + 0x4000, 0x1000) != 0));
        /* same package again: nothing to erase */
        erases = 0; requests = fetches = slot_fetches = hash_reads = 0;
        rc = ldr_session();
        bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
        if (h)
            bad += check("... and fetches none of the slot's blocks (equal by their hashes)",
                         slot_fetches == 0 && hash_reads == LDR_NBLK);
        {                                            /* one block of the firmware different: it alone is written */
            nor[0x40000] ^= 0x01;
            erases = 0; slot_fetches = 0;
            rc = ldr_session();
            bad += check(h ? "one block different: it alone fetched and written" :
                         "one block different: it alone written, the mark's block kept",
                         rc == 0 && erases == 1 && (!h || slot_fetches == 8u) &&
                         !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000));
        }
        /* a package for another chip key is refused before any erase */
        {
            uint8_t save = logical[nfo + 0x4000 + 5];
            logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
            memcpy(nor, oldf, 0x93000);
            erases = 0;
            rc = ldr_session();
            bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
            logical[nfo + 0x4000 + 5] = save;
        }
        /* one bit damaged on the way, once: the CRC sees it, the next pass rewrites that block */
        {
            const uint8_t r6 = 0x41;                     /* byte 6 of a valid record ("AT"); 0xFF once erased */
            memcpy(nor, oldf, 0x93000);
            put_record();
            record_cleared = 0;
            corrupt_at = nfo + 0x30123u;
            corrupt_once = 1;
            rc = ldr_session();
            bad += check("a byte damaged once: caught by the CRC, fixed by a 2nd pass", rc == 0 && record_cleared
                         && nor[0xE4F06] == 0xFF && !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000));
            /* the package itself damaged (every pass): the record stays, nothing is booted */
            memcpy(nor, oldf, 0x93000);
            put_record();
            record_cleared = 0;
            corrupt_at = nfo + 0x30123u;
            corrupt_once = 0;
            rc = ldr_session();
            bad += check("a damaged package: refused after 3 passes, the record stays", rc == -12 && !record_cleared
                         && nor[0xE4F06] == r6);
            bad += check("... the mark's block left erased: no mark over that body", mark_fits());
            corrupt_at = nfo + 0x100u;                   /* in the head: never written, still checked */
            rc = ldr_session();
            bad += check("... also when the damage is in the head", rc == -12 && !record_cleared);
            corrupt_at = 0;
            /* no "success" from the host: the record stays (the host can finish later) */
            no_finish = 1;
            rc = ldr_session();
            bad += check("no \"success\" from the host: the record stays", rc == -13 && !record_cleared &&
                         nor[0xE4F06] == r6);
            no_finish = 0;
            rc = ldr_session();
            bad += check("... and the next session finishes", rc == 0 && record_cleared && nor[0xE4F06] == 0xFF);
        }
        /* another flash chip: nothing written, the records go (the old firmware starts again) */
        {
            memcpy(nor, oldf, 0x93000);
            put_record();
            record_cleared = 0;
            erases = 0;
            flash_id = 0xEF4014u;                   /* (a chip no FM-1 has: a Winbond W25Q80's ID) */
            rc = ldr_session();
            bad += check("unknown flash chip: nothing written, record dropped", rc == -14 && record_cleared &&
                         erases == 1 && nor[0xE4F06] == 0xFF && !memcmp(nor, oldf, 0x93000));
        }
        /* the later FM-1s' chip (UC25HQ80): installed as on the first one */
        {
            memcpy(nor, oldf, 0x93000);
            put_record();
            record_cleared = 0;
            flash_id = FL_JEDEC_UCUN;
            rc = ldr_session();
            bad += check("the UC25HQ80 (B36014): the install goes through", rc == 0 && record_cleared &&
                         !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000));
            flash_id = FL_JEDEC_PUYA;
        }
    }
    /* power cuts: after each erase and each program of a whole install (OLD -> NEW), the flash as the start check
     * sees it, then a new session from there (as after the next power-on: the loader kept starts again) */
    answer_hashes = 1;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, oldf, 0x93000);
    erases = progs = 0;
    cut_after = 0;
    ldr_session();
    total_ops = erases + progs;
    for (cut_after = 1; cut_after <= total_ops; cut_after += cut_after < 64 ? 1 : 7) {
        memset(nor, 0xFF, sizeof nor);
        memcpy(nor, oldf, 0x93000);
        ops = 0;
        if (!setjmp(cut)) {
            ldr_session();
            continue;                                /* (the install ended before the cut) */
        }
        cuts++;
        cut_bad += !mark_fits();
        {
            uint32_t keep = cut_after;
            cut_after = 0;
            rc = ldr_session();
            resume_bad += rc != 0 || memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000) != 0;
            cut_after = keep;
        }
    }
    cut_after = 0;
    printf("  %u power cuts over %u flash operations\n", cuts, total_ops);
    bad += check("a power cut anywhere: a mark only over its own whole body", cuts && !cut_bad);
    bad += check("... and the next session finishes the install", cuts && !resume_bad);
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
