# Open questions

The questions still open, the proposals' included; nothing settled is kept here. A spec marks a part not yet
settled *(proposed)*; its question is here. A question answered goes into the spec it concerns (and the
playground), and its item leaves this file: the specs and the proposals are the record.

## The UI ([ui/](ui/))

- **FILTER's envelope in steps** ([ui/screens/fx.md](ui/screens/fx.md#filters-envelope)): the envelope sets the
  cutoff once a block (1.45 ms); whether those steps are heard with a high RESO and a short attack, to listen to
  on the FM-1 (16-frame steps where they are).
- **LOOPS, to try** ([ui/screens/loops.md](ui/screens/loops.md), [files/loops.md](files/loops.md)):
  - **its controls,** to be tried on the FM-1: whatever trying them changes (takes heard as written, CONTROL mode's
    tap and hold, the pattern recorder, RECORD LIVE);
  - **BEATS A BAR** (the METRONOME's) is the loops' bar: changing it with loops changes how many bars the length
    is, or leaves a part bar.
- **The palette's values** ([ui/colours.md](ui/colours.md#the-default-palette-soft-ega)) *(proposed)*: first
  values, tuned in the playground; to look at on the FM-1's screen.
- **STEREO SPREAD, a tool in the EDITOR's menu,** to make a pattern sound wider by pan commands: SEL → STEREO
  SPREAD asks for an amount first, then writes a pan command (`8xx`) at the top of each of the pattern's tracks:
  positive spreads them wider (the tracks spread across left and right, alternating sides, with a little variety
  so it sounds natural); negative narrows them toward the middle, at the most all to the middle (mono). One edit,
  so REVERT takes the pattern back to what was saved. To design: the amount's range and steps, where the pans go
  (row 0, or the pan commands there moved), how much variety exactly, and the dialog.
- **DJ, to try and to measure** ([ui/screens/dj.md](ui/screens/dj.md), marked *(proposed)* there; the record:
  [the DJ proposal](proposals/202610101408-dj.md)):
  - a synced right deck starting on the main's next bar line, not its next beat;
  - HOLD's feel: one row a click, at 24 clicks a turn;
  - BACKSPIN: what slides, arpeggios and retriggers do played backwards;
  - the crossfader's 32 clicks end to end, and its SHARP curve (both decks full until the last 2 clicks);
  - the help's names for the black key printed GLO and for the blank key, `BLACK GLO` and `BLANK KEY`;
  - FILTER THE SENDS' cost (OFF saving ~3-4 % of the CPU, estimated), to measure on the FM-1.
- **FX LIVE and the punch-ins** ([ui/screens/fx.md](ui/screens/fx.md#punch-in-effects)): LIVE's layout, to
  review once tried; the punch-in effects are a first pass, trimmed or changed once tried and their costs
  measured (vinyl stop's times among them, set by ear: it falls in 0.5 s, a turntable's brake, and comes back in
  ~0.1 s).
- **FX held + a black key** switching that key's effect in the chain on / off directly, from any screen (kept
  for later).
- **The low-memory toast's pace** ([ui/saving.md](ui/saving.md#the-edit-memory)): at most once a minute
  *(proposed)*, so editing on is not pestered.

## Files and formats ([files/](files/))

- **Named tracks in patterns:** a name of 4 characters a track (`TR00`, `TR01` ... until named). The pattern record
  has no room for them ([files/patterns.md](files/patterns.md): bytes 0-4 are full but for 3 spare bits in bytes 0
  and 2); 4 characters at 6 bits are 3 B a track, 48 B for 16. Either a new version of the pattern kind with the
  names after byte 4, or a record of its own, written only for a pattern with a name not its default, so patterns
  stay as they are. To decide then: which of the two; where names show (the EDITOR's track heads, the mixer, LOOPS);
  how they are entered; whether FUSION patterns' tracks have them.

- **The REVERB's and DELAY's resampling filter** (in and out at 29.4 kHz:
  [files/ram.md](files/ram.md#where-the-ram-goes)): its length, 48 taps for now, was chosen by ear; to measure.
- **Damaged records, and a tool to remove them** ([files/store.md](files/store.md)): a record whose CRC fails
  stays in the flash until its block is erased. A mount counts every one (an upload's result says how many),
  while CHECK STORAGE lists only those that cost something (the copy in use, one newer than it, one with none in
  use); a clean install ([files/blob.md](files/blob.md#seeding)) erases them all. Seen on the FM-1: four, between
  two installs of the debug build, their cause not known. To decide: a REPAIR STORAGE in SETTINGS' TOOLS, beside
  CHECK STORAGE, that removes them on the device:
  - **complete:** the good records sharing a damaged one's block written again elsewhere, sample audio included
    (compaction never moves audio), then the block erased; a damaged thing still in use deleted, as it cannot be
    read anyway; it asks first, shows its progress, says how many it removed, and holds through a power cut half
    way;
  - **or light:** only the blocks holding nothing good erased, so the count may stay above 0.
- **A save cut short** ([files/store.md](files/store.md#saving)): spoiling it at the next power-on, its records'
  kind bytes programmed to 0 (the only write a power-on makes), is *(proposed)*.
- **The spare zone** ([files/ram.md](files/ram.md#the-edit-memory-and-the-spare-zone)): what it is for beyond
  what borrows from it now (about 49 KB; DJ's 13 KB borrowed from its top, USB audio's 6.5 KB in reserve): left
  to a later review of the RAM.
- **Safe installs:** an install cut short by a power cut does not resume at the next power-on
  ([hardware.md](hardware.md#updates-and-rescue)); the USB rescue and the install page bring the FM-1 back. A boot
  stub that runs a kept loader when the firmware's last-written check fails would make installs safe against power
  cuts: a project of its own.
- **The CHIP engine's own tables** ([chip.md](chip.md#the-tables)): a CHIP instrument picks its
  volume, arpeggio, pitch, duty and noise lists and its wave from tables built into the engine, by number, which
  keeps an instrument at 28 B and a cartridge at 128 instruments. Custom tables could come later, beside the
  built-in ones: stored in the user area in a format of their own, as a sampler cartridge's samples are, and
  picked by an instrument as a built-in table is. To design then: the format, where they live (with a cartridge or
  apart), how an instrument tells a custom table from a built-in one, and their editing pages.

## The firmware

- **The second core, what is left** ([second-core.md](second-core.md)):
  - the effects that run at 29.4 kHz (the REVERB, the DELAY) at the full rate, with the RAM left over;
  - the voices split by cost between the cores (core 2 taking some of core 1's 12), if core 1 runs short;
  - raised supplies, only if some FM-1 misreads at the boot loader's levels (the one measured showed none).
- **The click at power-on: whether to work it out.** It is quieter, not gone
  ([hardware.md](hardware.md#sound): PC6 held low until 150 ms of silence have gone through the codec). What is
  left likely comes from the speaker's amplifier powering up, which no line the firmware drives is known to
  control. Ways in, if taken up: hear whether the FM-1's own firmware and the other free firmwares click the same
  (then it is the hardware); a longer wait before PC6 goes up, to hear whether the click moves with it or stays at
  power-on; the lines the firmware leaves alone at start, looked at for an amplifier's enable.
- **A lower sound rate,** an idea for later, after the CPU work that does not change the sound: everything
  rendered at 44.1 kHz costs the same work a sample whether it needs the top octave or not. Estimated from the
  FM-1's figures (DJ, two loops, ~6 voices, ~80 %): the whole sound at 29.4 kHz ~60 % (top ~13 kHz; a 2:3
  upsampler for the codec ~1-2 %; the half 192 frames, two 64-frame blocks at 29.4 kHz), at 22.05 kHz ~40-45 %
  (top ~10 kHz, more aliasing; a half-rate REVERB was heard clearly, which is why it runs at 29.4 kHz). The form
  that loses least:
  - **a rate a voice, chosen when its note starts**, never changed while it sounds: under load new notes start at
    22.05 kHz, the darkest first (low notes, dark instruments); the overload guard stays behind it. A voice's own
    work halves; a bus of the half-rate voices (dry and sends) is upsampled once, ~1-2 % while any voice is on it;
  - **decided by a cost model** (each engine's cost a block), not by the clock, so the emulator and the FM-1
    decide alike and a song sounds the same on every play;
  - **which sounds lose nothing** measured on the PC: a sound's energy above 11 kHz at 44.1 kHz is about what it
    would lose or fold back at 22.05 kHz; it depends on the instrument and the note (a bass played two octaves up
    gets bright), so not a setting an engine;
  - **a voice's low-pass filter tells it best** (the Juno-106's VCF, the TB-303's): with its cutoff low, nothing
    above it is heard. Two limits: the oscillator aliases before the filter (a saw made at 22.05 kHz folds its top
    harmonics below the cutoff, where the filter lets them through: small on low notes, more on high ones), and the
    rate is chosen at the note's start, so from the most the filter opens during the note (cutoff, its envelope,
    key follow, LFO), not where it stands;
  - **the effects:** REVERB and DELAY already run at 29.4 kHz; the Juno-106 chorus could run at 22.05 kHz; the DJ
    filters, the inserts and the LIMITER work on the whole mix and stay at 44.1 kHz.

  It saves only on the voices: with ~6 voices they are about a quarter of the CPU (~5 % back with half of them at
  half rate); with many voices they are most of it. It would take every engine able to run either rate a voice
  (pitch-indexed steps and filter tables move an octave; envelopes, the Juno-106's 236 Hz loop and the DX7's
  blocks count per rate), every engine's reference test at both rates, and the playground following or sounding a
  little brighter under load.
- **The sound made in mono when only mono is heard** ([its proposal](proposals/202610161350-mono.md)): set aside,
  nothing of it is built; to review again if a measure shows the need. Estimated (2026-10-25) on core 1, the core
  that runs late: under 1 % of a half on songs, about 2 % on DJ, about 3 % with DJ's filters on, and only under OUTPUT
  MONO or SYNC PULSE (STEREO is the default); DJ's sends are filtered as one channel already, so only the dry pair
  would halve. DJ's late halves are bursts of note starts (101-131 % of a half), which it would not remove.
- **USB audio's resampler, what could still be cut** ([connections.md](connections.md#usb-audio): 6.3-6.7 % of
  the CPU at 44.1 kHz with music). The device's compiler runs the two channels' multiply-accumulate chains one
  after the other with the coefficients spilled between them, and no form of the 16-tap kernel in C came out
  faster (the whole block's frames in one function, the history read as words, even and odd taps in sums of their
  own: each the same or worse). What is left changes something else:
  - **12 taps instead of 16:** about a quarter of the kernel (~1.2 % of the CPU). Measured on the PC with the
    device's integer arithmetic: 5-10 dB less clean at 1-10 kHz (84 dB to 75-79 dB for a Kaiser window) and
    0.5-1.5 dB less at 15 kHz; least-squares designs of 12 taps 60-80 dB.
  - **No resampling at 44.1 kHz:** the FM-1's own ~44,145 frames a second sent as they come (the endpoint is
    already asynchronous), nearly no CPU; the pitch then 1.8 cents sharp as on the jack, and keeping in step falls
    to the computer (a recording is fine; a monitor beside another device's clock drifts unless the computer
    corrects it). 48 kHz would still need the resampler.
  - **The kernel in an assembly file of its own** (both channels' chains interleaved, loads beside the
    multiplies): the build allows no inline assembly; what it would save is unknown until measured.
- **Parked notes on the FM-1** ([ui/screens/settings.md](ui/screens/settings.md#voices)): whether a note coming
  back (its 5.8 ms fade in) is ever heard as one, and what parking costs (a parked voice runs its envelopes only).
- **A safe start:** a crash caused by stored data comes back after a reinstall (the user area stays). Wanted: a
  key held at power-on that skips reading the records, and a way to erase them from there.
- **The autotests' stretches** ([debug.md](debug.md#the-scenarios)) *(proposed)*: the demo songs are measured from
  position 02 (PUNCH IN) or 03 for now, not yet from each one's heaviest stretch.
- **Connections, to try** ([connections.md](connections.md), marked *(proposed)* there):
  - the audio control header listing the audio streaming interface before the MIDI one, for older macOS's audio
    driver;
  - the jack's bytes read by their content, not by the DMA's count;
  - USB waiting (its packets refused) when the queue runs low, rather than a note off being lost;
  - a MIDI control learned again replacing its old mapping;
  - up to 64 mappings;
  - HOME the one control LEARN cannot take, as it cancels
    ([ui/screens/settings.md](ui/screens/settings.md#learn)).

## The controller, MIDI CONTROLLER ([controller.md](controller.md))

- **The knobs' acceleration in general mode** (clicks under 40 ms apart count 2, under 20 ms 4): to tune on the
  FM-1.

## Data and tools

- **The editor's MIDI files** ([editor.md](editor.md#midi-files)): quantising, tracks per channel, the instrument
  table and the controllers, to try on real files.
- **IT modules in the editor** ([editor.md](editor.md#drops-and-conversions): IT is not read): whether and how.
  - Why not yet: modules are read as IBXM reads them, the reference the converter's tests compare with, and IBXM
    reads no IT. Another reference would be needed (libxmp, MIT, reads and plays IT).
  - What maps as S3M and XM do: IT's effects are S3M's letters (their XM twins), the volume column, sample mode,
    the samples' C5 rates, patterns over 128 rows split. Compressed samples (IT 2.14 / 2.15) need a decoder. A
    sample's sustain loop is what the sampler's loop already is. An instrument whose keys pick other samples or
    other notes: an instrument a sample, the cells' notes moved.
  - What is lost or only near: new note actions and duplicate checks (a track's next note lets the previous one
    ring on, which comes near), the pan, pitch and filter envelopes, the resonant filters (`Zxx`, an instrument's
    cutoff), auto-vibrato, panbrello, random volume and pan; channel volume (`M`, `N`) and tempo slides (`T0x`,
    `T1x`) approximated.
  - Decided: **channels past 16 are folded into the 16 tracks,** for every module kind (MOD, S3M and XM keep the
    first 16 now). A channel keeps its track while it can; a note moves to a free track only when its own is
    taken, with its pan and the effect values it leans on; where more than 16 would sound at once the quietest is
    ended or left out, and the status line says how many and where.
  - Open: XM's and IT's volume envelopes left out (as XM's are now), or fitted to the sampler's envelope (R1-R4,
    L1-L4), listed as approximated.
  - Measured on a 27-channel IT 2.14 song (5:10, 43 instruments without envelopes, S3M's effects only): up to 25
    channels at once, more than 16 for 64-94 s of it; its samples take 549 KB in PTR at their own depths, 442 KB
    at 8 bits, against the user area's 412 KB (348 KB with the 64 KB left free): a song like it needs OPTIMIZE
    and an otherwise empty FM-1.
- **`seed.html`** ([layout.md](layout.md#the-built-files-dist-committed)), an uploader for any disk beside the
  install page, is not built (the install page uploads the demo disk, the emulator's SEED any disk, and the editor
  sends what it holds): whether it is still wanted.
- **One implementation of the records and converters, or two** (the files proposal, 12.3): two for now, the
  editor's in JavaScript and the tools' in Python (`tools/ptfm_records.py`), kept equal by the tests; or one in
  JavaScript, shared by the tools (run with node, which then need a FLAC decoder in JavaScript) and the editor.
- **The old CHIP cartridge's sounds** ([files/instruments.md](files/instruments.md#built-in-instruments)):
  its 32 Juno-106 patches left the built-ins when `F9` became the CHIP engine's cartridge (their source stays in
  `demo-disk/patches/juno106/chip.json`). JUNO-106 is full (128 of 128): which of them take the place of
  JUNO-106 patches that sound nearly the same (found by rendering and comparing) is to choose from a list.
- **12-bit samples:** would they serve better than 16-bit?
  - PTR codes 8 and 16 bits (the files proposal, section 6), and a 16-bit sample takes 3 times the flash of the
    same sound at 8 bits.
  - 12 bits give 72 dB of range against 8-bit's 48. 8-bit's noise is heard in quiet tails and fades, which a
    voice's envelope and level make quieter still.
  - Size: Rice coding spends about a bit a sample for each bit of depth, so 12-bit should take about half of
    16-bit's size (1.5 times 8-bit's). That is an estimate: measure it on the demo disk's sources at 8, 12 and 16
    bits.
  - The other choice: 12 replaces 16, or joins it as a third coding (the coding field has room).
  - The decoder's cost would be as 16-bit's, since its samples need 16 bits in RAM.
- **The repository's details** ([layout.md](layout.md)), each *(proposed)*: tests that compare with reference code
  from outside skip themselves when it is missing; a dev build's checks for finding faults (assertions stopping on
  a crash screen), left out of a production build; the same sources giving the same bytes (built files carry the
  version or the commit, not the build's time); every page carrying the licence texts of what it holds.

## The virtual FM-1

- **The silicone's lettering:** drawn as Bahnschrift Bold for now.
- **The SIDEWAYS view and the page** ([panel.md](panel.md#sideways-a-phone-held-sideways),
  [panel.md](panel.md#the-page)), each *(proposed)* as a whole, to settle once tried: the view for a phone held
  sideways (picked by itself, or by `#sideways` in the address), and the page around the panel that the
  playground and the emulator share (`panel/page.css`, `panel/page.js`).
