# Settings

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 9; QUANTIZE, [the loop station proposal](../proposals/202610240236-loops-station.md), section 5.

The settings record keeps what SETTINGS sets ([../ui/screens/settings.md](../ui/screens/settings.md))
and the user's own GATE patterns ([../ui/screens/fx.md](../ui/screens/fx.md)). Kind 0x30, number `00`,
133 B, saved by SAVE like everything else.

| Bytes | Field |
|---|---|
| 0-12 | OUTPUT (0 STEREO, 1 MONO, 2 SYNC PULSE L, 3 SYNC PULSE R), VOICES (the count: 8, 12 or 16; anything else reads as 16), THEME, KEYS VOLUME, KEYS OCTAVE, KNOB SPEED, HOLD DELAY (2 B), COMBO GRACE, PLAYBACK SCROLL, POWER-ON SCREEN, the screen at the last SAVE (for POWER-ON SCREEN = LAST), TUNING (A4 from 430.0 Hz in 0.1 Hz steps: 0-200, so up to 450.0) |
| 13-108 | your 16 GATE patterns: 16 steps of 3 bits (OFF, ON, ×2, ×3, ×4), 6 B each |
| 109 | bits 1-0 COUNT-IN, the EDITOR's: 0 OFF, 1 1 BAR, 2 2 BARS, 3 KEY; bits 4-2 QUANTIZE, the EDITOR's takes': 0 ROW, 1 2 ROWS, 2 3 ROWS, 3 4 ROWS, 4 REAL TIME (5-7 not written, read as ROW); bits 7-5 0 |
| 110 | ARP RHYTHM: the rhythm's number in the built-in list (0 `#`, 1 `#_`, 2 `#__`, 3 `#___`, 4 `##_`, 5 `###_`, 6 `##__`, 7 `#_##_`, 8 `#` and 7 rests, 9 `#` and 15 rests; new ones at the end) |
| 111 | LINE GAIN: dB + 24, 0-36 (−24 to +12 dB) |
| 112 | USB GAIN: dB + 24, 0-36 (−24 to +12 dB) |
| 113 | ARP PATTERN: 0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM, 4 AS PLAYED |
| 114 | ARP LATCH: 0 OFF, 1-8 notes |
| 115 | MIDI IN CHANNEL: 0 ALL, 1-16 ([../connections.md](../connections.md)) |
| 116 | MIDI OUT: 0 KEYS, 1 OFF, 2 ALL |
| 117 | MIDI OUT CHANNEL: the channel − 1, 0-15 |
| 118 | SYNC: 0 INTERNAL, 1 EXTERNAL |
| 119 | CLOCK OUT: 0 OFF, 1 ON |
| 120 | SYNC PULSE's side as byte 0 has it (0 none, 1 left, 2 right), written for readers from before SYNC PULSE became OUTPUT's; read only when byte 0 is 0 or 1, as SYNC PULSE L / R |
| 121-122 | the current instrument at the last SAVE: its cartridge, its slot |
| 123 | bit 0: bytes 121-122 are set (clear: F0:00, the first built-in instrument); bit 1: SECOND CORE OFF (clear: ON, [../second-core.md](../second-core.md)); bit 2: PRESETS LIST OFF (clear: ON, [../ui/screens/settings.md](../ui/screens/settings.md)); bits 3-7 0 |
| 124 | KEYS GAIN: dB, signed, −24 to +12 |
| 125 | KEYS: 0 THROUGH FX, 1 DRY |
| 126 | bit 0: byte 127 is set (clear: the EDITOR's own pattern); bits 1-7 0 |
| 127 | the numbered pattern the EDITOR was on at the last SAVE |
| 128-132 | FX MIXER's KEYS column ([../ui/screens/fx.md](../ui/screens/fx.md#mixer)), the keys' mixer bytes as a track's ([songs.md](songs.md#a-tracks-mixer-bytes)), each stored so that 0 is its default: PAN XOR `80`, DUCK, then the CHORUS, DELAY and REVERB SENDS each as `7F` − the send. Their LEVEL is KEYS GAIN (byte 124) |

Bytes 115-132 are 0 at their defaults, so a record from before them (128 B, or shorter) reads as the
defaults; so is QUANTIZE (byte 109's bits 4-2, which a record from before it holds as 0: ROW).

- **The screen at the last SAVE:** the FM-1 cannot write at power-off, so LAST means the screen when
  last saved. With POWER-ON SCREEN = LAST, SAVE writes the record again when the screen differs from the one
  it holds.
- **The current instrument at the last SAVE:** the one the keys play and PRESETS turns
  ([../ui/screens/instruments.md](../ui/screens/instruments.md)) is the current one again at power-on; F0:00
  at first, and if it is gone, the first cartridge's first instrument. Picking another is no change of its
  own (REVERT leaves it, the SAVE LED stays as it was); SAVE writes the record when it differs from the
  one held, even with nothing else to save.
- **The EDITOR's pattern at the last SAVE:** the pattern the EDITOR edits
  ([../ui/screens/editor.md](../ui/screens/editor.md#the-editors-own-pattern)) is the EDITOR's again at
  power-on: its own pattern at first, and when the numbered one is gone. Moving to another is no change of its
  own, as the current instrument; SAVE writes the record when it differs from the one held.
- **Built into the firmware, not stored:** the 16 built-in GATE patterns, the themes, the help.
- **Never stored:** mutes, the clipboard. There is no UNDO to keep; REVERT takes back what the flash holds
  ([../ui/saving.md](../ui/saving.md#revert)).
