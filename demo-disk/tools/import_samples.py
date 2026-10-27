#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The demo disk's sampler cartridges: recordings of natural instruments and real voices, and the basses the
synth engines cannot make (dubstep wobbles and growls), from the free libraries downloaded into DIR (data
only: nothing there runs; ffmpeg decodes the audio). Drums of the drum machines, chip sounds, synth pads and
effects are patches, not samples.

  import_samples.py --from DIR        -> samples/<set>/samples.json and a FLAC file a sample

DIR holds the libraries (REPOS: each a repository checked out at its commit, or a download, with at least
the files named below). `--list` prints, for each library, the files the sets need.

A sample, from the original (16/24-bit, 44.1/48 kHz, stereo; MP3 for some):
  1. decoded by ffmpeg, mixed to mono, at its working rate (`wrate`: 22,050 Hz; 11,025 for a low one);
  2. cut:
     - a one-shot from the first sound (-50 dB of the peak) or from `at` seconds, up to `max` seconds, ended
       where it falls 42 dB under its loudest (5 ms windows), its last `fade` seconds faded out;
     - `head`: the first `max` seconds as they are, the last `fade` seconds faded (decoded at `decode_rate`
       first, then resampled to the working rate, where given);
     - a sustained sound: a loop found near `loop.at` seconds (the loop's end; its length `lmin`..`lmax`
       seconds, where the waveform before its end matches the one before its start best), its last `xfade`
       seconds crossfaded with those before its start, the sample cut at the loop's end;
     - `bars` (a tempo and a number of bars): a sustain loop of those bars from the start, its first 20 ms
       blended with what follows its end (or, with nothing after it, 4 ms faded at both ends);
  3. scaled: its loudest at `peak` / 128 of full scale (126: room for the 8-bit coding's rounding), times
     `gain`;
  4. on its diet (the flash is small): `keep`, a one-shot's first samples kept (at the working rate: its tail
     under -45 dB of its loudest cut), the last 20 ms faded; `halve`, a loop of two bars kept to its first,
     4 ms faded at both ends; then stored at `rate`, resampled from the working rate where it is lower (a rate
     whose loss is under -36 dB of the whole); its instruments' positions scaled with it;
  5. written as 16-bit FLAC, the master; the device keeps it at 8 bits (`bits`), each value rounded; the samples
     in OPTIMIZED get `optimize`, applied when the disk is built.
