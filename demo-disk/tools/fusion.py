#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""FUSION, the demo disk's FUSION cartridge (our own, OWN_LICENSE): instruments made of a pattern, row r
playing for MIDI note r, every cell of the row sounding when that note plays. Six, of the disk's voices:

  SPACE PAD    two pads on every key, a deep bass on every key, a bell from C-4
  DUB CHORDS   a minor 9 chord, rolled, and a sub bass on every key
  SPLIT        a held bass under C-4, a space lead from C-4
  CHIPIN       a chip arpeggio on any key, in C minor pentatonic over a C bass
  COIN         a game's coin on any key: a fourth under for 5 ticks, then the key's note
  DRUM KIT     a drum on every key, laid out as General MIDI's kit with the kick on C-4, combos under it, the
               DRUM KIT sampler's drums alone, TB-303 drums and hand drums over it (KIT: drum_kit.json)

  fusion.py        -> fusion/fusion/fusion.json + patterns.json

Its patterns take the pool's numbers from FIRST up, a track a voice of each layer (DRUM KIT's one track): rows
0C-7F (C-0 to G-9), the notes the keys reach. Every cell is a voice: the songs count them (voices.py).
"""
from catalog import CARTRIDGES, CAT, FUSION_FIRST, FUSION_PATH
from pathlib import Path
from common import DISK, OWN_LICENSE, fmt, read_json, write_json
from compose import Pattern, Pool, midi

FIRST = FUSION_FIRST                     # the cartridge's patterns: E0 and up (the disk's, apart from the songs')
ROWS = range(0x0C, 0x80)                 # the rows a key plays (00-0B and 80-FF never do)

# A layer: (cartridge, instrument, volume 0-127, zones[, extra]). A zone: (lowest key, highest key, offsets
# [, note delays in ticks, one an offset (EDx)[, its arpeggio (x, y): 0xy]]): a key in a zone plays the
# instrument at the key + each offset. extra: "retrig" x (E9x), "cut" x (ECx), "vol" (x, y) (Axy). A tick is
# 2.5 s / TEMPO (20 ms at 125). The volumes are the layers' balance; the slot's LEVEL sets the whole.
ALL = [(0, 127, [0])]
LOW, HIGH = 59, 60                       # the split: under C-4, from C-4


def up(n, lo=0, hi=127):
    return [(lo, hi, [n])]


def fold(octave=60):
    """every key plays the note of its name in one octave (octave .. octave + 11): a bass the same on every key"""
    return [(12 * o, min(127, 12 * o + 11), [octave - 12 * o]) for o in range(11)]


# CHIPIN's scale: every key's note moved to the nearest of C minor pentatonic (C Eb F G Bb: no two of them
# clash over a C bass, in any order; B goes up to C), and on it the chip arpeggio, a chord of the same five
# notes (C 037 C Eb G, Eb 047 Eb G Bb, F 057 F Bb C, G 038 G Bb Eb, Bb 057 Bb Eb F)
PENTA = [0, -1, 1, 0, 1, 0, 1, 0, -1, 1, 0, 1]
CHORD = {0: (3, 7), 3: (4, 7), 5: (5, 7), 7: (3, 8), 10: (5, 7)}


def scale(shift=0, delay=0):
    """every key its own row, its note moved into C minor pentatonic (shift semitones up), its chord's 0xy"""
    return [(k, k, [PENTA[k % 12] + shift], [delay], CHORD[(k + PENTA[k % 12]) % 12])
            for k in range(128) if 0 <= k + PENTA[k % 12] + shift <= 127]


def fixed(note):
    """every key the same note"""
    return [(k, k, [note - k], [0]) for k in range(128)]


