/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The player (player.h): songs and patterns tick by tick, the keys, one pool of voices for the four
 * engines (DX7: eng/dx7, MSFA in C; Juno-106: eng/juno, after KR-106; TB-303: eng/tb303, Open303 in
 * integer; the sampler: eng/samp, PTR audio), the mix and the effects (fx/).
 *
 * The tracker effects run as IBXM runs them (specs/files/commands.md): at a song's start the player
 * builds an IBXM module of the song (its order, its patterns' rows and tracks, no samples) whose cells
 * come from the song's pattern records (pl_get_note: a cursor a track), and every tick IBXM's replay runs
 * one tick without audio (replay_fm1_tick); each track's note then starts, is let go or cut as the replay
 * says, and every voice of the track's latest note follows its pitch (slides, vibrato, arpeggio: a bend),
 * volume (slides, tremolo, tremor) and pan, whatever engine it is. The other commands are the player's: an
 * instrument's parameters (0x40-0x7F, a table each engine: they act on the track's sounding notes and on
 * its next ones, until another instrument), the track's mixer (0x80-0x84), the FX (0x90-0xBF), the
 * TB-303's ACC / SLD, the punch-ins (0xC3-0xD2). A FUSION instrument's note plays the layers of its
 * pattern's row (row = the note), each its own note, instrument, volume and parameters (EDx in a layer:
 * it starts x ticks later; ECx: it is cut x ticks after the note's start), and they follow the track as
 * one note. A tick lasts 2.5 s / TEMPO, SWING stretching the even rows' ticks and shrinking the odd
 * rows'.
 *
 * Every note takes a voice of the pool among the first pl_nv (SETTINGS' VOICES: 8, 12 or 16; 16 is 12 core 1's
 * and 4 core 2's, rendered there: the voice job, pl_v2), a free one, the first first, else one is taken from a note (pl_alloc: release tails first, the quietest first; then held song notes, the
 * quietest first; the keys' last). A held note taken is parked: it moves to a slot past the count, fades out
 * over a block and waits there unheard, its envelopes going on, until a voice is free again: then it comes back
 * fading in (let go meanwhile, it is gone). A track's next note lets the previous one go: it rings on in its
 * own voice. Everything is rendered in blocks of 64 frames,
 * four to a half. The main loop only asks (a queue of requests); the audio context acts at the start of
 * its next half, and leaves what the screens read in a snapshot. */
#include <string.h>
#include "player.h"
#include "rec.h"
#include "../plat.h"
#include "../store/store.h"
#include "../eng/dx7/dx7.h"
#include "../eng/juno/juno.h"
#include "../eng/samp/samp.h"
#include "../eng/tb303/tb303.h"
#include "../eng/chip/chip.h"
#include "../fx/fx.h"
#include "../dbg.h"                      /* the debug build's probes (empty in the release) */
#include "ibxm/ibxm.h"
#include "sound_tables.h"                /* LEVEL_Q16 (fw/tools/gen_tables.py) */

#define PL_BLOCK 64u                     /* = DX7_N = JUNO_N = SAMP_N = TB_N = FX_BLOCK */
#define PL_NV PL_VOICES
#define PL_NV_START 12u                  /* the voices notes take at first */
#define PL_NCH PL_TRACKS
#define PL_STOP_FADE 128u                /* a cut fades out over 2.9 ms: no click */
#define PL_SCOPE 64u                     /* frames of each voice kept for the scopes: its last block */
#define PL_NPAR 8u                       /* a track's parameter commands kept (codes 0x40-0x7F; the oldest goes) */
#define PL_NLAYER 8u                     /* a FUSION note's layers at most */
#define PL_QUIET 33                      /* a released note under -60 dB (16-bit scale) for PL_QUIET_N */
#define PL_QUIET_N 4u                    /* blocks (5.8 ms) has ended: its voice is free */
#define PL_BACK_N 4u                     /* a parked note comes back fading in over 4 blocks (5.8 ms) */
#define PL_VEL 100                       /* every DX7 note's velocity (a cell's volume is a gain) */
#define PL_CHASE_ROWS 64u                /* rows a half may run through to reach where a song starts */
#define PL_CHASE_ARRIVE 16u              /* ... and still start the song in it: its first notes and their voices begin
                                          * there too (more: it starts in the next half, 5.8 ms on) */
#define PL_RQ 32u                        /* the requests' queue (a half takes them all) */
#ifndef PL_NOISE_ALWAYS
#define PL_NOISE_ALWAYS 0                /* 1: the Juno-106's noise made every block, a Juno-106 there or not */
#endif
#ifdef PTFM_DEBUG
static uint8_t pl_dbg_rp;                /* (the debug build: REPLAY runs, so CELLS and NOTES are its parts) */
#define PL_DBG_PART(s, t) do { if (pl_dbg_rp) DBG_ADD(s, t); } while (0)
#else
#define PL_DBG_PART(s, t) ((void)0)
#endif
/* Every engine's voices enter the mix at half (-6.02 dB: specs/files/instruments.md), each engine already
 * scaled so its reference sound is at -20 dB RMS of the 16-bit full scale */
#define PL_ENG_Q12 2048
#define PL_BARRIER() __asm__ __volatile__("" ::: "memory")
/* a branch the audio context rarely takes: its code placed out of the way, so the usual way walks fewer of the
 * code cache's 64-B lines (on the FM-1 a half comes to its code cold: each line ~1.3 us) */
#define PL_RARE(x) __builtin_expect(!!(x), 0)

enum { EN_DX7, EN_JUNO, EN_SAMP, EN_TB, EN_CHIP, EN_FUS, EN_NONE };
enum { PV_FREE, PV_SONG, PV_KEY };      /* a voice: unused, a track's, a key's */
/* a voice taken from its note: none; fading out of the slot it moved to (held at the block's end: parked, else
 * gone); parked (silent, its time going on, waiting for a free voice) */
enum { PK_NO, PK_OUT, PK_PARKED };
/* idle (nothing plays, paused or stopped), chasing (running the rows before where it starts), playing,
 * stopped at its end (its notes ring out) */
enum { PS_IDLE, PS_CHASE, PS_PLAY, PS_END, PS_STEP,   /* PS_STEP: stepped, the row held (PLAYBACK's SELECT) */
       PS_ARMED,                         /* DJ: at its row, waiting for the other deck's bar line (or a start) */
       PS_HELD,                          /* DJ: a stop or a ROLL holds the replay; it lands in time when let go */
       PS_BUILD };                       /* DJ: a paused deck's start made ready, a step a half (pl_build_step) */
enum { PF_ACCENT = 16, PF_SLIDE = 32 };

typedef struct {
    uint8_t use, engine;                 /* PV_*, EN_* */
    uint8_t held;                        /* its note is held (a track's current note, a key down) */
    uint8_t who;                         /* its track (0-15), or its key's MIDI note */
    uint8_t note, pan;
    uint8_t live;                        /* it was rendered in the last block */
    uint8_t gen;                         /* its track's note it belongs to (it follows the track while that is the latest) */
    uint8_t cut;                         /* NOTE CUT: it fades out over its next block, then goes */
    uint8_t lcut;                        /* a FUSION layer's ECx: the ticks left to its cut (0: none) */
    uint8_t quiet;                       /* released, the blocks it has been under PL_QUIET */
    uint8_t park;                        /* PK_* */
    uint8_t back;                        /* back from parking: the blocks of its fade in still to come */
    int16_t peak;                        /* its last block's peak, 16-bit scale */
    uint32_t parked;                     /* when it was parked (pl_parks): the last parked comes back first */
    pl_inst_t inst;
    uint32_t age;                        /* when it started: the oldest goes first */
    int32_t lvl;                         /* Q12: LEVEL x its (FUSION layer's) level x the cell's volume
                                          * (a Juno-106's without its LEVEL: that is in trim) */
    int32_t trim;                        /* a Juno-106's LEVEL, applied to what its engine renders (Q12) */
    int32_t gain;                        /* DX7, samples, TB-303: Q12 (the mix's x lvl x the track's volumes) */
    int32_t g_last;                      /* what its last block ended at (x the track's level), Q12 */
    int32_t m_last;                      /* its mute's gain at its last block's end (0 muted, 4096) */
    int32_t tune;                        /* a sample's TUNE command (1/64 semitones) */
    int16_t arp64;                       /* a song's FUSION layer's 0xy: its arpeggio's step now (1/64 semitones) */
    uint8_t jp[JUNO_PATCH_BYTES];        /* a Juno-106 voice's own patch (parameter commands turn it) */
    union {
        dx7_voice_t dx;
        juno_voice_t ju;
        samp_voice_t sm;
        tb_voice_t tb;
        chip_voice_t ch;
    } u;
} pl_voice_t;

typedef struct {                         /* a track */
    pl_inst_t ins;                       /* its instrument (the last cell's) */
    uint8_t has_ins;
    uint8_t note;                        /* its last note (MIDI) */
    uint8_t row_note;                    /* this row's note (0: none) */
    uint8_t gen;                         /* its notes, counted: the voices of the latest follow it */
    uint8_t live;                        /* one of its voices sounds */
    int8_t voice;                        /* its 303's voice, -1 none */
    uint8_t slide, row_slide, row_acc;   /* its 303's last note slides into the next; this row's SLD, ACC */
    int32_t gate;                        /* frames its 303's gate stays open (< 0: until the next note) */
    uint8_t mix[RM_N];                   /* its mixer, as the song's commands changed it */
    uint8_t np, pc[PL_NPAR], pv[PL_NPAR];   /* its parameter commands (code, value): another instrument drops them */
    int32_t bend, vol, pan;              /* the replay's this tick: 1/64 semitones, 0-64, 0-255 */
    int32_t level;                       /* LEVEL x DUCK, Q12, this block */
    int32_t peak;                        /* its voices' loudest this block (DUCK's source) */
    uint8_t npend, fus_note;             /* FUSION layers waiting for their EDx, and the note they are of */
    uint8_t pend[PL_NLAYER][2];          /* (layer, ticks left) */
} pl_chan_t;

/* ---- state ---- */
static pl_voice_t pl_v[PL_NV] PLAT_POOL;
static pl_chan_t pl_chs[PL_NCH];         /* the tracks: the song's (DJ's main deck), the loops' while nothing else
                                          * plays; DJ's second deck has its own (pl_d2) */
static int16_t pl_scope[PL_NV][PL_SCOPE] PLAT_POOL;
static uint32_t pl_scope_w;
static int32_t pl_vbuf[PL_BLOCK];        /* a voice's block */
static juno_chorus_t pl_chorus0[2] PLAT_POOL;    /* Juno-106 chorus I, II (every voice of the mode: it is linear) */
static uint16_t pl_chorus_tail[2][2];    /* each bus's: frames each runs on after its last input */
static int32_t pl_jbus0[2][PL_BLOCK];    /* the Juno-106 buses: chorus I, II (the dry ones go into the mix) */
/* A half's blocks are made in turn up to their effects (pl_audio_a), then each block's effects and output stage, its
 * job (pl_c2_job: pl_audio_fx, pl_audio_c), run: core 2's as each block is handed over (specs/second-core.md#when);
 * on core 1 after the half's blocks are made, the effects taking them together where they can (fx_blocks: each
 * effect's code fetched once a half). Each block has its slot: its mix and sends, the music clock and the effects'
 * settings it had, its flags, SYNC PULSE, where it goes in the codec's half. Core 1 writes a slot while it makes its
 * block; the job reads and writes that slot alone, and core 1 takes it again only in the next half, once core 2 has
 * finished (specs/second-core.md#who-owns-what) */
#define PL_HALF FX_HALF_MOST             /* (a half's blocks: 256 frames) */
static int32_t pl_hmix[PL_HALF][2u * PL_BLOCK] PLAT_POOL;   /* each block's mix: stereo, 16-bit scale */
static int32_t pl_hsend[PL_HALF][3u * PL_BLOCK] PLAT_POOL;  /* each block's send buses: chorus, delay, reverb */
#define PL_MIX_BYTES sizeof pl_hmix[0]
#define PL_SEND_BYTES sizeof pl_hsend[0]
static int32_t *pl_mix = pl_hmix[0];     /* the block being made (core 1's; the job takes its slot's own): its mix */
static int32_t *pl_send = pl_hsend[0];   /* and its sends */
/* the keys heard DRY (SETTINGS' KEYS, specs/ui/screens/fx.md#the-keys-dry): their own mix a block's slot, added to the
 * slot's mix just before the output stage (pl_audio_c); their Juno-106 chorus lines of their own, so a patch's chorus
 * plays on dry */
static int32_t pl_hkmix[PL_HALF][2u * PL_BLOCK] PLAT_POOL;
static int32_t *pl_kmix = pl_hkmix[0];
static juno_chorus_t pl_kchorus[2] PLAT_POOL;
static uint16_t pl_kchorus_tail[2];
static int32_t pl_kjbus[2][PL_BLOCK];
static uint8_t pl_kjbus_used;
static fx_clock_t pl_hclk[PL_HALF];      /* each block's music clock, as its effects work with it */
static fx_set_t pl_hset[PL_HALF];        /* and their settings */
static struct {
    uint8_t fx;                          /* its effects: 0 none (CUT held: done), 1 the song's (at rest, or fx_block
                                          * with the sends if `sends`), 2 DJ's (fx_block with the sends) */
    uint8_t sends;                       /* SENDS routing: its sends go to them */
    uint8_t silent;                      /* (pl_silent) */
    uint8_t clean;                       /* (pl_mix_clean) */
    uint8_t keys;                        /* its keys' mix (pl_hkmix) holds the keys heard DRY: added, then cleared */
    uint8_t pulse_side;                  /* SYNC PULSE's side as its block was made (pl_pulse_side, read once) */
    int16_t pulse_at;                    /* SYNC PULSE: a click starts at this frame of it (-1 none) */
    uint16_t b;                          /* its first frame in the codec's half */
} pl_hb[PL_HALF];
static int16_t *pl_lr;                   /* the half being made, the codec's (none: player_out takes each block): core 1
                                          * sets it at the half's start, once core 2 has finished the half before */
static uint32_t pl_slot;                 /* the block being made: its slot (core 1's) */
/* a silent block (no voice rendered, nothing from the Juno-106 buses) with the effects at rest (fx_rest_block): the
 * mix and sends stay 0s through it, untouched (specs/ui/screens/fx.md#effects-at-rest) */
static uint8_t pl_silent;                /* this block's voices put nothing into the mix or the sends */
#define pl_mix_clean (pl_hb[pl_slot].clean)   /* the block being made: its slot's mix and sends are 0s since it was last
                                               * used (it was silent at rest) */
static uint8_t pl_jbus_used[2];          /* a voice put something on replay k's Juno-106 buses since they were cleared */
/* SYNC PULSE (specs/connections.md#clock): a click on one side of the analog out at every half beat of what plays,
 * the music mono on the other (VISUAL and USB audio keep the stereo mix) */
#define PL_PULSE_N 221u                  /* (5 ms at full scale) */
static volatile uint8_t pl_pulse_side;   /* 0 off, 1 left, 2 right (the main loop's; each block keeps it in its slot) */
static uint32_t pl_pulse_left;           /* frames of the click under way still to go (the output's: the job's) */
void player_sync_pulse(int side) { pl_pulse_side = (uint8_t)(side == 1 || side == 2 ? side : 0); }
static uint32_t pl_age, pl_steals;
static uint32_t pl_age_seen;             /* pl_age at the last block's voices: a note started since has no peak yet */
static uint32_t pl_parks;                /* notes parked, counted */
/* CUT (player_hush): the main loop's wish (a byte: never lost, unlike a request), its state here (set at a half's
 * start, so the same through the half), the blocks since it began (PL_HUSH_N: faded out, the rest emptied), the
 * output's gain (Q15, the job's), faded over PL_HUSH_N blocks. CUT's halves run their effects on core 1 alone (their
 * voice jobs go to core 2 all the same) */
#define PL_HUSH_N 8u
static volatile uint8_t pl_hush_want;
static uint8_t pl_hush, pl_hush_n;
#define PL_HUSHED (pl_hush && pl_hush_n >= PL_HUSH_N)   /* CUT held and faded out: no effect is worked on */
static int32_t pl_out_g = 32768;
static uint8_t pl_nv = PL_NV_START;      /* the voices notes may take: the limit in force */
static uint8_t pl_nv_set = PL_NV_START;  /* SETTINGS' VOICES: 16 is 12 while core 2 does not take the halves */
static uint32_t pl_tick_acc;             /* the Juno-106 firmware loop (juno_tick_q16 per block) */
static uint8_t pl_voices_now, pl_voices_max;
/* the voices as the screens show them (specs/ui/frame.md#the-header): the most at once over a short window (STATUS)
 * and over 5 of them (the header), each published as it ends, so a count that comes and goes stays readable */
#define PL_VW_SHORT 17u                  /* halves: 98.7 ms */
#define PL_VW_LONG 5u                    /* short windows: 493 ms */
static uint8_t pl_vw_cur, pl_vw_acc, pl_vw_short, pl_vw_long, pl_vw_n, pl_vw_k;
static uint32_t pl_block_at;            /* the block's first frame in the half (the host tests' clock; MIDI OUT's
                                          * notes go out as it is heard: player_block_frame) */
uint32_t player_block_frame(void) { return pl_block_at; }
static volatile uint32_t pl_eng_us[4], pl_eng_blocks[4], pl_blocks;   /* what the sound costs (player_costs) */

/* what plays: the transport */
static uint8_t pl_what = PL_NONE;        /* what plays or played last */
static uint8_t pl_lcue, pl_lcue_back;    /* the loops held for the keys (player_cue_loops), and what pl_what was */
static struct { uint16_t tempo; uint8_t ticks, rpb, swing; } pl_lt;   /* the pattern loop's own timing (ticks 0: its
                                                                       * song's): player_play_pattern_at */

/* A replay: what plays on one set of 16 tracks, its song, its place, its transport. There are two: the song's
 * (and the pattern loop's; DJ's main deck), tracks 0-15, and LOOPS' (DJ's other deck), tracks 16-31, so the
 * decks play together and the loops have tracks of their own. pl_cur is the one being worked on; the code
 * reads its fields by the names below (pl_state ...). A voice's track (who) counts 0-31. */
typedef struct {
    uint8_t state, step_after;           /* PS_*; a chase that ends stepped (a step from paused) */
    uint8_t song;                        /* the song (PL_SONG), or the current song for the pattern loop */
    uint8_t loop_pat;                    /* the pattern loop's pattern (PL_LOOP) */
    uint8_t kept_pos, kept_row;          /* where it goes on from (paused) */
    uint8_t go_pos, go_row;              /* PS_CHASE: where it starts */
    uint8_t ended;                       /* the song stopped at its end */
    rec_song_t sd;                       /* the song (its record) */
    uint8_t sd_ok;
    uint8_t order1;                      /* the pattern loop's order: the pattern */
    uint16_t len;                        /* positions in the order played */
    const uint8_t *order;
    struct replay *rp;                   /* the song through IBXM's replay */
    struct module *mod;                  /* its module (the order played, its patterns' rows and tracks) */
    int rp_ended;                        /* the replay's count of the song's ends */
    uint32_t clock;                      /* frames into the current tick, Q16 */
    uint32_t tick_q16;                   /* the current tick's frames, Q16 (TEMPO, SWING) */
    int32_t gvol;                        /* the global volume (G, H) */
    uint32_t speed, tempo;
    uint8_t chasing;                     /* the chase runs: the rows' commands, no notes */
    int32_t skip_to;                     /* a skip queued for the row's end */
    int skip_rc;
    uint16_t restart;                    /* the skip landed: a clean start at position restart - 1 */
    uint32_t rows_seen;                  /* row starts since the start (the music clock) */
    uint32_t cap_beat, cap_frac;         /* the next row's start (playing: the clock stays under it) */
    uint32_t pu_rows;                    /* rows the punch-in a command started stays on */
    int32_t duck_env;                    /* DUCK: the source's level, Q15 */
    rec_pat_t *gp;                       /* the pattern the replay reads: its tracks' cursors, the row's cells
                                          * (shared by both while one plays; DJ's second deck has its own) */
    int32_t gp_num;
    rec_cur_t *gp_cur;
    rec_cell_t *cells;
    const pl_source_t *src;              /* another source of what plays (LOOPS), or none: the store */
    uint32_t mute;                       /* the muted tracks */
    uint32_t tscale;                     /* DJ: the tempo as a share of the song's, Q16 */
    uint16_t bpm_fix;                    /* DJ: a synced deck's tempo, BPM x 10, its song's own overridden (0 none) */
    int32_t key64;                       /* DJ: KEY, the deck's notes moved, 1/64 semitones */
    fx_clock_t clk;                      /* fx_clk while the other replay is worked on */
    pl_chan_t *ch;                       /* its 16 tracks */
    uint8_t handmix[PL_TRACKS][RM_N];    /* its song's mixer as its tracks last took it (a MIXER edit: what differs) */
    uint8_t k;                           /* 0, 1: its memory (its tracks from 16 k, its IBXM arena, its mix): it goes
                                          * with the replay when DJ's SWAP exchanges the two (its place in pl_ctxs is
                                          * its side: 0 the main) */
    uint8_t dj;                          /* a DJ deck: its song's own effects run on its sound alone */
    uint8_t arm, arm_chased;             /* DJ: a start ends armed (PS_ARMED); it got there by a chase */
    uint8_t build;                       /* PS_BUILD: its next step (1 the replay's seek, 2 the rest of its start) */
    uint8_t build_rp;                    /* ... it had a replay when it began: its song's own effects run on meanwhile,
                                          * as they would have till the start (a deck just loaded has none: still) */
    uint32_t build_at;                   /* ... the half its last step was taken in (pl_halves) */
    uint8_t ext_lock;                    /* SYNC EXTERNAL: it keeps its place on the clock (pl_ext_follow) */
    uint32_t ext_own;                    /* ... its place in the clock's clocks, Q8 (the ticks played) */
    int32_t ext_corr;                    /* ... its ticks shorter by this share, Q16 (longer: negative) */
} pl_ctx_t;
static pl_ctx_t pl_ctxs[2];
static pl_ctx_t *pl_cur = &pl_ctxs[0];
#define pl_state (pl_cur->state)
#define pl_step_after (pl_cur->step_after)
#define pl_song (pl_cur->song)
#define pl_loop_pat (pl_cur->loop_pat)
#define pl_kept_pos (pl_cur->kept_pos)
#define pl_kept_row (pl_cur->kept_row)
#define pl_go_pos (pl_cur->go_pos)
#define pl_go_row (pl_cur->go_row)
#define pl_ended (pl_cur->ended)
#define pl_sd (pl_cur->sd)
#define pl_sd_ok (pl_cur->sd_ok)
#define pl_handmix (pl_cur->handmix)
#define pl_order1 (pl_cur->order1)
#define pl_len (pl_cur->len)
#define pl_order (pl_cur->order)
#define pl_rp (pl_cur->rp)
#define pl_mod (pl_cur->mod)
#define pl_rp_ended (pl_cur->rp_ended)
#define pl_clock (pl_cur->clock)
#define pl_tick_q16 (pl_cur->tick_q16)
#define pl_gvol (pl_cur->gvol)
#define pl_speed (pl_cur->speed)
#define pl_tempo (pl_cur->tempo)
#define pl_chasing (pl_cur->chasing)
#define pl_skip_to (pl_cur->skip_to)
#define pl_skip_rc (pl_cur->skip_rc)
#define pl_rows_seen (pl_cur->rows_seen)
#define pl_cap_beat (pl_cur->cap_beat)
#define pl_cap_frac (pl_cur->cap_frac)
#define pl_pu_rows (pl_cur->pu_rows)
#define pl_duck_env (pl_cur->duck_env)
#define pl_gp (*pl_cur->gp)
#define pl_gp_num (pl_cur->gp_num)
#define pl_gp_cur (pl_cur->gp_cur)
#define pl_cells (pl_cur->cells)
static rec_pat_t pl_gp0;
static rec_cur_t pl_gp_cur0[PL_NCH];
static rec_cell_t pl_cells0[PL_NCH];
static uint8_t pl_swapped;               /* DJ's SWAP: pl_ctxs[0] is memory 1's replay (pl_dj_swap) */
#define PL_MCTX(m) (&pl_ctxs[((m) & 1u) ^ pl_swapped])   /* memory m's replay (a voice's: who >> 4) */
#define PL_SIDE ((uint32_t)(pl_cur != &pl_ctxs[0]))     /* the current replay's side: 0 the main (DJ's left) */
#define PL_CH(who) (PL_MCTX((who) >> 4)->ch[(who) & 15u])   /* a voice's track (who 0-31) */
#define pl_src (pl_cur->src)
#define pl_mute (pl_cur->mute)
#define pl_tscale (pl_cur->tscale)
#define pl_ch (pl_cur->ch)
#define pl_tbase (16u * pl_cur->k)       /* its first track among the 32 */
/* work on replay k from here (the music clock goes with it); called ~30 times a half: one copy of its code, not one
 * a call (inlined, its copies cost the FM-1 ~10 more code lines walked a half) */
static __attribute__((noinline)) void pl_use(uint32_t k)
{
    if (pl_cur == &pl_ctxs[k & 1u])
        return;
    pl_cur->clk = fx_clk;
    pl_cur = &pl_ctxs[k & 1u];
    fx_clk = pl_cur->clk;
}
static void pl_ctx_init(uint32_t k)
{
    pl_ctx_t *c = &pl_ctxs[k];
    memset(c, 0, sizeof *c);
    c->state = PS_IDLE;
    c->song = 0xFF;                      /* (none held: player_cue, a start) */
    c->tick_q16 = 882u << 16;
    c->gvol = 64;
    c->speed = 6;
    c->tempo = 125;
    c->skip_to = -1;
    c->gp_num = -1;
    c->tscale = 65536u;
    c->clk = fx_clk;
    c->ch = pl_chs;
    c->gp = &pl_gp0;
    c->gp_cur = pl_gp_cur0;
    c->cells = pl_cells0;
    c->k = (uint8_t)k;
}

/* ---- DJ's decks: the mix as the main loop set it (the mailbox), and the audio context's own ---- */
static player_dj_t pl_djm;               /* the mix's settings, as taken at this half's start */
static player_dj_t pl_djm_in;            /* the main loop's latest; its sequence odd while it is written */
static volatile uint32_t pl_djm_seq;
static uint32_t pl_djm_seen;
static uint8_t pl_dj_gen;                /* the stand's count of SWAPs as last made here (player_dj_swap) */
static void pl_swap_bytes(void *a, void *b, uint32_t n)   /* (in place: nothing of their size on the stack) */
{
    uint8_t *p = (uint8_t *)a, *q = (uint8_t *)b, t;
    while (n--) {
        t = *p;
        *p++ = *q;
        *q++ = t;
    }
}
#define PL_SWAP2(a) pl_swap_bytes(&(a)[0], &(a)[1], sizeof (a)[0])
/* the mix for the decks the other way round (a SWAP: as the stand, swapped, sends it) */
static void pl_djm_mirror(player_dj_t *m)
{
    m->xf = (uint8_t)(32u - m->xf);
    PL_SWAP2(m->vol);
    PL_SWAP2(m->filt);
    PL_SWAP2(m->filt_on);
    PL_SWAP2(m->filt_slide);
    PL_SWAP2(m->key);
    pl_swap_bytes(&m->lfo[PL_LFO_FILT_L], &m->lfo[PL_LFO_FILT_R], sizeof m->lfo[0]);
    m->lfo[PL_LFO_FADER].rev ^= 1u;      /* (its swing mirrored with it) */
}
typedef struct {                         /* an LFO swinging (as the playground's lfoRun) */
    uint8_t live;                        /* swinging, or fading out once switched off */
    uint8_t gen, shape, wait;            /* the start it is; its shape; waiting for the lead deck's next row */
    uint32_t ph, cyc;                    /* its phase (2^32 a cycle), its cycles */
    int32_t g;                           /* its swing's share, Q15: 1 on, fading out over 0.2 s once off */
    int32_t from, to;                    /* where it swings, in its thing's units, Q8 */
    uint32_t inc;                        /* its phase a block */
    int32_t r0, r1;                      /* RANDOM's places, Q16 */
    int32_t v;                           /* what comes out, smoothed over ~15 ms, Q8 */
    uint8_t has_v;
} pl_lfo_st_t;
typedef struct {                         /* a deck's stop (PLAY's pads 5-8), held */
    uint8_t kind;                        /* PL_STOP_* (NONE: no stop) */
    uint8_t held, stay;                  /* its pad down; let go, it stays stopped */
    uint8_t stood;                       /* it has stopped: silent, its place counting on */
    uint8_t beat_wait;                   /* ECHO / REVERB OUT: stopping on the next beat */
    uint8_t last_row;                    /* (the row it last saw: a new one on a beat is the beat) */
    uint8_t pos, row;                    /* where it was pressed */
    uint32_t frames;                     /* frames since the press (its place counts on from there) */
    uint32_t row_frames;                 /* a row's frames then */
    uint32_t back;                       /* BACKSPIN: frames to its next row back, Q8 */
    uint32_t spin;                       /* BRAKE / BACKSPIN let go: frames of spinning up left */
} pl_stop_t;
typedef struct {                         /* a deck's loop (LOOPS' pads) */
    uint8_t kind;                        /* PL_DLOOP_* */
    uint8_t pos, row;                    /* where it starts */
    uint16_t rows, n;                    /* its rows; rows into it */
    uint8_t roll;                        /* a ROLL (held: let go, the deck plays on from where it is in the loop) */
} pl_dloop_t;
typedef struct {
    uint8_t on;                          /* the decks are what plays */
    const uint8_t *setup;                /* DJ's FX setup (the shared effects) */
    fx_strip_t strip[2];                 /* each deck's song's own effects */
    fx_djf_t f_dry[2], f_snd[2][3];      /* each deck's DJ FILTER: on its sound, on each send stream */
    int32_t g_last[2];                   /* each deck's VOLUME x the crossfader at the last block's end, Q12 */
    int32_t sgain[2];                    /* a stop's gain on each deck's voices, Q12 */
    int32_t sbend[2];                    /* a stop's bend on them, 1/64 semitones */
    int32_t pbend[2];                    /* the bend last given them (KEY + the stop's) */
    int32_t xf8;                         /* the crossfader now (its LFO's), 0-32 << 8 */
    int32_t plain8[2], plain_v[2];       /* each filter as its knob and key have it (a slide in it), and glided, Q8 */
    int32_t heard[2];                    /* each filter's LFO heard (its key has it on), glided, Q15 */
    int32_t cut8[2], prev8[2];           /* each DJ FILTER as heard now; its LFO's swing (the preview), Q8 */
    uint8_t sliding[2];                  /* a slide under way */
    int32_t sl_from[2];                  /* from where, Q8 */
    uint32_t sl_n[2];                    /* blocks since */
    int32_t mix8;                        /* the FX LFO's MIX now (% Q8); -1 none */
    int8_t mix_fx;                       /* the effect it swings now (0 REVERB, 1 DELAY, 2 CHORUS); -1 none */
    uint8_t mix_sent;                    /* the MIX last given the effect, % */
    pl_lfo_st_t lfo[PL_NLFO];
    pl_stop_t stop[2];
    pl_dloop_t loop[2];
    uint32_t hand_q16[2];                /* HOLD's hand: a stepped row's ticks at its speed, frames Q16 ... */
    uint8_t hand_n[2];                   /* ... for this many more */
    uint32_t ring[2];                    /* ECHO OUT's and REVERB OUT's rings: frames left (the effect on for them) */
    uint8_t ring_fx[2];                  /* which: 1 the DELAY, 2 the REVERB */
    uint8_t arm_bar;                     /* bit d: deck d armed, waiting for the other's bar line */
    uint8_t row_now[2], bar_now[2];      /* deck d started a row, a bar in this block */
    /* the LFOs are worked out for a half's blocks at once, in its first (pl_dj_lfos: their code fetched once, not
     * once a block between the voices'), each block then taking its own (pl_dj_lfo_block) */
#define PL_LFO_STEPS 4u                  /* (a half's blocks: 256 frames) */
    uint8_t row_seen[2];                 /* deck d started a row since the LFOs were last worked out */
    uint8_t row_ago[2];                  /* the blocks since the first of those rows (0: this block) */
    uint8_t lfo_j, lfo_n;                /* the next block's step of those worked out, and how many there are */
    uint32_t lfo_at;                     /* the half they were last worked out in (pl_halves) */
    int32_t lfo_xf[PL_LFO_STEPS];        /* each step's crossfader, Q8 */
    int32_t lfo_cut[2][PL_LFO_STEPS];    /* each step's DJ FILTERs as heard, Q8 */
    int16_t lfo_mix[PL_LFO_STEPS];       /* each step's MIX for the FX LFO's effect, %; -1 none */
    uint8_t lfo_mixk[PL_LFO_STEPS];      /* (that MIX's FX command) */
} pl_djs_t;
/* DJ's own memory: DJ's state above, and the second deck's own (its 16 tracks, the pattern its replay reads, its
 * Juno-106 chorus and buses, its mix and sends; its IBXM arena after this), lent by the store from the ROM
 * cache's part while DJ plays (player_dj_mem; specs/files/ram.md). Without it there is no DJ, and the second
 * replay (the loops, while nothing else plays) shares the first's tracks, pattern, arena and buses */
typedef struct {
    pl_djs_t dj;
    pl_chan_t ch[PL_NCH];
    rec_pat_t gp;
    rec_cur_t gp_cur[PL_NCH];
    rec_cell_t cells[PL_NCH];
    juno_chorus_t chorus[2];
    int32_t jbus[2][PL_BLOCK];
    int32_t mix[2u * PL_BLOCK];
    int32_t send[3u * PL_BLOCK];
} pl_d2_t;
static pl_d2_t *pl_d2;
#define pl_dj (pl_d2->dj)                /* (only while DJ plays: PL_DJ_ON) */
#define PL_DJ_ON PL_RARE(pl_d2 && pl_d2->dj.on)   /* (DJ's code out of the other halves' way) */
#define PL_CHORUS(k) ((k) ? pl_d2->chorus : pl_chorus0)
#define PL_JBUS(k) ((k) ? pl_d2->jbus : pl_jbus0)
#define pl_mix1 (pl_d2->mix)
#define pl_send1 (pl_d2->send)
static int8_t pl_alloc_k = -1;           /* who asks for a voice: a replay (0, 1), the keys -1 */
static void pl_dj_row(const struct ibxm_fm1_state *st);
static PLAT_COLD void pl_dj_swap(void);

/* requests (main loop -> audio) and the snapshot (audio -> main loop) */
enum { RQ_SONG, RQ_PATTERN, RQ_PAUSE, RQ_CONTINUE, RQ_TO_START, RQ_STOP, RQ_SKIP, RQ_CUT, RQ_MUTES, RQ_VOICES,
       RQ_SONG_CHANGED, RQ_STORE, RQ_KEY_ON, RQ_KEY_OFF, RQ_KEYS_OFF, RQ_PUNCH, RQ_FX_CMD, RQ_FX_RELOAD, RQ_BYPASS,
       RQ_SOURCE, RQ_SRC_CHANGED, RQ_CLICK, RQ_STEP, RQ_ARP, RQ_ARP_CLEAR, RQ_ARP_START,
       RQ_ARP_INST /* the arpeggio's instrument */, RQ_KEY_BEND /* MIDI's pitch bend */,
       RQ_TEMPO_SHARE /* PLAYBACK's speed */, RQ_SONG_VOLUME /* ... and its volume */, RQ_END_MODE /* SONGS' play mode */,
       RQ_FX_MINE /* the FX page's effect under the cursor */, RQ_EDITING /* what the screen edits */,
       RQ_CUE /* nothing played yet: the keys' song held */, RQ_CUE_LOOPS /* LOOPS shown, nothing playing: the loops */,
       RQ_UNLOAD /* SONGS' UNLOAD SONG */, RQ_TAKE_CELL /* the EDITOR's take: a cell heard as written */,
       RQ_LP_NOTE, RQ_LP_TAKE, RQ_LP_MUTES, RQ_LP_PREVIEW, RQ_LP_ALONE, RQ_LP_KEEP, RQ_LP_REST /* LOOPS' lanes */,
       /* (from RQ_DECK_LOAD on: DJ's decks, but the two after them, taken first) */
       RQ_DJ, RQ_DECK_LOAD, RQ_DECK_PLAY, RQ_DECK_ARM, RQ_DECK_PLACE, RQ_DECK_TEMPO, RQ_DECK_MUTES,
       RQ_DECK_STOP, RQ_DECK_LOOP, RQ_DJ_SWAP, RQ_DECK_HAND /* DJ's decks */, RQ_DJ_MEM, RQ_COUNT /* the count-in */ };
#define PL_SECOND(w) ((w) == PL_LOOPS || (w) == PL_PREVIEW)   /* what plays on the second replay (a source) */
static uint32_t pl_halves;
/* VISUAL's: the output's last frames (a ring, the job's), each track's peak since the main loop last read it */
#define PL_MASTER 128u                   /* (whole blocks: a block written never wraps) */
#if PL_MASTER % 64u
#error "PL_MASTER: whole blocks of PL_BLOCK"
#endif
static int16_t pl_master[2u * PL_MASTER] PLAT_POOL;
uint32_t player_ram(void)
{
    return (uint32_t)(sizeof pl_v + sizeof pl_scope + sizeof pl_chorus0 + sizeof pl_hmix + sizeof pl_hsend +
                      sizeof pl_hkmix + sizeof pl_kchorus + sizeof pl_master);
}
static volatile uint32_t pl_master_w;
static volatile uint16_t pl_tpeak[2u * PL_TRACKS];
typedef struct {
    uint8_t type, a, b, c, d;
    const void *ptr;
} pl_rq_t;
static pl_rq_t pl_rq[PL_RQ];
static volatile uint32_t pl_rq_w, pl_rq_r;
/* the count-in (specs/ui/transport.md#count-in): bars of 4 beats, each a click of its own (BEEP at 60 %, the bar's
 * first higher), timed by the audio a block at a time; what starts after it (the EDITOR's take: RQ_PATTERN, the
 * loops: RQ_SOURCE) is posted after player_count_in and held here till the last beat is over, so it starts on the
 * beat */
static struct {
    uint8_t on, beats, beat, has_def;    /* counting; its beats, those clicked; a start held */
    int32_t left;                        /* frames to the next beat */
    uint32_t fpb;                        /* frames a beat */
    pl_rq_t def;                         /* the start held */
} pl_cnt;
/* the queue's last PL_RQ_KEEP slots are for what must never be lost; past them those are counted (the main loop
 * counts, the audio context takes the counts): the requests without values, and each note's key-off as a bit
 * toggled. MIDI's pitch bends are not queued: the latest is taken at a half's start */
#define PL_RQ_KEEP 6u
static const uint8_t PL_LATE[5] = {RQ_PAUSE, RQ_STOP, RQ_CUT, RQ_STORE, RQ_KEYS_OFF};
static volatile uint8_t pl_late_req[5];
static uint8_t pl_late_seen[5];
static volatile uint32_t pl_koff_req[4];
static uint32_t pl_koff_ack[4];
static volatile int16_t pl_bend_req;
static volatile uint32_t pl_bend_seq;
static uint32_t pl_bend_seen;
static player_state_t pl_snap;
static volatile uint32_t pl_snap_seq;
static pl_voice_info_t pl_vsnap[PL_NV];

/* ---- instruments: a cartridge's record, cached by number ---- */
#define PL_NCART 8u
static struct {
    uint8_t num, valid;
    uint32_t age;
    rec_cart_t c;
} pl_carts[PL_NCART];
static uint32_t pl_cart_age;

static const rec_cart_t *pl_cart(uint8_t num)
{
    uint32_t i, old = 0;
    for (i = 0; i < PL_NCART; i++)
        if (pl_carts[i].valid && pl_carts[i].num == num) {
            pl_carts[i].age = ++pl_cart_age;
            return pl_carts[i].c.kind ? &pl_carts[i].c : 0;
        }
    for (i = 1; i < PL_NCART; i++)
        if (!pl_carts[i].valid || (pl_carts[old].valid && pl_carts[i].age < pl_carts[old].age))
            old = i;
    pl_carts[old].num = num;
    pl_carts[old].valid = 1;
    pl_carts[old].age = ++pl_cart_age;
    if (rec_cart(num, &pl_carts[old].c)) {
        pl_carts[old].valid = 0;         /* (not kept: a cartridge in ROM not yet in the ROM cache comes later) */
        return 0;
    }
    return pl_carts[old].c.kind ? &pl_carts[old].c : 0;
}

static void pl_fpat_forget(void);

static void pl_carts_forget(void)
{
    uint32_t i;
    for (i = 0; i < PL_NCART; i++)
        pl_carts[i].valid = 0;
    pl_gp_num = -1;
    pl_fpat_forget();
}

/* an instrument's engine and its slot's bytes (EN_NONE: none there) */
static uint32_t pl_engine(pl_inst_t in, const uint8_t **slot)
{
    const rec_cart_t *c = pl_cart(in.cart);
    const uint8_t *s = c ? rec_slot(c, in.slot) : 0;
    if (slot)
        *slot = s;
    if (!s)
        return EN_NONE;
    return c->kind == REC_DX7 ? EN_DX7 : c->kind == REC_JUNO ? EN_JUNO : c->kind == REC_TB303 ? EN_TB
         : c->kind == REC_SAMPLER ? EN_SAMP : c->kind == REC_CHIP ? EN_CHIP : EN_FUS;
}

static int32_t pl_level_q12(pl_inst_t in)    /* its LEVEL as a gain, Q12 */
{
    const rec_cart_t *c = pl_cart(in.cart);
    const uint8_t *s = c ? rec_slot(c, in.slot) : 0;
    return s ? (int32_t)(LEVEL_Q16[(int32_t)rec_level(c->kind, s) + 128] >> 4) : 4096;
}

/* a sample instrument as the sampler plays it; 0 if its sample's audio is not there */
static int pl_sample_of(pl_inst_t in, samp_t *s, uint8_t *volume)
{
    const rec_cart_t *c = pl_cart(in.cart);
    const uint8_t *slot = c ? rec_slot(c, in.slot) : 0;
    rec_sinst_t si;
    rec_sample_t sa;
    uint32_t len;
    uint8_t ver;
    if (!slot || c->kind != REC_SAMPLER)
        return 0;
    rec_sampler_inst(slot, &si);
    if (volume)
        *volume = si.volume > 64u ? 64u : si.volume;
    if (rec_sample(c, si.sample, &sa))
        return 0;
    s->rec = store_get(REC_AUDIO, in.cart, si.sample, &len, &ver);
    if (!s->rec || ver != 1)
        return 0;
    s->rec_len = len;
    s->length = (int32_t)sa.length;
    s->rate = sa.rate ? sa.rate : 44100u;
    s->start = (int32_t)si.start;
    s->end = (int32_t)(si.end ? si.end : sa.length);
    s->loop_start = (int32_t)si.loop_start;
    s->loop_end = (int32_t)si.loop_end;
    s->loop = si.loop;
    s->base = si.base;
    s->tune = (int16_t)((int32_t)si.tune - 128);
    memcpy(s->env, si.env, 8);
    return 1;
}

/* ---- an instrument's parameters (0x40-0x7F: specs/files/commands.md) ---- */
/* a DX7 parameter's byte in the 156 (OP6 first), and its top; -1 none */
static int32_t pl_dx7_at(uint32_t code, uint32_t *top)
{
    static const uint8_t OP[8] = {16, 18, 19, 20, 0, 1, 2, 3}, OPTOP[8] = {99, 31, 99, 14, 99, 99, 99, 99};
    static const uint8_t GL[11] = {134, 135, 136, 144, 137, 138, 139, 140, 142, 141, 143};
    static const uint8_t GLTOP[11] = {31, 7, 1, 48, 99, 99, 99, 99, 5, 1, 7};
    if (code >= 0x40u && code < 0x70u) {
        *top = OPTOP[(code - 0x40u) & 7u];
        return (int32_t)((5u - (code - 0x40u) / 8u) * 21u + OP[(code - 0x40u) & 7u]);
    }
    if (code >= 0x70u && code < 0x7Bu) {
        *top = GLTOP[code - 0x70u];
        return GL[code - 0x70u];
    }
    return -1;
}

/* a Juno-106 parameter into a patch's 18 bytes: the sliders, then the switches' bits */
static void pl_juno_par(uint8_t *p, uint32_t code, uint32_t v)
{
    if (code >= 0x40u && code < 0x50u) {
        p[code - 0x40u] = (uint8_t)(v > 127u ? 127u : v);
        return;
    }
    switch (code) {
    case 0x50: p[16] = (uint8_t)((p[16] & ~7u) | (v == 0u ? 1u : v == 1u ? 2u : 4u)); break;   /* RANGE */
    case 0x51: p[16] = (uint8_t)(v ? p[16] | 8u : p[16] & ~8u); break;                          /* PULSE */
    case 0x52: p[16] = (uint8_t)(v ? p[16] | 16u : p[16] & ~16u); break;                        /* SAW */
    case 0x53: p[16] = (uint8_t)((p[16] & ~0x60u) | (v == 0u ? 0x20u : v == 1u ? 0x40u : 0u)); break;   /* CHORUS */
    case 0x54: p[17] = (uint8_t)(v ? p[17] | 1u : p[17] & ~1u); break;                          /* PWM: MAN */
    case 0x55: p[17] = (uint8_t)(v ? p[17] | 2u : p[17] & ~2u); break;                          /* ENV - */
    case 0x56: p[17] = (uint8_t)(v ? p[17] | 4u : p[17] & ~4u); break;                          /* VCA: GATE */
    case 0x57: p[17] = (uint8_t)((p[17] & ~0x18u) | ((3u - (v > 3u ? 3u : v)) << 3)); break;     /* HPF */
    default: break;
    }
}

/* a sample's position parameter (START, END, the loop's): the value x 1/256 of its length; 255 its end */
static int32_t pl_samp_pos(const samp_t *s, uint32_t v)
{
    return v >= 255u ? s->length : (int32_t)(((uint32_t)s->length >> 8) * v + (((uint32_t)s->length & 255u) * v >> 8));
}

/* a parameter turned on a sounding voice, as turning the knob would (a sample's START: from the next note) */
static void pl_param_live(pl_voice_t *v, uint32_t code, uint32_t val)
{
    uint32_t top;
    int32_t at;
    if (v->engine == EN_TB) {
        if (code < 0x40u + TB_NK)
            tb_knob(&v->u.tb, (int)(code - 0x40u), (int)val);
    } else if (v->engine == EN_CHIP) {
        chip_param(&v->u.ch, code, val);
    } else if (v->engine == EN_JUNO) {
        pl_juno_par(v->jp, code, val);
        juno_patch(&v->u.ju, v->jp);
    } else if (v->engine == EN_DX7) {
        at = pl_dx7_at(code, &top);
        if (at >= 0)
            dx7_live(&v->u.dx, at, (int)(val > top ? top : val));
    } else if (v->engine == EN_SAMP) {
        samp_t *s = &v->u.sm.s;
        if (code == 0x41u) {             /* END */
            int32_t e = pl_samp_pos(s, val);
            s->end = e < 1 ? 1 : e;
        } else if (code == 0x42u) {      /* LOOP */
            s->loop = (uint8_t)(val > 2u ? 2u : val);
            v->u.sm.looping = (uint8_t)(v->u.sm.looping && s->loop != SAMP_LOOP_OFF);
        } else if (code == 0x43u) {
            s->loop_start = pl_samp_pos(s, val);
        } else if (code == 0x44u) {
            s->loop_end = pl_samp_pos(s, val);
        } else if (code == 0x45u) {      /* TUNE: 1/128 semitones */
            v->tune = ((int32_t)val - 128) / 2;
        } else if (code >= 0x48u && code < 0x50u) {   /* the envelope */
            s->env[code - 0x48u] = (uint8_t)(val > 99u ? 99u : val);
        }
    }
}

/* ---- voices ---- */
static int pl_v_sounds(pl_voice_t *v)
{
    if (v->use == PV_FREE)
        return 0;
    return v->engine == EN_DX7 ? dx7_active(&v->u.dx) : v->engine == EN_JUNO ? juno_busy(&v->u.ju)
         : v->engine == EN_TB ? tb_active(&v->u.tb) : v->engine == EN_CHIP ? chip_active(&v->u.ch) : samp_active(&v->u.sm);
}

static void pl_v_off(pl_voice_t *v)      /* note off: it releases */
{
    if (v->engine == EN_DX7)
        dx7_note_off(&v->u.dx);
    else if (v->engine == EN_JUNO)
        juno_note_off(&v->u.ju);
    else if (v->engine == EN_TB)
        tb_note_off(&v->u.tb);
    else if (v->engine == EN_CHIP)
        chip_note_off(&v->u.ch);
    else
        samp_note_off(&v->u.sm);
    v->held = 0;
}

static void pl_v_free(pl_voice_t *v)
{
    if (v->use == PV_SONG && v->who < 2u * PL_NCH && PL_CH(v->who).voice >= 0 && &pl_v[PL_CH(v->who).voice] == v)
        PL_CH(v->who).voice = -1;
    v->use = PV_FREE;
    v->held = 0;
    v->cut = 0;
    v->park = PK_NO;
    v->back = 0;
}

static void pl_v_cut(pl_voice_t *v)      /* silence now */
{
    if (v->engine == EN_DX7)
        dx7_cut(&v->u.dx);
    else if (v->engine == EN_JUNO)
        juno_cut(&v->u.ju);
    else if (v->engine == EN_TB)
        tb_cut(&v->u.tb);
    else if (v->engine == EN_CHIP)
        chip_cut(&v->u.ch);
    else
        samp_cut(&v->u.sm);
    pl_v_free(v);
}

/* ---- the pool: notes sound in the first pl_nv voices; the slots past them hold the notes taken from their voices,
 * fading out (a block) and, if held, parked until a voice is free (specs/ui/screens/settings.md#voices) ---- */
static void pl_kf_moved(int from, int to);

/* voice `from`'s note into slot `to`, as it is (its patch copy, its scope, the keys' FUSION layer that names it) */
static PLAT_COLD void pl_move(pl_voice_t *from, pl_voice_t *to)
{
    int a = (int)(from - pl_v), b = (int)(to - pl_v);
    *to = *from;
    if (to->engine == EN_JUNO && to->u.ju.p == from->jp)
        to->u.ju.p = to->jp;
    memcpy(pl_scope[b], pl_scope[a], sizeof pl_scope[0]);
    if (from->use == PV_SONG && from->who < 2u * PL_NCH && PL_CH(from->who).voice == (int8_t)a)
        PL_CH(from->who).voice = -1;     /* (a track's 303 no longer: its next note takes a voice of its own) */
    pl_kf_moved(a, b);
}

/* the notes the pool holds at most, parked and fading ones included: the count and the 4 past VOICES 16, 16 at
 * least (VOICES 16: 20; 12 and 8: 16) */
static uint32_t pl_places(void)
{
    uint32_t n = pl_nv + (PL_NV - PL_VOICES_MOST);
    return n > PL_VOICES_MOST ? n : PL_VOICES_MOST;
}

/* a slot past the count for a note taken: a free one while the pool holds fewer notes than pl_places(), else the
 * note parked longest goes (unheard: nothing to hear of it), a song's before the keys'; 0 if every one is still
 * fading out */
static pl_voice_t *pl_spare(void)
{
    pl_voice_t *v, *free = 0, *old = 0;
    uint32_t used = 0;
    for (v = pl_v; v < pl_v + PL_NV; v++) {
        if (v->use == PV_FREE) {
            if (!free && v >= pl_v + pl_nv)
                free = v;
            continue;
        }
        used++;
        if (v >= pl_v + pl_nv && v->park == PK_PARKED &&
            (!old || (v->use == PV_KEY) < (old->use == PV_KEY) ||
             ((v->use == PV_KEY) == (old->use == PV_KEY) && v->parked < old->parked)))
            old = v;
    }
    if (free && used < pl_places())
        return free;
    if (old)
        pl_v_cut(old);
    return old;
}

/* voice v's slot for a new note: its note moves past the count and fades out over a block, a held one (but a
 * TB-303's) to be parked, the rest to go; with no slot there it is cut at once */
static void pl_take(pl_voice_t *v)
{
    pl_voice_t *s = pl_spare();
    if (s) {
        pl_move(v, s);
        if (s->held && !s->cut && s->engine != EN_TB)
            s->parked = ++pl_parks;
        else
            s->cut = 1;
        s->park = PK_OUT;
        s->back = 0;
    }
    pl_v_cut(v);
}

/* how much a voice would be missed (the least goes first): release tails, the new note's own track's first, then
 * the quietest; then held song notes, the quietest first (a muted track's count as silent); the keys' held notes
 * last, the oldest first, however quiet (pl_alloc gives them up only to the keys' own). A note started since the
 * last block has no peak yet: the last of its kind */
static uint32_t pl_worth(const pl_voice_t *v, int who)
{
    uint32_t loud = v->m_last && v->peak > 0 ? (uint32_t)v->peak : 0u, rank;
    if (!v->held || v->cut) {
        rank = v->use == PV_SONG && (int)v->who == who ? 0u : 1u;
    } else if (v->use == PV_SONG) {
        rank = 2u;
    } else {
        rank = 3u;
        loud = 0;
    }
    if (v->age > pl_age_seen)
        loud = 0xFFFFu;
    return rank << 16 | loud;
}
static pl_voice_t *pl_victim(int deck, int who)   /* the voice to take (of a deck's: deck >= 0); ties: the oldest */
{
    pl_voice_t *best = 0, *v, *end = pl_v + pl_nv;
    uint32_t bw = 0;
    for (v = pl_v; v < end; v++) {
        uint32_t w;
        if (deck >= 0 && (v->use != PV_SONG || (v->who >> 4 & 1u) != (uint32_t)deck))
            continue;
        w = pl_worth(v, who);
        if (!best || w < bw || (w == bw && v->age < best->age)) {
            best = v;
            bw = w;
        }
    }
    return best;
}
static int pl_dj_heard(int d)            /* DJ: a deck heard (the crossfader and VOLUME give it some) */
{
    return pl_dj.g_last[d] > 0;
}
/* a voice for a new note of track `who` (-1: the keys) among the first pl_nv: a free one, else one taken. The keys'
 * held notes are never taken for a song's note (specs/ui/screens/settings.md#voices): with every voice a key's, or
 * a key's note parked and waiting for one, the song's note gets a slot past the count and starts parked
 * (pl_born_parked), to come back as a voice comes free, after the keys' */
static uint8_t pl_born_parked;           /* pl_voice_on's next note starts parked */
static pl_voice_t *pl_park_new(void)
{
    pl_voice_t *s = pl_spare();
    pl_born_parked = s != 0;
    return s;
}
static PLAT_COLD pl_voice_t *pl_alloc(int who)
{
    pl_voice_t *best = 0, *v, *end = pl_v + pl_nv;
    if (pl_hush)                         /* (CUT held: no note starts) */
        return 0;
    if (who >= 0)                        /* (a key's note waiting for a voice: the song's waits behind it) */
        for (v = end; v < pl_v + PL_NV; v++)
            if (v->use == PV_KEY && v->park == PK_PARKED && v->held && !v->cut)
                return pl_park_new();
    for (v = pl_v; v < end; v++)
        if (v->use == PV_FREE)
            return v;
    if (PL_DJ_ON && pl_alloc_k >= 0) {   /* DJ: the deck lower in the mix gives way (the right at the middle); a
                                          * deck faded out takes only its own, never the heard deck's */
        int me = pl_alloc_k, lower = pl_dj.xf8 < (16 << 8) ? 1 : pl_dj.xf8 > (16 << 8) ? 0 : 1;
        int heard = pl_dj_heard(me ^ pl_swapped), from = heard ? lower ^ pl_swapped : me;   /* (me a memory, lower a
                                                                                        * side: pl_swapped maps them) */
        best = pl_victim(from, who);
        if (!best && from != me)
            best = pl_victim(me, who);
        if (!best && !heard)
            return 0;
    }
    if (!best)
        best = pl_victim(-1, who);
    if (who >= 0 && best && best->use == PV_KEY && best->held && !best->cut)
        return pl_park_new();            /* (the keys hold every voice: the song's note starts parked) */
    pl_steals++;
    pl_take(best);
    return best;
}

/* before a block's voices: a voice free among the first pl_nv takes back the note parked last, fading in, the keys'
 * before a song's; a note parked in a slot the count now reaches comes back where it is */
static void pl_unpark(void)
{
    pl_voice_t *v, *s, *b;
    int none = 0;
    for (v = pl_v; v < pl_v + pl_nv; v++) {
        if (v->use != PV_FREE) {
            if (v->park == PK_PARKED) {
                v->park = PK_NO;
                v->back = PL_BACK_N;
            }
            continue;
        }
        for (b = 0, s = pl_v + pl_nv; !none && s < pl_v + PL_NV; s++)
            if (s->use != PV_FREE && s->park == PK_PARKED && s->held && !s->cut &&
                (!b || (s->use == PV_KEY) > (b->use == PV_KEY) ||
                 ((s->use == PV_KEY) == (b->use == PV_KEY) && s->parked > b->parked)))
                b = s;
        if (!b) {
            none = 1;
            continue;
        }
        pl_move(b, v);
        v->park = PK_NO;
        v->back = PL_BACK_N;
        b->use = PV_FREE;                /* (its slot: what is left there is written over when it is used) */
        b->held = b->cut = 0;
        b->park = PK_NO;
    }
}

/* another count: the notes held above it are parked (a TB-303's let go), their tails ring on; notes parked where
 * the count now reaches come back */
static void pl_nv_apply(uint32_t n)
{
    uint32_t i;
    pl_nv = (uint8_t)(n < 1u ? 1u : n > PL_VOICES_MOST ? PL_VOICES_MOST : n);
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        if (v->use == PV_FREE)
            continue;
        if (i < pl_nv) {
            if (v->park == PK_PARKED) {
                v->park = PK_NO;
                v->back = PL_BACK_N;
            }
        } else if (v->held && !v->cut && v->park == PK_NO) {
            if (v->engine == EN_TB) {
                if (v->use == PV_SONG && PL_CH(v->who).voice == (int8_t)i)
                    PL_CH(v->who).voice = -1;
                pl_v_off(v);
            } else {
                v->park = PK_OUT;
                v->parked = ++pl_parks;
                v->back = 0;
            }
        }
    }
}

/* a voice's level now: its lvl x dyn (Q12: the track's volume x the global volume; the keys 4096) */
static void pl_v_level(pl_voice_t *v, int32_t dyn)
{
    int32_t l = (v->lvl * dyn) >> 12;
    if (v->engine == EN_JUNO)
        juno_level(&v->u.ju, l);
    else
        v->gain = (PL_ENG_Q12 * l) >> 12;
}

/* a voice's pitch: its note bent by bend64 / 64 semitones */
static void pl_v_pitch(pl_voice_t *v, int32_t bend64)
{
    int32_t q24 = bend64 * 21845 + bend64 / 3;   /* (an octave is 2^24) */
    if (v->engine == EN_DX7)
        v->u.dx.bend = q24;
    else if (v->engine == EN_JUNO)
        v->u.ju.bend = q24;
    else if (v->engine == EN_TB)
        tb_pitch(&v->u.tb, v->note, bend64);
    else if (v->engine == EN_CHIP)
        chip_pitch(&v->u.ch, bend64);
    else
        samp_pitch(&v->u.sm, v->note, bend64 + v->tune);
}

/* voice v plays instrument in's note (use, who: whose; vol 1-127; pan 0-255) at its LEVEL x lvl_q12 (a
 * FUSION layer's level, else 4096), with the parameter commands pc / pv (np of them) on top of its slot;
 * offset: a sample's SAMPLE OFFSET. 0 if the instrument cannot sound (the voice is then left free) */
static PLAT_COLD int pl_voice_on(pl_voice_t *v, uint32_t use, uint32_t who, pl_inst_t in, int note, uint32_t vol,
                       uint32_t pan, uint32_t acc, int32_t lvl_q12, const uint8_t *pc, const uint8_t *pv,
                       uint32_t np, int32_t offset)
{
    const uint8_t *slot;
    uint32_t e = pl_engine(in, &slot), i, born = pl_born_parked;
    int32_t trim;
    pl_born_parked = 0;
    if (!v || e == EN_NONE || e == EN_FUS)   /* (no voice: DJ's deck faded out finds none it may take) */
        return 0;
    if (born && e == EN_TB)              /* (a TB-303 slides from note to note in its voice: no voice, no note) */
        return 0;
    DBG_NOTE();                          /* (the debug build: notes started in the half) */
    if (v->engine != e) {                /* another engine's state was here: start from zero */
        memset(&v->u, 0, sizeof v->u);
        v->engine = (uint8_t)e;
    }
    v->use = (uint8_t)use;
    v->who = (uint8_t)who;
    v->held = 1;
    v->cut = v->quiet = v->lcut = 0;
    v->park = PK_NO;
    v->back = 0;
    v->inst = in;
    v->note = (uint8_t)note;
    v->pan = (uint8_t)pan;
    v->age = ++pl_age;
    v->peak = 0;
    v->tune = 0;
    v->arp64 = 0;
    v->g_last = 0;
    v->m_last = use == PV_SONG && (PL_MCTX(who >> 4)->mute >> (who & 15u) & 1u) ? 0 : 4096;   /* (a muted
                                                                     * track's: silent from its start) */
    trim = pl_level_q12(in);
    /* a Juno-106 plays at its engine's own level and its LEVEL scales what it renders (the engine is not
     * linear in its level: its filter's seed noise, its saturation); the others' LEVEL scales their output */
    v->trim = e == EN_JUNO ? trim : 4096;
    v->lvl = e == EN_JUNO ? lvl_q12 : (int32_t)(((int64_t)trim * lvl_q12) >> 12);
    v->lvl = (v->lvl > 65535 ? 65535 : v->lvl) * (int32_t)vol / 127;   /* (+24 dB at most) */
    memset(pl_scope[v - pl_v], 0, sizeof pl_scope[0]);
    if (e == EN_DX7) {
        uint8_t vc[156];
        uint32_t top;
        rec_dx7_vced(slot, vc);
        for (i = 0; i < np; i++) {       /* the track's parameters on its copy */
            int32_t at = pl_dx7_at(pc[i], &top);
            if (at >= 0)
                vc[at] = (uint8_t)(pv[i] > top ? top : pv[i]);
        }
        dx7_note_on(&v->u.dx, vc, note, PL_VEL);
    } else if (e == EN_JUNO) {
        rec_juno_patch(slot, v->jp);
        for (i = 0; i < np; i++)
            pl_juno_par(v->jp, pc[i], pv[i]);
        juno_note_on(&v->u.ju, v->jp, note, v->lvl);
    } else if (e == EN_TB) {
        uint8_t k[TB_NK];
        rec_tb_knobs(slot, k);
        for (i = 0; i < np; i++)
            if (pc[i] >= 0x40u && pc[i] < 0x40u + TB_NK)
                k[pc[i] - 0x40u] = (uint8_t)(pv[i] > 127u ? 127u : pv[i]);
        tb_note_on(&v->u.tb, k, note, (int)acc);
    } else if (e == EN_CHIP) {
        chip_patch_t cp;
        chip_unpack(slot, &cp);
        for (i = 0; i < np; i++)         /* the track's parameters on its copy */
            chip_set(&cp, pc[i], pv[i]);
        chip_note_on(&v->u.ch, &cp, note);
    } else {
        samp_t s;
        if (!pl_sample_of(in, &s, 0)) {
            v->use = PV_FREE;
            v->held = 0;
            return 0;
        }
        for (i = 0; i < np; i++) {
            if (pc[i] == 0x40u)          /* START */
                s.start = pl_samp_pos(&s, pv[i]);
            else if (pc[i] == 0x45u)
                v->tune = ((int32_t)pv[i] - 128) / 2;
        }
        if (s.start >= s.length)
            s.start = 0;
        samp_note_on(&v->u.sm, &s, note, offset);
        for (i = 0; i < np; i++)         /* END, the loop, the envelope */
            if (pc[i] != 0x40u && pc[i] != 0x45u)
                pl_param_live(v, pc[i], pv[i]);
        if (v->tune)
            samp_pitch(&v->u.sm, note, v->tune);
    }
    pl_v_level(v, 4096);
    if (born) {                          /* (past the count, the keys holding every voice: unheard until one is free) */
        v->park = PK_PARKED;
        v->parked = ++pl_parks;
    }
    return 1;
}

/* ---- what plays: the song and its patterns, from the store or from another source (LOOPS) ---- */
static int pl_rec_song(rec_song_t *s) { return pl_src ? pl_src->song(s) : rec_song(pl_song, s); }
static int pl_rec_pattern(uint8_t num, rec_pat_t *pt) { return pl_src ? pl_src->pattern(num, pt) : rec_pattern(num, pt); }

static void pl_src_insts(void)           /* a source's tracks' sounds (LOOPS: a loop's; its cells name none) */
{
    uint32_t t;
    for (t = 0; t < PL_NCH && pl_src && pl_src->track_inst; t++) {
        pl_inst_t in;
        pl_chan_t *ch = &pl_ch[t];
        if (!pl_src->track_inst((int)t, &in))
            continue;
        if (!ch->has_ins || in.cart != ch->ins.cart || in.slot != ch->ins.slot)
            ch->np = 0;
        ch->ins = in;
        ch->has_ins = 1;
    }
}

/* ---- patterns: the replay's cells ---- */
static void pl_cell_none(rec_cell_t *c)
{
    c->has = 0;
    c->n = 0;
}

/* pattern num's row `row` into cells (the tracks it has; the rest empty): for FUSION rows, read whole. The
 * last two patterns' layouts are kept (a FUSION track plays one pattern note after note) */
static struct {
    int16_t num;
    rec_pat_t pt;
} pl_fpat[2];                            /* (num -1 at player_init: pl_carts_forget) */
static uint8_t pl_fpat_next;

static void pl_fpat_forget(void) { pl_fpat[0].num = pl_fpat[1].num = -1; }
static rec_cell_t pl_fcells[PL_NCH];     /* a FUSION row's layers as read (one at a time: the keys', a trigger's, a
                                          * waiting layer's) */

static uint32_t pl_row_of(uint8_t num, uint32_t row, rec_cell_t *cells)
{
    rec_pat_t *pt = 0;
    uint32_t t;
    for (t = 0; t < PL_NCH; t++)
        pl_cell_none(&cells[t]);
    for (t = 0; t < 2u; t++)
        if (pl_fpat[t].num == num)
            pt = &pl_fpat[t].pt;
    if (!pt) {
        pt = &pl_fpat[pl_fpat_next].pt;
        pl_fpat[pl_fpat_next].num = -1;
        if (rec_pattern(num, pt))
            return 0;
        pl_fpat[pl_fpat_next].num = num;
        pl_fpat_next ^= 1u;
    }
    if (row >= pt->rows)
        return 0;
    for (t = 0; t < pt->ntrk; t++) {
        rec_cur_t c;
        rec_cur_set(&c, pt->trk[t]);
        rec_cur_at(&c, (int)row, &cells[t]);
    }
    return pt->ntrk;
}

/* ---- FUSION ---- */
/* a FUSION instrument's row for note (its layers' cells); its tracks, 0: none */
static uint32_t pl_fusion_row(pl_inst_t f, uint32_t note, rec_cell_t *cells)
{
    const rec_cart_t *c = pl_cart(f.cart);
    const uint8_t *slot = c && c->kind == REC_FUSION ? rec_slot(c, f.slot) : 0;
    uint32_t n;
    if (!slot || note < 12u || note > 127u)
        return 0;
    n = pl_row_of(rec_fusion_pattern(slot), note, cells);
    return n < PL_NLAYER ? n : PL_NLAYER;
}

/* a FUSION layer's delay (its EDx: ticks), 0 none */
static uint32_t pl_layer_delay(const rec_cell_t *c)
{
    uint32_t k;
    for (k = 0; k < c->n; k++)
        if (c->cmd[k][0] == 0x0Eu && (c->cmd[k][1] >> 4) == 0xDu)
            return c->cmd[k][1] & 15u;
    return 0;
}

/* layer c of FUSION instrument f's note on a new voice: its note, instrument, volume and parameters, at the
 * FUSION slot's LEVEL; 0 if it has none. at: the ticks since the note's start (its EDx); a song's layer with
 * ECx is cut x ticks after the note's start (at or before at: it never sounds). No nesting: a layer naming
 * a FUSION instrument is silent */
static PLAT_COLD pl_voice_t *pl_layer_on(uint32_t use, uint32_t who, pl_inst_t f, const rec_cell_t *c, uint32_t pan,
                               uint32_t at)
{
    uint8_t pc[4], pv[4];
    uint32_t k, np = 0;
    int32_t cut = -1;
    pl_voice_t *v;
    pl_inst_t in;
    if (!(c->has & RC_NOTE) || c->note == REC_NOTE_OFF || !(c->has & RC_INS))
        return 0;
    in.cart = c->cart;
    in.slot = c->slot;
    k = pl_engine(in, 0);
    if (k == EN_NONE || k == EN_FUS)
        return 0;
    for (k = 0; k < c->n; k++)
        if (c->cmd[k][0] >= 0x40u && c->cmd[k][0] < 0x80u) {
            pc[np] = c->cmd[k][0];
            pv[np++] = c->cmd[k][1];
        } else if (c->cmd[k][0] == 0x0Eu && (c->cmd[k][1] >> 4) == 0xCu) {
            cut = (int32_t)(c->cmd[k][1] & 15u);
        }
    if (use == PV_SONG && cut >= 0 && (uint32_t)cut <= at)
        return 0;
#ifdef PL_TEST_TRIGGER
    if (use == PV_SONG)
        PL_TEST_TRIGGER(who, c->note, in);
#endif
    v = pl_alloc(use == PV_SONG ? (int)who : -1);
    if (!pl_voice_on(v, use, who, in, c->note, c->has & RC_VOL ? c->vol : 127u, pan, 0, pl_level_q12(f), pc, pv, np, 0))
        return 0;
    if (use == PV_SONG && cut >= 0)
        v->lcut = (uint8_t)((uint32_t)cut - at);
    return v;
}

static void pl_layer_cuts(void)          /* a tick on for the layers' ECx */
{
    uint32_t i;
    for (i = 0; i < PL_NV; i++)          /* (the current replay's: each counts its own ticks) */
        if (pl_v[i].lcut && (pl_v[i].use != PV_SONG || (pl_v[i].who >> 4 & 1u) == pl_cur->k) && !--pl_v[i].lcut)
            pl_v[i].cut = 1;
}

/* ---- the keys ---- */
static uint8_t pl_keys[8], pl_nkeys;     /* the keys down, oldest first (a 303 slides back to the last) */
static uint8_t pl_kfire;                 /* the keys started a note since the last block (FILTER's envelope) */
static int32_t pl_key_bend;              /* MIDI's pitch bend on the keys, 1/64 semitones */

/* ---- the keys' FUSION notes: their layers' tick commands. A key's row has no length of its own: its ticks count
 * from the press, at the tempo of what plays, for as long as the key is down (as the playground's): EDx starts a
 * layer x ticks late, ECx cuts it at tick x (at or before its start: it never sounds), E9x starts it again every
 * x ticks, 0xy arpeggiates it (its note, + x, + y semitones, a tick each), Axy slides its volume (x up, y down, 2
 * a tick, but on its starts). A song's FUSION cell whose row has E9x, 0xy or Axy runs here too (pl_sf), its ticks
 * from the note's start at its replay's tempo, for as long as the track's note sounds (its next note, a note off or
 * a cut stop them: specs/files/patterns.md#fusion-patterns); one with EDx and ECx alone keeps its own way (pl_pending,
 * pl_layer_cuts: the replay's ticks). */
#define PL_KF 4u                         /* key notes running at once (a fifth: the oldest gives way, sounding on) */
#define PL_SF 4u                         /* songs' FUSION notes with tick commands running at once (the same) */
#define PL_KL_NONE 0xFFu
typedef struct {
    int8_t v;                            /* its voice (-1: none) */
    uint8_t d, cut, rt, arp, sl, vol;    /* EDx; ECx (PL_KL_NONE: none); E9x; 0xy; Axy; its volume now */
    uint8_t done;                        /* cut, never to sound, or its voice taken by another note */
    uint16_t age;                        /* its voice's start (so a voice taken since is not its) */
} pl_kl_t;
typedef struct {
    uint8_t on, who, row, n;             /* who: the key's (a PV_KEY voice's who) or the track's (PV_SONG's: t + its
                                          * replay's 16); row: its note; n: its layers */
    uint8_t use, gen;                    /* PV_KEY or PV_SONG; a song's: its track's note (the voices' gen) */
    uint8_t lane;                        /* a loop's lane's (LOOPS: its lanes each their own), else 0xFF */
    pl_inst_t in;                        /* the FUSION instrument */
    uint16_t tick;                       /* ticks since the press */
    uint32_t q16;                        /* frames into this tick, Q16 */
    pl_kl_t l[PL_NLAYER];
} pl_fn_t;
static pl_fn_t pl_kf[PL_KF], pl_sf[PL_SF];

static void pl_kf_moved(int from, int to)   /* a voice's note moved to another slot (pl_move): its layer follows */
{
    uint32_t f, k;
    for (f = 0; f < PL_KF + PL_SF; f++)
        for (k = 0; k < PL_NLAYER; k++) {
            pl_kl_t *l = f < PL_KF ? &pl_kf[f].l[k] : &pl_sf[f - PL_KF].l[k];
            if (l->v == (int8_t)from)
                l->v = (int8_t)to;
        }
}

static pl_voice_t *pl_kl_voice(const pl_kl_t *l, const pl_fn_t *fn)   /* the layer's voice, while it is still its */
{
    pl_voice_t *v = l->v >= 0 ? &pl_v[l->v] : 0;
    return v && v->use == fn->use && v->who == fn->who && (uint16_t)v->age == l->age ? v : 0;
}

static void pl_kl_start(pl_fn_t *fn, uint32_t k, const rec_cell_t *c)   /* layer k of FUSION note fn starts (again) */
{
    pl_kl_t *l = &fn->l[k];
    pl_voice_t *v = pl_kl_voice(l, fn);
    int key = fn->use == PV_KEY;
    if (v)
        v->cut = 1;                      /* (E9x: its last start fades out over a block) */
    pl_alloc_k = key ? -1 : (int8_t)(fn->who >> 4 & 1u);   /* (the keys ask, or the track's replay) */
    v = pl_layer_on(fn->use, fn->who, fn->in, c, key ? 128u : (uint32_t)PL_CH(fn->who).pan, 0);
    l->v = v ? (int8_t)(v - pl_v) : -1;
    if (!v) {
        l->done = 1;
        return;
    }
    l->age = (uint16_t)v->age;
    l->vol = (uint8_t)(c->has & RC_VOL ? c->vol : 127u);
    if (!key) {                          /* (a song's: its track's note, its ECx this runner's; its track's volume and
                                          * bend at once, as pl_track_follow has them at the next tick) */
        v->gen = fn->gen;
        v->lcut = 0;
        pl_v_level(v, PL_CH(fn->who).vol * PL_MCTX(fn->who >> 4)->gvol);
        pl_v_pitch(v, PL_CH(fn->who).bend + PL_MCTX(fn->who >> 4)->key64 + v->arp64);
    } else if (pl_key_bend) {
        pl_v_pitch(v, pl_key_bend);
    }
}

/* a layer's volume now (Axy), as pl_voice_on's: a song's under its track's volume, as pl_track_follow has it */
static void pl_kl_vol(pl_voice_t *v, const pl_fn_t *fn, uint32_t vol)
{
    int32_t lq = pl_level_q12(fn->in), trim = pl_level_q12(v->inst);
    int32_t lvl = v->engine == EN_JUNO ? lq : (int32_t)(((int64_t)trim * lq) >> 12);
    v->lvl = (lvl > 65535 ? 65535 : lvl) * (int32_t)vol / 127;
    pl_v_level(v, fn->use == PV_KEY ? 4096 : PL_CH(fn->who).vol * PL_MCTX(fn->who >> 4)->gvol);
}

static PLAT_COLD void pl_kf_tick(pl_fn_t *fn)   /* FUSION note fn's tick (fn->tick): its layers' commands */
{
    rec_cell_t *cells = pl_fcells;
    uint32_t n = fn->tick, k, got = 0, more = 0;
    for (k = 0; k < fn->n; k++) {
        pl_kl_t *l = &fn->l[k];
        pl_voice_t *v;
        if (l->done)
            continue;
        if (n < l->d) {
            more = 1;
            continue;
        }
        if (l->cut != PL_KL_NONE && n >= l->cut) {      /* ECx, before any start (at or before its EDx: silent) */
            if ((v = pl_kl_voice(l, fn)) != 0)
                v->cut = 1;
            l->done = 1;
            continue;
        }
        if (n == l->d || (l->rt && (n - l->d) % l->rt == 0u)) {
            if (!got)                    /* (its row read again: what was read at the press is gone by now) */
                got = pl_fusion_row(fn->in, fn->row, cells) + 1u;
            if (k + 1u >= got) {
                l->done = 1;
                continue;
            }
            pl_kl_start(fn, k, &cells[k]);
            if (l->done)
                continue;
        } else if ((v = pl_kl_voice(l, fn)) != 0) {
            if (l->sl) {
                int32_t x = (int32_t)l->vol + 2 * ((int32_t)(l->sl >> 4) - (int32_t)(l->sl & 15u));
                l->vol = (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x);
                pl_kl_vol(v, fn, l->vol);
            }
            if (l->arp) {
                uint32_t s = (n - l->d) % 3u;
                int32_t a = 64 * (int32_t)(s == 0u ? 0u : s == 1u ? l->arp >> 4 : l->arp & 15u);
                if (fn->use == PV_KEY) {
                    pl_v_pitch(v, pl_key_bend + a);
                } else {                 /* (a song's: on its track's bend and its replay's KEY, as pl_track_follow) */
                    v->arp64 = (int16_t)a;
                    pl_v_pitch(v, PL_CH(fn->who).bend + PL_MCTX(fn->who >> 4)->key64 + a);
                }
            }
        } else {
            l->done = 1;                 /* (its voice taken by another note) */
            continue;
        }
        if (l->rt || l->arp || l->sl || l->cut != PL_KL_NONE)
            more = 1;
    }
    if (!more)                           /* (nothing more to come: its slot free) */
        fn->on = 0;
}

/* a FUSION note's row's layers, tick 0, in pool fs (n of them: the keys' or the songs'); who's own (a loop's lane's
 * own: lane, else 0xFF), else a free one, else the oldest gives way */
static PLAT_COLD void pl_fn_on(pl_fn_t *fs, uint32_t nfs, uint32_t use, uint32_t who, uint32_t gen, pl_inst_t in, uint32_t row,
                     uint32_t lane)
{
    uint32_t f, k, j, n = pl_fusion_row(in, row, pl_fcells);
    for (f = 0; f < nfs && !(fs[f].on && fs[f].who == who && fs[f].lane == lane); f++)
        ;
    if (f == nfs)
        for (f = 0; f < nfs && fs[f].on; f++)
            ;
    if (f == nfs)                        /* all running: the oldest gives way */
        for (f = k = 0; k < nfs; k++)
            if (fs[k].tick > fs[f].tick)
                f = k;
    pl_fn_t *fn = &fs[f];
    fn->on = 1;
    fn->who = (uint8_t)who;
    fn->use = (uint8_t)use;
    fn->gen = (uint8_t)gen;
    fn->lane = (uint8_t)lane;
    fn->row = (uint8_t)row;
    fn->in = in;
    fn->n = (uint8_t)n;
    fn->tick = 0;
    fn->q16 = 0;
    for (k = 0; k < n; k++) {
        const rec_cell_t *c = &pl_fcells[k];
        pl_kl_t *l = &fn->l[k];
        l->v = -1;
        l->done = 0;
        l->d = l->rt = l->arp = l->sl = 0;
        l->cut = PL_KL_NONE;
        l->vol = (uint8_t)(c->has & RC_VOL ? c->vol : 127u);
        for (j = 0; j < c->n; j++) {
            uint32_t code = c->cmd[j][0], val = c->cmd[j][1];
            if (code == 0x0Eu && val >> 4 == 0xDu)
                l->d = (uint8_t)(val & 15u);
            else if (code == 0x0Eu && val >> 4 == 0xCu)
                l->cut = (uint8_t)(val & 15u);
            else if (code == 0x0Eu && val >> 4 == 0x9u)
                l->rt = (uint8_t)(val & 15u);
            else if (code == 0x00u)
                l->arp = (uint8_t)val;
            else if (code == 0x0Au)
                l->sl = (uint8_t)val;
        }
    }
    pl_kf_tick(fn);
}
static void pl_kf_on(uint32_t who, pl_inst_t in, uint32_t row)   /* key who's FUSION note */
{
    pl_fn_on(pl_kf, PL_KF, PV_KEY, who, 0, in, row, 0xFFu);
}

static void pl_kf_off(int who)           /* key who let go (-1: all): its ticks stop; a layer still to start never does */
{
    uint32_t f;
    for (f = 0; f < PL_KF; f++)
        if (who < 0 || pl_kf[f].who == (uint32_t)who)
            pl_kf[f].on = 0;
}
/* a song's: track who's FUSION note ends (its next note, a note off, a cut: its ticks stop); -1 every one of replay
 * k's (k 2: both) */
static void pl_sf_off(int who, uint32_t k)
{
    uint32_t f;
    for (f = 0; f < PL_SF; f++)
        if (who >= 0 ? pl_sf[f].who == (uint32_t)who : k > 1u || (pl_sf[f].who >> 4 & 1u) == k)
            pl_sf[f].on = 0;
}
static void pl_sf_lane_off(uint32_t lane) /* a loop's lane's FUSION note ends (LOOPS' lanes) */
{
    uint32_t f;
    for (f = 0; f < PL_SF; f++)
        if (pl_sf[f].lane == lane)
            pl_sf[f].on = 0;
}

static __attribute__((noinline)) void pl_kf_run(pl_fn_t *fn, uint32_t len)   /* (one sounding: out of the way) */
{
    fn->q16 += PL_BLOCK << 16;
    while (fn->on && fn->q16 >= len) {
        fn->q16 -= len;
        if (fn->tick < 0xFFFFu)
            fn->tick++;
        pl_kf_tick(fn);
    }
}
static void pl_kf_block(void)            /* a block on: the key notes' ticks due, at the tempo of what plays; the
                                          * songs' at their replay's */
{
    uint32_t f, len = pl_tick_q16 > (64u << 16) ? pl_tick_q16 : 882u << 16;
    for (f = 0; f < PL_KF; f++)
        if (PL_RARE(pl_kf[f].on))
            pl_kf_run(&pl_kf[f], len);
    for (f = 0; f < PL_SF; f++)          /* (as long as its note sounds, paused too: as the keys') */
        if (PL_RARE(pl_sf[f].on)) {
            uint32_t sl = PL_MCTX(pl_sf[f].who >> 4)->tick_q16;
            pl_kf_run(&pl_sf[f], sl > (64u << 16) ? sl : 882u << 16);
        }
}

static void pl_key_off(uint8_t note)
{
    uint32_t i, k;
    for (i = k = 0; i < pl_nkeys; i++)
        if (pl_keys[i] != note)
            pl_keys[k++] = pl_keys[i];
    pl_nkeys = (uint8_t)k;
    pl_kf_off(note);                     /* (a FUSION note's ticks stop with it) */
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        if (v->use != PV_KEY || v->who != note || !v->held)
            continue;
        if (v->engine == EN_TB && pl_nkeys) {   /* a 303 with keys still down: back to the last of them */
            v->who = pl_keys[pl_nkeys - 1u];
            v->note = v->who;
            tb_slide_to(&v->u.tb, v->note, 0);
        } else {
            pl_v_off(v);
        }
    }
}

static void pl_key_on(uint8_t note, pl_inst_t in, uint8_t vel)
{
    pl_alloc_k = -1;                     /* (the keys ask) */
    uint32_t i, e = pl_engine(in, 0);
    if (pl_nkeys < sizeof pl_keys)
        pl_keys[pl_nkeys++] = note;
    if (e != EN_NONE)
        pl_kfire = 1;                    /* (FILTER's envelope, SOURCE KEYS) */
    if (e == EN_FUS) {                   /* its layers, their ticks from the press */
        pl_kf_on(note, in, note);
        return;
    }
    if (e == EN_NONE)
        return;
    if (e == EN_TB)                      /* a 303 sounding on the keys: legato, it slides there */
        for (i = 0; i < PL_NV; i++) {
            pl_voice_t *v = &pl_v[i];
            if (v->use == PV_KEY && v->engine == EN_TB && v->held && v->inst.cart == in.cart && v->inst.slot == in.slot) {
                v->who = note;
                v->note = note;
                tb_slide_to(&v->u.tb, note, 0);
                return;
            }
        }
    {
        pl_voice_t *v = pl_alloc(-1);
        if (pl_voice_on(v, PV_KEY, note, in, note, vel ? vel : 127u, 128, 0, 4096, 0, 0, 0, 0) && pl_key_bend)
            pl_v_pitch(v, pl_key_bend);  /* (MIDI's pitch bend, as it stands) */
    }
}

/* MIDI's pitch bend: the keys' sounding notes, and those to come, bent by pl_key_bend / 64 semitones */
static void pl_keys_bend(int32_t bend64)
{
    uint32_t i;
    pl_key_bend = bend64;
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_KEY && pl_v[i].engine != EN_FUS)
            pl_v_pitch(&pl_v[i], bend64);
}

static void pl_keys_release(void)
{
    uint32_t i;
    pl_nkeys = 0;
    pl_kf_off(-1);
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_KEY && pl_v[i].held)
            pl_v_off(&pl_v[i]);
}

/* ---- the arpeggiator (player.h): its notes sound on the keys' voices as one more key, PL_ARP_WHO ---- */
#define PL_ARP_WHO 0xFFu
static const char *const PL_RHYTHMS[10] = {"#", "#_", "#__", "#___", "##_", "###_", "##__", "#_##_", "#_______",
                                           "#_______________"};
static struct {
    uint8_t on, rhythm, pattern, latch, n, vel, sounding;
    uint8_t notes[8];                    /* in the order played */
    pl_inst_t inst;
    uint32_t k, i;                       /* notes played; the rhythm's steps */
    int32_t t, off;                      /* frames to its next step; to its sounding note's end */
    uint32_t seed;
} pl_arp;

static void pl_arp_silence(void)
{
    uint32_t i;
    pl_kf_off(PL_ARP_WHO);
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_KEY && pl_v[i].who == PL_ARP_WHO && pl_v[i].held)
            pl_v_off(&pl_v[i]);
    pl_arp.sounding = 0;
}

static void pl_arp_keep(uint32_t most)   /* the oldest go past `most` */
{
    while (pl_arp.n > most) {
        uint32_t i;
        for (i = 1; i < pl_arp.n; i++)
            pl_arp.notes[i - 1u] = pl_arp.notes[i];
        pl_arp.n--;
    }
}

static void pl_arp_add(uint8_t note, int alone)
{
    uint32_t i;
    if (alone && pl_arp.latch)           /* (a key with none held: a new set) */
        pl_arp.n = 0;
    for (i = 0; i < pl_arp.n; i++)
        if (pl_arp.notes[i] == note)
            return;
    pl_arp_keep(sizeof pl_arp.notes - 1u);
    pl_arp.notes[pl_arp.n++] = note;
    if (pl_arp.latch)
        pl_arp_keep(pl_arp.latch);
}

static void pl_arp_lift(uint8_t note)    /* its key let go: with the latch OFF, the note goes */
{
    uint32_t i, k;
    if (pl_arp.latch)
        return;
    for (i = k = 0; i < pl_arp.n; i++)
        if (pl_arp.notes[i] != note)
            pl_arp.notes[k++] = pl_arp.notes[i];
    pl_arp.n = (uint8_t)k;
}

static uint8_t pl_arp_next(void)         /* the next note in the pattern */
{
    uint8_t up[8], seq[16];
    uint32_t n = pl_arp.n, i, j, m = 0;
    for (i = 0; i < n; i++)              /* (sorted up) */
        up[i] = pl_arp.notes[i];
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && up[j - 1u] > up[j]; j--) {
            uint8_t x = up[j];
            up[j] = up[j - 1u];
            up[j - 1u] = x;
        }
    if (pl_arp.pattern == PL_ARP_RANDOM) {
        pl_arp.seed = pl_arp.seed * 1103515245u + 12345u;
        return up[(pl_arp.seed >> 16) % n];
    }
    if (pl_arp.pattern == PL_ARP_PLAYED)
        return pl_arp.notes[pl_arp.k % n];
    for (i = 0; i < n; i++)
        seq[m++] = pl_arp.pattern == PL_ARP_DOWN ? up[n - 1u - i] : up[i];
    if (pl_arp.pattern == PL_ARP_UPDOWN)
        for (i = n >= 2u ? n - 2u : 0u; i >= 1u && i < n; i--)
            seq[m++] = up[i];
    return seq[pl_arp.k % m];
}

static void pl_arp_sound(uint8_t note)   /* a note of the arpeggio, the last key's instrument */
{
    pl_inst_t in = pl_arp.inst;
    uint32_t e = pl_engine(in, 0);
    pl_alloc_k = -1;                     /* (the keys ask) */
    if (e != EN_NONE)
        pl_kfire = 1;                    /* (FILTER's envelope, SOURCE KEYS) */
    if (e == EN_FUS) {
        pl_kf_on(PL_ARP_WHO, in, note);
    } else if (e != EN_NONE) {
        pl_voice_on(pl_alloc(-1), PV_KEY, PL_ARP_WHO, in, note, pl_arp.vel ? pl_arp.vel : 127u, 128, 0, 4096, 0, 0, 0, 0);
    }
    pl_arp.sounding = 1;
}

static __attribute__((noinline)) void pl_arp_run(void)   /* (on: out of the way of the blocks without it) */
{
    int32_t row;
    const char *rh;
    uint32_t len;
    row = (int32_t)(fx_clk.beat_frames / (fx_clk.rpb ? fx_clk.rpb : 4u));
    rh = PL_RHYTHMS[pl_arp.rhythm < 10u ? pl_arp.rhythm : 3u];
    len = (uint32_t)strlen(rh);
    if (pl_arp.sounding && (pl_arp.off -= (int32_t)PL_BLOCK) <= 0)
        pl_arp_silence();
    if (!pl_arp.n) {                     /* (no notes: it waits for a key, from its rhythm's start) */
        pl_arp.t = 0;
        pl_arp.i = 0;
        return;
    }
    pl_arp.t -= (int32_t)PL_BLOCK;
    while (pl_arp.t <= 0) {
        if (rh[pl_arp.i % len] == '#') {
            pl_arp_silence();
            pl_arp_sound(pl_arp_next());
            pl_arp.k++;
            pl_arp.off = row * 9 / 10;
        }
        pl_arp.i++;
        pl_arp.t += row > (int32_t)PL_BLOCK ? row : (int32_t)PL_BLOCK;
    }
}
static void pl_arp_block(void)           /* a block on: its steps, a row each at the tempo now */
{
    if (PL_RARE(pl_arp.on))              /* (off: every block costs nothing) */
        pl_arp_run();
}

static void pl_arp_set(uint8_t on, uint8_t rhythm, uint8_t pattern, uint8_t latch)
{
    uint32_t i;
    if (on && !pl_arp.on) {              /* the keys held become its notes (no longer sounding as keys) */
        pl_arp.n = 0;
        for (i = 0; i < pl_nkeys && i < sizeof pl_arp.notes; i++)
            pl_arp.notes[pl_arp.n++] = pl_keys[i];
        for (i = 0; i < PL_NV; i++)
            if (pl_v[i].use == PV_KEY && pl_v[i].who != PL_ARP_WHO && pl_v[i].held)
                pl_v_off(&pl_v[i]);
        pl_arp.k = pl_arp.i = 0;
        pl_arp.t = 0;
        pl_arp.seed = 0x2545F491u;
    }
    if (!on && pl_arp.on)
        pl_arp_silence();
    pl_arp.on = on;
    pl_arp.rhythm = rhythm;
    pl_arp.pattern = pattern;
    pl_arp.latch = latch > 8u ? 8u : latch;
    if (pl_arp.latch) {                  /* (fewer kept: the oldest go; OFF: only the keys held stay) */
        pl_arp_keep(pl_arp.latch);
    } else {
        uint32_t k = 0, j;
        for (i = 0; i < pl_arp.n; i++)
            for (j = 0; j < pl_nkeys; j++)
                if (pl_keys[j] == pl_arp.notes[i]) {
                    pl_arp.notes[k++] = pl_arp.notes[i];
                    break;
                }
        pl_arp.n = (uint8_t)k;
    }
}

/* ---- LOOPS' lanes (player.h): their state. A lanes source (pl_source_t.lanes) plays on the second replay, which keeps
 * the time alone; loop i is its track i, read from a view of the store's loops (pl_lp: instrument, base, repeats, its
 * stream's cursor), taken at the loops' start and again at every edit of them (player_store_watch). Each loop has 4
 * lanes, each holding a note as a key does, its voices the track's with a gen of their own (the lane in bits 0-1, 4 the
 * preview's, the lane's notes counted above), so the track's mix, mute, DUCK and parking treat them as any track's and
 * only the lane ends them. The pattern recorder's preview is a loop more (PL_LPV, its lanes after the loops'), its
 * notes from its grid; RECORD LIVE's keys 4 lanes more (PL_LPK), their notes asked for, each in its instrument, on the
 * keys' voices (PV_KEY, who PL_LNK_WHO + lane: never a key's note). ---- */
#define PL_LPV 16u                       /* the preview's place among the loops */
#define PL_LPK (4u * PL_LPV + 4u)        /* the keys' first lane (player.h's PL_LP_KEYS' 4) */
#define PL_NLN (PL_LPK + 4u)             /* the lanes: 4 a loop, the preview's, the keys' */
#define PL_LNK_WHO 0xF0u                 /* the keys' lanes' voices' who: + lane */
#define PL_LPQ 16u                       /* notes asked for at rows ahead (player_lp_note) */
#define PL_LANES_SRC(c) ((c)->src && (c)->src->lanes)   /* replay c plays a lanes source */
enum { LF_ACC = 1, LF_SLD = 2, LF_LEG = 4 };   /* a note's ACC, its SLD (a TB-303's), its LEG */
typedef struct {
    pl_inst_t in;                        /* its instrument */
    uint8_t base, repeats;               /* its base's rows, its repeat count */
    uint8_t look;                        /* its colour and MONO (with its instrument: another loop in its place) */
    rec_cur_t cur;                       /* its stream's cursor */
} pl_lp_t;
typedef struct {
    uint8_t note;                        /* its note (0: none sounding) */
    uint8_t gen;                         /* its voices' gen */
    uint8_t vnote;                       /* the note its voices started at (a glide bends them from it) */
    uint8_t wait;                        /* a note (or an off) waiting for its EDx: ticks left, 0 none; ... */
    uint8_t wnote, wvol, wfl, wglide;    /* ... its note (REC_NOTE_OFF: an off), volume, LF_*, glide */
    uint8_t koff;                        /* a key off (Kxx) due: ticks left, 0 none */
    uint8_t slide;                       /* a TB-303's: its note slides into the next (SLD) */
    uint8_t glide;                       /* a glide (3xx) under way: its xx (1/16 semitone a tick), 0 none */
    int8_t voice;                        /* a TB-303's voice, -1 none */
    int16_t bend, bend_to;               /* the glide: the pitch now and where it goes, against vnote, 1/64 semitones */
    int16_t gate;                        /* a TB-303's gate: blocks it stays open (< 0: till its next note) */
} pl_ln_t;
static pl_lp_t pl_lp[PL_LPV];
static pl_ln_t pl_ln[PL_NLN];
static uint8_t pl_lpn;                   /* the loops in the view */
static uint32_t pl_lpb[3], pl_lpg[3];    /* the lanes with something due each tick (a wait, a key off, a glide), with a
                                          * TB-303's gate open (counted a block) */
static uint16_t pl_lp_pass;              /* the loops' passes since they started */
static struct { uint8_t ln, note, vol, row, delay, glide; pl_inst_t in; } pl_lpq[PL_LPQ];   /* (in: a keys' lane's) */
static uint8_t pl_lpq_n;
static pl_inst_t pl_lpk_in[4];           /* each keys' lane's instrument: its note's */
static struct { int8_t loop, skip; uint8_t state; } pl_lpt = {-1, -1, PL_TAKE_NONE};   /* the take (PL_TAKE_*); the
                                          * loop whose stream this pass leaves unread */
static struct { uint16_t eff, bm, bs; uint8_t bar, rows; } pl_lpm;   /* the loops silent; a change waiting for the
                                          * bar line: its mutes, solos, the bar's rows */
static struct { uint8_t on, alone, kpos, krow; pl_lp_grid_t g; } pl_lpv;   /* the preview, its grid; alone: the
                                          * loops' place kept */
static pl_lp_grid_t pl_lpv_in;           /* player_lp_preview's (the main loop's: its sequence odd while written) */
static volatile uint32_t pl_lpv_seq;
/* RECORD LIVE's rest (player_lp_rest): on 1 the loops silent while their replay runs on, their place kr; 2 a jump
 * queued (on from their place, or their start: kr), landing at the next row; 3 silent from the next row (zero: their
 * place then their start; kr0, their place given: player_lp_rest_at). Each switch counted (n) with the rows played
 * where it took effect and the loops' row from there (0xFF silent); nat: the row a queued jump takes the place of (the
 * keys' notes asked for it move with it) */
static struct { uint8_t on, kr, kr0, zero, nat, n, row, pend; uint32_t rows; } pl_lpr = {0, 0, 0xFF, 0, 0, 0, 0, 0, 0};
                                         /* (pend: the next pl_start's on: a silent start's 3, its first row silent) */

/* ---- the song through IBXM's replay ---- */
static uint32_t pl_key_of(uint32_t note) { return note < 12u ? 1u : note > 107u ? 96u : note - 11u; }

/* the replay's hook: pattern pat's cell (row, chan) as IBXM's note: the key (MIDI - 11; === key off), an
 * instrument (the replay's one: its default volume as the volume when the cell has none, as IBXM sets an
 * instrument's), the volume (0-127 -> 0-64), the tracker effects (code 0x1E, S3M's FINE VIBRATO: IBXM's
 * 0x95) and one volume-column effect; the other commands are the player's (pl_row_start), from pl_cells */
static void pl_gp_load(int pat)          /* the pattern the replay reads: its tracks' cursors at their starts */
{
    uint32_t t;
    int got = !pl_rec_pattern((uint8_t)pat, &pl_gp);
    if (!got)
        pl_gp.ntrk = 0;
    for (t = 0; t < PL_NCH; t++)
        rec_cur_set(&pl_gp_cur[t], t < pl_gp.ntrk ? pl_gp.trk[t] : 0);
    pl_gp_num = got ? pat : -1;          /* (not there: asked again at the next row; a pattern in ROM may come) */
}
/* LEG on cell c's note (commands.md#legato): legato only onto track chan's latest note, held and sounding, of the
 * row's instrument (the track's until this row: the cell names the same or none) on DX7, JUNO-106, CHIP or a
 * sampler; else a plain note. Not while chasing: the first note heard has its attack */
static __attribute__((noinline)) PLAT_COLD int pl_leg_ok(int chan, const rec_cell_t *c)
{
    const pl_chan_t *ch = &pl_ch[chan];
    uint32_t e, i;
    if (pl_chasing || !(c->has & RC_NOTE) || c->note == REC_NOTE_OFF || !ch->has_ins ||
        ((c->has & RC_INS) && (c->cart != ch->ins.cart || c->slot != ch->ins.slot)))
        return 0;
    e = pl_engine(ch->ins, 0);
    if (e != EN_DX7 && e != EN_JUNO && e != EN_SAMP && e != EN_CHIP)
        return 0;
    for (i = 0; i < PL_NV; i++) {
        const pl_voice_t *v = &pl_v[i];
        if (v->use == PV_SONG && v->who == (uint32_t)chan + pl_tbase && v->gen == ch->gen && v->held && !v->cut &&
            v->inst.cart == ch->ins.cart && v->inst.slot == ch->ins.slot)
            return 1;
    }
    return 0;
}
/* (a cell as IBXM's note, as above: the replay's, and a take's heard at once, pl_take_cell; inlined in both, the
 * replay's reading as it was. LEG, when pl_leg_ok, goes among the effects: IBXM plays it as a slide arriving at once) */
static inline __attribute__((always_inline)) void pl_cell_note(int chan, rec_cell_t *c, struct note *n)
{
    uint32_t k, nfx = 0, leg = 0;
    if (c->has & RC_NOTE)
        n->key = (uint8_t)(c->note == REC_NOTE_OFF ? 97u : pl_key_of(c->note));
    if (pl_src && (c->has & RC_NOTE) && c->note != REC_NOTE_OFF && !(c->has & RC_INS) && pl_ch[chan].has_ins) {
        c->has |= RC_INS;                /* (a source's note plays its track's sound, as an instrument named) */
        c->cart = pl_ch[chan].ins.cart;
        c->slot = pl_ch[chan].ins.slot;
    }
    if ((c->has & RC_INS) && !pl_chasing) {   /* (a chase: the channels' notes are not taken) */
        pl_inst_t in;
        const uint8_t *slot;
        uint32_t vol = 64;
        in.cart = c->cart;
        in.slot = c->slot;
        n->instrument = 1;
        if (pl_engine(in, &slot) == EN_SAMP) {   /* (a sampler's: its instrument's VOLUME, as pl_sample_of gives it) */
            vol = rec_sampler_volume(slot);
            vol = vol > 64u ? 64u : vol;
        }
        n->volume = (uint8_t)(0x10u + vol);
    }
    if (c->has & RC_VOL)
        n->volume = (uint8_t)(0x10u + ((uint32_t)c->vol * 64u + 63u) / 127u);
    for (k = 0; k < c->n; k++) {
        uint32_t code = c->cmd[k][0], v = c->cmd[k][1];
        if (code < 0x24u) {
            code = code == 0x1Eu ? 0x95u : code;
            if (!nfx) {
                n->effect = (uint8_t)code;
                n->param = (uint8_t)v;
            } else if (nfx < 4u) {
                n->fx2[nfx - 1u] = (uint8_t)code;
                n->param2[nfx - 1u] = (uint8_t)v;
            }
            nfx++;
        } else if (code <= 0x2Du && !n->vcol) {
            n->vcol = (uint8_t)((0x60u + (code - 0x24u) * 16u) | (v & 15u));
        } else if (code == 0xD3u) {
            leg = 1;
        }
    }
    if (PL_RARE(leg) && nfx < 4u && pl_leg_ok(chan, c)) {   /* (a cell holds 4 commands, LEG one: a place left) */
        if (!nfx)
            n->effect = 0xD3u;
        else
            n->fx2[nfx - 1u] = 0xD3u;
    }
}
static void pl_cell_get(int pat, int row, int chan, struct note *n)
{
    rec_cell_t *c = &pl_cells[chan];
    if (pat != pl_gp_num)
        pl_gp_load(pat);
    pl_cell_none(c);
    if (chan >= pl_gp.ntrk || !rec_cur_at(&pl_gp_cur[chan], row, c))
        return;
    pl_cell_note(chan, c, n);
}
static PLAT_COLD void pl_get_note(struct module *m, int pat, int row, int chan, struct note *n)
{
    DBG_T(tc);
    (void)m;
    if (PL_RARE(PL_LANES_SRC(pl_cur)))   /* (LOOPS: no cells, the lanes play the notes) */
        return;
    pl_cell_get(pat, row, chan, n);
    PL_DBG_PART(DS_CELLS, tc);           /* (the debug build: REPLAY's reading of the cells) */
}

/* the order played as an IBXM module in IBXM's arena (no samples: one instrument for the replay's rules)
 * (pl_ib_module), and its replay, which starts at its first row, its notes waiting (pl_ib_replay): both at once
 * (pl_ib_build), or a half apart for a DJ deck's start made ready (pl_build_step) */
static uint32_t pl_ib_arena(void) { return pl_cur->k && pl_d2 ? 1u : 0u; }
static PLAT_COLD int pl_ib_module(void)
{
    struct module *m;
    struct sample *s;
    uint32_t i, ntrk = 1;
    {                                    /* (its arena: the second deck's own while DJ lends it, else the first's;
                                          * the other replay in the same one gives it up: it is paused) */
        uint32_t a = pl_ib_arena(), o = 1u - pl_cur->k, ao = o && pl_d2 ? 1u : 0u;
        ibxm_arena_use((int)a);
        if (ao == a) {
            pl_ctxs[o].rp = 0;
            if (pl_ctxs[o].state != PS_IDLE && pl_ctxs[o].state != PS_END)
                pl_ctxs[o].state = PS_IDLE;
        }
    }
    ibxm_arena_reset();
    pl_rp = 0;
    pl_gp_num = -1;
    m = ibxm_alloc(1, sizeof *m);
    if (!m)
        return 0;
    m->num_instruments = 1;
    m->num_patterns = 256;
    m->sequence_len = pl_len;
    m->restart_pos = pl_what == PL_LOOP || pl_src ? 0 : pl_sd.loop_from;
    m->default_gvol = pl_sd.gvol;
    m->default_speed = pl_sd.ticks;
    m->default_tempo = pl_sd.tempo;
    m->c2_rate = 8363;
    m->gain = 64;
    m->linear_periods = (pl_sd.flags & RS_LINEAR) != 0;
    m->fast_vol_slides = (pl_sd.flags & RS_FASTVOL) != 0;
    m->get_note = pl_get_note;
    m->fm1_song = 1;
    m->default_panning = ibxm_alloc(IBXM_MAX_CHANNELS, 1);
    m->sequence = ibxm_alloc(256, 1);
    m->patterns = ibxm_alloc(256, sizeof(struct pattern));
    m->instruments = ibxm_alloc(2, sizeof(struct instrument));
    s = ibxm_alloc(2, sizeof(struct sample));
    if (!m->default_panning || !m->sequence || !m->patterns || !m->instruments || !s)
        return 0;
    for (i = 0; i < IBXM_MAX_CHANNELS; i++)
        m->default_panning[i] = pl_sd.mix[i][RM_PAN];
    for (i = 0; i < 256u; i++) {         /* (a pattern not there: 64 empty rows) */
        m->patterns[i].num_channels = 1;
        m->patterns[i].num_rows = 64;
    }
    {
        uint32_t seen[8] = {0};          /* (each pattern's header read once: no stream walked here) */
        for (i = 0; i < pl_len; i++) {
            uint8_t num = pl_order[i], nt;
            uint16_t rows;
            m->sequence[i] = num;
            if (seen[num >> 5] >> (num & 31u) & 1u)
                continue;
            seen[num >> 5] |= 1u << (num & 31u);
            if (pl_src) {
                rec_pat_t pt;
                if (pl_src->pattern(num, &pt)) {
                    if (!(pl_src->lanes && pl_lpv.on && pl_lpv.g.rows))
                        continue;
                    pt.ntrk = 1;         /* (LOOPS with no loops yet: the preview's own clock) */
                    pt.rows = pl_lpv.g.rows;
                }
                nt = pt.ntrk;
                rows = pt.rows;
            } else if (rec_pattern_size(num, &nt, &rows)) {
                continue;
            }
            m->patterns[num].num_channels = nt;
            m->patterns[num].num_rows = (int)rows;
            if (nt > ntrk)
                ntrk = nt;
        }
    }
    m->num_channels = (int)ntrk;
    for (i = 0; i < 2u; i++) {
        struct instrument *in = &m->instruments[i];
        in->num_samples = 1;
        in->key_to_sample = ibxm_no_keys;
        in->vol_env = in->pan_env = &ibxm_no_env;
        in->samples = &s[i];
        s[i].volume = 64;
    }
    pl_mod = m;
    return 1;
}
static int pl_ib_replay(void)
{
    ibxm_arena_use((int)pl_ib_arena());  /* (its module's arena again: another replay may have been built since) */
    pl_rp = new_replay(pl_mod, PLAT_RATE, 0);
    pl_rp_ended = 0;
    return pl_rp != 0;
}
static int pl_ib_build(void) { return pl_ib_module() && pl_ib_replay(); }

/* a replay's tempo as a share of its song's, a turntable's pitch fader, Q16 (pl_tscale: each replay's); the
 * song's tempo commands act scaled with it. DJ's decks set theirs with player_deck_tempo; songs PLAYBACK's speed
 * (player_tempo_share: pl_ps_share, the main loop's request), each song at that share of its own tempo, 1.0 again
 * when anything but a song starts (pl_start); 1.0 for anything else */
static uint32_t pl_ps_share = 65536u;
/* ... and PLAYBACK's K3, the songs' volume (player_song_volume): dB on each song's own level, its voices alone (not
 * the keys'), -24 to +6, -25 OFF; kept and dropped as the speed is */
static int8_t pl_sv_db;
/* SONGS' play mode (player_end_mode): how a song ends while songs play, PL_END_OWN (its AT THE END) when anything but
 * a song starts; PLAY NEXT's next song (the main loop's, kept up), and the one to start at the next half's start */
static uint8_t pl_end_mode = PL_END_OWN, pl_end_next = 0xFF, pl_chain = 0xFF;
/* the FX page's effect under the cursor (player_fx_mine): the hand's alone. Nothing else turns its knobs or switches
 * it (a song's commands, DJ's FX LFO: pl_fx_turn), and a song's start keeps it as it plays (fx_setup_keep) */
static uint8_t pl_mine_e = 0xFF;
static uint64_t pl_mine_codes;
static void pl_fx_turn(uint32_t code, uint32_t v)
{
    if (code == 0xAEu || code == 0xAFu ? v == pl_mine_e : code >= 0x90u && code <= 0xBFu && (pl_mine_codes >> (code - 0x90u) & 1u))
        return;
    fx_cmd((uint8_t)code, (uint8_t)v);
}
/* what the screen edits (player_editing: PL_ED_*): commands never change it */
static uint8_t pl_ed_what;
static uint32_t pl_ed_key;
static int pl_inst_edited(const pl_chan_t *ch)   /* the track's instrument is being edited: no `*` commands for it */
{
    return pl_ed_what == PL_ED_INST && ch->has_ins && ((uint32_t)ch->ins.cart << 8 | ch->ins.slot) == pl_ed_key;
}
static uint8_t pl_np(const pl_chan_t *ch) { return pl_inst_edited(ch) ? 0u : ch->np; }   /* (its notes as edited) */
static int pl_mix_edited(uint32_t t) { return pl_ed_what == PL_ED_MIX && (pl_ed_key >> (t + pl_tbase) & 1u); }
static int pl_time_edited(void)          /* the replay's song (or the loops') properties open: it plays at them */
{
    if (pl_ed_what != PL_ED_TIME || pl_cur->dj)
        return 0;
    return pl_src ? pl_what == PL_LOOPS && pl_ed_key == PL_ED_LOOPS : pl_what == PL_SONG && pl_ed_key == pl_song;
}
/* the song's mixer as read again (pl_sd) into its tracks as they play: every byte (all), or those that differ from
 * what the tracks last took (pl_handmix: the bytes changed by hand; one a command set and no hand changed stays); a PAN
 * changed into the replay's channel too, whose pan each tick is (fx.md#the-fx-setup: what you change is what you
 * hear) */
static void pl_mix_apply(int all)
{
    uint32_t t, k;
    for (t = 0; t < PL_NCH; t++)
        for (k = 0; k < RM_N; k++) {
            uint8_t v = pl_sd.mix[t][k];
            if (!all && v == pl_handmix[t][k])
                continue;
            pl_ch[t].mix[k] = v;
            if (k == RM_PAN && v != pl_handmix[t][k]) {
                pl_ch[t].pan = v;
                if (pl_rp)
                    replay_fm1_pan(pl_rp, (int)t, v);
            }
            pl_handmix[t][k] = v;
        }
}
/* SETTINGS' KEYS GAIN (player_keys_gain): dB on every note the keys start (MIDI's as the keys too), -24 to +12 */
static volatile int8_t pl_kg_db;
void player_keys_gain(int db) { pl_kg_db = (int8_t)(db < -24 ? -24 : db > 12 ? 12 : db); }
/* SETTINGS' KEYS (player_keys_dry): the keys heard DRY, past every effect (pl_hkmix) */
static volatile uint8_t pl_keys_dry;
void player_keys_dry(int on) { pl_keys_dry = (uint8_t)(on != 0); }
/* FX MIXER's KEYS column (player_keys_mix): the keys' PAN, DUCK amount and sends as a track's mixer bytes (RM_*; their
 * LEVEL is KEYS GAIN), and their DUCK this block (Q15, pl_levels: the song's replay's) */
static uint8_t pl_kstrip[RM_N] = {0x7F, 0x80, 0, 0x7F, 0x7F, 0x7F};
static int32_t pl_kdk = 32768;
static int32_t pl_kpeak;                 /* the keys' voices' loudest this block (DUCK's SOURCE KEYS) */
/* DUCK's level now (Q15: its source's, 4 x its peak at most full, falling over RELEASE), the song's replay's: FX
 * EDIT's DUCK meter */
int32_t player_duck_env(void) { return pl_ctxs[0].duck_env; }
void player_keys_mix(const uint8_t *mix)
{
    uint32_t i;
    for (i = RM_PAN; i < RM_N; i++)
        pl_kstrip[i] = mix[i];
}

/* MIDI's SYNC EXTERNAL (specs/connections.md#clock): the outside clock's tempo, BPM x 10, in place of what
 * plays' own (0: its own); DJ's decks take it through their tempo (the main deck the clock's, its half or double) */
static volatile uint16_t pl_ext_bpm10;
void player_ext_tempo(uint16_t bpm10) { pl_ext_bpm10 = bpm10; }

/* ... and its clocks (player_ext_clock): counted since the master's START or CONTINUE (0: none since), the last
 * one's arrival (plat_ms), the time between two (us). The main loop writes them, odd seq while it does. */
static volatile struct { uint32_t seq, n, at_ms, per_us; } pl_ext;
void player_ext_clock(uint32_t n, uint32_t at_ms, uint32_t per_us)
{
    pl_ext.seq++;
    PL_BARRIER();
    pl_ext.n = n;
    pl_ext.at_ms = at_ms;
    pl_ext.per_us = per_us;
    PL_BARRIER();
    pl_ext.seq++;
}

/* the BPM x 10 a replay plays at in place of its song's own (0: its own): SYNC EXTERNAL's clock; on DJ the main
 * deck the clock's, its half or double, whichever is nearest its song's own (as SYNC picks the right deck's), and
 * a synced right deck that x SYNC's multiple; else a synced right deck's (bpm_fix) */
static uint32_t pl_fix10(void)
{
    const rec_song_t *s = &pl_ctxs[0].sd;
    uint32_t e = pl_ext_bpm10, own, m, best = e, bd = ~0u;
    if (!e)
        return pl_cur->bpm_fix;
    if (!pl_cur->dj)
        return e;
    if (PL_SIDE && !pl_djm.sync)
        return pl_cur->bpm_fix;
    own = (uint32_t)s->tempo * 240u / ((s->ticks ? s->ticks : 6u) * (s->rpb ? s->rpb : 4u));
    for (m = 0; m < 3u; m++) {           /* (x1, x1/2, x2) */
        uint32_t v = m == 1u ? e / 2u : m == 2u ? e * 2u : e, d = v > own ? v - own : own - v;
        if (d < bd) {
            bd = d;
            best = v;
        }
    }
    return !PL_SIDE ? best : pl_djm.sync_mult == 2u ? best * 2u : pl_djm.sync_mult == 1u ? best / 2u : best;
}

/* a tick's frames (Q16) in row `row`: 2.5 s / TEMPO; SWING: an even row's longer, an odd row's shorter */
static uint32_t pl_tick_len(uint32_t tempo, uint32_t row)
{
    /* (x 16, its share taken: DJ's or PLAYBACK's speed, 1.0 elsewhere; whole tempos alone stepped 1 BPM at 6 ticks of
     * 4 rows a beat, so a share of 0.1 BPM went unheard. At 1.0 exactly as ((110250 << 8) / tempo) << 8) */
    uint32_t t16 = (uint32_t)(((uint64_t)tempo * pl_tscale + 2048u) >> 12);
    if (PL_RARE(pl_cur->dj) && pl_dj.hand_n[PL_SIDE]) {   /* (HOLD's hand: a stepped row's ticks at its speed) */
        pl_dj.hand_n[PL_SIDE]--;
        return pl_dj.hand_q16[PL_SIDE];
    }
    t16 = t16 < PL_TEMPO_MIN * 16u ? PL_TEMPO_MIN * 16u : t16 > PL_TEMPO_MAX * 16u ? PL_TEMPO_MAX * 16u : t16;
    uint32_t base = ((110250u << 12) / t16) << 8, sw = pl_sd.swing;
    uint32_t fix = pl_fix10();          /* (SYNC EXTERNAL's clock; DJ's synced deck) */
    if (fix) {                           /* a beat (ROWS A BEAT rows of TICKS A ROW ticks) at that tempo: */
        uint32_t d = fix * (pl_sd.rpb ? pl_sd.rpb : 4u) * (pl_speed ? pl_speed : 6u);   /* (TEMPO x 240) */
        d = d < PL_TEMPO_MIN * 240u ? PL_TEMPO_MIN * 240u : d > PL_TEMPO_MAX * 240u ? PL_TEMPO_MAX * 240u : d;
        base = ((26460000u * 16u) / d) << 12;   /* 44,100 x 600 / d frames, Q16 */
    }
    if (pl_cur->ext_lock && pl_cur->ext_corr)   /* (on the clock: a little shorter or longer, till in place) */
        base = (uint32_t)(((uint64_t)base * (uint32_t)(65536 - pl_cur->ext_corr)) >> 16);
    return sw == 50u ? base : base / 50u * (row & 1u ? 100u - sw : sw);
}

static uint16_t pl_bpm10(void)           /* the current replay's tempo now, BPM x 10 */
{
    uint32_t b = pl_tempo * 240u / ((pl_speed ? pl_speed : 6u) * (pl_sd.rpb ? pl_sd.rpb : 4u)), fix = pl_fix10();
    if (fix)
        return (uint16_t)fix;
    return (uint16_t)(((uint64_t)b * pl_tscale + 32768u) >> 16);
}

static void pl_track_release(uint32_t t) /* track t's sounding notes let go: they ring on in their voices */
{
    uint32_t i;
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_SONG && pl_v[i].who == t + pl_tbase && pl_v[i].held)
            pl_v_off(&pl_v[i]);
}

static void pl_track_follow(uint32_t t)  /* the voices of track t's latest note follow it */
{
    pl_chan_t *ch = &pl_ch[t];
    int32_t dyn = ch->vol * pl_gvol;     /* Q12 */
    uint32_t i;
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        if (v->use != PV_SONG || v->who != t + pl_tbase || v->gen != ch->gen)
            continue;
        pl_v_pitch(v, ch->bend + pl_cur->key64 + v->arp64);   /* (DJ: KEY and a stop's bend on top; a FUSION
                                                               * layer's arpeggio) */
        pl_v_level(v, dyn);
        v->pan = (uint8_t)ch->pan;
    }
}

/* a TB-303 note on track t: one voice a track, as the one 303 it is: a note after a slide glides there
 * (nothing starts again), else it starts again in that voice; its gate closes half a row on, unless it
 * slides into the next note */
static void pl_tb_note(uint32_t t, uint32_t note)
{
    pl_chan_t *ch = &pl_ch[t];
    pl_voice_t *v = ch->voice >= 0 ? &pl_v[ch->voice] : 0;
    uint32_t slide = ch->slide, i;
    ch->slide = ch->row_slide;
    ch->gate = ch->slide ? -1 : (int32_t)((pl_speed * (pl_tick_q16 >> 8)) >> 9);   /* half a row */
    ch->gen++;
    if (v && v->engine == EN_TB && v->use == PV_SONG && v->who == t + pl_tbase && v->inst.cart == ch->ins.cart &&
        v->inst.slot == ch->ins.slot && tb_active(&v->u.tb)) {
        if (slide && v->held) {
            tb_slide_to(&v->u.tb, (int)note, ch->row_acc);
        } else {
            const uint8_t *slot;
            uint8_t k[TB_NK];
            pl_engine(ch->ins, &slot);
            rec_tb_knobs(slot, k);
            for (i = 0; i < pl_np(ch); i++)
                if (ch->pc[i] >= 0x40u && ch->pc[i] < 0x40u + TB_NK)
                    k[ch->pc[i] - 0x40u] = (uint8_t)(ch->pv[i] > 127u ? 127u : ch->pv[i]);
            tb_note_on(&v->u.tb, k, (int)note, ch->row_acc);
        }
        for (i = 0; i < PL_NV; i++)      /* (the track's other notes let go) */
            if (&pl_v[i] != v && pl_v[i].use == PV_SONG && pl_v[i].who == t + pl_tbase && pl_v[i].held)
                pl_v_off(&pl_v[i]);
        v->held = 1;
        v->note = (uint8_t)note;
        v->age = ++pl_age;
        v->gen = ch->gen;
        return;
    }
    pl_track_release(t);
    v = pl_alloc((int)(t + pl_tbase));
    if (!pl_voice_on(v, PV_SONG, t + pl_tbase, ch->ins, (int)note, 127, (uint32_t)ch->pan, ch->row_acc, 4096, ch->pc,
                     ch->pv, pl_np(ch), 0))
        return;
    v->gen = ch->gen;
    ch->voice = (int8_t)(v - pl_v);
}

/* the replay started track t's note (key: IBXM's; the row's own MIDI note when it is that key). (Inlined: the replay's
 * tick keeps it in its loop, the EDITOR's take a copy of its own) */
static inline __attribute__((always_inline)) void pl_trigger(uint32_t t, const struct ibxm_fm1_chan *c)
{
    pl_chan_t *ch = &pl_ch[t];
    uint32_t note = ch->row_note && pl_key_of(ch->row_note) == (uint32_t)c->key ? ch->row_note : (uint32_t)c->key + 11u;
    uint32_t e;
    ch->note = (uint8_t)note;
    ch->npend = 0;
    pl_sf_off((int)(t + pl_tbase), 0);   /* (its FUSION note before: its ticks stop) */
    if (!ch->has_ins)
        return;
    e = pl_engine(ch->ins, 0);
    if (e != EN_NONE && t < 16u)
        fx_clk.fire |= 2u << t;          /* (FILTER's envelope, SOURCE this track: this replay's clock) */
    if (player_note_hook && !PL_SIDE && t < 16u && !(pl_mute >> t & 1u))   /* MIDI OUT (ALL): the song's (the
                                                                     * main deck's) note, its volume as velocity */
        player_note_hook((uint8_t)t, (uint8_t)note, (uint8_t)(ch->vol >= 64 ? 127 : ch->vol < 1 ? 1 : ch->vol * 2));
#ifdef PL_TEST_TRIGGER
    if (e != EN_FUS)
        PL_TEST_TRIGGER(t, note, ch->ins);   /* (the host tests: every note the replay starts; FUSION's layers) */
#endif
    if (e == EN_TB) {
        pl_tb_note(t, note);
        return;
    }
    pl_track_release(t);
    ch->gen++;                           /* (the released ones stay as they are) */
    if (e == EN_FUS) {                   /* its layers: each a voice (EDx: later) */
        rec_cell_t *cells = pl_fcells;
        uint32_t n = pl_fusion_row(ch->ins, note, cells), k, j;
        ch->fus_note = (uint8_t)note;
        for (k = 0; k < n; k++)          /* (E9x, 0xy or Axy in its row: the runner, every tick command) */
            for (j = 0; j < cells[k].n; j++) {
                uint32_t code = cells[k].cmd[j][0], val = cells[k].cmd[j][1];
                if ((code == 0x0Eu && val >> 4 == 0x9u && (val & 15u)) || (code == 0x00u && val) ||
                    (code == 0x0Au && val)) {
                    pl_fn_on(pl_sf, PL_SF, PV_SONG, t + pl_tbase, ch->gen, ch->ins, note, 0xFFu);
                    return;
                }
            }
        for (k = 0; k < n; k++) {
            uint32_t d = pl_layer_delay(&cells[k]);
            pl_voice_t *v;
            if (d && ch->npend < PL_NLAYER) {
                ch->pend[ch->npend][0] = (uint8_t)k;
                ch->pend[ch->npend++][1] = (uint8_t)d;
            } else if (!d && (v = pl_layer_on(PV_SONG, t + pl_tbase, ch->ins, &cells[k], (uint32_t)ch->pan, 0)) != 0) {
                v->gen = ch->gen;
            }
        }
        return;
    }
    if (e == EN_NONE)
        return;
    {
        pl_voice_t *v = pl_alloc((int)(t + pl_tbase));
        if (pl_voice_on(v, PV_SONG, t + pl_tbase, ch->ins, (int)note, 127, (uint32_t)ch->pan, 0, 4096, ch->pc, ch->pv,
                        pl_np(ch), c->offset))
            v->gen = ch->gen;
    }
}

/* a legato note on track t (LEG, pl_leg_ok): the replay moved the track's pitch to it, so pl_track_follow retunes its
 * voices (nothing starts again: no FUSION, no FILTER envelope); the track's note is the new one, a JUNO-106's filter
 * follows the new key, MIDI OUT sends it */
static __attribute__((noinline)) PLAT_COLD void pl_legato(uint32_t t, const struct ibxm_fm1_chan *c)
{
    pl_chan_t *ch = &pl_ch[t];
    uint32_t note = ch->row_note && pl_key_of(ch->row_note) == (uint32_t)c->key ? ch->row_note : (uint32_t)c->key + 11u;
    uint32_t i;
    ch->note = (uint8_t)note;
    if (player_note_hook && !PL_SIDE && t < 16u && !(pl_mute >> t & 1u))   /* (as pl_trigger's) */
        player_note_hook((uint8_t)t, (uint8_t)note, (uint8_t)(ch->vol >= 64 ? 127 : ch->vol < 1 ? 1 : ch->vol * 2));
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_SONG && pl_v[i].who == t + pl_tbase && pl_v[i].gen == ch->gen &&
            pl_v[i].engine == EN_JUNO)
            juno_key(&pl_v[i].u.ju, (int)note);
}

static void pl_pending(uint32_t t)       /* FUSION layers waiting for their EDx: a tick on */
{
    pl_chan_t *ch = &pl_ch[t];
    rec_cell_t *cells = pl_fcells;
    uint32_t i, j, n = 0;
    for (i = j = 0; i < ch->npend; i++) {
        if (--ch->pend[i][1]) {
            ch->pend[j][0] = ch->pend[i][0];
            ch->pend[j++][1] = ch->pend[i][1];
            continue;
        }
        if (!n)
            n = pl_fusion_row(ch->ins, ch->fus_note, cells);
        if (ch->pend[i][0] < n) {
            pl_voice_t *v = pl_layer_on(PV_SONG, t + pl_tbase, ch->ins, &cells[ch->pend[i][0]], (uint32_t)ch->pan,
                                        pl_layer_delay(&cells[ch->pend[i][0]]));
            if (v)
                v->gen = ch->gen;
        }
    }
    ch->npend = (uint8_t)j;
}

/* a parameter command on track t: kept for its next notes (until another instrument), and turned on its
 * sounding ones */
static void pl_param(uint32_t t, uint32_t code, uint32_t val)
{
    pl_chan_t *ch = &pl_ch[t];
    uint32_t i;
    for (i = 0; i < ch->np && ch->pc[i] != code; i++)
        ;
    if (i == ch->np) {
        if (ch->np < PL_NPAR) {
            ch->np++;
        } else {                         /* (full: the oldest goes) */
            for (i = 1; i < PL_NPAR; i++) {
                ch->pc[i - 1u] = ch->pc[i];
                ch->pv[i - 1u] = ch->pv[i];
            }
            i = PL_NPAR - 1u;
        }
        ch->pc[i] = (uint8_t)code;
    }
    ch->pv[i] = (uint8_t)val;
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        if (v->use == PV_SONG && v->who == t + pl_tbase && v->gen == ch->gen && v->engine != EN_FUS)
            pl_param_live(v, code, val);
    }
}

/* the row the replay started: each track's instrument (another one drops its parameters), its parameters,
 * mixer, the FX, its 303's ACC / SLD, the punch-ins (as their keys held that many rows; one at a time, from
 * any track; not while chasing: they are events) */
static void pl_row_start(void)
{
    uint32_t t, k;
    for (t = 0; t < PL_NCH; t++) {
        pl_chan_t *ch = &pl_ch[t];
        const rec_cell_t *c = &pl_cells[t];
        ch->row_note = (uint8_t)((c->has & RC_NOTE) && c->note != REC_NOTE_OFF ? c->note : 0u);
        ch->row_acc = ch->row_slide = 0;
        if (c->has & RC_INS) {
            if (!ch->has_ins || c->cart != ch->ins.cart || c->slot != ch->ins.slot)
                ch->np = 0;
            ch->ins.cart = c->cart;
            ch->ins.slot = c->slot;
            ch->has_ins = 1;
        }
        for (k = 0; k < c->n; k++) {
            uint32_t code = c->cmd[k][0], v = c->cmd[k][1];
            if (code == 0xC0u || code == 0xC2u)
                ch->row_acc = 1;
            if (code == 0xC1u || code == 0xC2u)
                ch->row_slide = 1;
            if (code >= 0x40u && code < 0x80u) {
                if (!pl_inst_edited(ch))      /* (the instrument being edited plays as edited) */
                    pl_param(t, code, v);
            } else if (code >= 0x80u && code <= 0x84u) {   /* the mixer: LEVEL, the SENDS, DUCK (not one being edited) */
                if (!pl_mix_edited(t))
                    ch->mix[code == 0x80u ? RM_LEVEL : code == 0x84u ? RM_DUCK : RM_CHORUS + (code - 0x81u)] =
                        (uint8_t)(v > 127u ? 127u : v);
            }
            else if (code >= 0x90u && code <= 0xBFu) {
                if (pl_cur->dj)          /* (a deck: its own effects; DJ's CHORUS, DELAY, REVERB, LIMITER stay DJ's) */
                    fx_strip_cmd(&pl_dj.strip[PL_SIDE], (uint8_t)code, (uint8_t)v);
                else
                    pl_fx_turn(code, v);
            } else if (code >= 0xC3u && code <= 0xD2u && !pl_chasing &&
                       (!pl_cur->dj || !pl_dj_heard(1 - (int)PL_SIDE))) {   /* (DJ: only while its deck alone
                                                                                * is heard) */
                fx_punch((int)(code - 0xC3u), 1);
                pl_pu_rows = v ? v : 1u;
            }
        }
    }
}

/* ---- the music clock (fx_clk) ---- */
static void pl_clk_at_row(uint32_t r, uint32_t *beat, uint32_t *frac)
{
    uint32_t rpb = pl_sd.rpb ? pl_sd.rpb : 4u, m = r % rpb;
    *beat = r / rpb;
    *frac = m * (0xFFFFFFFFu / rpb + 1u);   /* (m / rpb of a beat, rounded up; no 64-bit divide) */
}

static void pl_clk_row(void)             /* a row started: the clock there */
{
    uint32_t r = pl_rows_seen ? pl_rows_seen - 1u : 0u;
    pl_clk_at_row(r, &fx_clk.beat, &fx_clk.frac);
    pl_clk_at_row(r + 1u, &pl_cap_beat, &pl_cap_frac);
}

static void pl_clk_pace(void)            /* a beat's frames now, a frame's step, the block's frames */
{
    uint32_t rpb = pl_sd.rpb ? pl_sd.rpb : 4u, bf;
    bf = (uint32_t)(((uint64_t)rpb * (pl_speed ? pl_speed : 6u) * pl_tick_q16) >> 16);
    bf = bf ? bf : 1u;
    fx_clk.beat_frames = bf;
    fx_clk.inc = 0xFFFFFFFFu / bf;
    fx_clk.rpb = (uint8_t)rpb;
    fx_clk.hold = PL_BLOCK;
    fx_clk.playing = pl_state == PS_PLAY;
    fx_clk.keys = pl_what == PL_NONE;    /* (THE KEYS' setup held: its tempo the keys') */
    if (pl_state == PS_PLAY) {
        uint64_t now = (uint64_t)fx_clk.beat << 32 | fx_clk.frac, cap = (uint64_t)pl_cap_beat << 32 | pl_cap_frac;
        uint32_t n = cap <= now ? 1u : cap - now > 0xFFFFFFFFu ? PL_BLOCK   /* (a beat or more away) */
                   : (uint32_t)(cap - now - 1u) / fx_clk.inc + 1u;
        fx_clk.hold = n < PL_BLOCK ? n : PL_BLOCK;
    }
}

/* FILTER's envelope's sources (fx_clock_t.fire, held): the notes held now, each replay's tracks' into its clock (a
 * parked note and a muted track's too), the keys' (keys down, or a key's note held) into what plays'; the notes started
 * are marked as they start (pl_trigger; pl_kfire) */
static void pl_env_held(uint32_t prim)
{
    uint32_t h[2] = {0, 0}, keys = pl_nkeys != 0, i, k;
    for (i = 0; i < PL_NV; i++) {
        const pl_voice_t *v = &pl_v[i];
        if (!v->held || v->cut)
            continue;
        if (v->use == PV_KEY)
            keys = 1;
        else if (v->use == PV_SONG && v->who < 2u * PL_NCH)
            h[(v->who >> 4 & 1u) ^ pl_swapped] |= 2u << (v->who & 15u);   /* (by the replay's side) */
    }
    for (k = 0; k < 2u; k++) {
        fx_clock_t *c = pl_cur == &pl_ctxs[k] ? &fx_clk : &pl_ctxs[k].clk;
        c->held = h[k] | (k == prim ? keys : 0u);
        if (k == prim && pl_kfire)
            c->fire |= 1u;
    }
    pl_kfire = 0;
}

/* a block on; a song that plays not past its next row (each replay's every block: one copy of its code) */
static __attribute__((noinline)) void pl_clk_advance(void)
{
    uint32_t f = fx_clk.frac + fx_clk.inc * PL_BLOCK;
    fx_clk.fire = 0;                     /* (FILTER's envelope: this block's notes taken) */
    fx_clk.beat += f < fx_clk.frac;
    fx_clk.frac = f;
    if (pl_state == PS_PLAY &&
        (fx_clk.beat > pl_cap_beat || (fx_clk.beat == pl_cap_beat && fx_clk.frac >= pl_cap_frac))) {
        fx_clk.beat = pl_cap_frac ? pl_cap_beat : pl_cap_beat - 1u;
        fx_clk.frac = pl_cap_frac - 1u;
    }
}

/* ---- LOOPS' lanes: their work (their state above). Only the second replay plays them, the audio context with it
 * (pl_cur), or the store's watch with the audio held off ---- */
static void pl_lbit(uint32_t *m, uint32_t i, int on)   /* lane i's bit in pl_lpb / pl_lpg */
{
    if (on)
        m[i >> 5] |= 1u << (i & 31u);
    else
        m[i >> 5] &= ~(1u << (i & 31u));
}
static uint32_t pl_ln_who(uint32_t i)    /* lane i's voices' track (16-31); a keys' lane's who */
{
    return i >= PL_LPK ? PL_LNK_WHO + (i & 3u) : 16u + (i < 4u * PL_LPV ? i >> 2 : pl_lpv.g.track & 15u);
}
static int pl_ln_mine(const pl_voice_t *v, uint32_t who, uint32_t gen)   /* a voice of a lane's note */
{
    return v->use == (who >= PL_LNK_WHO ? PV_KEY : PV_SONG) && v->who == who && v->gen == gen;
}
static uint16_t pl_lp_silent(uint32_t muted, uint32_t soloed)   /* the loops not heard */
{
    return (uint16_t)(soloed & 0xFFFFu ? ~soloed : muted);
}
static PLAT_COLD void pl_ln_release(uint32_t i)   /* lane i's note let go: it rings on in its voices */
{
    uint32_t who = pl_ln_who(i), gen = pl_ln[i].gen, k;
    if (who >= PL_LNK_WHO) {             /* (a keys' lane's: every voice of its who, a FUSION's layers too) */
        for (k = 0; k < PL_NV; k++)
            if (pl_v[k].held && pl_v[k].use == PV_KEY && pl_v[k].who == who)
                pl_v_off(&pl_v[k]);
        pl_kf_off((int)who);
        return;
    }
    for (k = 0; k < PL_NV; k++)
        if (pl_v[k].held && pl_ln_mine(&pl_v[k], who, gen))
            pl_v_off(&pl_v[k]);
    pl_sf_lane_off(i);
}
static void pl_ln_off(uint32_t i)        /* lane i's note off (a note off, a key off) */
{
    pl_ln_t *ln = &pl_ln[i];
    pl_ln_release(i);
    ln->note = 0;
    ln->slide = ln->glide = 0;
    ln->gate = -1;
    pl_lbit(pl_lpg, i, 0);
}
static void pl_ln_end(uint32_t i)        /* lane i at rest: its note let go, nothing of it due */
{
    pl_ln_off(i);
    pl_ln[i].wait = pl_ln[i].koff = 0;
    pl_lbit(pl_lpb, i, 0);
}
static void pl_lp_stop(uint32_t lo)      /* loop lo's lanes at rest (its repeats over, a cut repeat's end, a take's) */
{
    uint32_t l;
    for (l = 0; l < 4u; l++)
        pl_ln_end(4u * lo + l);
}
/* (the songs' functions call the lanes' under PL_RARE, never inlined: their code out of the songs' way) */
static __attribute__((noinline)) void pl_lp_rest(void)   /* every lane at rest, nothing due (the loops' voices let go
                                                          * by the caller, the keys' lanes' here) */
{
    uint32_t i;
    for (i = PL_LPK; i < PL_NLN; i++)
        pl_ln_release(i);
    for (i = 0; i < PL_NLN; i++) {
        pl_ln_t *ln = &pl_ln[i];
        uint8_t gen = ln->gen;
        memset(ln, 0, sizeof *ln);
        ln->gen = gen;
        ln->voice = -1;
        ln->gate = -1;
    }
    memset(pl_lpb, 0, sizeof pl_lpb);
    memset(pl_lpg, 0, sizeof pl_lpg);
    pl_lpq_n = 0;
}

static void pl_ln_glide(uint32_t i)      /* a tick of lane i's glide: xx / 16 semitone on toward its note */
{
    pl_ln_t *ln = &pl_ln[i];
    int32_t d = ln->bend_to - ln->bend, s = 4 * (int32_t)ln->glide;
    uint32_t who = pl_ln_who(i), k;
    ln->bend = (int16_t)(d > s ? ln->bend + s : d < -s ? ln->bend - s : ln->bend_to);
    if (ln->bend == ln->bend_to)
        ln->glide = 0;
    for (k = 0; k < PL_NV; k++)
        if (pl_ln_mine(&pl_v[k], who, ln->gen))
            pl_v_pitch(&pl_v[k], ln->bend + pl_v[k].arp64);
}

/* a TB-303 lane's note, as a track's (pl_tb_note): one voice the lane's, a note after a slide glides there (nothing
 * starts again), else it starts again in that voice; its gate closes half a row on, unless it slides into the next.
 * dyn: its level, as the track's volume gives it; < 0 a keys' lane's, a key's voice at velocity -dyn */
static PLAT_COLD void pl_ln_tb(uint32_t i, pl_inst_t in, const uint8_t *slot, uint32_t note, uint32_t fl, uint32_t glide,
                     int32_t dyn)
{
    pl_ln_t *ln = &pl_ln[i];
    const pl_ctx_t *c = &pl_ctxs[1];
    pl_voice_t *v = ln->voice >= 0 ? &pl_v[ln->voice] : 0;
    uint32_t who = pl_ln_who(i), slide = ln->slide || glide, acc = fl & LF_ACC ? 1u : 0u, old = ln->gen, k, key = dyn < 0;
    ln->slide = (uint8_t)(fl & LF_SLD ? 1u : 0u);
    ln->gate = (int16_t)(ln->slide ? -1 : (int32_t)(((c->speed * (c->tick_q16 >> 8)) >> 15) + 1u));   /* (half a row
                                          * in blocks: a track's gate in frames, closed in the same block) */
    pl_lbit(pl_lpg, i, ln->gate >= 0);
    ln->note = ln->vnote = (uint8_t)note;
    ln->bend = ln->bend_to = 0;
    ln->glide = 0;
    if (v && v->engine == EN_TB && pl_ln_mine(v, who, old) && v->inst.cart == in.cart && v->inst.slot == in.slot &&
        tb_active(&v->u.tb)) {
        ln->gen = (uint8_t)(old + 8u);
        if (slide && v->held) {
            tb_slide_to(&v->u.tb, (int)note, (int)acc);
        } else {
            uint8_t kn[TB_NK];
            rec_tb_knobs(slot, kn);
            tb_note_on(&v->u.tb, kn, (int)note, (int)acc);
        }
        for (k = 0; k < PL_NV; k++)      /* (the lane's other notes let go) */
            if (&pl_v[k] != v && pl_v[k].held && pl_ln_mine(&pl_v[k], who, old))
                pl_v_off(&pl_v[k]);
        v->held = 1;
        v->note = (uint8_t)note;
        v->age = ++pl_age;
        v->gen = ln->gen;
    } else {
        pl_ln_release(i);
        ln->gen = (uint8_t)(old + 8u);
        v = pl_alloc(key ? -1 : (int)who);
        if (!pl_voice_on(v, key ? PV_KEY : PV_SONG, who, in, (int)note, key ? (uint32_t)-dyn : 127u,
                         key ? 128u : (uint32_t)c->ch[who & 15u].pan, acc, 4096, 0, 0, 0, 0)) {
            ln->voice = -1;
            return;
        }
        v->gen = ln->gen;
        ln->voice = (int8_t)(v - pl_v);
    }
    if (!key)
        pl_v_level(v, dyn);
    pl_v_pitch(v, 0);
}

/* lane i's note legato (LEG, commands.md#legato) to note, onto its note held in instrument in (DX7, JUNO-106, CHIP or a
 * sampler: the caller's): its voices retuned there at once (the glide's bend, arrived), a JUNO-106's filter following
 * the new key; dyn >= 0 their level (a loop's lane's, as its glide's). 0 when it holds none: a plain note then */
static __attribute__((noinline)) PLAT_COLD int pl_ln_leg(uint32_t i, pl_inst_t in, uint32_t note, int32_t dyn)
{
    pl_ln_t *ln = &pl_ln[i];
    uint32_t who = pl_ln_who(i), k, n = 0;
    for (k = 0; k < PL_NV; k++)
        n += pl_v[k].held && !pl_v[k].cut && pl_ln_mine(&pl_v[k], who, ln->gen) && pl_v[k].inst.cart == in.cart &&
             pl_v[k].inst.slot == in.slot;
    if (!n)
        return 0;
    ln->note = (uint8_t)note;
    ln->bend = ln->bend_to = (int16_t)(((int32_t)note - ln->vnote) * 64);
    ln->glide = 0;
    for (k = 0; k < PL_NV; k++)
        if (pl_ln_mine(&pl_v[k], who, ln->gen)) {
            if (dyn >= 0 && pl_v[k].held)
                pl_v_level(&pl_v[k], dyn);
            pl_v_pitch(&pl_v[k], ln->bend + pl_v[k].arp64);
            if (pl_v[k].engine == EN_JUNO)
                juno_key(&pl_v[k].u.ju, (int)note);
        }
    return 1;
}

/* a keys' lane's next note (RECORD LIVE's): as a key's (pl_key_on: its volume the velocity, the keys' strip), on the
 * lane: only the lane's note before it let go; a glide, a legato and a TB-303's slide as a loop's lane's */
static __attribute__((noinline)) PLAT_COLD void pl_lnk_note(uint32_t i, uint32_t note, uint32_t vol, uint32_t fl, uint32_t glide)
{
    pl_ln_t *ln = &pl_ln[i];
    pl_inst_t in = pl_lpk_in[i & 3u];
    uint32_t who = pl_ln_who(i), vel = vol >= 1u && vol <= 127u ? vol : 127u, e, k, n;
    const uint8_t *slot;
    pl_voice_t *v;
    if (note < 12u || note > 127u || (e = pl_engine(in, &slot)) == EN_NONE)
        return;
    pl_alloc_k = -1;                     /* (the keys ask) */
    pl_kfire = 1;                        /* (FILTER's envelope, SOURCE KEYS) */
#ifdef PL_TEST_LANE
    PL_TEST_LANE(i, note, vel, glide);   /* (the host tests: every note a lane starts) */
#endif
    if (e == EN_TB) {
        pl_ln_tb(i, in, slot, note, fl, glide, -(int32_t)vel);
        return;
    }
    if ((fl & LF_LEG) && ln->note && e != EN_FUS && pl_ln_leg(i, in, note, -1))
        return;
    if (glide && ln->note && e != EN_FUS) {   /* a glide: the lane's note slides there, its voices going on */
        for (k = n = 0; k < PL_NV; k++)
            n += pl_v[k].held && pl_ln_mine(&pl_v[k], who, ln->gen);
        if (n) {
            ln->note = (uint8_t)note;
            ln->bend_to = (int16_t)(((int32_t)note - ln->vnote) * 64);
            ln->glide = (uint8_t)glide;
            pl_lbit(pl_lpb, i, 1);
            return;
        }
    }
    pl_ln_release(i);
    ln->gen = (uint8_t)(ln->gen + 8u);
    ln->note = ln->vnote = (uint8_t)note;
    ln->bend = ln->bend_to = 0;
    ln->glide = 0;
    if (e == EN_FUS) {                   /* (its layers as a key's: their ticks from now) */
        pl_fn_on(pl_kf, PL_KF, PV_KEY, who, 0, in, note, 0xFFu);
        return;
    }
    v = pl_alloc(-1);
    if (pl_voice_on(v, PV_KEY, who, in, (int)note, vel, 128, 0, 4096, 0, 0, 0, 0)) {
        v->gen = ln->gen;
        if (pl_key_bend)
            pl_v_pitch(v, pl_key_bend);  /* (MIDI's pitch bend, as it stands) */
    }
}

/* lane i's next note (12-127; vol 00-7F, 0xFF the instrument's own; LF_* flags; a glide's xx), started as a track's is
 * (pl_trigger), on the lane: only the lane's note before it let go */
static void pl_ln_note(uint32_t i, uint32_t note, uint32_t vol, uint32_t fl, uint32_t glide)
{
    pl_ln_t *ln = &pl_ln[i];
    pl_ctx_t *c = &pl_ctxs[1];
    uint32_t who = pl_ln_who(i), t = who & 15u, e, k, n;
    pl_chan_t *ch = &c->ch[t];
    pl_inst_t in = i < 4u * PL_LPV ? pl_lp[i >> 2].in : pl_lpv.g.inst;
    const uint8_t *slot;
    int32_t dyn;
    pl_voice_t *v;
    if (note < 12u || note > 127u || (e = pl_engine(in, &slot)) == EN_NONE)
        return;
    k = vol <= 127u ? (vol * 64u + 63u) / 127u : e == EN_SAMP ? rec_sampler_volume(slot) : 64u;   /* (as the replay
                                          * takes a cell's volume, or an instrument's own) */
    ch->vol = (int32_t)(k > 64u ? 64u : k);   /* (the track's volume: its last note's, as a song track's; a FUSION
                                               * note's tick layers take it) */
    ch->note = (uint8_t)note;
    dyn = ch->vol * c->gvol;
    fx_clk.fire |= 2u << t;              /* (FILTER's envelope, SOURCE this track) */
    pl_alloc_k = 1;
#ifdef PL_TEST_LANE
    PL_TEST_LANE(i, note, ch->vol, glide);   /* (the host tests: every note a lane starts) */
#endif
    if (e == EN_TB) {
        pl_ln_tb(i, in, slot, note, fl, glide, dyn);
        return;
    }
    if (PL_RARE(fl & LF_LEG) && ln->note && e != EN_FUS && pl_ln_leg(i, in, note, dyn))
        return;                          /* (a legato: there at once, as below's glide arrived) */
    if (glide && ln->note && e != EN_FUS) {   /* a glide: the lane's note slides there, its voices going on */
        for (k = n = 0; k < PL_NV; k++)
            if (pl_v[k].held && pl_ln_mine(&pl_v[k], who, ln->gen)) {
                pl_v_level(&pl_v[k], dyn);
                n++;
            }
        if (n) {
            ln->note = (uint8_t)note;
            ln->bend_to = (int16_t)(((int32_t)note - ln->vnote) * 64);
            ln->glide = (uint8_t)glide;
            pl_lbit(pl_lpb, i, 1);
            return;
        }
    }
    pl_ln_release(i);
    ln->gen = (uint8_t)(ln->gen + 8u);
    ln->note = ln->vnote = (uint8_t)note;
    ln->bend = ln->bend_to = 0;
    ln->glide = 0;
    if (e == EN_FUS) {                   /* its layers, each a voice of the lane; a row with tick commands or delays runs
                                          * among the songs' FUSION notes (pl_sf), as the lane's own */
        rec_cell_t *cells = pl_fcells;
        uint32_t j, run = 0;
        n = pl_fusion_row(in, note, cells);
        for (k = 0; k < n && !run; k++)
            for (j = 0; j < cells[k].n; j++) {
                uint32_t code = cells[k].cmd[j][0], val = cells[k].cmd[j][1];
                run |= (code == 0x0Eu && (val >> 4 == 0x9u || val >> 4 == 0xDu) && (val & 15u)) ||
                       (code == 0x00u && val) || (code == 0x0Au && val);
            }
        if (run) {
            pl_fn_on(pl_sf, PL_SF, PV_SONG, who, ln->gen, in, note, i);
            return;
        }
        for (k = 0; k < n; k++)
            if ((v = pl_layer_on(PV_SONG, who, in, &cells[k], (uint32_t)ch->pan, 0)) != 0) {
                v->gen = ln->gen;
                pl_v_level(v, dyn);
                pl_v_pitch(v, 0);
            }
        return;
    }
    v = pl_alloc((int)who);
    if (pl_voice_on(v, PV_SONG, who, in, (int)note, 127, (uint32_t)ch->pan, 0, 4096, 0, 0, 0, 0)) {
        v->gen = ln->gen;
        pl_v_level(v, dyn);
        pl_v_pitch(v, 0);
    }
}
static PLAT_COLD void pl_ln_fire(uint32_t i, uint32_t note, uint32_t vol, uint32_t fl, uint32_t glide)
{
    if (note == REC_NOTE_OFF)
        pl_ln_off(i);
    else if (i >= PL_LPK)
        pl_lnk_note(i, note, vol, fl, glide);
    else
        pl_ln_note(i, note, vol, fl, glide);
}
/* lane i's next event (a cell's, a take's): what waited replaced; after `delay` ticks (EDx), else now */
static void pl_ln_event(uint32_t i, uint32_t note, uint32_t vol, uint32_t fl, uint32_t glide, uint32_t delay)
{
    pl_ln_t *ln = &pl_ln[i];
    ln->wait = ln->koff = 0;
    if (delay) {
        ln->wait = (uint8_t)delay;
        ln->wnote = (uint8_t)note;
        ln->wvol = (uint8_t)vol;
        ln->wfl = (uint8_t)fl;
        ln->wglide = (uint8_t)glide;
        pl_lbit(pl_lpb, i, 1);
        return;
    }
    pl_ln_fire(i, note, vol, fl, glide);
}
/* lane i's cell at its row: a note or a note off (EDx: that many ticks late; past the row's ticks never, as in the
 * trackers), its volume, a key off (Kxx: at that tick of the row), a glide (3xx; 300 as 320), ACC, SLD and LEG */
static void pl_ln_cell(uint32_t i, const rec_cell_t *c)
{
    uint32_t k, delay = 0, koff = 0xFFu, glide = 0, fl = 0, sp = pl_ctxs[1].speed ? pl_ctxs[1].speed : 6u;
    for (k = 0; k < c->n; k++) {
        uint32_t code = c->cmd[k][0], v = c->cmd[k][1];
        if (code == 0x0Eu && v >> 4 == 0xDu)
            delay = v & 15u;
        else if (code == 0x14u)
            koff = v;
        else if (code == 0x03u)
            glide = v ? v : 0x20u;
        else if (code >= 0xC0u && code <= 0xC2u)
            fl |= code == 0xC0u ? (uint32_t)LF_ACC : code == 0xC1u ? (uint32_t)LF_SLD : (uint32_t)(LF_ACC | LF_SLD);
        else if (code == 0xD3u)
            fl |= LF_LEG;
    }
    if ((c->has & RC_NOTE) && delay < sp)
        pl_ln_event(i, c->note, c->has & RC_VOL ? c->vol : 0xFFu, fl, glide, delay);
    if (koff < sp) {
        if (!koff) {
            pl_ln_off(i);
        } else {
            pl_ln[i].koff = (uint8_t)koff;
            pl_lbit(pl_lpb, i, 1);
        }
    }
}

static void pl_lp_tick(void)             /* a tick on: the lanes' glides, waiting notes and key offs */
{
    uint32_t w, j, i;
    for (w = 0; w < 3u; w++)
        for (j = 0; j < 32u && pl_lpb[w] >> j; j++) {
            pl_ln_t *ln;
            if (!(pl_lpb[w] >> j & 1u))
                continue;
            i = 32u * w + j;
            ln = &pl_ln[i];
            if (ln->glide)
                pl_ln_glide(i);
            if (ln->wait && !--ln->wait)
                pl_ln_fire(i, ln->wnote, ln->wvol, ln->wfl, ln->wglide);
            if (ln->koff && !--ln->koff)
                pl_ln_off(i);
            if (!ln->wait && !ln->koff && !ln->glide)
                pl_lbit(pl_lpb, i, 0);
        }
}
static __attribute__((noinline)) void pl_lp_gates(void)   /* a block on: the TB-303 lanes' gates (closed half a row
                                                           * on) */
{
    uint32_t w, j, i;
    for (w = 0; w < 3u; w++)
        for (j = 0; j < 32u && pl_lpg[w] >> j; j++) {
            pl_ln_t *ln;
            pl_voice_t *v;
            if (!(pl_lpg[w] >> j & 1u))
                continue;
            i = 32u * w + j;
            ln = &pl_ln[i];
            if (--ln->gate > 0)
                continue;
            ln->gate = -1;
            pl_lbit(pl_lpg, i, 0);
            v = ln->voice >= 0 ? &pl_v[ln->voice] : 0;
            if (v && v->engine == EN_TB && pl_ln_mine(v, pl_ln_who(i), ln->gen))
                pl_v_off(v);
        }
}

/* loop lo's cells at its base's row br (lane l at its stream's row 4 br + l) */
static void pl_lp_read(uint32_t lo, uint32_t br)
{
    rec_cur_t *c = &pl_lp[lo].cur;
    rec_cell_t cell;
    uint32_t s = 4u * br, l;
    if (!c->s || ((int)s > c->asked && c->row > (int)s + 3))   /* (no cell on these rows: the usual way) */
        return;
    for (l = 0; l < 4u; l++)
        if (rec_cur_at(c, (int)(s + l), &cell))
            pl_ln_cell(4u * lo + l, &cell);
}
/* the preview's row r of the loops (len rows; wrap: their start again): its steps from their start, repeated over
 * their length (with no loops: TIMES times in its own clock), the last repeat cut there */
static void pl_lpv_row(uint32_t r, uint32_t len, int wrap)
{
    const pl_lp_grid_t *g = &pl_lpv.g;
    uint32_t sr = g->step_rows ? g->step_rows : 1u, steps = g->length < 1u ? 1u : g->length > 16u ? 16u : g->length;
    uint32_t base = sr * steps, reps = pl_lpn ? (len + base - 1u) / base : g->times ? g->times : 1u, end = base * reps;
    uint32_t br, k, j;
    if (wrap && end > len && len % base)
        pl_lp_stop(PL_LPV);
    if (r >= end) {
        if (r == end)
            pl_lp_stop(PL_LPV);
        return;
    }
    br = r % base;
    if (br % sr)
        return;
    k = br / sr;
    for (j = 0; j < 4u; j++) {
        if (g->steps[j] >> k & 1u)
            pl_ln_event(4u * PL_LPV + j, g->note[j], g->vol, 0, 0, 0);
        else if (!g->ring && (g->steps[j] >> (k ? k - 1u : steps - 1u) & 1u))
            pl_ln_event(4u * PL_LPV + j, REC_NOTE_OFF, 0, 0, 0, 0);   /* (A STEP LONG: a step after its note) */
    }
}
/* RECORD LIVE's switch at row r of the replay (pl_lpr.on 3: the loops silent from here, their voices let go; 2: a
 * jump landed, the loops on from here, the keys' notes asked for the row it took the place of here), counted */
static __attribute__((noinline)) PLAT_COLD void pl_lpr_row(uint32_t r)
{
    uint32_t i;
    if (pl_lpr.on == 3u) {
        for (i = 0; i < pl_lpn; i++)
            pl_lp_stop(i);
        pl_lpr.kr = (uint8_t)(pl_lpr.zero ? 0u : pl_lpr.kr0 != 0xFFu ? pl_lpr.kr0 : r);
        pl_lpr.kr0 = 0xFF;
        pl_lpr.on = 1;
        pl_lpr.row = 0xFF;
    } else {
        for (i = 0; i < pl_lpq_n; i++)
            if (pl_lpq[i].row == pl_lpr.nat)
                pl_lpq[i].row = (uint8_t)r;
        pl_lpr.on = 0;
        pl_lpr.row = (uint8_t)r;
    }
    pl_lpr.zero = 0;
    pl_lpr.rows = pl_rows_seen;
    pl_lpr.n++;
}
/* row r of the loops started (wrap: their start again, a pass more): the take's turn, a mute change waiting for this
 * bar line, each loop's cells at its base's row while its repeats last (then it rests; a repeat the length cut stops
 * at the wrap; none while RECORD LIVE's rest silences them), the preview's, the notes asked for at this row */
static PLAT_COLD void pl_lp_row(uint32_t r, int wrap)
{
    uint32_t len = (uint32_t)pl_mod->patterns[pl_order[0]].num_rows, i, k, j;
    if (PL_RARE(pl_lpr.on >= 2u))
        pl_lpr_row(r);
    if (wrap) {
        pl_lp_pass++;
        pl_lpt.skip = -1;
        if (pl_lpt.state == PL_TAKE_RUNNING) {    /* (the take's pass over: what it holds ends at the length's end) */
            pl_lpt.state = PL_TAKE_DONE;
            pl_lp_stop((uint32_t)pl_lpt.loop);
        } else if (pl_lpt.state == PL_TAKE_ARMED && !pl_lpv.alone) {
            pl_lpt.state = PL_TAKE_RUNNING;
            pl_lpt.skip = pl_lpt.loop;
        }
    }
    if (pl_lpm.bar && r % pl_lpm.rows == 0u) {
        pl_lpm.eff = pl_lp_silent(pl_lpm.bm, pl_lpm.bs);
        pl_lpm.bar = 0;
        pl_mute = pl_lpm.eff;
    }
    for (i = 0; i < pl_lpn && !pl_lpv.alone && pl_lpr.on != 1u; i++) {
        pl_lp_t *p = &pl_lp[i];
        uint32_t base = p->base ? p->base : 1u, end = base * p->repeats;
        if ((int)i == pl_lpt.skip)       /* (the take's pass: its notes come from player_lp_note) */
            continue;
        if (wrap && end > len && len % base)
            pl_lp_stop(i);
        if (r >= end) {
            if (r == end)
                pl_lp_stop(i);
            continue;
        }
        pl_lp_read(i, r % base);
    }
    if (PL_RARE(pl_lpv.on) && !pl_lpv.g.mute)
        pl_lpv_row(r, len, wrap);
    for (k = j = 0; k < pl_lpq_n; k++) { /* (the notes asked for at this row, in their order) */
        if (pl_lpq[k].row == r) {
            if (pl_lpq[k].ln >= PL_LPK && pl_lpq[k].note != REC_NOTE_OFF)
                pl_lpk_in[pl_lpq[k].ln & 3u] = pl_lpq[k].in;
            pl_ln_event(pl_lpq[k].ln, pl_lpq[k].note, pl_lpq[k].vol, pl_lpq[k].delay & 16u ? (uint32_t)LF_LEG : 0u,
                        pl_lpq[k].glide, pl_lpq[k].delay & 15u);   /* (delay's 16: a legato) */
        } else {
            pl_lpq[j++] = pl_lpq[k];
        }
    }
    pl_lpq_n = (uint8_t)j;
}
static __attribute__((noinline)) PLAT_COLD void pl_lp_after(int row, uint32_t r)   /* after the lanes source's tick (row: it
                                                                          * started row r) */
{
    if (pl_chasing)                      /* (a chase: no notes; the cursors find their rows when asked) */
        return;
    if (pl_lpb[0] | pl_lpb[1] | pl_lpb[2])
        pl_lp_tick();
    if (row)
        pl_lp_row(r, r == 0u && pl_rows_seen > 1u);
}

/* the view of the loops read again from the store (rec_loops: their table, each one's stream); keep: each cursor stays
 * on its row (an edit while they play: heard from the next row), and a loop whose place another took (its instrument,
 * colour or MONO changed: a peel, a clear) or whose notes all went (a re-record) stops what its lanes hold */
static PLAT_COLD void pl_lp_load(int keep)
{
    rec_loops_t l;
    rec_loop_t e = {0};
    uint32_t i, n = rec_loops(&l, 1) ? 0u : l.n;
    for (i = 0; i < PL_LPV; i++) {
        pl_lp_t *p = &pl_lp[i];
        uint8_t look = 0;
        if (i < n) {
            rec_loop(&l, (int)i, &e);
            look = (uint8_t)(e.colour | e.mono << 4);
        }
        if (keep && i < pl_lpn && (i >= n || p->in.cart != e.cart || p->in.slot != e.slot || p->look != look ||
                                   (p->cur.s && !l.trk[i])))
            pl_lp_stop(i);
        if (i >= n) {
            rec_cur_set(&p->cur, 0);
            continue;
        }
        p->in.cart = e.cart;
        p->in.slot = e.slot;
        p->base = e.base;
        p->repeats = e.repeats;
        p->look = look;
        if (keep && i < pl_lpn)
            rec_cur_reseat(&p->cur, l.trk[i], l.trk_end[i]);
        else
            rec_cur_set_end(&p->cur, l.trk[i], l.trk_end[i]);
    }
    pl_lpn = (uint8_t)n;
}
/* the store's watch for LOOPS (the audio held off), reading no more than the edit changed: a loop's stream rewritten or
 * moved (its cursor onto it, on its row; all its notes gone: what its lanes hold stops); the head rewritten (a loop
 * new, gone or another in its place, its base, its repeats); else (a stream back as saved, REVERT, a save) the loops
 * read whole again, which walks every stream the flash holds */
static void pl_lp_watch(uint32_t id)
{
    rec_loops_t l;
    rec_loop_t e = {0};
    const uint8_t *s;
    uint32_t len, i;
    if (id < PL_LPV) {
        if (id >= pl_lpn)                /* (a loop the view has not yet: its head brings it) */
            return;
        if (!(s = store_piece(REC_LOOPS, 0, (uint16_t)id, &len))) {
            pl_lp_load(1);
            return;
        }
        if (len > 1u) {
            rec_cur_reseat(&pl_lp[id].cur, s, s + len);
        } else {
            if (pl_lp[id].cur.s)
                pl_lp_stop(id);
            rec_cur_set(&pl_lp[id].cur, 0);
        }
        return;
    }
    if (id != STORE_PC_HEAD) {
        if (id != STORE_PC_PHEAD)
            pl_lp_load(1);
        return;
    }
    if (rec_loops(&l, 0)) {
        pl_lp_load(1);
        return;
    }
    for (i = 0; i < PL_LPV; i++) {
        pl_lp_t *p = &pl_lp[i];
        uint8_t look;
        if (i >= l.n) {
            if (i < pl_lpn) {
                pl_lp_stop(i);
                rec_cur_set(&p->cur, 0);
            }
            continue;
        }
        rec_loop(&l, (int)i, &e);
        look = (uint8_t)(e.colour | e.mono << 4);
        if (i < pl_lpn && (p->in.cart != e.cart || p->in.slot != e.slot || p->look != look))
            pl_lp_stop(i);
        p->in.cart = e.cart;
        p->in.slot = e.slot;
        p->base = e.base;
        p->repeats = e.repeats;
        p->look = look;
        if (i >= pl_lpn) {               /* (a loop new to the view: its stream, a piece) */
            if (!(s = store_piece(REC_LOOPS, 0, (uint16_t)i, &len))) {
                pl_lp_load(1);
                return;
            }
            rec_cur_set_end(&p->cur, len > 1u ? s : 0, len > 1u ? s + len : 0);
        }
    }
    pl_lpn = l.n;
}
/* the lanes source starts (pl_start_tail; the voices let go before): the view read, every lane at rest, an armed take
 * running from here, the loops' mutes */
static __attribute__((noinline)) void pl_lp_start(void)
{
    pl_lp_load(0);
    pl_lp_rest();
    pl_lp_pass = 0;
    pl_lpt.skip = -1;
    if ((pl_lpt.state == PL_TAKE_ARMED || pl_lpt.state == PL_TAKE_RUNNING) && !pl_lpv.alone) {
        pl_lpt.state = PL_TAKE_RUNNING;
        pl_lpt.skip = pl_lpt.loop;
    }
    pl_mute = pl_lpm.eff;
}
static __attribute__((noinline)) void pl_lp_changed(void)   /* the loops changed (player_source_changed; the view is
                                                             * the watch's): their voices' pan as their mix has it */
{
    uint32_t k;
    for (k = 0; k < PL_NV; k++)
        if (pl_v[k].use == PV_SONG && (pl_v[k].who >> 4) == 1u)
            pl_v[k].pan = (uint8_t)pl_ctxs[1].ch[pl_v[k].who & 15u].pan;
}

/* ---- the transport ---- */
static void pl_tracks_reset(void)        /* the tracks as at a song's start (no instrument yet, its mixer) */
{
    uint32_t c;
    for (c = 0; c < PL_NCH; c++) {
        pl_chan_t *ch = &pl_ch[c];
        ch->has_ins = 0;
        ch->note = ch->row_note = 0;
        ch->voice = -1;
        ch->slide = ch->row_slide = ch->row_acc = 0;
        ch->gate = -1;
        ch->np = ch->npend = 0;
        ch->bend = 0;
        ch->vol = 64;
        ch->pan = pl_sd.mix[c][RM_PAN];
        memcpy(ch->mix, pl_sd.mix[c], RM_N);
        memcpy(pl_handmix[c], pl_sd.mix[c], RM_N);
        ch->level = (int32_t)ch->mix[RM_LEVEL] * 4096 / 127;
        ch->peak = 0;
    }
    pl_sf_off(-1, pl_cur->k);            /* (its FUSION notes' ticks) */
}

static PLAT_COLD void pl_song_release(void)   /* the song's notes let go (they ring out) */
{
    uint32_t i;
    for (i = 0; i < PL_NV; i++)          /* (this replay's: its tracks') */
        if (pl_v[i].use == PV_SONG && pl_v[i].held && (pl_v[i].who >> 4 & 1u) == pl_cur->k)
            pl_v_off(&pl_v[i]);
    for (i = 0; i < PL_NCH; i++)
        pl_ch[i].npend = 0;
    pl_sf_off(-1, pl_cur->k);            /* (its FUSION notes' ticks stop) */
    if (PL_RARE(PL_LANES_SRC(pl_cur)))   /* (LOOPS: every lane at rest) */
        pl_lp_rest();
}

/* the song stopped at its end (AT THE END = STOP): its notes let go and ring out; PLAY plays it again */
static void pl_song_end(void)
{
    pl_song_release();
    if (!pl_cur->dj)                     /* (DJ: the punch-ins are the mix's, not a deck's) */
        fx_punch_stop();
    pl_pu_rows = 0;
    pl_state = PS_END;
    pl_ended = 1;
    pl_kept_pos = pl_kept_row = 0;
}

/* after the replay's tick (row: it started a row): the tracks' notes start, let go, are cut; every track's
 * latest note follows its pitch, volume and pan. Chasing, only the rows' commands and the tracks' state */
static PLAT_COLD void pl_after_tick(int row)
{
    struct ibxm_fm1_state st;
    struct ibxm_fm1_chan c;
    uint32_t t;
    pl_alloc_k = (int8_t)pl_cur->k;      /* (its notes ask for voices) */
    pl_layer_cuts();
    if (pl_rp)                           /* (its properties open: at its TICKS A ROW and TEMPO as they change, no Fxx) */
        replay_fm1_timing(pl_rp, pl_time_edited() && pl_sd_ok ? (int)pl_sd.ticks : 0, (int)pl_sd.tempo);
    replay_fm1_state(pl_rp, &st);
    if (st.ended != pl_rp_ended) {
        pl_rp_ended = st.ended;
        if (pl_chasing)
            return;
        if (pl_what == PL_LOOP || pl_src) {   /* the pattern loop, a source: each pass at its own tempo again */
            replay_set_position_row(pl_rp, 0, 0);
            replay_fm1_state(pl_rp, &st);
            pl_rp_ended = st.ended;
        } else if (pl_state == PS_PLAY && (pl_sd.flags & RS_STOP)) {
            /* the end of a song that stops: as SONGS' play mode says, else it stops (a looping song loops from its
             * LOOP FROM in every mode: the replay goes on by itself) */
            uint32_t em = pl_what == PL_SONG && !pl_cur->dj ? pl_end_mode : PL_END_OWN;
            if (em == PL_END_REPEAT) {   /* (again from its start) */
                replay_set_position_row(pl_rp, 0, 0);
                replay_fm1_state(pl_rp, &st);
                pl_rp_ended = st.ended;
            } else {
                if (em == PL_END_NEXT && pl_end_next != 0xFFu)
                    pl_chain = pl_end_next;  /* (the next song from its start, as the next half begins: pl_start out
                                              * of the replay's tick; this one stops at its end meanwhile) */
                pl_song_end();
                return;
            }
        }
    }
    pl_gvol = st.global_vol;
    pl_speed = (uint32_t)st.speed;
    pl_tempo = (uint32_t)st.tempo;
    pl_tick_q16 = pl_tick_len((uint32_t)st.tempo, (uint32_t)st.row);
    if (pl_skip_to >= 0 && st.row_count != pl_skip_rc) {
        pl_cur->restart = (uint16_t)(pl_skip_to + 1);  /* (the row ended: a queued skip lands, a clean start there,
                                                         * pl_clock_block's: this row's notes never start) */
        pl_skip_to = -1;
        return;
    }
    if (row) {
        pl_rows_seen++;
        pl_clk_row();
        if (pl_pu_rows && !--pl_pu_rows)
            fx_punch(-1, 0);
        if (pl_cur->dj)
            pl_dj_row(&st);              /* (a deck: its loop, its rows and bars counted for DJ) */
        if (!PL_RARE(PL_LANES_SRC(pl_cur)))
            pl_row_start();
    }
    if (PL_RARE(PL_LANES_SRC(pl_cur))) { /* LOOPS: the replay keeps the time, the lanes play the notes */
        pl_lp_after(row, (uint32_t)st.row);
        return;
    }
    for (t = 0; t < PL_NCH && !pl_chasing; t++) {   /* (a chase's channels: untouched, they start over at its end) */
        pl_chan_t *ch = &pl_ch[t];
        if (t >= (uint32_t)pl_mod->num_channels)
            break;
        replay_fm1_chan(pl_rp, (int)t, &c);
        ch->bend = c.bend64;
        ch->vol = c.volume;
        ch->pan = c.panning;
        {
            DBG_T(tn);
            if (c.trig && c.key > 0 && c.key <= 96)
                pl_trigger(t, &c);
            else if (PL_RARE(c.leg))
                pl_legato(t, &c);
            else if (ch->npend)
                pl_pending(t);
            PL_DBG_PART(DS_NOTES, tn);   /* (the debug build: REPLAY's notes started) */
        }
        if ((c.off || c.cut) && player_note_hook && t < 16u)
            player_note_hook((uint8_t)t, 0, 0);     /* (MIDI OUT: the track's note sent ends) */
        if (c.off) {
            pl_track_release(t);
            ch->slide = 0;
            ch->gate = -1;
        }
        if (c.cut) {                     /* NOTE CUT: its notes fade out over a block and go */
            uint32_t i;
            for (i = 0; i < PL_NV; i++)
                if (pl_v[i].use == PV_SONG && pl_v[i].who == t + pl_tbase && pl_v[i].gen == ch->gen)
                    pl_v[i].cut = 1;
            ch->npend = 0;
        }
        if (PL_RARE(c.off || c.cut))      /* (a FUSION note's ticks stop with it) */
            pl_sf_off((int)(t + pl_tbase), 0);
        pl_track_follow(t);
    }
}

/* the EDITOR's take (player_take_cell): track t's cell at row `row` of the pattern loop, just written, heard as
 * written. at 0: its row started already: its note now, as the replay would start it there (its EDx and Kxx not
 * waited for: it is played now; its instrument the track's from then, the row's own cell going on), or (off) the
 * track's note let go now. Else the row's count: still to come, the replay reads it there; started before the cell
 * was written (its cells read without it), now */
static __attribute__((noinline)) PLAT_COLD void pl_take_cell(uint32_t t, uint32_t row, uint32_t at, uint32_t off)
{
    pl_chan_t *ch = &pl_ch[t];
    const rec_cell_t *read = &pl_cells[t];   /* (the row playing, as the replay read it) */
    rec_cur_t cu;
    rec_cell_t c;
    struct note n;
    struct ibxm_fm1_chan o;
    uint32_t k, rn, ra, rs;
    if (pl_what != PL_LOOP || pl_state != PS_PLAY || !pl_rp || pl_gp_num != (int32_t)pl_loop_pat ||
        t >= (uint32_t)pl_gp.ntrk || t >= (uint32_t)pl_mod->num_channels || (at && pl_rows_seen < at))
        return;
    if (off) {
        for (k = 0; k < read->n && read->cmd[k][0] != 0x14u; k++)
            ;
        if (at && pl_rows_seen == at && ((read->has & RC_NOTE) || k < read->n))
            return;                      /* (read with it: the replay lets the note go there) */
        if (player_note_hook && t < 16u)
            player_note_hook((uint8_t)t, 0, 0);     /* (MIDI OUT: the track's note sent ends) */
        pl_track_release(t);
        ch->slide = 0;
        ch->gate = -1;
        pl_sf_off((int)(t + pl_tbase), 0);
        return;
    }
    rec_cur_set_end(&cu, pl_gp.trk[t], pl_gp.trk_end[t]);
    if (!rec_cur_at(&cu, (int)row, &c) || !(c.has & RC_NOTE) || c.note == REC_NOTE_OFF ||
        (at && pl_rows_seen == at && (read->has & RC_NOTE) && read->note == c.note))
        return;                          /* (read with it: heard there) */
    memset(&n, 0, sizeof n);
    pl_cell_note((int)t, &c, &n);
    if (c.has & RC_INS) {                /* (its instrument, as a row's start takes it) */
        if (!ch->has_ins || c.cart != ch->ins.cart || c.slot != ch->ins.slot)
            ch->np = 0;
        ch->ins.cart = c.cart;
        ch->ins.slot = c.slot;
        ch->has_ins = 1;
    }
    rn = ch->row_note;
    ra = ch->row_acc;
    rs = ch->row_slide;
    ch->row_note = c.note;
    ch->row_acc = ch->row_slide = 0;
    for (k = 0; k < c.n; k++) {          /* (its 303's ACC, SLD) */
        if (c.cmd[k][0] == 0xC0u || c.cmd[k][0] == 0xC2u)
            ch->row_acc = 1;
        if (c.cmd[k][0] == 0xC1u || c.cmd[k][0] == 0xC2u)
            ch->row_slide = 1;
    }
    replay_fm1_note(pl_rp, (int)t, &n);
    replay_fm1_chan(pl_rp, (int)t, &o);
    ch->bend = o.bend64;
    ch->vol = o.volume;
    ch->pan = o.panning;
    pl_alloc_k = (int8_t)pl_cur->k;
    if (o.trig && o.key > 0 && o.key <= 96)
        pl_trigger(t, &o);
    else if (o.leg)
        pl_legato(t, &o);
    pl_track_follow(t);
    ch->row_note = (uint8_t)rn;          /* (the row's own again) */
    ch->row_acc = (uint8_t)ra;
    ch->row_slide = (uint8_t)rs;
}

/* SYNC EXTERNAL (specs/connections.md#clock): the replay that follows the clock (what plays; on DJ the main deck)
 * keeps its place on it. Each block, where the clock is when the block is heard (the last clock's count and
 * arrival, on at its pace; the block plays about a half after it is made) against where the replay is, both in
 * clocks (Q8); its ticks a little shorter or longer till they meet: 4 % for a clock apart, 5 % at most, so it
 * never drifts and the clock's jitter is smoothed. The first block it follows locks it: its place there is the
 * clock's. A beat or more apart (the clock's tempo jumped), its place is taken as the clock's again. Returns the
 * clocks a tick (Q8) to count its ticks with; 0 while the master's START waits for its first clock: it holds */
#define PL_EXT_LAT_US 6000u              /* (a block made now is heard about a half later) */
#define PL_EXT_GAIN 10                   /* (Q16 shorter for each 1/256 clock behind: 2,560 = 3.9 % a clock) */
#define PL_EXT_MOST 3277                 /* (5 %) */
static uint32_t pl_ext_follow(void)
{
    uint32_t seq = pl_ext.seq, n, at, per, rpb, spd, own10, cpt, own, master, dt, tk;
    int32_t err;
    n = pl_ext.n;
    at = pl_ext.at_ms;
    per = pl_ext.per_us;
    PL_BARRIER();
    rpb = pl_sd.rpb ? pl_sd.rpb : 4u;
    spd = pl_speed ? pl_speed : 6u;
    own10 = pl_cur->dj ? pl_bpm10() : pl_ext_bpm10;
    cpt = own10 ? 6144u * pl_ext_bpm10 / (own10 * rpb * spd) : 0u;   /* (24 a beat of the clock's, Q8) */
    if ((seq & 1u) || seq != pl_ext.seq || !per || !cpt)
        return cpt ? cpt : 1u;           /* (being written, no pace yet: as it was) */
    if (!n) {                            /* (START, no clock since: it waits for the first) */
        pl_cur->ext_lock = 0;
        pl_cur->ext_corr = 0;
        return 0;
    }
    dt = plat_ms() - at;
    dt = (dt > 2000u ? 2000u : dt) * 1000u + PL_EXT_LAT_US + pl_block_at * 10000u / 441u;
    master = (n - 1u) * 256u + dt * 256u / per;
    tk = pl_tick_q16 >> 8;
    tk = pl_clock / (tk ? tk : 1u);      /* (the tick's part played, Q8) */
    own = pl_cur->ext_own + (cpt * (tk > 256u ? 256u : tk) >> 8);
    err = (int32_t)(master - own);
    if (!pl_cur->ext_lock || err > 24 * 256 || err < -24 * 256) {
        pl_cur->ext_own += (uint32_t)err;  /* (locked here: its place is the clock's) */
        pl_cur->ext_lock = 1;
        err = 0;
    }
    err *= PL_EXT_GAIN;
    pl_cur->ext_corr = err > PL_EXT_MOST ? PL_EXT_MOST : err < -PL_EXT_MOST ? -PL_EXT_MOST : err;
    return cpt;
}

static void pl_start(uint8_t what, uint8_t num, uint8_t pos, uint8_t row);
static __attribute__((noinline)) void pl_clock_block(void)   /* the ticks due in this block (what plays: its own) */
{
    uint32_t cpt = 0;
    if (!pl_rp || (pl_state != PS_PLAY && pl_state != PS_STEP))
        return;
    if (pl_ext_bpm10 && pl_state == PS_PLAY &&
        (PL_DJ_ON ? !PL_SIDE : pl_cur->k == (PL_SECOND(pl_what) ? 1u : 0u))) {
        if (!(cpt = pl_ext_follow()))
            return;                      /* (the master's START: no clock yet) */
    } else {
        pl_cur->ext_lock = 0;
        pl_cur->ext_corr = 0;
    }
    pl_clock += PL_BLOCK << 16;
    while (pl_clock >= pl_tick_q16 && pl_rp && (pl_state == PS_PLAY || pl_state == PS_STEP) && !pl_cur->restart) {
        pl_clock -= pl_tick_q16;
        if (pl_state == PS_STEP)
            replay_hold_row(pl_rp);      /* (stepped: the row holds, its tick effects go on) */
        pl_after_tick(replay_fm1_tick(pl_rp));
        pl_cur->ext_own += cpt;
    }
    if (pl_cur->restart) {               /* PLAYBACK's K1 landed: every note of the song cut, the effects' tails emptied,
                                          * and the song started afresh at that position (its rows before run through) */
        uint32_t i, to = pl_cur->restart - 1u;
        pl_cur->restart = 0;
        for (i = 0; i < PL_NV; i++)
            if (pl_v[i].use == PV_SONG && (pl_v[i].who >> 4 & 1u) == pl_cur->k)
                pl_v[i].cut = 1;
        for (i = 0; i < PL_NCH; i++)
            pl_ch[i].npend = 0;
        pl_sf_off(-1, pl_cur->k);
        fx_kill();
        pl_start(PL_SONG, pl_song, (uint8_t)to, 0);
    }
}

/* what plays from now: the song (or the pattern loop) read and built as IBXM's module, its tracks and FX
 * as at its start, at (pos, row): the rows before it run through first (a chase, PS_CHASE).
 * A DJ deck not sounding (paused, at its end, armed, chasing or already being made ready: its arena unused) is
 * made ready over the halves after this one (specs/ui/screens/dj.md#playing-and-pausing), so no half takes the
 * whole start: here the song read, its module built and its first pattern's tracks found (PS_BUILD); in the next
 * half the replay's seek to its first row; in the one after, the rest (pl_start_tail), its first notes then. Each
 * step is what this function did at once, in the same order: the deck sounds as it would have, two halves later */
static void pl_start_tail(uint8_t pos, uint8_t row);
static PLAT_COLD void pl_start(uint8_t what, uint8_t num, uint8_t pos, uint8_t row)
{
    int spread = what == PL_DJ && pl_cur->dj && pl_d2 &&
                 (pl_state == PS_IDLE || pl_state == PS_END || pl_state == PS_ARMED || pl_state == PS_CHASE ||
                  pl_state == PS_BUILD);
    uint8_t had_rp = (uint8_t)(pl_state == PS_BUILD ? pl_cur->build_rp : pl_rp != 0);
    if (PL_RARE(pl_cur->dj))             /* (HOLD's hand: a stepped row's ticks given up) */
        pl_dj.hand_n[PL_SIDE] = 0;
    pl_song_release();
    if (!pl_cur->dj)
        fx_punch_stop();
    pl_pu_rows = 0;
    pl_skip_to = -1;
    pl_cur->restart = 0;
    pl_ended = 0;
    pl_what = what;
    pl_lcue = 0;                         /* (something plays: the loops' hold for the keys over) */
    pl_lpr.on = pl_lpr.pend;             /* (and RECORD LIVE's rest: player_lp_rest sets it again after; a silent start
                                          * from its first row, which may start here) */
    pl_lpr.pend = 0;
    pl_cur->ext_lock = 0;                /* (on a clock: locked afresh where it starts) */
    pl_cur->ext_corr = 0;
    if (what != PL_SONG) {               /* (something else plays: PLAYBACK's speed and volume back to 1.0, 0 dB;
                                          * SONGS' play mode off, the songs end as their AT THE END says) */
        pl_ps_share = 65536u;
        pl_sv_db = 0;
        pl_end_mode = PL_END_OWN;
        pl_chain = 0xFF;
    }
    if (what != PL_DJ)                   /* (DJ's tempo share: only DJ's decks; a song PLAYBACK's speed) */
        pl_tscale = what == PL_SONG && !pl_cur->dj ? pl_ps_share : 65536u;
    if (what == PL_SONG)
        pl_song = num;
    else if (what == PL_LOOP)
        pl_loop_pat = num;
    pl_sd_ok = !pl_rec_song(&pl_sd);     /* (a source's own: LOOPS) */
    if (!pl_sd_ok) {                     /* (no song: the pattern loop at its defaults) */
        memset(&pl_sd, 0, sizeof pl_sd);
        pl_sd.tempo = 125;
        pl_sd.ticks = 6;
        pl_sd.rpb = 4;
        pl_sd.swing = 50;
        pl_sd.gvol = 64;
        pl_sd.len = 0;
        {
            uint32_t t;
            for (t = 0; t < PL_NCH; t++) {
                pl_sd.mix[t][RM_LEVEL] = 0x7F;
                pl_sd.mix[t][RM_PAN] = 0x80;
            }
        }
    }
    if (what == PL_LOOP) {
        pl_order1 = num;
        pl_order = &pl_order1;
        pl_len = 1;
        pos = 0;
        if (pl_lt.ticks) {               /* (its own timing: player_play_pattern_at, LOOPS' LOAD's preview) */
            pl_sd.tempo = pl_lt.tempo;
            pl_sd.ticks = pl_lt.ticks;
            pl_sd.rpb = pl_lt.rpb;
            pl_sd.swing = pl_lt.swing;
        }
    } else {
        pl_order = pl_sd.order;
        pl_len = pl_sd_ok ? pl_sd.len : 0;
    }
    pl_state = PS_IDLE;
    pl_cur->build = 0;
    if (spread) {
        if (!pl_len || !pl_ib_module())
            return;
        pl_gp_load(pl_order[0]);         /* (what the replay's seek reads first: its tracks found now) */
        pl_go_pos = (uint8_t)(pos >= pl_len ? pl_len - 1u : pos);
        pl_go_row = row;
        pl_state = PS_BUILD;
        pl_cur->build = 1;
        pl_cur->build_rp = had_rp;
        pl_cur->build_at = pl_halves;
        return;
    }
    if (!pl_len || !pl_ib_build())
        return;
    pl_start_tail(pos, row);
}
/* a DJ deck's start made ready (PS_BUILD): its next step, one a half after the start's own (pl_start) */
static void pl_build_step(void)
{
    if (pl_cur->build_at == pl_halves)
        return;
    pl_cur->build_at = pl_halves;
    if (pl_cur->build == 1) {            /* the replay, set at its first row */
        pl_cur->build = 2;
        if (!pl_ib_replay()) {
            pl_cur->build = 0;
            pl_state = PS_IDLE;
        }
        return;
    }
    pl_cur->build = 0;                   /* the rest of the start: the deck from here as pl_start had it */
    pl_state = PS_IDLE;
    pl_start_tail(pl_go_pos, pl_go_row);
    if (PL_RARE(pl_step_after) && pl_state != PS_CHASE) {   /* (HOLD's hand started it: stepped, its row held) */
        if (pl_state == PS_PLAY)
            pl_state = PS_STEP;
        pl_step_after = 0;
    }
}
/* nothing played yet (player_cue): the song held, its FX setup in the effects and its time in the clock, so the keys
 * play as a synth through what the FX page shows, at its tempo (pl_cur: the first replay; its notes untouched) */
static void pl_cue_load(void)
{
    fx_setup_keep(pl_sd_ok ? pl_sd.fx : 0, pl_mine_e, pl_mine_codes);
    pl_speed = pl_sd_ok && pl_sd.ticks ? pl_sd.ticks : 6u;
    pl_tempo = pl_sd_ok ? pl_sd.tempo : 125u;
    pl_tick_q16 = pl_tick_len(pl_tempo, 0);
}

static PLAT_COLD void pl_start_tail(uint8_t pos, uint8_t row)
{
    if (pos >= pl_len)
        pos = (uint8_t)(pl_len - 1u);
    if (pl_cur->dj)                      /* (a deck: its song's own effects; the shared ones are DJ's) */
        fx_strip_setup(&pl_dj.strip[PL_SIDE], pl_sd_ok ? pl_sd.fx : 0);
    else                                 /* (the FX page's effect under the cursor kept as it plays) */
        fx_setup_keep(pl_sd_ok ? pl_sd.fx : 0, pl_mine_e, pl_mine_codes);
    pl_tracks_reset();
    pl_src_insts();
    pl_gvol = pl_sd.gvol;
    pl_speed = pl_sd.ticks;
    pl_tempo = pl_sd.tempo;
    pl_tick_q16 = pl_tick_len(pl_sd.tempo, 0);
    pl_clock = 0;
    pl_rows_seen = 0;
    fx_clk.beat = fx_clk.frac = 0;
    pl_cap_beat = pl_cap_frac = 0;
    pl_duck_env = 0;
    pl_go_pos = pos;
    pl_go_row = row;
    pl_state = pos || row ? PS_CHASE : PS_PLAY;
    if (PL_RARE(PL_LANES_SRC(pl_cur)))
        pl_lp_start();                   /* (LOOPS: the loops read, their lanes at rest) */
    if (pl_state == PS_PLAY && pl_cur->arm) {   /* (DJ: armed, its notes wait for its start: pl_go) */
        pl_state = PS_ARMED;
        pl_cur->arm_chased = 0;
        return;
    }
    if (pl_state == PS_PLAY)
        pl_after_tick(1);                /* (row 0, processed by the replay's start: its notes now) */
}

static void pl_chase(void)               /* a half's share of the chase */
{
    uint32_t n;
    struct ibxm_fm1_state st;
    pl_chasing = 1;
    if (!pl_rows_seen)
        pl_after_tick(1);                /* (row 0's commands) */
    for (n = 0; n < PL_CHASE_ROWS; n++) {
        replay_fm1_state(pl_rp, &st);
        if (st.seq_pos == (int)pl_go_pos && st.row == (int)pl_go_row)
            break;
        if (pl_rows_seen > 65536u || (st.ended && pl_rows_seen > 1u)) {   /* (never there: from there as at the start) */
            replay_set_position_row(pl_rp, pl_go_pos, pl_go_row);
            break;
        }
        replay_fm1_chase_row(pl_rp);     /* (its cells and path only: the channels start over at the end) */
        pl_after_tick(1);
    }
    pl_chasing = 0;
    replay_fm1_state(pl_rp, &st);
    if ((st.seq_pos == (int)pl_go_pos && st.row == (int)pl_go_row) || n < PL_CHASE_ROWS) {
        if (n > PL_CHASE_ARRIVE && !pl_cur->arm)
            return;                      /* (there: it starts in the next half, its first notes with little chase; an
                                          * armed deck's notes wait for its start: armed at once, as before) */
        pl_rp_ended = st.ended;
        if (pl_cur->arm) {               /* (DJ: there, armed; its row's notes at pl_go) */
            pl_state = PS_ARMED;
            pl_cur->arm_chased = 1;
            pl_clock = 0;
            return;
        }
        pl_state = pl_step_after ? PS_STEP : PS_PLAY;
        pl_step_after = 0;
        pl_clock = 0;
        {                                /* (the trigger flags the chase left: its notes are not this row's) */
            struct ibxm_fm1_chan c;
            uint32_t t;
            for (t = 0; t < (uint32_t)pl_mod->num_channels; t++)
                replay_fm1_chan(pl_rp, (int)t, &c);
        }
        replay_set_position_row(pl_rp, pl_go_pos, pl_go_row);   /* (the row again: its notes) */
        pl_rows_seen--;                  /* (the chase counted it: once, or the music clock runs a row ahead) */
        pl_after_tick(1);
    }
}

static void pl_pause(void)               /* what plays pauses and keeps its place; its notes ring out */
{
    struct ibxm_fm1_state st;
    if (pl_state != PS_PLAY && pl_state != PS_CHASE && pl_state != PS_STEP && pl_state != PS_ARMED &&
        pl_state != PS_HELD && pl_state != PS_BUILD)
        return;
    pl_step_after = 0;
    pl_cur->arm = 0;
    pl_cur->build = 0;                   /* (a start being made ready: given up, its place kept as a chase's) */
    pl_skip_to = -1;                     /* (a K1 skip waiting for its row's end, or its restart: given up) */
    pl_cur->restart = 0;
    if (PL_RARE(pl_cur->dj))             /* (HOLD's hand: a stepped row's ticks given up) */
        pl_dj.hand_n[PL_SIDE] = 0;
    if ((pl_state == PS_PLAY || pl_state == PS_STEP || pl_state == PS_HELD) && pl_rp) {
        replay_fm1_state(pl_rp, &st);
        pl_kept_pos = (uint8_t)st.seq_pos;
        pl_kept_row = (uint8_t)st.row;
    } else {
        pl_kept_pos = pl_go_pos;
        pl_kept_row = pl_go_row;
    }
    if (PL_RARE(pl_lpr.on) && PL_LANES_SRC(pl_cur)) {   /* (RECORD LIVE's rest: the loops' place is theirs, not the
                                                         * replay's; a jump queued: where it went) */
        if (pl_lpr.on == 1u || pl_lpr.on == 2u) {
            pl_kept_pos = 0;
            pl_kept_row = pl_lpr.kr;
        }
        pl_lpr.on = 0;
    }
    pl_song_release();
    if (!pl_cur->dj)
        fx_punch_stop();
    pl_pu_rows = 0;
    pl_state = PS_IDLE;
    if (PL_RARE(PL_LANES_SRC(pl_cur)) && pl_lpt.state == PL_TAKE_RUNNING) {
        pl_lpt.state = PL_TAKE_DONE;     /* (LOOPS paused: a take running is over) */
        pl_lpt.skip = -1;
    }
}

/* PLAYBACK's stepping (player_step): the first step pauses (its notes let go) and plays the row it is on
 * again; then a row forward, or back as if it came next (no reset, no replay from the pattern's start); the
 * row holds between. From paused: what is paused, at its place, stepped there (a chase first if needed) */
static void pl_steps(int d)
{
    struct ibxm_fm1_state st;
    pl_skip_to = -1;                     /* (stepping: a K1 skip waiting, or its restart, given up) */
    pl_cur->restart = 0;
    if (pl_state == PS_PLAY && pl_rp) {
        replay_fm1_state(pl_rp, &st);
        pl_song_release();
        fx_punch_stop();
        pl_pu_rows = 0;
        pl_state = PS_STEP;
        replay_jump_row(pl_rp, st.seq_pos, st.row);
        pl_after_tick(1);
    } else if (pl_state == PS_STEP && pl_rp) {
        replay_fm1_state(pl_rp, &st);
        if (d > 0) {
            replay_next_row(pl_rp);
        } else {
            int pos = st.seq_pos, row = st.row;
            if (d < 0 && row > 0) {
                row--;
            } else if (d < 0 && pos > 0) {
                pos--;
                row = pl_mod->patterns[pl_order[pos]].num_rows - 1;
            }
            replay_jump_row(pl_rp, pos, row);
        }
        pl_after_tick(1);
    } else if (pl_state == PS_IDLE || pl_state == PS_END) {
        uint8_t pos = pl_ended ? 0 : pl_kept_pos, row = pl_ended ? 0 : pl_kept_row;
        if (pl_what == PL_NONE)
            return;
        pl_step_after = 1;
        pl_start(pl_what, pl_what == PL_LOOP ? pl_loop_pat : pl_song, pl_what == PL_LOOP ? 0 : pos, row);
        if (pl_state == PS_PLAY)         /* (from its start: row 0's notes are sounding) */
            pl_state = PS_STEP;
        if (pl_state != PS_CHASE)
            pl_step_after = 0;
    }
    pl_clock = 0;
}

static void pl_cut_all(void)             /* all sound off: every voice fades out over a block and goes */
{
    uint32_t i;
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use != PV_FREE)
            pl_v[i].cut = 1;
    for (i = 0; i < 2u * PL_NCH; i++)
        PL_CH(i).npend = 0;
    pl_sf_off(-1, 2u);
    pl_nkeys = 0;
    pl_kf_off(-1);                       /* (the keys' FUSION layers still to start: never) */
    pl_arp.n = 0;                        /* (the arpeggio's notes go too: it waits for a key) */
    pl_arp.sounding = 0;
    fx_kill();
    fx_punch_stop();
    pl_pu_rows = 0;
}

/* ---- DJ: the decks' own work in the audio context (specs/ui/screens/dj.md) ---- */
#define PL_BRAKE_F 22050u                /* BRAKE: to a stop in 0.5 s */
#define PL_BACK_F 44100u                 /* BACKSPIN: to a stop in 1 s, three times as fast at first */
#define PL_SPINUP_F 4410u                /* a stop let go: spinning up again in 0.1 s */
#define PL_ECHO_RING 176400u             /* ECHO OUT's echoes ring on 4 s */
#define PL_REV_RING 264600u              /* REVERB OUT's reverb 6 s */
/* a dB's gain, -24 to +12 dB (Q12: VOLUME's to +6, KEYS GAIN's to +12); the SMOOTH crossfader's law (a quarter
 * cosine, Q12, 0-32) */
static const int16_t PL_VOL_Q12[37] = {258, 290, 325, 365, 410, 460, 516, 579, 649, 728, 817, 917, 1029, 1154, 1295,
                                       1453, 1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651, 4096, 4596, 5157, 5786,
                                       6492, 7284, 8173, 9170, 10289, 11544, 12953, 14533, 16306};
static const int16_t PL_COS_Q12[33] = {4096, 4091, 4076, 4052, 4017, 3973, 3920, 3857, 3784, 3703, 3612, 3513, 3406,
                                       3290, 3166, 3035, 2896, 2751, 2598, 2440, 2276, 2106, 1931, 1751, 1567, 1380,
                                       1189, 995, 799, 601, 401, 201, 0};

static int32_t pl_log2_q16(uint32_t x)   /* log2(x / 65536), Q16 (x > 0); within 0.01 */
{
    int32_t e = 0, m;
    while (x >= 131072u) {
        x >>= 1;
        e++;
    }
    while (x < 65536u) {
        x <<= 1;
        e--;
    }
    m = (int32_t)x - 65536;              /* log2(1 + m) ~ m (1.3465 - 0.3465 m) */
    return e * 65536 + (int32_t)(((int64_t)m * (88245 - ((22708 * (int64_t)m) >> 16))) >> 16);
}
static uint32_t pl_bar_rows(void)        /* the current replay's bar: BEATS A BAR beats of ROWS A BEAT rows */
{
    uint32_t rpb = pl_sd.rpb ? pl_sd.rpb : 4u, beats = pl_sd.fx ? pl_sd.fx[11] : 4u;
    return rpb * (beats < 1u ? 4u : beats > 16u ? 16u : beats);
}
static uint32_t pl_row_frames(void) { return (uint32_t)(((uint64_t)(pl_speed ? pl_speed : 6u) * pl_tick_q16) >> 16); }
static uint32_t pl_rows_of(uint32_t pos) { return pl_mod && pos < pl_len ? (uint32_t)pl_mod->patterns[pl_order[pos]].num_rows : 64u; }
static int pl_deck_runs(uint32_t k)      /* a deck plays (a stop's or a ROLL's counting included) */
{
    uint8_t s = pl_ctxs[k].state;        /* (a start being made ready does not run yet: it leads nothing; HOLD's hand
                                          * steps a held deck: it does not play) */
    return s == PS_PLAY || s == PS_HELD || (s == PS_CHASE && !pl_ctxs[k].arm && !pl_ctxs[k].step_after);
}

/* a deck's row started (pl_after_tick): its row and bar counted for DJ; its loop round (a jump queued for the
 * row's end, as a pattern break) */
static void pl_dj_row(const struct ibxm_fm1_state *st)
{
    uint32_t k = PL_SIDE, br = pl_bar_rows();
    pl_dloop_t *lp = &pl_dj.loop[k];
    pl_dj.row_now[k] = 1;
    if (br && (uint32_t)st->row % br == 0u)
        pl_dj.bar_now[k] = 1;
    if (pl_chasing || !lp->kind)
        return;
    if (lp->kind == PL_DLOOP_PATTERN) {  /* the position over and over, its breaks and jumps brought back too */
        if (st->seq_pos != lp->pos || (uint32_t)st->row + 1u >= pl_rows_of(lp->pos))
            replay_queue_row(pl_rp, lp->pos, 0);
        return;
    }
    if (st->seq_pos == lp->pos && st->row == lp->row)
        lp->n = 0;
    else
        lp->n++;
    if ((uint32_t)lp->n + 1u >= lp->rows)
        replay_queue_row(pl_rp, lp->pos, lp->row);
}

/* the current deck plays on from where it would have been: from (pos, row) when the hands took it, frames
 * later at its tempo, through its song's flow (its jumps and breaks; the rows passed run silently) */
static void pl_dj_land(uint8_t pos, uint8_t row, uint32_t frames, uint32_t row_frames)
{
    struct ibxm_fm1_state st;
    struct ibxm_fm1_chan c;
    uint32_t n = row_frames ? frames / row_frames : 0u, rem = row_frames ? frames - n * row_frames : 0u, i, t, ticks;
    if (!pl_rp)
        return;
    pl_song_release();
    pl_chasing = 1;
    replay_jump_row(pl_rp, pos, row);
    pl_after_tick(1);
    for (i = 0; i < n && i < 4096u; i++) {
        replay_next_row(pl_rp);
        pl_after_tick(1);
    }
    pl_chasing = 0;
    replay_fm1_state(pl_rp, &st);
    for (t = 0; t < (uint32_t)pl_mod->num_channels; t++)
        replay_fm1_chan(pl_rp, (int)t, &c);   /* (the flags the silent rows left) */
    replay_jump_row(pl_rp, st.seq_pos, st.row);   /* (the row again: its notes) */
    pl_state = PS_PLAY;
    pl_clock = 0;
    pl_after_tick(1);
    rem = rem < 65535u ? rem : 65535u;   /* (into the row: its ticks, then the tick's frames) */
    ticks = pl_tick_q16 ? (rem << 16) / pl_tick_q16 : 0u;
    for (i = 0; i < ticks && i + 1u < pl_speed && pl_state == PS_PLAY; i++)
        pl_after_tick(replay_fm1_tick(pl_rp));
    pl_clock = (rem << 16) - i * pl_tick_q16;
    if (pl_clock >= pl_tick_q16)
        pl_clock = pl_tick_q16 - 1u;
}

static void pl_go(void)                  /* the current deck, armed, starts now */
{
    struct ibxm_fm1_chan c;
    uint32_t t;
    if (pl_state != PS_ARMED)
        return;
    pl_cur->arm = 0;
    pl_state = PS_PLAY;
    pl_clock = 0;
    if (pl_cur->arm_chased) {            /* (its row again: the chase's flags dropped, its notes now) */
        for (t = 0; t < (uint32_t)pl_mod->num_channels; t++)
            replay_fm1_chan(pl_rp, (int)t, &c);
        replay_set_position_row(pl_rp, pl_go_pos, pl_go_row);
        pl_rows_seen--;                  /* (the chase counted it: once) */
    }
    pl_after_tick(1);
}

static void pl_dj_cut(uint32_t k)        /* a deck's voices fade out over a block and go (k: its side) */
{
    uint32_t i, m = pl_ctxs[k].k;
    for (i = 0; i < PL_NV; i++)
        if (pl_v[i].use == PV_SONG && (pl_v[i].who >> 4 & 1u) == m)
            pl_v[i].cut = 1;
}

static void pl_dj_back(void)             /* BACKSPIN: the current deck a row back (round a loop), its notes */
{
    struct ibxm_fm1_state st;
    pl_dloop_t *lp = &pl_dj.loop[PL_SIDE];
    int pos, row;
    replay_fm1_state(pl_rp, &st);
    pos = st.seq_pos;
    row = st.row;
    if (lp->kind == PL_DLOOP_ROWS && pos == lp->pos && row == lp->row) {   /* (round the loop: to its end) */
        row = lp->row + lp->rows - 1;
        if (row >= (int)pl_rows_of((uint32_t)pos))
            row = (int)pl_rows_of((uint32_t)pos) - 1;
    } else if (lp->kind == PL_DLOOP_PATTERN && row == 0) {
        row = (int)pl_rows_of((uint32_t)pos) - 1;
    } else if (row > 0) {
        row--;
    } else if (pos > 0) {
        pos--;
        row = (int)pl_rows_of((uint32_t)pos) - 1;
    } else {
        return;
    }
    replay_jump_row(pl_rp, pos, row);
    pl_after_tick(1);
}

/* a deck's stop, a block on (before its ticks): BRAKE and BACKSPIN bend and fade its voices as the record
 * slows; ECHO OUT and REVERB OUT stop it on its next beat; stood, it is silent, its place counting on */
static void pl_dj_stop_block(uint32_t k)
{
    pl_stop_t *s = &pl_dj.stop[k];
    int32_t bend = 0, gain = 4096;
    pl_use(k);
    if (s->spin) {                       /* (let go: spinning up from low, its notes rising) */
        uint32_t x = (PL_SPINUP_F - s->spin) * 65536u / PL_SPINUP_F;
        x = x < 4096u ? 4096u : x;
        bend = (768 * pl_log2_q16(x)) >> 16;
        gain = (int32_t)(x >> 4);
        s->spin = s->spin > PL_BLOCK ? s->spin - PL_BLOCK : 0u;
    }
    if (s->kind) {
        s->frames += PL_BLOCK;
        if (!s->stood && s->kind == PL_STOP_BRAKE) {
            uint32_t x = s->frames >= PL_BRAKE_F ? 0u : 65536u - s->frames * 65536u / PL_BRAKE_F;
            bend = x ? (768 * pl_log2_q16(x)) >> 16 : -3072;
            bend = bend < -3072 ? -3072 : bend;   /* (four octaves down at most) */
            gain = (int32_t)(x >> 4);
            if (!x) {
                s->stood = 1;
                pl_dj_cut(k);
            }
        } else if (!s->stood && s->kind == PL_STOP_BACKSPIN) {
            uint32_t x = s->frames >= PL_BACK_F ? 0u : 65536u - s->frames * 65536u / PL_BACK_F;
            bend = x ? (768 * pl_log2_q16(3u * x)) >> 16 : -3072;
            bend = bend < -3072 ? -3072 : bend;
            gain = x > 6554u ? 4096 : (int32_t)(x * 10u >> 4);   /* (the last tenth fading) */
            s->back += (3u * x * PL_BLOCK) >> 8;   /* (rows back at three times the row rate, slowing: Q8 frames) */
            while (x && s->row_frames && s->back >= s->row_frames << 8) {
                s->back -= s->row_frames << 8;
                pl_dj_back();
            }
            if (!x) {
                s->stood = 1;
                pl_dj_cut(k);
            }
        } else if (!s->stood && s->beat_wait && pl_rp) {   /* ECHO / REVERB OUT: its next beat */
            struct ibxm_fm1_state st;
            uint32_t rpb = pl_sd.rpb ? pl_sd.rpb : 4u, was = s->last_row;
            replay_fm1_state(pl_rp, &st);
            s->last_row = (uint8_t)st.row;
            if ((uint32_t)st.row != was && (uint32_t)st.row % rpb == 0u) {
                s->stood = 1;
                s->beat_wait = 0;
                pl_dj_cut(k);
                if (pl_state == PS_PLAY || pl_state == PS_STEP)
                    pl_state = PS_HELD;
                pl_dj.ring[k] = s->kind == PL_STOP_ECHO ? PL_ECHO_RING : PL_REV_RING;
                pl_dj.ring_fx[k] = s->kind == PL_STOP_ECHO ? 1u : 2u;
            }
        }
        if (s->stood) {
            gain = 0;
            if (s->stay && !s->held) {   /* (let go to stay: paused where the stop left it) */
                s->kind = PL_STOP_NONE;
                pl_pause();
            }
        }
    }
    pl_dj.sbend[k] = bend;
    pl_dj.sgain[k] = gain;
    if (pl_dj.ring[k])
        pl_dj.ring[k] = pl_dj.ring[k] > PL_BLOCK ? pl_dj.ring[k] - PL_BLOCK : 0u;
}

/* KEY and a stop's bend on a deck's voices, when they change */
static void pl_dj_pitch(uint32_t k)
{
    int32_t want = pl_djm.key[k] * 64 + pl_dj.sbend[k];
    uint32_t i, m = pl_ctxs[k].k;        /* (k its side, m its voices' memory) */
    if (want == pl_dj.pbend[k])
        return;
    pl_dj.pbend[k] = want;
    pl_ctxs[k].key64 = want;
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        if (v->use == PV_SONG && (v->who >> 4 & 1u) == m && v->engine != EN_FUS)
            pl_v_pitch(v, PL_CH(v->who).bend + want);
    }
}

/* ---- DJ's LFOs (specs/ui/screens/dj.md#the-lfo-pads), as the playground's lfoRun: each swings by itself
 * from `from` to `to` and back, its phase counted a block at a time; what comes out is smoothed over ~15 ms;
 * switched off, its swing fades out over 0.2 s. The shapes move as a hand on a knob would ---- */
#define PL_LFO_SLEW 3021                 /* 1 - exp(-64 / 44100 / 0.015), Q15 */
#define PL_LFO_FADE 238                  /* 64 / 44100 / 0.2, Q15 */
#define PL_TRI_E 11141                   /* TRIANGLE's eased sixth (0.17), Q16 */
#define PL_TRI_K2 18494                  /* 2 x 0.17 x 0.83, Q16 */
static uint32_t pl_rnd = 0x9E3779B9u;
static int32_t pl_rnd16(void)
{
    pl_rnd ^= pl_rnd << 13;
    pl_rnd ^= pl_rnd >> 17;
    pl_rnd ^= pl_rnd << 5;
    return (int32_t)(pl_rnd >> 16);
}
static int32_t pl_cos16(uint32_t ph) { return dx7_sin(((ph + (1u << 30)) >> 8) & 0xFFFFFFu) >> 8; }   /* Q16 */
static int32_t pl_rc16(int32_t x)       /* (1 - cos(pi x)) / 2, x 0-1 Q16 */
{
    x = x < 0 ? 0 : x > 65536 ? 65536 : x;
    return (65536 - pl_cos16((uint32_t)x << 15)) >> 1;
}
static int32_t pl_ease16(int32_t x)     /* TRIANGLE's way: parabolas into the straight middle */
{
    if (x < PL_TRI_E)
        return (int32_t)(((int64_t)x * x >> 16) * 65536 / PL_TRI_K2);
    if (x > 65536 - PL_TRI_E) {
        int64_t y = 65536 - x;
        return 65536 - (int32_t)((y * y >> 16) * 65536 / PL_TRI_K2);
    }
    return (int32_t)((int64_t)(x - PL_TRI_E / 2) * 65536 / (65536 - PL_TRI_E));
}
/* x (0-65535) x 65536 / d (1-65536): in 32 bits unsigned, exactly (a 64-bit division is a slow loop on the FM-1) */
static int32_t pl_frac16(int32_t x, int32_t d) { return (int32_t)(((uint32_t)x << 16) / (uint32_t)d); }
static int32_t pl_lfo_w(const pl_lfo_st_t *st, int32_t e)   /* where the shape has it, 0-1 Q16 (e: a hand's time) */
{
    int32_t f = (int32_t)(st->ph >> 16);
    e = e < 1 ? 1 : e;
    switch (st->shape) {
    case 0:                              /* SINE */
        return (65536 - pl_cos16(st->ph)) >> 1;
    case 1:                              /* SQUARE */
        return f < e ? pl_rc16(pl_frac16(f, e)) : f < 32768 ? 65536
             : f < 32768 + e ? 65536 - pl_rc16(pl_frac16(f - 32768, e)) : 0;
    case 2:                              /* TRIANGLE */
        return pl_ease16(f < 32768 ? 2 * f : 131072 - 2 * f);
    case 3:                              /* SAW UP */
        return f < 65536 - e ? pl_frac16(f, 65536 - e) : 65536 - pl_rc16(pl_frac16(f - 65536 + e, e));
    case 4:                              /* SAW DOWN */
        return f < e ? pl_rc16(pl_frac16(f, e)) : 65536 - pl_frac16(f - e, 65536 - e);
    default:                             /* RANDOM */
        return st->r0 + (int32_t)(((int64_t)(st->r1 - st->r0) * pl_rc16(f)) >> 16);
    }
}
/* a fresh one sets off from where the thing is (u: between from and to, Q16), heading for `to` */
static void pl_lfo_start(pl_lfo_st_t *st, int32_t u, int32_t e)
{
    uint32_t lo = 0, hi = 32768, i;
    st->r0 = u;
    st->r1 = pl_rnd16();
    st->cyc = 0;
    if (st->shape == 0 || st->shape == 2) {   /* (its rising half: where it is u) */
        for (i = 0; i < 16u; i++) {
            uint32_t mid = (lo + hi) / 2u;
            st->ph = mid << 16;
            if (pl_lfo_w(st, e) < u)
                lo = mid;
            else
                hi = mid;
        }
        st->ph = hi << 16;
    } else if (st->shape == 3) {
        st->ph = (uint32_t)(((int64_t)u * (65536 - e)) >> 16) << 16;
    } else if (st->shape == 5) {
        st->ph = 0;
    } else {
        st->ph = (uint32_t)e << 16;
    }
}
static int32_t pl_u16(int32_t here, int32_t from, int32_t to);
/* an LFO a block on: a (its settings, or off), from / to / here / home as its thing stands now (where here is
 * between from and to: pl_u16, only when it sets off); ok: it may set off now (the lead deck started a row, or
 * none plays), late blocks after that row (it sets off as far on as it would be had it set off then). Returns
 * what comes out, Q8 */
static int32_t pl_lfo_run(pl_lfo_st_t *st, const pl_lfo_t *a, int ok, uint32_t late, int32_t from, int32_t to,
                          int32_t here, uint32_t inc, int32_t home)
{
    int32_t want = home, e = (int32_t)(((uint64_t)inc * 4064256u) >> 32);   /* (a hand: 90 ms, a quarter cycle at most) */
    e = e > 16384 ? 16384 : e;
    if (a && a->on) {
        if (!st->live || st->gen != a->gen) {
            if (ok) {
                st->gen = a->gen;
                st->shape = a->shape;
                st->live = 1;
                pl_lfo_start(st, pl_u16(here, from, to), e);
                if (late) {
                    uint32_t ph = st->ph;
                    st->ph += inc * late;
                    if (st->ph < ph) {   /* (a cycle on: RANDOM's next place) */
                        st->cyc++;
                        st->r0 = st->r1;
                        st->r1 = pl_rnd16();
                    }
                }
            }
        }
        if (st->live && st->gen == a->gen) {
            st->shape = a->shape;
            st->from = from;
            st->to = to;
            st->inc = inc;
            st->g = 32768;
        }
    } else if (st->live && (st->g -= PL_LFO_FADE) <= 0) {
        st->live = 0;
    }
    if (st->live) {
        uint32_t ph = st->ph;
        int32_t w = pl_lfo_w(st, e);
        want = home + (int32_t)(((int64_t)(st->from + (int32_t)(((int64_t)(st->to - st->from) * w) >> 16) - home) * st->g) >> 15);
        st->ph += st->inc;
        if (st->ph < ph) {               /* (a cycle on: RANDOM's next place) */
            st->cyc++;
            st->r0 = st->r1;
            st->r1 = pl_rnd16();
        }
    }
    if (!st->has_v || (want - st->v < 3 && st->v - want < 3))
        st->v = want;
    else
        st->v += (int32_t)(((int64_t)(want - st->v) * PL_LFO_SLEW) >> 15);
    st->has_v = 1;
    return st->v;
}
static int32_t pl_u16(int32_t here, int32_t from, int32_t to)   /* where `here` is between from and to, Q16 */
{
    int64_t u;
    if (to == from)
        return 0;
    u = (int64_t)(here - from) * 65536 / (to - from);
    return (int32_t)(u < 0 ? 0 : u > 65536 ? 65536 : u);
}
static int32_t pl_glide(int32_t v, int32_t to)   /* the ~15 ms smoothing */
{
    return to - v < 3 && v - to < 3 ? to : v + (int32_t)(((int64_t)(to - v) * PL_LFO_SLEW) >> 15);
}

static const uint8_t PL_MIXK[3] = {0xA7, 0xA3, 0x9E};            /* REVERB, DELAY, CHORUS: their MIX */
/* the LFOs, the crossfader, the filters as heard: a block on, as step j of those worked out at once (ok: an LFO may
 * set off, late blocks after the row that lets it: pl_lfo_run); what each block takes: lfo_xf, lfo_cut, lfo_mix at j */
static void pl_dj_lfo_step(uint32_t j, int ok, uint32_t late)
{
    uint32_t k, i;
    int32_t xf = (int32_t)pl_djm.xf << 8, from, to, end;
    /* the crossfader: its place to an end, end to end, SWING around its place, MIRROR */
    {
        const pl_lfo_t *F = &pl_djm.lfo[PL_LFO_FADER];
        uint32_t rev = F->rev ^ pl_swapped;
        if (pl_swapped)                  /* (worked the way the decks stood when DJ went on, mirrored there and back:
                                          * its sums the same whichever way they stand now) */
            xf = (32 << 8) - xf;
        end = rev ? 32 << 8 : 0;
        if (F->range == 2) {
            int32_t a = xf < (32 << 8) - xf ? xf : (32 << 8) - xf;
            from = rev ? xf + a : xf - a;
            to = rev ? xf - a : xf + a;
        } else if (F->range == 3) {
            from = xf;
            to = (32 << 8) - xf;
        } else if (F->range == 1) {
            from = (32 << 8) - end;
            to = end;
        } else {
            from = xf;
            to = end;
        }
        xf = pl_lfo_run(&pl_dj.lfo[PL_LFO_FADER], F, ok, late, from, to, xf, pl_dj.lfo[PL_LFO_FADER].inc, xf);
        pl_dj.lfo_xf[j] = pl_swapped ? (32 << 8) - xf : xf;
    }
    /* each DJ FILTER: as its knob and key have it (a slide in it), and its LFO, heard while its key has it on */
    for (k = 0; k < 2u; k++) {
        const pl_lfo_t *a = &pl_djm.lfo[PL_LFO_FILT_L + k];
        int32_t set = (int32_t)pl_djm.filt[k] * 256, target = pl_djm.filt_on[k] ? set : 0, h, v, p;
        int heard = pl_djm.filt_on[k] || pl_djm.filt_slide[k];
        if (pl_djm.filt_slide[k]) {      /* (the key held: from where it was, over 0.5 s at most) */
            if (!pl_dj.sliding[k]) {
                pl_dj.sliding[k] = 1;
                pl_dj.sl_from[k] = pl_dj.plain8[k];
                pl_dj.sl_n[k] = 0;
            }
            pl_dj.sl_n[k]++;
            i = pl_dj.sl_n[k] * PL_BLOCK >= 22050u ? 65536u : pl_dj.sl_n[k] * PL_BLOCK * 65536u / 22050u;
            pl_dj.plain8[k] = pl_dj.sl_from[k] + (int32_t)(((int64_t)(target - pl_dj.sl_from[k]) * (int32_t)i) >> 16);
        } else {
            pl_dj.sliding[k] = 0;
            pl_dj.plain8[k] = target;
        }
        h = heard ? pl_dj.plain8[k] : set;
        end = h > 0 ? 63 << 8 : -16384;
        if (a->range == 0) {
            from = h;
            to = 0;
        } else if (a->range == 2) {
            from = h;
            to = end;
        } else if (a->range == 3) {
            from = h;
            to = -h < -16384 ? -16384 : -h > (63 << 8) ? 63 << 8 : -h;
        } else if ((end - h < 0 ? h - end : end - h) >= (h < 0 ? -h : h)) {
            from = 0;
            to = end;
        } else {
            from = end;
            to = 0;
        }
        v = pl_lfo_run(&pl_dj.lfo[PL_LFO_FILT_L + k], a, ok, late, from, to, h, pl_dj.lfo[PL_LFO_FILT_L + k].inc,
                       a->on ? h : pl_dj.plain8[k]);
        pl_dj.prev8[k] = v;
        p = pl_dj.plain_v[k] = pl_glide(pl_dj.plain_v[k], pl_dj.plain8[k]);
        if (a->on) {
            int32_t on = heard ? 32768 : 0;
            pl_dj.heard[k] = on - pl_dj.heard[k] < 328 && pl_dj.heard[k] - on < 328 ? on
                           : pl_dj.heard[k] + (int32_t)(((int64_t)(on - pl_dj.heard[k]) * PL_LFO_SLEW) >> 15);
        }
        pl_dj.lfo_cut[k][j] = p + (int32_t)(((int64_t)(v - p) * pl_dj.heard[k]) >> 15);
    }
    /* the FX LFO: the MIX of the effect DJ's properties pick, only while it is on (off: it does nothing); a MIX to
     * give the effect waits for its block (pl_dj_lfo_block) */
    pl_dj.lfo_mix[j] = -1;
    {
        const pl_lfo_t *a = &pl_djm.lfo[PL_LFO_FX];
        uint32_t fx = pl_djm.lfo_fx > 2u ? 0u : pl_djm.lfo_fx, bit = fx == 0u ? FX_REVERB : fx == 1u ? FX_DELAY : FX_CHORUS;
        uint32_t on = pl_dj.setup ? ((uint32_t)(pl_dj.setup[7] | pl_dj.setup[8] << 8) >> bit & 1u) : 0u;
        int32_t set = (int32_t)pl_djm.mix_set[fx == 0u ? 2 : fx == 1u ? 1 : 0] << 8, m;
        if (pl_dj.mix_fx >= 0 && (uint32_t)pl_dj.mix_fx != fx) {   /* another effect: the one before back as set */
            uint32_t was = (uint32_t)pl_dj.mix_fx;      /* (at once: DJ's settings change only between halves, so
                                                         * this is the first step's, the first block's) */
            pl_fx_turn(PL_MIXK[was], pl_djm.mix_set[was == 0u ? 2 : was == 1u ? 1 : 0]);
            pl_dj.mix_fx = -1;
            memset(&pl_dj.lfo[PL_LFO_FX], 0, sizeof pl_dj.lfo[PL_LFO_FX]);
        }
        if (on && (a->on || pl_dj.lfo[PL_LFO_FX].live)) {
            from = a->range ? 0 : set;
            to = 100 << 8;
            m = pl_lfo_run(&pl_dj.lfo[PL_LFO_FX], a, ok, late, from, to, set,   /* (pl_u16: set x 65536 / (100 << 8) */
                           pl_dj.lfo[PL_LFO_FX].inc, set);                /* in range, 0 from set) */
            pl_dj.mix8 = pl_dj.lfo[PL_LFO_FX].live ? m : -1;
            pl_dj.mix_fx = (int8_t)fx;
            i = (uint32_t)((m + 128) >> 8);
            if (i != pl_dj.mix_sent || !pl_dj.lfo[PL_LFO_FX].live) {
                pl_dj.mix_sent = (uint8_t)i;
                pl_dj.lfo_mix[j] = (int16_t)(pl_dj.lfo[PL_LFO_FX].live ? i : (uint32_t)pl_djm.mix_set[fx == 0u ? 2 : fx == 1u ? 1 : 0]);
                pl_dj.lfo_mixk[j] = PL_MIXK[fx];
            }
            if (!pl_dj.lfo[PL_LFO_FX].live)
                pl_dj.mix_fx = -1;
        } else {
            pl_dj.mix8 = -1;
            pl_dj.lfo[PL_LFO_FX].live = 0;
        }
    }
}
/* the LFOs worked out for the next n blocks at once (n <= PL_LFO_STEPS): each block as it once was, a step each,
 * their code run in one go. Only two things differ from working each out in its own block: a fresh LFO sets off at
 * the first step, on the first row the lead deck started since the last time (in the blocks since, or this one), as
 * far on as it would be had it set off then (the blocks between give their thing as it was), not on a row started
 * in one of the n - 1 blocks to come (it sets off with the next ones then, as far on); and their speed is the lead
 * deck's tempo at the first block (a tempo command in the others reaches them at the next) */
static void pl_dj_lfos(uint32_t n)
{
    static const uint8_t HB[9] = {64, 32, 16, 8, 4, 2, 4, 2, 1};   /* each speed: bars (x beats x 2), else half beats */
    int lead = pl_deck_runs(0) ? 0 : pl_deck_runs(1) ? 1 : -1, seen = lead >= 0 && pl_dj.row_seen[lead];
    uint32_t bpm10 = 1200u, beats = 4u, i, j, late = seen ? pl_dj.row_ago[lead] : 0u;
    pl_dj.row_seen[0] = pl_dj.row_seen[1] = 0;
    if (lead >= 0) {
        pl_use((uint32_t)lead);
        bpm10 = pl_bpm10();
        beats = pl_bar_rows() / (pl_sd.rpb ? pl_sd.rpb : 4u);
    }
    for (i = 0; i < PL_NLFO; i++) {      /* (each one's speed: a note length of the lead deck's tempo) */
        const pl_lfo_t *a = &pl_djm.lfo[i];
        uint32_t len = a->len > 8u ? 6u : a->len, hb = len < 6u ? HB[len] * beats : HB[len], inc;
        inc = bpm10 * 20777u / (hb ? hb : 1u);
        pl_dj.lfo[i].inc = inc > 62331140u ? 62331140u : inc;   /* (10 Hz at most) */
    }
    for (j = 0; j < n; j++)
        pl_dj_lfo_step(j, lead < 0 || (!j && seen), j ? 0u : late);
}
static void pl_dj_lfo_block(uint32_t j)  /* a block takes step j's crossfader, filters and MIX */
{
    pl_dj.xf8 = pl_dj.lfo_xf[j];
    pl_dj.cut8[0] = pl_dj.lfo_cut[0][j];
    pl_dj.cut8[1] = pl_dj.lfo_cut[1][j];
    if (pl_dj.lfo_mix[j] >= 0)
        pl_fx_turn(pl_dj.lfo_mixk[j], (uint8_t)pl_dj.lfo_mix[j]);
}

/* the beat lock (SYNC): at the main deck's beat, the other's place against it; off by more than a block, its
 * clock nudged a little, by more than an eighth of a beat its rows moved (a note or two skipped or repeated) */
static void pl_dj_sync(void)
{
    struct ibxm_fm1_state s0, s1;
    uint32_t rpb0, rpb1, tf1, rf1, bf1, f0, f1, target;
    int32_t e;
    if (!pl_djm.sync || pl_ctxs[0].state != PS_PLAY || pl_ctxs[1].state != PS_PLAY || !pl_dj.row_now[0] ||
        pl_dj.stop[0].kind || pl_dj.stop[1].kind || pl_dj.loop[0].roll || pl_dj.loop[1].roll ||
        !pl_ctxs[0].rp || !pl_ctxs[1].rp)
        return;
    pl_use(0);
    replay_fm1_state(pl_rp, &s0);
    rpb0 = pl_sd.rpb ? pl_sd.rpb : 4u;
    if ((uint32_t)s0.row % rpb0 != 0u || (uint32_t)s0.tick + 1u < pl_speed)   /* (its beat's first tick) */
        return;
    f0 = pl_clock >> 16;
    pl_use(1);
    replay_fm1_state(pl_rp, &s1);
    rpb1 = pl_sd.rpb ? pl_sd.rpb : 4u;
    tf1 = pl_tick_q16 >> 16;
    rf1 = pl_row_frames();
    bf1 = rf1 * rpb1;
    if (!bf1)
        return;
    /* (its place in its beat: the rows, the row's ticks done (they count down: speed on its first, 1 on its last), the
     * tick's frames) */
    f1 = ((uint32_t)s1.row % rpb1) * rf1 + (pl_speed - (uint32_t)s1.tick < pl_speed ? (pl_speed - (uint32_t)s1.tick) * tf1 : 0u)
       + (pl_clock >> 16);
    target = pl_djm.sync_mult == 2u ? f0 * 2u : pl_djm.sync_mult == 1u ? ((pl_ctxs[0].rows_seen / rpb0) & 1u ? bf1 / 2u : 0u) + f0 / 2u : f0;
    e = (int32_t)(f1 % bf1) - (int32_t)(target % bf1);
    if (e > (int32_t)bf1 / 2)
        e -= (int32_t)bf1;
    else if (e < -(int32_t)bf1 / 2)
        e += (int32_t)bf1;
    if (e <= (int32_t)PL_BLOCK && e >= -(int32_t)PL_BLOCK)
        return;
    if (e < (int32_t)bf1 / 8 && e > -(int32_t)bf1 / 8) {   /* a nudge: within its tick, an eighth of it at most */
        int32_t n = e > (int32_t)tf1 / 8 ? (int32_t)tf1 / 8 : e < -(int32_t)tf1 / 8 ? -(int32_t)tf1 / 8 : e;
        int64_t c = (int64_t)pl_clock - ((int64_t)n << 16);
        pl_clock = c < 0 ? 0u : c >= (int64_t)pl_tick_q16 ? pl_tick_q16 - 1u : (uint32_t)c;
        return;
    }
    {                                    /* its rows moved onto the main's beat */
        int32_t rows = (e + (e > 0 ? (int32_t)rf1 / 2 : -(int32_t)rf1 / 2)) / (int32_t)(rf1 ? rf1 : 1u);
        int32_t row = s1.row - rows, rmax = (int32_t)pl_rows_of((uint32_t)s1.seq_pos) - 1;
        row = row < 0 ? 0 : row > rmax ? rmax : row;
        replay_jump_row(pl_rp, s1.seq_pos, row);
        pl_after_tick(1);
        pl_clock = (f0 << 16) < pl_tick_q16 ? f0 << 16 : 0u;
    }
}

/* armed decks: one waiting for the other's bar line starts on it, in phase */
static void pl_dj_starts(void)
{
    uint32_t d;
    for (d = 0; d < 2u; d++) {
        uint32_t o = 1u - d;
        if (!(pl_dj.arm_bar >> d & 1u) || pl_ctxs[d].state != PS_ARMED)
            continue;
        if (pl_deck_runs(o) && !pl_dj.bar_now[o])
            continue;                    /* (still waiting) */
        pl_dj.arm_bar &= (uint8_t)~(1u << d);
        pl_use(d);
        pl_go();
        if (pl_deck_runs(o))
            pl_clock = pl_ctxs[o].clock < pl_tick_q16 ? pl_ctxs[o].clock : 0u;   /* (as far into its row as the other) */
    }
}

/* a deck's sound and sends out at its gain (VOLUME x the crossfader), ramped over the block from g0 to g1 (Q12):
 * the first deck's as the mix and its sends (add 0), the second's added to them (add 1) */
static inline __attribute__((always_inline)) void pl_dj_out(const int32_t *l, const int32_t *r, int32_t *snd,
                                                            int32_t g0, int32_t g1, const int add)
{
    uint32_t i, s;
    if (g0 == g1 && g1 == 4096) {        /* (x x 4096 >> 12 is x: the sound as it is) */
        for (i = 0; i < PL_BLOCK; i++) {
            pl_mix[2u * i] = add ? pl_mix[2u * i] + l[i] : l[i];
            pl_mix[2u * i + 1u] = add ? pl_mix[2u * i + 1u] + r[i] : r[i];
        }
        if (add)
            for (i = 0; i < 3u * PL_BLOCK; i++)
                pl_send[i] += snd[i];
        return;
    }
#if PL_BLOCK != 64u
#error "pl_dj_out's ramp divides by 64 as shifts"
#endif
    for (i = 0; i < PL_BLOCK; i++) {     /* (/ 64 rounded toward 0 as shifts: the device's compiler divided, ~30
                                          * cycles a frame) */
        int32_t t = (g1 - g0) * (int32_t)(i + 1u), g = g0 + ((t + (int32_t)((uint32_t)(t >> 31) >> 26)) >> 6);
        int32_t a = (int32_t)(((int64_t)l[i] * g) >> 12), b = (int32_t)(((int64_t)r[i] * g) >> 12);
        pl_mix[2u * i] = add ? pl_mix[2u * i] + a : a;
        pl_mix[2u * i + 1u] = add ? pl_mix[2u * i + 1u] + b : b;
        for (s = 0; s < 3u; s++) {
            int32_t c = (int32_t)(((int64_t)snd[s * PL_BLOCK + i] * g) >> 12);
            pl_send[s * PL_BLOCK + i] = add ? pl_send[s * PL_BLOCK + i] + c : c;
        }
    }
}

/* DJ's mix, a block: each deck's own effects, its DJ FILTER (its sends too), ECHO / REVERB OUT fed, VOLUME x the
 * crossfader on its sound and sends, the decks summed; 1: the shared effects with DJ's setup are due on the mix and
 * sends (fx_clk the lead deck's), 0: CUT held, nothing more to do */
static int pl_dj_mix(void)
{
    int32_t l[PL_BLOCK], r[PL_BLOCK], g0, g1, x8;
    uint32_t k, m, i, s, lead = pl_deck_runs(0) ? 0u : pl_deck_runs(1) ? 1u : 0u, forced = 0, taken;
#ifdef PTFM_DEBUG
    uint32_t dm = plat_us(), dm_parts = dbg_half[DS_DJFILTERS] + dbg_half[DS_INSERTS];   /* (DJ MIX: this, less those) */
#endif
    if (PL_HUSHED) {                     /* (CUT held, faded out: the decks' clocks only, nothing worked on) */
        for (k = 0; k < (pl_d2 ? 2u : 1u); k++) {
            pl_use(k);
            pl_clk_pace();
        }
        pl_use(lead);
        fx_hold_block();
        memset(pl_mix, 0, PL_MIX_BYTES);
        return 0;
    }
    for (k = 0; k < (pl_d2 ? 2u : 1u); k++) {   /* (ECHO / REVERB OUT: the DELAY or REVERB made to take its send) */
        if (pl_dj.stop[k].kind == PL_STOP_ECHO || pl_dj.stop[k].kind == PL_STOP_REVERB)
            forced |= pl_dj.stop[k].kind == PL_STOP_ECHO ? 1u : 2u;
        if (pl_dj.ring[k])
            forced |= pl_dj.ring_fx[k];
    }
    taken = fx_sends_on((int)forced);    /* (a send whose effect is off: nobody hears it, not filtered) */
    for (m = 0; m < (pl_d2 ? 2u : 1u); m++) {   /* (no memory for the second deck: the main alone. In the memories'
                                          * order: memory 0's sound is in the mix, which the first one out writes) */
        int32_t *mix = m ? pl_mix1 : pl_mix, *snd = m ? pl_send1 : pl_send;
        int v;
        k = m ^ pl_swapped;              /* (its side: its own effects, filter, stop, VOLUME, the crossfader's end) */
        v = pl_djm.vol[k];
        pl_use(k);
        pl_clk_pace();
        for (i = 0; i < PL_BLOCK; i++) {
            l[i] = mix[2u * i];
            r[i] = mix[2u * i + 1u];
        }
        if (pl_ctxs[k].state == PS_BUILD ? pl_ctxs[k].build_rp : pl_ctxs[k].rp != 0) {   /* (its song's own effects:
                                          * their time counted here, apart from the mix's effects'; their code in
                                          * core 2's room, as the DJ FILTERs') */
            uint32_t us = PLAT_FAR(fx_strip_lr)(&pl_dj.strip[k], l, r, snd);
            DBG_ADD_US(DS_INSERTS, us);
            (void)us;
        }
        DBG_T(tf);
        PLAT_FAR(fx_djfilter)(&pl_dj.f_dry[k], l, r, pl_dj.cut8[k], pl_djm.reso);
        if (pl_djm.sends_filtered)
            for (s = 0; s < 3u; s++) {
                if (taken >> s & 1u)
                    PLAT_FAR(fx_djfilter)(&pl_dj.f_snd[k][s], snd + s * PL_BLOCK, 0, pl_dj.cut8[k], pl_djm.reso);
                else
                    PLAT_FAR(fx_djfilter_rest)(&pl_dj.f_snd[k][s], pl_dj.cut8[k]);
            }
        DBG_ADD(DS_DJFILTERS, tf);
        if (pl_dj.stop[k].kind == PL_STOP_ECHO || pl_dj.stop[k].kind == PL_STOP_REVERB) {
            int32_t *b = snd + (pl_dj.stop[k].kind == PL_STOP_ECHO ? 1u : 2u) * PL_BLOCK;
            if (!pl_dj.stop[k].stood)    /* (the deck's whole sound into it, till it stops) */
                for (i = 0; i < PL_BLOCK; i++)
                    b[i] += (l[i] + r[i]) >> 1;
        }
        /* VOLUME x the crossfader's law, ramped over the block */
        x8 = pl_dj.xf8 < 0 ? 0 : pl_dj.xf8 > (32 << 8) ? 32 << 8 : pl_dj.xf8;
        if (pl_djm.sharp) {
            g1 = k ? (x8 >= 2 << 8 ? 4096 : x8 * 8) : (x8 <= 30 << 8 ? 4096 : ((32 << 8) - x8) * 8);
        } else {
            int32_t x = k ? (32 << 8) - x8 : x8, i0 = x >> 8, f = x & 255;
            g1 = PL_COS_Q12[i0] + (i0 < 32 ? ((PL_COS_Q12[i0 + 1] - PL_COS_Q12[i0]) * f) >> 8 : 0);
        }
        g1 = v <= -25 ? 0 : (g1 * PL_VOL_Q12[(v < -24 ? -24 : v > 6 ? 6 : v) + 24]) >> 12;
        g0 = pl_dj.g_last[k];
        pl_dj.g_last[k] = g1;
        if (m)
            pl_dj_out(l, r, snd, g0, g1, 1);
        else
            pl_dj_out(l, r, snd, g0, g1, 0);
    }
#ifdef PTFM_DEBUG
    dbg_half[DS_DJMIX] += plat_us() - dm - (dbg_half[DS_DJFILTERS] + dbg_half[DS_INSERTS] - dm_parts);
#endif
    fx_force((int)(forced & 1u), (int)(forced & 2u));
    pl_use(lead);                        /* (the shared effects on the lead deck's clock) */
    return 1;
}

/* ---- requests ---- */
static void pl_post(uint8_t type, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t w = pl_rq_w, used = w - pl_rq_r, i;
    int keep = type == RQ_KEY_OFF;       /* (what is never lost) */
    for (i = 0; i < sizeof PL_LATE; i++)
        keep |= type == PL_LATE[i];
    if (used >= PL_RQ || (!keep && used >= PL_RQ - PL_RQ_KEEP)) {
        if (type == RQ_KEY_OFF)          /* (full: kept aside, taken at the next half) */
            pl_koff_req[a >> 5 & 3u] ^= 1u << (a & 31u);
        for (i = 0; i < sizeof PL_LATE; i++)
            if (type == PL_LATE[i])
                pl_late_req[i]++;
        return;                          /* (another request is dropped) */
    }
    pl_rq[w % PL_RQ].type = type;
    pl_rq[w % PL_RQ].a = a;
    pl_rq[w % PL_RQ].b = b;
    pl_rq[w % PL_RQ].c = c;
    pl_rq[w % PL_RQ].d = d;
    PL_BARRIER();
    pl_rq_w = w + 1u;
}

void player_play_song(uint8_t song, uint8_t pos, uint8_t row) { pl_post(RQ_SONG, song, pos, row, 0); }
static void pl_post_p(uint8_t type, uint8_t a, uint8_t b, uint8_t c, uint8_t d, const void *p);
void player_play_pattern(uint8_t pat, uint8_t row) { pl_post_p(RQ_PATTERN, pat, row, 0, 0, 0); }
void player_play_pattern_at(uint8_t pat, uint8_t row, uint16_t tempo, uint8_t ticks, uint8_t rpb, uint8_t swing)
{
    pl_post_p(RQ_PATTERN, pat, row, ticks, rpb, (const void *)(uintptr_t)((uint32_t)tempo | (uint32_t)swing << 16));
}
void player_pause(void) { pl_post(RQ_PAUSE, 0, 0, 0, 0); }
void player_continue(void) { pl_post(RQ_CONTINUE, 0, 0, 0, 0); }
void player_to_start(void) { pl_post(RQ_TO_START, 0, 0, 0, 0); }
void player_stop(void) { pl_post(RQ_STOP, 0, 0, 0, 0); }
void player_skip(int positions) { pl_post(RQ_SKIP, (uint8_t)(int8_t)(positions < -128 ? -128 : positions > 127 ? 127 : positions), 0, 0, 0); }
void player_all_sound_off(void) { pl_post(RQ_CUT, 0, 0, 0, 0); }
void player_hush(int on) { pl_hush_want = (uint8_t)(on != 0); }
void player_mutes(uint32_t muted) { pl_post(RQ_MUTES, (uint8_t)muted, (uint8_t)(muted >> 8), 0, 0); }
void player_voice_count(int n) { pl_post(RQ_VOICES, (uint8_t)(n < 1 ? 1 : n > PL_VOICES_MOST ? PL_VOICES_MOST : n), 0, 0, 0); }   /* (applied at a half's start: pl_nv_half) */
void player_song_changed(uint8_t song) { pl_post(RQ_SONG_CHANGED, song, 0, 0, 0); }
void player_store_changed(void) { pl_post(RQ_STORE, 0, 0, 0, 0); }
void player_key_on(uint8_t note, pl_inst_t inst, uint8_t vel) { pl_post(RQ_KEY_ON, note, inst.cart, inst.slot, vel); }
void player_key_off(uint8_t note) { pl_post(RQ_KEY_OFF, note, 0, 0, 0); }
void player_keys_off(void) { pl_post(RQ_KEYS_OFF, 0, 0, 0, 0); }
void player_keys_bend(int16_t bend64) { pl_bend_req = bend64; PL_BARRIER(); pl_bend_seq++; }   /* (the latest wins) */
void (*volatile player_note_hook)(uint8_t track, uint8_t note, uint8_t vel);
void player_punch(int k, int down) { pl_post(RQ_PUNCH, (uint8_t)k, (uint8_t)(down != 0), 0, 0); }
void player_take_cell(int track, int row, uint32_t at, int off)
{
    if (track >= 0 && track < PL_TRACKS && row >= 0 && row < 256)
        pl_post_p(RQ_TAKE_CELL, (uint8_t)track, (uint8_t)row, (uint8_t)(off != 0), 0, (const void *)(uintptr_t)at);
}
void player_fx_cmd(uint8_t code, uint8_t value) { pl_post(RQ_FX_CMD, code, value, 0, 0); }
void player_source_changed(int rows_changed) { pl_post(RQ_SRC_CHANGED, (uint8_t)(rows_changed != 0), 0, 0, 0); }

static void pl_source_post(uint8_t what, const pl_source_t *src, uint8_t pos, uint8_t row, uint8_t silent)
{
    uint32_t w = pl_rq_w;
    if (w - pl_rq_r >= PL_RQ - PL_RQ_KEEP)
        return;
    pl_rq[w % PL_RQ].type = RQ_SOURCE;
    pl_rq[w % PL_RQ].a = what;
    pl_rq[w % PL_RQ].b = pos;
    pl_rq[w % PL_RQ].c = row;
    pl_rq[w % PL_RQ].d = silent;
    pl_rq[w % PL_RQ].ptr = src;
    PL_BARRIER();
    pl_rq_w = w + 1u;
}
void player_play_source(uint8_t what, const pl_source_t *src, uint8_t pos, uint8_t row) { pl_source_post(what, src, pos, row, 0); }
void player_play_source_silent(const pl_source_t *src, uint8_t place) { pl_source_post(PL_LOOPS, src, 0, 0, (uint8_t)(place + 1u)); }
void player_source_end(uint8_t what) { player_play_source(what, 0, 0, 0); }
/* LOOPS' lanes (player.h) */
void player_lp_mutes(uint32_t muted, uint32_t soloed)
{
    pl_post_p(RQ_LP_MUTES, (uint8_t)muted, (uint8_t)(muted >> 8), (uint8_t)soloed, (uint8_t)(soloed >> 8), 0);
}
void player_lp_mutes_at_bar(uint32_t muted, uint32_t soloed, int bar_rows)
{
    pl_post_p(RQ_LP_MUTES, (uint8_t)muted, (uint8_t)(muted >> 8), (uint8_t)soloed, (uint8_t)(soloed >> 8),
              (const void *)(uintptr_t)(uint32_t)(bar_rows < 1 ? 1 : bar_rows > 255 ? 255 : bar_rows));
}
void player_lp_note(int loop, int lane, uint8_t note, uint8_t vol, int row, int delay, unsigned glide)
{
    if (loop < 0 || (loop >= (int)PL_LPV && loop != PL_LP_KEYS) || lane < 0 || lane > 3)
        return;
    pl_post_p(RQ_LP_NOTE, (uint8_t)(4 * loop + lane), note, vol, (uint8_t)(row < 0 || row > 254 ? 0xFF : row),
              (const void *)(uintptr_t)((uint32_t)(delay < 0 ? 0 : delay > 15 ? 15 : delay) | (glide & 255u) << 8 |
                                        (glide & PL_LP_LEG ? 16u : 0u)));
}
void player_lp_keys(int lane, pl_inst_t in, uint8_t note, uint8_t vol, int row, unsigned glide)
{
    if (lane < 0 || lane > 3)
        return;
    pl_post_p(RQ_LP_NOTE, (uint8_t)(PL_LPK + (uint32_t)lane), note, vol, (uint8_t)(row < 0 || row > 254 ? 0xFF : row),
              (const void *)(uintptr_t)((glide & 255u) << 8 | (glide & PL_LP_LEG ? 16u : 0u) |
                                        (uint32_t)in.cart << 16 | (uint32_t)in.slot << 24));
}
void player_lp_off(int loop, int lane, int row) { player_lp_note(loop, lane, REC_NOTE_OFF, 0, row, 0, 0); }
void player_lp_take(int loop) { pl_post(RQ_LP_TAKE, (uint8_t)(loop < 0 || loop >= (int)PL_LPV ? 0 : loop + 1), 0, 0, 0); }
void player_lp_preview(const pl_lp_grid_t *g)
{
    if (g) {                             /* (the grid copied: taken whole at the next half) */
        pl_lpv_seq++;
        PL_BARRIER();
        pl_lpv_in = *g;
        PL_BARRIER();
        pl_lpv_seq++;
    }
    pl_post(RQ_LP_PREVIEW, (uint8_t)(g != 0), 0, 0, 0);
}
void player_lp_preview_alone(int on) { pl_post(RQ_LP_ALONE, (uint8_t)(on != 0), 0, 0, 0); }
void player_lp_rest(int how) { pl_post(RQ_LP_REST, (uint8_t)(how < 0 ? 0 : how > 2 ? 2 : how), 0, 0, 0); }
void player_lp_preview_keep(int loop) { pl_post(RQ_LP_KEEP, (uint8_t)(loop < 0 ? 0xFF : loop), 0, 0, 0); }
void player_fx_reload(void) { pl_post(RQ_FX_RELOAD, 0, 0, 0, 0); }
void player_fx_bypass(int on) { pl_post(RQ_BYPASS, (uint8_t)(on != 0), 0, 0, 0); }
void player_click(int accent) { pl_post(RQ_CLICK, (uint8_t)(accent != 0), 0, 0, 0); }
void player_count_in(int bars, int bpm10)
{
    pl_post(RQ_COUNT, (uint8_t)(bars < 1 ? 1 : bars > 2 ? 2 : bars), (uint8_t)bpm10, (uint8_t)(bpm10 >> 8), 0);
}
void player_count_stop(void) { pl_post(RQ_COUNT, 0, 0, 0, 0); }
void player_step(int rows) { pl_post(RQ_STEP, (uint8_t)(int8_t)(rows < 0 ? -1 : rows > 0 ? 1 : 0), 0, 0, 0); }
void player_arp(int on, uint8_t rhythm, uint8_t pattern, uint8_t latch) { pl_post(RQ_ARP, (uint8_t)(on != 0), rhythm, pattern, latch); }
void player_arp_clear(void) { pl_post(RQ_ARP_CLEAR, 0, 0, 0, 0); }
void player_arp_start(uint8_t note, pl_inst_t inst, uint8_t vel) { pl_post(RQ_ARP_START, note, inst.cart, inst.slot, vel); }
void player_arp_inst(pl_inst_t inst) { pl_post(RQ_ARP_INST, inst.cart, inst.slot, 0, 0); }

/* DJ's decks (player.h) */
static void pl_post_p(uint8_t type, uint8_t a, uint8_t b, uint8_t c, uint8_t d, const void *p)
{
    uint32_t w = pl_rq_w;
    if (w - pl_rq_r >= PL_RQ - PL_RQ_KEEP)
        return;
    pl_rq[w % PL_RQ].type = type;
    pl_rq[w % PL_RQ].a = a;
    pl_rq[w % PL_RQ].b = b;
    pl_rq[w % PL_RQ].c = c;
    pl_rq[w % PL_RQ].d = d;
    pl_rq[w % PL_RQ].ptr = p;
    PL_BARRIER();
    pl_rq_w = w + 1u;
}
void player_dj(int on, const uint8_t *fx_setup) { pl_post_p(RQ_DJ, (uint8_t)(on != 0), 0, 0, 0, fx_setup); }
uint32_t player_dj_mem_size(void) { return (uint32_t)sizeof(pl_d2_t) + ibxm_arena_size() + 8u; }
void player_dj_mem(void *mem) { pl_post_p(RQ_DJ_MEM, 0, 0, 0, 0, mem); }
static PLAT_COLD void pl_d2_set(void *mem)   /* the second deck's memory, or none (the deck on it pauses, empty) */
{
    pl_ctx_t *c = &pl_ctxs[1];
    uint32_t i;
    if (mem)                             /* (8-byte aligned: the arena's tables hold pointers) */
        mem = (void *)(((uintptr_t)mem + 7u) & ~(uintptr_t)7u);
    if (pl_d2 == mem)
        return;
    if (PL_RARE(pl_swapped))             /* (DJ's SWAP undone first: the second replay is memory 1's) */
        pl_dj_swap();
    pl_use(1);                           /* (the second replay pauses: its tracks and pattern change place) */
    pl_pause();
    pl_rp = 0;
    pl_gp_num = -1;
    if (pl_d2 && !mem)
        pl_song = 0xFF;
    for (i = 0; i < PL_NV; i++)          /* (its voices go: they belong to the tracks that move) */
        if (pl_v[i].use == PV_SONG && (pl_v[i].who >> 4 & 1u))
            pl_v[i].cut = 1;
    pl_chorus_tail[1][0] = pl_chorus_tail[1][1] = 0;
    pl_d2 = (pl_d2_t *)mem;
    if (pl_d2) {
        memset(pl_d2, 0, sizeof *pl_d2);
        for (i = 0; i < PL_NCH; i++)
            pl_d2->ch[i].voice = -1;
        pl_dj.sgain[0] = pl_dj.sgain[1] = 4096;
        pl_dj.mix8 = -1;
        pl_dj.mix_fx = -1;
        c->ch = pl_d2->ch;
        c->gp = &pl_d2->gp;
        c->gp_cur = pl_d2->gp_cur;
        c->cells = pl_d2->cells;
    } else {                             /* (the second replay shares the first's again) */
        c->ch = pl_chs;
        c->gp = &pl_gp0;
        c->gp_cur = pl_gp_cur0;
        c->cells = pl_cells0;
        pl_ctxs[0].gp_num = -1;
    }
    ibxm_arena_mem1(pl_d2 ? (uint8_t *)pl_d2 + sizeof *pl_d2 : 0);
    pl_use(0);
}
void player_dj_set(const player_dj_t *m)
{
    pl_djm_seq++;                        /* (odd: being written) */
    PL_BARRIER();
    pl_djm_in = *m;
    PL_BARRIER();
    pl_djm_seq++;
}
void player_deck_load(int d, uint8_t song) { pl_post_p(RQ_DECK_LOAD, (uint8_t)(d & 1), song, 0, 0, 0); }
void player_deck_play(int d, int on, uint8_t pos, uint8_t row) { pl_post_p(RQ_DECK_PLAY, (uint8_t)(d & 1), (uint8_t)(on != 0), pos, row, 0); }
void player_deck_arm(int d, uint8_t pos, uint8_t row) { pl_post_p(RQ_DECK_ARM, (uint8_t)(d & 1), pos, row, 1, 0); }
void player_deck_place(int d, uint8_t pos, uint8_t row) { pl_post_p(RQ_DECK_PLACE, (uint8_t)(d & 1), pos, row, 0, 0); }
void player_tempo_share(uint32_t q16)
{
    uint32_t s = q16 < PL_SHARE_MIN ? PL_SHARE_MIN : q16 > PL_SHARE_MAX ? PL_SHARE_MAX : q16;
    pl_post_p(RQ_TEMPO_SHARE, 0, 0, 0, 0, (const void *)(uintptr_t)s);
}
void player_song_volume(int db) { pl_post(RQ_SONG_VOLUME, (uint8_t)(int8_t)(db < -25 ? -25 : db > 6 ? 6 : db), 0, 0, 0); }
void player_end_mode(uint8_t mode, uint8_t next) { pl_post(RQ_END_MODE, mode, next, 0, 0); }
void player_cue(uint8_t song) { pl_post(RQ_CUE, song, 0, 0, 0); }
void player_unload(void) { pl_post(RQ_UNLOAD, 0, 0, 0, 0); }
void player_cue_loops(const pl_source_t *src) { pl_post_p(RQ_CUE_LOOPS, 0, 0, 0, 0, src); }
void player_editing(uint8_t what, uint32_t key) { pl_post_p(RQ_EDITING, what, 0, 0, 0, (const void *)(uintptr_t)key); }
void player_fx_mine(uint8_t e, uint64_t codes)
{
    pl_post_p(RQ_FX_MINE, e, (uint8_t)(codes >> 32), (uint8_t)(codes >> 40), 0, (const void *)(uintptr_t)(uint32_t)codes);
}
void player_deck_tempo(int d, uint32_t share_q16, uint16_t fixed_bpm10)
{
    uint32_t s = share_q16 < PL_SHARE_MIN ? PL_SHARE_MIN : share_q16 > PL_SHARE_MAX ? PL_SHARE_MAX : share_q16;
    pl_post_p(RQ_DECK_TEMPO, (uint8_t)(d & 1), (uint8_t)fixed_bpm10, (uint8_t)(fixed_bpm10 >> 8), 0, (const void *)(uintptr_t)s);
}
void player_deck_mutes(int d, uint32_t muted) { pl_post_p(RQ_DECK_MUTES, (uint8_t)(d & 1), (uint8_t)muted, (uint8_t)(muted >> 8), 0, 0); }
void player_deck_stop(int d, int kind, int down, int stay)
{
    pl_post_p(RQ_DECK_STOP, (uint8_t)(d & 1), (uint8_t)kind, (uint8_t)(down != 0), (uint8_t)(stay != 0), 0);
}
void player_deck_loop(int d, int kind, uint8_t pos, uint8_t row, uint16_t rows, int roll)
{
    pl_post_p(RQ_DECK_LOOP, (uint8_t)((d & 1) | (kind & 3) << 1 | (roll ? 8 : 0)), pos, row, 0, (const void *)(uintptr_t)rows);
}
void player_deck_roll_end(int d) { pl_post_p(RQ_DECK_LOOP, (uint8_t)((d & 1) | 16), 0, 0, 0, 0); }
void player_dj_swap(uint8_t swaps) { pl_post_p(RQ_DJ_SWAP, 0, swaps, 0, 0, 0); }
void player_deck_hand(int d, uint8_t pos, uint8_t row, uint32_t ms)
{
    pl_post_p(RQ_DECK_HAND, (uint8_t)(d & 1), pos, row, 0, (const void *)(uintptr_t)(ms < 20u ? 20u : ms > 400u ? 400u : ms));
}

/* SWAP (specs/ui/screens/dj.md#the-main-and-swap): the decks exchange sides as they play. The two replays, each with
 * its memory (k: its tracks, its IBXM arena, its mix), change places in pl_ctxs, the decks' own state in pl_dj with
 * them, the crossfader and its LFO's swing mirrored: nothing heard changes. The mix's settings mirrored to match (the
 * main loop's next ones are for the new sides) */
static PLAT_COLD void pl_dj_swap(void)
{
    uint32_t a = pl_dj.arm_bar, j;
    pl_use(0);
    pl_cur->clk = fx_clk;                /* (the current replay's clock is fx_clk) */
    pl_swap_bytes(&pl_ctxs[0], &pl_ctxs[1], sizeof pl_ctxs[0]);
    fx_clk = pl_cur->clk;                /* (the new main's) */
    pl_swapped ^= 1u;
    PL_SWAP2(pl_dj.strip);
    PL_SWAP2(pl_dj.f_dry);
    PL_SWAP2(pl_dj.f_snd);
    PL_SWAP2(pl_dj.g_last);
    PL_SWAP2(pl_dj.sgain);
    PL_SWAP2(pl_dj.sbend);
    PL_SWAP2(pl_dj.pbend);
    PL_SWAP2(pl_dj.plain8);
    PL_SWAP2(pl_dj.plain_v);
    PL_SWAP2(pl_dj.heard);
    PL_SWAP2(pl_dj.cut8);
    PL_SWAP2(pl_dj.prev8);
    PL_SWAP2(pl_dj.sliding);
    PL_SWAP2(pl_dj.sl_from);
    PL_SWAP2(pl_dj.sl_n);
    PL_SWAP2(pl_dj.stop);
    PL_SWAP2(pl_dj.loop);
    PL_SWAP2(pl_dj.hand_q16);
    PL_SWAP2(pl_dj.hand_n);
    PL_SWAP2(pl_dj.ring);
    PL_SWAP2(pl_dj.ring_fx);
    PL_SWAP2(pl_dj.row_now);
    PL_SWAP2(pl_dj.bar_now);
    PL_SWAP2(pl_dj.row_seen);
    PL_SWAP2(pl_dj.row_ago);
    PL_SWAP2(pl_dj.lfo_cut);
    pl_swap_bytes(&pl_dj.lfo[PL_LFO_FILT_L], &pl_dj.lfo[PL_LFO_FILT_R], sizeof pl_dj.lfo[0]);
    pl_dj.arm_bar = (uint8_t)((a & ~3u) | (a & 1u) << 1 | (a >> 1 & 1u));
    pl_dj.xf8 = (32 << 8) - pl_dj.xf8;   /* (its LFO's state stays: pl_dj_lfo_step works it by pl_swapped) */
    for (j = 0; j < PL_LFO_STEPS; j++)
        pl_dj.lfo_xf[j] = (32 << 8) - pl_dj.lfo_xf[j];
    pl_djm_mirror(&pl_djm);
}

static PLAT_COLD void pl_dj_off(void)    /* the decks are no longer what plays: as a song's replays again */
{
    uint32_t k;
    if (!PL_DJ_ON)
        return;
    if (pl_swapped) {                    /* (each replay back on its memory's side, as the second deck's memory goes:
                                          * both paused, the main's song stays the one held, at its place, its module
                                          * built again when it plays) */
        pl_ctx_t *c = &pl_ctxs[0], *mn = &pl_ctxs[1];
        pl_dj_swap();
        pl_use(1);
        pl_pause();
        pl_use(0);
        pl_pause();
        c->state = mn->state == PS_END ? PS_END : PS_IDLE;
        c->song = mn->song;
        c->sd = mn->sd;
        c->sd_ok = mn->sd_ok;
        c->kept_pos = mn->kept_pos;
        c->kept_row = mn->kept_row;
        c->ended = mn->ended;
        c->order = mn->order;
        c->len = mn->len;
        c->rp = 0;
        c->gp_num = -1;
    }
    pl_dj_gen = 0;
    pl_dj.on = 0;
    fx_strip_noise(0);                   /* (the mix's noise from where the decks' DUST got to) */
    if (pl_dj.mix_fx >= 0) {             /* (the FX LFO's effect back to its MIX as set) */
        uint32_t was = (uint32_t)pl_dj.mix_fx;
        pl_fx_turn(PL_MIXK[was], pl_djm.mix_set[was == 0u ? 2 : was == 1u ? 1 : 0]);
        pl_dj.mix_fx = -1;
    }
    fx_force(0, 0);
    for (k = 0; k < 2u; k++) {
        pl_ctx_t *c = &pl_ctxs[k];
        c->dj = 0;
        c->arm = 0;
        c->bpm_fix = 0;
        c->tscale = 65536u;
        c->key64 = 0;
        memset(&pl_dj.stop[k], 0, sizeof pl_dj.stop[k]);
        memset(&pl_dj.loop[k], 0, sizeof pl_dj.loop[k]);
        pl_dj.sgain[k] = 4096;
        pl_dj.sbend[k] = pl_dj.pbend[k] = 0;
        pl_dj.ring[k] = 0;
    }
    pl_dj.arm_bar = 0;
    pl_d2_set(0);                        /* (the second deck's memory given up: the main loop returns it) */
}

static void pl_dj_on(const uint8_t *setup)   /* the decks are what plays, with DJ's FX setup on the shared effects */
{
    uint32_t k;
    if (!pl_d2)                          /* (no memory lent for DJ: no DJ) */
        return;
    pl_dj.setup = setup;
    if (!pl_dj.on) {
        pl_dj.on = 1;
        fx_strip_noise(1);               /* (the decks' DUST from where the mix's noise is) */
        memset(pl_dj.lfo, 0, sizeof pl_dj.lfo);
        memset(pl_dj.f_dry, 0, sizeof pl_dj.f_dry);
        memset(pl_dj.f_snd, 0, sizeof pl_dj.f_snd);
        pl_dj.mix8 = -1;
        pl_dj.mix_fx = -1;
        pl_dj.lfo_at = pl_halves - 1u;   /* (the LFOs worked out afresh in the first block) */
        pl_dj.row_seen[0] = pl_dj.row_seen[1] = 0;
        pl_dj.arm_bar = 0;
        pl_dj_gen = 0;                   /* (the stand's count starts again as the decks go live: dj.c) */
        for (k = 0; k < 2u; k++) {
            pl_dj.g_last[k] = 0;
            pl_dj.sgain[k] = 4096;
            pl_dj.sbend[k] = pl_dj.pbend[k] = 0;
            pl_dj.plain8[k] = pl_dj.plain_v[k] = 0;
            pl_dj.heard[k] = 0;
        }
        pl_use(1);                       /* (what plays stops: the loops, whose tracks the other deck plays on) */
        pl_lpv.on = pl_lpv.alone = 0;
        if (pl_src || pl_what == PL_LOOPS)
            pl_pause();
        pl_src = 0;
        pl_use(0);                       /* (a song or the pattern loop) */
        pl_pause();
        if (pl_what != PL_DJ)
            pl_song = 0xFF;
        pl_cut_all();                    /* (and its sound cut: every voice out, the effects' tails emptied: DJ
                                          * starts dry) */
        for (k = 0; k < 2u; k++) {
            pl_ctxs[k].dj = 1;
            fx_strip_setup(&pl_dj.strip[k], pl_ctxs[k].sd_ok ? pl_ctxs[k].sd.fx : 0);
        }
        if (pl_ctxs[1].state == PS_IDLE)
            pl_ctxs[1].song = 0xFF;
        pl_what = PL_DJ;
        pl_lcue = 0;
    }
    fx_setup(setup);
}

static void pl_deck_rq(const pl_rq_t *q)  /* a DJ deck's request */
{
    uint32_t d = q->a & 1u;
    pl_stop_t *s = &pl_dj.stop[d];
    pl_dloop_t *lp = &pl_dj.loop[d];
    struct ibxm_fm1_state st;
    pl_use(d);
    switch (q->type) {
    case RQ_DECK_LOAD:
        if (d && !pl_d2) {               /* (no memory for the second deck: it stays empty) */
            pl_song = 0xFF;
            break;
        }
        pl_pause();
        pl_cur->arm = 0;
        pl_dj.arm_bar &= (uint8_t)~(1u << d);
        memset(s, 0, sizeof *s);
        memset(lp, 0, sizeof *lp);
        pl_src = 0;
        pl_song = q->b;
        pl_kept_pos = pl_kept_row = 0;
        pl_ended = 0;
        pl_rp = 0;
        pl_sd_ok = q->b != 0xFFu && !rec_song(q->b, &pl_sd);
        if (!pl_sd_ok)
            pl_song = 0xFF;
        fx_strip_setup(&pl_dj.strip[d], pl_sd_ok ? pl_sd.fx : 0);
        break;
    case RQ_DECK_PLAY:
        if (q->b && pl_song != 0xFFu && pl_state == PS_STEP && pl_rp) {   /* (HOLD's hand let go: on from where it
                                                                          * left the deck, at once, a loop counted
                                                                          * from there) */
            pl_cur->arm = 0;
            pl_dj.arm_bar &= (uint8_t)~(1u << d);
            pl_dj.hand_n[d] = 0;
            pl_song_release();
            replay_jump_row(pl_rp, q->c, q->d);
            pl_state = PS_PLAY;
            pl_after_tick(1);
            pl_clock = 0;
            pl_ended = 0;
            if (lp->kind == PL_DLOOP_ROWS)
                lp->n = (uint16_t)(q->c == lp->pos && q->d >= lp->row ? q->d - lp->row : 0);
        } else if (q->b && pl_song != 0xFFu) {
            pl_cur->arm = 0;
            pl_dj.arm_bar &= (uint8_t)~(1u << d);
            pl_start(PL_DJ, pl_song, q->c, q->d);
        } else if (!q->b) {
            memset(s, 0, sizeof *s);
            pl_dj.arm_bar &= (uint8_t)~(1u << d);
            pl_pause();
        }
        break;
    case RQ_DECK_ARM:
        if (pl_song == 0xFFu)
            break;
        pl_cur->arm = 1;
        pl_start(PL_DJ, pl_song, q->b, q->c);
        if (q->d)
            pl_dj.arm_bar |= (uint8_t)(1u << d);
        break;
    case RQ_DECK_PLACE:
        if ((pl_state == PS_PLAY || pl_state == PS_STEP) && pl_rp) {   /* (on from there at once: its notes) */
            pl_song_release();
            replay_jump_row(pl_rp, q->b, q->c);
            pl_after_tick(1);
            pl_clock = 0;
            pl_ended = 0;
            if (lp->kind == PL_DLOOP_ROWS)
                lp->n = (uint16_t)(q->b == lp->pos && q->c >= lp->row ? q->c - lp->row : 0);
        } else if (pl_state == PS_ARMED || ((pl_state == PS_CHASE || pl_state == PS_BUILD) && pl_cur->arm)) {
            pl_start(PL_DJ, pl_song, q->b, q->c);
        } else if (pl_state != PS_HELD) {
            pl_kept_pos = q->b;
            pl_kept_row = q->c;
            pl_ended = 0;
            if (pl_state == PS_END)
                pl_state = PS_IDLE;
        }
        break;
    case RQ_DECK_TEMPO:
        pl_tscale = (uint32_t)(uintptr_t)q->ptr;
        pl_cur->bpm_fix = (uint16_t)(q->b | q->c << 8);
        break;
    case RQ_DECK_MUTES:
        pl_mute = q->b | (uint32_t)q->c << 8;
        break;
    case RQ_DECK_STOP:
        if (q->c) {                      /* down: one stop at a time, on a deck playing */
            if (s->kind || (pl_state != PS_PLAY && pl_state != PS_STEP) || !pl_rp || q->b == PL_STOP_NONE)
                break;
            memset(s, 0, sizeof *s);
            replay_fm1_state(pl_rp, &st);
            s->kind = q->b;
            s->held = 1;
            s->pos = (uint8_t)st.seq_pos;
            s->row = (uint8_t)st.row;
            s->row_frames = pl_row_frames();
            s->last_row = (uint8_t)st.row;
            if (s->kind == PL_STOP_BRAKE || s->kind == PL_STOP_BACKSPIN)
                pl_state = PS_HELD;      /* (the record's hand: its rows stand, the stop moves them) */
            else
                s->beat_wait = 1;
        } else if (s->kind) {            /* up: on in time where it would have been, or stays stopped */
            uint8_t kind = s->kind;
            s->held = 0;
            s->stay = q->d;
            if (s->stay) {
                if (s->stood) {
                    s->kind = PL_STOP_NONE;
                    pl_pause();
                }
                break;                   /* (a stop still running: paused once it stands) */
            }
            s->kind = PL_STOP_NONE;
            s->stood = 0;
            if (pl_state == PS_HELD)
                pl_dj_land(s->pos, s->row, s->frames, s->row_frames);
            if (kind == PL_STOP_BRAKE || kind == PL_STOP_BACKSPIN)
                s->spin = PL_SPINUP_F;
        }
        break;
    case RQ_DECK_HAND: {                 /* HOLD's hand (dj.md#hold): the row at (b, c) heard as if it came next, its
                                          * ticks spread over the time since the hand's last click (ptr: ms), then held,
                                          * its notes ringing, as a step */
        uint32_t ms = (uint32_t)(uintptr_t)q->ptr, sp = pl_speed ? pl_speed : 6u;
        if (pl_song == 0xFFu)
            break;
        pl_cur->arm = 0;
        pl_dj.arm_bar &= (uint8_t)~(1u << d);
        if (pl_rp && (pl_state == PS_STEP || pl_state == PS_IDLE || pl_state == PS_END)) {
            pl_dj.hand_q16[d] = (ms * 441u / 10u / sp) << 16;
            pl_dj.hand_n[d] = (uint8_t)(sp - 1u);
            replay_jump_row(pl_rp, q->b, q->c);
            pl_state = PS_STEP;
            pl_ended = 0;
            pl_after_tick(1);
            pl_clock = 0;
        } else {                         /* (no replay to move yet, or a start under way: started there, stepped) */
            pl_dj.hand_n[d] = 0;
            pl_step_after = 1;
            pl_start(PL_DJ, pl_song, q->b, q->c);
            if (pl_state == PS_PLAY)
                pl_state = PS_STEP;
            if (pl_state != PS_CHASE && pl_state != PS_BUILD)
                pl_step_after = 0;
        }
        break;
    }
    case RQ_DJ_SWAP:                     /* (b: the stand's count of SWAPs; a lost one mended by the next) */
        if ((q->b ^ pl_dj_gen) & 1u)
            pl_dj_swap();
        pl_dj_gen = q->b;
        break;
    case RQ_DECK_LOOP:
        if (q->a & 16u) {                /* a ROLL let go: on from where the roll is, out through the loop's end */
            lp->kind = PL_DLOOP_NONE;
            lp->roll = 0;
            break;
        }
        lp->kind = (uint8_t)(q->a >> 1 & 3u);
        if (!lp->kind) {
            lp->roll = 0;
            break;
        }
        lp->pos = q->b;
        lp->row = q->c;
        lp->rows = (uint16_t)(uintptr_t)q->ptr;
        lp->rows = lp->rows ? lp->rows : 1u;
        lp->n = 0;
        if (pl_rp && (pl_state == PS_PLAY || pl_state == PS_STEP || pl_state == PS_HELD)) {
            replay_fm1_state(pl_rp, &st);
            if (lp->kind == PL_DLOOP_ROWS && st.seq_pos == lp->pos && st.row >= lp->row)
                lp->n = (uint16_t)(st.row - lp->row);
            if (q->a & 8u)               /* a ROLL: held */
                lp->roll = 1;
            if ((lp->kind == PL_DLOOP_ROWS && lp->n + 1u >= lp->rows) ||
                (lp->kind == PL_DLOOP_PATTERN && (uint32_t)st.row + 1u >= pl_rows_of((uint32_t)st.seq_pos)))
                replay_queue_row(pl_rp, lp->pos, lp->kind == PL_DLOOP_PATTERN ? 0 : lp->row);
        } else if (!(q->a & 8u)) {
            lp->roll = 0;
        }
        if (!(q->a & 8u))
            lp->roll = 0;
        break;
    default:
        break;
    }
}

void player_costs(player_costs_t *c, int reset)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        c->eng_us[i] = pl_eng_us[i];
        c->eng_blocks[i] = pl_eng_blocks[i];
        if (reset)
            pl_eng_us[i] = pl_eng_blocks[i] = 0;   /* (a race with the audio context costs a block's figure) */
    }
    for (i = 0; i < 11u; i++) {
        c->fx_us[i] = fx_us[i] + (i < 4u ? fx_strip_us[i] : 0u);   /* (GATE .. LO-FI: the decks' own too) */
        if (reset)
            fx_us[i] = 0;
        if (reset && i < 4u)
            fx_strip_us[i] = 0;
    }
    c->blocks = pl_blocks;
    if (reset)
        pl_blocks = 0;
}

int player_master(int16_t *lr, int n)
{
    uint32_t w = pl_master_w, i;
    n = n < 0 ? 0 : n > (int)PL_MASTER ? (int)PL_MASTER : n;
    for (i = 0; i < (uint32_t)n; i++) {
        uint32_t j = (w + PL_MASTER - (uint32_t)n + i) % PL_MASTER;
        lr[2u * i] = pl_master[2u * j];
        lr[2u * i + 1u] = pl_master[2u * j + 1u];
    }
    return n;
}

void player_levels(uint16_t *lv)
{
    uint32_t t, b = PL_TRACKS * pl_ctxs[0].k;   /* (the main's tracks: DJ's SWAP moves it to the second memory's) */
    for (t = 0; t < PL_TRACKS; t++) {
        lv[t] = pl_tpeak[b + t];
        pl_tpeak[b + t] = 0;             /* (a race with the audio context costs one block's peak) */
    }
}

/* a source's length changed while it plays: its replay built again where it is (past its new end: at its start, LOOPS'
 * lanes beginning a pass there) */
static void pl_src_rebuild(void)
{
    struct ibxm_fm1_state st;
    struct ibxm_fm1_chan c;
    uint32_t t, pos, row;
    replay_fm1_state(pl_rp, &st);
    pl_order = pl_sd.order;
    pl_len = pl_sd_ok ? pl_sd.len : 0;
    if (!pl_len || !pl_ib_build()) {
        pl_state = PS_IDLE;
        return;
    }
    pos = st.seq_pos < (int)pl_len ? (uint32_t)st.seq_pos : 0u;
    row = st.row < pl_mod->patterns[pl_order[pos]].num_rows ? (uint32_t)st.row : 0u;
    replay_set_position_row(pl_rp, (int)pos, (int)row);
    for (t = 0; t < (uint32_t)pl_mod->num_channels; t++)
        replay_fm1_chan(pl_rp, (int)t, &c);   /* (the row's notes are sounding already) */
    if (PL_RARE(PL_LANES_SRC(pl_cur)) && row != (uint32_t)st.row && pl_state == PS_PLAY)
        pl_lp_row(0, 1);
}

/* ---- LOOPS' lanes: the main loop's requests (player.h), on the second replay (pl_cur) ---- */
/* a note (REC_NOTE_OFF: an off) for lane i: at row `row` of the loops (0xFF: at once), its delay counted from then
 * (delay's 16: a legato, LEG) */
static void pl_lp_rq_note(uint32_t i, uint32_t note, uint32_t vol, uint32_t row, uint32_t delay, uint32_t glide,
                          pl_inst_t in)
{
    if (!PL_LANES_SRC(pl_cur) || i >= PL_NLN || (i < 4u * PL_LPV && (i >> 2) >= pl_lpn))
        return;
    if (row != 0xFFu && pl_rp && pl_state == PS_PLAY && !pl_lpv.alone) {
        struct ibxm_fm1_state st;
        uint32_t len = (uint32_t)pl_mod->patterns[pl_order[0]].num_rows, d;
        replay_fm1_state(pl_rp, &st);
        d = (row + len - (uint32_t)st.row) % len;
        if (row < len && d && d <= len / 2u && pl_lpq_n < PL_LPQ) {   /* (a row ahead: then; else at once, as close to
                                                                       * its row as it can) */
            pl_lpq[pl_lpq_n].ln = (uint8_t)i;
            pl_lpq[pl_lpq_n].note = (uint8_t)note;
            pl_lpq[pl_lpq_n].vol = (uint8_t)vol;
            pl_lpq[pl_lpq_n].row = (uint8_t)row;
            pl_lpq[pl_lpq_n].delay = (uint8_t)delay;
            pl_lpq[pl_lpq_n].in = in;
            pl_lpq[pl_lpq_n++].glide = (uint8_t)glide;
            return;
        }
    }
    if (i >= PL_LPK && note != REC_NOTE_OFF)
        pl_lpk_in[i & 3u] = in;
    pl_ln_event(i, note, vol, delay & 16u ? (uint32_t)LF_LEG : 0u, glide, delay & 15u);
}
static void pl_lp_take(int loop)         /* player_lp_take */
{
    if (loop >= 0) {
        pl_lpt.loop = (int8_t)loop;
        pl_lpt.state = PL_TAKE_ARMED;
    } else if (pl_lpt.state == PL_TAKE_ARMED) {
        pl_lpt.state = PL_TAKE_NONE;
    } else if (pl_lpt.state == PL_TAKE_RUNNING) {   /* (ended early: what it holds stops, its stream unread till the
                                                     * loops' next start) */
        pl_lpt.state = PL_TAKE_DONE;
        pl_lp_stop((uint32_t)pl_lpt.loop);
    }
}
/* the preview alone (player_lp_preview_alone): on, the loops pause where they are and the replay runs on (playing),
 * or runs from row 0 (not); off, the replay pauses, the loops' place as kept */
static void pl_lp_alone(int on)
{
    uint32_t i;
    if (!PL_LANES_SRC(pl_cur) || (on && !pl_lpv.on) || !on == !pl_lpv.alone)
        return;
    if (!on) {
        pl_lpv.alone = 0;
        pl_pause();
        pl_kept_pos = pl_lpv.kpos;
        pl_kept_row = pl_lpv.krow;
        return;
    }
    if (pl_rp && (pl_state == PS_PLAY || pl_state == PS_CHASE)) {
        struct ibxm_fm1_state st;
        replay_fm1_state(pl_rp, &st);
        pl_lpv.kpos = (uint8_t)(pl_state == PS_CHASE ? pl_go_pos : st.seq_pos);
        pl_lpv.krow = (uint8_t)(pl_state == PS_CHASE ? pl_go_row : st.row);
        if (pl_lpt.state == PL_TAKE_RUNNING)
            pl_lpt.state = PL_TAKE_DONE;
        pl_lpt.skip = -1;
        for (i = 0; i < pl_lpn; i++)
            pl_lp_stop(i);
        pl_lpq_n = 0;
        pl_lpv.alone = 1;
        return;
    }
    pl_lpv.kpos = pl_kept_pos;
    pl_lpv.krow = pl_kept_row;
    pl_lpv.alone = 1;
    pl_start(PL_LOOPS, 0, 0, 0);
}
/* player_lp_rest (RECORD LIVE): 1 the loops silent from the next row, their place kept (a replay just started: from
 * its first row); 0 on again from their place, on the next row; 2 to their start (playing: from the top on the next
 * row; silent or paused: waiting there). The jumps queued land as the row ends (replay_queue_row) */
static PLAT_COLD void pl_lpr_req(uint32_t how)
{
    struct ibxm_fm1_state st;
    uint32_t len;
    if (!PL_LANES_SRC(pl_cur) || pl_lpv.alone)
        return;
    pl_lpr.kr0 = 0xFF;
    if (pl_state != PS_PLAY && pl_state != PS_CHASE && pl_state != PS_BUILD && pl_state != PS_ARMED) {
        if (how == 2u)
            pl_kept_pos = pl_kept_row = 0;
        return;
    }
    if (how == 1u) {                     /* (a jump queued lands first, and is silent from there) */
        if (pl_lpr.on != 1u)
            pl_lpr.on = 3;
        return;
    }
    if (how == 0u && pl_lpr.on == 3u) {  /* (silence not begun: called off) */
        pl_lpr.on = 0;
        return;
    }
    if (how == 2u && pl_lpr.on == 3u) {
        pl_lpr.zero = 1;
        return;
    }
    if (how == 2u && pl_lpr.on == 1u) {
        pl_lpr.kr = 0;
        return;
    }
    if (how == 0u && pl_lpr.on != 1u)    /* (playing, or on its way: nothing to do) */
        return;
    if (pl_state != PS_PLAY || !pl_rp)   /* (still finding its place, or being made ready: from there) */
        return;
    replay_fm1_state(pl_rp, &st);
    len = (uint32_t)pl_mod->patterns[pl_order[0]].num_rows;
    if (how == 2u)
        pl_lpr.kr = 0;
    replay_queue_row(pl_rp, 0, pl_lpr.kr);
    pl_lpr.nat = (uint8_t)(len ? ((uint32_t)st.row + 1u) % len : 0u);
    pl_lpr.on = 2;
}
static void pl_lpv_off(void)             /* the preview gone (alone: the replay pauses, the loops' place kept) */
{
    pl_lp_stop(PL_LPV);
    pl_lp_alone(0);
    pl_lpv.on = 0;
}
static void pl_lpv_set(void)             /* player_lp_preview's grid taken (the main loop's copy, whole) */
{
    uint32_t q = pl_lpv_seq;
    pl_lp_grid_t g;
    PL_BARRIER();
    g = pl_lpv_in;
    PL_BARRIER();
    if ((q & 1u) || pl_lpv_seq != q)     /* (being written: its next request brings it) */
        return;
    if (!pl_lpv.on || g.mute || g.track != pl_lpv.g.track || g.inst.cart != pl_lpv.g.inst.cart ||
        g.inst.slot != pl_lpv.g.inst.slot)
        pl_lp_stop(PL_LPV);              /* (another sound, or none: what it holds stops) */
    if (pl_lpv.on && pl_lpv.alone && !pl_lpn && g.rows != pl_lpv.g.rows && pl_rp) {
        pl_lpv.g = g;                    /* (alone with no loops: its clock's rows changed) */
        pl_src_rebuild();
    }
    pl_lpv.g = g;
    pl_lpv.on = 1;
}
/* player_lp_preview_keep: REC wrote the preview into loop lo; its lanes' notes go on as the loop's (their voices its
 * track's, the loop's own notes let go first), the preview gone; alone, the loops on from here */
static void pl_lpv_keep(uint32_t lo)
{
    uint32_t j, k, f;
    if (!PL_LANES_SRC(pl_cur) || !pl_lpv.on || lo >= pl_lpn) {
        if (pl_lpv.on)
            pl_lpv_off();
        return;
    }
    pl_lp_stop(lo);
    for (j = 0; j < 4u; j++) {
        uint32_t a = 4u * PL_LPV + j, b = 4u * lo + j, wa = pl_ln_who(a);
        uint8_t gb = (uint8_t)(((pl_ln[b].gen + 8u) & ~7u) | j);
        for (k = 0; k < PL_NV; k++)
            if (pl_ln_mine(&pl_v[k], wa, pl_ln[a].gen)) {
                pl_v[k].who = (uint8_t)(16u + lo);
                pl_v[k].gen = gb;
            }
        for (f = 0; f < PL_SF; f++)
            if (pl_sf[f].on && pl_sf[f].lane == a) {
                pl_sf[f].lane = (uint8_t)b;
                pl_sf[f].who = (uint8_t)(16u + lo);
                pl_sf[f].gen = gb;
            }
        pl_ln[b] = pl_ln[a];
        pl_ln[b].gen = gb;
        pl_lbit(pl_lpb, b, pl_lpb[a >> 5] >> (a & 31u) & 1u);
        pl_lbit(pl_lpg, b, pl_lpg[a >> 5] >> (a & 31u) & 1u);
        pl_ln[a].note = pl_ln[a].wait = pl_ln[a].koff = pl_ln[a].slide = pl_ln[a].glide = 0;
        pl_ln[a].voice = -1;
        pl_ln[a].gate = -1;
        pl_lbit(pl_lpb, a, 0);
        pl_lbit(pl_lpg, a, 0);
    }
    pl_lpv.on = 0;
    pl_lpv.alone = 0;                    /* (the loops on from the replay's place, theirs dropped) */
}

/* the loops' hold for the keys over (player_cue_loops): what was held before held again, as if they never were */
static void pl_lcue_drop(void)
{
    if (!pl_lcue || pl_what != PL_LOOPS)
        return;
    pl_use(1);
    pl_src = 0;
    pl_what = pl_lcue_back;
    pl_lcue = 0;
    pl_use(0);                           /* (a song held, or one paused: its setup again; a paused one keeps its place and
                                          * its replay's time) */
    if (pl_what == PL_NONE)
        pl_cue_load();
    else
        fx_setup_keep(pl_sd_ok ? pl_sd.fx : 0, pl_mine_e, pl_mine_codes);
}

static PLAT_COLD void pl_handle(pl_rq_t q)   /* a request, in the audio context */
{
    /* (the transport as if the loops were never held: PLAY goes on with what was held before, LOOPS' PLAY starts them
     * as from nothing; a start the count-in holds, at its end) */
    if (pl_lcue && (q.type == RQ_SONG || q.type == RQ_PATTERN || q.type == RQ_SOURCE || q.type == RQ_CONTINUE ||
                    q.type == RQ_TO_START || q.type == RQ_STEP || q.type == RQ_SKIP || q.type == RQ_DJ) &&
        !(pl_cnt.on && (q.type == RQ_PATTERN || q.type == RQ_SOURCE)))
        pl_lcue_drop();
    {
        if (q.type == RQ_COUNT) {        /* the count-in: bars (0: none, what it held dropped), the tempo */
            uint32_t bpm10 = q.b | (uint32_t)q.c << 8;
            memset(&pl_cnt, 0, sizeof pl_cnt);
            if (q.a) {
                pl_cnt.on = 1;
                pl_cnt.beats = (uint8_t)(4u * q.a);
                pl_cnt.fpb = 26460000u / (bpm10 ? bpm10 : 1200u);   /* (44100 x 600 / BPM x 10: at 0.1 BPM 26.5 M) */
            }
            return;
        }
        if (pl_cnt.on && (q.type == RQ_PATTERN || q.type == RQ_SOURCE)) {
            pl_cnt.def = q;              /* (held till the count's end) */
            pl_cnt.has_def = 1;
            return;
        }
        if (q.type == RQ_DJ_MEM) {       /* (the second deck's memory, lent or taken back) */
            pl_d2_set((void *)(uintptr_t)q.ptr);
            return;
        }
        if (q.type >= RQ_DECK_LOAD) {    /* (DJ's decks) */
            if (PL_DJ_ON)
                pl_deck_rq(&q);
            return;
        }
        if (PL_DJ_ON && (q.type == RQ_PAUSE || q.type == RQ_CONTINUE || q.type == RQ_TO_START || q.type == RQ_STOP)) {
            uint32_t k;                  /* (the decks are what played last: PLAY pauses and resumes both) */
            for (k = 0; k < 2u; k++) {
                pl_use(k);
                if (q.type == RQ_CONTINUE) {
                    if (pl_state == PS_IDLE && pl_song != 0xFFu && !pl_ended)
                        pl_start(PL_DJ, pl_song, pl_kept_pos, pl_kept_row);
                    else if (pl_state == PS_ARMED)
                        pl_go();
                } else {
                    memset(&pl_dj.stop[k], 0, sizeof pl_dj.stop[k]);
                    pl_pause();
                    if (q.type != RQ_PAUSE)
                        pl_kept_pos = pl_kept_row = 0;
                }
            }
            return;
        }
        pl_use(PL_SECOND(pl_what) ? 1u : 0u);   /* (what plays: the loops, a preview on the second replay) */
        switch (q.type) {
        case RQ_SONG:
        case RQ_PATTERN:
            pl_dj_off();                 /* (a song or a pattern started: the decks, the loops, a source go) */
            pl_use(1);
            pl_lpv.on = pl_lpv.alone = 0;
            pl_pause();
            pl_src = 0;
            pl_use(0);
            pl_src = 0;
            if (q.type == RQ_SONG)
                pl_start(PL_SONG, q.a, q.b, q.c);
            else {                       /* (c, d, ptr: its own timing, ticks 0 none: player_play_pattern_at) */
                uint32_t p = (uint32_t)(uintptr_t)q.ptr;
                pl_lt.ticks = q.c;
                pl_lt.rpb = q.d;
                pl_lt.tempo = (uint16_t)p;
                pl_lt.swing = (uint8_t)(p >> 16);
                pl_start(PL_LOOP, q.a, 0, q.b);
            }
            break;
        case RQ_SOURCE:
            pl_dj_off();
            pl_carts_forget();
            pl_use(0);                   /* (the loops: on the second replay, tracks 16-31; the song pauses) */
            pl_pause();
            pl_src = 0;
            pl_use(1);
            pl_src = (const pl_source_t *)q.ptr;
            pl_lpv.alone = 0;            /* (the loops start: the preview with them; another source: none) */
            if (!PL_LANES_SRC(pl_cur))
                pl_lpv.on = 0;
            if (pl_src) {
                if (q.d && PL_LANES_SRC(pl_cur)) {   /* (RECORD LIVE: the loops silent from the first row, their place
                                                      * given: player_play_source_silent) */
                    pl_lpr.pend = 3;
                    pl_lpr.zero = 0;
                    pl_lpr.kr0 = (uint8_t)(q.d - 1u);
                }
                pl_start(q.a, 0, q.b, q.c);
            } else {                     /* (a source's end: what played before it is what PLAY continues) */
                pl_pause();
                pl_what = q.a;
                pl_lcue = 0;
            }
            break;
        case RQ_DJ:
            if (q.a)
                pl_dj_on((const uint8_t *)q.ptr);
            else if (PL_DJ_ON) {         /* (the decks pause, a song's replays again: the main deck's song, paused) */
                pl_use(1);
                pl_pause();
                pl_use(0);
                pl_pause();
                pl_dj_off();
                pl_what = pl_song != 0xFFu ? PL_SONG : PL_NONE;
                pl_lcue = 0;
                fx_setup(pl_sd_ok ? pl_sd.fx : 0);
            }
            break;
        case RQ_SRC_CHANGED:
            pl_gp_num = -1;              /* (its cells read again from the next row on) */
            if (pl_src) {                /* its song again (the mixer, the tempo), its tracks' sounds */
                pl_sd_ok = !pl_rec_song(&pl_sd);
                if (pl_sd_ok)
                    pl_mix_apply(1);
                if (pl_mod && pl_sd_ok) {
                    pl_mod->default_speed = pl_sd.ticks;
                    pl_mod->default_tempo = pl_sd.tempo;
                    pl_mod->default_gvol = pl_sd.gvol;
                }
                pl_src_insts();
                if (pl_sd_ok && pl_what == PL_LOOPS && (pl_state == PS_IDLE || pl_state == PS_END))
                    pl_cue_load();       /* (the loops not playing: their setup and time as now, for the keys) */
                if (PL_LANES_SRC(pl_cur))
                    pl_lp_changed();     /* (LOOPS: the loops read again, their cursors on their rows) */
            }
            if (pl_src && q.a && pl_rp && (pl_state == PS_PLAY || pl_state == PS_CHASE))
                pl_src_rebuild();        /* its length changed: the replay built again where it is */
            break;
        case RQ_PAUSE:
            if (PL_RARE(pl_lpv.alone))
                pl_lp_alone(0);          /* (the preview alone stops) */
            pl_pause();
            break;
        case RQ_CONTINUE:
            if (PL_RARE(pl_lpv.alone) && PL_LANES_SRC(pl_cur)) {   /* (the preview alone: the loops from their place,
                                                                * the preview with them) */
                pl_lpv.alone = 0;
                pl_start(PL_LOOPS, 0, pl_lpv.kpos, pl_lpv.krow);
                break;
            }
            if (pl_state == PS_PLAY || pl_state == PS_CHASE)
                break;
            if (pl_state == PS_STEP) {   /* stepped: plays on from the row it holds */
                pl_state = PS_PLAY;
                pl_clock = 0;
                break;
            }
            if (pl_what == PL_LOOP)
                pl_start(PL_LOOP, pl_loop_pat, 0, pl_kept_row);
            else if (pl_src)
                pl_start(pl_what, 0, pl_kept_pos, pl_kept_row);
            else
                pl_start(PL_SONG, pl_song, pl_ended ? 0 : pl_kept_pos, pl_ended ? 0 : pl_kept_row);
            break;
        case RQ_TO_START:
            if (PL_RARE(pl_lpv.alone)) { /* (the preview alone: the loops' place back to their start) */
                pl_lpv.kpos = pl_lpv.krow = 0;
                break;
            }
            pl_kept_pos = pl_kept_row = 0;
            if (pl_state == PS_PLAY || pl_state == PS_CHASE)
                pl_start(pl_what, pl_what == PL_LOOP ? pl_loop_pat : pl_song, 0, 0);
            break;
        case RQ_STOP:
            if (PL_RARE(pl_lpv.alone))
                pl_lp_alone(0);
            pl_pause();
            pl_kept_pos = pl_kept_row = 0;
            break;
        case RQ_SKIP:
            if (pl_state == PS_PLAY && pl_what == PL_SONG && pl_rp) {   /* playing: a clean start there at the row's
                                                                          * end (pl_after_tick, pl_clock_block) */
                struct ibxm_fm1_state st;
                int32_t from, to;
                replay_fm1_state(pl_rp, &st);
                from = pl_skip_to >= 0 ? pl_skip_to : st.seq_pos;
                to = from + (int8_t)q.a;
                to = to < 0 ? 0 : to >= (int32_t)pl_len ? (int32_t)pl_len - 1 : to;
                if (to != from) {
                    pl_skip_to = to;
                    pl_skip_rc = st.row_count;
                }
            } else if (pl_state == PS_CHASE && pl_what == PL_SONG && !pl_cur->arm) {   /* (a start under way, a clean
                                                                                      * K1 start too: from there instead) */
                int32_t to = (int32_t)pl_go_pos + (int8_t)q.a;
                to = to < 0 ? 0 : to >= (int32_t)pl_len ? (int32_t)pl_len - 1 : to;
                if (to != (int32_t)pl_go_pos)
                    pl_start(PL_SONG, pl_song, (uint8_t)to, 0);
            } else if (pl_state != PS_PLAY && pl_state != PS_CHASE) {   /* paused: where PLAY goes on */
                int32_t to = (int32_t)pl_kept_pos + (int8_t)q.a, len = pl_sd_ok ? pl_sd.len : 1;
                pl_kept_pos = (uint8_t)(to < 0 ? 0 : to >= len ? len - 1 : to);
                pl_kept_row = 0;
                pl_ended = 0;
            }
            break;
        case RQ_CUT:
            pl_cut_all();
            break;
        case RQ_TAKE_CELL:               /* (a: the track, b: the row, c: off; ptr: the row's count, 0 at once) */
            pl_take_cell(q.a, q.b, (uint32_t)(uintptr_t)q.ptr, q.c);
            break;
        case RQ_MUTES:                   /* (what plays: the song's tracks or a source's; on DJ, the main deck's; LOOPS'
                                          * lanes have theirs: player_lp_mutes) */
            pl_ctxs[0].mute = q.a | (uint32_t)q.b << 8;
            if (!PL_DJ_ON && !PL_LANES_SRC(&pl_ctxs[1]))
                pl_ctxs[1].mute = pl_ctxs[0].mute;
            break;
        case RQ_LP_MUTES: {              /* (LOOPS' mutes and solos: now, or at the next bar line, ptr its rows) */
            uint32_t m = q.a | (uint32_t)q.b << 8, s = q.c | (uint32_t)q.d << 8, bar = (uint32_t)(uintptr_t)q.ptr;
            if (bar) {
                pl_lpm.bm = (uint16_t)m;
                pl_lpm.bs = (uint16_t)s;
                pl_lpm.rows = (uint8_t)bar;
                pl_lpm.bar = 1;
                break;
            }
            pl_lpm.eff = pl_lp_silent(m, s);
            pl_lpm.bar = 0;
            if (PL_LANES_SRC(&pl_ctxs[1]))
                pl_ctxs[1].mute = pl_lpm.eff;
            break;
        }
        case RQ_LP_NOTE: {               /* (a: the lane, b: the note, c: its volume, d: its row (0xFF at once); ptr: its
                                          * delay, 16 a legato, its glide << 8, a keys' lane's instrument << 16) */
            uint32_t p = (uint32_t)(uintptr_t)q.ptr;
            pl_inst_t in = {(uint8_t)(p >> 16), (uint8_t)(p >> 24)};
            pl_use(1);
            pl_lp_rq_note(q.a, q.b, q.c, q.d, p & 31u, p >> 8 & 255u, in);
            break;
        }
        case RQ_LP_TAKE:
            pl_lp_take((int)q.a - 1);
            break;
        case RQ_LP_PREVIEW:
            pl_use(1);
            if (q.a)
                pl_lpv_set();
            else if (pl_lpv.on)
                pl_lpv_off();
            break;
        case RQ_LP_ALONE:
            pl_use(1);
            pl_lp_alone(q.a);
            break;
        case RQ_LP_KEEP:
            pl_use(1);
            pl_lpv_keep(q.a);
            break;
        case RQ_LP_REST:
            pl_use(1);
            pl_lpr_req(q.a);
            break;
        case RQ_VOICES:
            pl_nv_set = q.a;             /* (in force from this half: pl_nv_half) */
            break;
        case RQ_SONG_CHANGED:
            if (q.a == pl_song && pl_sd_ok && !pl_src) {
                pl_sd_ok = !rec_song(pl_song, &pl_sd);
                if (pl_sd_ok)                /* (the MIXER changed by hand: heard at once) */
                    pl_mix_apply(0);
                if (pl_mod && pl_sd_ok) {    /* (F00 and a start again: its own as it is now) */
                    pl_mod->default_speed = pl_sd.ticks;
                    pl_mod->default_tempo = pl_sd.tempo;
                }
                if (pl_sd_ok && pl_what == PL_NONE)   /* (held, nothing played: its tempo, swing, setup as now) */
                    pl_cue_load();
            }
            break;
        case RQ_TEMPO_SHARE:             /* (PLAYBACK's speed: the song's replay takes it now if that song plays) */
            pl_ps_share = (uint32_t)(uintptr_t)q.ptr;
            if (pl_what == PL_SONG && !PL_DJ_ON)
                pl_ctxs[0].tscale = pl_ps_share;
            break;
        case RQ_SONG_VOLUME:             /* (... and its volume: pl_voices takes it at once) */
            pl_sv_db = (int8_t)q.a;
            break;
        case RQ_END_MODE:                /* (SONGS' play mode, and PLAY NEXT's next song) */
            pl_end_mode = q.a <= PL_END_NEXT ? q.a : PL_END_OWN;
            pl_end_next = q.b;
            break;
        case RQ_UNLOAD:                  /* (UNLOAD SONG: what played last let go at once, a song, the EDITOR's pattern,
                                          * the loops or DJ's decks, as SOUND CUT cuts; nothing held, as at power-on:
                                          * the UI cues the keys' song next) */
            if (pl_what == PL_NONE || pl_what == PL_PREVIEW || pl_lcue)
                break;
            if (PL_DJ_ON) {              /* (as RQ_DJ's off: the decks pause, DJ's stand kept) */
                pl_use(1);
                pl_pause();
                pl_use(0);
                pl_pause();
                pl_dj_off();
            } else {
                pl_use(PL_SECOND(pl_what) ? 1u : 0u);
                pl_lpv.on = pl_lpv.alone = 0;
                pl_pause();
                pl_kept_pos = pl_kept_row = 0;
            }
            pl_cut_all();
            pl_what = PL_NONE;
            break;
        case RQ_CUE: {                   /* (nothing played yet: the keys' song's setup and time, for the keys) */
            rec_song_t s;
            if (pl_what != PL_NONE || PL_DJ_ON || rec_song(q.a, &s))
                break;
            pl_use(0);
            pl_song = q.a;
            pl_sd = s;
            pl_sd_ok = 1;
            pl_cue_load();
            break;
        }
        case RQ_CUE_LOOPS:               /* (LOOPS shown, nothing playing: the loops held on their replay for the keys,
                                          * nothing started; 0: LOOPS left, what was held before held again) */
            if (q.ptr) {
                rec_song_t s;
                uint32_t k, busy = 0;
                for (k = 0; k < 2u; k++)
                    busy |= pl_ctxs[k].state != PS_IDLE && pl_ctxs[k].state != PS_END;
                if (PL_DJ_ON || busy || (pl_what == PL_LOOPS && !pl_lcue) || pl_what == PL_PREVIEW ||
                    ((const pl_source_t *)q.ptr)->song(&s))
                    break;
                if (!pl_lcue)
                    pl_lcue_back = pl_what;
                pl_use(1);
                pl_src = (const pl_source_t *)q.ptr;
                pl_sd = s;
                pl_sd_ok = 1;
                pl_what = PL_LOOPS;
                pl_lcue = 1;
                pl_cue_load();
            } else
                pl_lcue_drop();
            break;
        case RQ_EDITING:                 /* (what the screen edits: the commands for it skipped) */
            pl_ed_what = q.a;
            pl_ed_key = (uint32_t)(uintptr_t)q.ptr;
            break;
        case RQ_FX_MINE:                 /* (the FX page's effect under the cursor, its knobs' codes) */
            pl_mine_e = q.a;
            pl_mine_codes = (uint32_t)(uintptr_t)q.ptr | (uint64_t)(q.b | (uint32_t)q.c << 8) << 32;
            break;
        case RQ_STORE: {
            uint32_t k;
            pl_carts_forget();
            for (k = 0; k < 2u; k++) {   /* (both replays read again) */
                pl_use(k);
                if (pl_state == PS_PLAY || pl_state == PS_CHASE || pl_state == PS_HELD || pl_state == PS_ARMED ||
                    pl_state == PS_BUILD)
                    pl_pause();
                if (k && PL_RARE(pl_lpv.alone)) {   /* (the preview alone: stopped, the loops' place as kept) */
                    pl_lpv.alone = 0;
                    pl_kept_pos = pl_lpv.kpos;
                    pl_kept_row = pl_lpv.krow;
                }
                pl_sd_ok = pl_song != 0xFFu && !pl_rec_song(&pl_sd);
                pl_rp = 0;
            }
            if (pl_what == PL_NONE && !PL_DJ_ON) {   /* (a song held, nothing played: its setup and time again) */
                pl_use(0);
                if (pl_sd_ok)
                    pl_cue_load();
            } else if (pl_lcue) {        /* (the loops held for the keys: theirs) */
                pl_use(1);
                pl_sd_ok = !pl_rec_song(&pl_sd);
                pl_cue_load();
            } else if (!PL_DJ_ON) {      /* (what played, paused now: its setup as the store has it, REVERT's too, as
                                          * its next start would read it; DJ's comes again with dj_load) */
                pl_use(PL_SECOND(pl_what) ? 1u : 0u);
                if (pl_src)
                    pl_sd_ok = !pl_rec_song(&pl_sd);
                fx_setup_keep(pl_sd_ok ? pl_sd.fx : 0, pl_mine_e, pl_mine_codes);
            }
            break;
        }
        case RQ_KEY_ON: {
            pl_inst_t in;
            in.cart = q.b;
            in.slot = q.c;
            if (pl_arp.on) {             /* the arpeggiator takes the key: a note of its set */
                int alone = pl_nkeys == 0;
                if (pl_nkeys < sizeof pl_keys)
                    pl_keys[pl_nkeys++] = q.a;
                pl_arp_add(q.a, alone);
                pl_arp.inst = in;
                pl_arp.vel = q.d;
            } else {
                pl_key_on(q.a, in, q.d);
            }
            break;
        }
        case RQ_KEY_OFF:
            if (pl_arp.on) {
                uint32_t i, k;
                for (i = k = 0; i < pl_nkeys; i++)
                    if (pl_keys[i] != q.a)
                        pl_keys[k++] = pl_keys[i];
                pl_nkeys = (uint8_t)k;
                pl_arp_lift(q.a);
            } else {
                pl_key_off(q.a);
            }
            break;
        case RQ_KEYS_OFF:
            pl_keys_release();
            break;
        case RQ_KEY_BEND:
            pl_keys_bend((int16_t)(q.a | q.b << 8));
            break;
        case RQ_PUNCH:
            fx_punch(q.a, q.b);
            break;
        case RQ_FX_CMD:
            fx_cmd(q.a, q.b);
            break;
        case RQ_FX_RELOAD:               /* (the song's, or the source's when it plays; DJ's on DJ) */
            if (PL_DJ_ON) {
                fx_setup(pl_dj.setup);
                break;
            }
            pl_sd_ok = !pl_rec_song(&pl_sd);
            fx_setup(pl_sd_ok ? pl_sd.fx : 0);
            break;
        case RQ_BYPASS:
            fx_bypass(q.a);
            break;
        case RQ_CLICK:
            fx_click(1, 60, q.a);        /* (BEEP, 60 %) */
            break;
        case RQ_ARP:
            pl_arp_set(q.a, q.b, q.c, q.d);
            break;
        case RQ_ARP_CLEAR:
            pl_arp.n = 0;
            pl_arp_silence();
            break;
        case RQ_ARP_START:               /* (FX LIVE: hands free, from the last note) */
            pl_arp.n = 1;
            pl_arp.notes[0] = q.a;
            pl_arp.inst.cart = q.b;
            pl_arp.inst.slot = q.c;
            pl_arp.vel = q.d;
            break;
        case RQ_ARP_INST:                /* (the current instrument changed: from the arpeggio's next note) */
            pl_arp.inst.cart = q.a;
            pl_arp.inst.slot = q.b;
            break;
        case RQ_STEP:
            pl_steps((int8_t)q.a);
            break;
        default:
            break;
        }
    }
}

/* the main loop's requests, in order; then those that found the queue full (key-offs, pauses, stops, all sound
 * off, the store read again, the keys let go: never lost), and MIDI's latest pitch bend */
static __attribute__((noinline)) void pl_requests_take(void)
{
    uint32_t i, k;
    pl_rq_t q;
    while (pl_rq_r != pl_rq_w) {
        PL_BARRIER();
        q = pl_rq[pl_rq_r % PL_RQ];
        pl_rq_r++;
        pl_handle(q);
    }
    memset(&q, 0, sizeof q);
    for (i = 0; i < sizeof PL_LATE; i++)
        if (pl_late_req[i] != pl_late_seen[i]) {
            pl_late_seen[i] = pl_late_req[i];
            q.type = PL_LATE[i];
            pl_handle(q);
        }
    for (k = 0; k < 4u; k++) {
        uint32_t pend = pl_koff_req[k] ^ pl_koff_ack[k];
        pl_koff_ack[k] ^= pend;
        for (i = 0; pend; i++, pend >>= 1)
            if (pend & 1u) {
                q.type = RQ_KEY_OFF;
                q.a = (uint8_t)(32u * k + i);
                pl_handle(q);
            }
    }
    if (pl_bend_seq != pl_bend_seen) {   /* (bends: only the latest counts) */
        pl_bend_seen = pl_bend_seq;
        pl_keys_bend(pl_bend_req);
    }
}
static void pl_requests(void)            /* (most halves find none: a short look, the taking out of the way) */
{
    uint32_t i, asked = pl_rq_r != pl_rq_w || pl_bend_seq != pl_bend_seen;
    for (i = 0; i < sizeof PL_LATE; i++)
        asked |= pl_late_req[i] != pl_late_seen[i];
    for (i = 0; i < 4u; i++)
        asked |= pl_koff_req[i] != pl_koff_ack[i];
    if (PL_RARE(asked))
        pl_requests_take();
}

/* ---- rendering ---- */
static void pl_ramp(int32_t g0, int32_t g1)   /* pl_vbuf times a gain ramped from g0 to g1 (Q12), in place */
{
    int32_t acc = g0 << 6, d = g1 - g0;
    uint32_t i;
    if (g0 == g1) {
        for (i = 0; i < PL_BLOCK; i++)
            pl_vbuf[i] = (pl_vbuf[i] * g1) >> 12;
        return;
    }
    for (i = 0; i < PL_BLOCK; i++) {
        acc += d;
        pl_vbuf[i] = (pl_vbuf[i] * (acc >> 6)) >> 12;
    }
}

static void pl_pan_out(uint32_t pan, int32_t *mix)   /* pl_vbuf into the mix, panned (0 left, 128 centre, 255 right) */
{
    int32_t gl = pan <= 128u ? 4096 : (int32_t)(255u - pan) * 32, gr = pan >= 128u ? 4096 : (int32_t)pan * 32;
    uint32_t i;
    for (i = 0; i < PL_BLOCK; i++) {
        mix[2u * i] += (pl_vbuf[i] * gl) >> 12;
        mix[2u * i + 1u] += (pl_vbuf[i] * gr) >> 12;
    }
}

static void pl_v_scope(pl_voice_t *v)    /* the voice's block (as it goes into the mix) into its scope, its peak */
{
    int16_t *o = &pl_scope[v - pl_v][pl_scope_w];
    int32_t pk = 0, s, a;
    uint32_t i;
    for (i = 0; i < PL_BLOCK; i++) {
        s = pl_vbuf[i];
        s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
        o[i] = (int16_t)s;
        a = s < 0 ? -s : s;
        pk = a > pk ? a : pk;
    }
    v->peak = (int16_t)pk;
}

/* A voice's block into the mix in one pass: bit for bit what pl_ramp (its gain), pl_v_scope, its pan
 * (pl_pan_out) or its Juno-106 chorus bus, then its track's sends do one after another (a ramp with g0 == g1
 * multiplies by g1 throughout, as pl_ramp's own loop for it), without their loops' cost again and again (on
 * the FM-1 a taken jump is ~6 cycles: the separate loops were half of a voice's mixing). src: the voice's block
 * (pl_vbuf; the voice job's own on core 2); jb: the chorus bus, else panned into mix by gl / gr; ns sends,
 * sb[k] += x * sa[k] >> 7. Its peak back. */
#define PL_MIX_INLINE static inline __attribute__((always_inline))
PL_MIX_INLINE int32_t pl_mix_pass(const int32_t *src, int32_t g0, int32_t g1, int16_t *o, int32_t *mix, int32_t gl,
                                  int32_t gr, int32_t *jb, int32_t *const *sb, const int32_t *sa, const int ns)
{
    int32_t acc = g0 << 6, d = g1 - g0, pk = 0;
    int32_t *b0 = ns > 0 ? sb[0] : 0, *b1 = ns > 1 ? sb[1] : 0, *b2 = ns > 2 ? sb[2] : 0;
    int32_t a0 = ns > 0 ? sa[0] : 0, a1 = ns > 1 ? sa[1] : 0, a2 = ns > 2 ? sa[2] : 0;
    uint32_t i;
    for (i = 0; i < PL_BLOCK; i++) {
        int32_t x, s, a;
        acc += d;
        x = (src[i] * (acc >> 6)) >> 12;
        s = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        o[i] = (int16_t)s;
        a = s < 0 ? -s : s;
        pk = a > pk ? a : pk;
        if (jb) {
            jb[i] += x;
        } else {
            mix[2u * i] += (x * gl) >> 12;
            mix[2u * i + 1u] += (x * gr) >> 12;
        }
        if (ns > 0)
            b0[i] += (x * a0) >> 7;
        if (ns > 1)
            b1[i] += (x * a1) >> 7;
        if (ns > 2)
            b2[i] += (x * a2) >> 7;
    }
    return pk;
}

static int32_t pl_mix_voice(const int32_t *src, int32_t g0, int32_t g1, int16_t *o, int32_t *mix, uint32_t pan,
                            int32_t *jb, int32_t *const *sb, const int32_t *sa, int ns)
{
    int32_t gl = pan <= 128u ? 4096 : (int32_t)(255u - pan) * 32, gr = pan >= 128u ? 4096 : (int32_t)pan * 32;
    if (jb)
        switch (ns) {
        case 0: return pl_mix_pass(src, g0, g1, o, mix, 0, 0, jb, sb, sa, 0);
        case 1: return pl_mix_pass(src, g0, g1, o, mix, 0, 0, jb, sb, sa, 1);
        case 2: return pl_mix_pass(src, g0, g1, o, mix, 0, 0, jb, sb, sa, 2);
        default: return pl_mix_pass(src, g0, g1, o, mix, 0, 0, jb, sb, sa, 3);
        }
    switch (ns) {
    case 0: return pl_mix_pass(src, g0, g1, o, mix, gl, gr, 0, sb, sa, 0);
    case 1: return pl_mix_pass(src, g0, g1, o, mix, gl, gr, 0, sb, sa, 1);
    case 2: return pl_mix_pass(src, g0, g1, o, mix, gl, gr, 0, sb, sa, 2);
    default: return pl_mix_pass(src, g0, g1, o, mix, gl, gr, 0, sb, sa, 3);
    }
}

/* every track's level for this block: its LEVEL, under DUCK (the source track's peak of the last block,
 * four times it at most full: at once up, down in a straight line over RELEASE) */
static void pl_levels(void)
{
    fx_duck_t d;
    uint32_t t, src;
    int32_t env = pl_duck_env;
    if (PL_RARE(pl_cur->dj))
        fx_strip_duck(&pl_dj.strip[PL_SIDE], &d);   /* (a deck's: its song's own) */
    else
        fx_duck(&d);
    src = d.on && d.src < PL_NCH ? d.src + 1u : d.on && d.src == FX_DUCK_KEYS ? PL_NCH + 1u : 0u;
    if (PL_RARE(src)) {                  /* (PL_NCH + 1: the keys, the song's replay's; no track is the source then) */
        int32_t pk = src > PL_NCH ? (PL_SIDE ? 0 : pl_kpeak) : pl_ch[src - 1u].peak, to = pk >= 8192 ? 32768 : pk * 4;
        int32_t step = d.release_frames ? (int32_t)(32768u * PL_BLOCK / d.release_frames) : 32768;
        env = to > env ? to : env - step < to ? to : env - step;
    } else {
        env = 0;
    }
    pl_duck_env = env;
    if (!PL_SIDE) {                      /* (the keys duck as a track of the song's replay: their DUCK amount; never
                                          * as the source) */
        pl_kdk = env && pl_kstrip[RM_DUCK] && src <= PL_NCH ?
                 32768 - ((env * d.depth_q15) >> 15) * pl_kstrip[RM_DUCK] / 127 : 32768;
        pl_kpeak = 0;
    }
    for (t = 0; t < PL_NCH; t++) {
        pl_chan_t *ch = &pl_ch[t];
        int32_t lv = ((int32_t)ch->mix[RM_LEVEL] * 2113666) >> 16;   /* (= x 4096 / 127 for every byte, no divide) */
        if (env && t != src - 1u && ch->mix[RM_DUCK])
            lv = (int32_t)(((int64_t)lv * (32768 - ((env * d.depth_q15) >> 15) * ch->mix[RM_DUCK] / 127)) >> 15);
        ch->level = lv;
        ch->peak = 0;
    }
}

/* Juno-106 chorus buses (chorus I, II: their lines, their tails, the buses, whether a voice used them) into a mix
 * (centre); a chorus without input stops once its line has run out. 1: something went into the mix */
static uint32_t pl_buses(juno_chorus_t *ch, uint16_t *tail, int32_t (*jb)[PL_BLOCK], uint8_t *used, int32_t *mix)
{
    int32_t *l = pl_vbuf, r[PL_BLOCK];   /* (pl_vbuf free here: the voices rendered) */
    uint32_t i, m, any = 0;
    for (m = 1; m < 3u; m++) {
        if (!tail[m - 1u])
            continue;
        if (!any) {
            memset(l, 0, PL_BLOCK * sizeof *l);
            memset(r, 0, PL_BLOCK * sizeof *r);
            any = 1;
        }
        juno_chorus(&ch[m - 1u], (int)m, jb[m - 1u], l, r);
        tail[m - 1u] = tail[m - 1u] > PL_BLOCK ? (uint16_t)(tail[m - 1u] - PL_BLOCK) : 0;
    }
    if (any)
        for (i = 0; i < PL_BLOCK; i++) {
            mix[2u * i] += l[i];
            mix[2u * i + 1u] += r[i];
        }
    if (*used) {                         /* (else they hold 0s still) */
        memset(jb, 0, sizeof pl_jbus0);
        *used = 0;
    }
    return any;
}
/* a replay's Juno-106 chorus buses into its mix */
static uint32_t pl_juno_buses(uint32_t k, int32_t *mix)
{
    return pl_buses(PL_CHORUS(k), pl_chorus_tail[k], PL_JBUS(k), &pl_jbus_used[k], mix);
}

static void pl_ticks(void)               /* the current replay's ticks due in this block */
{
    uint32_t c;
    if (pl_state == PS_PLAY || pl_state == PS_STEP) {
        pl_clock_block();
        for (c = 0; c < PL_NCH; c++)     /* the 303s' gates: closed half a row on (unless sliding) */
            if (pl_ch[c].gate >= 0 && (pl_ch[c].gate -= (int32_t)PL_BLOCK) < 0 && pl_ch[c].voice >= 0 &&
                pl_v[pl_ch[c].voice].engine == EN_TB)
                pl_v_off(&pl_v[pl_ch[c].voice]);
    }
    if (PL_RARE(pl_cur->k && (pl_lpg[0] | pl_lpg[1] | pl_lpg[2])))
        pl_lp_gates();                   /* (LOOPS' TB-303 lanes: theirs, playing or not) */
    if (!pl_cur->k || pl_ch != pl_chs)   /* (the second replay on the first's tracks: their levels are the first's
                                          * pass's, DUCK's included; a second pass would read the peaks it zeroed) */
        pl_levels();
}

/* ---- the voice job (specs/second-core.md#the-voices-on-core-2): the voices in core 2's places (PL_C2_FROM up to
 * the count) rendered on core 2, one after another into its one buffer, while core 1 renders its own, and mixed there
 * into mixes of core 2's own (a replay's mix, sends and Juno-106 chorus buses, or the DRY keys'), which core 1 adds
 * into its own after the job: whole-number sums, so the sound is one core's mixing them all. A voice whose mute fades
 * this block is not handed over: core 1 renders and mixes it in its place, as its own. Core 1 makes every change to
 * them before the hand-over (their gains, pans and sends too); the job runs from the image (the engines' code, the
 * voices' data read in place), so every flash operation waits for core 2 idle. PL_ONE_CORE_16: a test's reference, 16
 * voices on one core rendered in place (no job), whether core 2 takes the halves or not ---- */
#ifndef PL_ONE_CORE_16
#define PL_ONE_CORE_16 0
#endif
#define PL_V2 (PL_VOICES_MOST - PL_C2_FROM)
#define PL_V2_MIXES 3                    /* core 2's mixes: the first replay's, the second's, the DRY keys' */
enum { V2_MIX = 1u, V2_SEND = 2u, V2_JBUS = 16u };   /* what of a mix the job wrote (V2_SEND << send, V2_JBUS << bus) */
typedef struct {
    int32_t g0, g1;                      /* its gain ramp (Q12) */
    int32_t sa[3];                       /* its sends' amounts */
    uint8_t to, pan, ns, jb;             /* its mix (PL_V2_MIXES), pan, sends, chorus bus (1, 2; 0: none) */
    uint8_t sidx[3];                     /* its sends' buses (0-2) */
    int16_t peak;                        /* the job's: its peak */
} pl_v2mix_t;
typedef struct {                         /* a mix of core 2's, as core 1's: stereo, the sends, the chorus buses */
    int32_t mix[2u * PL_BLOCK], send[3][PL_BLOCK], jbus[2][PL_BLOCK];
} pl_v2out_t;
static struct {
    uint8_t n, tick;                     /* the voices handed over; the Juno-106's firmware tick this block */
    uint8_t place[PL_V2];                /* their places, in order */
    volatile uint8_t started, done;      /* the job's own marks: a fallback's job never ran, or ran part way */
    uint32_t us[PL_V2], mus[PL_V2];      /* each one's rendering time (player_costs, the engines' figures), mixing */
    pl_v2mix_t mx[PL_V2];
    uint8_t used[PL_V2_MIXES];           /* what of each mix the job wrote (V2_*) */
} pl_v2;
static int32_t pl_v2buf[PL_BLOCK] PLAT_POOL;   /* (each voice in turn: mixed before the next is rendered) */
static pl_v2out_t pl_v2out[PL_V2_MIXES] PLAT_POOL;
#ifdef PTFM_DEBUG
static uint32_t pl_v2_jobs, pl_v2_us, pl_v2_wait_sum, pl_v2_wait_max;   /* the debug build's (player_v2_figures) */
static uint8_t pl_v2_most;
#endif

/* a part of a mix of core 2's, the first time the job writes it this block: zeroed */
static int32_t *pl_v2_part(uint32_t to, uint32_t bit, int32_t *p, uint32_t bytes)
{
    if (!(pl_v2.used[to] & bit)) {
        memset(p, 0, bytes);
        pl_v2.used[to] |= (uint8_t)bit;
    }
    return p;
}

static __attribute__((noinline)) void pl_v2_job(uint32_t j)
{
    uint32_t i, k;
    (void)j;
    pl_v2.started = 1;
    for (i = 0; i < pl_v2.n; i++) {
        pl_voice_t *v = &pl_v[pl_v2.place[i]];
        pl_v2mix_t *x = &pl_v2.mx[i];
        pl_v2out_t *o = &pl_v2out[x->to];
        int32_t *buf = pl_v2buf, *sb[3], *jb = 0, *mix = 0;
        uint32_t t0 = PLAT_FAR(plat_c2_us)();
        memset(buf, 0, sizeof pl_v2buf);
        if (v->engine == EN_DX7)
            dx7_render(&v->u.dx, buf);
        else if (v->engine == EN_JUNO)
            juno_render(&v->u.ju, buf, pl_v2.tick);
        else if (v->engine == EN_TB)
            tb_render(&v->u.tb, buf);
        else if (v->engine == EN_CHIP)
            chip_render(&v->u.ch, buf);
        else
            samp_render(&v->u.sm, buf);
        t0 = PLAT_FAR(plat_c2_us)() - t0;
        pl_v2.us[i] = t0;
        DBG2_ADD_US(v->engine == EN_DX7 ? DS_DX7 : v->engine == EN_JUNO ? DS_JUNO : v->engine == EN_TB ? DS_TB303 :
                    DS_SAMPLE, t0);
        DBG2_ADD_US(DS_VOICES, t0);
        t0 = PLAT_FAR(plat_c2_us)();     /* (into its mix of core 2's, as core 1 mixes its own) */
        if (x->jb)
            jb = pl_v2_part(x->to, V2_JBUS << (x->jb - 1u), o->jbus[x->jb - 1u], sizeof o->jbus[0]);
        else
            mix = pl_v2_part(x->to, V2_MIX, o->mix, sizeof o->mix);
        for (k = 0; k < x->ns; k++)
            sb[k] = pl_v2_part(x->to, V2_SEND << x->sidx[k], o->send[x->sidx[k]], sizeof o->send[0]);
        x->peak = (int16_t)pl_mix_voice(buf, x->g0, x->g1, &pl_scope[pl_v2.place[i]][pl_scope_w], mix, x->pan, jb,
                                        sb, x->sa, x->ns);
        t0 = PLAT_FAR(plat_c2_us)() - t0;
        pl_v2.mus[i] = t0;
        DBG2_ADD_US(DS_MIX, t0);
        DBG2_ADD_US(DS_VOICES, t0);
    }
    pl_v2.done = 1;
}

/* the job done: core 2's mixes added into core 1's (whole-number sums: the order makes no difference) */
static void pl_v2_add(void)
{
    uint32_t to, s, i;
    for (to = 0; to < PL_V2_MIXES; to++) {
        uint32_t u = pl_v2.used[to];
        const pl_v2out_t *o = &pl_v2out[to];
        if (!u)
            continue;
        if (u & V2_MIX) {
            int32_t *mix = to == 2u ? pl_kmix : to ? pl_mix1 : pl_mix;
            for (i = 0; i < 2u * PL_BLOCK; i++)
                mix[i] += o->mix[i];
        }
        for (s = 0; s < 3u; s++)
            if (u & V2_SEND << s) {
                int32_t *send = (to ? pl_send1 : pl_send) + s * PL_BLOCK;
                for (i = 0; i < PL_BLOCK; i++)
                    send[i] += o->send[s][i];
            }
        for (s = 0; s < 2u; s++)
            if (u & V2_JBUS << s) {
                int32_t *bus = to == 2u ? pl_kjbus[s] : PL_JBUS(to)[s];
                for (i = 0; i < PL_BLOCK; i++)
                    bus[i] += o->jbus[s][i];
            }
    }
}

/* the voice job not done (a fallback held core 2): that block's voices of core 2's unheard; then held notes parked
 * (a job that never ran: their state as it was) or every one let go (a job that ran part way: their state may be half
 * done); the count 12 from here */
static void pl_v2_lost(void)
{
    uint32_t i;
    for (i = 0; i < pl_v2.n; i++) {
        pl_voice_t *v = &pl_v[pl_v2.place[i]];
        if (pl_v2.started)
            pl_v_cut(v);
        else
            v->g_last = 0;               /* (unheard this block: what follows fades from silence) */
    }
    pl_nv_apply(PL_VOICES_C1);
}

/* the limit in force this half: SETTINGS' VOICES, 16 counted as 12 while core 2 does not take the halves (c2) */
static void pl_nv_half(int c2)
{
    uint32_t n = pl_nv_set > PL_VOICES_C1 && !c2 && !PL_ONE_CORE_16 ? PL_VOICES_C1 : pl_nv_set;
    if (n != pl_nv)
        pl_nv_apply(n);
}

/* a voice's gain into the mix this block (Q12): its gain (a Juno-106's is in its engine: its LEVEL here) x its
 * track's level, DJ's stop, PLAYBACK's song volume; the keys' KEYS GAIN and DUCK; 0 while cut or fading out of its
 * place, back from parking a quarter more each block. The keys': their PAN set (FX MIXER's KEYS column) */
static int32_t pl_v_gain(pl_voice_t *v, uint32_t key, uint32_t k, uint32_t dry, int32_t sv, int32_t kg)
{
    int32_t g = v->engine == EN_JUNO ? (int32_t)(((int64_t)PL_ENG_Q12 * v->trim) >> 12) : v->gain;
    if (!key)
        g = (g * PL_CH(v->who).level) >> 12;
    if (!key && PL_DJ_ON)                /* (DJ: a stop's fall or spin-up) */
        g = (g * pl_dj.sgain[k]) >> 12;
    if (!key && !k && sv != 4096)        /* (PLAYBACK's song volume: not the keys') */
        g = (g * sv) >> 12;
    else if (key) {                      /* (SETTINGS' KEYS GAIN: the keys' alone; their PAN and, through the effects,
                                          * their DUCK: FX MIXER's KEYS column) */
        if (kg != 4096)
            g = (g * kg) >> 12;
        if (!dry && pl_kdk != 32768)
            g = (int32_t)(((int64_t)g * pl_kdk) >> 15);
        v->pan = pl_kstrip[RM_PAN];
    }
    if (v->cut || v->park == PK_OUT)
        g = 0;
    else if (v->back)
        g = (int32_t)((int64_t)g * (int32_t)(PL_BACK_N + 1u - v->back) / (int32_t)PL_BACK_N);
    return g;
}

/* a voice's sends: its track's, or the keys' (FX MIXER's KEYS column); none DRY. Their buses (0-2) and amounts, how
 * many */
static uint32_t pl_v_sends(const pl_voice_t *v, uint32_t key, uint32_t sends, uint32_t dry, uint8_t *sidx, int32_t *sa)
{
    uint32_t s, ns = 0;
    if (sends && !dry) {
        const uint8_t *mx = key ? pl_kstrip : PL_CH(v->who).mix;
        for (s = 0; s < 3u; s++)
            if (mx[RM_CHORUS + s]) {
                sidx[ns] = (uint8_t)s;
                sa[ns++] = mx[RM_CHORUS + s];
            }
    }
    return ns;
}

/* core 2's voice i, place v: its mixing as core 1 would do it this block (pl_voices). 0 while its mute fades: not
 * handed over (core 1 renders and mixes it in its place) */
static int pl_v2_prep(uint32_t i, pl_voice_t *v, int32_t sv, int32_t kg)
{
    pl_v2mix_t *x = &pl_v2.mx[i];
    uint32_t key = v->use == PV_KEY, k = key ? 0u : v->who >> 4 & 1u, kb = pl_d2 ? k : 0u;
    uint32_t sends = PL_DJ_ON ? pl_dj.strip[k].route : (uint32_t)fx_sends(), dry = key && pl_keys_dry;
    int32_t m = !key && (PL_MCTX(k)->mute >> (v->who & 15u) & 1u) ? 0 : 4096;
    if (m != 4096 || v->m_last != 4096)
        return 0;
    x->g0 = v->g_last;
    x->g1 = pl_v_gain(v, key, k, dry, sv, kg);
    x->to = (uint8_t)(dry ? 2u : kb);
    x->pan = v->pan;
    x->jb = v->engine == EN_JUNO ? (uint8_t)juno_chorus_mode(v->u.ju.p) : 0u;
    x->ns = (uint8_t)pl_v_sends(v, key, sends, dry, x->sidx, x->sa);
    return 1;
}

/* every voice that sounds, into its replay's mix and sends (the keys' into the song's); core 2's places through the
 * voice job when it runs */
static void pl_voices(void)
{
    uint32_t c, c2, i, voices = 0, v2 = 0, v2i = 0;
    int tick;
    pl_tick_acc += juno_tick_q16;
    tick = pl_tick_acc >= 65536u;
    if (tick)
        pl_tick_acc -= 65536u;
    for (c = 0; c < 2u * PL_NCH; c++)
        PL_CH(c).live = 0;
    {                                    /* the noise generator, shared: run while a Juno-106 voice is there, else it
                                          * rests (a later note hears another stretch of the same noise; built with
                                          * PL_NOISE_ALWAYS it runs every block, as before, for comparing renders) */
        int juno = PL_NOISE_ALWAYS;
        for (c = 0; c < PL_NV && !juno; c++)
            juno = pl_v[c].use != PV_FREE && pl_v[c].engine == EN_JUNO;
        if (juno)
            juno_block(tick);
    }
    pl_unpark();
    /* PLAYBACK's song volume (pl_sv_*): the song's voices alone, while that song is what plays (4096: none) */
    int32_t sv = pl_what == PL_SONG && !PL_DJ_ON && pl_sv_db ? (pl_sv_db <= -25 ? 0 : PL_VOL_Q12[pl_sv_db + 24]) : 4096;
    int32_t kg = PL_VOL_Q12[pl_kg_db + 24];   /* (KEYS GAIN on the keys' voices; 4096: none) */
    if (pl_nv > PL_VOICES_C1 && !PL_ONE_CORE_16) {   /* core 2's places that sound: their job, handed over before
                                          * core 1 renders its own (the ones the loop below would render) */
        pl_v2.n = 0;
        for (c = PL_C2_FROM; c < pl_nv; c++) {
            pl_voice_t *v = &pl_v[c];
            if (v->use != PV_FREE && v->park != PK_PARKED && pl_v_sounds(v) && pl_v2_prep(pl_v2.n, v, sv, kg))
                pl_v2.place[pl_v2.n++] = (uint8_t)c;
        }
        if (pl_v2.n) {
            pl_v2.tick = (uint8_t)tick;
            pl_v2.started = pl_v2.done = 0;
            memset(pl_v2.used, 0, sizeof pl_v2.used);
            plat_c2_post(pl_v2_job, PLAT_C2_VOICE_JOB);
            v2 = 1;
#ifdef PTFM_DEBUG
            pl_v2_jobs++;
            if (pl_v2.n > pl_v2_most)
                pl_v2_most = pl_v2.n;
#endif
        }
    }
    pl_silent = 1;
    for (c = 0; c < PL_NV; c++) {
        pl_voice_t *v = &pl_v[c];
        if (PL_RARE(c == PL_C2_FROM && v2)) {     /* core 2's voices next: their job done first */
            DBG_T(tw);
            plat_c2_wait();
#ifdef PTFM_DEBUG
            tw = plat_us() - tw;
            pl_v2_wait_sum += tw;
            if (tw > pl_v2_wait_max)
                pl_v2_wait_max = tw;
            for (i = 0; pl_v2.done && i < pl_v2.n; i++)
                pl_v2_us += pl_v2.us[i] + pl_v2.mus[i];
#endif
            if (PL_RARE(!pl_v2.done)) {  /* (a fallback: this block's unheard, v2 2 skips them) */
                pl_v2_lost();
                v2 = 2;
            } else {                     /* (its mixes into core 1's) */
                DBG_T(ta);
                pl_v2_add();
                DBG_ADD(DS_MIX, ta);
            }
        }
        v->live = 0;
        if (PL_RARE(v2 == 2u && v2i < pl_v2.n && pl_v2.place[v2i] == c)) {
            v2i++;                       /* (unheard this block: pl_v2_lost) */
            continue;
        }
        if (v->use == PV_FREE)
            continue;
        uint32_t key = v->use == PV_KEY, k = key ? 0u : v->who >> 4 & 1u, kb = pl_d2 ? k : 0u;   /* (kb: its bus) */
        uint32_t sends = PL_DJ_ON ? pl_dj.strip[k].route : (uint32_t)fx_sends(), dry = key && pl_keys_dry;
        int32_t *mix = dry ? pl_kmix : kb ? pl_mix1 : pl_mix, *send = kb ? pl_send1 : pl_send;
        int32_t g;
        if (v->park == PK_PARKED) {      /* (unheard, its time going on; let go, cut or ended meanwhile: gone) */
            if (!v->held || v->cut || !pl_v_sounds(v))
                pl_v_cut(v);
            else if (v->engine == EN_DX7)
                dx7_skip(&v->u.dx);
            else if (v->engine == EN_JUNO)
                juno_skip(&v->u.ju, tick);
            else if (v->engine == EN_SAMP)
                samp_skip(&v->u.sm);
            else if (v->engine == EN_CHIP)
                chip_skip(&v->u.ch);
            continue;
        }
        int jv = PL_RARE(v2 == 1u && v2i < pl_v2.n && pl_v2.place[v2i] == c);   /* (core 2 rendered and mixed it: the
                                          * job's) */
        const pl_v2mix_t *jx = jv ? &pl_v2.mx[v2i] : 0;
        if (!jv && !pl_v_sounds(v)) {    /* it has ended (the job's: it sounded when handed over, as the render it
                                          * made, which may have ended it, was made) */
            pl_v_free(v);
            continue;
        }
        if (dry)                         /* (it goes into the keys' mix: the effects may rest) */
            pl_hb[pl_slot].keys = 1;
        else
            pl_silent = 0;               /* (it goes into the mix, the sends or the buses) */
        if (jv) {
            uint32_t en = v->engine == EN_DX7 ? 0u : v->engine == EN_JUNO ? 1u : v->engine == EN_TB ? 2u : 3u;
            pl_eng_us[en] += pl_v2.us[v2i];
            pl_eng_blocks[en]++;
            DBG_BLOCK(en);
            v2i++;
        } else {
            memset(pl_vbuf, 0, sizeof pl_vbuf);
            uint32_t t0 = plat_us(), en = v->engine == EN_DX7 ? 0u : v->engine == EN_JUNO ? 1u : v->engine == EN_TB ? 2u : 3u;
            if (v->engine == EN_DX7)                     /* (CHIP's time counted with the samples': en 3) */
                dx7_render(&v->u.dx, pl_vbuf);
            else if (v->engine == EN_JUNO)
                juno_render(&v->u.ju, pl_vbuf, tick);
            else if (v->engine == EN_TB)
                tb_render(&v->u.tb, pl_vbuf);
            else if (v->engine == EN_CHIP)
                chip_render(&v->u.ch, pl_vbuf);
            else
                samp_render(&v->u.sm, pl_vbuf);
            t0 = plat_us() - t0;
            pl_eng_us[en] += t0;
            pl_eng_blocks[en]++;
            DBG_ADD_US(DS_DX7 + en, t0);
            DBG_BLOCK(en);
        }
        DBG_T(tm);                       /* (the debug build: its mixing in, to its peaks) */
        g = jx ? jx->g1 : pl_v_gain(v, key, k, dry, sv, kg);   /* (core 2's: its gain as handed over) */
        {
            int32_t m = !key && (PL_MCTX(k)->mute >> (v->who & 15u) & 1u) ? 0 : 4096;
            uint32_t jm = v->engine == EN_JUNO ? (uint32_t)juno_chorus_mode(v->u.ju.p) : 0u;   /* (its chorus bus) */
            int32_t *bus = jm ? dry ? pl_kjbus[jm - 1u] : PL_JBUS(kb)[jm - 1u] : 0;   /* (DRY keys: their own) */
            int32_t *sb[3], sa[3];
            uint8_t sidx[3];
            uint32_t ns = jx ? 0u : pl_v_sends(v, key, sends, dry, sidx, sa);
            for (c2 = 0; c2 < ns; c2++)
                sb[c2] = send + sidx[c2] * PL_BLOCK;
            if (jm && dry) {
                pl_kchorus_tail[jm - 1u] = 512;
                pl_kjbus_used = 1;
            } else if (jm) {
                pl_chorus_tail[kb][jm - 1u] = 512;
                pl_jbus_used[kb] = 1;
            }
            if (jx) {                    /* (core 2 mixed it: its peak) */
                v->peak = jx->peak;
                v->g_last = g;
            } else if (m == 4096 && v->m_last == 4096) {   /* (no mute fading: all in one pass) */
                v->peak = (int16_t)pl_mix_voice(pl_vbuf, v->g_last, g, &pl_scope[v - pl_v][pl_scope_w], mix, v->pan,
                                                bus, sb, sa, (int)ns);
                v->g_last = g;
            } else {
                pl_ramp(v->g_last, g);
                v->g_last = g;
                pl_v_scope(v);           /* (before the mute: its scope, its peak, so its life and DUCK are as heard) */
                pl_ramp(v->m_last, m);
                v->m_last = m;
                if (jm)
                    for (i = 0; i < PL_BLOCK; i++)
                        bus[i] += pl_vbuf[i];
                else
                    pl_pan_out(v->pan, mix);
                for (c2 = 0; c2 < (uint32_t)ns; c2++)
                    for (i = 0; i < PL_BLOCK; i++)
                        sb[c2][i] += (pl_vbuf[i] * sa[c2]) >> 7;
            }
        }
        if (!key && v->peak > PL_CH(v->who).peak)
            PL_CH(v->who).peak = v->peak;
        else if (key && v->peak > pl_kpeak)
            pl_kpeak = v->peak;
        if (!key && v->who < 2u * PL_TRACKS && (uint16_t)v->peak > pl_tpeak[v->who])
            pl_tpeak[v->who] = (uint16_t)v->peak;
        DBG_ADD(DS_MIX, tm);
        if (v->cut) {                    /* (NOTE CUT: faded out) */
            pl_v_cut(v);
            continue;
        }
        if (v->park == PK_OUT) {         /* (taken from its voice, faded out: held, it waits parked; else gone) */
            if (!v->held) {
                pl_v_cut(v);
                continue;
            }
            v->park = PK_PARKED;
            v->g_last = 0;
            v->peak = 0;
            memset(pl_scope[c], 0, sizeof pl_scope[0]);
            continue;
        }
        if (v->back)
            v->back--;
        v->quiet = (uint8_t)(!v->held && v->peak < PL_QUIET ? v->quiet + 1u : 0u);
        if (v->quiet >= PL_QUIET_N) {    /* (a released note under hearing: it has ended) */
            pl_v_cut(v);
            continue;
        }
        v->live = 1;
        if (!key)
            PL_CH(v->who).live = 1;
        voices++;
    }
    pl_scope_w = (pl_scope_w + PL_BLOCK) % PL_SCOPE;
    DBG_T(tj);
    if (pl_juno_buses(0, pl_mix))
        pl_silent = 0;
    if (pl_d2 && pl_juno_buses(1, pl_mix1))
        pl_silent = 0;
    if ((pl_kchorus_tail[0] || pl_kchorus_tail[1] || pl_kjbus_used) &&   /* (the keys heard DRY: their own chorus) */
        pl_buses(pl_kchorus, pl_kchorus_tail, pl_kjbus, &pl_kjbus_used, pl_kmix))
        pl_hb[pl_slot].keys = 1;
    DBG_ADD(DS_JCHORUS, tj);
    pl_voices_now = (uint8_t)voices;
    if (voices > pl_voices_max)
        pl_voices_max = (uint8_t)voices;
    if (voices > pl_vw_cur)
        pl_vw_cur = (uint8_t)voices;
    pl_age_seen = pl_age;
}

static void pl_snapshot(void)            /* what the screens read */
{
    uint32_t t, i;
    struct ibxm_fm1_state st;
    pl_snap_seq++;
    PL_BARRIER();
    pl_snap.what = pl_what;
    pl_snap.lcue = pl_lcue;
    pl_snap.lp_take = pl_lpt.state;
    pl_snap.lp_take_loop = pl_lpt.loop;
    pl_snap.lp_pass = pl_lp_pass;
    pl_snap.lp_bar_wait = pl_lpm.bar;
    pl_snap.lp_alone = pl_lpv.alone;
    pl_snap.lp_rest = pl_lpr.on;
    pl_snap.lp_rest_row = pl_lpr.kr;
    pl_snap.lp_sw = pl_lpr.n;
    pl_snap.lp_sw_row = pl_lpr.row;
    pl_snap.lp_sw_rows = pl_lpr.rows;
    pl_snap.count_beats = pl_cnt.on ? pl_cnt.beats : 0u;
    pl_snap.count_beat = pl_cnt.beat;
    pl_snap.playing = (pl_state == PS_PLAY || (pl_state == PS_CHASE && !pl_step_after) || pl_state == PS_BUILD) &&
                      !pl_lpv.alone;    /* (the preview alone: the loops are paused) */
    pl_snap.stepping = pl_state == PS_STEP || (pl_state == PS_CHASE && pl_step_after);
    pl_snap.arp_on = pl_arp.on;
    pl_snap.arp_notes = pl_arp.n;
    pl_snap.ended = pl_ended;
    pl_snap.song = pl_song;
    pl_snap.voices = pl_voices_now;
    pl_snap.voices_max = pl_voices_max;
    pl_snap.voices_short = pl_vw_short;
    pl_snap.voices_long = pl_vw_long;
    pl_snap.tempo_share = pl_ps_share;
    pl_snap.vol_db = pl_sv_db;
    pl_snap.end_mode = pl_end_mode;
    pl_snap.end_next = pl_end_next;
    pl_snap.ed_what = pl_ed_what;
    pl_snap.ed_key = pl_ed_key;
    pl_snap.voice_count = pl_nv;
    pl_snap.steals = pl_steals;
    for (i = 0, pl_snap.parked = 0; i < PL_NV; i++)
        pl_snap.parked += pl_v[i].use != PV_FREE && pl_v[i].park == PK_PARKED;
    pl_snap.hushed = pl_hush;
    pl_snap.muted = pl_mute;
    pl_snap.speed = (uint8_t)pl_speed;
    pl_snap.bpm10 = pl_bpm10();
    if (pl_ext_bpm10)
        pl_snap.bpm10 = pl_ext_bpm10;    /* (SYNC EXTERNAL: the clock's) */
    if (pl_rp && (pl_state == PS_PLAY || pl_state == PS_STEP)) {
        replay_fm1_state(pl_rp, &st);
        pl_snap.pos = (uint8_t)st.seq_pos;
        pl_snap.row = (uint8_t)st.row;
        pl_snap.tick = (uint8_t)(pl_speed - (uint32_t)st.tick);
    } else {
        pl_snap.pos = pl_state == PS_CHASE || pl_state == PS_BUILD ? pl_go_pos : pl_kept_pos;
        pl_snap.row = pl_state == PS_CHASE || pl_state == PS_BUILD ? pl_go_row : pl_kept_row;
        pl_snap.tick = 0;
    }
    pl_snap.pat = pl_len ? pl_order[pl_snap.pos < pl_len ? pl_snap.pos : 0] : 0;
    pl_snap.rows = pl_rp ? (uint8_t)(pl_mod->patterns[pl_snap.pat].num_rows - 1) : 63;
    pl_snap.ntrk = pl_rp ? (uint8_t)pl_mod->num_channels : 0;
    pl_snap.rows_played = pl_rows_seen;
    pl_snap.halves = pl_halves;
    for (t = 0; t < PL_TRACKS; t++) {
        pl_snap.track[t].note = pl_ch[t].note ? pl_ch[t].note : PL_NO_NOTE;
        pl_snap.track[t].inst = pl_ch[t].ins;
        pl_snap.track[t].vol = (uint8_t)(pl_ch[t].vol * 127 / 64);
        pl_snap.track[t].sounding = pl_ch[t].live;
    }
    for (i = 0; i < PL_NV; i++) {
        pl_voice_t *v = &pl_v[i];
        pl_vsnap[i].on = v->use != PV_FREE && v->park != PK_PARKED;   /* (a parked note is not heard) */
        pl_vsnap[i].engine = v->use == PV_FREE ? PL_ENG_NONE : v->engine == EN_DX7 ? PL_ENG_DX7
                           : v->engine == EN_JUNO ? PL_ENG_JUNO : v->engine == EN_TB ? PL_ENG_TB303
                           : v->engine == EN_CHIP ? PL_ENG_CHIP : PL_ENG_SAMPLER;
        pl_vsnap[i].track = v->use == PV_KEY ? 0xFFu : v->who;
        pl_vsnap[i].note = v->note;
        pl_vsnap[i].inst = v->inst;
        pl_vsnap[i].level = (uint8_t)(v->peak >> 7);
        pl_vsnap[i].pos = v->use != PV_FREE && v->engine == EN_SAMP ? v->u.sm.pos : -1;
    }
    {                                    /* DJ: the decks, the LFOs */
        pl_ctx_t *was = pl_cur;
        uint32_t k;
        static const pl_dloop_t NO_LOOP;
        static const pl_stop_t NO_STOP;
        for (k = 0; k < 2u; k++) {
            pl_ctx_t *c = &pl_ctxs[k];
            const pl_dloop_t *lp = pl_d2 ? &pl_dj.loop[k] : &NO_LOOP;
            const pl_stop_t *sp = pl_d2 ? &pl_dj.stop[k] : &NO_STOP;
            pl_use(k);
            pl_snap.deck[k].song = pl_song;
            pl_snap.deck[k].playing = (uint8_t)(pl_deck_runs(k) || (c->state == PS_BUILD && !c->arm && !c->step_after));
            pl_snap.deck[k].armed = (uint8_t)(c->state == PS_ARMED || ((c->state == PS_CHASE || c->state == PS_BUILD) &&
                                                                       c->arm));
            pl_snap.deck[k].ended = pl_ended;
            if (pl_rp && (pl_state == PS_PLAY || pl_state == PS_STEP || pl_state == PS_HELD)) {
                replay_fm1_state(pl_rp, &st);
                pl_snap.deck[k].pos = (uint8_t)st.seq_pos;
                pl_snap.deck[k].row = (uint8_t)st.row;
                pl_snap.deck[k].tick = (uint8_t)(pl_speed - (uint32_t)st.tick);
            } else {
                int go = pl_state == PS_CHASE || pl_state == PS_ARMED || pl_state == PS_BUILD;
                pl_snap.deck[k].pos = go ? pl_go_pos : pl_kept_pos;
                pl_snap.deck[k].row = go ? pl_go_row : pl_kept_row;
                pl_snap.deck[k].tick = 0;
            }
            pl_snap.deck[k].pat = pl_len ? pl_order[pl_snap.deck[k].pos < pl_len ? pl_snap.deck[k].pos : 0] : 0;
            pl_snap.deck[k].rows = pl_rp ? (uint8_t)(pl_mod->patterns[pl_snap.deck[k].pat].num_rows - 1) : 63;
            pl_snap.deck[k].speed = (uint8_t)pl_speed;
            pl_snap.deck[k].bpm10 = pl_bpm10();
            pl_snap.deck[k].rows_played = pl_rows_seen;
            pl_snap.deck[k].stop = sp->kind;
            pl_snap.deck[k].stopped = sp->stood;
            pl_snap.deck[k].loop = lp->kind;
            pl_snap.deck[k].loop_pos = lp->pos;
            pl_snap.deck[k].loop_row = lp->row;
            pl_snap.deck[k].loop_rows = lp->rows;
            pl_snap.deck[k].roll = lp->roll;
        }
        pl_use(was != &pl_ctxs[0]);
        pl_snap.lfo_xf8 = (int16_t)(PL_DJ_ON ? pl_dj.xf8 : (int32_t)pl_djm.xf << 8);
        pl_snap.lfo_live = 0;
        pl_snap.lfo_mix = -1;
        for (k = 0; k < 2u; k++)
            pl_snap.lfo_filt8[k] = pl_snap.lfo_prev8[k] = 0;
        if (pl_d2) {
            for (k = 0; k < 2u; k++) {
                pl_snap.lfo_filt8[k] = (int16_t)pl_dj.cut8[k];
                pl_snap.lfo_prev8[k] = (int16_t)pl_dj.prev8[k];
            }
            for (k = 0; k < PL_NLFO; k++)
                pl_snap.lfo_live |= (uint8_t)(pl_dj.lfo[k].live << k);
            pl_snap.lfo_mix = (int16_t)(pl_dj.mix8 < 0 ? -1 : (pl_dj.mix8 + 128) >> 8);
        }
        if (PL_DJ_ON)
            pl_snap.playing = (uint8_t)(pl_snap.deck[0].playing || pl_snap.deck[1].playing);
        pl_snap.dj_mem = pl_d2 != 0;
        pl_snap.dj_swaps = pl_dj_gen;
    }
    PL_BARRIER();
    pl_snap_seq++;
}

void player_state(player_state_t *s, int reset_max)
{
    uint32_t q, tries;
    for (tries = 0; tries < 8u; tries++) {   /* (a copy the audio context did not write into meanwhile) */
        q = pl_snap_seq;
        PL_BARRIER();
        memcpy(s, &pl_snap, sizeof *s);
        PL_BARRIER();
        if (!(q & 1u) && pl_snap_seq == q)
            break;
    }
    if (reset_max)
        pl_voices_max = 0;               /* (a byte: the audio context's next snapshot carries it) */
}

int player_voices_now(pl_voice_info_t *v, int max)
{
    uint32_t q, tries;
    int n = max < (int)PL_NV ? max : (int)PL_NV;
    for (tries = 0; tries < 8u; tries++) {
        q = pl_snap_seq;
        PL_BARRIER();
        memcpy(v, pl_vsnap, (uint32_t)n * sizeof *v);
        PL_BARRIER();
        if (!(q & 1u) && pl_snap_seq == q)
            break;
    }
    return n;
}

int player_scope(int voice, int16_t *dst, int n)
{
    uint32_t w = pl_scope_w, i;
    if (voice < 0 || voice >= (int)PL_NV || n < 1)
        return 0;
    n = n > (int)PL_SCOPE ? (int)PL_SCOPE : n;
    for (i = 0; i < (uint32_t)n; i++)
        dst[i] = pl_scope[voice][(w + PL_SCOPE - (uint32_t)n + i) % PL_SCOPE];
    return n;
}

PLAT_COLD void player_init(void)
{
    uint32_t c;
    dx7_init();
    juno_init();
    fx_init();
    for (c = 0; c < PL_NV; c++) {
        pl_v[c].use = PV_FREE;
        pl_v[c].engine = 255;            /* (none yet: the first note zeroes its state) */
    }
    pl_ctx_init(0);
    pl_ctx_init(1);
    pl_ctxs[1].song = 0xFF;              /* (the second replay: no song till a deck has one) */
    pl_cur = &pl_ctxs[0];
    for (c = 0; c < PL_NCH; c++)
        pl_chs[c].voice = -1;
    for (c = 0; c < PL_NLN; c++)         /* (LOOPS' lanes: each its gen's low bits, the preview's with 4) */
        pl_ln[c].gen = (uint8_t)((c & 3u) | (c >= 4u * PL_LPV ? 4u : 0u));
    pl_lp_rest();
    pl_lpn = 0;
    pl_lpt.loop = pl_lpt.skip = -1;
    pl_lpt.state = PL_TAKE_NONE;
    memset(&pl_lpm, 0, sizeof pl_lpm);
    memset(&pl_lpv, 0, sizeof pl_lpv);
    for (c = 0; c < PL_SF; c++)
        pl_sf[c].lane = 0xFF;
    pl_carts_forget();
    pl_sd_ok = !rec_song(0, &pl_sd);
    pl_snapshot();
    store_set_watch(player_store_watch);
}

/* An edit wrote, moved or dropped a piece of what the store holds (store_set_watch: the main loop, the audio context
 * held off): what plays hears it from its next row or note (specs/files/ram.md#editing-and-saving). A pattern read
 * from the store has its tracks' places put on their streams as they are now, the row each is on kept; a FUSION
 * layout read again; a cartridge's layout read again at its next note; a song's record read again. */
PLAT_COLD void player_store_watch(uint8_t kind, uint8_t num, uint16_t id)
{
    uint32_t k, t;
    if (kind == REC_PATTERN) {
        for (k = 0; k < 2u; k++) {
            pl_ctx_t *c = &pl_ctxs[k];
            rec_pat_t np;
            if (c->src || c->gp_num != (int32_t)num)
                continue;
            if (rec_pattern(num, &np)) { /* (gone: asked again at the next row) */
                c->gp_num = -1;
                continue;
            }
            for (t = 0; t < PL_NCH; t++)
                if (id >= 16u || t == id)
                    rec_cur_reseat(&c->gp_cur[t], t < np.ntrk ? np.trk[t] : 0, t < np.ntrk ? np.trk_end[t] : 0);
            *c->gp = np;
        }
        for (t = 0; t < 2u; t++)
            if (pl_fpat[t].num == (int16_t)num)
                pl_fpat[t].num = -1;
    } else if (kind >= REC_DX7 && kind <= REC_CART_LAST) {
        for (k = 0; k < PL_NCART; k++)
            if (pl_carts[k].valid && pl_carts[k].num == num)
                pl_carts[k].valid = 0;
    } else if (kind == REC_SONG) {
        for (k = 0; k < 2u; k++) {
            pl_ctx_t *c = &pl_ctxs[k];
            if (!c->src && c->song == num && c->sd_ok)
                c->sd_ok = !rec_song(num, &c->sd);
        }
    } else if (kind == REC_LOOPS && PL_LANES_SRC(&pl_ctxs[1])) {
        pl_lp_watch(id);                 /* (LOOPS' lanes: what changed read again, each cursor on its row) */
    }
}

/* the mix out, under a soft ceiling: straight up to 0.8 of full scale, then a tanh curve into the last 0.2
 * (the playground's output), so nothing past full scale is cut off hard */
static PLAT_C2 int16_t pl_ceiling(int32_t s)
{
    int32_t a = s < 0 ? -s : s;
    if (a > 26214) {
        int32_t u = (a - 26214) * 5, t;  /* (in units of 0.2 of full scale, Q15) */
        if (u >= 65536)
            t = FX_TANH_Q15[256];
        else
            t = FX_TANH_Q15[u >> 8] + (((FX_TANH_Q15[(u >> 8) + 1] - FX_TANH_Q15[u >> 8]) * (u & 255)) >> 8);
        a = 26214 + ((t * 6553) >> 15);
        a = a > 32767 ? 32767 : a;
    }
    return (int16_t)(s < 0 ? -a : a);
}

static void pl_dj_take(void)             /* the mailbox: DJ's mix as the main loop last wrote it, whole */
{
    uint32_t q = pl_djm_seq;
    player_dj_t m;
    if (q == pl_djm_seen || (q & 1u))
        return;
    PL_BARRIER();
    m = pl_djm_in;
    PL_BARRIER();
    if (pl_djm_seq != q)
        return;                          /* (written meanwhile: the next half) */
    if ((m.swaps ^ pl_dj_gen) & 1u)      /* (set for the sides after a SWAP not made here yet, or before one made:
                                          * for the decks as they are) */
        pl_djm_mirror(&m);
    pl_djm = m;
    pl_djm_seen = q;
}

void (*player_out)(const int32_t *lr, int at, int n);
void (*player_usb)(const int32_t *lr, const int16_t *click, int n);
/* a block of the count-in: a beat's click when one is due; past the last beat, what it held starts (1) */
static int pl_count_block(void)
{
    if (pl_cnt.left <= 0) {
        if (pl_cnt.beat >= pl_cnt.beats) {
            pl_rq_t d = pl_cnt.def;
            int has = pl_cnt.has_def;
            memset(&pl_cnt, 0, sizeof pl_cnt);
            if (has)
                pl_handle(d);
            return 1;
        }
        fx_click(1, 60, pl_cnt.beat % 4u == 0u);
        pl_cnt.beat++;
        pl_cnt.left += (int32_t)pl_cnt.fpb;
    }
    pl_cnt.left -= (int32_t)PL_BLOCK;
    return 0;
}

/* CUT, faded out: what still holds sound emptied, the effects (fx_empty), the Juno-106 chorus buses, DJ's FILTERs */
static void pl_hush_empty(void)
{
    uint32_t k, s;
    fx_empty();
    memset(pl_chorus0, 0, sizeof pl_chorus0);
    memset(pl_chorus_tail, 0, sizeof pl_chorus_tail);
    memset(pl_kchorus, 0, sizeof pl_kchorus);           /* (the keys' own, DRY) */
    memset(pl_kchorus_tail, 0, sizeof pl_kchorus_tail);
    if (!pl_d2)                          /* (DJ's own: only while DJ has its memory) */
        return;
    memset(pl_d2->chorus, 0, sizeof pl_d2->chorus);
    for (k = 0; k < 2u; k++) {
        memset(pl_dj.f_dry[k].s, 0, sizeof pl_dj.f_dry[k].s);
        for (s = 0; s < 3u; s++)
            memset(pl_dj.f_snd[k][s].s, 0, sizeof pl_dj.f_snd[k][s].s);
    }
}

/* the output's gain over the block (mix: its slot's), CUT's fade (Q15): silent while CUT is on, as it is (untouched)
 * otherwise */
static PLAT_C2 void pl_out_fade(int32_t *mix)
{
    int32_t g0 = pl_out_g, step = 32768 / (int32_t)PL_HUSH_N, g1 = pl_hush ? g0 - step : g0 + step;
    uint32_t i;
    g1 = g1 < 0 ? 0 : g1 > 32768 ? 32768 : g1;
    pl_out_g = g1;
    if (!PL_RARE(g0 != 32768 || g1 != 32768))   /* (no CUT, none fading back: the usual way) */
        return;
    if (!g0 && !g1) {
        c2_memset(mix, 0, PL_MIX_BYTES);
        return;
    }
    for (i = 0; i < PL_BLOCK; i++) {
        int32_t g = g0 + (int32_t)(((g1 - g0) * (int32_t)(i + 1u)) / (int32_t)PL_BLOCK);
        mix[2u * i] = (int32_t)(((int64_t)mix[2u * i] * g) >> 15);
        mix[2u * i + 1u] = (int32_t)(((int64_t)mix[2u * i + 1u] * g) >> 15);
    }
}

static void pl_use_slot(uint32_t j)      /* make block j in its slot (core 1) */
{
    pl_slot = j;
    pl_mix = pl_hmix[j];
    pl_send = pl_hsend[j];
    pl_kmix = pl_hkmix[j];
}

/* block j of the half (its frames from b; left: the blocks from it to the half's end), up to its effects: the
 * controls, the replays' ticks, the voices, DJ's decks' mix; what its effects work with kept in its slot (the music
 * clock, their settings: pl_hclk, pl_hset, pl_hb); then the music clocks a block on, SYNC PULSE's frame */
static void pl_audio_a(uint32_t b, uint32_t j, uint32_t left, uint32_t *prim)
{
    static uint8_t pulse_was_play;       /* (what plays played in the last block: its first, a click at once) */
    static uint64_t pulse_end;           /* (where the last block left what plays' music clock: beats Q32) */
    uint32_t k;
    DBG_T(dc);
    pl_use_slot(j);
    pl_block_at = b;
    pl_hb[j].b = (uint16_t)b;
    pl_blocks++;
    if (!pl_mix_clean) {                 /* (after a silent block at rest they hold 0s still) */
        memset(pl_mix, 0, PL_MIX_BYTES);
        memset(pl_send, 0, PL_SEND_BYTES);
    }
    pl_mix_clean = 0;
    if (PL_RARE(pl_d2)) {
        memset(pl_mix1, 0, sizeof pl_d2->mix);
        memset(pl_send1, 0, sizeof pl_d2->send);
    }
    if (PL_RARE(pl_cnt.on) && pl_count_block())   /* (the count-in over: what it held starts, on this block) */
        *prim = PL_SECOND(pl_what) ? 1u : 0u;
    pl_use(*prim);
    pl_arp_block();                      /* (the keys' arpeggio, at what plays' tempo) */
    pl_kf_block();                       /* (the keys' FUSION notes' ticks, the same) */
    if (PL_DJ_ON) {                      /* the decks: their stops, KEY */
        DBG_T(dj);
        pl_dj.row_now[0] = pl_dj.row_now[1] = pl_dj.bar_now[0] = pl_dj.bar_now[1] = 0;
        for (k = 0; k < 2u; k++) {
            pl_dj_stop_block(k);
            pl_dj_pitch(k);
        }
        DBG_ADD(DS_DJDECKS, dj);
    }
    DBG_ADD(DS_CONTROL, dc);
    DBG_T(dr);
#ifdef PTFM_DEBUG
    pl_dbg_rp = 1;
#endif
    for (k = 0; k < 2u; k++) {           /* each replay's ticks due, its tracks' levels (in the memories' order: their
                                          * notes take voices in the same order whatever side each is on) */
        pl_use(k ^ pl_swapped);
        pl_ticks();
    }
#ifdef PTFM_DEBUG
    pl_dbg_rp = 0;
#endif
    DBG_ADD(DS_REPLAY, dr);
    DBG_T(dd);
    if (PL_DJ_ON) {
        pl_dj_starts();                  /* (armed decks on the other's bar line) */
        pl_dj_sync();
        DBG_ADD(DS_DJDECKS, dd);         /* (DJ's parts of CONTROL) */
        DBG_T(dl);
        for (k = 0; k < 2u; k++) {       /* (the first row since the LFOs were worked out, and how long ago) */
            if (pl_dj.row_seen[k])
                pl_dj.row_ago[k] += pl_dj.row_ago[k] < 255u;
            else if (pl_dj.row_now[k])
                pl_dj.row_seen[k] = 1, pl_dj.row_ago[k] = 0;
        }
        if (pl_dj.lfo_at != pl_halves || pl_dj.lfo_j >= pl_dj.lfo_n) {   /* (the half's blocks at once) */
            pl_dj.lfo_at = pl_halves;
            pl_dj.lfo_n = (uint8_t)(left < PL_LFO_STEPS ? left : PL_LFO_STEPS);
            pl_dj.lfo_j = 0;
            pl_dj_lfos(pl_dj.lfo_n);
        }
        pl_dj_lfo_block(pl_dj.lfo_j++);
        DBG_ADD(DS_DJLFOS, dl);
    }
    DBG_ADD(DS_CONTROL, dd);
    DBG_T(dv);
    pl_voices();
    pl_env_held(*prim);
    DBG_ADD(DS_VOICES, dv);
    if (PL_RARE(pl_hush && pl_hush_n)) {   /* CUT: what plays plays on unheard, the effects fed nothing (after the
                                          * block the notes faded out in) */
        memset(pl_mix, 0, PL_MIX_BYTES);
        memset(pl_send, 0, PL_SEND_BYTES);
        if (pl_d2) {
            memset(pl_mix1, 0, sizeof pl_d2->mix);
            memset(pl_send1, 0, sizeof pl_d2->send);
        }
    }
    if (PL_DJ_ON) {
        DBG_T(de);
        pl_hb[j].fx = pl_dj_mix() ? 2u : 0u;   /* (fx_clk the lead deck's) */
        DBG_ADD(DS_EFFECTS, de);
    } else {                             /* (the loops' tracks are in the mix already: one thing plays) */
        pl_use(*prim);
        DBG_T(dp);
        pl_clk_pace();
        DBG_ADD(DS_CLOCK, dp);
        if (PL_RARE(PL_HUSHED)) {        /* (CUT held, faded out: nothing worked on, the time going on) */
            DBG_T(de);
            fx_hold_block();
            memset(pl_mix, 0, PL_MIX_BYTES);
            pl_hb[j].fx = 0;
            DBG_ADD(DS_EFFECTS, de);
        } else {
            pl_hb[j].fx = 1;
            pl_hb[j].sends = (uint8_t)(fx_sends() != 0);
            pl_hb[j].silent = pl_silent;
        }
    }
    {
        DBG_T(db);
        pl_hclk[j] = fx_clk;             /* (what its effects work with: as at this point) */
        fx_set_get(&pl_hset[j]);
        DBG_ADD(DS_BLOCKS, db);
    }
    DBG_T(dk);
    {                                    /* (SYNC PULSE: a half beat of what plays crossed in this block, at its frame;
                                          * as it starts to play, at once; its side as the main loop has it now, kept
                                          * for the block's output) */
        uint32_t ck = PL_SIDE, a_beat = fx_clk.beat, a_frac = fx_clk.frac, inc = fx_clk.inc;
        uint8_t play = fx_clk.playing, side = pl_pulse_side;
        int32_t pulse_at = -1;
        for (k = 0; k < 2u; k++) {
            pl_use(k);
            pl_clk_advance();
        }
        if (PL_RARE(side && play)) {     /* (from where the last block left the clock: a row's start sets it at the
                                          * block's start; a start, or a jump back, from here) */
            uint64_t a = (uint64_t)a_beat << 32 | a_frac, e, from;
            int fresh = !pulse_was_play || a < pulse_end;
            pl_use(ck);
            e = (uint64_t)fx_clk.beat << 32 | fx_clk.frac;
            from = fresh ? a : pulse_end;
            if (fresh && !(a & 0x7FFFFFFFu))
                pulse_at = 0;            /* (starting on a half beat: at once) */
            else if ((e >> 31) > (from >> 31)) {
                uint64_t line = ((from >> 31) + 1u) << 31;
                uint32_t f = line <= a || !inc ? 0u : (uint32_t)(line - a) / inc;
                pulse_at = f < PL_BLOCK ? (int32_t)f : (int32_t)PL_BLOCK - 1;
            }
            pulse_end = e;
        }
        pulse_was_play = play;
        pl_hb[j].pulse_side = side;
        pl_hb[j].pulse_at = (int16_t)pulse_at;
    }
    DBG_ADD(DS_CLOCK, dk);
}

/* ---- a block's job (pl_c2_job: pl_audio_fx, pl_audio_c), on either core: its slot alone, through pointers of its
 * own, never the block being made (pl_slot, pl_mix, pl_send, pl_kmix, pl_mix_clean are core 1's); the output's state
 * (CUT's gain, SYNC PULSE's count, VISUAL's ring) is its own; its time goes to dbg2_half, never dbg_half. Its code is
 * core 2's (PLAT_C2: in its room on the FM-1), reached from core 1's through PLAT_FAR ---- */

/* block j's effects (its settings and music clock in place: fx_work) */
static PLAT_C2 void pl_audio_fx(uint32_t j)
{
    int32_t *mix = pl_hmix[j], *snd = pl_hsend[j];
    DBG2_T(de);
    if (pl_hb[j].fx == 2u)
        fx_block(mix, snd);
    else if (pl_hb[j].fx == 1u) {
        if (pl_hb[j].silent && fx_rest_block())
            pl_hb[j].clean = 1;          /* (a silent block, the effects at rest: 0s through, their clocks on) */
        else
            fx_block(mix, pl_hb[j].sends ? snd : 0);
    }
    DBG2_ADD(DS_EFFECTS, de);
}

/* block j out (into the codec's half at its frame b): CUT's fade, USB audio, the soft ceiling, SYNC PULSE, VISUAL's
 * ring, the output */
static PLAT_C2 void pl_audio_c(uint32_t j)
{
    int32_t *mix = pl_hmix[j], *kmix = pl_hkmix[j];
    int16_t *lr = pl_lr;
    uint32_t i, b = pl_hb[j].b, side = pl_hb[j].pulse_side;
    int32_t pulse_at = pl_hb[j].pulse_at;
    DBG2_T(dq);
    if (PL_RARE(pl_hb[j].keys)) {        /* the keys heard DRY: into the mix here, past every effect */
        for (i = 0; i < 2u * PL_BLOCK; i++)
            mix[i] += kmix[i];
        c2_memset(kmix, 0, PL_MIX_BYTES);
        pl_hb[j].keys = 0;
        pl_hb[j].clean = 0;              /* (written: the next block clears it) */
    }
    {                                    /* CUT's fade (faded out, CUT's halves empty the rest: player_audio) */
        int whole = pl_out_g == 32768 && !pl_hush;   /* (the clicks in the mix as they are) */
        pl_out_fade(mix);
        if (player_usb) {                /* USB audio: always the stereo mix, the clicks left out */
            DBG2_T(du);
            player_usb(mix, whole ? fx_clicks() : 0, (int)PL_BLOCK);
            DBG2_ADD(DS_USB, du);
        }
    }
    {                                    /* the soft ceiling, where the block reaches it (up to 0.8 of full scale it
                                          * changes nothing), and VISUAL's ring (a block at a time: never wraps in one) */
        uint32_t w = pl_master_w;
        int16_t *m = &pl_master[2u * w];
        int32_t pk = 0;
        if (pl_hb[j].clean && !(side && (pl_pulse_left || pulse_at >= 0))) {   /* (silent at rest: 0s) */
            c2_memset(m, 0, 2u * PL_BLOCK * sizeof *m);
            if (lr)
                c2_memset(&lr[2u * b], 0, 2u * PL_BLOCK * sizeof *lr);
        } else {
            for (i = 0; i < 2u * PL_BLOCK; i += 4u) {   /* (four values a turn: a turn costs as its work) */
                int32_t s0 = mix[i], s1 = mix[i + 1u], s2 = mix[i + 2u], s3 = mix[i + 3u];
                int32_t a0 = s0 < 0 ? -s0 : s0, a1 = s1 < 0 ? -s1 : s1, a2 = s2 < 0 ? -s2 : s2, a3 = s3 < 0 ? -s3 : s3;
                a0 = a0 > a1 ? a0 : a1;
                a2 = a2 > a3 ? a2 : a3;
                a0 = a0 > a2 ? a0 : a2;
                pk = a0 > pk ? a0 : pk;
                m[i] = (int16_t)s0;
                m[i + 1u] = (int16_t)s1;
                m[i + 2u] = (int16_t)s2;
                m[i + 3u] = (int16_t)s3;
            }
            if (pk > 26214)
                for (i = 0; i < 2u * PL_BLOCK; i++)
                    m[i] = (int16_t)(mix[i] = pl_ceiling(mix[i]));
            if (side) {                  /* SYNC PULSE: its side the clicks, the other the music in mono */
                uint32_t left = pl_pulse_left;
                for (i = 0; i < PL_BLOCK; i++) {
                    int32_t mono = (mix[2u * i] + mix[2u * i + 1u]) >> 1, c;
                    if ((int32_t)i == pulse_at)
                        left = PL_PULSE_N;
                    c = left ? 32767 : 0;
                    left -= left ? 1u : 0u;
                    mix[2u * i] = side == 1u ? c : mono;
                    mix[2u * i + 1u] = side == 1u ? mono : c;
                }
                pl_pulse_left = left;
                pl_hb[j].clean = 0;      /* (written: the next block clears it) */
                pk = 1;
            }
            if (lr)
                for (i = 0; i < 2u * PL_BLOCK; i++)
                    lr[2u * b + i] = (int16_t)mix[i];
        }
        pl_master_w = (w + PL_BLOCK) % PL_MASTER;
        if (player_out)                  /* (the device: the block out at once; a silent one as 0: nothing to scale) */
            player_out(pk ? mix : 0, (int)b, (int)PL_BLOCK);
    }
    DBG2_ADD(DS_OUTPUT, dq);
}

/* block j's job: its settings and music clock put in place, the changes to the effects' own state it made, its
 * effects, its output. Core 2 runs it as the block is handed over (plat_c2_post), core 1 in the halves it keeps (one
 * code: the same sound on either) */
static __attribute__((noinline)) PLAT_C2 void pl_c2_job(uint32_t j)
{
    DBG2_T(dh);
    fx_work(&pl_hset[j], &pl_hclk[j]);
    fx_events((int)j);
    DBG2_ADD(DS_BLOCKS, dh);
    pl_audio_fx(j);
    pl_audio_c(j);
}

/* block j's job in CUT's halves (core 1's, block after block): the same but for the changes to the effects, made at
 * once there, none kept */
static PLAT_C2 void pl_cut_job(uint32_t j)
{
    fx_work(&pl_hset[j], &pl_hclk[j]);
    pl_audio_fx(j);
    pl_audio_c(j);
}

void player_audio(int16_t *lr, int frames)
{
    uint32_t b, k, prim;
    int c2, c2v;
    plat_c2_wait();                      /* (the half before: core 2 finished it before anything here touches what it
                                          * worked on, as the FM-1's interrupt makes sure first thing) */
    DBG_T(dt);                           /* (the debug build's stages: specs/debug.md) */
    rec_audio_ctx = 1;
    pl_lr = lr;
    pl_block_at = 0;                     /* (the requests' notes: at the half's start) */
    c2v = plat_c2_half();                /* (core 2 takes this half: its blocks' effects, but in CUT's halves, and the
                                          * voices in its places, CUT's too) */
    pl_dj_take();                        /* DJ's mix as the main loop last set it */
    pl_requests();
    pl_nv_half(c2v);
    if (PL_RARE(pl_chain != 0xFFu)) {    /* SONGS' PLAY NEXT: the song that ended, the next one from its start */
        uint8_t n = pl_chain;
        pl_chain = 0xFF;
        if (pl_what == PL_SONG && !PL_DJ_ON) {
            pl_use(0);
            pl_start(PL_SONG, n, 0, 0);
        }
    }
    if (PL_RARE(pl_hush_want != pl_hush)) {   /* CUT on: all sound off, the fade begins; off: the fade back */
        pl_hush = pl_hush_want;
        if (pl_hush) {
            pl_cut_all();
            pl_hush_n = 0;
        }
    }
    for (k = 0; k < 2u; k++) {           /* (each deck's start being made ready: its next step) */
        pl_use(k);
        if (PL_RARE(pl_state == PS_BUILD))
            pl_build_step();
    }
    for (k = 0; k < 2u; k++) {           /* (each replay's chase: a half's share) */
        pl_use(k);
        if (PL_RARE(pl_state == PS_CHASE && pl_rp))
            pl_chase();
    }
    DBG_ADD(DS_START, dt);
    prim = PL_SECOND(pl_what) ? 1u : 0u;
    c2 = !PL_RARE(pl_hush) && c2v;       /* (core 2 takes the half's blocks' effects: never in CUT's halves, the one
                                          * CUT starts in too; their voice jobs it takes all the same) */
    for (b = 0; b + PL_BLOCK <= (uint32_t)frames;) {   /* (up to PL_HALF blocks at a time: a half) */
        uint32_t n = ((uint32_t)frames - b) / PL_BLOCK, j;
        n = n > PL_HALF ? PL_HALF : n;
        if (PL_RARE(pl_hush)) {          /* CUT: block after block, on core 1 (its fade counts blocks out: the next
                                          * waits); faded out, what still holds sound emptied */
            for (j = 0; j < n; j++, b += PL_BLOCK) {
                pl_audio_a(b, j, n - j, &prim);
                PLAT_FAR(pl_cut_job)(j);
                if (pl_hush_n < 255u && ++pl_hush_n == PL_HUSH_N)
                    pl_hush_empty();
            }
            continue;
        }
        if (c2) {                        /* each block to core 2 as it is made, the changes to its effects with it; core
                                          * 1 makes the next meanwhile, and returns after the last */
            if (b)
                plat_c2_wait();          /* (more than a half's blocks: their slots again once core 2 is done) */
            for (j = 0; j < n; j++) {
                fx_defer((int)j);
                pl_audio_a(b + j * PL_BLOCK, j, n - j, &prim);
                plat_c2_post(pl_c2_job, j);
            }
            fx_defer(-1);
            b += n * PL_BLOCK;
            continue;
        }
        for (j = 0; j < n; j++) {        /* every block up to its effects, the changes to theirs kept with it */
            fx_defer((int)j);
            pl_audio_a(b + j * PL_BLOCK, j, n - j, &prim);
        }
        DBG_T(db);
        fx_defer(-1);
        {
            int all = 1, together;
            for (j = 0; j < n; j++)      /* (a block silent, or done: a block at a time, the rest path its own) */
                all &= pl_hb[j].fx == 2u || (pl_hb[j].fx == 1u && !pl_hb[j].silent);
            together = all && fx_blocks_ok(n, pl_hset);
            DBG_ADD(DS_BLOCKS, db);
            if (together) {              /* the effects take them together: each its code once */
                int32_t *mix[PL_HALF];
                const int32_t *snd[PL_HALF];
                DBG_T(de);
                for (j = 0; j < n; j++) {
                    mix[j] = pl_hmix[j];
                    snd[j] = pl_hb[j].fx == 2u || pl_hb[j].sends ? pl_hsend[j] : 0;
                }
                PLAT_FAR(fx_blocks)(mix, snd, n, pl_hclk, pl_hset);
                DBG_ADD(DS_EFFECTS, de);
                for (j = 0; j < n; j++)
                    PLAT_FAR(pl_audio_c)(j);
            } else {
                for (j = 0; j < n; j++)  /* (as each block had them: its settings, its clock, its changes) */
                    PLAT_FAR(pl_c2_job)(j);
            }
        }
        b += n * PL_BLOCK;
    }
    for (; lr && b < (uint32_t)frames; b++)  /* (a half not a whole number of blocks: silence for the rest) */
        lr[2u * b] = lr[2u * b + 1u] = 0;
    DBG_T(ds);
    pl_halves++;
    if (PL_RARE(++pl_vw_n >= PL_VW_SHORT)) {   /* (the voices' windows: one ended) */
        pl_vw_n = 0;
        pl_vw_short = pl_vw_cur;
        if (pl_vw_cur > pl_vw_acc)
            pl_vw_acc = pl_vw_cur;
        pl_vw_cur = 0;
        if (++pl_vw_k >= PL_VW_LONG) {
            pl_vw_k = 0;
            pl_vw_long = pl_vw_acc;
            pl_vw_acc = 0;
        }
    }
    pl_use(PL_SECOND(pl_what) ? 1u : 0u);
    pl_snapshot();
    rec_audio_ctx = 0;
    DBG_ADD(DS_END, ds);
}
#ifdef PTFM_DEBUG
uint8_t player_voices_sounding(void) { return pl_voices_now; }   /* (the debug build's window) */

/* (the main loop: a race with the audio context costs a block's figure, as player_costs) */
void player_v2_figures(player_v2_t *f)
{
    f->jobs = pl_v2_jobs;
    f->us = pl_v2_us;
    f->wait_sum = pl_v2_wait_sum;
    f->wait_max = pl_v2_wait_max;
    f->most = pl_v2_most;
    pl_v2_jobs = pl_v2_us = pl_v2_wait_sum = pl_v2_wait_max = 0;
    pl_v2_most = 0;
}

/* ---- the debug build's bench (core/bench.c): a voice of an instrument outside the pool, in the caller's memory,
 * started as pl_voice_on starts one (the track's parameters and LEVEL aside) and rendered a block at a time as
 * pl_voices renders the pool's. Main loop, interrupts off (pl_cart's cache is the audio context's too). ---- */
uint32_t player_bench_size(void) { return sizeof(pl_voice_t); }

int player_bench_start(void *mem, pl_inst_t in, int note)
{
    pl_voice_t *v = mem;
    const uint8_t *slot;
    uint32_t e = pl_engine(in, &slot);
    memset(v, 0, sizeof *v);
    v->engine = (uint8_t)e;
    if (e == EN_DX7) {
        uint8_t vc[156];
        rec_dx7_vced(slot, vc);
        dx7_note_on(&v->u.dx, vc, note, PL_VEL);
        return PL_ENG_DX7;
    }
    if (e == EN_JUNO) {
        rec_juno_patch(slot, v->jp);
        juno_note_on(&v->u.ju, v->jp, note, 4096);
        return PL_ENG_JUNO;
    }
    if (e == EN_TB) {
        uint8_t k[TB_NK];
        rec_tb_knobs(slot, k);
        tb_note_on(&v->u.tb, k, note, 0);
        return PL_ENG_TB303;
    }
    if (e == EN_SAMP) {
        samp_t s;
        if (!pl_sample_of(in, &s, 0))
            return 0;
        samp_note_on(&v->u.sm, &s, note, 0);
        return PL_ENG_SAMPLER;
    }
    if (e == EN_CHIP) {
        chip_patch_t cp;
        chip_unpack(slot, &cp);
        chip_note_on(&v->u.ch, &cp, note);
        return PL_ENG_CHIP;
    }
    return 0;
}

void player_bench_render(void *mem, int32_t *buf, int tick)
{
    pl_voice_t *v = mem;
    if (v->engine == EN_DX7)
        dx7_render(&v->u.dx, buf);
    else if (v->engine == EN_JUNO)
        juno_render(&v->u.ju, buf, tick);
    else if (v->engine == EN_TB)
        tb_render(&v->u.tb, buf);
    else if (v->engine == EN_SAMP)
        samp_render(&v->u.sm, buf);
    else if (v->engine == EN_CHIP)
        chip_render(&v->u.ch, buf);
}
#endif

/* ---- AUTO LEVEL's measurement (specs/files/instruments.md, LEVEL): an instrument's note alone, outside the pool,
 * as the LEVELs are defined and the demo disk's were measured: from a clean start (the voices new, a Juno-106
 * chorus's line silent and its LFO, and the Juno-106's firmware loop, where 2 s of silence leave them), held 1 s
 * of 1.2 s, the loudest 300 ms's mean square of (L + R) / 2, unity gain (no LEVEL, no track, voices not at half).
 * Its note: C-4; a TB-303's C-2; a sample instrument's its BASE NOTE. A FUSION instrument: its layers as they
 * start together (their parameter and tick commands aside), each at its own LEVEL and its cell's volume, the
 * louder of C-3 and E-4. The main loop's: a part's block at a time with the interrupts off (the engines' scratch,
 * pl_cart's cache and the noise generator are the audio context's too). Its sizeable state on the stack. ---- */
#define PM_HELD 689u                     /* blocks of 64 frames at 44,100 Hz: 1 s */
#define PM_ALL 826u                      /* 1.2 s */
#define PM_WIN 206u                      /* 300 ms */
#define PM_REST 1378u                    /* 2 s */
typedef struct { pl_voice_t *v; int32_t gain; uint8_t mode; } pm_part_t;   /* gain Q12; mode: its chorus (0 off) */
static uint32_t (*pm_lock)(void);        /* the caller's: the interrupts held back, and let go */
static void (*pm_unlock)(uint32_t);

/* instrument in's note on v, as pl_voice_on starts it (vol 1-127, at its LEVEL or not); 0 none. Interrupts off */
static PLAT_COLD int pm_start(pm_part_t *p, pl_inst_t in, int note, uint32_t vol, int level)
{
    const uint8_t *slot;
    pl_voice_t *v = p->v;
    uint32_t e = pl_engine(in, &slot);
    int32_t trim = level ? pl_level_q12(in) : 4096, l = (int32_t)(4096u * vol / 127u);
    memset(v, 0, sizeof *v);
    v->engine = (uint8_t)e;
    v->use = PV_KEY;                     /* (for pl_v_sounds; never in the pool) */
    v->held = 1;
    p->mode = 0;
    p->gain = (int32_t)(((int64_t)trim * l) >> 12);
    if (e == EN_DX7) {
        uint8_t vc[156];
        rec_dx7_vced(slot, vc);
        dx7_note_on(&v->u.dx, vc, note, PL_VEL);
    } else if (e == EN_JUNO) {           /* (its level in the engine, its LEVEL on what it renders) */
        rec_juno_patch(slot, v->jp);
        juno_note_on(&v->u.ju, v->jp, note, l);
        p->gain = trim;
        p->mode = (uint8_t)juno_chorus_mode(v->jp);
    } else if (e == EN_TB) {
        uint8_t k[TB_NK];
        rec_tb_knobs(slot, k);
        tb_note_on(&v->u.tb, k, note, 0);
    } else if (e == EN_SAMP) {
        samp_t s;
        if (!pl_sample_of(in, &s, 0))
            return 0;
        samp_note_on(&v->u.sm, &s, note, 0);
    } else if (e == EN_CHIP) {
        chip_patch_t cp;
        chip_unpack(slot, &cp);
        chip_note_on(&v->u.ch, &cp, note);
    } else
        return 0;
    return 1;
}

/* the parts sounding together: the loudest window's mean square, the peak; 0 if none */
static int pm_run(pm_part_t *p, uint32_t n, uint32_t *ms, int32_t *peak)
{
    juno_chorus_t ch[2];
    juno_noise_t noise;
    int32_t tmp[PL_BLOCK], mix[PL_BLOCK], bus[2][PL_BLOCK], l[PL_BLOCK], r[PL_BLOCK];
    uint32_t win[PM_WIN], acc = 0, b, i, k, f, modes = 0, juno = 0;
    uint64_t sum = 0, best = 0;
    int32_t pk = 0;
    if (!n)
        return 0;
    for (k = 0; k < n; k++) {
        modes |= p[k].mode ? 1u << (p[k].mode - 1u) : 0u;
        juno |= p[k].v->engine == EN_JUNO;
    }
    memset(ch, 0, sizeof ch);
    memset(tmp, 0, sizeof tmp);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    if (juno) {                          /* the noise generator from its power-on state (its slow flicker filters
                                          * would otherwise make a noise patch's reading wander by a dB or more),
                                          * the audio context's put back after */
        f = pm_lock();
        juno_noise_get(&noise);
        juno_init();
        pm_unlock(f);
    }
    for (b = 0; b < PM_REST; b++) {      /* (the silence before: the noise, the chorus's LFO, the firmware loop) */
        acc += juno_tick_q16;
        if (acc >= 65536u)
            acc -= 65536u;
        if (juno) {
            f = pm_lock();
            juno_block(0);
            pm_unlock(f);
        }
        for (k = 0; k < 2u; k++)
            if (modes >> k & 1u)
                juno_chorus(&ch[k], (int)k + 1, tmp, l, r);
    }
    for (b = 0; b < PM_ALL; b++) {
        int tick;
        uint64_t s = 0;
        acc += juno_tick_q16;
        tick = acc >= 65536u;
        if (tick)
            acc -= 65536u;
        memset(mix, 0, sizeof mix);
        memset(bus, 0, sizeof bus);
        if (juno) {
            f = pm_lock();
            juno_block(tick);
            pm_unlock(f);
        }
        for (k = 0; k < n; k++) {
            pl_voice_t *v = p[k].v;
            int32_t *to = p[k].mode ? bus[p[k].mode - 1u] : mix;
            f = pm_lock();
            if (b == PM_HELD)
                pl_v_off(v);
            if (v->engine == EN_JUNO && !juno_busy(&v->u.ju)) {
                juno_idle(&v->u.ju, tick);
                pm_unlock(f);
                continue;
            }
            if (!pl_v_sounds(v)) {
                pm_unlock(f);
                continue;
            }
            memset(tmp, 0, sizeof tmp);
            if (v->engine == EN_DX7)
                dx7_render(&v->u.dx, tmp);
            else if (v->engine == EN_JUNO)
                juno_render(&v->u.ju, tmp, tick);
            else if (v->engine == EN_TB)
                tb_render(&v->u.tb, tmp);
            else if (v->engine == EN_CHIP)
                chip_render(&v->u.ch, tmp);
            else
                samp_render(&v->u.sm, tmp);
            pm_unlock(f);
            for (i = 0; i < PL_BLOCK; i++)
                to[i] += (int32_t)(((int64_t)tmp[i] * p[k].gain) >> 12);
        }
        memcpy(l, mix, sizeof l);
        memcpy(r, mix, sizeof r);
        for (k = 0; k < 2u; k++)
            if (modes >> k & 1u)
                juno_chorus(&ch[k], (int)k + 1, bus[k], l, r);
        for (i = 0; i < PL_BLOCK; i++) {
            int64_t m = (int64_t)l[i] + r[i];
            int32_t al = l[i] < 0 ? -l[i] : l[i], ar = r[i] < 0 ? -r[i] : r[i];
            s += (uint64_t)(m * m);
            if (al > pk)
                pk = al;
            if (ar > pk)
                pk = ar;
        }
        s >>= 8;                         /* (a block's mean square of (L + R) / 2) */
        if (b >= PM_WIN)
            sum -= win[b % PM_WIN];
        win[b % PM_WIN] = s > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)s;
        sum += win[b % PM_WIN];
        if (b + 1u >= PM_WIN && sum > best)
            best = sum;
    }
    if (juno) {
        f = pm_lock();
        juno_noise_set(&noise);
        pm_unlock(f);
    }
    *ms = (uint32_t)(best / PM_WIN);
    *peak = pk;
    return 1;
}

PLAT_COLD int player_measure(pl_inst_t in, void *mem, uint32_t size, uint32_t (*lock)(void), void (*unlock)(uint32_t),
                   uint32_t *ms, int32_t *peak)
{
    pm_part_t p[PL_NLAYER];
    pl_voice_t *vs = mem;
    uint32_t nv = size / sizeof(pl_voice_t), n = 0, f, e, k;
    int r = 0;
    if (nv > PL_NLAYER)
        nv = PL_NLAYER;
    if (!nv)
        return 0;
    pm_lock = lock;
    pm_unlock = unlock;
    for (k = 0; k < nv; k++)
        p[k].v = &vs[k];
    f = pm_lock();
    e = pl_engine(in, 0);
    if (e != EN_FUS) {
        samp_t s;
        int note = e == EN_TB ? 36 : 60;
        if (e == EN_SAMP && pl_sample_of(in, &s, 0))
            note = s.base;
        n = (uint32_t)pm_start(&p[0], in, note, 127u, 0);
    }
    pm_unlock(f);
    if (e != EN_FUS)
        return pm_run(p, n, ms, peak);
    {
        static const uint8_t NOTES[2] = {48, 64};
        uint32_t j;
        *ms = 0;
        *peak = 0;
        for (j = 0; j < 2u; j++) {
            rec_cell_t cells[PL_NCH];
            uint32_t m, t, msj;
            int32_t pkj;
            n = 0;
            f = pm_lock();
            m = pl_fusion_row(in, NOTES[j], cells);
            for (t = 0; t < m && n < nv; t++) {
                const rec_cell_t *c = &cells[t];
                pl_inst_t li;
                if (!(c->has & RC_NOTE) || c->note == REC_NOTE_OFF || !(c->has & RC_INS))
                    continue;
                li.cart = c->cart;
                li.slot = c->slot;
                k = pl_engine(li, 0);
                if (k == EN_NONE || k == EN_FUS)
                    continue;
                n += (uint32_t)pm_start(&p[n], li, c->note, c->has & RC_VOL ? c->vol : 127u, 1);
            }
            pm_unlock(f);
            if (pm_run(p, n, &msj, &pkj) && msj >= *ms) {
                *ms = msj;
                *peak = pkj;
                r = 1;
            }
        }
    }
    return r;
}
