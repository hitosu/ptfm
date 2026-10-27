#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# USB audio (specs/connections.md#usb-audio) on the PC: the resampler's table as fw/tools/gen_uac.py makes it;
# the stream (fw/device/uac.h) with the FM-1's clock and a computer's side by side (uac_test.c); the USB
# descriptors as the firmware builds them (usb_desc_test.c); SysEx out when the host stops reading
# (usb_sx_test.c). Needs cc and python3.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/uac
mkdir -p "$OUT"
python3 -I fw/tools/gen_uac.py --check
${CC:-cc} -std=gnu11 -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
    fw/tests/uac_test.c -o "$OUT/uac_test" -lm
"$OUT/uac_test"
${CC:-cc} -std=gnu11 -O1 -w -Ifw/hal -Ifw/device fw/tests/usb_desc_test.c -o "$OUT/usb_desc_test"
"$OUT/usb_desc_test"
${CC:-cc} -std=gnu11 -O1 -w -Ifw/hal -Ifw/device fw/tests/usb_sx_test.c -o "$OUT/usb_sx_test"
"$OUT/usb_sx_test"
