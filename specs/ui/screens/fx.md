# FX

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
section 6.6, its help 13.6.

The effects on the whole mix, the mixer, the metronome and the punch-in effects. FX opens the screen
from anywhere, and FX again goes back to where you were. It has no black key: the FX button is its way in;
it is in the round between VISUAL and LOOPS, in the PLAY group ([../navigation.md](../navigation.md)).

**SEQ** turns three pages, LIVE, EDIT and MIXER, round. The header's subpage names which, white after
`FX` ([../frame.md](../frame.md)). FX opens on the page it showed last, LIVE at first.
- **LIVE:** a live FX control. The white keys run the punch-ins while held, the black keys pick the
  effects (a fixed key each) and BYPASS; the keys play no notes here.
- **EDIT:** the chain as a list, the picked effect's knobs and its picture; the keys play notes, to hear
  an effect while it is shaped. The effect picked is the same on LIVE and EDIT.
- **MIXER:** each track's level, pan, duck amount and sends.

**FX from LOOPS does not leave LOOPS:** a take or a song's recording goes on (going to any other screen
ends it: [loops.md](loops.md)), LIVE and EDIT work on LOOPS' own setup, playing or not, and FX again goes
back to LOOPS. Going on to another screen from there leaves LOOPS then. Wherever the loops play, FX shows
their setup, as it shows the playing song's. Punch-ins played on LIVE are heard and, as FX held + a white
key, written only by RECORD LIVE ([loops.md](loops.md#record-live)).

**FX from DJ does not leave DJ either** ([dj.md](dj.md#effects)): LIVE and EDIT work on DJ's own setup,
which holds CHORUS, DELAY, REVERB, the LIMITER and the METRONOME (the decks' songs keep their GATE, FILTER,
DRIVE, LO-FI and DUCK; those are not shown); its ROUTING is SENDS, and there is no MIXER page. Wherever the
decks play, FX shows DJ's setup. While something not started on DJ plays, FX from DJ sets DJ's setup up
unheard: it is heard once DJ plays ([dj.md](dj.md)).

## The FX setup

**The FX setup belongs to the song; LOOPS and DJ have one of their own.**
- **What it covers:** the chain's order, ROUTING, every effect's knobs and on / off, the mixer, and the
  METRONOME: different songs can count differently.
- **When it applies:** starting a song loads its setup (but the effect under the FX page's cursor, below).
  **At power-on the keys play through THE KEYS' setup and tempo** (the keys' song, below), whatever songs there
  are, as a synth needs no song: the FX page's edits are heard at once, and ARP, GATE, the DELAY's FOLLOW and
  FILTER's envelope run at its tempo; picking a song on SONGS changes nothing. **Once a song has played, its
  setup and tempo stay what the keys play through** after it stops or pauses, until something else plays;
  THE KEYS' come back at the next power-on, or with UNLOAD SONG ([songs.md](songs.md), SONGS' SEL menu and
  PLAYBACK's GLO), which stops what plays at once. A pattern played from the EDITOR uses the current song's;
  the loops use their own. **With LOOPS on screen (FX from LOOPS too) and nothing playing,
  the loops' setup and tempo are what the keys play through,** before the first loop too; a song paused
  meanwhile keeps its place, and once LOOPS is left it is what is held again (PLAY goes on with it). A
  song playing while LOOPS is shown is what is heard.
- **Which setup the FX screen edits:** in LOOPS, the loops'; on DJ, DJ's; elsewhere the playing song's, so that what
  you change is what you hear. With nothing playing, what played last's: the song's, or the loops' (`THE LOOPS`)
  once they have played and stopped, as the keys hear them. Before anything has played (and
  with no song at all), **the keys' song's** (`THE KEYS`): a song the firmware carries for the keys (song `FE`), never listed, TEMPO 120 at 6
  ticks of 4 rows a beat with a new song's setup and the REVERB on at its own settings (SIZE 60, DAMP 40, PRE-DELAY
  10, MIX 25 %), so the FM-1 plays as a synth from its keys alone and sounds round at once; its TEMPO and
  SWING in GLO's properties ([below](#fx-properties-glo)); changed, it is kept as a built-in instrument's change
  is (SAVE: a copy in the user area). Its FX's MIXER has the KEYS column alone (no tracks). GLO's title names
  whose setup it is.
- **CLONE on SONGS** copies the setup with the song.
- **Not part of it:** the punch-in effects ([below](#punch-in-effects)). They are events, not
  settings: a song carries them as commands
  ([../../files/commands.md](../../files/commands.md#punch-ins)); a loop never does.
- **How it is stored:** [../../files/songs.md](../../files/songs.md#the-fx-setup). Each knob is kept
  as its step from the bottom of its range.

**What plays is what the page shows.** A song's `#` commands (knobs, ON / OFF) and `=` commands (the
mixer) change the setup's values as they play. A value a command moved off the setup's is drawn in the
effect's colour (on the MIXER: white, `BY THE SONG`). A knob turned takes over at once.

**The effect under the cursor is yours.** On LIVE and EDIT, while what the page edits plays, nothing but
your hand changes the effect the cursor is on:
- **the song's commands** for it, its knobs and its ON / OFF, are skipped; the other effects follow theirs;
- **a song's start** (PLAY NEXT's next song, a song started again) loads the new setup but that effect,
  which plays on as it was; the others take the new song's;
- **DJ's FX LFO** leaves its MIX alone.

Punch-ins play on: they are events, not settings. The cursor moved off it, or the page left, the effect is
the song's again from its next command. On the MIXER, the mixer shown is skipped the same way
([editor.md](editor.md), commands never change what you are editing).

## Controls

**LIVE:**

| Control | Does |
|---|---|
| white keys | the punch-ins ([below](#punch-in-effects)), each while its key is held; ARP pressed meanwhile holds it on |
| black keys | pick an effect, a fixed key each: F#3 GATE, G#3 FILTER, A#3 DRIVE; C#4 LO-FI, D#4 CHORUS; F#4 DELAY, G#4 REVERB, A#4 LIMITER; C#5 DUCK, D#5 METRONOME (the effects' order, [../../files/commands.md](../../files/commands.md#the-numbers-are-fixed)) |
| F#5 | BYPASS: every effect off while held (the chain and DUCK; the METRONOME clicks on) |
| SELECT | picks the effect too, in the keys' order |
| OCT+ / OCT- | switch the picked effect on / off |
| K1-K4 | its knobs; SEL held: coarse steps (10) |
| ALGORITHM | the knob page (FILTER has 8 knobs, DELAY 5) |
| LFO | resets the last knob turned |
| SEL | the menu: RESET, REVERT |
| EDIT | the picked effect on EDIT (its picture, its place in the chain) |
| SEQ | the next page, EDIT (the header says where: `SEQ 1/3`) |

**EDIT:**

| Control | Does |
|---|---|
| SELECT | picks the effect |
| ALGORITHM | the knob page (for effects with more than 4 knobs) |
| K1-K4 | the effect's knobs; SEL held: coarse steps (10) |
| OCT+ / OCT- | switch the effect on / off, so it can be set first and switched on after; the selected row shows the one that changes it (`OCT+ ON`, `OCT- OFF`) |
| SEL + OCT+ / OCT- | the keys' octave (the keys play notes on EDIT), with its toast (`OCT +3`); the effect stays as it is |
| LFO | resets the last knob turned |
| SEL | the menu: MOVE (reorders the chain), RESET (the effect's defaults), REVERT (every effect back as saved, [below](#the-sel-menu)) |
| GLO | the FX properties, titled whose setup this is (the song's name, THE LOOPS, DJ or THE KEYS): ROUTING ([below](#routing)) and KEYS, for every setup: through the effects or dry ([below](#the-keys-dry)); for THE KEYS, their TEMPO and SWING first |
| EDIT, on GATE | its 16 steps on the white keys ([GATE](#gate)) |
| ENV, on GATE | the GATE patterns |
| ARP | the arpeggiator, every screen's ([../controls.md](../controls.md)): on EDIT and MIXER the keys feed it (not while GATE's steps are edited); on LIVE the keys stay punch-ins, so it plays hands free (the last note played, with LATCH not OFF); pressed while a punch-in's key is held, on any screen, it holds the punch-in on instead ([below](#punch-in-effects)); SETTINGS' ARP RHYTHM, ARP PATTERN and ARP LATCH ([settings.md](settings.md)) |
| SEQ | MIXER |
| FX | back to where you were (on every page) |

**MIXER:**

| Control | Does |
|---|---|
| ALGORITHM | picks the track; 8 show at a time, and tracks `08`-`0F` scroll into view |
| SELECT | picks the value: LVL, PAN, DCK, CHO, DLY, REV (the rows on show) |
| K1 | changes it; SEL held: coarse steps (16) |
| LFO | resets it to its default |
| HOME + white key | mutes a track, as everywhere |
| SEQ | LIVE |

**FX held + a white key** runs a punch-in effect while the key is held, on any screen
([below](#punch-in-effects)).

**The LEDs** ([../leds.md](../leds.md)):
- OCT+ and OCT- on LIVE and EDIT: the picked effect's state lit full (OCT+ when it is on, OCT- when
  off), the other one dim; dark on the MIXER and on items with no switch.
- On LIVE: white keys 1, 5, 9 and 13 dim, where each group of four starts, the others dark; the punch-in
  that runs bright; a black key dim while its effect is on, the picked effect's bright, BYPASS's dark until held.
- EDIT and ENV: dim while GATE is selected (on EDIT); EDIT full while GATE's steps are edited.
- SEL: full while an effect is being moved. ARP: full while the arpeggiator is on, dim while it can come on
  (or hold a punch-in on).
- FX, on every screen: full while a punch-in is held on.
- FX held: as LIVE's white keys.
- Editing GATE's steps: OFF dark, ON half, a ratchet blinks its count (`X2` twice, `X3` three times,
  `X4` four, then a pause, over and over: 240 ms a blink in a 1.4 s round); the playing step's key goes
  dark as the playhead passes. Both from the clock.

## Routing

**GLO → ROUTING:** two models; switching between them loses nothing.

- **GLOBAL:** one chain on the whole mix.
  - Every effect can be moved anywhere in it; only LIMITER stays last.
  - The MIXER has level, pan and the duck amount.
- **SENDS:** CHORUS, DELAY and REVERB leave the chain and become shared sends.
  - There is still one of each, and each track sets how much of its sound goes into each (the
    MIXER's send columns). A pad can be wet while the drums stay dry. The keys send by the MIXER's KEYS
    column, fully at first ([MIXER](#mixer)).
  - Their returns join the chain at a movable item, `◆ SENDS`:
    - the effects above it act on the dry mix only (LO-FI there leaves the reverb tail clean);
    - the effects below it act on everything.
- **What a switch keeps:**
  - Switching to GLOBAL puts CHORUS, DELAY and REVERB where `◆ SENDS` was. Switching back takes them
    out, and `◆ SENDS` takes the place of the first of them.
  - The tracks' send amounts are kept while unused.
- **Cost:**
  - The effects cost the same in both models.
  - Sends add the routing: about 6 multiply-adds a voice a frame (estimated: 1-2 % CPU at 16 voices)
    and ~6 KB RAM.
  - A send no track uses is skipped.

```
GLOBAL                      SENDS (the same setup, switched)
  GATE                        GATE
  FILTER                      FILTER
  LO-FI                       LO-FI      <- dry mix only
  CHORUS                      ◆ SENDS    <- CHORUS, DELAY, REVERB join
  DELAY                       DRIVE      <- everything
  REVERB                      LIMITER    (last)
  DRIVE
  LIMITER    (last)
```

### The keys, DRY

One setting, SETTINGS' KEYS ([settings.md](settings.md)), also in GLO's FX PROPERTIES (its one line for every
setup, saying so): `THROUGH FX` (at first) or `DRY`.

- **THROUGH FX:** the keys as heard go into the mix with everything else, through the chain and the punch-ins,
  with their own PAN, DUCK amount and sends (the MIXER's KEYS column, [MIXER](#mixer)).
- **DRY:** the keys as heard (the FM-1's, MIDI's notes, the arpeggiator's, the previews) skip every effect: the
  chain (the LIMITER, last in it, too), the sends, the punch-ins (not in the punch-in buffer), DUCK. They still
  fire FILTER's envelope with SOURCE `KEYS` ([FILTER's envelope](#filters-envelope)): the keys play the filter on
  the background alone. A Juno-106
  patch's own chorus is the instrument's, not an effect: the keys' notes have their own chorus lines, so it plays on
  dry. The keys join the mix just before the output stage, after the METRONOME: LINE GAIN, MASTER, MONO and the soft
  ceiling (peaks above −1.9 dB rounded off) take them with the rest; USB audio and VISUAL get the whole mix, keys
  included. What the effects work on is then what plays in the background alone.
- **What is recorded** is never touched: a take or a loop's notes play back through the effects as any notes.
- **Cost:** 4,640 B of RAM (the keys' own mix, a block's slot each, 2,048 B; their own Juno-106 chorus lines and
  buses, 2,592 B); about 5 µs a half while keys sound dry (a chorused Juno-106 patch: its chorus too, as it would
  cost anyway), nothing otherwise.

## The effects

| Effect | Knobs: values (default) | Picture |
|---|---|---|
| GATE | PATTERN `00`-`1F`, 16 built in (`00`-`0F`) and 16 of yours (`00`); RATE 1/8, 1/16, 1/32, 1/8T, 1/16T (1/16); DEPTH (100 %); SMOOTH (20 %) | 16 steps with ratchets and the playhead |
| FILTER | CUT, in Hz: low-pass to the left (`LP 100` … `LP 20K`), OFF, high-pass to the right (`HP 20` … `HP 8.0K`) (OFF); RESO (30 %); then its envelope ([below](#filters-envelope)): ENV, how far a note opens it (0 %: off); SOURCE `KEYS`, a track (`TRACK 00` … `TRACK 0F`), `4 BEATS`, `2 BEATS`, `BEAT`, `1/2 BEAT`, `1/4 BEAT` (`KEYS`); ATTACK 2 ms-3 s (`2.0 MS`); DECAY 5 ms-5 s (`296 MS`); SUSTAIN (0 %); RELEASE 5 ms-5 s (`296 MS`) | the response in dB at the cutoff now, the cutoff, the peak; with ENV, where it rests and how far it opens |
| DRIVE | DRIVE (30 %); TONE (50 %) | the harmonics it adds |
| LO-FI | BITS 2-16 (8); RATE 1-44 kHz (22); DUST (20 %) | a sine crushed |
| CHORUS | MODE I, II, I+II: Juno-style (I); RATE (30 %); DEPTH (50 %); MIX (50 %) | the pitch each side gets |
| DELAY | TIME 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/2T, 1/4., 1/2, up to 1 s, with PING-PONG up to 0.5 s: a longer one plays the longest that fits (1/8.); FEEDBACK (40 %); TONE, each repeat's lowpass, up to ~13 kHz (60 %); PING-PONG OFF, ON (OFF); MIX (30 %). Echoes are mono (one line: [../../files/ram.md](../../files/ram.md#where-the-ram-goes)); PING-PONG puts them left and right in turn | the echoes on a bar's beats |
| REVERB | a plate, Dattorro's: SIZE, its decay, about 1.3 s to 11 s to −60 dB (60 %); DAMP, the tail's lowpass (40 %); PRE-DELAY 0-100 ms (10); MIX (25 %) | the decay relief |
| LIMITER | CEILING −12 to 0 dB (−1); RELEASE 10-1000 ms in 10s (100) | the curve and the release in one box |
| DUCK | SOURCE a track, `00`-`0F`, or `KEYS` (`00`); DEPTH (60 %); RELEASE 20-1000 ms in 10s (150): the dip comes back in a straight line over it | the tracks' dips and the curve |
| METRONOME | TEMPO FOLLOW or 30-300 BPM (FOLLOW); BEATS A BAR 1-16 (4); SOUND CLICK, BEEP, WOOD, COWBELL (BEEP); LEVEL (60 %). A count-in before a take is not the METRONOME's: COUNT-IN, [../transport.md](../transport.md#count-in) | the bar's beats |

**The CPU an effect takes** is measured on the device: the timer read before and after each effect in
each audio block, summed, the footer's figure the average of the last quarter second, redrawn 4 times a
second; only while the FX screen is open. The playground, whose audio worklet has no fine timer, shows
these figures as estimates (GATE 0.4 … REVERB 7.5, [Texts](#texts)).

A knob shown with only a default runs 0-100 %. At power-on and in a new song, only LIMITER is on.

- **DUCK and the METRONOME are not part of the chain.** EDIT lists them after it, apart, and MOVE
  does not reach them.
- **In SENDS,** the MIX knob of CHORUS, DELAY and REVERB becomes their return level.
- **MOVE** (SEL → MOVE) floats the selected effect: SELECT moves it through the chain, heard in its new
  order as it moves; SEL puts it down (`MOVED`), HOME puts the chain back as it was (`NOT MOVED`).
  LIMITER, DUCK, the METRONOME and, in SENDS, the three sends do not move (`DELAY DOES NOT MOVE`);
  nothing moves below LIMITER ([../dialogs.md](../dialogs.md)).

### Effects at rest

An effect fed nothing costs (almost) nothing, so that the CPU with nothing playing is the same whatever the
song's effects are:
- **CHORUS, DELAY, REVERB, the punch-ins' buffer:** fed only silence for a whole turn of their lines (the CHORUS
  23 ms, the REVERB ~0.15 s, the DELAY and the buffer 1 s and 0.5 s), they rest: skipped while nothing comes
  in, their clocks (an LFO, where the lines stand) going on, so they wake with the same sound to the sample.
  The DELAY's and the REVERB's feedback low-passes round toward 0 and stop a step short of it: a tail ends in
  a few units circling under the output's last bit. Once what they give has stayed within 2 units of 0
  (16-bit scale, −84 dB) for a whole turn, those remains are cleared and they rest. Measured on the PC: after
  STOP every demo song's DELAY and REVERB rest within 1-4 s. Resting, the DELAY and the REVERB skip their
  44.1 / 29.4 kHz converters too, once those hold only 0s: only where they stand moves, and that goes round
  every 3 frames (2 samples down and back), so a block moves it 3 frames and its odd one, not 64.
- **The DJ FILTERs** on a paused deck rest the same way (their remains cleared after 46 ms of at most 2 units
  out).
- **The rest** (GATE, FILTER, DRIVE, LO-FI, the LIMITER) cost little on silence and run as they are: LO-FI's DUST
  is a sound of its own.
- **A silent block at rest is not looked at:** a block (64 frames) to which no voice and no Juno-106 chorus gave
  anything, while every effect at work rests (the CHORUS, DELAY and REVERB resting, the LIMITER fully open, the
  punch-ins' buffer holding only silence, the METRONOME with no click starting in that block; GATE, FILTER,
  DRIVE, LO-FI, a click ringing, a punch-in or all sound off fading each count as at work), goes through as
  silence without a frame read: the effects'
  clocks and places move on as their rest moves them, the mix stays cleared for the next block, and VISUAL and
  USB audio get its silence. The same sound to the sample; with nothing playing the CPU is mostly the player's
  bookkeeping.

[CUT](../controls.md#cut) empties every effect at once.

### DUCK

DUCK works on the tracks before they are mixed, so its source is never lowered with the rest:
- **The source** is a track, or the keys (`KEYS`: every note they play, MIDI's and the arpeggiator's too, so
  what plays behind can duck under a part played live). Its level is followed, rising at once and falling over
  RELEASE.
- **Every other track** is lowered by that level × DEPTH × the track's own amount, the MIXER's DCK
  row (none by default, as [../../files/songs.md](../../files/songs.md) keeps the mixer). The bass and
  the pad can duck fully while the hats do not. The keys duck the same by the KEYS column's DCK (none
  at first), through the effects only (not DRY), unless they are the source: a source never ducks itself.
- **The sends** take a track's sound after ducking, so a ducked pad's reverb ducks with it.
- **The cost:** following the source adds one addition a frame for each of its voices, and each
  track's gain is updated once a block, eased over the block so it never clicks: well under 1 % CPU.

### GATE

- **A pattern** is 16 steps, each OFF, ON, `X2`, `X3` or `X4` (ratchets).
- **RATE:** 1/8, 1/16, 1/32, 1/8T or 1/16T.
- **Locked to the song's position:** at 1/16 the 16 steps are a bar (4 beats), step 1 on the bar's first
  beat; a tempo change keeps it in place. With nothing playing it runs on from the last bar at the
  tempo; in LOOPS it follows the loops' position.
- **The patterns:** 16 built in, and 16 of your own: `00` STRAIGHT, `01` OFFBEAT, `02` TRANCE, `03`
  HALF, `04` GALLOP, `05` SKIP, `06` SPARSE, `07` ROLL, `08` STUTTER, `09` DOTTED, `0A` SYNCOPE, `0B`
  PULSE, `0C` RAMP, `0D` BREAK, `0E` TRIPS, `0F` CHOP; yours `10`-`1F`, named `MY GATE 10` …
  `MY GATE 1F` (`MY GATE 10` all on, the rest empty).
  - **EDIT on GATE:** the white keys edit the steps. A key switches its step on and off: an OFF
    step comes on as the key goes down, an ON one (or a ratchet) goes off as it comes up. A key held,
    ALGORITHM sets its step's ratchets: ON, `X2`, `X3`, `X4`, clockwise more, never off (every step
    whose key is held alike); a step it turned stays on as its key comes up. EDIT again keeps the changes; HOME drops them ([../controls.md](../controls.md)): the gate patterns
    and GATE's knobs as they were, a built-in pattern's copy among yours included. A built-in pattern
    is not edited itself: EDIT copies it into the first of yours that is empty (all off, or one step),
    else `MY GATE 10`, and PATTERN moves there (`STRAIGHT COPIED TO MY GATE 11`).
  - **ENV on GATE:** the pattern list. SELECT picks; ENV takes it.

### FILTER's envelope

Notes, or the beat, open the filter and let it close, as a synth's filter envelope does. FILTER works on the whole
mix (or wherever it sits in the chain), so there is **one envelope**, not one a note. Its knobs are FILTER's second
to eighth: page 1 K1 CUT, K2 RESO, K3 ENV, K4 SOURCE; page 2 (ALGORITHM) K1 ATTACK, K2 DECAY, K3 SUSTAIN, K4 RELEASE.

- **CUT is where the filter rests** between notes: the mix sounds as FILTER without an envelope sounds.
- **ENV is how far a note opens it,** as a part of the way from CUT to fully open, counted in octaves (CUT's own
  scale): a low-pass opens upward toward LP 20K, a high-pass downward toward HP 20. ENV 100 % opens it all the way.
  At 0 % FILTER is as it would be without the envelope, to the sample. CUT at OFF: no filter, and the envelope has
  nothing to move.
- **The envelope, an ADSR:**
  - **ATTACK:** from where the envelope is to fully open (as far as ENV says), in a straight line at one pace: from
    rest it takes ATTACK, from halfway open half of it. 2 ms at least, so no hit clicks.
  - **DECAY:** down toward SUSTAIN on a curve, e times closer each quarter of DECAY (98 % of the way at its end).
  - **SUSTAIN:** the level held while the source's note is held, a part of ENV's opening.
  - **RELEASE:** from where it is back to CUT, on the same curve, once the source lets go.
  - **One envelope for many notes:** each new note of the source starts the attack again from wherever the
    envelope is (no jump, no click); it holds while any of the source's notes is held, and releases with the last.
  - **The times:** ATTACK 2 ms to 3 s, DECAY and RELEASE 5 ms to 5 s, 128 steps each, spaced evenly on a log scale
    (step *i*: 2 ms × 1500^(*i* / 127), 5 ms × 1000^(*i* / 127), rounded to 0.1 ms); shown in tenths of a ms under
    10 ms (`2.0 MS`), in whole ms under a second (`296 MS`), then in seconds (`1.22 S`).
- **SOURCE, what fires it:**

  | SOURCE | Fires | Held |
  |---|---|---|
  | `KEYS` | each note the keys play: the FM-1's, MIDI's, the arpeggiator's | while the key is held |
  | `TRACK 00` … `TRACK 0F` | each note that track starts | until the note ends (its track's next note, a note off, NOTE CUT, a FUSION layer's end) |
  | `4 BEATS`, `2 BEATS`, `BEAT`, `1/2 BEAT`, `1/4 BEAT` | the beat of what plays, every so many beats counted from the song's start (`4 BEATS`: on beats 0, 4, 8 …) | half its time (`BEAT`: half a beat; `4 BEATS`: two beats), then RELEASE |

  - **The keys DRY** ([The keys, DRY](#the-keys-dry)) still fire it: the keys play the filter on the background
    alone.
  - **A track fires it heard or not:** a muted track's notes fire it too, so a silent track can carry a rhythm for
    the filter.
  - **The beat** is what plays's music clock (the song's, the loops', a DJ deck's), as GATE's steps and the DELAY's
    note lengths follow it. A hit comes in the block the clock reaches it; a clock that jumps (a start, a skip back)
    counts from where it lands, so a song's start on a beat fires at once. With nothing playing it runs on from the
    last bar at the tempo, as [GATE](#gate) does: the keys, THROUGH FX, play into a filter pulsing in time.
  - **In LOOPS** SOURCE keeps a track, as everywhere (a loop's number changes as a loop above it is peeled, its
    track does not), and shows the loop playing there: `LOOP 03`, or `TRACK 05` where no loop is.
  - **On DJ** each deck plays its song as made, its FILTER and the envelope with it, on that deck's own tracks and
    beat ([dj.md](dj.md#effects)); DJ's FX does not show them. `KEYS` does nothing there (the keys play no notes on
    DJ). The DJ FILTER is another filter after the song's, with no envelope: the LFO pads swing it.
- **A block at a time:** the envelope sets the cutoff at each block's start (64 frames, 1.45 ms), at once, where
  CUT turned by hand glides (~46 ms end to end, against zipper noise). *(Proposed: whether those steps are heard,
  with a high RESO and a 2 ms attack, is to be listened to on the FM-1; 16-frame steps where they are.)*
- **Cost:** a few multiplies a block while ENV is above 0 %, nothing at 0 %; the player marks the keys' and the
  tracks' notes started and held, a bit each a block.

### METRONOME

- It is mixed after the effects and never takes a voice.
- TEMPO FOLLOW uses the song's or the loops' tempo, counting while they play; on THE KEYS (nothing played
  since the start or UNLOAD SONG) it counts at THE KEYS' TEMPO, as ARP and GATE follow it. A number sets its
  own, so it works with nothing playing.
- FX EDIT's picture lights the beat it clicked last, at FOLLOW or at a number, with nothing playing too.
- BEATS A BAR in the loops' setup is also the loops' bar ([loops.md](loops.md#the-loops)).
- **Its sounds are its own,** made as it clicks: no sample, no instrument, nothing from the disk, so it
  works with an empty one. Each click lasts 80 ms, the same on both sides; *t* is the time since the
  click (s), the accent the bar's first beat:

  | SOUND | The click |
  |---|---|
  | CLICK | white noise × e^(−300 *t*): gone in about 15 ms |
  | BEEP | sin(2π *f t*) × e^(−60 *t*), *f* 1000 Hz, 1500 Hz on the accent |
  | WOOD | sin(2π *f t* (1 − 2 *t*)) × e^(−90 *t*), *f* 800 Hz, 1100 Hz on the accent: its pitch falls as it rings, a third lower at its end |
  | COWBELL | 0.4 × (square(540 Hz) + square(800 Hz)) × e^(−40 *t*), square a ±1 square wave at that frequency: the TR-808's two tones |

  Then × LEVEL × 0.5 on the accent, × 0.32 on the other beats.

## Punch-in effects

**FX held + a white key** runs that effect on the whole mix while the key is held, locked to the tempo. It works on every screen.

The 16 are four groups of four keys, one kind a group; the rates line up across the groups, each
gate under the loop of its note value:

| Keys | The group | Its four effects |
|---|---|---|
| 1-4 | loops | loop 1/4, loop 1/8, loop 1/16, loop 1/32 |
| 5-8 | chops | stutter 1/16T, bit crush, gate 1/16, gate 1/32 |
| 9-12 | filters and space | low-pass sweep, high-pass sweep, echo throw, reverb swell |
| 13-16 | tape moves | reverse, half speed, tape stop, vinyl stop |

This order is the keys' alone: a punch-in's command keeps its code wherever its key moves
([../../files/commands.md](../../files/commands.md#the-numbers-are-fixed)).

- **The keys light:** while FX is held (and on LIVE), white keys 1, 5, 9 and 13 dim, where each group of
  four starts, so the keys can be found by touch; the others dark; the effect that runs bright.
- **Let go, a punch-in fades back into the mix over 80 ms,** short but heard, never a jump (vinyl
  stop has its own spin-up, below). A punch-in command's end fades the same way. The two sweeps also fade
  in over 80 ms as they are pressed, so the filter's start makes no bump.
- **Echo throw and reverb swell work in the setup's DELAY line and REVERB plate** (the FM-1 has one of
  each, no RAM for more): while one holds its line or plate, the setup's DELAY or REVERB, and DJ's ECHO
  OUT or REVERB OUT, is not heard.
  - **Echo throw** sends the mix into the line: a dotted 1/8, 65 % feedback, ping-pong, TONE 60, its
    echoes at 80 %, gliding there from the DELAY's MIX over 80 ms as it is pressed (the line's echoes go on
    at the throw's time). Let go, nothing more goes in and the echoes ring on for 2.4 s, as a throw does;
    then the line is the DELAY's again, its MIX gliding in from the throw's over 80 ms.
  - **Reverb swell** sends the mix into the plate (SIZE 95, DAMP 20, no PRE-DELAY), rising over two beats
    from where the REVERB was (its wet, its dry, the mix it was fed; nothing with REVERB off), its tail
    going on in the plate, to its own: the wet at 120 %, the sound under it at half. Let go, the plate is
    the REVERB's again at once:
    the swell's tail rings on in it at the REVERB's settings, its wet and dry gliding from the swell's to
    the REVERB's over 80 ms, so the reverb never drops out. With REVERB off, the swell fades out over
    80 ms and its tail is emptied (none comes back when REVERB is switched on).
  - Another punch-in pressed over either gives the line or the plate back the same way. Another setup
    taken (a song's, REVERT, RESET, a chain or ROUTING edit, CUT held 2 s) ends an echo throw at once
    (its echoes left in the line for the setup's DELAY) and lets a reverb swell go.
- **ARP holds a punch-in on:** pressed while a punch-in's key is held (FX held + the key, or the key on
  LIVE), it keeps the punch-in running after the key is let go (`LOOP 1/4 HELD ON: ITS KEY STOPS IT`),
  on any screen, so the hands are free. The FX button lights full while one is held on.
  - **It stops** when its key is pressed again (FX held + the key, or on LIVE the key alone), as if let
    go; another punch-in's key replaces it, running while its own key is held (ARP holds that one on
    in turn); HOME + REC (all sound off) stops it too.
  - Leaving LIVE stops a punch-in held there by its key, but not one held on by ARP.
- **Vinyl stop** works like a turntable's STOP button and a hand on the record:
  - **pressed:** the speed falls to zero in 0.5 s, whatever the tempo (a turntable's brake stops the
    platter in half a second to a second);
  - **held:** silence;
  - **let go:** the sound spins back up in about 0.1 s, straight into the song: the spin-up plays the
    last 50 ms heard, so the song, which kept time underneath, returns on the beat.
  - **Tape stop** (key 15, beside it) is its sibling synced to the tempo: it winds down over about a beat
    (0.5 s at 120 BPM; at most 1 s, under 60 BPM, so that what it plays stays in the buffer), as an
    effect rather than a cut. The vinyl stop takes the same time at any tempo; the tape stop follows the
    song's, so it is the slower of the two under 120 BPM and the faster over it.
  - **Stopped, both are silent,** not a sample held: the sound fades out over the last tenth of the fall.
- **Half speed** plays the mix from the moment pressed at half speed, so it falls behind by half the
  time held; once that reaches the buffer (after 1 s), it starts again from the moment, a short fade over
  the seam: held long, each second plays the half second that began it.
- **Some punch-ins record the last beat:** the loops, the stutter, reverse, tape stop, half speed and
  vinyl stop.
  - **The buffer holds the last 0.5 s as its middle and its sides** (36,750 B:
    [../../files/ram.md](../../files/ram.md#where-the-ram-goes)): the middle, (L + R) / 2, at 29.4 kHz,
    as the DELAY and the REVERB run, so a replayed slice keeps everything up to ~13 kHz; the sides,
    (L − R) / 2, at 7.35 kHz, so it keeps the song's stereo up to ~3 kHz: the pans of drums, bass, chords
    and pads stay where they were, and only the width of the top (hats, cymbals, the REVERB's air) comes
    to the middle. Played back, left = middle + sides and right = middle − sides, so a mono output hears
    the middle exactly. The middle is recorded through a 3:2 converter and the sides through a 6:1 one
    while anything plays; both are read back through theirs only while a punch-in plays.
  - At 60 BPM a 1/4 note is 1 s: 73.5 KB kept so; at 120 BPM, 36.75 KB. A replay stays a few frames
    behind the newest (under 1 ms: the converters' own delay) and keeps clear of the oldest, so a beat fits
    from 121 BPM up. Where a beat does not fit (slower tempos), these punch-ins use the longest note
    value that does: a 1/4 loop becomes a 1/8 loop, reverse plays the last 1/8 backwards.
  - The punch-ins that do not replay the buffer (bit crush, the gates, the sweeps, echo throw, reverb
    swell) work on the live mix, in full stereo at the full rate.
  - The buffer records whenever anything plays, **except while a loop, the stutter or reverse plays from
    it:** it keeps what they replay, however long they are held, and records again once they end.
  - The playground's buffer is the same, 0.5 s kept as middle and sides at those rates, so it sounds as
    the FM-1 will.
- **Punch-ins are commands too** (`!08`: [../../files/commands.md](../../files/commands.md#punch-ins)):
  a pattern's cell can hold one, played exactly as if its key were held for that many rows, so songs
  can carry them. **In LOOPS a punch-in is heard and never written into a loop;** RECORD LIVE writes it
  into its song as that command, at the row pressed and for the rows held ([loops.md](loops.md#record-live)).
- **The set is a first pass,** to be trimmed or changed once tried and its costs measured
  ([../../open-questions.md](../../open-questions.md#the-ui-ui)). FX held + black keys are kept for later.

## MIXER

A grid of all 16 tracks, with the tracks across and their values down. The track count belongs to each
pattern, so the mixer always has all 16; a track no pattern of the song uses is dim.

- **The values:** LEVEL, PAN and DUCK (DCK: how much the track ducks; `S` marks DUCK's source). With
  ROUTING at SENDS, also the sends: CHORUS, DELAY, REVERB.
  - The DCK row is shown only while DUCK is on.
  - LEVEL, DUCK and the sends run `00`-`7F` (LEVEL `7F` by default, the rest `00`); PAN `00`-`FF`,
    `80` the middle.
- **Muted tracks** are drawn dark. Muting is HOME + white key, as everywhere.
- **KEYS, the last column:** the keys' own strip (the FM-1's keys, MIDI's notes, the arpeggiator's, the
  previews, as heard; what is recorded plays on its tracks), **one for every setup**, kept with the
  settings ([../../files/settings.md](../../files/settings.md)): the same in every song's MIXER, the loops'
  and the keys' song's; DJ has no MIXER (the keys play no notes there).
  - **LVL is SETTINGS' KEYS GAIN** ([settings.md](settings.md)), −24 to +12 dB, set from either place; its
    bar fills by (dB + 24) / 36. K1 turns it by 1 dB, with SEL held by 6.
  - **PAN** `00`-`FF` (`80` at first), live on the keys' notes; **DCK** `00`-`7F` (`00` at first): DUCK's
    source lowers the keys as it does a track ([DUCK](#duck)); with the keys as DUCK's source, `S` in their cell; **CHO, DLY, REV** `00`-`7F` (`7F` at
    first): in SENDS the keys go into each send by its amount, so out of the box they hear every send
    that is on, each at its MIX.
  - LFO resets a value: `0 DB`, `80`, `00`, `7F`.
  - **With SETTINGS' KEYS at DRY** only LVL and PAN act: DCK and the sends are drawn dark grey and the
    words line ends in `  DRY`.
  - It is a setting: changed, it is unsaved until SAVE (SEL's REVERT on SETTINGS takes it back), and
    no command sets it.
  - The columns go 8 a page: `00`-`07`, `08`-`0F`, then KEYS alone on the third; the loops' MIXER has
    KEYS after the last loop, and alone before the first; the keys' song's has KEYS alone.
- **The loops' mixer** is shown in LOOPS: `MIXER: THE LOOPS`. Its columns are the loops, up to 16, a
  column a loop (`00`-`0F`; a muted or silenced loop's dark), then KEYS, a loop being one track with
  its mix ([../../files/loops.md](../../files/loops.md#the-record)): LEVEL, PAN, DELAY and REVERB SENDS
  as K1-K4 on LOOPS set them, CHORUS and DUCK here alone ([loops.md](loops.md#the-loops)). The next
  loop's mix is set on LOOPS, by K1-K4 on `+ NEW LOOP`. The footer and the words line name it `LOOP 03`.

## Layout

The body is y 17-226 (`Y0` = 17); the frame's header and footer: [../frame.md](../frame.md).

### LIVE

FILTER picked, TAPE STOP held:

```
FX LIVE                  SEQ 1/3 >  |####   S: the name, the subpage white; T: SEQ's page (the frame's)
PUNCH-INS: HOLD A KEY; ARP HOLDS IT ON    T, cyan
[LOOP 1/4 ][LOOP 1/8 ][LOOP 1/16][LOOP 1/32]   the punch-ins on chips, a row a group of four keys
[STUTTER  ][BIT CRUSH][GATE 1/16][GATE 1/32]
[LP SWEEP ][HP SWEEP ][ECHO THRW][REV SWELL]
[REVERSE  ][HALF SPD ][TAPE STOP][VINYL STP]   the one held red
----------------------------------------
EFFECTS: BLACK KEYS                       T, cyan
GATE [FILTER] DRIVE       LO-FI CHORUS    as the black keys sit: groups of 3 and 2
DELAY REVERB LIMITER      DUCK  METRO
BYPASS
----------------------------------------
FILTER  ON                                S: the picked effect; `ALG 1/2` at the right when it has more than 4 knobs
K1 CUT    K2 RESO                         T: the knobs, their values, a bar each
LP 3.8K   40%
=====     ====
SELECT FX  OCT OFF/ON  EDIT ITS PICTURE   T: the hint
----------------------------------------
01:00 EL PIANO DX7         FILTER 1.2% CPU
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| `PUNCH-INS: HOLD A KEY; ARP HOLDS IT ON` | 0, 18 | | T | `HELD ON: ITS KEY OR ANOTHER STOPS IT` while a punch-in is held on |
| punch-in *k* (0-15) | chip at `60 (k mod 4) + 1`, `31 + 13 ⌊k / 4⌋`, 58 × 12; its short name at the chip's x + 2, y + 1 | | T | key *k* + 1's punch-in ([below](#punch-in-effects)) |
| the line | 0, 86 | 240 × 1 | | |
| `EFFECTS: BLACK KEYS` | 0, 89 | | T | |
| effect *b* (black key *b*, 0-10) | x 2, 41, 86 (a group of three), 163, 202 (a group of two, against the right edge); y `103 + 15 r`, r = 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2 | | T | GATE, FILTER, DRIVE, LO-FI, CHORUS, DELAY, REVERB, LIMITER, DUCK, `METRO`, BYPASS |
| the picked effect's box | its name's x − 3, y − 2 | its width + 5 × 14 | | a 1-px box |
| the line | 0, 148 | 240 × 1 | | |
| the picked effect | 0, 152 | | S | its name and `  ON` / `  OFF` |
| the knob page | right-aligned to 238, 154 | | T | `ALG 1/2`, `ALG 2/2`: only for an effect of more than 4 knobs (FILTER, DELAY), where ALGORITHM turns the page |
| knob *k* | `60 k + 2`, 172: `K1 ` and its name (6 characters); its value at y 185 (9 characters); its bar at y 200, 54 × 3 | | T | |
| the hint | 0, 214 | | T | `SELECT FX  OCT OFF/ON  EDIT ITS PICTURE` (SEQ's pages are the header's) |

Colours: the chips light grey's deep shade, their names light grey; the one held red, its name white (black where
the theme's red is nearer white: [../colours.md](../colours.md#what-carries-meaning)).
An effect's name in its colour while on, light grey while off (BYPASS light grey, red while held); the
picked one's box white. The picked effect's name in its colour (dark grey while off), `ON` light grey,
`OFF` dark grey; the knob page's `ALG` yellow, its `1/2` light grey. `K1`-`K4` yellow, the names light grey, the values in the
effect's colour (the last turned white); the bars' track dark grey's deep shade, their fill the
effect's colour. The hint's control names yellow, the rest light grey. Labels cyan; lines dark grey.

### EDIT

GLOBAL routing, GATE selected:

```
FX EDIT                  SEQ 1/2 >  |####   S, 16 px: the name, the subpage white; SEQ's page; the transport, CPU, battery
GATE           OFF  ##########  OCT+ ON   T, a row of 11 px; the selected boxed
FILTER         OFF  ..........
LO-FI          OFF  ..........
CHORUS         OFF  ..........
DELAY          ON   ####......
REVERB         ON   #######...
DRIVE          OFF  ..........
LIMITER        ON   ##........
----------------------------------------  the line before the items apart
DUCK           ON   ..........
METRONOME      OFF  ..........
----------------------------------------
K1 PATTRN K2 RATE   K3 DEPTH  K4 SMOOTH   T: the knobs' names
00        1/16      100%      20%         T: their values
STRAIGHT                                  the picture, to y 224
 |#| |#| |#| |#| |#| |#| |#| |#| ...
----------------------------------------
10:12 EMPTY FUSION         GATE 0.4% CPU  T, the footer
```

In SENDS, `◆ SENDS` takes its place in the chain and CHORUS, DELAY and REVERB follow it, indented:

```
GATE           OFF  ..........
FILTER         OFF  ..........
LO-FI          OFF  ..........
<> SENDS
  CHORUS       OFF  ..........
  DELAY        ON   ####......
  REVERB       ON   #######...
DRIVE          OFF  ..........
LIMITER        ON   ##........
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| row *i* of the list | 0, `18 + 11 i` | 240 × 11 | | the chain in its order, then DUCK and METRONOME |
| the selected row's box | 0, `17 + 11 i` | 240 × 12 | | a 1-px box |
| the line before DUCK | 0, the DUCK row's y − 1 | 240 × 1 | | |
| the effect's name | 4 (16 for the three sends in SENDS), row y | | T | its name |
| `◆ SENDS` | the diamond at 4, y − 1 (7 × 7); `SENDS` at 14, y | | T | the sends' return |
| ON / OFF | 100, row y | | T | the switch |
| the activity meter | 128, y + 3 | 60 × 5 | | its track, then its fill: how hard the effect works now, in dB, a pixel a dB from −60 dB (none) to 0 dB (full), of full scale. The measure: an effect in the chain \|RMS after − RMS before\| × 4 + RMS before / 2 (at most full); a send its change × 3; the METRONOME what its clicks add, as a chain effect's; DUCK its level (its source's, as it ducks). Each held to its peak, falling × 0.85 a measured block (a send × 0.8), an effect off (or its line a punch-in's) × 0.8; measured only while EDIT shows, on one audio block in 4; redrawn only when its length changes |
| the switch hint (selected row) | 190, row y | | T | `OCT+ ON` while off, `OCT- OFF` while on |
| the knobs' line | 0, `K = 18 + 11 n + 2` (*n* rows) | 240 × 1 | | K = 130 in GLOBAL, 141 in SENDS |
| knob *k*'s name | `60 k`, K + 3 | | T | `K1`-`K4` and the knob's name, 6 characters at most |
| knob *k*'s value | `60 k`, K + 15 | | T | its value, 9 characters at most (as wide as the name's line, 54 px: 6 px before the next knob) |
| the knob page | right-aligned to 238, K + 30, on black over the picture | 46 × 12 | T | `ALG 1/2` when the effect has more than 4 knobs (ALGORITHM turns the page); not on the values' line, where K4's value is |
| the picture | 2, K + 30 | 236 × (226 − K − 32) | | the selected effect's ([below](#the-pictures)) |

The knobs' short names: PATTERN `PATTRN`, FEEDBACK `FEEDBK`, PRE-DELAY `PRE-DL`, CEILING `CEIL`,
RELEASE `RELEAS`, SUSTAIN `SUSTN`, BEATS A BAR `BEATS`, PING-PONG `PINGPG`; the others as they are.

Colours: an effect's name in its own colour while on, dark grey while off (GATE light blue, FILTER
cyan, DRIVE light red, LO-FI yellow, CHORUS light cyan, DELAY light green, REVERB light magenta,
LIMITER white, DUCK brown, METRONOME green); `◆ SENDS` light blue. ON green, OFF dark grey, either in
the effect's colour when a command switched it. The meter's track deep grey, its fill the effect's
colour (none while off). The selected row's box white, yellow while it moves. In the switch hint
`OCT+` / `OCT-` yellow, `ON` / `OFF` dark grey. Knob names: `K1`-`K4` yellow, the name cyan; values
light grey, the last one turned white, one a command holds in the effect's colour; the knob page's `ALG`
yellow, its `1/2` dark grey. Lines dark grey.

### The pictures

In the picture's box (x 2, y `P` = K + 30, w 236, h `H` = 226 − K − 32: 64 px in GLOBAL, 53 in SENDS),
in the effect's colour (*C*) while it is on, dark grey while off. **A picture follows the knobs, never
what plays** (it is drawn again only when a knob changes; GATE's playhead and the METRONOME's beat follow
the clock, FILTER's cutoff its envelope), and **it never writes a value a knob already shows**: only other units (Hz, ms, cents),
results (dB a repeat, a peak) and names.

| Effect | Drawn |
|---|---|
| GATE | the pattern's name at 2, P (T); the 16 steps: step *i* a 1-px box at `6 + 14 i`, P + 16, 11 × 28, filled with as many bars as hits (ON 1, `X2`-`X4` 2-4), each 7 px wide, stacked; the step playing behind it, 13 × 30, deep blue |
| FILTER | the response in dB (+18 at the top to −24, 42 dB in H − 16 px) over 20 Hz-20 kHz across (log; lines and `100`, `1K`, `10K` under them, dark grey); 0 dB a dark grey line; filled under the curve in *C*'s deep shade, the curve in *C*; the cutoff a line in *C*; `PEAK +12 DB` (light grey, on black over the lines) at the top, on the side away from the cutoff, clear of the knob page (`ALG 1/2`), when RESO lifts it over 1 dB; OFF: the flat line only. With ENV above 0 %: CUT, where it rests, a dotted light grey line, and ENV's reach (CUT moved ENV of the way to fully open) a dotted yellow one; the response and the cutoff where the envelope has the filter now, drawn again as it moves; at each hit of the source the reach line's top 6 px white for 0.1 s |
| DRIVE | `HARMONICS` (light grey) at 2, P; what DRIVE adds to a 441 Hz sine at −12 dB, TONE's lowpass included: 8 bars, the odd harmonics 1-15 (DRIVE's small bias adds even ones too, far lower), each in a 29-px slot, 17 px wide, as high as its level from −72 dB (H − 26 px), the fundamental white, the others *C*; their numbers under them (dark grey); TONE's cutoff a dotted light grey line among them (none when OPEN); at the right `+2 DB  TONE 5.1 KHZ` (*C*): a quiet sound's gain, the cutoff |
| LO-FI | a sine, two cycles across 236 px (a 750 Hz sine at full level, 2 px a sample), in *C*'s deep shade; the crushed one over it in *C*: BITS' steps, RATE's holds in whole samples, each taken at its hold's middle, joined by vertical lines; DUST as light grey specks from a fixed scatter of 400, each shown when its draw is under DUST; nothing written |
| CHORUS | `L` (*C*) `R` (light grey) at 2, P; the pitch each side gets, in cents, over 3 s (x 28-238): left in *C*, right light grey, opposite; the scale by MODE, its widest swing (I ±30, II ±80, I+II ±160 cents), `+30`, `0`, `-30` at the left (dark grey); lines at 1 s and 2 s; `0`, `1 S`, `2 S`, `3 S` under; at the right `0.36 HZ  +-7 CENTS` (*C*) |
| DELAY | a bar's 4 beats (lines dark grey's deep shade, `1`-`4` under); the dry a white bar at the start; each echo a 3-px bar at its time, its height in dB down to −48 (half the area each way from the middle line), deep *C*, its treble (5 kHz, after TONE's lowpass once a repeat) bright *C* inside it; PING-PONG's echoes up (left) and down (right) in turn, `L` / `R` at the right; at the right of P, clear of the knob page (`ALG 1/2`): `321 MS  -9 DB EACH  TONE 8.2K` (`PLAYS 1/8.` first when a shorter value plays than TIME's; `ONCE` at FEEDBACK 0; `OPEN`); where it would not clear it, single spaces, then without TONE's part |
| REVERB | the decay relief: time across (0 to 2, 4, 8 or 12 s, whichever holds the decay; seconds under it), 12 bands bass (bottom) to treble (top), 3 px each (2 in SENDS), each shaded as it decays at its own rate (DAMP shortening the treble): above −12 dB *C*, to −24 *C* and its deep shade in a checkerboard, to −40 the deep shade, to −60 the deep shade every other pixel; PRE-DELAY the gap at the start; at 2, P the decay time (`7.5 S`, *C*), at the right `TREBLE 5.2 S` (dark grey) |
| LIMITER | one box, one output axis (−24 to +6 dB, up): at the left (70 px) the input across, −24 to +6 dB: the clean diagonal dark grey, the output *C*, flat at CEILING, the gain taken off between them in red's deep shade; a dark grey line, then time, 0-1 s after a +6 dB peak: a sound held at −12 dB coming out lower and climbing back over RELEASE, the gain still taken off in red's deep shade, a dotted light grey line at RELEASE; CEILING dotted light grey across both; `IN`, `+6`, `0`, `1 S` under (dark grey); nothing written above |
| DUCK | one canvas, one dB scale (24 dB its height): the tracks' bars rise from the bottom line, a column a track of the song (8 at least; at 16, 14 px each), each as high as its dip (DEPTH × its MIXER DCK amount), *C*'s deep shade with a *C* top line; under them the tracks' digits, the source's inverted (white, a black digit; none with the keys as the source), a ducking track's *C*, the others dark grey; over them the curve hangs from the top, a white line: a full-DCK track after one loud hit of the source, over a bar at the tempo (beat lines dark grey's deep shade); the source's instrument (white) at 2, P, `-8 DB AT MOST` (*C*) at the right; with no DCK amount anywhere `NO TRACK DUCKS: MIXER, DCK` (light grey) along the bottom |
| METRONOME | a box a beat across 236 px (BEATS A BAR), the bar's first as high as the area under the line, the others 60 %; *C*'s deep shade with a *C* top line, the playing beat filled *C*; at TEMPO FOLLOW the tempo it follows at the right of P, clear of the knob page (`140 BPM`, *C*) |
| `◆ SENDS` | 4 lines (T) at P + 4, + 18, + 30, + 44: `CHORUS, DELAY AND REVERB RETURN HERE` (light grey), `ABOVE: THE DRY MIX ONLY`, `BELOW: EVERYTHING`, `MIXER, CHO DLY REV: EACH TRACK'S SEND` (dark grey; 37 characters, as all four lines within the 236 px). On DJ's setup: `DJ'S OWN, SHARED BY THE DECKS` (light grey), `EACH DECK'S SONG SENDS AS IT WAS MADE`, `ITS SENDS FOLLOW THE CROSSFADER`, `AND ITS DJ FILTER` (dark grey) |

GATE's name line says how its steps are edited: `STRAIGHT  EDIT: ITS STEPS`, the name light grey, `EDIT` yellow (a
key, as the help draws one), `: ITS STEPS` dark grey; while its steps are edited
`MY GATE 11  KEY: ON/OFF  KEY+ALG: X2-X4` (39 characters at most), the name white, `KEY` and `KEY+ALG`
yellow, the rest light grey. While the steps are edited the picture shows the pattern being edited,
even where a song's command plays another.

### GATE's steps being edited

As EDIT with GATE selected; the name line as above. EDIT is lit, and the white keys show the steps
([the LEDs](#controls)).

### MOVE

As EDIT; the moving effect's row is boxed yellow, and SEL is lit. SELECT moves it a row at a time.

### The GATE patterns (ENV on GATE)

```
+-GATE PATTERNS--------------------------+  a panel over the body
| 00 STRAIGHT         ################   |  T, rows of 13 px, 14 rows on show
| 01 OFFBEAT          .#.#.#.#.#.#.#.#   |
| 02 TRANCE           #.###.###.###.##   |
| ...                                    |
| 10 MY GATE 10       ################   |
+----------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | titled `GATE PATTERNS` ([../dialogs.md](../dialogs.md)) |
| row *i* (of those on show) | 8, `35 + 13 i` | | T | the pattern's number and name |
| the selected row's box | 4, `34 + 13 i` | 232 × 13 | | |
| its steps | `130 + 6 k`, row y + 2 | 4 × 7 each | | step *k* on or off |

The list scrolls to keep the selected row on show (14 rows of the 32). Colours: the built-in ones'
names light grey, yours light blue; a step on light blue, off deep grey; the box white.

### FX PROPERTIES (GLO)

The properties dialog ([../dialogs.md](../dialogs.md)), titled on two lines: `FX PROPERTIES FOR` (cyan), then
whose setup it is (white): the song's name, `THE LOOPS`, `DJ` or `THE KEYS`.

| Item | Shows | K1 |
|---|---|---|
| TEMPO | `120 BPM`: the keys' song's own; only for THE KEYS (a song's is SONGS' properties') | 3-600; SEL held: by 10 |
| SWING | `50 %`; only for THE KEYS | 50-75 %; SEL held: by 5 |
| ROUTING | `GLOBAL` or `SENDS` | switches |
| KEYS, EVERYWHERE | `THROUGH FX` or `DRY`: SETTINGS' KEYS, for every setup ([The keys, DRY](#the-keys-dry)); not on DJ, where the keys play no notes | switches |

### The SEL menu

The menu ([../dialogs.md](../dialogs.md)), titled with the selected item's name (`GATE`): MOVE, RESET,
REVERT (REVERT only while the effects FX edits have something unsaved).

- **REVERT** takes the whole FX setup FX edits (the song's, the loops' or DJ's, as FOR says) back as saved,
  after a question (`REVERT THE EFFECTS TO SAVED?`, `FOR CITY LIGHTS`, `FOR THE LOOPS` or `FOR DJ`; OCT+
  `REVERT`, OCT- `KEEP`; then `REVERTED`); what plays goes on with them from the next block. A song's other
  changes stay.

### MIXER

```
FX MIXER                 SEQ 3/3 >  |####   S, 16 px
MIXER                                     T
     00   01  02  03   04  05  06   07    T: the tracks
LVL  [#]  #   #   #    #   #   #    #     a bar, 18 px high
PAN  -+-  -+- -+- -+-  -+- -+- -+-  -+-   a mark on a line
DCK   S   .   .   .    #   #   .    .     only while DUCK is on
CHO   .   .   .   .    .   .   .    .     CHO, DLY, REV only in SENDS
DLY   .   .   .   _    .   .   #    .
REV   .   _   .   .    .   #   #    _
TRACK 00  LVL 7A 96%                      T: the selected value in words
10:12 EMPTY FUSION              TRACK 00  T, the footer
```

The third page, KEYS (after `0F`; with DRY its DCK and sends dark grey):

```
MIXER
    KEYS
LVL  [#]                                  its bar: (dB + 24) / 36
PAN  -+-
DCK   .
CHO   #                                   the sends 7F at first
DLY   #
REV   #
KEYS, EVERYWHERE  LVL 0 DB
10:12 EMPTY FUSION                  KEYS
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the title | 2, 19 | | T | `MIXER`, or `MIXER: THE LOOPS` |
| track *k*'s number (of 8 on show) | `33 + 26 k`, 33 | | T | `00`-`07`, `08`-`0F`, by the page the selected column is on (8 a page) |
| the KEYS column's name | `28 + 26 k`, 33 | | T | `KEYS` |
| row *j*'s name | 2, `52 + 26 j` | | T | `LVL`, `PAN`, `DCK`, `CHO`, `DLY`, `REV`: the rows on show |
| a cell | `30 + 26 k`, `47 + 26 j` | 20 × 20 | | the value |
| a bar (LVL, DCK, the sends) | cell x + 6, cell y + 2 | 8 × 18 | | its track, then its fill from the bottom, `v / 7F` of 18 px; KEYS' LVL `(dB + 24) / 36` |
| PAN | a line at cell x + 1, cell y + 10, 18 px; a mark 2 × 11 at cell x + 9 + `(v − 80) / 80 × 9`, cell y + 5 | | | the pan |
| DUCK's source | cell x + 7, cell y + 5 | | T | `S` in its DCK cell, instead of a bar |
| the selected cell's box | cell x − 2, cell y − 2 | 24 × 24 | | |
| the value in words | 2, 213 | | T | `TRACK 00  LVL 7A 96%`, `PAN 80 MIDDLE` / `LEFT` / `RIGHT`; then `  BY THE SONG` while a command holds it, `  MUTED` for a muted track. KEYS: `KEYS, EVERYWHERE  LVL +3 DB` (`0 DB`, `-6 DB`), `KEYS, EVERYWHERE  PAN 80 MIDDLE`, `KEYS, EVERYWHERE  REV 7F 100%`; then `  DRY` while SETTINGS' KEYS is DRY |
| the footer's right | | | T | `TRACK 00`, `LOOP 03`, or `KEYS` |

Colours: the title cyan; track numbers and `KEYS` cyan, the selected one white, a track dim (unused or muted) dark
grey; row names cyan, the selected row white; with DRY, KEYS' DCK and sends dark grey. Bars: LVL green, DCK brown, the sends light blue, over a
deep grey track; PAN's line dark grey, its mark light grey; `S` brown; a dim track's values dark grey,
a value a command holds white. The box white. The words line light grey, white while a command holds
the value.

## Texts

- **The header:** `FX`, in the PLAY group's colour; the subpage, white: `LIVE`, `EDIT` or `MIXER`.
- **The footer's right:** on LIVE and EDIT the picked effect's CPU share, `GATE 0.4% CPU` (measured on
  the device; the playground's estimates: GATE 0.4, FILTER 1.2, DRIVE 0.8, LO-FI 0.5, CHORUS 2.2, DELAY
  1.4, REVERB 7.5, LIMITER 0.6, DUCK 0.3, METRONOME 0.1; nothing on `◆ SENDS`); on MIXER `TRACK 00`.
  While a knob turns: the knob's name and value, `GATE RATE 1/16`, `FILTER ENV 60%`, `FILTER SOURCE 1/2 BEAT`,
  `FILTER DECAY 1.22 S`, `LVL 03 7A 96%`.
- **LIVE:** `PUNCH-INS: HOLD A KEY; ARP HOLDS IT ON`, `HELD ON: ITS KEY OR ANOTHER STOPS IT`, the punch-ins' short names ([../../files/commands.md](../../files/commands.md#punch-ins));
  `EFFECTS: BLACK KEYS`, the effects' names (`METRO`), `BYPASS`; `ON`, `OFF`, `1/2`; `SELECT FX  OCT
  OFF/ON  EDIT ITS PICTURE`; `ALG 1/2`; the header's `SEQ 1/3`.
- **The list:** the effects' names, `SENDS`, `ON`, `OFF`, `OCT+ ON`, `OCT- OFF`.
- **The knobs:** `K1`-`K4`, the names above; values: `00`-`1F` (PATTERN), `1/16`, `100%`, `LP 3.8K`,
  `OFF`, `HP 347`, `KEYS`, `TRACK 02`, `LOOP 03`, `4 BEATS`, `2 BEATS`, `BEAT`, `1/2 BEAT`, `1/4 BEAT`, `2.0 MS`,
  `296 MS`, `1.22 S`, `22 KHZ`, `I`, `II`, `I+II`, `1/8.`, `ON`, `OFF`, `10 MS`, `-1 DB`, `TRACK 00`,
  `FOLLOW`, `120 BPM`, `CLICK`, `BEEP`, `WOOD`, `COWBELL`.
- **The pictures:** the GATE pattern's name and `  EDIT: ITS STEPS`, or `  KEY: ON/OFF  KEY+ALG: X2-X4`; `100`, `1K`, `10K`, `PEAK +12 DB`;
  `HARMONICS`, `+2 DB  TONE 5.1 KHZ`, `1`-`15`; `L`, `R`, `+30`, `0`, `-30`, `1 S`, `0.36 HZ  +-7 CENTS`;
  `1`-`4`, `321 MS  -9 DB EACH  TONE 8.2K`, `PLAYS 1/8.`, `ONCE`, `OPEN`; `7.5 S`, `TREBLE 5.2 S`;
  `IN`, `+6`; the source's instrument, `-8 DB AT MOST`, `NO TRACK DUCKS: MIXER, DCK`, `0`-`F`;
  `140 BPM`; and `◆ SENDS`'s four lines.
- **ARP held** ([../dialogs.md](../dialogs.md)): every screen's: `HOLD ARP +`, `SELECT: THE RHYTHM, # A
  NOTE, _ A REST`, `ALGORITHM: THE PATTERN`, `K1: HOW MANY NOTES LATCH`, `LFO: LET THE LATCHED NOTES GO`.
- **MIXER:** `MIXER`, `MIXER: THE LOOPS`, `KEYS`, `LVL`, `PAN`, `DCK`, `CHO`, `DLY`, `REV`, `S`, the words line
  (`TRACK 00  LVL 7A 96%`, `MIDDLE`, `LEFT`, `RIGHT`, `BY THE SONG`, `MUTED`; `KEYS, EVERYWHERE  LVL +3 DB`,
  `DRY`); SEL held: `K1: BY 16`, on KEYS' LVL `K1: BY 6 DB`.
- **Menus and dialogs:** the SEL menu (the item's name; MOVE, RESET, REVERT); REVERT's question
  (`REVERT THE EFFECTS TO SAVED?`, `FOR CITY LIGHTS`, `FOR THE LOOPS`, `FOR DJ`; OCT+ `REVERT`, OCT-
  `KEEP`); `FX PROPERTIES FOR` and whose (`THE LOOPS`, `DJ`, `THE KEYS`, a song's name); its items (TEMPO, `120 BPM`;
  SWING; ROUTING: GLOBAL, SENDS; KEYS, EVERYWHERE: THROUGH FX, DRY); `GATE PATTERNS`.
- **Toasts:**

  | Toast | When |
  |---|---|
  | `GATE ON`, `GATE OFF` | OCT+ / OCT- switched the effect |
  | `LOOP 1/4 HELD ON: ITS KEY STOPS IT` | ARP held a punch-in on (its name from the punch-ins' list), on any screen |
  | `ROUTING GLOBAL`, `ROUTING SENDS` | GLO → ROUTING changed |
  | `KEYS THROUGH FX`, `KEYS DRY` | GLO → KEYS, EVERYWHERE changed |
  | `SELECT MOVES IT, SEL PUTS IT DOWN` | SEL → MOVE |
  | `MOVED` | SEL put it down |
  | `NOT MOVED` | HOME during MOVE: the chain as it was |
  | `LIMITER DOES NOT MOVE` | MOVE on LIMITER, DUCK, the METRONOME, or a send in SENDS |
  | `REVERTED` | SEL → REVERT, after its question |
  | `STRAIGHT COPIED TO MY GATE 11` | EDIT on a built-in GATE pattern |
  | `ARP UP C-4`, `ARP OFF` | ARP on (the pattern; the note played hands free) / off; the others: [../controls.md](../controls.md) |
  | `CHANGES DROPPED` | HOME while GATE's steps are edited, with something changed ([../saving.md](../saving.md)) |

## Help

```
[FX]: EFFECTS ON THE WHOLE MIX
{SEQ} (SHOW ELSE, QUICK): THE PAGES
  LIVE: PLAY THE EFFECTS BY HAND
  EDIT: THE CHAIN OF EFFECTS, KNOBS
  MIXER: EACH TRACK'S LEVEL AND SENDS
{SELECT}: PICK AN EFFECT
{K1}-{K4}: ITS KNOBS; {ALGORITHM}: MORE
{OCT+} / {OCT-}: THE EFFECT ON / OFF
{LFO} (LEAVE FIELD OPEN): THE KNOB
  TURNED LAST BACK TO ITS DEFAULT
{GLO}: HOW THE EFFECTS ARE WIRED
{ARP} (AGAIN, REPEAT, PLEASE): ARPEGGIO
EFFECTS: EACH SONG HAS ITS OWN; HERE,
  THOSE OF WHAT PLAYS ({GLO} SAYS WHOSE)
```

```
LIVE: PLAY THE EFFECTS
{WHITE KEYS}: PUNCH-INS, WHILE HELD
  + {ARP}: HOLDS ON; ITS KEY STOPS IT
  4 GROUPS: LOOPS, CHOPS, SWEEPS, TAPE
{BLACK KEY}: PICK AN EFFECT
  GATE FILTER DRIVE  LO-FI CHORUS
  DELAY REVERB LIMITER  DUCK METRO
  THE LAST: BYPASS, ALL OFF WHILE HELD
{SELECT}: PICK AN EFFECT TOO
{OCT+} / {OCT-}: THE EFFECT ON / OFF
{K1}-{K4}: ITS KNOBS
{ALGORITHM}: ITS NEXT 4 KNOBS
{EDIT}: THE EFFECT ON THE EDIT PAGE
{SEL}: RESET IT, OR REVERT ALL
{KEYS}: NO NOTES HERE, ONLY EFFECTS
FROM LOOPS: THE LOOPS' OWN EFFECTS
FROM [DJ]: DJ'S OWN EFFECTS
```

```
EDIT: THE CHAIN OF EFFECTS
ORDER: THE SOUND GOES TOP TO BOTTOM
{KEYS}: PLAY NOTES, TO HEAR IT
HOLD {SEL} + {OCT+} / {OCT-}: THE OCTAVE
{SEL}: MENU FOR THE EFFECT PICKED
  MOVE: ANOTHER PLACE IN THE CHAIN
  RESET: THE EFFECT AS AT FIRST
  REVERT: EVERY EFFECT AS LAST SAVED
LIMITER: ALWAYS LAST
DUCK, METRONOME: OUTSIDE THE CHAIN
GATE: THE SOUND ON / OFF IN 16 STEPS
  {EDIT}: ITS STEPS ON THE {WHITE KEYS}
  TAP: ON/OFF, HOLD + {ALGORITHM}: X2-4
  {ENV} (EXPLORE NAMED VAULTS): PATTERNS
FILTER ENV: NOTES OPEN IT, ADSR
  SOURCE: KEYS, A TRACK, THE BEAT
```

```
WIRING: {GLO}
ROUTING GLOBAL: ONE CHAIN, WHOLE MIX
ROUTING SENDS: CHORUS, DELAY, REVERB
  APART; EACH TRACK SENDS ITS AMOUNT
  THEY RETURN AT THE SENDS LINE
  ABOVE IT: EFFECTS ON THE DRY SOUND
  BELOW IT: EFFECTS ON EVERYTHING
KEYS: THROUGH FX, OR DRY: THE {KEYS}
  SKIP EVERY EFFECT
DUCK: ONE TRACK PUSHES OTHERS DOWN
  LIKE A KICK MAKING ROOM
  MIXER, DCK: HOW MUCH EACH TRACK DUCKS
METRONOME, KEYS DRY: AFTER ALL EFFECTS
NOTHING PLAYED YET: THE {KEYS}' OWN
  EFFECTS; {GLO} ALSO SETS THEIR
  TEMPO AND SWING
```

```
MIXER: {SEQ}, THE THIRD PAGE
{ALGORITHM}: PICK A TRACK
{SELECT}: PICK A ROW
  LVL, PAN, DCK: LEVEL, PAN, DUCKING
  CHO, DLY, REV: SENDS (ROUTING SENDS)
{K1}: CHANGE IT
{LFO}: BACK TO DEFAULT
KEYS: LAST COLUMN, SAME IN EVERY SONG
{HOME}, {WHITE KEY}: MUTE A TRACK
PUNCH-IN EFFECTS
HOLD {FX} + {WHITE KEY}: EFFECT WHILE HELD
  + {ARP}: HOLDS ON; ITS KEY STOPS IT
{KEYS} 1-4: LOOP 1/4, 1/8, 1/16, 1/32
{KEYS} 5-8: STUTTER, CRUSH, GATE 1/16, 32
{KEYS} 9-12: LP, HP SWEEP, ECHO, REVERB
{KEYS} 13-16: REVERSE, HALF, TAPE, VINYL
```
