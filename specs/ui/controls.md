# The controls

Status: **settled** (2026-10-09), LOOPS' controls to be tried in the playground. The records:
[the UI proposal](../proposals/202610072229-ui.md), sections 1.1-1.3 and 9; LOOPS' controls,
[the loop station proposal](../proposals/202610240236-loops-station.md), section 6.

How the knobs, buttons and keys work on every screen: the grammar of taps, holds and combinations,
one meaning for each button, and the key map. What the hardware has is in
[README.md](README.md#the-hardware-the-ui-works-with); what the overlays look like is in
[dialogs.md](dialogs.md).

## Knobs

- **SELECT** moves the cursor vertically, **ALGORITHM** horizontally.
  - The cursor stops at the edges. Only three things wrap: PRESETS within its cartridge, the
    cartridges (K1 on INSTRUMENTS, SEL + PRESETS), and the screens in HOME's combination.
- **PRESETS** changes the **current instrument** on every screen.
  - It wraps within the cartridge.
  - Its instrument list shows over the screen while it turns, gone a second after the last click
    ([dialogs.md](dialogs.md#presets-instrument-list); SETTINGS' PRESETS LIST switches it off).
  - The footer names the new instrument at once ([frame.md](frame.md#the-footer)), so there is no
    toast for it.
  - The keys play the current instrument wherever they are not taken for something else.
- **K1-K4** change values at the cursor.
  - What they change depends on the screen, and the screen names it ([frame.md](frame.md#the-footer)).
  - K1-K4 never move the cursor. Only writing a note or clearing a cell (LFO) advances it.
- **Acceleration:** a fast turn takes bigger steps (2×, 4×, 8× with speed); a slow turn always moves
  one step. The speed is set in SETTINGS (KNOB SPEED, [screens/settings.md](screens/settings.md)).
  PRESETS never accelerates.
- **SEL held + a knob** gives coarse steps: an octave for notes, `10` for a `00`-`7F` value.
  - The exceptions: SEL + SELECT / ALGORITHM marks a block, and SEL + PRESETS turns cartridges (not on DJ,
    which has no keyboard functions: [screens/dj.md](screens/dj.md#controls)).
- **SEL + PRESETS, SEL held or tapped first:** the cartridges, one a click, each picked at once, the header
  naming it ([frame.md](frame.md#the-header)), with the cartridges' list open over the screen, the current one
  highlighted ([dialogs.md](dialogs.md#presets-cartridge-list)). A SEL tap does its own job first (a menu opens,
  say); PRESETS turned next closes that menu and opens the list in its place, and the list stays while PRESETS turns;
  any other control closes it and does its own job, as if it had not been open (HOME and SEL only close it). With SEL held the list closes when
  SEL is let go, and PRESETS turns the instruments again, as before. Only where SEL +
  PRESETS changes the current cartridge: not on DJ, and not over a question, a name being typed or any overlay the
  SEL tap did not open (there SEL held + PRESETS turns the cartridge without the list, as before).

## Buttons: tap, hold and layers

- **Few combinations** (2026-10-09), as they are not easy to press on the FM-1:
  - **every function is reachable with taps and knob turns alone;**
  - **a held combination is only a shortcut** for something taps also reach, or, as the last resort,
    the only way to something rare (big steps with SEL held);
  - where a mode can be a switch, a tap toggles it;
  - **what is played live goes on the note keys,** which are easier to press than the buttons
    (2026-10-10);
  - **where a combination is needed, a note key with a knob** comes before a button with a knob: DJ's
    HOLD, a deck key held while its knob moves the deck ([screens/dj.md](screens/dj.md)).

  New controls follow this. HOME's combination opens with a tap ([below](#homes-combination)); the
  others are held, as shortcuts, and each has a tap route.
- **PLAY, REC and the note keys act the moment they are pressed,** for timing.
  - **The one exception:** on the EDITOR with REC on, PLAY acts on release. Holding PLAY and pressing
    a key starts a take on that note ([screens/editor.md](screens/editor.md)).
  - **PLAY held** past the hold delay also stops: back to the start
    ([transport.md](transport.md)).
- **Every other button acts on its tap**, at release, so it can also be held as a modifier:
  - **tap:** press and release with no other control touched in between;
  - **held + another control:** a combination. The button's own tap does not happen;
  - **a combination repeats, and its first use locks it:** while the button is held, the member that
    acted first can act again and again (SEL held: SELECT and ALGORITHM go on marking; FX held: punch-in after punch-in);
    another member then closes the combination and does its own job (SEL held, after marking, PRESETS
    turns the instruments, not the cartridges);
  - **a control that is no member closes it** and does its own job, as if the button were not held;
    the button's tap then does not happen either (SEL held, then SEQ: SEQ's own job, and no menu when SEL
    is let go). PLAY, REC and OCT, which act at once, stay as they are;
  - **held alone past the hold delay** (500 ms; SETTINGS → HOLD DELAY): an overlay shows the button's
    *layer*, meaning what the keys and knobs do with it (HOME: the screens and tracks; FX: the punch-in
    effects; [dialogs.md](dialogs.md#hold-layers)).
    - Letting go then closes the overlay and does nothing else; one exception, DJ's LFO: let go, the pads are
      the LFO pads its layer showed ([screens/dj.md](screens/dj.md#the-lfo-pads)).
    - Combinations work at once; they never wait for the overlay.
    - **The overlay stays up as long as the button is held,** through the combinations made with it
      and a change of screen: HOME + OP1, then OP2 with HOME still held, goes on to the EDITOR with
      the layer still shown. It only shows; the keys and knobs act as they would without it.
    - **A hold can be an action instead:** PLAY held on the EDITOR with REC on shows how to start a
      take, and letting go starts it ([screens/editor.md](screens/editor.md)).
    - **A screen's own hold acts only on the screen itself:** with a dialog, a list or HOME's
      combination open, the button is theirs (HOME tapped open, then LFO: the help, however long LFO is
      down). Under HOME's combination a button shows no layer either: it acts on its release.
  - While a modifier is held, the keys it uses do not play notes.
- **The combination grace period** (100 ms; SETTINGS → COMBO GRACE, 0-250 ms):
  - **Why:** the FM-1's buttons are not precise, and two pressed "together" drift apart.
  - **A modifier still counts for the grace period after it is let go.** HOME released and LFO
    pressed 50 ms later is still HOME + LFO.
  - **A button that can form a combination on the current screen** acts on its tap only once the
    grace period has passed with no partner pressed. HOME tap opens its combination, and LFO tap
    clears, only then. So HOME + LFO can never clear a cell.
    - These buttons are usually HOME, LFO, SEL, FX, ARP and SAVE. On the EDITOR with REC on, PLAY
      waits too, since PLAY + a key starts a take there.
    - Their taps act about 0.1 s after release.
  - **Buttons that combine with nothing on the screen act at once** on release: usually EDIT, GLO,
    ENV and SEQ.
  - **Knobs and buttons count in the order they came,** however close together: a knob turned and then
    a button tapped is a turn and a tap; a button pressed and then a knob turned is the combination.
  - **Two tap-acting buttons combine in either order,** except OCT: it never waits, so the keys'
    octave never lags behind a fast change. SAVE + OCT- and SAVE + OCT+ need SAVE first (or let go
    within the grace period).
  - **With a control that acts on press** (PLAY, REC, the note keys), the modifier has to come
    first, though it may have been let go within the grace period. A note must sound at once, so it
    cannot wait to see whether a modifier follows.
- **The key that opens something closes it:** an overlay, a mode or an editor. The EDITOR entered
  deep from another screen is left with HOME tap ([navigation.md](navigation.md#jumps-into-the-editor)).
- **An edit mode entered with EDIT** (an instrument's edit page, a song's order, the note editor,
  GATE's steps) **keeps a copy of what it edits, as it was at EDIT:**
  - **EDIT** leaves it keeping the changes;
  - **HOME** leaves it dropping them: what it edits goes back to the copy (the toast
    `CHANGES DROPPED`). What it dropped is gone (there is no step back); with nothing else unsaved,
    nothing is unsaved after it ([saving.md](saving.md)).
  - **EDIT never goes deeper from inside one:** that is in its SEL menu (EDIT PATTERN, RENAME).
  - **The copy** is small: the instrument records the session touched (a few hundred bytes each), a
    song's order (at most 256 B), the cells changed, the gate patterns. It comes from the edit
    memory. The pattern editor itself is not an edit mode: its edits have no drop, and REVERT
    ([saving.md](saving.md#revert)) takes the pattern back to what was saved.
- **HOME tap:**
  - **deep:** **back**. It closes the topmost level and returns to where you were. Deep means one of:
    - an overlay, a mode or an editor is open, a block is marked or carried;
    - EDIT has taken you into the EDITOR from another screen: from PLAYBACK, from a song position,
      or from a FUSION instrument ([navigation.md](navigation.md#jumps-into-the-editor)).
  - **at the top of a screen,** with nothing to back out of: it opens HOME's combination
    ([below](#homes-combination)).

## HOME's combination

HOME's combination is the one tap opens: the screens, the tracks' mutes, rewinding, the CUT and the
help, from any screen. **HOME + X** means X in HOME's combination, whichever way it was opened.

- **Opened by a tap** at the top of a screen; its overlay shows what can act
  ([dialogs.md](dialogs.md#homes-combination)), and HOME breathes ([leds.md](leds.md)). **Held,** HOME
  opens the same combination at once, as a shortcut; letting go closes it.
- **Its members:**

  | Control | Does | Then |
  |---|---|---|
  | SELECT | the next or the previous screen of the round ([navigation.md](navigation.md#the-screens)), one a click, wrapping | stays open: turn on |
  | a black key | its screen | closes |
  | a white key | mutes or unmutes its track (key 1 = track `00` … key 16 = track `0F`); held past the hold delay, solos it; in LOOPS, its loop the same way, at once (key 1 the top loop: [screens/loops.md](screens/loops.md#control-mode-mutes-and-solos)); on DJ, keys 1-8 the left deck's tracks `00`-`07`, keys 9-16 the right deck's ([screens/dj.md](screens/dj.md#mutes)) | stays open: key after key |
  | SEQ | switches the white keys to SOLO, and back to MUTE, again and again while they are members | stays open |
  | OCT- / OCT+ | on DJ, with the white keys: the tracks they reach, `00`-`07` or `08`-`0F`, where a deck's song has tracks that way; else nothing, no message ([screens/dj.md](screens/dj.md#mutes)) | stays open |
  | PLAY | rewinds what plays, or played last ([transport.md](transport.md#rewinding-and-stopping)) | closes |
  | REC | the [CUT](#cut): silent while REC is held | closes |
  | LFO | the help for this screen, "LOOK FOR ONSWERS" ([help.md](help.md)) | closes |
  | HOME | closes it | |

- **The first use locks it:** once a member has acted, only that member acts on. Any other control
  closes the combination and does its own job: after a mute, PLAY plays or pauses (it does not rewind),
  a black key plays its note. SEQ's switch and DJ's OCT- / OCT+ go with the white keys: they are not a
  use of their own, and they act on as long as the white keys do (HOME, SEQ, then a white key solos its
  track; after a mute, SEQ switches to SOLO, and a white key then solos).
- **A control that is no member** (EDIT, a knob other than SELECT, ...) closes it and does its own job,
  as if it had not been open.
- **Held,** the rule is the same: HOME + OP1, then OP2 with HOME still held, goes on to the EDITOR; HOME
  + a white key, then a black key with HOME still held, plays that key's note; letting go of HOME does
  nothing more.

## What each button means

One meaning per button across screens; each screen applies it to its own content
([the key map](#the-key-map) has every screen).

| Button | Meaning |
|---|---|
| **EDIT** | Opens the thing at the cursor for editing: a cell (the note editor), a song (its order), an instrument (its editor), GATE's steps. Pressed again, it leaves keeping the changes; HOME leaves dropping them ([above](#buttons-tap-hold-and-layers)). On PLAYBACK it opens the EDITOR on what plays, entered deep: HOME tap returns. On STATUS, any page, it goes to SETTINGS, as HOME + MONO does: nothing to return to. |
| **SEL** | Select and act. **Tap** opens the actions menu for the marked block, or for the item at the cursor (copy, paste, move, delete...); where blocks or slots can be marked (the EDITOR, INSTRUMENTS), the menu starts with **MARK**: SELECT / ALGORITHM then stretch the block from the cursor, and SEL opens its menu. **Held** works as a shift, a shortcut: SELECT / ALGORITHM mark a block, PRESETS turns cartridges, K1-K4 take coarse steps. On DJ, which has no menu, a tap resets the last knob turned (LFO's job elsewhere: there LFO is the LFO pads). |
| **ENV** | `EXPLORE NAMED VAULTS`: the list of containers where things live (patterns, cartridges, samples), with SEL actions inside the list. **Held,** in the EDITOR: the pipette, the cursor's cell's instrument becomes the current one (a shortcut for SEL's TAKE ITS INSTRUMENT). On DJ, which has no containers to list, it turns what K2 / K3 do, a cycle: the decks' filters, their volumes. |
| **GLO** | Properties of the current container: the pattern, song, cartridge, the loops, FX chain or view. |
| **SEQ** | `SHOW ELSE, QUICK`: the next subview: the track count, the visualiser, the edit page. In LOOPS, which has one view, it switches the keys between PLAY (notes) and CONTROL (the loops' mutes and solos, QUANTIZE, MONO / POLY: [screens/loops.md](screens/loops.md#control-mode-mutes-and-solos)). On DJ it swaps the decks: shown the other way round. On SONGS, which has one view, it turns the play mode, ONE PLAY, REPEAT SONG, PLAY NEXT: how a song that stops ends, one going on into the next with PLAY NEXT ([screens/songs.md](screens/songs.md#the-play-modes)). |
| **ARP** | `AGAIN, REPEAT, PLEASE`: **the arpeggiator,** one for every screen but two editors with an ARP of their own: the EDITOR (the row preview, the cursor's row in the rhythm) and LOOPS (the pattern recorder, [screens/loops.md](screens/loops.md#the-pattern-recorder-arp)). **Tap:** on / off (lit while on, dim while it can come on); on with latched notes and no key held, the tap lets them go first and it stays on. **On:** the keys feed it instead of sounding (a screen's own use of a key comes first: FX LIVE's punch-ins); keys held as it comes on become its notes; its notes take turns in SETTINGS' ARP PATTERN (UP, DOWN, UP-DOWN, RANDOM, AS PLAYED: AS PLAYED in the order pressed), a note a `#` of ARP RHYTHM (a row a step), each the current instrument's, however it is picked. **The latch,** ARP LATCH: with 1-8, notes keep playing once their keys are let go, that many at most (the oldest goes past it), and a key pressed with none held starts a new set; OFF, a note stops with its key. Switched on with no key held, it waits for one: it never starts on a note by itself. Only on FX LIVE, where the keys are punch-ins and play no notes, does it start with the last note played, hands free (LATCH not OFF). A change of screen switches it off. **Held:** + SELECT the rhythm (the EDITOR's row preview's too), + ALGORITHM the pattern, + K1 the latch (`ARP UP`, `LATCH 4 NOTES`, `LATCH OFF`), + LFO lets the latched notes go; in LOOPS nothing. DJ: nothing (its keys are the decks'). **With a punch-in's key held** (FX held + a white key, or FX LIVE's white key), on every screen: ARP holds the punch-in on, its key or another punch-in stopping it ([screens/fx.md](screens/fx.md#punch-in-effects)). |
| **LFO** | **CLR**, `LEAVE FIELD OPEN`: clear, or reset to the default. On a cell it clears, and on an empty cell it writes a note off. On a value it resets. In LOOPS it peels: the selected loop goes (on `+ NEW LOOP`, the last), without a question ([screens/loops.md](screens/loops.md#recording)). On FX it resets the last knob turned. On DJ it brings in the LFO pads, and again goes back (SEL resets the knob there). **HOME + LFO** is the help: "LOOK FOR ONSWERS". |
| **REC** | Record. In the EDITOR it switches writing on and off: with it off the keys only play; with it on they write, step by step or in a realtime take. In LOOPS it records where the selection is, one pass of the length: a new loop on `+ NEW LOOP`, a re-record on a loop; while the loops play it arms, and the take starts at the loops' start; again, it stops the take early ([screens/loops.md](screens/loops.md#recording)). |
| **PLAY** | Play / pause ([transport.md](transport.md)). |
| **HOME** | **Tap:** when deep, back; at the top of a screen, [HOME's combination](#homes-combination): SELECT the screens, a black key a screen, white keys mute (SEQ: solo), PLAY rewinds, REC the [CUT](#cut), LFO the help. **Held:** the same combination, as a shortcut. |
| **SAVE** | Writes all changes to flash. **SAVE + OCT-:** REVERT ALL, after a question, the shortcut for SETTINGS → TOOLS → REVERT ALL ([saving.md](saving.md)). **Held** past the hold delay, its let-go is no tap and saves nothing; **SAVE + OCT+** saves then. |
| **FX** | **Tap:** the FX screen (again: back). **Held + white key:** a punch-in effect while the key is held ([screens/fx.md](screens/fx.md#punch-in-effects)). |
| **OCT- / OCT+** | The keyboard's octave, −4 to +4, with a toast (`OCT +3`) at each change. On the FX screen they switch the selected effect off / on instead; on FX's EDIT, where the keys play notes, **SEL + OCT** changes the octave. On DJ, which has no keyboard, they do nothing alone; in HOME's combination they pick the tracks its white keys reach, `00`-`07` or `08`-`0F`. |

- **Mutes** apply to what plays: the song or the looped pattern. They are performance state and are
  not saved. Each change has a toast: `TRACK 02 MUTED`, `TRACK 02 ON`. In LOOPS the white keys mute and
  solo loops instead (`LOOP 03 MUTED`; held in CONTROL mode, at the next bar line), and the loops' mutes
  and solos are kept with the loops ([screens/loops.md](screens/loops.md#control-mode-mutes-and-solos)).
- **Solo:** HOME, SEQ, then a white key solos that track (`TRACK 02 SOLO`); so does HOME + a white key
  held past the hold delay. The same again on the soloed track ends the solo (`SOLO OFF`); on another
  track it moves the solo there.

### CUT

**HOME + REC, on every screen:** HOME held, or tapped first (its combination open), then REC. REC alone stays
the EDITOR's and LOOPS' recording and does nothing elsewhere.
- **At the press** every note fades out and the output fades to silence over about 12 ms; then every effect is
  emptied (the CHORUS, DELAY and REVERB lines, a punch-in, the inserts' and the DJ FILTERs' states, a METRONOME
  click, the LIMITER's gain), and the Juno-106 chorus with them.
- **While REC is held** there is no sound (the speaker, the jack, USB audio) and nothing is worked on: no
  note starts, from what plays, the keys or the arpeggiator, and no effect runs. Only the clocks go on: what
  plays keeps its time (DJ's decks stay in step), the METRONOME and a count-in count on unheard (their clicks
  after the let-go on the beat; one due during the CUT is never heard late), and the CPU is at rest. The CUT
  never moves the timeline.
- **Let go,** what plays is heard again from its next notes, its effects starting dry, the output fading in
  over about 12 ms; with nothing playing, it stays silent.
- REC is lit full while held; the toast `SOUND CUT: LET GO OF REC`.
- **Held 2 s,** every effect of the setup FX would edit from this screen (the song's, the loops' on LOOPS, DJ's on
  DJ: [screens/fx.md](screens/fx.md#the-fx-setup)), the METRONOME and DUCK included, is switched off, as switching
  each off on FX would: an edit, which SAVE keeps and REVERT takes back; the toast `SOUND CUT, EFFECTS OFF` (with
  every effect off already, only the toast). Let go, what plays is heard dry.

An effect left with nothing to do costs nothing anyway ([screens/fx.md](screens/fx.md#effects-at-rest)); CUT
is for when what rings should end at once.

## The key map

**On every screen:**

| Control | Does |
|---|---|
| PRESETS | the current instrument |
| SEL + PRESETS (SEL held, or tapped first) | its cartridge, the cartridges' list open ([above](#knobs)) |
| PLAY | play / pause: the screen's own thing on SONGS, EDITOR, LOOPS and DJ (both decks), else what played last ([transport.md](transport.md)) |
| HOME tap | when deep, back; at the top, HOME's combination (HOME + below: tapped, then the control; or held) |
| HOME + SELECT | the screens, one by one, round |
| HOME + black key | go to a screen |
| HOME + white key | mute a track (held: solo; after SEQ: solo); in LOOPS a loop, the same way; on DJ a deck's track (keys 1-8 the left deck's, 9-16 the right's) |
| HOME + PLAY | rewind (on DJ: both decks back to their cue points) |
| HOME + REC | the [CUT](#cut): all sound off, every effect emptied, silent while REC is held; held 2 s, every effect switched off |
| HOME + LFO | help for this screen: "LOOK FOR ONSWERS" |
| SAVE | save |
| SAVE + OCT+ | save (as SAVE's tap; held SAVE, its let-go, saves nothing) |
| SAVE + OCT- | revert all to saved, after a question |
| FX tap | the FX screen |
| FX + white key | a punch-in effect |
| OCT- / OCT+ | the octave (on FX: the effect off / on; on DJ: nothing, HOME + OCT the mutes' tracks) |
| SEL + OCT- / OCT+ | on FX's EDIT: the octave (OCT alone switches the effect there) |
| keys | play the current instrument, unless the screen uses them otherwise |

**Per screen.** In this table "—" means no function (the LED is dark). LOOPS' column is the one
accepted to be tried in the playground ([screens/loops.md](screens/loops.md#controls)); DJ's too
([screens/dj.md](screens/dj.md#controls)).

| | SONGS | EDITOR | INSTRUMENTS | PLAYBACK | LOOPS | DJ | FX |
|---|---|---|---|---|---|---|---|
| SELECT | songs / positions | rows | slots | pause, play row by row | the loops, then `+ NEW LOOP` | the song list | effect |
| ALGORITHM | where PLAY starts; in the order, positions | tracks | columns; edit: the pages | the cursor track, one at a time | TEMPO, any time | the crossfader | knob page |
| K1-K4 | pattern at position | K1 slot, K2 command, K3 / K4 values | K1 cartridge; edit: the group | K1: position; K2: 1 / 2 / 4 / 8 tracks; K3: the song's volume; K4: the song's tempo | the selected loop's LEVEL, PAN, DELAY SEND, REVERB SEND; on `+ NEW LOOP` the next loop's | K1 / K4 the decks' tempos (with HOLD held: the deck by hand), K2 / K3 their filters or volumes (ENV) | effect knobs |
| keys | play | play; with REC on, write | play | play | PLAY mode: play, written into a take while one records; CONTROL mode (SEQ): white keys mute / solo the loops, black keys QUANTIZE, MONO / POLY, all back on | black keys the decks' PLAY, HOLD, FILTER, the LOADs, SYNC, the blank key the pads' mode (PLAY / LOOPS); white keys the pads | LIVE: white keys punch-ins, black keys the effects; EDIT, MIXER: play |
| EDIT | order (EDIT keeps, HOME drops) | note editor | edit mode | → EDITOR | RE-RECORD the selected loop | — | LIVE: the picked effect on EDIT; EDIT: GATE steps |
| SEL | menu | menu, MARK; blocks; TAKE ITS INSTRUMENT; RECORD PATTERN | slots, menu, MARK; edit, on NAME: type it | — | menu: APPLY INSTRUMENT, DOUBLE, RECORD LIVE, SAVE TO / LOAD FROM PATTERN, CLEAR LOOPS | reset the last knob, or the KEY | EDIT: menu, MOVE in the chain |
| ENV | patterns | patterns; held: the pipette | cartridges; edit: samples, a FUSION slot's pattern | songs | cartridges | what K2 / K3 turn: the filters, the volumes | gate patterns |
| GLO | song properties | pattern properties | cartridge | view options | the loops' properties | DJ's properties | FX properties: ROUTING |
| SEQ | the play mode: ONE PLAY, REPEAT SONG, PLAY NEXT | 8 / 4 / 2 / 1 tracks | edit pages | 8 / 4 / 2 / 1 tracks | the keys: PLAY / CONTROL | SWAP the decks | LIVE / EDIT / MIXER |
| ARP | arpeggiator | row preview | arpeggiator | arpeggiator | the pattern recorder | — | arpeggiator |
| LFO | remove position | clear / note off | reset value | the volume or tempo turned last: 0 dB, 100 % | PEEL: the selected loop goes | the LFO pads; again, back | reset knob |
| REC | — | writing on / off (step by step, the loop playing on); PLAY with it on: the loop pauses, else a realtime take that loops | — | — | records at the selection, one pass: a new loop or a re-record; while the loops play it arms | — (HOME + REC: the [CUT](#cut), the decks playing on) | — |
| OCT | octave | octave | octave | octave | octave | — (HOME + OCT: the mutes' tracks) | effect off / on (EDIT: SEL + OCT the octave) |

**VISUAL, STATUS, SETTINGS and the help** use only a few controls (SELECT, ALGORITHM, K1, K2, SEQ, GLO,
LFO; SETTINGS also EDIT, which runs a tool and opens ABOUT; STATUS's pages turn with SELECT or
ALGORITHM, its ENV opens the song list, as PLAYBACK's does, and its EDIT goes to SETTINGS, as HOME + MONO
does), as their files describe; ARP is the arpeggiator there too (the help's excepted).
