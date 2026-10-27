# The panel

Status: **settled** (2026-10-10).

The panel (`panel/`) is the virtual FM-1's controls for the pages that run PT/FM on a computer or a
phone: the playground ([playground.md](playground.md)) and the emulator ([emulator.md](emulator.md)).
It is made to be easy to use, not to look like the device: no picture of the FM-1, one big screen, and
small knobs, buttons and keys placed where they are easy to reach. Each lights as its LED does.

## What it has

- **The screen,** 240 × 240, as big as the view allows. Its pixels stay square and sharp: a whole
  number of display pixels each where the size allows. Right-click saves or copies it.
- **The 14 buttons,** named as printed, each lit in its LED's colour (PLAY red or green, REC red, the
  rest white) at the LED's level: off, dim or lit.
- **The 7 knobs,** each a control with **−** and **+** at its ends and its name between:
  - a tap on − or + is one click (on letting go); held still, it clicks again and again, faster and
    faster;
  - dragging anywhere on it turns it, up or right clockwise (a click every 14 px, once the finger has
    moved 8 px), so a drag that starts on − or + turns it too;
  - the mouse's wheel turns it;
  - while it turns, it shows the clicks of the turn (`+3`), for a second after the last.
- **MASTER,** a bar to drag (or the wheel), its % written on it; a double-click puts it back at half.
- **The 27 keys:** 16 white, the 11 black ones over the gaps they sit at, named as printed (OP1 …
  POLY), each lit as its LED.
- **Holding:** a pointer holds what it presses until let go, so on a touch screen several fingers hold
  several controls (SEL held with one finger while another turns PRESETS: SEL + PRESETS). With a mouse,
  Shift+click latches a button or key until the next click, and a button held keeps the pointer, so the
  wheel can turn a knob while it is held. The computer's keyboard plays the keys
  (`panel/keyboard.js`).
- **Gliding:** a pointer that pressed a note key follows it: moved onto another note key, it lets the one it
  held go and plays that one, as a finger slid along a keyboard; off the note keys it keeps the last. A button
  stays held wherever its pointer goes.

## Three views