# (slot name, what it plays, LEVEL dB, layers). Measured on the engines: Juno-106 Sub Bass and Drone sound two
# octaves under the note they play (16' and the sub oscillator), so played C-4 to B-4 they sound C-2 to B-2;
# DX7 FM BASS one octave. Each LEVEL brings the louder of C-3 and E-4 to the loudness of one instrument
# (levels.py), its peak under -6 dB.
FUSIONS = [
    # Drone swells in and rings on as Space Pad does (both ~1.4 s to full)
    ("SPACE PAD", "two pads and a deep bass on every key, a bell from C-4", -4.0, [
        ("JUNO-106", "Space Pad", 110, ALL),
        ("PADS/CHOIRS", "GLASS PAD", 70, up(12)),
        ("JUNO-106", "Drone", 110, fold()),
        ("BELLS/PLUCKS", "SPACE BELL", 45, up(12, HIGH))]),
    # rolled with the cells' note delays, whole ticks: root and fifth at once, seventh and ninth a tick later,
    # the third on top two (0, 21, 42 ms at 120)
    ("DUB CHORDS", "a minor 9 chord, rolled, and a sub bass on every key", -6.0, [
        ("JUNO-106", "Dub Chord", 75, [(0, 127, [0, 7, 10, 14, 15], [0, 0, 1, 1, 2])]),
        ("JUNO-106", "Sub Bass", 70, fold())]),
    # held 3 s: Demo Bass holds its level after its first 0.5 s, Sub Bass loses 6 dB; no lead rings after its
    # release, so Shimmer, a pad that swells in and rings ~1 s, is the lead's halo; THEREMIN: a sine with ~35
    # cents of vibrato
    ("SPLIT", "a held bass under C-4, a space lead from C-4", -1.0, [
        ("JUNO-106", "Demo Bass", 110, up(0, 0, LOW)),
        ("JUNO-106", "Sub Bass", 70, up(12, 0, LOW)),
        ("LEADS/BASSES", "THEREMIN", 110, up(0, HIGH)),
        ("JUNO-106", "Soft Lead", 70, up(0, HIGH)),
        ("JUNO-106", "Shimmer", 55, up(12, HIGH))]),
    # the lead (the console's 25 % pulse) and its echo (a 12.5 % pulse, 3 ticks later, fading: the consoles' echo)
    # arpeggiate as long as the key is held; the triangle bass on C-3 whatever the key; a blip at the press (an
    # octave flick), a ratchet: retriggered every 3 ticks, cut after the third; its LEVEL as AUTO LEVEL measures it
    # (the layers on CHIP)
    ("CHIPIN", "a chip arpeggio on any key, in C minor pentatonic over a C bass", -2.0, [
        ("CHIPTUNE", "8BIT LEAD", 100, scale()),
        ("CHIPTUNE", "8BIT ECHO", 40, scale(0, 3), {"vol": (0, 1)}),
        ("CHIPTUNE", "TRI BASS", 85, fixed(48)),
        ("CHIPTUNE", "BLIP", 50, scale(), {"retrig": 3, "cut": 9})]),
    # the coin of the console's games, a 50 % square: a fourth under the key for 5 ticks (EC5), then the key's note
    # (ED5), ringing out (5 ticks: 83 ms at 150 BPM, the console's 5 frames); one COIN PULSE sounding at a time,
    # so the LEVEL is COIN PULSE's own
    ("COIN", "a game's coin on any key: a fourth under for 5 ticks, then the key's note", 0.0, [
        ("CHIPTUNE", "COIN PULSE", 127, [(0, 127, [-5])], {"cut": 5}),
        ("CHIPTUNE", "COIN PULSE", 127, [(0, 127, [0], [5])])]),
]

