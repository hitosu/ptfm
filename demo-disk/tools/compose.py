# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Writing the demo disk's songs and FUSION patterns from Python, in its notation (../README.md).

  pool = Pool()                                          # the disk's patterns, 00-FF
  s = Song(pool, "NAME", "what it is", tempo=120, tracks=8)
  p = s.pattern(64, "what it plays")                     # a pattern of the song's tracks
  p.note(1, 0, "C-4", KICK, 0x70)                        # track (1..), row, note, instrument (cart, slot), volume
  p.note(2, 0, "A-2", BASS, None, "ACC", "*41:60")       # ... and up to four commands
  p.hits(1, KICK, "x...x...x...x...", 0x70)              # a drum line, one character a row, repeated
  p.melody(5, LEAD, "E5/4,447 D5/2 B4/2 ./4 A4/4", 0x60) # notes, their rows, their commands
  p.chords(6, PAD, ["Am7", "Fmaj7"], 32, 0x50)            # chords on tracks 6.., each held 32 rows
  p.sweep(3, 0, 64, "*41", 0x20, 0x70)                    # a parameter turned over rows 0-63
  s.play(p, p, q)                                         # the positions
  s.mix(1, level=110, pan=128); s.fx("delay", on=True, mix=40)
  s.write(DISK / "songs" / "name")                        # song.json + patterns.json

Notes are "C-4", "C4", "C#4", "Db4" or MIDI numbers (C-4 = 60). Commands are written as the notation
writes them: "A04", "~-4", "ACC", "*41:60", "=80:70", "#A7:40", "!D1:08".
"""
from common import OWN_LICENSE, fmt, note_name, write_json

SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
OFF = -1
TRACKER = {*range(0x12), 0x14, 0x19, 0x1B, 0x1D, 0x1E, 0x21, 0x22}
VOLCOL = {"-": 0x24, "+": 0x25, "D": 0x26, "U": 0x27, "S": 0x28, "V": 0x29, "P": 0x2A, "<": 0x2B, ">": 0x2C, "M": 0x2D}
OWN = {"ACC": 0xC0, "SLD": 0xC1, "A+S": 0xC2, "LEG": 0xD3}
GROUPS = {"*": range(0x40, 0x80), "=": range(0x80, 0x85), "#": range(0x90, 0xB0), "!": range(0xC3, 0xD3)}
QUALITIES = {"": (0, 4, 7), "m": (0, 3, 7), "5": (0, 7, 12), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10),
             "maj7": (0, 4, 7, 11), "6": (0, 4, 7, 9), "m6": (0, 3, 7, 9), "9": (0, 4, 7, 10, 14),
             "m9": (0, 3, 7, 10, 14), "maj9": (0, 4, 7, 11, 14), "add9": (0, 4, 7, 14), "madd9": (0, 3, 7, 14),
             "sus2": (0, 2, 7), "sus4": (0, 5, 7), "7sus4": (0, 5, 7, 10), "dim": (0, 3, 6), "dim7": (0, 3, 6, 9),
             "m7b5": (0, 3, 6, 10), "aug": (0, 4, 8), "13": (0, 4, 10, 14, 21), "7b9": (0, 4, 7, 10, 13),
             "m11": (0, 3, 7, 10, 17), "6/9": (0, 4, 7, 9, 14)}

# the FX setup's defaults (specs/ui/screens/fx.md): the LIMITER on, the rest off
CHAIN = ["GATE", "FILTER", "LO-FI", "SENDS", "CHORUS", "DELAY", "REVERB", "DRIVE", "LIMITER"]
FX_DEFAULTS = {
    "gate": {"on": False, "pattern": 1, "rate": "1/16", "depth": 100, "smooth": 20},
    "filter": {"on": False, "cut": "OFF", "reso": 30},
    "drive": {"on": False, "drive": 30, "tone": 50},
    "lofi": {"on": False, "bits": 8, "rate": 22, "dust": 20},
    "chorus": {"on": False, "mode": "I", "rate": 30, "depth": 50, "mix": 50},
    "delay": {"on": False, "time": "1/8.", "feedback": 40, "tone": 60, "ping_pong": False, "mix": 30},
    "reverb": {"on": False, "size": 60, "damp": 40, "pre_delay": 10, "mix": 25},
    "limiter": {"on": True, "ceiling": -1, "release": 100},
    "duck": {"on": False, "source": 1, "depth": 60, "release": 150},
    "metronome": {"on": False, "tempo": "FOLLOW", "beats_a_bar": 4, "sound": "BEEP", "level": 60},
}
MIX_DEFAULT = {"level": 127, "pan": 128, "duck": 0, "chorus": 0, "delay": 0, "reverb": 0}


def midi(n):
    """"C-4", "C4", "C#4", "Db4" or a number -> MIDI (C-4 = 60)"""
    if isinstance(n, int):
        return n
    s = n.replace("-", "")
    semi, rest = SEMI[s[0]], s[1:]
    while rest and rest[0] in "#b":
        semi += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + semi


def chord_notes(symbol, octave=3):
    """"Am7" -> its notes from the root in `octave` (MIDI); "C/G": the bass note below"""
    sym, _, bass = symbol.partition("/") if "/9" not in symbol else (symbol, "", "")
    k = 2 if len(sym) > 1 and sym[1] in "#b" else 1
    root = midi(sym[:k] + str(octave))
    notes = [root + i for i in QUALITIES[sym[k:]]]
    if bass:
        b = midi(bass + str(octave))
        notes = [b - 12 if b >= root else b] + notes
    return notes


def voice(notes, center):
    """the chord's notes each moved by octaves to lie within a fifth or so of `center`, low to high"""
    out = []
    for n in notes:
        while n < center - 6:
            n += 12
        while n > center + 6:
            n -= 12
        out.append(n)
    return sorted(set(out))


