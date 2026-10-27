#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# LOOPS' loops (fw/core/play/loops.c) on the PC: loops_test.c against the store on a fake flash, with sanitizers; again
# with an edit memory of 96 KB (pieces past 64 KB). Then their lanes (fw/core/play/player.c): lanes_test.c, the loops
# written through loops.c and played by the player with the firmware's built-in instruments.
set -e
cd "$(dirname "$0")"
OUT=../../build/fw/loops
mkdir -p "$OUT"
CC="${CC:-gcc}"
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
SRC="../core/play/loops.c ../core/play/rec.c ../core/store/store.c ../core/store/seed.c ../core/store/crc.c ../core/spare.c
     store_plat.c"

$CC $FLAGS -o "$OUT/loops_test" loops_test.c $SRC store_builtin.c
"$OUT/loops_test"
$CC $FLAGS -DSTORE_EDIT_MAX=98304u -o "$OUT/loops_test_96k" loops_test.c $SRC store_builtin.c
"$OUT/loops_test_96k"

GEN=$OUT/gen
python3 -I ../tools/gen.py "$GEN" > /dev/null
# (negative values shifted left, as the engines do, are what both compilers make of them: two's complement)
$CC -std=c99 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-type-limits -Werror \
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
    -I../core -I../core/play/ibxm -I"$GEN" -o "$OUT/lanes_test" lanes_test.c $SRC "$GEN/builtin.c" \
    ../core/play/ibxm/ibxm.c ../core/fx/fx.c ../core/eng/dx7/dx7.c ../core/eng/juno/juno.c ../core/eng/tb303/tb303.c \
    ../core/eng/samp/samp.c ../core/eng/chip/chip.c -lm
ASAN_OPTIONS=detect_leaks=0 "$OUT/lanes_test"
