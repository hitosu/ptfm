# The repository

Status: **settled** (2026-10-07; the playground's sound and `demo-disk/` 2026-10-08), with
proposed details marked *(proposed)*.

## Folders

```
ptfm/
  AGENTS.md  README.md  LICENSE  .gitignore
  specs/        these specifications (Markdown)
  fw/           the firmware's sources: the engine only, no data
  emulator/     the emulator's sources: the web hardware layer (C), the page, the audio worklet
  panel/        the virtual FM-1's controls (panel.md): screen, knobs, buttons, keys; shared by emulator/ and playground/
  manual/       the manual's sources (manual.md): the chapters, the page's and the PDF's stylesheets, the page's script
  site/         the web site, hitosu.github.io/ptfm (site.md): the landing, its pictures and their tools, assemble.sh
  .github/      the workflow that publishes the site
  playground/   the UI playground: no build; the UI and the sound in plain JS
  demo-disk/    the demo disk's sources: patches, samples and songs under free licences
  assets/       sources of built-in assets (fonts)
  tools/        build scripts, the data packer, converters
  tests/        host tests, and small free test data
  dist/         the built files, a folder for each kind of build; committed
  build/        intermediate files, ignored by git
```

- **`fw/`** holds the firmware without data: engines, player, UI, file system and the device's
  hardware layer. Patches, samples and songs reach the device as a data blob (the files proposal,
  section 12: [proposals/202610080050-fs.md](proposals/202610080050-fs.md)).
  - `fw/core/` the app, shared with the emulator and the host tests, touching the hardware only
    through `fw/core/plat.h`; `fw/device/` boot, interrupts, flash, USB, update, rescue; `fw/hal/` the
    device's hardware layer; `fw/loader/` the update loader the installers know; `fw/tools/`, `fw/web/` (the
    install page, the seeding module), `fw/tests/` ([../fw/README.md](../fw/README.md)). The controller,
    SETTINGS' MIDI CONTROLLER ([controller.md](controller.md)), is part of it (`fw/core/ctl.h`,
    `fw/core/ctlmode.c`); its page side is `panel/midi.js`.
- **`emulator/`** runs the firmware in a browser ([emulator.md](emulator.md)). It holds sources only;
  its compiled part is built into `dist/emulator/`.
- **`panel/`** is one copy of the virtual FM-1's controls for both pages, so the two can never drift
  apart ([panel.md](panel.md)); `panel/page.css` and `panel/page.js` are the two pages' frame around it.
- **`editor/`** is the editor's sources ([editor.md](editor.md)): `editor/build.py` makes the one file
  `dist/editor/editor.html`, the firmware's WebAssembly inside; its tests are `tests/editor/` (Node,
  `run_tests.sh`; the page in a headless Chrome, `run_page.sh` with its scenarios in `tests/editor/page/`).
- **`playground/`** is for trying the UI fast, with sound, and without a build: the files are edited
  and the page reloaded. What it is and fakes: the UI proposal, section 11
  ([proposals/202610072229-ui.md](proposals/202610072229-ui.md)).
  - It looks like the emulator, because it uses the same panel.
  - `playground/content.js` is generated from the demo data by `tools/gen_playground.py`, and
    committed: the page needs no step to run.
  - Its screen driver (`playground/lcd.js`) works like the firmware's display code.
- **`demo-disk/`** holds the demo disk's sources: patches, samples and songs under free licences,
  each with its licence ([../demo-disk/README.md](../demo-disk/README.md)). The playground shows them,
  and later the demo disk (the disk proposal: [proposals/202610081349-disk.md](proposals/202610081349-disk.md))
  is built from them.
