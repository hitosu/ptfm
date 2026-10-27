#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The CHIP engine (fw/core/eng/chip/, specs/chip.md) on the PC: built with sanitizers (and once more for signed
# overflows); the playground's tables as the firmware's (fw/tools/gen_chip.py --check); every instrument of the demo
# cartridge rendered by the firmware's engine and by the playground's twin (playground/dsp-chip.js), equal sample
# for sample; the reference sound at -20 dB RMS.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/chip
mkdir -p "$OUT/gen"
python3 -I fw/tools/gen_tables.py "$OUT/gen"
python3 -I fw/tools/gen_chip.py --check
FLAGS="-std=c11 -O2 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -Ifw/core -I$OUT/gen"
${CC:-cc} $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -o "$OUT/chip_test" fw/tests/chip_test.c fw/core/eng/chip/chip.c -lm
python3 -I fw/tests/chip_scenarios.py > "$OUT/scenarios.txt"
"$OUT/chip_test" < "$OUT/scenarios.txt" > "$OUT/c.txt"
node fw/tests/chip_twin.js "$OUT/scenarios.txt" "$OUT/c.txt"
"$OUT/chip_test" -t < "$OUT/scenarios.txt" > "$OUT/tune.txt"           # (the INSTRUMENTS screen's tuning, both ways)
node fw/tests/chip_twin.js "$OUT/scenarios.txt" "$OUT/tune.txt" --tuning
# the same from another compiler (undefined behaviour shows as a difference): clang as the device's and the browser's
if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -O2 -Wall -Wextra -Werror -Ifw/core -I"$OUT/gen" -o "$OUT/chip_clang" fw/tests/chip_test.c fw/core/eng/chip/chip.c -lm
    "$OUT/chip_clang" < "$OUT/scenarios.txt" | cmp - "$OUT/c.txt"
    echo "chip: clang's build the same"
fi
# the reference (the first scenario): its loudest 300 ms at -20 dB RMS of the 16-bit full scale (3,277), within 0.1 dB
# every instrument's LEVEL (C-4 held 1 s of 1.2 s, AUTO LEVEL's way) as demo-disk/patches/chip/chiptune.json has it
python3 -I fw/tests/chip_scenarios.py --levels > "$OUT/levels-scen.txt"
"$OUT/chip_test" -l < "$OUT/levels-scen.txt" | python3 -I fw/tests/chip_scenarios.py --check-levels
head -1 "$OUT/c.txt" | awk '{ db = 20 * log($4 / 32767) / log(10); printf "chip: the reference at %.2f dB RMS\n", db;
    if (db < -20.1 || db > -19.9) { print "chip: the reference is not at -20 dB: CHIP_OUT"; exit 1 } }'
echo "chip: ALL PASSED"
