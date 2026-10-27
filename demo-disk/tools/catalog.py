# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The demo disk's cartridges: their numbers, files and slots. Turns an instrument's name into its
(cartridge, slot), and says how long a note of it sounds (for counting voices: voices.py). A name on more
than one cartridge of its engine needs the cartridge's name too.

  from catalog import CAT
  CAT.dx("TINE EP") -> (0xF0, 0x0F)     CAT.dx("STEEL DRUM", "PERC/FX")     CAT.ju("Space Pad")
  CAT.ju("Snare", "CHIP")               CAT.tb("Hard Acid")                 CAT.smp("KICK") -> (0x01, 0)
  CAT.fu("SPACE PAD")
"""
import importlib.util
import os

from common import DISK, read_json

# the CHIP engine's built-in tables, from their one source (fw/tools/gen_chip.py: the firmware's and the playground's)
_spec = importlib.util.spec_from_file_location(
    "gen_chip", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "fw", "tools", "gen_chip.py"))
CHIP_GEN = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(CHIP_GEN)


def chip_table(kind, name):
    """a CHIP table's values and its loop (-1: none), by its kind (vol, arp, pitch, duty, noise) and name"""
    return CHIP_GEN.parse(dict(CHIP_GEN.TABLES[kind])[name])

# number, type, path (from demo-disk/), the cartridge's name (12 characters), in numbers' order. The sampler
# cartridges and FUSION are the uploaded disk's, from 01; the synth cartridges are the firmware's built-in
# instruments, in the numbers F0-FF kept for them (specs/files/instruments.md, Built-in instruments)
CARTRIDGES = [
    (0x01, "sampler", "samples/drum-kit/samples.json", "DRUM KIT"),
    (0x02, "sampler", "samples/percussion/samples.json", "PERCUSSION"),
    (0x03, "sampler", "samples/band-orch/samples.json", "BAND/ORCH"),
    (0x04, "sampler", "samples/voices/samples.json", "VOICES"),
    (0x05, "sampler", "samples/bass/samples.json", "BASS"),
    (0x06, "fusion", "fusion/fusion/fusion.json", "FUSION"),
    (0xF0, "dx7", "patches/dx7/keys-organs.json", "KEYS/ORGANS"),
    (0xF1, "dx7", "patches/dx7/bells-plucks.json", "BELLS/PLUCKS"),
    (0xF2, "dx7", "patches/dx7/guitar-strings.json", "GUITAR/STRGS"),
    (0xF3, "dx7", "patches/dx7/winds-brass.json", "WINDS/BRASS"),
    (0xF4, "dx7", "patches/dx7/pads-choirs.json", "PADS/CHOIRS"),
    (0xF5, "dx7", "patches/dx7/leads-basses.json", "LEADS/BASSES"),
    (0xF6, "dx7", "patches/dx7/fm-drum-kit.json", "FM DRUM KIT"),
    (0xF7, "dx7", "patches/dx7/perc-fx.json", "PERC/FX"),
    (0xF8, "juno106", "patches/juno106/juno-106.json", "JUNO-106"),
    (0xF9, "chip", "patches/chip/chiptune.json", "CHIPTUNE"),
    (0xFA, "tb303", "patches/tb303/tb-303.json", "TB-303"),
]
BUILT_IN = 0xF0                          # cartridges and patterns from here up are the firmware's
FUSION_CART = 0x06
FUSION_FIRST = 0xE0                      # FUSION's patterns: E0 and up (the top of the user's numbers; songs' from 00)
FUSION_PATH = "fusion/fusion/fusion.json"

