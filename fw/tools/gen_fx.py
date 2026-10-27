# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The effects' tables (fw/core/fx/fx.c), for fw/tools/gen_tables.py: c_lines() gives their C. The values
follow specs/ui/screens/fx.md's knobs and the playground's DSP (playground/dsp-fx.js), here in fixed point:

  FX_TANH_Q15[257]      tanh over 0..2 (DRIVE's soft clip): tanh(i / 128), Q15
  FX_G_Q12[256]         a TPT SVF's g = tan(pi f / 44.1 kHz), f = 20 Hz x 1000^(i / 255) (to 20 kHz), Q12
  FX_FILT_IX[128]       FILTER's CUT (its step 0-127: LP 64 .. OFF 64 .. HP 63) as an FX_G_Q12 index
  FX_RESO_K[101]        FILTER's RESO % as the SVF's k = 1 / q, q = 0.7 + 8 RESO, Q12
  FX_ENV_T10[2][128]    FILTER's envelope's times by step, 0.1 ms: ATTACK 2 ms x 1500^(i / 127), DECAY and RELEASE
                        5 ms x 1000^(i / 127), rounded (the knobs' values; the playground rounds the same)
  FX_ENV_AINC[128]      ATTACK's rise a block (64 frames), Q24 a full opening: the whole way in ATTACK's time
  FX_ENV_K[128]         DECAY's and RELEASE's curve a block, Q24: e^(-64 / tau), tau a quarter of the time
  FX_GATE_PAT[32][16]   GATE's patterns: 16 built in, then 16 of the user's (MY GATE 10 all on, the rest empty)
  FX_GATE_K[101]        GATE's SMOOTH % as its gain's one-pole coefficient a frame (0.5 ms + 20 ms SMOOTH), Q15
  FX_NV24[12]           DELAY's TIME list in 1/24 beats (1/32 .. 1/2, with the dotted and triplet ones)
  FX_DLY_TONE[101]      DELAY's TONE % as its repeats' one-pole coefficient at 29.4 kHz, Q15
  FX_CHO_INC[3][101]    CHORUS's LFO a frame (2^32 a cycle), by MODE and RATE %
  FX_CHO_DEP[3][101]    CHORUS's depth in frames, Q8, by MODE and DEPTH %
  FX_REV_DECAY[101]     the plate's decay by SIZE % (0.25 + 0.65 SIZE^1.5), Q15
  FX_REV_DD2[101]       its second diffusers' gain (decay + 0.15, 0.25 .. 0.5), Q15
  FX_REV_DAMP[101]      its damping lowpass by DAMP % (1 - (0.7 DAMP)^1.5), Q15
  FX_REV_LEN[12]        its lines' lengths at 29.4 kHz (Dattorro's at 29,761 Hz, scaled)
  FX_REV_TAP[14]        its output taps
  FX_DB_Q15[89]         dB -64 .. +24 as a gain, Q15 (the LIMITER's CEILING)
  FX_AA_*               the 2:3 converters' anti-alias biquad (a low-pass at 12.5 kHz, Q14)
  FX_SD_*               the punch-in buffer's sides read back: a low-pass at 3.5 kHz (Q14; B1, B2 = 2 B0, B0)
  FX_MET_*              the METRONOME's clicks: decays a frame (Q30), pitches (2^32 a cycle a frame)

The tables the effects' work reads are marked PLAT_C2_RO (core 2's room in RAM on the FM-1, fw/core/plat.h); the two
read only by core 1's code (FX_ENV_T10 the screens', FX_GATE_PAT copied at power-on) are not.
"""
import math

SR = 44100
PR = SR * 2 // 3                         # the REVERB's and the DELAY's rate: 29.4 kHz
PK = SR / PR                             # (the playground's: a step at 29.4 kHz keeps its meaning at 44.1)

GATE_PATTERNS = [
    "1111111111111111", "0101010101010101", "1011101110111011", "1100110011001100", "1011011010110110",
    "1101101101101101", "1000100010001000", "1111222211113333", "2020202020202020", "1001001001001001",
    "1001010010010100", "1110111011101110", "1010110111101111", "1111111100000000", "3030303030303030",
    "1100101011001010"] + ["1" * 16] + ["0" * 16] * 15
NV24 = [3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48]


def arr(name, ctype, vals, per=12, room=True):
    out = ["static const %s %s[%d]%s = {" % (ctype, name, len(vals), " PLAT_C2_RO" if room else "")]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def arr2(name, ctype, rows, per=12, room=True):
    out = ["static const %s %s[%d][%d]%s = {" % (ctype, name, len(rows), len(rows[0]), " PLAT_C2_RO" if room else "")]
    for r in rows:
        out.append("    {")
        for i in range(0, len(r), per):
            out.append("        " + ", ".join(str(v) for v in r[i:i + per]) + ",")
        out.append("    },")
    out.append("};")
    return out


def g_index(hz):
    return max(0, min(255, round(255 * math.log(hz / 20) / math.log(1000))))


def biquad_lp(fc, q=0.7071, fs=SR):
    w = 2 * math.pi * fc / fs
    a = math.sin(w) / (2 * q)
    b0 = (1 - math.cos(w)) / 2
    b1 = 1 - math.cos(w)
    a0, a1, a2 = 1 + a, -2 * math.cos(w), 1 - a
    return [round(v / a0 * 16384) for v in (b0, b1, b0, a1, a2)]


def ds(n):
    return int(math.floor(n * PR / 29761 + 0.5))


def env_t10():
    """FILTER's envelope's times by step, in 0.1 ms: ATTACK's, then DECAY's and RELEASE's"""
    return ([round(20 * 1500 ** (i / 127)) for i in range(128)], [round(50 * 1000 ** (i / 127)) for i in range(128)])


