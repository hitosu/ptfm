# References and integrity

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 10.

Records refer to each other by number. This file lists what refers to what, how a MOVE, SWAP or DELETE
keeps those references right, and how the store is checked.

## What refers to what

- a cell to an instrument (cartridge:slot, [patterns.md](patterns.md#a-cells-bytes));
- a song position and a FUSION slot to a pattern;
- a sample instrument to a sample of its cartridge;
- the loops to their instruments, one a loop (the loop table); their cells name none
  ([loops.md](loops.md#the-record)).

## MOVE, SWAP or DELETE

MOVE, SWAP or DELETE of an instrument, a cartridge, a pattern or a sample asks first
([../ui/saving.md](../ui/saving.md)):
- **YES:** everything pending is saved; then the change and every record it touches (patterns, songs,
  cartridges, the loops) are written as one save ([store.md](store.md#saving)). A power cut leaves the
  state before or the state after, never a mix.
- **NO:** the references stay as they are. One that points at nothing is drawn red and plays nothing.
- **The built-in instruments never move,** so nothing refers to a number that changes under them.
- **Finding them** decodes every pattern's instruments in place, and reads the songs, the FUSION and
  sampler cartridges and the loops: a few milliseconds for a full store.
- **The cost:** an instrument used in 40 patterns ~0.3 s; the most, a cartridge used in every pattern,
  ~2 s, with a progress bar ([ram.md](ram.md#what-writing-costs)).

## Integrity

- At power-on every record in the user area is checked ([store.md](store.md#power-on)). A record that
  fails falls back to its older version if the flash still holds one; else its object is missing.
  CHECK STORAGE (below) lists either.
- CHECK REFERENCES lists references to nothing; CLEAN UP deletes unused patterns, instruments and
  samples; CHECK STORAGE reads and checks everything (SETTINGS → TOOLS,
  [../ui/screens/settings.md](../ui/screens/settings.md)).
