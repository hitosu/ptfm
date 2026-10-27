#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# FX, VISUAL, LOOPS and DJ on the PC: all of fw/core (the real player, engines, effects) built with the
# sanitizers and a fake platform (ui3_test.c), the demo disk (dist/firmware/demo.ptfmdsk) laid into its flash, a script;
# what each screen does checked, its shots in build/fw/shots3/ (PNG, 2x) and compared with the playground's
# of the same name (build/playground/, made by tests/playground/screens.js). Needs cc and node.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/ui3
GEN=$OUT/gen
SHOTS=build/fw/shots3
mkdir -p "$OUT" "$SHOTS"
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_ui3.sh: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
python3 -I fw/tools/gen.py "$GEN" > "$OUT/gen.log"
SRC=$(find fw/core -name '*.c' | sort)
INC=$(find fw/core -type d | sed 's/^/-I/')
export ASAN_OPTIONS=detect_leaks=0
${CC:-cc} -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-type-limits \
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
    $INC -I"$GEN" $SRC "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c fw/tests/ui3_test.c -o "$OUT/ui3_test" -lm
rm -f "$SHOTS"/*.ppm "$SHOTS"/*.png
"$OUT/ui3_test" dist/firmware/demo.ptfmdsk "$SHOTS"
mkdir -p "$OUT/keys"
"$OUT/ui3_test" - "$OUT/keys"                           # (no disk: the FM-1 as a synth only)
# the same built as the firmware is (specs/ui/texts.md): fw/core's copies with each text's number (gen_texts.py
# --rewrite), the texts unpacked from the table as they are drawn: every check passes again and every shot is the
# same, pixel for pixel
rm -rf "$OUT/rw" "$OUT/packed"
python3 -I fw/tools/gen_texts.py "$GEN" --rewrite "$OUT/rw" > /dev/null
mkdir -p "$OUT/packed"
${CC:-cc} -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-type-limits \
    -Werror=int-conversion -Werror=incompatible-pointer-types -DUI_TXT_PACKED -DUI_TXT_CHECK \
    -include fw/tests/txt_fmt.h -Werror=format \
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
    $(find "$OUT/rw" -type d | sed 's/^/-I/') -I"$GEN" $(find "$OUT/rw" -name '*.c' | sort) "$GEN"/builtin.c "$GEN"/fonts.c \
    "$GEN"/texts.c fw/tests/ui3_test.c -o "$OUT/ui3_packed" -lm
"$OUT/ui3_packed" dist/firmware/demo.ptfmdsk "$OUT/packed" | tail -1 | sed 's/^/packed: /'
d=0
for f in "$SHOTS"/*.ppm; do
    cmp -s "$f" "$OUT/packed/${f##*/}" || { echo "packed: ${f##*/} differs"; d=$((d + 1)); }
done
[ $d = 0 ] || { echo "run_ui3.sh: the packed texts' screens differ in $d shots"; exit 1; }
echo "packed: every shot the same ($(ls "$SHOTS"/*.ppm | wc -l))"
# MIDI CONTROLLER, the PT/FM controller (fw/core/ctlmode.c, specs/controller.md): its start, both modes
${CC:-cc} -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -Ifw/core -Ifw/core/ui -I"$GEN" \
    fw/core/ctlmode.c fw/core/ui/gfx.c fw/core/ui/txt.c fw/core/ui/ui_fmt.c "$GEN"/fonts.c "$GEN"/texts.c \
    fw/tests/ctlmode_test.c -o "$OUT/ctlmode_test"
"$OUT/ctlmode_test" mirror "$SHOTS"
"$OUT/ctlmode_test" general "$SHOTS"
"$OUT/ctlmode_test" general "$SHOTS" fast          # (the platform's 1 kHz tick runs it, as on the FM-1)
node fw/tests/ui_shots.js "$SHOTS" build/playground
# the debug build (specs/debug.md): the same with PTFM_DEBUG, its figures asked for and answered, a whole bench
# run (THE CORE, ALONE); the page's side (fw/web/figures.js) on the answers and records caught
mkdir -p "$OUT/debug"
${CC:-cc} -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-type-limits \
    -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
    -DPTFM_DEBUG '-DPTFM_VERSION="TEST DEBUG"' \
    $INC -I"$GEN" $SRC "$GEN"/builtin.c "$GEN"/fonts.c "$GEN"/texts.c fw/tests/ui3_test.c -o "$OUT/ui3_debug" -lm
"$OUT/ui3_debug" dist/firmware/demo.ptfmdsk "$OUT/debug"
node fw/tests/figures_test.js "$OUT/debug/figures1.syx" "$OUT/debug/figures2.syx" "$OUT/debug/bench.syx" \
    "$OUT/debug/figures3.syx" "$OUT/debug/figures1c2.syx" "$OUT/debug/figures3c2.syx"
