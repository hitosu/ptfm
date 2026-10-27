# The store

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 1.

How PT/FM keeps its records in the FM-1's flash. The store is a log: records are appended into erased
flash, never rewritten in place, and nothing like a directory is kept. Each save writes only what
changed; an old version becomes garbage, and a block is erased only when nothing in it is still live.

## The flash

The flash is 1 MiB of Puya NOR.
- **Erase** works on 4 KB blocks and sets every bit to 1. **Program** writes 1 to 256 bytes within
  one 256 B page, and only clears bits.
- **A page can be programmed again, in pieces,** before its next erase, also after a power-cycle: no
  ECC over pages or chunks.
- About 100,000 erases a block; reads are unlimited.
- **An erase or a program stops code fetches,** so audio is silenced around them.
- **Measured on the FM-1,** with the interrupts off:

  | Operation | Time |
  |---|---|
  | erase, 4 KB | 17.1 ms |
  | program, 1 to 255 B | 1.93-2.0 ms, whatever the size |
  | program, a full 256 B page | 1.70 ms |
  | direct read | 6 us + 0.72 us a byte (16.1 us for 16 B; 2.95 ms for 4 KB) |
  | read in place, cold | 1.3 us a cache-line fill (0.36 us a 16 B read when sequential) |
  | CRC-32 of 4 KB (table) | 0.2 ms |

  So a write costs its programs and erases, not its bytes: a 4 KB block takes 16 page programs
  (27 ms) and an erase (17 ms).
