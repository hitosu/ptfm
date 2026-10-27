# The help: HOME + LFO, "LOOK FOR ONSWERS"

Status: **settled** (2026-10-09). The record: [the UI proposal](../proposals/202610072229-ui.md),
sections 6.10 and 13.

A short manual for the screen you are on, instead of a manual screen. This file says how the help
works, how its text is written, marked and stored, and holds the shared pages; each screen's own
pages are at the end of its file ([screens/](README.md#the-files)).

## Opening it

- **HOME + LFO,** on any screen: HOME tapped, then LFO (HOME's combination,
  [controls.md](controls.md#homes-combination)). Held, at any depth: LFO while HOME is held, or HOME
  while LFO is (where something is open, a HOME tap backs out of it first).
- It opens as an overlay. The header reads `LOOK FOR ONSWERS` and the page (`LOOK FOR ONSWERS 1/13`, counted from 1)
  in place of the screen's name; the screen is named by the first page's title
  (`EDITOR: WRITE PATTERNS`).
- The grace period ([controls.md](controls.md)) makes sure the combination never clears anything.
- HOME stays lit while the help is open ([leds.md](leds.md)).

## What it shows

For the screen it was opened on: the screen's controls (knobs, buttons, and what held buttons do
there), then the shared pages ([below](#the-shared-pages)). On STATUS the shared pages come first
instead ([screens/status.md](screens/status.md)).

## How a page looks

Up to 17 lines of T, 40 characters each, colour-coded as the screens are.
- **By the line's shape:**
  - the first line is the page's title, in cyan; so is any line with no `:` that is not indented
    (`BLOCKS`, `SAMPLE SOUNDS`);
  - every other line is one pair: what you do, then `:`, then what happens. The text is light grey; a
    line indented by two spaces goes on from the one above.
- **By the words:**
  - **every key name is yellow,** wherever it stands, so a key reads as a key at a glance: buttons
    (`HOME`, `OCT+`, `SEL`), knobs (`SELECT`, `K1`), black keys (`OP3`, `POLY`, the unprinted
    `BLANK KEY`), and `KEYS`, `WHITE KEY`, `BLACK KEY`;
  - a reading in brackets, `(SHOW ELSE, QUICK)`, is muted, in dark grey: the label stays the thing
    to look for;
  - a screen's name is in its group's colour (`EDITOR`, `FX`, `LOOPS`; [colours.md](colours.md));
  - the word `RED` is red;
  - engine names stay plain here: yellow belongs to the keys on these pages.
- **The help names only what works.** A key kept for later (OP6, as a screen's key) is never mentioned
  where it does nothing; DJ's help names OP6, which works there.

## Marks

- **How it is stored, as a terminal does it:** a few reserved tokens only set the colour for the
  characters that follow, until the next such token or the line's end. A colour token costs one byte.
- **Marked in the source, not guessed:** a word list would also colour the verb "play" or the MONO in
  "MONO FOR THE SPEAKER". So the source marks them, and the build turns the marks into colour tokens:

  | Mark | Means | Drawn |
  |---|---|---|
  | `{HOME}` | a key: a button, a knob, a black key (`BLANK KEY` too), `KEYS`, `WHITE KEY`, `BLACK KEY` | the name, yellow |
  | `[EDITOR]` | a screen | the name, in its group's colour |
  | `{SEQ} (SHOW ELSE, QUICK)` | a key with its reading written after it | the key yellow, the reading dark grey |
  | `{LFO*}`, `{SEQ*}` | a key with a reading, the reading added where this screen's help has not named the key yet (on the shared pages: `{LFO*}` on KNOBS, `{SEQ*}` on THE HOME KEY) | yellow, and the reading dark grey when added |
  | `RED`, as a word of its own | the colour | red; not marked, the build finds it |

- **Braces and square brackets are kept for the marks:** the help's text never shows them, so a mark
  can never be meant as text. The pages in these specs are written with their marks, as the source
  holds them; a marked line may pass 40 characters in the source, never on the screen.

## Moving and leaving

- **SELECT** turns its pages (N/M in the header); **HOME** closes it, as always.
- **The first page of every help ends with `PRESS HOME TO GET BACK`.** The help draws that line
  itself, as the page's 17th line, so a first page keeps its last two lines free (15 lines of its
  own, then a blank one).

## The shared pages' place

What is shared by all screens (the global keys, the control rules of [controls.md](controls.md),
saving, the themes) sits on the last pages of every screen's help. On STATUS those pages come first
instead.

## Readings

A button whose label reads wrong ([README.md](README.md)) is named with its reading once in a
screen's help, at its first mention in page order: `ENV (EXPLORE NAMED VAULTS): PATTERNS`. Every
later mention, on that page or the next ones, uses the label alone.
- **The shared pages are stored once.** The build applies the first-mention rule in each help's own
  page order and records, for each screen, which buttons it has named: the shared pages give a
  reading only where that screen has not. On STATUS the shared pages come first, so they carry the
  readings there, and STATUS's own page uses the labels alone.
- **LFO's reading,** `LEAVE FIELD OPEN`, is given only where LFO clears, deletes or resets something
  (`{LFO*}: DELETE A LETTER` on the KNOBS page). `LOOK FOR ONSWERS` is the help's own header.
- **The screen keys go by theirs** on the shared SCREENS page: `OP1 (OPEN PAGE 1): SONGS`.
- **Every line has to fit its 40 characters** with any reading it may get; the build checks it.

## Writing the text

- **Every line fits 40 characters and every page 17 lines,** with the readings it may get.
- **The characters:** printable ASCII without the lowercase letters, the fonts' characters
  ([frame.md](frame.md#fonts)); capitals only.
- **It reads plainly to a newcomer,** with no guessing what a line means, and then compresses well.
- **Written for the tokenizer:** the text uses few words, always in the same phrasings (`HOME, X:` for
  HOME's combination, `HOLD X + Y:` for a held one, `HOME COMES BACK HERE`, `X: ON / OFF`). When two
  wordings say the same, the one that repeats an existing phrase wins.
- **Where the pages are:** each screen's own pages, in order, in its file's Help part; the shared ones
  below. The last line of a first page, `PRESS HOME TO GET BACK`, is drawn by the help and not written
  in the pages.

## Compression

The help is stored packed, with every other fixed text of the firmware, by one tokenizer: byte-pair
encoding, one table, one decoder ([texts.md](texts.md)). What is the help's own:
- **Its size:** 45 pages (39 of the screens' own, the EDITOR's 10 among them, 6 shared), about 23.8 KB as
  plain text; about half that packed.
- **Unpacking** is for the one page on screen, a line at a time.
- **The readings:** on a screen's own pages the build writes them in, since it knows the page order.
  On the shared pages, a button that may need one is a token of its own, and the help adds the reading
  when that screen's pages have not named the button.
- **Part of the build toolchain:** the help is written as plain text in the repository; the texts'
  tool in `tools/` ([texts.md](texts.md#in-the-firmwares-source)) packs it on every firmware build:
  - it checks every page (40 characters as drawn, with the readings each screen's page order gives, 17
    lines, the fonts' characters);
  - it adds the readings and the colour tokens before training the tokenizer on the help and the
    other texts together;
  - a host test unpacks every page with the firmware's own code and compares it with the text.

## Layout

### A help page

The overlay covers the whole body; the header shows the help's name and page
([frame.md](frame.md#the-headers-forms)).

```
LOOK FOR ONSWERS 1/16             # |||      header, S: cyan, the page light grey
----------------------------------------
EDITOR: WRITE PATTERNS                       line 1: the title
PATTERN: TRACKS ACROSS, ROWS DOWN            line 2
  A CELL: A NOTE, ITS SOUND, VOLUME          indented: goes on from the line above
  AND UP TO 4 COMMANDS
SELECT: UP / DOWN THE ROWS
ALGORITHM: ACROSS THE TRACKS
SEQ (SHOW ELSE, QUICK): SEE 8, 4, 2 OR 1
  TRACKS AT ONCE
REC: WRITE WITH THE KEYS, ON / OFF
PRESETS: THE SOUND THE KEYS WRITE
PLAY: LOOP IT FROM THE CURSOR'S ROW
ENV (EXPLORE NAMED VAULTS): PATTERNS
  ENV AGAIN OPENS THE ONE PICKED
  HOLD ENV: TAKE THE CURSOR'S SOUND
GLO: TRACKS, ROWS, EDIT STEP
                                             line 16 free on a first page
PRESS HOME TO GET BACK                       line 17, drawn by the help
----------------------------------------
0B:05 SCREAMER          A04 SLIDE DOWN 4     the footer stays the screen's
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the ground | 0, 17 | 240 × 210 | | the body, cleared |
| line *i* (0-16) | 0, 19 + 12 × *i* | up to 240 × 12 | T | the page's lines; the last ends at y 223 |
| `PRESS HOME TO GET BACK` | 0, 211 | 132 × 12 | T | a first page's 17th line |

Colours: by the page's rules above: titles cyan, text light grey, keys yellow, readings dark grey,
screens in their group's colour, `RED` red. `PRESS HOME TO GET BACK` light grey with `HOME` yellow.

## Texts

- **The header:** `LOOK FOR ONSWERS` and the page, N/M ([frame.md](frame.md#numbers)).
- **A first page's last line:** `PRESS HOME TO GET BACK`.
- **The pages:** each screen's in its file; the shared ones below.

## The shared pages

Stored once, at the end of every screen's help (first on STATUS). A button that the screen's own pages
have not named gets its reading here (`{SEQ*}` on THE HOME KEY page); LFO only where it clears, deletes
or resets (`{LFO*}: DELETE A LETTER` on the KNOBS page).

```
GLOBAL KEYS: ON EVERY SCREEN
{KEYS}: PLAY THE SOUND PICKED, UNLESS
  THE SCREEN GIVES THEM A JOB
{PRESETS}: PICK THE SOUND
{SEL}, {PRESETS}: PICK A CARTRIDGE,
  A BANK OF SOUNDS
{OCT-} / {OCT+}: {KEYS} AN OCTAVE DOWN / UP
{PLAY}: PLAY / PAUSE
  HELD: STOP, BACK TO THE START
{SAVE}: SAVE ALL CHANGES
{FX}: THE EFFECTS SCREEN, AGAIN: BACK
HOLD {FX} + {WHITE KEY}: AN EFFECT
  WHILE THE KEY IS HELD (A PUNCH-IN)
{HOME}: BACK; MORE ON THE NEXT PAGE
{HOME}, {LFO}: THIS HELP
```

On STATUS, where this page comes first, the help adds `PRESS HOME TO GET BACK` as its 17th line.

```
THE HOME KEY
{HOME}: BACK OUT OF WHAT IS OPEN
  WITH NOTHING OPEN: THE KEYS BELOW
{HOME}, A KEY: TAP {HOME}, THEN THE KEY
  OR HOLD {HOME} AND PRESS THE KEY
  {HOME} BREATHES WHILE IT WAITS
{HOME}, {BLACK KEY}: A SCREEN (NEXT PAGE)
{HOME}, {WHITE KEY}: MUTE A TRACK
  THE 16 WHITE KEYS: TRACKS 00-0F
  HELD: SOLO IT INSTEAD
  {SEQ*} FIRST: SOLO
{HOME}, {PLAY}: BACK TO THE START
{HOME}, {REC}: ALL SOUND OFF
  SILENT WHILE {REC} IS HELD
  HELD 2 S: EVERY EFFECT SWITCHED OFF
{HOME}, {LFO}: HELP, LOOK FOR ONSWERS
{HOME} OPEN: THE KEYS THAT WORK LIGHT UP
```

```
SCREENS: {HOME}, THEN A {BLACK KEY}
{OP1} (OPEN PAGE 1): [SONGS]
{OP2} (OPEN PAGE 2): [EDITOR]
{OP3} (OPEN PAGE 3): [INSTRUMENTS]
{OP4} (OPEN PAGE 4): [PLAYBACK]
{OP5} (OPEN PAGE 5): [DJ]
{PIT} (PICTURES IN TIME): [VISUAL]
{GLO}: [STATUS]
{MONO} (MY OWN NERDY OPTIONS): [SETTINGS]
{POLY} (PARTY ON, LOOP YOURSELF): [LOOPS]
{FX}: [FX], A BUTTON OF ITS OWN
{HOME}, {SELECT}: SCREEN AFTER SCREEN
  [STATUS] [SONGS] [EDITOR] [INSTRUMENTS]
  [PLAYBACK] [VISUAL] [FX] [LOOPS] [DJ]
  [SETTINGS], THEN ROUND AGAIN
  {HOME} AGAIN: STAY ON THAT SCREEN
```

```
BUTTONS: TAP AND HOLD
TAP: PRESS AND LET GO; MOST ACT THEN
{PLAY}, {REC}, {KEYS}: ACT AS PRESSED
HOLD {SEL} + {K1}: KEEP {SEL} DOWN AND
  TURN {K1}; AGAIN AND AGAIN IS FINE
  {SEL} LET GO UP TO 0.1 S EARLY: OK
HOLD ONE ALONE: IT SHOWS WHAT IT DOES
  WITH THE OTHER KEYS AND KNOBS
THE KEY THAT OPENS: CLOSES, KEEPS EDITS
{HOME}: BACK, CANCEL, DROPS EDITS
QUESTIONS: {OCT+} YES, {OCT-} NO
  {HOME}: CANCEL, NOTHING HAPPENS
LIGHT DIM: IT WORKS ON THIS SCREEN
LIGHT OFF: IT DOES NOTHING HERE
LIGHT ON: ITS MODE OR LIST IS OPEN
HOLD TIME AND SLACK: IN [SETTINGS]
```

```
KNOBS
{SELECT}: MOVE UP / DOWN
{ALGORITHM}: MOVE LEFT / RIGHT
{K1}-{K4}: CHANGE VALUES AT THE CURSOR
TURN FAST: BIGGER STEPS
HOLD {SEL} + A KNOB: BIG STEPS
AT AN END: THE CURSOR STOPS
  PRESETS, CARTRIDGES, SCREENS: ROUND
LISTS AND MENUS
THE KEY THAT OPENED IT: PICK, OK
{SEL}: A MENU, ITS LAST CHOICE READY
MOVE: {SELECT} CARRIES, {SEL} PUTS DOWN
TYPING NAMES: {EDIT} DONE, {HOME} CANCEL
{ALGORITHM}: MOVE ALONG THE NAME
{SELECT}: CHANGE THE LETTER
{LFO*}: DELETE A LETTER
{OCT+}: ADD A SPACE
```

```
SAVING YOUR WORK
EDITS: KEPT IN MEMORY UNTIL {SAVE}
POWER OFF: UNSAVED EDITS ARE LOST
{SAVE} LIGHT DIM: SOMETHING UNSAVED
{SAVE} WHILE PLAYING: WAITS FOR A PAUSE
  {SAVE} BLINKS WHILE IT WAITS
HOLD {SAVE} + {OCT-}: DROP ALL EDITS
  SINCE THE LAST SAVE (IT ASKS FIRST)
{SEL}, REVERT: THIS ONE BACK AS SAVED
MOVE OR DELETE A THING IN USE: IT ASKS
  YES: WHAT USES IT FOLLOWS, ALL SAVED
  NO: WHAT USES IT KEEPS THE OLD NUMBER
A MISSING THING: RED, AND SILENT
MEMORY FOR EDITS FULL: IT ASKS TO SAVE
BROWN NUMBER: BUILT IN; EDIT IT AS ANY,
  {SAVE} KEEPS YOUR CHANGES
COLOURS: IN [SETTINGS], THEME
```
