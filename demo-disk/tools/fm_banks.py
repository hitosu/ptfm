# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The free DX7 banks and FM patch sets the DX7 cartridges take voices from, read from their files (data
only: nothing in them runs), each voice in the demo disk's DX7 form (../README.md: Patch banks).

  dxheader(FILE)    the DX header bank's engines/src/dx7_bank.h: 32 packed voices (kBank)     name -> voice
  fm6(REPO)         the FM6 bank's tools/gen_fm6_patches.py: its factory patches (PATCHES)    name -> voice
  dxbank(REPO)      the DX bank's tools/gen_dx7_bank.py: its factory voices (SYNTH)           name -> voice
  classix(FILE)     Idiomdrottning's classix, a bank of packed voices (.dx7)                  name -> voice
  ym2612(REPO)      ym2612-patches: Mega Drive patches, DefleMask .dmp (YM2612, 4 operators)  "family/name" -> voice
  x16(REPO)         the Commander X16 ROM's audio/fmpatches.s (YM2151, 4 operators)          title -> (number, voice)

The FM6 and DX banks' voices are Python lists in their generators. They are read without running them:
the list's expression is parsed, checked to hold only literals, arithmetic and calls of names, and then
evaluated against functions of our own (no builtins reachable) that build our form from their arguments.

