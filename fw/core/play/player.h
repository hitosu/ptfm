/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The player: the transport (specs/ui/transport.md), the replay of songs and patterns tick by tick
 * (specs/files/patterns.md, commands.md, songs.md), the keys, the voice pool, the effects and the mix.
 *
 * The main loop only asks (player_* requests below: they are queued and return at once); the audio
 * context acts at the start of its next half (player_audio). What the screens show is read with
 * player_state() / player_voices_now(), a copy made by the audio context once a half. */
#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>
#include "rec.h"

#define PL_TRACKS     16        /* a pattern's tracks at most */
#define PL_VOICES     20        /* the voice pool's places: songs and the keys share them */
#define PL_VOICES_MOST 16       /* the most notes take at once: 10 on core 1 and 6 on core 2 */
#define PL_VOICES_C1  12        /* the most on one core (core 2 not taking the halves) */
#define PL_C2_FROM    10        /* at VOICES 16 core 1's places are the first 10, core 2's the next 6
                                 * (specs/second-core.md#the-voices-on-core-2) */
#define PL_NO_NOTE    0xFF

/* What plays: one thing at a time (specs/ui/transport.md#play). */
enum { PL_NONE, PL_SONG, PL_LOOP, PL_LOOPS, PL_DJ, PL_PREVIEW };    /* PL_LOOP: the EDITOR's pattern loop; PL_PREVIEW:
                                                                     * RECORD PATTERN's steps (a source) */

/* An instrument: cartridge 00-FF (F0-FF the built-in ones), slot in it. */
typedef struct { uint8_t cart, slot; } pl_inst_t;

void player_init(void);
void player_audio(int16_t *lr, int frames);     /* the audio context: frames stereo pairs */
uint8_t player_voices_sounding(void);           /* (the debug build: the voices now, from the audio context) */
typedef struct {                                /* (the debug build: core 2's voice jobs since the last call) */
    uint32_t jobs, us;                          /* the voice jobs handed over; their time summed (core 2's), us */
    uint32_t wait_sum, wait_max;                /* core 1's waits for them, us */
    uint8_t most;                               /* the most of core 2's places sounding at once */
} player_v2_t;
void player_v2_figures(player_v2_t *f);         /* (and a new window started) */
/* AUTO LEVEL's measurement (specs/files/instruments.md, LEVEL): instrument in's note alone, held 1 s of 1.2 s from
 * a clean start at unity gain; *ms the loudest 300 ms's mean square of (L + R) / 2 (16-bit scale), *peak either
 * side's. mem: its voices (size bytes, 8-aligned; a FUSION instrument's layers, a voice each, as many as fit).
 * The main loop's (~826 blocks of each voice, each with the interrupts held back by lock / unlock: the platform's
 * plat_lock / plat_unlock; ~4.5 KB of stack). 1 measured, 0 none */
int player_measure(pl_inst_t in, void *mem, uint32_t size, uint32_t (*lock)(void), void (*unlock)(uint32_t),
                   uint32_t *ms, int32_t *peak);
/* each block as it is made (its PL_BLOCK frames, 16-bit values in int32, from frame at): a platform that
 * passes no lr writes them out itself, so its buffer holds made sound only (a late half: silence, never a
 * half-written buffer) */
extern void (*player_out)(const int32_t *lr, int at, int n);   /* (lr 0: the block is silent) */
/* USB audio's tap (specs/connections.md#usb-audio): each block's mix after the effects (16-bit values in int32, n
 * frames), and the clicks in it (fx_clicks: 0 none), which USB leaves out */
extern void (*player_usb)(const int32_t *lr, const int16_t *click, int n);

/* ---- the transport ----
 * player_play_song: the song from a position and row (the UI passes where PLAY starts; a song other
 * than the last played rewinds the one left, as transport.md says: the UI asks with pos 0, row 0).
 * player_play_pattern: the EDITOR's loop, the pattern from a row, at the current song's tempo.
 * player_pause: what plays pauses and keeps its place. player_continue: what played last goes on from
 * its place (from its start if it has none). player_to_start: back to the start (if playing, it
 * restarts from there: HOME + PLAY); player_stop: pause, then back to the start (PLAY held). */
void player_play_song(uint8_t song, uint8_t pos, uint8_t row);
/* nothing played yet (PL_NONE): the player holds the song all the same, its FX setup in the effects and its tempo
 * in the music clock (the keys' effects, ARP, GATE, the DELAY's FOLLOW, FILTER's envelope ...), nothing started;
 * the main loop's, for the keys' song. Once anything plays it does nothing */
void player_cue(uint8_t song);
/* UNLOAD SONG: what played last let go (a song, the EDITOR's pattern, the loops, DJ's decks): the sound cut at once
 * (every voice, the effects' tails), nothing held (PL_NONE), as at power-on; DJ's stand and the loops themselves kept */
void player_unload(void);
void player_play_pattern(uint8_t pat, uint8_t row);
/* ... at a timing of its own (LOOPS' LOAD's preview: as LOAD would bring it), not the current song's */
void player_play_pattern_at(uint8_t pat, uint8_t row, uint16_t tempo, uint8_t ticks, uint8_t rpb, uint8_t swing);
void player_pause(void);
void player_continue(void);
void player_to_start(void);
void player_stop(void);
void player_skip(int positions);                /* a song playing: on to another position at the row's end */
void player_all_sound_off(void);                /* every voice lets go at once, the effects' tails cleared */
/* CUT (specs/ui/controls.md#cut): on, all sound off and the output faded to silence over 8 blocks (11.6 ms), then
 * every effect and the Juno-106 chorus emptied; while on, no note starts and no effect runs, only the clocks go
 * on (what plays keeps its time), the output silent (USB audio too); off, heard again from the next notes, faded
 * in over 8 blocks, the effects starting dry */
void player_hush(int on);
void player_mutes(uint32_t muted);              /* bit t: track t silent (not stored in a song) */
void player_voice_count(int n);                 /* 8, 12 or 16 voices for notes (SETTINGS' VOICES; PL_VOICES_MOST):
                                                 * more than PL_VOICES_C1 only while core 2 takes the halves */
void player_song_changed(uint8_t song);         /* the song's record changed (an edit): read it again */
void player_store_changed(void);                /* the store was mounted or saved: every record read again */
/* the store's watch (store_set_watch, set by player_init): an edit's piece written, moved or gone, told in the main
 * loop with the audio context held off; what plays reads it from its next row or note */
void player_store_watch(uint8_t kind, uint8_t num, uint16_t id);
void player_punch(int k, int down);             /* a punch-in (FX held + white key k, 0-15) pressed / let go */
/* the EDITOR's realtime take (specs/ui/screens/editor.md#realtime-takes), its keys heard exactly as written: it wrote
 * track `track`'s cell at row `row` of the pattern loop. at 0: at once, its row started already: the cell's note sounds
 * now on the track as the replay would start it (its note delay EDx not waited for: it is played now), or with off the
 * track's note lets go now (a note off's, a key off's). Else the row's count (player_state().rows_played's there): the
 * replay plays it from the pattern when it gets there, or now if that row started before the cell was written */
void player_take_cell(int track, int row, uint32_t at, int off);

/* ---- the FX screen ----
 * player_fx_cmd: an FX command (0x90-0xAF: a knob turned on FX, 0xAE / 0xAF an effect on / off) as a song's
 * would be, taking over until the song's next command for that knob. player_fx_reload: the setup that plays
 * read again from its record (ROUTING, the chain's order, RESET, UNDO), the commands' changes dropped.
 * player_fx_bypass: BYPASS held: the chain and DUCK off (the METRONOME clicks on). */
void player_fx_cmd(uint8_t code, uint8_t value);
void player_fx_reload(void);
void player_fx_bypass(int on);
/* the count-in (specs/ui/transport.md#count-in): bars (1-2) of 4 beats at bpm10, each beat the count-in's click,
 * timed by the audio; the next start posted (player_play_pattern, player_play_source) waits for its end and starts
 * on the beat. player_count_stop: cancelled, what waited dropped. player_state: count_beats, count_beat */
void player_count_in(int bars, int bpm10);
void player_count_stop(void);
void player_click(int accent);                  /* a count-in's beat: BEEP at 60 %, the bar's first higher
                                                 * (specs/ui/transport.md#count-in), METRONOME on or off */

/* ---- what the sound costs: the time the audio context spent, read with plat_us() around each voice and each
 * effect (the effects' only while fx_timed: the FX screen), since the last reset; read by nothing now (STATUS and the
 * FX screen's footer have their own: player_fx_cost, fx_spent_us) ---- */
typedef struct {
    uint32_t eng_us[4];         /* DX7, Juno-106, TB-303, sampler: their voices' time, us */
    uint32_t eng_blocks[4];     /* voice blocks rendered (64 frames each, 1,451 us of sound) */
    uint32_t fx_us[11];         /* each effect's time by its number (FX_GATE ... FX_METRONOME), 10: the punch-ins */
    uint32_t blocks;            /* blocks of 64 frames played */
} player_costs_t;
void player_costs(player_costs_t *c, int reset);
/* STATUS PERFORMANCE's EFFECTS (specs/ui/screens/status.md): the whole effects stage's time (us), on whichever core,
 * and the audio halves measured, since the last call; each call keeps it measured ~0.2 s more (main loop) */
void player_fx_cost(uint32_t *us, uint32_t *halves);

/* ---- VISUAL ---- */
int player_master(int16_t *lr, int n);          /* the last n (<= 128) stereo frames out, interleaved */
void player_levels(uint16_t *lv);               /* each track's peak since the last call, 0-32767 (16) */

/* ---- another source of what plays (LOOPS; later a DJ deck) ----
 * Its "song" (TEMPO, TICKS A ROW, ROWS A BEAT, SWING, the flags, the global volume, its order, every track's
 * mixer, its 64-B FX setup) and its patterns (rec_pat_t: each track's stream, as a pattern record's) come from
 * these instead of the store. player_play_source plays it from (pos, row), looping its order as the pattern
 * loop does; player_state().what says `what`; PAUSE, PLAY, HOME + PLAY and STOP work on it as on any; a song
 * or a pattern started drops it. player_source_changed: its cells changed (an edit, an erase): what is read is
 * read again from the next row on; rows_changed: its length too (the replay is built again at the row it is
 * on). The audio context reads what the pointers give from its next half on: a buffer given before must stay
 * as it was until player_state().halves has gone up by 2 after the call. */
typedef struct {
    int (*song)(rec_song_t *s);                     /* 0 read */
    int (*pattern)(uint8_t num, rec_pat_t *pt);     /* 0 read */
    int (*track_inst)(int track, pl_inst_t *in);    /* 1: the track's sound (a note without an instrument plays
                                                     * it), 0 none; asked at the start and at every change */
    uint8_t lanes;                                  /* 1: LOOPS' own (play/loops.c): its pattern holds no cells, the
                                                     * replay keeps the time alone and the loops' lanes play the notes
                                                     * (below) */
} pl_source_t;
void player_play_source(uint8_t what, const pl_source_t *src, uint8_t pos, uint8_t row);
/* RECORD LIVE: the loops' source from row 0, the loops silent from its first row (player_lp_rest's 1, their place
 * `place`): one request, so no row of theirs sounds first */
void player_play_source_silent(const pl_source_t *src, uint8_t place);
void player_source_changed(int rows_changed);
/* a source's play ended (RECORD PATTERN's preview): paused, dropped, and what played before it (`what`, paused where
 * it was) is what PLAY continues */
void player_source_end(uint8_t what);
/* LOOPS shown, nothing playing: the player holds the loops all the same (the source src, nothing started), their FX
 * setup in the effects and their time in the music clock, so the keys play through what LOOPS' FX page shows at the
 * loops' tempo; player_state().what says PL_LOOPS, .lcue 1. src 0: LOOPS left, what was held before held again (a
 * paused song keeps its place). Once anything plays the hold is over (src 0 then does nothing) */
void player_cue_loops(const pl_source_t *src);

/* ---- LOOPS' lanes (specs/files/loops.md; specs/ui/screens/loops.md) ----
 * A lanes source plays on the second replay, which keeps the time alone: the length's rows go round, loops or none
 * (the METRONOME, the beats). Loop i is its track i (the voices' track 16 + i: its instrument, mix, mute, DUCK), and its
 * notes are its stream's (rec_loops: lane l of its base's row r at the stream's row 4r + l), read as the loop is heard:
 * at the loops' row r it plays its row (r - SHIFT) round the length, while its repeats last ((r - SHIFT) / base <
 * repeats, at base row (r - SHIFT) % base). A lane holds one note, as a key does: its next note, note off or key off
 * (Kxx) ends it, a note delay (EDx) starts it late, a MONO loop's glide (3xx: xx / 16 semitone a tick, the voice going
 * on; a TB-303's SLD: its slide) takes it to the next. A note rings across repeats and the loops' wrap, and stops where
 * its loop goes to rest, at a cut repeat's end (the loop's own row 0, where its first repeat begins again) and where a
 * jump lands in its rest. The view of the loops is read again from the store at their start and whenever an edit changes
 * them (the store's watch).
 * Their passes (player_state().lp_pass) count from 0 at their start: a row at or before the one played last begins the
 * next (their wrap, a jump forward past the length's end, the rest's own jumps), but a jump back (player_lp_jump).
 * Mutes and solos: while any loop is soloed only the soloed ones sound, else the muted ones are silent; _at_bar from the
 * next row where row % bar_rows == 0; a change now drops one waiting for the bar. */
void player_lp_mutes(uint32_t muted, uint32_t soloed);
void player_lp_mutes_at_bar(uint32_t muted, uint32_t soloed, int bar_rows);
/* A note on loop's lane, heard as it is written: at row `row` of the loops (when the replay starts it), or at once (row
 * < 0, a row already started, or one more than half the length away: past), its `delay` ticks (EDx) counted from then;
 * vol 00-7F (0xFF: the instrument's own); glide: a 3xx's xx (0 none; on a TB-303 any: a slide), or PL_LP_LEG: a legato
 * note (LEG). player_lp_off: the lane's note ends, the same way. A queue of 16 rows ahead; dropped when the loops stop;
 * a jump forward drops those for the rows it passes over (a jump back keeps them for when their rows come).
 * loop: 0-15 a loop's lanes, PL_LP_STAGE_KEYS the keys on the stage (below) */
#define PL_LP_LEG 0x100u
void player_lp_note(int loop, int lane, uint8_t note, uint8_t vol, int row, int delay, unsigned glide);
void player_lp_off(int loop, int lane, int row);
/* RECORD LIVE's tap (specs/ui/screens/loops.md#record-live): where notes enter the voices, an event in a ring for each
 * note that sounds and ends, whatever plays it: a lane's (the loops, the preview, the stage, its keys, the stage-off keys),
 * a key's on another screen, the pattern recorder's single notes. Only notes with a voice among the VOICES count: one given
 * none is not told; one losing it, taken or parked past the count, is told PT_LOST; a parked note coming back tells
 * nothing. A muted loop's notes are not heard, so not told; a change of the loops heard comes as PT_MUTE. An event per
 * note, never per frame; the audio context writes, the main loop reads. t: the low bits of the loops' replay's tick
 * (player_state().lp_ticks); note: 12-127 a note, else PT_*; fl: LF-style bits PTF_*, glide: its 3xx's xx. A lane holds
 * one note; PT_KEY's lane: a key's, told apart by its note. A ring too full loses the event (counted: player_tap_lost) */
enum { PT_OFF, PT_LOST, PT_MUTE, PT_MOVE };     /* (a note's lane's: ends; its voice lost; the loops silent now are cart |
                                                 * slot << 8; lane's note goes on on lane vol, its source ch) */
enum { PTF_ACC = 1, PTF_SLD = 2, PTF_LEG = 4, PTF_SLID = 8 };   /* its ACC, its SLD, its LEG, come by a TB-303's slide */
#define PT_KEY 96                               /* lane: the keys (a key's note, its key in glide), on any screen */
#define PT_HEAR 97                              /* lane: the pattern recorder's single notes */
#define PT_SKEYS 72                             /* lane: the stage's keys' first (4 x PL_LP_STAGE_KEYS) */
#define PT_KEYCH 16                             /* ch: the keys' own (else a replay's channel) */
typedef struct { uint16_t t; uint8_t lane, note, vol, fl, glide, ch, cart, slot; } pl_tap_t;
void player_tap(pl_tap_t *ring, uint32_t n);    /* on, a ring of n (a power of 2) entries; ring NULL: off */
int player_tap_peek(pl_tap_t *e);               /* the oldest event: 0 none */
void player_tap_next(void);                     /* ... gone */
uint32_t player_tap_lost(void);                 /* notes the ring had no room for, since it went on (an event that is no note is lost uncounted) */
uint32_t player_tap_held(void);                 /* events waiting in the ring */

/* ---- LOOPS' stage (specs/ui/screens/loops.md#the-stage; specs/files/ram.md#the-stage-in-the-edit-memory) ----
 * The stage is a loop more, belonging to no loop: its stream the store's (store_stage, read again whenever the store's
 * watch tells of it: player_store_watch; a stage dropped stops what its lanes hold, unless player_lp_stage_end(loop)
 * came first), its base the length, played once, SHIFT 0, in the keys' instrument and mode (player_lp_stage), on its 4
 * lanes. Its voices sound through the mix of the loop REC would put it in (`track`: the selected loop's place, on + NEW
 * LOOP loops_n(), the next loop's mix) and never a mute or a solo: they are that track's (its LEVEL, PAN, DUCK and sends,
 * a DUCK source and FILTER's envelope's as it), but the mute check passes them by; so do the preview's and
 * player_hear's. A new track takes the sounding notes with it at once.
 * - MONO: a stage of more than one lane plays on one voice (its lane 0): a note on any lane takes over (the later lane's
 *   of one row), an end counts only on the lane whose note sounds, as REC writes it (loops_stage_write): the merge
 *   counted from the stage's row 0, so a jump or an edit gives the same as playing it through. Switched to MONO, the
 *   notes of its lanes 1-3 end at its next row; POLY again, its lanes play their own from the next row.
 * - The keys on the stage (player_lp_note(PL_LP_STAGE_KEYS, lane, ...)): 4 lanes of their own, each note asked for at
 *   its landing row (ahead) or at once, with its note delay, glide, LEG or a TB-303's slide, in the stage's instrument,
 *   through the stage's column. A note asked for at a row ahead stands in for the stage's cell at that row and lane, that
 *   once (the stage's lane does not start it then: in MONO, the cell its merge takes), so a note is heard once as it is
 *   played, then from the stage each time the loops reach its row.
 * - The keys while the stage is off (player_lp_note(PL_LP_STAGE_KEYS, 4 + lane, ...), lane 0 to PL_LP_OFF_LANES - 1):
 *   16 lanes more, each note at once, in the stage's instrument, through the stage's column, as the keys on it are, but
 *   standing in for nothing; the screen gives each POLY key a lane (as many as the pool's 16 voices can sound), MONO one,
 *   with its glide, LEG or slide. The stage's rest does not touch them; the loops' stop and the preview alone let them
 *   go.
 * - The lanes held (player_lp_hold): from their row the stage's cells on those lanes are not read and their notes end
 *   (the row's first, before anything of it plays), until they are free again; the other lanes and the rows before play
 *   on. A jump takes the held lanes to where the loops land (OVERWRITE goes on from there); a start frees them.
 * - The selected loop told silent (`silent`): its lanes are not read, its notes end at once; heard again from the next
 *   row once it is not.
 * - Its rest (`rest`, while the stage is switched off): the stage is not read, its notes end at once, no loop is silent;
 *   from the next row once it is over.
 * - The preview alone silences it with the loops. */
#define PL_LP_STAGE_KEYS 18
#define PL_LP_OFF_LANES 16              /* the stage-off keys' lanes (PL_LP_STAGE_KEYS' 4 to 19) */
typedef struct {
    pl_inst_t inst;             /* the keys' instrument: the stage's sound, its keys' too */
    uint8_t mono;               /* the keys' mode: 1 MONO (one voice), 0 POLY */
    uint8_t track;              /* the mix it sounds through: the selected loop's place, or loops_n() (+ NEW LOOP) */
    uint8_t silent;             /* the loop the stage stands for, silent (the selected one while the stage holds notes);
                                 * 0xFF none */
    uint8_t rest;               /* 1: the stage resting (switched off) */
} pl_lp_stage_t;
void player_lp_stage(const pl_lp_stage_t *s);   /* (copied; taken at the next half) */
/* the stage's lanes `lanes` (bits 0-3) held from row `row` of the loops' pass `pass` (player_state().lp_pass; a row
 * already begun: at once), as the screen tells each change: POLY's key down all four, MONO's lane 0, OVERWRITE all four
 * from the line; each lane its earliest held place. row < 0: those lanes free again (read from the next row) */
void player_lp_hold(unsigned lanes, int pass, int row);
/* ALGORITHM while the loops play (specs/ui/screens/loops.md#the-play-position-play-and-jumps): the replay's next row
 * moved `rows` rows round the length (asked again before it lands: from where it was going), landing as the row ends.
 * No line: the notes sounding ring on (each until its lane's next cell), a loop landing in its rest falls silent there,
 * the stage plays what it holds from there. A jump back keeps the loops' pass, one forward past the length's end counts
 * the next. player_state(): lp_jumps counts the landings, lp_land_row the last one's row (its pass below). Each landing
 * counts as a switch of the rest's too (lp_sw ..., the loops' row from there); while RECORD LIVE's rest keeps them silent
 * it moves their place (lp_rest_row) instead, and while one of its own jumps is under way it does nothing */
void player_lp_jump(int rows);
/* REC sent the stage (specs/ui/screens/loops.md#rec-edit-and-the-arm): loop >= 0 (the loop loops_stage_write wrote, the
 * stage's track): what of the stage sounds goes on as that loop's, without a break (the loop's own notes let go first;
 * its mute applies again); < 0: what of the stage sounds lets go at once (EDIT, the stage's clear, CLEAR LOOPS). Call it
 * once the loop is written and before the stage is dropped (store_stage_drop): a drop told before the request is taken
 * keeps the stage's lanes for it. Once a send, and not again before the request is taken (the audio takes it within a
 * half; REC's 500 ms of nothing after a job keeps a second send that far off): a second call before then would leave
 * a drop between the two keeping lanes that nothing moves */
void player_lp_stage_end(int loop);
/* The pattern recorder's preview (specs/ui/screens/loops.md#the-pattern-recorder-arp), a loop more: with the loops
 * playing it plays in time with them, its steps from their start, a grid row each, repeated over their length (the last
 * repeat cut there); alone (player_lp_preview_alone(1)) the replay runs for it with the loops and the stage silent. Each
 * row a lane (the rows with a step on, in order): a step on starts its note, held NOTE LENGTH's rows plus its ties' steps
 * (a tie: a step held from the one before), ended there by a note off unless the row's next note comes first (the
 * pattern round: the next repeat's first); note_rows 0, TO THE NEXT: no note off. Its voices are track `track`'s (its
 * mix, the source's song's: the next loop's at loops_n()), on the stage's column (no mute or solo silences them), their
 * lanes its own. (Copied; taken at the next half) */
typedef struct {
    uint16_t steps[4];          /* each lane's steps switched on: bit k step k */
    uint16_t ties[4];           /* ... and its ties: bit k step k held from the step before */
    uint8_t note[4];            /* each lane's note, MIDI */
    uint8_t length;             /* LENGTH: its steps, 1-16 */
    uint8_t note_rows;          /* NOTE LENGTH in rows (0: TO THE NEXT) */
    uint8_t mute;               /* its sound off (ARP while the loops play): nothing starts, what sounds stops */
    uint8_t vol;                /* its notes' volume, 00-7F (KEYS VOLUME) */
    uint8_t track;              /* its track: the loop it was opened on, else the next loop's place (loops_n()) */
    pl_inst_t inst;             /* its instrument */
} pl_lp_grid_t;
void player_lp_preview(const pl_lp_grid_t *g);  /* NULL: off (alone: the replay stops, the loops' place kept) */
/* ... a note of it heard on its own (K3, OCT, SEL + a key, a step switched on): instrument inst at vol (00-7F) on the
 * loops' track `track` (the voices' 16 + track), through its mix as the preview's notes are, on the stage's column, the
 * one heard before let go; note 0: that one let go. (DJ's decks held, the right deck on those tracks: a key's note) */
void player_hear(uint8_t note, pl_inst_t inst, uint8_t vol, int track);
/* 1: the preview alone, the loops silent, their place kept: if they play they pause and the replay runs on from where
 * it is (PLAY pausing them), else it runs from row 0 (ARP starting it); player_continue then plays the loops from their
 * place, the preview with them. 0: it stops (the replay paused, the loops' place as kept) */
void player_lp_preview_alone(int on);
/* REC put the preview onto the stage (its stream given before, store_stage_put): the preview goes, its sounding notes
 * going on as the stage's lanes', without a break (the stage's own let go first); alone, the loops play on from the
 * replay's place (theirs dropped). With no stage, its notes let go (alone, the loops play on all the same) */
void player_lp_preview_keep(void);
/* RECORD LIVE's rest (specs/ui/screens/loops.md#record-live): the loops' replay runs on while the loops are silent.
 * 1: silent from the next row, their place kept (a replay just started, loops_play: from its first row); 0: on again
 * from their place, on the next row; 2: to their start (playing: from the top on the next row; silent or paused:
 * waiting there). Each switch's row in player_state() (lp_sw a count, lp_sw_rows the rows_played it took effect at,
 * lp_sw_row the loops' row from there, 0xFF silent); a pause or another start ends the rest (paused: at the loops'
 * place) */
void player_lp_rest(int how);
/* the tempo's limits (specs/ui/screens/dj.md#tempo-and-sync), a song's TEMPO's: a tick at TEMPO 3 at the longest
 * (0.83 s: Q16 frames hold it with SWING 75's long rows) to TEMPO 600 at the shortest (4.2 ms), 3 to 600 BPM at 6
 * ticks of 4 rows a beat; every tempo a replay plays is held within them. The shares that reach them from any song's
 * own: 0.005 (600 to 3) to 200 (3 to 600), Q16 */
#define PL_TEMPO_MIN REC_TEMPO_MIN
#define PL_TEMPO_MAX REC_TEMPO_MAX
#define PL_SHARE_MIN 327u
#define PL_SHARE_MAX 13107200u
/* PLAYBACK's K4, a playback speed (specs/ui/screens/playback.md): the songs at a share of their tempo, Q16
 * (PL_SHARE_MIN to _MAX; the screen holds it to 3-600 BPM), as a DJ deck's: every tempo a song plays scaled (its set
 * tempo, its tempo commands), heard at once; kept through
 * pauses, jumps, starts and other songs, 1.0 again when anything but a song starts (a pattern, the loops, DJ). Not
 * stored. (DJ's decks: player_deck_tempo) */
void player_tempo_share(uint32_t q16);
/* the FX page's effect under the cursor (specs/ui/screens/fx.md#the-fx-setup): the hand's alone. Its knobs' commands
 * (`codes`, bit c - 0x90) and its FX ON / OFF (e, the effect's number) are skipped, from songs and DJ's FX LFO alike,
 * and a song's start keeps it as it plays; 0xFF: none (punch-ins play on: they are events, not settings) */
void player_fx_mine(uint8_t e, uint64_t codes);
/* what the screen edits now (specs/ui/screens/editor.md: commands never change what you are editing), one thing:
 * PL_ED_MIX the tracks' mixers in `key` (bit track, 0-31: the song's 0-15, the loops' 16-31) skip their `=` commands;
 * PL_ED_INST the instrument `key` (cart << 8 | slot) skips its `*` commands and plays as edited, without the
 * parameters commands gave its tracks; PL_ED_TIME the song `key` (PL_ED_LOOPS: the loops) plays at its TEMPO, TICKS
 * A ROW, ROWS A BEAT and SWING as they change, its Fxx skipped */
enum { PL_ED_NONE, PL_ED_MIX, PL_ED_INST, PL_ED_TIME };
#define PL_ED_LOOPS 0x100u
void player_editing(uint8_t what, uint32_t key);
/* PLAYBACK's K3: the songs' volume, dB on each one's own level (-24 to +6, -25 OFF), their voices alone (the keys' as
 * they are), at once; kept and dropped as the speed is. Not stored */
void player_song_volume(int db);
/* SETTINGS' KEYS GAIN (specs/ui/screens/settings.md): dB on every note the keys start, MIDI's notes as the keys too
 * (-24 to +12), at once; the songs', the loops' and the decks' notes as they are */
void player_keys_gain(int db);
/* SETTINGS' KEYS (specs/ui/screens/fx.md#the-keys-dry): 1 DRY, the keys as heard (their notes, MIDI's, the
 * arpeggiator's, the previews) past every effect, the LIMITER and the punch-ins too, into the mix just before the
 * output stage, a Juno-106 patch's chorus on lines of their own; 0 THROUGH FX. From the next block */
void player_keys_dry(int on);
/* FX MIXER's KEYS column (specs/ui/screens/fx.md#mixer): the keys' strip as a track's mixer bytes (RM_*: PAN, DUCK
 * and the three sends; LEVEL is KEYS GAIN's, not read), one for every setup. PAN live; DUCK and the sends while the
 * keys go through the effects (not DRY), the sends in SENDS routing. From the next block */
void player_keys_mix(const uint8_t *mix);
int32_t player_duck_env(void);          /* DUCK's level now, Q15 (FX EDIT's meter), the song's replay's */
/* SONGS' play mode (specs/ui/screens/songs.md#the-play-modes): how a song ends while songs play, whatever its AT THE
 * END says: ONCE it stops, REPEAT it plays again (a looping one its own loop, one that stops from its start), NEXT
 * song `next` from its start at once (0xFF: none, it stops); OWN: as its AT THE END. Back to OWN when anything but a
 * song starts (a pattern, the loops, DJ). The main loop keeps `next` up as each song starts (player_state().end_next) */
enum { PL_END_OWN, PL_END_ONCE, PL_END_REPEAT, PL_END_NEXT };
void player_end_mode(uint8_t mode, uint8_t next);

/* ---- DJ's decks (specs/ui/screens/dj.md; play/dj.c drives them) ----
 * The main deck (0) is the song's replay, tracks 0-15; the other (1) is LOOPS' replay, tracks 16-31: the
 * decks play together, each its own song, place, tempo, mutes, its song's own GATE, FILTER, DRIVE, LO-FI and
 * DUCK, and a DJ FILTER; then VOLUME and the crossfader on its sound and its sends alike; the shared CHORUS,
 * DELAY and REVERB, the punch-ins and the LIMITER with DJ's FX setup. player_dj(1) makes the decks what plays
 * (PL_DJ); a song, a pattern or the loops started end it. A deck's state: player_state().deck[d]. */
enum { PL_LFO_FADER, PL_LFO_FILT_L, PL_LFO_FILT_R, PL_LFO_FX, PL_NLFO };
typedef struct {                /* an LFO's settings (specs/files/dj.md, An LFO's setup) */
    uint8_t on;
    uint8_t gen;                /* its starts, counted: another one sets off afresh, on the lead deck's next row */
    uint8_t shape;              /* 0 SINE, 1 SQUARE, 2 TRIANGLE, 3 SAW UP, 4 SAW DOWN, 5 RANDOM */
    uint8_t range;              /* the crossfader: 0 L TO HERE, 1 L TO R, 2 SWING, 3 MIRROR; a filter: 0 OFF TO HERE,
                                 * 1 OFF TO FULL, 2 HERE TO FULL, 3 SWING; the FX LFO: 0 HERE TO 100 %, 1 0 TO 100 % */
    uint8_t rev;                /* the crossfader's reversed */
    uint8_t len;                /* its speed, a note length of the lead deck's tempo: 0 32 BARS, 1 16 BARS, 2 8 BARS,
                                 * 3 4 BARS, 4 2 BARS, 5 1 BAR, 6 1/2, 7 1/4, 8 1/8 (10 Hz at most) */
} pl_lfo_t;
typedef struct {                /* DJ's mix as it stands: the main loop sets it whole (player_dj_set) */
    uint8_t xf;                 /* the crossfader as set: 0 (left) - 32 */
    uint8_t sharp;              /* its law: 0 SMOOTH, 1 SHARP */
    uint8_t reso;               /* the DJ FILTERs' resonance, 0-100 */
    uint8_t sends_filtered;     /* FILTER THE SENDS */
    uint8_t sync;               /* the other deck's beats kept on the main's */
    uint8_t sync_mult;          /* its tempo the main's (0), its half (1) or its double (2) */
    int8_t vol[2];              /* VOLUME, dB, -24 to 6; -25 OFF */
    int8_t filt[2];             /* the DJ FILTER's setting, -64 (LP's end) to 63 (HP's), 0 OFF */
    uint8_t filt_on[2];         /* its FILTER key has it on */
    uint8_t filt_slide[2];      /* its FILTER key held: sliding to on / off over 0.5 s at most (let go: there) */
    int8_t key[2];              /* KEY, semitones, -6 to 6 */
    uint8_t lfo_fx;             /* the FX LFO's effect: 0 REVERB, 1 DELAY, 2 CHORUS */
    uint8_t mix_set[3];         /* the shared CHORUS', DELAY's, REVERB's MIX as set on FX, % */
    pl_lfo_t lfo[PL_NLFO];
    uint8_t swaps;              /* the SWAPs the stand has made (player_dj_swap's count): a mix set before a SWAP and
                                 * taken after it (or the other way) is taken for the decks' sides as they are */
} player_dj_t;
/* SYNC's multiple (specs/ui/screens/dj.md#tempo-and-sync): base10 (BPM x 10) at x1 (m 0), x1/2 (1) or x2 (2), the half
 * to the nearest 0.1 BPM (a half up); and the multiple whose BPM is nearest own10 (a tie: the first). The screen, the
 * stand and the player pick it alike */
static inline uint32_t pl_mult_bpm10(uint32_t base10, uint32_t m)
{
    return m == 1u ? (base10 + 1u) / 2u : m == 2u ? base10 * 2u : base10;
}
static inline uint32_t pl_nearest_mult(uint32_t base10, uint32_t own10)
{
    uint32_t m, best = 0, bd = ~0u;
    for (m = 0; m < 3u; m++) {
        uint32_t v = pl_mult_bpm10(base10, m), d = v > own10 ? v - own10 : own10 - v;
        if (d < bd) {
            bd = d;
            best = m;
        }
    }
    return best;
}
void player_dj(int on, const uint8_t *fx_setup);    /* DJ's FX setup, 64 B, the caller's while DJ plays */
/* The second deck's own memory (its IBXM tables, its Juno-106 chorus, its mix: player_dj_mem_size bytes), lent
 * by the caller while DJ plays (specs/files/ram.md: from the ROM cache's part); without it the second deck stays
 * empty. NULL takes it back; the player gives it up by itself when DJ ends: player_state().dj_mem says whether
 * it still holds it (the caller frees it once that is 0) */
uint32_t player_dj_mem_size(void);
uint32_t player_ram(void);              /* the bytes of its voices, mixes and scopes in RAM (STATUS' RAM MAP: SOUND) */
void player_dj_mem(void *mem);
void player_dj_set(const player_dj_t *m);           /* (copied; taken at the next half) */
void player_deck_hand(int d, uint8_t pos, uint8_t row, uint32_t ms);   /* HOLD's hand (specs/ui/screens/dj.md#hold):
                                         * the deck's row at (pos, row) heard as if it came next, its ticks spread
                                         * over ms (the time since the hand's last click, 20-400), then held, its notes
                                         * ringing, as a step; the deck reads as not playing meanwhile */
void player_dj_swap(uint8_t swaps);     /* SWAP (specs/ui/screens/dj.md#the-main-and-swap): the decks exchange sides as
                                         * they play, nothing reloaded, nothing heard changing; swaps: the stand's
                                         * count of SWAPs with this one (only its parity counts: a lost request is
                                         * mended by the next). The deck requests after it are by the new sides */
void player_deck_load(int d, uint8_t song);         /* the deck paused at the song's start (0xFF: empty) */
void player_deck_play(int d, int on, uint8_t pos, uint8_t row);  /* on: from (pos, row) now; off: paused there */
void player_deck_arm(int d, uint8_t pos, uint8_t row);   /* ready at (pos, row); it starts on the other deck's next
                                                 * bar line while that one plays, else at once */
void player_deck_place(int d, uint8_t pos, uint8_t row); /* playing: on from there at once; paused: there */
void player_deck_tempo(int d, uint32_t share_q16, uint16_t fixed_bpm10);   /* its song's tempo x share (Q16, as
                                                 * player_tempo_share's), its tempo commands scaled; fixed (SYNC): that
                                                 * BPM x 10 */
void player_deck_mutes(int d, uint32_t muted);
enum { PL_STOP_NONE, PL_STOP_BRAKE, PL_STOP_BACKSPIN, PL_STOP_ECHO, PL_STOP_REVERB };
void player_deck_stop(int d, int kind, int down, int stay);   /* a stop's pad down (kind) / up; let go with stay:
                                                 * it stays stopped (paused where the stop left it) */
enum { PL_DLOOP_NONE, PL_DLOOP_PATTERN, PL_DLOOP_ROWS };
void player_deck_loop(int d, int kind, uint8_t pos, uint8_t row, uint16_t rows, int roll);   /* PATTERN: the deck's
                                                 * position over and over; ROWS: rows from (pos, row) round and round;
                                                 * roll: a ROLL, held (player_deck_roll_end lets it go) */
void player_deck_roll_end(int d);               /* a ROLL let go: the loop off, the deck on from where it is in it */
/* What waits for a playing deck's next bar line or beat (specs/ui/screens/dj.md#loading, #the-pads), landed there in the
 * audio context: only on a row the deck plays into (never the row a jump lands on, nor BACKSPIN's rows back), never
 * through a stop (the first one after the deck plays on); a pause (HOLD, PLAY), another LOAD, its song's end drop it.
 * player_deck_load_at: a LOAD at the bar line: there the song playing stops and `song` plays on from its start, at
 * that tempo (player_deck_tempo's), its mutes, loop and KEY cleared, a JUMP or MOVE waiting dropped; a song that stops
 * has its last bar line at its end. Asked again, it is the one waiting (a tempo changed meanwhile); 0xFF: cancelled.
 * player_deck_jump_at: on the deck's next beat a JUMP to (pos, row), or (move) a MOVE into the bar at (pos, row), on
 * the beat the deck plays in its own; a loop goes along (bars: a REPEAT's bars, on their grid); player_deck_place
 * drops it. player_state().deck[d]: land says what waits, loads and jumps count the landings */
void player_deck_load_at(int d, uint8_t song, uint32_t share_q16, uint16_t fixed_bpm10);
void player_deck_jump_at(int d, uint8_t pos, uint8_t row, int move, int bars);

/* ---- stepping (PLAYBACK's SELECT: specs/ui/screens/playback.md) ----
 * The first step, either way, pauses what plays (or takes what is paused at its place) and plays its current
 * row again; then each step forward plays the next row, back the previous one, as if it came next. The row
 * holds meanwhile (its notes ring, its tick effects go on); player_continue plays on from there. */
void player_step(int rows);                     /* +1 / -1 (0: just the current row again) */

/* ---- the arpeggiator (specs/ui/controls.md, ARP; SETTINGS' ARP RHYTHM, PATTERN, LATCH) ----
 * On, the keys (player_key_on / off) feed it instead of sounding; the keys held as it comes on are its notes;
 * they take turns in its pattern, a note a '#' of its rhythm (a row a step, at the tempo of what plays or
 * played last), each the instrument of the last key, or the one player_arp_inst gave since. Latch 1-8: notes keep
 * playing once their keys are let go, that many at most (the oldest goes), and a key pressed with none held starts
 * a new set; 0 (OFF): a note stops with its key. player_arp again while on changes the settings. */
enum { PL_ARP_UP, PL_ARP_DOWN, PL_ARP_UPDOWN, PL_ARP_RANDOM, PL_ARP_PLAYED };
void player_arp(int on, uint8_t rhythm, uint8_t pattern, uint8_t latch);   /* rhythm: ARP RHYTHM's index 0-9 */
void player_arp_clear(void);                    /* ARP held + LFO: the latched notes let go */
void player_arp_start(uint8_t note, pl_inst_t inst, uint8_t vel);   /* FX LIVE: from the last note, hands free */
void player_arp_inst(pl_inst_t inst);           /* the current instrument changed: the arpeggio plays it from its next note */

/* ---- the keys ----
 * note: MIDI 12-127; vel 1-127 (the keys play 7F unless a velocity is given). A key's note sounds the
 * instrument as a cell's would, through its cartridge's engine; a FUSION instrument plays its row. */
void player_key_on(uint8_t note, pl_inst_t inst, uint8_t vel);
void player_key_off(uint8_t note);
void player_keys_off(void);                     /* every key's note lets go */
/* THE SAMPLES list's keys (specs/ui/screens/instruments.md#the-samples): a sample instrument's slot bytes, copied now,
 * that a key names as PL_KEYS_SAMPLE_CART:PL_KEYS_SAMPLE_SLOT (no cartridge has slot FF): what ENV would make of the
 * highlighted sample, heard before it is picked; NULL none (such a key then plays nothing). The bytes are read at each
 * note-on, so a key plays the bytes last given before it. FE:FF, not FF:FF: the UI's "no instrument" is 0xFFFF
 * (ui_last_inst), so a "none" that reached the player by mistake plays nothing, never the preview */
#define PL_KEYS_SAMPLE_CART 0xFEu
#define PL_KEYS_SAMPLE_SLOT 0xFFu
#define PL_KEYS_SAMPLE_BYTES 40         /* (a sample instrument's slot: rec_slot_size(REC_SAMPLER)) */
void player_keys_sample(const uint8_t *slot);

/* ---- MIDI (specs/connections.md; core/midi) ----
 * player_ext_tempo: SYNC EXTERNAL's tempo, BPM x 10, in place of what plays' own (0: its own again); on DJ the
 * main deck takes it, its half or double nearest its song's own, and a synced right deck the main's.
 * player_ext_clock: each clock: their count since the master's START or CONTINUE (0: none yet, what starts waits
 * for the first), its arrival (plat_ms), the time between two (us, 0 unknown): what plays (on DJ the main deck)
 * keeps its place on them (specs/connections.md#clock).
 * player_keys_bend: the keys' notes bent by bend64 / 64 semitones (MIDI's pitch bend, ±2 semitones).
 * player_note_hook: called in the audio context with each note a track starts (vel 1-127, the track's volume)
 * and ends (note 0, vel 0) for MIDI OUT's ALL; NULL none. Muted tracks start none. */
void player_ext_tempo(uint16_t bpm10);
void player_ext_clock(uint32_t n, uint32_t at_ms, uint32_t per_us);
void player_sync_pulse(int side);              /* SYNC PULSE: 0 off, 1 the clicks on the left, 2 on the right: a 5 ms
                                                 * click each half beat of what plays, the music mono on the other
                                                 * side (the audio out only: VISUAL and USB audio keep stereo) */
uint32_t player_block_frame(void);              /* audio context: the frame of the half the block being made
                                                 * starts at (a note's, for MIDI OUT to send it as it is heard) */
void player_keys_bend(int16_t bend64);
extern void (*volatile player_note_hook)(uint8_t track, uint8_t note, uint8_t vel);

/* ---- what the screens read ---- */
typedef struct {
    uint8_t note;               /* the last note the track started (PL_NO_NOTE none yet) */
    pl_inst_t inst;             /* its instrument now */
    uint8_t vol;                /* its volume now, 00-7F */
    uint8_t sounding;           /* a voice of the track sounds */
} pl_track_t;

typedef struct {
    uint8_t what;               /* PL_NONE, PL_SONG, ... what plays or played last */
    uint8_t playing;            /* 1 playing, 0 paused / stopped */
    uint8_t ended;              /* the song stopped at its end (AT THE END = STOP) */
    uint8_t song;               /* the song (PL_SONG), or the current song */
    uint8_t pos, pat, row;      /* where it is: position, its pattern, the row */
    uint8_t rows;               /* the pattern's rows - 1 */
    uint8_t tick, speed;        /* the tick in the row, TICKS A ROW now */
    uint8_t ntrk;               /* the pattern's tracks */
    uint16_t bpm10;             /* the tempo now, BPM x 10 */
    uint32_t tempo_share;       /* PLAYBACK's speed, Q16 (player_tempo_share; 65536: 100 %) */
    int8_t vol_db;              /* PLAYBACK's volume, dB (player_song_volume; -25 OFF) */
    uint8_t end_mode;           /* SONGS' play mode in hold (PL_END_*: OWN none) */
    uint8_t end_next;           /* PLAY NEXT's next song (0xFF: none) */
    uint8_t ed_what;            /* what the screen edits (player_editing: PL_ED_*), and its key */
    uint32_t ed_key;
    uint32_t rows_played;       /* rows since it started (a clock for the screens) */
    uint8_t voices;             /* voices sounding now */
    uint8_t voices_max;         /* the most at once since player_state(s, 1) */
    uint8_t voices_short;       /* the most at once over the last whole window of 17 halves (98.7 ms): STATUS's */
    uint8_t voices_long;        /* the most at once over the last 5 such windows (493 ms): the header's */
    uint8_t voice_count;        /* the limit in force: 8, 12 or 16 (16 is 12 while core 2 does not run) */
    uint32_t steals;            /* voices taken from notes still sounding (a held one is parked) */
    uint8_t parked;             /* held notes parked now: unheard, waiting for a free voice */
    uint8_t hushed;             /* CUT on: the output silent */
    uint32_t muted;
    pl_track_t track[PL_TRACKS];
    uint32_t halves;            /* audio halves played since power-on (a clock for the main loop's hand-overs) */
    uint8_t stepping;           /* PLAYBACK's stepping holds a row (playing is 0 meanwhile) */
    uint8_t arp_on, arp_notes;  /* the arpeggiator on; the notes it has */
    /* DJ: the decks (what == PL_DJ), the LFOs' values now */
    struct {
        uint8_t song;           /* 0xFF none */
        uint8_t playing;        /* playing (a stop or a ROLL running counts) */
        uint8_t armed;          /* waiting to start */
        uint8_t ended;          /* stopped at its song's end */
        uint8_t pos, pat, row, rows, tick, speed;
        uint16_t bpm10;
        uint32_t rows_played;
        uint8_t stop;           /* PL_STOP_* running or standing */
        uint8_t stopped;        /* it has stopped (stands silent) */
        uint8_t loop;           /* PL_DLOOP_* */
        uint8_t loop_pos, loop_row, roll;
        uint16_t loop_rows;
        uint8_t land;           /* what waits for its bar line or beat: bit 0 a LOAD, bit 1 a JUMP, bit 2 a MOVE */
        uint8_t loads, jumps;   /* the LOADs and the JUMPs or MOVEs landed so far (counting on, wrapping) */
    } deck[2];
    int16_t lfo_xf8;            /* the crossfader now, 0-32 << 8 */
    int16_t lfo_filt8[2];       /* each DJ FILTER as heard, -64 << 8 .. 63 << 8 */
    int16_t lfo_prev8[2];       /* each filter's LFO's swing, heard or not (the filter row's preview) */
    int16_t lfo_mix;            /* the FX LFO's MIX now, %; -1 none */
    uint8_t lfo_live;           /* bit n: LFO n swings (or fades out) */
    uint8_t dj_mem;             /* the player holds the second deck's lent memory (player_dj_mem) */
    uint8_t dj_swaps;           /* the SWAPs made (player_dj_swap's count): deck[] is by the sides as they were then */
    uint8_t count_beats;        /* a count-in under way: its beats (4 or 8), else 0 */
    uint8_t count_beat;         /* its beats clicked so far (1 .. count_beats) */
    uint8_t lcue;               /* the loops held for the keys (player_cue_loops), not played */
    /* LOOPS' lanes (what == PL_LOOPS: row, tick and muted are theirs, muted the loops silent now) */
    uint8_t lp_jumps;           /* the jumps landed (player_lp_jump; counting on, wrapping) ... */
    uint8_t lp_land_row;        /* ... the last one's row (its pass: lp_pass, one less while row is under it: read within
                                 * a pass of it) */
    uint16_t lp_pass;           /* the loops' passes since they started (each wrap one more, a jump forward past the end) */
    uint8_t lp_bar_wait;        /* a mute / solo change waits for the bar line */
    uint8_t lp_alone;           /* the preview plays alone (playing is 0: the loops are paused, row its place) */
    uint8_t lp_rest;            /* RECORD LIVE's rest (player_lp_rest): 1 the loops silent (playing is 1, row the
                                 * replay's), 2 a jump queued, 3 silent from the next row */
    uint8_t lp_rest_row;        /* the loops' place while silent (or a queued jump's row) */
    uint8_t lp_sw, lp_sw_row;   /* the rest's switches so far (wrapping), the last one's loops' row (0xFF silent) */
    uint32_t lp_sw_rows;        /* ... and the rows_played it took effect at */
    uint32_t lp_ticks, lp_t0;   /* the loops' replay's ticks, counted on for good (the tick playing, RECORD LIVE's time);
                                 * the first tick of its last start */
} player_state_t;
void player_state(player_state_t *s, int reset_max);

/* Each voice of the pool (STATUS, the scopes). */
enum { PL_ENG_NONE, PL_ENG_DX7, PL_ENG_JUNO, PL_ENG_TB303, PL_ENG_SAMPLER, PL_ENG_CHIP };
typedef struct {
    int32_t pos;                /* a sample voice's place in its sample (the sampler's picture); -1 another voice */
    uint8_t on;                 /* sounding (held or releasing) */
    uint8_t engine;             /* PL_ENG_* */
    uint8_t track;              /* 0-15; 0xFF the keys */
    uint8_t note;
    pl_inst_t inst;
    uint8_t level;              /* its loudness lately, 0-255 */
} pl_voice_info_t;
int player_voices_now(pl_voice_info_t *v, int max);     /* PL_VOICES entries at most */
int player_scope(int voice, int16_t *dst, int n);       /* the voice's last n frames (n <= 64), mono */

#endif
