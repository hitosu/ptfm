#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The store and seeding (fw/core/store/, fw/web/seed.js) on the PC: the C tests on a fake flash with
# sanitizers, then dist/firmware/demo.ptfmdsk seeded through seed.js into the device's seeding code built natively.
set -e
cd "$(dirname "$0")"
OUT=../../build/fw/store
mkdir -p "$OUT"
CC="${CC:-gcc}"
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
STORE="../core/store/store.c ../core/store/seed.c ../core/store/crc.c ../core/spare.c store_plat.c store_builtin.c"

$CC $FLAGS -o "$OUT/store_test" store_test.c $STORE ../core/play/rec.c
"$OUT/store_test"
# again with an edit memory of 96 KB: pieces past 64 KB (their offsets kept in 4-byte units)
$CC $FLAGS -DSTORE_EDIT_MAX=98304u -o "$OUT/store_test_96k" store_test.c $STORE ../core/play/rec.c
"$OUT/store_test_96k"

$CC $FLAGS '-DPTFM_VERSION="TEST 1"' -o "$OUT/seed_bridge" seed_bridge.c $STORE
node seed_test.js "$OUT/seed_bridge" ../../dist/firmware/demo.ptfmdsk
