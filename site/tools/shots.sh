#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The landing's screens (specs/site.md): site/tools/shots.c compiled against fw/core as the manual's screenshots are
# (tools/build_manual.py), run on the demo disk, its pictures made PNGs in site/screens/. From anywhere:
#   sh site/tools/shots.sh
set -e
cd "$(dirname "$0")/../.."
W=build/site
rm -rf "$W/shots"
mkdir -p "$W/shots"
python3 -I fw/tools/gen.py "$W/gen" > "$W/gen.log"
INC=$(find fw/core -type d | sort | sed 's/^/-I/')
SRC=$(find fw/core -name '*.c' | sort)
# shellcheck disable=SC2086
"${CC:-cc}" -std=gnu11 -D_DEFAULT_SOURCE -DPTFM_VERSION="\"$(cat fw/VERSION)\"" -O1 -g -Wall -Wextra \
    -Wno-unused-parameter -Wno-unused-function -Wno-type-limits -Wno-missing-field-initializers \
    $INC -Ifw/tests -I"$W/gen" $SRC "$W/gen/builtin.c" "$W/gen/fonts.c" "$W/gen/texts.c" site/tools/shots.c \
    -o "$W/shots_bin" -lm
"$W/shots_bin" dist/firmware/demo.ptfmdsk "$W/shots"
python3 -I site/tools/images.py screens "$W/shots"