# the songs, in their numbers' order (from 00): (folder in songs/, name); songs.py writes them. The demo songs,
# then the DJ loops (DJ_LOOPS: one or two patterns that loop, after an intro in six of them, at most 4 voices,
# so two of them on DJ's decks never take each other's voices; a track an instrument, for LOOPS)
SONGS = [("punch-in", "PUNCH IN"), ("spice-road", "SPICE ROAD"), ("midnight-run", "MIDNIGHT RUN"),
         ("city-lights", "CITY LIGHTS"), ("acid-test", "ACID TEST"), ("adventure", "ADVENTURE"),
         ("trance-loop", "TRANCE LOOP"), ("breaks-loop", "BREAKS LOOP"), ("techno-loop", "TECHNO LOOP"),
         ("acid-loop", "ACID LOOP"), ("disco-loop", "DISCO LOOP"), ("chip-loop", "CHIP LOOP"),
         ("hip-hop-loop", "HIP-HOP LOOP"), ("dnb-loop", "DNB LOOP"), ("rock-loop", "ROCK LOOP"),
         ("metal-loop", "METAL LOOP"), ("electro-loop", "ELECTRO LOOP"), ("world-loop", "WORLD LOOP")]
DJ_LOOPS = ("trance-loop", "breaks-loop", "techno-loop", "acid-loop", "disco-loop", "chip-loop", "hip-hop-loop",
            "dnb-loop", "rock-loop", "metal-loop", "electro-loop", "world-loop")
DJ_VOICES = 4                            # a DJ loop's voices at most


# the DX7's carriers (the operators heard) in each of its 32 algorithms
DX7_CARRIERS = [(1, 3), (1, 3), (1, 4), (1, 4), (1, 3, 5), (1, 3, 5), (1, 3), (1, 3), (1, 3), (1, 4), (1, 4), (1, 3),
                (1, 3), (1, 3), (1, 3), (1,), (1,), (1,), (1, 4, 5), (1, 2, 4), (1, 2, 4, 5), (1, 3, 4, 5),
                (1, 2, 4, 5), (1, 2, 3, 4, 5), (1, 2, 3, 4, 5), (1, 2, 4), (1, 2, 4), (1, 3, 6), (1, 2, 3, 5),
                (1, 2, 3, 6), (1, 2, 3, 4, 5), (1, 2, 3, 4, 5, 6)]


def dx7_r4(op):
    """the parameter code of a DX7 operator's R4 (specs/files/commands.md)"""
    return 0x40 + 8 * (op - 1) + 7


# Measured on the engines: ms from full level to 60 dB under it, a DX7 envelope at each rate 0-99 (and a
# sample instrument's envelope, the same), and the Juno-106's DECAY (SUSTAIN 0) or RELEASE at each slider
# value 0-127 (both the same curve).
DX7_T60 = [60135] * 13 + [
    59800, 59800, 47855, 39875, 39875, 34165, 29905, 20040, 20040, 19910, 19910, 17075, 14945, 14945, 11955,
    11955, 9955, 8530, 8530, 7465, 5965, 5965, 4975, 4270, 4270, 3730, 3730, 2975, 2485, 2485, 2125, 1870, 1870,
    1485, 1240, 1240, 1060, 935, 935, 740, 740, 615, 535, 535, 460, 370, 370, 305, 265, 265, 230, 185, 185, 155,
    155, 130, 115, 115, 90, 75, 75, 65, 60, 60, 45, 45, 40, 35, 35, 30, 25, 25, 20, 15, 15, 15, 10, 10, 10, 10,
    10, 10, 10, 5, 5, 5, 5]
