# Connections

Status: **settled** (2026-10-10). The record: [the connections proposal](proposals/202610101831-connections.md).
The parts marked *(proposed)* are not decided yet and can change when tried
([open-questions.md](open-questions.md#the-firmware)).

PT/FM talks to the world over USB (MIDI and audio) and the MIDI IN jack (a TRS jack: a keyboard plugs in
through a TRS-to-DIN adapter, type A or B). The FM-1 has no MIDI OUT jack. What SETTINGS sets here is on
its MIDI page ([ui/screens/settings.md](ui/screens/settings.md#the-midi-page)); the levels of the audio
out, LINE GAIN and USB GAIN, on its first page.

## USB audio

- **From the FM-1 to the computer only.** The FM-1 is a class-compliant audio input: no driver on
  Windows, macOS, Linux or a phone. No audio comes back.
- **UAC1, 16-bit stereo, at 44.1 or 48 kHz,** whichever the computer picks (phones and many interfaces
  prefer 48).
- **Resampled to the computer's clock.** The FM-1's audio runs at its own rate, ~44,145 Hz (+0.1 %,
  measured against a USB clock). A resampler, steered by how full the USB buffer runs, gives the
  computer exactly its rate, so a recording and a live monitor both run without drift or clicks. One
  resampler serves both rates; integer arithmetic; it runs only while a computer records. Its CPU,
  measured on the FM-1 (the debug build's USB AUDIO stage): 6.3-6.7 % at 44.1 kHz with music (366-391
  µs a half), whatever plays; the bench's pieces: an output frame 366 cycles, a 64-frame block 89.6 µs
  at 44.1 kHz and 93.8 µs at 48 kHz.
  - **The resampler** is a windowed sinc of 16 taps, 128 phases, interpolated between two phases (about
    80 dB clean on a full-scale tone, measured on the PC).
  - **Its steering** follows the buffer's fill, smoothed, through a gentle integrator: within ±1,000 ppm
    of the computer's clock it holds the fill within about 45 frames (1 ms) of its mark and moves its
    rate by at most about 1 ppm a half, so the pitch never wobbles; far off (a start, a stall) it catches
    up faster.
  - **Every USB packet holds exactly the computer's frames** (44 or 45 at 44.1 kHz, 48 at 48), the last
    frame repeated if the buffer ever runs dry.
- **What it carries:** the mix after the effects, without the METRONOME's and the count-in's clicks
  (those play in the speaker and the jack only), times USB GAIN in MASTER's place, always stereo, then
  the soft ceiling ([ui/screens/settings.md](ui/screens/settings.md)). On DJ it is the same mix the
  jack plays ([ui/screens/dj.md](ui/screens/dj.md#usb-audio)).

## The USB device

- **No serial port.** A serial console beside the audio makes macOS 13-15 lose the audio input; what a
  console would carry goes over MIDI SysEx, as installs and data do.
- **The USB ID 1209:0001, the product "FM-1 PT/FM", the maker "PT/FM".** Every FM-1 firmware uses this
  ID, and the install pages find the FM-1 by "FM-1" in its name. The device's version number (bcdDevice)
  goes up whenever its descriptors change, so computers read them again.
- **The serial number "PTFM".** Windows names a device without a serial number the first time it sees its
  ID on a USB port, and keeps that name: where the FM-1 ran another firmware under the same ID, PT/FM's
  audio input showed that firmware's name. With a serial number PT/FM is a device of its own to Windows,
  on any port, named from its own strings. Every FM-1 running PT/FM has the same one.
- *(proposed)* **The audio control header lists the audio streaming interface before the MIDI one,** for
  older macOS's audio driver.
- **A SysEx message the computer stops taking is ended, not left open.** Messages out (the state, the
  figures, seeding's answers) go through a ring of 64 USB-MIDI packets, shorter than the longest message.
  After 200 ms without the computer reading, the message is given up with part of it sent; a lone F7
  follows as soon as there is room, so the computer reads a short message and drops it, and the next one
  arrives whole. The FM-1's MIDI notes wait until then.

## MIDI in: USB and the jack

Both ports feed one queue and behave the same.

- **Notes act as the FM-1's keys,** on every screen as its keys do: they play the current instrument,
  write in the EDITOR with REC on, record into LOOPS, feed the arpeggiator. Velocity sets the note's
  volume (1-127 as `01`-`7F`); the FM-1's own keys, which have none, keep KEYS VOLUME. Every note plays,
  not only the 27 keys' range.
- **MIDI IN CHANNEL:** ALL (the default) or 1-16; messages on other channels are ignored.
- **Program change** picks the instrument in the current cartridge, as PRESETS does (program *n* the
  cartridge's *n*-th used slot, counted from 0); **bank select** (CC 0) picks the cartridge, as SEL + PRESETS
  does (bank *n* the *n*-th cartridge).
- **Pitch bend** bends the keys' notes, ±2 semitones, those sounding and those to come: the FM-1's own keys'
  too, as they share the keys' voices.
- **The sustain pedal** (CC 64) holds the notes played from MIDI while it is down; their note offs come
  when it is let go.
- **Every other CC does nothing unless learned** onto an FM-1 control ([below](#midi-learn)).
- **Realtime messages** (clock, START, STOP, CONTINUE, the song position): as SYNC says
  ([below](#clock)). Both ports' realtime messages count, USB's included.
- *(proposed)* **The jack's bytes are read by their content, not by the DMA's count:** the count can miss
  a byte, and a note off then waits for the next note.
- *(proposed)* **When the queue runs low, USB waits:** its packets are refused until there is room, rather
  than a note off being lost.

## MIDI learn

**An outside controller's knobs and pads map onto the FM-1's own controls:** its 7 knobs, MASTER, the 14
buttons and the 27 keys. A learned control works exactly as the FM-1's does, on every screen, with its
combinations, holds and layers. MIDI notes need no learning: they play as the keys
([above](#midi-in-usb-and-the-jack)); a note learned onto a control is that control instead.

- **Knobs:** learning tells the kind from the turn made while learning: a knob that never gives the same value
  twice in a row is absolute; one that gives the same codes again and again is relative, its coding read from
  them.
  - An **absolute** knob (0-127) gives a click per step of its value, in the direction it moves; its
    first value only sets where it stands, so nothing jumps; at its ends it gives no more clicks.
  - A **relative** knob (an endless encoder) gives its clicks as they come, in any of the three usual
    codings: two's complement (1 up, 127 down), offset 64 (65 up, 63 down), sign and magnitude (1 up,
    65 down).
  - **MASTER,** a pot, takes an absolute knob as a position.
- **Pads and buttons,** a note or a CC: learning tells the kind from the press made while learning.
  - A **momentary** pad (a message on press and one on release) is held while it is held, as the
    FM-1's button.
  - A **toggle** pad (one message a press) is a tap on each press: a pad that sends no release within a
    second of its press is taken as one.
- **What learns what:** the knobs and MASTER learn CCs only; pads and buttons a note or a CC.
- **A learned control answers on its own channel,** whatever MIDI IN CHANNEL says.
- **How:** SETTINGS' MIDI page, EDIT on LEARN ([ui/screens/settings.md](ui/screens/settings.md#learn)):
  `TOUCH AN FM-1 CONTROL`; the FM-1's control turned or pressed (it does nothing else then),
  `MOVE THE MIDI CONTROL`; the outside one moved: its first message names it, and what comes within a
  second tells its kind; a toast `K1: CC 74, CH 1`. HOME cancels at either step.
- *(proposed)* **A MIDI control learned again replaces its old mapping;** an FM-1 control may have
  several.
- **The mappings,** a list on the MIDI page: SELECT goes through them, LFO deletes the one picked.
- **Kept:** the MIDI map record ([files/midi.md](files/midi.md)), saved by SAVE like everything else;
  *(proposed)* up to 64 mappings.

## MIRROR: the panel from a page

A page on the computer (the debug page's autotests, [debug.md](debug.md#autotests)) plays the FM-1 as a hand on
its panel would, with the PT/FM controller's mirror messages ([controller.md](controller.md#2-mirror-mode)): a
press from MIDI is a press, with every combination, hold and layer the FM-1's own has. In every build.

| Message, on MIRROR's channel, from USB | Is |
|---|---|
| Note On 53 + k, velocity above 0; Note Off 53 + k, or Note On at velocity 0 | key k (0-26, F3-G5) held; let go |
| CC 102 + id, 127 / 0 | button id (OCT- 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7, HOME 8, SAVE 9, ARP 10, SEQ 11, PLAY 12, REC 13) held; let go |
| CC 20 + id | knob id (SELECT 0, ALGORITHM 1, K1-K4 2-5, PRESETS 6) turned: 1-63 that many clicks clockwise, 65-127 (128 minus it) clicks back |
| CC 7 | MASTER, 0-127 |

- **On:** the controller's hello in mirror mode, `F0 7D 50 54 43 02 00 <version> <channel> F7`, arriving over
  USB: from then on that channel's messages from USB are the panel (`MIRROR ON`). Nothing is set on the
  device; other channels, and the jack, work as before.
- **Off:** a hello in general mode (`F0 7D 50 54 43 02 01 ...`), a USB bus reset (unplugged, the computer
  restarting) and power-off (`MIRROR OFF`, but at power-off).
- **At every hello,** on or off, every control MIRROR holds is let go.
- **Only the panel:** MIRROR's messages are not notes, not learned controls (MIRROR comes first on its channel,
  before any mapping) and not MIDI OUT's; MIDI IN CHANNEL does not apply to them.
- **A press shorter than a main loop pass is not lost:** a press and its release that come between two passes
  are seen as a press, then a release.
- **The FM-1's own panel stays live,** both hands count.
- **No LEDs go back,** and the controller's question (`43 01`) gets no answer: PT/FM is not a controller.

### The state

**40 STATE?** (`F0 7D 50 54 40 F7`) asks; the FM-1 answers **41 STATE** from the main loop (so after a
progress bar ends), its bytes packed as seeding's messages, 7 in 8 ([files/blob.md](files/blob.md#seeding)).
In every build; only a page asks, and it costs nothing until then. Little-endian; version 1:

| Offset | Size | What |
|---|---|---|
| 0 | u8 | the version: 1 |
| 1 | u8 | bit 0 the debug build; bit 1 MIRROR on; bit 2 ARP on; bit 3 ARP latched; bit 4 playing; bit 5 the USB test tone on ([debug.md](debug.md#the-usb-test-tone)) |
| 2 | u8 | MIRROR's channel, 0-15 |
| 3 | u8 | what plays: 0 nothing, 1 a song, 2 a pattern, 3 LOOPS, 4 DJ |
| 4 | u8, u8, u8 | the song (`FF` none); its position; its row |
| 7 | u16 | the tempo now, BPM × 10 |
| 9 | u16 | the effects on and working now, a bit each by their numbers ([ui/screens/fx.md](ui/screens/fx.md)) |
| 11 | u8, u8 | VOICES, the limit in force (SETTINGS' 16 is 12 while the second core does not run); the voices sounding now |
| 13 | u16 | the unsaved changes |
| 15 | u16, u16 | HOLD DELAY; COMBINATION GRACE, ms |
| 19 | u8, u8 | the instrument the keys play: its cartridge, its slot |
| 21 | i8 | KEYS OCTAVE |
| 22 | u8, u8 | SYNC (0 INTERNAL, 1 EXTERNAL); COUNT-IN (SETTINGS' value) |
| 24 | i8 | USB GAIN, dB |
| 25 | u8 | the answer's number: 1 to 255, then 1 again, one more for each answer sent (so a page can count the answers it never got); 0 from a firmware that does not number them |
| 26 | 16 | the screen's name, ASCII, NUL-padded |
| 42 | u8, u8 | the overlay on top: 0 none, 1 a menu, 2 a question, 3 properties, 4 a text, 5 a progress bar, 6 help, 7 HOME's combination, 8 a list, 9 another box; how many are open |
| 44 | 24 | the top overlay's words: a question's first line; a menu's highlighted item by its name (`BACK` first in a group, a group's head by the group's name); else empty |
| 68 | 8 | the screen's cursor (below) |
| 76 | 16 | the current song's name (the one SONGS' SELECT picks, its number SONGS' cursor; what plays is byte 4's) |
| 92 | 24 | the firmware's version, ASCII, NUL-padded |
| 116 | 185 | DJ, as the debug build's FIGURES carry it ([debug.md](debug.md#djs-bytes)): the decks as they play and DJ's record |
| 301 | 24 | the message in the header now (a toast: `MIRROR ON`, a refusal), empty if none |
| 325 | u16 | the edit memory ([files/ram.md](files/ram.md#editing-and-saving)), in 4-byte units as its three figures are: what the unsaved changes take, the pieces (each its room rounded up to 4 B) and their list's entries |
| 327 | u16 | the edit memory filled, in 4-byte units: from its bottom to the last piece's end, plus the list at its top (room freed between pieces counts until pieces are moved down into it) |
| 329 | u16 | the edit memory's size, in 4-byte units (17,152: 68,608 B; nothing borrows from it) |
| 331 | | the end (331 B; 385 on the wire). A reader takes a shorter answer of version 1 as older (325 B: no edit memory) |

**The screen's cursor,** as each screen gives it (zeros elsewhere):

| Screen | Bytes |
|---|---|
| SONGS | the current song; the position PLAY starts from |
| EDITOR | the pattern it is on (`FE` its own); the cursor's row; its track; bit 0: its own pattern holds anything |
| INSTRUMENTS | the cartridge; the slot |
| FX | the page (0 LIVE, 1 EDIT, 2 MIXER); the picked effect, by its number; u16 the effects switched on in the setup it edits (its `ON` / `OFF`, a bit each by number: on even while nothing plays, unlike byte 9's, which are the ones at work) |
| DJ | the song the list highlights; the pads' mode (0 PLAY, 1 LOOPS, 2 LFO); what K2 and K3 turn (0 FILTER, 1 VOLUME) |
| SETTINGS | the page (0 the list, 1 MIDI); the line, from 0; the tool TOOLS picks |
| LOOPS | the loops; the one selected (the loops' count: `+ NEW LOOP`); the notes it starts; a take armed (bit 0), a take recording (bit 1), the keys in CONTROL mode (bit 2), the selected loop muted (bit 3), soloed (bit 4), a song armed (bit 5), a song recording (bit 6), the pattern recorder open (bit 7); the length in bars (0 with no loops); the row the loops are at; u16 the notes all the loops start |

## MIDI out: USB

- **MIDI OUT:** OFF, KEYS (the default) or ALL.
  - **KEYS:** the FM-1's keys as played, with KEYS VOLUME as velocity, on MIDI OUT CHANNEL (1 at
    first).
  - **ALL:** the keys, and everything that plays (songs, the loops, the arpeggio, the EDITOR's row
    preview): track 0 on channel 1 … track 15 on channel 16, a note's volume as its velocity; the keys
    on MIDI OUT CHANNEL.
- **Notes that came in over MIDI are never sent back out** (no MIDI loop). STOP, all sound off and
  leaving a screen that played end every note sent.

## Clock

- **SYNC:** INTERNAL (the default) or EXTERNAL.
  - **EXTERNAL** follows the clock of whichever port sends one, USB or the jack. The tempo comes from it
    (the header's transport shows it, as BPM); START plays the current song from its start, STOP stops,
    CONTINUE goes on; the **song position pointer** moves PT/FM there, so a computer's sequencer starts
    it from any bar. What the transport plays on the FM-1 itself (PLAY) still works.
  - **Locked to the clock, not only its tempo:** every clock moves the master's place on by 1/24 beat, and what
    plays keeps its own place on it. Each 1.45 ms block compares where the clock is when the block is heard
    (the last clock's count and arrival, on at the measured pace) with where what plays is, and makes its ticks
    a little shorter or longer until they meet: about 4 % for a clock apart, 5 % at most. Nothing drifts, however
    far the master's tempo is from a tenth of a BPM, and the clock's own jitter (a USB clock arrives within about
    a millisecond) is smoothed out. Measured on the PC: a 120.04 BPM master with clocks up to 1 ms late, the rows
    stay within 2 ms of their clocks after 90 s (a tempo-only follower drifts 30 ms there). After START or
    CONTINUE what plays waits for the next clock, its first. Started on the FM-1 itself (PLAY) while a clock
    runs, it locks where it starts. A beat or more apart (the master's tempo jumped), its place is taken as the
    clock's again.
  - **On LOOPS and DJ** the master's START, STOP, CONTINUE and song position act on what that screen plays:
    LOOPS as its PLAY (START the loops from bar 1, STOP paused, CONTINUE on, the song position that bar within
    the loops' length; no loops yet, nothing); DJ as the main (left) deck's PLAY (START from its cue point, STOP
    paused, CONTINUE on; the song position nothing: the decks are moved by hand). DJ's main deck plays at the
    clock's tempo, its half or its double, whichever is nearest its song's own (as SYNC picks the right deck's:
    [ui/screens/dj.md](ui/screens/dj.md#tempo-and-sync)); a right deck synced follows the main, one not synced
    keeps its own. Every other screen: the current song, as above.
  - **The clock lost** for half a second: PT/FM plays on at the last tempo; the toast `CLOCK LOST`.
- **CLOCK OUT:** OFF (the default) or ON.
  - **ON:** while PT/FM runs on its own tempo (SYNC INTERNAL), it sends the clock (24 a beat, stopped too,
    at the tempo of what plays or played last) and START / STOP / CONTINUE, and the song position when it jumps (another song, a position skipped).
    Never with SYNC EXTERNAL: no clock echoed back.
  - **Sent as the sound is heard:** the sound is made a buffer (5.8 ms) ahead of being heard, so each clock, and
    each note MIDI OUT sends for the tracks (ALL), is stamped with the frame it belongs to and goes out when that
    frame is heard (within 0.5 ms, on the FM-1's own audio clock, so it never drifts from the sound). The clocks
    come evenly, none bunched into a buffer's moment, and an instrument following them plays with PT/FM, not ahead
    of it. The emulator sends them as they are made.
- **SYNC PULSE:** two of the main SETTINGS' OUTPUT values, SYNC PULSE L and SYNC PULSE R (OUTPUT is STEREO at
  first), acting with SYNC INTERNAL (as CLOCK OUT; with SYNC EXTERNAL the out plays stereo, no clicks). The FM-1
  has no MIDI OUT socket; instruments that follow a sync pulse through an audio cable (Korg Volca, Teenage
  Engineering Pocket Operator: 2 a beat) follow PT/FM through a stereo-split cable instead:
  - **a click on that side** of the audio out at every half beat of what plays: 5 ms at full scale, the first as
    it starts on a beat; the clicks come with the rows (to a 64-frame block, 1.45 ms), so with the notes heard;
  - **the music in mono on the other side;** a cable plugged in mutes the speaker, which plays one side only;
  - **MASTER** sets the clicks' level too: up for an instrument to see them;
  - VISUAL and USB audio keep the stereo music, no clicks.

## Not here

- **Bluetooth MIDI:** PT/FM never enables the radio, which is reached only through closed libraries
  ([hardware.md](hardware.md#the-radio-bluetooth-wi-fi)).
- **Audio from the computer** and **MIDI out on the jack** (the FM-1 has no such jack).