The 4-operator sets become DX7 voices of 4 operators (the other two silent): each chip algorithm inside a
DX7 algorithm (ALG4), levels by dB (a chip's modulators MOD_BOOST levels up: their full swing is twice the
DX7's), the chips' rates mapped to the DX7 rate whose fall is nearest in time (the chips' envelope timing
against the DX7 engine's, measured: catalog.py's DX7_T60), and the chips' global LFO as a gentle, delayed
vibrato where a patch is sensitive to it.
"""
import ast
import math
import re
from pathlib import Path

from catalog import DX7_T60

CURVES = ["-lin", "-exp", "+exp", "+lin"]
WAVES = ["triangle", "saw down", "saw up", "square", "sine", "sample&hold"]
MODES = ["ratio", "fixed"]


# ---------------------------------------------------------------- packed voices (a 32-voice cartridge's)
# the bits each packed byte uses; the others the DX7 ignores: kept as "ignored_bits" {byte: bits}, so a voice
# packs back to the same bytes
OP_USED = {11: 0x0F, 12: 0x7F, 13: 0x1F, 15: 0x3F}
VOICE_USED = {110: 0x1F, 111: 0x0F, 116: 0x7F}


def _ignored(b, used, base=0):
    return {str(k): b[base + k] & ~m for k, m in used.items() if b[base + k] & ~m}


def packed_voice(b):
    """one 128-byte packed voice (OP6 first) -> a voice, exactly (values past 99 kept)"""
    ops = []
    for op in range(6):
        p = bytes(x & OP_USED.get(i, 0xFF) for i, x in enumerate(b[op * 17:op * 17 + 17]))
        ign = _ignored(b, OP_USED, op * 17)
        ops.append({
            "eg": {"rates": list(p[0:4]), "levels": list(p[4:8])}, "level": p[14], "mode": MODES[p[15] & 1],
            "coarse": p[15] >> 1, "fine": p[16], "detune": (p[12] >> 3) - 7, "rate_scaling": p[12] & 7,
            "amp_mod_sens": p[13] & 3, "key_vel_sens": p[13] >> 2,
            "kbd": {"break_point": p[8], "left_depth": p[9], "right_depth": p[10],
                    "left_curve": CURVES[p[11] & 3], "right_curve": CURVES[(p[11] >> 2) & 3]}})
        if ign:
            ops[-1]["ignored_bits"] = ign
    ops.reverse()                                        # OP1 first
    ign = _ignored(b, VOICE_USED)
    b = bytes(x & VOICE_USED.get(i, 0xFF) for i, x in enumerate(b))
    if (b[116] >> 1) & 7 > 5:
        raise SystemExit("fm_banks: a voice's LFO wave past S&H")
    v = {"name": bytes(c if 32 <= c < 127 else 32 for c in b[118:128]).decode("ascii"),
         "algorithm": b[110] + 1, "feedback": b[111] & 7, "osc_key_sync": bool(b[111] & 8), "transpose": b[117] - 24,
         "pitch_eg": {"rates": list(b[102:106]), "levels": list(b[106:110])},
         "lfo": {"speed": b[112], "delay": b[113], "pm_depth": b[114], "am_depth": b[115], "key_sync": bool(b[116] & 1),
                 "wave": WAVES[(b[116] >> 1) & 7], "pitch_mod_sens": b[116] >> 4},
         "ops": ops}
    if ign:
        v["ignored_bits"] = ign
    return v


def dxheader(path):
    """the DX header bank's dx7_bank.h: kBank[32][128], the packed voices"""
    text = Path(path).read_text()
    body = text[text.index("kBank[kBankSize][128] = {"):]
    body = re.sub(r"//[^\n]*", "", body[:body.index("};")])
    vals = [int(v) for v in re.findall(r"\b\d+\b", body.split("=", 1)[1])]
    if len(vals) != 32 * 128 or max(vals) > 127:
        raise SystemExit(f"fm_banks: {path}: {len(vals)} values, not 32 x 128 of 0..127")
    return named([packed_voice(bytes(vals[i:i + 128])) for i in range(0, len(vals), 128)], path)


def classix(path):
    """a file of packed voices, one after another"""
    b = Path(path).read_bytes()
    return named([packed_voice(b[i:i + 128]) for i in range(0, len(b) - 127, 128)], path)


def named(voices, where):
    out = {}
    for v in voices:
        name = v["name"].strip()
        if name in out:
            raise SystemExit(f"fm_banks: {where}: the voice {name!r} twice")
        out[name] = v
    return out


# ---------------------------------------------------------------- our form from operator settings
def op_dict(r, l, ol, coarse, fine, det, mode=0, kvs=0, ams=0, rs=0, bp=39, ld=0, rd=0, lc=0, rc=0):
    return {"eg": {"rates": list(r), "levels": list(l)}, "level": ol, "mode": MODES[1 if mode else 0],
            "coarse": coarse, "fine": fine, "detune": det, "rate_scaling": rs, "amp_mod_sens": ams,
            "key_vel_sens": kvs,
            "kbd": {"break_point": bp, "left_depth": ld, "right_depth": rd, "left_curve": CURVES[lc],
                    "right_curve": CURVES[rc]}}


def voice_dict(name, alg, ops, fb=0, oks=1, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfs=35, lfd=0, lpmd=0,
               lamd=0, lks=1, lfw=0, lpms=0, trnsp=24):
    """ops OP1 first"""
    name = re.sub(r"[^ -~]", " ", name)[:10].ljust(10)
    return {"name": name, "algorithm": alg, "feedback": fb, "osc_key_sync": bool(oks), "transpose": trnsp - 24,
            "pitch_eg": {"rates": list(pr), "levels": list(pl)},
            "lfo": {"speed": lfs, "delay": lfd, "pm_depth": lpmd, "am_depth": lamd, "key_sync": bool(lks),
                    "wave": WAVES[min(lfw, 5)], "pitch_mod_sens": lpms},
            "ops": list(ops)}


# ---------------------------------------------------------------- a generator's list, read without running it
SAFE = (ast.Expression, ast.Call, ast.Name, ast.Load, ast.Constant, ast.Tuple, ast.List, ast.Dict, ast.keyword,
        ast.Starred, ast.UnaryOp, ast.USub, ast.UAdd, ast.BinOp, ast.Mult, ast.Add, ast.Sub, ast.Div)


def literal_list(path, var, ns):
    tree = ast.parse(Path(path).read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == var for t in node.targets):
            expr = ast.Expression(node.value)
            for n in ast.walk(expr):
                if not isinstance(n, SAFE):
                    raise SystemExit(f"fm_banks: {path}: {var} holds a {type(n).__name__}")
                if isinstance(n, ast.Name) and n.id not in ns:
                    raise SystemExit(f"fm_banks: {path}: {var} names {n.id}")
            # safe: every node was checked above (no attribute, subscript, lambda or comprehension; names only
            # our own functions), and no builtins are reachable
            return eval(compile(expr, str(path), "eval"), {"__builtins__": {}}, ns)
    raise SystemExit(f"fm_banks: {path}: no {var}")


def fm6(repo):
    def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=0, fc=1, ff=0, det=7, mode=0, kvs=0, ams=0, rs=0,
           bp=39, ld=0, rd=0, lc=0, rc=0):
        return op_dict(r, l, ol, fc, ff, det - 7, mode, kvs, ams, rs, bp, ld, rd, lc, rc)

    def voice(name, alg, ops, fb=0, oks=1, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfs=35, lfd=0, lpmd=0,
              lamd=0, lks=1, lfw=0, lpms=3, trnsp=24):
        return voice_dict(name, alg, ops, fb, oks, pr, pl, lfs, lfd, lpmd, lamd, lks, lfw, lpms, trnsp)

    path = Path(repo) / FM6_FILE
    return named([v for _, v in literal_list(path, "PATCHES", {"op": op, "voice": voice})], path)