# DRUM KIT: a drum, or a combo of drums, on every key from F-2 to G-8; its cells are kept as made in drum_kit.json
# (each key's layers, a track each, and why). The keys F-3 to G-5 (OCT 0): General MIDI's kit moved up two octaves,
# so GM's 35-55 fall on B-3 to G-5 and the kick on C-4 (the snares and the clap over it, the toms on the white keys
# between the hats on the black ones, the cymbals at the top), hand percussion on the six keys under it; the octave
# under the keys combos (a fill, a flam, DUM-TSS ...), the octave over them the DRUM KIT sampler's drums alone (a
# drum a key, on the kit's letters), the next TB-303 drums, the one over that hand drums and chimes. The synth
# cartridges' drums and the samplers' (DRUM KIT, PERCUSSION: every sample of the two plays somewhere) in layers. Every drum rings out the same however short the tap (its release made its decay by
# parameter commands in its cell: one_shot's), but CHIME RISE (a swell, played held); the drums even as a kit,
# measured on the engines as LEVELs are (levels.py), in tiers (the cymbals and hats a few dB under the drums).
KIT = read_json(Path(__file__).resolve().parent / "drum_kit.json")
KIT_LEVEL = KIT["level"]                 # its LEVEL: the louder of C-3 and E-4 at one instrument's loudness

FIND = {"dx7": CAT.dx, "juno106": CAT.ju, "chip": CAT.ch, "tb303": CAT.tb, "sampler": CAT.smp}


def instrument(cart, name):
    kind = next(k for _, k, _, c in CARTRIDGES if c == cart)
    return FIND[kind](name, cart)


def pattern(pool, n, what, layers):
    """the FUSION's pattern: a track for each offset of each layer"""
    width = [max(len(z[2]) for z in zones) for _, _, _, zones, *_ in layers]
    p = Pattern(pool.take("FUSION", FIRST + n), sum(width), 128, what, edit_step=0)
    first = 1
    for (cart, name, vol, zones, *extra), w in zip(layers, width):
        ins, extra = instrument(cart, name), (extra[0] if extra else {})
        for r in ROWS:
            zone = next((z for z in zones if z[0] <= r <= z[1]), None)
            if zone is None:
                continue
            lo, hi, offs, *more = zone
            delays = more[0] if more else [0] * len(offs)
            for k, off in enumerate(offs):
                if not 12 <= r + off <= 127:
                    continue
                cmds = []
                if len(more) > 1:
                    cmds.append(f"0{more[1][0]:X}{more[1][1]:X}")
                if delays[k]:
                    cmds.append(f"ED{delays[k]:X}")
                if "retrig" in extra:
                    cmds.append(f"E9{extra['retrig']:X}")
                if "cut" in extra:
                    cmds.append(f"EC{extra['cut']:X}")
                if "vol" in extra:
                    cmds.append(f"A{extra['vol'][0]:X}{extra['vol'][1]:X}")
                p.note(first + k, r, r + off, ins, vol, *cmds)
        first += w
    return p


def one_shot(ins):
    """parameter commands that make a drum ring out the same whenever its note ends (the key let go, the track's
    next note), as a drum machine's do: a DX7 operator's R4 set to the rate it falls at while held (its stage to
    level 0), an operator held up (a modulator) falling with the slowest of those, the carriers first (slowest
    first), then the modulators (loudest first), four at most; a Juno-106's ENV R set to its DECAY (SUSTAIN 0); a
    CHIP instrument's RELEASE at 15 (its VOL table or envelope then ends it, as when held); a sample's R4 set as a
    DX7 operator's, or to 0 when its envelope holds it up and it does not loop (the sample plays on to its END)"""
    d = CAT.data(ins[0])
    if CAT.kind(ins) == "sampler":
        it = d["instruments"][ins[1]]
        (r1, r2, r3, r4), (l1, l2, l3, _) = it["envelope"]["rates"], it["envelope"]["levels"]
        fall = r1 if l1 == 0 else r2 if l2 == 0 else r3 if l3 == 0 else None
        if fall is None and it["loop"] != "off":
            return []                                      # held up and looping: only its release can end it
        want = 0 if fall is None else fall
        return [] if r4 == want else [f"*4B:{want:02X}"]   # R4 (0x4B)
    if CAT.kind(ins) == "chip":
        return [] if d["instruments"][ins[1]]["volume"]["release"] == 15 else ["*57:0F"]   # RELEASE (0x57) = 15
    if CAT.kind(ins) == "juno106":
        p = d["patches"][ins[1]]
        if p["vca"]["mode"] != "env" or p["env"]["s"] or p["env"]["r"] == p["env"]["d"]:
            return []
        return [f"*4E:{p['env']['d']:02X}"]                # ENV R (0x4E) = ENV D
    v = d["voices"][ins[1]]
    falls = {}                                             # operator -> the rate it falls to 0 at, held; None: held up
    for op, o in enumerate(v["ops"], 1):
        (r1, r2, r3, _), (l1, l2, l3, _) = o["eg"]["rates"], o["eg"]["levels"]
        if o["level"]:
            falls[op] = r2 if l2 == 0 else r3 if l3 == 0 else None
    carriers = sorted((falls[op], op) for op in CAT.carriers(ins) if op in falls and falls[op] is not None)
    if not carriers:
        return []
    slowest = carriers[0][0]
    mods = sorted(((-v["ops"][op - 1]["level"], op) for op in falls if op not in CAT.carriers(ins)))
    want = [(op, rate) for rate, op in carriers] + [(op, slowest if falls[op] is None else falls[op]) for _, op in mods]
    want = [(op, rate) for op, rate in want if abs(v["ops"][op - 1]["eg"]["rates"][3] - rate) > 2]
    return [f"*{0x40 + 8 * (op - 1) + 7:02X}:{rate:02X}" for op, rate in want[:4]]     # the operator's R4


