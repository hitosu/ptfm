/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PTR's decoder (eng/samp) against the packer's own decoding: every sample of a disk decoded whole, then
 * from places in the middle (as a note's START or a loop's start would), each equal to DUMP/audio_CC_SS.s8
 * (or .s16): tools/pack.py --dump.
 *   ptr_test DISK DUMP */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/eng/samp/samp.h"
#include "../../core/store/store.h"

int disk_load(const char *path);

int main(int argc, char **argv)
{
    int cart, idx, bad = 0, checked = 0;
    if (argc != 3 || disk_load(argv[1])) {
        fprintf(stderr, "ptr_test DISK DUMP\n");
        return 2;
    }
    for (cart = 0; cart < 0xF0; cart++)
        for (idx = 0; idx < 64; idx++) {
            uint32_t len;
            uint8_t ver;
            const uint8_t *rec = store_get(REC_AUDIO, (uint8_t)cart, (uint8_t)idx, &len, &ver);
            char path[512];
            FILE *f;
            int32_t n, i, wide;
            int16_t *ref, *out;
            ptr_dec_t d;
            if (!rec)
                continue;
            wide = rec[0] == 2;
            n = rec[1] | rec[2] << 8 | rec[3] << 16;
            snprintf(path, sizeof path, "%s/audio_%02X_%02X.%s", argv[2], cart, idx, wide ? "s16" : "s8");
            f = fopen(path, "rb");
            if (!f) {
                printf("%02X:%02X: no %s\n", cart, idx, path);
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
                printf("%02X:%02X: decoded whole, differs\n", cart, idx);
                bad++;
            }
            for (i = 0; i < 8; i++) {    /* from places in the middle: a block's start, inside one, the end */
                int32_t at = (int32_t)((uint32_t)n * (uint32_t)i / 8u) + (i & 1) * 77, m = n - at < 300 ? n - at : 300;
                if (at >= n || m <= 0)
                    continue;
                ptr_read(&d, rec, len, at, m, out);
                if (memcmp(ref + at, out, (size_t)m * 2u)) {
                    printf("%02X:%02X: from %d, differs\n", cart, idx, at);
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
