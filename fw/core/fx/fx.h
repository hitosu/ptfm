/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The effects (specs/ui/screens/fx.md): the chain on the mix (GATE, FILTER, DRIVE, LO-FI, CHORUS, DELAY,
 * REVERB, LIMITER, in the setup's order; in SENDS routing CHORUS, DELAY and REVERB are shared sends whose
 * returns join the chain at its SENDS item), the punch-ins on the whole mix from a buffer of what just
 * played, the METRONOME after the effects. The player runs it in the audio context, a block of 64 frames
 * at a time, on the mix at the voices' scale (16-bit full scale; a voice enters at half). DUCK is the
 * player's (it works on the tracks before they are mixed); its knobs are read from here. */
#ifndef FX_H
#define FX_H
#include <stdint.h>

#define FX_BLOCK 64

/* the effects' numbers (specs/files/commands.md#the-numbers-are-fixed) */
enum { FX_GATE, FX_FILTER, FX_DRIVE, FX_LOFI, FX_CHORUS, FX_DELAY, FX_REVERB, FX_LIMITER, FX_DUCK,
       FX_METRONOME, FX_SENDS, FX_N = 10 };

/* the music clock: where the song is in beats (beat, and frac: 2^32 a beat), its pace (inc a frame, a
 * beat's frames), how far it may run in this block (hold: frames; the next row's start is its limit while a
 * song plays, so the effects never run ahead of the rows); the player sets it every block */
typedef struct {
    uint32_t beat, frac, inc, beat_frames, hold;
    uint8_t playing;                    /* something plays (the punch-in buffer records, GATE locks to it) */
    uint8_t rpb;                        /* ROWS A BEAT */
    uint8_t keys;                       /* THE KEYS' setup held (nothing played since the start or UNLOAD SONG): the
                                         * METRONOME at FOLLOW counts at its tempo */
    uint32_t fire, held;                /* FILTER's envelope's sources (specs/ui/screens/fx.md#filters-envelope): bit 0
                                         * the keys, bit 1 + t track t (0-15) of what plays; notes started in this block,
                                         * notes held (parked and muted ones too) */
} fx_clock_t;
extern fx_clock_t fx_clk;               /* the player's, as it plays now (the live clock: a block's is kept with it, and
                                         * the effects work with that one, fx_work); DJ's decks' own effects run on it */

/* FILTER's envelope, one a FILTER (the mix's; each DJ deck's own) */
typedef struct {
    uint64_t pos;                       /* a beat SOURCE: the music clock as far as its hits were counted, beats Q32 */
    uint32_t v;                         /* its level, Q24 (1 << 24: as open as ENV says) */
    uint32_t hits;                      /* its source's hits, counted (the picture) */
    volatile uint32_t open;             /* how far it has the FILTER open, Q16 (the picture: fx_filter_open) */
    uint8_t stage;                      /* 0 at rest, 1 ATTACK, 2 DECAY / SUSTAIN, 3 RELEASE */
} fx_fenv_t;
uint32_t fx_filter_open(void);          /* the mix's FILTER: how far its envelope has it open now, as a share of the way
                                         * from CUT to fully open, Q16 (0 at rest) */
uint32_t fx_filter_hits(void);          /* its source's hits, counted */
uint32_t fx_env_t10(int decay, int step);   /* an envelope time's step in 0.1 ms: ATTACK's (decay 0), DECAY's and
                                             * RELEASE's (1) */

#define FX_DUCK_KEYS 16u                /* DUCK's SOURCE: the keys (0-15 the tracks) */
typedef struct {
    uint8_t on, src;                    /* DUCK on; its source: a track (0-15), or the keys (FX_DUCK_KEYS) */
    int32_t depth_q15;                  /* DEPTH */
    uint32_t release_frames;            /* RELEASE: the dip comes back in a straight line over it */
} fx_duck_t;

