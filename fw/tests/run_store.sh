#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The store and seeding (fw/core/store/, fw/web/seed.js) on the PC: the C tests on a fake flash with
# sanitizers, then dist/firmware/demo.ptfmdsk seeded through seed.js into the device's seeding code built natively,
# and the conversion after an install from the demo disk in the old form (tools/pack.py --old-form, pinned below).
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
# the old form as FM-1s hold it: pack.py --old-form gives the demo disk as an older PT/FM kept it, samples inside their
# samplers, so the conversion is tested on that form. Should the demo disk's sources change, this sum changes: check that
# only what was meant to change did, then pin the new sum.
OLD_SHA=354a8a5bac2af9b39da55c13277eb33cd1a47118dc23205148a0ab88e5b02dae
python3 -I ../../tools/pack.py --old-form --out "$OUT/demo-old.ptfmdsk"
python3 -I ../../tools/pack.py --low-samples --out "$OUT/demo-conv.ptfmdsk"
[ "$(sha256sum "$OUT/demo-old.ptfmdsk" | cut -d' ' -f1)" = "$OLD_SHA" ] ||
    { echo "run_store: pack.py --old-form no longer gives the old-form demo disk pinned here (sha256 $OLD_SHA)"; exit 1; }
node seed_test.js "$OUT/seed_bridge" ../../dist/firmware/demo.ptfmdsk "$OUT/demo-old.ptfmdsk" "$OUT/demo-conv.ptfmdsk"
