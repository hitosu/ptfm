#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# MIDI CONTROLLER's protocol on the PC (fw/core/ctl.h, specs/controller.md), gcc with the sanitizers: ctl_test
# checks it against fw/tests/vectors.json, the table panel/midi.js is checked against too (tests/controller_page.js
# in tests/run_playground.sh), then both modes' sessions byte for byte. The controller on PT/FM's platform (its
# start, its screens, the fast tick) is ctlmode_test's, in run_ui3.sh.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/ctl
mkdir -p "$OUT"
python3 -I fw/tests/gen_vectors.py "$OUT/vectors.h"
${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -I"$OUT" fw/tests/ctl_test.c -o "$OUT/ctl_test"
"$OUT/ctl_test"
