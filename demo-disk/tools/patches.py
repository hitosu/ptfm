#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The demo disk's patch cartridges: 8 DX7 cartridges (dx7_carts.py: our own voices and the free banks'),
JUNO-106 (Juno-106 patches of our own, written here: no Juno-106 bank under a free licence exists), TB-303
(presets of our own, and mpc-vst-303's). Every slot's LEVEL from levels.py. (CHIP below, chiptune on the
Juno-106, was a cartridge before the CHIP engine had F9: patches/juno106/chip.json is its last output, kept with
the list for the open question of which of them join JUNO-106; it is no longer written.)

  patches.py --dxheader FILE --fm6 REPO --dxbank REPO --classix FILE --ym2612 REPO --x16 REPO --mpc303 REPO
      -> patches/dx7/<cartridge>.json (8), patches/juno106/juno-106.json, patches/tb303/tb-303.json

The sources, data only (nothing in them runs; fm_banks.py reads them):
  --dxheader  the DX header bank's dx7_bank.h (SOURCES)         --fm6      a checkout of the FM6 bank (SOURCES)
  --dxbank    a checkout of the DX bank (SOURCES)               --classix  Idiomdrottning's classix (.dx7)
  --ym2612    a checkout of ym2612-patches                      --x16      a checkout of the X16 ROM (its audio/)
  --mpc303    a checkout of mpc-vst-303 (its presets.json)

A cartridge's licence is ours (the choice of its voices and their names); a voice, patch or preset taken from
elsewhere carries its own licence and its source (../README.md: Licences).

A Juno-106 patch below: name, then the panel from left to right:
  LFO rate delay | DCO lfo pwm (pwm source: "l"fo / "m"anual) range pulse saw sub noise |
  HPF | VCF freq res env (polarity +/-) lfo kybd | VCA env/gate level | ENV a d s r | chorus
Sliders 0-127, HPF 0-3. The ENV's release, in the times the engine takes to fall 60 dB (catalog.py,
JUNO_T60): basses and plucks 80-200 ms (R 10-14), leads and keys 150-500 ms (R 13-27), bells about 1.5 s,
pads and textures 2.4 s (R 47). Measured on the engine: Sub Bass and Drone sound two octaves under their
note (16' and the sub oscillator); the VCF ringing by itself (RES 127) cannot be tuned (a FREQ step moves it
~130 cents), so melodic sounds take their pitch from the DCO; a VCF ring starts at FREQ + ENV, so a large ENV
starts too high to ring at once.
"""
import argparse
from pathlib import Path

import dx7_voices
import fm_banks
from common import DISK, OWN_LICENSE, fmt, read_json, sha_file, write_json
from dx7_carts import CARTRIDGES as DX7_CARTRIDGES
from levels import LEVELS

# where a voice from elsewhere comes from: the licence (id, the url stating it, the credit), the file it is
# read from (within its folder) and the commit the demo disk took it at
SOURCES = {
    "dxheader": ("MIT", "https://github.com/ip2k/lunar-modulator/blob/c63b51a678a60b83a4a61985ce0e5eac51b123d4/LICENSE",
                 "Lunar Modulator's DX7 voices, Copyright (c) 2026 ip2k", "engines/src/dx7_bank.h",
                 "c63b51a678a60b83a4a61985ce0e5eac51b123d4"),
    "fm6": ("GPL-3.0-only", "https://github.com/hugelton/Felucca/blob/ac6bfcbd76ee3de79eb461719e8d123b5512aa3c/LICENSE",
            "Felucca's FM6 factory patches, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments",
            fm_banks.FM6_FILE, "ac6bfcbd76ee3de79eb461719e8d123b5512aa3c"),
    "dxbank": ("GPL-3.0-only", "https://github.com/zvenson/dxsloop/blob/17e395b878f0e112d5192b9f0250dcaa11808674/LICENSING.md",
               "sloopDX's factory voices, Copyright (C) 2026 Sven Trogus", fm_banks.DXBANK_FILE,
               "17e395b878f0e112d5192b9f0250dcaa11808674"),
    "classix": ("CC-BY-SA-4.0", "https://idiomdrottning.org/dx7-old", "classix, a DX7 bank by Idiomdrottning",
                "idiomdrottning-classix.dx7", None),
    "md": ("CC0-1.0", "https://github.com/ulalume/ym2612-patches/blob/693380920326ed3fd28c962843ecb4059e33f79f/LICENSE",
           "ym2612-patches by ulalume: Mega Drive patches, converted here from the YM2612's 4 operators", None,
           "693380920326ed3fd28c962843ecb4059e33f79f"),
    "x16": ("BSD-2-Clause", "https://github.com/X16Community/x16-rom/blob/fbe32a60704f555c0515fe8d3cb76857eb56d670/LICENSE.md",
            "the Commander X16 ROM's FM patches (its audio library, X16Community), converted here from the YM2151's "
            "4 operators", fm_banks.X16_FILE, "fbe32a60704f555c0515fe8d3cb76857eb56d670"),
    "mpc303": ("GPL-3.0-only", "https://github.com/saustin2010/mpc-vst-303/blob/2abe95b797426a85ca50747badaf5a0e7cb17d30/LICENSE",
               "mpc-vst-303's presets by saustin2010, for Robin Schmidt's Open303", "presets.json",
               "2abe95b797426a85ca50747badaf5a0e7cb17d30"),
}

# name,            lfo        dco: lfo pwm src range pulse saw sub noise  hpf  vcf: freq res env pol lfo kybd   vca      env a d s r        chorus
JUNO = [
    # A11-A21 keys: pianos, electric pianos and clavs; A22-A26 organs
    ("Syn Piano",     (40, 0),   (0, 40, "m", "8'", 1, 0, 60, 0),     1, (46, 10, 50, "+", 0, 70),      ("e", 100), (0, 72, 0, 13),      "off"),
    ("Combo Org",     (90, 0),   (10, 30, "m", "8'", 1, 0, 50, 0),    1, (90, 0, 0, "+", 0, 90),        ("g", 100), (0, 60, 100, 20),    "I"),
    ("Toy Piano",     (50, 0),   (0, 50, "m", "4'", 1, 0, 0, 0),      1, (80, 0, 40, "+", 0, 100),      ("e", 100), (0, 40, 0, 30),      "off"),
    ("Juno Keys",     (40, 0),   (0, 0, "m", "4'", 1, 0, 30, 0),      0, (26, 0, 56, "+", 0, 60),       ("e", 110), (0, 82, 30, 20),     "I"),
    ("Harpsi",        (40, 0),   (0, 92, "m", "4'", 1, 0, 0, 0),      3, (110, 20, 30, "+", 0, 70),     ("e", 108), (0, 56, 0, 10),      "off"),
    ("Clavi",         (40, 0),   (0, 100, "m", "16'", 1, 0, 0, 0),    2, (16, 10, 86, "+", 0, 30),      ("g", 110), (0, 40, 40, 10),     "off"),
    ("Mellow",        (60, 60),  (10, 30, "m", "8'", 1, 0, 0, 0),     0, (40, 10, 20, "+", 0, 80),      ("e", 96),  (10, 60, 100, 16),   "I"),
    ("Wah Clav",      (40, 0),   (0, 80, "m", "8'", 1, 0, 0, 0),      2, (30, 90, 90, "+", 0, 60),      ("e", 104), (0, 40, 30, 10),     "off"),
    ("Bell EP",       (50, 0),   (0, 20, "m", "8'", 1, 0, 40, 0),     1, (48, 20, 45, "+", 0, 90),      ("e", 100), (0, 75, 30, 45),     "I"),
    ("Perc Organ",    (60, 0),   (0, 0, "m", "8'", 1, 0, 90, 0),      1, (80, 0, 10, "+", 0, 80),       ("g", 90),  (0, 30, 0, 0),       "I"),
    ("Pipe Organ",    (60, 0),   (0, 20, "m", "4'", 1, 1, 50, 0),     1, (70, 0, 0, "+", 0, 90),        ("g", 84),  (0, 0, 127, 10),     "II"),
    ("Jazz Organ",    (60, 0),   (0, 0, "m", "8'", 1, 0, 80, 0),      0, (70, 0, 0, "+", 0, 90),        ("g", 90),  (0, 0, 127, 6),      "I"),
    ("Rock Organ",    (50, 0),   (0, 0, "m", "8'", 1, 1, 80, 0),      2, (100, 0, 0, "+", 0, 90),       ("g", 100), (0, 60, 100, 20),    "II"),
    ("Accordion",     (50, 0),   (0, 25, "m", "8'", 1, 1, 0, 0),      2, (80, 0, 0, "+", 0, 90),        ("g", 100), (0, 60, 100, 20),    "II"),
    # A27-A35 mallets and bells
    ("Mallet",        (40, 0),   (0, 0, "m", "4'", 1, 0, 0, 0),       0, (30, 30, 52, "+", 0, 80),      ("e", 112), (0, 36, 0, 27),      "I"),
    ("Bell",          (40, 0),   (0, 0, "m", "4'", 1, 0, 0, 0),       0, (50, 127, 0, "+", 0, 127),     ("e", 100), (0, 70, 0, 47),      "I"),
    ("Chime",         (40, 0),   (0, 100, "m", "4'", 1, 0, 0, 10),    2, (80, 110, 10, "+", 0, 127),    ("e", 104), (0, 56, 0, 41),      "II"),
    ("Bell Pad",      (40, 0),   (0, 50, "l", "4'", 1, 0, 0, 0),      1, (60, 60, 40, "+", 0, 110),     ("e", 86),  (0, 90, 60, 47),     "II"),
    ("Marimba",       (50, 0),   (0, 0, "m", "8'", 1, 0, 0, 0),       1, (30, 30, 30, "+", 0, 127),     ("e", 100), (0, 35, 0, 30),      "off"),
    ("Music Box",     (50, 0),   (0, 90, "m", "4'", 1, 0, 0, 0),      1, (90, 0, 0, "+", 0, 100),       ("e", 100), (0, 60, 0, 60),      "off"),
    ("Xylophone",     (50, 0),   (0, 20, "m", "4'", 1, 0, 0, 0),      1, (50, 20, 60, "+", 0, 110),     ("e", 100), (0, 22, 0, 18),      "off"),
    # A36-A47 plucks, a harp and a koto
    ("Pluck",         (40, 0),   (0, 30, "m", "8'", 1, 1, 0, 0),      1, (30, 50, 66, "+", 0, 70),      ("e", 100), (0, 32, 0, 12),      "II"),
    ("Hard Pluck",    (40, 0),   (0, 30, "l", "8'", 1, 1, 0, 0),      0, (40, 40, 80, "+", 0, 80),      ("e", 100), (0, 40, 0, 14),      "II"),
    ("Bubble",        (40, 0),   (0, 0, "m", "4'", 1, 0, 0, 0),       0, (30, 110, 70, "+", 0, 90),     ("e", 100), (0, 30, 0, 16),      "II"),
    ("Psy Pluck",     (40, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       0, (24, 90, 90, "+", 0, 70),      ("e", 100), (0, 26, 0, 13),      "off"),
    ("Arp Saw",       (40, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       0, (40, 46, 60, "+", 0, 70),      ("e", 100), (0, 36, 10, 11),     "I"),
    ("Seq Lead",      (40, 0),   (0, 40, "m", "8'", 1, 1, 0, 0),      0, (40, 70, 64, "+", 0, 70),      ("e", 100), (0, 30, 20, 10),     "I"),
    ("Prog Pluck",    (40, 0),   (0, 30, "l", "8'", 1, 1, 0, 0),      0, (34, 40, 70, "+", 0, 70),      ("e", 104), (0, 30, 0, 13),      "II"),
    ("Space Arp",     (40, 0),   (0, 50, "m", "4'", 1, 1, 0, 0),      1, (54, 60, 50, "+", 0, 80),      ("e", 100), (0, 34, 10, 16),     "II"),
    ("Harp",          (50, 0),   (0, 30, "m", "8'", 1, 0, 0, 0),      1, (50, 0, 50, "+", 0, 90),       ("e", 100), (0, 70, 0, 50),      "I"),
    ("Koto",          (50, 0),   (0, 70, "m", "8'", 1, 0, 0, 0),      1, (60, 50, 60, "+", 0, 90),      ("e", 100), (0, 50, 0, 30),      "off"),
    # A48-A54 strings
    ("Slow Strgs",    (52, 30),  (0, 52, "l", "8'", 1, 1, 0, 0),      1, (76, 6, 6, "+", 0, 90),        ("e", 76),  (60, 60, 110, 16),   "II"),
    ("Strings 80",    (50, 40),  (0, 60, "l", "8'", 1, 1, 0, 0),      1, (72, 8, 14, "+", 0, 90),       ("e", 80),  (40, 60, 110, 27),   "II"),
    ("String Box",    (54, 20),  (6, 60, "l", "8'", 1, 1, 0, 0),      1, (80, 4, 0, "+", 0, 90),        ("e", 80),  (50, 60, 127, 37),   "II"),
    ("Juno Strgs",    (40, 0),   (0, 60, "l", "8'", 1, 1, 0, 0),      1, (64, 10, 10, "+", 0, 60),      ("e", 100), (50, 80, 110, 50),   "II"),
    ("Pizz Strgs",    (50, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (50, 0, 50, "+", 0, 70),       ("e", 100), (0, 38, 0, 20),      "I"),
    # A55-A58 choirs
    ("Aah Voices",    (60, 44),  (10, 30, "m", "8'", 1, 0, 0, 0),     2, (56, 62, 6, "+", 0, 60),       ("e", 100), (52, 62, 120, 34),   "I"),
    ("Vox Pad",       (50, 60),  (6, 40, "m", "8'", 1, 1, 0, 0),      2, (50, 100, 0, "+", 40, 60),     ("e", 90),  (80, 90, 110, 47),   "I"),
    ("Ooh Choir",     (30, 60),  (6, 70, "l", "8'", 1, 0, 30, 0),     1, (46, 30, 0, "+", 0, 70),       ("e", 100), (60, 90, 110, 55),   "II"),
    ("Air Choir",     (34, 0),   (0, 80, "l", "8'", 1, 0, 0, 40),     1, (56, 40, 0, "+", 0, 70),       ("e", 100), (70, 90, 110, 60),   "I"),
    # A61-A67 brass, A68-A71 reeds and flutes
    ("Syn Brass",     (40, 0),   (0, 0, "m", "8'", 0, 1, 20, 0),      0, (30, 16, 70, "+", 0, 70),      ("e", 100), (10, 62, 72, 13),    "I"),
    ("Brass Stab",    (40, 0),   (0, 30, "m", "16'", 1, 1, 40, 0),    0, (26, 26, 80, "+", 0, 60),      ("e", 98),  (4, 46, 30, 10),     "I"),
    ("Soft Horns",    (40, 0),   (4, 0, "m", "8'", 0, 1, 0, 0),       1, (40, 6, 36, "+", 0, 60),       ("e", 96),  (36, 70, 92, 27),    "II"),
    ("Neon Brass",    (40, 0),   (0, 20, "m", "8'", 1, 1, 30, 0),     0, (32, 10, 74, "+", 0, 70),      ("e", 100), (12, 58, 80, 13),    "I"),
    ("Poly Brass",    (40, 0),   (0, 0, "m", "8'", 0, 1, 20, 0),      0, (36, 14, 80, "+", 0, 70),      ("e", 100), (8, 54, 70, 12),     "I"),
    ("Slow Brass",    (50, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (30, 20, 70, "+", 0, 60),      ("e", 100), (70, 80, 100, 30),   "I"),
    ("Jump Brass",    (50, 0),   (0, 40, "m", "8'", 1, 1, 0, 0),      1, (50, 0, 50, "+", 0, 70),       ("e", 100), (8, 60, 90, 25),     "II"),
    ("Oboe",          (68, 45),  (8, 85, "m", "8'", 1, 0, 0, 0),      2, (58, 45, 0, "+", 0, 100),      ("e", 100), (12, 60, 100, 18),   "off"),
    ("Flute Lead",    (70, 50),  (10, 10, "m", "8'", 1, 0, 0, 20),    1, (40, 30, 0, "+", 0, 100),      ("e", 100), (20, 60, 100, 20),   "off"),
    # A72-A88 pads
    ("Warm Pad",      (38, 0),   (0, 72, "l", "8'", 1, 1, 30, 0),     0, (50, 16, 20, "+", 0, 60),      ("e", 84),  (72, 82, 100, 47),   "II"),
    ("Glass Pad",     (30, 0),   (0, 90, "l", "4'", 1, 0, 0, 0),      1, (66, 70, 16, "+", 0, 80),      ("e", 82),  (82, 92, 92, 47),    "II"),
    ("Dark Pad",      (24, 0),   (0, 50, "l", "8'", 1, 1, 60, 0),     0, (30, 30, 36, "+", 0, 50),      ("e", 90),  (86, 100, 82, 47),   "I"),
    ("Sweep Pad",     (44, 0),   (0, 60, "l", "8'", 1, 1, 0, 0),      0, (24, 82, 70, "+", 0, 60),      ("e", 82),  (92, 112, 60, 47),   "II"),
    ("Space Pad",     (20, 0),   (0, 80, "l", "8'", 1, 1, 0, 0),      1, (56, 40, 30, "+", 20, 60),     ("e", 84),  (100, 110, 100, 47), "II"),
    ("Drone",         (10, 0),   (0, 60, "l", "16'", 1, 1, 90, 10),   0, (34, 60, 10, "+", 30, 40),     ("e", 88),  (110, 127, 127, 47), "I"),
    ("Shimmer",       (70, 0),   (0, 100, "l", "4'", 1, 0, 0, 0),     3, (90, 40, 0, "+", 0, 100),      ("e", 80),  (90, 100, 100, 47),  "II"),
    ("Dream Pad",     (30, 0),   (0, 70, "l", "8'", 1, 1, 20, 0),     1, (58, 24, 24, "+", 0, 70),      ("e", 84),  (80, 90, 110, 47),   "II"),
    ("Prog Pad",      (36, 0),   (0, 70, "l", "8'", 1, 1, 0, 0),      1, (50, 20, 30, "+", 10, 70),     ("e", 86),  (70, 90, 110, 47),   "II"),
    ("Phase Pad",     (24, 0),   (0, 90, "l", "8'", 1, 1, 30, 0),     1, (66, 24, 10, "+", 20, 80),     ("e", 84),  (80, 90, 120, 47),   "II"),
    ("Str Swell",     (36, 0),   (0, 64, "l", "8'", 1, 1, 0, 0),      1, (50, 0, 30, "+", 0, 60),       ("e", 100), (95, 100, 127, 60),  "II"),
    ("PWM Pad",       (25, 0),   (0, 80, "l", "8'", 1, 0, 0, 0),      1, (70, 0, 0, "+", 0, 60),        ("e", 100), (60, 100, 120, 70),  "II"),
    ("Saw Pad",       (12, 0),   (0, 0, "m", "8'", 0, 1, 40, 0),      1, (50, 20, 15, "+", 20, 80),     ("e", 100), (70, 100, 120, 70),  "I"),
    # (VCF LFO 30 swung it from -34 to -57 dB: 12)
    ("Night Pad",     (15, 0),   (0, 40, "m", "8'", 1, 1, 0, 0),      1, (48, 50, 0, "+", 12, 60),      ("e", 100), (90, 110, 120, 80),  "II"),
    ("UFO",           (90, 0),   (40, 0, "m", "8'", 0, 1, 0, 0),      1, (50, 100, 0, "+", 100, 80),    ("e", 100), (30, 80, 127, 60),   "II"),
    # B11-B16 stabs and chords
    ("Dub Chord",     (40, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (36, 50, 50, "+", 0, 60),      ("e", 98),  (0, 38, 0, 13),      "I"),
    ("Tech Stab",     (40, 0),   (0, 50, "m", "8'", 1, 1, 30, 0),     0, (30, 70, 90, "+", 0, 60),      ("e", 100), (0, 30, 0, 10),      "off"),
    ("Deep Chord",    (40, 0),   (0, 40, "m", "8'", 1, 1, 20, 0),     0, (40, 30, 40, "+", 0, 70),      ("e", 92),  (2, 60, 30, 16),     "II"),
    ("Dark Stab",     (40, 0),   (0, 40, "m", "8'", 1, 1, 0, 0),      0, (40, 60, 64, "+", 0, 60),      ("e", 104), (0, 44, 0, 14),      "I"),
    ("House Stab",    (50, 0),   (0, 20, "m", "8'", 1, 1, 0, 0),      1, (60, 20, 50, "+", 0, 80),      ("e", 100), (0, 40, 0, 30),      "II"),
    ("Rave Stab",     (100, 0),  (0, 70, "l", "8'", 1, 1, 0, 0),      1, (70, 60, 60, "+", 0, 80),      ("e", 100), (0, 50, 10, 30),     "I"),
    # B17-B33 leads
    ("Acid Saw",      (40, 0),   (0, 0, "m", "16'", 0, 1, 0, 0),      0, (20, 102, 72, "+", 0, 30),     ("e", 96),  (0, 40, 10, 10),     "off"),
    ("Saw Lead",      (70, 60),  (12, 0, "m", "8'", 0, 1, 0, 0),      0, (70, 26, 30, "+", 0, 80),      ("e", 96),  (0, 50, 92, 10),     "off"),
    ("Square Ld",     (70, 60),  (12, 0, "m", "8'", 1, 0, 40, 0),     0, (60, 10, 26, "+", 0, 70),      ("e", 98),  (0, 62, 100, 11),    "off"),
    ("Reso Lead",     (70, 70),  (10, 0, "m", "8'", 0, 1, 0, 0),      0, (46, 100, 56, "+", 0, 60),     ("e", 94),  (0, 72, 62, 10),     "off"),
    ("Soft Lead",     (64, 50),  (12, 40, "m", "4'", 1, 0, 0, 0),     0, (46, 0, 30, "+", 0, 70),       ("e", 104), (16, 62, 90, 14),    "I"),
    ("Trance Saw",    (50, 0),   (0, 40, "l", "8'", 1, 1, 40, 0),     0, (88, 18, 24, "+", 0, 80),      ("e", 96),  (2, 60, 100, 16),    "II"),
    ("Hoover",        (60, 0),   (8, 110, "l", "8'", 1, 1, 60, 0),    0, (70, 30, 20, "+", 0, 60),      ("e", 96),  (6, 70, 100, 14),    "II"),
    ("Glide Lead",    (60, 70),  (14, 30, "m", "8'", 1, 1, 0, 0),     0, (66, 30, 30, "+", 0, 80),      ("e", 96),  (4, 70, 100, 14),    "I"),
    ("Hot Lead",      (64, 60),  (12, 0, "m", "4'", 0, 1, 0, 0),      0, (84, 40, 30, "+", 0, 90),      ("e", 94),  (0, 60, 100, 13),    "I"),
    ("Rock Lead",     (70, 60),  (14, 20, "m", "8'", 1, 1, 40, 0),    0, (76, 50, 20, "+", 0, 80),      ("e", 100), (0, 60, 110, 13),    "off"),
    ("Super Saw",     (50, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (92, 8, 12, "+", 0, 70),       ("e", 100), (1, 70, 104, 22),    "II"),
    ("Trance Ld",     (62, 70),  (5, 0, "m", "8'", 1, 1, 16, 0),      1, (90, 20, 30, "+", 0, 80),      ("e", 100), (0, 64, 100, 18),    "II"),
    ("Demo Lead",     (70, 60),  (8, 30, "m", "8'", 1, 0, 0, 0),      1, (100, 10, 20, "+", 0, 80),     ("e", 100), (0, 50, 110, 16),    "I"),
    # B34-B55 basses, the sub basses (two octaves under their note) and a wobble
    ("Round Bass",    (40, 0),   (0, 0, "m", "16'", 0, 1, 70, 0),     0, (28, 20, 58, "+", 0, 40),      ("e", 104), (0, 46, 24, 10),     "off"),
    ("Pluck Bass",    (40, 0),   (0, 0, "m", "16'", 0, 1, 40, 0),     0, (24, 62, 76, "+", 0, 60),      ("e", 106), (0, 36, 0, 10),      "off"),
    ("Rubber Bs",     (40, 0),   (0, 62, "m", "16'", 1, 0, 72, 0),    0, (34, 46, 52, "+", 0, 44),      ("e", 102), (0, 50, 30, 10),     "off"),
    ("Fat Bass",      (40, 0),   (0, 40, "m", "16'", 1, 1, 84, 0),    0, (38, 12, 46, "+", 0, 40),      ("e", 92),  (2, 56, 52, 10),     "I"),
    ("Organ Bass",    (40, 0),   (0, 0, "m", "8'", 1, 0, 100, 0),     0, (60, 0, 30, "+", 0, 70),       ("e", 104), (0, 40, 10, 10),     "off"),
    ("Night Bass",    (40, 0),   (0, 0, "m", "16'", 0, 1, 60, 0),     0, (26, 30, 64, "+", 0, 40),      ("e", 104), (0, 44, 40, 10),     "off"),
    ("Pop Bass",      (40, 0),   (0, 40, "m", "16'", 1, 1, 50, 0),    0, (30, 40, 56, "+", 0, 50),      ("e", 104), (0, 40, 30, 10),     "off"),
    ("Seq Bass",      (40, 0),   (0, 0, "m", "16'", 1, 1, 60, 0),     0, (26, 64, 60, "+", 0, 50),      ("e", 110), (0, 26, 10, 10),     "off"),
    ("Prog Bass",     (40, 0),   (0, 0, "m", "16'", 0, 1, 80, 0),     0, (24, 30, 50, "+", 0, 50),      ("e", 110), (0, 40, 30, 10),     "off"),
    ("808 Bass",      (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (28, 127, 10, "+", 0, 127),    ("e", 127), (0, 60, 40, 13),     "off"),
    ("Demo Bass",     (40, 0),   (0, 0, "m", "16'", 0, 1, 0, 0),      0, (30, 80, 70, "+", 0, 60),      ("e", 108), (0, 30, 30, 10),     "off"),
    ("Funk Bass",     (50, 0),   (0, 40, "m", "8'", 1, 0, 60, 0),     1, (30, 50, 60, "+", 0, 50),      ("e", 100), (0, 40, 20, 10),     "off"),
    ("Acid Bass",     (50, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (25, 100, 80, "+", 0, 40),     ("e", 100), (0, 35, 0, 10),      "off"),
    ("Synth Bass",    (50, 0),   (0, 20, "m", "8'", 1, 1, 60, 0),     1, (45, 0, 40, "+", 0, 60),       ("e", 100), (0, 55, 60, 12),     "off"),
    ("Moog Bass",     (50, 0),   (0, 0, "m", "16'", 0, 1, 100, 0),    0, (35, 40, 55, "+", 0, 50),      ("e", 100), (0, 50, 50, 10),     "off"),
    ("Sub Bass",      (40, 0),   (0, 0, "m", "16'", 1, 0, 127, 0),    0, (22, 0, 24, "+", 0, 50),       ("e", 110), (0, 62, 84, 10),     "off"),
    ("Rumble",        (30, 0),   (0, 0, "m", "16'", 0, 0, 127, 20),   0, (14, 30, 20, "+", 0, 30),      ("e", 108), (0, 90, 20, 16),     "off"),
    ("Wobble Bs",     (70, 0),   (0, 0, "m", "16'", 1, 1, 70, 0),     0, (30, 80, 20, "+", 80, 50),     ("e", 108), (0, 50, 100, 10),    "off"),
    # B56-B82 drums, right at C-4: a kick is the VCF ringing at full RES, its ENV sweeping it down (KYBD 127: a
    # note tunes it); snares, claps, hats, crash and tambourine are the NOISE, filtered
    ("909 Kick",      (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (30, 127, 30, "+", 0, 127),    ("e", 127), (0, 24, 0, 24),      "off"),
    ("808 Kick",      (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (28, 127, 20, "+", 0, 127),    ("e", 127), (0, 36, 0, 36),      "off"),
    ("Tech Kick",     (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 20),      0, (30, 127, 36, "+", 0, 127),    ("e", 127), (0, 20, 0, 20),      "off"),
    ("Snare",         (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 127),     1, (84, 20, 30, "+", 0, 40),      ("e", 110), (0, 20, 0, 20),      "off"),
    ("Clap",          (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     2, (78, 56, 24, "+", 0, 0),       ("e", 120), (0, 16, 0, 16),      "off"),
    ("Rim",           (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 40),      0, (74, 127, 6, "+", 0, 127),     ("e", 110), (0, 5, 0, 5),        "off"),
    ("808 Snare",     (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 90),      1, (76, 30, 40, "+", 0, 60),      ("e", 110), (0, 22, 0, 22),      "off"),
    ("Gated Room",    (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     2, (96, 0, 0, "+", 0, 0),         ("g", 100), (0, 0, 127, 0),      "off"),
    ("Hat Closed",    (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (100, 30, 20, "+", 0, 0),      ("e", 110), (0, 8, 0, 8),        "off"),
    ("Hat Open",      (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (100, 30, 20, "+", 0, 0),      ("e", 110), (0, 28, 0, 28),      "off"),
    ("Shaker",        (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (127, 0, 0, "+", 0, 0),        ("e", 104), (2, 10, 0, 10),      "off"),
    ("Crash",         (50, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (110, 40, 0, "+", 0, 0),       ("e", 100), (0, 90, 0, 80),      "off"),
    ("Tambourine",    (50, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (127, 80, 0, "+", 0, 0),       ("e", 100), (0, 20, 0, 12),      "off"),
    ("Tom Lo",        (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (38, 127, 18, "+", 0, 127),    ("e", 127), (0, 28, 0, 28),      "off"),
    ("Tom Hi",        (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (46, 127, 18, "+", 0, 127),    ("e", 127), (0, 26, 0, 26),      "off"),
    ("Syn Tom",       (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 30),      0, (42, 127, 40, "+", 0, 127),    ("e", 127), (0, 30, 0, 30),      "off"),
    ("Clave",         (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (80, 127, 0, "+", 0, 127),     ("e", 110), (0, 8, 0, 8),        "off"),
    # (a square an octave up under a low-pass that tracks the key, near a sine, a breath of noise: in tune)
    ("Whistle",       (72, 40),  (10, 0, "m", "4'", 1, 0, 0, 8),      1, (52, 20, 0, "+", 0, 127),      ("e", 100), (20, 60, 110, 20),   "off"),
    ("Steel Drum",    (50, 0),   (0, 60, "m", "8'", 1, 0, 20, 0),     1, (55, 60, 40, "+", 0, 100),     ("e", 100), (0, 50, 0, 40),      "I"),
    ("Cowbell",       (50, 0),   (0, 50, "m", "4'", 1, 0, 0, 0),      2, (62, 60, 0, "+", 0, 60),       ("e", 100), (0, 25, 0, 10),      "off"),
    ("Conga",         (50, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (40, 127, 25, "+", 0, 127),    ("e", 127), (0, 22, 0, 10),      "off"),
    # B83-B88 effects
    ("Noise Swp",     (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     0, (20, 90, 100, "+", 0, 60),     ("e", 100), (80, 110, 0, 47),    "II"),
    ("Laser Zap",     (40, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       0, (10, 120, 120, "+", 0, 80),    ("e", 96),  (0, 30, 0, 10),      "off"),
    ("Glitch",        (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (70, 127, 30, "+", 0, 127),    ("e", 110), (0, 6, 0, 6),        "off"),
    ("Click",         (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (127, 0, 0, "+", 0, 0),        ("e", 120), (0, 3, 0, 3),        "off"),
    ("Siren",         (20, 0),   (127, 0, "m", "8'", 1, 0, 0, 0),     1, (90, 0, 0, "+", 0, 80),        ("g", 100), (0, 60, 100, 20),    "off"),
    ("Riser",         (50, 0),   (0, 0, "m", "8'", 0, 1, 0, 60),      1, (20, 80, 127, "+", 0, 40),     ("e", 100), (127, 127, 127, 30), "off"),
]

# CHIP: chiptune sounds on the Juno-106. Its pulse's duty is 50 % - 45 % x PWM / 105, so the consoles' duties
# are exact: 50 % at PWM 0, 25 % at 58, 12.5 % at 87; the LFO on the width moves it as a home computer's chip did; the
# sub oscillator, the noise; the VCA in GATE for a chip's instant on and off; the VCF open (FREQ 127, KYBD
# 127) for a raw wave. No stepped triangle: a square under a low-pass that tracks the key stands in (at FREQ
# 64 a triangle's harmonics: 3rd -17 dB, 5th -28, 7th -37). No pitch envelope: blips and zaps are the
# self-oscillating VCF, its pitch following the envelope down (measured at C-3: the kick 183, 110, 73, 51 Hz at
# 0, 20, 40, 60 ms; the tom 590 to 120 Hz over 220 ms; the zap 3.9 kHz to 170 Hz over 200 ms), or the
# pattern's slides and arpeggios (0xy). Nor a second note in one: the coin's two (a fourth under for 5 ticks,
# then the key's) were FUSION's COIN, on Coin, the 50 % square of a console's coin, ringing out over ~1 s.
CHIP = [
    # A11-A18 the duties: raw, then with a delayed vibrato (~6 Hz), a softer end
    ("Chip Sq",       (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 0),       0, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("Chip 25%",      (40, 0),   (0, 58, "m", "8'", 1, 0, 0, 0),      0, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("Chip 12%",      (40, 0),   (0, 87, "m", "8'", 1, 0, 0, 0),      0, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("Chip Pulse",    (40, 0),   (0, 100, "m", "8'", 1, 0, 0, 0),     0, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("Sq Vib",        (72, 55),  (14, 0, "m", "8'", 1, 0, 0, 0),      1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("25% Vib",       (72, 55),  (14, 58, "m", "8'", 1, 0, 0, 0),     1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("12% Vib",       (72, 55),  (14, 87, "m", "8'", 1, 0, 0, 0),     1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("Soft Sq",       (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 0),       1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 0, 127, 26),     "off"),
    # A21-A24 a home computer's chip: the pulse's width moving, a saw and a pulse bass through the resonant filter
    ("SID PWM",       (34, 0),   (0, 90, "l", "8'", 1, 0, 0, 0),      1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("SID Sweep",     (92, 0),   (0, 100, "l", "8'", 1, 0, 0, 0),     1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
    ("SID Saw",       (40, 0),   (0, 0, "m", "8'", 0, 1, 0, 0),       1, (54, 70, 60, "+", 0, 90),      ("e", 100), (0, 70, 50, 14),     "off"),
    ("SID Bass",      (40, 0),   (0, 58, "m", "16'", 1, 0, 0, 0),     0, (40, 80, 70, "+", 0, 80),      ("e", 100), (0, 50, 30, 10),     "off"),
    # A25-A28 arpeggio plucks: short, for 0xy and fast runs
    ("Arp Sq",        (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 0),       1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 36, 0, 8),       "off"),
    ("Arp 25%",       (40, 0),   (0, 58, "m", "8'", 1, 0, 0, 0),      1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 36, 0, 8),       "off"),
    ("Arp 12%",       (40, 0),   (0, 87, "m", "8'", 1, 0, 0, 0),      1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 36, 0, 8),       "off"),
    ("Blip",          (40, 0),   (0, 58, "m", "4'", 1, 0, 0, 0),      1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 14, 0, 4),       "off"),
    # A31-A36 basses
    ("Chip Bass",     (40, 0),   (0, 0, "m", "16'", 1, 0, 0, 0),      0, (90, 0, 0, "+", 0, 90),        ("g", 104), (0, 0, 127, 0),      "off"),
    ("Bass 25%",      (40, 0),   (0, 58, "m", "16'", 1, 0, 0, 0),     0, (90, 0, 0, "+", 0, 90),        ("g", 100), (0, 0, 127, 0),      "off"),
    ("Bass Sub",      (40, 0),   (0, 0, "m", "16'", 1, 0, 127, 0),    0, (90, 0, 0, "+", 0, 90),        ("g", 100), (0, 0, 127, 0),      "off"),
    ("Tri Bass",      (40, 0),   (0, 0, "m", "16'", 1, 0, 0, 0),      0, (64, 0, 0, "+", 0, 127),       ("g", 100), (0, 0, 127, 0),      "off"),
    ("Soft Tri",      (40, 0),   (0, 0, "m", "8'", 1, 0, 0, 0),       1, (46, 0, 0, "+", 0, 127),       ("g", 100), (0, 0, 127, 0),      "off"),
    ("Bass Blip",     (40, 0),   (0, 58, "m", "16'", 1, 0, 0, 0),     0, (90, 0, 0, "+", 0, 90),        ("e", 100), (0, 44, 0, 10),      "off"),
    # A37-A46 drums and a zap (noise; the VCF ringing for the kick, the tom and the zap), A47-A48 a coin, an alarm
    ("Kick",          (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (8, 127, 48, "+", 0, 0),       ("e", 127), (0, 24, 0, 20),      "off"),
    ("Chip Noise",    (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     0, (60, 0, 0, "+", 0, 127),       ("e", 100), (0, 14, 0, 8),       "off"),
    ("Hat",           (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (118, 70, 0, "+", 0, 127),     ("e", 100), (0, 14, 0, 8),       "off"),
    ("Open Hat",      (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     3, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 44, 0, 30),      "off"),
    ("Snare",         (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     2, (100, 0, 0, "+", 0, 127),      ("e", 100), (0, 24, 0, 14),      "off"),
    ("Crash",         (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 127),     2, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 78, 0, 60),      "off"),
    ("Tom",           (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       0, (45, 127, 30, "+", 0, 127),    ("e", 127), (0, 26, 0, 18),      "off"),
    ("Zap",           (40, 0),   (0, 0, "m", "8'", 0, 0, 0, 0),       1, (36, 127, 60, "+", 0, 60),     ("e", 100), (0, 26, 0, 10),      "off"),
    ("Coin",          (40, 0),   (0, 0, "m", "4'", 1, 0, 0, 0),       1, (127, 0, 0, "+", 0, 127),      ("e", 100), (0, 42, 0, 42),      "off"),
    ("Alarm",         (100, 0),  (110, 87, "m", "8'", 1, 0, 0, 0),    1, (127, 0, 0, "+", 0, 127),      ("g", 100), (0, 0, 127, 0),      "off"),
]


def MPC(name, preset):
    """a TB-303 preset of mpc-vst-303 (its presets.json), by its name there, under our name"""
    return ("mpc303", name, preset)


# the TB-303 presets: WAVE (0 saw - 127 square), CUTOFF, RESONANCE, ENV MOD, DECAY, ACCENT; ours, and six of
# mpc-vst-303's (only the six knobs: its drive and Devilfish settings are not on PT/FM's TB-303)
TB303 = [
    # TB01-TB16 classic, square, squelch, sub, plucky, sweeps, screamers
    ("Classic", 0, 45, 100, 70, 45, 80), ("Square", 127, 45, 100, 70, 45, 80), ("Squelch", 0, 25, 120, 110, 30, 110),
    ("Rubber", 0, 15, 40, 30, 70, 50), ("Deep Sub", 127, 5, 20, 10, 90, 30), ("Screamer", 0, 60, 127, 127, 40, 127),
    ("Plucky", 0, 35, 80, 90, 10, 60), ("Long Sweep", 0, 20, 100, 100, 127, 70),
    ("Soft Acid", 64, 50, 60, 50, 60, 40), ("Hard Acid", 0, 70, 115, 90, 35, 127),
    ("Bubble", 127, 30, 115, 80, 25, 90), ("Growl", 0, 10, 90, 60, 50, 100), ("Bright Saw", 0, 100, 60, 40, 60, 60),
    ("Dark Sq", 127, 20, 70, 30, 80, 70), ("Wah Bass", 32, 30, 110, 120, 80, 50),
    ("Phuture", 0, 40, 110, 85, 40, 100),
    # TB17-TB22 mpc-vst-303's
    MPC("Squelch Ld", "Squelch Lead"), MPC("Deep Sub 2", "Deep Sub"), MPC("Rubber Sq", "Rubber Square"),
    MPC("Plucky Saw", "Plucky Saw"), MPC("Long Swp 2", "Long Sweep"), MPC("Soft Bass", "Soft Attack Bass"),
    # TB23-TB38 saws and squares, low to open
    ("Low Saw", 0, 18, 70, 55, 55, 75), ("Mid Saw", 0, 60, 70, 40, 55, 70), ("High Saw", 0, 88, 75, 45, 40, 75),
    ("Res Saw", 0, 38, 122, 65, 45, 90), ("Blip Saw", 0, 28, 95, 110, 8, 95), ("Slow Saw", 0, 30, 90, 80, 120, 70),
    ("Thin Saw", 0, 110, 95, 25, 30, 60), ("Open Saw", 0, 127, 20, 10, 70, 50), ("Low Sq", 127, 18, 70, 55, 55, 75),
    ("Mid Sq", 127, 55, 85, 60, 50, 80), ("High Sq", 127, 88, 75, 45, 40, 75), ("Res Sq", 127, 38, 122, 65, 45, 90),
    ("Blip Sq", 127, 28, 95, 110, 8, 95), ("Slow Sq", 127, 30, 90, 80, 120, 70),
    ("Hollow Sq", 127, 75, 40, 30, 70, 60), ("Open Sq", 127, 127, 20, 10, 70, 50),
    # TB39-TB46 acids
    ("Squeak", 0, 55, 127, 120, 20, 120), ("Chirp", 0, 70, 110, 127, 6, 110), ("Wet Acid", 0, 35, 118, 95, 70, 100),
    ("Rave Acid", 0, 50, 112, 100, 30, 127), ("Acid Sq", 127, 40, 118, 100, 35, 110),
    ("Zap Acid", 0, 15, 125, 127, 15, 127), ("Talk Acid", 64, 30, 120, 115, 55, 90),
    ("Mild Acid", 32, 45, 90, 70, 45, 70),
    # TB47-TB54 basses
    ("Sub Saw", 0, 8, 25, 15, 85, 30), ("Fat Sq", 127, 25, 50, 40, 70, 60),
    ("Round Bs", 64, 20, 30, 35, 75, 50), ("Thump", 0, 12, 60, 90, 12, 90), ("Dub Bass", 127, 15, 85, 50, 100, 40),
    ("Funk Bass", 0, 30, 70, 80, 25, 80), ("Saw Bass", 0, 25, 45, 50, 60, 60), ("Mellow Bs", 96, 22, 40, 30, 80, 40),
    # TB55-TB60 plucks, TB61-TB64 sweeps and a drone
    ("Pluck Sq", 127, 35, 80, 90, 10, 60), ("Tick", 0, 60, 60, 127, 3, 100), ("Stab Acid", 0, 45, 100, 100, 15, 127),
    ("Short Res", 0, 30, 125, 80, 12, 80), ("Click Bs", 127, 20, 40, 120, 5, 90),
    ("Pluck Mid", 64, 40, 70, 100, 15, 70), ("Long Res", 0, 25, 125, 110, 127, 80),
    ("Swell", 64, 10, 100, 127, 127, 60), ("Long Sq", 127, 20, 105, 100, 127, 70), ("Drone", 0, 40, 80, 60, 127, 40),
]


def license_of(key):
    lid, url, credit = SOURCES[key][:3]
    return {"id": lid, "credit": credit, "url": url}


def source_of(key, path, file=None, **more):
    """a voice's source: its file (as its source names it), the commit, the sha256 of the file read (path), and
    what names it there"""
    src = {"file": file or SOURCES[key][3]}
    if SOURCES[key][4]:
        src["commit"] = SOURCES[key][4]
    src["sha256"] = sha_file(path)
    src.update(more)
    return src


def levels(file, n):
    lv = LEVELS[file]
    if len(lv) != n:
        raise SystemExit(f"patches: {file}: {len(lv)} levels for {n} slots")
    return lv


def name_ok(name, what):
    if len(name) > 10 or set(name.upper()) - set(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%"):
        raise SystemExit(f"patches: {what}: the name {name!r} (10 of the device's characters)")


# ------------------------------------------------------------------ Juno-106 ---
def juno_patch(i, row):
    name, (rate, delay), (lfo, pwm, src, rng, pulse, saw, sub, noise), hpf, vcf, (mode, level), env, chorus = row
    freq, res, amt, pol, vlfo, kybd = vcf
    a, d, s, r = env
    for v in (rate, delay, lfo, pwm, sub, noise, freq, res, amt, vlfo, kybd, level, a, d, s, r):
        if not 0 <= v <= 127:
            raise SystemExit(f"patches: {name}: a slider at {v} (0..127)")
    if not 0 <= hpf <= 3 or rng not in ("16'", "8'", "4'") or chorus not in ("off", "I", "II") or pol not in "+-":
        raise SystemExit(f"patches: {name}: a switch out of range")
    name_ok(name, "a Juno-106 patch")
    return {"label": f"{'AB'[i // 64]}{i % 64 // 8 + 1}{i % 8 + 1}", "name": name, "lfo": {"rate": rate, "delay": delay},
            "dco": {"lfo": lfo, "pwm": pwm, "noise": noise, "sub": sub, "range": rng, "pulse": bool(pulse),
                    "saw": bool(saw), "pwm_source": {"l": "lfo", "m": "manual"}[src]},
            "hpf": hpf, "vcf": {"freq": freq, "res": res, "env": amt, "lfo": vlfo, "kybd": kybd, "env_polarity": pol},
            "vca": {"level": level, "mode": {"e": "env", "g": "gate"}[mode]},
            "env": {"a": a, "d": d, "s": s, "r": r}, "chorus": chorus}


def juno_bank(file, name, rows):
    patches = [juno_patch(i, row) for i, row in enumerate(rows)]
    if len(patches) > 128 or len({p["name"] for p in patches}) != len(patches):
        raise SystemExit(f"patches: {name}: more than 128 patches, or a name twice")
    for p, lv in zip(patches, levels(file, len(patches))):
        p["level"] = lv
    write_json(DISK / "patches" / "juno106" / f"{file}.json", {
        "format": fmt("juno106-bank"), "version": 1, "name": name, "license": OWN_LICENSE, "patches": patches})
    return len(patches)


# ------------------------------------------------------------------- TB-303 ---
def tb303_bank(mpc_repo):
    presets = {p["name"]: p for p in read_json(Path(mpc_repo) / SOURCES["mpc303"][3])["presets"]}
    out = []
    for i, row in enumerate(TB303):
        p = {"label": f"TB{i + 1:02d}"}
        if row[0] == "mpc303":
            _, name, preset = row
            v = presets[preset]["values"]
            knobs = [127 if v["waveform"] == "SQUARE" else 0] + [int(round(127 * v[k])) for k in
                                                                 ("cutoff", "resonance", "env_mod", "decay", "accent")]
            lost = [w for w, on in (("drive", v.get("drive")), ("shaper", v.get("tanh_shaper_drive")),
                                    ("Devilfish", v.get("devil_mod_switch") == "ON")) if on]
            extra = {"license": license_of("mpc303"),
                     "source": source_of("mpc303", Path(mpc_repo) / SOURCES["mpc303"][3], preset=preset,
                                         **({"not_kept": lost} if lost else {}))}
        else:
            name, *knobs = row
            extra = {}
        name_ok(name, "a TB-303 preset")
        if not all(0 <= k <= 127 for k in knobs):
            raise SystemExit(f"patches: the TB-303 preset {name}: a knob out of 0..127")
        p.update({"name": name, **dict(zip(("wave", "cutoff", "reso", "envmod", "decay", "accent"), knobs))})
        out.append((p, extra))
    if len(out) > 128 or len({p["name"] for p, _ in out}) != len(out):
        raise SystemExit("patches: TB-303: more than 128 presets, or a name twice")
    patches = [{**p, "level": lv, **extra} for (p, extra), lv in zip(out, levels("tb-303", len(out)))]
    write_json(DISK / "patches" / "tb303" / "tb-303.json", {
        "format": fmt("tb303-bank"), "version": 1, "name": "TB-303", "license": OWN_LICENSE, "patches": patches})
    return len(patches)


# ---------------------------------------------------------------------- DX7 ---
def dx7_cartridges(a):
    ours = {v["name"].strip(): v for v in dx7_voices.VOICES}
    banks = {"dxheader": fm_banks.dxheader(a.dxheader), "fm6": fm_banks.fm6(a.fm6),
             "dxbank": fm_banks.dxbank(a.dxbank), "classix": fm_banks.classix(a.classix),
             "md": fm_banks.ym2612(a.ym2612), "x16": fm_banks.x16(a.x16)}
    # the file each source's voices are read from
    paths = {"dxheader": Path(a.dxheader), "fm6": Path(a.fm6) / fm_banks.FM6_FILE,
             "dxbank": Path(a.dxbank) / fm_banks.DXBANK_FILE, "classix": Path(a.classix),
             "x16": Path(a.x16) / fm_banks.X16_FILE}
    count = 0
    for file, cname, slots in DX7_CARTRIDGES:
        if len(slots) > 32 or len({n for n, _, _ in slots}) != len(slots):
            raise SystemExit(f"patches: {cname}: more than 32 voices, or a name twice")
        voices = []
        for (name, src, what), lv in zip(slots, levels(file, len(slots))):
            name_ok(name, cname)
            if src == "ours":
                v, extra = ours[what], {}
            else:
                v = banks[src].get(what)
                if v is None:
                    raise SystemExit(f"patches: {cname} {name}: no {what!r} in {src}")
                more = {"voice": what}
                if src == "x16":
                    num, v = v
                    more = {"voice": f"{num:03d} {what}"}
                if src == "md":                         # a file a patch
                    source = source_of(src, Path(a.ym2612) / f"{what}.dmp", f"{what}.dmp")
                else:
                    source = source_of(src, paths[src], **more)
                extra = {"license": license_of(src), "source": source}
            voice = {"name": name.ljust(10), "level": lv, **{k: x for k, x in v.items() if k != "name"}, **extra}
            voices.append(voice)
        write_json(DISK / "patches" / "dx7" / f"{file}.json", {
            "format": fmt("dx7-bank"), "version": 1, "name": cname, "license": OWN_LICENSE, "voices": voices})
        count += len(voices)
    return count


def main():
    ap = argparse.ArgumentParser(description="the demo disk's patch cartridges")
    for arg, what in (("dxheader", "the DX header bank's dx7_bank.h"), ("fm6", "a checkout of the FM6 bank"),
                      ("dxbank", "a checkout of the DX bank"), ("classix", "Idiomdrottning's classix bank (.dx7)"),
                      ("ym2612", "a checkout of ym2612-patches"), ("x16", "a checkout of the X16 ROM (audio/)"),
                      ("mpc303", "a checkout of mpc-vst-303")):
        ap.add_argument("--" + arg, type=Path, required=True, help=what)
    a = ap.parse_args()
    n_dx = dx7_cartridges(a)
    n_ju = juno_bank("juno-106", "JUNO-106", JUNO)   # (CHIP's Juno-106 patches: no longer a cartridge, F9 is the
    n_tb = tb303_bank(a.mpc303)                      # CHIP engine's; kept for which may join JUNO-106)
    print(f"patches: {n_dx} DX7 voices in {len(DX7_CARTRIDGES)} cartridges, {n_ju} Juno-106 patches (JUNO-106), "
          f"{n_tb} TB-303 presets")


if __name__ == "__main__":
    main()
