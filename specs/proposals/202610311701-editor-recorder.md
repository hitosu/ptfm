# Proposal: the EDITOR page's recorder, and cutting samples

Status: **settled** (2026-10-31); its specs are written when the work on it starts, and lead from then on.

**The EDITOR page records sound** from any input the computer has (a microphone, a line in, the FM-1 itself over
USB audio) and keeps the take worth keeping as a sample. **Every sample's tab learns to cut:** select a part, trim
to it, save it as a new sample, and find the hits in a recording (drums played one after another, a phrase of
notes) to remove, keep apart or turn into a playable kit. Page work: the firmware does not change
([../editor.md](../editor.md)).

**Two players** are named below. **The preview** plays what the FM-1 would, through the firmware's sound code
([../editor.md](../editor.md#the-preview)), up to its room of about 1 MB. **The page's own player** plays a sound
as it is (a take, an original, a sample the preview cannot hold) through the browser at its own rate and the
page's VOL. Both show in the footer's player (STOP, the progress); starting one stops the other.

**Originals:** a sample made from a dropped sound or a take has its **original**, the sound as it came, which the
SAMPLE tab's CHANNELS, RATE, DEPTH and OPTIMIZE start from ([../editor.md](../editor.md#the-tabs)). A sample without
one (from a module, a disk, a backup, the FM-1 panel, which never keeps originals) starts from its own audio as the
tab opened it; "the original" below means that audio for it.

## 1. The recorder

- **`● RECORD`** among the TABLE's keys ([../editor.md](../editor.md#table)) opens the recorder, a dialog over the
  page; it stops the preview. The browser asks for the input when the dialog first opens (on the page opened from
  a file again after every reload, since such a page keeps no permission); refused, the dialog says so and records
  nothing.
  - **INPUT:** the inputs the browser offers, by name; the FM-1 among them while it is connected, under the name
    the computer gives its USB audio (Windows names it `Line (FM-1 PT/FM)`), so what the FM-1 plays can be
    recorded. The last one picked is found again by its name, as a MIDI keyboard is. INPUT is locked while a take
    runs.
  - **The meter:** the input's level, live, its peak held a second, and `CLIP` lit for a second after any sample
    reaches full scale.
  - **RECORD / STOP** (Space too, never also pressing a focused key): a take, from RECORD to STOP, as long as it
    runs: no limit; fitting the FM-1 is the author's to see to (section 4).
  - **The take:** its waveform and length, **▶** to hear it (the page's own player), and **SAVE**.
- **Takes one after another:** RECORD again replaces the take in the dialog, saved or not; there is no list. Only
  SAVE puts a take on the TABLE, as a loose sample (in no sampler), named by the naming rule (section 2) from `TAKE`
  (`TAKE00`, `TAKE01`, …), its source `RECORDING` and the time, its tab not opened: the dialog stays open for the
  next take. SAVE is one step of FILES' UNDO ([../editor.md](../editor.md#undo)).
- **Closing** (✕, Esc) stops a take that runs, then, with a take not saved, asks: SAVE, DISCARD or STAY (back to
  the dialog). A reload with a take not saved asks first, as a tab's changes do.
- **SAVE writes the take's original first;** when the browser cannot keep it, SAVE is refused, saying so, and the
  take stays in the dialog.
- **An input that goes away** (unplugged) stops the take, which is kept, and the dialog says `THE INPUT WENT AWAY`.
- **The best quality PT/FM holds:** the recorder runs at the input's own rate, the browser's voice processing (echo
  cancelling, noise suppression, automatic gain) switched off, and keeps the take as it comes, stereo when the input
  is. Its sample is made at the best a sample on the FM-1 can be: 16 bits, 44.1 kHz (the take converted by the
  page's own resampler), CHANNELS MIXED (samples are mono), not at the lowest rate a dropped sound is proposed. The
  take is kept as the sample's original, as a dropped sound's is, so the SAMPLE tab's CHANNELS, RATE, DEPTH and
  OPTIMIZE start from it and make it smaller when wanted.
- Nothing is heard through the page while recording (no feedback); the meter shows the level.
- **Room:** a take lives in the page's memory until SAVE. **Originals are kept in a store of their own** in the
  browser, each under an id of its own, written once; an item names its original, a SAVE that remakes it (REMOVE,
  section 3) writes a new one, and one is dropped when no item and no UNDO step names it. A dropped sound's original
  moves there too, so a long one is not written again at every change of the page
  ([../editor.md](../editor.md#kept-in-the-browser) says so).

## 2. The SAMPLE tab: the selection

**The cut becomes a selection.** In the SAMPLE tab ([../editor.md](../editor.md#the-tabs)) the waveform shows the
sample as the draft has it, nothing selected when the tab opens. Dragging across it (or dragging the selection's
START and END) selects a part and changes nothing by itself; `▶ PLAY THE SELECTION` plays it through the preview, as
the FM-1 would (the page's own player when the preview cannot hold it), and the whole sample with nothing selected.
The selection and the slices' markers stay on the same sound when RATE or OPTIMIZE changes the frames. Every
sample's tab, on either panel, gains:
- **TRIM:** the sample becomes the selection, in the tab's draft (UNDO in the tab, SAVE keeps it); dimmed with
  nothing selected. **TRIM SILENCE** selects the sound and trims to it: from 5 ms before the first sound over the
  silence level (the slices', section 3) to 5 ms after the last; **WHOLE SAMPLE** selects all and, for a sample with
  an original, brings back the whole original. OPTIMIZE's LOSSLESS keeps its own −50 dB for the silent tail it
  cuts.
- **SAVE AS A NEW SAMPLE** (dimmed with nothing selected): the selection becomes a new sample, made from the draft's
  sound (a REMOVE not yet saved included) at its CHANNELS, RATE and DEPTH, at the original's level (a level SMALLEST
  lowered for this sample's instruments is not taken: the new one has none), its original the same part of this
  one's, in the background (a Worker); the tab stays on the current sample, which does not change. One step of
  FILES' UNDO; DISCARD of the tab keeps it. It is a copy under its new name, a sample of its own; on the TABLE its
  source is `FROM` and the sample's name (`FROM KICK`).
  - **Where:** on the same panel. A sample in a sampler gives one in the same sampler, at its first free sample,
    with no instrument (the sampler's NEW INSTRUMENT FOR IT makes one); a loose sample on the TABLE gives a loose
    one. Refused, saying so: a full sampler (`SAMPLER 0B IS FULL: 64 SAMPLES` on the FM-1 panel, the sampler's name
    on the TABLE), no room on the FM-1 panel (as a move that does not fit is,
    [../editor.md](../editor.md#moving-items)), and a sampler whose tab has changes not saved
    (`SAVE OR CLOSE THE SAMPLER'S TAB FIRST`), so that tab's SAVE never drops the new sample.
  - **The naming rule:** the sample's name with any digits at its end read off, then two digits, the first number
    not taken by a sample on that panel (`KICK` → `KICK00`, `KICK01`; `TAKE00` → `TAKE01`); a name too long for the
    digits within a sample name's 10 characters gives up its last letters to them (`DRUMLOOP 1` → `DRUMLOOP00`);
    past 99, three digits; a sample with no name counts as `SAMPLE`.
- **Instruments that play the sample follow TRIM and REMOVE on SAVE** as they follow a cut today, and more
  exactly: a point inside a removed part moves to its join, a loop left with no length turns OFF, and the status
  line names the instruments changed.

## 3. The SAMPLE tab: SLICES

**DETECT SLICES** finds where the sounds in a sample start (each hit of a drum recorded one after another, each note
of a phrase) and cuts the selection (the whole sample when nothing is selected) into **slices**, shown on the
waveform as markers over numbered spans:

- **Detection:** the level is measured in 5 ms windows. A slice starts 5 ms before the level jumps up (its attack
  kept whole), and ends where its level drops under the silence level and stays under it for 100 ms (the slice ends
  at the drop), or at the next start; what lies between is a **silent slice**, drawn dimmed, as is the part before
  the first start when it stays under the silence level. **The silence level** is 40 dB under the highest peak in
  what is cut (the selection, or the whole sample), so a microphone's noise counts as silence; TRIM SILENCE uses the
  same. **SENSITIVITY** (1-10, 5 at first) sets the jump that starts a slice: 24 dB at 1, 3 dB at 10, evenly
  between; starts closer than 30 ms count as one. Moving it detects again, the markers placed by hand given up
  (UNDO in the tab). With markers placed or moved by hand, each slice's end and the silent slices are found again
  from the starts the same way.
- **By hand:** a marker dragged moves a slice's start (a drag that starts on a marker moves the marker, else it
  selects); a click on a span picks the slice (Ctrl and Shift for several; on a touch screen each tap adds or
  removes a slice from those picked); a double-click adds a marker; **ADD A SLICE** (a marker at the selection's
  start) and **DELETE THE MARKER** (the picked slice's start; Delete too) do the same by touch. Each slice has its
  ▶ (through the preview, as PLAY THE SELECTION). Markers added, moved or deleted are steps of the tab's UNDO; the
  tab's ✕ does not ask for them alone (they are not stored).
- **REMOVE SILENCE:** the silent slices are taken out, the rest joined end to start, each join with a 5 ms
  crossfade so it does not click.
- **REMOVE:** the picked slices are taken out, joined the same way.
- **REMOVE and REMOVE SILENCE remake the sound:** the parts kept are joined from the original's own frames, its
  channels and rate kept, and the joined sound becomes the sample's new original (in the tab's draft, UNDO in the
  tab; SAVE keeps it, writing it as a new original, section 1), so CHANNELS, RATE, DEPTH and OPTIMIZE start from it
  and WHOLE SAMPLE brings back the joined sound, not what was removed.
- **SAVE AS SAMPLES:** each picked slice (all but the silent ones when none is picked) becomes a new sample, as SAVE
  AS A NEW SAMPLE makes one (its place, the naming rule, the refusals, one step of FILES' UNDO); the tab stays on the
  current sample. When not all fit (a sampler's 64, the FM-1 panel's room, a slice longer than a sample can be in a
  sampler: `SLICE 03 IS TOO LONG`), none is saved, saying so.
- **MAKE A KIT:** the picked slices (all but the silent ones when none is picked) become a playable kit on the same
  panel, one step of FILES' UNDO, its items' source `KIT FROM` and the sample's name; more than 64 refuse it, saying
  so (`64 SLICES AT MOST IN A KIT`), as a slice longer than a sample can be does (`SLICE 03 IS TOO LONG`):
  - **a sampler cartridge** named after the sample, slice 1 its sample 00 and the instrument in its slot 00, slice
    2 in 01, and so on; each sample made as SAVE AS A NEW SAMPLE makes one and named by the naming rule, each
    instrument named as its sample, playing it whole: START 0, END its length, LOOP OFF, BASE NOTE C-4, LEVEL 0 dB,
    a new instrument's envelope;
  - **a FUSION cartridge** named after the sample cut to 8 characters, then ` KIT`; its slot 00 the kit, named as
    the sample, LEVEL 0 dB: a FUSION pattern (on the TABLE named as the FUSION cartridge) of 128 rows and one track,
    EDIT STEP 0 as a FUSION slot's new pattern has ([../files/patterns.md](../files/patterns.md#fusion-patterns));
    row `3C` + *n* − 1 holds slice *n*'s instrument at note C-4 with an R4 parameter command of 0, so each hit rings
    out after its key is let go, as the demo disk's DRUM KIT's cells do. A FUSION plays each layer at its cell's
    own note (FILL writes each row's own note, `C-4` on row `3C`:
    [../ui/screens/instruments.md](../ui/screens/instruments.md#controls)), so slice 1 sounds on the key C-4, slice 2
    on C#4, and up, a key each, every slice at its own pitch;
  - both opened in their tabs, to be edited as any (another key, another LEVEL, a loop).
  - **On the FM-1 panel** it needs two cartridge numbers, a pattern number (`00`-`EF`) and room, and is refused as
    a move that does not fit is.
- **The slices are the tab's alone:** they are not stored with the sample; TRIM keeps the markers inside the part
  kept, and SAVE keeps what REMOVE and TRIM did to the sound, not the markers.

## 4. Too long for the FM-1

A sample's length is 20 bits ([../files/instruments.md](../files/instruments.md#samples-and-fusion-slots)): at
most 2^20 − 1 frames, about 23.8 seconds at 44.1 kHz. **Only a loose sample on the TABLE can be longer:** a take, a
dropped sound (which today is refused instead), or a new sample saved from a loose one that is. It holds its
original and no record; its size reads `TOO LONG`, its hash is its original's, its icon is drawn from its original,
and it plays through the page's own player. It cannot go into a sampler or to the FM-1 panel, a move refusing it
(`TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST` at its rate), until TRIM, REMOVE, REMOVE SILENCE, a lower RATE or
OPTIMIZE makes it fit: an OPTIMIZE level that makes it fit counts as smaller. A change that would make a sample in a
sampler too long is refused, as today. The preview's room is its own limit: a sample it cannot hold (a long one at
16 bits passes it near 20 seconds) plays through the page's own player too.

## 5. Everywhere it is built

- **The EDITOR page:** the recorder (an AudioWorklet from a `data:` URL, as the page's other worklets, in a context
  at the input's own rate), the page's own player, a Worker from a `data:` URL for the work done in the background,
  the originals' own store, the SAMPLE tab's selection, TRIM, SAVE AS A NEW SAMPLE and SLICES;
  [../editor.md](../editor.md) written to say so: the TABLE (its keys, the sources `RECORDING`, `FROM …`,
  `KIT FROM …`), items, references and hashes (an item with no record: its size `TOO LONG`, its hash its
  original's), moving items (the TOO LONG refusal), UNDO's steps, drops and conversions (a long sound no longer
  refused), the preview's room, the tabs (SAMPLE), kept in the browser, the tests, the page's texts.
- **Not the firmware,** the playground or the emulator: they have no recorder and no SAMPLE tab.
- **The manual:** the EDITOR page's chapter (recording, selecting and trimming, slices and kits).
- **Tests:** the page's scenarios in a headless Chrome with a fake input (a take saved, replaced, discarded, the
  input gone; the naming rule: digits read off, past 10 characters and 99, no name; TRIM, TRIM SILENCE's level,
  SAVE AS A NEW SAMPLE, a full sampler and a sampler's open tab; DETECT SLICES on a sample of known hits, silent
  slices, SENSITIVITY, markers by hand and by keys, touch picking, REMOVE SILENCE's joins and instruments following,
  SAVE AS SAMPLES, MAKE A KIT's two cartridges on both panels played by the preview, each hit ringing out; `TOO LONG`
  from a take and a dropped sound, refused until it fits, OPTIMIZE making it fit).
