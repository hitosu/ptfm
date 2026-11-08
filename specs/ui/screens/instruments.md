# INSTRUMENTS

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.3 and 13.3.

INSTRUMENTS (OP3, the MAKE group) picks, plays and edits the instruments: a cartridge's slots, the
current instrument in detail, and an edit mode for each engine (DX7, Juno-106, TB-303, CHIP, sample
instruments, FUSION). How cartridges and slots are stored: [../../files/instruments.md](../../files/instruments.md);
CHIP's engine: [../../chip.md](../../chip.md).

## What it shows

**Browsing:**
- **The top:**
  - a line naming the cartridge: ID, name, type, slots used;
  - its slots in two columns of 8 (index, name). The page is N/M.
- **The bottom:** the current instrument in detail:

  | Instrument | Shown as |
  |---|---|
  | DX7 | operators |
  | Juno-106 | sliders |
  | TB-303 | knobs |
  | CHIP | its oscillator's steps and its tables |
  | sample instrument | waveform |
  | FUSION | its pattern, as a small map |

**The footer** ([../frame.md](../frame.md)): on the left the current instrument, as on every screen; on
the right this screen's own, the cartridge (`01 KEYS/ORGANS`) in its type's colour. A knob turned shows
its name and value in the header's place for 1.2 s (`RELEASE 33 MS`).

## Controls

**Navigating:**
- **The cursor (SELECT / ALGORITHM) is the current instrument.** SELECT moves a slot at a time,
  ALGORITHM by 8 (a column).
- **PRESETS** steps to the next instrument there is, skipping empty slots, turning the pages, and wraps,
  as it does on every screen; from an empty slot (the cursor can stand on one) it goes to the next one
  filled in its direction. SELECT and ALGORITHM still move over every slot, empty ones too.
- **K1** turns the cartridges, and wraps.

**Listening:**
- **The keys** play the current instrument.
- **ARP** is the arpeggiator, every screen's ([../controls.md](../controls.md)): switched on, it waits for
  the keys and plays them in SETTINGS' ARP RHYTHM, ARP PATTERN and ARP LATCH (with LATCH, notes keep
  playing once let go, so both hands are free for the knobs; a key pressed with none held starts a new
  set). It plays the current instrument, however it is picked.

