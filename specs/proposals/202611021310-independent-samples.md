# Proposal: independent samples, numbered on their own, named by any sampler's instruments

Status: **settled** (2026-11-02); its specs are written when the work on it starts, and lead from then on.

Today a sample belongs to one sampler cartridge: the sampler's record holds its instruments and its samples' table,
the audio is numbered under the sampler (`0B:05`), and an instrument names a sample by its place in its own sampler
([../files/instruments.md](../files/instruments.md#samples-and-fusion-slots)). From now on **a sample is a thing of
its own,** numbered `00`-`FF`, and **any sampler's instruments name any sample** by its number. A sampler cartridge
holds instruments only. So one sound serves instruments in several samplers without a copy, a sample is moved,
uploaded or deleted as itself, and a sampler is as light as any other cartridge.

## 1. A sample

- **A record of a new kind:** kind **0x11**, number the sample's (`00`-`FF`), sub 0, version 1
  ([../files/store.md](../files/store.md#a-record)). It carries everything about the sample:

  | Bytes | Field |
  |---|---|
  | 0-15 | the entry: length in samples (20 bits), rate in Hz (18 bits; an instrument's BASE NOTE plays it at that rate), coding (4 bits: 1 PTR 8-bit, 2 PTR 16-bit), name (60 bits: 10 characters), spare (26 bits) |
  | 16- | the audio, as the old audio record whole ([../files/ptr.md](../files/ptr.md)): coding, length, the index, the blocks |

  The entry is the one the sampler's table held, unchanged in its bits; the audio is unchanged. **The index's
  offsets count from byte 16,** the audio's own start, as they counted from the old record's payload start. The
  coding and the length are in both the entry and the audio's head: the entry is what is read, and the two always
  agree. The reader treats a sample whose two differ as missing, and CHECK STORAGE checks kind 0x11 for it (its CRC
  check alone would not see it) and lists it: `SAMPLE 23: ENTRY AND AUDIO DIFFER`.
- **Kind 0x10, the old audio, is kept as audio but no longer read.** Its key named the sampler and the sample's
  place in it; the new key names the sample alone. A new kind keeps the two apart, so an old audio record and a new
  sample never share a key, and both can lie in the flash side by side while the samplers are converted one at a
  time (section 7). The number 0x10 stays the old audio's for good
  ([../files/README.md](../files/README.md#extending)).
- **Both kinds are audio to the store:** a record that may cross blocks (the scan's check), blocks marked as audio
  (never compacted nor appended to), the head, compaction, the LAYOUT, and the uploader's placing and size checks
  take 0x10 and 0x11 alike. Only reading 0x10 stops (playing it, listing it, counting it as a sample). A firmware
  that took 0x10 out of its audio would see every old audio record as damaged and count its blocks free, so its
  first save could write over audio the conversion has not read yet.
- **256 samples on an FM-1 at most.** The store's map (about 560 objects of every kind) and the user area run out
  first. All of `00`-`FF` are the user's: the firmware never carries samples
  ([../files/instruments.md](../files/instruments.md#built-in-instruments)).
- **It has no owner:** any number of instruments, in any samplers, name it. Its audio keeps its own blocks in the
  user area, as audio does now.
- **A sample's number and a sampler's are apart:** sample `05` and sampler cartridge `05` have nothing to do with
  each other. Every place that ties a sampler's number to audio under the same number goes: the store's taking of a
  cartridge number (another kind written there deletes the old one: its audio no longer with it), a sampler's
  save, forget and things, the cartridge list's free numbers, the edit memory's family of records, the INSTRUMENTS
  and EDITOR screens' sample lookups ([../files/store.md](../files/store.md), "a sampler's audio with it").

## 2. A sampler cartridge

- **Instruments only:** kind 0x04, **version 2**: name (9 B), instruments stored (1 B), the instruments (40 B each).
  No samples' table: at most 2,570 B.
- **An instrument's SAMPLE is a sample's number,** 8 bits, in the slot's first byte (6 bits today, in its own
  sampler), the other fields after it as now; the spare bits 84 (86 today). The layout is packed anew in version 2.
- **A sample instrument naming a number with no sample** (a partial upload, a disk without it, a save cut between
  a sample's deletion and its instruments', section 3) plays nothing, as an empty slot does; its page says `THE
  SAMPLE IS MISSING` where the waveform would be, grey as every reference to nothing is
  ([202610311848-editor-instruments.md](202610311848-editor-instruments.md) section 4); CHECK REFERENCES lists it
  in its list's form, where first, then what it names: `SAMPLER 01:05: SAMPLE 23`. This replaces that proposal's `THE SAMPLE IS DELETED` and its grey "deleted
  sample's line in its sampler's list" (a deleted sample is now a row of THE SAMPLES list until SAVE, section 4).
  **CLEAN UP's MISSING REFERENCES** empties such an instrument's slot, as deleting its sample would have, before it
  clears the cells: the cells that named the emptied instrument are cleared in the same run, and its count includes
  them.
- **CLONE and MOVE of a sampler** are those of any cartridge: its instruments only, edits like any (SAVE writes
  them, REVERT takes them back). The audio copy written at once, its progress bar, its `SAVE IT FIRST` and its `NO
  ROOM IN THE USER AREA` go ([../ui/screens/instruments.md](../ui/screens/instruments.md), the cartridge list's
  menu, Texts).
- **DELETE of a sampler cartridge** takes its instruments only; its samples stay, and CLEAN UP deletes those no
  instrument names any more.

## 3. Deleting and cleaning up

- **Deleting a sample deletes the instruments that use it, in every sampler,** after its warning counting them all:
  `DELETE SAMPLE? 5 INSTRUMENTS USE IT AND ARE DELETED TOO`. What named those instruments is as for any deleted
  instrument ([../files/references.md](../files/references.md)).
- **Saved together when there is room:** the sample's deletion and the changed samplers go in one save. When
  storage is full, the save writes its deletions first ([../files/store.md](../files/store.md), Deletions first):
  the sample is deleted whole and no new sample is among the changes, so its deletion goes first and the changed
  samplers follow once compaction has freed its blocks. A power cut between the two leaves instruments naming a
  missing sample: silent, grey, listed by CHECK REFERENCES, emptied by CLEAN UP's MISSING REFERENCES (section 2).
- **REVERT and a pending deletion:** until SAVE, the deleted sample's row in THE SAMPLES list reads `(DELETED)`,
  grey. A REVERT that brings back an instrument naming it (the slot's, the sampler cartridge's) takes the sample's
  deletion back with it, so no REVERT leaves an instrument naming a sample about to go; the instruments deleted with
  it in other samplers stay deleted until their own REVERT (deleting an instrument never deletes its sample, nor
  does bringing one back bring the others). REVERT ALL takes everything back. This is the one REVERT that takes back
  more than its own thing's changes, and [../ui/saving.md](../ui/saving.md#revert) ("nothing else's") says so.
- **An instrument naming a sample already gone** is a reference to nothing (section 2): one a REVERT brings back
  after a split save (the sample's deletion saved first, the samplers' changes still waiting), and one the FM-1's
  PASTE puts down after its sample was deleted (the FM-1's clipboard holds the instrument's bytes, its sample's
  number among them). Neither is refused: it plays nothing, grey, and CHECK REFERENCES lists it.
- **Deleting an instrument never deletes its sample.** CLEAN UP deletes the samples no instrument names, in any
  sampler.
- **A sample's number stays** on the FM-1: DELETE is still the only change made to a sample there. On the EDITOR
  page's FM-1 panel a sample can take another free number, the instruments naming it following, as a cartridge's
  move takes its references (section 5).
- **A record the device does not read** ([../files/disk.md](../files/disk.md#versions): an old sampler, kind 0x04
  version 1, and the old audio, kind 0x10) **is never touched** by the device: the old sampler's number stays taken
  (no NEW, CLONE or MOVE lands there, as disk.md says and the cartridge list's free numbers must honour: today they
  count only what the reader reads), CLEAN UP and DELETE leave it, and what names its instruments is not a reference
  to nothing: CHECK REFERENCES lists each old sampler once, `CARTRIDGE 03: OLDER FORM`, after the references to
  nothing and not counted among them (not each cell naming it), and CLEAN UP's MISSING REFERENCES leaves it. The old
  audio takes no sample number (its kind is not a sample's).

## 4. On the FM-1

- **The samples are not instruments:** never among the cartridges of PRESETS, K1's order, LOOPS' list, MIDI's bank
  select or the FUSION fill's pick. The cartridge list (ENV) shows them as one line of their own, at its end (below).
- **THE SAMPLES list** is today's samples list ([../ui/screens/instruments.md](../ui/screens/instruments.md), The
  samples) holding every sample on the FM-1: titled `SAMPLES (23)` however it is opened, a row a sample in number
  order, each row as today's (its number, name, length and rate, its waveform on the row; 14 rows show). It is
  reached three ways:
  - **ENV on a sample instrument's SAMPLE** (as today, in edit mode): the instrument's sample highlighted (the first
    sample when the instrument names a missing one); the keys play the highlighted sample through the instrument,
    as today; ENV picks it (played whole: START at its start, END at its end, LOOP OFF, as today); HOME leaves the
    instrument as it was.
  - **REPLACE SAMPLE and CREATE** (below): the keys play the highlighted sample as a new instrument would, since
    that is what ENV makes of it (whole, BASE NOTE C-4 at its own rate, LOOP OFF, a new instrument's envelope).
  - **The cartridge list's last line,** after the cartridges (after `EMPTY` too when no cartridge is left), shown
    while the FM-1 holds a sample: ENV on it opens THE SAMPLES list to look, listen and delete without an instrument.
    There the keys play the highlighted sample as a new instrument would, and ENV or HOME go back to the line. SEL
    on the line opens nothing: it is not a cartridge. The line, in the list's columns
    ([../ui/screens/instruments.md](../ui/screens/instruments.md), The cartridges):

    | Element | x | Font | Shows | Colour |
    |---|---|---|---|---|
    | the ID column | 8 | T | nothing | |
    | the name | 26 | T | `ALL SAMPLES` | yellow, as the samples' names (the cursor's white) |
    | the type column | 110 | T | their size, `184K` (as STATUS writes sizes) | light grey |
    | the slots column | 170 | T | their count of 256, `023/256` | light grey |

    The cursor's box is any line's.
  - **SEL → DELETE** on a sample in any of them, with its warning (section 3); a pending deletion's row reads
    `(DELETED)`, grey, until SAVE, and SEL there opens nothing. **In edit mode** (ENV on SAMPLE is an edit-mode
    control, so most deletions happen there) DELETE first leaves edit mode keeping the changes, as EDIT does, then
    deletes: so HOME can never take a confirmed deletion back with the mode's other changes. Today's spec says this
    for the edited instrument's own sample only, and the firmware does not do it (a BUG, fixed with this).
- **With no sample on the FM-1,** REPLACE SAMPLE, CREATE and EDIT on an empty sampler slot say `NO SAMPLES ON THE
  FM-1` and open nothing; ENV on SAMPLE does the same.
- **K1 on SAMPLE** turns through all samples by number (today the sampler's own), passing over pending deletions,
  the other values kept as today (START, END and the loop to the new sample's whole).
- **A sampler cartridge's SEL menu** ([../ui/screens/instruments.md](../ui/screens/instruments.md), SEL menu), as the
  EDITOR page's tab has them (section 5):
  - **on an instrument: REPLACE SAMPLE,** browsing and in edit mode, after PASTE: THE SAMPLES list opens, the
    instrument's sample highlighted; ENV picks one, and the instrument plays it as a new one would, every value back
    to a new instrument's (START, END, LOOP OFF, BASE NOTE, TUNE, VOLUME, LEVEL, the envelope), its name following
    when it was the old sample's; HOME leaves it as it was. It acts on one slot: with slots marked it is not in the
    menu.
  - **INIT on a used sampler slot** keeps its sample and sets every other value back to a new instrument's, as
    REPLACE SAMPLE with its own sample does, and names it after its sample as CREATE does (the other engines' INIT
    names it `INIT`; today a sampler's names the sampler's first sample with no length). Over marked slots, INIT
    does so on each used one and leaves the empty ones empty: an instrument needs a sample.
  - **DELETE in edit mode** too, on a sample instrument: it leaves edit mode keeping the changes, as EDIT does, then
    deletes as DELETE does, with its question when cells use the instrument. Answered so that nothing is deleted
    (HOME), the instrument stays, browsed, its changes kept unsaved.
  - **on an empty slot: CREATE,** in place of INIT (an instrument needs a sample): THE SAMPLES list opens; ENV picks
    one, and a new instrument there plays it whole, named after it; HOME makes nothing. The empty slot's line reads
    `EMPTY SLOT: SEL MENU, CREATE OR PASTE` in a sampler.
  - **EDIT on an empty sampler slot** opens the same pick, in place of its toast `EMPTY SLOT: SEL, INIT`; once a
    sample is picked, edit mode opens on the new instrument.
- **The sample instrument's page** names its sample by number: `SAMPLE 23 KICK  0.50 S  22050 HZ`.
- **STATUS** counts samples as now (their kind's line); the USER FLASH MAP's SAMPLES as now; CHECK STORAGE names a
  sample by its number (`SAMPLE 23`, today `SAMPLE 0C:03`); CLEAN UP's list as now.
- **The help** ([../ui/help.md](../ui/help.md)): INSTRUMENTS' SAMPLE SOUNDS page becomes (17 lines; its line
  `SAMPLES FROM THE PC` is no longer so; THE MENUS stays as it is, full):

  ```
  SAMPLE SOUNDS
  SAMPLER CARTRIDGE: SOUNDS THAT PLAY
    SAMPLES, ANY OF THEM, BY NUMBER
  ON SAMPLE, {ENV}: LIST THE SAMPLES
    {KEYS}: HEAR THE ONE HIGHLIGHTED
    {ENV} AGAIN: USE IT
    {SEL}, DELETE: IT AND THE SOUNDS
    THAT USE IT GO (IT ASKS)
  ALL SAMPLES: {ENV}'S LAST LINE
  {SEL}, REPLACE SAMPLE: ALL SET ANEW
  {SEL}, CREATE: ON AN EMPTY SLOT
    {EDIT} THERE DOES THE SAME
  START, END: THE PART THAT PLAYS
  LOOP: OFF, FWD OR PING; FROM, TO: THE
    PART THAT REPEATS; RELEASE: THE FADE
  BASE NOTE: AS IS; TUNE: 1/128 SEMITONE
  ENVELOPE: THE SECOND PAGE
  ```

## 5. The EDITOR page

- **A sample is an item of its own** on both panels: on the FM-1 panel numbered (`23 KICK`), on the TABLE without a
  number, as a loose sample is now. The SAMPLES group lists them all; a sampler's item no longer holds samples, and
  its size is its instruments'.
- **A sampler's tab:** an instrument's SAMPLE names any sample on its panel (by number on the FM-1 panel, by name on
  the TABLE). Its keys:
  - **on an instrument: DELETE** (the instrument goes, its sample stays; one step of the tab's UNDO, as the
    selection's DELETE) **and REPLACE SAMPLE:** a pick of any sample on the panel; the instrument then plays it as a
    new one would, every slider back to a new instrument's (START at the sample's start, END at its end, LOOP OFF,
    BASE NOTE, TUNE, VOLUME, LEVEL and the envelope as made), its name following when it was the old sample's;
  - **on an empty slot: CREATE,** a pick of any sample on the panel: a new instrument there playing it whole, named
    after it.

  The card of the sampler's samples becomes **THE SAMPLES IT USES** (each sample its instruments name, with NEW
  INSTRUMENT FOR IT, at the first empty slot).
- **A sample's tab** (the SAMPLE tab, as now): PLAYED BY lists the instruments in every sampler that name it; their
  START, END and loop points follow a change on SAVE, in every sampler.
- **What goes along in a move between panels:** a sampler takes the samples its instruments name, as a song takes the
  cartridges it uses. On the FM-1 panel each takes the number it had (from a disk or a READ) when free, else a free
  one, **passing over the numbers references still name** ([../files/references.md](../files/references.md#a-reference-to-nothing)),
  so an instrument naming a missing sample never starts playing an unrelated one; the instruments' references are
  written anew. A sample moves alone as an item of its own.
- **PASTE of sample instruments:** the clipboard holds what was copied, the instruments with their samples
  ([202610311848-editor-instruments.md](202610311848-editor-instruments.md) section 2: a paste is the same whatever
  happened to the originals since). On a panel holding the same sample (section 6: the whole record equal) the
  pasted instrument names it, nothing copied; otherwise the sample comes along, on the FM-1 panel at a free number,
  and with none free the paste is refused: `NO FREE SAMPLE NUMBER`.
- **New samples** (a dropped sound, a take, SAVE AS A NEW SAMPLE, SAVE AS SAMPLES) are items of their own, as now. MAKE
  A KIT makes a sampler whose instruments name the slices' samples, and the FUSION cartridge, as now.
- **A module** (MOD, S3M, XM) becomes its samples and samplers of up to 64 instruments naming them (a sampler for
  every 64 instruments, as now). With fewer than its samples' count of free numbers it is refused, saying how many
  it needs.
- **DELETE** of a sample on the FM-1 panel takes the instruments that name it, in every sampler there, after the
  page's own question saying how many (section 3). On the TABLE, whose items have no numbers, it takes what uses it
  as DELETE does there now (the instruments, and what uses those). **ALSO WHAT ONLY IT USES** on a sampler takes the
  samples no other sampler's instrument names.
- **An old sampler the page could not convert** (past 256 samples, section 6) is an item marked `OLDER FORM` on the
  FM-1 panel: listed with its size, neither played nor edited, and DELETE takes it (its old audio with it).
- **WRITE** sends the current versions. To an FM-1 running an older PT/FM, which takes only the old sampler and the
  old audio, it converts down as WRITE does for every type ([../files/disk.md](../files/disk.md#versions), each new
  version with its converter both ways): each sampler becomes an old sampler holding copies of the samples its
  instruments name, numbered under it (64 instruments name 64 samples at most, so each fits), and the samples
  themselves are not sent. Its `NOT KEPT` names what is lost or costs room: the samples no instrument names (not
  written), and each sample several samplers name (a copy in each, its size counted that many times).

## 6. The old form and its converter

- **The FM-1 reads the new form only** ([../files/README.md](../files/README.md#extending)): an old sampler or old
  audio is kept untouched and not read (section 3), so an old sampler is absent from the lists and what names its
  instruments plays nothing, until it is converted.
- **The EDITOR page converts as it loads:** a disk, a backup, a READ, and what it keeps in the browser (the TABLE, the
  FM-1 panel and its base, once, at the first load). Each old sampler becomes a sampler of version 2 and its samples
  samples of their own, each entry moved from the table into its sample; **the same sample twice** becomes one,
  both samplers naming it: the same means the new record's whole payload equal, the entry (name, rate, coding,
  length) and the audio, as a CLONE's copy is; the same audio under two names stays two samples, so no name is
  lost. Converted samplers keep their numbers and their instruments' slots, so the cells, FUSION's layers, the loops
  and the favourites that name them are untouched.
- **What the old form can hold that the new cannot name:**
  - an old instrument naming an emptied entry of its table (its sample deleted: a slot's REVERT can bring such an
    instrument back today), or an entry past the table's end, played nothing: it becomes an empty slot, as deleting
    its sample now deletes it, and what named it is a reference to nothing (grey);
  - old audio no old sampler's table names (no old sampler at its number, or past its table's count) has no entry,
    so no rate and no name: nothing can play it, and the conversion deletes it, counting it where it reports.
- **The samples' numbers** on the FM-1 panel: free numbers, the lowest first, sampler by sampler in number order and
  in each in its table's order, passing over numbers references name. **The demo disk is numbered exactly so,** so
  an FM-1 converted from the old demo disk holds the records the new demo disk uploads.
- **Past 256 samples:** whole samplers are converted in number order while their samples fit in the numbers left,
  every sample of each (none is left out, so nothing is lost); the rest stay in the old form, marked `OLDER FORM`
  (section 5), and the page says how many samples they need and that deleting them, or other samples, frees
  numbers.
- **The EDITOR page's first WRITE of a converted panel** rewrites every sample (each record's key and payload
  change): it says so, offers its BACKUP of the old records first (they are not "seen already" for it), and when the
  new samples do not fit beside the old ones it writes the user area anew, as WRITE does when room runs short
  ([../editor.md](../editor.md#the-fm-1-read-write-and-disks)). The old records are planned away in it (deleted).
- **The install page and the emulator** share the converter ([../files/disk.md](../files/disk.md#versions): "the
  editor and the install page, which share the converters"): a disk of the old form uploaded there (UPLOAD CUSTOM
  DISK, an old backup) is converted as it loads, and the emulator converts the flash it keeps in the browser at its
  first load after the change, as the install page converts an FM-1 (section 7).
- **Going back to an older PT/FM loses samples unless they are copied first.** It does not read the new form, and
  worse: its store takes only kind 0x10 for a record that crosses blocks, so at its start every sample over 4 KB
  (and any that crosses a block in an audio run) counts as damaged, is left out, and its blocks are free for its
  next save. Its NEW may also land on a converted sampler's number. So going back **must** start with READ, then
  DOWNLOAD DISK on the EDITOR page, and end with WRITE (which converts down). The CHANGELOG, the manual and
  disk.md say so, and [../files/README.md](../files/README.md#extending)'s "a device keeps the records it does not
  know" gets its exception for today's firmware. (A key inside kind 0x10, which today's firmware keeps as audio,
  was weighed: today's NEW, CLONE and MOVE delete all audio under a cartridge number, so going back would lose
  samples that way too; the copy first is needed either way, and the new kind keeps the keys apart.)

## 7. Installing over an older PT/FM: the install page converts

As [../files/disk.md](../files/disk.md#versions) has said since the files were settled: **after an install that
changes a type's version, the install page reads the records of the older version back, converts them, and writes
them back.** This is the first install to do so; every sampler is converted, the user's own as the demo disk's, with
nothing asked of the user and no warning before.
- **The install itself never touches the user area,** as always: the old samplers and audio wait there, unread, for
  the conversion. The new firmware runs it; the page reads records of any kind and version by their addresses
  ([../files/blob.md](../files/blob.md#seeding): READ and DATA).
- **When:** after every INSTALL FIRMWARE that ends well, a resumed one included, unless CLEAN INSTALL is on (it
  empties the user area). The page waits for PT/FM to answer as CLEAN INSTALL's erase waits for it, and converts when
  the LAYOUT lists a live record of an older version the shared converters know (an old sampler, old audio, the
  loops' version 3) and the firmware takes that kind's current version. Over another
  firmware, or with nothing in the old form, there is nothing to do and nothing is shown. The status line shows it as
  a step of the install: `Converting the samplers to the new form: 2 of 5. Keep the FM-1 connected.`, then `Done:
  the FM-1 runs PT/FM (FM-1_7101); 5 samplers converted to the new form, 33 samples.`
- **How: whole samplers a save,** in number order, as many in one save as fit together (fewer saves leave less
  slack: each save's audio starts a run of its own and opens a record block, audio blocks are never appended to, and
  blocks two old samplers' audio share are free only once both are converted). A save writes the samplers' version 2
  under their numbers and their samples as samples of their own (numbered as in section 6; the same sample merged,
  also with one an earlier save converted), deletes their old audio, and empties what section 6 says cannot be
  named. It is a partial plan: everything else on the FM-1 is kept as it is, whatever its kind or version, and is
  not checked against what the device takes (only the records written are). Each save goes into free blocks only
  (keeping the device's reserve when it can, else to the user area's last block), never over blocks that hold
  records, so a power cut or a pulled cable leaves the samplers converted so far and the rest as they were: nothing
  is lost, and the next INSTALL FIRMWARE (the install itself skips what is already equal) goes on from there; with
  nothing changed on the FM-1 between, it numbers exactly as one run would. The old audio a save deletes frees its
  blocks for the next one: an FM-1 holding the demo disk (about 140-144 KB free; its samplers' audio 17-46 KB each,
  157 KB in all) converts with room to spare.
- **What it reads back:** the old samplers and their audio, the version 2 samplers already on the FM-1 (small: so
  the numbers their instruments name are passed over), and the samples a merge compares (those of the same length
  as a sample being converted); nothing else.
- **The map's room:** each converted sample adds a live record while its old audio's deletion stays listed until
  compaction, and the store lists at most a fixed number of records (560): past it, a start would drop records and
  count their blocks free. The LAYOUT reports that number (in its spare bytes), and the page plans each save within
  it; a sampler that does not fit there stops the conversion as one too big for the free blocks does (below).
- **Its other records of an older version** that the shared converters know (today the loops' version 3) are
  converted in the same way, each in a save of its own, as disk.md promises.
- **When a sampler does not fit** even alone (its audio in the free blocks, or its records in the map's room), the
  conversion stops before it: `Done: the FM-1 runs PT/FM (FM-1_7101); 3 of 5 samplers converted. Sampler 05 needs
  46 KB more room: delete something on the FM-1 and press INSTALL FIRMWARE again, or on the EDITOR page READ, then
  WRITE (it writes the user area anew, after its backup).` The install page never writes over records; the EDITOR
  page's WRITE, with its BACKUP, is the way that does.
- **Past 256 samples:** the samplers whose samples find no number stay in the old form (section 6); the line says how
  many and that the EDITOR page lists them as `OLDER FORM`.
- **Refusals:** edits not saved on the FM-1 (made since its start: an install's restart leaves none) stop it with the
  upload's words (`SAVE them (or REVERT) on the FM-1, then press INSTALL FIRMWARE again`); a SAVE on the FM-1 meanwhile
  (CHANGED) has it read the layout and plan again, as an upload does.
- **Installed by another installer** (one that only writes the firmware), the FM-1 keeps its old samplers unread
  until the install page's next INSTALL FIRMWARE, or the EDITOR page's READ, then WRITE.
- **What else changes with it:** the install page now reads back and writes into the user area, for this conversion
  alone: blob.md's "it reads nothing back and writes nothing into the user area", the install page's own help line
  ("the install writes nothing into the user area"), layout.md's and AGENTS.md's "the firmware alone", and disk.md's
  "as one save" say so; saving.md's "the install page backs everything up first" was never so and goes (copies of
  the library are the EDITOR page's).
- **The emulator,** with no install step (its page updates with the site), converts the flash it keeps the same way,
  at its first load after the change (section 6).

## 8. Everywhere it is built

- **The firmware:** the sampler engine finds a voice's sample by its number (the record (0x11, n, 0) read in place,
  its entry from the record itself); the sample instrument's page, K1 on SAMPLE, THE SAMPLES list and its line in
  INSTRUMENTS' cartridge list; the sampler's SEL menu (REPLACE SAMPLE, CREATE, INIT keeping the sample, DELETE in
  edit mode), EDIT on an empty sampler slot; DELETE of a sample across samplers, deletions first, REVERT bringing a
  pending deletion back; the sampler number no longer tied to audio (section 1's places); CLEAN UP, MISSING
  REFERENCES and CHECK REFERENCES (`SAMPLER 01:05: SAMPLE 23`, `CARTRIDGE 03: OLDER FORM`); an old sampler's
  number taken and the old records left alone (section 3); the store's audio both kinds, 0x10 and 0x11 (section 1);
  CHECK STORAGE's entry check for 0x11; a sample's DELETE in edit mode leaving it first; the LAYOUT reporting the
  map's room; a sampler's CLONE and MOVE as any cartridge's (the store's audio copy goes); seeding takes the new form
  only (0x04 version 2, 0x11 version 1; no longer 0x04 version 1 or 0x10); the help's SAMPLE SOUNDS page.
  - **Its room:** the debug build has 216 B left (the release 3,144 B; the builds of 2026-11-02). The removal is built first and
    measured (the store's sampler clone and its write steps, about 1.5 KB, with the progress code and texts), then
    the additions are estimated against what it freed. If they do not fit, what gives way is named then, from the
    known levers: more main-loop code made cold, more debug-only code moved into the debug part
    ([../debug.md](../debug.md#the-debug-part)), the built-in instruments compressed.
- **The playground,** its twin: samples numbered on their own and the samplers' instruments naming them; K1 over all
  samples; THE SAMPLES list both ways and its line; the SEL menu (REPLACE SAMPLE, CREATE, INIT, DELETE in edit mode);
  EDIT on an empty sampler slot; `THE SAMPLE IS MISSING`; DELETE across samplers and REVERT with a pending deletion;
  CLONE and MOVE without audio; CLEAN UP, MISSING REFERENCES and CHECK REFERENCES across samplers; the storage
  figures (the samples' table no longer counted in a sampler, each sample's entry in its own record).
- **The EDITOR page** (sections 5, 6): its model, the panels, the tabs, moves, PASTE, the converters (modules, MAKE A
  KIT), READ, WRITE (and its conversion down), disks, the conversion up of everything it keeps, `OLDER FORM` items;
  `editor/js/records.js` and `tools/ptfm_records.py` (kept equal) know both forms.
- **The install page and the emulator's uploader** (`fw/web/seed.js`, the shared converter): the new form, both
  kinds of audio where it means audio (placing, the 4 KB check); a partial plan (records written and deleted,
  everything else kept unread); the LAYOUT's map room; the install page's conversion after every install
  (section 7: read back, convert, one sampler a save in free blocks, the other old versions the converters know); the
  emulator's conversion of its kept flash. seed.js holds no converter today: all of it is new there.
- **The demo disk's tools** (`demo-disk/tools/`, `tools/pack.py`, `tools/gen_playground.py`): samples numbered as the
  conversion numbers them, the samplers (the drum kit's too) naming them.
- **The specs:** [../files/store.md](../files/store.md) (the kinds' table: 0x11, 0x10 the old audio, 0x04 version
  2; both kinds audio; Deletions first), [../files/instruments.md](../files/instruments.md),
  [../files/ptr.md](../files/ptr.md), [../files/references.md](../files/references.md),
  [../files/README.md](../files/README.md) (samples numbered `00`-`FF`, not under a sampler's `00`-`3F`; the exception
  to "a device keeps the records it does not know"), [../files/disk.md](../files/disk.md) (versions: the install
  page's conversion, saves of whole samplers, going back; a disk's order, objects whole, audio last, with samples
  as audio: coded too in seed.js's makeDisk, `editor/js/model.js` and `tools/ptfm_records.py`),
  [../files/blob.md](../files/blob.md) (the install page's read back and conversion, the partial plan, the LAYOUT's
  map room), [../files/ram.md](../files/ram.md) (the sampler CLONE's flash writes go),
  [../ui/saving.md](../ui/saving.md) (REVERT with a pending deletion; the install page's backup line; a sampler's
  CLONE and MOVE no longer writing the flash), [../ui/screens/instruments.md](../ui/screens/instruments.md) (`CLONE
  FAILED: FLASH ERROR` goes), [../layout.md](../layout.md) and `AGENTS.md` (the install page),
  [../ui/screens/settings.md](../ui/screens/settings.md) (CLEAN UP, MISSING REFERENCES, CHECK REFERENCES, CHECK
  STORAGE), [../ui/help.md](../ui/help.md), [../editor.md](../editor.md) (samples no longer filed under a sampler's
  number; `SAMPLER 0B IS FULL: 64 SAMPLES` goes), [../emulator.md](../emulator.md),
  [../debug.md](../debug.md) (AUTOTEST's sampler instrument; the sampler CLONE's flash writes), [../connections.md](../connections.md) (the install
  page); the install page's help line; the manual (the basics, the instruments chapter, the EDITOR page's, the specs' "up to 64 a sampler", the
  glossary, the messages; going back: the copy first); CHANGELOG (FEATURE: independent samples, REPLACE SAMPLE,
  CREATE; CHANGE: the samples list, INIT in a sampler, CLONE and MOVE of a sampler, DELETE of a sampler; the
  conversion and going back, its copy a must; BUG: a sample's DELETE in edit mode undone by HOME).
- **Tests:**
  - the store's: a sample's record; a sampler without audio; DELETE across samplers in one save, and split by
    deletions first with a cut between; DELETE of a sampler leaving its samples; CLEAN UP; the old records kept
    unread, an old sampler's number taken; old audio and new samples crossing blocks side by side, mounted, compacted
    around and never written over; the entry check;
  - the firmware's UI: the list three ways and its line, K1 (passing pending deletions), REPLACE SAMPLE, CREATE,
    INIT, EDIT on an empty slot, DELETE's warning, DELETE in edit mode (of an instrument, and of a sample from the
    list, HOME after it), REVERT after a sample's deletion (pending and after a split save), PASTE after it, `THE
    SAMPLE IS MISSING`, CHECK REFERENCES' lines, MISSING REFERENCES, no samples at all, CLONE and MOVE;
  - the sound: a voice's sample found by number, output equal to today's on the converted demo disk;
  - the playground's twins of each;
  - the EDITOR page's: conversion of a disk, a READ, the TABLE and the FM-1 panel; the same sample merged and the
    same audio under two names kept two; emptied entries and audio with no entry; past 256 samples (`OLDER FORM`,
    deleted); moves with samples, numbers passed over; PASTE (the sample deleted since the COPY; no free number);
    WRITE converting down and its `NOT KEPT`; the conversion's BACKUP and rewrite; MAKE A KIT; a module;
  - **the converter:** the old demo disk converted equals the new demo disk record for record (the JS converter
    against `tools/pack.py` and `tools/ptfm_records.py`);
  - the install page's conversion: whole samplers a save; records of kinds and versions the device does not take
    kept as they are; never a block holding records written over; cut short after each save and finished by the
    next INSTALL FIRMWARE, numbered as one run, a resumed install included; a sampler too big for the free room, or
    for the map's room; past 256 samples; UNSAVED and CHANGED during it; CLEAN INSTALL skipping it; the loops'
    version 3 converted, and the trigger firing for it alone; what it reads back and nothing else;
  - the emulator's conversion of its kept flash.
