# Disks

Status: **settled** (2026-10-09). The record: [the disk proposal](../proposals/202610081349-disk.md).

A **disk** is a plain file on a computer that stores a set of PT/FM's files, to be passed on:
cartridges and their samples' audio, patterns, songs, the loops, the settings. The demo data is a disk.
The web editor (`editor/`: [../editor.md](../editor.md)) opens and saves disks; sending updates to the device is
the editor's, not the disk's. The device itself never reads a disk.

**Files** here are the store's records ([store.md](store.md#a-record)): a file on a disk is a record's
payload, byte for byte, so nothing converts between a disk and the device.

## The file

A 16-byte header, then the files one after another; nothing else. Named `<name>.ptfmdsk`; whatever opens
a disk knows it by its magic, not its name (so an older `.ptd` opens too).

| Bytes | Field |
|---|---|
| 0-7 | magic `PTFMDISK` |
| 8 | the container's layout: 1 |
| 9-11 | spare, 0 |
| 12-15 | the number of files |

**A file:** a 12-byte header, then its payload; the next file starts at the next multiple of 4, the gap
written 0.

| Bytes | Field |
|---|---|
| 0 | type: the record's kind ([store.md](store.md#a-record)) |
| 1 | number |
| 2 | sub |
| 3 | the type's version |
| 4-7 | the payload's length |
| 8-11 | CRC-32 of the payload |

- **Each (type, number, sub) at most once.**
- **Objects are whole:** a pattern with all its parts, the loops with all their parts, a sampler
  cartridge with its samples' audio.
- **Any set:** a whole library, one cartridge, or a song with its patterns, cartridges and audio.
  References may point at files the disk does not hold (a song without its cartridges).
- **Order:** settings, cartridges, songs, patterns, the loops, notes, then the audio, each by number and
  sub.

**Types only a disk holds** (0xE0-0xEF), never sent to the device:

| Type | What | Payload |
|---|---|---|
| 0xE0 | notes | text, UTF-8: what the disk holds, its credits and licences |

A disk whose works ask for their notice to travel with them (MIT, CC BY) holds it in its notes.

**Size:** a whole library is under 1 MB, as the FM-1's flash is.

## Versions

- **Each type has its own version,** in every file's header, as in every record in the flash
  ([store.md](store.md#a-record), byte 3; the table of kinds there gives each one's current version). A
  new layout of a type takes its next version ([README.md](README.md#extending)); the other types keep
  theirs.
- **The version stays with each file,** not once for the whole disk. A disk read back from a device can
  hold one type in two versions (songs not yet converted beside songs saved by a newer firmware), and a
  file copied to another disk keeps its own. It costs nothing: a file's header is 12 bytes either way.
- **Only the PC side converts:** the editor and the install page, which share the converters. They
  read every version of a type there has been and write the current one, so an older disk is upgraded
  when it is opened: the loops' version 3, a loop a track, becomes version 4, each old loop a loop of one
  lane ([loops.md](loops.md#the-record)). The converters cost the device no flash.
- **The editor also converts down,** for an FM-1 running an older PT/FM: when the device does not take a type's
  version the panel holds but takes an older one, WRITE converts the object to that one, naming first what the older
  version cannot hold ([../editor.md](../editor.md#the-fm-1-read-write-and-disks)). Each new version of a type comes
  with its converter both ways, tested both ways; disks and the panels keep the current version.
- **The device reads one version of each type,** the one it was built for, and reports it
  ([blob.md](blob.md#seeding)); the editor sends only those. **It never converts.** A record of another
  version is kept untouched, as a kind it does not know is ([README.md](README.md#extending)): it plays
  nothing, its object counts as missing ([references.md](references.md#integrity)), and its number
  stays taken, so nothing made on the device takes it. The settings, the loops and DJ's setup, one of
  each, start from their defaults instead, and saving them replaces the old record.
- **After an install that changes a type's version,** the install page reads the device's records of
  the older version back, converts them as the editor does, and writes them back as one save
  ([blob.md](blob.md#seeding)). An install never touches the user area
  ([store.md](store.md#where-things-go)), so this works after any install, ours or another
  installer's; until the PC is connected, those objects are only missing. Copies of the device's library
  are the editor page's: READ, then DOWNLOAD DISK saves it as a disk (for when another firmware runs on the
  FM-1 in between: [../editor.md](../editor.md#the-fm-1-read-write-and-disks)). Reading records back takes
  messages of their own, READ and DATA: [blob.md](blob.md#seeding).
- **A file of a version newer than the editor knows** is kept as it is, shown, and not sent: the
  editor says to update itself.
- **The container's layout** (header byte 8) is the disk's own; it changes only if the header or the
  file headers do.

## The demo disk

- **`demo-disk/`** holds the demo disk's sources: patch banks, sample sets and songs as JSON and FLAC
  files, each with its licence ([../../demo-disk/README.md](../../demo-disk/README.md)), and the tools
  that make them (`demo-disk/tools/`).
- **The playground has the demo content built in** (`tools/gen_playground.py` makes
  `playground/content.js` from these sources); it knows nothing of disks. Only the editor and the
  firmware work with disks' files.
- **The packer** (`tools/pack.py`) builds `dist/firmware/demo.ptfmdsk` from them, committed with the other built
  files: the demo songs, their patterns, the sampler cartridges and their audio, and FUSION (cartridge `06`,
  its patterns `E0`-`E5`, kept apart from the songs' patterns that count up from `00`; its layers play the
  synth cartridges and the samplers alike). Its notes hold `demo-disk/CREDITS.md`.
- **The built-in instruments are not on it:** the synth cartridges go into the firmware, made by its build
  from the same sources ([instruments.md](instruments.md#built-in-instruments)). Uploaded, the demo disk
  takes 67 of the user area's 103 blocks (~260 KB, the samples' audio ~160 KB of it) and leaves 36; a SAVE
  always keeps 10 of them (compaction's and the EDITOR's, [store.md](store.md)), so ~104 KB is left for saving
  (STATUS' STORAGE figure), about 135 patterns of 8 tracks and 64 rows; deleting its things gives the room back.
