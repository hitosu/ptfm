# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The CHIP engine's tables, from one source (specs/chip.md): the engine works in integers only, and the
playground's twin computes the same integers from the same tables, so both are written from here.

    python3 -I fw/tools/gen_chip.py GEN       GEN/chip_tables.h (fw/tools/gen_tables.py runs it)
    python3 -I fw/tools/gen_chip.py --js      writes playground/dsp-chip-tables.js and fw/core/ui/chip_names.c (the
                                              tables' and waves' names as the screens' texts)
    python3 -I fw/tools/gen_chip.py --check   both as --js would write them (fw/tests/run_chip.sh)

Times are in Q16 samples at 44,100 Hz (a step's length, a frame's, an envelope step's); levels Q15; the
band-limited kernel Q12 (its taps sum to 4096 at every phase, so a step integrates to exactly its size).
"""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
JS = os.path.join(ROOT, "playground", "dsp-chip-tables.js")
NAMES_C = os.path.join(ROOT, "fw", "core", "ui", "chip_names.c")

SR = 44100
KW, PH, KQ = 16, 64, 12                  # the kernel: taps, phases, scale (bits)
Q16 = 1 << 16

# the clocks: BRK, 8BIT, PSG 1.77, PSG 1.00, PSG 1.79, 16BIT (EXACT has none: 6)
CLOCK_NAMES = ["BRK", "8BIT", "PSG 1.77", "PSG 1.00", "PSG 1.79", "16BIT", "EXACT"]
PSG_HZ = [1773400, 1000000, 1789772, 2000000]

# what a step's rate is for each clock and law (pulse, triangle / wave, buzz of 32 steps, buzz of 16): the
# constant over the register, and the register's range
def law_c(clock, law):
    if clock == 0:
        return 1048576 if law == 0 else 2097152
    if clock == 1:
        return 894886.5 if law == 0 else 1789773
    clk = PSG_HZ[clock - 2]
    return clk / 2 if law == 0 else clk / 16 if law == 3 else clk / 8


def law_range(clock, law):
    if clock < 2:
        return 1, 2048
    return (1, 4095) if law == 0 else (1, 65535)


# the built-in tables, by number from 1 (0: none); "|" marks the loop (specs/chip.md#the-tables). Their numbers are
# fixed for good: a table added later goes at the end of its kind.
TABLES = {
    "vol": [("SETTLE 11", "15 14 13 12 12 11 | 11"), ("SETTLE 10", "15 13 12 11 10 10 | 10"),
            ("SOFT 10", "14 12 11 10 10 | 10"), ("SETTLE 12", "15 14 13 12 12 | 12"), ("SETTLE 13", "15 14 14 13 13 | 13"),
            ("ATTACK 14", "15 15 14 14 | 14"), ("ACCENT", "15 11 10 10 | 10"), ("TICK", "15 9 5 2 0"),
            ("TICK SOFT", "12 8 5 3 1 0"), ("SHORT", "15 13 0"), ("BLIP", "15 12 9 6 3 0"),
            ("PLUCK", "15 12 10 8 7 6 5 4 4 3 3 2 2 1 1 0"), ("PLUCK SUS", "15 12 10 9 8 | 8"),
            ("RING", "14 12 10 8 7 6 5 4 3 2 1 0"), ("FADE 13", "12 11 10 9 8 7 6 5 4 3 2 1 0"),
            ("SNARE", "15 13 11 9 7 6 5 4 3 2 2 1 1 0"), ("CLAP", "15 0 14 0 15 11 8 6 4 3 2 1 0"),
            ("SHAKE", "4 9 13 9 5 2 0"), ("GATE 7", "15 15 15 15 15 15 15 0"), ("THUD", "15 15 15 14 13 12 10 8 5 0"),
            ("CRASH", "15 14 14 13 13 12 12 11 11 10 10 9 9 8 8 7 7 6 6 5 5 4 4 3 3 2 2 1 1 0"),
            ("COIN", "15 15 15 15 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0"),
            ("BELL", "15 14 13 12 11 10 9 8 7 6 5 4 | 4"), ("SWELL", "3 5 7 9 11 13 14 15 | 15"),
            ("SWELL SLOW", "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 | 15"), ("PAD", "4 8 11 13 14 15 | 14 13 13 14 15 15"),
            ("REVERSE", "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0"), ("TREMOLO", "| 15 13 11 9 11 13"),
            ("TREM DEEP", "| 15 11 6 3 6 11"), ("GATE 8TH", "| 15 15 15 0 0 0"),
            ("STUTTER", "15 0 15 0 15 0 12 0 9 0 6 0 3 0"),
            ("ECHOES", "15 12 9 0 0 0 9 7 5 0 0 0 5 4 3 0 0 0 3 2 1 0"), ("DOUBLE", "15 10 0 0 15 10 6 3 1 0")],
    "arp": [("KICK DROP", "24 12 5 0 -5 -9 -12 -14 -16 -17"), ("KICK FAST", "12 5 0 -5 -8 -11 -13"),
            ("TOM DROP", "7 4 2 0 -2 -3 -4 -5"), ("SNAP", "7 3 0 -2"), ("OCT FLICK", "12 0"), ("MAJOR", "| 0 4 7"),
            ("MINOR", "| 0 3 7"), ("MAJOR 7", "| 0 4 7 11"), ("MINOR 7", "| 0 3 7 10"), ("SUS 4", "| 0 5 7"),
            ("DIMINISHED", "| 0 3 6"), ("POWER", "| 0 7 12"), ("FIFTH", "| 0 7"), ("OCTAVE", "| 0 12"),
            ("OCT DOWN", "| 0 -12"), ("TRILL", "| 0 2"), ("HALF TRILL", "| 0 1"), ("RUN UP", "0 4 7 12 16 19 24"),
            ("RUN DOWN", "24 19 16 12 7 4 0"), ("SCALE UP", "0 2 4 5 7 9 11 12"), ("RISE TO", "-12 -7 -4 -2 -1 0"),
            ("FALL OFF", "0 0 0 0 0 0 -1 -2 -4 -7 -12 -19 -24"), ("LASER", "24 21 18 15 12 9 6 3 0 -3 -6 -9 -12"),
            ("POWER UP", "0 4 7 12 4 7 12 16 7 12 16 19 12 16 19 24"), ("ZAP", "36 24 12 0"), ("UP 2 OCT", "| 24")],
    "pitch": [("SCOOP", "-80 -60 -40 -25 -15 -8 -4 -2 0"), ("SCOOP FAST", "-50 -25 -10 0"),
              ("FALL", "0 0 0 0 0 0 0 0 -5 -10 -20 -35 -55 -80 -110 -127"), ("DIVE", "0 -10 -25 -45 -70 -100 -127"),
              ("BEND UP", "0 5 10 20 30 45 60 80 100 127"), ("DETUNE UP", "| 8"), ("DETUNE DN", "| -8"),
              ("WOBBLE", "| 0 25 50 25 0 -25 -50 -25"), ("SIREN", "| 0 30 60 90 120 90 60 30"), ("SHIVER", "| 0 6 0 -6")],
    "duty": [("SWEEP", "| 2 2 2 2 1 1 1 1 0 0 0 0 1 1 1 1"), ("PWM", "| 0 1 2 3 2 1"), ("FLUTTER", "| 2 1"),
             ("OPEN", "0 1 2"), ("CLOSE", "2 1 0"), ("BUZZY", "| 0 2"), ("THIN OUT", "2 2 2 1 1 1 0"),
             ("PULSE 2", "| 1 1 0 0")],
    "noise": [("SNARE", "4 6 6 7 7 7 8"), ("RISE", "6 6 6 5 5 5 4 4 4 3 3 3 2 2 2 1 1 1 0"),
              ("FALL", "0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 10 11 12"), ("ZAP", "0 2 4 6 8 10 12 14"),
              ("THUMP", "10 12 14 15"), ("SWEEP", "| 2 3 4 5 6 7 6 5 4 3"), ("CLAP", "3 3 6 3 3 6 4 5 6"),
              ("STEAM", "| 1 2"), ("BREATH", "| 5 6 5 4")],
}
KINDS = ["vol", "arp", "pitch", "duty", "noise"]


def parse(s):
    vals, loop = [], -1
    for t in s.split():
        if t == "|":
            loop = len(vals)
        else:
            vals.append(int(t))
    return vals, loop


def jsround(x):
    """as JS's Math.round (halves up)"""
    return math.floor(x + 0.5)


def sn(x):
    return math.sin(2 * math.pi * x)


# the 16 waves: one period drawn from a formula over x = 0 .. 31/32, scaled to 0-15
WAVE_DEFS = [
    ("SAW", lambda x: 1 - x), ("SQUARE", lambda x: 1 if x < .5 else 0), ("SINE", sn),
    ("TRIANGLE", lambda x: x if x < .5 else 1 - x), ("FAT", lambda x: .65 * (1 - x) + .35 * sn(x)),
    ("ORGAN", lambda x: sn(x) + .5 * sn(2 * x) + .3 * sn(4 * x)), ("HOLLOW", lambda x: sn(x) + .7 * sn(3 * x)),
    ("BELL", lambda x: sn(x) + .6 * sn(5 * x) + .35 * sn(9 * x)), ("PULSE 25", lambda x: 1 if x < .25 else 0),
    ("PULSE 12", lambda x: 1 if x < .125 else 0), ("HALF SINE", lambda x: max(0, sn(x))),
    ("DOUBLE SAW", lambda x: (1 - x) + .5 * (1 - (2 * x) % 1)),
    ("VOX", lambda x: sn(x) + .8 * sn(2 * x) + .6 * sn(3 * x) + .2 * sn(5 * x)),
    ("CLAV", lambda x: .4 * sn(x) + (1 if x < .3 else -.2)), ("WARM", lambda x: sn(x) + .25 * sn(2 * x)),
    ("GRIT", lambda x: sn(x) + .5 * sn(7 * x) + .4 * sn(13 * x)),
]


def wave(f):
    v = [f(i / 32) for i in range(32)]
    lo, hi = min(v), max(v)
    return [jsround((x - lo) / (hi - lo) * 15) for x in v]


def kernel():
    """a windowed sinc a phase (PH of a sample), KW taps, cut at 0.45 of the rate, Blackman; its centre at tap 7 +
    the phase. Q12, each phase's taps summing to exactly 4096 (the rounding's rest on its largest tap)"""
    fc, half, out = .45, KW // 2, []
    for p in range(PH):
        fr, row = p / PH, []
        for k in range(KW):
            x = k - (half - 1) - fr
            s = 2 * fc if abs(x) < 1e-9 else math.sin(2 * math.pi * fc * x) / (math.pi * x)
            n = (k + 1 - fr) / (KW + 1)
            row.append(s * (.42 - .5 * math.cos(2 * math.pi * n) + .08 * math.cos(4 * math.pi * n)))
        tot = sum(row)
        q = [jsround(r / tot * (1 << KQ)) for r in row]
        q[max(range(KW), key=lambda k: q[k])] += (1 << KQ) - sum(q)
        out.append(q)
    return out


def curve(c, d, mx):
    """a level d of 0..mx (15 or 31) as amplitude, 0..1: BRK straight, 8BIT the console's mixer, PSG 3 dB a step,
    16BIT 1.5 dB a step of 32"""
    if d <= 0:
        return 0.0
    if c == 0:
        return d / mx
    if c == 1:
        x = d * 15 / mx
        return (95.88 / (8128 / x + 100)) / (95.88 / (8128 / 15 + 100))
    if c == 2:
        x = d * 15 / mx
        return 10 ** (-(15 - x) * 3 / 20)
    x = d if mx == 31 else 2 * d + 1
    return 10 ** (-(31 - x) * 1.5 / 20)


def hp(f):
    rc = 1 / (2 * math.pi * f)
    return jsround(32768 * rc / (rc + 1 / SR))


def data():
    d = {"KW": KW, "PH": PH, "KQ": KQ}
    d["KERNEL"] = [t for row in kernel() for t in row]
    d["CURVE"] = [jsround(32768 * curve(c, min(lv, mx), mx)) for c in range(4) for mx in (15, 31) for lv in range(32)]
    d["DUTY"] = [[0, 0, 0, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, 0, 0, 1], [1, 0, 0, 0, 0, 1, 1, 1], [0, 1, 1, 1, 1, 1, 1, 0],
                 [0, 1, 0, 0, 0, 0, 0, 0], [0, 1, 1, 0, 0, 0, 0, 0], [0, 1, 1, 1, 1, 0, 0, 0], [1, 0, 0, 1, 1, 1, 1, 1]]
    d["DUTY"] = [sum(b << s for s, b in enumerate(row)) for row in d["DUTY"]]   # a byte a width, bit s = step s
    d["TRI"] = [15 - i for i in range(16)] + list(range(16))
    d["WAVES"] = [v for _, f in WAVE_DEFS for v in wave(f)]
    d["WAVE_NAMES"] = [n for n, _ in WAVE_DEFS]
    kc, rc, lo, hi = [], [], [], []
    for clock in range(6):
        for law in range(4):
            c = law_c(clock, law)
            kc.append(jsround(SR * 2 ** 32 / c))
            rc.append(jsround(c * 4096 / SR))
            a, b = law_range(clock, law)
            lo.append(a)
            hi.append(b)
    d["KC"], d["RC"], d["RLO"], d["RHI"] = kc, rc, lo, hi
    f0 = 440 * 2 ** (-69 / 12)                                   # MIDI note 0
    d["LEN0"] = jsround(SR * Q16 / (f0 * 8))                      # a pulse step's length at note 0
    d["T12"] = [jsround(32768 * 2 ** (-s / 12)) for s in range(12)]
    d["T64"] = [jsround(32768 * 2 ** (-q / 768)) for q in range(64)]
    d["FRAME"] = [jsround(SR * Q16 / hz) for hz in (59.7275, 60.0988, 60, 50)]   # BRK, 8BIT, else, 50 Hz
    d["ENV_STEP"] = [jsround(s * SR * Q16 / 64) for s in range(8)] + [jsround((s + 1) * SR * Q16 / 240) for s in range(8)]
    d["SWEEP_PACE"] = [jsround(s * SR * Q16 / 128) for s in range(8)] + [jsround((s + 1) * SR * Q16 / 120) for s in range(8)]
    d["KE"] = [jsround(16 * SR * Q16 / hz) for hz in PSG_HZ] + [jsround(8 * SR * Q16 / hz) for hz in PSG_HZ]
    per_8bit = [4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068]   # the noise's periods
    per_psg = [1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 19, 22, 25, 28, 31]
    rates = [[524288] + [262144 / 2 ** (i - 1) for i in range(1, 16)], [1789773 / p for p in per_8bit]]
    rates += [[hz / (16 * p) for p in per_psg] for hz in PSG_HZ]
    d["NOISE_LEN"] = [jsround(SR * Q16 / r) for fam in rates for r in fam]
    d["HP"] = [hp(30), 0, hp(90), hp(440), hp(20), 0, hp(5), 0]  # BRK, 8BIT, PSG, NONE: two stages (0: none)
    d["LP_8BIT"] = jsround(32768 * (1 - math.exp(-2 * math.pi * 14000 / SR)))
    tv, tab, names = [], [], {}
    for k in KINDS:
        names[k] = [n for n, _ in TABLES[k]]
        for _, s in TABLES[k]:
            vals, loop = parse(s)
            tab.append([len(tv), len(vals), loop])
            tv += vals
    d["TV"], d["TAB"], d["TAB_NAMES"] = tv, tab, names
    d["TAB_FIRST"] = [sum(len(TABLES[x]) for x in KINDS[:i]) for i in range(len(KINDS))]
    d["TAB_COUNT"] = [len(TABLES[k]) for k in KINDS]
    return d


def arr(name, ctype, vals, per=12):
    out = ["static const %s %s[%d] = {" % (ctype, name, len(vals))]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def c_text(d):
    out = ["/* generated by fw/tools/gen_chip.py: do not edit (specs/chip.md) */",
           "#ifndef CHIP_TABLES_H", "#define CHIP_TABLES_H", "#include <stdint.h>",
           "#define CHIP_KW %d                      /* the band-limited kernel: taps, phases, Q */" % KW,
           "#define CHIP_PH %d" % PH, "#define CHIP_KQ %d" % KQ,
           "#define CHIP_LEN0 %du               /* a pulse step's length at MIDI note 0, Q16 samples */" % d["LEN0"],
           "#define CHIP_LP_8BIT %d                /* the console's 14 kHz low-pass, Q15 */" % d["LP_8BIT"],
           "#define CHIP_NTAB %d" % len(d["TAB"])]
    out += ["/* KERNEL[phase * KW + tap], Q12 */"] + arr("CHIP_KERNEL", "int16_t", d["KERNEL"], 16)
    out += ["/* CURVE[(curve * 2 + (max 31)) * 32 + level], Q15: BRK, 8BIT, PSG, 16BIT; of 16 levels and of 32 */"]
    out += arr("CHIP_CURVE", "uint16_t", d["CURVE"], 16)
    out += ["/* the pulse's widths, a byte each (bit s = step s): BRK's four, then 8BIT's */"]
    out += arr("CHIP_DUTY", "uint8_t", d["DUTY"], 8)
    out += arr("CHIP_TRI", "uint8_t", d["TRI"], 16)
    out += ["/* the 16 waves, 32 steps of 4 bits: %s */" % ", ".join(d["WAVE_NAMES"])]
    out += arr("CHIP_WAVES", "uint8_t", d["WAVES"], 16)
    out += ["/* each clock (BRK, 8BIT, PSG 1.77, PSG 1.00, PSG 1.79, 16BIT) x law (pulse, triangle / wave, buzz 32, buzz",
            " * 16): a step's length = (register x KC) >> 16 (Q16 samples); the register for a length = (length x RC +",
            " * 2^27) >> 28; the register's range */"]
    out += arr("CHIP_KC", "uint32_t", [str(v) + "u" for v in d["KC"]], 8)
    out += arr("CHIP_RC", "uint32_t", d["RC"], 8)
    out += arr("CHIP_RLO", "uint16_t", d["RLO"], 12)
    out += arr("CHIP_RHI", "uint16_t", d["RHI"], 12)
    out += ["/* 2^(-s/12) and 2^(-q/768), Q15: a pitch in 1/64 semitones as a step's length */"]
    out += arr("CHIP_T12", "uint16_t", d["T12"], 12) + arr("CHIP_T64", "uint16_t", d["T64"], 16)
    out += ["/* a frame (BRK 59.73 Hz, 8BIT 60.10, else 60, 50 Hz); the step envelope's steps (STEP 0-7: BRK and the",
            " * rest, then 8BIT); the sweep's (PACE 0-7, likewise); the PSG envelope's step a PERIOD unit (16 steps for each",
            " * PSG clock, then 32): Q16 samples */"]
    out += arr("CHIP_FRAME", "uint32_t", d["FRAME"], 8) + arr("CHIP_ENV_STEP", "uint32_t", d["ENV_STEP"], 8)
    out += arr("CHIP_SWEEP_PACE", "uint32_t", d["SWEEP_PACE"], 8) + arr("CHIP_KE", "uint32_t", d["KE"], 8)
    out += ["/* the noise's step at each RATE: BRK, 8BIT (EXACT too), the four PSG clocks; Q16 samples */"]
    out += arr("CHIP_NOISE_LEN", "uint32_t", d["NOISE_LEN"], 8)
    assert 65536 // min(d["NOISE_LEN"]) + 1 < 16, "a sample's noise steps past CHIP_RCP"
    out += ["/* 2^31 / k, up (k 1-15: the noise's steps in a sample): a ns / k as (a ns RCP[k]) >> 31, exact under 2^19 */"]
    out += arr("CHIP_RCP", "uint32_t", ["0u"] + [str(-(-(1 << 31) // k)) + "u" for k in range(1, 16)], 8)
    out += ["/* the output filters (BRK, 8BIT, PSG, NONE): two high-passes each (0: none), Q15 */"]
    out += arr("CHIP_HP", "uint16_t", d["HP"], 8)
    out += ["/* the tables (VOL, ARP, PITCH, DUTY, NOISE; specs/chip.md#the-tables): their values, then each one's",
            " * {first value, length, loop (-1: none)}, a kind's from TAB_FIRST[kind] */"]
    out += arr("CHIP_TV", "int8_t", d["TV"], 20)
    out += ["static const struct { uint16_t at; uint8_t len; int8_t loop; } CHIP_TAB[%d] = {" % len(d["TAB"])]
    out += ["    {%d, %d, %d}," % tuple(t) for t in d["TAB"]] + ["};"]
    out += arr("CHIP_TAB_FIRST", "uint8_t", d["TAB_FIRST"]) + arr("CHIP_TAB_COUNT", "uint8_t", d["TAB_COUNT"])
    out += ["#endif"]
    return "\n".join(out) + "\n"


def js_text(d):
    body = json.dumps(d, separators=(",", ":"))
    return ("/* SPDX-License-Identifier: GPL-3.0-only\n * Copyright (C) 2026 Vasily Kazantsev */\n"
            "/* generated by fw/tools/gen_chip.py: do not edit. The CHIP engine's tables (specs/chip.md), the same the\n"
            " * firmware's engine reads: for the worklet (E.CHIPT, dsp-chip.js) and the page (PTFM.chipTables). */\n"
            "window.PTFM = window.PTFM || {};\n"
            "PTFM.dsp = (PTFM.dsp || []).concat(function (E) {\n"
            "    E.CHIPT = " + body + ";\n"
            "});\n"
            "PTFM.chipTables = (() => { const E = {}; PTFM.dsp[PTFM.dsp.length - 1](E); return E.CHIPT; })();\n")


def names_text(d):
    """the tables' and waves' names for the screens: TX() texts (specs/ui/texts.md), a kind's in number order"""
    out = ["/* SPDX-License-Identifier: GPL-3.0-only", " * Copyright (C) 2026 Vasily Kazantsev */",
           "/* generated by fw/tools/gen_chip.py --js: do not edit. The CHIP engine's built-in tables' and waves' names,",
           " * as the INSTRUMENTS screens show them (ed_inst.h) */", '#include "ed_inst.h"', ""]
    for k in KINDS:
        nm = d["TAB_NAMES"][k]
        out.append("static const txt_t T_%s[%d] = {" % (k.upper(), len(nm)))
        out += ["    " + ", ".join('TX("%s")' % n for n in nm[i:i + 6]) + "," for i in range(0, len(nm), 6)]
        out.append("};")
    out.append("const txt_t *const CHIP_TAB_NAMES[5] = {%s};" % ", ".join("T_" + k.upper() for k in KINDS))
    out.append("const uint8_t CHIP_TAB_N[5] = {%s};" % ", ".join(str(len(d["TAB_NAMES"][k])) for k in KINDS))
    out.append("const txt_t CHIP_WAVE_NAMES[16] = {")
    w = d["WAVE_NAMES"]
    out += ["    " + ", ".join('TX("%s")' % n for n in w[i:i + 8]) + "," for i in range(0, len(w), 8)]
    out.append("};")
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    d = data()
    if sys.argv[1] == "--js":
        with open(JS, "w") as f:
            f.write(js_text(d))
        with open(NAMES_C, "w") as f:
            f.write(names_text(d))
    elif sys.argv[1] == "--check":
        with open(JS) as f:
            if f.read() != js_text(d):
                raise SystemExit("gen_chip: playground/dsp-chip-tables.js is not as fw/tools/gen_chip.py makes it (--js)")
        with open(NAMES_C) as f:
            if f.read() != names_text(d):
                raise SystemExit("gen_chip: fw/core/ui/chip_names.c is not as fw/tools/gen_chip.py makes it (--js)")
        print("gen_chip: the playground's CHIP tables as the firmware's")
    else:
        os.makedirs(sys.argv[1], exist_ok=True)
        with open(os.path.join(sys.argv[1], "chip_tables.h"), "w") as f:
            f.write(c_text(d))


if __name__ == "__main__":
    main()
