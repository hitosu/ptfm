#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Vasily Kazantsev
# Build PT/FM's firmware (README.md):
#   ./build.sh               a dev build: ../build/fw/ptfm.fwsc and ../build/fw/install.html
#   ./build.sh --release     a production build: ../dist/firmware/ptfm.fwsc and install.html (a new stamp in VERSION)
#   ./build.sh --debug       the debug build: ../dist/debug/ptfm-debug.fwsc and install.html (../specs/debug.md)
#   ./build.sh --bringup     the device layer alone with a test app: ../build/fw/ptfm-bringup.fwsc
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout, V1.2.1 (default: ~/fw-AC79_AIoT_SDK)
#   JIELI_DOCKER     1 (default): the compiler in an offline container; 0: run it natively
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"

[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "${JIELI_DOCKER:-1}" = 1 ]; then
    docker image inspect debian:bookworm-slim >/dev/null 2>&1 || { echo "build.sh: docker pull debian:bookworm-slim"; exit 1; }
fi
exec "$PY" -I tools/build.py "$@"
