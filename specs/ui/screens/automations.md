# AUTOMATIONS

**An automation records the FM-1's controls as they are worked and plays them back:** every key, button and knob, at
the times they came. Sixteen of them, kept in one file ([../../files/automations.md](../../files/automations.md)). A
performance of mutes, knob sweeps or keys played into LOOPS is done again by the FM-1 itself, on any screen, in any
state: an automation restores nothing but the screen it was recorded on, and where it lands is the player's to arrange.
**HOME + OP6** opens it ([../controls.md](../controls.md)); it is last in HOME's round. **HOME, ARP, then a white key**
plays a slot from any screen ([below](#playing)), **HOME, ARP, REC, then a white key** records into one
([below](#recording)).

## The screen

```
AUTOMATIONS                    > |##9    the header
 00          01          02          03       a 4 x 4 grid of slots, 60 x 46 each: its number (S),
 LOOPS       EMPTY       FX          SONGS       the screen it was recorded on (T, 9 characters),
 0:42                    1:05        0:08        its length (T, M:SS); EMPTY dark grey
 ...
REC RECORD  PLAY PLAY  SEL MENU               the hint line (T), at y 213
```

- The slot highlighted is boxed white, its number white (the others cyan); the names light grey, `EMPTY` dark grey; a
  screen number no screen of the round has reads `?`.
- **SELECT** and **ALGORITHM** move the highlight a slot a click (row by row, 00 to 0F, stopping at either end); **a
  white key** picks a slot (white key *n*, 1-16: slot *n* − 1, as LOOPS' CONTROL mode numbers its loops). The keys play
  nothing here.
- **REC** records into the highlighted slot ([below](#recording)); a slot holding one asks first (`AUTOMATION 03 HOLDS
  0:42` / `REPLACE IT?`, OCT+ `REPLACE`, OCT- `KEEP`).
- **PLAY** plays the highlighted slot as it is let go ([below](#playing)); an empty one: `AUTOMATION 03 IS EMPTY`, PLAY
  blinking.
- **SEL** opens the menu, titled `AUTOMATIONS`: RECORD, PLAY, COMPACT ([below](#compacting)), CLEAR (on a slot
  holding one: `CLEAR AUTOMATION 03?`, CLEAR / KEEP; `AUTOMATION 03 CLEARED`), REVERT (while the file has changes not
  saved: `REVERT AUTOMATIONS TO SAVED?`).
- **The LEDs:** SEL, REC and PLAY dim.

## Recording

- REC (or RECORD) closes AUTOMATIONS and goes back to the screen it was opened from: **that screen is the
  automation's**. It waits for the first press, key or turn, which is its time 0 (no silence before it). The header
  shows `AUTO 03` after a red ● in place of the screen's name while it records (and waits).
- **Everything the FM-1 takes as its own input is recorded:** each key and button pressed and let go, each knob's
  clicks, MIDI's learned controls and notes as the controls they stand for, in the order the FM-1 takes them within a
  pass of its main loop (the clicks before the first press, the presses, the other clicks, the releases). **MASTER is
  never recorded.** A release of what was down before the recording began is left out.
- **From any screen: HOME, ARP, REC, then a white key** records into slot *n* − 1 for white key *n*, as REC does
  here, on the screen HOME's combination is over (on AUTOMATIONS itself, the screen it was opened from): after ARP, REC
  switches HOME's combination's white keys from playing the automations to recording them, and back
  ([../controls.md](../controls.md#homes-combination)). Taps, HOME tapped first or held. A slot holding one asks first,
  over the screen; while an automation records or plays, none starts: `ONE AUTOMATION AT A TIME`, the combination
  staying open. None of HOME, ARP, REC and the white key is in it: it starts at the first press after them.
- **HOME + OP6 ends it,** HOME held or tapped first: AUTOMATIONS opens as ever, `AUTOMATION 03 RECORDED: 0:42`.
  **HOME, ARP, REC ends it where it is,** however it started: the screen stays, `AUTOMATION 03 RECORDED: 0:42`.
  None of the end's controls is in it: HOME's press is held back until something else shows it is not an end (then
  written at its own time), and a HOME tapped, then OP6, or then ARP and REC, is taken out again as the recording ends
  (when only those controls' own events followed HOME's press). What the automation holds then is let go in it at that time. Nothing pressed:
  `NOTHING RECORDED`, the slot as it was.
- It is an edit: SAVE keeps it, AUTOMATIONS' REVERT drops it. The events gather in RAM and go into the edit memory
  about once a second, a part of the slot at a time (not while the end's HOME may still be taken out); the automation the
  slot held before goes at the first of them. The edit memory full stops it there (`MEMORY FOR EDITS IS FULL`), what was
  written kept. Its 16 parts full (more than the edit memory holds) end it there, kept: `AUTOMATION 03 IS FULL: 61:40`.
  While the FM-1 takes no input (a save under way) nothing is recorded.
- **REVERT ALL ends it** (`AUTOMATION STOPPED`), its changes dropped with the others; so does a page writing to the
  FM-1 (an upload, the EDITOR's WRITE).

## Playing

- PLAY (or the menu's PLAY) closes AUTOMATIONS, **goes to the automation's screen**, and from there gives the FM-1
  the recorded input at its times in place of its controls': each press, release and turn as if made by hand (holds by
  their timing, a question answered by its keys, SAVE saving, HOME's combinations going to other screens). Nothing
  else is set first: it plays in whatever state the FM-1 is in. The header shows `AUTO 03` after a green ▶.
- **From any screen: HOME, ARP, then a white key** plays slot *n* − 1 for white key *n* (AUTOMATIONS' own numbering),
  as PLAY does here: ARP switches HOME's combination's white keys from the mutes to the automations
  ([../controls.md](../controls.md#homes-combination)). HOME tapped first or held, as every combination; nothing else
  needs holding. An empty slot: `AUTOMATION 03 IS EMPTY`, the combination staying open. While an automation records
  or plays, none starts: `ONE AUTOMATION AT A TIME`.
- **Any key, button or knob of the player's stops it** (`AUTOMATION STOPPED`); that press does nothing else, nor its
  release. **MASTER** stays the player's throughout. Controls held as it starts stay held.
- At its end what it holds is let go (`AUTOMATION 03 PLAYED`); controls held as it started stay held. It plays once.
- REVERT ALL, or a page writing to the FM-1, stops it (`AUTOMATION STOPPED`).
- **Timing:** each pass of the main loop gives the events due by then, at most one recorded pass's (events recorded in
  one pass come in one pass, the order kept); a pass is about 1 ms, longer while the screen draws. The knobs'
  acceleration (KNOB SPEED) counts the gaps between the passes as recorded, so a fast turn takes the steps it took when
  recorded, whenever its passes come.
- **While the FM-1 takes no input** (a save's progress) a playback's time stands still: nothing is given meanwhile,
  and it goes on from there after (the events given then would be lost).

## Compacting

For work done on the screens at a hand's pace and wanted back as fast as it can go; an automation of music keeps its
pauses as they were.

- **COMPACT** (SEL's menu) opens its list, titled `COMPACT AUTOMATION 03`: `1 S`, `0.5 S`, `0.3 S`
  ([../dialogs.md](../dialogs.md#menus)); on an empty slot: `AUTOMATION 03 IS EMPTY`.
- The limit picked shortens **every pause while nothing is held** that is longer: from an event after which no key or
  button of the automation's is down, to the next event. A pause while something is held keeps its time, as it is part
  of what that control does: a hold (HOLD DELAY), the 2-second holds (FX held, EDIT held on LOOPS, HOME + REC held), a
  knob turned with SEL held, a note's length. Every event stays, in its order; events recorded in one pass stay in one.
- `AUTOMATION 03 COMPACTED: 0:18`, its new length; no such pause longer than the limit: `NOTHING TO COMPACT`, the
  slot as it was.
- **An edit,** as a recording: SAVE keeps it, REVERT drops it. Its parts are written again: the edit memory too full
  for them, `MEMORY FOR EDITS IS FULL`, the slot as it was.
- **The limits:** 0.3 s stays above COMBO GRACE's longest, 250 ms (a button let go within it still makes a combination
  with the next press). A slot whose steps wait for the music (DJ's LOAD at the bar, LOOPS' QUANTIZE line) takes a
  longer one; compacting again with a shorter one goes further.

## Texts

- **The screen:** `AUTOMATIONS`; the slots' `00`-`0F`, the screens' names, `EMPTY`, `0:42`; the hint line `REC RECORD
  PLAY PLAY  SEL MENU`; the menu's RECORD, PLAY, COMPACT, CLEAR, REVERT; COMPACT's list `COMPACT AUTOMATION 03`, `1 S`,
  `0.5 S`, `0.3 S`.
- **The header:** `AUTO 03`.
- **Questions:** `AUTOMATION 03 HOLDS 0:42` / `REPLACE IT?`, REPLACE, KEEP (REC here, or HOME, ARP, REC, a white key); `CLEAR AUTOMATION 03?`, CLEAR, KEEP;
  `REVERT AUTOMATIONS TO SAVED?`.
- **Toasts:**

  | Toast | When |
  |---|---|
  | `AUTOMATION 03 RECORDED: 0:42` | HOME + OP6, or HOME, ARP, REC, ends a recording |
  | `AUTOMATION 03 IS FULL: 61:40` | its 16 parts full: the recording ends there, kept |
  | `NOTHING RECORDED` | ... with nothing pressed |
  | `AUTOMATION 03 PLAYED` | an automation played to its end |
  | `AUTOMATION STOPPED` | a control of the player's stopped it; REVERT ALL or a page writing to the FM-1 ended a recording or a playback |
  | `AUTOMATION 03 IS EMPTY` | PLAY or COMPACT on an empty slot; HOME, ARP, its white key |
  | `ONE AUTOMATION AT A TIME` | HOME, ARP (REC), a white key while an automation records or plays |
  | `AUTOMATION 03 COMPACTED: 0:18` | COMPACT done: its new length |
  | `NOTHING TO COMPACT` | COMPACT with no pause longer than the limit while nothing is held |
  | `AUTOMATION 03 CLEARED` | SEL → CLEAR, CLEAR |
  | `MEMORY FOR EDITS IS FULL` | the edit memory full while recording, or for COMPACT's parts |

## Help

```
[AUTOMATIONS]: YOUR CONTROLS PLAYED AGAIN
16 SLOTS OF YOUR KEYS AND KNOBS, IN TIME
{SELECT}, {ALGORITHM} OR A {WHITE KEY}:
  PICK A SLOT
{REC}: RECORD INTO IT, BACK WHERE YOU
  WERE; IT STARTS AT YOUR FIRST PRESS
{HOME}, {OP6}: THE RECORDING ENDS
{PLAY}: PLAY IT, ON ITS OWN SCREEN
  ANY KEY OR KNOB BUT MASTER STOPS IT
{HOME}, {ARP} (AUTOMATIC REPEAT PROCEDURE)
  THEN ITS {WHITE KEY}: PLAY IT ANYWHERE
  {REC}, ITS {WHITE KEY}: RECORD THERE;
  {HOME}, {ARP}, {REC} AGAIN: IT ENDS
{SEL}: RECORD, PLAY, CLEAR, REVERT,
  COMPACT: ITS PAUSES SHORTENED
```