def c_lines():
    out = ["/* the effects' tables (fw/tools/gen_fx.py) */"]
    out += arr("FX_TANH_Q15", "int16_t", [int(32767 * math.tanh(i / 128)) for i in range(257)])
    g = [round(4096 * math.tan(math.pi * min(20 * 1000 ** (i / 255), 0.45 * SR) / SR)) for i in range(256)]
    out += arr("FX_G_Q12", "uint16_t", g)
    filt = []
    for step in range(128):
        c = step - 64
        if c < 0:
            hz = 20000 * (100 / 20000) ** (-c / 64)
        elif c > 0:
            hz = 20 * (8000 / 20) ** (c / 63)
        else:
            hz = 20000
        filt.append(g_index(hz))
    out += arr("FX_FILT_IX", "uint8_t", filt, 16)
    out += arr("FX_RESO_K", "uint16_t", [round(4096 / (0.7 + r / 100 * 8)) for r in range(101)])
    att, dec = env_t10()
    out += arr2("FX_ENV_T10", "uint16_t", [att, dec], room=False)
    out += arr("FX_ENV_AINC", "uint32_t", [round((1 << 24) * 64 / (t * SR / 10000)) for t in att], 8)
    out += arr("FX_ENV_K", "uint32_t", [round((1 << 24) * math.exp(-64 * 4 / (t * SR / 10000))) for t in dec], 8)
    out.append("static const uint8_t FX_GATE_PAT[32][16] = {")
    for s in GATE_PATTERNS:
        out.append("    {" + ", ".join(s) + "},")
    out.append("};")
    out += arr("FX_GATE_K", "uint16_t", [round(32768 * (1 - math.exp(-1 / ((0.0005 + s / 100 * 0.02) * SR))))
                                        for s in range(101)])
    out += arr("FX_NV24", "uint8_t", NV24)
    out += arr("FX_DLY_TONE", "uint16_t", [round(32768 * (1 - (1 - (0.1 + t / 100 * 0.85)) ** PK)) for t in range(101)])
    hz = [0.5, 0.83, 9]
    dep = [0.0019, 0.0029, 0.0006]
    out += arr2("FX_CHO_INC", "uint32_t", [[round(h * (0.3 + r / 100 * 1.4) / SR * 2 ** 32) for r in range(101)]
                                          for h in hz], 8)
    out += arr2("FX_CHO_DEP", "uint16_t", [[round(d * (0.3 + p / 100 * 1.4) * SR * 256) for p in range(101)]
                                          for d in dep])
    decay = [0.25 + 0.65 * (s / 100) ** 1.5 for s in range(101)]
    out += arr("FX_REV_DECAY", "uint16_t", [round(32768 * d) for d in decay])
    out += arr("FX_REV_DD2", "uint16_t", [round(32768 * min(0.5, max(0.25, d + 0.15))) for d in decay])
    out += arr("FX_REV_DAMP", "uint16_t", [round(32768 * (1 - (0.7 * d / 100) ** PK)) for d in range(101)])
    lens = [ds(n) for n in (142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163)]
    out.append("/* in1 in2 in3 in4, apL d1L ap2L d2L, apR d1R ap2R d2R */")
    out += arr("FX_REV_LEN", "uint16_t", lens)
    out.append("#define FX_REV_EXC %d   /* the modulated allpasses' excursion, frames at 29.4 kHz */" % ds(16))
    out.append("#define FX_REV_TOTAL %d   /* every line one after another */" % (sum(lens) + 2 * (ds(16) + 2)))
    out += arr("FX_REV_TAP", "uint16_t", [ds(n) for n in (266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228,
                                                          2673, 2111, 335, 121)], 14)
    out.append("#define FX_REV_BW %d   /* the input's bandwidth, Q15 */" % round(32768 * (1 - 0.3 ** PK)))
    out.append("#define FX_REV_LFO %d   /* the tank's LFO, Q30 (about 0.9 Hz) */" % round(2 ** 30 * 0.000128 * PK))
    out += arr("FX_DB_Q15", "int32_t", [round(32768 * 10 ** (d / 20)) for d in range(-64, 25)])
    out.append("#define FX_AA_B0 %d\n#define FX_AA_B1 %d\n#define FX_AA_B2 %d\n#define FX_AA_A1 %d\n#define FX_AA_A2 %d"
               % tuple(biquad_lp(12500)))
    sd = biquad_lp(3500)                 # (B1 and B2 taken as 2 B0 and B0: a low-pass's)
    out.append("#define FX_SD_B0 %d\n#define FX_SD_A1 %d\n#define FX_SD_A2 %d" % (sd[0], sd[3], sd[4]))
    out.append("/* the METRONOME's clicks: CLICK, BEEP, WOOD, COWBELL's decay a frame (Q30), their pitches */")
    out += arr("FX_MET_DECAY", "int32_t", [round(2 ** 30 * math.exp(-k / SR)) for k in (300, 60, 90, 40)])
    out += arr("FX_MET_HZ", "uint32_t", [round(f / SR * 2 ** 32) for f in (1000, 1500, 800, 1100, 540, 800)])
    out.append("/* WOOD's pitch falls: sin(2 pi f t (1 - 2 t)), its step a frame f - C n (2^32 a cycle) */")
    out += arr("FX_MET_WC", "uint32_t", [round(4 * f / SR * 2 ** 32 / SR) for f in (800, 1100)])
    out.append("#define FX_MET_LEN %d   /* a click's frames: 80 ms */" % round(0.08 * SR))
    return out
