# The controller (MIDI CONTROLLER)

Status: **settled** (2026-10-08), from the controller proposal
([proposals/202610081913-controller.md](proposals/202610081913-controller.md)). Built into PT/FM's firmware:
SETTINGS' MIDI page, MIDI CONTROLLER. The protocol first ran on the FM-1 as a firmware of its own, whose mirror
mode worked with the playground; that firmware is gone, PT/FM's carrying the same code. Inside PT/FM it is not
yet tried on the FM-1.

**The controller** turns the FM-1 into a USB-MIDI controller, without installing anything: PT/FM ends, until
the FM-1 is switched off and on. Its first use is to work the virtual FM-1 (the playground, the emulator) from
the real panel; it also plays synths and DAWs. Its screen shows the MIDI it sends.

- **Mirror mode** (channel 16): every control sends one fixed message; the virtual FM-1 turns it back
  into the same control, and the page drives the FM-1's LEDs in return.
- **General mode** (channel 1): a keyboard controller: notes with an octave shift, knobs as values,
  Program Change, a play button for DAWs.

**The page** below is the one showing the virtual FM-1 (`panel/midi.js`).

## 1. Switching, and the start

- **SETTINGS' MIDI page, MIDI CONTROLLER, switches** ([ui/screens/settings.md](ui/screens/settings.md#midi-controller)).
  There is no way back but switching the FM-1 off and on, so it asks first, then whether to save unsaved
  changes. Then PT/FM ends for good: the sound fades out (12 ms), MIDI OUT ends its notes and the clock, and
  50 ms later nothing of the tracker runs any more: no player, engines or effects, no store, MIDI or screens.
  The audio interrupt still comes every 5.8 ms (the codec keeps running) and gives silence at once, so the
  FM-1 makes no sound. Only the controller runs, with no RAM of its own (its state, ~550 B, in the store's
  scratch).
- **The start screen asks for the mode:** `PT/FM CONTROLLER`, `PRESS A BUTTON TO START`, `REC MIRROR CH 16`,
  `HOME GENERAL CH 1`, `SELECT: CHANNEL DEFAULT`, with REC's and HOME's LEDs lit, until **REC** (mirror) or
  **HOME** (general) is pressed. A key held since before the start picks nothing until it is let go and
  pressed again.
- **SELECT turns the MIDI channel first:** `DEFAULT` is each mode's own (mirror 16, general 1);
  clockwise 1 to 16, for whichever mode is then picked; back below 1, `DEFAULT` again. The two lines
  show the channel each would use.