def root_of(symbol, octave=2):
    sym = symbol.partition("/")[0] if "/9" not in symbol else symbol
    k = 2 if len(sym) > 1 and sym[1] in "#b" else 1
    return midi(sym[:k] + str(octave))


def parse_cmd(tok):
    """a command as written -> (code, value); SystemExit if it is not one"""
    if tok in OWN:
        return OWN[tok], 0
    if len(tok) == 3 and tok[0] == "~" and tok[1] in VOLCOL:
        return VOLCOL[tok[1]], int(tok[2], 16)
    if len(tok) == 6 and tok[0] in GROUPS and tok[3] == ":":
        code, value = int(tok[1:3], 16), int(tok[4:6], 16)
        if code in GROUPS[tok[0]]:
            return code, value
    if len(tok) == 3 and tok[0] in "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        code = int(tok[0], 36)
        if code in TRACKER:
            return code, int(tok[1:], 16)
    raise SystemExit(f"compose: not a command: {tok!r}")


def cmd_key(code, value):
    """what makes two commands of a cell the same: the code, and for Exy (0x0E) also x, the effect it names
    (E93 RETRIG and EC9 NOTE CUT are two commands)"""
    return (code, value >> 4) if code == 0x0E else code


def ref_text(ins):
    return "..:.." if ins is None else f"{ins[0]:02X}:{ins[1]:02X}"


class Pool:
    """the disk's patterns: numbers 00-FF, handed out in turn (or from a given start)"""
    def __init__(self):
        self.used = {}

    def take(self, owner, at=None):
        n = at if at is not None else next(i for i in range(256) if i not in self.used)
        if n in self.used or not 0 <= n <= 255:
            raise SystemExit(f"compose: pattern {n:02X} taken by {self.used.get(n)}")
        self.used[n] = owner
        return n