- **`specs/`** holds the specifications, one subject per file ([README.md](README.md)).
- **`tools/`** holds the build scripts, the data packer, the texts' packer (the firmware's fixed texts
  and the help: [ui/texts.md](ui/texts.md)) and the converters (for example MOD / S3M to PT/FM's formats).
- **`tests/`** holds the host tests. Tests that compare with reference code outside this repository
  skip themselves when that code is missing. *(proposed)*
- **`manual/`** holds the manual's sources ([manual.md](manual.md)): `manual/src/` the chapters,
  `web.css` and `manual.js` the page's, `print.css` the PDF's; `tools/build_manual.py` makes
  `dist/manual/`; its tests are `tests/manual/`, run by `fw/tests/run_manual.sh`.
- **`site/`** holds the web site's landing, `site/index.html`, with its pictures (`img/`, `screens/`), its pixel
  fonts (`fonts/`) and the tools that make them (`tools/`); `site/assemble.sh` puts the landing and the pages it
  links together, and `.github/workflows/pages.yml` publishes that on every push to `main` ([site.md](site.md)).
- **`assets/fonts/`** holds the Terminus fonts (SIL OFL 1.1). Tools turn them into C tables for the
  firmware and JS for the pages. Beside them the pages' font, Iosevka Aile, and the PDF manual's,
  Archivo (both SIL OFL 1.1), cut once from their releases by `fw/tools/webfont.py`.

## The built files (`dist/`, committed)

A folder for each kind of build; the pages are self-contained and open from the file system.

| File | What |
|---|---|
| **`firmware/`** | **PT/FM's firmware, the release** |
| `firmware/install.html` | installs the firmware on the FM-1 and uploads the demo disk (Chrome / Edge, Web MIDI): INSTALL FIRMWARE, the firmware alone (or, with CLEAN INSTALL switched on, everything erased after: an empty user area), and UPLOAD DEMO DISK ([files/blob.md](files/blob.md#seeding)) |
| `firmware/ptfm.fwsc` | the firmware package, for other installers |
| `firmware/demo.ptfmdsk` | the demo disk, built from `demo-disk/` by the packer ([files/disk.md](files/disk.md)) |
| `firmware/seed.html` | the data uploader for the real device: opens a data blob or a disk and seeds it (the files proposal, 12.2) *(not built yet: the install page seeds the demo disk, the emulator's SEED any disk)* |
| **`debug/`** | **the debug build: the same firmware with its figures** ([debug.md](debug.md)) |
| `debug/install.html` | installs it (CLEAN INSTALL on at first: everything on the FM-1 erased, an empty user area; UPLOAD DEMO DISK beside it; CLEAR USER DATA erases without installing), then reads its figures over USB once a second, runs its bench, gathers both into a report to copy |
| `debug/ptfm-debug.fwsc` | its package |
| **`emulator/`** | **what the emulator loads** (`emulator/index.html`, [emulator.md](emulator.md)) |
| `emulator/ptfm.wasm.js` | the firmware built for the emulator, as a script |
| `emulator/demo.ptfmdsk.js` | the demo disk as a script, for the emulator's DEMO DISK (a page from `file://` cannot read a file next to it) |
| `emulator/emulator.html` | the emulator in one file, to pass on: `emulator/index.html` with its stylesheets, scripts, the firmware and the demo disk written in (`emulator/build.py --release`, or `--page` alone) |
| **`editor/`** | **the editor** ([editor.md](editor.md)) |
| `editor/editor.html` | the editor in one file, the firmware's WebAssembly inside (`editor/build.py`) |
| **`manual/`** | **the manual** ([manual.md](manual.md)) |
| `manual/manual.html` | the manual as a page, in one file (`tools/build_manual.py`) |
| `manual/ptfm-manual.pdf` | the manual as a PDF (`tools/build_manual.py --pdf`) |
| **`playground/`** | **the playground to pass on** |
| `playground/playground.html` | the playground as one page, a single file to share: `playground/index.html` with every stylesheet and script written in, made by `tools/build_playground.py` (`--check`: whether it is current); the playground itself still needs no build |

- **Every kind of build has its folder;** the packages keep their own names (`ptfm.fwsc`,
  `ptfm-debug.fwsc`), as files that are passed on.
- **Committed:** the built files live next to their sources, which suits the GPL too.
- **Everything else is ignored:** objects, generated headers and logs go to `build/`.

### Three builds of the firmware: dev, debug and production

The firmware is made three ways, all real builds of the same sources; the playground is for the UI,
fast, and is none of them.

| | dev | debug | production |
|---|---|---|---|
| For | trying things, every day | measuring on the FM-1 ([debug.md](debug.md)) | a release |
| Made | by default | `fw/build.sh --debug` | `fw/build.sh --release` |
| Goes to | `build/` (not committed) | `dist/debug/` (committed) | `dist/firmware/` (committed) |
| The version (UI proposal, 6.8) | `fw/VERSION`'s number, `DEV` after it | the number, its own build time (month, day, hour, minute), `DEBUG` | the number alone, set by hand in `fw/VERSION` |
| What it holds more | | the figures, the bench and their USB answers; nothing else differs | |
| Its package identity (the installers take only `FM-1_<digits>`) | `FM-1_7101` | `FM-1_7102` | `FM-1_7101` |

The emulator's build is made two ways, dev (`build/fw/web/`) and production (`dist/emulator/`).

- *(proposed)* **What else differs:** a dev build may carry checks for finding faults (assertions that
  stop on a crash screen); a production build leaves them out. The figures are the debug build's. Nothing
  a user does differs between the three.
- **The emulator** runs either: the production build from `dist/emulator/`, or a dev build from `build/`.
- *(proposed)* **The same sources give the same bytes.** Built files carry the version or the git
  commit, not the time of the build, so a rebuild without changes leaves git clean.
- *(proposed)* **Notices travel with the pages.** `install.html`, `seed.html` and the emulator carry
  the licence texts of what they contain: GPL-3.0, plus Apache-2.0, MIT, BSD-3-Clause and OFL-1.1
  for third-party parts.

## Pages opened from the file system

The pages are opened straight from the file system, so every page works from `file://`, without a
web server:

- classic `<script>` files, no ES modules (Chrome refuses module scripts from `file://`);
- no `fetch()` of local files: data comes in as scripts, or as files the user picks;
- an AudioWorklet loads from a `data:` URL (a `blob:` URL is refused: the page's origin is null);
- no external resources (no CDNs, no web fonts);
- relative paths between folders work (`playground/` loads `../panel/device.js`).

## Data: only the demo data

The only patches, samples and songs in this repository are the demo disk's (`demo-disk/`): works under
licences that let anyone pass them on, each file and sample with its own, all listed in
`demo-disk/CREDITS.md`. Nothing else is shipped; other data sets are made into blobs by the same
tools, outside this repository.
