# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Keys and chords for writing the songs: notes moved along a key's scale, so a riff or an arpeggio follows
the chords without leaving the key.

  K = Key("A", "minor")
  K.up(midi("A-2"), 2)          -> C-3: two steps of the scale up
  K.chord("F-3")                -> F-3 A-3 C-4: the triad the key builds on F
  K.line("F-2", [0, 7, 4, 2])   -> F-2 F-3 C-3 A-2: scale steps from a root, an octave being 7
"""
from compose import midi

MODES = {"major": (0, 2, 4, 5, 7, 9, 11), "minor": (0, 2, 3, 5, 7, 8, 10), "dorian": (0, 2, 3, 5, 7, 9, 10),
         "phrygian": (0, 1, 3, 5, 7, 8, 10), "mixolydian": (0, 2, 4, 5, 7, 9, 10),
         "harmonic": (0, 2, 3, 5, 7, 8, 11)}
PCS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


class Key:
    def __init__(self, tonic, mode="minor"):
        pc = PCS[tonic[0]] + sum(1 if c == "#" else -1 for c in tonic[1:])
        self.tonic, self.mode = pc % 12, mode

    def _index(self, note):
        """(octave, step) of a note in the key (a note outside it: the step below)"""
        n = midi(note)
        rel = (n - self.tonic) % 12
        step = max(k for k, s in enumerate(MODES[self.mode]) if s <= rel)
        return (n - self.tonic - rel) // 12, step, rel - MODES[self.mode][step]

    def up(self, note, steps):
        """the note `steps` steps of the scale up (down if negative); a note outside the key keeps its
        distance from the step under it"""
        octv, step, extra = self._index(note)
        k = step + steps
        o, s = octv + k // 7, k % 7
        return self.tonic + 12 * o + MODES[self.mode][s] + extra

    def line(self, root, steps):
        """scale steps from a root (None stays None)"""
        return [None if s is None else self.up(root, s) for s in steps]

    def chord(self, root, size=3):
        """the key's chord on a root: thirds stacked on it (3: a triad, 4: a seventh chord)"""
        return [self.up(root, 2 * k) for k in range(size)]
