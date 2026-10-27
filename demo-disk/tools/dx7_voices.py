# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Our own DX7 voices (their licence: OWN_LICENSE), written here from scratch for the demo disk: FM drums
and effects, chip tones, bells, pads, plucks, leads and basses. The DX7 cartridges take them by name
(dx7_carts.py); patches.py writes them.

  VOICES: 32 voices in the demo disk's DX7 form (README: Patch banks)

A voice is written as op(...) for each operator, OP1 first (an operator left out is silent), and dx(...)
around them:
  op(level, ratio=1.0 | hz=..., eg=(rates, levels), detune=0, rs=0, ams=0)
  dx(name, algorithm, ops, feedback=0, peg=(rates, levels), lfo=(speed, delay, pm, am, wave, pms), transpose=0)
A ratio is the DX7's COARSE x (1 + FINE / 100) (COARSE 0 is 0.5); a fixed frequency (hz) is 10^(COARSE mod 4
+ FINE / 100). The pitch EG's levels: 50 is the note, each step from it about 3/8 of a semitone near 50, 99
four octaves up; it starts at L4, goes to L1, L2, L3 while held and back to L4 after.

Drums sound right at C-4 (a drum track or the keys play them there).
"""
import math

ENV_ON = ((99, 99, 99, 99), (99, 99, 99, 0))
MOD = ((99, 99, 99, 30), (99, 99, 99, 0))   # a modulator that stays while its carrier rings out after the note
CHOKE_R4 = 82                                # 35 ms to -60 dB


def ratio_bytes(r):
    """a frequency ratio -> (COARSE, FINE), the closest the DX7 has"""
    best = None
    for coarse in range(32):
        base = 0.5 if coarse == 0 else coarse
        fine = round((r / base - 1) * 100)
        if 0 <= fine <= 99:
            err = abs(base * (1 + fine / 100) - r)
            if best is None or err < best[0]:
                best = (err, coarse, fine)
    if best is None or best[0] > 0.01 * r:
        raise SystemExit(f"dx7_voices: no ratio {r}")
    return best[1], best[2]


def hz_bytes(hz):
    """a fixed frequency -> (COARSE, FINE): 10^(COARSE + FINE / 100) Hz"""
    lg = math.log10(hz)
    coarse = min(3, int(lg))
    fine = round((lg - coarse) * 100)
    if not 0 <= fine <= 99:
        raise SystemExit(f"dx7_voices: no fixed frequency {hz} Hz")
    return coarse, fine


def op(level, ratio=1.0, hz=None, eg=ENV_ON, detune=0, rs=0, ams=0, vel=0, kbd=None):
    coarse, fine = hz_bytes(hz) if hz else ratio_bytes(ratio)
    bp, ld, rd, lc, rc = kbd or (39, 0, 0, "-lin", "-lin")
    return {"eg": {"rates": list(eg[0]), "levels": list(eg[1])}, "level": level,
            "mode": "fixed" if hz else "ratio", "coarse": coarse, "fine": fine, "detune": detune,
            "rate_scaling": rs, "amp_mod_sens": ams, "key_vel_sens": vel,
            "kbd": {"break_point": bp, "left_depth": ld, "right_depth": rd, "left_curve": lc, "right_curve": rc}}


WAVES = ("triangle", "saw down", "saw up", "square", "sine", "sample&hold")


def dx(name, algorithm, ops, feedback=0, peg=((99, 99, 99, 99), (50, 50, 50, 50)),
       lfo=(35, 0, 0, 0, "triangle", 0), transpose=0, sync=True):
    if len(name) > 10 or len(ops) > 6:
        raise SystemExit(f"dx7_voices: {name}")
    speed, delay, pm, am, wave, pms = lfo
    if wave not in WAVES:
        raise SystemExit(f"dx7_voices: {name}: the LFO wave {wave}")
    ops = list(ops) + [op(0) for _ in range(6 - len(ops))]
    return {"name": name.ljust(10), "algorithm": algorithm, "feedback": feedback, "osc_key_sync": sync,
            "transpose": transpose, "pitch_eg": {"rates": list(peg[0]), "levels": list(peg[1])},
            "lfo": {"speed": speed, "delay": delay, "pm_depth": pm, "am_depth": am, "key_sync": True,
                    "wave": wave, "pitch_mod_sens": pms},
            "ops": ops}


def decay(r2, l2=0, r1=99, r4=None, r3=99, l3=None):
    """an operator EG: up at once (or at r1), down to l2 at r2, held at l3 (l2), down at r4 (r2) after"""
    return ((r1, r2, r3, r2 if r4 is None else r4), (99, l2, l2 if l3 is None else l3, 0))


def pitch_drop(start, rate, end=50):
    """a pitch EG that starts at `start` and falls to the note at `rate` (L4 is the start)"""
    return ((rate, 99, 99, 0), (end, end, end, start))


VOICES = [
    # 00-07: the FM kit: kick, snare, clap, hat, open hat, tom, cowbell, crash
    dx("FM KICK", 5, [op(99, 1.0, eg=decay(52, r4=60)), op(72, 1.0, eg=decay(76)),
                      op(62, 7.0, eg=decay(88)), op(80, 3.0, eg=decay(90))],
       peg=pitch_drop(82, 99), transpose=-24),
    dx("FM SNARE", 5, [op(92, 1.0, eg=decay(68)), op(55, 1.41, eg=decay(78)),
                       op(85, 5.0, eg=decay(66)), op(99, 7.3, eg=decay(66)),
                       op(80, 9.0, eg=decay(62)), op(99, 13.0, eg=MOD)],
       feedback=7, peg=pitch_drop(58, 99), transpose=-5),
    dx("FM CLAP", 5, [op(0), op(0), op(88, hz=1300, eg=decay(64)), op(99, hz=2900, eg=MOD),
                      op(84, hz=1700, eg=decay(66)), op(99, hz=3700, eg=MOD)], feedback=7),
    dx("FM HAT", 5, [op(0), op(0), op(88, hz=5400, eg=decay(82)), op(99, hz=3300, eg=MOD),
                     op(92, hz=7600, eg=decay(84)), op(99, hz=2550, eg=MOD)], feedback=7),
    dx("FM HAT OPN", 5, [op(0), op(0), op(88, hz=5400, eg=decay(63)), op(99, hz=3300, eg=MOD),
                         op(92, hz=7600, eg=decay(64)), op(99, hz=2550, eg=MOD)], feedback=7),
    dx("FM TOM", 5, [op(99, 1.0, eg=decay(55)), op(50, 1.0, eg=decay(80))],
       peg=pitch_drop(62, 80), transpose=-12),
    dx("COWBELL", 5, [op(94, hz=540, eg=((99, 88, 58, 58), (99, 72, 0, 0))), op(62, hz=1080, eg=MOD),
                      op(92, hz=800, eg=((99, 88, 58, 58), (99, 72, 0, 0))), op(62, hz=1600, eg=MOD)]),
    dx("FM CRASH", 5, [op(0), op(0), op(90, hz=4800, eg=decay(50)), op(99, hz=3150, eg=MOD),
                       op(94, hz=6900, eg=decay(48)), op(99, hz=2200, eg=MOD)], feedback=7),
    # 08-0F: more percussion
    dx("FM RIDE", 5, [op(80, hz=3900, eg=decay(52)), op(70, hz=5900, eg=MOD),
                      op(84, hz=6400, eg=decay(54)), op(99, hz=2600, eg=MOD),
                      op(72, hz=9100, eg=decay(70)), op(99, hz=4100, eg=MOD)], feedback=6),
    dx("CLAVE", 5, [op(96, hz=2500, eg=decay(84))]),
    dx("RIMSHOT", 5, [op(94, hz=1700, eg=decay(86)), op(40, hz=2900, eg=decay(90)),
                      op(70, hz=480, eg=decay(86))]),
    dx("FM CONGA", 5, [op(99, 1.0, eg=decay(66)), op(58, 2.0, eg=decay(86))],
       peg=pitch_drop(60, 90), transpose=-5),
    dx("CHIP KICK", 1, [op(99, 1.0, eg=decay(72)), op(52, 2.0, eg=MOD)], peg=pitch_drop(90, 99)),
    dx("METAL HIT", 5, [op(94, 1.0, eg=decay(54)), op(78, 3.53, eg=decay(60)),
                        op(86, 2.76, eg=decay(52)), op(70, 7.13, eg=decay(58))]),
    dx("808 BOOM", 5, [op(99, 1.0, eg=decay(46, r4=56))], peg=pitch_drop(66, 99), transpose=-24),
    dx("TIMBALE", 5, [op(96, 1.0, eg=decay(64)), op(64, 1.5, eg=decay(76)), op(70, hz=3300, eg=decay(84))],
       peg=pitch_drop(58, 95)),
    # 10-15: effects
    dx("ZAP", 1, [op(99, 1.0, eg=decay(70)), op(70, 2.0, eg=decay(70))], peg=pitch_drop(99, 82)),
    dx("LASER", 1, [op(99, 1.0, eg=decay(62)), op(84, 1.0, eg=decay(66))], feedback=0,
       peg=pitch_drop(94, 70), lfo=(70, 0, 30, 0, "triangle", 3)),
    dx("IMPACT", 5, [op(99, 0.5, eg=decay(42, r4=50)), op(60, 0.5, eg=decay(60)),
                     op(0), op(0), op(90, 3.0, eg=decay(46)), op(99, 7.0, eg=MOD)],
       feedback=7, peg=pitch_drop(74, 72), transpose=-12),
    dx("SUB DROP", 1, [op(99, 1.0, eg=decay(40, r4=48))], peg=pitch_drop(82, 60), transpose=-12),
    dx("RISER", 2, [op(99, 1.0, eg=((45, 99, 99, 60), (99, 99, 99, 0))),
                    op(82, 1.0, eg=((40, 99, 99, 60), (99, 99, 99, 0)))],
       feedback=6, peg=((15, 99, 99, 99), (90, 90, 90, 50))),
    dx("SIREN", 1, [op(99, 1.0), op(66, 2.0)], peg=((99, 99, 99, 99), (50, 50, 50, 50)),
       lfo=(32, 0, 99, 0, "triangle", 6)),
    # 16-1F: tones
    dx("CHIP TRI", 1, [op(99, 1.0, eg=((99, 99, 99, 90), (99, 99, 99, 0))),
                       op(52, 2.0, eg=((99, 99, 99, 90), (99, 99, 99, 0)))]),
    dx("THEREMIN", 1, [op(99, 1.0, eg=((60, 99, 99, 64), (99, 99, 99, 0))), op(38, 1.0, eg=ENV_ON)],
       lfo=(40, 40, 10, 0, "sine", 4)),
    dx("SPACE BELL", 5, [op(99, 1.0, eg=decay(38, r4=40)), op(70, 3.5, eg=decay(46)),
                         op(86, 2.0, eg=decay(42, r4=42)), op(66, 5.19, eg=decay(50)),
                         op(76, 1.0, eg=((70, 50, 99, 50), (99, 0, 0, 0))), op(60, 1.0, eg=ENV_ON)],
       feedback=3),
    dx("CRYSTAL", 5, [op(99, 1.0, eg=((60, 30, 99, 44), (99, 80, 80, 0)), detune=3),
                      op(66, 4.0, eg=decay(40)), op(96, 1.0, eg=((60, 30, 99, 44), (99, 80, 80, 0)), detune=-3),
                      op(60, 7.0, eg=decay(46)), op(80, 2.0, eg=((50, 30, 99, 44), (99, 70, 70, 0))),
                      op(60, 1.0, eg=ENV_ON)], feedback=4, lfo=(30, 0, 0, 10, "triangle", 0)),
    dx("FM VOICES", 1, [op(99, 1.0, eg=((48, 99, 99, 50), (99, 99, 99, 0)), ams=1),
                        op(70, 1.0, eg=((52, 99, 99, 50), (99, 92, 92, 0))),
                        op(92, 1.0, eg=((48, 99, 99, 50), (99, 99, 99, 0)), detune=4, ams=1),
                        op(54, 3.0, eg=ENV_ON), op(50, 6.0, eg=ENV_ON), op(40, 1.0, eg=ENV_ON)],
       feedback=2, lfo=(34, 30, 8, 12, "triangle", 3)),
    dx("PROG PLUCK", 5, [op(99, 1.0, eg=decay(54, r4=60), detune=2), op(84, 1.0, eg=decay(66)),
                         op(96, 1.0, eg=decay(54, r4=60), detune=-2), op(78, 2.0, eg=decay(70)),
                         op(80, 2.0, eg=decay(58, r4=60)), op(70, 1.0, eg=decay(72))], feedback=5),
    dx("SYNC BASS", 1, [op(99, 1.0, eg=decay(46, l2=70, r4=72)), op(84, 1.0, eg=decay(60, l2=40)),
                        op(80, 0.5, eg=decay(50, l2=60, r4=72)), op(70, 1.0, eg=decay(64))],
       feedback=6, transpose=-12),
    dx("DIGI BASS", 5, [op(99, 0.5, eg=decay(48, l2=60, r4=74)), op(78, 1.0, eg=decay(66)),
                        op(90, 1.0, eg=decay(54, r4=74)), op(64, 3.0, eg=decay(76))], feedback=4),
    dx("GLASS ARP", 5, [op(99, 1.0, eg=decay(50, r4=58)), op(72, 6.0, eg=decay(60)),
                        op(90, 2.0, eg=decay(52, r4=58)), op(60, 9.0, eg=decay(64))]),
    # a Reese bass: two FM saws (a carrier and its modulator at ratio 1) detuned apart, a third an octave down
    dx("REESE", 5, [op(99, 1.0, eg=((99, 99, 99, 74), (99, 99, 99, 0)), detune=7), op(90, 1.0, eg=MOD),
                    op(97, 1.0, eg=((99, 99, 99, 74), (99, 99, 99, 0)), detune=-7), op(90, 1.0, eg=MOD),
                    op(86, 0.5, eg=((99, 99, 99, 74), (99, 99, 99, 0))), op(72, 0.5, eg=MOD)],
       feedback=6, transpose=-12),
]
# the drums (00-0F) choke: every operator's R4 fast, so the track's next note cuts the last at once
for _v in VOICES[:0x10]:
    for _o in _v["ops"]:
        _o["eg"]["rates"][3] = CHOKE_R4