def dxbank(repo):
    def Op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), out=0, ratio=1.0, hz=None, det=7, vel=0, rs=0, ams=0, bp=39,
           ld=0, rd=0, lc=0, rc=0, noise=False):
        if noise:
            raise ValueError("a noise operator")
        if hz is not None:
            lg = math.log10(max(1.0, hz))
            c = min(3, int(math.floor(lg)))
            f = int(round((lg - c) * 100))
            if f > 99:
                c, f = (c + 1, 0) if c < 3 else (3, 99)
            fixed = 1
        else:
            c = 0 if ratio < 1 else int(math.floor(ratio))
            base = 0.5 if c == 0 else c
            fixed, f = 0, max(0, min(99, int(round((ratio / base - 1) * 100))))
        return op_dict(r, l, out, c, f, det - 7, fixed, vel, ams, rs, bp, ld, rd, lc, rc)

    def voice(name, alg, ops, fb=0, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfo=(35, 0, 0, 0, 1, 0, 0), trans=24):
        lst = [ops.get(n) or Op() for n in range(1, 7)]
        s, d, pmd, amd, ks, w, pms = lfo
        return voice_dict(name, alg, lst, fb, 1, pr, pl, s, d, pmd, amd, ks, w, pms, trans)

    def D(r1, r2, r3, r4, l1=99, l2=80, l3=0, l4=0):
        return ((r1, r2, r3, r4), (l1, l2, l3, l4))

    def env(r, l):
        return {"r": r, "l": l}

    path = Path(repo) / DXBANK_FILE
    lst = literal_list(path, "SYNTH", {"Op": Op, "voice": voice, "D": D, "env": env})
    return named([v for v in lst if v["name"].strip() != "INIT VOICE"], path)


FM6_FILE = "tools/gen_fm6_patches.py"
DXBANK_FILE = "tools/gen_dx7_bank.py"
X16_FILE = "audio/fmpatches.s"


# ---------------------------------------------------------------- 4-operator FM (OPN2, OPM) -> DX7
# the 4-operator algorithms (S1 has the feedback) inside DX7 algorithms: (DX7 algorithm, the DX7 operators
# of S1..S4)
ALG4 = {0: (1, (6, 5, 4, 3)), 1: (14, (6, 5, 4, 3)), 2: (8, (4, 6, 5, 3)), 3: (7, (6, 5, 4, 3)),
        4: (5, (6, 5, 2, 1)), 5: (22, (6, 5, 4, 3)), 6: (29, (6, 5, 2, 1)), 7: (32, (6, 1, 2, 3))}