- **Power, measured on the FM-1:**
  - power-cycles keep everything written;
  - in five power cuts while writing, no record was torn: a program under way seems to finish. A
    torn write stays possible, so every record carries a CRC;
  - an erase can be cut short: the block then reads almost all 0xFF, with a few bytes not
    ([The head, erases and wear](#the-head-erases-and-wear));
  - nothing outside the block being written was ever touched.

## Where things go

| Area | Holds |
|---|---|
| 0x000000 - 0x004000 | never: the boot code |
| **FWS, the firmware space**, 0x004000 - the firmware's end | the firmware with its built-in instruments ([instruments.md](instruments.md#built-in-instruments)), written by installs |
| **FWS's free part,** after the firmware, up to 0x092000 | nothing: the room the firmware grows into |
| 0x092000 - 0x093000 | never: the SDK's files, written by installs |
| **the user area:** fast region 1, 0x093000 - 0x0E0000 (308 KB) | records; audio |
| update staging, 0x0E0000 - 0x0E5000 | never |
| **the user area:** fast region 2, 0x0E5000 - 0x0FF000 (104 KB) | records; audio |
| 0x0FF000 - 0x100000 | never: the packages' flash header reserves it for the chip |

The screens name the user area USER and the firmware space FWS ([../ui/saving.md](../ui/saving.md#fws)).

- **The user area** (the fast regions) holds every save and everything uploaded: patterns, songs, the
  loops, cartridges, settings, sample audio. An install never touches it, and a power-on reads and
  checks it in about 40 ms ([Power-on](#power-on)).
- **FWS is the firmware slot: the firmware and what it carries, written only by an install.** It holds the
  built-in instruments (cartridges `F0`-`FA`), the keys' song `FE`, the
  EDITOR's empty pattern `FE` ([patterns.md](patterns.md#numbers)), and the defaults of every record the user
  may change (settings, DJ's setup, the loops, the MIDI map). They are part of the firmware's image: read in
  place as its own data, no records.
  - **The device never writes FWS.** Its free part, after the firmware's image, holds nothing and is never
    read: the room the firmware grows into.
  - **A built-in thing that was changed** is saved as a record in the user area under its own number
    (F0-FF), found before the firmware's own; its record deleted (BACK TO BUILT-IN), the firmware's own is
    back. A built-in thing never moves and has no DELETE.
  - **More room** in the user area comes from deleting things, the demo disk's among them.
- **Reading:** the user area and the built-in instruments are read in place, as the firmware's own data.
- **A reserve of 64 KB** (16 blocks) of the user area is left free by the install page's uploader, for the
  device's saves and for compaction. The editor's WRITE does not keep it: it may fill the user area to its
  last block, and says first what fewer free blocks cost ([../editor.md](../editor.md#the-fm-1-read-write-and-disks)):
  under the 40 KB a SAVE of new things keeps (below), nothing new is saved; under 3 blocks, nothing at all.
- **Installs:** the update loader writes the whole firmware slot, 0x004000 - 0x093000, and never the user
  area: installing PT/FM keeps everything the user made, the changed built-ins included. Other firmwares
  write the fast regions too: for PT/FM their install is the loss of everything, and a copy made with the
  editor page before (READ, then DOWNLOAD DISK: a disk) is how it comes back once PT/FM is installed again
  ([blob.md](blob.md#seeding)).
- **An install cut short by a power cut does not resume** at the next power-on: the boot code looks for
  the request to run the update loader only in RAM. The USB rescue and the install page bring the FM-1
  back.
- **What other firmwares leave** in the fast regions (their settings at 0x0FC000-0x0FEFFF, their
  projects in region 1) holds no record of PT/FM's: those blocks count as free, and are erased when
  the head reaches them ([The head, erases and wear](#the-head-erases-and-wear)).
- **The update path** marks a pending update with a record in the last 256 B of a block (at
  0x0E4F00, in the staging), where the boot code looks for it. An install's loader erases every
  block from 0x093000 to 0x0FC000 that ends in such a record. A record is told by a marker and a
  CRC-16, so data of the store matches one by chance about once in 4 billion blocks: nothing is done
  about it.

## A record

A 16-byte header, then the payload; the next record starts at the next multiple of 4.

| Bytes | Field |
|---|---|
| 0 | kind (0xFF: erased flash, the end of a block's records) |
| 1 | number: which cartridge, pattern, song (00-FF) |
| 2 | sub: a pattern's or the loops' part (0-15); a sample's index in its cartridge (00-3F); else 0 |
| 3 | the kind's format version |
| 4-6 | the payload's length |
| 7 | flags: bit 0 the last record of its save; bit 1 deleted (no payload) |
| 8-11 | save number |
| 12-15 | CRC-32 of bytes 0-11 and the payload |

| Kind | What | Number | Sub | Version |
|---|---|---|---|---|
| 0x01 | DX7 cartridge ([instruments.md](instruments.md)) | the cartridge, 00-FF | 0 | 1 |
| 0x02 | Juno-106 cartridge | the cartridge | 0 | 1 |
| 0x03 | TB-303 cartridge | the cartridge | 0 | 1 |
| 0x04 | sampler cartridge | the cartridge | 0 | 1 |
| 0x05 | FUSION cartridge | the cartridge | 0 | 1 |
| 0x06 | CHIP cartridge | the cartridge | 0 | 1 |
| 0x10 | sample audio ([ptr.md](ptr.md)) | its cartridge | the sample, 00-3F | 1 |
| 0x20 | pattern ([patterns.md](patterns.md)) | 00-FF | the part, 0-15 | 1 |
| 0x21 | song ([songs.md](songs.md)) | 00-FF | 0 | 1 |
| 0x22 | the loops ([loops.md](loops.md)) | 00 | the part, 0-15 | 4 |
| 0x23 | DJ's setup ([dj.md](dj.md)) | 00 | 0 | 1 |
| 0x30 | settings ([settings.md](settings.md)) | 00 | 0 | 1 |
| 0x31 | the MIDI map: MIDI learn's mappings ([midi.md](midi.md)) | 00 | 0 | 1 |

**Version** is each kind's current layout, the one the device reads ([disk.md](disk.md#versions)).

- **Records a SAVE writes are at most 4 KB** (a 4,080-byte payload) **and never cross a block.**
- **The one-of-a-kind records** (the settings, the loops, DJ's setup, the MIDI map): with none in the
  flash the device runs on the firmware's defaults (the loops empty, no mappings), and nothing is copied
  at power-on; the record is written at the first SAVE that changes it, into the user area.
- **Audio has blocks of its own in the user area:** only the uploader and CLONE or MOVE of a sampler
  cartridge with samples write it, packed one record after another over consecutive blocks of one region,
  and no other record shares those blocks. So compaction never meets a block it cannot free.
- **CLONE or MOVE of a sampler cartridge with samples** ([../ui/screens/instruments.md](../ui/screens/instruments.md))
  is one save written at once: every sample's audio copied under the new number (from wherever it lies),
  a MOVE's deleted records for the old number, then the cartridge's record, the save's last. A power cut
  leaves it as before or as after.
- **The six cartridge kinds share one number space:** a cartridge number is live in at most one of
  them. Writing a cartridge of another kind under a number deletes the old one in the same save, a
  sampler's audio with it. **Cartridge, pattern and song numbers `F0`-`FF` are the
  firmware's,** for its built-in things ([instruments.md](instruments.md#built-in-instruments),
  [patterns.md](patterns.md#numbers)): a record under one of them is a changed built-in. A cartridge there of
  a kind the firmware has no built-in of under that number (an earlier build's) is left out at power-on, dead,
  and its compaction drops it.

## Saving

- **One save:** every record a SAVE writes carries the same save number, the next after the highest
  in the flash. The last one has flag bit 0.
- **All or nothing:** at power-on, the records of a save whose last record is missing or fails its CRC
  are ignored. A power cut leaves the state before the save, or after it.
  - *(proposed)* **A save cut short is spoilt at the next power-on,** the only write a power-on makes:
    the newest save, found without its last record, gets each record's kind byte programmed to 0, so it
    never counts later (~2 ms a record). Only the newest save can be told for cut: compaction may free a
    finished save's last record while its other records live on. The next save number is past every
    number seen, finished or not.
- **Whole records from the edits' pieces:** the edit memory holds only the pieces that changed (a pattern's
  tracks, a cartridge's slots, a small record whole: [ram.md](ram.md#editing-and-saving)); a save builds each
  changed record from its bytes in the flash and its pieces, one at a time in the store's 4 KB scratch, and
  writes it whole: records in the flash are always whole.
- **Whole pages:** a program costs the same whatever it writes, so a save lays its records out in RAM
  and programs page by page, the flag inside the last record's header rather than as a write of its
  own.

## Power-on

- **The scan:** every block's record headers in the user area are read in place; a big audio record's
  later blocks are skipped. The newest version of each object (kind, number, sub) wins; a deleted flag
  removes it. FWS's free part is not read: whatever an older firmware left there counts for nothing.
- **The check:** the CRC of every record in the user area is checked (~30 ms for all 412 KB). SETTINGS →
  TOOLS → CHECK STORAGE checks them all again on request ([../ui/screens/settings.md](../ui/screens/settings.md)).
- **A record that fails its CRC** does not count. As its header may be wrong too, the scan finds the
  next record by trying each following 4-byte step for a header whose record checks: a bad record
  costs only itself.
- **The map:** what is live, and where, stays in RAM: 8 B an object, 4.4 KB for 560 objects.
- **The scan's worst case,** a header every 16 B over all of the user area, takes 8.6 ms (a cold
  header read: 1.3 us in place).

## The head, erases and wear

- **The head** is where the next record goes: the erased tail of the open block, then the next free
  block in address order, wrapping around after the last. So the free blocks take their turns evenly
  (wear: [ram.md](ram.md#what-writing-costs)).
- **At power-on** the head is after the newest save's last record, if the rest of that block is all
  0xFF; else the next free block.
- **A block is checked before it is written:** every byte 0xFF (~0.1 ms in place), else it is erased
  again, as an erase cut short leaves a few bytes not 0xFF.
- **Erases are rare:** one when the head opens a block, about one save in six for small edits.

## Garbage and compaction

- An object's older versions are dead once a newer one is saved; a block with nothing live in it is
  free.
- **Compaction,** when free blocks run short: the live records of the block with the fewest live bytes
  are written again at the head, as a save of their own, and the block becomes free. Audio is never
  moved. Compaction works on the user area only.
- **A deleted record** (a header with flag bit 1, no payload) stays only as long as an older version
  of its object is left in the flash; compaction then drops it.
- **SAVE refuses** if it would leave fewer than 2 free blocks once compaction has run: compaction
  always needs room to move into. The screen says what to delete ([../ui/saving.md](../ui/saving.md)).

## The room for the EDITOR's pattern

The EDITOR's own pattern `FE` ([../ui/screens/editor.md](../ui/screens/editor.md#the-editors-own-pattern))
always has room to be saved:
- **8 blocks (32 KB) are kept for it,** the largest pattern's room ([patterns.md](patterns.md#size)), on top
  of the 2 blocks compaction needs. A SAVE is refused when what it writes, the EDITOR's pattern apart, would
  leave fewer than 10 free blocks once compaction has run; the EDITOR's pattern may use the 8.
- **An amount, not a place:** its record is written into the log at the head like any other, so its blocks
  take their turns with the rest (no wear of their own).
- **Saved as everything is,** all or nothing: the new copy is written beside the old one, so a power cut
  leaves what the last SAVE wrote. So the 8 blocks hold a full pattern once; rewriting one near its largest
  while the rest of the flash is full needs its room a second time, and that SAVE asks as any SAVE that does
  not fit ([../ui/saving.md](../ui/saving.md#storage-full)): the changes stay in RAM, and once something is
  deleted SAVE writes them.
- **Deletions first:** a SAVE that does not fit writes its deletions first, as a save of their own, then the
  rest once compaction has freed their blocks; a power cut between the two leaves the deletions saved and the
  rest not. Only deletions nothing among the changes could be made from go first: a thing deleted whole whose
  kind (patterns; songs; the cartridges with their samples, as one) has no new thing among the changes (one
  the flash does not hold, or one made anew, as MOVE and CLONE make), the EDITOR's pattern and the keys'
  song apart. Otherwise the SAVE writes nothing and asks.
- **STATUS** counts the 40 KB as used ([../ui/screens/status.md](../ui/screens/status.md)). The uploader's
  64 KB reserve covers them.
