# The manual, PT/FM OWNER'S MANUAL

Status: **settled** (2026-10-27). The record: [the manual proposal](proposals/202610271851-manual.md).

**The manual** tells someone who has just installed PT/FM how it works, and stays the place to look anything up
later. One hand-written text, `manual/src/`, is built into two outputs: `dist/manual/manual.html` (the page) and
`dist/manual/ptfm-manual.pdf` (the PDF). Its title is `PT/FM OWNER'S MANUAL`, with `POCKET TRACKER / FUSION MACHINE`,
`BY HITOSU` and `for PT/FM` and the version from `fw/VERSION` under it: the manual is the one document that names the
version.

## 1. The sources

`manual/src/`, one HTML fragment a chapter, read in the order of their names:

| File | Chapter id | What |
|---|---|---|
| `00-front.html` | `front` | the cover, how the manual writes things |
| `01-glance.html` ... `05-first-steps.html` | `glance`, `install`, `panel`, `basics`, `first-steps` | the basics |
| `06-songs.html` ... `15-fx.html` | the screen's name in lower case | a screen each: SONGS, EDITOR, INSTRUMENTS, PLAYBACK, DJ, VISUAL, STATUS, SETTINGS, LOOPS, FX |
| `16-midi-usb.html`, `17-editor-page.html` | `midi-usb`, `editor-page` | MIDI and USB; the EDITOR page |
| `A-shortcuts.html` ... `F-midi-chart.html` | `shortcuts`, `messages`, `troubleshooting`, `data`, `specs`, `midi-chart` | the appendices A to F |
| `G-glossary.html` | `glossary` | the words the manual uses, a line each, with where each is explained |
| `Z-credits.html` | `credits` | the licences |

The contents are made by the build; so is the PDF's index (appendix H). A chapter's number is its file's: `01` to
`17`, `A` to `G`.

**The vocabulary** (nothing else is used; both outputs render it):