JUNO_T60 = [
    20, 20, 20, 25, 35, 45, 50, 55, 60, 70, 80, 100, 115, 150, 200, 300, 305, 315, 330, 345, 350, 365, 380, 400,
    410, 430, 450, 480, 495, 530, 560, 595, 625, 690, 710, 790, 840, 950, 1020, 1180, 1295, 1540, 1755, 2295,
    2295, 2330, 2360, 2395, 2425, 2475, 2525, 2590, 2640, 2690, 2785, 2835, 2900, 2950, 3050, 3130, 3195, 3295,
    3375, 3460, 3525, 4410, 4425, 4440, 4490, 4540, 4555, 4605, 4655, 4705, 4770, 4820, 4900, 4950, 5000, 5130,
    5145, 5195, 5295, 5360, 5410, 5605, 5670, 5785, 5835, 5965, 6030, 6145, 6260, 6360, 6440, 6980, 7015, 7095,
    7260, 7390, 7555, 7685, 7965, 8195, 8340, 8520, 9045, 9260, 9520, 9865, 10160, 10815, 11060, 11520, 11915,
    12685, 13210, 14010, 14960, 16075, 17090, 18420, 20040, 22385, 22910, 23415, 24050, 24050]


def dx7_release(rate):
    """seconds a DX7 envelope (or a sample instrument's) takes to fall from full to -60 dB at `rate`"""
    return DX7_T60[rate] / 1000


def juno_release(r):
    """seconds the Juno-106's envelope takes to fall from full to -60 dB at a DECAY or RELEASE of r"""
    return JUNO_T60[r] / 1000


def dx7_natural(eg):
    """seconds a DX7 envelope (rates, levels) sounds while held: None when it sustains, else until its
    decay stages have fallen to 0"""
    (r1, r2, r3, _), (l1, l2, l3, _) = eg
    if l2 == 0:
        return dx7_release(r2)
    if l3 == 0:
        return dx7_release(r2) * (99 - l2) / 99 + dx7_release(r3)
    return None


