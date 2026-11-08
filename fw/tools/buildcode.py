#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""A firmware build's code: 8 hex digits on the crash screens (`BUILD 3F2A91C0`), so a report names the very
build, also when its version is a release's plain number. It holds the build's time to the minute (UTC) and its
kind, scrambled so it reads as no date, and a check that catches most misread digits.

  python3 -I fw/tools/buildcode.py CODE [CODE ...]    what each code says (the kind, the time built)

The word under the scramble: bits 0-23 the minutes since 2026-01-01 00:00 UTC (to 2057), 24-25 the kind
(KINDS), 26-31 the check; the scramble is a bijection on 32 bits (two odd multiplies between xor-shifts), so
every word has one code and back. build.py makes the code (SOURCE_DATE_EPOCH, when set, is the build's time: a
rebuild then gives the same image) and passes it to the device's code as PTFM_BUILD.
"""
import sys
import time

EPOCH = 1767225600                                  # 2026-01-01 00:00 UTC
KINDS = ("release", "debug", "dev", "bring-up")
_M = 0xFFFFFFFF
_K1, _K2 = 0x7FEB352D, 0x846CA68B


def _check(minutes, kind):
    return ((minutes * 0x9E3779B1 + kind * 0x85EBCA6B + 0x5BD1E995) & _M) >> 26


def _mix(x):
    x ^= x >> 16
    x = (x * _K1) & _M
    x ^= x >> 15
    x = (x * _K2) & _M
    return x ^ (x >> 16)


def _unmix(x):
    x ^= x >> 16
    x = (x * pow(_K2, -1, 1 << 32)) & _M
    x ^= (x >> 15) ^ (x >> 30)
    x = (x * pow(_K1, -1, 1 << 32)) & _M
    return x ^ (x >> 16)


def encode(t, kind):
    """the code of a build at t (seconds since 1970, UTC) of kind (an index into KINDS)"""
    minutes = (int(t) - EPOCH) // 60
    if not 0 <= minutes < 1 << 24 or not 0 <= kind < len(KINDS):
        raise ValueError(f"buildcode: no code for a build at {t} (kind {kind})")
    code = f"{_mix(minutes | kind << 24 | _check(minutes, kind) << 26):08X}"
    if decode(code) != (kind, EPOCH + minutes * 60):
        raise ValueError(f"buildcode: {code} does not read back")
    return code


def decode(code):
    """(kind, t) of a code (t at the minute, UTC), or None: not 8 hex digits, or its check fails"""
    s = code.strip().upper()
    if len(s) != 8 or any(c not in "0123456789ABCDEF" for c in s):
        return None
    w = _unmix(int(s, 16))
    minutes, kind = w & 0xFFFFFF, w >> 24 & 3
    if w >> 26 != _check(minutes, kind):
        return None
    return kind, EPOCH + minutes * 60


def main(argv):
    if not argv:
        print(__doc__.strip())
        return 2
    bad = 0
    for code in argv:
        d = decode(code)
        if d is None:
            print(f"{code}: not a PT/FM build's code (8 hex digits; one misread fails its check)")
            bad = 1
            continue
        kind, t = d
        print(f"{code.strip().upper()}: the {KINDS[kind]} build of {time.strftime('%Y-%m-%d %H:%M', time.gmtime(t))} UTC")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
