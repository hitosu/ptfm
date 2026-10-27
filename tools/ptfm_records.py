# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""PT/FM's records, byte for byte as specs/files/ say, both ways: what tools/pack.py writes and what its
--check reads back. This file is the reference for the bytes the firmware reads.

Conventions (specs/files/README.md):
- names: 6 bits a character, the first character in the lowest bits (NAME_CODES); padded with spaces;
- bit-packed fields follow each other from bit 0 of their first byte upward, each least significant bit
  first (BitWriter, BitReader); spare bits are 0;
- multi-byte numbers little-endian; CRC-32 is zlib's.

The records (specs/files/store.md#a-record):
- cartridges, kinds 0x01-0x06 (instruments.md): name (9 B), slots stored (1 B), then the slots at the
  type's fixed size; a sampler's: name (9 B), instruments stored, samples stored, the instruments
  (40 B each), the samples' table (16 B each);
- sample audio, kind 0x10 (ptr.md): PTR;
- patterns, kind 0x20 (patterns.md), in parts split between tracks when one record would pass 4,080 B;
- songs, kind 0x21 (songs.md), with their FX setup (64 B).

Where a spec leaves a choice open, this file makes it, and says so:
- a cell's bytes: the head, then its fields in the order the head's bits list them: the note, the
  instrument, the volume, the commands, and last the byte of rows skipped (when the head's bits 2-0
  are 7);
- PTR: a block's partitions of 64 count the predicted samples, from the first after the warm-up (the
  last partition may be shorter); 16-bit samples, raw or warm-up, are 16 bits, most significant first;
- the FX setup: ROUTING 0 GLOBAL, 1 SENDS; the chain's unused nibbles 15; the METRONOME's bytes 9-13
  its values as the FX screen shows them (TEMPO 0 FOLLOW or the BPM, BEATS A BAR 1-16, SOUND its place
  in CLICK BEEP WOOD COWBELL, LEVEL in %).
