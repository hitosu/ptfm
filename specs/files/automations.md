# Automations

AUTOMATIONS' file ([../ui/screens/automations.md](../ui/screens/automations.md)): 16 slots, each the FM-1's input as
it was worked, at its times. **Kind 0x32**, number 00, version 1: one file of the kind, **its records its slots'
parts**, sub = the slot × 16 + the part (slot 0-15, part 0-15). An empty slot has none. Each part is a record of at most
4 KB (a 4,080-byte payload), so a slot holds about 64 KB (the edit memory holds less); a recording rewrites only its
slot's last part, and a slot is copied or cleared by its parts.

| Bytes | Field |
|---|---|
| 0 | part 0 only: the screen it was recorded on, a number fixed for good (each screen's place in HOME's round as it is now; a round ordered otherwise later keeps these numbers): 0 STATUS, 1 SONGS, 2 EDITOR, 3 INSTRUMENTS, 4 PLAYBACK, 5 VISUAL, 6 FX, 7 LOOPS, 8 DJ, 9 SETTINGS (another number: it plays on the screen AUTOMATIONS was opened from) |
| then 2 | the events in this part, little-endian |
| then | the events, bit-packed: whole events in each part (the next part starts on a byte with the next event); the last byte padded with 0 |

**A part closes** once its next event, at its longest (37 bits), might not fit: when its head (3 B in part 0, 2 B
after) and its events' bits plus 37, in bytes rounded up, would pass 4,080, the event starts the next part. So two
writers of the same events write the same bytes.

**The events** are one after another, bits taken from each byte's bit 0 up (as the instruments' packed fields); a
time counts on across a part's end. An event is its time, then its action:

| Time since the event before (the first: since time 0) | Bits |
|---|---|
| `0` + 5 bits: 0-31 ms (several in one pass: 0) | 6 |
| `10` + 8 bits: 32-287 ms | 10 |
| `110` + 12 bits: 288 ms to 4,383 ms | 15 |
| `1110` + 16 bits: 4,384 ms to 69,919 ms | 20 |
| `1111` + 24 bits: 69,920 ms on (16,847,135 ms, 4.7 h, at most) | 28 |

The prefix's bits come first (a 1 a step, then the 0 that ends it, none after four 1s), then the number, from its
least significant bit; the time is the class's first plus the number.

| Action | Bits |
|---|---|
| `0` + 6 bits: pressed: key 0-26 (F-3 .. G-5), button 27-40 (OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC) | 7 |
| `10` + 6 bits: let go (the same numbers) | 8 |
| `11` + 3 bits the knob (0 SELECT, 1 ALGORITHM, 2-5 K1-K4, 6 PRESETS; 7 kept) + 4 bits its clicks, −8 to +7 (two's complement, never 0) | 9 |

**Presses and releases are explicit** (never toggles), so a stream cut or copied anywhere leaves no key down: playback
lets go at its end of all it holds, and a release of what is not down is skipped. A press or release of 41-63 and
knob 7 are kept for later: a reader skips such an event. A note is about 4.4 B (pressed 250 ms after the event before,
held 180 ms), busy playing about 1 KB a minute.

**The EDITOR** keeps it as a system item ([../editor.md](../editor.md)); an FM-1 that does not read the kind keeps the
records as they are.
