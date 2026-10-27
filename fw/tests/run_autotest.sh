#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The debug page's autotests (fw/web/autotest.js, specs/debug.md#autotests) against the firmware built for the
# browser with the debug build's figures: the demo disk seeded (fw/tests/web_seed.js), then every scenario through
# MIRROR and the state, its measured times shortened (0.05), each one passing (fw/tests/autotest_test.js).
# Needs clang + lld (wasm32), node, python3.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/autotest
mkdir -p "$OUT"
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_autotest.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
python3 -I emulator/build.py --debug --out "$OUT" --fw-end 50000 > "$OUT/build.log"
node fw/tests/web_seed.js "$OUT/ptfm.wasm" dist/firmware/demo.ptfmdsk "$OUT/flash.bin" > "$OUT/seed.log"
# (--single-threaded: V8 without its background threads; with them node hung at its exit about 1 run in 13)
node --single-threaded fw/tests/autotest_test.js "$OUT/ptfm.wasm" "$OUT/flash.bin" 0.05
# again, as slow as the FM-1 (its passes run past 100 ms while a list or DJ redraws; here only the screen's
# transfers take time): the scenarios that turn, pick and hold by hand, every main-loop pass 100 ms
echo "== as slow as the FM-1: every pass 100 ms"
AT_PASS_MS=100 node --single-threaded fw/tests/autotest_test.js "$OUT/ptfm.wasm" "$OUT/flash.bin" 0.05 \
    "A BEAT" "THE EDITOR'S OWN PATTERN" "NEW SONG" "THE KEYS ON LOOPS" "LOOPS TO A PATTERN AND BACK" "RECORD LIVE" \
    > "$OUT/slow.log" || { cat "$OUT/slow.log"; exit 1; }
tail -1 "$OUT/slow.log"
# the state's answers lost on the way (as a busy computer drops the FM-1's messages now and then): six in a row, the
# FIGURES still coming; the scenario goes on and notes the silence, the answers numbered telling how many were lost
echo "== six state answers lost on the way"
AT_LOSE_STATES=20:6 node --single-threaded fw/tests/autotest_test.js "$OUT/ptfm.wasm" "$OUT/flash.bin" 0.05 \
    "THE EDITOR'S OWN PATTERN" > "$OUT/lose.log" || { cat "$OUT/lose.log"; exit 1; }
grep -q "THE STATE ANSWERED ONLY AFTER .* S: THE FM-1 SENT 6 ANSWERS THE COMPUTER NEVER GOT, 0 CAME LATE" "$OUT/lose.log" ||
    { cat "$OUT/lose.log"; echo "FAIL the lost answers: no note of the silence, or not 6 counted"; exit 1; }
grep "THE STATE ANSWERED" "$OUT/lose.log"
