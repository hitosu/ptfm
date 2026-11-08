/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the start check (fw/stub/stub_core.h): its decision for every case, the loader head it takes (as the
 * firmware's install stages it, device/ota.c), the update record it writes (as the firmware's install writes it),
 * and, given a built slot, its mark: the start check would start that firmware, and not one block of it damaged.
 *   stub_test [APP.bin]   (APP.bin: the slot, build/fw/app.bin) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../stub/stub_core.h"

#define FW_ID "FM-1_7101"
#define FW_OTA_DRYRUN 0
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}
static uint8_t nor[0x100000];
static int ota_erase(uint32_t off) { memset(nor + off, 0xFF, 0x1000); return 0; }
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../device/ota.c"

static int check(const char *what, int ok) { printf("%-64s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

int main(int argc, char **argv)
{
    uint32_t t[256], why, i;
    int bad = 0, d;
    stub_crc_table(t);
    bad += check("CRC-32 is zlib's (\"123456789\" -> 0xCBF43926)",
                 stub_crc32(t, 0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
    /* the decision */
    bad += check("nothing wrong: the firmware", stub_decide(0, 1, 0, &why) == STUB_APP && !why);
    bad += check("... with a loader start counted: the firmware still", stub_decide(0, 0, 1, &why) == STUB_APP);
    bad += check("OCT- held: the loader kept", stub_decide(STUB_W_OCT, 1, 0, &why) == STUB_LOADER && why == STUB_W_OCT);
    bad += check("the firmware asked: the loader", stub_decide(STUB_W_REQ, 1, 0, &why) == STUB_LOADER);
    bad += check("no mark: the loader", stub_decide(STUB_W_MARK, 1, 0, &why) == STUB_LOADER);
    bad += check("the body not the mark's: the loader", stub_decide(STUB_W_BODY, 1, 0, &why) == STUB_LOADER);
    bad += check("no loader kept: the chip's own USB mode",
                 stub_decide(STUB_W_BODY, 0, 0, &why) == STUB_ROM && (why & STUB_W_NOLDR));
    bad += check("the loader started once already: the chip's own USB mode",
                 stub_decide(STUB_W_MARK, 1, 1, &why) == STUB_ROM && (why & STUB_W_AGAIN));
    /* a power-on, by the chip's reset reasons (the box set anew), as the firmware's boot guard reads them */
    bad += check("a power-on: P33's bit 0 alone", stub_power_on(0x01) && !stub_power_on(0x05) && !stub_power_on(0x41) &&
                 !stub_power_on(0x04) && !stub_power_on(0x40) && !stub_power_on(0));
    /* the loader's head as the firmware's install stages it (ota.c's ota_stage_head), and a damaged one */
    memset(nor, 0xFF, sizeof nor);
    ota_stage_head(0x2400);
    bad += check("the staged loader's head is taken", stub_loader_ok(nor + STUB_LOADER_AT, STUB_LOADER_AT));
    bad += check("... only where its head says it is", !stub_loader_ok(nor + STUB_LOADER_AT, 0x000FC000u));
    nor[STUB_LOADER_AT + 20] ^= 1;
    bad += check("... and not when damaged", !stub_loader_ok(nor + STUB_LOADER_AT, STUB_LOADER_AT));
    memset(nor, 0xFF, sizeof nor);
    bad += check("... nor an empty staging", !stub_loader_ok(nor + STUB_LOADER_AT, STUB_LOADER_AT));
    {                                                /* the flash's end: 12 KB for PT/FM's, a bigger one from 0x0FA000 */
        static const uint32_t places[STUB_NPLACES] = STUB_LOADER_PLACES;
        uint8_t h[32] = {0};
        uint32_t c;
        memcpy(h + 16, "LOADER.BIN", 10);
        h[12] = 0x41;
        h[4] = 0x00; h[5] = 0xC0; h[6] = 0x0F; h[8] = 0x00; h[9] = 0x28;       /* at 0x0FC000, 10 KB */
        c = stub_crc16(h + 2, 30); h[0] = (uint8_t)c; h[1] = (uint8_t)(c >> 8);
        bad += check("a loader kept at the flash's end (0x0FC000, 10 KB) is taken", stub_loader_ok(h, 0x000FC000u));
        h[9] = 0x40;                                                           /* 16 KB: past 0x0FF000 */
        c = stub_crc16(h + 2, 30); h[0] = (uint8_t)c; h[1] = (uint8_t)(c >> 8);
        bad += check("... not one that would run into 0x0FF000", !stub_loader_ok(h, 0x000FC000u));
        bad += check("the places: the flash's end first, the old staging last",
                     places[0] == 0x000FC000u && places[1] == 0x000FA000u && places[2] == STUB_LOADER_AT);
    }
    /* the update record: the start check's equals the firmware's install's (ota.c's ota_session) */
    {
        uint8_t p[112], q[112] = {0};
        stub_parm(p, OTA_AREA);
        /* the firmware's record as ota_session builds it */
        ota_wr16(q + 2, 0x5A0Du); ota_wr16(q + 4, 0x5A01u); ota_wr16(q + 6, 0x5441u);
        memcpy(q + 8, "ota-FM-1_015", 12);
        ota_wr32(q + 72, OTA_AREA);
        ota_wr16(q, ota_crc16(q + 2, 78, 0));
        bad += check("the update record: the firmware's install's, byte for byte", !memcmp(p, q, 112));
        bad += check("the staging the start check reads is the install's", STUB_LOADER_AT == OTA_AREA);
    }
    /* a built slot: its mark */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static uint8_t s[STUB_SLOT_LEN + 16];
        size_t n = f ? fread(s, 1, sizeof s, f) : 0;
        const uint8_t *m = s + STUB_SLOT_LEN - STUB_MARK_LEN, *body = s + STUB_LEN;
        if (f) fclose(f);
        bad += check("the slot is the package's length", n == STUB_SLOT_LEN);
        bad += check("its mark checks out", stub_mark_ok(t, m));
        bad += check("its body's CRC-32 is the mark's: the start check starts it",
                     stub_crc32(t, 0, body, STUB_BODY_LEN) == stub_rd32(m + 8));
        bad += check("its CRC16 is the same in every build (0x5054)", stub_crc16(s, STUB_SLOT_LEN) == 0x5054u);
        d = 0;
        for (i = 0; i < STUB_BODY_LEN; i += 4096) {   /* any block of the body damaged: refused */
            s[STUB_LEN + i] ^= 0x10;
            d += stub_crc32(t, 0, body, STUB_BODY_LEN) == stub_rd32(m + 8);
            s[STUB_LEN + i] ^= 0x10;
        }
        bad += check("a byte damaged in any block of the body: refused", !d);
        s[STUB_SLOT_LEN - STUB_MARK_LEN + 9] ^= 1;
        bad += check("a damaged mark: refused", !stub_mark_ok(t, m));
    } else {
        printf("(no slot given: its mark not checked)\n");
    }
    printf("%s\n", bad ? "START CHECK TEST FAILED" : "start check test passed");
    return bad != 0;
}
