# Saving and references

Status: **settled** (2026-10-09). The record:
[the UI proposal](../proposals/202610072229-ui.md), section 8; the edit memory and REVERT:
[the edits proposal](../proposals/202610151809-edits.md) (2026-10-15).

How changes are kept: edits in RAM, SAVE writing them to flash, REVERT, the limited memory for edits,
FWS (the firmware space), and references between things and what changes them. How the flash stores it
all is in [../files/store.md](../files/store.md) and [../files/ram.md](../files/ram.md).

## How saving works

- **Edits go to RAM.** SAVE writes them all to flash, into the user area. The only other things that
  write to flash are a reference update and CLONE or MOVE of a sampler cartridge with samples
  ([screens/instruments.md](screens/instruments.md)); REVERT writes nothing.
- **The SAVE LED** is dim while anything is unsaved, dark when nothing is ([leds.md](leds.md)).
  - When a REVERT, an edit mode left with HOME, or an edit that puts a thing back as the flash holds it
    brings everything back to what the flash holds, nothing is unsaved, and SAVE goes dark.
  - **DJ's stand** ([../files/dj.md](../files/dj.md#the-stand)), the decks as they stand, is written by every
    SAVE; the decks standing otherwise than written count as one unsaved change. REVERT ALL brings back the
    stand as saved.
  - **The current instrument** ([navigation.md](navigation.md)) is kept by SAVE in the settings record
    ([../files/settings.md](../files/settings.md)). Picking another is no change of its own: the SAVE LED stays
    as it was, and REVERT leaves it; a SAVE writes it whenever it differs from the one kept.
  - **The pattern the EDITOR is on** (its own, or a numbered one:
    [screens/editor.md](screens/editor.md#the-editors-own-pattern)) is kept the same way.
  - **The EDITOR's own pattern** is kept by SAVE as a changed built-in is; emptied, its record is deleted.
- **SAVE with nothing unsaved** (and the current instrument and the EDITOR's pattern as kept) writes nothing:
  `NOTHING TO SAVE`.
- **SAVE during playback** is queued: SAVE blinks dim until playback pauses, and the write happens
  then, in silence (`SAVE WAITS FOR A PAUSE`). A save still waiting is lost if the FM-1 is switched off
  first.
- **A save** shows a progress bar (`SAVING 3 CHANGES`), then `SAVED`.
- **REVERT takes a change back** ([below](#revert)): REVERT in a screen's SEL menu the thing it shows,
  REVERT ALL everything.
- **REVERT ALL, after a question:** every change is dropped and everything is reloaded from flash. By
  taps: SETTINGS → TOOLS → REVERT ALL ([screens/settings.md](screens/settings.md)); SAVE + OCT- is
  its shortcut. SAVE held alone only shows its layer, as any held button does
  ([dialogs.md](dialogs.md#hold-layers)): let go then, it saves nothing; **SAVE + OCT+** saves (a
  SAVE's tap, held or not), so a SAVE held too long still saves by choice.
  - Playback pauses first; a progress bar (`RELOADING`), then `REVERTED TO SAVED`.
  - With nothing unsaved: `NOTHING TO REVERT`, and no question.
- **Switching off loses unsaved changes.**

## The edit memory

**The RAM for changes is limited:** 67 KB, the edits' alone, LOOPS' included
([../files/ram.md](../files/ram.md#editing-and-saving)).
- **It holds only what differs from the flash, each piece once:** a changed track of a pattern, a changed
  slot of a cartridge, a small record (a song, the settings) whole. A piece changed again is rewritten where
  it is, so turning a knob or changing a cell again and again costs no more memory than the first change. A
  piece that is back as the flash holds it goes, and is unsaved no more.
- **Every edit is heard at once,** wherever it plays: a pattern's from the next row, an instrument's from the
  next note, a song, the EDITOR's loop, LOOPS or a DJ deck alike.
- **It fills only with real changes:** about 650 changed tracks. Nothing else uses it, and a SAVE empties it.
- **Running low** (2026-10-10): `SAVE CHANGES TO FREE MEMORY` shows for 2 s, `SAVE` in the keys' yellow as
  the help draws a key ([help.md](help.md)), when both hold:
  - an edit has just made the edit memory grow;
  - less than 25 % of it is free (STATUS's EDITS FREE yellow, red under 10 %,
    [screens/status.md](screens/status.md)).

  At most once a minute *(proposed)*, so editing on is not pestered; never over a question.
- **Full:** an edit that needs more memory is not made. A question comes up instead, and stays when
  the screen changes.
  - **OCT+** writes everything at once. If music is playing, it pauses first: this save does not wait
    for a pause, since editing cannot go on without it.
  - **OCT-** (or HOME) goes back without saving. Playing, listening and moving around still work; the
    next edit that needs memory brings the question back.
  - **After the save,** the refused edit can be made again. It is not made by itself.

## Storage full

A SAVE that does not fit in the user area writes nothing (but the deletions it can write first, below), and
asks.
- The changes stay in RAM, so nothing is lost while the FM-1 is on.
- OCT+ runs SETTINGS → TOOLS → CLEAN UP (its checklist included), then saves if it now fits.
- Otherwise deleting a pattern, an instrument or a cartridge makes room, the demo disk's things among
  them; the free storage is on STATUS. The next SAVE writes those deletions first, then the rest
  ([../files/store.md](../files/store.md#the-room-for-the-editors-pattern)); if the rest still does not fit,
  it asks again.
- **Its figures count the user area,** with the room kept for the EDITOR's own pattern counted as used
  ([../files/store.md](../files/store.md#the-room-for-the-editors-pattern)).

## FWS

**FWS, the firmware space,** is the firmware's part of the flash
([../files/store.md](../files/store.md#where-things-go)): the firmware with its built-in things, the built-in
instruments ([../files/instruments.md](../files/instruments.md#built-in-instruments)) among them. The device
never writes it; only an install does. The rest of the flash is **the user area**, where every save goes.
**Everything in FWS is edited as anything else;** its changes are kept in the user area.

- **A built-in thing** (the firmware's own, F0-FF: the synth cartridges; patterns there too, once it has some) plays, is used and
  is edited as anything: keys, knobs, REC, LFO, EDIT, names, its menus' items. SAVE keeps the changed copy
  in the user area under the same number, and it is found before the firmware's own from then on, also
  after a firmware update.
- **BACK TO BUILT-IN** (the cartridge list's and the pattern list's SEL menus, on a built-in one that was
  changed) deletes the change, an edit like any other: the firmware's own is back
  (`CARTRIDGE F0 BUILT-IN AGAIN`).
- **A built-in thing never moves,** and has no DELETE.
- **Where it is shows:** a built-in thing as the firmware has it shows its number in brown wherever it is
  listed (patterns, cartridges); changed, it is the user area's, from the first edit on, saved or not.
- **A firmware update keeps the user area:** everything the user made stays. The install page backs
  everything up first, for the case where another firmware runs on the FM-1 in between
  ([../files/blob.md](../files/blob.md#seeding)).

## References

**Things that refer to other things:**
- a cell refers to an instrument;
- a song position and a FUSION instrument refer to a pattern;
- a sample instrument refers to a sample.

**Actions that change what a reference points at** ask first: MOVE, SWAP or DELETE of an instrument, a
cartridge, a pattern or a sample (`PATTERN 1A IS USED 3 TIMES`, `UPDATE REFERENCES?`); YES (OCT+) / NO
(OCT-); HOME cancels. An item nothing uses changes without a question.
- **YES:**
  1. The change is made, with every reference it touches. A pattern's DELETE takes it out of every song's
     order, except a song whose whole order it is (kept, so no song is left with no positions; it plays
     nothing), and sets every FUSION slot that played it to `--`, no pattern (`FF`,
     [../files/instruments.md](../files/instruments.md)).
  2. Playback pauses, as for a SAVE.
  3. Everything pending, the change with it, is written to flash as one save, with a progress bar
     (`UPDATING 3 REFERENCES`), then `REFERENCES UPDATED, SAVED` once it is written. A save that does not fit
     asks as SAVE does ([storage full](#storage-full)); its SAVE after CLEAN UP is a plain one (`SAVED`).

  The save's last record is written last ([../files/store.md](../files/store.md#saving)), so a power
  cut leaves the old state whole.
- **NO:**
  - The change is made, and the references are left as they were. They now point at whatever is
    there.
  - A reference to nothing is drawn in red and plays nothing.
- **HOME:** nothing happens.

**Tools (SETTINGS → TOOLS,** [screens/settings.md](screens/settings.md)**):**
- **CHECK REFERENCES** lists the missing ones.
- **CLEAN UP** finds unused patterns, samples and instruments, and asks which of them go, as a
  checklist ([screens/settings.md](screens/settings.md)).

## REVERT

**REVERT takes the thing a screen works on back to what the flash holds.** It is in the screen's SEL menu,
listed only while that thing has a change not saved. After a question (`REVERT PATTERN 0A TO SAVED?`,
`REVERT` / `KEEP`; HOME cancels), that thing's changes are dropped, nothing else's: `REVERTED`.

| Screen | What REVERT takes back | Its question |
|---|---|---|
| EDITOR | the pattern shown | `REVERT PATTERN 0A TO SAVED?`; on its own pattern `REVERT THE PATTERN TO SAVED?` |
| INSTRUMENTS | in a slot's menu, the slot; in the cartridge list's menu, the whole cartridge | `REVERT SLOT 0B:05 TO SAVED?`, `REVERT CARTRIDGE 0B TO SAVED?` |
| SONGS | the song, its record whole: positions, names, mixer, FX setup | `REVERT SONG 03 TO SAVED?` |
| FX | the effects of what plays: the song's, LOOPS' or DJ's | `REVERT THE EFFECTS TO SAVED?` over `FOR CITY LIGHTS` (the song's name), `FOR THE LOOPS` or `FOR DJ` |
| LOOPS | all of LOOPS: every loop, the length, the properties, the loops' mixes and FX setup; what the flash holds plays again | `REVERT LOOPS TO SAVED?` |
| SETTINGS | the settings; on the MIDI page, the MIDI map | `REVERT THE SETTINGS TO SAVED?`, `REVERT THE MIDI MAP TO SAVED?` |

- **Playback goes on:** what plays hears the thing as saved at once, as it hears every edit.
- **REVERT ALL** ([above](#how-saving-works)) takes everything back at once, DJ's stand included.
- **There is no step back** (UNDO) in this version: what an edit or an edit mode's drop (HOME,
  [controls.md](controls.md)) changed, only REVERT takes back, to what was saved.
- **So the big actions ask first:** CLEAR LOOPS, LOOPS' LENGTH made shorter where notes would go, LOAD FROM
  PATTERN over loops and AUTO LEVEL ALL, each with its question ([screens/loops.md](screens/loops.md),
  [screens/settings.md](screens/settings.md)). The small ones stay one press (a block's CUT, CLEAR and
  TRANSFORM, LOOPS' RE-RECORD and PEEL, a MIDI mapping deleted): REVERT is their way back.

## Layout

The questions here are laid out as every question is ([dialogs.md](dialogs.md#questions)): the panel
centred in the body, the lines centred, the answers in S over OCT- and OCT+.

```
 +-------------------------------------+
 |          REVERT TO SAVED?           |
 |       DROP 3 UNSAVED CHANGES        |
 |        AND RELOAD FROM FLASH        |
 |                                     |
 |         OCT-              OCT+      |
 |         KEEP             REVERT     |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

```
 +-------------------------------------+
 |     REVERT PATTERN 0A TO SAVED?     |
 |                                     |
 |         OCT-              OCT+      |
 |         KEEP             REVERT     |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

```
 +-------------------------------------+
 |      MEMORY FOR EDITS IS FULL       |
 |    SAVE BEFORE MAKING MORE EDITS    |
 |         23 UNSAVED CHANGES          |
 |                                     |
 |         OCT-              OCT+      |
 |         BACK            SAVE NOW    |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

```
 +-------------------------------------+
 |     NOT ENOUGH STORAGE TO SAVE      |
 |         12 KB MORE NEEDED           |
 |        CLEAN UP FREES 30 KB         |
 |                                     |
 |         OCT-              OCT+      |
 |         BACK            CLEAN UP    |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

```
 +-------------------------------------+
 |       PATTERN 1A IS USED 3 TIMES    |
 |          UPDATE REFERENCES?         |
 |                                     |
 |         OCT-              OCT+      |
 |          NO                YES      |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

The item in the references question is named as the screens name it: `PATTERN 1A`, an instrument's
ID (`0B:05`), a cartridge, a sample.

## Texts

`3`, `1A` stand for the actual count or number; a count of 1 takes the singular (`1 CHANGE`,
`1 TIME`, `1 REFERENCE`).

| Text | Where | When |
|---|---|---|
| `NOTHING TO SAVE` | toast | SAVE with nothing unsaved |
| `SAVE WAITS FOR A PAUSE` | toast | SAVE while playing |
| `SAVING 3 CHANGES` | progress | a save |
| `SAVED` | toast | the save done |
| `NOTHING TO REVERT` | toast | REVERT ALL (SETTINGS → TOOLS, or SAVE + OCT-) with nothing unsaved |
| `REVERT TO SAVED?`, `DROP 3 UNSAVED CHANGES`, `AND RELOAD FROM FLASH` | question; `REVERT` / `KEEP` | REVERT ALL, or SAVE + OCT- |
| `RELOADING` | progress | REVERT ALL |
| `REVERTED TO SAVED` | toast | REVERT ALL done |
| `REVERT PATTERN 0A TO SAVED?` (`THE PATTERN` on the EDITOR's own, `SLOT 0B:05`, `CARTRIDGE 0B`, `SONG 03`, `LOOPS`, `THE SETTINGS`, `THE MIDI MAP`); FX's `REVERT THE EFFECTS TO SAVED?` over `FOR CITY LIGHTS` (the song's name; `FOR THE LOOPS`, `FOR DJ`) | question; `REVERT` / `KEEP` | REVERT in a SEL menu ([above](#revert)) |
| `REVERTED` | toast | REVERT in a SEL menu done |
| `SAVE CHANGES TO FREE MEMORY` (`SAVE` yellow) | toast | an edit grows the edit memory, and under 25 % of it is free ([above](#the-edit-memory)) |
| `MEMORY FOR EDITS IS FULL`, `SAVE BEFORE MAKING MORE EDITS`, `23 UNSAVED CHANGES` | question; `SAVE NOW` / `BACK` | an edit that does not fit |
| `NOT ENOUGH STORAGE TO SAVE`, `12 KB MORE NEEDED`, `CLEAN UP FREES 30 KB` | question; `CLEAN UP` / `BACK` | a save that does not fit |
| `CARTRIDGE F0 BUILT-IN AGAIN`, `PATTERN F0 BUILT-IN AGAIN` | toast | BACK TO BUILT-IN |
| `PATTERN 1A IS USED 3 TIMES`, `UPDATE REFERENCES?` | question; `YES` / `NO` | MOVE, SWAP, DELETE of something used |
| `UPDATING 3 REFERENCES` | progress | YES |
| `REFERENCES UPDATED, SAVED` | toast | the update done |
| `CHANGES DROPPED` | toast | HOME leaves an edit mode that changed something |
