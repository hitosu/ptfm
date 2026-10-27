<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 Vasily Kazantsev -->
# The UI (`fw/core/ui/`)

The screens of `specs/ui/`, in C, on the platform's `plat.h` only. The playground (`../../../playground/`)
is the reference that matches the specs: each file here names the playground file it follows.

## Files

| File | What |
|---|---|
| `ui.h` | `ui_init`, `ui_poll`: what `app.c` calls |
| `ui_core.h` | **the API for screens**: the screen struct, overlays and dialogs, toasts, the grammar's state, the current things (song, instrument, octave, mutes), settings, formatting |
| `gfx.h`, `gfx.c` | drawing: palette roles and themes, the fonts T S B L X, text, runs, symbols; the half-screen strip and the changed rows to the panel |
| `ui.c` | the controls' grammar (taps, holds, grace, layers, HOME's combination), navigation, the transport's PLAY, the frame (header, footer), the LEDs, `ui_poll` |
| `ui_dlg.c` | menus with levels, questions, properties, text entry, progress, the help, HOME's panel, the held buttons' layers, the seeding screen |
| `ui_lists.c` | the song list (ENV on STATUS and PLAYBACK) |
| `ui_grid.h`, `ui_grid.c` | the pattern grid (8, 4, 2, 1 tracks) and commands in 3 characters: PLAYBACK uses it, the EDITOR builds on it |
| `ui_data.h`, `ui_data.c` | names and figures from the records (songs, patterns, cartridges, slots); `../play/rec.h` reads cells and more |
| `ui_set.c` | the settings: the table SETTINGS shows, the settings record, SAVE / REVERT |
| `ui_edit.c` | editing on the store's edit memory: MEMORY FOR EDITS IS FULL, running low, REVERT of one thing, an edit mode's copy (HOME drops what it changed), SAVE and REVERT ALL; no UNDO |
| `ui_tools.c` | SETTINGS' TOOLS: CHECK REFERENCES, CLEAN UP; the checklist question |
| `ui_fmt.c` | `ui_fmt` (a small printf), hex, decimal, `N/M`, notes, times |
| `txt.h`, `txt.c` | the fixed texts: `T("...")`, unpacking (`specs/ui/texts.md`) |
| `ui_cmds.h`, `ui_cmds.c` | the commands as the screens say them: in words, short names (the 1-track view), the list K2 turns through, each engine's parameters (`*`), the mixer (`=`), the FX (`#`, on `fxdef.h`'s effects) |
| `ed_pat.h`, `ed_pat.c` | editing a pattern in its packed form: a rewrite of the tracks it changes, cell by cell from the old streams (`ed_rewrite`, `ed_old`), the parts built in `ui_scratch` and put as one edit; the clipboard, kept packed |
| `ed_inst.h`, `ed_inst.c` | editing instruments: each engine's edit pages (groups of up to four parameters, where their bits lie), INIT, a slot or a cartridge record written into the edit memory |
| `ui_ed.h`, `ed_plist.c` | what the EDITOR and INSTRUMENTS share: the pattern list (ENV) with its menu and PLAY's preview, references updated (`ed_remap`), ROM's refusals |
| `screens.c` | every screen registered: **add a line here** for a new one |
| `scr_*.c` | the screens: STATUS, SONGS, PLAYBACK, SETTINGS so far |
| `scr_fx.c`, `fx_target.c`, `fxdef.c`, `fx_pics.c` | FX: LIVE, EDIT, MIXER; whose setup it edits (a song's, the loops', DJ's) and its mixer; the effects' knobs and the setup's bytes; EDIT's pictures |
| `fxm.c` | fixed-point maths for the pictures: sin, log2, exp2, dB, tanh, sqrt |
| `ui_punch.c` | the punch-ins played live (FX held + a white key, FX LIVE), ARP holding one on |
| `scr_visual.c` | VISUAL: the voices' scopes, the output's scope, wave, spectrum (an integer FFT), the tracks' levels |
| `scr_loops.c`, `lp_rec.c` | LOOPS: the loop station, its pattern recorder (its loops: `../play/loops.c`; RECORD LIVE's song: `../play/live.c`) |
| `scr_dj.c` | DJ: the decks, the crossfader, the list (the stand and the decks in the player: `../play/dj.c`) |

## Adding a screen

1. **`scr_<name>.c`** defines `const ui_screen_t SCR_<NAME>` with designated initializers (fields left
   out are NULL / 0): `.name` (`"EDITOR"`: the header's name, the help's key, the round's), `.group`
   (`G_MAKE`, `G_PLAY`, `G_HEAR`, `G_SYSTEM`), `.does` (`BIT(B_EDIT) | ...`: the buttons lit dim), and the
   hooks it needs: `draw` (the body, y 17-226, clipped), `knob(e, steps, coarse)`, `tap(b)`, `key(n, down)`,
   `back()` (HOME tap: 1 if it closed something of its own), `sub` / `page` / `seq` / `footer` (texts the
   frame draws; return a static buffer), `lit(b)` (an LED's level or `LV_NONE`), `key_leds(keys)`,
   `sel_lines` (SEL held's layer), `play_down` / `play_up` (a PLAY of its own), `rec`, `hold(b)`,
   `has_combo(held, b)` with `combo(held, b)` (button + button, either order), `hold_action(b)`, `can_play`,
   `tick` (every pass while on screen), `enter` / `leave`; for LOOPS and DJ: `mute_key` / `solo_key` /
   `mute_leds` / `home_area` (HOME + white keys on their own things), `foot_hints` (the footer's left as
   key hints), `presets` / `presets_cart` (PRESETS of their own), `no_keyboard`.
2. **Register it** in `screens.c` (an `extern` and a `ui_register` line). The black key, the round and
   POWER-ON SCREEN find it by its name.
3. **Its help** comes from its spec's Help part by itself (`fw/tools/gen_texts.py`).

The core calls `draw` twice a frame (once a half of the screen), about 30 frames a second: draw from
state, change nothing in `draw`. Hooks the struct lacks can be added at its end (additive only).

## Rules

- **Texts:** every fixed text is `T("...")` where it is drawn (one literal; `ui_txt_v("TRACK # MUTED",
  ui_hex2(t))` for a value in it). `T()` is a call: not in static initializers (a screen's `.name`, a
  table of names: plain strings there). A packed text's buffer lasts only a few texts: the dialogs copy
  what they keep.
- **Numbers** as `specs/ui/frame.md` writes them: `ui_hex2`, `ui_dec`, `ui_of`, `ui_page_of`, `ui_fmt`.
- **Colours** by role (`C_LGREY`, `C_DEEP + C_BLUE`, ...), never RGB; the engines' `ENG_COL[]`, the groups'
  `UI_GROUP_COL[]`.
- **The player:** ask through `../play/player.h` (requests, applied in the audio context); read with
  `player_state()`. **Records:** `../store/store.h` (`store_get`), `../play/rec.h`, `ui_data.h`.
- **Dialogs:** `ui_menu`, `ui_ask`, `ui_props`, `ui_text_entry`, `ui_progress`, `ui_song_list`; an
  overlay of your own: fill a `ui_overlay_t` (designated initializers) and `ui_open` it.
- **Editing:** a change is a whole new payload for a record: `ui_edit_put(kind, num, sub, ver, payload,
  len)` / `ui_edit_del`, kept in the store's edit memory (`store_edit_*`), so `store_get` and the player see
  it at once; `ui_edit_piece` changes one piece (a pattern's track or head), `ui_edit_reserve` makes room
  for an edit of several pieces first; `ui_edit_revert` / `ui_edit_revert_slot` are REVERT (the thing as the
  flash has it; there is no UNDO), `ui_edit_unsaved` says whether REVERT is listed; `ui_edit_session` /
  `ui_edit_session_drop` keep an edit mode's copy and put it back at HOME. A record changed back to what the
  flash holds is unsaved no more. SAVE, REVERT, the SAVE LED and MEMORY
  FOR EDITS IS FULL are the core's. A setting: `ui_settings_changed()` (the settings record as an edit).
- **Other dialogs:** `ui_checklist` (CLEAN UP's), `ui_pattern_list` (ENV: a plain first version),
  `ui_check_references`, `ui_clean_up`.

## Tests

`../../tests/run_ui.sh`: the UI built natively with a fake platform (`../../tests/ui_test.c`) and a
stand-in player (`../../tests/ui_player.c`), the demo disk laid into its flash, a script; screenshots in
`build/fw/shots/` (2x PNG), each checked for partial redraws == the whole frame, and compared with the
playground's of the same name (`build/playground/`, `tests/playground/screens.js`). Add your screen's
shots and checks to the script. Each source is compiled on its own and the test linked from an archive:
a file that does not compile yet is named and left out; what the sound's side gives the screens is
stood in for by `ui_player.c` (add what your screen calls there).
