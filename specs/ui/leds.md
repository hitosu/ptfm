# LEDs

Status: **settled** (2026-10-09). The record:
[the UI proposal](../proposals/202610072229-ui.md), section 1.6.

Every button and every key has an LED ([README.md](README.md#the-hardware-the-ui-works-with)). The
buttons' LEDs show what does something on the current screen and what is on; the keys' show layers,
steps and what sounds.

## Buttons

- **A button's LED:**
  - lit while its overlay or mode is on (GLO, ENV, EDIT, SEL marking, ARP's arpeggiator or row preview
    on, LOOPS' ARP with the pattern recorder open): the button
    that opened an overlay stays lit until it closes;
  - dim where the button has a function on the current screen;
  - dark where it has none.

  The dim state shows at a glance which buttons do something here. HOME, FX and SEL (SEL + PRESETS
  turns cartridges) have a function on every screen, so they are never dark.
- **EDIT** is lit while the EDITOR is entered deep ([navigation.md](navigation.md#jumps-into-the-editor)).
- **HOME** is lit while the help is open ([help.md](help.md)).
- **HOME's combination** ([controls.md](controls.md#homes-combination)), tapped open or held: **HOME
  breathes** (dim to full and back, starting full), the members that can act light dim (PLAY's red,
  REC, LFO, SEQ; SEQ lit while the white keys solo), every other button is dark. Once a member has
  acted, only its own lights stay.
- **PLAY** (green and red): [transport.md](transport.md#plays-two-leds).
- **REC:** lit while writing is on (EDITOR); blinking during a realtime take.
- **LOOPS** ([screens/loops.md](screens/loops.md#controls)): REC dim, blinking while a take or a song is
  armed, full while one records; EDIT and LFO dim while there is a loop, dark with none; SEQ dim in PLAY
  mode, full in CONTROL mode; ARP dim, full while the pattern recorder is open.
- **SAVE** follows its own rule, not the dim one: dim while there are unsaved changes, dark when there
  is nothing to save; blinking dim while a save waits for a pause ([saving.md](saving.md)).
- **OCT- / OCT+** follow their own rule, not the dim one: dark at OCT 0, and OCT- lit for the octaves
  below it, OCT+ for those above:
  - dim at 1 octave, full at 2;
  - blinking slowly at 3, fast at 4.

  OCT 0 is the keys' F-3 to G-5; KEYS OCTAVE ([screens/settings.md](screens/settings.md)) only sets
  where the octave starts at power-on. On the FX screen, where OCT- / OCT+ switch the selected effect
  off / on, they light as buttons with a function there.
- **A question** ([dialogs.md](dialogs.md#questions)): OCT- and OCT+, its answers, lit; HOME dim, as it
  cancels; every other button dark, as nothing else acts (PLAY's green still shows what plays).
- **A refusal** blinks the button that cannot act, fast, over every other rule: PLAY with no song to
  play, EDIT when a locked jump refuses to leave ([navigation.md](navigation.md#jumps-into-the-editor)).

## Keys

**The keys light in layers:**
- **HOME's combination** (tapped open or held): the black keys are the screens, the current one
  bright, the others dim, a key with no screen dark; the white keys are the tracks, dim while they
  play, dark while muted, bright while soloed (in LOOPS, the loops, key 1 the top one, lit as in
  CONTROL mode, below). Once a member has acted, only its keys stay lit (after SELECT, the screens).
- **FX held:** the white keys are the punch-in effects: keys 1, 5, 9 and 13 dim, where each group of
  four starts, and the one running bright; the others dark. FX LIVE lights them the same.
  While a punch-in is held on by ARP, the FX button is full ([screens/fx.md](screens/fx.md#punch-in-effects)).
- **A screen's own layer:** GATE's steps while EDIT edits them ([screens/fx.md](screens/fx.md)).
  LOOPS' CONTROL mode ([screens/loops.md](screens/loops.md#controls)): white key *n* full while loop *n*
  sounds, dim while it is muted or silenced by a solo, blinking while its mute or solo waits for the bar
  line, dark with no loop; the black keys OP1-OP5 full on the QUANTIZE chosen, dim otherwise; MONO or POLY
  full by the next loops' mode, the other dim; GLO full in SOLO, dim in MUTE; PIT dim. In PLAY mode
  LOOPS' keys light with their notes, as with no layer up; its pattern recorder's steps as RECORD
  PATTERN's (below). DJ's keys are the decks': each black key's state, the pads' by their mode
  ([screens/dj.md](screens/dj.md#leds)).
- **White keys used as buttons** (FX held, FX LIVE, RECORD PATTERN's and LOOPS' pattern recorder's steps,
  DJ's pads) **light for finding them by touch:** keys 1, 5, 9 and 13 dim, where each group of four starts, the others dark (`#___#___#___#___`);
  a key whose thing is on lights full over that, one that waits blinks.

**With no layer up, a played key lights with its note:** full while the note is loud, dim once it has
fallen under half its loudest, dark under an eighth (or once it ends). A plucked or struck sound
flashes and fades, a held pad stays lit: the keys show what sounds, not only what is pressed. A key
lights at once when pressed, before its sound's level is known.

## Levels and rates

The playground's values, which the device follows:

| What | Value |
|---|---|
| lit | full |
| dim | a quarter of full |
| a refusal | 80 ms on, 80 ms off, for 0.6 s |
| a breath (HOME's combination) | dim to full and back in 2 s, starting full; the level grows exponentially, so it looks even to the eye. The device sets the LED's light pulse length each scan; the controller (MIDI CONTROLLER, mirror mode: a page's LEDs on the FM-1) shows only off, dim and lit ([../controller.md](../controller.md)), so through it a breath steps between dim and lit |
| SAVE waiting for a pause | dim 250 ms, dark 250 ms |
| OCT at 3 octaves / at 4 | full 500 ms, dark 500 ms / full 150 ms, dark 150 ms |
| PLAY's green while playing | a little over half between beats, full on each beat |
| a pressed key, before its level is known | full for 100 ms |
