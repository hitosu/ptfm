# Files and formats

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
its introduction and sections 2 and 13.

What PT/FM stores, and how: the store in the flash, and every record in it (patterns and their
commands, songs, the loops, cartridges and instruments, sample audio, settings, the MIDI map), how records refer to
each other, how edits reach the flash, how a data blob gets onto the device, and the disks that carry
sets of files between computers. The UI ([../ui/README.md](../ui/README.md)) says what the screens
show and do; these files say what is kept.

## Words used here

- **Record:** one object in the flash: a 16-byte header and its payload. A pattern, a song, a
  cartridge, a sample's audio, the loops, the settings.
- **Kind, number, sub:** what a record is (a pattern), which one (`3C`), and which part of it.
- **Save:** the records one SAVE writes, all with one save number; they count all together or not
  at all.
- **Block:** 4 KB, the flash's erase unit. **Page:** 256 B, the most one program writes.
- **The user area** (the fast regions): flash outside the firmware slot, read in place; every save goes
  there, and installs keep it. **FWS, the firmware space:** the firmware slot: the firmware with its
  built-in instruments, read in place, written only by installs; its free part holds nothing, the room the
  firmware grows into ([store.md](store.md#where-things-go)). What is there is edited as anything else, its
  changes saved in the user area.

## The files

| File | Subject |
|---|---|
| [store.md](store.md) | the store: a log of records in the flash, where things go, a record's header, saving, power-on, the head, compaction |
| [patterns.md](patterns.md) | patterns: tracks, rows, a cell's bytes, parts, FUSION patterns |
| [commands.md](commands.md) | commands: tracker effects, time, the instruments' parameters, mixer and FX, PT/FM's own, the punch-ins |
| [songs.md](songs.md) | songs: the order, the mixer, the FX setup |
| [loops.md](loops.md) | LOOPS' loops: one length, the setup, the loop table, each loop's notes as one stream |
| [dj.md](dj.md) | DJ's setup and stand: its FX setup and properties, and the decks as they stand (SAVE, power-on) |
| [instruments.md](instruments.md) | cartridges and instruments: DX7, Juno-106, TB-303, sample instruments, FUSION |
| [ptr.md](ptr.md) | sample audio: PTR, PT/FM's lossless coding |
| [settings.md](settings.md) | the settings record |
| [midi.md](midi.md) | the MIDI map: MIDI learn's mappings of an outside controller onto the FM-1's controls |
| [references.md](references.md) | what refers to what, reference updates, integrity |
| [ram.md](ram.md) | editing, saving and RAM: the edit memory, where the RAM goes, what writing costs |
| [blob.md](blob.md) | the data blob, seeding and converters |
| [disk.md](disk.md) | disks: a plain file holding a set of PT/FM's files, for the web editor; the demo disk |

## Names, numbers and bits

- **Names use 6 bits a character:**

  | Code | Character |
  |---|---|
  | 0 | space (also the padding) |
  | 1-26 | `A`-`Z` |
  | 27-36 | `0`-`9` |
  | 37-48 | `:` `.` `-` `?` `\|` `/` `\` `*` `#` `$` `@` `%` |
  | 49-62 | spare |
  | 63 | (a name's first character: the slot or the sample entry is empty) |

  An instrument's name (10 characters) takes 60 bits, a cartridge's (12) 9 bytes, a song's (16) 12
  bytes.
- **Numbers:** cartridges, patterns and songs `00`-`FF`; samples `00`-`3F` within their cartridge.
  Cartridges and patterns `F0`-`FF` are the firmware's built-in instruments'
  ([instruments.md](instruments.md#built-in-instruments)); the user's are `00`-`EF`. Song `FE` is the firmware's
  too, the keys' song ([../ui/screens/fx.md](../ui/screens/fx.md#the-fx-setup)), never listed; the user's songs
  are `00`-`FD`. Pattern `FE` is the EDITOR's own, never listed; `FF` "no pattern"
  ([patterns.md](patterns.md#numbers)).
- **Bit-packed fields** follow each other from bit 0 of their first byte upward, in the order listed;
  a field's own bits likewise, least significant first. Spare bits are written 0 ([Extending](#extending)).
- **Multi-byte numbers** are little-endian.

## Extending

- **A new kind of record** takes a new kind code; a new layout of a kind takes the next version. Every
  kind starts at version 1; [store.md](store.md#a-record)'s table of kinds gives each one's current
  version.
- **A device keeps the records it does not know,** and those of a version it does not read: it does not
  read them, and compaction moves them as they are, byte for byte. Only the PC side converts
  ([disk.md](disk.md#versions)).
- **Spare bits and bytes** are written 0, and a meaning given to them keeps 0 as "as before", so a new
  parameter in a slot's spare bits needs no new version.
- **Larger slots** are a new version of their kind; every full cartridge leaves room in its block.
- **Numbers are names, never places** (2026-10-10: the numbers as they stand are the base). Whatever a
  file stores by number keeps its meaning for good: command codes ([commands.md](commands.md#the-numbers-are-fixed)),
  the effects' numbers, the punch-ins', the knobs', a list knob's items (DELAY's TIME, GATE's RATE,
  METRONOME's SOUND ...), the built-in GATE patterns, an engine's parameters.
  - **Something new takes a free number;** nothing is renumbered, and no number is used twice.
  - **Where a thing sits** on the screen or the keys comes from the firmware's own tables, apart from
    its number: moving a punch-in to another key, or an effect in a list, changes no file.
  - **A list grows only at its end;** the screen may show it in another order. **A range keeps its
    bottom** (a knob is stored as its step from it); it may grow at the top.
  - **Something removed is retired:** its number is never given again; a song keeps it, the player skips
    it, the EDITOR shows it dim until it is deleted.
  - **Where a change cannot keep a meaning,** the kind's version goes up and the PC side converts
    the older version ([disk.md](disk.md#versions)): no song is rewritten by hand.
- **Reserved:** name code 63 (an empty slot or sample entry), kind 0xFF (erased flash), command code
  0xFF (a cell's list of commands).