`note` is the recording's pitch (MIDI), measured on it: the libraries name their files in more than one octave
convention, so a name can be an octave off; a recording a little off its note gets a TUNE.
"""
import argparse
import math
import re
import subprocess
from pathlib import Path

import numpy as np

from common import DISK, flac_read, flac_write, fmt, note_name, sha_file, write_json
from levels import LEVELS

RATE, LOW_RATE = 22050, 11025

# the libraries: folder, commit (None: not a repository; then the url it was downloaded from), licence
# (the url where it is stated), the credit's start
REPOS = {
    "VCSL": ("VCSL", "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e", "CC0-1.0",
             "https://github.com/sgossner/VCSL/blob/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/LICENSE",
             "Versilian Community Sample Library (VCSL), Versilian Studios"),
    "VSCO-2-CE": ("VSCO-2-CE", "440300901dfe9275fd84e0b7763af1f8443ae62e", "CC0-1.0",
                  "https://github.com/sgossner/VSCO-2-CE/blob/440300901dfe9275fd84e0b7763af1f8443ae62e/LICENSE",
                  "VSCO 2 Community Edition, Versilian Studios"),
    "sonic-pi": ("sonic-pi", "7ec6e5df11a5c13f383926e69c8f133e3a6cc34f", "CC0-1.0",
                 "https://github.com/sonic-pi-net/sonic-pi/blob/7ec6e5df11a5c13f383926e69c8f133e3a6cc34f/LICENSE.md",
                 "Sonic Pi's samples"),
    "world-percussion": ("world-percussion", "e54eb2912a0d6d4444ab205d52f778e27da0fc96", "CC0-1.0",
                         "https://github.com/freepats/world-percussion/blob/e54eb2912a0d6d4444ab205d52f778e27da0fc96/LICENSE.txt",
                         "World Percussion by Xavimart, Gonzalo and Roberto, FreePats"),
    # the Discord SFZ GM Bank: a licence for each instrument, stated in its .sfz file (the sample's `lic`)
    "Discord-SFZ-GM-Bank": ("Discord-SFZ-GM-Bank", "7a9c478fe331f94f246d33332f0adedb25bbbe27", "CC0-1.0",
                            "https://github.com/sfzinstruments/Discord-SFZ-GM-Bank/blob/7a9c478fe331f94f246d33332f0adedb25bbbe27/",
                            "from the Discord SFZ GM Bank"),
    "karoryfer.emilyguitar": ("karoryfer.emilyguitar", "b4920dc662fd9cad6dcaccdeecffdd91c8725d8c", "CC0-1.0",
                              "https://github.com/sfzinstruments/karoryfer.emilyguitar/blob/b4920dc662fd9cad6dcaccdeecffdd91c8725d8c/LICENSE",
                              "Emilyguitar by Karoryfer Samples (D. Smolken)"),
    # downloads, not repositories: the folder holds the files as the source names them
    "producer-space": ("producer-space/Producer Space/Packs", None, "CC0-1.0",
                       "https://archive.org/details/producer-space-cc0-sample-library",
                       "Producer Space's CC0 sample library (producerspace.com, Creative Nevada)"),
    # Freesound's previews (MP3) of CC0 sounds, named fs<the sound's number>.mp3; a licence for each sound,
    # stated on its page (the sample's `url`)
    "freesound": ("dubstep", None, "CC0-1.0", None, "Freesound"),
}


def S(sid, name, kind, repo, path, note=60, max=1.0, fade=0.05, loop=None, wrate=RATE, gain=1.0,
      lic=None, credit=None, process=None, env=None, at=None, skip=0.0, tune=0, rate=None, keep=None,
      halve=False, head=False, bars=None, decode_rate=None, peak=126, url=None):
    """a sample from a library. path: a file, or a tuple of files mixed (two microphones); lic, credit: the
    file stating its licence and whose work it is, where the library has one per instrument (credit: a part
    of the credit for this sample, where its library has none per instrument; url: the page stating its
    licence, where each sound has its own); process: made into another sound (PROCESS); env: its own
    instrument's envelope, if not its kind's; at: a one-shot taken from this second of the file (a phrase out
    of a longer recording); skip: the file's first seconds left out (a held note sung later in the
    recording); tune: its own instrument's TUNE (1/128 semitone, + raises), bringing a recording that is off
    its note in tune; rate, keep, halve: its diet (above); head, bars, decode_rate, peak: how it is cut and
    scaled (above)"""
    return dict(id=sid, name=name, kind=kind, repo=repo, path=path, note=note, max=max, fade=fade, loop=loop,
                wrate=wrate, gain=gain, lic=lic, credit=credit, process=process, env=env, at=at, skip=skip,
                tune=tune, rate=rate or wrate, keep=keep, halve=halve, head=head, bars=bars,
                decode_rate=decode_rate, peak=peak, url=url)


def L(at, lmin, lmax, xfade):
    return dict(at=at, lmin=lmin, lmax=lmax, xfade=xfade)


def I(name, sample, **fields):
    """an instrument beyond a sample's own: its name, its sample's id, and what differs from the sample's
    own instrument (start, end, base_note, tune, envelope); START and END as a share of the sample's length,
    START "loop" at its loop"""
    return dict(name=name, sample=sample, **fields)


def E(rates, levels):
    return {"rates": list(rates), "levels": list(levels)}


# envelopes (rates R1-R4, levels L1-L4): drums let go at once when the track's next note comes (a choke)
ENV_DRUM = E((99, 99, 99, 85), (99, 99, 99, 0))
ENV_CRASH = E((99, 99, 99, 62), (99, 99, 99, 0))
ENV_BASS = E((99, 99, 99, 74), (99, 99, 99, 0))
ENV_SUB = E((99, 99, 99, 72), (99, 99, 99, 0))
ENV_PLUCK = E((99, 99, 99, 68), (99, 99, 99, 0))
ENV_SUSTAIN = E((96, 99, 99, 66), (99, 99, 99, 0))
ENV_PAD = E((62, 99, 99, 54), (99, 99, 99, 0))
ENV_VOICE = E((99, 99, 99, 70), (99, 99, 99, 0))
ENV_KIND = {"kick": ENV_DRUM, "snare": ENV_DRUM, "hat": ENV_DRUM, "perc": ENV_DRUM, "crash": ENV_CRASH,
            "fx": ENV_CRASH, "bass": ENV_BASS, "pluck": ENV_PLUCK, "lead": ENV_SUSTAIN, "pad": ENV_PAD,
            "voice": ENV_VOICE}
# the sampler's envelope is a DX7 operator's: a decay to silence takes ~1.15 s at rate 50, 0.46 s at 58, 0.33 s at
# 62, 0.19 s at 66; an attack from silence ~1 s at rate 35
ENV_MUTE = E((99, 62, 99, 85), (99, 0, 0, 0))              # dampened, gone in ~0.3 s
ENV_STAB = E((99, 58, 99, 80), (99, 0, 0, 0))              # a short stab, ~0.45 s


SP, WP, VC, VS, GM, EG, PS, FS = ("sonic-pi", "world-percussion", "VCSL", "VSCO-2-CE", "Discord-SFZ-GM-Bank",
                                  "karoryfer.emilyguitar", "producer-space", "freesound")
GMM = "Discord GM/Melodic/"
SPS = "etc/samples/"
VCI, VCM = "Idiophones/Struck Idiophones/", "Membranophones/Struck Membranophones/"

# ------------------------------------------------------------------- sets ---
DRUM_KIT = [
    # Sonic Pi: an acoustic kit (menegass's); FreePats World Percussion: an egg shaker
    S("SP01", "KICK", "kick", SP, SPS + "drum_bass_hard.flac", max=0.5, rate=8000),
    S("SP03", "SNARE", "snare", SP, SPS + "drum_snare_hard.flac", max=0.45),
    S("SP05", "HAT", "hat", SP, SPS + "drum_cymbal_closed.flac", max=0.2),
    S("SP07", "HAT OPEN", "hat", SP, SPS + "drum_cymbal_open.flac", max=0.45, fade=0.2),
    S("SP08", "CRASH", "crash", SP, SPS + "drum_cymbal_hard.flac", max=0.9, fade=0.5, keep=18734),
    S("WP06", "SHAKER", "perc", WP, "samples/EggShaker/fast_02.flac", max=0.2),
    S("SP11", "TOM MID", "perc", SP, SPS + "drum_tom_mid_hard.flac", max=0.4, wrate=LOW_RATE, rate=8000),
]
# the toms on one tom's sample, retuned to the pitches of Sonic Pi's low and high toms (104 and 167 Hz against
# 136); a crash grabbed by the hand
DRUM_KIT_SLOTS = ["SP01", "SP03", "SP05", "SP07", "SP08", I("CRSH CHOKE", "SP08", envelope=E((99, 66, 99, 85), (99, 0, 0, 0))),
                  "WP06", I("TOM LO", "SP11", base_note="F-4", tune=39), "SP11",
                  I("TOM HI", "SP11", base_note="G#3", tune=-57)]

PERCUSSION = [
    # VCSL: mark trees, a darbuka; FreePats World Percussion: a conga, a bongo; Sonic Pi: tabla; VSCO 2 CE: timpani
    S("VC38", "WIND CHIME", "fx", VC, VCI + "Mark Trees/Legacy/windchimes_asc1.wav", 60, max=1.4, fade=0.6, wrate=LOW_RATE),
    S("VC06", "DARBUKA", "perc", VC, VCM + "Darbuka/Darbuka_1_hit_vl2_rr1.wav", max=0.3),
    S("WP02", "CONGA", "perc", WP, "samples/Conga/v3_01_01.flac", max=0.4, rate=16000),
    S("WP05", "BONGO LO", "perc", WP, "samples/Bongos/2_10.flac", max=0.3, rate=11025),
    S("SP17", "TABLA NA", "perc", SP, SPS + "tabla_na.flac", max=0.35),
    S("SP18", "TABLA GE", "perc", SP, SPS + "tabla_ghe1.flac", max=0.45, wrate=LOW_RATE),
    S("SP19", "TABLA TUN", "perc", SP, SPS + "tabla_tun1.flac", max=0.5, wrate=LOW_RATE, rate=8000),
    S("VS16", "TIMPANI", "perc", VS, "Percussion/Timpani/Timpani1_Hit_v3_rr1_Sum.wav", 60, max=1.0, fade=0.5,
      wrate=LOW_RATE, rate=8000, keep=10469),
]
# the chime rising in as if reversed; the congas on one conga's sample, retuned to FreePats' high and low
# congas (217 and 137 Hz against 164)
PERCUSSION_SLOTS = ["VC38", I("CHIME RISE", "VC38", envelope=E((40, 99, 99, 60), (99, 99, 99, 0))), "VC06",
                    I("CONGA HI", "WP02", base_note="G-3", tune=-16), "WP02", I("CONGA LO", "WP02", base_note="D#4", tune=-4),
                    "WP05", "SP17", "SP18", "SP19", "VS16"]

BAND_ORCH = [
    # VCSL: a piano, a vibraphone, a tenor sax
    S("VC10", "PIANO", "pluck", VC, "Chordophones/Zithers/Grand Piano, Steinway B/Sus/JHPiano_Sus_Close_C4_vl3_rr1.wav",
      60, max=1.2, fade=0.5, rate=11025),
    S("VC22", "VIBRAPHONE", "pluck", VC, VCI + "Vibraphone/Soft Mallets/Vibes_soft_D4_v2_rr1_Main.wav", 74, max=1.2,
      fade=0.5, rate=8000),
    # Emilyguitar (an electric guitar recorded direct): our power chord made of its low E
    S("EG02", "DIST GTR", "lead", EG, "notes/e2_f_rr1.wav", 40, max=1.2, fade=0.3, process="power chord",
      env=E((99, 99, 99, 85), (99, 99, 99, 0)), rate=8000),
    # the Discord SFZ GM Bank: a steel-string guitar (its own licence)
    S("GM02", "STEEL GTR", "pluck", GM, GMM + "026-Acoustic Guitar (steel)/MartinGM2_052__E3_1.wav", 52, max=1.1,
      fade=0.5, lic=GMM + "026-Acoustic Guitar (steel).sfz", credit="a Martin HD28 sampled by Jeff Learman", rate=16000),
    # VSCO 2 CE: violins, a flute, the upright bass played pizzicato
    S("VS10", "VIOLINS", "pad", VS, "Strings/Violin Section/susVib/VlnEns_susVib_C4_v1.wav", 72, loop=L(0.9, 0.3, 0.5, 0.1),
      rate=16000),
    S("VC14", "TENOR SAX", "lead", VC, "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_C3_vl3_rr1.wav",
      60, loop=L(0.7, 0.2, 0.4, 0.06), env=E((92, 99, 99, 80), (99, 99, 99, 0))),
    S("VS13", "FLUTE", "lead", VS, "Woodwinds/Flute/susvib/LDFlute_susvib_C5_v1_1.wav", 84, loop=L(0.8, 0.25, 0.45, 0.06),
      rate=16000),
    S("VS01", "UPRIGHT", "bass", VS, "Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_C#2_v1_rr1.wav", 37, max=0.8, fade=0.3,
      wrate=LOW_RATE, env=E((99, 99, 99, 82), (99, 99, 99, 0))),
]
# more instruments from the same samples, their envelope another: muted strokes, a palm-muted cut of the power
# chord, a short bow from the violins' sustain (from its loop: from the start their own attack meets the decay),
# a slow swell, a short stab of the sax
BAND_ORCH_SLOTS = [
    "VC10", I("PIANO MUTE", "VC10", envelope=ENV_MUTE), "VC22", I("VIBES MUTE", "VC22", envelope=ENV_MUTE),
    "EG02", "GM02", I("STEEL MUTE", "GM02", envelope=E((99, 64, 99, 85), (99, 0, 0, 0))),
    I("GTR MUTE", "EG02", end=0.12, envelope=E((99, 70, 99, 85), (99, 70, 70, 0))),
    "VS10", I("VLN STAB", "VS10", start="loop", envelope=ENV_STAB),
    I("VLN SWELL", "VS10", envelope=E((35, 99, 99, 50), (99, 99, 99, 0))),
    "VC14", I("SAX STAB", "VC14", envelope=ENV_STAB), "VS13", "VS01"]

VOICES = [
    # Producer Space: dance shouts, and words in a computer's manner, a count-in
    S("PS01", "HERE COMES", "voice", PS, "Dance Vocal Shouts 2/DVSV2 - Here Comes the Drop 1.wav", 60, max=0.8,
      fade=0.05, wrate=16000, credit="Dance Vocal Shouts 2: Here Comes the Drop"),
    S("PS02", "R U READY", "voice", PS, "Dance Vocal Shouts 2/DVSV2 - Are You Ready for This 1.wav", 60, max=1.0,
      fade=0.05, wrate=16000, credit="Dance Vocal Shouts 2: Are You Ready for This"),
    *[S(sid, name, "voice", PS, f"Computer World/CW - {file}.wav", 60, max=1.6, fade=0.05, wrate=11025,
        credit=f"Computer World: {file}")
      for sid, name, file in (("PS10", "WARNING", "Warning"), ("PS15", "THREE", "3"), ("PS16", "TWO", "2"),
                              ("PS17", "ONE", "1"))],
]

BASS = [
    # Freesound: dubstep basses, CC0 (each its page); loops of whole bars at their tempo
    S("FS01", "WOBBLE 140", "bass", FS, "fs162257.mp3", 24, wrate=16000, bars=(140, 2), halve=True, peak=127,
      env=ENV_SUB, credit="Dubstep Wobble 140BPM by kasa90", url="https://freesound.org/s/162257/"),
    S("FS02", "YOI YOI", "bass", FS, "fs164044.mp3", 29, wrate=16000, bars=(128, 1), peak=127, env=ENV_SUB,
      credit="YoiYoiYoi by Flammenzunge", url="https://freesound.org/s/164044/"),
    S("FS03", "GROWL", "bass", FS, "fs392696.mp3", 41, wrate=16000, head=True, max=0.72, fade=0.03, keep=11200,
      peak=127, env=ENV_SUB, credit="Dubstep Growl by Poligonstudio", url="https://freesound.org/s/392696/"),
    # Sonic Pi: a drum and bass bass
    S("SP20", "DNB BASS", "bass", SP, SPS + "bass_dnb_f.flac", 29, head=True, max=0.87, fade=0.05, decode_rate=44100,
      peak=127, env=ENV_SUB),
]

# folder, the cartridge's name (12 characters), samples, the instruments in their slots (a sample's id: its
# own instrument; I(...): another made from a sample)
SETS = [("drum-kit", "DRUM KIT", DRUM_KIT, DRUM_KIT_SLOTS), ("percussion", "PERCUSSION", PERCUSSION, PERCUSSION_SLOTS),
        ("band-orch", "BAND/ORCH", BAND_ORCH, BAND_ORCH_SLOTS), ("voices", "VOICES", VOICES, None),
        ("bass", "BASS", BASS, None)]

# the samples kept smaller, picked by ear against their masters (2026-10-26): `optimize` on each (README.md: a softer
# top and 6 dB lower, which the build makes up in their instruments' LEVEL); the snare, the hats, the crash, the
# shaker, the wind chime and the basses kept as they are
OPTIMIZE = {"soften": 0.25, "gain": -6}
OPTIMIZED = {"SP01", "SP11", "VC06", "WP02", "WP05", "SP17", "SP18", "SP19", "VS16", "VC10", "VC22", "EG02", "GM02",
             "VS10", "VC14", "VS13", "VS01", "PS01", "PS02", "PS10", "PS15", "PS16", "PS17"}


# ------------------------------------------------------------------- audio ---
def decode(path, rate):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(rate), "-f", "f32le", "pipe:1"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).astype(np.float64)


def env(x, win):
    n = len(x) // win
    return np.sqrt((x[:n * win].reshape(n, win) ** 2).mean(axis=1)) if n else np.array([0.0])


def fade_out(x, n):
    x = x.copy()
    if n:
        x[-n:] *= np.linspace(1, 0, n)
    return x


def one_shot(x, s):
    peak = np.abs(x).max()
    R = s["wrate"]
    if s.get("at") is not None:
        start = int(s["at"] * R)
    else:
        start = max(0, int(np.argmax(np.abs(x) > peak * 10 ** (-50 / 20))) - R // 1000)
    x = x[start:start + int(s["max"] * R)] if s["max"] else x[start:]
    win = R // 200
    e = env(x, win)
    loud = np.nonzero(e > e.max() * 10 ** (-42 / 20))[0]
    x = x[:min(len(x), (loud[-1] + 1) * win)]
    n = min(len(x), int(s["fade"] * R))
    if n:
        x = x.copy()
        x[-n:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n))
    return x


def sustained(x, s):
    """a loop near s.loop.at: the end e and length L where the waveform before e matches the one before
    e - L best (one window, a few periods long), then crossfaded"""
    lp = s["loop"]
    peak = np.abs(x).max()
    R = s["wrate"]
    x = x[max(0, int(np.argmax(np.abs(x) > peak * 10 ** (-50 / 20))) - R // 1000):].copy()
    period = R / (440 * 2 ** ((s["note"] - 69) / 12))
    w = int(max(3 * period, 0.01 * R))
    lmin, lmax = int(lp["lmin"] * R), int(lp["lmax"] * R)
    at = min(int(lp["at"] * R), len(x) - 1)
    best = None
    for e in range(max(lmax + w, at - int(0.15 * R)), min(len(x), at + int(0.15 * R)), max(1, int(period / 4))):
        tail = x[e - w:e]
        seg = x[e - lmax - w:e - lmin]
        corr = np.correlate(seg, tail, mode="valid")[::-1]
        sq = np.convolve(seg ** 2, np.ones(w), mode="valid")[::-1]
        err = ((tail ** 2).sum() + sq - 2 * corr) / ((tail ** 2).sum() + 1e-12)
        k = int(np.argmin(err))
        if best is None or err[k] < best[0]:
            best = (err[k], e, lmin + k)
    if best is None:
        raise SystemExit(f"import_samples: {s['id']}: no room for a loop near {lp['at']} s")
    _, e, length = best
    c = min(int(lp["xfade"] * R), length // 2, e - length)
    t = np.linspace(0, 1, c, endpoint=False) + 1 / c
    x[e - c:e] = x[e - c:e] * (1 - t) + x[e - length - c:e - length] * t
    return x[:e], (e - length, e), best[0]


def bars_loop(x, s):
    """a sustain loop of whole bars from the start: its first 20 ms blended with what follows its end"""
    R = s["wrate"]
    bpm, bars = s["bars"]
    n = int(round(bars * 4 * 60 / bpm * R))
    m = int(0.02 * R)
    x = x.copy()
    if len(x) >= n + m:
        w = np.linspace(0, 1, m)
        x[:m] = x[n:n + m] * (1 - w) + x[:m] * w
    else:
        x = np.concatenate([x, np.zeros(max(0, n - len(x)))])
        f = int(0.004 * R)
        x[:f] *= np.linspace(0, 1, f)
        x[n - f:n] *= np.linspace(1, 0, f)
    return x[:n], (0, n)


def head(x, s, R):
    """the first s.max seconds at rate R, the last s.fade seconds faded"""
    x = x[:int(s["max"] * R)]
    return fade_out(x, min(len(x), int(s["fade"] * R)))


def power_chord(x, R):
    """a power chord from one clean note: the note, its fifth and its octave (resampled copies), through a
    distortion and a speaker's band"""
    from scipy import signal
    n = len(x)
    fifth = signal.resample(x, int(n / 2 ** (7 / 12)))[:n]
    octave = x[::2]
    y = x.copy()
    y[:len(fifth)] += 0.8 * fifth
    y[:len(octave)] += 0.6 * octave
    y = np.tanh(6 * y / np.abs(y).max())
    sos = signal.butter(2, [90, 4200], btype="bandpass", fs=R, output="sos")
    return signal.sosfilt(sos, y)


