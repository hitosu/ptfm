#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# DJ's decks on the PC (fw/core/play/player.c's two replays, play/dj.c): both decks heard and mixed, a synced
# start and its phase, the loops and ROLLs, the stops, the LFOs, KEY, PREVIEW, the loops on tracks 16-31, the
# stand through the store; built with gcc's sanitizers, on dist/firmware/demo.ptfmdsk (tests/sound/dj_test.c).
set -e
cd "$(dirname "$0")/../.."
GEN=build/fw/dj/gen
OUT=build/fw/dj
mkdir -p "$OUT"
python3 -I fw/tools/gen.py "$GEN" >/dev/null
[ -f dist/firmware/demo.ptfmdsk ] || { echo "run_dj: no dist/firmware/demo.ptfmdsk (tools/pack.py makes it)"; exit 1; }
CORE="fw/core/play/player.c fw/core/play/dj.c fw/core/play/rec.c fw/core/play/ibxm/ibxm.c fw/core/fx/fx.c
      fw/core/eng/dx7/dx7.c fw/core/eng/juno/juno.c fw/core/eng/tb303/tb303.c fw/core/eng/samp/samp.c
      fw/core/eng/chip/chip.c fw/core/spare.c fw/tests/sound/disk_store.c $GEN/builtin.c"
CF="-std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-type-limits -I$GEN -Ifw/core -Ifw/core/play -Ifw/core/play/ibxm"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=undefined"
export ASAN_OPTIONS=detect_leaks=0
gcc $CF $SAN -o "$OUT/dj_test" fw/tests/sound/dj_test.c $CORE -lm
"$OUT/dj_test" dist/firmware/demo.ptfmdsk
