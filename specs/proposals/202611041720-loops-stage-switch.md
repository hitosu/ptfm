# Proposal: LOOPS' stage switch

Status: **settled** (2026-11-04); the specs lead: [../ui/screens/loops.md](../ui/screens/loops.md) (the stage, REC, EDIT
and the arm, the pattern recorder, Controls, the LEDs, the layout, the hint line, Texts, Help),
[../ui/controls.md](../ui/controls.md), [../ui/dialogs.md](../ui/dialogs.md#hold-layers), [../ui/leds.md](../ui/leds.md)
and the others [section 6](#6-what-else-changes) names. It changes LOOPS as [the stage
proposal](202611030330-loops-redesign.md) made it. The playground has it (`playground/scr-loops.js`, `ui.js`,
`help.js`; `tests/playground/loops.js` pins its behaviour and texts); the firmware follows
([section 5](#5-the-firmware)).

## 1. Why

On LOOPS every note the keys play while the loops go round lands on the stage, and the stage takes the selected loop's
place. To play along with the loops without keeping anything (to find a line before recording it, to jam over a
finished set) the keys need a way off the stage. **ARP's tap switches the stage off and on:** off, the keys sound over
the loops as played and are written nowhere, the stage keeps its notes for later, and the selected loop is heard. The
pattern recorder is **ARP held + REC**, with **SEL's RECORD PATTERN** as its tap route (a held combination is only a shortcut: [../ui/controls.md](../ui/controls.md#buttons-tap-hold-and-layers)).

- **Goals:** play the keys over the loops without writing them, at once, in the stage's sound; nothing lost: the
  stage's notes come back as they were; one tap each way; the recorder still one gesture away.
- **Not goals:** a second stage; switching the stage per loop; keeping the switch through a power cycle.
- **A failure even if it works:** a key written while the stage is off; a stage that comes back changed; REC sending
  a stage nobody hears.

## 2. The stage off

- **ARP tapped switches it** (`STAGE OFF`, `STAGE ON`), on LOOPS' screen whatever else goes on: armed (the arm ends:
  `CANCELLED`, a count-in with it; the arm is the stage's, as its first key would start the loops writing) or a song
  armed or recorded (the stage rests then as ever; the switch is heard once the song ends).
- **Off, the stage keeps its notes,** drawn dimmed (dark grey and its deep shade, as a silenced loop's), not heard; the
  selected loop is heard in its place (not `STAGED`), outside LOOPS too.
- **The keys, while the loops play, sound at once in the stage's sound** (the keys' instrument and MONO / POLY, the
  mix REC would put the stage in, LOOPS' effects; POLY every key a note of its own, up to 16 at once on 16 lanes (the
  lowest free one; a 17th key takes the lanes in turn, letting that lane's note go to ring out, and the key that had it,
  still down, is forgotten); MONO one note, as MONO LEGATO says), each ended by its key's release, with no wait for
  QUANTIZE's line, and are written nowhere. Stopped, they play freely as ever.
- **REC's own stage jobs do nothing** (sending, arming: REC blinks, `STAGE OFF: ARP TURNS IT ON`); a count-in's cancel
  and RECORD LIVE's start and end stay REC's. LFO held does nothing (no OVERWRITE, no keep); LFO held + REC held still
  clears the stage.
- **EDIT and the pattern recorder's REC fill the stage and turn it on:** an explicit fill of the stage means it is to
  be heard; each replaces what the stage held, as ever.
- **On at every power-on, never saved;** SAVE, REVERT ALL, LOOPS' REVERT and CLEAR LOOPS leave it as it is.
- **Shown:** the stage's title `STAGE OFF` in place of the sound (where OVERWRITE shows), its right `ARP: STAGE ON` in
  place of REC's target (REC sends nothing then); the hint line `STAGE OFF: THE KEYS ARE NOT KEPT`, the last in its
  order; ARP lit full (a mode, as SEQ's CONTROL), REC dim.

## 3. ARP on LOOPS

- **ARP forms a combination here** (ARP held + REC), so its tap waits the combination grace as any such button's does;
  held past the hold delay it shows its layer, `REC: RECORD PATTERN`, and let go does nothing.
- **ARP held + REC opens the pattern recorder,** at REC's release; REC's press then does none of its own jobs (no send,
  arm, count-in's cancel, song start or end) and ARP's tap does not follow. ARP let go within the grace counts as
  held; REC pressed before ARP is plain REC. Refused where the recorder always was: armed, during a count-in, a song
  armed or recorded (`RECORDING A SONG`), REC and ARP blinking. With the stage off it opens, its REC turning the stage
  on. Inside the recorder ARP is its own (the preview on / off).
- **SEL's RECORD PATTERN** opens it too, on a loop and on `+ NEW LOOP`, refused the same way.
- **With LFO held, ARP is only OVERWRITE's keep;** while OVERWRITE is kept, ARP's tap is OVERDUB again, and the
  switch is the next tap.

## 4. Texts and help

| Text | Where | Length |
|---|---|---|
| `STAGE OFF`, `STAGE ON` | toasts: ARP tapped | 9, 8 |
| `STAGE OFF: ARP TURNS IT ON` | toast: REC while the stage is off | 26 |
| `OFF` | the stage's title, after `STAGE ` (`STAGE OFF`, 9 at 6, 162) | 3 |
| `ARP: STAGE ON` | the stage's title's right, right-aligned to 234 | 13 |
| `STAGE OFF: THE KEYS ARE NOT KEPT` | the hint line, at 2, 214 | 32 |
| `REC: RECORD PATTERN` | ARP's hold layer | 19 |
| `RECORD PATTERN` | the SEL menu, before RECORD LIVE | 14 |

The help's first page names ARP's switch (`{ARP} (AUTOMATIC REPEAT PROCEDURE):` / `THE STAGE OFF / ON; OFF: KEYS NOT
KEPT`, SELECT and EDIT sharing a line to keep the page to its 15 lines); its fourth opens with `HOLD {ARP} + {REC}:
RECORD PATTERN`, and the SEL line lists RECORD PATTERN.

## 5. The firmware

The firmware follows the specs and the playground: the switch a bit beside the stage (the player told, as of the loop
told silent: the stage off is not heard, the selected loop not silenced); ARP's tap after the combination grace; ARP
held + REC as a combination taken at REC's press, acting at its release; ARP's layer; the keys while the stage is off
on the stage's keys lanes at once, written nowhere; REC's, LFO's and EDIT's rules above; the recorder's REC turning
the stage on; the SEL item; the LEDs, the title, the hint line, the help. LOOPS' UI tests take the playground's
checks as their twins. The state's LOOPS byte 334 gets bit 3, the stage off
([../connections.md](../connections.md#the-state)), for the AUTOTEST and the debug page; the AUTOTEST's LOOPS
scenarios open the recorder with ARP held + REC ([../debug.md](../debug.md#the-scenarios)).

## 6. What else changes

- **The specs:** [../ui/screens/loops.md](../ui/screens/loops.md), [../ui/controls.md](../ui/controls.md) (the holds'
  shortcut, REC at its release with ARP held, ARP's and REC's meanings, the key map),
  [../ui/dialogs.md](../ui/dialogs.md) (ARP's layer on LOOPS), [../ui/leds.md](../ui/leds.md), [../files/ram.md](../files/ram.md) (the switch's bit),
  [../connections.md](../connections.md), [../debug.md](../debug.md), [../open-questions.md](../open-questions.md) (to
  try on the FM-1), [../README.md](../README.md), `AGENTS.md`.
- **The manual's LOOPS chapter:** the switch, ARP held + REC, the SEL item, the messages; **CHANGELOG.md:** FEATURE the
  stage's switch; CHANGE the pattern recorder on ARP held + REC and SEL's RECORD PATTERN.

## 7. Open

To try on the FM-1 with the rest of LOOPS' controls ([../open-questions.md](../open-questions.md#the-ui-ui)): ARP's tap
waiting the grace, and ARP held + REC as the recorder's gesture.
