# References and integrity

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 10.

Records refer to each other by number. This file lists what refers to what, how a MOVE or SWAP keeps those
references right, what a reference to something deleted does, and how the store is checked. The record of the
change to DELETE: [the EDITOR instruments proposal](../proposals/202610311848-editor-instruments.md), section 4; of
samples named by number: [the independent samples proposal](../proposals/202611021310-independent-samples.md).

## What refers to what

- a cell to an instrument (cartridge:slot, [patterns.md](patterns.md#a-cells-bytes));
- a song position and a FUSION slot to a pattern;
- a sample instrument to a sample, by its number `00`-`FF`, whatever sampler it is in
  ([instruments.md](instruments.md#samples));
- the loops to their instruments, one a loop (the loop table); their cells name none
  ([loops.md](loops.md#the-record));
- a FAVORITES slot to an instrument (cartridge:slot, [instruments.md](instruments.md#favorites)).

Nothing refers to a FAVORITES cartridge or a favourite's place: a favourite is only ever the one referring. **A
reference that names a FAVORITES cartridge's number names nothing:** a cell, a loop or a favourite left naming a
number a FAVORITES cartridge took later (after a DELETE, or under an older firmware) plays nothing and is listed by
CHECK REFERENCES.

## MOVE and SWAP

MOVE or SWAP of an instrument, a cartridge or a pattern asks first ([../ui/saving.md](../ui/saving.md)). A sample
keeps its number on the FM-1; on the EDITOR page's FM-1 panel it can take another, the instruments naming it
following ([../editor.md](../editor.md)). That panel's SET NUMBER is a cartridge's MOVE or SWAP, always as YES below
([../editor.md](../editor.md#moving-items)).
- **YES:** everything pending is saved; then the change and every record it touches (patterns, songs,
  cartridges, the loops' table, the favourites) are written as one save ([store.md](store.md#saving)). A power cut
  leaves the state before or the state after, never a mix.
- **NO:** the references stay as they are, naming the place: one left naming nothing is grey and silent
  ([below](#a-reference-to-nothing)).
- **A MOVE onto an empty place is a SWAP with it:** the references that still name that place, grey, go to the place
  left, so they stay grey; the question counts them as uses of the place, as SWAP's counts both sides.
- **The built-in instruments never move,** so nothing refers to a number that changes under them.
- **The favourites:** MOVE or SWAP of an instrument or a cartridge, answered YES, rewrites the favourites that name
  it in the same save; answered NO, they keep naming the same place, as cells do. A favourite counts as a use in the
  questions (`… IS USED 3 TIMES`, `UPDATING 3 REFERENCES`). A FAVORITES cartridge's own MOVE, SWAP or DELETE, and a
  favourite's, ask nothing: nothing refers to them.

## A reference to nothing

- **DELETE never rewrites references,** of an instrument, a slot range, a cartridge (built-in or not) or a pattern.
  When something uses what goes it asks `… IS USED 12 TIMES`, `DELETE ANYWAY?`, DELETE or KEEP (HOME as KEEP)
  ([../ui/saving.md](../ui/saving.md)); DELETE is then an edit like any other: SAVE writes it, REVERT ALL takes it
  back before SAVE.
  - **A pattern deleted** stays named by the song positions and FUSION slots that played it: such a position plays
    64 empty rows ([songs.md](songs.md)), such a slot nothing (`D0 MISSING`).
  - **A sample deleted** keeps its rule ([instruments.md](instruments.md#deleting)): the instruments that play it,
    in every sampler, go with it (in one save when there is room), and the cells that named them go grey.
  - **A sample instrument naming a missing sample** (a partial upload, a save cut between a sample's deletion and
    its samplers', a REVERT or a PASTE that brings back an instrument whose sample is gone) is a reference to nothing
    too: it plays nothing, its page says `THE SAMPLE IS MISSING`, and CHECK REFERENCES lists it (`SAMPLER 01:05:
    SAMPLE 23`).
- **An instrument that does not exist plays as a silent engine** (an empty slot, a deleted cartridge, a deleted
  built-in), in whichever way a cell names it ([patterns.md](patterns.md#a-cells-bytes)), and on a loop's lane and
  the keys too: the note does everything a note does but sound.
- **It is drawn grey** ([../ui/colours.md](../ui/colours.md)): a cell's instrument, a loop's, a favourite's
  `MISSING`, a song position's or a FUSION slot's missing pattern, a sample instrument's missing sample.
- **What names an old sampler** (the old form, [instruments.md](instruments.md#the-old-form)) is not a reference to
  nothing: it is kept for a page to convert. CHECK REFERENCES lists each old sampler once, `CARTRIDGE 03: OLDER
  FORM`, after the references to nothing and not counted among them; MISSING REFERENCES leaves it.
- **It names the place, not what was there:** an instrument put in that place later (PASTE, REPLACE, MOVE, a
  cartridge made under that number) plays in those cells again. So **wherever a new number is taken, the numbers
  references still name are passed over** (a cartridge's NEW and CLONE, every new or cloned pattern, an item landing
  at a free number on the EDITOR page's FM-1 panel, a sample's number there, which passes over the numbers sample
  instruments still name); when every free number is named, the first free one is taken.
- **`FF` is no pattern, not a reference to nothing:** a song position or a FUSION slot set to `--` names none on
  purpose. It is not grey and CHECK REFERENCES does not list it; it plays as a missing pattern does.
- **CLEAN UP clears them when asked** (its `MISSING REFERENCES` line, [../ui/screens/settings.md](../ui/screens/settings.md)):
  the favourites naming nothing emptied, the song positions and FUSION slots naming a missing pattern set to `--`,
  the sample instruments naming a missing sample emptied, as deleting their sample would have, then the cells naming
  nothing cleared a run at a time (those that named the emptied instruments among them, in the same run); the loops
  left.
- **Finding them** decodes every pattern's instruments in place, and reads the songs, the FUSION, sampler and
  FAVORITES cartridges and the loops: a few milliseconds for a full store.
- **The cost:** an instrument used in 40 patterns ~0.3 s; the most, a cartridge used in every pattern,
  ~2 s, with a progress bar ([ram.md](ram.md#what-writing-costs)).

## Integrity

- At power-on every record in the user area is checked ([store.md](store.md#power-on)). A record that
  fails falls back to its older version if the flash still holds one; else its object is missing.
  CHECK STORAGE (below) lists either.
- CHECK REFERENCES lists references to nothing (the loops' too; not `FF`) and the old samplers; CLEAN UP deletes
  unused patterns, instruments and samples (those no instrument names, in any sampler), and clears references to
  nothing when asked; CHECK STORAGE reads and checks everything
  (SETTINGS → TOOLS,
  [../ui/screens/settings.md](../ui/screens/settings.md)).
