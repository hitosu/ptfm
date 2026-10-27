# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""How many voices a song sounds at once, estimated from its notes (at most 16, the largest pool).

A voice sounds from its note until its release has rung out 60 dB (catalog.py's times, measured on the
engines), or until a one-shot sample ends or its envelope has fallen to nothing; a release of 40 ms or less
(a drum's choke) is not counted. A track's next note releases its previous one, which rings
on; a note off (`===`), KEY OFF (`Kxx`) and NOTE CUT (`ECx`) release it too. A FUSION note sounds every
cell of its pattern's row, each a voice (a cell's ECx cuts its own). A TB-303 track keeps one voice. Tempo and ticks follow `Fxx`;
`Dxx` and a `Bxx` forwards are followed, a jump back ends the walk. A note with TONE PORTA (`3xx`, `5xy`) on
a held note bends it in its voice, as the replay does. A song that loops at its end is walked
round twice, so what rings on across its loop is counted. SWING, pattern loops and delays are left out.

  count(song, patterns, fusion) -> (most voices, the time of the most, the position and row)
"""
from catalog import CAT, FUSION_CART
from compose import midi, parse_cmd

CHOKE = 0.04                             # seconds: a release this short is not counted


def parse_cell(text):
    """a cell's text -> (note: MIDI, "off" or None; (cart, slot) or None; volume or None; [(code, value)])"""
    f = text.split()
    note = None if f[0] == "---" else "off" if f[0] == "===" else midi(f[0])
    ins = None if f[1] == "..:.." else (int(f[1][:2], 16), int(f[1][3:], 16))
    vol = None if f[2] == ".." else int(f[2], 16)
    return note, ins, vol, [parse_cmd(t) for t in f[3:]]


def rows_of(pattern):
    return [[parse_cell(c) for c in row.split(" | ")] for row in pattern["rows"]]


class Voice:
    __slots__ = ("start", "end", "held", "rel", "track", "ins", "note", "layer")

    def __init__(self, start, natural, rel, track, ins=None):
        self.start, self.end, self.held, self.rel, self.track, self.ins = start, natural, True, rel, track, ins
        self.note, self.layer = None, None

    def release(self, t, rel=None):
        if self.held:
            self.held = False
            r = self.rel if rel is None else rel
            self.end = min(self.end, t + r) if self.end is not None else t + r

    def sounds(self, t):
        """sounding at t; a release of CHOKE or less (a drum's fade, its voice taken at once when needed)
        counts as ended with its note"""
        if not self.held and self.rel <= CHOKE:
            return False
        return self.start <= t and (self.end is None or t < self.end)


def count(song, patterns, fusion, peak=None):
    """song: song.json's object; patterns: number -> pattern object; fusion: slot -> pattern object; peak: a
    list that gets what sounds at the most"""
    peak = [] if peak is None else peak
    tempo, ticks = song["tempo"], song["ticks_a_row"]
    cache = {}
    voices, by_track, current, params = [], {}, {}, {}
    t, most, at, where = 0.0, 0, 0.0, None
    pos, row, seen = 0, 0, set()
    rounds = 2 if song.get("at_the_end") == "loop" else 1
    while pos < len(song["positions"]):
        n = song["positions"][pos]
        if n not in cache:
            cache[n] = rows_of(patterns[n])
        rows = cache[n]
        nxt = None
        while row < len(rows):
            for t_idx, (note, ins, vol, cmds) in enumerate(rows[row]):
                track = t_idx + 1
                for code, value in cmds:
                    if code == 0x0F and value:
                        if value < 0x20:
                            ticks = value
                        else:
                            tempo = value
                    elif code == 0x0D:
                        nxt = (pos + 1, (value >> 4) * 10 + (value & 15))
                    elif code == 0x0B:
                        nxt = (value, 0) if value > pos else (len(song["positions"]), 0)
                if ins is not None:
                    if current.get(track) != ins:
                        params[track] = {}                   # a new instrument drops the parameters
                    current[track] = ins
                pcmds = {c: v for c, v in cmds if 0x40 <= c < 0x80}
                if pcmds:
                    params.setdefault(track, {}).update(pcmds)
                    for v in by_track.get(track, []):        # they act on the sounding notes too
                        if v.held and v.layer is None:
                            v.rel = CAT.sound(v.ins, v.note, params[track])[1]
                cut = next(((c, v) for c, v in cmds if c == 0x0E and v >> 4 == 0xC), None)
                off = note == "off" or any(c == 0x14 for c, _ in cmds)
                if off:
                    for v in by_track.get(track, []):
                        v.release(t)
                if isinstance(note, int) and track in current:
                    tb = CAT.kind(current[track]) == "tb303"
                    old = by_track.get(track, [])
                    glide = any(c in (0x03, 0x05) for c, _ in cmds)
                    if tb and any(v.sounds(t) for v in old):
                        for v in old:
                            v.end, v.held = None, True       # the 303 takes over its voice
                    elif glide and any(v.held for v in old):
                        pass                                 # TONE PORTA: the held note bends, in its voice
                    else:
                        for v in old:
                            v.release(t)
                        layers = []
                        if current[track][0] == FUSION_CART:
                            frows = cache.setdefault(("F", current[track][1]), rows_of(fusion[current[track][1]]))
                            for k, (fnote, fins, _, fcmds) in enumerate(frows[note] if note < len(frows) else []):
                                if isinstance(fnote, int) and fins is not None and fins[0] != FUSION_CART:
                                    lcut = next((v & 15 for c, v in fcmds if c == 0x0E and v >> 4 == 0xC), None)
                                    layers.append((fnote, fins, {c: v for c, v in fcmds if 0x40 <= c < 0x80}, k, lcut))
                        else:
                            layers.append((note, current[track], params.get(track, {}), None, None))
                        new = []
                        for ln, li, lp, layer, lcut in layers:
                            natural, rel = CAT.sound(li, ln, lp)
                            v = Voice(t, None if natural is None else t + natural, rel, track, li)
                            v.note, v.layer = ln, layer
                            if lcut is not None:             # a layer's ECx: cut that many ticks after the note
                                v.release(t + lcut * 2.5 / tempo, 0.005)
                            new.append(v)
                        voices.extend(new)
                        by_track[track] = new
                if cut:
                    tick_len = 2.5 / tempo
                    for v in by_track.get(track, []):
                        v.release(t + cut[1] % 16 * tick_len, 0.005)
            busy = sum(v.sounds(t) for v in voices)
            if busy > most:
                most, at, where = busy, t, (pos, row)
                peak.clear()
                peak.extend(f"T{v.track} {CAT.name(v.ins)} {'held' if v.held else 'rings %.2f s' % (v.end - t)}"
                            for v in voices if v.sounds(t))
            voices = [v for v in voices if v.end is None or v.end > t]
            t += ticks * 2.5 / tempo
            row += 1
            if nxt:
                break
        if nxt:
            pos, row = nxt
        else:
            pos, row = pos + 1, 0
        if pos >= len(song["positions"]) and rounds > 1:  # (looping: round once more, from its loop)
            rounds -= 1
            pos, row, seen = song.get("loop_from", 0), 0, set()
        key = (pos, row)
        if key in seen:
            break
        seen.add(key)
    return most, at, where
