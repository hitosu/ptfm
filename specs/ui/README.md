# The user interface

Status: **settled** (2026-10-09), LOOPS' controls to be tried in the playground. The record:
[the UI proposal](../proposals/202610072229-ui.md), sections 2 and 10.

PT/FM's UI: the screens, the controls, the colours and how they behave. These files hold no firmware
code and no data formats; storage appears only where the UI meets it, saving and references between
things ([saving.md](saving.md)). The formats are in [../files/](../files/README.md). The playground
([../playground.md](../playground.md)) is the UI's reference implementation: every screen's layout and
texts here are taken from it.

## Words

- **Cartridge:** a bank of instrument slots, all of one type, with a fixed number of slots.
- **Instrument:** one slot of a cartridge. It holds one of:
  - a DX7, Juno-106 or TB-303 patch;
  - a sample instrument;
  - a FUSION instrument.
- **Pattern:** a grid of tracks × rows. A **cell** is one track's entry in one row: note, instrument,
  volume and up to four commands.
- **Command (CMD):** an instruction to the player, such as a pitch slide, a retrigger, a jump or a
  TB-303 accent. Trackers call them effects. Here **FX** means only the audio effects of the FX
  screen. Which commands exist is in [../files/commands.md](../files/commands.md).
- **Song:** a list of patterns to play, in order. Patterns are one shared pool: two songs can play the
  same pattern.