void fx_init(void);
uint32_t fx_ram(void);                  /* the bytes of its lines and the punch-in buffer (STATUS' RAM MAP: EFFECTS) */
void fx_reset(void);                    /* every line and state cleared, as at power-on */
void fx_setup(const uint8_t *setup);    /* a song's FX setup (64 B; NULL: a new song's): its commands' changes dropped */
/* ... but the effect e (FX_*; FX_N or more: none) kept as it plays: its knobs (`codes`, bit c - 0x90), its ON / OFF,
 * the METRONOME's settings if it is the one. The FX page's effect under the cursor at a song's start
 * (specs/ui/screens/fx.md#the-fx-setup) */
void fx_setup_keep(const uint8_t *setup, uint32_t e, uint64_t codes);
void fx_cmd(uint8_t code, uint8_t value);   /* an FX command, 0x90-0xBF */
int fx_sends(void);                     /* SENDS routing: the player feeds the sends */
void fx_duck(fx_duck_t *d);
void fx_block(int32_t *mix, const int32_t *sends);  /* FX_BLOCK stereo frames in place; sends: chorus, delay,
                                                     * reverb, FX_BLOCK each (SENDS), else NULL */
int fx_rest_block(void);                /* a block of 0s in, mix and sends, when every effect at work rests on it: what
                                         * fx_block would do to them without looking at a frame (their clocks and places
                                         * on); 1 done (0s out), 0 not at rest (fx_block it) */
void fx_punch(int k, int down);         /* a punch-in (0-15 as commands.md's 0xC3 + k) pressed / let go */
void fx_punch_stop(void);               /* STOP: the punch-in ends now */
void fx_kill(void);                     /* all sound off: the tails fade out and are cleared */
void fx_empty(void);                    /* CUT, once faded out: every effect's lines and states 0 at once (its tails,
                                         * a click under way, the LIMITER's gain), the settings as they are */
void fx_hold_block(void);               /* CUT held: fx_block's place, nothing worked on but the time (the METRONOME's
                                         * own tempo counting on); a click started meanwhile dropped */
void fx_bypass(int on);                 /* BYPASS: the chain off (DUCK too: fx_duck says off) */
void fx_click(int sound, int level, int accent);    /* a click now, as the METRONOME's (SOUND 0-3, LEVEL %),
                                                     * heard whether it is on or not: the count-in's */
const int16_t *fx_clicks(void);         /* the last block's clicks (the METRONOME's, a count-in's), as added to both
                                         * sides, FX_BLOCK of them; 0 when it had none (USB audio leaves them out) */
void fx_now(uint8_t *setup);            /* the setup as it plays now (64 B: the commands' changes in it); main loop */
void fx_gate_pattern(int i, const uint8_t *steps);   /* GATE pattern i (16-31, the user's) set: 16 steps, 0-4 */
void fx_gate_steps(int i, uint8_t *steps);           /* GATE pattern i's 16 steps (0-31) */
uint32_t fx_active(void);               /* bit n: effect n is on and working now (the screens) */
/* FX EDIT's activity meters (specs/ui/screens/fx.md#edit): measured while fx_watch(1) (EDIT shown), one block in 4;
 * effect e's (FX_*; DUCK's is the player's) how hard it works, Q15 (32768 full; a send's can pass it) */
void fx_watch(int on);
int32_t fx_meter_q15(int e);
uint32_t fx_asleep(void);               /* bit n: CHORUS, DELAY, REVERB asleep (their lines all 0: skipped while
                                         * nothing comes in, the same sound) */
extern volatile uint32_t fx_us[11];     /* each effect's time (plat_us), by its number; 10: the punch-ins */
uint8_t fx_gate_step(void);             /* the GATE step playing (0-15) */
int fx_met_beat(void);                  /* the bar's beat the METRONOME clicked last (0: the first), -1 while it does not
                                         * count (off, or FOLLOW with nothing playing) */
int32_t fx_limiter_gain(void);          /* the LIMITER's least gain since the last call, Q15 */

