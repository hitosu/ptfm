# PLAYBACK

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.4 and 13.4.

PLAYBACK watches what plays: the playing pattern, scrolling with the play position, in the EDITOR's
grid ([editor.md](editor.md#the-grid)). Its black key is OP4
([../navigation.md](../navigation.md)); it is in the PLAY group.

## What it shows

- **A view of what plays.** It shows the playing pattern and scrolls with the play position. It has no
  cursor, and nothing on it can be edited.
- **What it shows when nothing plays:** where playback paused, or, if nothing has played yet, the
  current song's start; with no song to show, a line saying PLAY starts the song.
- **The header's page** is the song position. While the EDITOR's pattern loop plays, there is no
  song position: the header shows the pattern (`PAT 0C`) and the footer `PAT 0C ROW 1F`; on the EDITOR's own
  pattern ([editor.md](editor.md#the-editors-own-pattern)) the header `EDITOR` and the footer `ROW 1F`.
- **LOOPS' loops, playing on outside LOOPS** (or what played last): a loop a track, each row showing the cell
  of the loop's lowest lane that has one there as the loops play it (repeats included), the header `LOOPS`,
  the footer `LOOPS ROW 05`. SELECT and K1 do nothing there (the loops are played in LOOPS); EDIT goes to
  LOOPS.
- **The footer's right side:** the play position (`POS 02 ROW 1F`) ([../frame.md](../frame.md#the-footer)).

## Controls

| Control | Does |
|---|---|
| SEQ | 8, 4, 2 or 1 tracks (1: the EDITOR's 1-track view, its instrument at the playing row; the header's `SEQ 8 TRACKS`) |
| ALGORITHM | moves the cursor track, a track a click; the view scrolls sideways only to keep it in sight. The cursor track's column has a faint background (dark grey's deep shade, under the playing row's band), its number white |
| SELECT | plays row by row. The first click, either way, pauses playback and plays the current row again. Then each click forward plays the next row, and each click back the previous one, as if it came next (no reset, no replay from the pattern's start). PLAY resumes from there |
| K1 | another position of the song: playing, the jump comes as the current row ends (clicks within one row add up), and it is a clean start there: every note of the song is cut and the effects' tails emptied (nothing of the position before rings on into the new one), then the song plays from that position's first row as PLAY from there would, the rows before it run through first ([../transport.md](../transport.md#play): their instruments, mixer and FX commands, the song's FX setup as there); paused, PLAY resumes from that position's first row. It stops at the song's first and last positions. The keys' own notes play on |
| K2 | the view, as SEQ's: 1, 2, 4 or 8 tracks, clockwise more; it stops at 1 and 8 (the header: `K2 VIEW 4 TRACKS`). It works on LOOPS' loops too |
| K4 | **the playback speed:** the song's tempo as a share of its own, as a DJ deck's tempo is ([dj.md](dj.md)), turned in BPM: 1 BPM a click, SEL held 10, from 3 to 600 BPM whatever the song's own tempo, as a deck's ([dj.md](dj.md#tempo-and-sync)); kept as the share (so its commands scale with it); heard at once, playing or not (a paused song starts at it). Every tempo the song plays is scaled by it, its set tempo and its tempo commands alike: at half speed a command setting 140 BPM plays 70 (the header: `K4 TEMPO 124.0 BPM -3.2%`, the BPM at the song's set tempo, the change against it). Not an edit and not stored (a song's own tempo is its TEMPO, its properties' on SONGS, [songs.md](songs.md)): it stays through pauses, K1's jumps, new starts and other songs (each song at that share of its own tempo), and goes back to 100 % when anything but a song starts to play (the EDITOR's pattern loop, LOOPS, DJ), and at power-off. Nothing on LOOPS' loops or the EDITOR's pattern loop |
| K3 | **the songs' volume:** dB on each song's own level, as a DJ deck's VOLUME ([dj.md](dj.md#volume)): a dB a click, SEL held 6, from +6 dB down to −24 dB, then OFF; a gain on the song's voices alone (their sends too), not on the keys', so the keys can play over a quieter song (the header: `K3 VOLUME -6 DB`). Heard at once; kept and dropped as the speed is, not stored |
| LFO | the one of K3 and K4 turned last back: the volume to 0 dB, the speed to 100 %, the song's own tempo (`K4 TEMPO 124.0 BPM +0.0%`); K4 if neither has turned |
| GLO | the view's options: SCROLL ROWS (the playing row stays in the middle) or PAGES (the highlight moves, the pages flip); while a song is held, UNLOAD SONG ([below](#glo-the-views-options)) |
| EDIT | the EDITOR on the playing pattern, with the cursor on the playing row; HOME tap brings you back |
| ENV | the song list ([../dialogs.md](../dialogs.md#the-song-list)): PLAY there plays the highlighted song from its start, as the current song, PLAYBACK following it; ENV again only closes the list |
| keys | play the current instrument over the song |
| ARP | the arpeggiator, as on every screen but the EDITOR and LOOPS ([../controls.md](../controls.md)) |
| HOME + white keys | mute tracks |
| PLAY | pauses and resumes what played last ([../transport.md](../transport.md)) |

The buttons it uses light dim: EDIT, GLO, SEQ, ENV, LFO, ARP (full while the arpeggiator is on)
([../leds.md](../leds.md)).

## Layout

The grid is the EDITOR's, in the same three views and geometry, with no cursor, no focus row and no
slot: every row has the view's row height, and the 2-track view shows each cell's first command
([editor.md](editor.md#the-grid)). The playing row sits on the deep blue band.

**Which rows show:**
- **PAGES:** the page holding the playing row (16, 12 or 8 rows, as the view).
- **ROWS:** the playing row in the middle: the rows from half a page above it; at the pattern's start
  the page starts at row `00`.

```
PLAYBACK 00/0B                   > | |||   header: S, 16 px
   00  01  02  03  04  05  06  07          track numbers: T, 12 px
00C-4 --- --- A-2 --- --- --- ---          the playing row: a deep blue band
01--- --- --- A-2 --- --- --- ---          rows: 12 px, S cropped
02--- --- C-4 A-3 --- --- --- ---
03--- --- --- A-2 --- --- --- ---
04C-4 --- --- E-3 --- --- --- ---
05--- --- --- --- --- --- --- ---
06--- --- C-4 A-2 --- --- --- ---
07--- --- --- C-3 --- --- --- ---
08C-4 --- --- A-2 --- --- --- ---
09--- --- --- A-3 --- --- --- ---
0A--- --- C-4 E-3 --- --- --- ---
0B--- --- --- A-2 --- --- --- ---
0CC-4 --- --- C-4 --- --- --- ---
0D--- --- --- A-3 --- --- --- ---
0E--- --- C-4 --- --- --- --- ---
0F--- --- --- E-3 --- --- --- ---
10:12 EMPTY FUSION          POS 00 ROW 00  footer: T, 12 px
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the grid | as the EDITOR's ([editor.md](editor.md#the-grid)) | | | the playing pattern; no cursor, no focus row, no slot's name |
| nothing to show | 0, 57 | 240 × 12 | T | `NOTHING HAS PLAYED: PLAY STARTS THE SONG` |

Colours: as the EDITOR's grid; the playing row's band deep blue; the empty screen's line dark grey.

### GLO: the view's options

```
+--------------------------------------+   a panel at 6, 21, 228 x 52 (38 with SCROLL alone)
| PLAYBACK VIEW                        |   the title, T at 10, 24
|[SCROLL                           ROWS]|   items, T, every 14 px from 40
| UNLOAD SONG                  PUNCH IN|   (while a song is held)
+--------------------------------------+
```

SELECT picks; K1 on SCROLL switches it (it is the setting PLAYBACK SCROLL, [settings.md](settings.md)):
ROWS keeps the playing row in the middle, PAGES moves the highlight and flips the pages.
**UNLOAD SONG** is there while something other than THE KEYS is held, playing or not (a song played since
power-on, the EDITOR's pattern on the current song's setup, the loops once played, DJ's decks), its value what
is held: the song's name, `THE LOOPS` or `DJ`. EDIT on it closes the panel and does SONGS' UNLOAD SONG
([songs.md](songs.md)): the sound cut at once, the keys back on THE KEYS' setup ([fx.md](fx.md#the-fx-setup)),
whatever was held. The panel's layout is the properties panel's ([../dialogs.md](../dialogs.md)).

Colours: the title cyan; names cyan, the selected one's white; SCROLL's value light grey (white when
selected); the two explanations dark grey.

## Texts

| Text | Where, when |
|---|---|
| `PLAYBACK` | the header, in the PLAY group's colour |
| `00/0B` | the header's page: the song position and the last one ([../frame.md](../frame.md)) |
| `PAT 0C` | the header's page while the EDITOR's pattern loop plays; `EDITOR` on the EDITOR's own pattern |
| `POS 00 ROW 00` | the footer's right: the play position |
| `PAT 0C ROW 1F` | the footer's right while the EDITOR's pattern loop plays; `ROW 1F` on the EDITOR's own pattern |
| `K1 POSITION 03/0B PATTERN 05` | in the header's place while K1 turns (1.2 s): the position it goes to, and its pattern |
| `NOTHING HAS PLAYED: PLAY STARTS THE SONG` | the body, with no song to show |
| `8 TRACKS`, `4 TRACKS`, `2 TRACKS`, `1 TRACK` | toasts: SEQ |
| `K2 VIEW 4 TRACKS` | in the header's place while K2 turns (`1 TRACK`, `2 TRACKS`, `4 TRACKS`, `8 TRACKS`) |
| `K4 TEMPO 124.0 BPM -3.2%` | in the header's place while K4 turns, and after LFO: the playback speed, the BPM at the song's set tempo and the change against it, in 0.1 % (DJ's words) |
| `K3 VOLUME -6 DB` | ... while K3 turns, and after LFO: the songs' volume (`0 DB`, `+2 DB`, `OFF`, DJ's VOLUME's words) |
| `PLAYBACK VIEW` | GLO's panel: `SCROLL` `ROWS` / `PAGES`; `UNLOAD SONG` and the song's name, `THE LOOPS` or `DJ` (its toast `SONG UNLOADED`, SONGS') |
| `SONGS` | ENV's song list's title ([../dialogs.md](../dialogs.md#the-song-list)) |

The grid's own texts (`---`, `===`, `..:..`, `<`, `>`) are the EDITOR's ([editor.md](editor.md#texts)).

## Help

HOME + LFO ([../help.md](../help.md)): two pages of its own, then the shared ones. The help draws the
first page's last line, `PRESS HOME TO GET BACK`, itself.

```
[PLAYBACK]: FOLLOW WHAT PLAYS
ON SCREEN: THE PATTERN PLAYING NOW
  NOTHING HERE CAN BE CHANGED
{PLAY}: PLAY / PAUSE WHAT PLAYED LAST
{SEQ} (SHOW ELSE, QUICK) OR {K2}:
  SEE 8, 4, 2 OR 1 TRACKS AT ONCE
{ALGORITHM}: MOVE ACROSS THE TRACKS
{SELECT}: PAUSE, THEN A ROW A CLICK
  {PLAY} GOES ON FROM THERE
{K1}: JUMP TO ANOTHER POSITION
{K3}: THE SONG'S VOLUME, NOT THE {KEYS}'
{K4}: THE SONG'S SPEED, IN BPM
{LFO} (LEAVE FIELD OPEN): {K3} OR {K4},
  THE ONE TURNED LAST: 0 DB, 100 %
{EDIT}: CHANGE IT IN [EDITOR]
```

```
MORE ON [PLAYBACK]
{EDIT}: OPENS ON THE ROW PLAYING,
  {HOME} COMES BACK HERE
{ENV} (EXPLORE NAMED VAULTS): SONGS
  {PLAY} THERE: PLAY ONE FROM ITS START
{GLO}: SCROLL BY ROWS OR BY PAGES
  UNLOAD SONG: ALL STOPS, AS AT POWER-ON
{ARP} (AGAIN, REPEAT, PLEASE): ARPEGGIO
  HOLD {ARP}: RHYTHM, ORDER, LATCH
SPEED, VOLUME: NOT SAVED, FOR ALL
  SONGS UNTIL SOMETHING ELSE PLAYS
HEADER: THE POSITION / THE LAST ONE
FOOTER: THE POSITION AND ROW PLAYING
```
