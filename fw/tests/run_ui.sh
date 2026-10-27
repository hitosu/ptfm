#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The UI (fw/core/ui) on the PC: fw/core built natively with sanitizers and a fake platform
# (ui_test.c), the demo disk (dist/firmware/demo.ptfmdsk) laid into its flash, a script of presses and turns;
# screenshots in build/fw/shots/ (PNG, 2x, to look at), each checked: the rows sent as they changed
# equal the frame sent whole. Then each shot against the playground's of the same name
# (build/playground/, made by tests/playground/screens.js and mirror_ui.js, the latter this test's steps on the
# playground), where there is one: which match, which not.
# The sound (the player, the engines, the effects) is left out: ui_player.c stands in for play/player.h.
# A source that does not compile yet (work under way) is left out and named; the link then says whether
# anything needed it. Needs cc and node.
set -e
SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"   # this script, wherever it was called from
cd "$(dirname "$0")/../.."
OUT=build/fw/ui
GEN=$OUT/gen
OBJ=$OUT/obj
SHOTS=build/fw/shots
mkdir -p "$OUT" "$OBJ" "$SHOTS"
exec 9> "$OUT/run.lock"                         # (one run at a time: they share these folders)
flock 9
rm -f "$OBJ"/*.o
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_ui.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
python3 -I fw/tools/gen.py "$GEN" > "$OUT/gen.log"
python3 -I fw/tools/gen_texts.py "$GEN" --check > /dev/null
INC=$(find fw/core -type d | sed 's/^/-I/')

# RAM: every fw/core/ui object's variables (.data + .bss), built for a 32-bit freestanding target as on
# the FM-1 (clang, i386), against specs/files/ram.md: the screen's strip (gfx.c's 57,600 B) apart, all of
# the UI within "the screens' and the player's state", of which the UI's share is 28 KB. A file that does
# not compile yet counts nothing (named above).
mkdir -p "$OUT/size"
rm -f "$OUT/size"/*.o
for f in fw/core/ui/*.c; do
    clang --target=i386-none-elf -ffreestanding -std=gnu11 -Os -w -Ifw/device/include $INC -I"$GEN" \
        -c "$f" -o "$OUT/size/$(basename "$f" .c).o" 2>/dev/null || echo "run_ui.sh: RAM not counted, does not compile: $f"
done
for o in "$OUT/size"/*.o; do
    size -A "$o" | awk -v f="$(basename "$o")" '$1 ~ /^\.(data|bss)/ {s += $2} END {printf "%7d %s\n", s, f}'
done | sort -n > "$OUT/size.txt"
RAM_OK=0
awk '{t += $1} $2 == "gfx.o" {t -= 57600} END {
    printf "RAM: the UI'"'"'s variables %d B (the strip apart) of 28672\n", t
    if (t > 28672) { print "RAM: OVER THE BUDGET (specs/files/ram.md): the biggest:"; exit 1 } }' "$OUT/size.txt" || \
    { tail -8 "$OUT/size.txt"; RAM_OK=1; }      # (the screens are still tested; the script fails at its end)

FLAGS="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
# REAL=1: with the sound, all of fw/core, instead of ui_player.c
if [ "${REAL:-0}" = 1 ]; then
    SRC=$(find fw/core -name '*.c' | sort)
    STAND_IN=
    FLAGS="$FLAGS -fno-sanitize=shift-base"
else
    SRC=$(find fw/core -name '*.c' | grep -v -e '/play/player.c' -e '/play/ibxm/' -e '/eng/' -e '/fx/' | sort)
    SRC="$SRC fw/core/eng/samp/samp.c"                 # (PTR's reader: INSTRUMENTS draws a sample's waveform)
    SRC="$SRC fw/core/eng/chip/chip.c"                 # (and CHIP's pictures: its tables, its tuning)
    STAND_IN=fw/tests/ui_player.c
fi
for f in $SRC "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c $STAND_IN fw/tests/ui_test.c; do
    o="$OBJ/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
    if ! ${CC:-cc} $FLAGS $INC -I"$GEN" -c "$f" -o "$o" 2> "$o.log"; then
        echo "run_ui.sh: left out, does not compile yet: $f"
        rm -f "$o"
    fi
done
# (an archive: only what the test needs is linked, so a file under way that nothing calls yet does no harm)
rm -f "$OUT/core.a"
ar rcs "$OUT/core.a" $(ls "$OBJ"/*.o | grep -v -e ui_test.o -e ui_player.o)
${CC:-cc} $FLAGS "$OBJ"/fw_tests_ui_test.o $(ls "$OBJ"/fw_tests_ui_player.o 2>/dev/null) "$OUT/core.a" -o "$OUT/ui_test" -lm
export ASAN_OPTIONS=detect_leaks=0
rm -f "$SHOTS"/*.ppm "$SHOTS"/*.png
"$OUT/ui_test" - "$SHOTS"                              # a new FM-1, nothing uploaded
"$OUT/ui_test" dist/firmware/demo.ptfmdsk "$SHOTS"
if [ "${REAL:-0}" = 0 ]; then                       # the texts packed as the firmware has them (specs/ui/texts.md):
    rm -rf "$OUT/rw" "$OUT/packed"                  # fw/core's copies with each text's number; every shot the same
    python3 -I fw/tools/gen_texts.py "$GEN" --rewrite "$OUT/rw" > /dev/null
    mkdir -p "$OUT/packed"
    ${CC:-cc} $FLAGS -Werror=int-conversion -Werror=incompatible-pointer-types -DUI_TXT_PACKED -DUI_TXT_CHECK \
        -include fw/tests/txt_fmt.h -Werror=format \
        $(find "$OUT/rw" -type d | sed 's/^/-I/') -I"$GEN" $(echo $SRC | tr ' ' '\n' | sed "s#^fw/core/#$OUT/rw/#") \
        "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c $STAND_IN fw/tests/ui_test.c -o "$OUT/ui_packed" -lm
    "$OUT/ui_packed" - "$OUT/packed" > /dev/null
    "$OUT/ui_packed" dist/firmware/demo.ptfmdsk "$OUT/packed" > /dev/null
    d=0
    for f in "$SHOTS"/*.ppm; do
        cmp -s "$f" "$OUT/packed/${f##*/}" || { echo "packed: ${f##*/} differs"; d=$((d + 1)); }
    done
    [ $d = 0 ] || { echo "run_ui.sh: the packed texts' screens differ in $d shots"; exit 1; }
    echo "packed: every shot the same ($(ls "$SHOTS"/*.ppm | wc -l))"
fi
node fw/tests/ui_shots.js "$SHOTS" build/playground
[ "$RAM_OK" = 0 ] || { echo "run_ui.sh: FAILED: the UI's RAM is over its budget (above)"; exit 1; }
# then the same with the real player (REAL=1 runs only that)
[ "${REAL:-0}" = 1 ] || { echo "== run_ui.sh again with the real player"; REAL=1 exec sh "$SELF"; }
