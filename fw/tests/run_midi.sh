#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# MIDI on the PC (specs/connections.md): the MIDI IN jack's parser (fw/device/midi_uart.c), then all of
# fw/core with a fake platform whose MIDI in and out the test drives (midi_test.c): notes as the keys, MIDI
# OUT, MIDI learn and its record, the clock in and out. Screenshots of SETTINGS' MIDI page and LEARN's box
# in build/fw/midi/shots/ (PNG, to look at). Needs cc and node.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/midi
GEN=$OUT/gen
OBJ=$OUT/obj
SHOTS=$OUT/shots
mkdir -p "$OUT" "$OBJ" "$SHOTS"
exec 9> "$OUT/run.lock"
flock 9
rm -f "$OBJ"/*.o "$SHOTS"/*
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_midi.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
FLAGS="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"

# the jack's parser
${CC:-cc} $FLAGS -Ifw/tests/midi_fake fw/tests/midi_uart_test.c -o "$OUT/midi_uart_test"
"$OUT/midi_uart_test"

# all of fw/core (the sound included), the fake platform
python3 -I fw/tools/gen.py "$GEN" > "$OUT/gen.log"
INC=$(find fw/core -type d | sed 's/^/-I/')
for f in $(find fw/core -name '*.c' | sort) "$GEN"/*.c fw/tests/midi_test.c; do
    o="$OBJ/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
    ${CC:-cc} $FLAGS -fno-sanitize=shift-base $INC -I"$GEN" -c "$f" -o "$o"
done
${CC:-cc} $FLAGS "$OBJ"/*.o -o "$OUT/midi_test" -lm
ASAN_OPTIONS=detect_leaks=0 "$OUT/midi_test" dist/firmware/demo.ptfmdsk "$SHOTS"
node fw/tests/web_png.js "$SHOTS"/*.ppm
echo "run_midi.sh: all passed"
