/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the device's memset and memcpy (fw/device/libc.c), a word at a time where they can: every
 * length to 300 B from every alignment 0-7 of either side, against byte-by-byte references; the bytes
 * around never touched.
 *   cc -O2 -o libc_test fw/tests/libc_test.c && ./libc_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define memset dev_memset
#define memcpy dev_memcpy
#define memcmp dev_memcmp
#include "../device/libc.c"
#undef memset
#undef memcpy
#undef memcmp

int main(void)
{
    static unsigned char a[400], b[400], ref[400];
    int fails = 0, cases = 0;
    for (int da = 0; da < 8; da++)
        for (int sa = 0; sa < 8; sa++)
            for (int n = 0; n <= 300; n++) {
                for (int i = 0; i < 400; i++) { a[i] = (unsigned char)(i * 7 + 3); b[i] = (unsigned char)(i * 13 + 5); }
                memcpy(ref, a, sizeof ref);
                for (int i = 0; i < n; i++) ref[16 + da + i] = b[16 + sa + i];
                if (dev_memcpy(a + 16 + da, b + 16 + sa, (unsigned)n) != a + 16 + da || memcmp(a, ref, sizeof a)) fails++;
                cases++;
                if (sa) continue;                   /* (memset: the destination's alignment only) */
                for (int i = 0; i < 400; i++) a[i] = (unsigned char)(i * 7 + 3);
                memcpy(ref, a, sizeof ref);
                for (int i = 0; i < n; i++) ref[16 + da + i] = 0xA5;
                if (dev_memset(a + 16 + da, 0x1A5, (unsigned)n) != a + 16 + da || memcmp(a, ref, sizeof a)) fails++;
                cases++;
            }
    printf(fails ? "libc test: %d of %d cases FAILED\n" : "libc test: memset and memcpy, %d cases passed\n", fails ? fails : cases, cases);
    (void)dev_memcmp; (void)str_cpy; (void)fmt_int;
    return fails != 0;
}
