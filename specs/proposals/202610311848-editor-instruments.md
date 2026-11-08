# Proposal: the EDITOR page's instruments (select, copy, paste, replace, delete, rearrange), and deleting built-in cartridges

Status: **settled** (2026-10-31); its specs are written when the work on it starts, and lead from then on.

**A cartridge's tab on the EDITOR page works on many instruments at once:** several are selected, copied into
another cartridge of the same type, put in place of others, deleted, or rearranged, with what plays them
following. Today a cartridge tab picks one instrument at a time, lists slots only up to the last used one, and moves
nothing ([../editor.md](../editor.md#the-tabs), Cartridges). **And a built-in cartridge can be deleted,** on the
FM-1 and on the page, so the cartridges not wanted leave the lists (section 4): that part is the firmware's too.

## 1. Selecting

- **The list shows every slot** of the cartridge, to its size (32 for a DX7, 128 for a Juno-106 …), so any empty
  slot can be a target.
- **Picking:** a click picks a slot; Shift-click picks the range to it; Ctrl-click adds or takes away one; ↑ and ↓
  move the pick and Shift with them extends it, as FILES' list does. On a touch screen each tap adds or takes away
  one. **SELECT ALL** (Ctrl+A) picks every used slot; **SELECT NONE** (Esc) picks only the slot shown.
- **The instrument's page** shows the slot picked last, and its edits change that slot alone.
- Empty slots can be picked (a place to move to); the actions below skip them where an instrument is needed.

## 2. COPY, CUT, PASTE, REPLACE, DELETE

Keys in the tab, and Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+Shift+V (REPLACE) and Delete or Backspace:

- **COPY:** the selected instruments into the page's clipboard, in slot order, as many as are picked (no limit; the
  FM-1 copies 4 at most). **The clipboard holds what they are, not where they were:** each instrument's bytes, and
  with it what it needs from elsewhere (a sample instrument's sample, a FUSION slot's pattern, a favourite's source
  cartridge), so a paste is the same whatever happens to the originals meanwhile. It lasts while the page is open
  (one page is open at a time, [../editor.md](../editor.md#kept-in-the-browser)).
- **CUT:** COPY, then DELETE. CUT and PASTE carry no references: to move an instrument and keep what plays it, drag
  it (section 3).
- **PASTE adds:** the instruments go into empty slots, those no reference names first, then the others, in order,
  replacing nothing. When there are not enough empty slots for all of them, nothing is pasted:
  `5 COPIED, 3 EMPTY SLOTS: NOTHING PASTED`. When it has to use slots that references still name (section 4),
  those references play the pasted instruments, and the status line says so (`PASTED 5 · 12 GREY REFERENCES
  PLAY AGAIN`).
- **REPLACE** puts them in place, from the first slot picked on, over what is there: the instruments there are gone,
  and every reference to those places now plays the new ones (that is what REPLACE is for). When the cartridge ends
  before the last one, nothing is replaced: `5 COPIED, 3 SLOTS FROM HERE: NOTHING REPLACED`. Over an empty slot
  that references still name, they play the new instrument, the status line saying so as PASTE's does. Replacing instruments
  that are used asks first (`REPLACE 2 USED INSTRUMENTS? USED 12 TIMES`), as a system item moved onto the FM-1
  panel asks before replacing ([../editor.md](../editor.md#moving-items)).
- **DELETE** empties the selected slots, and replaces the tab's EMPTY THIS PLACE. **Deleting never rewrites a
  reference** (section 4, A reference to nothing): instruments that are used ask first, `2 INSTRUMENTS ARE USED 12
  TIMES`, `DELETE ANYWAY?`; DELETE deletes them, and the cells, loops and favourites that named them keep naming
  those places, grey and silent; KEEP, or closing the dialog (Esc, as the page's dialogs close), leaves everything
  as it was. In a FAVORITES tab DELETE is the FM-1's REMOVE:
  nothing to ask.
- **"Used"** counts what the FM-1's question counts: cells, loops and favourites (the firmware's count gains
  the loops and favourites, section 5).
- **Same type only:** a DX7 instrument into a DX7 cartridge, a Juno-106 into a Juno-106, and so on; anything else is
  refused as the FM-1 refuses it, `COPIED FROM DX7: NOT HERE` (`COPIED FROM FAVORITES: NOT HERE` the other way,
  [202610311523-favorites.md](202610311523-favorites.md)). Any tab, on either panel.
- **What comes along:**
  - **a sample instrument** brings its sample into the target sampler, at its first free sample, or uses one
    already there when it is the same sound (by hash); with no free sample for it the paste is refused (nothing
    pasted, `SAMPLER 0B IS FULL: 64 SAMPLES`, the sampler's name on the TABLE). A replaced or deleted instrument's
    sample stays, as deleting an instrument never deletes its sample
    ([../files/instruments.md](../files/instruments.md#deleting));
  - **a FUSION slot** brings its pattern: on the same panel, the same pattern when it is still there and the same
    (by hash), else a copy of it; on the other panel, as a move brings one;
  - **a FAVORITES slot** is a reference: on the same panel it names the same source when that cartridge is still
    there and the same (by hash), else a copy of the source's cartridge comes along; on the other panel its
    source's cartridge comes along, as moving a FAVORITES cartridge takes it.
  - **On the FM-1 panel** what comes along needs numbers and room, and a paste that does not fit is refused as a
    move that does not fit is ([../editor.md](../editor.md#moving-items)).
  - The samples and patterns that come along are added at the tab's SAVE (DISCARD adds nothing), and the preview
    plays the pasted instruments with them before.
- **A built-in cartridge** takes PASTE, REPLACE and DELETE as edits: its tab's SAVE makes a changed copy under its
  number, as any edit of it does.

## 3. Rearranging

- **Dragging** an instrument onto an empty slot moves it there; onto a used slot, the two swap places. A selection
  dragged moves as a block onto slots that are empty or its own (else nothing moves). **MOVE TO**, a key, does the
  same by touch: the selection floats, a tap on a slot puts it there, as the FM-1's MOVE floats a slot.
- **↑ and ↓ keys** (Alt+↑, Alt+↓) move the selection a slot up or down: each selected slot steps over its neighbour,
  which goes to the other side; gaps in the selection stay as they are.
- **References follow places:** a move takes every reference naming the place it leaves, whatever that place holds
  at the time (after a REPLACE, PASTE or DELETE in the same draft too); nothing else moves references. **A move onto
  an empty slot is a swap with it:** the references that still name that slot (grey, section 4) go to the place
  left, so they stay grey and nothing comes back by accident. The draft keeps that map of places, and the tab's
  UNDO steps it back with the moves.
- **What follows,** on the cartridge's own panel: every cell (a cell naming only a slot, under its track's last
  cartridge, included), the loops, the favourites and the settings' current instrument at the last SAVE
  ([../files/settings.md](../files/settings.md)), so everything sounds as before. The other panel's items are
  items of their own and do not follow. **Records the page cannot read** (listed dim,
  [../editor.md](../editor.md#fm-1)) cannot follow: when the panel holds a pattern, song, loops or FAVORITES
  record of a version the page does not know, the SAVE's status line says so (`2 RECORDS THE PAGE CANNOT READ DO
  NOT FOLLOW`).
- **Written with the tab's SAVE,** in the same step (one step of FILES' UNDO takes back both,
  [../editor.md](../editor.md#undo)), the status line saying what followed (`SAVED 0B · 14 PATTERNS, THE LOOPS AND
  2 FAVORITES FOLLOW`); on the FM-1 panel WRITE then sends every record changed. Those items' hashes change with
  them, as any edit's do. **The SAVE is refused** while a tab with changes not saved is open on an item it would
  rewrite (`SAVE OR CLOSE PATTERN 0A'S TAB FIRST`), so that tab's SAVE never puts the old places back.
- **The preview plays the draft's places:** a song previewed while instruments are rearranged plays them where they
  have gone, as it will after SAVE.
- **To put another instrument in a place the songs play,** REPLACE it (section 2): that is the way to change what a
  place plays.
- **A built-in cartridge rearranges as any:** its edits are a changed copy in the user area under its number
  ([../files/instruments.md](../files/instruments.md#built-in-instruments)), so its slots move, swap, are replaced
  and deleted as any cartridge's, the references on its panel following; the cartridge itself keeps its number (a
  built-in never moves to another). BACK TO BUILT-IN, or DELETE and then RESTORE, puts the firmware's slots back
  in the firmware's order: what followed a move names the place, so it plays the firmware's instrument there, as
  any change of a built-in dropped leaves what uses it playing the firmware's own. FAVORITES cartridges, `FB`
  included, rearrange freely: nothing refers to a favourite, so there is nothing to rewrite.
- Every move is a step of the tab's UNDO.

## 4. Deleting, and a reference to nothing

### A reference to nothing, for every cartridge

On the FM-1 and the page alike ([../ui/saving.md](../ui/saving.md), [../files/references.md](../files/references.md)
and [../files/patterns.md](../files/patterns.md) change with it):
- **DELETE never rewrites references,** of an instrument, a slot range, a cartridge (built-in or not) or a
  pattern. When something uses what goes it asks `… IS USED 12 TIMES`, `DELETE ANYWAY?`, answered in its own words
  as questions are ([../ui/dialogs.md](../ui/dialogs.md#questions)): DELETE or KEEP (HOME as KEEP), in place of
  `UPDATE REFERENCES?`. DELETE is then an edit like any other: SAVE writes it (no save of its own, as the
  references' rewrite had), and REVERT ALL takes it back before SAVE. MOVE and SWAP keep their question: there the
  references can follow (the loops' too: the firmware's MOVE and SWAP gain the loop table, section 5). **A MOVE
  onto an empty place is a SWAP with it,** on the FM-1 and the page alike (section 3): the references that still
  name that place, grey, go to the place left, so they stay grey; the question counts them as uses of the place, as
  SWAP's counts both sides.
  - **A pattern deleted** stays named by the song positions and FUSION slots that played it: such a position
    plays 64 empty rows, as a missing pattern does now (the player's rule, which
    [../files/songs.md](../files/songs.md) gains), and such a slot nothing (`D0 MISSING`). Today's YES took such
    positions out of the orders and set such slots to `--`; CLEAN UP now sets both to `--` (below).
  - **A sample deleted** keeps its rule ([../files/instruments.md](../files/instruments.md#deleting)): the
    instruments that play it go with it, in one save, after its own warning; no reference is rewritten there
    either, so the cells that named those instruments go grey.
- **An instrument that does not exist plays as a silent engine** (an empty slot, a deleted cartridge), in
  whichever of a cell's three ways it is named: in full, the slot alone, or as the same as this track's last one in
  the record ([../files/patterns.md](../files/patterns.md#a-cells-bytes)). The cell is a cell like any other and
  every rule applies: it becomes the track's instrument (the track's parameters dropped, as for any other
  instrument), its note releases the note before as any note does and leaves its tail as it was, its volume and
  commands work, MIDI OUT sends the note, the FILTER's envelope keyed to the track fires and is held until the note
  ends ([../ui/screens/fx.md](../ui/screens/fx.md): its next note, a note off, NOTE CUT; a sounding note's hold
  stays as today, read from its voices); only no voice sounds. The cells after it that name no instrument play it
  too, silent, until one names another. **A portamento onto it** (3xx, 5xx, the volume column's Fx) starts no note,
  as onto any other instrument: the sounding note glides on. LEG onto it is a plain note, as LEG onto another
  instrument is ([../files/commands.md](../files/commands.md#legato)): silent, releasing the one before.
  **A loop's lane and the keys** on a missing instrument are the same silent engine.
  - The firmware's player does this already for a song's cells, but for the FILTER's envelope (fired only for a
    note an engine plays, its hold read from the voices: a silent note adds its own) and the lanes and keys
    (which stop before it). The
    playground's twin is further off: its note start leaves the note before held and sounding. Both twins change
    to this rule (section 5).
  - On the EDITOR screen those notes take the grey too, with its block, as the track's current instrument's colour.
- **Such a reference is drawn grey:** dark grey on dark grey's deep shade ([../ui/colours.md](../ui/colours.md)),
  the block keeping it apart from an empty cell or a muted track, in place of red, for every reference to nothing:
  a cell's instrument, a loop's, a favourite's `MISSING`, a song position's or a FUSION slot's missing pattern
  (`D0 MISSING`), a sample instrument's deleted sample (`THE SAMPLE IS DELETED`) and a deleted sample's line in its
  sampler's list. CHECK REFERENCES' list keeps its light grey lines. Red keeps its other meanings (recording, a
  late half, a refusal). colours.md, the EDITOR screen's, SONGS', INSTRUMENTS' and LOOPS' specs and the help's
  lines (below) change with it; in the help the word `GREY` is drawn dark grey, as `RED` is red
  ([../ui/help.md](../ui/help.md), Marks). **The pipette** on a cell naming nothing picks nothing: `NO INSTRUMENT
  HERE`, as on a cell and track with none.
- **On the EDITOR page** ([../editor.md](../editor.md#delete-and-delete-all) changes with it): on the **FM-1 panel**
  DELETE of an item follows this rule as the FM-1 does: only what is deleted goes (ALSO WHAT ONLY IT USES still
  offering what it alone uses; a sample taking the instruments that play it, as above), and what used it keeps
  grey references; it no longer takes those users with it. A built-in's DELETE, changed or not, deletes it
  (below); **BACK TO BUILT-IN**, a key of its own on a changed one, does what DELETE of a changed one does today
  (the firmware's own back), and an unchanged one's DELETE is no longer refused. On the **TABLE**, whose items have
  no numbers and so no places to keep naming, DELETE keeps taking with an item what uses it, as today. DELETE ALL
  on the FM-1 panel empties the user area, deletion marks included, so every built-in is back after the WRITE.
- **It names the place, not what was there:** an instrument put in that place later (PASTE, REPLACE, MOVE, a
  cartridge made under that number) plays in those cells again. So **wherever a new number is taken, the numbers
  references still name are passed over:** a cartridge's NEW and CLONE (after the user's last one, as now,
  [../ui/screens/instruments.md](../ui/screens/instruments.md)), every new or cloned pattern (SONGS' NEW PATTERN,
  CLONE PATTERN and `+ NEW PATTERN`, the pattern list's NEW and CLONE, SAVE TO PATTERN, the EDITOR's STORE IN A
  PATTERN, EDIT on an empty FUSION slot, the patterns RECORD LIVE writes), and on the page an item landing at a free
  number on the FM-1 panel; when every free number is named, the rule as now ([../ui/dialogs.md](../ui/dialogs.md)'s
  "the first free number" says so). **An item's own number comes first:** one moved onto the FM-1 panel still takes
  the number it had when that is free ([../editor.md](../editor.md#moving-items)), so a backup's cartridge moved back
  finds its grey references and plays in them again; so may an unrelated one that had the same number elsewhere, and
  a move that lands on a number grey references name says so in its status line, as PASTE does (`12 GREY
  REFERENCES PLAY AGAIN`). PASTE prefers empty slots no reference names (section 2). So nothing new lands in a place
  references still name unannounced. A MOVE onto such a place swaps (above), so its references stay grey; REPLACE
  over one makes them play, saying so (section 2).
- **`FF` is no pattern, not a reference to nothing:** a song position or a FUSION slot set to `--` names none on
  purpose. It is not grey, CHECK REFERENCES no longer lists it (today it does), and it plays as a missing pattern
  does: a position 64 empty rows, a slot nothing.
- **CLEAN UP clears them when asked:** its checklist ([../ui/screens/settings.md](../ui/screens/settings.md), the
  tools) gains a fourth line, `MISSING REFERENCES`, with how many it clears (no size: it frees next to nothing; the
  checklist's panel grows to 4 lines). Ticked, it empties the favourites that name nothing and sets the song
  positions and FUSION slots that name a missing pattern to `--` (`FF`): heard the same, and no song's order moves,
  so LOOP FROM and every `Bxx` still land where they did.
  - **The cells that name nothing in a song's pattern** are cleared a run at a time: a run is a cell naming a
    missing instrument and the cells after it on its track that name none, up to one that names another, within its
    pattern. In a run the missing instrument goes; the first cell that starts a note (a plain note, LEG's too, or a
    retrigger, E9x or Rxy) becomes a note off (`===`, its retrigger taken out), so the note before still ends there,
    and portamentos before it stay (they glide the note before, heard); after it, the notes go, with the
    retriggers, portamentos (3xx, 5xx, the volume column's Fx), NOTE CUTs and parameter commands (the missing
    engine's, `40`-`7F`), which would act on the note before. Volumes and the other commands stay; a cell left
    empty goes.
  - **It is an edit, not a no-op** (the manual says so where it describes CLEAN UP): MIDI OUT and the FILTER's
    envelope no longer get those notes; the note before fades with the run's volume, panning and pitch commands,
    without the missing instrument's own volume; a cell after the run naming the instrument from before it keeps
    the track's parameters (no instrument change between).
  - **Runs left** (their cells stay grey, CHECK REFERENCES lists them, the line does not count them): a run that
    reaches its pattern's end (the next pattern's first notes that name none play the silent instrument through
    the song, and would play the one before); every run in a pattern that holds a jump or a loop (`Bxx`, `Dxx`,
    `E6x`, on any track) or that a song enters past its first row (a `Dxx` with a row in the pattern before it in
    the song's order); every run in a pattern that songs and FUSION slots both play; the EDITOR's own pattern `FE`
    (CHECK REFERENCES leaves it out too).
  - **A FUSION slot's pattern** is cleared a row at a time (a row is a note, not a time,
    [../files/patterns.md](../files/patterns.md#fusion-patterns)): a row naming a missing instrument loses its
    instrument and note; a row naming none is silent already and stays.
  - Loops are left and not counted, their notes worth keeping (LOOPS gives one another instrument; a loop's missing
    instrument shows `MISSING`, grey; CHECK REFERENCES lists it, section 5).
  - So the line reads `NONE` once nothing it clears is left, and `NOTHING TO CLEAN UP` can show. It comes
    unticked: clearing gives up those references playing again if something is put back in the same place. The
    toast counts both (`CLEANED UP: 4 DELETED, 12 CLEARED`). CLEAN UP's UNUSED INSTRUMENTS and UNUSED PATTERNS count
    a grey reference as no use, and UNUSED INSTRUMENTS counts the loops' and the favourites' uses, as settings.md
    says (today it reads the cells alone).

### Deleting a built-in cartridge

So that the built-in cartridges not wanted do not fill the lists, **a built-in cartridge can be deleted as any
cartridge is:** DELETE in its SEL menu on the FM-1's cartridge list ([../ui/screens/instruments.md](../ui/screens/instruments.md),
the cartridges; a built-in's menu becomes NEW, CLONE, RENAME, AUTO LEVEL ALL, DELETE, and BACK TO BUILT-IN once
changed), and on the EDITOR page's FM-1 panel.
- **The same flow as any cartridge's:** used, it asks `CARTRIDGE F3 IS USED 12 TIMES`, `DELETE ANYWAY?`; DELETE
  deletes it, and what named its instruments keeps naming them, grey and silent; KEEP and HOME do nothing. Its
  changes, if it had any, go with it.
- **The mark:** a live record of length 0, kind **0x0F**, version 1, under the cartridge's number
  ([../files/store.md](../files/store.md#a-record) gains it), written with the changed copy's deletion if there was
  one, and carried by compaction as any live record. It is not a cartridge kind: the cartridges' one number space
  does not take it in. **A cartridge record under a marked number always wins** (an older firmware, which keeps
  the mark it does not know, may save a changed copy there later, and its compaction may rewrite either record as
  a newer save, so save order tells nothing): the built-in counts as not deleted, and the next SAVE writes the
  mark's deletion ([../files/store.md](../files/store.md#power-on) gains the rule, beside FAVORITES'). A mark under
  a number where the firmware has no built-in is left out, dead, and its compaction drops it. A firmware that
  does not know the kind shows the built-in again.
- **When storage is full,** the mark counts as its cartridge's deletion ([../files/store.md](../files/store.md),
  Deletions first): it goes first when the cartridges' deletions do, and always in the same save as the changed
  copy's deletion, so a power cut between the two saves never brings the built-in back.
- **Gone from every list and pick** of a cartridge or its instruments: ENV's, PRESETS' cartridge list, K1's order,
  the EDITOR's INSTR, the FUSION fill's pick, LOOPS' list, MIDI's bank select, the settings' fallback for the
  current instrument; the pipette on a cell naming it says `NO INSTRUMENT HERE`; on the page the pattern tab's,
  the LOOPS tab's and a favourite's source pickers leave it out. With every cartridge deleted, the keys play nothing
  and the footer's instrument reads `EMPTY`, as on an empty slot; ENV's list shows one line, `EMPTY` in dark grey,
  whose SEL menu is NEW alone.
- **The number is never reused:** NEW, CLONE and MOVE take `00`-`EF` only, as now; a deleted built-in's number stays
  the firmware's.
- **Back:** REVERT ALL before SAVE (a deleted cartridge leaves the list, so its own REVERT cannot be reached); after
  SAVE, FACTORY RESET brings every built-in back. **On the EDITOR page** the FM-1 panel keeps listing a deleted
  built-in in its place, greyed, `(DELETED)` after its name (`F3 WINDS/BRASS (DELETED)`), its size the user area
  its mark takes (`16 B`: the record's header, no payload; [../editor.md](../editor.md) Sizes), in place of a
  built-in's `IN FW`, counted as any item's in the selection bar and the panel's totals, with **RESTORE**: it
  removes the deletion mark, the built-in back as the firmware's own (its mark's deletion written at WRITE), one
  step of FILES' UNDO as DELETE is; a deleted built-in cannot be opened, moved or played until restored. A mark
  written or removed is a change WRITE counts, the item marked with its dot. After a WRITE that left the marks out
  (below), the panel shows what the FM-1 holds: those built-ins back.
- **Moves onto the FM-1 panel:** a changed built-in moved there while its number is deleted lands on its number
  and removes the mark (the built-in back, with the changes it carries). A song moved there that names a deleted
  built-in keeps those references, grey, and the status line says so (`F3 IS DELETED HERE: RESTORE BRINGS IT
  BACK`).
- **Disks and uploads** ([../files/disk.md](../files/disk.md), [../files/blob.md](../files/blob.md)): the marks are
  records like any: DOWNLOAD DISK carries them (after the cartridges, in the disk's order), and an upload makes the
  FM-1 hold what the disk holds, so a disk without them (the demo disk) brings every built-in back. WRITE to an
  FM-1 whose PT/FM does not know the kind leaves the marks out, saying so, and so do the install page's and the
  emulator's uploads (blob.md's stop on a (kind, version) the device does not take makes this one exception);
  WRITE's warning for built-ins the FM-1 does not have covers deleted ones. A mark under a number where the
  firmware has no built-in (a disk from another build) is left out of an upload too, saying so, as it would lie
  dead. A disk dropped on the TABLE leaves its marks out: the TABLE has no numbers to mark.
- **The built-in FAVORITES, `FB`,** can be deleted the same way ([202610311523-favorites.md](202610311523-favorites.md)
  says so: SEL + ENV and ADD TO FAVORITES then go to the first FAVORITES cartridge left).
- **The help** ([../ui/help.md](../ui/help.md), 40 characters a line, 17 lines a page). The built-in's line needs
  nothing: it names only what a built-in's menu adds (BACK TO BUILT-IN), and DELETE is in the cartridges' line
  already. INSTRUMENTS' menus page (16 lines) takes one more, its last two becoming three:
  ```
  A SOUND SONGS USE, MOVED OR DELETED:
    MOVED: IT ASKS IF THE SONGS FOLLOW
    DELETED: IT ASKS; THEIR NOTES GO GREY
  ```
  SAVING YOUR WORK (17 lines) keeps its count, its four lines on references becoming:
  ```
  MOVE OR DELETE A THING IN USE: IT ASKS
    MOVED: WHAT USES IT CAN FOLLOW
    DELETED: WHAT USED IT GOES GREY
  A MISSING THING: GREY, AND SILENT
  ```

## 5. Everywhere it is built

- **The EDITOR page:** the cartridge tabs' list (every slot, the selection, the keys, dragging, MOVE TO), the
  clipboard of contents, what comes along added at SAVE, the references rewritten at SAVE (each cell's slot read
  under its track's cartridge, the loops', the favourites' and the settings' instrument bytes), the preview fed the
  draft's places, the FM-1 panel's DELETE by the FM-1's rule and BACK TO BUILT-IN as a key, deleted built-ins
  greyed with `(DELETED)` and RESTORE, the marks in READ, WRITE, disks and moves, the grey; [../editor.md](../editor.md)
  written to say so (the tabs: Cartridges; the panels; moving items; DELETE and DELETE ALL; UNDO; READ, WRITE and
  disks; the page's texts); `editor/js/records.js` and `tools/ptfm_records.py` (kept equal) know kind 0x0F; the
  install page's and the emulator's uploader (`fw/web/seed.js`) leave the marks out for an older PT/FM.
- **The firmware** for section 4: the DELETE question for every slot range, cartridge and pattern (no rewrite:
  today's `ed_remap` with no target and the songs' order rewrite at DELETE go), the use count gaining the loops and
  favourites and MOVE / SWAP the loop table (today the count reads the patterns' cells alone and the rewrite only
  patterns, short of [../files/references.md](../files/references.md)), the silent engine (the FILTER's envelope
  fired and held by the note, not the voices; the loops' lanes and the keys taking it), the grey wherever a
  reference to nothing is drawn, the numbers no reference names, CLEAN UP's line, the record kind 0x0F and its
  rule at power-on, every lookup of a marked number finding nothing (the store's fall back to the built-in, the
  cartridge reader's scan of the kinds, a mark still pending in the edit memory hiding it already), the mark among
  the deletions written first and with the copy's deletion, a slot or cartridge MOVE onto an empty place rewriting
  as a SWAP, CHECK REFERENCES listing the loops' missing instruments and no longer `FF`, CLEAN UP's UNUSED
  INSTRUMENTS counting loops and favourites, DELETE in a built-in's menu, every list and pick
  leaving deleted built-ins out, ENV's `EMPTY` line, FACTORY RESET clearing the marks, the help's lines. The specs:
  [../files/store.md](../files/store.md), [../files/instruments.md](../files/instruments.md),
  [../files/references.md](../files/references.md), [../files/patterns.md](../files/patterns.md),
  [../files/settings.md](../files/settings.md), [../files/disk.md](../files/disk.md),
  [../files/blob.md](../files/blob.md), [../ui/saving.md](../ui/saving.md), [../ui/colours.md](../ui/colours.md),
  [../ui/help.md](../ui/help.md), [../ui/screens/instruments.md](../ui/screens/instruments.md),
  [../ui/screens/editor.md](../ui/screens/editor.md), [../ui/screens/songs.md](../ui/screens/songs.md),
  [../ui/screens/settings.md](../ui/screens/settings.md), [../ui/screens/loops.md](../ui/screens/loops.md),
  [../ui/dialogs.md](../ui/dialogs.md), [../ui/screens/fx.md](../ui/screens/fx.md) (FILTER's SOURCE),
  [../files/songs.md](../files/songs.md) (a missing pattern's 64 empty rows), [../files/README.md](../files/README.md)
  (the kinds), [../connections.md](../connections.md). **The playground,** its twin of section 4, the silent engine
  included: its note start on a missing instrument releases the note before and leaves its tail as the firmware
  does (today it keeps it held and sounding), fires and holds the FILTER, the lanes and keys alike; **the emulator**
  by the firmware itself. Sections 1-3 are the page's alone. **CHANGELOG:** FEATURE lines (the page's instruments,
  deleting built-ins) and CHANGE lines (DELETE no longer rewrites references; grey for red; a MOVE onto an empty
  place swaps references; CHECK REFERENCES and `FF`; CLEAN UP's MISSING REFERENCES), BUG lines (the playground's
  note start on a missing instrument; the FM-1's use counts and MOVE / SWAP missing the loops).
- **The manual:** the EDITOR page's chapter; the instruments chapter (DELETE, built-ins, grey), the songs' and
  the settings' (CLEAN UP).
- **Tests:** `tests/editor` (the selection by mouse, keys and touch; COPY, CUT, PASTE adding, its order of empty
  slots and refused, REPLACE and its question, DELETE and its question; across types refused; a sample brought
  along and reused by hash, a full sampler; a FUSION slot and a favourite on the same and the other panel; a paste
  that does not fit the FM-1 panel; a built-in's slots rearranged as an edit; moves by drag, MOVE TO and ↑ ↓, swaps,
  blocks, gaps; references following places across REPLACE and DELETE in one draft, rewritten at SAVE, refused over
  an open tab with changes, taken back by one UNDO; the preview during a rearrange; WRITE sending them, leaving the
  marks out for an older PT/FM); the firmware's (DELETE ANYWAY for slots and cartridges, nothing rewritten; the
  silent engine (a missing instrument's cells: the note before released and its tail left, the parameters
  dropped, volume and commands acting, MIDI OUT, the FILTER's envelope fired and held; the cells after it naming
  none silent too; a portamento onto it; a loop's lane and the keys), the firmware and the playground equal on it;
  the grey on the EDITOR screen, SONGS and LOOPS; the pipette; the numbers passed over, an item's own first;
  CLEAN UP's MISSING REFERENCES (a run cleared: its first note-starting cell a note off, portamentos before it
  kept, its notes, retriggers, portamentos, cuts and parameter commands after it gone, volumes and commands kept;
  the runs left: at a pattern's end, with jumps or loops, entered past the first row, songs' and FUSION's, `FE`; a
  FUSION pattern row by row; positions and slots to `FF`; the count reaching `NONE`); a MOVE onto an empty place
  as a SWAP; CHECK REFERENCES with loops, without `FF`; a built-in deleted, its mark, the lists, REVERT ALL,
  FACTORY RESET, compaction, the
  mark against a changed copy; a disk with and without marks) and the playground's twins of them.