/* ---- the settings live and working; a half's blocks at once ----
 * The settings the commands, the setups and the screens change (fx_cmd, fx_setup, fx_force, fx_bypass) are the live
 * ones: the player's, read by the voices' stage (fx_sends, fx_duck, fx_sends_on) and kept with each block it makes
 * (fx_set_get). The effects' work reads only a working copy, put in place with the block's music clock at each
 * block's start (fx_work): so a block's effects run with what that block had, whenever they run.
 * The player makes a half's blocks' voices first, then gives them to the effects together (fx_blocks): each effect
 * then works on them all in turn, its code fetched once, not once a block between the voices'. It is fx_block's
 * work block after block, to the bit, given what each block had: the music clock and the settings (fx_set_t), and
 * the changes to the effects' own state its block made (a click, a punch-in, all sound off, a setup's punch-ins
 * ended, CUT's emptying), which fx_defer keeps with their block instead of making them at once. */
#define FX_HALF_MOST 4u                 /* the blocks fx_blocks takes at most (a half: 256 frames) */
typedef struct {                        /* the settings fx_block works with; the arrays at 0 mod 4, as fx.c's own (the
                                         * device's memcpy copies ends aligned alike a word at a time, others a byte) */
    uint8_t kn[48], chain[12], met[5], route, nchain, forced, byp;
    uint16_t on;
    uint32_t seq;                       /* their generation (0: none yet): a get or a fx_work of the same one is
                                         * skipped, so a copy kept with a block stays valid till the settings change */
} __attribute__((aligned(4))) fx_set_t;
void fx_set_get(fx_set_t *s);           /* the live settings into s */
void fx_work(const fx_set_t *s, const fx_clock_t *clk);   /* a block's settings and music clock in place for its
                                         * effects (fx_block, fx_rest_block, fx_events and fx_blocks work with them) */
void fx_defer(int block);               /* from now, block's changes kept (fx_events / fx_blocks make them); block 0
                                         * starts a half's afresh (the half before's all made); -1: made at once again
                                         * (what was kept still waits) */
void fx_events(int block);              /* the changes kept for that block, made now (a block at a time: its job, on
                                         * either core; it only reads the queue) */
/* a change to the effects' own state, as a block's events make it (fx_events) and as a change made at once does:
 * FX_EV_CLICK (a: SOUND, b: LEVEL, c: accent), FX_EV_PUNCH (a: the punch-in, b: pressed; bf: a beat's frames then),
 * FX_EV_PUNCH_STOP, FX_EV_KILL, FX_EV_EMPTY, FX_EV_SETUP (a setup's punch-ins over), FX_EV_NOISE (DJ over, the decks'
 * DUST having drawn: the mix's noise from where the decks' got to, fx_strip_noise) */
enum { FX_EV_CLICK, FX_EV_PUNCH, FX_EV_PUNCH_STOP, FX_EV_KILL, FX_EV_EMPTY, FX_EV_SETUP, FX_EV_NOISE };
void fx_ev_now(uint32_t kind, int a, int b, int c, uint32_t bf);
int fx_blocks_ok(uint32_t n, const fx_set_t set[]);   /* fx_blocks may take these n blocks (else fx_events +
                                         * fx_block a block at a time): one chain throughout, nothing kept but punch-ins
                                         * (not ECHO THROW's or REVERB SWELL's start), neither of those two ending in
                                         * them, no all sound off, the METRONOME silent */
void fx_blocks(int32_t *const mix[], const int32_t *const sends[], uint32_t n, const fx_clock_t clk[],
               const fx_set_t set[]);   /* fx_block on mix[j] (sends[j]) for j < n, effect by effect; the working
                                         * settings and clock left as the last block's */

/* ---- DJ (specs/ui/screens/dj.md#effects) ----
 * A DJ FILTER: low-pass one way, high-pass the other, OFF in the middle, as FILTER's CUT, set in 1/256 of
 * a step (set_q8: -64 << 8, LP's end, to 63 << 8, HP's; 0 OFF); its resonance as FILTER's RESO (0-100). r
 * NULL: one channel (a send stream). Its cutoff glides a little, so a moving setting never zips. */