class Pattern:
    def __init__(self, number, tracks, rows, what, edit_step=1, rpb=4):
        if not 1 <= rows <= 256 or not 1 <= tracks <= 16:
            raise SystemExit(f"compose: a pattern of {tracks} tracks and {rows} rows (1..16, 1..256)")
        self.number, self.ntrk, self.what, self.edit_step, self.rpb = number, tracks, what, edit_step, rpb
        self.cells = [[None] * tracks for _ in range(rows)]

    @property
    def rows(self):
        return len(self.cells)

    def _cell(self, track, r):
        if not 1 <= track <= self.ntrk:
            raise SystemExit(f"compose: {self.what}: track {track} (1..{self.ntrk})")
        c = self.cells[r][track - 1]
        if c is None:
            c = self.cells[r][track - 1] = [None, None, None, []]
        return c

    def note(self, track, r, n, ins, vol=None, *cmds):
        """a note (None: none, OFF: a note off) with its instrument, volume and commands"""
        if not 0 <= r < len(self.cells):
            return
        c = self._cell(track, r)
        if n is not None:
            m = OFF if n == OFF else midi(n)
            if m != OFF and not 12 <= m <= 127:
                raise SystemExit(f"compose: {self.what}: note {n} ({m}) out of range")
            c[0] = m
        if ins is not None:
            c[1] = tuple(ins)
        if vol is not None:
            c[2] = max(0, min(127, round(vol)))
        self.cmd(track, r, *cmds)

    def cmd(self, track, r, *cmds):
        """commands added to a cell (made if empty); the same command already there (cmd_key) is replaced"""
        if not cmds or not 0 <= r < len(self.cells):
            return
        c = self._cell(track, r)
        for tok in cmds:
            key = cmd_key(*parse_cmd(tok))
            c[3] = [t for t in c[3] if cmd_key(*parse_cmd(t)) != key] + [tok]
        if len(c[3]) > 4:
            raise SystemExit(f"compose: {self.what}: row {r:02X} track {track}: {len(c[3])} commands (4 at most)")

    def off(self, track, r):
        if 0 <= r < len(self.cells):
            c = self.cells[r][track - 1]
            if c is None or c[0] is None:
                self._cell(track, r)[0] = OFF

    def clear(self, track, start=0, end=None):
        for r in range(start, len(self.cells) if end is None else end):
            self.cells[r][track - 1] = None

    def sweep(self, track, start, end, group_code, v0, v1, step=1):
        """a parameter (or mixer, FX) turned from v0 to v1 over rows start..end-1: a command every
        `step` rows; group_code: "*41", "=80", "#94" """
        n = max(1, (end - 1 - start) // step)
        for k, r in enumerate(range(start, end, step)):
            v = round(v0 + (v1 - v0) * min(k, n) / n)
            self.cmd(track, r, f"{group_code}:{max(0, min(255, v)):02X}")

    # ------------------------------------------------------------- helpers ---
    def hits(self, track, ins, steps, vol, start=0, end=None, note="C-4", soft=26, loud=14, cmds=()):
        """a drum line: one character a row, repeated from `start` to `end`: x a hit, X louder, o softer,
        r a ratchet (a retrigger, `E93`), . none; spaces are ignored"""
        steps = steps.replace(" ", "")
        end = len(self.cells) if end is None else end
        for r in range(start, end):
            c = steps[(r - start) % len(steps)]
            if c in "xXor":
                self.note(track, r, note, ins, vol + {"x": 0, "X": loud, "o": -soft, "r": 0}[c],
                          *(("E93",) if c == "r" else ()), *cmds)

    def melody(self, track, ins, text, vol, start=0, up=0, gate=None, legato=False):
        """notes in turn, each "NOTE/rows" (or "NOTE/rows@volume"), "./rows" a rest; a note's rows default
        to the last given; commands follow a comma ("E5/4,447,~V4"). A note off ends each note (after
        `gate` rows if given), unless legato. Returns the row after the last."""
        r, length = start, 4
        for tok in text.split():
            parts = tok.split(",")
            name, _, rest = parts[0].partition("/")
            nv = vol
            if rest:
                ln, _, v = rest.partition("@")
                length = int(ln)
                nv = int(v, 16) if v else vol
            if name != ".":
                self.note(track, r, midi(name) + up, ins, nv, *parts[1:])
                if not legato:
                    self.off(track, r + (gate if gate else length))
            else:
                self.cmd(track, r, *parts[1:])
            r += length
        return r

    def chords(self, first, ins, symbols, each, vol, center=60, start=0, tracks=None, gate=None, cmds=()):
        """chords held `each` rows each, a note a track from `first` (or `tracks`), voiced round `center`"""
        r = start
        for sym in symbols:
            notes = voice(chord_notes(sym), center) if isinstance(sym, str) else [midi(n) for n in sym]
            trs = tracks or list(range(first, first + len(notes)))
            for t, n in zip(trs, notes):
                self.note(t, r, n, ins, vol, *cmds)
                if gate:
                    self.off(t, r + gate)
            for t in trs[len(notes):]:
                self.off(t, r)
            r += each
        return r

    def stabs(self, first, ins, symbols, each, rhythm, vol, center=60, start=0, tracks=None, gate=1):
        """each chord over `each` rows played on the rhythm's x's (a string as in hits()), short"""
        rhythm = rhythm.replace(" ", "")
        r = start
        for sym in symbols:
            notes = voice(chord_notes(sym), center) if isinstance(sym, str) else [midi(n) for n in sym]
            trs = tracks or list(range(first, first + len(notes)))
            for k in range(each):
                c = rhythm[k % len(rhythm)]
                if c in "xXo":
                    v = vol + {"x": 0, "X": 12, "o": -20}[c]
                    for t, n in zip(trs, notes):
                        self.note(t, r + k, n, ins, v)
                        self.off(t, r + k + gate)
            r += each
        return r

    def arp(self, track, ins, symbols, each, vol, step=2, center=64, start=0, pattern=None, gate=None, accent=0):
        """each chord's notes in turn, every `step` rows, over `each` rows a chord; `pattern`: indexes into
        the voiced chord (default up; past its size: an octave up)"""
        r = start
        for sym in symbols:
            notes = voice(chord_notes(sym), center) if isinstance(sym, str) else [midi(n) for n in sym]
            seq = pattern or list(range(len(notes)))
            for i, rr in enumerate(range(r, r + each, step)):
                k = seq[i % len(seq)]
                n = notes[k % len(notes)] + 12 * (k // len(notes))
                self.note(track, rr, n, ins, vol + (accent if (rr - start) % (4 * step) == 0 else 0))
                if gate:
                    self.off(track, rr + gate)
            r += each
        return r

    def bassline(self, track, ins, symbols, each, figure, vol, octave=2, start=0, gate=None):
        """a figure under each chord: (row in the chord's span, semitones from its root[, volume change
        [, commands]])"""
        r = start
        for sym in symbols:
            root = root_of(sym, octave)
            for f in figure:
                f = tuple(f)
                rr, iv, dv, cmds = f + ((0, ()) if len(f) == 2 else ((),) if len(f) == 3 else ())
                if rr < each:
                    self.note(track, r + rr, root + iv, ins, vol + dv, *cmds)
                    if gate:
                        self.off(track, r + rr + gate)
            r += each
        return r

    def roll(self, track, ins, vol, start, end, note="C-4", rise=40):
        """a build: quarters, eighths, then sixteenths, louder as it goes, from `start` to `end`"""
        n = end - start
        for r in list(range(start, start + n // 2, 4)) + list(range(start + n // 2, start + 3 * n // 4, 2)) + \
                list(range(start + 3 * n // 4, end)):
            self.note(track, r, note, ins, vol - rise + (r - start) * rise // n)

    def ends(self, tracks, r=None):
        """what still sounds on these tracks stops at row r (the last row)"""
        for t in tracks:
            self.off(t, len(self.cells) - 1 if r is None else r)

    def cell_count(self):
        return sum(c is not None for row in self.cells for c in row)

    def text(self):
        out = []
        for cells in self.cells:
            parts = []
            for c in cells:
                if c is None:
                    parts.append("--- ..:.. ..")
                    continue
                note = "---" if c[0] is None else "===" if c[0] == OFF else note_name(c[0])
                s = f"{note} {ref_text(c[1])} {'..' if c[2] is None else f'{c[2]:02X}'}"
                parts.append(" ".join([s] + c[3]))
            out.append(" | ".join(parts))
        return out

    def json(self):
        return {"number": self.number, "tracks": self.ntrk, "edit_step": self.edit_step, "what": self.what,
                "rows": self.text()}


def fx_setup(**changes):
    """the FX setup: the defaults, `routing`, `chain`, and each effect's changes ({"on": True, "mix": 40})"""
    fx = {"routing": changes.pop("routing", "global"), "chain": list(changes.pop("chain", CHAIN))}
    for name, knobs in FX_DEFAULTS.items():
        fx[name] = dict(knobs)
        unknown = set(changes.get(name, {})) - set(knobs)
        if unknown:
            raise SystemExit(f"compose: the FX {name} has no {sorted(unknown)}")
        fx[name].update(changes.get(name, {}))
    if set(changes) - set(FX_DEFAULTS):
        raise SystemExit(f"compose: no such effect: {sorted(set(changes) - set(FX_DEFAULTS))}")
    if sorted(fx["chain"]) != sorted(CHAIN) or fx["chain"][-1] != "LIMITER":
        raise SystemExit(f"compose: a chain of {fx['chain']} (the 9 items, the LIMITER last)")
    return fx


class Song:
    def __init__(self, pool, name, what, tempo, tracks, ticks=6, rpb=4, swing=50, at_end="loop", loop_from=0,
                 slides="linear", global_volume=64, license=OWN_LICENSE):
        if len(name) > 16:
            raise SystemExit(f"compose: {name!r}: a song's name is 16 characters at most")
        if not 32 <= tempo <= 300 or not 1 <= ticks <= 31 or not 1 <= rpb <= 16 or not 50 <= swing <= 75:
            raise SystemExit(f"compose: {name}: tempo {tempo}, ticks {ticks}, rows a beat {rpb}, swing {swing}")
        self.pool, self.name, self.what, self.ntrk = pool, name, what, tracks
        self.tempo, self.ticks, self.rpb, self.swing = tempo, ticks, rpb, swing
        self.at_end, self.loop_from, self.slides, self.gvol, self.license = at_end, loop_from, slides, global_volume, license
        self.pats, self.order = [], []
        self.mixer = [dict(MIX_DEFAULT) for _ in range(tracks)]
        self.fxs = fx_setup()

    def pattern(self, rows=64, what="", tracks=None, edit_step=1):
        p = Pattern(self.pool.take(self.name), tracks or self.ntrk, rows, what, edit_step, self.rpb)
        self.pats.append(p)
        return p

    def play(self, *pats):
        self.order += [p.number for p in pats]

    def mix(self, track, **values):
        for k, v in values.items():
            if k not in MIX_DEFAULT:
                raise SystemExit(f"compose: {self.name}: the mixer has no {k}")
            self.mixer[track - 1][k] = v

    def fx(self, **changes):
        self.fxs = fx_setup(**changes)

    def seconds(self):
        """the song's length at its own tempo, ticks and rows (commands that change them aside)"""
        rows = sum(len(next(p for p in self.pats if p.number == n).cells) for n in self.order)
        return rows * self.ticks * 2.5 / self.tempo

    def json(self):
        last = max((i for i, m in enumerate(self.mixer) if m != MIX_DEFAULT), default=-1)
        return {"format": fmt("song"), "version": 2, "name": self.name, "what": self.what, "license": self.license,
                "tempo": self.tempo, "ticks_a_row": self.ticks, "rows_a_beat": self.rpb, "swing": self.swing,
                "at_the_end": self.at_end, "loop_from": self.loop_from, "slides": self.slides,
                "fast_volume_slides": False, "global_volume": self.gvol, "positions": self.order,
                "mixer": self.mixer[:last + 1], "fx": self.fxs, "patterns": "patterns.json"}

    def write(self, d):
        write_json(d / "song.json", self.json())
        write_json(d / "patterns.json", {"format": fmt("patterns"), "version": 2,
                                         "patterns": [p.json() for p in self.pats]})