PROCESS = {"power chord": power_chord}


def resample(x, src, dst):
    from scipy.signal import resample_poly
    g = math.gcd(int(src), int(dst))
    return resample_poly(x, int(dst) // g, int(src) // g)


def make(s, src):
    """-> the sample at its working rate (floats, -1..1, at its scale), its loop (start, end) or None, the
    loop's match error"""
    paths = [src / REPOS[s["repo"]][0] / p for p in (s["path"] if isinstance(s["path"], tuple) else (s["path"],))]
    R = s["wrate"]
    if s["head"] and s["decode_rate"]:                      # cut at the decoded rate, then to the working rate
        x = resample(head(decode(paths[0], s["decode_rate"]), s, s["decode_rate"]), s["decode_rate"], R)
        loop, err = None, None
    else:
        parts = [decode(p, R) for p in paths]
        x = np.zeros(max(len(p) for p in parts))
        for p in parts:
            x[:len(p)] += p
        x = x[int(s["skip"] * R):]
        loop, err = None, None
        if s["process"]:
            x = PROCESS[s["process"]](one_shot(x, dict(s, max=None, fade=0)), R)
        if s["bars"]:
            x, loop = bars_loop(x, s)
        elif s["head"]:
            x = head(x, s, R)
        elif s["loop"]:
            x, loop, err = sustained(x, s)
        else:
            x = one_shot(x, s)
    return x * (s["peak"] * s["gain"] / 128) / np.abs(x).max(), loop, err


def diet(x, loop, s):
    """the sample on its diet: kept to `keep` (a one-shot, its last 20 ms faded) or its loop halved, then at
    its rate -> (floats at the stored rate, the loop's start there or None)"""
    R, rate = s["wrate"], s["rate"]
    if s["halve"]:
        x = x[:len(x) // 2].copy()
        f = int(0.004 * R)
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
        loop = (min(loop[0], len(x)), len(x))
    if s["keep"] is not None:
        if loop or s["keep"] >= len(x):
            raise SystemExit(f"import_samples: {s['id']}: keep {s['keep']} of {len(x)} (a one-shot's tail)")
        x = fade_out(x[:s["keep"]], min(s["keep"], int(0.02 * R)))
    if rate > R:
        raise SystemExit(f"import_samples: {s['id']}: stored at {rate} Hz, above its working rate {R}")
    if rate != R:
        x = resample(x, R, rate)
    ls = None if loop is None else min(len(x), int(round(loop[0] * rate / R)))
    return x, ls


def to_16(x):
    return [int(v) for v in np.clip(np.round(x * 32768), -32768, 32767)]


def sonic_credits(src):
    """Sonic Pi's etc/samples/README.md: sample -> its source on freesound"""
    out = {}
    text = (src / "sonic-pi" / "etc" / "samples" / "README.md").read_text()
    for m in re.finditer(r"`:(\w+)`\s*-\s*(\S+)", text):
        out[m.group(1)] = m.group(2)
    for m in re.finditer(r"`(\w+)`\s*-\s*(Donated by [^\n]*)", text):      # lines of another form
        out[m.group(1)] = m.group(2).strip()
    return out


def license_of(s, sp_credits):
    folder, commit, lic, url, credit = REPOS[s["repo"]]
    if s["url"]:
        return {"id": lic, "credit": f"{s['credit']}, {credit}", "url": s["url"]}
    if s["lic"]:
        return {"id": lic, "credit": f"{s['credit']}, {credit}", "url": url + s["lic"].replace(" ", "%20")}
    if s["repo"] in ("karoryfer.emilyguitar",) and s["process"]:
        return {"id": lic, "credit": f"{credit}: its low E made into a power chord here", "url": url}
    if s["repo"] == "sonic-pi":
        name = Path(s["path"]).stem
        link = sp_credits.get(name)
        if not link:
            raise SystemExit(f"import_samples: {name}: not in Sonic Pi's samples README")
        who = re.search(r"/people/([^/]+)/", link)
        if link.startswith("Donated by "):
            credit = f"{name}, {link[0].lower()}{link[1:]}, from {credit}"
        else:
            credit = f"{name} by {who.group(1).replace('%20', ' ') if who else 'unknown'} ({link}), from {credit}"
    elif s["repo"] in ("VCSL", "VSCO-2-CE"):
        credit = f"{credit}: {Path(s['path']).parent.as_posix()}"
    elif s["credit"]:
        credit = f"{credit}: {s['credit']}"
    return {"id": lic, "credit": credit, "url": url}


def own_instrument(s, length, loop):
    """a sample's own instrument at its working rate: all of it, its loop if it has one, at its recorded
    pitch"""
    return {"name": s["name"], "sample": s["id"], "start": 0, "end": length,
            "loop": "forward" if loop else "off", "loop_start": loop[0] if loop else 0,
            "loop_end": loop[1] if loop else 0, "base_note": note_name(s["note"]),
            "tune": s["tune"], "level": 0, "volume": 64, "envelope": s.get("env") or ENV_KIND[s["kind"]]}


def instrument(slot, own, scale):
    """an instrument at the working rate (own: sample id -> its own instrument there), then its positions at
    the sample's stored rate (scale: sample id -> (rate / working rate, the stored length, the loop's start))"""
    if isinstance(slot, str):
        ins, fields = dict(own[slot]), {}
    else:
        fields = {k: v for k, v in slot.items() if k not in ("name", "sample")}
        ins = dict(own[slot["sample"]], name=slot["name"])
        ins.update({k: v for k, v in fields.items() if k != "start" or v != "loop"})
        for k in ("start", "end"):                          # a share of the sample's length
            if isinstance(ins[k], float):
                ins[k] = round(ins[k] * own[slot["sample"]]["end"])
        if "start" in fields and fields["start"] != "loop" or "end" in fields:    # a cut: its loop is the cut
            ins["loop_start"], ins["loop_end"] = ins["start"], ins["end"]
    k, n, ls = scale[ins["sample"]]
    if ins["loop"] != "off":                                # the sample's sustain loop, to its end
        ins["start"] = min(n, round(ins["start"] * k))
        ins["loop_start"], ins["loop_end"], ins["end"] = ls, n, n
    else:
        for f in ("start", "end", "loop_start", "loop_end"):
            ins[f] = min(n, round(ins[f] * k))
    if fields.get("start") == "loop":
        ins["start"] = ins["loop_start"]
    return ins


def sample_set(folder, title, specs, slots, src, sp_credits):
    d = DISK / "samples" / folder
    entries, report, own, scale = [], [], {}, {}
    if len(specs) > 64:
        raise SystemExit(f"import_samples: {title}: {len(specs)} samples (64 a cartridge)")
    for s in specs:
        if len(s["name"]) > 10:
            raise SystemExit(f"import_samples: {s['id']}: the name {s['name']!r} past 10 characters")
        x, loop, err = make(s, src)
        own[s["id"]] = own_instrument(s, len(x), loop)
        y, ls = diet(x, loop, s)
        data = to_16(y)
        scale[s["id"]] = (s["rate"] / s["wrate"], len(data), ls)
        e = {"id": s["id"], "file": f"{s['id']}.flac", "name": s["name"], "kind": s["kind"], "length": len(data),
             "rate": s["rate"], "bits": 8, "license": license_of(s, sp_credits)}
        folder_, commit = REPOS[s["repo"]][:2]
        files = s["path"] if isinstance(s["path"], tuple) else (s["path"],)
        named = "" if s["url"] else f"{folder_}/"           # a sound of its own page: its file's own name
        e["source"] = {"file": " + ".join(f"{named}{p}" for p in files)}
        if commit:
            e["source"]["commit"] = commit
        e["source"]["sha256"] = " + ".join(sha_file(src / folder_ / p) for p in files)
        if s["id"] in OPTIMIZED:
            e["optimize"] = dict(OPTIMIZE)
        flac_write(d / e["file"], data, s["rate"])
        if flac_read(d / e["file"]) != data:
            raise SystemExit(f"import_samples: {e['file']} does not read back")
        entries.append(e)
        report.append(f"  {s['id']} {s['name']:10s} {len(data) / s['rate']:5.2f} s at {s['rate']:5d} Hz {len(data):6d}"
                      + (f"  loop {len(data) - ls} (match {err:.3f})" if err is not None else "")
                      + (f"  (was {len(x)} at {s['wrate']} Hz)" if len(x) != len(data) else ""))
    for old in d.glob("*.flac"):
        if old.name not in {e["file"] for e in entries}:
            old.unlink()
    ins = [instrument(slot, own, scale) for slot in (slots or [s["id"] for s in specs])]
    levels = LEVELS[folder]
    if len(levels) != len(ins) or len(ins) > 64:
        raise SystemExit(f"import_samples: {title}: {len(ins)} instruments, {len(levels)} levels (64 a cartridge)")
    lengths = {e["id"]: e["length"] for e in entries}
    for i, lv in zip(ins, levels):
        i["level"] = lv
        if len(i["name"]) > 10:
            raise SystemExit(f"import_samples: the instrument {i['name']!r} past 10 characters")
        if not 0 <= i["start"] < i["end"] <= lengths[i["sample"]]:
            raise SystemExit(f"import_samples: {i['name']}: START {i['start']}, END {i['end']}")
    write_json(d / "samples.json", {"format": fmt("sampler"), "version": 2, "name": title, "samples": entries,
                                    "instruments": ins})
    return entries, report


def main():
    ap = argparse.ArgumentParser(description="the demo disk's sample sets")
    ap.add_argument("--from", dest="src", type=Path, required=True, help="the folder of the libraries")
    ap.add_argument("--list", action="store_true", help="print the files needed from each library")
    ap.add_argument("--only", help="make only this set (its folder)")
    a = ap.parse_args()
    if a.list:
        for repo, (folder, *_) in REPOS.items():
            paths = sorted({p for _, _, specs, _ in SETS for s in specs if s["repo"] == repo
                            for p in (s["path"] if isinstance(s["path"], tuple) else (s["path"],))})
            print(f"{folder}: {len(paths)} files")
            for p in paths:
                mark = "" if (a.src / folder / p).exists() else "   (missing)"
                print(f"  {p}{mark}")
        return
    sp = sonic_credits(a.src)
    total = 0
    for folder, title, specs, slots in SETS:
        if a.only and a.only != folder:
            continue
        entries, report = sample_set(folder, title, specs, slots, a.src, sp)
        size = sum(e["length"] for e in entries)
        total += size
        print(f"{title}: {len(entries)} samples, {size:,} sample values")
        print("\n".join(report))
    print(f"import_samples: {total:,} sample values (kept at 8 bits: as many bytes before coding)")


if __name__ == "__main__":
    main()
