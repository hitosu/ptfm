#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The emulator (specs/emulator.md, Testing): its WebAssembly against the same C built natively.
#   1. seed the demo disk (dist/firmware/demo.ptfmdsk) into a new emulated FM-1 through fw/web/seed.js, as the
#      page's DEMO DISK button does (web_seed.js): every block answered, the mount clean, the FM-1
#      holding exactly the disk; its flash is kept for 2;
#   2. one session (web_session.txt) on both builds from that flash: the same audio, screens and LEDs
#      at every shot. The native run's screens: build/fw/web/test/shot_<n>.png (2x) to look at.
# Needs clang + lld (wasm32), cc, node, python3.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/web/test
GEN=$OUT/gen
mkdir -p "$OUT"
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_web.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
python3 -I emulator/build.py --out "$OUT" --fw-end 50000 > "$OUT/build.log"
SRC=$(find fw/core -name '*.c' | sort)
INC=$(find fw/core -mindepth 2 -name '*.h' -exec dirname {} \; | sort -u | while read d; do printf ' -I%s' "$d"; done)
cc -O2 -std=gnu11 -Wall -Wno-unused-function -DWEB_NATIVE -DWEB_FW_END=0x50000u -Ifw/core $INC -I"$GEN" \
    emulator/plat_web.c $SRC $(ls "$GEN"/*.c 2>/dev/null) fw/tests/web_native.c -o "$OUT/web_native"
node fw/tests/web_seed.js "$OUT/ptfm.wasm" dist/firmware/demo.ptfmdsk "$OUT/flash.bin"
"$OUT/web_native" fw/tests/web_session.txt "$OUT/flash.bin" "$OUT/shot" > "$OUT/native.txt"
node fw/tests/web_test.js "$OUT/ptfm.wasm" fw/tests/web_session.txt "$OUT/flash.bin" > "$OUT/wasm.txt"
node fw/tests/web_png.js "$OUT"/shot_*.ppm
cat "$OUT/wasm.txt"
if cmp -s "$OUT/native.txt" "$OUT/wasm.txt"; then
    echo "the emulator's WebAssembly == the native build: the same audio, screens and LEDs"
else
    echo "DIFFERENT: native (<) and WebAssembly (>)"
    diff "$OUT/native.txt" "$OUT/wasm.txt"
    exit 1
fi