class Catalog:
    def __init__(self):
        self._files = {}

    def cart(self, number):
        for n, kind, path, name in CARTRIDGES:
            if n == number:
                return kind, path, name
        raise KeyError(number)

    def data(self, number):
        if number not in self._files:
            d = read_json(DISK / self.cart(number)[1])
            if self.cart(number)[0] == "chip":           # (a CHIP cartridge keeps its LEVELs apart: each slot's here)
                if len(d["levels"]) != len(d["instruments"]):
                    raise SystemExit(f"catalog: {self.cart(number)[1]}: {len(d['levels'])} LEVELs for "
                                     f"{len(d['instruments'])} instruments")
                d = dict(d, instruments=[dict(i, level=lv) for i, lv in zip(d["instruments"], d["levels"])])
            self._files[number] = d
        return self._files[number]

    def _find(self, kinds, name, cart=None):
        hits = []
        for n, kind, path, cname in CARTRIDGES:
            if kind not in kinds or cart is not None and cname != cart or not (DISK / path).exists():
                continue
            d = self.data(n)
            items = d.get("voices") or d.get("patches") or d.get("instruments") or d.get("slots") or []
            for i, it in enumerate(items):
                if it["name"].strip().upper() == name.upper():
                    hits.append((n, i))
        if len(hits) != 1:
            raise SystemExit(f"catalog: {name!r}: {'not found' if not hits else 'found twice: ' + str(hits)} "
                             f"in {cart or kinds}")
        return hits[0]

    def dx(self, name, cart=None):
        return self._find(("dx7",), name, cart)

    def ju(self, name, cart=None):
        return self._find(("juno106",), name, cart)

    def tb(self, name, cart=None):
        return self._find(("tb303",), name, cart)

    def ch(self, name, cart=None):
        return self._find(("chip",), name, cart)

    def smp(self, name, cart=None):
        return self._find(("sampler",), name, cart)

    def fu(self, name, cart=None):
        return self._find(("fusion",), name, cart)

    def kind(self, ins):
        return self.cart(ins[0])[0]

    def name(self, ins):
        d = self.data(ins[0])
        items = d.get("voices") or d.get("patches") or d.get("instruments") or d.get("slots")
        return items[ins[1]]["name"].strip()

    def carriers(self, ins):
        """a DX7 voice's carriers (operator numbers, 1-6)"""
        v = self.data(ins[0])["voices"][ins[1]]
        return DX7_CARRIERS[v["algorithm"] - 1]

    def sound(self, ins, note, params=None):
        """(seconds the note can sound at most while held, None for as long as held; seconds its release
        rings after it is let go). params: the track's parameter commands so far (code -> value)"""
        kind = self.kind(ins)
        d = self.data(ins[0])
        params = params or {}
        if kind == "dx7":
            v = d["voices"][ins[1]]
            heard = [op for op in self.carriers(ins) if params.get(0x40 + 8 * (op - 1), v["ops"][op - 1]["level"]) > 0]
            rel = [dx7_release(params.get(dx7_r4(op), v["ops"][op - 1]["eg"]["rates"][3])) for op in heard]
            nat = [dx7_natural([[params.get(0x40 + 8 * (op - 1) + 4 + k, v["ops"][op - 1]["eg"]["rates"][k])
                                 for k in range(4)], v["ops"][op - 1]["eg"]["levels"]]) for op in heard]
            natural = None if not nat or None in nat else max(nat)
            return natural, max(rel, default=0.005)
        if kind == "juno106":
            p = d["patches"][ins[1]]
            gate = params.get(0x56, 1 if p["vca"]["mode"] == "gate" else 0)
            if gate:
                return None, 0.005
            s = params.get(0x4D, p["env"]["s"])
            natural = juno_release(params.get(0x4C, p["env"]["d"])) if s == 0 else None
            return natural, juno_release(params.get(0x4E, p["env"]["r"]))
        if kind == "tb303":
            return None, 0.3
        if kind == "chip":                               # (specs/chip.md: frames, its VOL table, ENV, RELEASE)
            it = d["instruments"][ins[1]]
            fps = 50 if it["frames"] == 50 else 60
            vol = it["volume"]
            rel = params.get(0x57, vol["release"]) * 15 / fps or 0.005
            natural = None
            t = it.get("tables", {}).get("vol")
            if t:
                vals, loop = chip_table("vol", t[0])
                if loop < 0 and vals[-1] == 0:
                    natural = len(vals) * t[1] / fps
            if vol["env"] == "DOWN" and (vol["step"] or it["clock"] == "8BIT"):   # (a step: STEP / 64 s; 8BIT's
                per = (vol["step"] + 1) / 240 if it["clock"] == "8BIT" else vol["step"] / 64   # (STEP + 1) / 240 s)
                down = vol["vol"] * per
                natural = down if natural is None else min(natural, down)
            shape = it.get("psg_env", {}).get("shape", 9)
            if vol["env"] == "PSG" and (not shape & 8 or (shape & 1 and not (shape >> 2 & 1) ^ (shape >> 1 & 1))):
                period = it.get("psg_env", {}).get("period", 800)   # (shapes 0-7, 9 and F end after a round, the rest
                clk = {"PSG 1.00": 1000000, "PSG 1.79": 1789772, "16BIT": 2000000}.get(it["clock"], 1773400)  # go on)
                once = 256 * period / clk                       # (a round: 16 steps of 16 periods, or 32 of 8)
                natural = once if natural is None else min(natural, once)
            return natural, rel
        if kind == "sampler":
            it = d["instruments"][ins[1]]
            smp = next(s for s in d["samples"] if s["id"] == it["sample"])
            from compose import midi
            speed = 2 ** ((note - midi(it["base_note"])) / 12)
            rates = [params.get(0x48 + k, it["envelope"]["rates"][k]) for k in range(4)]
            levels = [params.get(0x4C + k, it["envelope"]["levels"][k]) for k in range(4)]
            rel = dx7_release(rates[3])
            env = dx7_natural([rates, levels])
            if it["loop"] != "off":
                return env, rel
            length = (it["end"] - it["start"]) / smp["rate"] / speed
            return length if env is None else min(length, env), rel
        raise SystemExit(f"catalog: no sound for a {kind} instrument")


CAT = Catalog()