typedef struct {
    int32_t s[4];                       /* (its integrators: two a channel) */
    int32_t ix8;                        /* its cutoff now: a log index, Q8 */
    int8_t mode;                        /* -1 LP, 1 HP, 0 off */
    uint8_t quiet;                      /* blocks in a row fed 0 and giving at most a few units: then its states 0,
                                         * and it rests while fed 0 (the same sound) */
} fx_djf_t;
void fx_djfilter(fx_djf_t *f, int32_t *l, int32_t *r, int32_t set_q8, int reso);
void fx_djfilter_rest(fx_djf_t *f, int32_t set_q8);  /* a block not heard (a send nothing takes): its setting
                                                      * followed, its states 0 (afresh when heard again) */
uint32_t fx_sends_on(int forced);       /* the sends an effect takes (SENDS routing): bit 0 CHORUS's, 1 DELAY's, 2
                                         * REVERB's, each on or forced (forced as fx_force: 1 the DELAY, 2 the REVERB) */
/* GATE's, FILTER's, DRIVE's and LO-FI's states: the mix's, and each DJ deck's own */
typedef struct {
    int32_t gate_g;                     /* GATE's gain now, Q24 */
    uint8_t gate_step;                  /* the step playing (the picture) */
    int8_t filt_mode;                   /* FILTER: -1 LP, 1 HP, 0 off */
    int32_t filt_s[4], filt_ix8;        /* its integrators (two a channel), its cutoff now (a log index, Q8) */
    fx_fenv_t filt_env;
    int32_t dr_hp[2], dr_lp[2], lofi_h[2], lofi_n;
} fx_ins_t;
/* A deck's own part of its song's setup: its GATE, FILTER, DRIVE and LO-FI in its chain's order, and
 * DUCK's knobs, with their states; its CHORUS, DELAY, REVERB, LIMITER and METRONOME are DJ's, shared. The
 * player runs each deck's on its own sound, with fx_clk the deck's: the effects' code on the deck's knobs and
 * states, never the mix's (LO-FI's DUST from a noise of the decks' own). */
typedef struct {
    uint8_t route, nchain, chain[12];
    uint16_t on;
    uint8_t kn[48];
    fx_ins_t ins;
} fx_strip_t;
void fx_strip_setup(fx_strip_t *s, const uint8_t *setup);    /* the deck's song's setup (NULL: a new song's) */
void fx_strip_cmd(fx_strip_t *s, uint8_t code, uint8_t value);   /* the deck's song's command for its own effects */
uint32_t fx_strip_lr(fx_strip_t *s, int32_t *l, int32_t *r, int32_t *sends);   /* FX_BLOCK frames in place, the two
                                                     * channels apart; a song in GLOBAL routing: its mix into sends
                                                     * (chorus, delay, reverb) at the MIX of each it has on. Its
                                                     * effects' time back (plat_us: the player counts it) */
extern volatile uint32_t fx_strip_us[4];    /* the decks' GATE, FILTER, DRIVE and LO-FI's time (plat_us), apart from
                                             * fx_us (the mix's effects') */
void fx_strip_noise(int on);            /* DJ's decks on (1): their DUST's noise takes up where the mix's is; off (0): the
                                         * mix's where theirs got to, if they drew from it. One noise as before but on DJ
                                         * with a deck's DUST and the METRONOME's CLICK both sounding (and the noise after
                                         * such a DJ); at a half's start */
void fx_strip_duck(const fx_strip_t *s, fx_duck_t *d);
void fx_force(int delay, int reverb);   /* ECHO OUT, REVERB OUT: the DELAY / the REVERB run, on or not */

/* (the debug build's bench: one block of an effect alone, on l and r (FX_BLOCK each); DJF1: a DJ FILTER on one
 * channel, l; SETS: the settings a block keeps for its effects, taken and put in place, as a half's blocks have them) */
enum { FXB_REVERB, FXB_DELAY, FXB_CHORUS, FXB_FILTER, FXB_DRIVE, FXB_LOFI, FXB_GATE, FXB_LIMITER, FXB_PUNCH, FXB_DJF,
       FXB_DJF1, FXB_SETS, FXB_N };
void fx_bench(int which, int32_t *l, int32_t *r);

#endif
