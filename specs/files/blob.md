# The data blob, seeding and converters

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 12.

A data blob holds records in the device's own formats. A page writes it to the device; nothing
converts on the way.

## The blob

- **One file holds every record** the device should have, each exactly as the device stores it. The
  uploader never changes a payload; it only decides where it goes.
- **No flash addresses:** records are placed when seeding, against the layout the device reports
  ([Seeding](#seeding)), so one blob works with every firmware that accepts its kinds and versions.
- **One blob for both:** the real FM-1 and the emulator ([../emulator.md](../emulator.md)).

A header, a table of records, then their payloads (each starting at a multiple of 4).

| Header bytes | Field |
|---|---|
| 0-7 | magic `PTFMDATA` |
| 8-9 | the blob format's version (1) |
| 10-11 | records, N |
| 12-15 | the blob's length |
| 16-19 | CRC-32 of bytes 32 to the end |
| 20-31 | spare |

| Entry bytes (N × 32) | Field |
|---|---|
| 0-3 | kind, number, sub, version |
| 4 | placement: 0 a record; 1 audio (blocks of its own) |
| 5-7 | spare |
| 8-11 | the payload's offset in the blob |
| 12-15 | its length |
| 16-19 | its CRC-32 |
| 20-31 | label, ASCII, for the page only |

The record header ([store.md](store.md#a-record)) is made when the blocks are laid out, with the save
number.

**Checks:**
- **the uploader:** the magic, the format's version, the blob's CRC and every payload's; no (kind,
  number, sub) twice; every (kind, version) one the device accepts ([Seeding](#seeding));
- **the device:** every block read back as it is written; every record's CRC at power-on
  ([store.md](store.md#power-on)).

## Seeding

- **Who seeds:** `dist/firmware/seed.html` ([../layout.md](../layout.md)) seeds the FM-1 over USB-MIDI (Web MIDI,
  in Chrome or Edge); the emulator's Seed button seeds the emulated one. One seeding module
  (JavaScript) serves both pages; only the transport differs: SysEx through Web MIDI, or function
  calls into the WebAssembly. The device's side is the firmware's own code in both.

1. **The device reports its layout:** its firmware and protocol versions; where its firmware ends; the
   user area's regions ([store.md](store.md#where-things-go)); the reserve; the
   built-in things it holds (in `F0`-`FF`: the instruments, any built-in patterns, the EDITOR's own pattern `FE`,
   the keys' song `FE`); the (kind, version) pairs it accepts; the
   largest message it takes; and its records: kind, number, sub, version, CRC, place, save number.
2. **The uploader plans:** a record already on the device with the same CRC stays where it is; new and
   changed ones go into free blocks of the user area, records packed together, audio in
   blocks of its own; the reserve stays free; records the device has and the blob does not get
   deleted records, so the device ends up holding exactly the blob: **the device's own saved work not in
   it is deleted**, and keeping a copy of that work is the user's to do. The emulator shows the plan (each
   area's space, what goes where, what does not fit, what will be deleted) and writes once the user
   accepts it; the install page's UPLOAD DEMO DISK plans and writes in one press, and so does its UPLOAD
   CUSTOM DISK with a disk file picked (refused before anything is sent when the file is no disk or holds
   nothing), its progress in the status line, and says there what keeps it from writing (it does not fit, a kind the device does not
   take), writing nothing then.
   - **Nothing goes into FWS:** the device never writes it ([store.md](store.md#where-things-go)).
   - **It stops there** if what is left does not fit in the user area, if the device does not accept a
     (kind, version), or if the blob holds a cartridge or a pattern numbered `F0`-`FF` where the firmware
     has no built-in one of that kind ([instruments.md](instruments.md#built-in-instruments)). One where it
     has is a change of that built-in thing (a backup holds such changes): it goes into the user area as
     any record, and is found before the firmware's own.
3. **It writes the blocks** as one save: the device's last save number + 1, the last record flagged.
   For each block the device erases it if it is not all 0xFF, programs it, reads it back and answers.
   The uploader waits for every answer: an erase takes tens of milliseconds, and MIDI has no flow
   control.
4. **The device mounts** and answers with the result.

- **The uploader owns placement.** The device does not allocate; it checks that every block written
  lies in a region it reported.
- **A seeding cut short** leaves the previous library whole as long as the new records went into free
  blocks: they count only once the last one is written. Only a full rewrite into a full flash can leave
  a part; seeding again mends it.
- **While seeding,** the device silences audio (an erase stops code fetches) and does not read the
  store. After 10 s without a message it ends the seeding and mounts what is there.
- **A firmware install** writes the whole firmware slot and never the user area
  ([store.md](store.md#where-things-go)): installing PT/FM keeps everything the user made. Other firmwares
  write the fast regions too, so for PT/FM their install is the loss of everything. The install page
  writes the firmware alone: it reads nothing back and writes nothing into the user area. Copies of the
  library are the editor page's (READ, then DOWNLOAD DISK: a disk, [../editor.md](../editor.md#the-fm-1-read-write-and-disks)), made before another
  firmware runs on the FM-1.
- **An install cut short by a power cut does not resume** at the next power-on: the USB rescue and the
  install page bring the FM-1 back ([store.md](store.md#where-things-go)).
- **A clean install** (the install page's CLEAN INSTALL switch beside INSTALL FIRMWARE: off at first, on
  at first on the debug page, [../debug.md](../debug.md#the-page)) keeps nothing: after the
  install the uploader writes every block of the user area empty (all 0xFF: the device erases it), and no
  record. Nothing that was there stays, damaged records included; the device then holds an empty user area,
  the firmware's built-ins alone. The demo disk is UPLOAD DEMO DISK's, a button of its own (UPLOAD CUSTOM
  DISK beside it, a disk file of the user's). The debug page's
  CLEAR USER DATA writes the same empty blocks without an install, PT/FM running. On the FM-1 alone,
  SETTINGS' FACTORY RESET erases them and restarts ([../ui/screens/settings.md](../ui/screens/settings.md));
  while it erases, the seeding's commands are answered busy.
- **The device refuses to be seeded with edits not saved:** `SAVE OR REVERT FIRST`.

**Messages:** SysEx `F0 7D 50 54 cmd payload F7`.
- `7D` is the manufacturer ID for non-commercial use, open to anyone: right for free firmware. A
  registered ID is needed only for a product that is sold.
- `50 54` ("PT") marks PT/FM's messages among others on `7D`. Every PT/FM protocol shares it; the
  command byte tells them apart, and seeding's are those below.
- The payload is 7-bit packed, 7 bytes in 8; at most 512 B of data a message, which keeps a frame
  under the firmware's 640 B USB-MIDI limit.

| cmd | Direction | Contents |
|---|---|---|
| 01 HELLO | uploader → device | the protocol's version |
| 02 LAYOUT | device → uploader | the layout (step 1), in several messages if long |
| 03 READ | uploader → device | an address and up to 504 B to read back (the user area only): the editor page's reading of records and its backups ([../editor.md](../editor.md#the-fm-1-read-write-and-disks)) |
| 04 DATA | device → uploader | READ's answer: the address and the bytes |
| 10 BEGIN | uploader → device | the save number, the number of blocks to write; refused (CHANGED) unless the save number is the layout's last + 1, so a save made on the device after the layout was read is never written over: the uploader asks again and plans again |
| 11 CHUNK | uploader → device | block, offset, up to 512 B of the block's bytes |
| 12 BLOCK | uploader → device | block, CRC-32 of its 4096 B: erase if needed, program, verify |
| 13 END | uploader → device | mount, and answer with the result |
| 1F ABORT | uploader → device | stop, and mount what is there |
| 20 ACK | device → uploader | the command and block it answers, and a result code (0 = done) |

The other protocols' commands on `50 54`: the debug build's 30-34 ([../debug.md](../debug.md#asking-sysex)); 40
STATE? and 41 STATE ([../connections.md](../connections.md#the-state)); 43, the PT/FM controller's hello
([../controller.md](../controller.md#4-finding-each-other)), which switches MIRROR on and off
([../connections.md](../connections.md#mirror-the-panel-from-a-page)).

In the emulator the same bytes pass through a function call each way, so the device's code and the
uploader's are both exercised.

Every message's bytes, the layout's included, are as `fw/web/seed.js` describes them at its
top (the layout: `PTLY`, its version, the firmware's end, the regions, the accepted pairs, the built-in
instruments, the last save number, every record with its CRC, place and save, the firmware's version as
text).

## Making blobs, and converters

- **The packer** (`tools/`) builds the demo data's blob from `demo-disk/` (its JSON and FLAC files,
  [../../demo-disk/README.md](../../demo-disk/README.md)), and again whenever the demo data changes.
  It makes every record these files describe but the built-in instruments: sampler cartridges and PTR
  audio from the sample sets, FUSION with its patterns, patterns and songs from the songs. The built-in
  instruments (the synth cartridges, [instruments.md](instruments.md#built-in-instruments)) go into the
  firmware instead, made by the firmware's build from the same sources. Other data sets are
  made into blobs by the same tool, outside this repository.
- **Converters** turn other formats into these records: MOD, S3M and XM into a song, its patterns, a
  sampler cartridge and PTR audio; `.syx` into DX7 cartridges and back; Juno-106 dumps into Juno-106
  cartridges and back; WAV and FLAC into PTR audio and an instrument.