def kit_pattern(pool, n, what):
    """DRUM KIT's pattern: each key's row its layers (KIT), a track each, their cells as made"""
    p = Pattern(pool.take("FUSION", FIRST + n), KIT["tracks"], 128, what, edit_step=0)
    rows = [midi(k["key"]) for k in KIT["keys"]]
    if len(set(rows)) != len(rows) or not all(r in ROWS for r in rows):
        raise SystemExit("fusion: DRUM KIT: a key twice, or one no key reaches")
    for k in KIT["keys"]:
        if not 1 <= len(k["layers"]) <= KIT["tracks"]:
            raise SystemExit(f"fusion: DRUM KIT {k['key']}: {len(k['layers'])} layers")
        for t, ly in enumerate(k["layers"], 1):
            ins = instrument(ly["cartridge"], ly["instrument"])
            code = {"sampler": "*4B", "chip": "*57"}.get(CAT.kind(ins))
            got = [c for c in ly["commands"] if code and c.startswith(code)]
            roll = any(c.startswith("E9") for c in ly["commands"])
            if got and not roll and got != one_shot(ins):  # (a sample's or a CHIP's one-shot as the rule makes it
                raise SystemExit(f"fusion: DRUM KIT {k['key']} {ly['instrument']}: {got}, the instrument's "  # now;
                                 f"one-shot is {one_shot(ins)}")  # none: held, or over before a note ends)
            p.note(t, midi(k["key"]), ly["note"], ins, ly["volume"], *ly["commands"])
    return p


def main():
    pool = Pool()
    slots = [(name, level, pattern(pool, n, what, layers)) for n, (name, what, level, layers) in enumerate(FUSIONS)]
    slots.append(("DRUM KIT", KIT_LEVEL, kit_pattern(pool, len(slots), "a drum on every key, GM's kit with the kick "
                                                     "on C-4; combos under it, the drum samples, TB-303 drums and "
                                                     "hand drums over it")))
    folder = (DISK / FUSION_PATH).parent
    write_json(DISK / FUSION_PATH, {
        "format": fmt("fusion"), "version": 1, "name": "FUSION", "license": OWN_LICENSE,
        "slots": [{"name": name, "pattern": p.number, "level": level} for name, level, p in slots],
        "patterns": "patterns.json"})
    write_json(folder / "patterns.json", {
        "format": fmt("patterns"), "version": 2, "patterns": [p.json() for _, _, p in slots]})
    print(f"fusion: FUSION, {len(slots)} instruments, patterns {FIRST:02X}-{FIRST + len(slots) - 1:02X}")


if __name__ == "__main__":
    main()
