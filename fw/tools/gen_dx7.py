#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The DX7 engine's tables (fw/core/eng/dx7/), computed here once instead of at boot: the FM-1 has
no FPU, and MSFA (the engine inside Dexed, Apache-2.0) computes them with doubles in its init
functions. Each table follows its MSFA original step for step (sin.cc, exp2.cc, freqlut.cc,
lfo.cc, pitchenv.cc, dx7note.cc osc_freq), so the values are the same as the original code's.

  fw/tools/gen_dx7.py OUT.h      (fw/tools/gen_tables.py runs it)

Python floats are IEEE doubles and math.* calls the C library, as the C++ does; the one float
(32-bit) step of osc_freq is rounded the same way here (struct). 44.1 kHz, blocks of N = 64.
"""
import math
import struct
import sys

N = 64                                   # MSFA's block (LG_N 6)
FS = 44100.0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def trunc32(x):                          # C (int32_t) / (uint32_t) of a double: toward zero
    return int(x)


def sintab():                            # Sin::init, SIN_DELTA layout: [2i] delta, [2i + 1] value
    n = 1024
    dphase = 2 * math.pi / n
    c = int(math.floor(math.cos(dphase) * (1 << 30) + 0.5))
    s = int(math.floor(math.sin(dphase) * (1 << 30) + 0.5))
    r = 1 << 29
    u, v = 1 << 30, 0
    t = [0] * (2 * n)
    for i in range(n // 2):
        t[(i << 1) + 1] = (v + 32) >> 6
        t[((i + n // 2) << 1) + 1] = -((v + 32) >> 6)
        tt = (u * s + v * c + r) >> 30
        u = (u * c - v * s + r) >> 30
        v = tt
    for i in range(n - 1):
        t[i << 1] = t[(i << 1) + 3] - t[(i << 1) + 1]
    t[(n << 1) - 2] = -t[(n << 1) - 1]
    return t


def exp2tab():                           # Exp2::init
    n = 1024
    inc = 2.0 ** (1.0 / n)
    y = float(1 << 30)
    t = [0] * (2 * n)
    for i in range(n):
        t[(i << 1) + 1] = int(math.floor(y + 0.5))
        y *= inc
    for i in range(n - 1):
        t[i << 1] = t[(i << 1) + 3] - t[(i << 1) + 1]
    t[(n << 1) - 2] = ((1 << 31) - t[(n << 1) - 1]) & 0xFFFFFFFF
    return t


def tanhtab():                           # Tanh::init (4th order Runge-Kutta), for the Juno filter
    n = 1024
    step = 4.0 / n
    y = 0.0
    t = [0] * (2 * n)

    def d(x):
        return 1 - x * x
    for i in range(n):
        t[(i << 1) + 1] = int((1 << 24) * y + 0.5)
        k1 = d(y)
        k2 = d(y + 0.5 * step * k1)
        k3 = d(y + 0.5 * step * k2)
        k4 = d(y + step * k3)
        y += (step / 6) * (k1 + k4 + 2 * (k2 + k3))
    for i in range(n - 1):
        t[i << 1] = t[(i << 1) + 3] - t[(i << 1) + 1]
    t[(n << 1) - 2] = int((1 << 24) * y + 0.5) - t[(n << 1) - 1]
    return t


def freqlut():                           # Freqlut::init(44100)
    y = float(1 << 44) / FS
    inc = 2.0 ** (1.0 / 1024)
    t = []
    for _ in range(1025):
        t.append(int(math.floor(y + 0.5)))
        y *= inc
    return t


LFO_SOURCE = [
    0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302, 1.249609, 1.436782,
    1.560915, 1.752081, 1.875117, 2.062494, 2.247191, 2.374451, 2.560492, 2.686728, 2.873976, 2.998950,
    3.188013, 3.369840, 3.500175, 3.682224, 3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209,
    4.814636, 4.930480, 5.121901, 5.315191, 5.434783, 5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
    6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719, 7.556294, 7.687577, 7.877738,
    7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573, 8.986341, 9.122423, 9.300595, 9.500285,
    9.607994, 9.798158, 9.950249, 10.117361, 11.251125, 11.384335, 12.562814, 13.676149, 13.904338, 15.092062,
    16.366612, 16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250, 24.003841, 24.260068,
    25.746653, 27.173913, 27.578599, 29.052876, 30.693677, 31.191516, 32.658393, 34.317090, 34.674064, 36.416606,
    38.197097, 38.550501, 40.387722, 40.749796, 42.625746, 44.326241, 44.883303, 46.772685, 48.590865, 49.261084]


def lfo():                               # Lfo::init(44100) and Lfo::reset's delta_
    unit = trunc32(N * 25190424 / FS + 0.5)
    ratio = trunc32(4437500000.0 * N / FS)          # lforatio_ (uint32_t)
    return unit, [trunc32(x * ratio) for x in LFO_SOURCE]


def pitchenv_unit():                     # PitchEnv::init(44100)
    return trunc32(N * (1 << 24) / (21.3 * FS) + 0.5)


def finetab():                           # osc_freq: the FINE term, fine 0..99
    return [0] + [int(math.floor(24204406.323123 * math.log(1 + 0.01 * f) + 0.5)) for f in range(1, 100)]


def logfreq_detuned():                   # osc_freq, mode 0, standard tuning: logfreq after DETUNE
    base, step = 50857777, (1 << 24) // 12
    t = []
    for mn in range(128):
        lf = base + step * mn
        ratio = 0.0209 * math.exp(-0.396 * (f32(f32(float(lf)) / (1 << 24)))) / 7
        t.append([trunc32(float(lf) + ratio * lf * (d - 7)) for d in range(15)])
    return t


def ampmod():                            # Dx7Note::compute: (uint32_t)exp(sensamp / 262144 * 0.07 + 12.2)
    return [int(math.exp(float((i << 16) / 262144.0) * 0.07 + 12.2)) for i in range(258)]


def dd_encode(vals):
    """A smooth table as its first value, first difference and second differences (int16):
    rebuilt exactly at boot (dx7.c dx7_init), a quarter of the flash"""
    dd = [vals[i + 2] - 2 * vals[i + 1] + vals[i] for i in range(len(vals) - 2)]
    if not all(-32768 <= x < 32768 for x in dd):
        raise SystemExit("gen_dx7_tables: a second difference past int16")
    return vals[0], vals[1] - vals[0], dd


def dd_decode(v0, d0, dd):               # as dx7.c's (b + (b - a): no int32 overflow)
    out, a, b = [v0, v0 + d0], v0, v0 + d0
    for x in dd:
        c = b + (b - a) + x
        out.append(c)
        a, b = b, c
    return out


def interleave(values, last_delta):      # MSFA's SIN_DELTA layout from the values
    n = len(values)
    t = [0] * (2 * n)
    for i in range(n):
        t[2 * i + 1] = values[i]
    for i in range(n - 1):
        t[2 * i] = t[2 * i + 3] - t[2 * i + 1]
    t[2 * n - 2] = last_delta
    return t


def c_dd(name, vals):
    v0, d0, dd = dd_encode(vals)
    return "\n".join([f"#define {name}_V0 {v0}", f"#define {name}_D0 {d0}", c_array("int16_t", f"{name}_DD", dd, 16)])


def c_array(ctype, name, vals, per=8):
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    unit, deltas = lfo()
    det = logfreq_detuned()
    exp2 = [v - (1 << 32) if v >= 1 << 31 else v for v in exp2tab()]
    tanh, freq, sin = tanhtab(), freqlut(), sintab()
    n = 1024
    dphase = 2 * math.pi / n
    sin_c = int(math.floor(math.cos(dphase) * (1 << 30) + 0.5))
    sin_s = int(math.floor(math.sin(dphase) * (1 << 30) + 0.5))
    # the tables in flash as dx7_init rebuilds them; check that it gets every value back
    tanh_end = tanh[2 * n - 2] + tanh[2 * n - 1]
    if interleave(dd_decode(*dd_encode(exp2[1::2])), (1 << 31) - exp2[-1]) != exp2:
        raise SystemExit("gen_dx7_tables: exp2 does not come back")
    if interleave(dd_decode(*dd_encode(tanh[1::2])), tanh_end - tanh[-1]) != tanh:
        raise SystemExit("gen_dx7_tables: tanh does not come back")
    if dd_decode(*dd_encode(freq)) != freq:
        raise SystemExit("gen_dx7_tables: freqlut does not come back")
    out = ["/* generated by fw/tools/gen_dx7.py: MSFA's tables (Dexed, Apache-2.0), 44.1 kHz. The",
           " * four big ones as dx7_init rebuilds them (sin from Sin::init's own integer recurrence; exp2,",
           " * tanh, freqlut from second differences): 6.5 KB of flash, not 28.7 KB */",
           "#define DX7_LFO_UNIT %d" % unit,
           "#define DX7_PITCHENV_UNIT %d" % pitchenv_unit(),
           f"#define DX7_SIN_C {sin_c}   /* cos(2 pi / 1024) << 30 */",
           f"#define DX7_SIN_S {sin_s}",
           c_dd("DX7_EXP2", exp2[1::2]),
           c_dd("DX7_TANH", tanh[1::2]),
           f"#define DX7_TANH_END {tanh_end}   /* the value after the last: its delta */",
           c_dd("DX7_FREQ", freq),
           c_array("uint32_t", "DX7_LFO_DELTA", deltas),
           c_array("int32_t", "DX7_FINE", finetab(), 10),
           c_array("uint32_t", "DX7_AMPMOD", ampmod()),
           "static const int32_t DX7_LOGFREQ[128][15] = {"]
    for row in det:
        out.append("    {" + ", ".join(str(v) for v in row) + "},")
    out.append("};")
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
