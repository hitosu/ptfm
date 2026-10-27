#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# Every host test of the firmware: each run_*.sh beside this file, in order. Stops at the first failure.
set -e
cd "$(dirname "$0")"
for t in run_*.sh; do
    [ "$t" = run_tests.sh ] && continue
    echo "== $t"
    sh "./$t"
done
echo "fw tests: ALL PASSED"
