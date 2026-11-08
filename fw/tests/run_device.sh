#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The device side's host tests (gcc, the sanitizers where the code is ours):
#   1. out_test: the analog out's last stage (MASTER, LINE GAIN, mono, the soft ceiling: fw/device/out.h);
#      libc_test: the device's memset and memcpy (a word at a time) against byte-by-byte references
#   2. enc_test: the knobs' decoder in hal/fm1_input.h, turns at speed
#   3. the firmware built (fw/build.sh, a dev build) when the JieLi toolchain is there (JIELI_TOOLCHAIN,
#      AC79_SDK as fw/build.sh takes them; PTFM_NO_BUILD=1 skips it); else the package tests below use a
#      package already built, or are skipped
#   4. ota_test: the M-UPGRADE entry (device/ota.c) takes the package and keeps PT/FM's loader staged; stub_test:
#      the start check (stub/stub_core.h) and the slot's mark; ldr_screen_test: the update loader's screens;
#      crash_screen_test: the red screens (device/crash_screen.c), every line in its box;
#      ldr_test: the update loader installs it over the debug build and back (with and without hashes, the mark's
#      block first and last, a power cut after every erase and page)
#   5. install_page.mjs: the install page against a simulated FM-1 (install, upload a disk; needs node), and
#      end to end the built page's demo disk into the device's own seeding code (after run_store.sh)
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/host
mkdir -p "$OUT"
CC="${CC:-gcc}"
SAN="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"

run() {                                  # run a test; on failure its whole output, else its last lines
    n=$1
    shift
    if out=$("$@" 2>&1); then
        printf '%s\n' "$out" | tail -n "$n"
    else
        printf '%s\n' "$out"
        echo "run_device: FAILED: $*"
        exit 1
    fi
}

$CC $SAN -o "$OUT/out_test" fw/tests/out_test.c -lm
run 12 "$OUT/out_test"
$CC $SAN -Wno-unused-function -o "$OUT/libc_test" fw/tests/libc_test.c
run 1 "$OUT/libc_test"
$CC -std=c11 $SAN -Wno-unused-function -Wno-unused-parameter -o "$OUT/enc_test" fw/tests/enc_test.c
run 2 "$OUT/enc_test"

TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
if [ -x "$TC/pi32v2/bin/clang" ] && [ "${PTFM_NO_BUILD:-0}" != 1 ]; then
    fw/build.sh > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "run_device: the firmware did not build"; exit 1; }
    grep -E "^  (ok|FAIL)|^(version|app|package|page) " "$OUT/build.log"
fi
PKG=build/fw/ptfm.fwsc
[ -f "$PKG" ] || PKG=dist/firmware/ptfm.fwsc
if [ ! -f "$PKG" ]; then
    echo "SKIP  the package's tests: no firmware built (fw/build.sh needs the JieLi toolchain)"
    exit 0
fi
echo "package: $PKG"
$CC -O1 -Wall -Wno-unused-function -Wno-maybe-uninitialized -o "$OUT/ota_test" fw/tests/ota_test.c
run 1 "$OUT/ota_test" "$PKG"
# the start check: its decisions, and the slot the dev build made (its mark, its body's CRC-32)
$CC $SAN -Wno-unused-function -Wno-unused-parameter -o "$OUT/stub_test" fw/tests/stub_test.c
run 1 "$OUT/stub_test" build/fw/app.bin
# the update loader's screens, drawn as the panel shows them (build/fw/host/ldr_screens/*.ppm to look at)
if [ -f build/fw/loader/ldr_font.h ]; then
    mkdir -p "$OUT/ldr_screens"
    $CC $SAN -Wno-unused-function -Ibuild/fw/loader -o "$OUT/ldr_screen_test" fw/tests/ldr_screen_test.c
    run 1 "$OUT/ldr_screen_test" "$OUT/ldr_screens"
fi
# the red screens, drawn as the panel shows them (build/fw/host/crash_screens/*.ppm, and debug/*.ppm), with a release's
# version (its line one row) and a debug build's, the longest (two rows): every line in its box
if [ -f build/fw/gen_dev/fonts.h ]; then
    mkdir -p "$OUT/crash_screens/debug"
    $CC -std=c11 $SAN -Wno-unused-function -DPTFM_BRINGUP -Ibuild/fw/gen_dev '-DPTFM_VERSION="0.27"' \
        '-DPTFM_BUILD="BAA5C210"' -o "$OUT/crash_screen_test" fw/tests/crash_screen_test.c
    run 1 "$OUT/crash_screen_test" "$OUT/crash_screens"
    $CC -std=c11 $SAN -Wno-unused-function -DPTFM_BRINGUP -Ibuild/fw/gen_dev '-DPTFM_VERSION="0.27 11-06 20:16 DEBUG"' \
        '-DPTFM_BUILD="C10E9263"' -o "$OUT/crash_screen_test_d" fw/tests/crash_screen_test.c
    run 1 "$OUT/crash_screen_test_d" "$OUT/crash_screens/debug"
fi
$CC -O1 -Wall -Wno-unused-function -o "$OUT/ldr_test" fw/tests/ldr_test.c
OTHER=dist/debug/ptfm-debug.fwsc                 # (another firmware to install over, and back: the debug build's)
if [ -f "$OTHER" ]; then
    run 1 "$OUT/ldr_test" "$OTHER" "$PKG"
    run 1 "$OUT/ldr_test" "$PKG" "$OTHER"
else
    echo "SKIP  ldr_test: no $OTHER to install over"
fi
if command -v node > /dev/null; then
    run 40 node fw/tests/install_page.mjs "$PKG"
else
    echo "SKIP  install_page.mjs: no node"
fi
echo "run_device: all passed"
