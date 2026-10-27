# The MIDI map

Status: **settled** (2026-10-10). The record: [the connections proposal](../proposals/202610101831-connections.md),
section 4; what it does: [../connections.md](../connections.md#midi-learn).

The MIDI map keeps what MIDI learn mapped: an outside controller's knobs and pads onto the FM-1's own
controls. Kind 0x31, number `00`, saved by SAVE like everything else; no record means no mappings.

## The record

| Bytes | Field |
|---|---|
| 0 | how many mappings, 0-64 |
| 1-3 | 0 |
| 4- | the mappings, 4 B each, in the order learned |

**A mapping:**

| Byte | Field |
|---|---|
| 0 | bits 3-0 the MIDI channel (0-15: channel 1-16); bits 5-4 the message: 0 a control change, 1 a note; bits 7-6 0 |
| 1 | the CC's or the note's number, 0-127 |
| 2 | the FM-1 control ([below](#the-fm-1s-controls)) |
| 3 | its kind: 0 an absolute knob; 1 a relative knob, two's complement; 2 a relative knob, offset 64; 3 a relative knob, sign and magnitude; 4 a momentary pad; 5 a toggle pad |

- **One MIDI control maps once:** learned again, its old mapping goes. An FM-1 control may have several.
- **Size:** 4 B and 4 B a mapping, 260 B at the most.

## The FM-1's controls

The numbers are names, fixed for good ([README.md](README.md#extending)); a new control takes the next
free number.

| Number | Control |
|---|---|
| 0-6 | the knobs: SELECT, ALGORITHM, K1, K2, K3, K4, PRESETS |
| 7 | MASTER |
| 8-21 | the buttons, 8 + the button's own number: OCT- 8, OCT+ 9, FX 10, SEL 11, ENV 12, LFO 13, EDIT 14, GLO 15, HOME 16, SAVE 17, ARP 18, SEQ 19, PLAY 20, REC 21 |
| 22-48 | the keys, 22 + the key: key 0 (F, the lowest) 22 … key 26 (G, the highest) 48 |
| 49-255 | free |

- A knob's kind is 0-3, a button's or a key's 4-5; MASTER takes only 0.