CARRIERS4 = {0: (4,), 1: (4,), 2: (4,), 3: (4,), 4: (2, 4), 5: (2, 3, 4), 6: (2, 3, 4), 7: (1, 2, 3, 4)}
MOD_BOOST = 8          # a 4-op chip's full modulator swings ~4 cycles, the DX7's ~2: modulators 6 dB up
REG_ORDER = (0, 2, 1, 3)        # a chip's register order S1, S3, S2, S4 -> S1..S4


def opn_t96(rate, eg_hz):
    """seconds for an OPN/OPM envelope to fall 96 dB at effective rate 0-63"""
    if rate <= 0:
        return 1e9
    if rate >= 60:
        return 128 / eg_hz
    k = rate % 4
    return 1024 / ((4 + k) / 8) * 2.0 ** (11 - rate // 4) / eg_hz


def dx_rate(db_per_s):
    """the DX7 rate whose fall is nearest (dB a second, against DX7_T60's 60 dB)"""
    if db_per_s <= 60 / (DX7_T60[0] / 1000) * 1.01:
        return 0
    return min(range(100), key=lambda r: abs(math.log((60 / (DX7_T60[r] / 1000)) / db_per_s)))


def eff(r2, ks, note=60):
    """an OPN rate field (0-31) -> its effective rate at a note (key scaling: the key code's top bits)"""
    if r2 <= 0:
        return 0
    kc = max(0, min(31, (note - 12) * 32 // 96))
    return min(63, 2 * r2 + (kc >> (3 - ks)))


def ratio_bytes(r):
    """a frequency ratio -> (COARSE, FINE), the nearest the DX7 has"""
    best = None
    for coarse in range(32):
        base = 0.5 if coarse == 0 else coarse
        fine = round((r / base - 1) * 100)
        if 0 <= fine <= 99:
            err = abs(math.log(base * (1 + fine / 100) / r))
            if best is None or err < best[0]:
                best = (err, coarse, fine)
    return best[1], best[2]


def opn_voice(name, alg, fb, ops, pms=0, ams=0, eg_hz=53267 / 3):
    """ops S1..S4, each a dict: mul (0 = .5), cents (OPM's DT2), dt (-3..3), tl 0-127, ar, d1r, d2r (0-31),
    rr (0-15), sl (0-15), ks (0-3), am (bool)"""
    dalg, where = ALG4[alg]
    out = [op_dict((99, 99, 99, 99), (99, 99, 99, 0), 0, 1, 0, 0) for _ in range(6)]
    for i, o in enumerate(ops):
        s = i + 1
        car = s in CARRIERS4[alg]
        tl = o["tl"]
        ol = max(0, min(99, 99 - tl if car else 99 - tl + MOD_BOOST))
        if tl >= 127:
            ol = 0
        ratio = (0.5 if o["mul"] == 0 else o["mul"]) * 2 ** (o.get("cents", 0) / 1200)
        coarse, fine = ratio_bytes(ratio)
        ar, d1, d2 = eff(o["ar"], o["ks"]), eff(o["d1r"], o["ks"]), eff(o["d2r"], o["ks"])
        rr = min(63, 4 * o["rr"] + 2 + (max(0, min(31, 48 * 32 // 96)) >> (3 - o["ks"])))
        sl_db = 93.0 if o["sl"] >= 15 else 3.0 * o["sl"]
        r1 = 99 if ar >= 62 else dx_rate(96 / max(opn_t96(ar, eg_hz) / 9, 1e-4))
        r2 = 99 if d1 >= 63 and sl_db > 0 else dx_rate(96 / opn_t96(d1, eg_hz)) if d1 else 0
        r3 = dx_rate(96 / opn_t96(d2, eg_hz)) if d2 else 99
        r4 = dx_rate(96 / opn_t96(rr, eg_hz))
        l2 = max(0, int(round(99 - sl_db / 0.75)))
        if d1 == 0:
            l2, r2 = 99, 99                              # no first decay: it stays at the top
        l3 = 0 if d2 else l2
        out[where[i] - 1] = op_dict((r1, r2, r3, r4), (99, l2, l3, 0), ol, coarse, fine,
                                    max(-7, min(7, 2 * o["dt"])), 0, 0, (ams if o["am"] else 0), 2 * o["ks"])
    # the chips' LFO depth is global (the player sets it), so a patch's sensitivity only says it may have
    # vibrato or tremolo: a gentle one, after a delay
    lpms, lpmd, lfd = 0, 0, 0
    if pms:
        lpms, lpmd, lfd = (3 if pms <= 3 else 4), 6 + pms, 30
    lamd = 10 if ams and any(o["am"] for o in ops) else 0
    return voice_dict(name, dalg, out, fb, 1, lfs=34, lfd=lfd, lpmd=lpmd, lamd=lamd, lpms=lpms)


def ym2612(repo):
    """DefleMask .dmp files (version 11, the YM2612 system): "family/name" -> voice. The file stores S1, S3,
    S2, S4."""
    out = {}
    for f in sorted(Path(repo).glob("*/*.dmp")):
        b = f.read_bytes()
        if len(b) != 51 or b[0] != 11 or b[2] != 1:
            continue
        pms, fb, alg, ams = b[3:7]
        raw = [b[7 + i * 11:18 + i * 11] for i in range(4)]
        ops = []
        for s in range(4):
            o = raw[REG_ORDER[s]]
            ops.append({"mul": o[0], "tl": o[1], "ar": o[2], "d1r": o[3], "sl": o[4], "rr": o[5], "am": bool(o[6]),
                        "ks": o[7], "dt": max(-3, min(3, o[8] - 3)), "d2r": o[9]})
        out[f"{f.parent.name}/{f.stem}"] = opn_voice(f.stem.upper(), alg, fb, ops, pms, ams)
    return out


def x16(repo):
    """the X16 ROM's fmpatches.s: each patch's comment line (its number and title), its label, then 26 bytes
    of YM2151 registers: title -> (number as written, voice)"""
    txt = (Path(repo) / X16_FILE).read_text()
    out = {}
    for m in re.finditer(r"; (\d{3}) ([^\n]+)\n(?:;[^\n]*\n)*[^\n;]*:\s*\n((?:\s*\.byte[^\n]*\n){4})", txt):
        num, title, body = int(m.group(1)), m.group(2).strip(), m.group(3)
        b = [int(x, 16) for x in re.findall(r"\$([0-9A-Fa-f]{2})", body)]
        if len(b) != 26:
            continue
        con, fb = b[0] & 7, (b[0] >> 3) & 7
        pms, ams = (b[1] >> 4) & 7, b[1] & 3
        dtmul, tl, ksar, amd1r, dt2d2r, d1lrr = (b[2 + 4 * k:6 + 4 * k] for k in range(6))
        ops = []
        for s in range(4):
            i = REG_ORDER[s]
            dt1 = (dtmul[i] >> 4) & 7
            ops.append({"mul": dtmul[i] & 15, "cents": (0, 600, 781, 950)[(dt2d2r[i] >> 6) & 3], "tl": tl[i] & 127,
                        "ar": ksar[i] & 31, "ks": ksar[i] >> 6, "d1r": amd1r[i] & 31, "am": bool(amd1r[i] & 128),
                        "d2r": dt2d2r[i] & 31, "sl": d1lrr[i] >> 4, "rr": d1lrr[i] & 15,
                        "dt": dt1 if dt1 < 4 else -(dt1 - 4)})
        if title in out:
            raise SystemExit(f"fm_banks: {X16_FILE}: the title {title!r} twice")
        out[title] = (num, opn_voice(title.upper()[:10], con, fb, ops, pms, ams, eg_hz=3579545 / 64 / 3))
    return out
