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
    REDO. A click on `PT/FM EDITOR` shows FILES too.
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
  what came from the FM-1 panel, `BACKUP` and the time for what a WRITE backed up, `NEW` for what + NEW made; and the
  number it had on the FM-1, if any.
- **Names:** an item keeps its own; one with none gets a name made from its source on the TABLE (`MYSONG P07`,
  `SMP 0C`). Patterns have no names on the FM-1: such a name lives only on the TABLE.
- **`+ NEW`** makes an item here, as the FM-1 makes a new one, and opens it in its tab: a SONG (with an empty
  pattern in its one position), a PATTERN (8 tracks of 64 rows, empty), a cartridge of each engine (one plain
  instrument), a SAMPLER (empty: sounds dropped on it fill it), SETTINGS, MIDI MAP, LOOPS or DJ SETUP (their
  defaults). Their source is `NEW`.
- **Its keys:** `+ ADD FILES`, `+ NEW`, `DELETE ALL`, the filter and the views.

### FM-1

- **An image of an FM-1's user area,** kept in this browser too: what the last READ found, a disk put in by
  UPLOAD DISK, or a set built by hand from the TABLE with no FM-1 connected (then saved by DOWNLOAD DISK, to be
  put on an FM-1 later). Nothing reaches the device but by WRITE ([the FM-1](#the-fm-1-read-write-and-disks)).
- **Its header says where its contents come from** (`READ 26 OCT 14:02 · PT/FM 0.26`, `DISK BACKUP.PTFMDSK`,
  `EMPTY`) and how many changes are not written yet; each such item is marked with a dot.
- **The built-in instruments** are listed among the cartridges, marked `BUILT-IN`, their size `IN FW`; a changed
  one is marked `CHANGED` and counts its record's size.
- **The system items still at their defaults** are listed too, dark grey, marked `DEFAULT`: nothing to move or
  delete; a tab opened on one starts from the firmware's own default, and its SAVE makes the record.
- **What the page cannot read** (a record of a version or a kind it does not know, read from the FM-1 or on a disk put
  in by UPLOAD DISK) is listed dim and kept as it is: it never moves, and WRITE neither writes nor removes it.
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
    panel shows where it plays, `CITY LIGHTS · POS 02`), its kind (`4 TRACKS × 64`, `SAMPLER · 10 INSTR · 7 SMP`,
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
  Enter opens the item's tab, Space plays it, Delete deletes, F2 renames. The right-click menu: PLAY, EDIT IN A
  TAB, RENAME, the move to the other side, SELECT WHAT IT USES, SELECT WHAT USES IT, on the TABLE OPTIMIZE n
  SAMPLES… (below), DELETE.
- **OPTIMIZE SAMPLES** (the TABLE's menu): the samples picked, and every sample of a picked sampler, made smaller
  at one level, each as its tab's OPTIMIZE makes it (SAMPLE, below), its instruments following. A dialog lists the
  three levels with the size before and after and how many come out smaller (SMALLER picked at first, the last
  choice kept while the page is open); a sample a level would not make smaller stays as it is; OPTIMIZE applies
  it as one step (UNDO).
- **The selection bar** under each panel: how many are picked, their size, and their size with what they use; for
  one item, what it uses and what uses it, each a link to it; PLAY, EDIT, DELETE and the move key.
- **Sizes:** an item's size is its records' bytes as the FM-1 stores them (each with its 16-byte header, to the
  next multiple of 4); *with what it uses* adds everything it uses, the built-in instruments not counted.

## Items, references, hashes

- **Items are PT/FM's objects,** each made of all its records ([files/store.md](files/store.md#a-record)): a
  song; a pattern with all its parts; a cartridge of each kind; a sample (its audio record); the system items.
- **Samples on the FM-1 panel are always in a sampler cartridge,** their audio filed under its number. On the
  TABLE a sample may stand alone (a dropped sound). A sampler cartridge may hold samples no instrument plays
  ([files/instruments.md](files/instruments.md#samples-and-fusion-slots)).
- **References** ([files/references.md](files/references.md)): a song's positions name patterns, a cell its
  instrument's cartridge, a FUSION slot its pattern, a sample instrument its sample, a loop its instrument; the
  EDITOR's pattern names cartridges as any pattern does.
- **Remembered places are not uses:** the settings' current instrument and the EDITOR's numbered pattern, DJ's decks'
  songs. They never take an item along on a move nor keep it from a DELETE; moved without what they name, they find
  it on the other side when it is there (by its hash), else they are cleared, as the FM-1 empties a deck whose song is
  gone.
- **Every item has a content hash,** made from its payload with each reference replaced by the hash of what it
  refers to: equal content at other numbers hashes equal, and a song's hash covers its patterns', theirs their
  cartridges'. A sampler cartridge's covers its samples' (their audio). A built-in instrument's hash is its number.
  Items are told apart by hash, never by number; a copy meant to be edited apart is made on the FM-1 (CLONE) or
  differs once edited.

## Moving items

- **→ copies the TABLE's picked items to the FM-1 panel, ← the FM-1 panel's to the TABLE;** dragging them onto the
  other panel does the same. The side they come from keeps them.
- **With everything they use:** a song travels with its patterns, their cartridges, a sampler's samples, a
  FUSION's patterns. The built-in instruments never travel: a reference to one stays a reference to it, as every
  PT/FM carries them. A changed built-in travels as a cartridge that remembers its number; on the FM-1 panel it
  lands on that number when that number holds the firmware's own or the same change, else at a free number.
  On the FM-1 panel a number names what the panel holds there, as on the FM-1: once a changed built-in lands
  on its number (a move, a SAVE), everything that names that number uses it, and the tabs' instrument lists
  offer it in the firmware's place.
- **What the other side already holds is used, not copied:** an item whose hash the other side holds is that
  item there. A backup's song moved back finds its cartridges and samples already on the FM-1 panel. Equal items
  within one move (one song from two disks) are one item there.
- **Numbers on the FM-1 panel:** an item takes the number it had (from a disk or an earlier READ) when it is free,
  else the lowest free one, `00`-`EF` ([files/patterns.md](files/patterns.md#numbers); the six cartridge
  kinds share one number space). Every reference to it is written anew. A sample takes its sampler's number.
- **Loose samples:** dropped on a sampler cartridge of the FM-1 panel, they join it at its next free sample
  places (64 at most); moved any other way, one new sampler cartridge holds them all. No instrument is made for
  them: instruments are made on the FM-1 or in the cartridge's tab.
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
- **A sampler cartridge goes with its samples.** A sample an instrument plays takes its sampler (which uses it), and
  so what plays the sampler; one no instrument plays can go alone.
- **The built-in instruments** have nothing to delete; deleting a changed one brings the firmware's own back (BACK
  TO BUILT-IN). Nothing that uses it goes with it: what uses it plays the firmware's own then; and ALSO WHAT ONLY IT
  USES never takes one back by itself.
- **DELETE ALL** empties a panel, after asking. On the FM-1 panel the built-in instruments stay (changed ones back
  to the firmware's), and the system items go back to their defaults: a WRITE then empties the FM-1's user area.
- **Nothing changes on the FM-1 until WRITE.**

## UNDO

- **One history for FILES,** 100 steps: moves, deletes, renames, imports, a tab's SAVE, READ, UPLOAD DISK, REVERT,
  DELETE ALL. Ctrl+Z and Ctrl+Y, or the header's keys. A tab has its own until SAVE.
- **WRITE is not taken back** (the FM-1 has changed): UNDO and REDO bring back what the panels showed, never what the
  FM-1 is known to hold (what the last READ or WRITE found there). The history is the page's memory and goes with a
  reload.

## The FM-1: READ, WRITE and disks

- **No connection is kept.** READ and WRITE each find the FM-1 among the computer's MIDI ports by its name, as the
  install page does, talk the seeding protocol ([files/blob.md](files/blob.md#seeding)) for as long as they
  run, and let it go. An FM-1 not running PT/FM (another firmware, the USB rescue) is named in the status line,
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
  CRC stay where they are, the rest go into free blocks, to the last one if need be (no reserve kept). Before
  writing it shows:
  - fewer free blocks after it than advised, what that costs the FM-1's own saves (as the space bar says it), with
    WRITE ANYWAY and CANCEL;
  - anything saved on the FM-1 after the panel's last READ or WRITE (its save number is newer), at the top, with
    READ FIRST, WRITE ANYWAY and CANCEL;
  - what it writes, and the FM-1's items it removes, by name, red;
  - that it backs up first, when the write replaces or removes anything;
  - what it converts down: an object of a version the FM-1 does not take, where the FM-1 takes an older version of
    its kind that the page converts to, is written as that version (`LOOPS 00: VERSION 4 WRITTEN AS VERSION 3`),
    with what the older version cannot hold (`NOT KEPT: SOLO`) and then WRITE ANYWAY and CANCEL. The panel keeps its
    own version; a READ after brings the FM-1's back converted up
    ([files/disk.md](files/disk.md#versions)).

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
- **The EDITOR's own pattern and the keys' song (`FE`)** are items like the others here: a backup keeps them and a
  backup put back restores them. A WRITE makes the FM-1 hold exactly the panel, so a panel without them removes
  the FM-1's, listed as removed before anything is written.

## Drops and conversions

Files dropped on the TABLE, or picked with `+ ADD FILES`, become its items, all of each file. The status line says
what each made and what could not be kept. Taking only a part (some of a module's patterns, a cut of a sound) is
done afterwards, in the item's tab ([the tabs](#the-tabs)).

- **Disks** (`.ptfmdsk`; any file whose first bytes are `PTFMDISK`, an older `.ptd` too): their items as they are,
  each remembering its number. Putting a disk on the FM-1 as it is goes through the FM-1 panel's UPLOAD DISK.
  Only what the FM-1 can hold is taken from a disk.
- **Tracker modules: MOD, S3M and XM,** read as IBXM reads them (the reference the conversion is tested against,
  [files/commands.md](files/commands.md#tracker-effects)): the song (tempo, ticks, the order, its restart, the
  global volume, the pans, the slides' kind), its patterns (a channel a track, 16 at most; each cell's note,
  instrument, volume, effect and XM's volume column as commands, S3M's effects as their XM twins, `Dxx` rewritten in
  hex), a sampler cartridge with an instrument for each module instrument or sample (its loop, volume, base note
  and tune from the sample's rate, finetune and relative note) and every sample's audio. **IT is not read;** nor
  are XM's envelopes, kept as the sampler's envelope at most. A pattern of more than 128 rows is split
  ([files/patterns.md](files/patterns.md#numbers)).
- **MIDI files** ([below](#midi-files)): a song and its patterns.
- **DX7 cartridges** (`.syx`, the 32-voice bulk dump): a DX7 cartridge, exactly
  ([files/instruments.md](files/instruments.md#dx7)); a single voice's dump: a cartridge of one.
- **Sounds** (WAV read by the page itself; FLAC, MP3, OGG and the rest as the browser decodes them): a sample, at
  the lowest rate that loses nothing heard (the highest frequency the sound holds, measured, under half the rate),
  8 bits, coded in PTR as the packer codes it ([files/ptr.md](files/ptr.md)). The TABLE keeps the file's own audio
  beside it, so a later cut, rate or depth starts from the original.

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
- **SAMPLE:** its waveform with START and END dragged (or a drag across it for a new cut); PLAY THE CUT; CHANNELS
  (mixed, left or right, for a stereo source); RATE (the proposed one marked); DEPTH (8 or 16 bits); TRIM SILENCE;
  WHOLE SAMPLE; the original's and the result's size; the instruments that play it. On the FM-1 panel there is no
  original: a cut is final once written.
  - **OPTIMIZE ▾** makes it take less room, the codec kept (PTR codes a smaller, smoother, quieter sound in fewer
    bits): three levels, each listed with its size and what it saves, put in the tab's draft (heard before SAVE,
    UNDO in the tab). LOSSLESS changes nothing heard: what no instrument plays cut off (before the first START, past
    the last END or LOOP END) and the silent tail (under −50 dB). SMALLER adds the proposed rate. SMALLEST adds 8
    bits, a softer top (a low-pass at a quarter of the rate) and, when instruments play it, the level 6 dB lower
    with their LEVEL 6 dB higher. Measured on module samples: a softer top −15 %, 6 dB lower −17 %, a rate halved
    −46 %. A sample with no original starts from its own audio, so SMALLEST on it again lowers it again.
  - **Its instruments follow a change on SAVE:** their START, END and loop points move to where the same sound now
    is (a cut, another rate), and their LEVEL makes up a level OPTIMIZE lowered.
- **Cartridges:** the cartridge's name and its instruments in a list; an instrument's page: NAME, LEVEL (AUTO
  LEVEL as the FM-1 measures it), and its engine's parameters with their pictures, as the FM-1's EDIT pages
  ([ui/screens/instruments.md](ui/screens/instruments.md)): the DX7 (the voice, the LFO, the six operators
  and their envelopes), the Juno-106, the TB-303, CHIP (TONE, VOLUME, TABLES, WAVE), a FUSION slot (its pattern, its
  LEVEL), a sampler's instrument (its sample, START, END, the loop, BASE NOTE, TUNE, VOLUME, the envelope) and the
  sampler's samples (each with NEW INSTRUMENT FOR IT). Every instrument page has a keyboard of the FM-1's 27 keys
  to play it by mouse, touch, the computer's keys, or a MIDI keyboard picked from the computer's inputs; the list
  offers the VIRTUAL MIDI KEYBOARD too, the computer's keys played anywhere on the tab but in a field (`Z S X D C …`
  from F3 as the FM-1's keys, `[` and `]` the octave), the keys pressed lit on the keyboard. The choice is the
  page's: the same in every instrument tab, the tab shown the one that plays, kept in this browser (a MIDI keyboard
  found again by its name when it comes back). MIDI is asked for when the list is first opened, one question at a
  time; a browser that allows it already is not asked.
- **SETTINGS** (every setting, the 16 GATE patterns of your own as rows of steps), **MIDI MAP** (each mapping's
  channel, message, number, FM-1 control and kind), **LOOPS** (each loop's instrument, POLY or MONO, lanes and
  repeats; its notes in the pattern grid), **DJ SETUP** (the crossfader, the filter, the FX LFO, the decks,
  DJ's own FX setup).
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

## Kept in the browser

- **Both panels survive a reload** (IndexedDB): their records, and the page's own data beside them (sources, the
  TABLE's names, remembered numbers, sounds' original audio), the views and the folded groups.
- **The tabs do not:** a reload, or a closed page, with a tab's changes not saved asks first. The FM-1 panel's
  changes not written are kept, and still marked.
- **One page at a time:** the page opened again in the same browser, while it is open elsewhere, says so and does
  nothing until the other one is closed, so two pages never write the same panels.

## Tests

- **Node** (`tests/editor/`): the panels' rules (moves with what they use; what is reused; numbers kept and given;
  every reference rewritten; loose samples into a sampler; the system items replaced; DELETE with what uses it, to
  the end, ALSO WHAT ONLY IT USES; DELETE ALL; the space counted as the planner places; UNDO), the records against
  `tools/ptfm_records.py`, disks both ways, the converters against upstream IBXM's loading, OPTIMIZE's levels and
  OPTIMIZE SAMPLES on the demo disk's samplers (each sample as its tab makes it).
- **READ and WRITE against the virtual FM-1:** the firmware's WebAssembly seeded through function calls instead of
  MIDI: READ back, only the changed records read again, the plan, CHANGED, `SAVE OR REVERT FIRST`, a version the
  FM-1 does not take, a backup put back exactly (`FE` included). The page itself takes the virtual FM-1 for the FM-1
  when opened with `?virtual` after its file's name.
- **The previews:** the flash image of each kind played by the firmware; the repeat with its second of silence.
- **The pictures:** each equals the firmware's own screen pixels for the same bytes.
- **The page:** scenarios in a headless Chrome, a command of their own (`tests/editor/run_page.sh`, the Node tests
  needing no browser): drops, moves, DELETE and its dialogs, every tab edited and saved, the pictures beside their
  sliders, the keyboard's input, + NEW, OPTIMIZE and OPTIMIZE SAMPLES, READ and WRITE on the virtual FM-1 with BACKUP and a version converted down,
  a phone's width; each passes or fails, an exception on the page fails it; screenshots looked at. Run before a build
  is handed over.