"""
import importlib.util
import os
import struct
import zlib

# the CHIP engine's tables, for their names (fw/tools/gen_chip.py: the firmware's and the playground's source)
_spec = importlib.util.spec_from_file_location(
    "gen_chip", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fw", "tools", "gen_chip.py"))
CHIP_GEN = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(CHIP_GEN)

# ------------------------------------------------------------------------------------------- basics ---
NAME_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%"   # code = place; 63 marks an empty slot
NAME_EMPTY = 63
NOTE_OFF = 1                                                       # a cell's note byte for `===`

KIND_DX7, KIND_JUNO, KIND_TB303, KIND_SAMPLER, KIND_FUSION, KIND_CHIP = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
KIND_AUDIO, KIND_PATTERN, KIND_SONG = 0x10, 0x20, 0x21
KIND_NOTES = 0xE0                                                  # a disk's notes, never sent to the device
VERSION = {KIND_DX7: 1, KIND_JUNO: 1, KIND_TB303: 1, KIND_SAMPLER: 1, KIND_FUSION: 1, KIND_CHIP: 1, KIND_AUDIO: 1,
           KIND_PATTERN: 1, KIND_SONG: 1, KIND_NOTES: 1}
RECORD_MAX = 4080                                                  # a record a SAVE writes: 4 KB less its header


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def name_codes(name, width):
    """a name (capitals and the names' signs) -> its character codes, padded with spaces"""
    if len(name) > width:
        raise ValueError(f"name past {width} characters: {name!r}")
    codes = []
    for c in name.ljust(width):
        i = NAME_CHARS.find(c)
        if i < 0:
            raise ValueError(f"not a name's character: {c!r} in {name!r}")
        codes.append(i)
    return codes


def name_text(codes):
    """character codes -> the name, trailing spaces dropped; None for an empty slot's (first code 63)"""
    if codes and codes[0] == NAME_EMPTY:
        return None
    return "".join(NAME_CHARS[c] if c < len(NAME_CHARS) else " " for c in codes).rstrip()


class BitWriter:
    """fields packed from bit 0 upward, each least significant bit first"""

    def __init__(self):
        self.acc, self.n = 0, 0

    def put(self, value, bits):
        if not 0 <= value < 1 << bits:
            raise ValueError(f"{value} does not fit {bits} bits")
        self.acc |= value << self.n
        self.n += bits

    def put_signed(self, value, bits):
        if not -(1 << (bits - 1)) <= value < 1 << (bits - 1):
            raise ValueError(f"{value} does not fit {bits} signed bits")
        self.put(value & ((1 << bits) - 1), bits)

    def put_name(self, name, width):
        for c in name_codes(name, width):
            self.put(c, 6)

    def spare(self, bits):
        self.n += bits

    def bytes(self, size):
        if self.n > size * 8:
            raise ValueError(f"{self.n} bits do not fit {size} bytes")
        return self.acc.to_bytes(size, "little")


class BitReader:
    def __init__(self, b):
        self.acc, self.n = int.from_bytes(b, "little"), 0

    def get(self, bits):
        v = (self.acc >> self.n) & ((1 << bits) - 1)
        self.n += bits
        return v

    def get_signed(self, bits):
        v = self.get(bits)
        return v - (1 << bits) if v >> (bits - 1) else v

    def get_name(self, width):
        return name_text([self.get(6) for _ in range(width)])

    def skip(self, bits):
        self.n += bits


def level_byte(db):
    """LEVEL: dB in 0.5 dB steps -> its signed byte's steps"""
    steps = round(db * 2)
    if steps != db * 2:
        raise ValueError(f"LEVEL {db} dB is not in 0.5 dB steps")
    return steps


def pack_name(name, width):
    """a name alone (a cartridge's 12 characters: 9 B; a song's 16: 12 B)"""
    w = BitWriter()
    w.put_name(name, width)
    return w.bytes(width * 6 // 8)


def unpack_name(b, width):
    return BitReader(b).get_name(width)


# ------------------------------------------------------------------------------------- cartridges ---
# Each slot's fields in order, (name, bits); `name`, `level` and the spare bits close every slot.
DX7_OP = [("r1", 7), ("r2", 7), ("r3", 7), ("r4", 7), ("l1", 7), ("l2", 7), ("l3", 7), ("l4", 7),
          ("break_point", 7), ("left_depth", 7), ("right_depth", 7), ("left_curve", 2), ("right_curve", 2),
          ("rate_scaling", 3), ("amp_mod_sens", 2), ("key_vel_sens", 3), ("output_level", 7), ("mode", 1),
          ("coarse", 5), ("fine", 7), ("detune", 4)]                                   # 113 bits
DX7_VOICE = [("pr1", 7), ("pr2", 7), ("pr3", 7), ("pr4", 7), ("pl1", 7), ("pl2", 7), ("pl3", 7), ("pl4", 7),
             ("algorithm", 5), ("feedback", 3), ("osc_key_sync", 1), ("lfo_speed", 7), ("lfo_delay", 7),
             ("lfo_pm_depth", 7), ("lfo_am_depth", 7), ("lfo_key_sync", 1), ("lfo_wave", 3),
             ("pitch_mod_sens", 3), ("transpose", 6)]                                  # 106 bits
JUNO = [("lfo_rate", 7), ("lfo_delay", 7), ("dco_lfo", 7), ("dco_pwm", 7), ("dco_noise", 7), ("vcf_freq", 7),
        ("vcf_res", 7), ("vcf_env", 7), ("vcf_lfo", 7), ("vcf_kybd", 7), ("vca_level", 7), ("env_a", 7),
        ("env_d", 7), ("env_s", 7), ("env_r", 7), ("dco_sub", 7), ("range", 2), ("pulse", 1), ("saw", 1),
        ("chorus", 2), ("pwm_source", 1), ("env_polarity", 1), ("vca_mode", 1), ("hpf", 2)]   # 123 bits
TB303 = [("wave", 7), ("cutoff", 7), ("resonance", 7), ("env_mod", 7), ("decay", 7), ("accent", 7)]
SAMPLER = [("sample", 6), ("start", 20), ("end", 20), ("loop_start", 20), ("loop_end", 20), ("loop", 2),
           ("base_note", 7), ("tune", 8)]               # then LEVEL 8, VOLUME 7, the envelope 56, name 60
ENV = [("r1", 7), ("r2", 7), ("r3", 7), ("r4", 7), ("l1", 7), ("l2", 7), ("l3", 7), ("l4", 7)]
FUSION = [("pattern", 8)]
CHIP_KINDS = CHIP_GEN.KINDS                                         # its tables: vol, arp, pitch, duty, noise
CHIP = [("osc", 3), ("duty", 2), ("bz_shape", 2), ("bz_steps", 1), ("bz_gate", 1), ("n_mix", 1), ("n_short", 1),
        ("n_rate", 4), ("n_key", 1), ("clock", 3), ("curve", 2), ("filter", 2), ("fps50", 1), ("sweep", 2),
        ("sw_shift", 3), ("sw_pace", 3), ("vol", 4), ("env", 2), ("env_step", 3), ("release", 4), ("psg_shape", 4),
        ("psg_period", 16), ("wave", 4)] + [(f"{f}_{k}", b) for k in CHIP_KINDS for f, b in (("tab", 6), ("spd", 2))] + \
       [("vib_delay", 5), ("vib_speed", 2), ("vib_depth", 5)]     # 121 bits; spd and vib_speed are SPEED - 1,
                                                                    # vib_delay and vib_depth half their frames, cents

SLOT = {  # kind: (fields, slot size, spare bits after name and LEVEL, slots at most)
    KIND_DX7: (None, 112, 44, 32), KIND_JUNO: (JUNO, 28, 33, 128), KIND_TB303: (TB303, 24, 82, 128),
    KIND_FUSION: (FUSION, 16, 52, 64), KIND_CHIP: (CHIP, 28, 35, 128)}
SAMPLER_SLOT, SAMPLE_ENTRY, SAMPLER_MAX = 40, 16, 64

CURVES = ["-lin", "-exp", "+exp", "+lin"]
WAVES = ["triangle", "saw down", "saw up", "square", "sine", "sample&hold"]
RANGES = ["16'", "8'", "4'"]
CHORUSES = ["off", "I", "II"]
LOOPS = ["off", "forward", "ping-pong"]


def _put_fields(w, fields, values):
    for f, bits in fields:
        w.put(values[f], bits)


def _get_fields(r, fields):
    return {f: r.get(bits) for f, bits in fields}


def dx7_values(v):
    """a DX7 voice as the demo disk writes it -> {field: value} for every operator (OP6 first) and the voice"""
    ops = []
    for o in reversed(v["ops"]):                                       # OP6 first, as Yamaha's packed voice
        k = o["kbd"]
        ops.append({"r1": o["eg"]["rates"][0], "r2": o["eg"]["rates"][1], "r3": o["eg"]["rates"][2],
                    "r4": o["eg"]["rates"][3], "l1": o["eg"]["levels"][0], "l2": o["eg"]["levels"][1],
                    "l3": o["eg"]["levels"][2], "l4": o["eg"]["levels"][3], "break_point": k["break_point"],
                    "left_depth": k["left_depth"], "right_depth": k["right_depth"],
                    "left_curve": CURVES.index(k["left_curve"]), "right_curve": CURVES.index(k["right_curve"]),
                    "rate_scaling": o["rate_scaling"], "amp_mod_sens": o["amp_mod_sens"],
                    "key_vel_sens": o["key_vel_sens"], "output_level": o["level"],
                    "mode": ["ratio", "fixed"].index(o["mode"]), "coarse": o["coarse"], "fine": o["fine"],
                    "detune": o["detune"] + 7})
    p, lfo = v["pitch_eg"], v["lfo"]
    voice = {"pr1": p["rates"][0], "pr2": p["rates"][1], "pr3": p["rates"][2], "pr4": p["rates"][3],
             "pl1": p["levels"][0], "pl2": p["levels"][1], "pl3": p["levels"][2], "pl4": p["levels"][3],
             "algorithm": v["algorithm"] - 1, "feedback": v["feedback"], "osc_key_sync": int(v["osc_key_sync"]),
             "lfo_speed": lfo["speed"], "lfo_delay": lfo["delay"], "lfo_pm_depth": lfo["pm_depth"],
             "lfo_am_depth": lfo["am_depth"], "lfo_key_sync": int(lfo["key_sync"]),
             "lfo_wave": WAVES.index(lfo["wave"]), "pitch_mod_sens": lfo["pitch_mod_sens"],
             "transpose": v["transpose"] + 24}
    return ops, voice


def dx7_voice(ops, voice):
    """dx7_values' inverse: the demo disk's form (OP1 first)"""
    out = []
    for o in reversed(ops):
        out.append({"eg": {"rates": [o["r1"], o["r2"], o["r3"], o["r4"]],
                           "levels": [o["l1"], o["l2"], o["l3"], o["l4"]]},
                    "level": o["output_level"], "mode": ["ratio", "fixed"][o["mode"]], "coarse": o["coarse"],
                    "fine": o["fine"], "detune": o["detune"] - 7, "rate_scaling": o["rate_scaling"],
                    "amp_mod_sens": o["amp_mod_sens"], "key_vel_sens": o["key_vel_sens"],
                    "kbd": {"break_point": o["break_point"], "left_depth": o["left_depth"],
                            "right_depth": o["right_depth"], "left_curve": CURVES[o["left_curve"]],
                            "right_curve": CURVES[o["right_curve"]]}})
    v = voice
    return {"algorithm": v["algorithm"] + 1, "feedback": v["feedback"], "osc_key_sync": bool(v["osc_key_sync"]),
            "transpose": v["transpose"] - 24,
            "pitch_eg": {"rates": [v["pr1"], v["pr2"], v["pr3"], v["pr4"]],
                         "levels": [v["pl1"], v["pl2"], v["pl3"], v["pl4"]]},
            "lfo": {"speed": v["lfo_speed"], "delay": v["lfo_delay"], "pm_depth": v["lfo_pm_depth"],
                    "am_depth": v["lfo_am_depth"], "key_sync": bool(v["lfo_key_sync"]),
                    "wave": WAVES[v["lfo_wave"]], "pitch_mod_sens": v["pitch_mod_sens"]},
            "ops": out}


def juno_values(p):
    d, f, a, e = p["dco"], p["vcf"], p["vca"], p["env"]
    return {"lfo_rate": p["lfo"]["rate"], "lfo_delay": p["lfo"]["delay"], "dco_lfo": d["lfo"], "dco_pwm": d["pwm"],
            "dco_noise": d["noise"], "vcf_freq": f["freq"], "vcf_res": f["res"], "vcf_env": f["env"],
            "vcf_lfo": f["lfo"], "vcf_kybd": f["kybd"], "vca_level": a["level"], "env_a": e["a"], "env_d": e["d"],
            "env_s": e["s"], "env_r": e["r"], "dco_sub": d["sub"], "range": RANGES.index(d["range"]),
            "pulse": int(d["pulse"]), "saw": int(d["saw"]), "chorus": CHORUSES.index(p["chorus"]),
            "pwm_source": ["lfo", "manual"].index(d["pwm_source"]), "env_polarity": ["+", "-"].index(f["env_polarity"]),
            "vca_mode": ["env", "gate"].index(a["mode"]), "hpf": p["hpf"]}


def juno_patch(v):
    return {"lfo": {"rate": v["lfo_rate"], "delay": v["lfo_delay"]},
            "dco": {"lfo": v["dco_lfo"], "pwm": v["dco_pwm"], "noise": v["dco_noise"], "sub": v["dco_sub"],
                    "range": RANGES[v["range"]], "pulse": bool(v["pulse"]), "saw": bool(v["saw"]),
                    "pwm_source": ["lfo", "manual"][v["pwm_source"]]},
            "hpf": v["hpf"],
            "vcf": {"freq": v["vcf_freq"], "res": v["vcf_res"], "env": v["vcf_env"], "lfo": v["vcf_lfo"],
                    "kybd": v["vcf_kybd"], "env_polarity": ["+", "-"][v["env_polarity"]]},
            "vca": {"level": v["vca_level"], "mode": ["env", "gate"][v["vca_mode"]]},
            "env": {"a": v["env_a"], "d": v["env_d"], "s": v["env_s"], "r": v["env_r"]},
            "chorus": CHORUSES[v["chorus"]]}


def tb303_values(p):
    return {"wave": p["wave"], "cutoff": p["cutoff"], "resonance": p["reso"], "env_mod": p["envmod"],
            "decay": p["decay"], "accent": p["accent"]}


def tb303_patch(v):
    return {"wave": v["wave"], "cutoff": v["cutoff"], "reso": v["resonance"], "envmod": v["env_mod"],
            "decay": v["decay"], "accent": v["accent"]}


CHIP_OSC = ["PULSE", "TRIANGLE", "WAVE", "BUZZ", "NOISE"]
CHIP_DUTY = ["12.5%", "25%", "50%", "75%"]
CHIP_SHAPE = ["SAW DN", "SAW UP", "TRI"]
CHIP_CLOCK = CHIP_GEN.CLOCK_NAMES                                   # BRK, 8BIT, PSG 1.77, PSG 1.00, PSG 1.79, 16BIT, EXACT
CHIP_CURVE = ["BRK", "8BIT", "PSG", "16BIT"]
CHIP_FILTER = ["BRK", "8BIT", "PSG", "NONE"]
CHIP_SWEEP = ["OFF", "DOWN", "UP"]
CHIP_ENV = ["NONE", "DOWN", "UP", "PSG"]
CHIP_WAVES = [n for n, _ in CHIP_GEN.WAVE_DEFS]
CHIP_TABLES = {k: [n for n, _ in CHIP_GEN.TABLES[k]] for k in CHIP_KINDS}   # a table's number is its place + 1


def chip_values(p):
    """a CHIP instrument as the demo disk writes it (demo-disk/patches/chip/) -> its fields; a field left out takes
    the engine's default"""
    b = p.get("buzz", {})
    n = p.get("noise", {})
    s = p.get("sweep", {})
    v = p["volume"]
    e = p.get("psg_env", {})
    t = p.get("tables", {})
    vib = p.get("vibrato") or [0, 1, 0]
    out = {"osc": CHIP_OSC.index(p["osc"]), "duty": CHIP_DUTY.index(p.get("duty", "50%")),
           "bz_shape": CHIP_SHAPE.index(b.get("shape", "SAW DN")), "bz_steps": [16, 32].index(b.get("steps", 16)),
           "bz_gate": int(b.get("gate", False)), "n_mix": int(n.get("mix", False)),
           "n_short": ["LONG", "SHORT"].index(n.get("mode", "LONG")), "n_rate": n.get("rate", 4),
           "n_key": int(n.get("key", False)), "clock": CHIP_CLOCK.index(p["clock"]), "curve": CHIP_CURVE.index(p["curve"]),
           "filter": CHIP_FILTER.index(p["filter"]), "fps50": [60, 50].index(p["frames"]),
           "sweep": CHIP_SWEEP.index(s.get("dir", "OFF")), "sw_shift": s.get("shift", 2), "sw_pace": s.get("pace", 2),
           "vol": v["vol"], "env": CHIP_ENV.index(v["env"]), "env_step": v["step"], "release": v["release"],
           "psg_shape": e.get("shape", 9), "psg_period": e.get("period", 800),
           "wave": CHIP_WAVES.index(p.get("wave", "SAW")),
           "vib_delay": vib[0] // 2, "vib_speed": vib[1] - 1, "vib_depth": vib[2] // 2}
    if vib[0] % 2 or vib[2] % 2 or not 1 <= vib[1] <= 4:
        raise ValueError(f"{p['name']}: vibrato {vib}: delay and depth in 2s, speed 1-4")
    for k in CHIP_KINDS:
        name, speed = t.get(k, [None, 1])
        out["tab_" + k] = CHIP_TABLES[k].index(name) + 1 if name else 0
        out["spd_" + k] = speed - 1
    return out


def chip_numeric(v):
    """a CHIP slot's fields as the engine's settings (fw/core/eng/chip/chip.h chip_patch_t; the playground's patch,
    playground/dsp-chip.js): SPEEDs 1-4, the vibrato's frames and cents, the tables by number"""
    out = {f: v[f] for f, _ in CHIP[:23]}
    out["psg_period"] = out["psg_period"] or 1
    out["tab"] = [v["tab_" + k] for k in CHIP_KINDS]
    out["spd"] = [v["spd_" + k] + 1 for k in CHIP_KINDS]
    out.update(vib_delay=v["vib_delay"] * 2, vib_speed=v["vib_speed"] + 1, vib_depth=v["vib_depth"] * 2)
    return out


def chip_patch(v):
    """chip_values' inverse, every field written out (the demo disk's form)"""
    out = {"osc": CHIP_OSC[v["osc"]], "duty": CHIP_DUTY[v["duty"]],
           "buzz": {"shape": CHIP_SHAPE[v["bz_shape"]], "steps": [16, 32][v["bz_steps"]], "gate": bool(v["bz_gate"])},
           "noise": {"mix": bool(v["n_mix"]), "mode": ["LONG", "SHORT"][v["n_short"]], "rate": v["n_rate"],
                     "key": bool(v["n_key"])},
           "clock": CHIP_CLOCK[v["clock"]], "curve": CHIP_CURVE[v["curve"]], "filter": CHIP_FILTER[v["filter"]],
           "frames": [60, 50][v["fps50"]], "sweep": {"dir": CHIP_SWEEP[v["sweep"]], "shift": v["sw_shift"], "pace": v["sw_pace"]},
           "volume": {"vol": v["vol"], "env": CHIP_ENV[v["env"]], "step": v["env_step"], "release": v["release"]},
           "psg_env": {"shape": v["psg_shape"], "period": v["psg_period"]}, "wave": CHIP_WAVES[v["wave"]],
           "tables": {k: [CHIP_TABLES[k][v["tab_" + k] - 1] if v["tab_" + k] else None, v["spd_" + k] + 1]
                      for k in CHIP_KINDS},                     # (a SPEED kept with no table too)
           "vibrato": [v["vib_delay"] * 2, v["vib_speed"] + 1, v["vib_depth"] * 2]}
    return out


def pack_slot(kind, slot):
    """a slot {name, level, ...values as the fields name them} -> its bytes; None: an empty slot"""
    fields, size, spare, _ = SLOT[kind]
    w = BitWriter()
    if slot is None:                                                    # empty: its name's first code 63
        w.spare(size * 8 - 60 - 8 - spare)
        w.put(NAME_EMPTY, 6)
        return w.bytes(size)
    if kind == KIND_DX7:
        for o in slot["ops"]:
            _put_fields(w, DX7_OP, o)
        _put_fields(w, DX7_VOICE, slot["voice"])
    else:
        _put_fields(w, fields, slot)
    w.put_name(slot["name"], 10)
    w.put_signed(slot["level"], 8)
    w.spare(spare)
    if w.n != size * 8:
        raise AssertionError(f"slot of kind {kind}: {w.n} bits, not {size * 8}")
    return w.bytes(size)


def unpack_slot(kind, b):
    fields, size, spare, _ = SLOT[kind]
    r = BitReader(b)
    if kind == KIND_DX7:
        out = {"ops": [_get_fields(r, DX7_OP) for _ in range(6)], "voice": _get_fields(r, DX7_VOICE)}
    else:
        out = _get_fields(r, fields)
    out["name"] = r.get_name(10)
    out["level"] = r.get_signed(8)
    return None if out["name"] is None else out


def pack_cartridge(kind, name, slots):
    """a DX7, Juno-106, TB-303, FUSION or CHIP cartridge: slots stored up to the last one used"""
    _, size, _, most = SLOT[kind]
    while slots and slots[-1] is None:
        slots = slots[:-1]
    if len(slots) > most:
        raise ValueError(f"{name}: {len(slots)} slots, {most} at most")
    return pack_name(name, 12) + bytes([len(slots)]) + b"".join(pack_slot(kind, s) for s in slots)


def unpack_cartridge(kind, b):
    _, size, _, _ = SLOT[kind]
    n = b[9]
    if len(b) != 10 + n * size:
        raise ValueError(f"cartridge kind {kind}: {len(b)} B for {n} slots")
    return {"name": unpack_name(b[0:9], 12),
            "slots": [unpack_slot(kind, b[10 + i * size:10 + (i + 1) * size]) for i in range(n)]}


def pack_sampler_instrument(s):
    if s is None:
        w = BitWriter()
        w.spare(SAMPLER_SLOT * 8 - 86 - 60)
        w.put(NAME_EMPTY, 6)
        return w.bytes(SAMPLER_SLOT)
    w = BitWriter()
    _put_fields(w, SAMPLER, s)
    w.put_signed(s["level"], 8)
    w.put(s["volume"], 7)
    _put_fields(w, ENV, s["env"])
    w.put_name(s["name"], 10)
    w.spare(86)
    if w.n != SAMPLER_SLOT * 8:
        raise AssertionError(f"sampler slot: {w.n} bits")
    return w.bytes(SAMPLER_SLOT)


def unpack_sampler_instrument(b):
    r = BitReader(b)
    s = _get_fields(r, SAMPLER)
    s["level"] = r.get_signed(8)
    s["volume"] = r.get(7)
    s["env"] = _get_fields(r, ENV)
    s["name"] = r.get_name(10)
    return None if s["name"] is None else s


def pack_sample_entry(e):
    """a sample's entry: length 20, rate 18, coding 4, name 60, spare 26"""
    w = BitWriter()
    if e is None:
        w.spare(42)
        w.put(NAME_EMPTY, 6)
        return w.bytes(SAMPLE_ENTRY)
    w.put(e["length"], 20)
    w.put(e["rate"], 18)
    w.put(e["coding"], 4)
    w.put_name(e["name"], 10)
    w.spare(26)
    return w.bytes(SAMPLE_ENTRY)


def unpack_sample_entry(b):
    r = BitReader(b)
    e = {"length": r.get(20), "rate": r.get(18), "coding": r.get(4), "name": r.get_name(10)}
    return None if e["name"] is None else e


def pack_sampler(name, instruments, samples):
    if len(instruments) > SAMPLER_MAX or len(samples) > SAMPLER_MAX:
        raise ValueError(f"{name}: more than {SAMPLER_MAX} instruments or samples")
    return (pack_name(name, 12) + bytes([len(instruments), len(samples)])
            + b"".join(pack_sampler_instrument(s) for s in instruments)
            + b"".join(pack_sample_entry(e) for e in samples))


def unpack_sampler(b):
    ni, ns = b[9], b[10]
    if len(b) != 11 + ni * SAMPLER_SLOT + ns * SAMPLE_ENTRY:
        raise ValueError(f"sampler cartridge: {len(b)} B for {ni} instruments, {ns} samples")
    at = 11 + ni * SAMPLER_SLOT
    return {"name": unpack_name(b[0:9], 12),
            "instruments": [unpack_sampler_instrument(b[11 + i * SAMPLER_SLOT:11 + (i + 1) * SAMPLER_SLOT])
                            for i in range(ni)],
            "samples": [unpack_sample_entry(b[at + i * SAMPLE_ENTRY:at + (i + 1) * SAMPLE_ENTRY]) for i in range(ns)]}


# ------------------------------------------------------------------------------------------- PTR ---
PTR_8, PTR_16 = 1, 2
PTR_BLOCK, PTR_PART, PTR_RAW = 256, 64, 7


class MsbWriter:
    """PTR's bit stream: each byte from its most significant bit"""

    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, bits):
        self.acc = (self.acc << bits) | (v & ((1 << bits) - 1))
        self.n += bits
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def put_rice(self, u, k):
        q = u >> k
        while q >= 24:                                                 # long runs of zeros, a byte at a time
            self.put(0, 24)
            q -= 24
        self.put(1, q + 1)                                             # q zeros, then a one
        if k:
            self.put(u & ((1 << k) - 1), k)

    def finish(self):
        if self.n:
            self.put(0, 8 - self.n)                                    # padded to a whole byte
        return bytes(self.out)


class MsbReader:
    def __init__(self, b, at=0):
        self.b, self.pos = b, at * 8

    def get(self, bits):
        v = 0
        for _ in range(bits):
            byte = self.b[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def get_rice(self, k):
        q = 0
        while self.get(1) == 0:
            q += 1
        return (q << k) | (self.get(k) if k else 0)


def _predict(order, x1, x2, x3):
    return (0, x1, 2 * x1 - x2, 3 * x1 - 3 * x2 + x3)[order]


def _residuals(block, order):
    us = []
    for i in range(order, len(block)):
        x1 = block[i - 1] if i >= 1 else 0
        x2 = block[i - 2] if i >= 2 else 0
        x3 = block[i - 3] if i >= 3 else 0
        e = block[i] - _predict(order, x1, x2, x3)
        us.append(2 * e if e >= 0 else -2 * e - 1)
    return us


def _rice_cost(us, k):
    return sum((u >> k) for u in us) + len(us) * (1 + k)


def _best_k(us, kbits, plain_bits):
    """a partition's Rice parameter and its bits: k up to 2**kbits - 2, or the highest value: stored plainly"""
    escape = (1 << kbits) - 1
    best = (len(us) * plain_bits, escape)
    mean = sum(us) // max(1, len(us))
    k0 = max(0, mean.bit_length() - 2)
    for k in range(max(0, k0 - 1), min(escape - 1, k0 + 3) + 1):
        c = _rice_cost(us, k)
        if c < best[0]:
            best = (c, k)
    return best[1]


def _encode_block(block, bits):
    kbits, plain = (4, 12) if bits == 8 else (5, 20)
    escape = (1 << kbits) - 1
    best = None
    for order in range(min(3, len(block)) + 1):
        w = MsbWriter()
        w.put(order, 8)
        for x in block[:order]:
            w.put(x & ((1 << bits) - 1), bits)
        us = _residuals(block, order)
        for p in range(0, len(us), PTR_PART):
            part = us[p:p + PTR_PART]
            k = _best_k(part, kbits, plain)
            w.put(k, kbits)
            for u in part:
                if k == escape:
                    if u >= 1 << plain:
                        raise ValueError("PTR: a residual past the plain width")
                    w.put(u, plain)
                else:
                    w.put_rice(u, k)
        b = w.finish()
        if best is None or len(b) < len(best):
            best = b
    w = MsbWriter()                                                    # raw
    w.put(PTR_RAW, 8)
    for x in block:
        w.put(x & ((1 << bits) - 1), bits)
    raw = w.finish()
    return raw if len(raw) < len(best) else best


def _signed(v, bits):
    return v - (1 << bits) if v >> (bits - 1) else v


def _decode_block(b, at, count, bits):
    kbits, plain = (4, 12) if bits == 8 else (5, 20)
    escape = (1 << kbits) - 1
    r = MsbReader(b, at)
    mode = r.get(8)
    if mode == PTR_RAW:
        return [_signed(r.get(bits), bits) for _ in range(count)]
    if mode > 3:
        raise ValueError(f"PTR: block mode {mode}")
    xs = [_signed(r.get(bits), bits) for _ in range(min(mode, count))]
    left, k = count - len(xs), 0
    done = 0
    while left > 0:
        if done % PTR_PART == 0:
            k = r.get(kbits)
        u = r.get(plain) if k == escape else r.get_rice(k)
        e = (u >> 1) ^ -(u & 1)
        x1 = xs[-1] if xs else 0
        x2 = xs[-2] if len(xs) >= 2 else 0
        x3 = xs[-3] if len(xs) >= 3 else 0
        xs.append(e + _predict(mode, x1, x2, x3))
        left -= 1
        done += 1
    return xs


def ptr_encode(samples, bits):
    """signed samples (8 or 16 bits) -> a PTR record; checks itself by decoding"""
    n = len(samples)
    if n >= 1 << 24:
        raise ValueError("PTR: a sample past 2**24 samples")
    blocks = [_encode_block(samples[i:i + PTR_BLOCK], bits) for i in range(0, n, PTR_BLOCK)]
    nb = len(blocks)
    head = 4 + 4 * ((nb + 15) // 16) + 2 * nb
    offs, at = [], head
    for i, blk in enumerate(blocks):
        if i % 16 == 0:
            offs.append(at)
        if len(blk) > 0xFFFF:
            raise ValueError("PTR: a block past 64 KB")
        at += len(blk)
    rec = (bytes([PTR_8 if bits == 8 else PTR_16]) + n.to_bytes(3, "little")
           + b"".join(struct.pack("<I", o) for o in offs) + b"".join(struct.pack("<H", len(b)) for b in blocks)
           + b"".join(blocks))
    if ptr_decode(rec) != (bits, list(samples)):
        raise AssertionError("PTR: the coded sample does not decode back")
    return rec


def ptr_block_offset(rec, i):
    """where block i starts: the offset of its group of 16, then the lengths before it in the group"""
    nb = (int.from_bytes(rec[1:4], "little") + PTR_BLOCK - 1) // PTR_BLOCK
    groups = (nb + 15) // 16
    at = struct.unpack_from("<I", rec, 4 + 4 * (i // 16))[0]
    lens = 4 + 4 * groups
    for j in range(i - i % 16, i):
        at += struct.unpack_from("<H", rec, lens + 2 * j)[0]
    return at


def ptr_decode(rec):
    """a PTR record -> (bits, signed samples)"""
    bits = {PTR_8: 8, PTR_16: 16}[rec[0]]
    n = int.from_bytes(rec[1:4], "little")
    out = []
    for i in range((n + PTR_BLOCK - 1) // PTR_BLOCK):
        out += _decode_block(rec, ptr_block_offset(rec, i), min(PTR_BLOCK, n - i * PTR_BLOCK), bits)
    return bits, out


# -------------------------------------------------------------------------------------- patterns ---
CMD_LIST = 0xFF


def _track_stream(cells):
    """one track's cells [(row, note, ins, vol, cmds)] in row order -> its stream, ended by 0x00.
    note: 0 none, 1 note off, else MIDI 12-127; ins: -1 none, else cartridge << 8 | slot; vol: -1 or 0-127"""
    out, last_ins, prev = bytearray(), None, -1
    for row, note, ins, vol, cmds in cells:
        skip = row - prev - 1
        prev = row
        if not 0 <= skip <= 255:
            raise ValueError(f"row {row}: a skip of {skip}")
        head, body = min(skip, 7), bytearray()
        if note:
            if note != NOTE_OFF and not 12 <= note <= 127:
                raise ValueError(f"row {row}: note {note} out of C-0 .. G-9")
            head |= 0x80
            body.append(note)
        if ins >= 0:
            if last_ins == ins:
                head |= 0x20
            elif last_ins is not None and last_ins >> 8 == ins >> 8:
                head |= 0x40
                body.append(ins & 0xFF)
            else:
                head |= 0x60
                body += bytes([ins >> 8, ins & 0xFF])
            last_ins = ins
        if vol >= 0:
            if vol > 0x7F:
                raise ValueError(f"row {row}: volume {vol:02X}")
            head |= 0x10
            body.append(vol)
        if cmds:
            if len(cmds) > 4:
                raise ValueError(f"row {row}: {len(cmds)} commands")
            head |= 0x08
            if len(cmds) == 1:
                body += bytes(cmds[0])
            else:
                body += bytes([CMD_LIST, len(cmds)])
                for c in cmds:
                    body += bytes(c)
        if head & 0xF8 == 0:
            raise ValueError(f"row {row}: a cell with no field")
        if skip >= 7:
            body.append(skip)
        out += bytes([head]) + body
    return bytes(out) + b"\x00"


def _read_stream(b, at):
    """a track's stream from b[at] -> ([(row, note, ins, vol, cmds)], where it ended + 1)"""
    cells, last_ins, prev = [], None, -1
    while True:
        head = b[at]
        at += 1
        if head == 0:
            return cells, at
        note = ins = vol = -1
        note = 0
        if head & 0x80:
            note = b[at]
            at += 1
        mode = (head >> 5) & 3
        if mode == 1:
            ins = last_ins
        elif mode == 2:
            ins = (last_ins & 0xFF00) | b[at]
            at += 1
        elif mode == 3:
            ins = b[at] << 8 | b[at + 1]
            at += 2
        if mode:
            if ins is None:
                raise ValueError("a cell names the track's last instrument before it has one")
            last_ins = ins
        else:
            ins = -1
        if head & 0x10:
            vol = b[at]
            at += 1
        cmds = []
        if head & 0x08:
            if b[at] == CMD_LIST:
                n = b[at + 1]
                at += 2
            else:
                n = 1
            for _ in range(n):
                cmds.append((b[at], b[at + 1]))
                at += 2
        skip = head & 7
        if skip == 7:
            skip = b[at]
            at += 1
        row = prev + 1 + skip
        prev = row
        cells.append((row, note, ins, vol, cmds))


def pack_pattern(tracks, rows, edit_step, cells):
    """cells: {(row, track): (note, ins, vol, cmds)} -> the pattern's records' payloads (part 0, 1, ...)"""
    if not 1 <= tracks <= 16 or not 1 <= rows <= 256 or not 0 <= edit_step <= 16:
        raise ValueError(f"pattern of {tracks} tracks, {rows} rows, EDIT STEP {edit_step}")
    streams = {}
    for t in range(tracks):
        mine = sorted((r, *c) for (r, tt), c in cells.items() if tt == t)
        if any(r >= rows for r, *_ in mine):
            raise ValueError("a cell past the pattern's last row")
        if mine:
            streams[t] = _track_stream(mine)
    parts, cur = [], []
    for t in sorted(streams):
        if 5 + sum(len(streams[x]) for x in cur) + len(streams[t]) > RECORD_MAX and cur:
            parts.append(cur)
            cur = []
        cur.append(t)
    if cur or not parts:
        parts.append(cur)
    if len(parts) > 16:
        raise ValueError("a pattern past 16 parts")
    out = []
    for part in parts:
        mask = sum(1 << t for t in part)
        out.append(bytes([tracks, rows - 1, edit_step]) + struct.pack("<H", mask)
                   + b"".join(streams[t] for t in part))
    return out


def unpack_pattern(parts):
    """the payloads of a pattern's parts -> {tracks, rows, edit_step, cells {(row, track): (note, ins, vol, cmds)}}"""
    cells, head = {}, None
    for b in parts:
        if head is None:
            head = (b[0], b[1] + 1, b[2])
        elif (b[0], b[1] + 1, b[2]) != head:
            raise ValueError("a pattern's parts disagree on its size")
        mask, at = struct.unpack_from("<H", b, 3)[0], 5
        for t in range(16):
            if mask >> t & 1:
                if t >= head[0]:
                    raise ValueError(f"track {t} stored past the pattern's {head[0]}")
                found, at = _read_stream(b, at)
                for row, note, ins, vol, cmds in found:
                    if row >= head[1]:
                        raise ValueError(f"track {t}: row {row} past the pattern's {head[1]}")
                    cells[(row, t)] = (note, ins, vol, cmds)
        if at != len(b):
            raise ValueError(f"a pattern part: {len(b) - at} B after its streams")
    return {"tracks": head[0], "rows": head[1], "edit_step": head[2], "cells": cells}


# ------------------------------------------------------------------------------------------ songs ---
EFFECTS = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME",
           "SENDS"]                                                    # their fixed numbers (commands.md)
CHAIN_END = 15
NV = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"]
GATE_RATES = ["1/8", "1/16", "1/32", "1/8T", "1/16T"]
CHORUS_MODES = ["I", "II", "I+II"]
SOUNDS = ["CLICK", "BEEP", "WOOD", "COWBELL"]
MIX_FIELDS = ["level", "pan", "duck", "chorus", "delay", "reverb"]
MIX_DEFAULT = {"level": 0x7F, "pan": 0x80, "duck": 0, "chorus": 0, "delay": 0, "reverb": 0}


def _cut_step(cut):
    """FILTER CUT as the disk writes it (`LP 64`..`OFF`..`HP 63`) -> its step, 0-127 (OFF 64)"""
    if cut == "OFF":
        return 64
    v = int(cut[3:])
    return 64 - v if cut.startswith("LP") else 64 + v


def _cut_text(step):
    return "OFF" if step == 64 else f"LP {64 - step}" if step < 64 else f"HP {step - 64}"


def knob_steps(fx):
    """the FX setup's knobs as the demo disk writes them -> {code: step} (code 0x90-0xAC, 0xB0-0xB5)"""
    g, f, d, l, c, y = fx["gate"], fx["filter"], fx["drive"], fx["lofi"], fx["chorus"], fx["delay"]
    r, lm, k = fx["reverb"], fx["limiter"], fx["duck"]
    steps = [g["pattern"] - 1, GATE_RATES.index(g["rate"]), g["depth"], g["smooth"],          # 0x90-0x93
             _cut_step(f["cut"]), f["reso"],                                                  # 0x94-0x95
             d["drive"], d["tone"],                                                           # 0x96-0x97
             l["bits"] - 2, l["rate"] - 1, l["dust"],                                         # 0x98-0x9A
             CHORUS_MODES.index(c["mode"]), c["rate"], c["depth"], c["mix"],                  # 0x9B-0x9E
             NV.index(y["time"]), y["feedback"], y["tone"], int(y["ping_pong"]), y["mix"],    # 0x9F-0xA3
             r["size"], r["damp"], min(100, r["pre_delay"]), r["mix"],                        # 0xA4-0xA7
             lm["ceiling"] + 12, (lm["release"] - 10) // 10,                                  # 0xA8-0xA9
             k["source"] - 1, k["depth"], (k["release"] - 20) // 10]                          # 0xAA-0xAC
    if (lm["release"] - 10) % 10 or (k["release"] - 20) % 10:
        raise ValueError("a RELEASE not in steps of 10 ms")
    out = {0x90 + i: s for i, s in enumerate(steps)}
    for i, key in enumerate(FILTER_ENV):                                                      # 0xB0-0xB5
        out[0xB0 + i] = f.get(key, FILTER_ENV_DEFAULT[key])
    return out


# FILTER's envelope (0xB0-0xB5, its steps: ui/screens/fx.md#filters-envelope); absent from a disk's song: a new
# song's (ENV 0 %: off)
FILTER_ENV = ["env", "source", "attack", "decay", "sustain", "release"]
FILTER_ENV_DEFAULT = {"env": 0, "source": 0, "attack": 0, "decay": 75, "sustain": 0, "release": 75}


def knob_values(steps):
    """knob_steps' inverse, as the demo disk writes the knobs"""
    s = [steps[0x90 + i] for i in range(29)]
    env = {key: steps.get(0xB0 + i, 0) for i, key in enumerate(FILTER_ENV)}
    return {"gate": {"pattern": s[0] + 1, "rate": GATE_RATES[s[1]], "depth": s[2], "smooth": s[3]},
            "filter": dict({"cut": _cut_text(s[4]), "reso": s[5]}, **env),
            "drive": {"drive": s[6], "tone": s[7]},
            "lofi": {"bits": s[8] + 2, "rate": s[9] + 1, "dust": s[10]},
            "chorus": {"mode": CHORUS_MODES[s[11]], "rate": s[12], "depth": s[13], "mix": s[14]},
            "delay": {"time": NV[s[15]], "feedback": s[16], "tone": s[17], "ping_pong": bool(s[18]), "mix": s[19]},
            "reverb": {"size": s[20], "damp": s[21], "pre_delay": s[22], "mix": s[23]},
            "limiter": {"ceiling": s[24] - 12, "release": s[25] * 10 + 10},
            "duck": {"source": s[26] + 1, "depth": s[27], "release": s[28] * 10 + 20}}


FX_KEYS = {"GATE": "gate", "FILTER": "filter", "DRIVE": "drive", "LO-FI": "lofi", "CHORUS": "chorus",
           "DELAY": "delay", "REVERB": "reverb", "LIMITER": "limiter", "DUCK": "duck", "METRONOME": "metronome"}


def pack_fx(fx):
    """an FX setup as the demo disk writes it -> its 64 bytes (songs.md#the-fx-setup)"""
    b = bytearray(64)
    b[0] = ["global", "sends"].index(fx["routing"])
    chain = [EFFECTS.index(x) for x in fx["chain"]]
    if len(chain) > 12:
        raise ValueError("a chain of more than 12 items")
    nibbles = chain + [CHAIN_END] * (12 - len(chain))
    for i, v in enumerate(nibbles):
        b[1 + i // 2] |= v << (4 * (i % 2))
    on = 0
    for name, key in FX_KEYS.items():
        if fx[key]["on"]:
            on |= 1 << EFFECTS.index(name)
    struct.pack_into("<H", b, 7, on)
    m = fx["metronome"]
    struct.pack_into("<H", b, 9, 0 if m["tempo"] == "FOLLOW" else m["tempo"])
    b[11], b[12], b[13] = m["beats_a_bar"], SOUNDS.index(m["sound"]), m["level"]
    for code, step in knob_steps(fx).items():
        if not 0 <= step <= 255:
            raise ValueError(f"FX knob {code:02X}: step {step}")
        b[16 + code - 0x90] = step
    return bytes(b)


def unpack_fx(b):
    chain = []
    for i in range(12):
        v = (b[1 + i // 2] >> (4 * (i % 2))) & 15
        if v == CHAIN_END:
            break
        chain.append(EFFECTS[v])
    fx = {"routing": ["global", "sends"][b[0]], "chain": chain}
    fx.update(knob_values({0x90 + i: b[16 + i] for i in range(48)}))
    tempo = struct.unpack_from("<H", b, 9)[0]
    fx["metronome"] = {"tempo": "FOLLOW" if tempo == 0 else tempo, "beats_a_bar": b[11], "sound": SOUNDS[b[12]],
                       "level": b[13]}
    on = struct.unpack_from("<H", b, 7)[0]
    for name, key in FX_KEYS.items():
        fx[key]["on"] = bool(on >> EFFECTS.index(name) & 1)
    return fx


def pack_song(s):
    """a song (name, tempo, ticks, rpb, swing, at_end 'stop' / 'loop', loop_from, slides 'linear' / 'amiga',
    fast_volume_slides, global_volume, positions, mixer [{level, pan, duck, chorus, delay, reverb}], fx)"""
    pos = s["positions"]
    if not 1 <= len(pos) <= 256:
        raise ValueError(f"{s['name']}: {len(pos)} positions")
    flags = (s["at_end"] == "stop") | (s["slides"] == "linear") << 1 | bool(s["fast_volume_slides"]) << 2
    mixer = [dict(MIX_DEFAULT, **m) for m in s["mixer"]]
    while mixer and mixer[-1] == MIX_DEFAULT:
        mixer.pop()
    if len(mixer) > 16:
        raise ValueError(f"{s['name']}: a mixer of {len(mixer)} tracks")
    out = (pack_name(s["name"], 16) + struct.pack("<H", s["tempo"])
           + bytes([s["ticks"], s["rpb"], s["swing"], flags, s["loop_from"], s["global_volume"], len(pos) - 1])
           + bytes(pos) + bytes([len(mixer)]) + b"".join(bytes(m[f] for f in MIX_FIELDS) for m in mixer)
           + pack_fx(s["fx"]) + bytes(8))
    return out


def unpack_song(b):
    npos = b[20] + 1
    at = 21 + npos
    nmix = b[at]
    mixer = [dict(zip(MIX_FIELDS, b[at + 1 + 6 * i:at + 7 + 6 * i])) for i in range(nmix)]
    at += 1 + 6 * nmix
    if len(b) != at + 64 + 8:
        raise ValueError(f"song: {len(b)} B, {at + 72} expected")
    return {"name": unpack_name(b[0:12], 16), "tempo": struct.unpack_from("<H", b, 12)[0], "ticks": b[14],
            "rpb": b[15], "swing": b[16], "at_end": "stop" if b[17] & 1 else "loop",
            "slides": "linear" if b[17] & 2 else "amiga", "fast_volume_slides": bool(b[17] & 4),
            "loop_from": b[18], "global_volume": b[19], "positions": list(b[21:21 + npos]),
            "mixer": mixer + [dict(MIX_DEFAULT) for _ in range(16 - nmix)], "fx": unpack_fx(b[at:at + 64])}


# -------------------------------------------------------------------------------------- the disk ---
DISK_MAGIC = b"PTFMDISK"
DISK_LAYOUT = 1
DISK_ORDER = {0x30: 0, 0x31: 0, KIND_DX7: 1, KIND_JUNO: 1, KIND_TB303: 1, KIND_SAMPLER: 1, KIND_FUSION: 1,
              KIND_CHIP: 1, KIND_SONG: 2, KIND_PATTERN: 3, 0x22: 4, 0x23: 4, KIND_NOTES: 5, KIND_AUDIO: 6}


def disk_sort_key(f):
    """the disk's order: settings, cartridges, songs, patterns, the loops, notes, then the audio; each by
    number and sub"""
    return DISK_ORDER.get(f[0], 7), f[1], f[2], f[0]


def pack_disk(files):
    """files: [(kind, number, sub, payload)] -> a .ptfmdsk (disk.md): each (kind, number, sub) once, in order"""
    seen = set()
    out = bytearray(DISK_MAGIC + bytes([DISK_LAYOUT, 0, 0, 0]) + struct.pack("<I", len(files)))
    for kind, num, sub, payload in sorted(files, key=disk_sort_key):
        if (kind, num, sub) in seen:
            raise ValueError(f"a file twice: {kind:02X} {num:02X} {sub}")
        seen.add((kind, num, sub))
        out += bytes([kind, num, sub, VERSION[kind]]) + struct.pack("<II", len(payload), crc32(payload))
        out += payload
        out += bytes(-len(out) % 4)
    return bytes(out)


def unpack_disk(b):
    """a .ptfmdsk -> [(kind, number, sub, version, payload)], every CRC checked"""
    if b[0:8] != DISK_MAGIC or b[8] != DISK_LAYOUT:
        raise ValueError("not a PT/FM disk (layout 1)")
    n, at, files = struct.unpack_from("<I", b, 12)[0], 16, []
    for _ in range(n):
        kind, num, sub, ver = b[at:at + 4]
        length, crc = struct.unpack_from("<II", b, at + 4)
        payload = b[at + 12:at + 12 + length]
        if len(payload) != length or crc32(payload) != crc:
            raise ValueError(f"file {kind:02X} {num:02X} {sub}: bad length or CRC")
        files.append((kind, num, sub, ver, payload))
        at += 12 + length
        at += -at % 4
    if at != len(b):
        raise ValueError(f"{len(b) - at} B after the disk's last file")
    return files