**EDIT: edit mode** (EDIT lit). The list gives way to two panels: the parameters, and a picture of the
instrument that follows the cursor.
- **Patches:** the parameters below, the picture above:

  | Engine | Picture |
  |---|---|
  | DX7 | the algorithm, and the selected operator's envelope |
  | Juno-106 | its envelope and filter |
  | TB-303 | its envelope (the cutoff's sweep over a note, an accented one too) and filter, as the Juno-106's |
  | CHIP | a picture a page ([below](#edit-mode-chip)): the oscillator's steps and the volume over a note; the envelope; the table at the cursor; the wave table |
- **Sample instruments:** the parameters above, the picture below, the same on both pages (ALGORITHM
  turns only the table). The parameters: SAMPLE, START, END, LOOP (OFF / FORWARD / PING-PONG), LOOP
  START, LOOP END, BASE NOTE, TUNE (in 1/128 of a semitone), VOLUME (a module's default volume, 0-64),
  LEVEL, and on a second page an ENVELOPE as a DX7 operator's (R1-R4, L1-L4).
  - **The picture** (from y 100; the table takes 4 rows at most): the whole sample's waveform with its
    markers (START and END white, the loop's green) and a yellow line for each voice playing the sample
    (any of the cartridge's instruments on it), where it plays, moving as it plays (redrawn about 30 times a
    second while one sounds), and under it, on the same x, a 34-px graph of the
    envelope over the region the instrument plays, the same markers through it, its foot (gain 0) a dark
    grey line; the legend `WHITE START/END  GREEN LOOP  YELLOW PLAY` at y 213.
  - **The envelope as heard:** a note at the BASE NOTE on the sampler's own voice, its envelope and its
    place in the sample a block at a time, so x is where in the sample, y the gain (linear, full at the
    graph's top), in the sampler's colour. With a loop the key is held while the loop comes round, until a
    pass enters it with R1-R3 done, and let go just before that pass reaches LOOP END: the held level runs
    across the loop and the release from LOOP END to END; the passes before it, heard while the envelope
    still rises, in dark grey. Without a loop the key is held to END (a release starts wherever the key
    is let go, and END stops the sound anyway).
  - **Its ghost:** past END the same note goes on, dotted, as if END were the sample's end, until the
    sample ends or the release reaches silence, so an envelope longer than the region shows where it
    would go without saying that the sample past END plays. With END at the sample's end there is no
    room, and no ghost.
  - START and END make an instrument from a cut of a sample. Another sample picked (K1 on SAMPLE, or
    THE SAMPLES list) is played whole: START at its start, END at its end, LOOP OFF. **K1 on SAMPLE** turns
    through every sample on the FM-1 by number, passing over those whose deletion waits for SAVE; past the
    first or the last it does nothing.
  - The loop repeats while the key is held; at release the sound goes on from where it is to END,
    while the envelope releases.
  - **RELEASE** sits on the first page with the loop (K4 of its row): the envelope's R4, the same
    value, shown as a time: how long the tail (the rest of the loop and of the sample) takes to fade
    to silence (-60 dB, from full) once the key is let go. The sample's speed stays.
    - Right is longer. The quickest is 4 ms (R4 99), instant to the ear and without a click; the
      default 33 ms (R4 80).
    - **It goes as far as the tail's own length:** START (with a loop, LOOP START) to END at the base
      note. A longer fade would never be heard: the sample ends first.
    - Neighbouring rates that fade alike are one step. The times are the envelope's, measured: R4 70
      116 ms, 60 309 ms, 50 0.9 s, 40 3 s, 30 8.5 s, 20 30 s, 10 79 s; under 7 it never fades
      (`ENDLESS`, set on the ENVELOPE page).
- **The controls in edit mode:**

  | Control | Does |
  |---|---|
  | SELECT | picks the group (a row of the table) |
  | K1-K4 | edit the group at the cursor (a DX7 operator's 4 rates) |
  | ALGORITHM (or SEQ) | turns the edit pages (DX7: OPERATORS, ENVELOPES, PITCH / LFO, VOICE); SEQ wraps |
  | PRESETS | still changes the instrument; editing goes on with the new one (HOME drops its changes too) |
  | SEL + PRESETS | the cartridge as everywhere, passing FAVORITES cartridges by (MIDI's bank select the same: one there does nothing) |
  | LFO | resets the value at the cursor (the last knob turned on the row, else K1's) |
  | SEL on NAME | text entry ([../dialogs.md](../dialogs.md)) at once; on the other rows SEL opens the menu |
  | SEL on PATTERN (FUSION) | the pattern in the EDITOR, locked, at once (the menu also has EDIT PATTERN) |
  | ENV on SAMPLE / PATTERN | THE SAMPLES list, every sample on the FM-1 ([below](#the-samples)) / the pattern list |
  | EDIT | leaves edit mode, keeping the changes |
  | HOME | leaves edit mode, dropping them ([../controls.md](../controls.md)): every instrument edited since EDIT as it was |
- **LEVEL:** every instrument has one (dB). SEL → AUTO LEVEL measures and sets it.
- **SEL held + a knob** takes coarse steps ([../controls.md](../controls.md)): LEVEL by 6 dB, a value by
  `10`, RELEASE by 4 steps, a sample position by 10 steps.

**The parameters, page by page.** A page holds groups, a group up to 4 values, one a knob. Values are
written as the frame's rules say ([../frame.md](../frame.md)): hex without a unit, as wide as their
range, signed ones with their sign.

| Engine | Page | Groups: K1-K4 |
|---|---|---|
| DX7 | OPERATORS | OP1 … OP6: LEVEL, COARSE, FINE, DETUNE |
| | ENVELOPES | for each operator, OPn RATE: R1-R4, and OPn LVL: L1-L4 |
| | PITCH / LFO | P RATE: R1-R4; P LEVEL: L1-L4; LFO: SPEED, DELAY, PM DEPTH, AM DEPTH; LFO 2: WAVE (`TRI`, `SAW-`, `SAW+`, `SQR`, `SINE`, `S+H`), SYNC (`OFF`, `ON`), P SENS |
| | VOICE | NAME; VOICE: ALGORITHM (`00`-`1F`), FEEDBACK, OSC SYNC, TRANSPOSE; LEVEL |
| Juno-106 | DCO | DCO: LFO, PWM, NOISE, SUB; WAVES: RANGE (`16'`, `8'`, `4'`), PULSE, SAW, PWM BY (`LFO`, `MAN`) |
| | FILTERS | VCF: FREQ, RES, ENV, LFO; VCF 2: KYBD, ENV +/- (`+`, `-`), HPF (`0`-`3`) |
| | ENVELOPE | ENV: A, D, S, R; VCA: LEVEL, MODE (`ENV`, `GATE`), CHORUS (`OFF`, `I`, `II`); LFO: RATE, DELAY |
| | VOICE | NAME; LEVEL |
| TB-303 | KNOBS | KNOBS: WAVE, CUTOFF, RESO, ENV MOD; KNOBS 2: DECAY, ACCENT; NAME; LEVEL |
| CHIP | TONE | OSC: OSC (`PULSE`, `TRIANGLE`, `WAVE`, `BUZZ`, `NOISE`), then the oscillator's own: PULSE's DUTY (`12.5%`, `25%`, `50%`, `75%`), WAVE's WAVE (the wave's name, `ORGAN`), BUZZ's SHAPE (`SAW DN`, `SAW UP`, `TRI`), STEPS (`16`, `32`), GATE (`OFF`, `ON`); NOISE: NOISE (`OFF`, `ON`), MODE (`LONG`, `SHORT`), RATE, KEY (`KEY OFF`, `KEY ON`); CLOCK: CLOCK (`BRK`, `8BIT`, `PSG 1.77`, `PSG 1.00`, `PSG 1.79`, `16BIT`, `EXACT`), CURVE (`BRK`, `8BIT`, `PSG`, `16BIT`), FILTER (`BRK`, `8BIT`, `PSG`, `NONE`), FRAMES (`60 HZ`, `50 HZ`); SWEEP: SWEEP (`OFF`, `DOWN`, `UP`), SHIFT, PACE; LEVEL; NAME |
| | VOLUME | VOLUME: VOL, ENV (`NONE`, `DOWN`, `UP`, `PSG`), STEP, RELEASE; PSG ENV: SHAPE (`9 \___`), PERIOD, FINE; LEVEL |
| | TABLES | VOL, ARP, PITCH: TABLE (`01 SETTLE 11`, `--`), SPEED (`X1`-`X4`); VIBRATO: DELAY, SPEED (`X1`-`X4`), DEPTH; DUTY, NOISE: TABLE, SPEED |
| | WAVE | TABLE: TABLE (`00 SAW`) |
| sample instrument | SAMPLE | SAMPLE: SAMPLE, START, END; LOOP: LOOP (`OFF`, `FWD`, `PING`), FROM (LOOP START), TO (LOOP END), RELEASE; PITCH: BASE NOTE (`C-4`), TUNE, VOLUME, LEVEL; NAME |
| | ENVELOPE | RATES: R1-R4; LEVELS: L1-L4 |
| FUSION | FUSION | PATTERN: PATTERN; NAME; LEVEL |

- **LEVEL** is in dB, half a dB a click, -64 to +63.5 (`0 DB`, `+1.5 DB`, `-3 DB`).
- **Sample positions** (START, END, FROM, TO) are hex as wide as they need; a click moves 1/200 of the
  sample. The table names FROM and TO; the footer's knob display says LOOP START and LOOP END.
- **RELEASE** is a time: `4 MS` … `926 MS`, `1.1 S` … `9.9 S`, `12 S` … `118 S`, `ENDLESS`.
- **PATTERN** is the pattern's number, `--` when the slot has none, `D0 MISSING` when its pattern is
  gone.
- **CHIP** ([../../chip.md](../../chip.md) says what each does): the TONE page's OSC row has the
  oscillator's own knobs, so it changes as OSC turns (TRIANGLE and NOISE: OSC alone). VOL, STEP, RELEASE,
  RATE, SHIFT and PACE are hex, two digits (`0F`; a sampler's RELEASE is a time, CHIP's the chip's 0-F); the PSG
  envelope's SHAPE is its number and a picture of it in four characters (`` ` `` the level held high:
  `0`-`3` and `9` `\___`, `4`-`7` and `F` `/___`, `8` `\\\\`, `A` `\/\/`, `B` `` \``` ``, `C` `////`, `D`
  `` /``` ``, `E` `/\/\`); its PERIOD is four hex digits (`0320`; 0 reads as 1): PERIOD moves the first
  two, FINE the last two. A table is its number and name (`01 SETTLE 11`), `--` for none; SPEED `X1`-`X4`
  (frames a step). VIBRATO's DELAY is in frames and its DEPTH in cents, both even (0-62); the wave is its
  number and name (`00 SAW`).

**SEL menu** (the slots are fixed):
- MARK (below), while nothing is marked;
- COPY, PASTE;
- MOVE, SWAP (the instrument floats over the slots, [../dialogs.md](../dialogs.md): SELECT one at a
  time, ALGORITHM by 8; MOVE only to empty slots, and not at all in a full cartridge), DELETE: these ask
  about the cells that use the instrument ([../saving.md](../saving.md));
- INIT, RENAME, AUTO LEVEL;
- **ADD TO FAVORITES,** right after MARK (first with slots marked), on a slot that holds an instrument, while a
  FAVORITES cartridge exists ([FAVORITES](#favorites));
- **NEW CARTRIDGE,** while browsing, after AUTO LEVEL: a group titled `NEW CARTRIDGE` ([../dialogs.md](../dialogs.md#levels)):
  BACK, DX7, JUNO-106, TB-303, CHIP, SAMPLER, FUSION, FAVORITES. The cartridge is made as the cartridge list's NEW
  makes it, at the smallest free number, and opened (`CARTRIDGE 11 MADE`);
- **REVERT:** the slot back as saved, after a question (`REVERT SLOT 01:00 TO SAVED?`, OCT+ `REVERT`,
  OCT- `KEEP`; then `REVERTED`), its sound heard so from the next note; listed only while the slot has
  something unsaved ([../saving.md](../saving.md)).
- **In edit mode** only what changes the instrument itself: EDIT PATTERN (FUSION), ADD TO FAVORITES, COPY, PASTE,
  INIT, RENAME, AUTO LEVEL, REVERT. MOVE, SWAP and DELETE move slots and their references, so they
  wait until edit mode is left.
- **Every change is heard at once:** a note that starts after it plays the slot as changed, whatever
  plays it (a song, the EDITOR's loop, the loops, a DJ deck, the keys).
- PASTE and INIT over a used slot change its sound, as editing does; the cells keep playing that
  slot.
- **MARK,** first in the menu while browsing with nothing marked, marks several slots: SELECT (and
  ALGORITHM, by 8) then stretch the mark from the slot, and SEL opens the menu for them; COPY, INIT
  and DELETE act on all of them, COPY on 4 at most (the first 4 marked: `COPIED 4`). SEL held + SELECT marks the same way, as a shortcut. HOME drops the
  mark.
- **A built-in cartridge** ([../saving.md](../saving.md#fws)), in the firmware space, plays and is edited
  as any other (EDIT, the knobs, names, its slots' menu); a SAVE keeps the changes in the user area under
  its own number, found before the firmware's own.

**ENV: the cartridges,** while browsing, on every type. An overlay listing each cartridge's ID, name,
type (in its colour) and slots used ([../dialogs.md](../dialogs.md)).
- **ENV again** shows the highlighted cartridge: its first used slot becomes current.
- **The order,** one in every cartridge list (ENV's, PRESETS' cartridge list, K1's order, LOOPS' list, MIDI's bank
  select): the built-in FAVORITES first, then the user's FAVORITES cartridges by number, then the other
  cartridges, the user's then the built-in ones.
- **SEL menu:** NEW (a group: pick its type, FAVORITES last), CLONE, MOVE, DELETE, RENAME, AUTO LEVEL ALL, REVERT.
  - **AUTO LEVEL ALL** asks first (`AUTO LEVEL 32 SOUNDS?`, `EACH ONE'S LEVEL SET AGAIN`, OCT+ `LEVEL`,
    OCT- `KEEP`): it changes every slot's LEVEL at once.
  - **REVERT:** the whole cartridge back as saved, every slot and its name, after a question (`REVERT
    CARTRIDGE 01 TO SAVED?`, OCT+ `REVERT`, OCT- `KEEP`; then `REVERTED`); listed only while the cartridge
    has something unsaved.
  - NEW and CLONE take the smallest free number, `00` up (`F0`-`FF` are the built-in instruments',
    [../../files/instruments.md](../../files/instruments.md#built-in-instruments)).
  - MOVE floats it to another free number ([../dialogs.md](../dialogs.md)).
  - **A sampler cartridge** clones, moves and is deleted as any cartridge: its instruments only. Its samples are
    records of their own ([../../files/instruments.md](../../files/instruments.md#samples)): a clone's instruments
    name the same samples, a MOVE leaves them where they are, and a DELETE leaves them for CLEAN UP.
  - **On a built-in cartridge** the menu is NEW, CLONE, RENAME, AUTO LEVEL ALL, and once changed BACK TO
    BUILT-IN (its change deleted, the firmware's own back: `CARTRIDGE F0 BUILT-IN AGAIN`).

**GLO: the cartridge's properties:** its NAME, with its type and the slots used shown beside it.

**Sampler cartridges:**
- **What they hold:** up to 64 sample instruments, each naming a sample by its number. The samples are records of
  their own, numbered `00`-`FF` apart from the cartridges, named by any sampler's instruments; they come from the PC
  (the device does not record) and are listed in [THE SAMPLES list](#the-samples), never among the cartridges.
- **The SEL menu in a sampler** adds to the slots' menu (the SEL menu, above):
  - **REPLACE SAMPLE** on an instrument, browsing and in edit mode, after PASTE: THE SAMPLES list opens, the
    instrument's sample highlighted; ENV picks one, and the instrument plays it as a new one would, every value back
    to a new instrument's (START, END, LOOP OFF, BASE NOTE, TUNE, VOLUME, LEVEL, the envelope:
    [../../files/instruments.md](../../files/instruments.md#sample-instruments)), its name following when it was the
    old sample's; HOME leaves it as it was. It acts on one slot: with slots marked it is not in the menu.
  - **INIT** on a used slot keeps its sample and sets every other value back to a new instrument's, named after its
    sample, as REPLACE SAMPLE with its own sample does (the other engines' INIT names it `INIT`). Over marked slots,
    INIT does so on each used one and leaves the empty ones empty: an instrument needs a sample.
  - **CREATE** on an empty slot, in place of INIT: THE SAMPLES list opens; ENV picks one, and a new instrument there
    plays it whole, named after it; HOME makes nothing.
  - **DELETE** in edit mode's menu too, on a sample instrument: it leaves edit mode keeping the changes, as EDIT
    does, then deletes as DELETE does, with its question when cells use the instrument; answered so that nothing is
    deleted (HOME), the instrument stays, browsed, its changes kept unsaved.
- **EDIT on an empty sampler slot** opens CREATE's pick, in place of its toast `EMPTY SLOT: SEL, INIT`; once a
  sample is picked, edit mode opens on the new instrument. The empty slot's line reads `EMPTY SLOT: SEL MENU, CREATE
  OR PASTE` in a sampler.
- **With no sample on the FM-1,** REPLACE SAMPLE, CREATE, EDIT on an empty sampler slot and ENV on SAMPLE say `NO
  SAMPLES ON THE FM-1` and open nothing.
- **Deleting a sample** (THE SAMPLES list, SEL → DELETE) is the only change made to a sample on the FM-1. It warns
  first when instruments use it, counting them in every sampler (`DELETE SAMPLE? 3 INSTRUMENTS USE IT AND ARE
  DELETED TOO`), and deletes them with it. Deleting an instrument never deletes its sample; CLEAN UP removes the
  samples no instrument names.

**FUSION cartridges** (a new type):
- **What a slot holds:** a reference to a pattern of the shared pool (`00`-`FD`; never `FE`, the
  EDITOR's own), as a song position does.
  - **EDIT on a FUSION instrument** opens its edit page, as on every type: PATTERN, NAME and LEVEL,
    with a map of the pattern above them. On PATTERN:
    - **ENV** opens the pattern list; ENV again makes the highlighted pattern the slot's. In it the
      keys play the highlighted pattern as this slot would, a row a key, so patterns can be tried
      before one is picked;
    - **K1** steps through the patterns there are;
    - **SEL** opens the pattern in the EDITOR, locked (below), at once (the EDITOR's warning first when
      its own pattern holds anything: [editor.md](editor.md#the-editors-own-pattern)).
  - **EDIT on an empty FUSION slot** makes a new pattern of 128 rows and 4 tracks (the next free
    number) and opens it. Its EDIT STEP is 0: writing a note keeps the cursor on the key's row.
    - **First it asks:** `FILL IT WITH 01:00 EL PIANO?`, offering the last instrument picked that is
      not a FUSION slot. **K1** offers another cartridge's (its first instrument), **SELECT** the next
      or previous instrument, and the keys play the one offered. **OCT+ (FILL)** writes that
      instrument's own note on each row `0C`-`7F`, on track `00` (`C-4` on row `3C`), so the slot
      plays as that instrument from the start, ready for layers; **OCT- (EMPTY)** leaves the pattern
      empty; HOME makes nothing.
  - **INIT** empties the slot: it refers to nothing and is silent.
- **Which row plays:** row *r* plays for MIDI note *r*. `C-4` (note 60) is row `3C`.
  - The keys at OCT 0 play rows `35` (F-3) to `4F` (G-5).
  - Rows below and above are notes too, reached with OCT and from song patterns. A pattern of 128
    rows (`00`-`7F`) covers every note.
- **The rows as a workspace:**
  - Any row can hold a row kept for later, and SEL → MOVE or SWAP brings it to the note where it
    should play.
  - A kept row is still a note, so it sounds if OCT or a song pattern reaches it.
- **How it plays:** a key plays every cell of its row; letting go of the key releases them.
- **Voices** are shared. Rows with many notes take voices from other sounds; keeping that in hand is
  up to the musician.
- **SEL on PATTERN** on its edit page opens the EDITOR on the pattern, locked ([editor.md](editor.md)):
  - EDIT stays lit;
  - the screen keys and FX tap are refused, and EDIT flashes;
  - **with REC off the keys play this FUSION**, as it is being written, whatever the current
    instrument; the rows they play light up (green) in the grid. With REC on they write, with the
    current instrument, as everywhere;
  - HOME tap returns to the edit page.
- **The same pattern** can also be edited from the EDITOR in the usual way, unlocked.
- **No nesting:** a FUSION instrument can be written anywhere, a FUSION's own pattern included, but
  when a FUSION plays its row, a cell naming a FUSION (itself or another) is silent. So a FUSION
  never plays more than its own row's cells: no chains, no loops.
- **Drum kits:** a FUSION instrument can be a kit, with a different sound on each key.

**PLAY** pauses and resumes what played last ([../transport.md](../transport.md)).

**The LEDs** ([../leds.md](../leds.md)): EDIT lit in edit mode; ARP lit while the arpeggiator is on; SEQ dim in
edit mode when the instrument has more than one page; SEL lit while slots are marked or an instrument
floats.

## FAVORITES

A FAVORITES cartridge ([../../files/instruments.md](../../files/instruments.md#favorites)) collects instruments from
any cartridges as bookmarks: each slot names an instrument, `CARTRIDGE:SLOT`. The built-in one is `FB`, named
`FAVORITES`, empty at first; the user's own are made with NEW. Settled with
[the proposal](../../proposals/202610311523-favorites.md).

**The current place and the current instrument:** with a FAVORITES cartridge current, the cursor's place is a
favourite (`FB:07`) and the current instrument is that favourite's source (`0B:05`). The keys play the source; REC,
RECORD LIVE, LOOPS, a cell written in the EDITOR and the footer's left all take the source's number. On an empty or
a missing favourite the current instrument stays the last source picked: the keys go on playing it and REC writes
it. When the favourite at the cursor changes (EDIT, PASTE, REVERT, a reference rewritten), the current instrument
follows it, unless it became empty or missing. Nothing ever takes `FB:07` as an instrument.

**Browsing:**

```
INSTRUMENTS                          1/8
----------------------------------------
FB FAVORITES     FAVORITES 7/128
 00 0B:05 BRASS        08 ----------
 01 F0:03 E.PIANO      09 ----------
 02 F8:12 STRINGS 2    0A ----------
 03 01:00 KICK         0B ----------
 04 0C:07 MISSING      0C ----------
 05 F9:21 COIN         0D ----------
 06 F4:02 CHOIR        0E ----------
 07 ----------         0F ----------
----------------------------------------
...      the source instrument, in detail, as its own cartridge shows it
0B:05 BRASS JUNO            FB FAVORITES
```

| Element | x, y | Font | Shows |
|---|---|---|---|
| the cartridge line | 0, 17 | S | `FB FAVORITES`, the type `FAVORITES`, used / size (`7/128`), as any cartridge's; drawn in T when longer than its 30 characters |
| slot *k*'s index | x + 2, y (the slots' grid as any cartridge's) | T | `00`-`7F` |
| its source | x + 20, y | T | `0B:05` |
| its source's name | x + 56, y | T | the name, 10 characters, whole; `MISSING`; `----------` when empty |

- **Colours:** the index dark grey, the source light grey, the name in the source engine's colour, the cursor's
  line white; a missing one's source and `MISSING` dark grey; an empty slot as in any cartridge. FAVORITES' own
  colour is light grey, the colour of no engine ([../colours.md](../colours.md)): its type in the cartridge lists,
  the cartridge line, the footer's right.
- **Missing:** a source that is gone (its cartridge deleted, its slot emptied, or its number now a FAVORITES
  cartridge's) plays nothing; the detail below is empty.
- **The detail below** shows the source instrument as its own cartridge shows it.
- **The footer:** its left names the current instrument as on every screen (`0B:05 BRASS JUNO`), its right the
  cartridge (`FB FAVORITES`). **PRESETS** steps through the favourites, skipping empty and missing ones.
- **An empty slot:** `EMPTY SLOT: EDIT PICKS AN INSTRUMENT`.

**EDIT edits the favourite,** which instrument the slot names, never the instrument itself (that is edited in its
own cartridge):

```
INSTRUMENTS EDIT
----------------------------------------
FAVORITE FB:07
SOURCE 0B:05 BRASS
K1: CARTRIDGE  PRESETS: INSTRUMENT

...      the picked instrument, in detail, as its own cartridge shows it
----------------------------------------
0B:05 BRASS JUNO            FB FAVORITES
```

- The title `FAVORITE FB:07` cyan at 4, 20 (T), `SOURCE` dark grey and the source as the list shows it at 4, 33,
  the hint at 4, 46 as INSTRUMENTS' hints are drawn (dark grey); the picked instrument's detail from y 100, as
  browsing shows it.
- **The current instrument** while editing is the one picked: the keys play it (ARP too), and the footer names it;
  EDIT keeps it, HOME drops it and the favourite's source is current again.
- **K1** turns the source's cartridge over every cartridge with an instrument (FAVORITES cartridges and empty ones
  left out), wrapping, at its first instrument; from a missing source it starts at the source's number if that
  cartridge is there, else at the first. While it turns, the cartridge list shows over the screen
  ([../dialogs.md](../dialogs.md#the-cartridge-list)), titled `CARTRIDGES`, listing the cartridges K1 reaches, the
  picked one highlighted, closing a second after the last click as PRESETS' instrument list does; it shows always,
  PRESETS LIST ON or OFF.
- **PRESETS** and **SELECT** step through that cartridge's instruments, from its first when the source is missing,
  PRESETS' instrument list showing as they turn while SETTINGS' PRESETS LIST is ON.
- **ENV** opens the same cartridge list as K1's; ENV again picks the highlighted one.
- **SEL + PRESETS** turns the FAVORITES cartridges only, editing going on in the one it lands on (MIDI's bank select
  the same: another cartridge there does nothing).
- **EDIT** again keeps the change, **HOME** drops it, as in every editor. EDIT on an empty slot starts here, at the
  current instrument (`EMPTY SLOT: NOTHING TO EDIT` is not shown in a FAVORITES cartridge).
- ALGORITHM and LFO do nothing here, GLO shows the cartridge's properties as while browsing, and SEL opens no menu.

**ADD TO FAVORITES** (the SEL menu on a slot that holds an instrument, [above](#controls)):
- Listed only while a FAVORITES cartridge exists.
- With one FAVORITES cartridge it is an action and adds there at once. With several it is a group of them
  ([../dialogs.md](../dialogs.md#levels)): titled `ADD TO FAVORITES`, BACK, then `FB FAVORITES` and the user's own in
  the lists' order, the first six (the menu's room; a later one is filled from its own edit mode), the highlight on the last one used, else on `FB FAVORITES`.
- It takes the first empty slot; marked slots each take one, in order, empty ones and those already there skipped.
  Toasts: `ADDED TO FB:07` (the first one added); `ALREADY IN FB:07` (nothing to add: the first one already there);
  `FAVORITES FULL` (not enough empty slots: nothing added).

**The SEL menu in a FAVORITES cartridge,** `SLOT FB:07` / `SLOTS FB:00-05`: MARK, COPY, PASTE, MOVE, SWAP, REMOVE,
NEW CARTRIDGE, REVERT (only while the slot has something unsaved).
- **REMOVE** empties the slot, or every marked one: the favourite goes, its source stays; no question.
- **COPY** copies references, 4 at most; **PASTE** writes them as they are, into a FAVORITES cartridge only (a
  favourite pasted twice is allowed). A FAVORITES copy pasted elsewhere, and an instrument's copy pasted into
  FAVORITES, are refused as PASTE refuses a copy of another type: `COPIED FROM FAVORITES: NOT HERE`, `COPIED FROM
  DX7: NOT HERE`.
- **MOVE and SWAP** as in any cartridge ([../dialogs.md](../dialogs.md#move)); nothing refers to a favourite's
  place, so they ask nothing.
- No INIT, RENAME or AUTO LEVEL: a favourite has no sound or name of its own.

**The built-in `FB`** is a changed built-in once something is added and saved ([../saving.md](../saving.md#fws)):
its ID no longer brown; BACK TO BUILT-IN empties it again; emptied by REMOVE, it equals the firmware's own and SAVE
deletes its record. It never moves; CLONE makes a copy at a user number.

**Reaching it: SEL + GLO,** on every screen ([../controls.md](../controls.md)): SEL held, then GLO pressed, acting
at GLO's press, switches the current cartridge to the built-in FAVORITES, at the favourite last used there (the
first one after a start). With the built-in FAVORITES current, however it became current, SEL + GLO goes back to
the place it held before: the cartridge and the slot, and so the instrument. GLO, away from SEL on the panel, is
easy to press with it; SEL's hold layer says it (`GLO: FAVORITES AND BACK`, [../dialogs.md](../dialogs.md#hold-layers)).
- Only SEL held first combines; SEL tapped first and then GLO is SEL's tap, then GLO's own job; GLO alone acts at
  once, as ever.
- **Where:** wherever SEL + PRESETS works (not on DJ, nor over a question, a name being typed or an overlay), and
  not in an editor's edit mode (INSTRUMENTS' EDIT, a favourite's edit): there it is refused, the button blinking.
  The header names the cartridge as SEL + PRESETS does.
- With no FAVORITES cartridge at `FB` it goes to the first FAVORITES cartridge in the lists' order. With no
  favourite in any FAVORITES cartridge it does not switch: `NO FAVORITES YET`.

**A favourite once: GLO + PRESETS,** on every screen but INSTRUMENTS ([../controls.md](../controls.md)): GLO held, then
PRESETS turned, picks from the FAVORITES cartridge SEL + GLO goes to, without switching to it: the favourite's source
becomes the current instrument (the keys play it, ARP too, the EDITOR writes it), and the current cartridge and slot
stay as they were.
- **A click** steps through that cartridge's favourites, empty and missing ones skipped, wrapping, from the favourite
  last used there (SEL + GLO's, shared both ways); with none used yet, the first click takes the first one (turned
  back, the last).
- **The list:** the favourites it steps through show over the screen while GLO is held, titled `FAVORITES`, laid out
  as PRESETS' instrument list shows a FAVORITES cartridge ([../dialogs.md](../dialogs.md#presets-instrument-list)),
  the picked one highlighted; it shows with SETTINGS' PRESETS LIST OFF too, and closes when GLO is let go. The header
  names the source as PRESETS does (`PRESETS F0:02 BRT PIANO`); the footer's left names it, its right the cartridge as
  before.
- **Once:** PRESETS alone, after, goes on from the current cartridge's slot as it stood, so the favourite is gone at
  its first click; a change of the favourite (EDIT, PASTE, REVERT) leaves the instrument alone.
- **GLO's tap does not happen** once PRESETS has turned with it held: let go, nothing opens. GLO held alone past the
  hold delay shows its layer, `PRESETS: A FAVORITE, ONCE` ([../dialogs.md](../dialogs.md#hold-layers)), and its let-go
  then does nothing, as every layer's; the layer goes while the list shows. GLO tapped still acts at once.
- **Where:** wherever SEL + GLO works (not on DJ, nor over a question, a name being typed or an overlay), and not on
  INSTRUMENTS, where the cursor is the current instrument: there it is refused, GLO blinking. With no favourite in that
  cartridge: `NO FAVORITES YET`, nothing changed. PRESETS turned with GLO held after another control closed GLO's
  combination (SEL + GLO's GLO included) turns the instruments as ever.

**Elsewhere:**
- **An empty FAVORITES cartridge picked** from a list has its cursor on its first slot and keeps the current
  instrument; on LOOPS, as any empty cartridge there: `CARTRIDGE FB IS EMPTY`.
- **PRESETS on every screen,** with a FAVORITES cartridge current, steps through its favourites; the header names
  the source, and PRESETS' instrument list shows the favourites it steps through as INSTRUMENTS lists them (`07
  0B:05 BRASS`; empty and missing ones left out).
- **The EDITOR's pipette** takes a cell's instrument, the source's number as the cell holds it; a cell naming a
  FAVORITES cartridge's number names no instrument: `NO INSTRUMENT HERE`.

## Layout

The frame (the header, its 1-px line at y 16, the footer from y 227) is [../frame.md](../frame.md)'s;
the body is y 17-226. Fonts: T 6 × 12, S 8 × 16.

### Browsing

```
INSTRUMENTS 1/2                             the frame's header
01 KEYS/ORGANS DX7 32/32                    S, 16 px
|00 EL PIANO        |08 HONKY PNO
 01 TINE PIANO       09 AC PIANO
 02 BRT PIANO        0A BRT AC PNO
 03 EL GRAND         0B EL GRAND 2
 04 EL PIANO 1       0C HONKY 2
 05 EL PIANO 3       0D EL PIANO 2
 06 GENTLE PNO       0E EL PIANO 4
 07 GRAND PNO        0F TINE EP
----------------------------------------
ALG 04  FB 0  LFO 22  LEVEL 5.5 DB          the details (DX7, below)
OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4
 1  1.00 00 63 63 63 60 63 63 63 00
 2  1.00 00 63 63 63 60 63 63 63 00
 3  1.00 00 63 63 63 63 63 63 63 00
 4  1.00 00 63 63 63 63 63 63 63 00
 5  1.00 63 63 21 1B 3A 63 43 00 00
 6  1.00 52 63 28 16 3A 63 4B 00 00
01:00 EL PIANO DX7        01 KEYS/ORGANS   the frame's footer
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the header's page | (the frame) | | S | the page of 16 slots, `1/2`; hidden with 16 slots or fewer |
| the cartridge line | 0, 17 | 240 × 16 | S | `ID `, the name, the type (`DX7`, `JUNO-106`, `TB-303`, `CHIP`, `SAMPLER`, `FUSION`), used / size (`32/32`) |
| slot *k* of the page (0-15) | x = 0 for *k* < 8, else 120; y = 34 + 12 × (*k* mod 8) | 119 × 12 | | |
| its index | x + 2, y | 2 characters | T | `00`-`FF` |
| its name | x + 20, y | 10 characters | T | the name; `----------` when empty |
| the cursor | x, y - 1 | 119 × 12 | | a box |
| a marked slot | x, y - 1 | 119 × 12 | | a filled band |
| the line | 0, 130 | 240 × 1 | | |
| the details | 0, 133 | 240 × 93 | T | the current instrument (below) |

Colours: the slots' names in the cartridge's type colour, their indexes dark grey; the cursor's slot
white in a white box (yellow while an instrument floats); marked slots on dark magenta. The cartridge
line: its ID white, its name light grey, its type in its colour, the count dark grey. The line dark
grey.

### Browsing: the details of each type

All from y = 133, 93 px tall.

```
DX7        ALG 04  FB 3  LFO 23  LEVEL 0 DB
           OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4
            1  1.00 61 63 22 1C 3E 63 50 00 00     6 rows of 11 px

JUNO-106   |  |  |  |  |  |  |  |  |  |  |  |  =  |  |  |  |
           |  |  |  |  |  =  |  |  |  |  |  |  |  |  |  |  |
           |  |  |  |  |  |  |  =  |  =  |  |  |  |  =  |  |
           =  |  |  |  |  |  |  |  |  |  |  |  |  |  |  =  =
           |  =  =  =  =  |  =  |  |  |  =  |  |  =  |  |  |
           RTDLLFPWNSSBHPFQRSENLFKBLV A D S R
           16' SAW CHORUS OFF ENV

TB-303      ( / )   ( \ )   ( - )   ( | )   ( \ )   ( / )
             WAVE   CUTOFF   RESO  ENVMOD   DECAY  ACCENT
              00      2D      64      46      2D      50

CHIP       PULSE 50%  CLOCK BRK
            __    __    __        VOL --
           |  |  |  |  |  |       ARP --
           |  |  |  |  |  |       PIT --                       its steps; the tables small
           |  |__|  |__|  |__     VIB ____|'-.-'-.-'
                                  DTY |||||_-_-_-_-_-
                                  NSE --
           SWEEP OFF  REL 0  60 HZ  LEVEL 2.5 DB

SAMPLE 23 KICK  0.50 S  22050 HZ
           ||||||||||||||||||||||||||||||||||||||||       the waveform
           LOOP OFF  BASE C-4  TUNE 00

FUSION     PATTERN D0  2 TRACKS, 128 ROWS
                            [|||||  ]                     the map
           00               C-4                    7F
```

| Type | Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|---|
| DX7 | the summary | 0, 133 | | T | `ALG ` the algorithm (`00`-`1F`), `  FB ` the feedback, `  LFO ` its speed, `  LEVEL ` the level in dB |
| | the column names | 0, 146 | | T | `OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4` |
| | operator *i* (0-5) | y = 158 + 11 × *i* | | T | its number at x 0 (` 1`), its frequency at x 18 (5 characters: a ratio `1.00`, a fixed frequency `1000H`), its level at x 54, its 4 rates at x 72, its 4 levels at x 144 (hex) |
| Juno-106 | slider *i* (0-16) | x = 2 + 14 × *i*; the track at x + 4, 135 | track 2 × 63; cap 8 × 4 | | the value: the cap at y 135 + round((1 - v / 127) × 59), from x + 1 |
| | the slider names | x (x + 2 for one letter), 201 | | T | `RT DL LF PW NS SB HP FQ RS EN LF KB LV A D S R`: the LFO's RATE and DELAY, the DCO's LFO, PWM, NOISE, SUB, the HPF, the VCF's FREQ, RES, ENV, LFO, KYBD, the VCA's LEVEL, the envelope's A, D, S, R |
| | the switches | 0, 214 | 40 characters | T | the RANGE, `SAW ` and `PULSE ` when on, `CHORUS ` and its setting, the VCA's mode (`16' SAW CHORUS OFF ENV`) |
| TB-303 | knob *i* (0-5) | centre 22 + 39 × *i*, 159; radius 14 | | | a circle and a pointer, from -135° (0) to +135° (127) |
| | its name | centred in 40 px about the knob, 176 | | T | `WAVE`, `CUTOFF`, `RESO`, `ENVMOD`, `DECAY`, `ACCENT` |
| | its value | centred, 188 | | T | hex |
| CHIP | the oscillator | 0, 133 | | T | its name (`PULSE`, `TRIANGLE`, `WAVE`, `BUZZ`, `NOISE`) and its setting (`50%`; the wave's name, `ORGAN`; `SAW DN 16`; none for TRIANGLE and NOISE), then `CLOCK` and the clock (`BRK`) |
| | its steps | 0, 147 | 114 × 62 | | as a line over a dark grey floor: PULSE three periods of its duty (24 steps of the clock's duty pattern), TRIANGLE and WAVE two periods of their 32 steps, BUZZ two rounds of its envelope, NOISE 64 steps of its long register from 1 |
| | the tables | 124, 145 + 11 × *r* | | T | the rows `VOL`, `ARP`, `PIT`, `VIB`, `DTY`, `NSE`; at x 146 the table drawn small (94 × 10: a bar a step, 8 steps at least, a yellow line where it loops; ARP and PITCH up and down from a dotted middle line), the vibrato its cycle over 48 frames after its delay (a yellow line where the delay ends), or `--` |
| | the bottom line | 0, 214 | | T | `SWEEP` and its value (`OFF`, `DOWN`, `UP`), `REL` and the RELEASE (hex, one digit), the FRAMES (`60 HZ`), `LEVEL` and the LEVEL (`2.5 DB`, a sign only below 0) |
| sample instrument | the sample | 0, 133 | | T | `SAMPLE `, its number, its name, then `  0.50 S  22050 HZ`: its length and rate |
| | the waveform | 0, 149 | 240 × 59 | | the whole sample, with the markers: START and END, and LOOP START and LOOP END with a loop, and a yellow line where each voice playing the sample is, moving as it plays |
| | the settings | 0, 212 | | T | `LOOP OFF  BASE C-4  TUNE 00` (LOOP `OFF`, `FORWARD` or `PING-PONG`) |
| | a missing sample | 0, 153 | | T | `THE SAMPLE IS MISSING`, in place of the rest (the instrument names a number with no sample, [../../files/references.md](../../files/references.md#a-reference-to-nothing)) |
| FUSION | the pattern | 0, 133 | | T | `PATTERN ` its number, `  2 TRACKS, 128 ROWS` |
| | the map | 0, 149 | 240 × 63 | | rows `00`-`7F` across, a track a band of *h* = max(2, min(12, floor(47 / tracks))) px |
| | the keys' rows | x = round(k0 / 128 × 240), 149 | 51 × tracks × *h* | | the rows the keys play at this OCT (k0 = `35` + 12 × OCT), a filled band |
| | a note | x = floor(row / 128 × 240), 149 + track × *h* | 2 × (*h* - 1) | | a mark |
| | C-4 | x 113, 147 | 1 × (tracks × *h* + 4) | | a line; `C-4` under it at x 105, y 149 + tracks × *h* + 3 |
| | the ends | 0 and 228, y 149 + tracks × *h* + 3 | | T | `00`, `7F` |
| | no pattern | 0, 153 | | T | `NO PATTERN: EDIT MAKES ONE` |
| an empty slot | | 4, 163 | | T | `EMPTY SLOT: SEL MENU, INIT OR PASTE` |

Colours: labels (`ALG`, `FB`, `LFO`, `LEVEL`, `PATTERN`, the TB-303's and the Juno-106's names) cyan,
values white. DX7: the column names dark grey; a carrier's number in the DX7's colour, a modulator's
light grey; the levels brown (an operator at 0 dark grey, its frequency too); rates and levels light
grey. Juno-106: the tracks dark grey; the caps by section: the LFO light blue, the DCO light cyan, the
HPF cyan, the VCF light red, the VCA brown, the envelope yellow; the switches light grey. TB-303: the
circles dark grey, the pointers in the TB-303's colour. CHIP: the oscillator's and the clock's names white, the
steps and the tables in CHIP's colour, a loop's line yellow, `--` dark grey; the bottom line's values white. Sample: the name in the sampler's colour, the
rest light grey; the waveform brown, START and END white, the loop's markers green, each voice playing
the sample a yellow line where it plays, moving as it plays (several voices, several lines); a missing sample's
text grey, as every reference to nothing ([../colours.md](../colours.md)). FUSION: the keys' band dark blue, each note in its instrument's colour, the C-4 line and label
white, `00` and `7F` dark grey, the counts light grey. An empty slot's line dark grey.

### Marking, MOVE and SWAP

Marking (SEL, MARK; or SEL held + SELECT): the slots from the mark to the cursor are filled dark magenta; SEL is lit;
the menu's title says the range (`SLOTS 00-05`).

MOVE or SWAP: the instrument floats. The cursor's box turns yellow and goes where SELECT (ALGORITHM by
8) takes it; that slot shows the floating instrument, and the slot it came from shows what would take
its place: empty for MOVE, the other instrument for SWAP. MOVE passes over used slots. PRESETS does nothing
while it floats. SEL puts it down (after the question about references, [../saving.md](../saving.md)); HOME
takes it back.

```
01 KEYS/ORGANS DX7 32/32                    S, 16 px
 00 BRT PIANO        08 HONKY PNO           SWAP: the place it came from shows what would take it
 01 TINE PIANO       09 AC PIANO
|02 EL PIANO        |0A BRT AC PNO          yellow: EL PIANO afloat over 02
 03 EL GRAND         0B EL GRAND 2
```

The cartridge is full, so this is SWAP; a MOVE would show `----------` at 00 and pass over the used
slots to an empty one.

### Edit mode: the patches

The picture above (y 17-116), the parameters below (from y 117). Shown for a DX7:

```
INSTRUMENTS EDIT                            the header: EDIT white, no page in edit mode
                     OP1 ENVELOPE
                      ___
          +-+       |/   \___
 [2] [4] [6]        |        \
  |   |   |         |         \
 [1] [3] [5]        |          \______
 -----------
----------------------------------------
OPERATORS 1/4  ALGORITHM: PAGES
K1-K4     LEVEL  COARSE  FINE   DETUNE
|OP1       00     01      00     00      |
 OP2       00     01      00     00
 OP3       00     01      00     00
 OP4       00     01      00     00
 OP5       63     01      00     00
 OP6       52     01      00     +02
01:00 EL PIANO DX7        01 KEYS/ORGANS
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| DX7: the algorithm | 0, 19 | 120 × 92 | T | the operators as boxes of 15 × 13 with their numbers: carriers along the bottom (y 99), each modulator a row above what it modulates (rows of at most 22 px, columns of at most 26); links between them; the feedback as a loop at the top right of its box; under the carriers, a line (the output) at y 110 |
| DX7: the envelope's name | 126, 19 | | T | `OP1 ENVELOPE`: the operator of the group at the cursor (OP1 where the group has none) |
| DX7: the envelope | 126, 35 | 110 × 70 | | its 4 rates and levels, with a line where the key is let go |
| Juno-106: names | 0, 19 and 124, 19 | | T | `ENVELOPE`, `FILTER` |
| Juno-106: the envelope | 0, 35 | 114 × 70 | | A, D, S, R |
| Juno-106: the filter | 124, 35 | 114 × 70 | | the response for FREQ and RES, with the 0 dB line at y 58 |
| TB-303: names | 0, 19 and 124, 19 | | T | `ENVELOPE`, and `ACC` right-aligned to 114; `FILTER`, and the wave's name (`SAW` under WAVE 16, `SQUARE` over 111, else `SAW+SQ`) right-aligned to 220 |
| TB-303: the wave | 224, 21 | 14 × 8 | | one cycle of it: the saw and the square mixed by WAVE |
| TB-303: the envelope | 0, 35 | 114 × 66 | | the cutoff over one note, as the engine moves it ([below](#the-tb-303s-pictures)): time across from 0 to the span (`0`, `604 MS` under it at y 103, the span right-aligned to 114), 200 Hz to 10 kHz up (log); filled under the curve; over it CUTOFF, where it comes to rest, a dotted line (every third pixel), then an accented note's curve dotted (every other column) |
| TB-303: the filter | 124, 35 | 114 × 66 | | the low-pass's response over 20 Hz-20 kHz (log), +18 dB at the top to −36 at the bottom: at the cutoff where it rests, filled under the curve, its cutoff a line; at the note's top, dotted (every other column), its cutoff a dotted line; the 0 dB line |
| FUSION: the pattern | 0, 19 | | T | `PATTERN D0  2 TRACKS, 128 ROWS` |
| FUSION: the map | 0, 35 | 240 × 78 | | as browsing's |
| the line | 0, 117 | 240 × 1 | | |
| the page | 0, 119 | | T | the page's name, `  1/4` (counted from 1), and `  ALGORITHM: PAGES` when there are more pages |
| the knobs' row | 2, 132 | | T | `K1-K4` (`K1-K3`, `K1-K2`, `K1`: as many as the group has) at x 2, then each knob's name at x 60 + 45 × *k* (7 characters) |
| | 0, 132 | | T | on PATTERN: `SEL: EDIT IT  ENV: PICK  K1: OTHER`; on NAME: `SEL: TYPE A NAME` |
| group *i* | y = 147 + 12 × (*i* - top) | 240 × 12 | T | its name at x 2 (9 characters), its values at x 60 + 45 × *k* (7 characters; a longer one loses the space before its unit, `-12.5DB`); NAME's row the name at x 60 |
| | | | | 6 groups show; top = max(0, min(cursor - 3, groups - 6)) |
| the cursor | 0, y - 1 | 240 × 12 | | a box |

Colours: the picture's names cyan. DX7: a carrier boxed in the DX7's colour, a modulator light grey,
the selected operator filled dark red with its number white; links dark grey, the feedback yellow; the
envelope in the DX7's colour, its release line dark grey. Juno-106: both curves light cyan, the 0 dB
line dark grey. TB-303: the curves, the cutoff's line and the wave in the TB-303's colour, the fills its deep
shade; the accented note's curve and `ACC` yellow; the filter at the note's top and its line light grey; CUTOFF's
rest and the 0 dB line dark grey, the wave's name light grey, the time span dark grey. The line dark grey; the page's name cyan, its
count and the hint dark grey; `K1-K4` and the hints' keys yellow, their words light grey; the knobs'
names light grey, the last turned white. The group names cyan (the cursor's white, boxed white), the
values light grey (the last turned on the cursor's row white), the name in the type's colour.

### The TB-303's pictures

Drawn from the knobs (0-127 each: *w* WAVE, *c* CUTOFF, *r* RESO, *e* ENV MOD, *d* DECAY, *a* ACCENT) by the
engine's own law (Open303's, measured), again as each turns:

```
ENVELOPE          ACC   FILTER       SAW /|
|\  .                         __/\           the accented note dotted; the filter at the note's top dotted
| \   .              ________/    \
|  \___ .    ....                  \___
0               604 MS
```

- **The cutoff over a note,** in octaves: log2 of 314 Hz × 7.624^(*c*/127), plus the envelope's sweep
  *S* × (*m* − *O*), where the envelope *m* falls from 1 as e^(−*t* / DECAY), DECAY 200 ms × 10^(*d*/127);
  *S* = (1 − *c*/127)(3.774 *e*/127 + 0.737) + (*c*/127)(4.195 *e*/127 + 0.864), *O* = 0.0483 *c*/127 + 0.2944.
  An accented note falls over 200 ms instead and adds *a*/127 × the envelope through a 15 ms RC (*A* = 1/15 ms,
  *B* = 1 / 200 ms: *A* / (*A* − *B*) × (e^(−*Bt*) − e^(−*At*))): a short rise at the start, the accent's
  "wow". Held between 200 Hz and 10 kHz. The span: 1.6 DECAYs, 0.3 s at least, 2.5 s at most; shown in ms under
  a second (`604 MS`), else in seconds to a tenth (`1.6 S`).
- **The filter:** a 24 dB-an-octave low-pass, the response of two 2-pole sections of Q 0.7 + 7 *r*/127
  (+18 dB at most, its peak at the cutoff), where it rests (the curve as *t* grows long) and at the note's top
  (*t* = 2 ms).
- **The wave:** a cycle of (1 − *w*/127) × the saw + *w*/127 × the square.

### Edit mode: CHIP

Four pages, laid out as the patches' (the picture y 17-116, the parameters from y 117): TONE, VOLUME, TABLES,
WAVE. The knobs' names stay at x 60 + 45 × *k*; CHIP's wider values have columns of their own (below).

```
INSTRUMENTS EDIT
WAVE        $1F5 -0.2   VOLUME          1.5 S
 __    __    __        |##########:
|  |  |  |  |  |       |##########:
|  |__|  |__|  |__     |##########:______
----------------------------------------
TONE  1/4  ALGORITHM: PAGES
K1-K2     OSC    DUTY
|OSC       PULSE   50%                   |
 NOISE     OFF     LONG      04    KEY OFF
 CLOCK     BRK      BRK      BRK     60 HZ
 SWEEP     OFF    02      02
 LEVEL     +2.5 DB
 NAME      BRK LEAD
F9:00 BRK LEAD CHIP       F9 CHIPTUNE
```

| Page | Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|---|
| TONE | the oscillator | 0, 19 | | T | `WAVE`; right-aligned to 114 the period register a C-4 lands on and how far it is from true, in cents (`$1F5 -0.2`; four hex digits past `FFF`), or `EXACT` on that clock, or the noise's `RATE 4` |
| | its steps | 0, 35 | 114 × 70 | | as browsing's |
| TONE, VOLUME | the volume | 124, 19 | | T | `VOLUME`; `1.5 S` right-aligned to 240 |
| | its picture | 124, 35 | 114 × 70 | | the level a frame over 1.5 s of a note let go after 1 s (VOL and its envelope, the VOL table, the RELEASE: their 16 steps multiplied, before the CURVE), filled; a dotted line where the key is let go |
| VOLUME | the envelope | 0, 19 | | T | `ENVELOPE` and, right-aligned to 114, its kind (`NONE`, `DOWN`, `UP`); or `PSG ENVELOPE` and its round's time (`46 MS`) |
| | its picture | 0, 35 | 114 × 70 | | the step envelope's level over a second, or four rounds of the PSG envelope's shape, as a line |
| TABLES | the row | 0, 19 | | T | the cursor's row (`VOL`, `ARP`, `PITCH`, `VIBRATO`, `DUTY`, `NOISE`); right-aligned to 240 its table's number, name and length, and where it loops (`01 SETTLE 11, 7 STEPS, LOOP 7`), or `NONE`; on VIBRATO `DELAY 24, 32 CENTS`, or `OFF` |
| | the table | 0, 33 | 240 × 72 | | over 48 frames at its SPEED, a bar a frame: past its end (its last step held, or its loop again) in the deep shade, a yellow line where it loops; ARP and PITCH up and down from a dotted middle line, the others up from a floor; VIBRATO flat through its delay, then its cycle, a yellow line where the delay ends; no table: the floor alone |
| | the scale | 0, 106 | | T | `0`, and `48 FRAMES` right-aligned to 240 |
| | each row's table | 172, the row's y | 66 × 10 | | small, as browsing's |
| WAVE | the title | 0, 19 | | T | `WAVE TABLE`; right-aligned to 240 the wave's number and name (`00 SAW`) |
| | the wave | 8 + 7 × *i*, bottom 83 | 5 × up to 48 | | its 32 steps as bars, 48 px for 15 |
| | the 16 waves | 30 × (*j* mod 8), 88 + 14 × (*j* div 8) | 30 × 14 | | each small (a step 1 px wide, 10 px for 15), the picked one boxed |
| | the note | 2, 159 | | T | with another OSC: `ONLY THE WAVE OSCILLATOR READS IT` |

**The columns:** the OSC row's values at x 60, 105, 150, 195 (8, 10, 7, 7 characters: `TRIANGLE`, a wave's
name); CLOCK's and PSG ENV's at 60, 110, 160, 210 (8, 8, 8, 5: `PSG 1.77`); the TABLES rows' TABLE at 60 (13
characters), SPEED at 156, the table small at 172; VIBRATO's DELAY, SPEED, DEPTH at 60, 86, 112; the WAVE page's
TABLE at 60 (13 characters: `0B DOUBLE SAW`).

Colours: the pictures' names cyan; the register, the table's description and the vibrato's light grey; `1.5 S`,
`0`, `48 FRAMES`, the envelope's kind, the round's time and `NONE` dark grey; the pictures in CHIP's colour, the
volume filled in its deep shade under its line, floors and the let-go line dark grey, loop and delay lines yellow;
the WAVE page's wave and its name dark grey while another OSC is picked; the picked small wave white, boxed white;
the note dark grey.

### Edit mode: a sample instrument

The parameters above (from y 17), the picture below (from y 100), the same on both pages.

```
INSTRUMENTS EDIT
SAMPLE 1/2  ALGORITHM: PAGES
K1-K3     SAMPLE START  END
|SAMPLE    00     0       F97            |
 LOOP      OFF    0       0      20 MS
 PITCH     C-4    00      40     -12.5DB
 NAME      KICK

|                         |              |
||  |  |                  |              |   the waveform, 72 px
||||||||||||||||||||||||||||||||||||||||||
||  |  |                  |              |
|\                        |              |   the envelope, 34 px
| \_______________________|. . . . .     |   (past END dotted: the ghost)
WHITE START/END  GREEN LOOP  YELLOW PLAY
01:00 KICK SAMPLE              01 DRUM KIT
```

The ENVELOPE page: its table, the same picture under it.

```
INSTRUMENTS EDIT
ENVELOPE 2/2  ALGORITHM: PAGES
K1-K4     R1     R2      R3     R4
|RATES     63     63      63     55      |
 LEVELS    63     63      63     00
          (the picture, as the SAMPLE page's)
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the line | 0, 17 | 240 × 1 | | (under the header's own) |
| the page | 0, 19 | | T | as the patches' |
| the knobs' row | 2, 32 | | T | as the patches' |
| group *i* | y = 47 + 12 × (*i* - top) | 240 × 12 | T | as the patches' |
| the waveform (both pages) | 0, 100 | 240 × 72 | | the whole sample with START and END, and the loop's markers |
| the envelope (both pages) | 0, 176 | 240 × 34 | | the envelope as heard over the region, on the waveform's x; the markers through it; its foot (gain 0) at y 209 |
| the key to the markers | 0, 213 | | T | `WHITE START/END  GREEN LOOP  YELLOW PLAY` |

Colours: as the patches'; the waveform brown, START and END white, the loop's markers green, the voices
playing it a yellow line each where they play (live); the markers' key dark grey; the envelope in the
sampler's colour, its loop passes heard while it still rises dark grey, its ghost past END dotted, its
foot dark grey.

### The SEL menu

A menu ([../dialogs.md](../dialogs.md)) at the top right of the body, over the screen:

```
              +----------------------+
              | SLOT 01:00           |
              |[MARK                ]|
              | COPY                 |
              | PASTE                |
              | MOVE                 |
              | SWAP                 |
              | DELETE               |
              | INIT                 |
              | RENAME               |
              | AUTO LEVEL           |
              | NEW CARTRIDGE        |
              | REVERT               |
              +----------------------+
```

ADD TO FAVORITES comes after MARK while a FAVORITES cartridge exists. Its title is
`SLOT 01:00`, or `SLOTS 00-05` with slots marked (then without MARK); in edit mode `SLOT 11:00` with the edit
mode's items (EDIT PATTERN first on a FUSION slot that has a pattern). REVERT is listed only while the slot
has something unsaved (with slots marked, never). The last item used is preselected.

### The cartridges (ENV)

```
 +--------------------------------------+
 | CARTRIDGES                           |
 | ID NAME         TYPE      SLOTS      |
 |  01 DRUM KIT     SAMPLER   010/064   |
 |  ...                                 |
 |  05 BASS         SAMPLER   004/064   |
 |  06 FUSION       FUSION    006/064   |
 |  F0 KEYS/ORGANS  DX7       032/032   |   built in, in FWS: the ID brown
 |  F1 BELLS/PLUCKS DX7       032/032   |
 |  ...                                 |
 |  F8 JUNO-106     JUNO-106  128/128   |
 |  F9 CHIPTUNE     CHIP      128/128   |
 | [FA TB-303       TB-303    064/128  ]|
 |     ALL SAMPLES  184K      023/256   |
 +--------------------------------------+
```

With the demo disk uploaded: the built-in FAVORITES `FB` first, then its sampler cartridges `01`-`05` and FUSION
`06` in the user area, then the built-in instruments `F0`-`FA`
([../../files/instruments.md](../../files/instruments.md#built-in-instruments)), then the samples' line; the list
scrolls, the line `FB FAVORITES    FAVORITES 000/128` above `01`.

**The samples' line,** the list's last (after `EMPTY` too when no cartridge is left), shown while the FM-1 holds a
sample: ENV on it opens [THE SAMPLES list](#the-samples) to look, listen and delete without an instrument; SEL on it
opens nothing (it is not a cartridge). In the list's columns:

| Element | x | Font | Shows | Colour |
|---|---|---|---|---|
| the ID column | 8 | T | nothing | |
| the name | 26 | T | `ALL SAMPLES` | yellow, as the samples' names (the cursor's white) |
| the type column | 110 | T | their size, `184K` (as STATUS writes sizes) | light grey |
| the slots column | 170 | T | their count of 256, `023/256` | light grey |

The cursor's box is any line's.

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a box |
| the title | 6, 22 | | T | `CARTRIDGES` |
| the column names | 8, 35 | | T | `ID NAME         TYPE      SLOTS` |
| cartridge *i* | y = 49 + 13 × *i* | | T | its ID at x 8, its name at x 26, its type at x 110, its slots used of all at x 170 (`032/032`, decimal) |
| the cursor | 4, y - 1 | 232 × 13 | | a box |

Colours: the panel's border light grey, the title cyan, the column names dark grey; IDs and names
light grey (the cursor's white), the ID of a built-in cartridge brown while unchanged
([../saving.md](../saving.md#fws)); each type in its colour, the counts light grey; the cursor's
box white, yellow while the cartridge floats (MOVE: it is listed under the number it would take).

Its SEL menu, `CARTRIDGE 01`: NEW, CLONE, MOVE, DELETE, RENAME, AUTO LEVEL ALL, REVERT (only while the
cartridge has something unsaved, here and in the built-in ones' menus). NEW is a group
([../dialogs.md](../dialogs.md#levels)), `CARTRIDGE 01 > NEW`: BACK, then the new cartridge's type: DX7,
JUNO-106, TB-303, CHIP, SAMPLER, FUSION, FAVORITES (named `MY FAVORITES`). On a built-in one, NEW, CLONE, RENAME,
AUTO LEVEL ALL, and once changed BACK TO BUILT-IN. On a FAVORITES cartridge AUTO LEVEL ALL is left out, and its MOVE
and DELETE ask nothing about references (none point at it).

### The samples

THE SAMPLES list holds every sample on the FM-1, by number ([../../files/instruments.md](../../files/instruments.md#samples)).

```
 +--------------------------------------+
 | SAMPLES (23)                         |
 |[00 KICK        0.50S  22050 ||||||| ]|
 |  01 SNARE       0.38S  22050 ||||||   |
 |  07 HAT         0.12S  11025 |||      |
 |  ...                                 |
 +--------------------------------------+
```

It opens three ways, titled the same:
- **ENV on a sample instrument's SAMPLE** (in edit mode): the instrument's sample highlighted (the first one when it
  names a missing sample); the keys play the highlighted sample through the instrument; ENV picks it (played whole:
  START at its start, END at its end, LOOP OFF); HOME leaves the instrument as it was.
- **REPLACE SAMPLE and CREATE** (a sampler's SEL menu, Sampler cartridges above): the keys play the highlighted sample as a
  new instrument would, since that is what ENV makes of it (whole, BASE NOTE C-4 at its own rate, LOOP OFF, a new
  instrument's envelope); ENV picks it, HOME makes nothing.
- **The cartridge list's samples' line** ([The cartridges](#the-cartridges-env)): the keys play the highlighted
  sample as a new instrument would; ENV or HOME go back to the line.

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a box |
| the title | 6, 22 | | T | `SAMPLES (23)`: how many the FM-1 holds |
| sample *i* | y = 37 + 13 × (*i* - top) | | T | its number at x 8, its name at x 24 (12 characters), its length at x 100 (`0.50S`), its rate in Hz at x 140 (`22050`); the cursor's row also its waveform at x 176 (56 × 10), unless it is `(DELETED)`; the others none (reading every row's audio at each redraw would take about 80 ms on the FM-1) |
| | | | | 14 show; top = max(0, min(cursor - 6, samples - 14)) |
| the cursor | 4, y - 1 | 232 × 13 | | a box |

Colours: the numbers dark grey; the names yellow (the cursor's white); the lengths light grey; the rates dark
grey; the waveform brown; a sample whose deletion waits for SAVE all grey, its name `(DELETED)`; the cursor's box
white.

Its SEL menu, `SAMPLE 03`: DELETE (not on one whose deletion waits, where SEL opens nothing). DELETE warns when
instruments use it, counting them in every sampler, and deletes them with it ([../../files/instruments.md](../../files/instruments.md#deleting));
**in edit mode** it first leaves edit mode keeping the changes, as EDIT does, so HOME never takes a confirmed
deletion back with the mode's other changes. After SAVE the sample leaves the list.

### The pattern list (ENV on PATTERN)

The pattern list of [../dialogs.md](../dialogs.md), titled `PATTERN FOR 11:00`; the keys play the
highlighted pattern as this FUSION would.

### The FUSION fill question

A question ([../dialogs.md](../dialogs.md)): 4 lines, the answers under OCT- and OCT+.

```
 +------------------------------------+
 |     A NEW PATTERN FOR THIS SLOT    |
 |    FILL IT WITH 01:00 EL PIANO?    |
 | ITS NOTE ON EACH ROW: THE SAME SOUND|
 |  K1, SELECT: ANOTHER  KEYS: HEAR IT |
 |                                    |
 |       OCT-              OCT+       |
 |      EMPTY              FILL       |  S, 16 px
 |                                    |
 |            HOME: CANCEL            |
 +------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, 67 | 224 × 110 | | a box |
| line *i* (0-3) | centred, 75 + 12 × *i* | | T | the question |
| the buttons | centred in 8-120 and 120-232, 127 | | T | `OCT-`, `OCT+` |
| the answers | centred under them, 140 | | S | `EMPTY`, `FILL` |
| the cancel | centred, 161 | | T | `HOME: CANCEL` |

Colours: the first line white, the others light grey; the buttons yellow, the answers white; `HOME`
yellow, `: CANCEL` light grey. The second line follows K1 and SELECT.

### Renaming

Text entry ([../dialogs.md](../dialogs.md)): an instrument's name has 10 characters, titled
`INSTRUMENT 01:00`; a cartridge's 12, titled `CARTRIDGE 01`. EDIT is done.

### The cartridge's properties (GLO)

The properties panel ([../dialogs.md](../dialogs.md)), titled `CARTRIDGE 01`: NAME (EDIT renames it),
TYPE, SLOTS USED (`32 OF 32`), RENAME WITH (`EDIT`); all but NAME fixed.

### AUTO LEVEL

A progress bar ([../dialogs.md](../dialogs.md)): `MEASURING EL PIANO`; AUTO LEVEL ALL
`MEASURING 32 SOUNDS`.

- **How:** each instrument is measured as LEVELs are defined
  ([../../files/instruments.md](../../files/instruments.md#level-and-the-engines-reference)), so it
  gives a demo disk's instrument back its own LEVEL: its note alone, apart from what plays, held a second
  of 1.2 s from a clean start, the loudest 300 ms of it brought to −20 dB, at most 30 dB either way, its
  peak kept under −6 dB, in half-dB steps. The note: C-4; a TB-303's C-2; a sample instrument's its
  BASE NOTE. A FUSION instrument: its layers as they start together (their parameter and tick commands
  aside), each at its own LEVEL and its cell's volume, the louder of C-3 and E-4.
- **The LEVEL set** is an edit (REVERT takes it back with the slot's other changes; AUTO LEVEL ALL's with
  the cartridge's, after asking first, as above); in edit mode HOME drops it with the rest. An instrument that makes no sound keeps its LEVEL
  (`NO SOUND TO MEASURE`).
- **One instrument a pass,** about 50 ms each on the FM-1: the sound goes on meanwhile (each block of the
  measurement is made with the audio's interrupt held back, a few tens of µs at a time).
- **The built-in ones are measured from their own menus** (AUTO LEVEL, a cartridge's AUTO LEVEL ALL) as
  any; SETTINGS → TOOLS → AUTO LEVEL ALL measures every cartridge in the user area and leaves the built-in
  ones out.

## Texts

**The header:** `INSTRUMENTS` (in the MAKE group's colour); the subpage `EDIT` (white) on the edit page;
the page `1/2` while browsing a cartridge of more than 16 slots.

**The footer:** on the right the cartridge, `01 KEYS/ORGANS`. **A knob turned,** its name and value in the
header's place for 1.2 s: `LEVEL +1.5 DB`, `RELEASE 33 MS`, `LOOP START 1F40`, `PATTERN D1`; LFO's reset adds
` (DEFAULT)`: `LEVEL 0 DB (DEFAULT)`; ARP held + SELECT: `RHYTHM #___`.

**On the screen:**
- the details' labels: `ALG`, `FB`, `LFO`, `LEVEL`, `OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4`; the
  Juno-106's slider names `RT DL LF PW NS SB HP FQ RS EN LF KB LV A D S R`; the TB-303's `WAVE`,
  `CUTOFF`, `RESO`, `ENVMOD`, `DECAY`, `ACCENT`; the sample's `LOOP`, `BASE`, `TUNE`, `S`, `HZ`;
  FUSION's `PATTERN`, `TRACKS`, `ROWS`, `C-4`, `00`, `7F`; CHIP's oscillators, `CLOCK` and the clocks, `VOL`,
  `ARP`, `PIT`, `VIB`, `DTY`, `NSE`, `SWEEP`, `REL`, `HZ`, `LEVEL`;
- `EMPTY SLOT: SEL MENU, INIT OR PASTE` (browsing an empty slot); in a sampler `EMPTY SLOT: SEL MENU, CREATE OR
  PASTE`;
- `EMPTY SLOT: NOTHING TO EDIT` (edit mode on an empty slot);
- `SAMPLE 23` (the sample instrument's page), `THE SAMPLE IS MISSING`;
- `NO PATTERN: EDIT MAKES ONE`;
- edit mode: the page names (`OPERATORS`, `ENVELOPES`, `PITCH / LFO`, `VOICE`, `DCO`, `FILTERS`,
  `ENVELOPE`, `KNOBS`, `TONE`, `VOLUME`, `TABLES`, `WAVE`, `SAMPLE`, `FUSION`), `ALGORITHM: PAGES`, the group
  and knob names of the parameters' table and their values (CHIP's: the oscillators, `12.5%` … `75%`, `SAW DN`,
  `SAW UP`, `TRI`, `16`, `32`, `ON`, `OFF`, `LONG`, `SHORT`, `KEY ON`, `KEY OFF`, the clocks, curves and filters,
  `NONE`, `60 HZ`, `50 HZ`, `DOWN`, `UP`, `PSG`, `X1`-`X4`, the 16 PSG shapes' pictures, the tables' and waves'
  names, [../../chip.md](../../chip.md)), `K1-K4`, `SEL: EDIT IT  ENV: PICK  K1: OTHER`, `SEL: TYPE A NAME`;
- the pictures: `OP1 ENVELOPE` … `OP6 ENVELOPE`, `ENVELOPE`, `FILTER`, the TB-303's `ACC`, `SAW`, `SQUARE`,
  `SAW+SQ`, `0`, `604 MS`, `1.6 S`, `WHITE START/END  GREEN LOOP  YELLOW PLAY`; CHIP's `WAVE`, `EXACT`, `RATE 4`,
  `VOLUME`, `1.5 S`, `PSG ENVELOPE`, `46 MS`, `VIBRATO`, `DELAY 24, 32 CENTS`, `7 STEPS`, `LOOP 7`, `NONE`,
  `48 FRAMES`, `WAVE TABLE`, `ONLY THE WAVE OSCILLATOR READS IT`.

**Menus:**
- the slots, `SLOT 01:00` / `SLOTS 00-05`: MARK (nothing marked), ADD TO FAVORITES, COPY, PASTE, REPLACE SAMPLE (a
  sample instrument, one slot), MOVE, SWAP, DELETE, INIT (CREATE on an empty sampler slot), RENAME, AUTO LEVEL, NEW
  CARTRIDGE, REVERT (one slot, with something unsaved);
- in edit mode, `SLOT 01:00`: EDIT PATTERN, ADD TO FAVORITES, COPY, PASTE, REPLACE SAMPLE (a sample instrument),
  INIT, RENAME, AUTO LEVEL, DELETE (a sample instrument), REVERT;
- in a FAVORITES cartridge, `SLOT FB:07` / `SLOTS FB:00-05`: MARK, COPY, PASTE, MOVE, SWAP, REMOVE, NEW
  CARTRIDGE, REVERT;
- ADD TO FAVORITES' group, `ADD TO FAVORITES`: BACK, `FB FAVORITES`, the user's own (`07 MY FAVORITES`);
- NEW CARTRIDGE's group, `NEW CARTRIDGE`: BACK, DX7, JUNO-106, TB-303, CHIP, SAMPLER, FUSION, FAVORITES;
- the cartridges, `CARTRIDGE 01`: NEW, CLONE, MOVE, DELETE, RENAME, AUTO LEVEL ALL (not in FAVORITES); built in:
  NEW, CLONE, RENAME, AUTO LEVEL ALL, BACK TO BUILT-IN (once changed); each with REVERT last while the
  cartridge has something unsaved;
- NEW's group, `CARTRIDGE 01 > NEW`: BACK, DX7, JUNO-106, TB-303, CHIP, SAMPLER, FUSION, FAVORITES;
- the samples, `SAMPLE 03`: DELETE.

FAVORITES' texts: the type `FAVORITES` (and the built-in's name), a new one's name `MY FAVORITES`, `MISSING`,
`EMPTY SLOT: EDIT PICKS AN INSTRUMENT`, `FAVORITE FB:07`, `SOURCE`, `K1: CARTRIDGE  PRESETS: INSTRUMENT`; toasts
`ADDED TO FB:07`, `ALREADY IN FB:07`, `FAVORITES FULL`, `NO FAVORITES YET`, `CARTRIDGE 11 MADE`, `COPIED FROM
FAVORITES: NOT HERE`, `COPIED FROM DX7: NOT HERE`.

BACK TO BUILT-IN's toast is [../saving.md](../saving.md#texts)'s.

**Lists and panels:** `CARTRIDGES`, `ID NAME         TYPE      SLOTS`, the samples' line `ALL SAMPLES`
(`184K`, `023/256`); `SAMPLES (23)`, `(DELETED)`;
`PATTERN FOR 11:00`; the properties `CARTRIDGE 01`: `NAME`, `TYPE`, `SLOTS USED` (`32 OF 32`),
`RENAME WITH` (`EDIT`); text entry `INSTRUMENT 01:00`, `CARTRIDGE 01`.

**Questions:**
- the FUSION fill: `A NEW PATTERN FOR THIS SLOT`, `FILL IT WITH 01:00 EL PIANO?`,
  `ITS NOTE ON EACH ROW: THE SAME SOUND`, `K1, SELECT: ANOTHER  KEYS: HEAR IT`; OCT+ `FILL`, OCT-
  `EMPTY`;
- references (MOVE, SWAP, DELETE of a used slot or cartridge, [../saving.md](../saving.md)):
  `01:00 IS USED 3 TIMES` / `CARTRIDGE 01 IS USED 40 TIMES`, `UPDATE REFERENCES?`; OCT+ `YES`, OCT-
  `NO`;
- REVERT: `REVERT SLOT 01:00 TO SAVED?`, `REVERT CARTRIDGE 01 TO SAVED?`; OCT+ `REVERT`, OCT- `KEEP`;
- AUTO LEVEL ALL: `AUTO LEVEL 32 SOUNDS?` (`1 SOUND?`), `EACH ONE'S LEVEL SET AGAIN`; OCT+ `LEVEL`, OCT-
  `KEEP`;
- deleting a used sample: `DELETE SAMPLE?`, `3 INSTRUMENTS USE IT AND ARE` (one: `1 INSTRUMENT USES
  IT AND IS`), `DELETED TOO` (counted in every sampler); OCT+ `DELETE`, OCT- `KEEP`. An unused one goes at once,
  toast `SAMPLE 03 DELETED`; in edit mode either first leaves edit mode, keeping the changes.

**Progress:** `MEASURING EL PIANO`, `MEASURING 32 SOUNDS` (`MEASURING 1 SOUND`), `UPDATING 3
REFERENCES`.

**Toasts:**

| Toast | When |
|---|---|
| `COPIED 3` | COPY: how many slots |
| `NOTHING COPIED` | PASTE before any COPY |
| `NO EMPTY SLOT: SWAP INSTEAD` | MOVE in a full cartridge |
| `SELECT MOVES IT, SEL PUTS IT DOWN` | MOVE or SWAP starts (a slot or a cartridge) |
| `NOT MOVED` | HOME while one floats |
| `REVERTED` | REVERT, after its question |
| `LEVEL SET TO -1.5 DB` | AUTO LEVEL done |
| `NO SOUND TO MEASURE` | AUTO LEVEL on an instrument that makes no sound (its LEVEL kept) |
| `LEVELS SET` | AUTO LEVEL ALL done |
| `CARTRIDGE 11 MADE` | NEW |
| `01 CLONED TO 11` | CLONE |
| `ALL CARTRIDGE NUMBERS USED` | NEW or CLONE with no number free |
| `ALL 240 PATTERNS USED` | EDIT on an empty FUSION slot with no pattern number free (`00`-`EF`) |
| `EMPTY SLOT: SEL, INIT` | EDIT on an empty slot (not FUSION, not a sampler: there it opens CREATE's pick) |
| `NO SAMPLES ON THE FM-1` | REPLACE SAMPLE, CREATE, EDIT on an empty sampler slot or ENV on SAMPLE with no sample on the FM-1 |
| `SAMPLE 03 DELETED` | DELETE of a sample no instrument uses |
| `ARP UP` / `ARP OFF` | ARP on (the pattern) / off; the others: [../controls.md](../controls.md) |
| `REFERENCES UPDATED, SAVED` | YES to the references question, done |
| `CHANGES DROPPED` | HOME leaves edit mode with changes |

**Names given:** a new cartridge `NEW DX7` (`NEW JUNO-106`, …); a clone the name's first 9 characters
and ` 2`; a FUSION slot made by EDIT `FUSION D0`; a name left empty `NO NAME`.

**ARP held** ([../controls.md](../controls.md)): every screen's: `HOLD ARP +`, `SELECT: THE RHYTHM, # A
NOTE, _ A REST`, `ALGORITHM: THE PATTERN`, `K1: HOW MANY NOTES LATCH`, `LFO: LET THE LATCHED NOTES GO`.

## Help

HOME + LFO ([../help.md](../help.md)): these seven pages, then the shared ones. The help draws the
first page's last line, `PRESS HOME TO GET BACK`, itself.

```
[INSTRUMENTS]: PICK, PLAY, EDIT SOUNDS
TOP: A CARTRIDGE, A BANK OF SOUNDS,
  EACH SOUND IN A NUMBERED SLOT
BOTTOM: THE SOUND PICKED, IN DETAIL
{SELECT}: A SLOT, {ALGORITHM}: BY 8
{PRESETS}: THE NEXT / PREVIOUS SOUND
{K1}: ANOTHER CARTRIDGE
{KEYS}: PLAY THE SOUND PICKED
{EDIT}: CHANGE THE SOUND (NEXT PAGE)
{GLO}: THE CARTRIDGE'S NAME AND TYPE
{ENV} (EXPLORE NAMED VAULTS): ALL THE
  CARTRIDGES, {ENV} AGAIN OPENS ONE
{SEL}: MENU FOR THE SLOT (PAGE 6)
{ARP} (AUTOMATIC REPEAT PROCEDURE):
  ARPEGGIO; HELD: RHYTHM, ORDER, LATCH
```

```
CHANGING A SOUND: {EDIT}
THE TABLE: ITS SETTINGS, 4 IN A ROW
{SELECT}: PICK A ROW
{K1}-{K4}: CHANGE THE ROW'S 4 VALUES
{ALGORITHM} OR {SEQ} (SHOW ELSE, QUICK):
  THE PAGES OF SETTINGS
{LFO} (LEAVE FIELD OPEN): THE KNOB
  TURNED LAST BACK TO ITS DEFAULT
{KEYS}: HEAR IT AS IT CHANGES
{PRESETS}: ANOTHER SOUND, STILL EDITING
{SEL} ON NAME: TYPE A NAME
{SEL}: COPY, PASTE, INIT, RENAME,
  AUTO LEVEL, REVERT
LEVEL: HOW LOUD THE SOUND IS, IN DB
{EDIT}: DONE, KEEP THE CHANGES
{HOME}: LEAVE, DROP THE CHANGES
```

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

```
CHIP SOUNDS: A VIRTUAL RETRO SOUND CHIP
OSC: PULSE, TRIANGLE, WAVE (ONE OF 16
  DRAWN WAVES), BUZZ, NOISE
CLOCK: THE CHIP IT IS TUNED ON (BRK,
  8BIT, PSG, 16BIT); HIGH NOTES GO OUT
  OF TUNE AS ON THE CHIP; EXACT: IN TUNE
CURVE: HOW ITS 16 VOLUMES SOUND
SWEEP: THE PITCH SLIDES BY ITSELF
VOL: 0-F; ENV: VOL STEPS DOWN OR UP,
  OR THE PSG'S OWN ENVELOPE
TABLES: BUILT-IN LISTS PLAYED ONE STEP
  A FRAME (60 A SECOND): VOL, ARP (NOTES
  ADDED: CHORDS, DRUM DROPS), PITCH,
  DUTY, NOISE; X1-X4: FRAMES A STEP
VIBRATO: DELAY, SPEED, DEPTH
WAVE PAGE: THE WAVE THE WAVE OSC PLAYS
```

```
FUSION: A SOUND MADE OF A PATTERN
{KEYS}: EACH PLAYS ITS OWN ROW OF IT
  C-4 PLAYS ROW 3C (ITS MIDI NOTE)
  EVERY NOTE OF THAT ROW AT ONCE
  LET GO: THEY ALL STOP
KIT: A DRUM SOUND ON EACH {KEY}
{EDIT}: ITS PATTERN, NAME AND LEVEL
  ON PATTERN, {K1} OR {ENV}: ANOTHER
  IN {ENV}'S LIST: {KEYS} TRY THEM
  {SEL}: WRITE IT IN [EDITOR]; THERE,
  {REC} OFF: {KEYS} PLAY IT; {HOME}: BACK
{EDIT} ON AN EMPTY ONE: A NEW PATTERN
  {OCT+}: FILLED WITH ONE SOUND,
  {K1} OR {SELECT} PICK IT; {OCT-}: EMPTY
OTHER ROWS: KEEP IDEAS; {SEL}, MOVE
  PUTS ONE ON A {KEY}
A FUSION INSIDE A FUSION: SILENT
```

```
THE MENUS
{SEL} ON A SLOT:
  MARK: SEVERAL, {SELECT} STRETCHES IT
  COPY, PASTE, DELETE
  MOVE: TO AN EMPTY SLOT; SWAP: TRADE
  INIT: A PLAIN STARTING SOUND
  RENAME: TYPE ITS NAME
  AUTO LEVEL: AS LOUD AS THE OTHERS
  REVERT: THE SOUND AS LAST SAVED
{SEL} IN THE CARTRIDGES ({ENV}):
  NEW (PICK ITS TYPE), CLONE, MOVE,
  DELETE, RENAME, AUTO LEVEL ALL,
  REVERT; BUILT IN (BROWN NUMBER):
  BACK TO BUILT-IN DROPS YOUR CHANGES
A SOUND SONGS USE, MOVED OR DELETED:
  IT ASKS IF THE SONGS SHOULD FOLLOW
```

```
FAVORITES: SOUNDS FROM ANY CARTRIDGE
  IN ONE LIST, BUILT IN AS FB
EACH SLOT POINTS AT A SOUND; WHAT YOU
  RECORD KEEPS THE SOUND'S OWN NUMBER
{SEL} ON A SOUND: ADD TO FAVORITES
HOLD {SEL} + {GLO}: TO FAVORITES AND BACK
{PRESETS}: THE NEXT FAVORITE
{EDIT} ON ONE: POINT IT ELSEWHERE
  {K1}: CARTRIDGE, {PRESETS}: SOUND
  {EDIT}: KEEP; {HOME}: DROP
{SEL} IN FAVORITES: REMOVE, MOVE,
  SWAP, COPY, PASTE
GREY, MISSING: ITS SOUND IS GONE
NEW CARTRIDGE ({SEL}): ANY TYPE
```
