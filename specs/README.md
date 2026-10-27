# PT/FM specifications

What PT/FM is and does, settled before code is written. One subject per file; the big subjects are
folders of files.

| File | Subject | Status |
|---|---|---|
| [layout.md](layout.md) | the repository: folders, what is committed, the built files | settled, with proposed details |
| [ui/](ui/README.md) | the user interface: controls, navigation, dialogs, LEDs, the frame, colours, the transport, saving, the help, and every screen ([ui/screens/](ui/README.md#the-files)) with its layout, texts and help pages | settled; LOOPS' controls to be tried in the playground; DJ (BETA) built, its *(proposed)* parts still to try and measure |
| [files/](files/README.md) | files and formats: the store, patterns, commands, songs, the loops, cartridges and instruments, PTR audio, settings, references, RAM, the data blob, disks | settled |
| [playground.md](playground.md) | the playground: what it is and what it fakes | settled |
| [panel.md](panel.md) | the panel: the virtual FM-1's controls for the playground and the emulator, made for ease of use; WIDE and PORTRAIT views | settled; built in the playground |
| [emulator.md](emulator.md) | the emulator: the firmware in a browser | settled in outline |
| [connections.md](connections.md) | connections: USB audio to the computer (resampled to its clock, 44.1 / 48 kHz), the USB device, MIDI in on USB and the jack, MIDI learn, MIDI out, the clock | settled; built in the playground as SETTINGS' MIDI page only |
| [controller.md](controller.md) | the controller, SETTINGS' MIDI CONTROLLER: the FM-1 as a USB-MIDI controller (mirror and general modes, the handshake, the screen, the pages) | settled; built, not yet tried on the FM-1 |
| [second-core.md](second-core.md) | the second core: the effects and the output stage on core 2, handed over block by block, and at VOICES 16 the voices past 10 (up to 6); who owns what, the flash, the hand-over, start, switch, fallback, its RAM | settled; the effects and core 2's voices built and run on the FM-1 (12 + 4; 10 + 6 built, not yet measured) |
| [editor.md](editor.md) | the editor page, EDITOR [beta]: TABLE (everything dropped, no numbers) and FM-1 (an image of the device's user area) side by side; items moved with what they use, reused by hash, renumbered; DELETE, UNDO; READ, WRITE, UPLOAD DISK, DOWNLOAD DISK; drops and conversions; the repeating preview through the firmware's sound code; every item edited in a tab of its own, with the FM-1's own pictures | settled (2026-10-26); being built |
| [manual.md](manual.md) | the manual, PT/FM OWNER'S MANUAL: one hand-written text, a page (the FM-1 drawn beside the text, every key named lighting on it) and a PDF styled as a 1990s device manual; its sources, build, screenshots from the firmware, and the tests that keep it complete | settled (2026-10-27); being built |
| [site.md](site.md) | the web site, hitosu.github.io/ptfm: the landing and the four pages it publishes (INSTALL, EMULATOR, MANUAL, EDITOR), put together by `site/assemble.sh` and published by a GitHub workflow; the landing's contents and its rules for texts; the pages' links back; its pictures and the tools that make them | settled (2026-10-28); built, not yet published |
| [chip.md](chip.md) | CHIP, the engine of the retro sound chips: the voice (oscillators, clocks, volumes, envelopes, sweep, noise, filters, frames), band-limited in integers; the built-in tables and waves; its LEVEL, costs and the playground's twin | settled |
| [hardware.md](hardware.md) | the FM-1's hardware as PT/FM depends on it, measured: the chip, RAM and what restarts write, the caches and the cache RAM, the flash and what may touch it when, the second core, sound, screen, controls, USB and MIDI, the radio, updates and rescue, the toolchain | a reference, kept current |
| [open-questions.md](open-questions.md) | the questions still open, the proposals' included; nothing settled is kept there | open |

## Proposals

`proposals/` holds the drafts, named `YYYYMMDDHHMM-<subject>.md`. What a review settles moves into the
specs above; the proposal stays as the record, and the specs lead from then on. A subject not yet
settled lives only in its proposal: no spec file repeats it.

| File | Subject | Status |
|---|---|---|
| [proposals/202610072229-ui.md](proposals/202610072229-ui.md) | the user interface | settled; its specs: [ui/](ui/README.md), [playground.md](playground.md) |
| [proposals/202610080050-fs.md](proposals/202610080050-fs.md) | files and formats | settled; its specs: [files/](files/README.md) |
| [proposals/202610081349-disk.md](proposals/202610081349-disk.md) | disks: a plain file holding a set of PT/FM's files, for the web editor; the demo disk | settled; its spec: [files/disk.md](files/disk.md) |
| [proposals/202610081913-controller.md](proposals/202610081913-controller.md) | the controller (first a firmware of its own) | settled; its spec: [controller.md](controller.md) |
| [proposals/202610101831-connections.md](proposals/202610101831-connections.md) | connections: USB audio (to the computer, resampled to its clock, 44.1 / 48 kHz), MIDI in and out, the jack, clock, MIDI learn onto the FM-1's controls, SETTINGS' MIDI page | settled (2026-10-10); its specs: [connections.md](connections.md), [ui/screens/settings.md](ui/screens/settings.md#the-midi-page), [files/settings.md](files/settings.md), [files/midi.md](files/midi.md) |
| [proposals/202610101408-dj.md](proposals/202610101408-dj.md) | the DJ screen (BETA): two decks, the main on the left, tempo and SYNC, a crossfader, PREVIEW on headphones, each deck's effects from its rows and engines | settled (2026-10-10) and reviewed, the specs written ([ui/screens/dj.md](ui/screens/dj.md), [files/dj.md](files/dj.md)); what is left is to try and measure, marked *(proposed)* in the spec |
| [proposals/202610102045-builtin.md](proposals/202610102045-builtin.md) | built-in instruments (the demo disk's synth cartridges in the firmware), the demo data as an uploaded disk in the user area, ROM (the firmware slot) read-only, finished things moved into it only by a confirmed choice on the device | settled (2026-10-10); its specs: [files/store.md](files/store.md#where-things-go), [files/instruments.md](files/instruments.md#built-in-instruments), [files/blob.md](files/blob.md), [files/disk.md](files/disk.md#the-demo-disk), [ui/saving.md](ui/saving.md#fws) and the screens' menus (ROM since renamed FWS, the firmware space: edited as anything else, its changes kept in the user area, and holding the firmware alone: [proposals/202610201630-fws-buffer.md](proposals/202610201630-fws-buffer.md)) |
| [proposals/202610142355-editor.md](proposals/202610142355-editor.md) | the editor: a page for the computer (FS, IMPORT, WRITER, CARTRIDGE, SETTINGS, MIDI, LISTEN), talking to the FM-1 over the seeding protocol and previewing through the firmware's own sound code | set aside (2026-10-26): replaced by [proposals/202610260240-editor-beta.md](proposals/202610260240-editor-beta.md) and its spec [editor.md](editor.md) |
| [proposals/202610151809-edits.md](proposals/202610151809-edits.md) | edits held as pieces and heard at once, REVERT for the current object, the EDITOR's REC and PLAY, and the RAM laid out anew | settled; its specs: [files/ram.md](files/ram.md), [ui/saving.md](ui/saving.md), [ui/transport.md](ui/transport.md), the screens' menus (its section 8) |
| [proposals/202610161350-mono.md](proposals/202610161350-mono.md) | the sound made in mono when only mono is heard (OUTPUT MONO, SYNC PULSE): what is stereo-only, what it would save, its catches | proposed, to be brainstormed |
| [proposals/202610161730-filter-env.md](proposals/202610161730-filter-env.md) | an envelope for FX's FILTER: ENV and an ADSR, fired by the keys, a track's notes or the beat | settled but for its 1.45 ms steps (to hear on the FM-1); its specs: [ui/screens/fx.md](ui/screens/fx.md#filters-envelope), [files/songs.md](files/songs.md#the-fx-setup), [files/commands.md](files/commands.md#mixer-and-fx-0x80-0xbf) |
| [proposals/202610201630-fws-buffer.md](proposals/202610201630-fws-buffer.md) | FWS for the firmware alone (MOVE TO FWS / FILL FWS gone), the EDITOR's own pattern kept by SAVE and never listed, patterns of at most 16 tracks × 128 rows | settled (2026-10-20); its specs: [files/store.md](files/store.md#where-things-go), [files/patterns.md](files/patterns.md#numbers), [ui/screens/editor.md](ui/screens/editor.md#the-editors-own-pattern), [ui/saving.md](ui/saving.md#fws) and the others its section 6 names |
| [proposals/202610222227-voices-on-core-2.md](proposals/202610222227-voices-on-core-2.md) | four more voices on core 2: 16 heard, 12 on core 1 and 4 more on core 2 taken only once core 1's 12 sound; the pool of 20 places, parking, VOICES 8 / 12 / 16, the voice job, who owns what, every flash operation waiting for core 2, the DX7's scratch per core | settled (2026-10-22); its specs: [second-core.md](second-core.md#the-voices-on-core-2), [ui/screens/settings.md](ui/screens/settings.md#voices), [ui/colours.md](ui/colours.md), [ui/screens/status.md](ui/screens/status.md), [ui/screens/dj.md](ui/screens/dj.md), [files/settings.md](files/settings.md), [files/ram.md](files/ram.md), [debug.md](debug.md) |
| [proposals/202610220009-second-core.md](proposals/202610220009-second-core.md) | the second core: the effects and the output stage on core 2, the voices on core 1; start, switch, fallback, RAM, the two cores on the screens and in the debug build | settled (2026-10-22); its specs: [second-core.md](second-core.md), [ui/frame.md](ui/frame.md#the-header), [ui/screens/status.md](ui/screens/status.md), [ui/screens/settings.md](ui/screens/settings.md), [files/settings.md](files/settings.md), [files/ram.md](files/ram.md), [debug.md](debug.md) |
| [proposals/202610251559-chip-engine.md](proposals/202610251559-chip-engine.md) | CHIP, a fifth engine: one virtual chip with the handheld's, the console's and the PSG's parts as an instrument's settings, band-limited, in integers; built-in tables picked by number, 28-B instruments, 128 a cartridge (`F9 CHIPTUNE`); its commands, edit pages, the demo disk's ADVENTURE and CHIP LOOP | settled; its spec: [chip.md](chip.md) |
| [proposals/202610260240-editor-beta.md](proposals/202610260240-editor-beta.md) | the editor page, EDITOR [beta]: TABLE (everything dropped, no numbers) and FM-1 (an image of the device's user area) side by side; items moved with what they use, reused by hash, renumbered; READ, WRITE, UPLOAD DISK, DOWNLOAD DISK; every item edited in a tab of its own, with the FM-1's own pictures; a repeating preview through the firmware's sound code | settled (2026-10-26); its spec: [editor.md](editor.md) |
| [proposals/202610271851-manual.md](proposals/202610271851-manual.md) | the manual, PT/FM OWNER'S MANUAL: a page and a PDF from one hand-written text, for a newcomer and to look things up; what it holds; what is left for players to find; kept complete by tests | settled (2026-10-27); its spec: [manual.md](manual.md) |

## Conventions

- **Status words:**
  - **settled**: decided;
  - **proposed**: a suggestion, not yet decided;
  - **open**: not decided, with the options known so far.
- **Open questions** all live in [open-questions.md](open-questions.md); a spec marks a part not yet
  settled *(proposed)* where the rest is settled. That file holds only what is open: a question answered
  goes into the spec it concerns, and its item leaves the file (the specs and the proposals are the record).
- **One voice:** decisions are woven into the text; the specs quote no one and say nothing of who
  chose what.
- **Headings are unnumbered;** specs refer to each other by links with anchors.
- **Screens are specified in text:** for each state an ASCII mockup on the T font's grid (40 × 20
  characters of 6 × 12 px), a table of its elements in pixels, its colours in words beside it (the
  colour coding itself is [ui/colours.md](ui/colours.md)'s), every text it shows, and its help pages.
- Numbers are little-endian, sizes in bytes, flash addresses in hex.