- **The footer says `SWITCH OFF AND ON FOR THE TRACKER`,** on the start and the monitor alike.
- **Nothing is saved;** the controller never writes to the flash (5).
- **While the start screen waits,** USB and the update path work (an install can start from it); the
  controller itself (MIDI out, the LEDs, the handshake's answer) starts once the mode is picked, with
  the hello (4) when USB is up. MIDI coming in meanwhile is dropped, and the page's question goes
  unanswered.
- **Controls held when the mode is picked** (the REC or HOME that picked it, too) send nothing until
  they are let go.
- **The log's first line** says the mode picked: `REC  MODE MIRROR` or `HOME  MODE GENERAL`.
- **Not at power-on:** a key held while switching on was tried first (REC mirror, HOME general); on the
  FM-1 it did not start the controller (the cause not found), so the controller is a switch in SETTINGS
  and asks for its mode once it is up.

## 2. Mirror mode

Channel 16 (or the start screen's), both ways. The tables give channel 16's status bytes.

### 2.1 From the FM-1

| Control | Message |
|---|---|
| a key, F3-G5 (k = 0-26) | Note On 53 + k, velocity 127; Note Off 53 + k, velocity 0, when let go |
| a button (id 0-13) | CC 102 + id: 127 pressed, 0 let go |
| a knob (id 0-6) | CC 20 + id, relative: 1-63 that many clicks clockwise, 127-65 1-63 clicks back |
| MASTER | CC 7, 0-127 |

- **The ids are the firmware's:** buttons OCT- 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7,
  HOME 8, SAVE 9, ARP 10, SEQ 11, PLAY 12, REC 13; knobs SELECT 0, ALGORITHM 1, K1-K4 2-5, PRESETS 6.
- **A knob's message carries its clicks since its last one,** at most one a millisecond per knob;
  more than 63 go over the next milliseconds. The page gives each click its message's time stamp
  (`dev.turn(id, step, t)`), so a page that speeds knobs up by the time between clicks counts them as
  they came, not as it happens to read them (several at once while it is busy drawing).
- **MASTER is sent when it moves 8 of its 1023 steps** (or reaches 0 or 127), so a still MASTER sends
  nothing. Its first reading after the mode is picked sends nothing, wherever MASTER stands.

### 2.2 To the FM-1: the LEDs

| LED | Message |
|---|---|
| key k | Note On 53 + k, velocity = level (a Note Off is level 0) |
| button id | CC 102 + id, value = level |
| PLAY's green LED | CC 116 |

- **Level:** 0 off, 1-63 dim, 64-127 lit. The page sends 0, 32 and 127.
- **Blinking** is the page sending changes; only changes are sent.
- **In mirror mode the FM-1 lights nothing by itself.**

## 3. General mode

Channel 1 (or the start screen's).

| Control | Sends |
|---|---|
| keys | notes, velocity 100 |
| OCT- / OCT+ | nothing: the keys an octave down / up, -3 to +3; both together: back to 0 |
| K1-K4 | CC 74 (cutoff), 71 (resonance), 73 (attack), 72 (release), 0-127 |
| SELECT | CC 1 (modulation), 0-127 |
| ALGORITHM | CC 21, 0-127 |
| PRESETS | Program Change, 1-128 |
| MASTER | CC 7, 0-127 |
| PLAY | CC 114: 127 pressed, 0 let go; and MMC Play (`F0 7F 7F 06 02 F7`), then MMC Stop (`F0 7F 7F 06 01 F7`), in turn |
| the other buttons | CC 102 + id, as in mirror mode: FX 104, SEL 105, ENV 106, LFO 107, EDIT 108, GLO 109, HOME 110, SAVE 111, ARP 112, SEQ 113, REC 115; 127 pressed, 0 let go |

- **A button has the same CC in both modes** (102 + its id), only on another channel.
- **The FM-1 keeps the values:** K1-K4 start at 64, SELECT and ALGORITHM at 0, the program at 1, the
  octave at 0, stopped; so at every start, and no value is sent until its control moves.
- **A click moves a value by 1;** clicks under 40 ms apart move it by 2, under 20 ms (or several in a
  millisecond) by 4. Values stop at 0 and 127. PRESETS moves the program by 1 a click, 1 to 128.
- **PLAY reaches the most DAWs this way:** those that follow MIDI Machine Control start and stop with
  no setup; the others learn CC 114. The controller sends no MIDI clock, so no MIDI Start / Stop.
- **A key's Note Off goes to the note its Note On sent,** whatever the octave is by then.
- **The LEDs:** keys lit while held; OCT- or OCT+ for the shift (dim at 1, lit at 2, blinking at 3);
  PLAY green from an MMC Play to the next MMC Stop; the other buttons lit while held.
- **When USB comes up,** the FM-1 sends All Notes Off (CC 123) on its channel.

## 4. Finding each other

SysEx under the non-commercial manufacturer ID 7D, then `PTC` (50 54 43):

| | Bytes |
|---|---|
| the page asks | `F0 7D 50 54 43 01 F7` |
| the FM-1 answers | `F0 7D 50 54 43 02 <mode> <version> <channel> F7`: mode 0 mirror, 1 general; version 2; the channel 0-15 |

- **Version 1's answer** had no channel byte (`... <mode> 01 F7`, 9 bytes) and meant the modes' own
  channels; the page still takes it.
- **The page asks** when its MIDI is turned on and whenever a MIDI port appears, on every output.
- **The FM-1 also sends its answer unasked** whenever USB comes up (and the mode is picked): a hello, at
  most one a second.
- **The page pairs** the input that answers with the output of the same name, on the answer's channel;
  port names do not matter otherwise.
- **At every answer the page** lets go of every control it holds from MIDI and, in mirror mode, sends
  every LED once.
- **After every answer in mirror mode the FM-1 sends its state:** MASTER's position, then every key and
  button held.

The test vectors of sections 2 and 4, read by both sides' tests: `fw/tests/vectors.json`.

## 5. In the firmware

- **The code:** the protocol, `fw/core/ctl.h` (portable: the panel to MIDI, the LED messages; its host test
  includes it as it is); its platform, `fw/core/ctlmode.c` (the start, PT/FM's input, LEDs, USB-MIDI and
  screen).
- **Time:** the keys, buttons, knobs and MASTER are read at 1 kHz in the FM-1's timer interrupt (the
  platform's `plat_fast`): a press goes out within a millisecond, a 2 ms tap whole, whatever the screen does;
  its SysEx (MMC) and the log's lines go to the main loop. A platform without such a tick (the emulator)
  runs it from its main loop, a pass at a time. The knobs' decoder counts every click of a fast turn
  ([../fw/hal/fm1_input.h](../fw/hal/fm1_input.h); its host test `fw/tests/enc_test.c` counts every click up to
  300 clicks a second).
- **USB:** the FM-1 is PT/FM's USB device (its name, "FM-1 PT/FM", and its USB audio, silent); the page
  pairs by the handshake, not the name. USB is up when the host has configured the FM-1; while it is not
  (unplugged), the screen says `USB OFF` in red, messages are dropped and counted (LOST), mirror mode's
  LEDs (the page's) go dark, and the hello goes out again when it comes back.
- **No stale backlog:** a USB packet no program takes for 100 ms (none has the FM-1's MIDI port open) is
  flushed with everything queued behind it; a program opening the port later gets only what happens from
  then on.
- **The flash:** the controller writes nothing. Only an update writes (the FM-1's update path: its staging,
  then the loader), which works from the start screen and the monitor alike; OCT- alone at power-on, or two
  failed boots in a row, start USB rescue, as always.

## 6. The screen: a MIDI monitor

PT/FM's palette and fonts on black (6x12, 40 columns).

```
PT/FM CONTROLLER             MIRROR CH 16    the header: the mode light cyan (mirror) or yellow (general)
────────────────────────────────────────
SENT 1234  LOST 0             LEDS IN 345    LOST red once one is lost; LEDS IN green once a page sent one;
────────────────────────────────────────     USB OFF red in its place while USB is down
K1        CC 22   +12          BF 16 01      the log: its last 15 lines, the newest at the bottom
C-4       NOTE ON 127          9F 3C 7F
PLAY      CC 114  127          BF 72 7F
MASTER    CC 7    96           BF 07 60
────────────────────────────────────────
SWITCH OFF AND ON FOR THE TRACKER            dark grey
```

- **A log line:** the control as printed on the panel (a key as the note it sent, `C-4`), the message
  in words, the bytes in dark grey (a SysEx as `F0 .. F7`).
- **Colours:** notes white, CCs light blue, Program Change yellow, MMC green, the handshake light magenta.
- **A turn does not flood the log:** while a knob's or MASTER's line is the newest, it is updated in
  place: in mirror mode the clicks add up (`+12`), in general mode it shows the value.
- **The first line** is the mode picked at the start (section 1): `REC  MODE MIRROR`.

## 7. The pages

- **PT/FM's firmware takes mirror messages as its own panel** ([connections.md](connections.md#mirror-the-panel-from-a-page),
  MIRROR): the debug page's autotests send them, after the mirror hello, to play the FM-1 by its panel; a
  controller routed to it through a computer would mirror the same way.
- **`panel/midi.js`** (the playground, the emulator): a MIDI button beside the size buttons
  turns it on (remembered across reloads; the button's click also starts the sound); a label shows
  `MIDI: OFF`, `LOOKING`, `FM-1 MIRROR` or `FM-1 GENERAL (ignored)`. Mirror messages become the panel's
  controls (`press` / `release` from source "midi", `turn`, `setMaster`), on the answer's channel;
  every LED change goes back. The paired port's messages go to the panel only. One page at a time: two
  would both drive the LEDs.
- **The playground's own MIDI CONTROLLER** (SETTINGS' MIDI page there too) ends the playground's tracker as
  the firmware's does and shows the start, then a monitor of what would be sent; it sends no MIDI.

## 8. Testing

- **`fw/tests/run_ctl.sh`** (gcc with the sanitizers): `ctl_test`, the protocol against the vectors, both
  modes byte for byte, MASTER's threshold and its first reading, the state after an answer.
- **`fw/tests/run_ui3.sh`** runs `ctlmode_test`: the switch from SETTINGS, the start, both modes on PT/FM's
  platform, the fast tick (a 2 ms tap's On and Off with the main loop away), the screens.
- **`tests/run_playground.sh`** runs `tests/controller_page.js`: `panel/midi.js` against the same
  vectors, the pairing, letting go, the LEDs.
- **On the FM-1:** not yet tried inside PT/FM: the switch, the start, both modes, how immediate it feels,
  general mode in a DAW, mirror mode with the playground.
- **Open** (the knobs' acceleration): [open-questions.md](open-questions.md).