| Source | Means |
|---|---|
| `<!-- SPDX ... Copyright ... -->` | each file's first line (dropped by the build) |
| `<section class="chapter" id="ID" data-kind="front\|basics\|screen\|appendix">` + `<h2>` | a chapter |
| `<section class="topic" id="ID">` + `<h3>` | a topic: a side head in the PDF, an entry of the contents |
| `<ol class="steps">` | numbered steps |
| `<aside class="memo">`, `<aside class="note">` | a MEMO (a tip), a NOTE (a caution) |
| `<kbd>SEQ</kbd>` | a button or knob, by its name on the panel (`PLAY` is the PLAY/STOP button, `GLO` the button) |
| `<kbd class="b">POLY</kbd>` | a black key by its print (`GLO` the black key); `BLANK` the unprinted one; `black key` any |
| `<kbd class="w">3</kbd>` | a white key by its number, 1-16 from the left; `1-8` several; `white key` any |
| `<kbd>KEYS</kbd>` | all the keys |
| `<span class="combo"><kbd>SEL</kbd> + <kbd>PRESETS</kbd></span>` | a combination: it lights as one |
| `<kbd>FX</kbd> held` | a hold, in words |
| `<samp class="msg">LOOP 03 MUTED</samp>` | a message the FM-1 shows; `data-t="PATTERN INTO #"` when it is filled in from a template |
| `<figure data-shot="ID"><figcaption>` | a screenshot ([section 3](#3-screenshots)) |
| `<figure data-panel="ID ID ..." data-crop="keys\|full">` | the panel with numbered callouts on the controls listed |
| `<table class="keys">` | a key table: the first cell names the keys, the row lights them |
| `<dl class="params">`, `<dt>`, `<dd>`, `<span class="range">` | a parameter, a setting or an effect by its name on the screen; its values |
| `{{VERSION}}` | `fw/VERSION`'s number |

Appendix A has a topic `shortcuts-everywhere` and a topic `shortcuts-SCREEN` for each screen.

## 2. The build

`python3 -I tools/build_manual.py` (from `ptfm/`) makes the screenshots, then both pages; `--pdf` prints the PDF too.

- **The page** (`dist/manual/manual.html`): `<!doctype html>`, one file, everything inlined (stylesheet
  `manual/web.css`, script `manual/manual.js`, images, the panel). PT/FM's dark page frame and the system's sans-serif
  (`panel/page.css`'s tokens); the header's PT/FM mark leads to PT/FM's page (https://hitosu.github.io/ptfm/), and
  DOWNLOAD PDF MANUAL beside it; the contents in a sidebar with a search over headings and text, the chapters' titles
  with only the chapter being read showing its topics (a search shows every match); the panel drawing in a
  column beside the text, sticky, its screen showing the current screenshot small (STATUS where there is none), and
  **under it a strip of the chapter's screenshots** at the column's width, with their captions, scrolling on its own:
  the current one (the last figure of the chapter above the reading line, 45 % down the window, else the chapter's
  first) highlighted and scrolled into view; a click on one takes the text there. HIDE THE FM-1 above the
  drawing folds it away (SHOW THE FM-1 brings it back, and so does a click on a key named in the text), the strip
  taking the column; the browser remembers which.
  A key, button or knob of the drawing pointed at shows its name and kind on the drawing's screen (`OP1`,
  `BLACK KEY`) while pointed at.
  Below about 900 px
  (a phone, with no pointing to light keys by) there is no drawing: the screenshots stand in the text, and the
  contents become a drawer. On a phone the page fills the screen (`viewport-fit=cover`): the sticky header reaches
  up under the status bar, so no text scrolls by there, and the page keeps clear of a notch in landscape (the safe
  area's insets).
- **The key lighting:** the build gives every key, combination and key table row a `data-k` (the panel's element ids
  it lights) and `tabindex="0"`; pointing at it, focusing it or tapping it lights those keys on the drawing, the whole
  combination at once, and leaving clears them. A key name the drawing has no element for fails the build.
- **The panel drawing** (`tools/manual_panel.py`) is drawn from `panel/layout.js`'s numbers at build time: on the page
  the dark panel whose controls light, in the PDF line art with numbered callouts. Element ids: `k-MASTER`,
  `k-SELECT`, `k-PRESETS`, `k-ALGORITHM`, `k-K1`-`k-K4`; `b-` and the button's name; `bk-` and the black key's print,
  `bk-BLANK`; `w-1`-`w-16`.
- **The PDF** (`dist/manual/ptfm-manual.pdf`): a print page (`build/manual/print.html`, stylesheet
  `manual/print.css`) printed by a headless Chrome, A4. The look of a 1990s device manual: a black band with the
  chapter's number, side heads in a wide left column, numbered steps with bold numerals, MEMO and NOTE boxes in one
  spot colour (red, `#C8102E`), the LCD figures framed, a ruled footer with the page number and the manual's name,
  each chapter on a new page; an index (H) last, of the chapters' titles (their first page in bold), the topics'
  headings, the `<dt>` terms (one named as a panel control told apart by its topic, `OP1 (DX7)`, the panel's own
  names pointing at the panel chapter) and a short list of words no heading names (`INDEX_MORE`: mute, backup,
  battery, messages ...). The contents' and the
  index's page numbers come from a first printing (each target's page read from the PDF's named destinations,
  `pdfinfo -dests`: Chrome names one for every id a link points at), filled in for the second; a target on another
  page in the second fails the build. The finished PDF's links jump to their pages, not to names. The PDF's dates
  and identifiers are fixed, so the same sources give the same bytes.
- **Fonts:** the page uses the system's sans-serif, as the other pages; the PDF uses Archivo (SIL OFL 1.1;
  `assets/fonts/Archivo.woff2`, cut once from its release by `fw/tools/webfont.py --archivo`, to the characters the
  manual uses; licence `assets/fonts/Archivo-LICENSE.txt`), inlined.

## 3. Screenshots

`fw/tests/manual_shots.c` runs all of `fw/core` on the PC on the host tests' fake FM-1 with the demo disk, scripts
each figure's state by its id and writes the screen. Every build makes them anew. Each script checks it reached its
screen and state before the shot; a failed check fails the build. The page shows them in colour at twice their size;
the PDF as a printed LCD: grey levels under 40, 110 and 190 (of 255) and above, printed as `#CDD2C4`, `#8E9488`,
`#4E544A` and `#181C16`. Each is also a file for the web site, `site/screens/manual/ID.png`: in colour, the screen's
own 240 x 240 pixels; published, no page links them ([site.md](site.md)).

## 4. The tests

`fw/tests/run_manual.sh` (part of `fw/tests/run_tests.sh`) builds the manual and runs `tests/manual/`. They fail on:

1. **a shortcut missing:** every help line naming a key (each screen's help pages and the shared ones), every row of
   [ui/controls.md](ui/controls.md#the-key-map)'s key map, every row of a screen's controls table needs a row with the
   same keys (and the same hold) in that screen's chapter's key tables and in appendix A;
2. **a message missing or wrong:** every text the firmware gives to a toast or a question needs its line in appendix B;
   every message quoted must be a text of the firmware;
3. **a place missing:** each screen its chapter; each SETTINGS item, each effect and each of its knobs, each instrument
   parameter its `<dt>` in its chapter (or a section's heading: each effect has a section of its own);
4. **a broken figure or link:** a screenshot id the screenshots program does not know, a `#link` with no target, an id
   twice, two keys joined by `+` outside a combination;
5. **what is left for players to find, mentioned:** FOMA's corners and score, the touch that steers FOMA, the header's
   warning when the sound takes too much of the chip;
6. **the version** not on the cover and the page's header;
7. **a character the PDF's font lacks.**

`--pdf` also builds the PDF twice and compares the two.

## 5. Writing

- For a newcomer: plain, short sentences; no word of the specs' own making without saying what it is.
- Keys as on the panel: `SEQ`, white key 3, the black key `POLY`.
- The FM-1's texts quoted exactly, spelling included (the help's header is `LOOK FOR ONSWERS`).
- No key or path that the firmware does not have.
- The credits are the licences only: GPL-3.0-only; SIL OFL 1.1 for Terminus and Archivo; Apache-2.0 for the three
  files of JieLi's SDK in the firmware packages; the demo disk's licences, with a pointer to its `CREDITS.md`.
