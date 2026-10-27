#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# INSTRUMENTS and the EDITOR on the PC (fw/tests/ui2_test.c): fw/core built natively with sanitizers and
# a fake platform (the sound: ui_player.c's stand-in), the demo disk laid into its flash; the playground's
# states shot (build/fw/shots2/, compared with build/playground/ by ui_shots.js), edits made through the
# controls, saved and read back; then ui2_check.py reads what was written with the packer's own code.
# Needs cc, python3 and node.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/ui2
GEN=$OUT/gen
OBJ=$OUT/obj
SHOTS=build/fw/shots2
mkdir -p "$OUT" "$OBJ" "$SHOTS" "$SHOTS/edited"
rm -f "$OBJ"/*.o
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_ui2.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
python3 -I fw/tools/gen.py "$GEN" > "$OUT/gen.log"
INC=$(find fw/core -type d | sed 's/^/-I/')
FLAGS="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
SRC="$(find fw/core -name '*.c' | grep -v -e '/play/player.c' -e '/play/ibxm/' -e '/eng/' -e '/fx/' | sort) fw/core/eng/samp/samp.c
     fw/core/eng/chip/chip.c"
for f in $SRC "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c fw/tests/ui_player.c fw/tests/ui2_test.c; do
    o="$OBJ/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
    if ! ${CC:-cc} $FLAGS $INC -I"$GEN" -c "$f" -o "$o" 2> "$o.log"; then
        echo "run_ui2.sh: left out, does not compile yet: $f"
        rm -f "$o"
    fi
done
rm -f "$OUT/core.a"
ar rcs "$OUT/core.a" $(ls "$OBJ"/*.o | grep -v -e ui2_test.o -e ui_player.o)
${CC:-cc} $FLAGS "$OBJ"/fw_tests_ui2_test.o "$OBJ"/fw_tests_ui_player.o "$OUT/core.a" -o "$OUT/ui2_test" -lm
export ASAN_OPTIONS=detect_leaks=0
rm -f "$SHOTS"/*.ppm "$SHOTS"/*.png "$SHOTS"/edited/*
"$OUT/ui2_test" dist/firmware/demo.ptfmdsk "$SHOTS"
# the texts packed as the firmware has them (specs/ui/texts.md): fw/core's copies with each text's number, the same
# test: every shot the same
rm -rf "$OUT/rw" "$OUT/packed"
python3 -I fw/tools/gen_texts.py "$GEN" --rewrite "$OUT/rw" > /dev/null
mkdir -p "$OUT/packed/edited"
${CC:-cc} $FLAGS -Werror=int-conversion -Werror=incompatible-pointer-types -DUI_TXT_PACKED -DUI_TXT_CHECK \
    -include fw/tests/txt_fmt.h -Werror=format \
    $(find "$OUT/rw" -type d | sed 's/^/-I/') -I"$GEN" $(echo $SRC | tr ' ' '\n' | sed "s#^fw/core/#$OUT/rw/#") \
    "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c fw/tests/ui_player.c fw/tests/ui2_test.c -o "$OUT/ui2_packed" -lm
"$OUT/ui2_packed" dist/firmware/demo.ptfmdsk "$OUT/packed" > /dev/null
d=0
for f in "$SHOTS"/*.ppm "$SHOTS"/edited/*.ppm; do
    [ -f "$f" ] || continue
    g="$OUT/packed/${f#"$SHOTS"/}"
    cmp -s "$f" "$g" || { echo "packed: ${f#"$SHOTS"/} differs"; d=$((d + 1)); }
done
[ $d = 0 ] || { echo "run_ui2.sh: the packed texts' screens differ in $d shots"; exit 1; }
echo "packed: every shot the same"
python3 -I fw/tests/ui2_check.py dist/firmware/demo.ptfmdsk "$SHOTS"
node fw/tests/ui_shots.js "$SHOTS" build/playground