- **Current song / pattern / instrument:** what the screens show and act on. Switching screens keeps
  them ([navigation.md](navigation.md#current-song-pattern-and-instrument)).

## The files

| File | What |
|---|---|
| [controls.md](controls.md) | the knobs; the buttons: tap, hold, layers, the grace period, edit modes; what each button means; the key map |
| [navigation.md](navigation.md) | the current song, pattern and instrument; the screens, their order and switching; LOOPS in the round; what plays plays on; the jumps into the EDITOR |
| [dialogs.md](dialogs.md) | overlays: menus, MOVE, questions, properties, the hold layers' overlays, the lists, text entry, progress; the shared toasts |
| [leds.md](leds.md) | the buttons' and keys' LEDs |
| [frame.md](frame.md) | the header, the footer, numbers, fonts |
| [colours.md](colours.md) | the palette, what carries meaning, themes |
| [transport.md](transport.md) | PLAY, tempo, rewinding, stopping, PLAY's two LEDs |
| [saving.md](saving.md) | SAVE, REVERT and REVERT ALL, the edit memory, references |
| [help.md](help.md) | HOME + LFO: the help, its rules, its shared pages |
| [texts.md](texts.md) | the firmware's fixed texts: one tokenizer packs them all, the help's included |
| [screens/songs.md](screens/songs.md) | SONGS: the songs and their order of patterns |
| [screens/editor.md](screens/editor.md) | the EDITOR: patterns, the note editor |
| [screens/instruments.md](screens/instruments.md) | INSTRUMENTS: cartridges and instruments |
| [screens/playback.md](screens/playback.md) | PLAYBACK: what plays, followed |
| [screens/loops.md](screens/loops.md) | LOOPS: the loop station, loops stacked over one length |
| [screens/dj.md](screens/dj.md) | DJ: two songs on two decks, in time, a crossfader, each deck's effects |
| [screens/fx.md](screens/fx.md) | FX: the effects, the mixer, punch-ins, the metronome |
| [screens/visual.md](screens/visual.md) | VISUAL: the visualisers |
| [screens/status.md](screens/status.md) | STATUS: the start screen and the figures |
| [screens/settings.md](screens/settings.md) | SETTINGS: global settings and tools |

## The hardware the UI works with

- **Knobs:**
  - 7 endless knobs, with no push switches: SELECT, ALGORITHM, PRESETS, K1-K4.
  - 24 clicks a turn; they give deltas only.
  - MASTER is a pot: the volume, never a UI control.
- **Buttons:** 14, each with an LED.
  - PLAY has two LEDs, green and red; REC's is red.
- **Keys:** 27, F-3 to G-5 at OCT 0, each with an LED.
  - **OCT** shifts them by −4 to +4 octaves, reaching C-0 to G-9 (MIDI 12-127, 116 notes): every
    note a pattern can hold.
  - **At OCT −4** the 7 lowest keys (F to B, below C-0) are silent, since the notation has no name
    for those notes.
  - **16 white keys:** used as 16 tracks, 16 steps or 16 effects.
  - **11 black keys, printed:** OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY | (blank). Used for
    the screens ([navigation.md](navigation.md#the-screens)).
- **Labels that read wrong.** The printed labels name the FM-1's own synth functions. Most still fit
  their PT/FM job: FX, SEL, EDIT, HOME, SAVE, PLAY / STOP, REC, OCT, and GLO as "global". The others
  get readings of their own:

  | Label | Printed for | Read as | Its job |
  |---|---|---|---|
  | LFO | low-frequency oscillator | `LEAVE FIELD OPEN`; with HOME, `LOOK FOR ONSWERS` | clear ([controls.md](controls.md#what-each-button-means)); the help ([help.md](help.md)) |
  | ENV | envelope | `EXPLORE NAMED VAULTS` | the lists of patterns, cartridges and samples |
  | SEQ | sequencer | `SHOW ELSE, QUICK` | the next subview |
  | ARP | arpeggiator | `AGAIN, REPEAT, PLEASE` | the arpeggiator, as printed; on the EDITOR the row preview, on LOOPS the pattern recorder |
  | OP1-OP6 | FM operators | `OPEN PAGE 1` … `OPEN PAGE 6` | SONGS, EDITOR, INSTRUMENTS, PLAYBACK, DJ (OP6 reserved) |
  | PIT | pitch | `PICTURES IN TIME` | VISUAL |
  | MONO | mono | `MY OWN NERDY OPTIONS` | SETTINGS |
  | POLY | polyphonic | `PARTY ON, LOOP YOURSELF` | LOOPS |

  - **Where they show:** only in the help, once in each screen's help, at the button's first mention
    ([help.md](help.md)).
  - **The blank key** has no screen (kept for later); on DJ it turns the pads' mode. It has no print,
    so the help calls it `BLANK KEY`, with no reading.

## Limits the screens must respect

| What | Limit | Notes |
|---|---|---|
| Screen | 240 × 240 | T: 40 characters across, S: 30, L: 15 |
| DX7 cartridge | 32 slots | as the DX7's own |
| Juno-106 cartridge | 128 slots | |
| TB-303 cartridge | 128 slots | |
| Sampler cartridge | 64 instruments, plus its samples | |
| FUSION cartridge | 64 slots | 4 pages of 16 |
| Tracks in a pattern | 1-16 | numbered `00`-`0F`, as the 16 white keys |
| Rows in a pattern | 1-128 | numbered `00`-`7F`: 8 bars at 4 rows a beat ([../files/patterns.md](../files/patterns.md#size)) |
| Patterns | 240 of the user's | `00`-`EF`; the firmware's `F0`-`FE`, `FE` the EDITOR's own ([../files/patterns.md](../files/patterns.md#numbers)) |
| Cartridges | numbered `00`-`FF` | the first half of an instrument ID (`0B:05`); flash space allows far fewer |
| Voices | 8, 12 or 16 heard (16: 4 of them on the second core) | shared by everything; notes past them parked ([screens/settings.md](screens/settings.md#voices)) |
| Notes | C-0 to G-9 (MIDI 12-127) | in patterns and from the keys (OCT −4 to +4) |
| Commands in a cell | 1-4 | the grid shows one, the note editor all |
| Instrument names | 10 characters | the DX7's own limit; it also fits the two-column list |
| Characters in names | `A`-`Z`, `0`-`9`, space, `: . - ? \| / \ * # $ @ %` | capitals only |
| Cartridge names | 12 characters | |
| Song names | 16 characters | |

**What fits on a screen:** the header takes 17 px and the footer 13 px, leaving a body of 210 px
([frame.md](frame.md)).

| Screen | Needs | Fits |
|---|---|---|
| EDITOR, PLAYBACK: 8 tracks | track numbers 12 + 15 rows × 12 + the focus row 16 = 208 px | yes, the tightest |
| EDITOR, PLAYBACK: 4 tracks | track numbers 12 + 12 rows × 16 = 204 px | yes |
| EDITOR, PLAYBACK: 2 tracks | track numbers 14 + 8 rows × 24 = 206 px | yes |
| INSTRUMENTS | the cartridge line 16 + the list 8 × 12 = 112; the details keep 97 px | yes: the DX7 table fits; the Juno-106 sliders get a little shorter |
| FX | the chain 10 × 12 = 120; the selected effect keeps 90 px | yes |
| DJ | the decks 88 + the crossfader 16 + the list's names 12 + 7 songs × 13 = 207 px | yes |
| SONGS, LOOPS, VISUAL, STATUS, SETTINGS, the help | | yes, with room to spare |

**What fits in a row:**

| View | One track | Tracks + row numbers (T, 12 px) |
|---|---|---|
| 8 tracks | the note in S (24 px) + a gap = 27 px | 8 × 27 + 12 = 228 px |
| 4 tracks | `C-4 0B:05` in T (54 px) | 4 × 54 + 12 = 228 px: the note and the ID ([screens/editor.md](screens/editor.md)); no room left for the volume or the command |
| 2 tracks | the note in S (24 px), then `0B:05 7F ACC` in T (72 px), gaps 12 px = 108 px | 2 × 108 + 12 = 228 px |
| 2 tracks, all in S | `C-4 0B:05 7F ACC`, 16 characters = 128 px | 2 × 128 + 12 = 268 px: too wide |
