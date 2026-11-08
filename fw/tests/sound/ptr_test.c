/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PTR's decoder (eng/samp) against the packer's own decoding: every sample of a disk (a record of its own, its audio
 * after its 16-byte entry: specs/files/instruments.md#samples) decoded whole, then from places in the middle (as a
 * note's START or a loop's start would), each equal to DUMP/sample_NN.s8 (or .s16): tools/pack.py --dump. Each
 * sample's entry agrees with its audio's head (store_sample_ok).
 *   ptr_test DISK DUMP */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/eng/samp/samp.h"
#include "../../core/store/store.h"

int disk_load(const char *path);

int main(int argc, char **argv)
{
    int num, bad = 0, checked = 0;
    if (argc != 3 || disk_load(argv[1])) {
        fprintf(stderr, "ptr_test DISK DUMP\n");
        return 2;
    }
    for (num = 0; num < 256; num++) {
        uint32_t len;
        uint8_t ver;
        const uint8_t *p = store_get(REC_SAMPLE, (uint8_t)num, 0, &len, &ver), *rec;
        char path[512];
        FILE *f;
        int32_t n, i, wide;
        int16_t *ref, *out;
        ptr_dec_t d;
        if (!p)
            continue;
        if (ver != 1 || !store_sample_ok(p, len)) {
            printf("%02X: version %d, %u B: its entry and its audio's head differ\n", num, ver, len);
            bad++;
            continue;
        }
        rec = p + 16;                    /* (the audio: its index's offsets count from here) */
        len -= 16;
        wide = rec[0] == 2;
        n = rec[1] | rec[2] << 8 | rec[3] << 16;
        snprintf(path, sizeof path, "%s/sample_%02X.%s", argv[2], num, wide ? "s16" : "s8");
        f = fopen(path, "rb");
        if (!f) {
            printf("%02X: no %s\n", num, path);
            bad++;
            continue;
        }
        ref = malloc((size_t)n * 2u);
        out = malloc((size_t)n * 2u);
        for (i = 0; i < n; i++) {
            if (wide) {
                int16_t v;
                if (fread(&v, 2, 1, f) != 1)
                    v = 0;
                ref[i] = v;
            } else {
                int8_t v;
                if (fread(&v, 1, 1, f) != 1)
                    v = 0;
                ref[i] = (int16_t)(v * 256);
            }
        }
        fclose(f);
        memset(&d, 0, sizeof d);
        ptr_read(&d, rec, len, 0, n, out);
        if (memcmp(ref, out, (size_t)n * 2u)) {
            printf("%02X: decoded whole, differs\n", num);
            bad++;
        }
        for (i = 0; i < 8; i++) {        /* from places in the middle: a block's start, inside one, the end */
            int32_t at = (int32_t)((uint32_t)n * (uint32_t)i / 8u) + (i & 1) * 77, m = n - at < 300 ? n - at : 300;
            if (at >= n || m <= 0)
                continue;
            ptr_read(&d, rec, len, at, m, out);
            if (memcmp(ref + at, out, (size_t)m * 2u)) {
                printf("%02X: from %d, differs\n", num, at);
                bad++;
                break;
            }
        }
        checked++;
        free(ref);
        free(out);
    }
    printf("ptr: %d samples decoded, %d wrong\n", checked, bad);
    return bad || !checked;
}
