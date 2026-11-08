# The editor page, EDITOR [beta]

Status: **settled** (2026-10-26). The record: [the editor proposal](proposals/202610260240-editor-beta.md); the
first one, [proposals/202610142355-editor.md](proposals/202610142355-editor.md), is set aside. Open points:
[open-questions.md](open-questions.md#data-and-tools).

**The editor** is a page for the computer that adds content to the FM-1 and backs it up. Two panels side by side
hold PT/FM's files: **TABLE**, everything dropped on the page, and **FM-1**, an image of what an FM-1 holds. Items
move between them with everything they use; READ and WRITE move the FM-1 panel to and from the device; every item
opens in a tab of its own to be edited. Only FILES, the two panels, is finished for the beta; the tabs give every
edit with plain controls, to be refined later.

## The page

- **One file,** `dist/editor/editor.html`, opened from the file system in Chrome or Edge (Web MIDI, an
  AudioWorklet), everything inlined as the other pages: classic scripts, the worklet from a `data:` URL, the
  firmware's WebAssembly. Made by `editor/build.py` from `editor/`.
- **The pages' look:** black, the FM-1 screen's EGA colours, the system's sans-serif, square boxes, keys' names
  yellow, labels cyan. No sidebar: the two panels take the width.
  - **The header:** `PT/FM EDITOR` (`PT/FM` a letter in each engine's colour, as the FM-1's STATUS draws it:
    [ui/screens/status.md](ui/screens/status.md)), a `BETA` mark, the tabs (FILES first, always there), UNDO and
    REDO. A click on `EDITOR` shows FILES too; `PT/FM` links to PT/FM's page, https://hitosu.github.io/ptfm/
    ([site.md](site.md)).
  - **The work area:** the tab shown.
  - **The footer:** the mini player ([the preview](#the-preview)) and the status line: what the last action did,
    with UNDO beside it, and the progress of READ, WRITE and imports.
- **Computer first.** Under about 860 px wide, FILES shows one panel at a time, with a TABLE / FM-1 switch on top.
  Every key and list works by touch; dragging between the panels is the mouse's, and the arrows do the same on
  touch.

## FILES

Two panels: **TABLE** on the left, **FM-1** on the right, and between them two arrows, → (to the FM-1) and
← (to the TABLE).

### TABLE

- **Everything dropped on the page,** of any size the browser lets the page keep (IndexedDB): files to look
  through, pick from and edit before anything reaches the FM-1.
- **Its items have no numbers.** They refer to each other directly, so the TABLE holds any number of songs,
  patterns and cartridges, and two imported modules never clash. Numbers are given only on the FM-1 panel
  ([moving items](#moving-items)).
- **Each item remembers where it came from:** the dropped file's name, a disk's name, `FM-1` and the time for
  what came from the FM-1 panel, `BACKUP` and the time for what a WRITE backed up, `NEW` for what + NEW made,
  `RECORDING` and the time for a take ([recording](#recording-and-cutting-sounds)), `FROM` and a sample's name for
  one saved from another (`FROM KICK`), `KIT FROM` and the sample's name for MAKE A KIT's; and the number it had on
  the FM-1, if any.
- **Names:** an item keeps its own; one with none gets a name made from its source on the TABLE (`MYSONG P07`,
  `SMP 0C`). Patterns have no names on the FM-1: such a name lives only on the TABLE.
- **`+ NEW`** makes an item here, as the FM-1 makes a new one, and opens it in its tab: a SONG (with an empty
  pattern in its one position), a PATTERN (8 tracks of 64 rows, empty), a cartridge of each engine (one plain
  instrument), a SAMPLER (empty: sounds dropped on it fill it), SETTINGS, MIDI MAP, LOOPS or DJ SETUP (their
  defaults). Their source is `NEW`.
- **Its keys:** `+ ADD FILES`, `+ NEW`, `● RECORD` ([the recorder](#the-recorder)), `DELETE ALL`, the filter and
  the views.

### FM-1

- **An image of an FM-1's user area,** kept in this browser too: what the last READ found, a disk put in by
  UPLOAD DISK, or a set built by hand from the TABLE with no FM-1 connected (then saved by DOWNLOAD DISK, to be
  put on an FM-1 later). Nothing reaches the device but by WRITE ([the FM-1](#the-fm-1-read-write-and-disks)).
- **Its header says where its contents come from** (`READ 26 OCT 14:02 · PT/FM 0.26`, `DISK BACKUP.PTFMDSK`,
  `EMPTY`) and how many changes are not written yet; each such item is marked with a dot.
- **The built-in instruments** are listed among the cartridges, marked `BUILT-IN`, their size `IN FW`; a changed
  one is marked `CHANGED` and counts its record's size. Moved to the TABLE, one is copied there as a cartridge of its
  own ([moving items](#moving-items)).
- **The system items still at their defaults** are listed too, dark grey, marked `DEFAULT`: nothing to move or
  delete; a tab opened on one starts from the firmware's own default, and its SAVE makes the record.
- **What the page cannot read** (a record of a version or a kind it does not know, read from the FM-1 or on a disk put
  in by UPLOAD DISK) is listed dim and kept as it is: it never moves, and WRITE neither changes nor removes it (a
  WRITE of the whole user area reads it first and writes it back as it was).
- **The space bar:** the user area's 103 blocks of 4 KB, as data, audio and free. Nothing is kept free: when fewer
  blocks than the 16 (64 KB) advised for the FM-1's own saves are left, it says what that costs
  ([files/store.md](files/store.md#where-things-go)): under 10 blocks the FM-1 cannot save new things (it can
  still delete), under 3 it can save nothing, not even a delete, until a WRITE frees room.
  It counts blocks as the uploader's planner places records (audio in blocks of its own), not summed bytes.
- **Its keys:** `READ`, `WRITE` (with the count of changes not written), `UPLOAD DISK`, `DOWNLOAD DISK`, `REVERT`,
  `DELETE ALL`, the filter and the views.

### Both panels

- **Groups by kind:** SONGS, PATTERNS, CARTRIDGES (DX7, Juno-106, TB-303, CHIP, FUSION), SAMPLERS, SAMPLES and
  SYSTEM (SETTINGS, MIDI MAP, LOOPS, DJ SETUP, EDITOR'S PATTERN, KEYS' SONG). A click on a group's header folds or
  opens it; Ctrl or Shift + click adds all its items to those picked. The header shows the count and their size.
  COLLAPSE ALL and EXPAND ALL fold or open every group of the panel; the folded groups are remembered with the views.
- **Three views,** picked per panel and remembered: DETAILS, LIST, ICONS.
  - **DETAILS:** a row an item: its icon, its number (FM-1) or source (TABLE), its name (a pattern on the FM-1
    panel shows where it plays, `CITY LIGHTS · POS 02`), its kind (`4 TRACKS × 64`, `SAMPLER · 10 INSTR · 7 SMP`: the
    samples its instruments name,
    `8-BIT · 22.1 KHZ · 0.42 S`), its size, its size with what it uses, and how many items use it. Each column
    sorts.
  - **LIST:** compact names in columns.
  - **ICONS:** tiles: a pattern's notes in its engines' colours, a sample's waveform, a song's positions; the
    other kinds their icon.
- **Icons** by kind, in the engine's colour of [ui/colours.md](ui/colours.md) (DX7 light red, Juno-106
  light cyan, TB-303 light green, CHIP light blue, FUSION light magenta, samplers and samples yellow); a system
  item still at its default, dark grey.
- **The filter** keeps the items whose name, source, number or kind holds what is typed.
- **Picking:** a click picks one, Ctrl adds, Shift a range, the arrows move, Ctrl+A picks all. Double-click or
  Enter opens the item's tab, Space plays it, Delete deletes, F2 renames, on the FM-1 panel F4 sets a cartridge's number
  ([moving items](#moving-items)). The right-click menu: PLAY, EDIT IN A TAB, RENAME, on the FM-1 panel SET NUMBER…, the
  move to the other side, SELECT WHAT IT USES, SELECT WHAT USES IT, on the TABLE OPTIMIZE n SAMPLES… (below), DELETE.
- **OPTIMIZE SAMPLES** (the TABLE's menu): the samples picked, and every sample a picked sampler's instruments name, made smaller
  at one level, each as its tab's OPTIMIZE makes it (SAMPLE, below), its instruments following. A dialog lists the
  three levels with the size before and after and how many come out smaller (SMALLER picked at first, the last
  choice kept while the page is open); a sample a level would not make smaller stays as it is; OPTIMIZE applies
  it as one step (UNDO).
- **The selection bar** under each panel: how many are picked, their size, and their size with what they use; for
  one item, what it uses and what uses it, each a link to it; PLAY, EDIT, DELETE, on the TABLE `DOWNLOAD AS DISK`
  ([the FM-1](#the-fm-1-read-write-and-disks)), and the move key.
- **Sizes:** an item's size is its records' bytes as the FM-1 stores them (each with its 16-byte header, to the
  next multiple of 4); *with what it uses* adds everything it uses, the built-in instruments not counted.

## Items, references, hashes

- **Items are PT/FM's objects,** each made of all its records ([files/store.md](files/store.md#a-record)): a
  song; a pattern with all its parts; a cartridge of each kind; a sample (its record: its entry and its audio); the
  system items.
- **A sample is an item of its own** on both panels ([files/instruments.md](files/instruments.md#samples)): on the
  FM-1 panel numbered (`23 KICK`), on the TABLE without a number. Any sampler's instruments name any sample on their
  panel (by number on the FM-1 panel, by name on the TABLE). A sampler's item holds instruments only, and its size
  is its instruments'.
- **The old form** ([files/instruments.md](files/instruments.md#the-old-form)) is converted as the page loads
  anything ([files/disk.md](files/disk.md#the-samplers-conversion)). An old sampler the conversion could not take
  (past 256 samples) is an item marked `OLDER FORM` on the FM-1 panel: listed with its size and the sample numbers it
  needs (`SAMPLER · OLDER FORM · 64 SAMPLES NEED NUMBERS`; its move refused with them and the numbers free), neither
  played nor edited; DELETE takes it, its old audio with it.
- **A sample whose entry and audio differ** ([files/instruments.md](files/instruments.md#samples)) is read as missing,
  as the FM-1 reads it: listed dim, `SAMPLE · ENTRY AND AUDIO DIFFER`, kept as it is (never moved, written back as it
  was), its number taken; the instruments naming it name nothing, grey.
- **References** ([files/references.md](files/references.md)): a song's positions name patterns, a cell its
  instrument's cartridge, a FUSION slot its pattern, a sample instrument its sample, a loop its instrument, a
  favourite its source instrument; the EDITOR's pattern names cartridges as any pattern does.
- **A FAVORITES cartridge** (kind 0x07, [files/instruments.md](files/instruments.md#favorites)) is an item like any
  cartridge: READ, WRITE, UPLOAD and DOWNLOAD DISK and the disks carry it; its kind is among the TABLE's groups and
  `+ NEW`'s kinds (a new one named `MY FAVORITES`), shown as `FAVORITES` in DETAILS, its icon light grey. Moving it
  to the other panel takes every cartridge it names along, as a song takes its cartridges (the built-in ones
  excepted); renumbering rewrites its references; it is reused by hash as any item. Nothing takes a FAVORITES
  cartridge along: nothing refers to it.
- **Remembered places are not uses:** the settings' current instrument and the EDITOR's numbered pattern, DJ's decks'
  songs. They never take an item along on a move nor keep it from a DELETE; moved without what they name, they find
  it on the other side when it is there (by its hash), else they are cleared, as the FM-1 empties a deck whose song is
  gone.
- **Every item has a content hash,** made from its payload with each reference replaced by the hash of what it
  refers to: equal content at other numbers hashes equal, and a song's hash covers its patterns', theirs their
  cartridges'. A sampler cartridge's covers the samples its instruments name; a sample's is its whole record's,
  entry and audio (the same audio under two names is two samples). A built-in instrument's hash is its number.
  Items are told apart by hash, never by number (and, on a move, by their kind of use: a changed built-in and a plain
  cartridge of the same content are two items, [moving items](#moving-items)); a copy meant to be edited apart is made on the FM-1 (CLONE), on the
  TABLE from a built-in ([moving items](#moving-items)), or differs once edited.

## Moving items

- **→ copies the TABLE's picked items to the FM-1 panel, ← the FM-1 panel's to the TABLE;** dragging them onto the
  other panel does the same. The side they come from keeps them.
- **With everything they use:** a song travels with its patterns, their cartridges, the samples a sampler's
  instruments name, a FUSION's patterns; a sample moves alone as an item of its own. The built-in instruments a moved
  item uses never travel: a reference to one stays a reference to it, as every PT/FM carries them. A changed built-in
  travels as a cartridge that remembers its number; on the FM-1 panel it lands on that number when that number holds
  the firmware's own or the same change, else at a free number.
  On the FM-1 panel a number names what the panel holds there, as on the FM-1: once a changed built-in lands
  on its number (a move, a SAVE), everything that names that number uses it, and the tabs' instrument lists
  offer it in the firmware's place.
- **A built-in cartridge picked and moved to the TABLE** (← or dragged; the firmware's own or a changed one) is copied
  there as a cartridge of its own: its contents (its changes too) and its name, no number to remember, not
  `BUILT-IN`. It is edited there as any cartridge; moved to the FM-1 panel it takes a free number (`00`-`EF`), a new
  cartridge beside the built-in. Its hash is its content's, so the same built-in moved twice is one copy there. A
  built-in's own number changes only on the FM-1 panel (its tab's SAVE). What uses a built-in is unchanged: a changed
  built-in a song takes along still remembers its number, never taken for a copy of it (below). ← of a system item at
  its default has nothing to move.
- **What the other side already holds is used, not copied:** an item whose hash the other side holds is that
  item there. A backup's song moved back finds its cartridges and samples already on the FM-1 panel. Equal items
  within one move (one song from two disks) are one item there. A cartridge that remembers a built-in's number (a
  changed built-in) and a plain one of the same content (a copy of it) are never the same item, nor are what uses
  the one and what uses the other; on the FM-1 panel a changed built-in whose number holds another change is the
  cartridge it took a free number as before.
- **Numbers on the FM-1 panel:** an item takes the number it had (from a disk or an earlier READ) when it is free,
  else the lowest free one, `00`-`EF` ([files/patterns.md](files/patterns.md#numbers); the seven cartridge
  kinds share one number space). Every reference to it is written anew. A sample takes the number it had when free,
  else a free one, `00`-`FF` apart from the cartridges', **passing over the numbers sample instruments still name**
  ([files/references.md](files/references.md#a-reference-to-nothing)), so an instrument naming a missing sample never
  starts playing an unrelated one; the instruments naming it are written anew. A move needing more sample numbers
  than are free (a module's samples) is refused, saying how many it needs (`NOT ENOUGH SAMPLE NUMBERS: 70 NEEDED, 12
  FREE`).
- **SET NUMBER… (F4, on the FM-1 panel)** gives one cartridge another number, typed as two hex digits, `00`-`EF`; the
  dialog says what each number would do. A free number moves it; a number another cartridge holds swaps the two,
  whatever their kinds, as all seven kinds share the numbers. **Every reference follows,** as MOVE and SWAP answered
  YES on the FM-1 ([files/references.md](files/references.md#move-and-swap)): the cells, the loops' table, the
  favourites, the settings' current instrument; references naming nothing go to the place left and still name nothing.
  Samples keep their numbers, a space of their own. No question first: it happens at once, one step of UNDO, the
  status line saying what changed (`DRUM KIT IS NOW 2B · 116 REFERENCES FOLLOW`), and reaches the FM-1 with WRITE, which
  does not list a cartridge that took another number among what it removes. Refused: a built-in, changed or not, and the
  numbers `F0`-`FF`; a panel holding a version the page cannot read, as what it names could not follow; a tab of the
  panel with changes not saved, which holds the numbers as they are: it is saved or closed first. A tab with no changes
  follows, its lists of cartridges too.
- **Samples moved alone** stay samples, no sampler made for them; dropped on a sampler cartridge's item, they come
  with an instrument each at its first empty slots, each playing its sample whole, named after it (64 at most).
  Otherwise instruments are made on the FM-1 or in the sampler's tab (CREATE, NEW INSTRUMENT FOR IT).
- **The system items:** the FM-1 holds one settings record, MIDI map, loops and DJ setup. Moving one to the FM-1
  panel replaces the panel's, after asking unless the panel's is the default; of several of one kind in one move the
  first is taken, and the status line says the others stayed. The EDITOR'S PATTERN and the KEYS' SONG moved to the
  TABLE become a plain pattern and song (`EDITOR FE`, `KEYS FE`), as a disk's dropped on it do; moved back, they take
  free numbers. A default (no record) has nothing to move.
- **What does not fit is refused, and nothing moves:** the space bar shows the missing part red, and the status
  line says how much is missing (`DOESN'T FIT: MYSONG NEEDS 72 KB MORE`). Room comes from deleting on the FM-1
  panel or trimming on the TABLE. So is a move needing a number when none is free.
- **No question first:** a move happens at once. The status line says what moved, what was there already and
  what took another number (`→ FM-1: SONG 12 SKETCH 19 + 3 PATTERNS · 3 ITEMS ALREADY THERE · MYSONG P00 IS NOW
  3A`), with UNDO; the moved items are picked on the other side.

## DELETE and DELETE ALL

- **DELETE takes with an item everything that uses it,** and what uses that, to the end, in the same dialog: it
  names each of them (a link to it) and what it uses, and DELETE ALL deletes them together; a system item on the
  FM-1 panel goes back to its default.
- **Otherwise it asks,** with ALSO WHAT ONLY IT USES, on at first: what goes with it (patterns, cartridges, samples
  nothing else uses) and the bytes it all frees. On the FM-1 panel that is the quick way to make room.
- **A sample on the FM-1 panel** takes the instruments that name it, in every sampler there, after the page's own
  question saying how many (as the FM-1's DELETE does, [files/instruments.md](files/instruments.md#deleting)); what
  named those instruments is as for any deleted instrument. On the TABLE, whose items have no numbers, it takes
  what uses it as any DELETE there (the instruments, and what uses those). A sampler cartridge's DELETE takes its
  instruments; **ALSO WHAT ONLY IT USES** takes the samples no other sampler's instrument names.
- **A FAVORITES cartridge never goes along with what it names:** deleting an instrument or a cartridge leaves the
  favourites naming it as they are, `MISSING` in grey, as on the FM-1; a FAVORITES cartridge's own DELETE never takes
  its sources (ALSO WHAT ONLY IT USES leaves them); and a cartridge a favourite names is still used, so deleting a
  song does not take it as one only that song used.
- **The built-in instruments** have nothing to delete; deleting a changed one brings the firmware's own back (BACK
  TO BUILT-IN). Nothing that uses it goes with it: what uses it plays the firmware's own then; and ALSO WHAT ONLY IT
  USES never takes one back by itself.
- **DELETE ALL** empties a panel, after asking. On the FM-1 panel the built-in instruments stay (changed ones back
  to the firmware's), and the system items go back to their defaults: a WRITE then empties the FM-1's user area.
- **Nothing changes on the FM-1 until WRITE.**

## UNDO

- **One history for FILES,** 100 steps: moves, deletes, renames, imports, a tab's SAVE, READ, UPLOAD DISK, REVERT,
  DELETE ALL, a take's SAVE, SAVE AS A NEW SAMPLE, SAVE AS SAMPLES, MAKE A KIT. Ctrl+Z and Ctrl+Y, or the header's keys. A tab has its own until SAVE.
- **WRITE is not taken back** (the FM-1 has changed): UNDO and REDO bring back what the panels showed, never what the
  FM-1 is known to hold (what the last READ or WRITE found there). The history is the page's memory and goes with a
  reload.

## The FM-1: READ, WRITE and disks

- **No connection is kept.** READ and WRITE each find the FM-1 among the computer's MIDI ports by its name, as the
  install page does, talk the seeding protocol ([files/blob.md](files/blob.md#seeding)) for as long as they
  run, and let it go. An FM-1 not running PT/FM (another firmware, update mode) is named in the status line,
  and nothing is read or written.
- **Another PT/FM version than the page's** is said in the FM-1 panel's header (`THE FM-1 RUNS PT/FM 0.25, THIS
  PAGE 0.26`), and READ and WRITE go on as long as the FM-1 takes the panel's (kind, version) pairs, or an older
  version of a kind that the page converts down to (below). The built-in
  instruments are shown and previewed as the page's own firmware has them. WRITE names, before writing, what uses
  a built-in instrument the FM-1's layout does not list (`SONGS 05, 0B USE F9, WHICH THE FM-1 DOES NOT HAVE`), with
  WRITE ANYWAY and CANCEL.
- **READ** asks for the layout, then reads the records back (READ and DATA, the user area only) and puts them in
  the FM-1 panel in place of what it showed; UNDO brings that back. After the first READ it reads only the records
  whose CRC changed since the last READ or WRITE.
- **WRITE** asks for the layout and plans with the uploader's own planner: records the FM-1 holds with the same
  CRC stay where they are, the rest go into free blocks, to the last one if need be (no reserve kept). **When the
  panel does not fit around what stays** (records kept where they are, blocks a deleted record's header still holds)
  **but fits the FM-1 emptied, the whole user area is written anew,** as the install page's REPLACE ALL does: the
  panel's records from the first block, every other block empty; what the page cannot read is read first and
  written back as it was. A panel that does not fit even the FM-1 emptied is refused. Before writing it shows:
  - that it writes the whole user area anew, when it does, and that this takes longer;
  - fewer free blocks after it than advised, what that costs the FM-1's own saves (as the space bar says it), with
    WRITE ANYWAY and CANCEL;
  - anything saved on the FM-1 after the panel's last READ or WRITE (its save number is newer), at the top, with
    READ FIRST, WRITE ANYWAY and CANCEL;
  - what it writes, and the FM-1's items it removes, by name, red;
  - that it backs up first, when the write replaces or removes anything;
  - what it converts down: an object of a version the FM-1 does not take, where the FM-1 takes an older version of
    its kind that the page converts to, is written as that version (`LOOPS 00: VERSION 4 WRITTEN AS VERSION 3`,
    each loop written out as it is heard, its SHIFT included: [files/loops.md](files/loops.md#the-record)), with what
    the older version cannot hold (`NOT KEPT: SOLO`; `NOT KEPT: VIEW, K1-K4'S JOBS, THE LENGTH`) and then WRITE
    ANYWAY and CANCEL. The panel keeps its
    own version; a READ after brings the FM-1's back converted up
    ([files/disk.md](files/disk.md#versions)). To an FM-1 that takes only the old-form sampler, each sampler is
    written as an old one holding copies of the samples its instruments name, numbered under it (64 instruments
    name 64 samples at most), and the samples are not sent; its `NOT KEPT` names the samples no instrument names
    (not written) and each sample several samplers name (a copy in each, its size counted that many times);
  - that a converted panel's first WRITE rewrites every sample, when the FM-1 still holds the old form: its BACKUP
    then takes the old records too (they are not "seen already" for it), and when the new samples do not fit beside
    the old ones it writes the user area anew, as it does when room runs short;

  When the FM-1 already holds exactly the panel, it says so and writes nothing. **BACKUP:** once you agree, before
  anything is written, it reads what the FM-1 holds that the write replaces or removes (every record whose CRC the
  panel does not have at its place; nothing else is read), leaving out what the last READ or WRITE saw exactly so
  (that CRC at that place): that was on the panel, and what was done with it since was chosen, so a READ, DELETE
  ALL and a WRITE back nothing up, and only what changed on the FM-1 since is backed up (the dialog says when
  nothing needs it). It puts that on the TABLE with what it uses, as a move to
  the TABLE does: an item the TABLE already holds (the same content, by its hash) is not copied again, only referred
  to. Their source is `BACKUP` and the time (`BACKUP 26 OCT 11:50`); UNDO takes the backup back as one step. A
  backup that cannot be made stops the WRITE with nothing written. Then it writes one save: BEGIN with the layout's
  last save number + 1 (refused as CHANGED when the FM-1 saved meanwhile: the page reads the layout, plans again and
  asks again, the newer save said at the top), the blocks, END; the FM-1 mounts. Its sound stops while
  it writes. It is refused with `SAVE OR REVERT FIRST` when the FM-1 has edits not saved, and when the FM-1 does
  not take a (kind, version) the panel holds and no older version of it the page converts down to (each named), or
  an object does not fit the older version. After it, the panel is what the FM-1 holds.
- **REVERT** puts back what the panel showed after the last READ or WRITE.
- **UPLOAD DISK** (or a disk file dropped on the FM-1 panel; any other file dropped there is refused, it goes on the
  TABLE) puts a disk's contents in the FM-1 panel in place of what it showed, numbers kept, the EDITOR'S
  PATTERN, the KEYS' SONG, the settings, the MIDI map, the loops and DJ's setup included: once written, a backup is
  back exactly. A file of a version the page cannot read stays in the panel as it is, shown dim, and is not
  written ([files/disk.md](files/disk.md#versions)); a file whose CRC does not match its bytes is left out, and said
  (a drop on the TABLE does the same).
- **DOWNLOAD DISK** saves the FM-1 panel as a disk (`ptfm-2026-10-26.ptfmdsk`): everything but the unchanged
  built-ins. It is the backup: READ, then DOWNLOAD DISK.
- **DOWNLOAD AS DISK** (the TABLE's selection bar) saves the TABLE's picked items as a disk with everything they use,
  numbered as a move to an empty FM-1 panel numbers them: each at the number it remembers when free, else the lowest
  free one, every reference written anew; the built-in instruments left out (references to them stay, as every PT/FM
  carries them), a changed one they use going along at its number; of several system items of one kind the first
  (the status line names the others). What that move refuses it refuses with the same words, `NO DISK SAVED` in place
  of `NOTHING MOVED` (a version the page cannot read, a sample too long, too many sample numbers), and so what does not
  fit an empty FM-1 (`DOESN'T FIT: MYSONG NEEDS 72 KB MORE ON THE FM-1. NO DISK SAVED.`): a disk is meant for an FM-1.
  The file is named after the item when one is picked (lowercase, spaces and signs a dash: `punch-in.ptfmdsk`), else
  `ptfm-selection-2026-11-04.ptfmdsk`; the status line says it as DOWNLOAD DISK's does, counting what the items took
  along (`DOWNLOAD AS DISK: punch-in.ptfmdsk, 14 ITEMS, 25 KB`). Neither panel changes, and it is no step of UNDO. Put
  in an FM-1 panel by UPLOAD DISK, the disk gives the items, numbers and hashes that move gives; dropped on the TABLE,
  items equal by hash to those picked and what they use.
- **The EDITOR's own pattern and the keys' song (`FE`)** are items like the others here: a backup keeps them and a
  backup put back restores them. A WRITE makes the FM-1 hold exactly the panel, so a panel without them removes
  the FM-1's, listed as removed before anything is written.

## Drops and conversions

Files dropped on the TABLE, or picked with `+ ADD FILES`, become its items, all of each file. The status line says
what each made and what could not be kept. Taking only a part (some of a module's patterns, a cut of a sound) is
done afterwards, in the item's tab ([the tabs](#the-tabs)).

- **Disks** (`.ptfmdsk`; any file whose first bytes are `PTFMDISK`, so a deprecated `.ptd` dropped still opens; the
  pickers offer `.ptfmdsk` only): their items as they are, each remembering its number. Putting a disk on the FM-1
  as it is goes through the FM-1 panel's UPLOAD DISK.
  Only what the FM-1 can hold is taken from a disk.
- **Tracker modules: MOD, S3M and XM,** read as IBXM reads them (the reference the conversion is tested against,
  [files/commands.md](files/commands.md#tracker-effects)): the song (tempo, ticks, the order, its restart, the
  global volume, the pans, the slides' kind), its patterns (a channel a track, 16 at most; each cell's note,
  instrument, volume, effect and XM's volume column as commands, S3M's effects as their XM twins, `Dxx` rewritten in
  hex), its samples as samples of their own, and sampler cartridges of up to 64 instruments naming them, an
  instrument for each module instrument or sample (its loop, volume, base note and tune from the sample's rate,
  finetune and relative note), a sampler for every 64. A module's samples past the 256th (the numbers there are) are
  left out, the page saying how many (`LEFT OUT: 12 × SAMPLES PAST THE 256TH`). Moved to the FM-1 panel with fewer
  free sample numbers than its samples, it is refused, saying how many it needs. **IT is not read;** nor
  are XM's envelopes, kept as the sampler's envelope at most. A pattern of more than 128 rows is split
  ([files/patterns.md](files/patterns.md#numbers)).
- **MIDI files** ([below](#midi-files)): a song and its patterns.
- **DX7 cartridges** (`.syx`, the 32-voice bulk dump): a DX7 cartridge, exactly
  ([files/instruments.md](files/instruments.md#dx7)); a single voice's dump: a cartridge of one.
- **Sounds** (WAV read by the page itself; FLAC, MP3, OGG and the rest as the browser decodes them): a sample, at
  the lowest rate that loses nothing heard (the highest frequency the sound holds, measured, under half the rate),
  8 bits, coded in PTR as the packer codes it ([files/ptr.md](files/ptr.md)). The TABLE keeps the file's own audio
  beside it, so a later cut, rate or depth starts from the original. A sound longer than a sample can be is kept as
  a loose sample `TOO LONG` ([below](#too-long-for-the-fm-1)), not refused.

### MIDI files

Formats 0 and 1, a first version:

- **Tracks:** each channel's notes become tracks, as many as the channel plays notes at once, 16 in all at most;
  what does not fit is counted.
- **Rows:** notes are put on the nearest row (ROWS A BEAT 4; TICKS A ROW 24 / ROWS A BEAT, so TEMPO is the
  file's BPM); velocity becomes the cell's volume, a note's end a note off (`===`, not the drums'). The sustain
  pedal holds notes' ends.
- **Tempo:** the first tempo becomes the song's TEMPO, later changes `Fxx`.
- **Patterns:** 4 bars of the file's time signature each; patterns alike are kept once.
- **Instruments:** each channel's first program onto a built-in instrument by a table of all 128 General MIDI
  programs (every PT/FM carries them: the grand piano on KEYS/ORGANS' GRAND PNO, the fingered bass on NICE BASS
  ...); channel 10's drums onto FM DRUM KIT, a sound for each drum note, played at C-4.
- **Left out and counted:** pitch bend, modulation and the other controllers.

## The preview

- **The firmware's own sound code:** the firmware compiled for the browser, as the emulator's, in an AudioWorklet
  at 44.1 kHz, fed with a flash image of what the item needs laid out as the FM-1's (the TABLE's items at numbers
  of the image's own). What is heard is what the FM-1 plays.
- **Its room is bigger than an FM-1's:** the preview's firmware (and the one drawing the pictures and measuring AUTO
  LEVEL) takes the whole flash from the firmware slot's start as its user area, about 1 MB against the FM-1's
  412 KB, so a TABLE item bigger than an FM-1 holds still plays; one bigger than that the preview refuses, saying
  so. Only the editor's build has this room; the virtual FM-1 and the device have the FM-1's.
- **▶ on every item that sounds,** Space on the picked one, and the footer's play key.
- **What plays,** then a second of silence, then again, until STOP:
  - a song from its start to its end;
  - a pattern once through, at the tempo of a song that plays it;
  - a sample once, at its own rate;
  - an instrument a melodic arpeggio: C-4 E-4 G-4 C-5 G-4 E-4 C-4 in eighths at 120 BPM, the last held a second;
  - a cartridge its instruments one after another, an arpeggio each;
  - a FUSION slot its pattern;
  - LOOPS all its loops together.

  A song whose AT THE END is LOOP is played once through too.
- **The mini player** in the footer: PLAY and STOP, what plays and from which panel, a progress bar (a click jumps
  there; a song's positions marked on it), the time, the voices sounding, VOL (the preview's level, −24 dB to +24 dB
  in steps of 1 dB, 0 dB the plain level in the middle; a limiter after it keeps it from clipping; kept in this
  browser); for a cartridge ◀ ▶ skip to the previous or next instrument. Starting another item replaces it.
  Everything else goes on working while it plays; WRITE stops the FM-1's sound, not the page's.
- **A tab plays what it edits,** before SAVE: its changes go in through the firmware's edit memory, as the FM-1's
  own edits before SAVE, so what plays goes on.
- **Playing notes by hand** belongs to the instrument tabs ([the tabs](#the-tabs)).

## The tabs

- **FILES is the first tab, always there.** Every item opens in a tab of its own (double-click, Enter, ✎, EDIT IN A
  TAB): its kind, number and name, and which panel it is on. An item has one tab; opening it again shows that tab.
- **A tab edits a copy.** SAVE puts the changes into the item, on its panel (on the FM-1 panel WRITE then sends
  them), and closes the tab; ✕ with changes not saved asks SAVE, DISCARD or KEEP EDITING. A tab whose item was
  deleted says so and cannot save; one whose item changed since it opened (another tab's SAVE, a rename) asks before
  SAVE replaces it. A built-in instrument's tab says that SAVE makes a changed copy. READ and WRITE wait for the FM-1
  panel's tabs with changes to be saved or closed.
- **Controls:** sliders, lists and fields, styled as the pages' (square, the engine's colour on a slider's filled
  part), every value in the words and units the FM-1 shows. A double-click on a slider puts its default back (a new
  instrument's, a new song's, a knob's, a setting's own); VOL's is 0 DB.
- **The pictures are the FM-1's own:** the firmware's drawing code, compiled into the page's WebAssembly, draws
  each one from the item's bytes exactly as on the FM-1's screen (the same pixels, palette and fonts, the DEFAULT
  theme), shown at a whole number of the display's pixels a screen pixel, and drawn again as a control moves.
  Each sits beside the controls it draws and stays in view while they scroll (above them on a narrow page): an
  instrument's parameters beside its EDIT picture and its details as INSTRUMENTS shows them; CHIP's in a section a
  page (TONE, VOLUME, TABLES, WAVE), TABLES showing the table last touched, as the FM-1's cursor picks it; an
  effect's knobs under FX EDIT's picture of it. Pattern cells read as the FM-1's grid shows them
  (`C-4 0B:05 7F A04`), in its colours.
- **SONG** (and the KEYS' SONG): NAME, TEMPO, TICKS A ROW, ROWS A BEAT, SWING, AT THE END; the ORDER (each position's
  pattern picked from a list, ✎ its pattern in a tab, play from here, move, remove, add); **TRIM** (tick the
  positions to keep; REMOVE WHAT IS NO LONGER USED, on at first, shows what goes and frees on SAVE); the MIXER (each
  track's LEVEL, PAN, DUCK and SENDS); the FX SETUP (ROUTING; the chain's order by dragging an effect before or after
  another, or by ◀ ▶ on each, by touch too; each effect on or off, its knobs, the FX screen's pictures).
- **PATTERN** (and the EDITOR'S PATTERN), made for the mouse and the finger:
  - the grid: tracks across, rows down, the beat's rows lighter, the playing row marked; FULL, NOTE + INSTRUMENT or
    NOTES; its size (tracks 1-16, rows 1-128);
  - a click or a tap picks a cell, a drag marks a block (on a touch screen with MARK BY DRAG on);
  - the cell's panel: a keyboard of the FM-1's 27 keys with the octave, NOTE OFF and NO NOTE; the instrument from a
    list of cartridges and their instruments by name; the volume; up to four commands, each from a list grouped as
    [files/commands.md](files/commands.md) groups them, its value and its meaning in words;
  - COPY, CUT, PASTE, CLEAR, TRANSPOSE ±1 and ±12, INSERT ROW, DELETE ROW, the EDIT STEP;
  - keys for a keyboard: the arrows, Page Up and Down, Home, End, Tab; the computer's keys write notes as the FM-1's
    keys play them (`Z S X D C F V …` from F3); `=` a note off; Delete clears; Ctrl+C, X, V; Space plays.
- **SAMPLE:** its waveform with a selection ([cutting](#cutting-a-sample-the-selection)): TRIM, TRIM SILENCE,
  WHOLE SAMPLE, SAVE AS A NEW SAMPLE, PLAY THE SELECTION, and the [slices](#slices); RATE (the proposed one
  marked); DEPTH (8 or 16 bits); the original's and the result's size;
  **PLAYED BY,** the instruments that name it, in every sampler on its panel. On the FM-1 panel there is no original:
  a cut is final once written.
  - **OPTIMIZE ▾** makes it take less room, the codec kept (PTR codes a smaller, smoother, quieter sound in fewer
    bits): three levels, each listed with its size and what it saves, put in the tab's draft (heard before SAVE,
    UNDO in the tab). LOSSLESS changes nothing heard: what no instrument plays cut off (before the first START, past
    the last END or LOOP END) and the silent tail (under −50 dB). SMALLER adds the proposed rate. SMALLEST adds 8
    bits, a softer top (a low-pass at a quarter of the rate) and, when instruments play it, the level 6 dB lower
    with their LEVEL 6 dB higher. Measured on module samples: a softer top −15 %, 6 dB lower −17 %, a rate halved
    −46 %. A sample with no original starts from its own audio, so SMALLEST on it again lowers it again.
  - **Its instruments follow a change on SAVE,** in every sampler naming it: their START, END and loop points move
    to where the same sound now is (a cut, another rate), and their LEVEL makes up a level OPTIMIZE lowered.
- **Cartridges:** the cartridge's name and its instruments in a list; an instrument's page: NAME, LEVEL (AUTO
  LEVEL as the FM-1 measures it), and its engine's parameters with their pictures, as the FM-1's EDIT pages
  ([ui/screens/instruments.md](ui/screens/instruments.md)): the DX7 (the voice, the LFO, the six operators
  and their envelopes), the Juno-106, the TB-303, CHIP (TONE, VOLUME, TABLES, WAVE), a FUSION slot (its pattern, its
  LEVEL), a sampler's instrument (its sample, any sample on its panel, START, END, the loop, BASE NOTE, TUNE,
  VOLUME, the envelope) and **THE SAMPLES IT USES** (each sample its instruments name, with NEW INSTRUMENT FOR IT,
  at the first empty slot). A sampler's tab has, on an instrument, **DELETE** (the instrument goes, its sample
  stays; one step of the tab's UNDO) and **REPLACE SAMPLE** (a pick of any sample on the panel: the instrument then
  plays it as a new one would, every slider back to a new instrument's, its name following when it was the old
  sample's), and on an empty slot **CREATE** (a pick of any sample on the panel: a new instrument there playing it
  whole, named after it). Every instrument page has a keyboard of the FM-1's 27 keys
  to play it by mouse, touch, the computer's keys, or a MIDI keyboard picked from the computer's inputs; the list
  offers the VIRTUAL MIDI KEYBOARD too, the computer's keys played anywhere on the tab but in a field (`Z S X D C …`
  from F3 as the FM-1's keys, `[` and `]` the octave), the keys pressed lit on the keyboard. The choice is the
  page's: the same in every instrument tab, the tab shown the one that plays, kept in this browser (a MIDI keyboard
  found again by its name when it comes back). MIDI is asked for when the list is first opened, one question at a
  time; a browser that allows it already is not asked.
- **Many instruments at once** in a cartridge's tab ([the proposal](proposals/202610311848-editor-instruments.md)
  sections 1-3):
  - **The list shows every slot** to the cartridge's size (32 for a DX7, 128 for a Juno-106 …). A click picks a
    slot; Shift-click the range to it; Ctrl-click adds or takes one away; ↑ and ↓ move the pick, Shift with them
    extends it; on a touch screen each tap adds or takes one away. **SELECT ALL** (Ctrl+A) picks every used slot;
    **SELECT NONE** (Esc) only the slot shown. The instrument's page shows the slot picked last.
  - **COPY** (Ctrl+C) puts the selected instruments in the page's clipboard, in slot order, no limit: each
    instrument's bytes and what it needs from elsewhere (a sample instrument's sample, a FUSION slot's pattern, a
    favourite's source cartridge), so a paste is the same whatever happens to the originals; it lasts while the page
    is open. **CUT** (Ctrl+X) is COPY, then DELETE; neither carries references.
  - **PASTE** (Ctrl+V) adds: into empty slots, in order, replacing nothing; not enough of them: nothing pasted, `5
    COPIED, 3 EMPTY SLOTS: NOTHING PASTED`. **REPLACE** (Ctrl+Shift+V) puts them in place from the first slot picked
    on, over what is there, every reference to those places then playing the new ones; the cartridge ending before
    the last: nothing replaced, `5 COPIED, 3 SLOTS FROM HERE: NOTHING REPLACED`; used instruments replaced ask first
    (`REPLACE 2 USED INSTRUMENTS? USED 12 TIMES`). **DELETE** (Delete, Backspace) empties the selected slots; used
    ones ask first (`2 INSTRUMENTS ARE USED 12 TIMES`, `DELETE ANYWAY?`: DELETE or KEEP), and what named them keeps
    naming those places. In a FAVORITES tab DELETE is REMOVE, asking nothing.
  - **Same type only:** a DX7 instrument into a DX7 cartridge and so on; else refused as the FM-1 refuses it,
    `COPIED FROM DX7: NOT HERE`. A sample instrument's sample comes along as an item of its own: on a panel holding
    the same sample (by hash: its whole record) the pasted instrument names it, nothing copied; otherwise the
    sample is added, on the FM-1 panel at a free number, and with none free the paste is refused (`NO FREE SAMPLE
    NUMBER`); a FUSION slot its pattern (the same one when
    still there and the same, else a copy); a favourite its source's cartridge when it is not there. On the FM-1
    panel what comes along needs numbers and room, refused as a move that does not fit; it is added at the tab's
    SAVE.
  - **Rearranging:** dragging an instrument onto an empty slot moves it there, onto a used one swaps the two; a
    selection dragged moves as a block onto slots empty or its own. **MOVE TO** does the same by touch (the
    selection floats, a tap puts it down). Alt+↑ and Alt+↓ move the selection a slot. **References follow places:**
    a move takes every reference naming the place it leaves; a move onto an empty slot is a swap with it, so the
    references still naming that slot go to the place left. What follows, on the cartridge's own panel: every cell,
    the loops, the favourites and the settings' current instrument; written with the tab's SAVE in the same step
    (`SAVED 0B · 14 PATTERNS, THE LOOPS AND 2 FAVORITES FOLLOW`). The SAVE is refused while a tab with changes not
    saved is open on an item it would rewrite (`SAVE OR CLOSE PATTERN 0A'S TAB FIRST`). Every move is a step of the
    tab's UNDO.
  - **A built-in cartridge** takes all of these as edits (its SAVE makes a changed copy under its number); it never
    moves to another number, its slots do.
- **A FAVORITES cartridge:** its name, and its slots in a list; each slot's source picked (a cartridge, then one of
  its instruments), slots removed, moved and swapped; the preview plays the source. On the FM-1 panel a favourite is
  listed as the FM-1 lists it (`07 0B:05 BRASS`, a missing one `MISSING` in grey); on the TABLE, which has no
  numbers, by its source's name and its source's cartridge's name.
- **SETTINGS** (every setting, the 16 GATE patterns of your own as rows of steps), **MIDI MAP** (each mapping's
  channel, message, number, FM-1 control and kind), **LOOPS** (the length, with loops or none; each loop's
  instrument, POLY or MONO, lanes, repeats and SHIFT, 0 to the length − 1, shown signed the nearer way round (`+3`,
  `-3`; the half way round is `+`, as the FM-1 shows it; the entry takes the shown form or the stored one, 0 to the
  length − 1, either sign, and shows it signed after); its notes in the pattern grid as stored, the hint saying how
  SHIFT plays them (`SHIFT +3: PLAYS 3 ROWS LATER`, `SHIFT -3: PLAYS 3 ROWS EARLIER`, `SHIFT 0: PLAYS AS STORED`); the
  length is shown, not edited (it is changed on the FM-1, where LENGTH keeps the notes as they are heard); the preview
  plays through the firmware, so it hears SHIFT; VIEW and K1-K4's jobs are the FM-1's own and not shown), **DJ SETUP**
  (the crossfader, the filter, the FX LFO, the decks, DJ's own FX setup).
- **Ranges and words** are the specs': [files/instruments.md](files/instruments.md),
  [files/commands.md](files/commands.md), [files/settings.md](files/settings.md),
  [files/midi.md](files/midi.md), [files/loops.md](files/loops.md), [files/dj.md](files/dj.md).

## What it is built from

- **A new page.** From the first editor's code it takes what fits and nothing of its layout:
  - every record both ways (`editor/js/records.js`, kept equal to `tools/ptfm_records.py` by the tests);
  - the converters (`conv_mod.js` with `ibxm_load.js`, `conv_midi.js`, `conv_syx.js`, `conv_sound.js`);
  - the seeding client and the uploader's planner (`fw/web/seed.js`, shared with the install page);
  - the page's part of the firmware's WebAssembly (`editor/wasm/`: the flash image, the edit memory, playing,
    AUTO LEVEL, the built-in instruments), with the pictures and the repeating preview added.
- **The firmware's pictures have entry points** the page's WebAssembly calls with an item's bytes and draws into
  an image of the screen: the instrument pages', the FX screen's, the waveform's. They are compiled only into the
  page's WebAssembly (`PTFM_EDITOR`): what the FM-1 draws, and the device's build, do not change. So is the
  preview's room (the store's `store_preview_room`, switched on only by the preview's and the pictures' instances).
- **The parts of the seeding protocol it leans on** are the firmware's: READ and DATA, and BEGIN refused as
  CHANGED unless its save number follows the layout's last ([files/blob.md](files/blob.md#seeding)).

## Recording and cutting sounds

The page records sound from any input the computer has (a microphone, a line in, the FM-1 itself over USB audio),
and every sample's tab cuts: select a part, trim to it, save it as a new sample, find the hits in a recording to
remove, keep apart or turn into a playable kit. Settled with
[the proposal](proposals/202610311701-editor-recorder.md); the firmware does not change.

**Two players:** **the preview** plays what the FM-1 would ([above](#the-preview)), up to its room; **the page's own
player** plays a sound as it is (a take, an original, a sample the preview cannot hold) through the browser at its
own rate and the page's VOL. Both show in the footer's player (STOP, the progress); starting one stops the other.

**Originals:** a sample made from a dropped sound or a take has its original, the sound as it came at its own rate
and depth, which RATE, DEPTH and OPTIMIZE start from. A sample without one (from a module, a disk, a backup, the FM-1
panel) starts from its own audio as the tab opened it; "the original" below means that audio for it.

**One channel, at once:** PT/FM's samples are one channel, so a stereo sound (a dropped file, a take from a stereo
input) is mixed to one as it comes, the two channels averaged, and its original is that mix: every sample on the
TABLE goes to the FM-1 as it is, its length aside ([TOO LONG](#too-long-for-the-fm-1)). An original the page kept stereo before
is made one channel at the next load, as its sample had it (the mix, or the left or right alone).

### The recorder

- **`● RECORD`** among the TABLE's keys opens the recorder, a dialog over the page; it stops the preview. The
  browser asks for the input when the dialog first opens (a page opened from a file asks again after every reload);
  refused, the dialog says so and records nothing.
- **INPUT:** the inputs the browser offers, by name (the FM-1 among them while connected: Windows names its USB
  audio `Line (FM-1 PT/FM)`). The last one picked is found again by its name. Locked while a take runs.
- **The meter:** the input's level, live, its peak held a second, and `CLIP` lit for a second after any sample
  reaches full scale.
- **RECORD / STOP** (Space too): a take, from RECORD to STOP, as long as it runs. While it runs its waveform grows
  as the sound comes in, the box 10 s wide (wider takes squeezed in as they pass it), its end a light red line.
- **The take:** its waveform across the box and its length, **▶** to hear it (the page's own player; a white line
  follows where it plays, gone when it stops; RECORD stops it), and **SAVE**. RECORD again replaces it, saved or not;
  there is no list. SAVE puts it on the TABLE as a loose sample, named by [the naming rule](#the-naming-rule) from
  `TAKE` (`TAKE00`, `TAKE01`, …), its source `RECORDING` and the time, its tab not opened, and closes the recorder
  (the status line names the sample; `● RECORD` opens it again for the next take). One step of FILES' UNDO.
- **A part of the take:** dragging across its waveform selects a part (a drag starting at the selection's end moves
  that end), its ground lit, its ends yellow, the rest dimmed; ▶ then plays the selection, and SAVE keeps only it
  (the part its original). **TRIM SILENCE** selects the sound within the selection, or within the take: from 5 ms
  before the first sound over the silence level to 5 ms after the last, as the SAMPLE tab's ([slices](#slices));
  with no silence at its ends, `NO SILENCE AT ITS ENDS: NOTHING TRIMMED`. **WHOLE TAKE** selects all of it again
  (dimmed with nothing selected). The length line adds what SAVE keeps: `THE TAKE: 0:12.3, STEREO MIXED TO MONO ·
  SAVE KEEPS 0:01.2 – 0:05.6 (0:04.4)` (a take from a stereo input says it was mixed: one channel,
  [above](#recording-and-cutting-sounds)). The
  take itself stays whole until SAVE; a new selection stops ▶.
- **Closing** (✕, Esc) stops a take that runs, then, with a take not saved, asks: SAVE, DISCARD or STAY. A reload
  with a take not saved asks first.
- **SAVE writes the take's original first;** when the browser cannot keep it, SAVE is refused, saying so, and the
  take stays in the dialog.
- **An input that goes away** stops the take, which is kept: `THE INPUT WENT AWAY`.
- **The best quality PT/FM holds:** the input at its own rate, the browser's voice processing (echo cancelling,
  noise suppression, automatic gain) off, a stereo input's take mixed to one channel at STOP (its waveform the mix
  while it runs too). Its sample: 16 bits, 44.1 kHz (the take converted by the page's own resampler); the take is its
  original.
- Nothing is heard through the page while recording; the meter shows the level. A take lives in the page's memory
  until SAVE.

### Cutting a sample: the selection

In the SAMPLE tab the waveform shows the sample as the draft has it, one channel as PT/FM's samples are, nothing
selected when the tab opens. Dragging
across it (or dragging the selection's START and END) selects a part and changes nothing by itself;
`▶ PLAY THE SELECTION` plays it through the preview (the page's own player when the preview cannot hold it), the
whole sample with nothing selected. The selection and the slices' markers stay on the same sound when RATE or
OPTIMIZE changes the frames.
- **TRIM:** the sample becomes the selection, in the tab's draft (UNDO in the tab, SAVE keeps it); dimmed with
  nothing selected. **TRIM SILENCE** selects the sound and trims to it: from 5 ms before the first sound over the
  silence level ([slices](#slices)) to 5 ms after the last. **WHOLE SAMPLE** selects all and, for a sample with an
  original, brings back the whole original. OPTIMIZE's LOSSLESS keeps its own −50 dB for the silent tail it cuts.
- **SAVE AS A NEW SAMPLE** (dimmed with nothing selected): the selection becomes a new sample, made from the draft's
  sound (a REMOVE not yet saved included) at its RATE and DEPTH, at the original's level, its original the
  same part of this one's, in the background (a Worker); the tab stays on the current sample. One step of FILES'
  UNDO; DISCARD of the tab keeps it. Its source is `FROM` and the sample's name (`FROM KICK`).
  - **Where:** on the same panel, a sample of its own, with no instrument (a sampler's CREATE or NEW INSTRUMENT FOR
    IT makes one); on the FM-1 panel at a free number, passing over the numbers references name. Refused, saying so:
    no free sample number (`ALL 256 SAMPLE NUMBERS USED`), no room on the FM-1 panel (as a move that does not fit).
- **The instruments that play the sample follow TRIM and REMOVE on SAVE** as they follow a cut: a point inside a
  removed part moves to its join, a loop left with no length turns OFF, and the status line names the instruments
  changed.

#### The naming rule

A new sample's name: the sample's name with any digits at its end read off, then two digits, the first number not
taken by a sample on that panel (`KICK` → `KICK00`, `KICK01`; `TAKE00` → `TAKE01`); a name too long for the digits
within a sample name's 10 characters gives up its last letters to them (`DRUMLOOP 1` → `DRUMLOOP00`); past 99, three
digits; a sample with no name counts as `SAMPLE`.

### SLICES

**DETECT SLICES** finds where the sounds in a sample start and cuts the selection (the whole sample when nothing is
selected) into **slices**, shown on the waveform as markers over numbered spans:
- **Detection:** the level measured in 5 ms windows. A slice starts 5 ms before the level jumps up (its attack kept
  whole) and ends where its level drops under the silence level and stays under it for 100 ms (the slice ends at
  the drop), or at the next start; what lies between is a **silent slice**, drawn dimmed, as is the part before the
  first start when it stays under the silence level. **The silence level** is 40 dB under the highest peak in what is
  cut (TRIM SILENCE uses the same). **SENSITIVITY** (1-10, 5 at first) sets the jump that starts a slice: 24 dB at
  1, 3 dB at 10, evenly between; starts closer than 30 ms count as one. Moving it detects again, the markers placed
  by hand given up (UNDO in the tab). With markers placed or moved by hand, each slice's end and the silent slices
  are found again from the starts the same way.
- **By hand:** a marker dragged moves a slice's start (a drag that starts on a marker moves the marker, else it
  selects); a click on a span picks the slice (Ctrl and Shift for several; on a touch screen each tap adds or removes
  a slice from those picked); a double-click adds a marker; **ADD A SLICE** (a marker at the selection's start) and
  **DELETE THE MARKER** (the picked slice's start; Delete too) do the same by touch. Each slice has its ▶ (through
  the preview). Markers added, moved or deleted are steps of the tab's UNDO; the tab's ✕ does not ask for them alone.
- **REMOVE SILENCE:** the silent slices taken out, the rest joined end to start, each join with a 5 ms crossfade.
  **REMOVE:** the picked slices taken out, joined the same way. Both remake the sound from the original's own frames,
  its rate kept, and the joined sound becomes the sample's new original (in the tab's draft; SAVE
  writes it as a new original), so WHOLE SAMPLE brings back the joined sound.
- **SAVE AS SAMPLES:** each picked slice (all but the silent ones when none is picked) becomes a new sample, as SAVE
  AS A NEW SAMPLE makes one; when not all fit (the free sample numbers, the FM-1 panel's room, a slice longer than a
  sample can be: `SLICE 03 IS TOO LONG`), none is saved, saying so.
- **MAKE A KIT:** the picked slices (all but the silent ones when none is picked) become a playable kit on the same
  panel, one step of FILES' UNDO, its items' source `KIT FROM` and the sample's name; more than 64 refuse it
  (`64 SLICES AT MOST IN A KIT`), as a slice too long does:
  - **the slices' samples,** each a sample of its own named by the naming rule (on the FM-1 panel at free numbers,
    in slice order), and **a sampler cartridge** named after the sample, slice 1's instrument in its slot 00, slice 2's
    in 01, and so on, each naming its slice's sample, named as it, playing it whole: START 0, END its length, LOOP
    OFF, BASE NOTE C-4, LEVEL 0 dB, a new instrument's envelope;
  - **a FUSION cartridge** named after the sample cut to 8 characters, then ` KIT`; its slot 00 the kit, named as the
    sample, LEVEL 0 dB: a FUSION pattern (on the TABLE named as the FUSION cartridge) of 128 rows and one track, EDIT
    STEP 0; row `3C` + *n* − 1 holds slice *n*'s instrument at note C-4 with an R4 parameter command of 0, so each
    hit rings out after its key is let go; slice 1 sounds on C-4, slice 2 on C#4, and up, every slice at its own
    pitch;
  - both cartridges opened in their tabs. On the FM-1 panel it needs two cartridge numbers, a pattern number
    (`00`-`EF`), a sample number a slice and room, and is refused as a move that does not fit is.
- **The slices are the tab's alone:** not stored with the sample; TRIM and TRIM SILENCE keep the markers inside the
  part kept (a slice across its end cut there, the slices picked let go), and
  SAVE keeps what REMOVE and TRIM did to the sound, not the markers.

### Too long for the FM-1

A sample's length is 20 bits ([files/instruments.md](files/instruments.md#samples)): at most
2^20 − 1 frames, about 23.8 s at 44.1 kHz. **Only a loose sample on the TABLE can be longer:** a take, a dropped
sound, a new sample saved from a loose one that is (SAVE AS A NEW SAMPLE, SAVE AS SAMPLES), or a loose one its tab
makes longer (WHOLE SAMPLE, a higher RATE). So a long recording is kept whole, to be cut into the pieces that go to
the FM-1. It holds its original and no record; its size reads `TOO LONG`, its hash is its original's, its icon is
drawn from its original, and it plays through the page's own player. Its tab says how long it is and what makes it
fit. No instrument may name it, nor may it go to the FM-1 panel, a move refusing it with everything picked with it (`TOO LONG
FOR THE FM-1: 31.2 S, 23.8 S AT MOST` at its rate; among others `TAKE00 IS TOO LONG …`), until TRIM, REMOVE, REMOVE
SILENCE, a lower RATE or OPTIMIZE makes it fit (an OPTIMIZE level that makes it fit counts as smaller). MAKE A KIT
refuses a slice too long. A change that would make a sample an instrument names too long is refused. A sample the
preview cannot hold (a long one at 16 bits passes its room near 20 s) plays through the page's own player too.

## Kept in the browser

- **Both panels survive a reload** (IndexedDB): their records, and the page's own data beside them (sources, the
  TABLE's names, remembered numbers), the views and the folded groups.
- **Originals are kept in a store of their own** (a sound's own audio, from a drop or a take): each under an id of
  its own, written once; an item names its original, a SAVE that remakes it (REMOVE) writes a new one, and one is
  dropped when no item names it (an UNDO that brings its item back writes it again). So a long one is not written again at every change of the page.
  An original the browser lost leaves its sample as its record has it; a sample too long, which has no record, goes
  with it.
- **The tabs do not:** a reload, or a closed page, with a tab's changes not saved asks first. The FM-1 panel's
  changes not written are kept, and still marked.
- **One page at a time:** the page opened again in the same browser, while it is open elsewhere, says so and does
  nothing until the other one is closed, so two pages never write the same panels.

## Tests

- **Node** (`tests/editor/`): the panels' rules (moves with what they use; what is reused; numbers kept and given;
  every reference rewritten; loose samples into a sampler; the system items replaced; a built-in copied to the TABLE
  and back beside it, a changed one a song takes along keeping its number; DOWNLOAD AS DISK's disk, its refusals, its
  file's name, put back by UPLOAD DISK and dropped on the TABLE; DELETE with what uses it, to
  the end, ALSO WHAT ONLY IT USES; DELETE ALL; the space counted as the planner places; SET NUMBER, moved, swapped and
  refused, every hash and every use the same after it; UNDO), the records against
  `tools/ptfm_records.py`, disks both ways, the converters against upstream IBXM's loading, OPTIMIZE's levels and
  OPTIMIZE SAMPLES on the demo disk's samplers (each sample as its tab makes it).
- **READ and WRITE against the virtual FM-1:** the firmware's WebAssembly seeded through function calls instead of
  MIDI: READ back, only the changed records read again, the plan, CHANGED, `SAVE OR REVERT FIRST`, a version the
  FM-1 does not take, a panel that fits only the FM-1 emptied written as the whole user area anew (what the page
  cannot read written back as it was), a backup put back exactly (`FE` included), SET NUMBER's swap of two kinds written
  and read back, nothing named as removed. The page itself takes the virtual FM-1 for the FM-1
  when opened with `?virtual` after its file's name.
- **The previews:** the flash image of each kind played by the firmware; the repeat with its second of silence.
- **The pictures:** each equals the firmware's own screen pixels for the same bytes.
- **Recording and cutting** ([above](#recording-and-cutting-sounds)): in Node, the naming rule (digits read off, past
  10 characters and 99, no name), TRIM SILENCE's level, DETECT SLICES on a sample of known hits (silent slices,
  SENSITIVITY), REMOVE SILENCE's joins and the instruments following, MAKE A KIT's two cartridges, `TOO LONG` (a take,
  a drop and new samples kept so, refused by moves and kits, OPTIMIZE and TRIM making it fit, a lost original); in the
  headless Chrome with a fake input, a take saved, replaced, discarded, the input gone, SAVE AS A NEW SAMPLE (a full
  sampler, a sampler's open tab), SAVE AS SAMPLES, MAKE A KIT played by the preview, `TOO LONG` refused until it fits.
- **The page:** scenarios in a headless Chrome, a command of their own (`tests/editor/run_page.sh`, the Node tests
  needing no browser): drops, moves (a built-in's copy, by the key and dragged), DOWNLOAD AS DISK (the file the page
  offers caught), DELETE and its dialogs, every tab edited and saved, the pictures beside their
  sliders, the keyboard's input, + NEW, OPTIMIZE and OPTIMIZE SAMPLES, READ and WRITE on the virtual FM-1 with BACKUP, a version converted down and the whole user area written anew,
  a phone's width; each passes or fails, an exception on the page fails it; screenshots looked at. Run before a build
  is handed over.