The panel picks its view from the window, again on every resize or turn of the phone, and sizes itself to
the room the page gives it ([below](#the-page)), not to the whole window.

### WIDE: a computer

```
[− SELECT    +]  +------------------+  [− K1  +]
[− ALGORITHM +]  |                  |  [− K2  +]
[− PRESETS   +]  |                  |  [− K3  +]
                 |    the screen    |  [− K4  +]
[MASTER  50%  ]  |                  |  [FX  ][HOME]
[OCT-][OCT+]     |                  |  [SEL ][SAVE]
                 |                  |  [ENV ][ARP ]
                 |                  |  [LFO ][SEQ ]
                 +------------------+  [EDIT][PLAY]
                                       [GLO ][REC ]
 OP1 OP2 OP3   OP4 OP5   OP6 PIT GLO   MONO POLY   ( )
[  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ]
```

- **The left side holds what the FM-1 has left of its screen:** SELECT, ALGORITHM, PRESETS, MASTER,
  OCT- and OCT+. **The right side holds K1-K4 and the 12 buttons,** in the panel's columns as rows (FX
  beside HOME), so the hands find them where the device has them.
- **The keys run below,** one row, F3 to G5.
- **ZOOM** (the page's sidebar): the screen at 1×, 2× or 3× (CSS pixels a screen pixel, rounded to whole
  display pixels) or FIT, the largest whole size the room holds beside the controls (the default; kept in the
  browser). The other two views size the screen themselves (ZOOM's keys dim there).

### PORTRAIT: a phone held upright

```
[MENU] PT/FM      TAP TO START THE SOUND   the page's header
+------------------------------+
|                              |
|          the screen          |   across the phone
|                              |
+------------------------------+
[− SELECT +][− ALGORITHM +][− PRESETS +]
[− K1 +][− K2 +][− K3 +][− K4 +]
[OCT-][ FX ][ SEL][ ENV][ LFO][EDIT][ GLO]
[OCT+][HOME][SAVE][ ARP][ SEQ][PLAY][ REC]
[           MASTER 50%            ]
   PIT GLO   MONO POLY   ( )
[  ][  ][  ][  ][  ][  ][  ][  ]     G4-G5
  OP1 OP2 OP3   OP4 OP5   OP6
[  ][  ][  ][  ][  ][  ][  ][  ]     F3-F4
                                     28 px free: the phone's gesture bar
```

- **Picked by itself** when the window stands upright and is either a touch screen or narrower than
  760 px. Adding `#wide`, `#portrait` or `#sideways` to the page's address forces a view, to try it.
- **Sized for fingers, the keys first:** the white keys 58 px high, the black 44, the knobs and
  buttons 36, MASTER a 24-px bar; the screen takes the height the rest leaves, at most the phone's width.
  On a tall phone the room left below goes to the keys (up to 84 px white, 60 px black).
- **The keys in two rows of eight,** the lower half (F3-F4, with OP1-OP6) below, the upper (G4-G5)
  above.
- **The buttons as the panel has them:** FX … GLO over HOME … REC, OCT- over OCT+ before them.
- **The bottom 28 px stay free** (or the phone's safe area, if larger): a press held at the bottom edge
  is the phone's own (Android's gesture bar).
- **A finger on the panel is the panel's:** no scrolling, zooming or long-press menu there, nor the
  gestures of an app the page is shown in.
- The page keeps only a slim header above the screen (MENU, the logo, the sound's sign); no footer.

### SIDEWAYS: a phone held sideways

```
[− SELECT    +]  +------------+  [− K1 +][− K2 +]
[− ALGORITHM +]  |            |  [− K3 +][− K4 +]
[− PRESETS   +]  | the screen |
[MASTER  50%  ]  |            |  [FX  ][SEL ][ENV ][LFO ][EDIT][GLO ]
[OCT-][OCT+]     |            |  [HOME][SAVE][ARP ][SEQ ][PLAY][REC ]
[    MENU     ]  +------------+
 OP1 OP2 OP3   OP4 OP5   OP6 PIT GLO   MONO POLY   ( )
[  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ][  ]
```

- **Picked by itself** when the window lies down and is at most 520 px high (a phone held sideways, or a
  low window); `#sideways` in the address forces it *(proposed: the whole view)*.
- **Every pixel of height is the panel's:** the page has no header and no footer; its MENU is a key of the
  panel's own, under OCT, and the sound's sign shows over the screen's top until the first tap.
- **The keys in one row** along the bottom, F3 to G5, 30 / 40 px high (black / white) and growing into the
  room the screen's whole steps leave (up to 44 / 60 px).
- **The screen as high as the keys leave,** whole display pixels a screen pixel (on a phone of 360-412 px
  high: 240-274 px); left of it SELECT, ALGORITHM, PRESETS, MASTER and OCT- beside OCT+, right of it K1-K4
  two a row and the 12 buttons as the panel has them, FX … GLO over HOME … REC, as big as the room there
  allows: two thumbs reach both sides.

## The page

The playground and the emulator have the same page around the panel, in the look of the install and debug
pages: the FM-1 screen's colours on black, the system's own sans-serif (no web font), keys with their names in
yellow, groups headed in cyan. `panel/page.css` is its look and `panel/page.js` its behaviour, for both
*(proposed: the whole page)*.

- **A sidebar** on the left: the logo and the page's name, then its groups: SOUND (POWER on the emulator: off
  until the first click, then ON while the sound runs, RESET; ERASE DATA, tapped twice, erases what the page
  keeps in the browser, the user area included, then starts it again: a new FM-1), DISKS (DEMO DISK; the
  emulator's SEED too), THE FM-1 OVER MIDI (CONNECT, the state, how), SCREEN (ZOOM), HOW TO PLAY; the emulator
  adds ITS USB MIDI (IN, OUT) and FIRMWARE (the file and its version).
  It scrolls by itself when it is longer than the window.
- **The user area is kept in the browser** (IndexedDB), on both: what SAVE writes survives a reload, as the
  FM-1's flash survives a power cycle; the changes not saved are the tab's alone, as the FM-1's RAM, gone with a
  reload or the tab. With nothing kept the page starts as a new FM-1: an empty user area, the built-ins alone.
  DEMO DISK writes the demo disk into the user area, in place of what is there (the playground's refuses while
  changes are not saved, as the FM-1's seeding does: SAVE or REVERT first), then starts again.
- **A header** over the panel: the page's name, a word on what it is (the emulator: its firmware), and on
  the right the sound's sign until the sound starts: `CLICK ANYWHERE TO START THE SOUND` (`TAP TO START THE
  SOUND` on a touch screen; the emulator: `… TO SWITCH THE FM-1 ON`), blinking when PLAY or a key asks for
  sound. The emulator's firmware stopping shows there too.
- **A footer** under it: what is held and the last thing that happened (`HELD: SEL HOME · LAST: K1 +1`); the
  emulator's says where its flash is kept.
- **Narrower than 1000 px, and on a phone, the sidebar is a drawer:** MENU in the header (SIDEWAYS: the
  panel's MENU key) slides it over the page from the left; a tap beside it, CLOSE or Escape puts it away. The
  header then shows the logo, and in PORTRAIT only MENU, the logo and the sound's sign.
- **The page never scrolls,** at any width: only the sidebar does, inside itself.
- **The sound starts with the first tap or click** on the page (a browser's rule; MENU's counts too).
- **Where a page's rules refuse an audio worklet** (a page shown inside another site), the playground
  runs the same sound code on the page's own thread ([playground.md](playground.md)).
- **MIDI:** the real FM-1 as the controller (MIDI CONTROLLER, mirror mode) works the panel as its own controls, and its LEDs
  follow the page's ([controller.md](controller.md)).
