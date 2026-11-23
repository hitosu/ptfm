/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The voice pool when notes want more engines than VOICES gives (specs/ui/screens/settings.md#voices;
 * specs/proposals/202611121830-ghost-notes.md), through player.h's calls, on a disk:
 *  - a held note taken from its engine goes on as a ghost, unheard, and comes back once an engine is free, started
 *    afresh and worked forward to where its note would be (its sound within 1 dB of the same note never taken), for each
 *    engine that makes ghosts (DX7, Juno-106, sampler, CHIP); let go as a ghost, it comes back in its release, as the
 *    note never taken would sound then;
 *  - the keys' held notes are taken last and come back first, the one made a ghost last first, 4 a block at most;
 *  - a take: the note taken fades out in its engine in the take's block (its last block rendered there), and the new
 *    note sounds from that block, as a note started in a free engine does;
 *  - the list keeps 24 ghosts: a 25th takes the place of the one a ghost longest (the keys'), and a song's note that
 *    ranks below every ghost is not kept;
 *  - a TB-303's note never becomes a ghost (one that would start as a ghost is not played);
 *  - a song's ghost let go by its track's next note, kept 10 s at the most;
 *  - a held ghost with an end of its own (a one-shot sample's, a CHIP note's) goes at that end, never back;
 *  - a smaller VOICES makes ghosts of the held notes above it, a larger one brings them back;
 *  - with no engine free a held ghost comes back into a release tail's engine (the tail fading out there over the
 *    block, among the block's fades, the return beginning there in the next block), a ghost let go never; on DJ the
 *    tail is the one a note of its deck would take;
 *  - a note silent at its last block's end gives its engine up with no fade rendered;
 *  - a mute makes ghosts: a song whose tracks are all muted has no note in an engine within a block, renders nothing,
 *    its new notes ghosts; heard again, they come back; DUCK's source's muted note ranks as heard for a take, a note
 *    muted in the take's own block as silent; the list full, a muted ghost goes before the held ones, a ghost let go
 *    before it, and a muted track's new note (pl_alloc) is not kept against held ones; a tail under hearing at its
 *    mute goes;
 *  - a ghost against the note never taken, every built-in instrument and the disk's (DX7, Juno-106, CHIP, sampler),
 *    held 0.1-60 s and let go, both started in fresh engines: back through the block's budget, or in pieces over many
 *    blocks (a sampler's pitch moved meanwhile too), its engine's envelopes, LFOs and place as the note's stepped block
 *    by block, its sound after the fade the note's (its oscillators' phases, which a return does not keep, made the
 *    note's); gone at its return only where the note has ended or is under hearing, back only where it is heard;
 *  - the budget (the FM-1's: a block begins one return at most, the rest of its work in the blocks after; the returns'
 *    rules cases with it out of their way): returns in pieces carried over to the next block in the engines they hold,
 *    in the order; a return
 *    under way taken by a new note before any sounding note, the ghost staying a ghost; a ghost dropped at its return
 *    counts not among the block's 4; a Juno-106 let go past its RELEASE dropped with no work; one let go dropped under
 *    hearing after its work leaves its engine cut; the walk goes on past a ghost that cannot begin to a return under
 *    way behind it; a return under way given up (a parameter command, a mute, VOICES, the list full) leaves its engine
 *    free, the ghost back later as the note never taken (with the command), or gone;
 *  - the load guard: the half's work on core 1 as it counts it (its engines by kind, a note started, a take's fade),
 *    and no return begun in a heavy half however long; the cap back, the ghosts back;
 *  - a song's ghost's parameter commands at its return: its track's while on its instrument, none (held) or gone (let
 *    go) once the track took another, a FUSION layer's cell read again with the track's on top (gone if changed), a
 *    Juno-106's patch copy kept, a sampler's place and end by the commands it comes back with;
 *  - section 2's rule, case by case: songs of the test's own (a DX7, a Juno-106, a sampler, a CHIP track, a track going
 *    A -> B -> A, each note let go by a note off and slid after it, LEG, 3xx, 1xx, 2xx, vibrato, arpeggio, Cxx, Axy,
 *    NOTE CUT, KEY OFF, SAMPLE OFFSET, parameter commands, K1's restart, the stop at the end; a FUSION track whose layers
 *    run 0xy, EDx, Axy, E9x, ECx) and DJ's decks (two sampler loops: KEY, BRAKE, BACKSPIN, ECHO OUT, REVERB OUT, the
 *    decks' mutes), each played with every note in an engine, then with its notes ghosts (the keys holding every
 *    engine, VOICES 4, its tracks muted and heard again): block by block every note's voice the same but for its
 *    engine (its level, pitch, key, let-go, commands, a sampler's place walked as its note's, its track's notes and
 *    FILTER's held tracks), its engine's pitch, key and level the note's once back;
 *  - the list full by every rank; returns into tails' engines as many a block as its fades left; a take past the
 *    block's fades; a note back not taken again while it fades in; a held ghost under hearing back all the same; a
 *    release unmuted in time heard; a return into its own track's tail first; a sampler ghost let go back in a long
 *    release; a sampler ghost slid, its place and end from one count; a sampler note born a ghost out of its loop by
 *    its own list (LOOP OFF, a loop with no room, a waiting start taken), as its note never taken; a CHIP return's
 *    figures worked out again as its note starts, by the commands it came back with (its track's RELEASE or VOL table
 *    changed since it became a ghost): back, waiting, let go, given up by a note, a command, a mute or VOICES, its end
 *    come by its start; one ending in the block it would come back in gone; an instrument deleted; a song's stop; a
 *    ghost let go through its track's A -> B -> A; a note that cannot sound with the list full; core 2's fallbacks (a
 *    job begun: a TB-303's gone, a sampler's, a CHIP's, a tail let go made ghosts; never begun: a tail in 13-16 rings
 *    on; begun with the list full); the second replay's tracks moving; a faded-out DJ deck's note taking a return
 *    under way; DJ's pass-by in a two-deck session; FILTER's envelope let go by a one-shot ghost's end; E9x restarting
 *    a layer whose return is under way; RECORD LIVE's tap and a return under way taken.
 * The player is included, its pool's links and lists checked after every half (pool_check.h).
 *   voices_test DISK */
#define POOL_CHECK 1
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../core/play/player.c"
#include "../../core/play/rec.h"
#include "../../core/store/store.h"
#include "pool_check.h"

/* the returns' rules cases written before the FM-1's weights (the order, 4 a block, the tails' fades, a mute's or a
 * VOICES change's returns, the waits they count): run with the budget out of their way (it never stops a walk there);
 * every other case runs on the FM-1's own budget (PL_BUDGET), and the FM-1's budget case checks what it does */
#define VT_RULES_BUDGET 65536
static void rules_budget(int on) { pl_budget = on ? VT_RULES_BUDGET : PL_BUDGET; }
#define RULES(call) (rules_budget(1), call, rules_budget(0))

int disk_load(const char *path);
void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);
static int fails;

#ifdef PTFM_DEBUG
/* (built with PTFM_DEBUG: the debug build's ghost figures, checked with the pool's links after every half or block,
 * whatever the case did: every ghost made is back, gone or still in the list, none counted twice or missed; a TB-303's
 * never made, none back while muted. A case starts the count afresh (gf_zero) to check its own figures) */
static int32_t gf_base;                  /* the list's length when the count started */
static void gf_zero(void)
{
    uint8_t g[DBG_GHOSTS_LEN];
    player_gh_figures(g);
    gf_base = (int32_t)pl_gn;
}
static void gf_check(const char *where)
{
    uint32_t made = 0, out = pl_gf.back_free + pl_gf.back_tail, i;
    for (i = 0; i < GM_N; i++)
        made += pl_gf.made[i];
    for (i = 0; i < GO_N; i++)
        out += pl_gf.gone[i][0] + pl_gf.gone[i][1];
    if ((int32_t)(made - out) != (int32_t)pl_gn - gf_base || pl_gf.made_tb || pl_gf.back_muted) {
        fprintf(stderr, "%s: the ghosts' figures: %u made, %u back or gone, the list %u (%d when counted from); "
                "a TB-303's made %u, back while muted %u\n", where, (unsigned)made, (unsigned)out, (unsigned)pl_gn,
                (int)gf_base, (unsigned)pl_gf.made_tb, (unsigned)pl_gf.back_muted);
        exit(1);
    }
    if (made > 20000u)                   /* (the counts kept clear of their 16 bits) */
        gf_zero();
}
#else
#define gf_check(where) ((void)0)
#endif

static double run(double seconds)        /* that long on: the output's RMS (dB) over it */
{
    static int16_t half[512];
    int n = (int)(seconds * 44100 / 256 + 0.5), i, k;
    double s = 0;
    for (k = 0; k < n; k++) {
        player_audio(half, 256);
        pool_check("voices_test");
        gf_check("voices_test");
        for (i = 0; i < 512; i++)
            s += (double)half[i] * half[i];
    }
    return 10 * log10(s / (512.0 * (n ? n : 1)) / (32768.0 * 32768.0) + 1e-20);
}

static double blocks(int n)              /* n blocks of 64 frames, each a call of its own: the peak (16-bit) */
{
    static int16_t b[128];
    int k, i;
    double pk = 0;
    for (k = 0; k < n; k++) {
        player_audio(b, 64);
        pool_check("voices_test");
        gf_check("voices_test");
        for (i = 0; i < 128; i++)
            pk = fabs((double)b[i]) > pk ? fabs((double)b[i]) : pk;
    }
    return pk;
}

static player_state_t st(void)
{
    player_state_t s;
    player_state(&s, 0);
    return s;
}

static int key_on_now(int note)          /* the keys' note `note` is heard */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), i;
    for (i = 0; i < n; i++)
        if (v[i].on && v[i].track == 0xFF && v[i].note == note)
            return 1;
    return 0;
}

static int keys_heard(int *song)         /* the keys' notes heard; *song: the song's */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), i, k = 0;
    *song = 0;
    for (i = 0; i < n; i++)
        if (v[i].on) {
            if (v[i].track == 0xFF)
                k++;
            else
                (*song)++;
        }
    return k;
}

static pl_voice_t *key_voice(int note)   /* the keys' note's voice (with an engine or a ghost), 0 none */
{
    uint32_t k;
    for (k = 0; k < PL_NV; k++)
        if (pl_v[k].use == PV_KEY && pl_v[k].who == note)
            return &pl_v[k];
    return 0;
}

static int in_use(uint32_t engine)       /* voices in use playing an engine (EN_*) */
{
    uint32_t k;
    int n = 0;
    for (k = 0; k < PL_NV; k++)
        n += pl_v[k].use != PV_FREE && pl_v[k].engine == engine;
    return n;
}

static void check(const char *what, int ok)
{
    printf("%-74s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}

static void quiet(void)                  /* nothing sounding, no ghost, the count back at 12, every engine fresh */
{
    uint32_t c;
    player_keys_off();
    player_stop();
    player_all_sound_off();
    player_voice_count(12);
    run(1.5);
    for (c = 0; c < PL_NE; c++)          /* (its next note starts from zero, as a ghost's return does: the two compared
                                          * alike, nothing carried over from the notes before) */
        pl_e[c].kind = 255;
    juno_init();
    memset(pl_chorus0, 0, sizeof pl_chorus0);
}

/* note 60 of `in` alone, or taken by 8 more keys at 0.5 s (VOICES 8) which are let go at 1 s (let_go: 60 is let go
 * too, at 0.8 s, a ghost then); the output's RMS over 2.5-3 s; *was_ghost: 60 was a ghost, unheard, at 0.9 s; *back:
 * 60 heard again alone at 2.5 s */
static double take(pl_inst_t in, int others, int let_go, int *was_ghost, int *back)
{
    int k;
    double r;
    quiet();
    player_voice_count(8);
    run(0.02);
    player_key_on(60, in, 100);
    run(0.5);
    for (k = 0; k < others; k++)
        player_key_on((uint8_t)(62 + k), in, 100);
    run(0.3);
    if (let_go)
        player_key_off(60);
    run(0.1);
    if (was_ghost) {
        pl_voice_t *v = key_voice(60);
        *was_ghost = !key_on_now(60) && v && v->place == PL_NO_ENG && v->gh == GH_GHOST && st().ghosts == (let_go ? 0 : 1);
    }
    run(0.1);
    for (k = 0; k < others; k++)
        player_key_off((uint8_t)(62 + k));
    run(1.5);
    if (back)
        *back = key_on_now(60) && st().voices == 1 && st().ghosts == 0;
    r = run(0.5);
    player_key_off(60);
    return r;
}

/* note 60 of `in` let go at 0.8 s, alone, or (others) taken at 0.5 s by 8 keys of `quick` (VOICES 8: 60 a ghost when let
 * go), let go at 0.9 s, their tails over at once: the output's RMS over 1.5-2 s (60 come back in its release by then) */
static double take_off(pl_inst_t in, pl_inst_t quick, int others)
{
    int k;
    quiet();
    player_voice_count(8);
    run(0.02);
    player_key_on(60, in, 100);
    run(0.5);
    for (k = 0; k < (others ? 8 : 0); k++)
        player_key_on((uint8_t)(62 + k), quick, 100);
    run(0.3);
    player_key_off(60);
    run(0.1);
    for (k = 0; k < (others ? 8 : 0); k++)
        player_key_off((uint8_t)(62 + k));
    run(0.6);
    return run(0.5);
}

/* the first instrument of that kind in the store whose note `ok` takes (its slot in *in, the figure in *got); 0 none */
static int first_of(uint8_t kind, pl_inst_t *in, double *got, int (*ok)(pl_inst_t, double *))
{
    int c, s;
    for (c = 0; c < 256; c++) {
        rec_cart_t rc;
        if (rec_cart((uint8_t)c, &rc) || rc.kind != kind)
            continue;
        for (s = 0; s < rc.nslots && s < 128; s++) {
            if (!rec_slot(&rc, (uint8_t)s))
                continue;
            in->cart = (uint8_t)c;
            in->slot = (uint8_t)s;
            if (ok(*in, got))
                return 1;
        }
    }
    return 0;
}
static int ok_sustains(pl_inst_t in, double *alone)   /* still sounding 2.5 s into a held note (its RMS then) */
{
    return (*alone = take(in, 0, 0, 0, 0)) > -45;
}
static int sustained(uint8_t kind, pl_inst_t *in, double *alone)
{
    return first_of(kind, in, alone, ok_sustains);
}
static int ok_quick(pl_inst_t in, double *rest)   /* sounding when held, silent 0.2 s after its let-go */
{
    double held;
    quiet();
    player_key_on(60, in, 100);
    held = run(0.3);
    player_key_off(60);
    run(0.2);
    *rest = run(0.2);
    return held > -45 && *rest < -100;
}
static pl_inst_t quick_in;
/* let go at 0.8 s, still heard 0.7 s on; a Juno-106's with no LFO on its sound and no chorus (a ghost keeps neither its
 * LFO's phase nor the chorus's, whose line rests while no Juno-106 feeds it: the note never taken would differ from it by
 * more than its envelope) */
static int ok_long(pl_inst_t in, double *alone_off)
{
    const uint8_t *slot;
    if (pl_engine(in, &slot) == EN_JUNO) {
        uint8_t p[JUNO_PATCH_BYTES];
        rec_juno_patch(slot, p);
        if (p[2] || p[8] || p[4] || (p[3] && !(p[17] & 1u)) || juno_chorus_mode(p))   /* (DCO LFO, VCF LFO, the noise,
                                          * PWM by the LFO, the chorus) */
            return 0;
    }
    return (*alone_off = take_off(in, quick_in, 0)) > -50;
}
static int ok_rel_short(pl_inst_t in, double *unused)   /* a Juno-106 whose RELEASE silences it 0.5 s after its
                                                          * let-go */
{
    const uint8_t *slot;
    uint8_t p[JUNO_PATCH_BYTES];
    (void)unused;
    if (pl_engine(in, &slot) != EN_JUNO)
        return 0;
    rec_juno_patch(slot, p);
    return juno_release_ticks(p) < 118u;
}
static int ok_any(pl_inst_t in, double *unused)    /* any instrument there */
{
    (void)in;
    (void)unused;
    return 1;
}
static int ok_loops(pl_inst_t in, double *unused)  /* a sampler instrument with a FORWARD loop */
{
    samp_t s;
    (void)unused;
    return pl_sample_of(in, &s, 0) && s.loop == SAMP_LOOP_FORWARD && s.loop_end > s.loop_start + 1;
}
static int ok_slow(pl_inst_t in, double *unused)   /* a Juno-106 pad with a slow ATTACK (50 and up), a high SUSTAIN (100
                                          * and up), its VCA on the envelope */
{
    const uint8_t *slot;
    uint8_t p[JUNO_PATCH_BYTES];
    *unused = 0;
    if (pl_engine(in, &slot) != EN_JUNO)
        return 0;
    rec_juno_patch(slot, p);
    return p[11] >= 50 && p[13] >= 100 && !(p[17] & 4u);
}
static uint32_t juno_env_of(int note)    /* the keys' note's Juno-106 envelope now (0: none in an engine) */
{
    pl_voice_t *v = key_voice(note);
    return v && v->place != PL_NO_ENG && v->engine == EN_JUNO ? pl_e[v->place].u.ju.env : 0u;
}
/* note 60 of `in` held, taken 4 blocks on by 8 keys of the takers held on (VOICES 8): a ghost with an end of its own (a
 * one-shot sample's END, a CHIP note's VOL table run out), the ghosts' soonest end no later; it goes in the block its
 * end falls in, never back in an engine meanwhile, and with it the soonest end (no ghost left) */
static void own_end(pl_inst_t in, const char *what)
{
    pl_voice_t *v;
    uint32_t age, end_in, b0, k;
    int freed = -1, back = 0, soon;
    char line[112];
    quiet();
    player_voice_count(8);
    run(0.02);
    player_key_on(60, in, 100);
    blocks(4);
    for (k = 0; k < 8; k++)
        player_key_on((uint8_t)(62 + k), quick_in, 100);
    blocks(2);
    v = key_voice(60);
    snprintf(line, sizeof line, "%s %02X:%02X: a ghost with an end of its own, the soonest no later", what, in.cart,
             in.slot);
    if (!v || v->gh != GH_GHOST || v->t_end == PL_NEVER) {
        check(line, 0);
        player_keys_off();
        return;
    }
    check(line, pl_gh_soon != PL_NEVER && (int32_t)(v->t_end - pl_gh_soon) >= 0);
    age = v->age;
    end_in = v->t_end - pl_blk;
    b0 = pl_blk;
    for (k = 0; k < 2u * PL_GH_KEEP && freed < 0; k++) {
        blocks(1);
        if (v->age != age || v->use == PV_FREE)
            freed = (int)(pl_blk - b0);
        else if (v->place != PL_NO_ENG)
            back = 1;
    }
    soon = pl_gn == 0 && pl_gh_soon == PL_NEVER;
    printf("%s %02X:%02X held as a ghost: its end %u blocks on, gone %d blocks on; back %d\n", what, in.cart, in.slot,
           end_in, freed, back);
    snprintf(line, sizeof line, "%s: gone at its own end (its block), never back, no soonest end left", what);
    check(line, freed == (int)end_in + 1 && !back && soon);
    player_keys_off();
}

/* ---- the returns' work on the pool itself, the player's halves not run: a ghost against the note never taken
 * (specs/proposals/202611121830-ghost-notes.md, section 11), the budget in pieces across blocks, a return under way
 * taken by a new note, a ghost dropped at its return not counted, the parameter commands at a return, the walk past a
 * ghost that cannot begin, a return given up, a drop's engine ---- */
static void pool_empty(void)             /* every voice free, every engine fresh (kind 255), VOICES 12 */
{
    uint32_t c;
    for (c = 0; c < PL_NV; c++)
        if (pl_v[c].use != PV_FREE)
            pl_v_free(&pl_v[c]);
    for (c = 0; c < PL_NE; c++) {
        pl_e[c].kind = 255;
        pl_e[c].fade = PL_NO_FADE;
        pl_e[c].wait = PL_NO_VOICE;
    }
    pl_nfd = 0;
    pl_gh_soon = PL_NEVER;
    pl_nv_apply(12);
}
static int blk_tick(void)                /* a block made, unheard: the block clock and the Juno-106's ticks on as the
                                          * voices pass moves them (together, as the forward work counts them); its
                                          * tick */
{
    int t;
    pl_tick_acc += juno_tick_q16;
    t = pl_tick_acc >= 65536u;
    if (t)
        pl_tick_acc -= 65536u;
    pl_blk++;
    return t;
}
static void blk_ends(void)               /* a block's start in the voices pass: the ghosts ended by then gone */
{
    if (pl_gh_soon != PL_NEVER && pl_t_due(pl_gh_soon))
        pl_gh_ends();
}
/* a key's note 60 of `in` (its voice's who) started in engine c, fresh, or (c PL_NO_ENG) as a ghost; 0 if it cannot */
static pl_voice_t *note_at(pl_inst_t in, uint32_t who, uint32_t c)
{
    pl_voice_t *v = pl_v_spare();
    if (c != PL_NO_ENG)
        pl_e[c].kind = 255;
    pl_al_c = (uint8_t)c;
    pl_al_born = c == PL_NO_ENG;
    return pl_voice_on(v, PV_KEY, who, in, 60, 127, 128, 0, 4096, 0, 0, 0, 0) ? v : 0;
}
static void eng_skip(pl_eng_t *e, int tick)   /* a block of an engine's own skip step (the forward work's reference) */
{
    if (e->kind == EN_DX7)
        dx7_skip(&e->u.dx);
    else if (e->kind == EN_JUNO)
        juno_skip(&e->u.ju, tick);
    else if (e->kind == EN_SAMP)
        samp_skip(&e->u.sm);
    else
        chip_skip(&e->u.ch);
}
static void eng_render(pl_eng_t *e, int32_t *b, int tick)
{
    memset(b, 0, PL_BLOCK * sizeof *b);
    if (e->kind == EN_DX7)
        dx7_render(&e->u.dx, b);
    else if (e->kind == EN_JUNO)
        juno_render(&e->u.ju, b, tick);
    else if (e->kind == EN_SAMP)
        samp_render(&e->u.sm, b);
    else
        chip_render(&e->u.ch, b);
}
/* engine a's state as engine b's, as far as the forward work keeps it (../../core/eng/fwd.h): the DX7's six envelopes,
 * pitch envelope and LFO; the Juno-106's whole state but its oscillator's phase (and its patch copy's place); the
 * sampler's envelope, its last block's gain and its place; the CHIP's tables, step envelope, PSG envelope, release and
 * frames */
static int env_same(uint32_t e, const pl_eng_t *a, const pl_eng_t *b)
{
    if (e == EN_DX7)
        return !memcmp(a->u.dx.env, b->u.dx.env, sizeof a->u.dx.env) &&
               !memcmp(&a->u.dx.penv, &b->u.dx.penv, sizeof a->u.dx.penv) &&
               !memcmp(&a->u.dx.lfo, &b->u.dx.lfo, sizeof a->u.dx.lfo);
    if (e == EN_JUNO) {
        juno_voice_t x;
        memcpy(&x, &a->u.ju, sizeof x);
        x.p = b->u.ju.p;
        x.phase = b->u.ju.phase;
        x.sub = b->u.ju.sub;
        x.dphase = b->u.ju.dphase;       /* (its pitch's history: the note comes back at its pitch now, the note never
                                          * taken takes a pitch moved since its last tick at its next) */
        return !memcmp(&x, &b->u.ju, sizeof x);
    }
    if (e == EN_SAMP) {
        const samp_voice_t *p = &a->u.sm, *q = &b->u.sm;
        return p->stage == q->stage && p->rising == q->rising && p->down == q->down && p->level == q->level &&
               p->target == q->target && p->statics == q->statics && p->amp == q->amp && p->pos == q->pos &&
               p->frac == q->frac && p->dir == q->dir && p->looping == q->looping && p->on == q->on;
    }
    {
        const chip_voice_t *p = &a->u.ch, *q = &b->u.ch;
        return !memcmp(p->tp, q->tp, sizeof p->tp) && !memcmp(p->tc, q->tc, sizeof p->tc) && p->vol == q->vol &&
               p->e_acc == q->e_acc && p->rel == q->rel && p->rel_c == q->rel_c && p->gate == q->gate &&
               p->dead == q->dead && p->e_pos == q->e_pos && p->e_r == q->e_r && p->frames == q->frames &&
               p->f_acc == q->f_acc && p->lvol == q->lvol;
    }
}
/* engine a's oscillators' phases made b's (what a return does not keep, specs/proposals/202611121830-ghost-notes.md,
 * section 5: the DX7's operators' phases and feedback; the Juno-106's oscillator and its step, the pitch's history;
 * the CHIP's oscillator, gate, noise and their next steps, its band-limited sum and output filters): its sound then
 * compared with b's, the rest its own */
static void phases_as(uint32_t e, pl_eng_t *a, const pl_eng_t *b)
{
    uint32_t i;
    if (e == EN_DX7) {
        for (i = 0; i < 6u; i++)
            a->u.dx.op[i].phase = b->u.dx.op[i].phase;
        memcpy(a->u.dx.fb_buf, b->u.dx.fb_buf, sizeof a->u.dx.fb_buf);
    } else if (e == EN_JUNO) {
        a->u.ju.phase = b->u.ju.phase;
        a->u.ju.sub = b->u.ju.sub;
        a->u.ju.dphase = b->u.ju.dphase;
    } else if (e == EN_CHIP) {
        chip_voice_t *p = &a->u.ch;
        const chip_voice_t *q = &b->u.ch;
        p->step = q->step;
        p->sq_step = q->sq_step;
        p->nbit = q->nbit;
        p->bz_r = q->bz_r;
        p->len_o = q->len_o;
        p->len_q = q->len_q;
        p->len_n = q->len_n;
        p->nx_o = q->nx_o;
        p->nx_q = q->nx_q;
        p->nx_n = q->nx_n;
        p->lfsr = q->lfsr;
        p->last = q->last;
        p->acc = q->acc;
        memcpy(p->tail, q->tail, sizeof p->tail);
        memcpy(p->hx, q->hx, sizeof p->hx);
        memcpy(p->hy, q->hy, sizeof p->hy);
        p->ly = q->ly;
    }
}
/* the note never taken (a key's, KEYS GAIN 0 dB) in engine r is under hearing: the most it still reaches (each engine's
 * own measure, ../../core/eng/fwd.h) at its gain into the mix (its level; a Juno-106's LEVEL, its level being in its
 * engine's), under -60 dB */
static int quiet_note(const pl_voice_t *v, const pl_eng_t *r)
{
    int64_t h = r->kind == EN_DX7 ? dx7_heard(&r->u.dx) : r->kind == EN_JUNO ? juno_heard(&r->u.ju)
              : r->kind == EN_SAMP ? samp_heard(&r->u.sm) : chip_heard(&r->u.ch);
    int64_t g = r->kind == EN_JUNO ? ((int64_t)PL_ENG_Q12 * v->trim) >> 12 : v->gain;
    return ((h * g) >> 12) < PL_QUIET;
}
typedef struct {
    int how;                             /* -1 cannot sound, 0 back, 1 gone (ended by itself, or let go and under
                                          * hearing), 2 never back (a fault) */
    int same, gone_ok, blocks;           /* back: its envelopes the note's; gone: the note never taken ended or under
                                          * hearing too; the blocks its return took */
    int equal;                           /* back: blocks 4-19 the note's sample for sample (the phases made its) */
    int heard_ok;                        /* back and let go: the note never taken not under hearing */
    double ghost, note;                  /* back: each one's RMS (dB) over blocks 4-19 after it */
    double own;                          /* back: the same with its own phases, the difference (dB) */
} nv_t;
/* instrument in's note 60 started as a ghost (a key's: who 60) and, beside it, the same note in a fresh engine (11)
 * kept there: the note never taken, stepped a block at a time by its engine's own skip step. Held `on` blocks, then
 * (off != FWD_HELD) both let go and `off` blocks more; bent: its pitch moved in some blocks (and every third one of its
 * return under way), twice each, the second standing, the note never taken's with it (a sampler ghost's place walked at
 * each pitch it had, once a block), and one held let go 3 blocks into its return (under way then, for the most).
 * Then the ghost comes back: through pl_returns with the block's
 * budget (piece 0), or claiming engine 0 and worked on by pl_forward with `piece` units a block (in pieces over many
 * blocks, the time going on meanwhile). Back, its engine compared with the note never taken's in that block
 * (env_same), and both rendered 20 blocks on (the Juno-106's noise shared) */
static nv_t never_taken(pl_inst_t in, uint32_t on, uint32_t off, int32_t piece, int bent)
{
    nv_t r = {-1, 0, 0, 0, 1, 1, -200, -200, 0};
    pl_voice_t *g, *v;
    pl_eng_t *ref = &pl_e[11];
    uint32_t b, e, end = on + (off == FWD_HELD ? 0u : off);
    int32_t bud, x[PL_BLOCK], y[PL_BLOCK];
    double sx = 0, sy = 0;
    int t, k, i;
    pool_empty();
    if (!(v = note_at(in, 61, 11)) || !(g = note_at(in, 60, PL_NO_ENG)))
        return r;
    e = g->engine;
    for (b = 0; b < end + 400u; b++) {
        if ((b == on && off != FWD_HELD) || (bent && off == FWD_HELD && b == end + 3u)) {   /* (bent and held: let
                                          * go 3 blocks into its return, under way then for the most) */
            pl_v_off(g);
            pl_v_off(v);
        }
        if (bent && (b % 41u == 7u || (b >= end && b % 3u == 1u))) {   /* (during its return under way too) */
            int32_t p = (int32_t)(b % 3u) * 384 - 384;   /* (-6, 0, +6 semitones) */
            pl_v_pitch(g, p + 64);
            pl_v_pitch(g, p);
            pl_v_pitch(v, p);
        }
        blk_ends();
        if (g->use == PV_FREE)
            break;
        if (b >= end) {                  /* (its time: it comes back) */
            if (piece) {
                if (g->gh == GH_GHOST) {
                    pl_e[0].wait = (uint8_t)(g - pl_v);
                    pl_e[0].kind = 255;
                    g->gh = GH_BACK;
                }
                bud = piece;
                pl_forward(g, 0, &bud);
            } else {
                pl_returns();
            }
            if (g->use == PV_FREE || g->place != PL_NO_ENG)
                break;
            r.blocks++;
        }
        eng_skip(ref, blk_tick());
    }
    if (g->use == PV_FREE) {             /* (gone: its note never taken ended in that block, or, let go, is under
                                          * hearing: a held note is never dropped for quiet) */
        r.how = 1;
        eng_skip(ref, blk_tick());
        r.gone_ok = !pl_e_sounds(ref) || (!v->held && quiet_note(v, ref));
        return r;
    }
    if (g->place == PL_NO_ENG) {
        r.how = 2;
        return r;
    }
    r.how = 0;
    r.blocks++;
    r.same = env_same(e, &pl_e[g->place], ref);
    r.heard_ok = g->held || !quiet_note(v, ref);
    {   /* (first with its own phases, on copies of both engines, the noise and the tick as they stand: the level a
         * return really has, a figure) */
        pl_eng_t ca, cb;
        juno_noise_t nz;
        uint32_t acc = pl_tick_acc;
        double ox = 0, oy = 0;
        memcpy(&ca, &pl_e[g->place], sizeof ca);
        memcpy(&cb, ref, sizeof cb);
        juno_noise_get(&nz);
        for (k = 0; k < 20; k++) {
            acc += juno_tick_q16;
            t = acc >= 65536u;
            if (t)
                acc -= 65536u;
            if (e == EN_JUNO)
                juno_block(t);
            eng_render(&ca, x, t);
            eng_render(&cb, y, t);
            for (i = 0; k >= 4 && i < (int)PL_BLOCK; i++) {
                ox += (double)x[i] * x[i];
                oy += (double)y[i] * y[i];
            }
        }
        juno_noise_set(&nz);
        r.own = fabs(10 * log10(ox + 1e-9) - 10 * log10(oy + 1e-9));
    }
    for (k = 0; k < 20; k++) {
        if (k < 4)                       /* (again after its first blocks: the first makes the operators' gains and the
                                          * DX7's feedback from the note on's; a Juno-106's next tick its pitch's
                                          * step) */
            phases_as(e, &pl_e[g->place], ref);
        t = blk_tick();
        if (e == EN_JUNO)
            juno_block(t);
        eng_render(&pl_e[g->place], x, t);
        eng_render(ref, y, t);
        for (i = 0; k >= 4 && i < (int)PL_BLOCK; i++) {
            sx += (double)x[i] * x[i];
            sy += (double)y[i] * y[i];
            r.equal &= x[i] == y[i];
        }
    }
    r.ghost = 10 * log10(sx / (16.0 * PL_BLOCK) / (32768.0 * 32768.0) + 1e-20);
    r.note = 10 * log10(sy / (16.0 * PL_BLOCK) / (32768.0 * 32768.0) + 1e-20);
    return r;
}
/* every built-in instrument and the disk's (DX7, Juno-106, CHIP; sampler), ages 0.1-60 s held and let go: through the
 * block's budget, in pieces of 3 units a block (to 10 s), and so bent too (a held one let go during its return) */
static void never_taken_all(void)
{
    static const struct {
        uint32_t on, off;
    } A[] = {{69, FWD_HELD}, {689, FWD_HELD}, {6891, FWD_HELD}, {41344, FWD_HELD},   /* (0.1 s, 1 s, 10 s, 60 s) */
             {69, 207}, {689, 34}, {689, 2067}, {6891, 689}};   /* (let go after 0.1 s, 0.3 s on; 1 s, 0.05 s; 1 s, 3 s;
                                          * 10 s, 1 s) */
    static const char *const KIND[] = {"through the budget", "in pieces of 3 units", "in pieces, bent"};
    int c, s, k, m, n[3] = {0, 0, 0}, same[3] = {0, 0, 0}, gone[3] = {0, 0, 0}, gone_ok[3] = {0, 0, 0},
        heard[3] = {0, 0, 0}, near[3] = {0, 0, 0}, equal[3] = {0, 0, 0}, never = 0, most[3] = {0, 0, 0}, nin = 0,
        unheard = 0;
    double worst[3] = {0, 0, 0}, own_worst[4] = {0, 0, 0, 0};
    int own_n[4] = {0, 0, 0, 0}, own_over[4] = {0, 0, 0, 0};   /* (with their own phases, through the budget: by
                                          * engine, DX7, Juno-106, sampler, CHIP) */
    pl_inst_t wi[3] = {{0, 0}, {0, 0}, {0, 0}};
    for (c = 0; c < 256; c++) {
        rec_cart_t rc;
        if (rec_cart((uint8_t)c, &rc) || (rc.kind != REC_DX7 && rc.kind != REC_JUNO && rc.kind != REC_SAMPLER &&
                                          rc.kind != REC_CHIP))
            continue;
        for (s = 0; s < rc.nslots && s < 128; s++) {
            pl_inst_t in = {(uint8_t)c, (uint8_t)s};
            if (!rec_slot(&rc, (uint8_t)s))
                continue;
            nin++;
            for (k = 0; k < (int)(sizeof A / sizeof A[0]); k++)
                for (m = 0; m < 3; m++) {
                    nv_t r;
                    double d;
                    if (m && A[k].on > 6891u)
                        continue;
                    r = never_taken(in, A[k].on, A[k].off, m ? 3 : 0, m == 2);
                    if (r.how < 0)
                        break;
                    n[m]++;
                    never += r.how == 2;
                    if (r.how == 1) {
                        gone[m]++;
                        gone_ok[m] += r.gone_ok;
                        if (!r.gone_ok)
                            printf("  %02X:%02X %u + %u blocks %s: gone, its note never taken still heard\n", c, s,
                                   A[k].on, A[k].off == FWD_HELD ? 0u : A[k].off, KIND[m]);
                        continue;
                    }
                    same[m] += r.same;
                    if (!r.same)
                        printf("  %02X:%02X %u + %u blocks %s: its envelopes not the note never taken's\n", c, s,
                               A[k].on, A[k].off == FWD_HELD ? 0u : A[k].off, KIND[m]);
                    if (!r.heard_ok) {
                        unheard++;
                        printf("  %02X:%02X %u + %u blocks %s: let go, back under hearing\n", c, s, A[k].on, A[k].off,
                               KIND[m]);
                    }
                    if (r.blocks > most[m])
                        most[m] = r.blocks;
                    if (r.note < -66)    /* (under hearing at its loudest's gain: no level to compare) */
                        continue;
                    heard[m]++;
                    equal[m] += r.equal;
                    d = fabs(r.ghost - r.note);
                    near[m] += d <= 0.5;
                    if (!m) {
                        int en = rc.kind == REC_DX7 ? 0 : rc.kind == REC_JUNO ? 1 : rc.kind == REC_SAMPLER ? 2 : 3;
                        own_n[en]++;
                        own_over[en] += r.own > 0.5;
                        own_worst[en] = r.own > own_worst[en] ? r.own : own_worst[en];
                    }
                    if (d > worst[m]) {
                        worst[m] = d;
                        wi[m] = in;
                    }
                }
        }
    }
    printf("a ghost against the note never taken: %d instruments, notes held 0.1-60 s and let go (the level compared "
           "with the oscillators' phases made the note's: they are not kept)\n", nin);
    for (m = 0; m < 3; m++) {
        printf("  %-22s %5d returns: %d back with the note's envelopes (of %d back), %d gone with the note ended or "
               "under hearing (of %d); the level after the fade within 0.5 dB %d of %d heard (worst %.2f dB, "
               "%02X:%02X), %d sample for sample; the longest return %d blocks\n", KIND[m], n[m], same[m],
               n[m] - gone[m], gone_ok[m], gone[m], near[m], heard[m], worst[m], wi[m].cart, wi[m].slot, equal[m],
               most[m]);
    }
    printf("  with their own phases (a figure, not a check: a return keeps no phases), through the budget, blocks 4-19 "
           "after it: over 0.5 dB DX7 %d of %d (worst %.1f dB), Juno-106 %d of %d (%.1f), sampler %d of %d (%.1f), "
           "CHIP %d of %d (%.1f)\n", own_over[0], own_n[0], own_worst[0], own_over[1], own_n[1], own_worst[1],
           own_over[2], own_n[2], own_worst[2], own_over[3], own_n[3], own_worst[3]);
    check("a ghost back: its envelopes the note never taken's, through the budget and in pieces",
          !never && n[0] && same[0] == n[0] - gone[0] && same[1] == n[1] - gone[1] && same[2] == n[2] - gone[2]);
    check("... gone at its return only where the note never taken has ended or (let go) is under hearing",
          gone_ok[0] == gone[0] && gone_ok[1] == gone[1] && gone_ok[2] == gone[2]);
    check("... a ghost let go back only where the note never taken is not under hearing", !unheard);
    check("... its level after the fade within 0.5 dB of the note never taken's (its sound the note's, sample for "
          "sample)", near[0] == heard[0] && near[1] == heard[1] && near[2] == heard[2] && equal[0] == heard[0] &&
          equal[1] == heard[1] && equal[2] == heard[2]);
    check("... in pieces: a return spanning blocks (the budget's carry-over)", most[1] > 1);
}

/* a held ghost of `in` (note 60, held `on` blocks) back into a release tail's engine, against the note never taken (in
 * engine 11, stepped by its skip): VOICES 9, engine 0 a DX7 tail (let go, heard), 1-8 held notes of the DX7 dx. The
 * ghost takes the tail in block N (its fade rendered there: pl_fades); its note starts in the next block, or (delay)
 * later: the 8 held notes made ghosts then, first in the order, their returns spending the blocks' budgets first; or
 * (gone) its instrument deleted between the claim and the start. Returns 1 when it comes back with the note's
 * envelopes and place (env_same), or (gone) when it goes with no start, its engine left free; *late: the blocks from
 * the claim to its start */
static int tail_return(pl_inst_t in, pl_inst_t dx, uint32_t on, int delay, int gone, int *late)
{
    pl_voice_t *g, *t, *s[8];
    pl_eng_t *ref = &pl_e[11];
    uint32_t b, k;
    int claimed;
    pool_empty();
    pl_nv_apply(9);
    if (!note_at(in, 61, 11))
        return 0;
    t = note_at(dx, 72, 0);
    for (k = 0; k < 8u; k++)
        s[k] = note_at(dx, 74u + k, 1u + k);
    g = note_at(in, 60, PL_NO_ENG);
    for (b = 0; b < on; b++)
        eng_skip(ref, blk_tick());
    pl_v_off(t);                         /* (the tail: let go, heard at its last block's end) */
    t->g_last = 4096;
    pl_age_seen = pl_age;
    blk_ends();
    pl_returns();                        /* (block N: the claim, the tail's fade) */
    claimed = g->gh == GH_BACK && pl_e[0].wait == (uint8_t)(g - pl_v) && pl_e[0].fade != PL_NO_FADE;
    pl_fades((int)pl_ticks_in(pl_blk, pl_blk + 1u));
    eng_skip(ref, blk_tick());
    if (gone)
        g->inst.cart = 0x7E;             /* (a cartridge not there) */
    for (k = 0; delay && k < 8u; k++)
        pl_take(s[k], 0);
    for (*late = 1; *late < 40 && g->use != PV_FREE && g->place == PL_NO_ENG; ++*late) {
        blk_ends();
        pl_returns();
        if (g->use == PV_FREE || g->place != PL_NO_ENG)
            break;
        eng_skip(ref, blk_tick());
    }
    if (gone)
        return claimed && g->use == PV_FREE && pl_e[0].wait == PL_NO_VOICE && pl_e[0].kind == 255;
    return claimed && g->place == 0 && env_same(g->engine, &pl_e[0], ref);
}
static void tail_returns(pl_inst_t dx)
{
    static const pl_inst_t IN[4] = {{0xA2, 0x08}, {0xF0, 0x01}, {0xF8, 0x01}, {0xF9, 0x00}};
    int k, ok = 1, r[4], l0[4], d, dl, gone, gl;
    for (k = 0; k < 4; k++) {
        r[k] = tail_return(IN[k], dx, 6891, 0, 0, &l0[k]);
        ok &= r[k];
    }
    d = tail_return(IN[0], dx, 6891, 1, 0, &dl);
    gone = tail_return(IN[1], dx, 6891, 0, 1, &gl);
    printf("held 10 s, back into a tail's engine, as the note never taken: A2:08 %d, F0:01 %d, F8:01 %d, F9:00 %d "
           "(started %d %d %d %d blocks after the claim); A2:08 started %d blocks after (the returns ahead of it "
           "spending the budget) %d; its instrument deleted before its start: gone, no start %d\n", r[0], r[1], r[2],
           r[3], l0[0], l0[1], l0[2], l0[3], dl, d, gone);
    check("a return into a tail's engine: checked at its start, as the note never taken (a sampler's place too), "
          "however late", ok && d && dl > 2 && gone);
    pool_empty();
}

/* the budget across blocks, in the order: eight held ghosts of a DX7 that sustains, made one a block (VOICES 12, every
 * engine free), 60 s on: the block's budget brings back some, a return not finished at a block's end holds its engine
 * (still a ghost) and goes on in the next block, back in that engine; they come back in the order (the one made last
 * first), 4 a block at most */
static void budget_order(pl_inst_t dx)
{
    pl_voice_t *g[8];
    uint8_t held[8];
    int back_at[8], k, b, carried = 0, kept = 1, order = 1, most = 0, all = 1;
    pool_empty();
    for (k = 0; k < 8; k++) {
        g[k] = note_at(dx, 60u + (uint32_t)k, PL_NO_ENG);
        back_at[k] = -1;
        held[k] = PL_NO_ENG;
        blk_tick();
    }
    for (b = 0; b < 41344; b++)
        blk_tick();
    for (b = 0; b < 40; b++) {
        int now = 0;
        blk_ends();
        pl_returns();
        for (k = 0; k < 8; k++) {
            if (back_at[k] < 0 && g[k]->place != PL_NO_ENG) {
                back_at[k] = b;
                now++;
                if (held[k] != PL_NO_ENG) {
                    carried++;
                    kept &= g[k]->place == held[k];
                }
            }
            held[k] = g[k]->gh == GH_BACK ? (uint8_t)pl_wait_eng(g[k]) : PL_NO_ENG;
        }
        most = now > most ? now : most;
        blk_tick();
    }
    for (k = 0; k < 8; k++)
        all &= back_at[k] >= 0;
    for (k = 7; k > 0; k--)              /* (made last: first in the order) */
        order &= back_at[k - 1] >= back_at[k];
    printf("8 held ghosts of %02X:%02X 60 s on, back in blocks %d %d %d %d %d %d %d %d (the one made last first); %d "
           "carried over a block's end in the engine they held %d; 4 a block at most: %d\n", dx.cart, dx.slot,
           back_at[7], back_at[6], back_at[5], back_at[4], back_at[3], back_at[2], back_at[1], back_at[0], carried,
           kept, most);
    check("the budget: returns in pieces carried over to the next block in their engines, in the order", all &&
          order && carried > 0 && kept && most <= 4);
}

/* a return under way (its note started, its forward work still to do) taken by a new note: VOICES 8, 6 notes held and a
 * release tail in engines 0-5 and 7, the ghost's return under way in 6; a new note takes 6 (silent: before any
 * sounding note, the tail's too), the ghost staying a ghost in its place in the list, no take counted; once an engine
 * is free again the ghost comes back */
static void taken_return(pl_inst_t dx)
{
    pl_voice_t *g, *v, *s[8];
    uint32_t c, s0;
    int32_t bud = 1;
    int under, took, back = 0, b;
    pool_empty();
    pl_nv_apply(8);
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    for (c = 0; c < 8u; c++)
        s[c] = note_at(dx, 70u + c, c);
    pl_v_off(s[7]);
    g = note_at(dx, 60, PL_NO_ENG);
    for (b = 0; b < 689; b++)
        blk_tick();
    pl_v_free(s[6]);
    pl_e[6].wait = (uint8_t)(g - pl_v);
    pl_e[6].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 6, &bud);
    under = g->gh == GH_BACK && g->place == PL_NO_ENG && pl_e[6].kind == EN_DX7 && pl_e[6].voice == PL_NO_VOICE;
    s0 = pl_steals;
    v = pl_alloc(-1, EN_DX7);
    took = pl_al_c == 6 && g->gh == GH_GHOST && pl_e[6].wait == PL_NO_VOICE && pl_e[6].kind == 255 && pl_gn == 1 &&
           pl_gl[0] == (uint8_t)(g - pl_v) && pl_steals == s0 && s[7]->place == 7;
    took &= v && pl_voice_on(v, PV_KEY, 61, dx, 60, 127, 128, 0, 4096, 0, 0, 0, 0) && v->place == 6;
    if (took)
        pl_v_free(v);
    for (b = 0; b < 4 && !back; b++) {
        blk_ends();
        pl_returns();
        back = g->place != PL_NO_ENG;
        blk_tick();
    }
    printf("a return under way (its start done) in engine 6: %d; a new note took 6 (the ghost a ghost, no take "
           "counted, the tail kept): %d; back once 6 is free again: %d\n", under, took, back);
    check("a return under way taken by a new note before any sounding note, the ghost staying a ghost", under &&
          took && back);
#ifdef PTFM_DEBUG
    check("the figures: a return given up for a new note", pl_gf.gave[GG_NOTE] == 1 && !pl_gf.gave[GG_COUNT]);
#endif
}

/* a Juno-106 ghost's return under way, its note started: its level, its key (a legato's) and its pitch turn its
 * engine as they change, as they would the note never taken's */
static void back_follows(pl_inst_t ju)
{
    pl_voice_t *g;
    pl_eng_t *e = &pl_e[0];
    int32_t bud = 1;
    int b, ok;
    pool_empty();
    g = note_at(ju, 60, PL_NO_ENG);
    for (b = 0; b < 689; b++)
        blk_tick();
    pl_e[0].wait = (uint8_t)(g - pl_v);
    pl_e[0].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 0, &bud);
    pl_v_level(g, 2048);
    pl_v_jkey(g, 62);
    pl_v_pitch(g, 128);
    ok = g->gh == GH_BACK && e->kind == EN_JUNO && e->u.ju.gain_q12 == g->gain && g->gain == (g->lvl * 2048) >> 12 &&
         e->u.ju.pitch88 == 62u << 8 && e->u.ju.bend == 128 * 21845 + 128 / 3;
    printf("a Juno-106 ghost's return under way: its level %d, key %u, pitch %d on its engine: %d\n", (int)g->gain,
           (unsigned)(e->u.ju.pitch88 >> 8), (int)g->bend, ok);
    check("a return under way: its level, key and pitch turn its engine as they change", ok);
    pool_empty();
}

/* a ghost dropped at its return does not count among the block's 4: VOICES 12, five keys' ghosts of a CHIP instrument
 * with a release and then one of a DX7 whose release is over at once (made last: first of the keys' let go), the DX7's
 * let go a block on, the CHIP's 2 s on, 0.1 s later they come back: the DX7's dropped after its forward work (ended or
 * under hearing), four of the CHIP's back in that block, the one made first still a ghost. And a Juno-106 ghost let go
 * whose RELEASE has made it silent by then is dropped before any work (its engine untouched) */
static void dropped_not_counted(pl_inst_t pad, pl_inst_t quick, pl_inst_t fast)
{
    pl_voice_t *p[5], *q, *j;
    int k, b, back = 0, ok;
    pool_empty();
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    for (k = 0; k < 5; k++)
        p[k] = note_at(pad, 48u + 2u * (uint32_t)k, PL_NO_ENG);
    q = note_at(quick, 70, PL_NO_ENG);
    blk_tick();
    pl_v_off(q);                         /* (let go long before: its work little, the block's budget enough for all) */
    for (b = 0; b < 1378; b++)
        blk_tick();
    for (k = 0; k < 5; k++)
        pl_v_off(p[k]);
    for (b = 0; b < 69; b++)
        blk_tick();
    blk_ends();
    pl_returns();
    for (k = 1; k < 5; k++)
        back += p[k]->place != PL_NO_ENG;
    ok = q->use == PV_FREE && back == 4 && p[0]->gh == GH_GHOST;
    printf("5 ghosts of %02X:%02X let go 0.1 s and one of %02X:%02X (made last) let go 2 s: the last gone %d, the "
           "others back in one block %d of 4, the first made still a ghost %d\n", pad.cart, pad.slot, quick.cart,
           quick.slot, q->use == PV_FREE, back, p[0]->gh == GH_GHOST);
    check("a ghost dropped at its return (after its work) counts not among the block's 4", ok);
#ifdef PTFM_DEBUG
    check("the figures: 6 made, 4 back, the let-go one dropped at its return after its work, the walk stopped by the "
          "count, 5 starts", pl_gf.made[0] + pl_gf.made[1] == 6 && pl_gf.back_free == 4 && pl_gf.n_most == 4 &&
          pl_gf.gone[GO_QUIET][1] + pl_gf.gone[GO_SELF_BACK][1] == 1 && pl_gf.stop4 == 1 &&
          pl_gf.start_n[0] + pl_gf.start_n[1] + pl_gf.start_n[2] + pl_gf.start_n[3] == 5);
#endif
    pool_empty();
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    j = note_at(fast, 60, PL_NO_ENG);
    for (b = 0; b < 69; b++)
        blk_tick();
    pl_v_off(j);
    for (b = 0; b < 689; b++)
        blk_tick();
    blk_ends();
    pl_returns();
    printf("a Juno-106 ghost %02X:%02X let go 1 s, its RELEASE over: gone %d, no engine started %d\n", fast.cart,
           fast.slot, j->use == PV_FREE, pl_e[0].kind == 255);
    check("... a Juno-106 ghost let go past its RELEASE dropped with no work",
          j->use == PV_FREE && pl_e[0].kind == 255);
#ifdef PTFM_DEBUG
    check("the figures: dropped, the JUNO-106's RELEASE over, let go; no start, no forward work",
          pl_gf.gone[GO_JREL][1] == 1 && !pl_gf.start_n[1] && !pl_gf.fwd_n[1] && pl_gf.ready_n[GK_PLAIN] == 1);
#endif
}

/* a song's ghost's parameter commands at its return (pl_gh_ready, specs/proposals/202611121830-ghost-notes.md,
 * section 2), on track 0 set by hand */
static pl_voice_t *song_ghost(pl_inst_t in, uint32_t t, uint32_t note)   /* a song's note of track t born a ghost */
{
    pl_voice_t *v = pl_v_spare();
    pl_al_c = PL_NO_ENG;
    pl_al_born = 1;
    if (!pl_voice_on(v, PV_SONG, t, in, (int)note, 127, 128, 0, 4096, 0, 0, 0, 0))
        return 0;
    v->gen = PL_CH(t).gen;
    return v;
}
static int par_is(const pl_voice_t *v, uint32_t n, const uint8_t *pc, const uint8_t *pv)   /* its list exactly this */
{
    uint32_t i;
    if (v->p.par.n != n)
        return 0;
    for (i = 0; i < n; i++)
        if (v->p.par.pc[i] != pc[i] || v->p.par.pv[i] != pv[i])
            return 0;
    return 1;
}
static void pars_at_return(pl_inst_t dx, pl_inst_t dx2, pl_inst_t ju, pl_inst_t loop)
{
    pl_chan_t *ch = &PL_CH(0);
    pl_voice_t *g, *h;
    uint8_t pc1[1] = {0x48}, pv1[1] = {10};
    int ok, f = -1, row = 0, k = 0, b, c, s;
    pl_inst_t fus = {0, 0};
    pool_empty();
    ch->ins = dx;
    ch->has_ins = 1;
    ch->np = 1;
    ch->pc[0] = 0x48;                    /* (OP6's EG RATE 1: on its copy) */
    ch->pv[0] = 10;
    ch->gen = 7;
    g = song_ghost(dx, 0, 60);           /* its own list: another command */
    g->p.par.n = 1;
    g->p.par.pc[0] = 0x49;
    g->p.par.pv[0] = 20;
    ok = pl_gh_ready(g) && par_is(g, 1, pc1, pv1);
    check("a song's DX7 ghost comes back with its track's commands as they are", ok);
    ch->ins = dx2;                       /* (the track took another instrument) */
    g = song_ghost(dx, 0, 62);
    g->p.par.n = 1;
    h = song_ghost(dx, 0, 64);
    pl_v_off(h);
    ok = pl_gh_ready(g) && g->p.par.n == 0 && !pl_gh_ready(h) && h->use == PV_FREE;
    check("... its track on another instrument: held, with none; let go, gone", ok);
    for (c = 0; c < 256 && f < 0; c++) {   /* (the disk's first FUSION row with a layer of a DX7, sampler or CHIP that
                                          * has parameter commands) */
        rec_cart_t rc;
        if (rec_cart((uint8_t)c, &rc) || rc.kind != REC_FUSION)
            continue;
        for (s = 0; s < rc.nslots && s < 128 && f < 0; s++) {
            pl_inst_t in = {(uint8_t)c, (uint8_t)s};
            if (!rec_slot(&rc, (uint8_t)s))
                continue;
            for (row = 12; row < 128 && f < 0; row++) {
                uint32_t n = pl_fusion_row(in, (uint32_t)row, pl_fcells), j, e, cmds;
                for (k = 0; k < (int)n && f < 0; k++) {
                    const rec_cell_t *cl = &pl_fcells[k];
                    pl_inst_t li = {cl->cart, cl->slot};
                    if ((cl->has & (RC_NOTE | RC_INS)) != (RC_NOTE | RC_INS) || cl->note == REC_NOTE_OFF)
                        continue;
                    e = pl_engine(li, 0);
                    for (j = cmds = 0; j < cl->n; j++)
                        cmds += cl->cmd[j][0] >= 0x40u && cl->cmd[j][0] < 0x80u;
                    if ((e == EN_DX7 || e == EN_SAMP || e == EN_CHIP) && cmds) {
                        f = k;
                        fus = in;
                    }
                }
            }
        }
    }
    if (f >= 0) {                        /* (the loops left row and k one past it) */
        const rec_cell_t *cl;
        pl_voice_t *o;
        uint8_t pc[PL_NPAR], pv[PL_NPAR];
        uint32_t n, j;
        int kept;
        row--;
        pl_fusion_row(fus, (uint32_t)row, pl_fcells);
        cl = &pl_fcells[f];
        ch->ins = fus;
        ch->fus_note = (uint8_t)row;
        ch->gen = 9;                     /* (its track's list still holds 0x48 = 10, from before the note: it never
                                          * turned the layer) */
        g = song_ghost((pl_inst_t){cl->cart, cl->slot}, 0, cl->note);
        g->acc = (uint8_t)(f + 1);
        g->p.par.n = 0;                  /* (its own list: its cell's as it started, a track's command that turned it
                                          * since, 0x4A = 30) */
        for (j = 0; j < cl->n; j++)
            if (cl->cmd[j][0] >= 0x40u && cl->cmd[j][0] < 0x80u)
                pl_par_put(g, cl->cmd[j][0], cl->cmd[j][1]);
        pl_par_put(g, 0x4A, 30);
        n = g->p.par.n;
        memcpy(pc, g->p.par.pc, sizeof pc);
        memcpy(pv, g->p.par.pv, sizeof pv);
        h = song_ghost((pl_inst_t){cl->cart, cl->slot}, 0, cl->note);
        h->acc = (uint8_t)(f + 1);
        h->note = (uint8_t)(cl->note + 1u);
        o = song_ghost((pl_inst_t){cl->cart, cl->slot}, 0, cl->note);
        o->acc = (uint8_t)(f + 1);
        o->gen = 8;                      /* (an older note's, let go) */
        pl_v_off(o);
        kept = pl_gh_ready(g) && par_is(g, n, pc, pv);
        ok = kept && !pl_gh_ready(h) && h->use == PV_FREE && !pl_gh_ready(o) && o->use == PV_FREE;
        printf("a FUSION layer (%02X:%02X row %d, its layer %d): back with its own commands (its cell's, its track's "
               "that turned it; not the track's earlier ones) %d; its cell no longer its note: gone %d; an older note's "
               "layer: gone %d\n", fus.cart, fus.slot, row, f + 1, kept, h->use == PV_FREE, o->use == PV_FREE);
        check("... a FUSION layer: its own commands, its cell read again (gone if changed); an older note's gone", ok);
    } else {
        printf("a FUSION layer: the disk has no layer with parameter commands: not tried\n");
    }
    ch->ins = ju;                        /* a Juno-106's: its patch copy, the track's list not on it */
    g = song_ghost(ju, 0, 60);
    g->p.jp[14] = 99;
    ch->np = 1;
    ch->pc[0] = 0x4E;                    /* (RELEASE) */
    ch->pv[0] = 5;
    check("... a Juno-106's: its patch copy kept", pl_gh_ready(g) && g->p.jp[14] == 99);
    ch->ins = loop;                      /* a sampler's in its loop: the track's LOOP OFF takes it out; END early:
                                          * gone */
    ch->np = 1;
    ch->pc[0] = 0x42;
    ch->pv[0] = 0;
    g = song_ghost(loop, 0, 60);
    h = song_ghost(loop, 0, 60);
    for (b = 0; b < 6891; b++)
        blk_tick();
    ok = pl_gh_ready(g) && !(g->x.sp.hi & SAMP_PL_LOOP) && g->p.par.n == 1;
    ch->pc[0] = 0x41;                    /* (END at 1/256: its place far past it) */
    ch->pv[0] = 1;
    ok &= !pl_gh_ready(h) && h->use == PV_FREE;
    printf("a sampler ghost %02X:%02X in its loop 10 s on: its track's LOOP OFF, it loops no more %d; END before its "
           "place: gone %d\n", loop.cart, loop.slot, g->use != PV_FREE && !(g->x.sp.hi & SAMP_PL_LOOP),
           h->use == PV_FREE);
    check("... a sampler's: its place and its end by the commands it comes back with", ok);
    ch->has_ins = 0;
    ch->np = 0;
    pool_empty();
}

/* a muted track's tail quiet at the mute goes (no ghost); a loud one is a ghost let go (pl_mute_ghost on a pool set by
 * hand) */
static void mute_quiet_tail(pl_inst_t dx)
{
    pl_voice_t *q, *l;
    int ok;
    pool_empty();
    q = note_at(dx, 60, 0);
    l = note_at(dx, 62, 1);
    q->use = l->use = PV_SONG;
    q->who = 0;
    l->who = 1;
    pl_v_off(q);
    pl_v_off(l);
    q->peak = PL_QUIET - 1;
    l->peak = 1000;
    q->m_last = l->m_last = 0;
    pl_mute_ghost(q);
    pl_mute_ghost(l);
    ok = q->use == PV_FREE && l->use == PV_SONG && l->gh == GH_GHOST && !l->held && pl_gn == 1;
    check("a mute: a tail under hearing at it goes, a heard one is a ghost let go", ok);
    pool_empty();
}

/* the walk goes on past a ghost that cannot begin: VOICES 8, the keys' held notes in engines 0-5 and 7, a song's held
 * ghost's return under way in 6 (its note started there, its forward work still to do), then a key's held ghost, first
 * in the order, with no engine free and no tail to take: the song's return goes on behind it and comes back */
static void walk_on(pl_inst_t dx)
{
    pl_chan_t *ch = &PL_CH(0);
    pl_voice_t *g, *k;
    int32_t bud = 1;
    int b, back = -1;
    uint32_t c;
    pool_empty();
    pl_nv_apply(8);
    ch->ins = dx;
    ch->has_ins = 1;
    ch->np = 0;
    for (c = 0; c < 8u; c++)
        if (c != 6u)
            note_at(dx, 70u + c, c);
    g = song_ghost(dx, 0, 60);
    for (b = 0; b < 6891; b++)
        blk_tick();
    pl_e[6].wait = (uint8_t)(g - pl_v);
    pl_e[6].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 6, &bud);
    k = note_at(dx, 80, PL_NO_ENG);
    for (b = 0; b < 12 && back < 0; b++) {
        blk_ends();
        pl_returns();
        pool_check("voices_test, the walk past a ghost");
        if (g->place != PL_NO_ENG)
            back = b;
        blk_tick();
    }
    printf("a key's held ghost that cannot begin, first in the order; a song's return under way behind it: back in "
           "block %d (-1 never), the key's a ghost still %d\n", back, k->gh == GH_GHOST);
    check("the returns' walk goes on past a ghost that cannot begin: a return under way behind it comes back",
          back >= 0 && k->gh == GH_GHOST);
    ch->has_ins = 0;
    pool_empty();
}

/* a return under way (a song's held DX7 ghost 1 s on, its note started in engine 10, its forward work still to do)
 * given up: by a parameter command on its track (pl_param_live: it starts again, with the command), its track muted
 * (passed by while muted), VOICES 8 (its engine past the count): its engine fresh with no ghost waiting there, the
 * ghost a ghost still, and back later, its envelopes the note never taken's (a key's beside it in engine 11, stepped by
 * its engine's own skip step; with the command: the command on its engine); or the list full: the ghost goes (a song's
 * held one before the keys'), its engine fresh */
static void give_ups(pl_inst_t dx)
{
    static const char *const WHAT[4] = {"a parameter command", "its track muted", "VOICES 8", "the list full"};
    pl_chan_t *ch = &PL_CH(0);
    pl_voice_t *g, *v = 0;
    pl_eng_t *ref = &pl_e[11];
    int32_t bud;
    int k, b, i, freed, back, ok, same, val = 0;
    const char *state;
    char line[112];
    for (k = 0; k < 4; k++) {
        pool_empty();
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        ch->ins = dx;
        ch->has_ins = 1;
        ch->np = 0;
        if (k < 2)
            v = note_at(dx, 61, 11);
        g = song_ghost(dx, 0, 60);
        for (b = 0; b < 689; b++)
            if (k < 2)
                eng_skip(ref, blk_tick());
            else
                blk_tick();
        pl_e[10].wait = (uint8_t)(g - pl_v);
        pl_e[10].kind = 255;
        g->gh = GH_BACK;
        bud = 1;
        pl_forward(g, 10, &bud);
        if (k == 0) {                    /* (OP2's EG RATE 1, another than the patch's: the track's list, then its
                                          * notes, as a replay sets a command) */
            int r0 = pl_e[10].u.dx.env[4].rates[0];
            val = r0 >= 50 ? r0 - 40 : r0 + 40;
            ch->np = 1;
            ch->pc[0] = 0x4C;
            ch->pv[0] = (uint8_t)val;
            pl_param_live(g, 0x4C, (uint32_t)val);
        } else if (k == 1) {
            pl_ctxs[0].mute = 1;
            pl_ctxs[0].duck = 0;
            blk_ends();
            pl_returns();
        } else if (k == 2) {
            pl_nv_apply(8);
        } else {
            for (i = 0; i < 23; i++)
                note_at(dx, 70u + (uint32_t)i, PL_NO_ENG);
            pl_gh_room(GR_KEY, FWD_HELD);
        }
        pool_check("voices_test, a return given up");
#ifdef PTFM_DEBUG
        snprintf(line, sizeof line, "the figures: a return under way given up by %s, counted so", WHAT[k]);
        check(line, k < 3 ? pl_gf.gave[k == 0 ? GG_CMD : k == 1 ? GG_MUTE : GG_COUNT] == 1 &&
                            pl_gf.gave[0] + pl_gf.gave[1] + pl_gf.gave[2] + pl_gf.gave[3] == 1
                          : pl_gf.gone[GO_FULL_SONG][0] == 1 && !pl_gf.gave[GG_NOTE] && !pl_gf.gave[GG_COUNT]);
#endif
        freed = pl_e[10].kind == 255 && pl_e[10].wait == PL_NO_VOICE;
        state = g->use == PV_FREE ? "gone" : g->gh == GH_GHOST && g->place == PL_NO_ENG ? "a ghost" : "not a ghost";
        ok = freed && (k == 3 ? g->use == PV_FREE : g->use == PV_SONG && g->gh == GH_GHOST && g->place == PL_NO_ENG);
        pl_ctxs[0].mute = 0;
        for (back = 0, b = 0; k < 3 && b < 12 && !back; b++) {
            if (k < 2)
                eng_skip(ref, blk_tick());
            else
                blk_tick();
            blk_ends();
            pl_returns();
            pool_check("voices_test, a return given up");
            back = g->place != PL_NO_ENG;
        }
        same = k == 0 ? back && pl_e[g->place].u.dx.env[4].rates[0] == val
             : k == 1 ? back && env_same(EN_DX7, &pl_e[g->place], ref) : 1;
        if (k < 3)
            ok &= back && same;
        printf("a return under way given up by %s: its engine fresh %d, the ghost %s; back later %d, %s %d\n", WHAT[k],
               freed, state, back, k == 0 ? "the command on its engine" : k == 1 ? "its envelopes the note never taken's"
               : "(not compared)", same);
        snprintf(line, sizeof line, "a return under way given up by %s: its engine free, the ghost %s", WHAT[k],
                 k == 3 ? "gone" : "back later");
        check(line, ok);
        if (v)
            pl_v_free(v);
        v = 0;
    }
    ch->has_ins = 0;
    ch->np = 0;
    pool_empty();
}

/* a key's ghost let go of a Juno-106 pad with a slow attack, its level 0 (as its track's volume slid down): under
 * hearing at its return, dropped after its forward work, its engine cut, so the next note there starts as in an engine
 * its note has left (its held envelope left there would skip the next note's attack) */
static void quiet_drop(pl_inst_t slow)
{
    pl_voice_t *g;
    int b, ok;
    pool_empty();
    g = note_at(slow, 60, PL_NO_ENG);
    for (b = 0; b < 689; b++)
        blk_tick();
    pl_v_off(g);
    pl_v_level(g, 0);
    for (b = 0; b < 4 && g->use != PV_FREE && g->place == PL_NO_ENG; b++) {
        blk_tick();
        blk_ends();
        pl_returns();
    }
    ok = g->use == PV_FREE && pl_e[0].kind == EN_JUNO && pl_e[0].voice == PL_NO_VOICE && !juno_busy(&pl_e[0].u.ju);
    printf("a Juno-106 pad's ghost %02X:%02X let go, its level 0: gone %d, its engine (0) worked %d and silent %d\n",
           slow.cart, slow.slot, g->use == PV_FREE, pl_e[0].kind == EN_JUNO, !juno_busy(&pl_e[0].u.ju));
    check("a ghost let go dropped under hearing at its return: its engine cut", ok);
    pool_check("voices_test, a return dropped");
    pool_empty();
}

/* ---- the player's cases of section 11 on a pool set by hand ---- */
/* a ghost set by hand, made last: a key's (use PV_KEY, who its key) or a song's (track who), held or let go `ago` blocks
 * ago */
static pl_voice_t *gh_put(uint32_t use, uint32_t who, int held, uint32_t ago)
{
    pl_voice_t *g = pl_v_spare();
    memset(g, 0, sizeof *g);
    g->use = (uint8_t)use;
    g->engine = EN_DX7;
    g->inst = (pl_inst_t){0xF0, 0x00};
    g->who = (uint8_t)who;
    g->held = (uint8_t)held;
    g->place = PL_NO_ENG;
    g->m_last = 4096;
    g->age = ++pl_age;
    g->t_on = pl_blk - 1000u;
    g->t_off = held ? FWD_HELD : pl_blk - ago;
    g->t_end = held ? PL_NEVER : g->t_off + PL_GH_KEEP;
    pl_gh_add(g);
    return g;
}
/* the list full, every rank (section 6), 24 ghosts set by hand: the keys' held (K), the songs' held (S), muted ones
 * (track 0's, M) and three let go (L, 100, 300 and 50 blocks ago); a key's held ghost more each time: those let go go
 * first, let go longest ago first, then the muted, the songs' held, the keys' held, each a ghost longest first. One
 * let go longer ago than every one let go is not kept; with the keys' held alone left, a song's held or a muted one is
 * not kept */
static void drop_ranks(void)
{
    static const char KIND[25] = "SMKLKSKKSKLKMKKSKLKKMKSK";
    static const int WANT[11] = {10, 3, 17, 1, 12, 20, 0, 5, 8, 15, 22};
    pl_voice_t *g[24];
    uint32_t age[24];
    int k, i, order = 1, at = 0, refused;
    quiet();
    pl_ctxs[0].mute = 1;
    pl_ctxs[0].duck = 0;
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    for (k = 0; k < 24; k++) {
        g[k] = gh_put(KIND[k] == 'K' ? PV_KEY : PV_SONG, KIND[k] == 'K' ? 40u + (uint32_t)k : KIND[k] == 'M' ? 0u : 1u,
                      KIND[k] != 'L', k == 3 ? 100u : k == 10 ? 300u : 50u);
        age[k] = g[k]->age;
    }
    refused = !pl_gh_room(GR_LET, pl_blk - 400u) && pl_gn == 24;   /* (let go before every one let go) */
    for (i = 0; i < 24; i++) {
        int gone = -1;
        if (!pl_gh_room(GR_KEY, FWD_HELD)) {
            order = 0;
            break;
        }
        for (k = 0; k < 24; k++)
            if (age[k] && (g[k]->use == PV_FREE || g[k]->age != age[k])) {
                gone = k;
                age[k] = 0;
            }
        order &= gone == (at < 11 ? WANT[at] : -2) || (at >= 11 && gone >= 0 && KIND[gone] == 'K');
        if (at >= 11)                    /* (the keys' held: each a ghost longest first) */
            for (k = 0; k < gone; k++)
                order &= KIND[k] != 'K' || !age[k];
        at++;
        gh_put(PV_KEY, 100u + (uint32_t)i, 1, 0);   /* (24 again) */
        if (at == 11)                    /* (the keys' held alone: a song's held, a muted one ranks below them) */
            refused &= !pl_gh_room(GR_SONG, FWD_HELD) && !pl_gh_room(GR_MUTED, FWD_HELD) && pl_gn == 24;
    }
    printf("the list full, every rank: dropped in the order of section 6 %d (%d drops); not kept when below them all "
           "%d\n", order, at, refused);
    check("the list full: let go (longest ago first), muted, songs' held, keys' held, each a ghost longest first", order &&
          at == 24 && refused);
#ifdef PTFM_DEBUG
    check("the figures: dropped for room by rank (3 let go, 3 muted, 5 songs' held, 13 keys' held), 3 not kept",
          pl_gf.gone[GO_FULL_LET][1] == 3 && pl_gf.gone[GO_FULL_MUTED][0] == 3 && pl_gf.gone[GO_FULL_SONG][0] == 5 &&
          pl_gf.gone[GO_FULL_KEY][0] == 13 && pl_gf.full_new[GR_LET] == 1 && pl_gf.full_new[GR_SONG] == 1 &&
          pl_gf.full_new[GR_MUTED] == 1);
#endif
    pool_empty();
    pl_ctxs[0].mute = 0;
    quiet();
}

/* returns into tails' engines as many a block as the block has fades left (pl_fade_most: 4 at VOICES 12, the takes'
 * fades first): VOICES 12, the 12 engines release tails heard, 8 keys' held ghosts: 4 claim tails (fading out there,
 * the ghosts' returns waiting), the other 4 wait; with 3 takes made in the block first, 1 */
static void fade_limit(pl_inst_t dx)
{
    int pre, ok = 1, got[2] = {0, 0}, still[2] = {0, 0};
    uint32_t c, k;
    for (pre = 0; pre < 2; pre++) {
        pl_voice_t *g[8];
        pool_empty();
        for (c = 0; c < 12u; c++) {
            pl_voice_t *t = note_at(dx, 70u + c, c);
            pl_v_off(t);
            t->g_last = 4096;
            t->peak = 500;
        }
        for (k = 0; k < 8u; k++)
            g[k] = note_at(dx, 40u + k, PL_NO_ENG);
        pl_age_seen = pl_age;            /* (every note heard at its last block's end) */
        for (k = 0; pre && k < 3u; k++) {   /* (three new keys' notes: each takes a tail, fading) */
            pl_voice_t *v = pl_alloc(-1, EN_DX7);
            ok &= v && pl_voice_on(v, PV_KEY, 90u + k, dx, 60, 127, 128, 0, 4096, 0, 0, 0, 0) && v->gh == GH_WAIT;
        }
        blk_ends();
        pl_returns();
        for (c = 0; c < PL_NE; c++)
            got[pre] += pl_e[c].fade != PL_NO_FADE && pl_e[c].wait != PL_NO_VOICE && pl_v[pl_e[c].wait].gh == GH_BACK;
        for (k = 0; k < 8u; k++)
            still[pre] += g[k]->gh == GH_GHOST;
        pl_fades((int)pl_ticks_in(pl_blk, pl_blk + 1u));
        pool_check("voices_test, the fade limit");
    }
    printf("returns into tails' engines at VOICES 12 (4 fades a block): %d claimed, %d waiting; with 3 takes' fades "
           "first: %d claimed, %d waiting\n", got[0], still[0], got[1], still[1]);
    check("returns into tails' engines: as many a block as its fades left, the takes' first", ok && got[0] == 4 &&
          still[0] == 4 && got[1] == 1 && still[1] == 7);
    pool_empty();
}

/* a take past the block's fades (section 7): VOICES 12, 12 keys held and heard; 6 more at one half's start: the first 4
 * taken notes fade out in their engines in that block, the other 2 are cut at once (no block rendered for them), all 6
 * go on as ghosts; the 6 new notes sound from that block */
static void take_past(pl_inst_t pad)
{
    int k, on = 1;
    uint32_t b0, s0;
    quiet();
    for (k = 0; k < 12; k++)
        player_key_on((uint8_t)(36 + 2 * k), pad, 100);
    run(0.5);
    s0 = pl_steals;
    for (k = 0; k < 6; k++)
        player_key_on((uint8_t)(70 + 2 * k), pad, 100);
    b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
    blocks(1);
    for (k = 0; k < 6; k++)
        on &= key_voice(70 + 2 * k) && key_voice(70 + 2 * k)->place != PL_NO_ENG;
    printf("6 takes in a block at VOICES 12: %u engine blocks (12 notes and the fades), %u taken, %d ghosts, the new "
           "notes in their engines %d\n", pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3] - b0,
           pl_steals - s0, st().ghosts, on);
    check("a take past the block's fades: cut at once (4 fades), each a ghost, the new notes sounding",
          pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3] - b0 == 16u && pl_steals - s0 == 6u &&
          st().ghosts == 6 && on);
    player_keys_off();
    quiet();
}

/* a note back, fading in, is not taken again (section 5), pools set by hand at VOICES 8: the songs' held notes, engine
 * 3's back from a ghost (fading in) and the quietest: the next quietest goes; the keys' held, engine 0's the oldest and
 * fading in: the next oldest goes */
static void back_kept(pl_inst_t dx)
{
    static const int16_t PEAK[8] = {300, 200, 400, 10, 500, 500, 500, 500};
    pl_voice_t *t[8], *a, *b;
    uint32_t c;
    pool_empty();
    pl_nv_apply(8);
    for (c = 0; c < 8u; c++) {
        t[c] = note_at(dx, 60u + c, c);
        t[c]->use = PV_SONG;
        t[c]->who = (uint8_t)c;
        t[c]->peak = PEAK[c];
        t[c]->g_last = 4096;
    }
    pl_age_seen = pl_age;
    t[3]->back = 2;
    a = pl_victim(-1, 9, 0);
    for (c = 0; c < 8u; c++) {
        t[c]->use = PV_KEY;
        t[c]->back = 0;
    }
    t[0]->back = 2;
    b = pl_victim(-1, -1, 0);
    printf("a note fading back in: the songs' quietest taken instead (engine %d), the keys' oldest instead (engine %d)\n",
           a ? a->place : -1, b ? b->place : -1);
    check("a note back, fading in, is not taken again: the next of its kind goes", a == t[1] && b == t[1]);
    pool_empty();
}

/* a held ghost quiet at its return comes back all the same (it may rise again: section 5): a Juno-106 pad with a slow
 * attack, a key's ghost from its start with its level 0 (as a track's volume slid down), back a block on, under
 * hearing; its level turned up: its engine's with it */
static void quiet_held(pl_inst_t slow)
{
    pl_voice_t *g;
    int back, under = 0, rose = 0;
    pool_empty();
    g = note_at(slow, 60, PL_NO_ENG);
    pl_v_level(g, 0);
    blk_tick();
    blk_ends();
    pl_returns();
    back = g->use == PV_KEY && g->place != PL_NO_ENG && g->held;
    if (back) {
        under = pl_gh_quiet(g, &pl_e[g->place]);
        pl_v_level(g, 4096);
        rose = pl_e[g->place].u.ju.gain_q12 == g->gain && g->gain > 0;
    }
    printf("a held ghost of %02X:%02X, its level 0, back a block on: back %d, under hearing then %d, its level up on its "
           "engine %d\n", slow.cart, slow.slot, back, under, rose);
    check("a held ghost under hearing at its return comes back (never dropped for quiet), then rises", back && under &&
          rose);
    pool_empty();
}

/* a release unmuted in time is heard (section 3): a song's held note (track 0, on its instrument) muted at 1 s (a ghost),
 * let go 0.1 s on while muted (a ghost let go), heard again 0.3 s on, in its release: back in its release, its envelopes
 * the note never taken's (beside it in engine 11, a key's, let go at the same block, stepped by its skip) */
static void release_heard(pl_inst_t lng)
{
    pl_chan_t *ch = &PL_CH(0);
    pl_voice_t *g, *v;
    pl_eng_t *ref = &pl_e[11];
    int b, kept = 1, back, same = 0;
    pool_empty();
    ch->ins = lng;
    ch->has_ins = 1;
    ch->np = 0;
    v = note_at(lng, 61, 11);
    g = note_at(lng, 60, 0);
    g->use = PV_SONG;
    g->who = 0;
    for (b = 0; b < 689; b++) {
        int t = blk_tick();
        eng_skip(ref, t);
        eng_skip(&pl_e[0], t);
    }
    pl_ctxs[0].mute = 1;
    pl_ctxs[0].duck = 0;
    g->m_last = 0;
    pl_mute_ghost(g);                    /* (as the voices pass makes it once its mute's ramp is done) */
    for (b = 0; b < 69; b++) {
        eng_skip(ref, blk_tick());
        blk_ends();
        pl_returns();
        kept &= g->use == PV_SONG && g->gh == GH_GHOST;
    }
    pl_v_off(g);
    pl_v_off(v);
    for (b = 0; b < 207; b++) {          /* (muted: never back) */
        eng_skip(ref, blk_tick());
        blk_ends();
        pl_returns();
        kept &= g->use == PV_SONG && g->gh == GH_GHOST && !g->held;
    }
    pl_ctxs[0].mute = 0;
    blk_ends();
    pl_returns();
    back = g->use == PV_SONG && g->place != PL_NO_ENG && !g->held;
    if (back)
        same = env_same(EN_JUNO, &pl_e[g->place], ref) && !quiet_note(v, ref);
    printf("a song's note of %02X:%02X muted, let go while muted, heard again 0.3 s on: a ghost while muted %d, back in "
           "its release %d, as the note never taken %d\n", lng.cart, lng.slot, kept, back, same);
    check("a release unmuted in time is heard: back in its release, as the note never taken", kept && back && same);
    ch->has_ins = 0;
    pool_empty();
}

/* a held ghost's return takes its own track's tail first (pl_choose's rank, as a new note of its track would): VOICES 8,
 * the 8 engines songs' release tails heard, track 1's in engine 5 (peak 300), track 2's in the others (peak 100 and
 * up); a held ghost of track 1 comes back into track 1's tail's engine; one of track 3 (none of its own) into the
 * quietest */
static void own_tail(pl_inst_t dx)
{
    pl_voice_t *t[8], *g1, *g3;
    uint32_t c, w1 = PL_NO_ENG, w3 = PL_NO_ENG;
    pool_empty();
    pl_nv_apply(8);
    for (c = 0; c < 2u; c++) {
        PL_CH(1u + 2u * c).ins = dx;     /* (tracks 1 and 3 on its instrument) */
        PL_CH(1u + 2u * c).has_ins = 1;
        PL_CH(1u + 2u * c).np = 0;
    }
    for (c = 0; c < 8u; c++) {
        t[c] = note_at(dx, 60u + c, c);
        t[c]->use = PV_SONG;
        t[c]->who = (uint8_t)(c == 5u ? 1u : 2u);
        pl_v_off(t[c]);
        t[c]->peak = (int16_t)(c == 5u ? 300 : c == 2u ? 100 : 200 + 10 * (int)c);
        t[c]->g_last = 4096;
    }
    pl_age_seen = pl_age;
    g1 = song_ghost(dx, 1, 50);
    g3 = song_ghost(dx, 3, 52);
    blk_ends();
    pl_returns();
    for (c = 0; c < PL_NE; c++) {
        if (pl_e[c].wait == (uint8_t)(g1 - pl_v))
            w1 = c;
        if (pl_e[c].wait == (uint8_t)(g3 - pl_v))
            w3 = c;
    }
    printf("held ghosts back into tails' engines: track 1's into engine %d (its own track's tail: 5), track 3's into %d "
           "(the quietest: 2)\n", (int)w1, (int)w3);
    check("a return takes its own track's tail first, else the quietest", w1 == 5u && w3 == 2u);
    pl_fades((int)pl_ticks_in(pl_blk, pl_blk + 1u));
    for (c = 0; c < 2u; c++)
        PL_CH(1u + 2u * c).has_ins = 0;
    pool_empty();
}

/* a key's note 60 (who) of `in` with parameter commands of its own (pc, pv, n: its list, as a track's would give it)
 * started in fresh engine c, or (c PL_NO_ENG) as a ghost */
static pl_voice_t *note_par(pl_inst_t in, uint32_t who, uint32_t c, const uint8_t *pc, const uint8_t *pv, uint32_t n)
{
    pl_voice_t *v = pl_v_spare();
    if (c != PL_NO_ENG)
        pl_e[c].kind = 255;
    pl_al_c = (uint8_t)c;
    pl_al_born = c == PL_NO_ENG;
    return pl_voice_on(v, PV_KEY, who, in, 60, 127, 128, 0, 4096, pc, pv, n, 0) ? v : 0;
}
/* a sampler ghost let go comes back in its release, a long one (section 11): an instrument that loops (in: its note 60
 * slow, a sample of a second played over 2 s), made to ring by its note's own list (its loop moved to the sample's
 * start, so a let-go plays on through the rest of it; R4 slow), held 1 s, let go, back 0.5 s on: in its release, its
 * envelope and place the note never taken's (beside it in engine 11, stepped by samp_skip), heard */
static void samp_long(pl_inst_t in)
{
    static const uint8_t PC[3] = {0x43, 0x44, 0x4B}, PV[3] = {4, 16, 30};   /* (LOOP START, LOOP END, R4) */
    pl_voice_t *g, *v;
    pl_eng_t *ref = &pl_e[11];
    int b, rings = 0, back = 0, same = 0;
    pool_empty();
    v = note_par(in, 61, 11, PC, PV, 3);
    g = note_par(in, 60, PL_NO_ENG, PC, PV, 3);
    for (b = 0; v && g && b < 689; b++) {
        eng_skip(ref, blk_tick());
        blk_ends();
    }
    if (v && g) {
        pl_v_off(g);
        pl_v_off(v);
        for (b = 0; b < 345 && g->use != PV_FREE; b++) {
            eng_skip(ref, blk_tick());
            blk_ends();
        }
        rings = pl_e_sounds(ref) && !quiet_note(v, ref);
        blk_ends();
        pl_returns();
        back = g->use == PV_KEY && g->place != PL_NO_ENG && !g->held;
        same = back && env_same(EN_SAMP, &pl_e[g->place], ref);
    }
    printf("a sampler ghost of %02X:%02X (its loop early, R4 slow) let go after 1 s: its note never taken heard 0.5 s on "
           "%d; back in its release %d, its envelope and place the note's %d\n", in.cart, in.slot, rings, back, same);
    check("a sampler ghost let go comes back in its release, as the note never taken", rings && back && same);
    pool_empty();
}
/* a sampler note born a ghost whose own list takes it out of its loop (section 2: its place where its note starts, as
 * its commands put it): A2:08 (it loops) with (a) LOOP OFF, (b) LOOP START and LOOP END on one point (a loop with no
 * room), as a key's ghost from its start; (c) a song's note with LOOP OFF whose start waits for a fading engine (it took
 * a song's note, VOICES 8, the other engines the keys'), taken in its block by a key's note (pl_take: born then from
 * the place its start kept). Each against the same note never taken (in engine 11, stepped by samp_skip): its place,
 * walked as its return would find it, the note's every block; gone in the block after the note ended (by its place: it
 * plays on to END) */
static int born_follow(pl_voice_t *g, pl_eng_t *ref, int *gone, int *end, int *by_place)
{
    int b, same = 1;
    *gone = *end = -1;
    *by_place = 0;
    for (b = 0; b < 20000; b++) {
        samp_t s;
        blk_ends();
        if (g->use == PV_FREE) {
            *gone = b;
            break;
        }
        if (*end < 0 && pl_v_samp(g, &s, 1)) {
            samp_place_t p = g->x.sp, a;
            if (pl_blk != g->t_pl)
                samp_place_go(&s, &p, samp_step(&s, g->note, g->pb + g->tune), (pl_blk - g->t_pl) * PL_BLOCK, g->held);
            samp_place_of(&ref->u.sm, &a);
            same &= !memcmp(&a, &p, sizeof a);
        }
        eng_skip(ref, blk_tick());
        if (*end < 0 && !pl_e_sounds(ref)) {
            *end = b;
            *by_place = ref->u.sm.pos < 0 || ref->u.sm.pos >= ref->u.sm.s.end - 1;
        }
    }
    return same;
}
static void born_unloop(pl_inst_t lp, pl_inst_t dx)
{
    static const char *const WHAT[3] = {"LOOP OFF", "a loop with no room", "LOOP OFF, its waiting start taken"};
    static const uint8_t PC[3][2] = {{0x42, 0}, {0x43, 0x44}, {0x42, 0}}, PV[3][2] = {{0, 0}, {128, 128}, {0, 0}};
    static const uint32_t NP[3] = {1, 2, 1};
    int m, ok = 1;
    for (m = 0; m < 3; m++) {
        pl_voice_t *g = 0, *v;
        int same = 0, gone = -1, end = -1, by_place = 0, born = 0;
        pool_empty();
        if (m < 2) {
            v = note_par(lp, 61, 11, PC[m], PV[m], NP[m]);
            g = note_par(lp, 60, PL_NO_ENG, PC[m], PV[m], NP[m]);
            born = g && g->gh == GH_GHOST;
        } else {
            pl_voice_t *s, *k;
            uint32_t c;
            pl_nv_apply(8);
            for (c = 0; c < 7u; c++)
                note_at(dx, 70u + c, c);
            s = note_at(dx, 80, 7);      /* (a song's held note, heard: the one a song's new note takes) */
            s->use = PV_SONG;
            s->who = 1;
            s->g_last = 4096;
            pl_age_seen = pl_age;
            g = pl_alloc(0, EN_SAMP);
            if (g && pl_al_c == 7 && pl_voice_on(g, PV_SONG, 0, lp, 60, 127, 128, 0, 4096, PC[m], PV[m], NP[m], 0) &&
                g->gh == GH_WAIT) {
                k = pl_alloc(-1, EN_DX7);   /* (a key's note takes it in its block: it is born a ghost there) */
                born = k && pl_al_c == 7 && g->gh == GH_GHOST && g->held && g->place == PL_NO_ENG &&
                       pl_voice_on(k, PV_KEY, 90, dx, 60, 127, 128, 0, 4096, 0, 0, 0, 0);
            }
            pl_fades((int)pl_ticks_in(pl_blk, pl_blk + 1u));
            v = note_par(lp, 61, 11, PC[m], PV[m], NP[m]);   /* (the note never taken, started in the same block) */
        }
        if (born && v)
            same = born_follow(g, &pl_e[11], &gone, &end, &by_place);
        printf("a sampler note of %02X:%02X born a ghost, %s: its place the note never taken's %d; the note ended in "
               "block %d (by its place %d), the ghost gone in block %d\n", lp.cart, lp.slot, WHAT[m], same, end, by_place,
               gone);
        ok &= born && same && by_place && end > 0 && gone == end + 1;
    }
    check("a sampler note born a ghost: out of its loop by its own list, as the note never taken (3 ways)", ok);
    pool_empty();
}
/* a CHIP note of in from its start (its own list pc / pv, np of them): the blocks to its end by itself, held (CHIP_NEVER:
 * none), or (held 0) let go then: its release's length, as a ghost's figures take it */
static uint32_t chip_end_of(pl_inst_t in, uint8_t pc, uint8_t pv, uint32_t np, int held)
{
    const uint8_t *slot;
    chip_patch_t cp;
    chip_voice_t cv;
    if (pl_engine(in, &slot) != EN_CHIP)
        return 0;
    chip_unpack(slot, &cp);
    if (np)
        chip_set(&cp, pc, pv);
    memset(&cv, 0, sizeof cv);
    chip_note_on(&cv, &cp, 60);
    return chip_end_blocks(&cv, held);
}
/* the blocks a frame of a CHIP note of in takes (its own list pc / pv, np of them: its clock and FPS), rounded up: a
 * note started and stepped till a frame comes round (0: none in 64 blocks) */
static uint32_t chip_frame_blocks(pl_inst_t in, uint8_t pc, uint8_t pv, uint32_t np)
{
    const uint32_t B = (uint32_t)CHIP_N << 16;
    const uint8_t *slot;
    chip_patch_t cp;
    chip_voice_t cv;
    uint32_t i, a;
    if (pl_engine(in, &slot) != EN_CHIP)
        return 0;
    chip_unpack(slot, &cp);
    if (np)
        chip_set(&cp, pc, pv);
    memset(&cv, 0, sizeof cv);
    chip_note_on(&cv, &cp, 60);
    for (i = 0; i < 64u && !cv.dead; i++) {
        a = cv.f_acc;
        chip_skip(&cv);
        if (cv.f_acc < a + B)            /* (a frame came round in this block: a + B - its length) */
            return (a + B - cv.f_acc + B - 1u) / B;
    }
    return 0;
}
/* a CHIP ghost's return, by the commands it comes back with (section 2: they act from its return, and it ends as its
 * voice would): a song's CHIP note of track 0 made a ghost, its track's command changed since (RELEASE 1, 8, 15 on chp,
 * whose own RELEASE gives it no release; on vt, whose own VOL table runs out at E, a VOL table running out later, at E2,
 * or sooner), its return begun in a fresh engine (its commands rebuilt, its note started there: its figures worked out
 * again from that note's start), its work in pieces, or waiting. Each against the note never taken (with that command
 * from its start, in an engine of its own, stepped by chip_skip). "Gone after the note": in the block after the one
 * the note never taken ended in, as a voice is freed in the block after its engine ends; "within a frame": its release
 * counted from its note's start frame, the note never taken's from its let-go's place in a frame, so up to a frame of
 * the instrument later (F blocks: its clock and FPS), never sooner, and exactly at its let-go plus the release's length
 * a scratch note with its rebuilt list gives from its start (chip_end_of):
 *  -  0-2: RELEASE 1, 8, 15, let go as its note starts, its work in pieces: back as the note never taken, ending with it;
 *  -    3: E2's table, held, its note started 3 blocks before E, its work waiting till 5 blocks after E, then in pieces:
 *          back as the note, ending with it;
 *  - 4, 5: controls: let go before its return begins, or with its claim on a tail's engine whose note is still to come
 *          (kind 255): gone at its let-go plus the release it was made with (none: at once);
 *  -    6: E2's table, its note started 3 blocks before E, its work waiting till 10 blocks after E, then given up (a
 *          key's note takes its engine, every other engine a key's): a ghost again, gone after the note (E2);
 *  -    7: the same with no command changed: gone after the note (E) while its work waits, never given up;
 *  -    8: RELEASE 1, its note started, its work waiting, let go 5 blocks on, given up 5 more on: within a frame;
 *  -    9: RELEASE 1, its note started, its work waiting, given up held 5 blocks on, let go 5 more on: within a frame;
 *  -   10: a VOL table running out before E, its return begun after that: gone as its note starts (its end come);
 *  - 11-13: 6 given up by a command on its track (RELEASE 3), its track muted (pl_returns, a held ghost of track 1
 *          below it in the list begun after it), VOICES 8 (its return in engine 11, the note never taken in 1) */
static void chip_rebuilt(pl_inst_t chp, pl_inst_t vt, pl_inst_t dx)
{
    static const char *const WHAT[14] = {
        "a CHIP return, RELEASE 1 since, let go as its note starts: as the note",
        "a CHIP return, RELEASE 8 since, let go as its note starts: as the note",
        "a CHIP return, RELEASE 15 since, let go as its note starts: as the note",
        "a CHIP return, a longer VOL table since, past its own end: as the note",
        "a CHIP ghost let go before its return begins: gone at once (a control)",
        "a CHIP ghost let go, its claim's note to come: gone at once (a control)",
        "a CHIP return waiting past its own end, given up by a note: after the note",
        "a CHIP return waiting, no command changed: gone after the note, at its end",
        "a CHIP return waiting, let go, given up: gone within a frame of the note",
        "a CHIP return waiting, given up, let go: gone within a frame of the note",
        "a CHIP return begun after a shorter VOL table's end: gone as it starts",
        "a CHIP return waiting past its end, given up by a command: after the note",
        "a CHIP return waiting past its end, given up by a mute: after the note",
        "a CHIP return waiting past its end, given up by VOICES 8: after the note"};
    pl_chan_t *ch = &PL_CH(0);
    uint32_t e_own = chip_end_of(vt, 0, 0, 0, 1), e2 = 0, es = 0, x, fb = chip_frame_blocks(chp, 0x57, 1, 1);
    uint32_t rel = chip_end_of(chp, 0x57, 1, 1, 0);   /* (RELEASE 1's length from a note's start: m 8, 9's end exactly) */
    uint8_t tab = 0, tabs = 0;
    int m;
    for (x = 1; x < 64u && e_own != CHIP_NEVER; x++) {   /* (VOL tables of vt's running out well after its own, sooner) */
        uint32_t e = chip_end_of(vt, 0x60, (uint8_t)x, 1, 1);
        if (!tab && e != CHIP_NEVER && e > e_own + 40u && e < 4000u) {
            tab = (uint8_t)x;
            e2 = e;
        }
        if (!tabs && e != CHIP_NEVER && e > 10u && e + 40u < e_own) {
            tabs = (uint8_t)x;
            es = e;
        }
    }
    if (!tab || !tabs || !fb) {
        check("a CHIP instrument whose VOL table runs out, others of it running out later, sooner; a frame", 0);
        return;
    }
    printf("%02X:%02X held: its own VOL table runs out in %u blocks, VOL TABLE %u in %u, VOL TABLE %u in %u; a frame of "
           "%02X:%02X with RELEASE 1 %u blocks at most\n", vt.cart, vt.slot, e_own, tab, e2, tabs, es, chp.cart, chp.slot,
           fb);
    for (m = 0; m < 14; m++) {
        int vol = m == 3 || m == 6 || m == 7 || m >= 10, how = m == 11 ? 1 : m == 12 ? 2 : m == 13 ? 3 : 0;
        int gave = m == 6 || m == 8 || m == 9 || m >= 11, work = m < 4, keys = m == 6 || m == 8 || m == 9;
        pl_inst_t in = vol ? vt : chp;
        uint8_t pc = (uint8_t)(vol ? 0x60 : 0x57), pv = (uint8_t)(m == 10 ? tabs : vol ? tab : m == 1 ? 8 : m == 2 ||
                                                                  m == 4 || m == 5 ? 15 : 1);
        uint32_t np = m == 7 ? 0u : 1u, hold = 689, b, c, c0 = how == 3 ? 11u : 0u, age, let_at = PL_NEVER;
        uint32_t give_at = PL_NEVER, work_at, at0;
        pl_eng_t *ref = &pl_e[how == 3 ? 1u : 11u];
        pl_voice_t *g, *v, *other = 0;
        int32_t bud;
        int started = 0, early = 0, back = 0, same = 0, ge = -1, re = -1, good, given = 0, ghost = 0, other_back = 0;
        int ended = 0;
        pool_empty();
        ch->ins = in;
        ch->has_ins = 1;
        ch->np = 0;
        if (how == 2)                    /* (a song's held ghost of track 1 made first: below g in the list) */
            other = song_ghost(dx, 1, 50);
        g = song_ghost(in, 0, 60);       /* (its figures by its own commands) */
        v = note_par(in, 61, (uint32_t)(ref - pl_e), &pc, &pv, np);
        for (c = 1; keys && c < 11u; c++)   /* (every other engine a key's: a key's note takes the return's) */
            note_at(dx, 70u + c, c);
        if (!g || !v || (how == 2 && !other)) {
            check(WHAT[m], 0);
            continue;
        }
        age = g->age;
        if (vol)                         /* (held to 3 blocks before its own VOL table runs out; m 10: past E') */
            hold = m == 10 ? (es + e_own) / 2u : g->t_end - pl_blk - 3u;
        if (vol && m != 10)
            give_at = g->t_end + 10u;
        work_at = m == 3 ? g->t_end + 5u : g->t_on;   /* (m 3: its work waiting past its own end) */
        for (b = 0; b < hold; b++) {
            eng_skip(ref, blk_tick());
            blk_ends();
        }
        ended = !pl_e_sounds(ref);
        ch->np = (uint8_t)np;            /* (the track's command since) */
        ch->pc[0] = pc;
        ch->pv[0] = pv;
        if (m != 5)
            pl_gh_ready(g);
        pl_e[c0].wait = (uint8_t)(g - pl_v);
        pl_e[c0].kind = 255;
        g->gh = GH_BACK;
        if (m != 4 && m != 5) {          /* (its note started, its work to come) */
            bud = 1;
            pl_forward(g, c0, &bud);
            started = pl_e[c0].kind == EN_CHIP && g->gh == GH_BACK;
            pool_check("a CHIP return's note started");
        } else if (m == 4) {             /* (control: let go before its return begins) */
            pl_back_stop(g);
        }
        if (m < 6 && m != 3) {
            pl_v_off(g);
            pl_v_off(v);
        }
        if (m == 8 || m == 9) {
            let_at = pl_blk + (m == 8 ? 5u : 10u);
            give_at = pl_blk + (m == 8 ? 10u : 5u);
        }
        at0 = pl_blk;                    /* (the loop's block b is at0 + b) */
        for (b = 0; b < 8000 && m != 10; b++) {
            blk_ends();
            if (g->use == PV_FREE || g->age != age) {   /* (gone; its voice perhaps the key's note's now) */
                early = !given;
                ge = (int)b;
                break;
            }
            if (pl_blk == let_at) {
                pl_v_off(g);
                pl_v_off(v);
            }
            if (gave && pl_blk == give_at) {
                if (how == 0) {          /* (a key's note takes its engine) */
                    pl_voice_t *k = pl_alloc(-1, EN_DX7);
                    given = k && pl_al_c == 0 && pl_voice_on(k, PV_KEY, 99, dx, 60, 127, 128, 0, 4096, 0, 0, 0, 0);
                } else if (how == 1) {
                    pl_param(0, 0x57, 3);
                    given = 1;
                } else if (how == 2) {
                    pl_ctxs[0].mute = 1; /* (track 0 muted) */
                    pl_ctxs[0].duck = 0;
                    pl_returns();
                    other_back = other->place != PL_NO_ENG || other->gh == GH_BACK;
                    pl_ctxs[0].mute = 0;
                    given = 1;
                } else {
                    pl_nv_apply(8);
                    given = 1;
                }
                ghost = g->use == PV_SONG && g->age == age && g->gh == GH_GHOST && pl_e[c0].wait != g - pl_v;
                pool_check("a CHIP return given up");
            }
            if (work && (int32_t)(pl_blk - work_at) >= 0) {
                bud = 1;
                pl_forward(g, c0, &bud);
                if (g->place != PL_NO_ENG) {
                    back = 1;
                    same = env_same(EN_CHIP, &pl_e[g->place], ref);
                    break;
                }
            }
            eng_skip(ref, blk_tick());
            if (re < 0 && !pl_e_sounds(ref))
                re = (int)b;
        }
        for (b = 0; back && b < 6000 && (ge < 0 || re < 0); b++) {   /* (both on, block by block, to their ends) */
            int t = blk_tick();
            eng_skip(&pl_e[g->place], t);
            eng_skip(ref, t);
            if (ge < 0 && !pl_e_sounds(&pl_e[g->place]))
                ge = (int)b;
            if (re < 0 && !pl_e_sounds(ref))
                re = (int)b;
        }
        if (m < 4) {
            good = started && !early && back && same && ge >= 0 && ge == re;
            printf("%s: %s; its engine and the note never taken's end in blocks %d and %d after that\n", WHAT[m],
                   back ? same ? "back as the note never taken" : "back, not as the note" : early ? "gone" : "not back",
                   ge, re);
        } else if (m < 6 || m == 10) {
            good = m == 10 ? !started && g->use == PV_FREE && pl_e[c0].wait == PL_NO_VOICE && ended : early && ge == 0;
            printf("%s: %s%s\n", WHAT[m], (m == 10 ? g->use == PV_FREE : ge == 0) ? "gone at once" : "not gone",
                   m != 10 ? "" : ended ? " (the note never taken ended by then)" : " (the note never taken still on)");
        } else {                         /* (gone after the note: exact, or within a frame of it, never sooner; m 8, 9
                                          * exactly at its let-go plus the rebuilt release's length from a note's
                                          * start) */
            int d = ge - (re + 1), ex = m == 8 || m == 9 ? (int)(let_at - at0 + rel) : re + 1;
            good = started && re >= 0 && ge >= 0 && (m == 7 ? early && !given : given && ghost && !early) &&
                   (m == 8 || m == 9 ? d >= 0 && d <= (int)fb : d == 0) && ge == ex && (how != 2 || other_back);
            printf("%s: %s, gone in block %d, the note never taken's voice freed in block %d (%+d; a frame %u; by its "
                   "figures %d)%s\n", WHAT[m], m == 7 ? (given ? "given up" : "never given up") : ghost ? "a ghost again"
                   : "not a ghost", ge, re + 1, d, fb, ex, how == 2 ? other_back ? "; the ghost below begun" :
                   "; the ghost below not begun" : "");
        }
        check(WHAT[m], good);
        pl_nv_apply(12);
    }
    ch->has_ins = 0;
    ch->np = 0;
    pool_empty();
}
/* a key's held note whose engine has ended by itself (a CHIP's VOL table run out; a one-shot sample past its END), its
 * voice not yet freed (the voices pass frees it at its engine's turn), taken: VOICES 8 over it in engine 11, as core 2's
 * fallback's smaller count takes inside the pass, the list full of songs' held ghosts (a key's held ranks above them).
 * It goes, not a ghost: no ghost dropped for it, none listed due */
static void take_ended(pl_inst_t chp, pl_inst_t shot)
{
    int m;
    for (m = 0; m < 2; m++) {
        pl_voice_t *v, *g[PL_GHOSTS];
        uint32_t i, b, age, kept = 0, due = 0;
        int ended;
        pool_empty();
        for (i = 0; i < PL_GHOSTS; i++)
            g[i] = gh_put(PV_SONG, 1, 1, 0);
        v = note_at(m ? shot : chp, 70, 11);
        for (b = 0; v && b < 20000 && pl_e_sounds(&pl_e[11]); b++)
            eng_skip(&pl_e[11], blk_tick());
        ended = v && v->place == 11 && !pl_e_sounds(&pl_e[11]);
        age = v ? v->age : 0u;
        pl_nv_apply(8);
        for (i = 0; i < PL_GHOSTS; i++)
            kept += g[i]->use != PV_FREE && g[i]->gh == GH_GHOST;
        for (i = 0; i < pl_gn; i++)
            due += (uint32_t)pl_t_due(pl_v[pl_gl[i]].t_end);
        printf("a key's held %s note ended in its engine (in %u blocks), taken with the list full: %s; the list's "
               "ghosts kept %u of %u, %u listed due\n", m ? "one-shot sample" : "CHIP", b, v && (v->use == PV_FREE ||
               v->age != age) ? "gone" : "a ghost", kept, PL_GHOSTS, due);
        check(m ? "a held one-shot sample ended in its engine, taken: gone, no ghost dropped"
                : "a held CHIP note ended in its engine, taken: gone, no ghost dropped",
              ended && (v->use == PV_FREE || v->age != age) && kept == PL_GHOSTS && pl_gn == PL_GHOSTS && !due);
        pl_nv_apply(12);
    }
    pool_empty();
}
/* a sampler ghost slid (its pitch moved every block, as a 3xx or 1xx slides it: -6 to +6 semitones and again): its
 * place, walked at each pitch it had, the note never taken's each block (beside it, stepped by samp_skip with the same
 * pitches); its end from that same count: gone in the block that frees the note never taken's voice (the one after it
 * ended) */
static void samp_slid(pl_inst_t smp)
{
    pl_voice_t *g, *v;
    pl_eng_t *ref = &pl_e[11];
    int b, same = 1, gone = -1, end = -1, by_place = 0;
    pool_empty();
    v = note_at(smp, 61, 11);
    g = note_at(smp, 60, PL_NO_ENG);
    for (b = 0; v && g && b < 20000; b++) {
        int32_t p = (int32_t)(b % 192) * 4 - 384;
        pl_v_pitch(g, p);
        pl_v_pitch(v, p);
        blk_ends();
        if (g->use == PV_FREE) {
            gone = b;
            break;
        }
        if (end < 0) {
            samp_place_t a;
            samp_place_of(&ref->u.sm, &a);
            same &= g->t_pl == pl_blk && !memcmp(&a, &g->x.sp, sizeof a);
        }
        eng_skip(ref, blk_tick());
        if (end < 0 && !pl_e_sounds(ref)) {
            end = b;
            by_place = ref->u.sm.pos < 0 || ref->u.sm.pos >= ref->u.sm.s.end - 1;
        }
    }
    printf("a sampler ghost of %02X:%02X slid every block: its place the note never taken's %d; the note ended in block "
           "%d (by its place %d), the ghost gone in block %d\n", smp.cart, smp.slot, same, end, by_place, gone);
    check("a sampler ghost slid: its place and its end from one count, as the note never taken's", same && by_place &&
          end > 0 && gone == end + 1);
    pool_empty();
}
/* a sampler ghost whose end falls in the block it would come back in goes at its return (pl_gh_ready): a one-shot's held
 * ghost, its end E: ready 2 blocks before it, gone 1 block before */
static void samp_edge(pl_inst_t smp)
{
    pl_voice_t *g;
    uint32_t e;
    int two = 0, one = 1;
    pool_empty();
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    g = note_at(smp, 60, PL_NO_ENG);
    e = g ? g->t_end : PL_NEVER;
    if (g && e != PL_NEVER) {
        while ((int32_t)(e - pl_blk) > 2)
            blk_tick();
        two = pl_gh_ready(g) && g->t_end == e;
        blk_tick();
        one = pl_gh_ready(g);
    }
    printf("a one-shot's ghost of %02X:%02X: ready 2 blocks before its end %d, gone at 1 block before %d\n", smp.cart,
           smp.slot, two, !one && g->use == PV_FREE);
    check("a sampler ghost ending in the block it would come back in goes at its return", two && !one &&
          g->use == PV_FREE);
#ifdef PTFM_DEBUG
    check("the figures: gone, a sampler ended by the commands it comes back with", pl_gf.gone[GO_SAMP_END][0] == 1);
#endif
    pool_empty();
}
/* an instrument deleted by its return (a cartridge deleted from the store): a key's held ghost of a sampler of the disk's
 * cartridge, the cartridge gone (its record emptied, the player's cartridges forgotten as the store's watch makes it):
 * dropped at its return, its engine not started; the cartridge back */
static void inst_gone(pl_inst_t sm)
{
    const uint8_t *p;
    uint32_t len;
    uint8_t ver;
    pl_voice_t *g;
    int ok;
    pool_empty();
#ifdef PTFM_DEBUG
    gf_zero();
#endif
    p = store_get(REC_SAMPLER, sm.cart, 0, &len, &ver);
    g = note_at(sm, 60, PL_NO_ENG);
    blk_tick();
    disk_put(REC_SAMPLER, sm.cart, 0, p, 0);
    pl_carts_forget();
    blk_ends();
    pl_returns();
    ok = p && g && g->use == PV_FREE && pl_e[0].kind == 255 && pl_e[0].wait == PL_NO_VOICE;
    disk_put(REC_SAMPLER, sm.cart, 0, p, len);
    pl_carts_forget();
    printf("a ghost of %02X:%02X, its cartridge deleted: gone at its return, no engine started %d\n", sm.cart, sm.slot,
           ok);
    check("an instrument deleted: its ghost goes at its return", ok);
#ifdef PTFM_DEBUG
    check("the figures: gone, its instrument gone", pl_gf.gone[GO_INST][0] == 1);
#endif
    pool_empty();
}
/* a song's stop: MIDNIGHT RUN at VOICES 8 with 8 keys held (its notes ghosts), stopped: in the next block every note of
 * it let go, each of its held ghosts (taken down before the stop) a ghost let go at the stop's block (the block the
 * stop lands in, the next half's first); the keys let go, they come back in their release or go; 10.5 s on, none left */
static void song_stopped(pl_inst_t pad)
{
    int k, held = 0, let = 1, all = 1, at_stop = 0, when = 1;
    uint32_t c, at, n = 0, idx[PL_GHOSTS], age[PL_GHOSTS];
    quiet();
    player_voice_count(8);
    player_play_song(0xA2, 0, 0);
    run(1.0);
    for (k = 0; k < 8; k++)
        player_key_on((uint8_t)(48 + 2 * k), pad, 100);
    run(1.0);
    for (c = 0; c < pl_gn; c++)
        if (pl_v[pl_gl[c]].use == PV_SONG && pl_v[pl_gl[c]].held) {
            idx[n] = pl_gl[c];
            age[n++] = pl_v[pl_gl[c]].age;
        }
    held = (int)n;
    player_stop();
    at = pl_blk;                         /* (the stop's block: requests land at the next half's start) */
    blocks(1);
    for (c = 0; c < PL_NV; c++)
        if (pl_v[c].use == PV_SONG)
            let &= !pl_v[c].held;
    for (c = 0; c < n; c++) {            /* (each held ghost, if not gone in that block, let go then) */
        const pl_voice_t *g = &pl_v[idx[c]];
        if (g->use == PV_SONG && g->age == age[c]) {
            at_stop++;
            when &= !g->held && g->t_off == at;
        }
    }
    player_keys_off();
    run(10.5);
    for (c = 0; c < PL_NV; c++)
        all &= pl_v[c].use != PV_SONG;
    printf("MIDNIGHT RUN under 8 keys, stopped: %d held ghosts of it (%d still there a block on, each let go at the "
           "stop's block %d); a block on every note of it let go %d; 10.5 s on, none left %d\n", held, at_stop, when, let,
           all);
    check("a song's stop: its held ghosts let go at the stop's block, gone or back in their release later", held > 0 &&
          at_stop > 0 && when && let && all);
    quiet();
}
/* a ghost let go survives its track going A -> B -> A before its return (the instrument looked at then): a song's DX7
 * ghost of track 0 held 1 s, let go, the track on dx2, then on dx again: back; the same with the track left on dx2:
 * gone */
static void track_aba(pl_inst_t dx, pl_inst_t dx2)
{
    pl_chan_t *ch = &PL_CH(0);
    pl_voice_t *g;
    int back, gone, b;
    pool_empty();
    ch->ins = dx;
    ch->has_ins = 1;
    ch->np = 0;
    g = song_ghost(dx, 0, 60);
    for (b = 0; b < 689; b++)
        blk_tick();
    pl_v_off(g);
    ch->ins = dx2;
    blk_tick();
    ch->ins = dx;
    blk_ends();
    pl_returns();
    back = g->use == PV_SONG && g->place != PL_NO_ENG;
    pool_empty();
    g = song_ghost(dx, 0, 60);
    for (b = 0; b < 689; b++)
        blk_tick();
    pl_v_off(g);
    ch->ins = dx2;
    blk_tick();
    blk_ends();
    pl_returns();
    gone = g->use == PV_FREE;
    printf("a song's ghost let go, its track A -> B -> A by its return: back %d; A -> B: gone %d\n", back, gone);
    check("a ghost let go survives its track going A -> B -> A; on B at its return it goes", back && gone);
    ch->has_ins = 0;
    pool_empty();
}
/* a muted track's sampler note that cannot sound (its sample deleted), the list full: pl_born makes room (a ghost let go
 * goes) before the note finds it cannot start; the note not played, that one ghost lost for it, the pool sound */
static void born_cannot(pl_inst_t sm)
{
    const uint8_t *slot, *p = 0;
    uint32_t len = 0, k;
    uint8_t ver;
    rec_sinst_t si;
    pl_voice_t *v, *let = 0;
    int played = -1;
    pool_empty();
    if (pl_engine(sm, &slot) == EN_SAMP) {
        rec_sampler_inst(slot, &si);
        p = store_get(REC_SAMPLE, si.sample, 0, &len, &ver);
    }
    if (p) {
        disk_put(REC_SAMPLE, si.sample, 0, p, 0);
        pl_ctxs[0].mute = 1;
        pl_ctxs[0].duck = 0;
        for (k = 0; k < 24u; k++) {
            pl_voice_t *g = gh_put(PV_SONG, 1, k != 5u, 20);
            if (k == 5u)
                let = g;
        }
        v = pl_alloc(0, EN_SAMP);
        played = v && pl_voice_on(v, PV_SONG, 0, sm, 60, 127, 128, 0, 4096, 0, 0, 0, 0);
        disk_put(REC_SAMPLE, si.sample, 0, p, len);
        pool_check("voices_test, a note that cannot sound");
    }
    printf("a muted track's note of %02X:%02X, its sample deleted, the list full: played %d; the ghost let go dropped for "
           "it %d, %u ghosts left\n", sm.cart, sm.slot, played, let && let->use == PV_FREE, (unsigned)pl_gn);
    check("a note that cannot sound, the list full: not played, one ghost (let go) dropped for it, the pool sound",
          p && !played && let && let->use == PV_FREE && pl_gn == 23);
    pl_ctxs[0].mute = 0;
    pool_empty();
}

/* core 2's fallbacks on pools set by hand (pl_v2_lost; specs/second-core.md): VOICES 16, engines 0-9 the keys' held
 * notes, the voice job's places 11-16 (engines 10-15) a TB-303's held note, a sampler's, a CHIP's, a DX7's tail (let go,
 * heard), a Juno-106's and a DX7's held, the keys'. A job begun, lost: the TB-303's goes, every other a ghost (the tail
 * let go), made in their engines' order after a key's held ghost already listed, those engines fresh, the count 12; four
 * engines freed then (with 11 and 12 six free): the job's held ones come back first, the one made last first, each into
 * the first engine free; in the next block the key's ghost made before the fallback, the tail's return begun behind it
 * (in pieces, the budget spent on the key's), the tail back let go. A job never
 * begun: 11 and 12 sound on, the held notes in 13-16 ghosts in order, the tail rings on where it is. Begun with the list
 * full (24 song ghosts held): each held one makes room (a song's held goes, a ghost longest first: the four made
 * first), the tail, below them all, goes */
static void c2_lost(pl_inst_t dx, pl_inst_t ju, pl_inst_t sm, pl_inst_t chp)
{
    static const char *const WHAT[3] = {"begun", "never begun", "begun, the list full"};
    const pl_inst_t TB = {0xFA, 0x00};
    pl_inst_t in[6];
    int m, k, ok[3] = {0, 0, 0};
    uint32_t c;
    in[0] = TB;
    in[1] = sm;
    in[2] = chp;
    in[3] = dx;
    in[4] = ju;
    in[5] = dx;
    for (m = 0; m < 3; m++) {
        pl_voice_t *v[6], *s[24], *e = 0;
        int good = 1;
        pool_empty();
        pl_nv_apply(16);
        for (c = 0; c < 10u; c++)
            note_at(dx, 40u + c, c);
        for (k = 0; k < 6; k++) {
            v[k] = note_at(in[k], 60u + (uint32_t)k, 10u + (uint32_t)k);
            good &= v[k] != 0;
        }
        if (!good) {
            check("core 2's fallbacks: notes of a TB-303, sampler, CHIP, DX7 and Juno-106 started", 0);
            break;
        }
        for (k = 0; k < 689; k++)        /* (1 s on: the tail heard when let go) */
            blk_tick();
        pl_v_off(v[3]);
        v[3]->g_last = 4096;
        v[3]->peak = 500;
        for (k = 0; m == 2 && k < 24; k++)
            s[k] = gh_put(PV_SONG, 1, 1, 0);
        if (m == 0)                      /* (a key's held ghost made before the fallback) */
            e = gh_put(PV_KEY, 90, 1, 0);
        pl_age_seen = pl_age;
        pl_v2.n = 6;
        for (k = 0; k < 6; k++)
            pl_v2.place[k] = (uint8_t)(10 + k);
        pl_v2.started = (uint8_t)(m != 1);
        pl_v2.done = 0;
        pl_v2_lost();
        pool_check("voices_test, a voice job lost");
        if (m == 0) {
            int order, next, b;
            good &= v[0]->use == PV_FREE && pl_nv == 12 && pl_gn == 6 && pl_gl[0] == (uint8_t)(e - pl_v) &&
                    pl_gl[1] == (uint8_t)(v[1] - pl_v) && pl_gl[2] == (uint8_t)(v[2] - pl_v) &&
                    pl_gl[3] == (uint8_t)(v[3] - pl_v) && pl_gl[4] == (uint8_t)(v[4] - pl_v) &&
                    pl_gl[5] == (uint8_t)(v[5] - pl_v) && !v[3]->held && v[3]->t_end != PL_NEVER;
            for (c = 10; c < 16u; c++)
                good &= pl_e[c].voice == PL_NO_VOICE && pl_e[c].kind == 255;
            for (c = 0; c < 4u; c++)     /* (four engines freed: with 11 and 12, six free) */
                pl_v_cut(&pl_v[pl_e[c].voice]);
            blk_tick();
            blk_ends();
            pl_returns();
            order = v[5]->place == 0 && v[4]->place == 1 && v[2]->place == 2 && v[1]->place == 3 &&
                    e->gh == GH_GHOST && v[3]->gh == GH_GHOST;   /* (4 a block: the job's made last first) */
            pl_budget = PL_BUDGET;       /* (then the FM-1's budget: the tail's work in pieces) */
            blk_tick();
            blk_ends();
            pl_returns();
            next = (e->place == 10 || e->gh == GH_BACK) && e->held && v[3]->gh & 1u;   /* (the key's ghost begun
                                          * first, into 10; the tail still a ghost or begun) */
            for (b = 0; b < 40 && v[3]->place == PL_NO_ENG; b++) {   /* (its work in pieces after the key's) */
                blk_tick();
                blk_ends();
                pl_returns();
            }
            next &= e->place == 10 && v[3]->place == 11 && !v[3]->held && v[3]->use == PV_KEY;
            good &= order && next;
        } else if (m == 1) {
            good &= v[0]->place == 10 && v[1]->place == 11 && v[3]->place == 13 && pl_e[13].voice == (uint8_t)(v[3] - pl_v)
                    && pl_gn == 3 && pl_gl[0] == (uint8_t)(v[2] - pl_v) && pl_gl[1] == (uint8_t)(v[4] - pl_v) &&
                    pl_gl[2] == (uint8_t)(v[5] - pl_v) && pl_nv == 12;
        } else {
            good &= pl_gn == 24 && v[0]->use == PV_FREE && v[3]->use == PV_FREE && (v[1]->gh & 1u) && (v[2]->gh & 1u) &&
                    (v[4]->gh & 1u) && (v[5]->gh & 1u);
            for (k = 0; k < 24; k++)     /* (the four made first gone, a ghost longest first) */
                good &= (s[k]->use == PV_FREE) == (k < 4);
        }
        ok[m] = good;
        printf("core 2's voice job lost, %s: as section 3 says %d (%u ghosts, the count %u)\n", WHAT[m], good,
               (unsigned)pl_gn, (unsigned)pl_nv);
    }
    check("core 2's fallbacks: a job begun (a TB-303's gone, the rest ghosts, the tail let go, back in order)", ok[0]);
    check("... a job never begun (11-12 on, 13-16's held ghosts, a tail rings on there)", ok[1]);
    check("... a job begun with the list full (room by rank, the tail not kept)", ok[2]);
    pool_empty();
}

/* the second replay's tracks moving (DJ's second deck's memory given or taken back: pl_d2_set): its ghosts go with its
 * notes, held or let go; the first replay's stay */
static void d2_moving(void)
{
    void *mem = malloc(player_dj_mem_size());
    pl_voice_t *a, *b, *c;
    int ok;
    pool_empty();
    a = gh_put(PV_SONG, 17, 1, 0);
    b = gh_put(PV_SONG, 2, 1, 0);
    c = gh_put(PV_SONG, 18, 0, 10);
    pl_d2_set(mem);
    ok = a->use == PV_FREE && c->use == PV_FREE && b->use == PV_SONG && pl_gn == 1;
    pl_d2_set(0);
    free(mem);
    check("the second replay's tracks moving: its ghosts go, the first's stay", ok);
    pool_empty();
}
/* DJ (a pool set by hand, VOICES 8, the right deck faded out): the left deck's held notes in engines 0-7 but 5, where a
 * left deck's ghost's return is under way; a note of the right deck takes that engine (a return under way is taken
 * before any deck's rule: silent, as a free engine), the ghost a ghost still */
static void dj_takes_return(pl_inst_t dx)
{
    void *mem = malloc(player_dj_mem_size());
    pl_voice_t *g, *v;
    uint32_t c;
    int32_t bud = 1;
    int ok;
    pool_empty();
    pl_nv_apply(8);
    pl_d2_set(mem);
    pl_d2->dj.on = 1;
    pl_dj.xf8 = 0;
    pl_dj.g_last[0] = 4096;
    pl_dj.g_last[1] = 0;
    for (c = 0; c < 8u; c++)
        if (c != 5u) {
            pl_voice_t *t = note_at(dx, 70u + c, c);
            t->use = PV_SONG;
            t->who = (uint8_t)c;
        }
    g = gh_put(PV_SONG, 3, 1, 0);
    pl_e[5].wait = (uint8_t)(g - pl_v);
    pl_e[5].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 5, &bud);
    pl_alloc_k = 1;
    v = pl_alloc(16 + 3, EN_DX7);
    ok = v && pl_al_c == 5 && g->gh == GH_GHOST && pl_e[5].wait == PL_NO_VOICE;
    pl_al_c = PL_NO_ENG;
    pl_alloc_k = -1;
    printf("DJ, the right deck faded out: its note took the left deck's return under way (engine %d), the ghost a ghost "
           "%d\n", v ? 5 : -1, g->gh == GH_GHOST);
    check("DJ: a faded-out deck's note takes the other deck's return under way (silent), the ghost a ghost", ok);
    pool_empty();
    pl_d2->dj.on = 0;
    pl_d2_set(0);
    free(mem);
}
/* a FUSION layer's E9x restarting it while its ghost's return is under way: the return given up (its engine free), the
 * restart's note in that engine (A5:03 row 60's layer 4, E93: pl_sf's runner set by hand, every other engine a key's) */
static void fusion_restart(pl_inst_t dx)
{
    const pl_inst_t f = {0xA5, 0x03};
    pl_fn_t *fn = &pl_sf[0];
    pl_voice_t *g, *n;
    uint32_t c, rows;
    int32_t bud = 1;
    int ok;
    pool_empty();
    rows = pl_fusion_row(f, 60, pl_fcells);
    if (rows < 4u || pl_engine((pl_inst_t){pl_fcells[3].cart, pl_fcells[3].slot}, 0) != EN_CHIP) {
        check("FUSION A5:03 row 60: a CHIP layer with E9x (the runner's case)", 0);
        return;
    }
    for (c = 0; c < 12u; c++)
        if (c != 3u)
            note_at(dx, 40u + c, c);
    PL_CH(0).ins = f;
    PL_CH(0).has_ins = 1;
    PL_CH(0).fus_note = 60;
    PL_CH(0).vol = 64;
    g = song_ghost((pl_inst_t){pl_fcells[3].cart, pl_fcells[3].slot}, 0, pl_fcells[3].note);
    g->acc = 4;
    pl_e[3].wait = (uint8_t)(g - pl_v);
    pl_e[3].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 3, &bud);
    memset(fn, 0, sizeof *fn);
    fn->on = 1;
    fn->who = 0;
    fn->use = PV_SONG;
    fn->gen = g->gen;
    fn->lane = 0xFF;
    fn->row = 60;
    fn->in = f;
    fn->n = 4;
    for (c = 0; c < 4u; c++) {
        fn->l[c].v = -1;
        fn->l[c].done = c != 3u;
        fn->l[c].cut = PL_KL_NONE;
    }
    fn->l[3].v = (int8_t)(g - pl_v);
    fn->l[3].age = (uint16_t)g->age;
    fn->l[3].rt = 3;
    fn->tick = 3;
    {
        uint32_t age = g->age;
        int gone;
        pl_kf_tick(fn);
        gone = g->use == PV_FREE || g->age != age;   /* (its voice free, or another note's now: the restart's) */
        n = fn->l[3].v >= 0 ? &pl_v[fn->l[3].v] : 0;
        ok = gone && n && n->age != age && n->place == 3 && n->gh == GH_NO && pl_e[3].wait == PL_NO_VOICE;
        printf("a FUSION layer's E9x restart while its ghost's return is under way: the ghost gone %d, the restart in its "
               "engine %d\n", gone, n && n->place == 3);
    }
    check("FUSION: E9x restarting a layer whose return is under way: given up, the new start in its engine", ok);
    memset(fn, 0, sizeof *fn);
    PL_CH(0).has_ins = 0;
    pool_empty();
}
/* RECORD LIVE's tap and a return under way taken by a note (section 8): a loop's held ghost (track 16), its return under
 * way, its engine taken by a key's note, then back once an engine is free: nothing told (it lost no engine it was heard
 * in, and a return is never written) */
static void tap_return(pl_inst_t dx)
{
    static pl_tap_t ring[16];
    pl_tap_t e;
    pl_voice_t *g, *v;
    uint32_t c;
    int32_t bud = 1;
    int b, told = 0, back = 0;
    pool_empty();
    pl_nv_apply(8);
    player_tap(ring, 16);
    for (c = 0; c < 8u; c++)
        if (c != 6u)
            note_at(dx, 70u + c, c);
    g = gh_put(PV_SONG, 16, 1, 0);
    pl_e[6].wait = (uint8_t)(g - pl_v);
    pl_e[6].kind = 255;
    g->gh = GH_BACK;
    pl_forward(g, 6, &bud);
    v = pl_alloc(-1, EN_DX7);
    if (v && pl_voice_on(v, PV_KEY, 90, dx, 60, 127, 128, 0, 4096, 0, 0, 0, 0))
        pl_v_cut(v);                     /* (an engine free again) */
    for (b = 0; b < 4 && !back; b++) {
        blk_tick();
        blk_ends();
        pl_returns();
        back = g->place != PL_NO_ENG;
    }
    while (player_tap_peek(&e)) {
        player_tap_next();
        told++;
    }
    player_tap(0, 0);
    printf("RECORD LIVE's tap: a loop's return under way taken by a key's note, then back: back %d, %d events told\n",
           back, told);
    check("RECORD LIVE's tap: a return under way taken, then back: nothing told", back && !told);
    pool_empty();
}

/* FILTER's envelope held by a ghost and let go by a one-shot's end (section 2): a song's one-shot sampler note on track 3
 * born a ghost: track 3 held in FILTER's sources while it lasts, not once it has ended by itself */
static void filter_held(pl_inst_t shot)
{
    pl_voice_t *g;
    int held = 0, after = 1, b;
    pool_empty();
    g = song_ghost(shot, 3, 60);
    if (g && g->t_end != PL_NEVER) {
        pl_env_held(0);
        held = (fx_clk.held >> 4 & 1u) != 0;   /* (track 3's bit: 2 << 3) */
        for (b = 0; b < 20000 && g->use != PV_FREE; b++) {
            blk_tick();
            blk_ends();
        }
        pl_env_held(0);
        after = (fx_clk.held >> 4 & 1u) != 0;
    }
    printf("FILTER's envelope: a one-shot's ghost on track 3 held there %d, its own end let it go %d\n", held, !after);
    check("FILTER's envelope held by a ghost, let go at a one-shot ghost's own end", held && !after);
    pool_empty();
}
/* LOOP END past END under a sounding note (specs/files/commands.md: the loop's end is the lesser of LOOP END and END at
 * every moment): a FORWARD and a PING-PONG note of `lp` (a sampler that loops), LOOP START turned to its sample's 16th
 * and END to its half by its list (in that order: the loop keeps room throughout), then LOOP END past END by a command:
 * its place stays under END; END turned up again: the loop's end follows it, the place past the old END */
static void loop_end_past(pl_inst_t lp)
{
    static const uint8_t PC[3] = {0x42, 0x43, 0x41};   /* (LOOP, LOOP START, END) */
    int mode, ok = 1;
    for (mode = 1; mode <= 2; mode++) {
        const uint8_t PV[3] = {(uint8_t)mode, 16, 128};
        int32_t buf[PL_BLOCK], half = 0, top = 0, top2 = 0;
        uint32_t b, n = 0;
        samp_voice_t *e = &pl_e[0].u.sm;
        pl_voice_t *v;
        pool_empty();
        v = note_par(lp, 60, 0, PC, PV, 3);
        if (v) {
            half = e->s.end;
            n = (uint32_t)((((uint64_t)e->s.length << 16) / (e->step ? e->step : 1u)) / PL_BLOCK) * 2u + 64u;
            pl_param_live(v, 0x44, 255);   /* LOOP END: the sample's end, past END */
            for (b = 0; b < n && e->on; b++) {
                memset(buf, 0, sizeof buf);
                samp_render(e, buf);
                top = e->pos > top ? e->pos : top;
            }
            pl_param_live(v, 0x41, 255);   /* END: the sample's end again */
            for (b = 0; b < n && e->on; b++) {
                memset(buf, 0, sizeof buf);
                samp_render(e, buf);
                top2 = e->pos > top2 ? e->pos : top2;
            }
        }
        printf("a sounding %s note of %02X:%02X, END at %d, LOOP END past it: its place at most %d; END back up: at most %d, "
               "looping %d\n", mode == 1 ? "FORWARD" : "PING-PONG", lp.cart, lp.slot, (int)half, (int)top, (int)top2,
               v ? e->looping : -1);
        ok &= v && e->on && e->looping && top < half && top2 >= half;
    }
    check("LOOP END past END under a sounding note: the loop ends at END, and follows END back up (FORWARD, PING-PONG)", ok);
    pool_empty();
}
static void bits_put(uint8_t *p, uint32_t pos, int n, uint32_t v)   /* rec_bits' field written (its lowest bit first) */
{
    int i;
    for (i = 0; i < n; i++, pos++)
        p[pos >> 3] = (uint8_t)((p[pos >> 3] & ~(1u << (pos & 7u))) | ((v >> i & 1u) << (pos & 7u)));
}
/* a sampler ghost whose instrument's loop is edited while it waits (settings.md, its parameter commands at its return:
 * its instrument as it comes back): `lp` (a sampler that loops) held as a key's ghost, its cartridge's slot turned (a)
 * LOOP OFF, (b) LOOP END onto LOOP START (no room), then an engine free: back out of its loop, on to END, ended there */
static void ghost_unlooped(pl_inst_t lp)
{
    static uint8_t was[REC_CART_HEAD + 40u * 256u], ed[sizeof was];
    const uint8_t *p, *sl;
    uint32_t len = 0, off = 0;
    uint8_t ver;
    rec_cart_t rc;
    int kase, ok = 1;
    p = store_get(REC_SAMPLER, lp.cart, 0, &len, &ver);
    if (!p || len > sizeof was || rec_cart(lp.cart, &rc) || !(sl = rec_slot(&rc, lp.slot)) || !rc.base ||
        sl < rc.base || (uint32_t)(sl - rc.base) + 40u > len) {
        check("a sampler ghost whose instrument's loop is edited while it waits: its cartridge's slot found", 0);
        return;
    }
    off = (uint32_t)(sl - rc.base);
    memcpy(was, p, len);
    for (kase = 0; kase < 2; kase++) {
        int32_t buf[PL_BLOCK];
        pl_voice_t *g;
        samp_voice_t *e = 0;
        int back = 0, looping = -1, ended = 0, b;
        memcpy(ed, was, len);
        if (kase == 0)
            bits_put(ed + off, 88, 2, 0);   /* LOOP: OFF */
        else
            bits_put(ed + off, 68, 20, rec_bits(ed + off, 48, 20));   /* LOOP END: at LOOP START */
        pool_empty();
        g = note_at(lp, 60, PL_NO_ENG);
        for (b = 0; g && b < 100; b++) {
            blk_tick();
            blk_ends();
        }
        disk_put(REC_SAMPLER, lp.cart, 0, ed, len);
        pl_carts_forget();
        blk_tick();
        blk_ends();
        pl_returns();
        back = g && g->use == PV_KEY && g->place != PL_NO_ENG && g->held;
        if (back) {
            e = &pl_e[g->place].u.sm;
            looping = e->looping;
            for (b = 0; b < 400000 && e->on; b++) {
                memset(buf, 0, sizeof buf);
                samp_render(e, buf);
            }
            ended = !e->on;
        }
        disk_put(REC_SAMPLER, lp.cart, 0, was, len);
        pl_carts_forget();
        printf("a held sampler ghost of %02X:%02X, its instrument edited to %s while it waits: back %d, looping %d, on to END "
               "and ended there %d\n", lp.cart, lp.slot, kase ? "a loop with no room" : "LOOP OFF", back, looping, ended);
        ok &= back && looping == 0 && ended;
    }
    check("a sampler ghost whose instrument's loop is edited while it waits (LOOP OFF, no room): back out of its loop, "
          "ending at END", ok);
    pool_empty();
}
/* the same, with a walk of its place between the edit and its return (settings.md, a sampler ghost keeps its place: an
 * edit counts from the ghost's next walk or its return, never at the edit itself): `lp` held as a key's ghost; (a)
 * LOOP OFF, (b) LOOP END on LOOP START, then its pitch turned (the voices pass walks it): out of its loop at that walk,
 * its end at END, gone by it with no engine free; (c) LOOP OFF, its pitch turned, the instrument put back, then an
 * engine free: back out of its loop (left at the walk, for good); (d) LOOP OFF and put back with no walk between, then
 * an engine free: back in its loop */
static void ghost_edit_walk(pl_inst_t lp)
{
    static uint8_t was[REC_CART_HEAD + 40u * 256u], ed[sizeof was];
    const uint8_t *p, *sl;
    uint32_t len = 0, off = 0;
    uint8_t ver;
    rec_cart_t rc;
    int kase, ok = 1;
    p = store_get(REC_SAMPLER, lp.cart, 0, &len, &ver);
    if (!p || len > sizeof was || rec_cart(lp.cart, &rc) || !(sl = rec_slot(&rc, lp.slot)) || !rc.base ||
        sl < rc.base || (uint32_t)(sl - rc.base) + 40u > len) {
        check("a sampler ghost's instrument edited, then a walk: its cartridge's slot found", 0);
        return;
    }
    off = (uint32_t)(sl - rc.base);
    memcpy(was, p, len);
    for (kase = 0; kase < 4; kase++) {
        pl_voice_t *g;
        int walked = -1, gone = 0, back = 0, looping = -1, b;
        memcpy(ed, was, len);
        if (kase == 1)
            bits_put(ed + off, 68, 20, rec_bits(ed + off, 48, 20));   /* LOOP END: at LOOP START */
        else
            bits_put(ed + off, 88, 2, 0);   /* LOOP: OFF */
        pool_empty();
        g = note_at(lp, 60, PL_NO_ENG);
        for (b = 0; g && b < 100; b++) {
            blk_tick();
            blk_ends();
        }
        disk_put(REC_SAMPLER, lp.cart, 0, ed, len);
        pl_carts_forget();
        blk_tick();
        if (g && kase < 3) {
            pl_gh_pitch(g, 64);          /* (a semitone up: its place walked in this block's voices pass) */
            blk_ends();
            walked = g->use == PV_KEY ? (g->x.sp.hi & SAMP_PL_LOOP) != 0 : -1;
        }
        if (kase >= 2) {
            disk_put(REC_SAMPLER, lp.cart, 0, was, len);   /* (put back: a loop to play again) */
            pl_carts_forget();
        }
        if (kase < 2) {                  /* (no engine taken back: on to its end) */
            for (b = 0; g && b < 400000 && g->use == PV_KEY; b++) {
                blk_tick();
                blk_ends();
            }
            gone = g && g->use == PV_FREE;
            ok &= walked == 0 && gone;
        } else {
            blk_tick();
            blk_ends();
            pl_returns();
            back = g && g->use == PV_KEY && g->place != PL_NO_ENG;
            looping = back ? pl_e[g->place].u.sm.looping : -1;
            ok &= back && looping == (kase == 2 ? 0 : 1) && (kase == 3 || walked == 0);
        }
        disk_put(REC_SAMPLER, lp.cart, 0, was, len);
        pl_carts_forget();
        printf("a held sampler ghost of %02X:%02X, %s: in its loop after the walk %d, gone by its end %d, back %d, looping "
               "%d\n", lp.cart, lp.slot, kase == 0 ? "LOOP OFF, its pitch turned" : kase == 1 ? "no room, its pitch turned" :
               kase == 2 ? "LOOP OFF, its pitch turned, put back" : "LOOP OFF and put back, no walk", walked, gone, back,
               looping);
    }
    check("a sampler ghost's instrument edited while it waits: out of its loop at its next walk (for good), not at the "
          "edit itself", ok);
    pool_empty();
}
/* a TB-303 line above a smaller count (settings.md, another count): track 5's 303 at VOICES 12 in engine 9 (the engines
 * under it the keys'), its note let go (a line's gate closes half a row on), engine 8's key let go for good (no key's
 * held ghost made by the count, which a song's note would wait behind), VOICES 8, an engine under 8 freed: its next
 * note takes an engine under 8, the one before ringing out in 9 */
static void tb_above(pl_inst_t pad, pl_inst_t tb)
{
    pl_chan_t *ch = &pl_ch[5];
    pl_voice_t *keys[9], *v, *w;
    uint32_t was = PL_NO_ENG;
    int k, ok;
    pool_empty();
    for (k = 0; k < 9; k++)
        keys[k] = note_at(pad, (uint32_t)(48 + k), (uint32_t)k);
    memset(ch, 0, sizeof *ch);
    ch->ins = tb;
    ch->has_ins = 1;
    ch->voice = -1;
    ch->pan = 128;
    pl_tb_note(5, 48);
    v = ch->voice >= 0 ? &pl_v[ch->voice] : 0;
    if (v) {
        was = v->place;
        pl_v_off(v);                     /* (its gate closed) */
    }
    if (keys[8])
        pl_v_free(keys[8]);
    pl_nv_apply(8);
    if (keys[3])
        pl_v_free(keys[3]);
    pl_tb_note(5, 50);
    w = ch->voice >= 0 ? &pl_v[ch->voice] : 0;
    ok = v && was == 9u && w && w != v && w->place < 8u && v->place == was && !v->held && pl_e_sounds(&pl_e[was]);
    printf("a TB-303 line in engine %u at VOICES 12, let go, VOICES 8: its next note in engine %u, the one before in %u "
           "ringing out %d\n", (unsigned)was, w ? (unsigned)w->place : 255u, v ? (unsigned)v->place : 255u,
           v && v->place < PL_NE ? pl_e_sounds(&pl_e[v->place]) : 0);
    check("a TB-303 line above a smaller count: its next note takes an engine within the count, the old one rings out",
          ok);
    memset(ch, 0, sizeof *ch);
    ch->voice = -1;
    pool_empty();
}
/* a TUNE command sets the note's TUNE in place of its instrument's (commands.md: a command sets its value), at its start
 * and on it while it sounds: the demo disk's TOM HI (DRUM KIT A0:09, its own TUNE -57) with TUNE 0x80 + 11 plays at
 * +11/128 as the engine takes it (1/64 steps: +5), not -57 + 11; then TUNE 0x80 - 3 turned on it, at -3/128 (-1) */
static void tune_cmd(void)
{
    pl_inst_t tom = {0xA0, 9};
    pl_voice_t *v = pl_v_spare();
    const uint8_t pc[1] = {0x45}, pv[1] = {0x80 + 11};
    samp_t s;
    uint32_t want1 = 0, want2 = 0, got1 = 0, got2 = 0;
    int inst = 0, on = 0;
    pool_empty();
    pl_e[0].kind = 255;
    pl_al_c = 0;
    pl_al_born = 0;
    if (v && pl_voice_on(v, PV_SONG, 4, tom, 60, 127, 128, 0, 4096, pc, pv, 1, 0) && v->place == 0u) {
        on = 1;
        s = pl_e[0].u.sm.s;
        inst = s.tune;
        s.tune = 11;
        want1 = samp_step(&s, 60, 0);
        got1 = pl_e[0].u.sm.step;
        pl_param_live(v, 0x45, 0x80 - 3);
        pl_v_pitch(v, v->bend);          /* (its track's tick follows it: pl_track_follow) */
        s.tune = -3;
        want2 = samp_step(&s, 60, 0);
        got2 = pl_e[0].u.sm.step;
    }
    printf("TUNE on A0:09 (its own %d): 0x80 + 11 at its start, step %u (+11 alone %u); 0x80 - 3 on it, %u (-3 alone %u)\n",
           inst, (unsigned)got1, (unsigned)want1, (unsigned)got2, (unsigned)want2);
    check("a TUNE command in place of the instrument's TUNE, at the note's start and on it", on && inst == -57 &&
          got1 == want1 && got2 == want2 && want1 != want2);
    pool_empty();
}
/* a TB-303's next note within its NOTE CUT's fade (the cut voice's gain 0 till the voices pass cuts it): the voice being
 * cut is not taken again, so the note takes one of its own and sounds; the cut one goes as cut */
static void tb_after_cut(pl_inst_t tb)
{
    pl_chan_t *ch = &pl_ch[5];
    pl_voice_t *v, *w;
    pool_empty();
    memset(ch, 0, sizeof *ch);
    ch->ins = tb;
    ch->has_ins = 1;
    ch->voice = -1;
    ch->pan = 128;
    pl_tb_note(5, 48);
    v = ch->voice >= 0 ? &pl_v[ch->voice] : 0;
    if (v)
        pl_v_cut_soon(v);                /* (NOTE CUT: it fades over the next block) */
    pl_tb_note(5, 50);                   /* (the next note before that block) */
    w = ch->voice >= 0 ? &pl_v[ch->voice] : 0;
    printf("a TB-303 note within its NOTE CUT's fade: in voice %d (the cut one %d, cut %d), cut %d, note %u\n",
           w ? (int)(w - pl_v) : -1, v ? (int)(v - pl_v) : -1, v ? v->cut : -1, w ? w->cut : -1, w ? (unsigned)w->note : 0u);
    check("a TB-303 note within its NOTE CUT's fade: a voice of its own, not the one being cut (heard)",
          v && v->cut && w && w != v && !w->cut && w->note == 50 && w->held);
    memset(ch, 0, sizeof *ch);
    ch->voice = -1;
    pool_empty();
}
/* LEG onto a one-shot that has ended by itself (specs/files/commands.md, legato: only onto a note still sounding): a
 * song's note of `shot` (a one-shot sample) held on track 4 in engine 0, played to its end there (its engine sounding
 * no more, the voice its engine's till the next voices pass): a LEG then is refused, the row a plain note; while it
 * sounded it was taken */
static void leg_ended(pl_inst_t shot)
{
    pl_chan_t *ch = &pl_ch[4];
    pl_voice_t *v = pl_v_spare();
    rec_cell_t c;
    int32_t buf[PL_BLOCK];
    int before = 0, after = 1, b;
    pool_empty();
    memset(ch, 0, sizeof *ch);
    ch->ins = shot;
    ch->has_ins = 1;
    ch->voice = -1;
    ch->pan = 128;
    pl_e[0].kind = 255;
    pl_al_c = 0;
    pl_al_born = 0;
    if (pl_voice_on(v, PV_SONG, 4, shot, 60, 127, 128, 0, 4096, 0, 0, 0, 0))
        v->gen = ch->gen;
    else
        v = 0;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE;
    c.note = 62;
    before = v && pl_leg_ok(4, &c);
    for (b = 0; v && b < 400000 && pl_e_sounds(&pl_e[0]); b++) {
        memset(buf, 0, sizeof buf);
        samp_render(&pl_e[0].u.sm, buf);
    }
    after = v && v->place == 0u && v->held && pl_leg_ok(4, &c);
    printf("LEG on a held one-shot of %02X:%02X on track 4: taken while it sounds %d; once it has ended by itself (its engine "
           "still its own %d) %s\n", shot.cart, shot.slot, before, v && v->place == 0u, after ? "taken" : "refused");
    check("LEG onto a held note that has ended by itself: refused, the row a plain note", before && !after && v &&
          v->place == 0u);
    memset(ch, 0, sizeof *ch);
    ch->voice = -1;
    pool_empty();
}
/* LEG onto a held ghost whose end has come (commands.md, legato: only onto a note still sounding): a song's note of
 * `shot` (a one-shot sample) held on track 4, born a ghost, the block clock run on to its end (the ghost still listed:
 * its end due, the voices pass that frees it not run yet): a LEG then is refused, the row a plain note; before its end
 * it was taken */
static void leg_ghost_ended(pl_inst_t shot)
{
    pl_chan_t *ch = &pl_ch[4];
    pl_voice_t *g;
    rec_cell_t c;
    int before = 0, after = 1, b;
    pool_empty();
    memset(ch, 0, sizeof *ch);
    ch->ins = shot;
    ch->has_ins = 1;
    ch->voice = -1;
    ch->pan = 128;
    g = song_ghost(shot, 4, 60);
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE;
    c.note = 62;
    before = g && g->t_end != PL_NEVER && !pl_t_due(g->t_end) && pl_leg_ok(4, &c);
    for (b = 0; g && b < 400000 && !pl_t_due(g->t_end); b++)
        blk_tick();
    after = g && g->use == PV_SONG && g->place == PL_NO_ENG && pl_t_due(g->t_end) && pl_leg_ok(4, &c);
    printf("LEG on a held one-shot ghost of %02X:%02X on track 4: taken before its end %d; its end come, still listed %d: "
           "%s\n", shot.cart, shot.slot, before, g && g->use == PV_SONG && pl_t_due(g->t_end), after ? "taken" : "refused");
    check("LEG onto a held ghost whose end has come (still listed): refused, the row a plain note", before && !after &&
          g && g->use == PV_SONG);
    memset(ch, 0, sizeof *ch);
    ch->voice = -1;
    pool_empty();
}
/* ---- section 2's rule, case by case (specs/proposals/202611121830-ghost-notes.md, sections 2 and 11): songs of the
 * test's own, put in the disk's place (disk_put: specs/files/patterns.md, songs.md) ---- */
typedef struct {
    uint8_t row, note;                   /* note: 0 none, REC_NOTE_OFF an off */
    uint8_t cart, slot;                  /* its instrument (cart 0: none named) */
    uint8_t vol;                         /* 0xFF: none */
    uint8_t n, cmd[4][2];                /* its commands (code, value) */
} mk_t;
static uint32_t mk_track(uint8_t *p, const mk_t *c, int n)   /* a track's stream (its cells in row order): its bytes */
{
    uint32_t at = 0, k;
    int i, last = -1;
    for (i = 0; i < n; i++) {
        uint32_t skip = (uint32_t)(c[i].row - last - 1), h = (c[i].note ? 0x80u : 0u) | (c[i].cart ? 0x60u : 0u) |
                        (c[i].vol != 0xFFu ? 0x10u : 0u) | (c[i].n ? 0x08u : 0u) | (skip < 7u ? skip : 7u);
        p[at++] = (uint8_t)h;
        if (c[i].note)
            p[at++] = c[i].note;
        if (c[i].cart) {
            p[at++] = c[i].cart;
            p[at++] = c[i].slot;
        }
        if (c[i].vol != 0xFFu)
            p[at++] = c[i].vol;
        if (c[i].n > 1u) {
            p[at++] = 0xFF;
            p[at++] = c[i].n;
        }
        for (k = 0; k < c[i].n; k++) {
            p[at++] = c[i].cmd[k][0];
            p[at++] = c[i].cmd[k][1];
        }
        if (skip >= 7u)
            p[at++] = (uint8_t)skip;
        last = c[i].row;
    }
    p[at++] = 0;
    return at;
}
/* pattern num of ntrk tracks (each its cells: cells[t], n[t]) and rows; song num playing it pos times, TEMPO 125, 6
 * ticks a row, STOP at its end, LIMITER alone on (a new song's setup). The disk keeps pointing at the records put
 * (disk_put), so each number has buffers of its own (made again, it takes its own: the disk's records of it replaced),
 * 8 numbers at most */
static void mk_song(uint8_t num, int ntrk, int rows, const mk_t *const *cells, const int *n, int pos)
{
    static uint8_t pat[8][4096], song[8][128], nums[8];
    static int used;
    uint8_t *p, *s;
    uint32_t at = 5, bits = (1u << ntrk) - 1u;
    int t;
    for (t = 0; t < used && nums[t] != num; t++)
        ;
    if (t == 8) {
        check("the test's own songs: 8 numbers at most", 0);
        return;
    }
    if (t == used)
        nums[used++] = num;
    p = pat[t];
    s = song[t];
    p[0] = (uint8_t)ntrk;
    p[1] = (uint8_t)(rows - 1);
    p[2] = 1;
    p[3] = (uint8_t)bits;
    p[4] = (uint8_t)(bits >> 8);
    for (t = 0; t < ntrk; t++)
        at += mk_track(p + at, cells[t], n[t]);
    disk_put(REC_PATTERN, num, 0, p, at);
    memset(s, 0, 128);
    s[12] = 125;
    s[14] = 6;
    s[15] = 4;
    s[16] = 50;
    s[17] = RS_LINEAR | RS_STOP;
    s[19] = 64;
    s[20] = (uint8_t)(pos - 1);
    for (t = 0; t < pos; t++)
        s[21 + t] = num;
    at = 21u + (uint32_t)pos;
    s[at++] = 0;                         /* (no track's mixer stored: the defaults) */
    s[at + 1] = 0xF7;                    /* (the chain: LIMITER, on) */
    memset(s + at + 2, 0xFF, 5);
    s[at + 7] = 0x80;
    disk_put(REC_SONG, num, 0, s, at + 64u + 8u);
}

/* a song voice's control part as the rules leave it (what a ghost keeps as a sounding note does), times from the song's
 * start, and what its engine got of it: its pitch, key and level there, a sampler's place (a ghost's walked to now, as
 * its return would find it) */
typedef struct {
    uint8_t who, gen, held, note, key, acc, lcut, engine, cart, slot, in_eng, has_sp;
    uint8_t older;                       /* not its track's latest note, a ghost whose return has begun (seen GH_BACK, or
                                          * with an engine after being a ghost): it has its track's commands as they
                                          * were then (section 2), not its own. A ghost whose return has not begun has
                                          * its own list, TUNE and place, compared */
    int16_t arp64, bend, tune;
    uint32_t t_on, t_off, age;
    int32_t lvl, trim, gain, e_pitch, e_key, e_gain;
    uint8_t p[JUNO_PATCH_BYTES];
    samp_place_t sp;
} dv_t;
static uint8_t dv_gen0[2u * PL_NCH];     /* (the tracks' note counts at the run's start: each run's counted from it) */
static uint32_t dv_ghosted[PL_NV];       /* (each voice's note, its age, while it is or was a ghost) */
static uint32_t dv_back[PL_NV];          /* (... once its return has begun: its commands its track's since) */
static void dv_of(const pl_voice_t *v, uint32_t t0, dv_t *d)
{
    uint32_t i = (uint32_t)(v - pl_v);
    memset(d, 0, sizeof *d);
    if (v->gh & 1u)
        dv_ghosted[i] = v->age;
    if (dv_ghosted[i] == v->age && (v->gh == GH_BACK || v->place != PL_NO_ENG))
        dv_back[i] = v->age;
    d->older = dv_back[i] == v->age && v->gen != PL_CH(v->who).gen;
    d->who = v->who;
    d->gen = (uint8_t)(v->gen - dv_gen0[v->who & 31u]);
    d->held = v->held;
    d->note = v->note;
    d->key = v->key;
    d->acc = v->acc;
    d->lcut = v->lcut;
    d->engine = v->engine;
    d->cart = v->inst.cart;
    d->slot = v->inst.slot;
    d->arp64 = v->arp64;
    d->bend = v->bend;
    d->tune = v->tune;
    d->t_on = v->t_on - t0;
    d->t_off = v->held ? FWD_HELD : v->t_off - t0;
    d->age = v->age;
    d->lvl = v->lvl;
    d->trim = v->trim;
    d->gain = v->gain;
    if (v->engine == EN_JUNO) {
        memcpy(d->p, v->p.jp, JUNO_PATCH_BYTES);
    } else {
        d->p[0] = v->p.par.n;
        memcpy(d->p + 1, v->p.par.pc, v->p.par.n);
        memcpy(d->p + 1 + PL_NPAR, v->p.par.pv, v->p.par.n);
    }
    if (v->place != PL_NO_ENG) {
        const pl_eng_t *e = &pl_e[v->place];
        d->in_eng = 1;
        if (v->engine == EN_DX7) {
            d->e_pitch = e->u.dx.bend;
        } else if (v->engine == EN_JUNO) {
            d->e_pitch = e->u.ju.bend;
            d->e_key = e->u.ju.pitch88;
            d->e_gain = e->u.ju.gain_q12;
        } else if (v->engine == EN_CHIP) {
            d->e_pitch = e->u.ch.bend64;
        } else if (v->engine == EN_SAMP) {
            const samp_voice_t *sm = &e->u.sm;
            d->e_pitch = (int32_t)sm->step;
            samp_place_of(sm, &d->sp);
            d->has_sp = sm->on || sm->pos < 0 || (sm->pos >= sm->s.end - 1 && !sm->looping);   /* (ended by its
                                          * envelope, its place stood where it was: a place, a ghost's, has no envelope) */
        }
    } else if (v->engine == EN_SAMP && (v->gh & 1u)) {
        samp_t s;
        if (pl_v_samp(v, &s, 1)) {       /* (its place walked to now on a copy, at the pitch it had since) */
            d->sp = v->x.sp;
            if (pl_blk != v->t_pl)
                samp_place_go(&s, &d->sp, samp_step(&s, v->note, v->pb + v->tune), (pl_blk - v->t_pl) * PL_BLOCK,
                              v->held);
            d->has_sp = 1;
        }
    }
}
static int dv_twin(const dv_t *a, const dv_t *b)   /* the same note of the song */
{
    return a->who == b->who && a->gen == b->gen && a->t_on == b->t_on && a->acc == b->acc && a->cart == b->cart &&
           a->slot == b->slot && a->engine == b->engine;
}
/* the reference's a and the run's b: the fields that differ (0: none). A song's older note whose return has begun (b)
 * has its track's commands as they were at its return (a DX7's, a sampler's, a CHIP's; a Juno-106's patch copy and a
 * FUSION layer's own list kept): its list, a sampler's TUNE and place by them, not compared. (Compared, the mutes run
 * fails at song 10's block 3526: its DX7 track's note 60, let go at row 40, back from a ghost with the 0x49 its track
 * turned at row 42, after it was let go, which the note never taken never got) */
static const char *dv_diff(const dv_t *a, const dv_t *b)
{
    int own = !b->older || b->engine == EN_JUNO || b->acc;
    if (a->held != b->held || a->t_off != b->t_off)
        return "held or let go";
    if (a->note != b->note || a->key != b->key)
        return "note or key";
    if (a->bend != b->bend || a->arp64 != b->arp64 || (own && a->tune != b->tune))
        return "pitch";
    if (a->lvl != b->lvl || a->trim != b->trim || a->gain != b->gain)
        return "level";
    if (a->lcut != b->lcut)
        return "ECx";
    if (own && memcmp(a->p, b->p, sizeof a->p))
        return "parameters";
    if (a->in_eng && b->in_eng && (a->e_key != b->e_key || a->e_gain != b->e_gain ||
                                   ((own || b->engine != EN_SAMP) && a->e_pitch != b->e_pitch)))
        return "its engine's pitch, key or level";
    if (own && a->has_sp && b->has_sp && memcmp(&a->sp, &b->sp, sizeof a->sp))
        return "a sampler's place";
    return 0;
}
#define DV_BLOCKS 12000
typedef struct {
    uint16_t n;
    dv_t v[PL_NV];
    uint32_t held;                       /* FILTER's envelope's held tracks (fx_clk.held, the keys' bit apart) */
    uint8_t ch_gen[2u * PL_NCH];         /* the tracks' note counts */
} dv_blk_t;
static dv_blk_t *dv_ref;                 /* the song with every note in an engine, block by block */
static int dv_ref_n;
static void dv_take(dv_blk_t *b, uint32_t t0, int ntrk)   /* the block's song voices (not being cut), by age */
{
    uint32_t k, i, j;
    b->n = 0;
    for (k = 0; k < PL_NV; k++)
        if (pl_v[k].use == PV_SONG && !pl_v[k].cut)
            dv_of(&pl_v[k], t0, &b->v[b->n++]);
    for (i = 1; i < b->n; i++)           /* (by age: twins paired in their order) */
        for (j = i; j > 0 && b->v[j - 1u].age > b->v[j].age; j--) {
            dv_t x = b->v[j];
            b->v[j] = b->v[j - 1u];
            b->v[j - 1u] = x;
        }
    b->held = fx_clk.held & ~1u;
    memset(b->ch_gen, 0, sizeof b->ch_gen);
    for (k = 0; k < (uint32_t)ntrk && k < 2u * PL_NCH; k++)
        b->ch_gen[k] = (uint8_t)(PL_CH(k).gen - dv_gen0[k]);
}
/* block b of a run against the reference's block b: every held note there a twin here and the reverse, every note let
 * go in both the same; the twins' fields the same; the tracks' notes and FILTER's held the same. The first difference
 * printed; 0 none. A note let go with no twin is counted (dv_miss: the reference's, the run's), not failed: a tail
 * freed under hearing or taken in one, a ghost let go kept in the other (the dedicated cases test those ends) */
static long dv_miss[2];
static int dv_cmp(const dv_blk_t *r, const dv_blk_t *h, int b, const char *run)
{
    uint8_t used[PL_NV];
    int i, j;
    memset(used, 0, sizeof used);
    if (r->held != h->held || memcmp(r->ch_gen, h->ch_gen, sizeof r->ch_gen)) {
        printf("  %s, block %d: FILTER's held tracks %X (%X) or the tracks' notes differ (%02X%02X%02X%02X%02X "
               "%02X%02X%02X%02X%02X)\n", run, b, h->held, r->held, h->ch_gen[0], h->ch_gen[1], h->ch_gen[2],
               h->ch_gen[3], h->ch_gen[4], r->ch_gen[0], r->ch_gen[1], r->ch_gen[2], r->ch_gen[3], r->ch_gen[4]);
        return 1;
    }
    for (i = 0; i < r->n; i++) {
        const dv_t *a = &r->v[i];
        const char *d;
        for (j = 0; j < h->n && (used[j] || !dv_twin(a, &h->v[j])); j++)
            ;
        if (j == h->n) {
            if (!a->held) {              /* (a tail freed under hearing in one, a ghost kept in the other) */
                dv_miss[0]++;
                continue;
            }
            printf("  %s, block %d: track %u's note %u (from block %u) held there, not here\n", run, b, a->who, a->note,
                   a->t_on);
            return 1;
        }
        used[j] = 1;
        if ((d = dv_diff(a, &h->v[j])) != 0) {
            printf("  %s, block %d: track %u's note %u (layer %u, from block %u): its %s differ (bend %d %d, gain %d %d, "
                   "held %u %u, in an engine %u %u, a sampler's place %X.%04X %X.%04X)\n", run, b, a->who, a->note, a->acc,
                   a->t_on, d, h->v[j].bend, a->bend, h->v[j].gain, a->gain, h->v[j].held, a->held, h->v[j].in_eng,
                   a->in_eng, (unsigned)h->v[j].sp.lo | (unsigned)h->v[j].sp.hi << 16, h->v[j].sp.frac,
                   (unsigned)a->sp.lo | (unsigned)a->sp.hi << 16, a->sp.frac);
            return 1;
        }
    }
    for (j = 0; j < h->n; j++)
        if (!used[j] && h->v[j].held) {
            printf("  %s, block %d: track %u's note %u (from block %u) held here, not in the reference\n", run, b,
                   h->v[j].who, h->v[j].note, h->v[j].t_on);
            return 1;
        } else if (!used[j]) {
            dv_miss[1]++;
        }
    return 0;
}
/* what a run plays (a song, DJ's decks): prep before the count starts; act before each block b (0: the start); its
 * tracks muted or heard (mute); at block cut_at an action that cuts every note of replay memory cut_mem (-1 both) it
 * started before (K1's restart, a stop's end), checked cut_wait blocks on; ntrk: the tracks compared */
typedef struct {
    void (*prep)(void);
    void (*act)(int b);
    void (*mute)(int on);
    void (*end)(void);
    int cut_at, cut_wait, cut_mem, ntrk, free_at;
} dv_drv_t;
/* the driver's run from a quiet pool at VOICES 12 (how 0: the reference, every note in an engine, its blocks kept), or
 * against it: how 1 twelve keys held on the takers from before it starts (every engine a key's: its notes born ghosts,
 * followed by every rule) and let go at block free_at (its ghosts back into the keys' tails' engines); how 2 at VOICES 4
 * (its notes taking each other's engines, ghosts and back all the time); how 3 its tracks muted and heard again every
 * 150 blocks (ghosts at each mute, back at each unmute). Each block's song voices compared with the reference's
 * (dv_cmp); the ghosts seen, the returns, the most ghosts at once, the cut's notes gone. 0: the same throughout */
typedef struct {
    int ghosts_seen, backs, most, steals, blocks, born, k1_gone;
} dv_run_t;
static int dv_run(const dv_drv_t *d, int how, dv_run_t *out)
{
    static const char *const RUN[4] = {"the reference", "the keys holding every engine", "VOICES 4", "mutes"};
    uint32_t t0, s0, k;
    int b, bad = 0, keys = how == 1;
    memset(out, 0, sizeof *out);
    quiet();
    if (how == 2)
        player_voice_count(4);
    if (d->prep)
        d->prep();
    run(0.05);
    if (keys)
        for (k = 0; k < 12u; k++)
            player_key_on((uint8_t)(36 + 3 * k), quick_in, 100);
    run(0.2);
    if (!how) {
        free(dv_ref);
        dv_ref = calloc(DV_BLOCKS, sizeof *dv_ref);
        if (!dv_ref) {
            fprintf(stderr, "voices_test: no memory for the reference run's %u blocks\n", (unsigned)DV_BLOCKS);
            exit(1);
        }
        dv_ref_n = 0;
    }
    s0 = pl_steals;
    memset(dv_ghosted, 0, sizeof dv_ghosted);
    memset(dv_back, 0, sizeof dv_back);
    for (k = 0; k < 2u * PL_NCH; k++)
        dv_gen0[k] = PL_CH(k).gen;
    t0 = pl_blk;
    for (b = 0; b < DV_BLOCKS; b++) {
        static dv_blk_t now;
        uint32_t g, ghosts = 0, left = 0;
        if (keys && b == d->free_at)
            player_keys_off();
        d->act(b);
        if (how == 3 && b % 150 == 75)
            d->mute(b % 300 == 75);
        blocks(1);
        for (g = 0; g < pl_gn; g++)
            ghosts += pl_v[pl_gl[g]].use == PV_SONG;
        out->ghosts_seen += ghosts > 0;
        out->most = (int)ghosts > out->most ? (int)ghosts : out->most;
        for (k = 0; k < PL_NV; k++) {
            out->backs += pl_v[k].use == PV_SONG && pl_v[k].back == PL_BACK_N - 1u;   /* (back in this block) */
            left += pl_v[k].use == PV_SONG && (d->cut_mem < 0 || (int)(pl_v[k].who >> 4 & 1u) == d->cut_mem) &&
                    (int32_t)(pl_v[k].t_on - t0) < d->cut_at;
        }
        if (b == d->cut_at + d->cut_wait)   /* (the cut done: every note of it from before gone, its ghosts at once) */
            out->k1_gone = !left;
        if (!how) {                      /* (the reference: on till nothing plays and its notes are gone) */
            dv_take(&dv_ref[b], t0, d->ntrk);
            dv_ref_n = b + 1;
            if (!st().playing && !dv_ref[b].n && b > d->cut_at + d->cut_wait + 200)
                break;
        } else {                         /* (a run: as long as the reference) */
            dv_take(&now, t0, d->ntrk);
            if (!bad)
                bad = dv_cmp(&dv_ref[b], &now, b, RUN[how]);
            if (b + 1 >= dv_ref_n)
                break;
        }
    }
    out->blocks = b + 1;
    out->steals = (int)(pl_steals - s0);
    player_keys_off();
    d->mute(0);
    if (d->end)
        d->end();
    player_stop();
    return bad;
}
/* the songs' driver: song dv_num from its start, PLAYBACK's K1 a position on at block 4500 (every note of the song cut,
 * the song started again there) */
static uint8_t dv_num;
static void dv_song_act(int b)
{
    if (!b)
        player_play_song(dv_num, 0, 0);
    else if (b == 4500)
        player_skip(1);
}
static void dv_song_mute(int on) { player_mutes(on ? 0x3Fu : 0u); }
/* DJ's driver: BREAKS LOOP (A7) on the left deck and WORLD LOOP (B1) on the right, both sampler loops, the crossfader in
 * the middle (both heard); KEY on each, the left's BRAKE (its pitch falling every block, then its notes cut where it
 * stands, then spun up again in time), the right's BACKSPIN, the left's ECHO OUT and the right's REVERB OUT; the decks'
 * mutes (dv_dj_mute) */
static void *dv_dj_mem;
static player_dj_t dv_mix;
#define DV_BRAKE 3500
static void dv_dj_prep(void)
{
    rec_song_t s;
    if (!dv_dj_mem)
        dv_dj_mem = malloc(player_dj_mem_size());
    player_dj_mem(dv_dj_mem);
    player_dj(1, rec_song(0xA7, &s) ? 0 : s.fx);
    memset(&dv_mix, 0, sizeof dv_mix);
    dv_mix.xf = 16;
    dv_mix.mix_set[0] = 50;
    dv_mix.mix_set[1] = 30;
    dv_mix.mix_set[2] = 35;
    player_dj_set(&dv_mix);
    player_deck_load(0, 0xA7);
    player_deck_load(1, 0xB1);
}
static void dv_dj_act(int b)
{
    if (!b) {
        player_deck_play(0, 1, 0, 0);
        player_deck_play(1, 1, 0, 0);
    } else if (b == 1500 || b == 2300) {
        dv_mix.key[b == 1500 ? 0 : 1] = (int8_t)(b == 1500 ? 3 : -4);
        player_dj_set(&dv_mix);
    } else if (b == DV_BRAKE || b == DV_BRAKE + 500) {
        player_deck_stop(0, PL_STOP_BRAKE, b == DV_BRAKE, 0);
    } else if (b == 5000 || b == 5800) {
        player_deck_stop(1, PL_STOP_BACKSPIN, b == 5000, 0);
    } else if (b == 6500 || b == 7000) {
        player_deck_stop(0, PL_STOP_ECHO, b == 6500, 0);
    } else if (b == 7800 || b == 8200) {
        player_deck_stop(1, PL_STOP_REVERB, b == 7800, 0);
    } else if (b == 9000) {
        player_deck_play(0, 0, 0, 0);
        player_deck_play(1, 0, 0, 0);
    }
}
static void dv_dj_mute(int on)
{
    player_deck_mutes(0, on ? 0x0Fu : 0u);
    player_deck_mutes(1, on ? 0x33u : 0u);
}
static void dv_dj_end(void) { player_dj(0, 0); }

/* DJ's pass-by in a real two-deck session (songs of the test's own on the decks, chords of a Juno-106 pad held), VOICES
 * 4: the left's four notes; the right's four 0.25 s on (both heard, the right the lower in the mix: the first takes a
 * left one's engine, the others their own deck's: ghosts); the left's fifth at row 16 (it takes the right's last: its
 * ghost made last); the crossfader full left (the right faded out); at row 32 the left's track 1 let go: the right's held
 * ghosts, first in the order, find none of their deck's to take and are passed by, the left's comes back into its
 * tail's engine. Throughout, a faded-out deck's note or ghost never takes an engine the heard deck's note held in the
 * block before */
static void dj_pass_by(pl_inst_t pad)
{
    mk_t l[5][2], r[4][1];
    const mk_t *L[5], *R[4];
    int nl[5], nr[4], k, b, taken = 0, lefts = 0, rights = 0, waits = 0, gl = 0, gr = 0, last = -1;
    uint8_t had[PL_NE];
    uint32_t c;
    for (k = 0; k < 5; k++) {
        mk_t on = {(uint8_t)(k < 4 ? 0 : 16), (uint8_t)(48 + 3 * k), pad.cart, pad.slot, 0x7F, 0, {{0, 0}}},
             off = {32, REC_NOTE_OFF, 0, 0, 0xFF, 0, {{0, 0}}};
        l[k][0] = on;
        l[k][1] = off;
        L[k] = l[k];
        nl[k] = k == 1 ? 2 : 1;
    }
    for (k = 0; k < 4; k++) {
        mk_t on = {0, (uint8_t)(62 + 4 * k), pad.cart, pad.slot, 0x7F, 0, {{0, 0}}};
        r[k][0] = on;
        R[k] = r[k];
        nr[k] = 1;
    }
    mk_song(0x12, 5, 64, L, nl, 1);
    mk_song(0x13, 4, 64, R, nr, 1);
    quiet();
    player_voice_count(4);
    dv_dj_prep();
    player_deck_load(0, 0x12);
    player_deck_load(1, 0x13);
    run(0.05);
    player_deck_play(0, 1, 0, 0);
    run(0.25);
    player_deck_play(1, 1, 0, 0);
    run(2.25);                           /* (the left's row 16 at 1.92 s) */
    dv_mix.xf = 0;
    player_dj_set(&dv_mix);
    run(0.1);
    for (c = 0; c < pl_gn; c++)
        if (pl_v[pl_gl[c]].use == PV_SONG && pl_v[pl_gl[c]].held) {
            uint32_t d = (pl_v[pl_gl[c]].who >> 4 & 1u) ^ pl_swapped;
            gl += d == 0u;
            gr += d == 1u;
            last = (int)d;               /* (the held ghost made last: first in the order) */
        }
    for (b = 0; b < 3300; b++) {         /* (4.8 s: the left's row 32 at 3.84 s, its end at 7.7 s) */
        uint32_t g, w = 0;
        for (c = 0; c < pl_nv; c++) {
            const pl_voice_t *v = pl_e_note(c);
            had[c] = (uint8_t)(!v ? 3u : v->use == PV_SONG ? (v->who >> 4 & 1u) ^ pl_swapped : 2u);
        }
        blocks(1);
        for (c = 0; c < pl_nv; c++) {
            const pl_voice_t *v = pl_e_note(c);
            uint32_t now = !v ? 3u : v->use == PV_SONG ? (v->who >> 4 & 1u) ^ pl_swapped : 2u;
            taken += had[c] == 0u && now == 1u;
            if (v && v->back == PL_BACK_N - 1u && v->use == PV_SONG) {
                rights += now == 1u;
                lefts += now == 0u;
            }
        }
        for (g = 0; g < pl_gn; g++)
            w += pl_v[pl_gl[g]].use == PV_SONG && pl_v[pl_gl[g]].held &&
                 ((pl_v[pl_gl[g]].who >> 4 & 1u) ^ pl_swapped) == 1u;
        waits += w > 0;
    }
    printf("DJ, the right deck faded out at VOICES 4 (held ghosts then: the left's %d, the right's %d, the one made last "
           "the %s's): the left deck's engines taken by the right's %d; returns: the left's %d, the right's %d; the "
           "right's held ghosts waiting in %d blocks of 3300; the right deck heard %d\n", gl, gr,
           last == 1 ? "right" : "left", taken, lefts, rights, waits, pl_dj_heard(1));
    check("DJ: a faded-out deck takes none of the heard deck's engines; the heard deck's ghosts come back past its",
          gl > 0 && gr > 0 && last == 1 && !taken && lefts >= 1 && !rights && waits == 3300 && !pl_dj_heard(1));
    dv_mix.xf = 16;
    dv_dj_end();
    quiet();
}

/* the rules on a song's ghosts: a DX7, a Juno-106, a sampler (its place walked through its slides) and a CHIP track, a
 * track going A -> B -> A, each note let go by a note off and slid after it (the track's following through it), LEG, 3xx,
 * 1xx, 2xx, vibrato, arpeggio, Cxx, Axy, NOTE CUT (ECx), KEY OFF, SAMPLE OFFSET, parameter commands (a Juno-106's in its
 * patch copy), K1's restart, the song's stop at its end; and a FUSION track whose row's layers run 0xy, EDx, Axy, E9x
 * and ECx (A5:03) */
static void rules_song(pl_inst_t dx, pl_inst_t dx2, pl_inst_t ju, pl_inst_t sm, pl_inst_t chp)
{
#define C0(r, nt) {r, nt, 0, 0, 0xFF, 0, {{0, 0}}}
#define CI(r, nt, i) {r, nt, (i).cart, (i).slot, 0x7F, 0, {{0, 0}}}
#define C1(r, nt, a, b) {r, nt, 0, 0, 0xFF, 1, {{a, b}}}
#define C2(r, nt, a, b, c, d) {r, nt, 0, 0, 0xFF, 2, {{a, b}, {c, d}}}
    const mk_t t0[] = {CI(0, 60, dx), C0(4, REC_NOTE_OFF), C1(5, 0, 0x0A, 0x04), CI(8, 64, dx), C1(10, 67, 0xD3, 0),
                       C1(12, 72, 0x03, 0x08), C1(13, 0, 0x03, 0), C1(16, 0, 0x01, 0x04), C1(18, 0, 0x04, 0x46),
                       C1(20, 0, 0x48, 30), C1(22, 0, 0x0C, 0x20), C1(24, 0, 0x0E, 0xC3), C0(26, 62),
                       C1(28, 0, 0x14, 0x03), C1(32, 60, 0x00, 0x37), C1(36, 0, 0x0A, 0x40), C0(40, 65),
                       C2(42, 0, 0x02, 0x03, 0x49, 40), C0(48, REC_NOTE_OFF), C1(49, 0, 0x0A, 0x08), C0(56, 60)};
    const mk_t t1[] = {CI(0, 48, ju), C1(8, 50, 0xD3, 0), C1(12, 55, 0x03, 0x06), C1(14, 0, 0x03, 0),
                       C1(16, 0, 0x45, 40), C1(18, 0, 0x4E, 90), C0(20, REC_NOTE_OFF), C1(21, 0, 0x0A, 0x04),
                       C0(24, 48), C1(30, 0, 0x04, 0x38), C1(34, 53, 0xD3, 0), C0(40, REC_NOTE_OFF), C0(44, 52)};
    const mk_t t2[] = {CI(0, 60, sm), C1(6, 72, 0x03, 0x10), C1(7, 0, 0x03, 0), C1(10, 0, 0x03, 0),
                       C1(14, 0, 0x02, 0x08), C1(16, 0, 0x01, 0x08), C1(20, 0, 0x45, 0xA0), C1(24, 62, 0x09, 0x02),
                       C1(28, 0, 0x04, 0x68), C1(30, 0, 0x42, 0), C1(32, 67, 0xD3, 0), C0(36, REC_NOTE_OFF),
                       C1(37, 0, 0x0A, 0x02), C0(44, 60), C1(46, 64, 0x03, 0x20), C1(48, 0, 0x03, 0)};
    const mk_t t3[] = {CI(0, 60, chp), C1(10, 65, 0xD3, 0), C1(14, 0, 0x00, 0x47), C1(18, 0, 0x57, 5),
                       C0(22, REC_NOTE_OFF), C1(23, 0, 0x0A, 0x06), C0(30, 60), C1(34, 0, 0x54, 8),
                       C1(36, 67, 0x03, 0x10), C1(38, 0, 0x03, 0), C0(44, REC_NOTE_OFF)};
    const mk_t t4[] = {CI(0, 60, dx), C0(2, REC_NOTE_OFF), CI(4, 62, dx2), C0(6, REC_NOTE_OFF), CI(8, 64, dx),
                       C0(10, REC_NOTE_OFF), CI(12, 65, dx2), CI(16, 67, dx), C0(20, REC_NOTE_OFF), CI(28, 60, dx2),
                       C0(30, REC_NOTE_OFF), CI(32, 62, dx), C0(36, REC_NOTE_OFF)};
    const mk_t f0[] = {{0, 60, 0xA5, 0x03, 0x7F, 0, {{0, 0}}}, C0(12, REC_NOTE_OFF), C0(16, 60),
                       C1(18, 0, 0x0A, 0x02), C0(28, 72), C0(40, REC_NOTE_OFF), C0(44, 60), C0(56, REC_NOTE_OFF)};
    const mk_t *const T[5] = {t0, t1, t2, t3, t4}, *const F[2] = {t1, f0};
    const int N[5] = {(int)(sizeof t0 / sizeof t0[0]), (int)(sizeof t1 / sizeof t1[0]), (int)(sizeof t2 / sizeof t2[0]),
                      (int)(sizeof t3 / sizeof t3[0]), (int)(sizeof t4 / sizeof t4[0])},
              NF[2] = {N[1], (int)(sizeof f0 / sizeof f0[0])};
    static const struct {
        uint8_t num;
        const char *what;
        dv_drv_t d;
    } S[3] = {{0x10, "a song of five tracks", {0, dv_song_act, dv_song_mute, 0, 4500, 90, -1, 5, 3000}},
              {0x11, "a song of a Juno-106 and FUSION A5:03", {0, dv_song_act, dv_song_mute, 0, 4500, 90, -1, 2, 3000}},
              {0, "DJ: BREAKS LOOP and WORLD LOOP, KEY, the stops",
               {dv_dj_prep, dv_dj_act, dv_dj_mute, dv_dj_end, DV_BRAKE + 346, 6, 0, 32, 6000}}};
    int s, how, ok[4] = {1, 1, 1, 1};
#undef C0
#undef CI
#undef C1
#undef C2
    mk_song(0x10, 5, 64, T, N, 2);
    mk_song(0x11, 2, 64, F, NF, 2);
    for (s = 0; s < 3; s++) {
        dv_run_t r[4];
        int bad[4];
        dv_num = S[s].num;
        dv_miss[0] = dv_miss[1] = 0;
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        for (how = 0; how < 4; how++)
            bad[how] = dv_run(&S[s].d, how, &r[how]);
        printf("  (%s: note-blocks let go with no twin, not compared: %ld of the reference's, %ld of the runs')\n",
               S[s].what, dv_miss[0], dv_miss[1]);
#ifdef PTFM_DEBUG
        if (s == 1)                      /* (the FUSION song: its layers' ghosts checked as layers at their returns) */
            check("the figures: the FUSION layers' checks before their returns counted as theirs",
                  pl_gf.ready_n[GK_FUS] > 0);
#endif
        printf("%s (%d blocks): the reference %d notes taken; the keys holding every engine: ghosts in %d blocks (%d at "
               "most), %d back; VOICES 4: %d taken, ghosts in %d blocks, %d back; mutes: ghosts in %d blocks, %d back; "
               "the cut (K1's restart, BRAKE's end) left none of its notes %d %d %d %d\n", S[s].what, r[0].blocks,
               r[0].steals, r[1].ghosts_seen, r[1].most, r[1].backs, r[2].steals, r[2].ghosts_seen, r[2].backs,
               r[3].ghosts_seen, r[3].backs, r[0].k1_gone, r[1].k1_gone, r[2].k1_gone, r[3].k1_gone);
        ok[0] &= !bad[0] && !r[0].steals && !r[0].ghosts_seen && r[0].k1_gone;
        ok[1] &= !bad[1] && r[1].ghosts_seen > 2000 && r[1].backs > 0 && r[1].k1_gone;
        ok[2] &= !bad[2] && r[2].steals > 0 && r[2].ghosts_seen > 0 && r[2].backs > 0 && r[2].k1_gone;
        ok[3] &= !bad[3] && r[3].ghosts_seen > 0 && r[3].backs > 0 && r[3].k1_gone;
    }
    check("section 2's rule: the songs and the decks alone, every note in an engine (the reference)", ok[0]);
    check("... the keys holding every engine: every ghost followed as its note, block by block", ok[1]);
    check("... at VOICES 4, notes taken and back: the same, block by block", ok[2]);
    check("... muted and heard again: the same, block by block", ok[3]);
    free(dv_ref);
    dv_ref = 0;
    quiet();
}

int main(int argc, char **argv)
{
    static const struct {
        uint8_t kind;
        const char *what;
    } P[] = {{REC_DX7, "a DX7"}, {REC_JUNO, "a Juno-106"}, {REC_SAMPLER, "a sampler"}, {REC_CHIP, "a CHIP"}};
    unsigned i;
    pl_inst_t pad;
    double r;
    if (argc != 2 || disk_load(argv[1])) {
        fprintf(stderr, "voices_test DISK\n");
        return 2;
    }
    player_init();
    check("an instrument whose release is over at once (the takers of the let-go cases)",
          first_of(REC_DX7, &quick_in, &r, ok_quick));
    printf("the takers: %02X:%02X\n", quick_in.cart, quick_in.slot);
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        int ghost = 0, back = 0, lr;
        pl_inst_t in, lin;
        double alone = -200, alone_off = -200, taken, gone = -200;
        char what[112];
        snprintf(what, sizeof what, "%s: an instrument that sustains", P[i].what);
        check(what, sustained(P[i].kind, &in, &alone));
        taken = take(in, 8, 0, &ghost, &back);
        lr = first_of(P[i].kind, &lin, &alone_off, ok_long);
        if (lr)
            gone = take_off(lin, quick_in, 1);
        printf("%s %02X:%02X: alone %.2f dB, a ghost and back %.2f dB; %02X:%02X let go at 0.8 s: alone %.2f dB, as a "
               "ghost %.2f dB\n", P[i].what, in.cart, in.slot, alone, taken, lin.cart, lin.slot, alone_off, gone);
        snprintf(what, sizeof what, "%s: a 9th key takes the oldest's engine, a ghost, unheard", P[i].what);
        check(what, ghost);
        snprintf(what, sizeof what, "%s: once an engine is free it comes back", P[i].what);
        check(what, back);
        snprintf(what, sizeof what, "%s: where its envelope would be (within 1 dB)", P[i].what);
        check(what, fabs(taken - alone) <= 1.0 && alone > -60);
        if (!lr) {                       /* (the disk has none with a release that long) */
            printf("%s: no instrument heard 0.7 s after its let-go: its release's return not tried\n", P[i].what);
            continue;
        }
        snprintf(what, sizeof what, "%s: let go as a ghost, it comes back in its release (within 1 dB)", P[i].what);
        check(what, fabs(gone - alone_off) <= 1.0);
    }
    check("a Juno-106 pad for the keys", sustained(REC_JUNO, &pad, &r));
    {   /* two keys held over MIDNIGHT RUN at VOICES 8: the song's notes are taken, never the keys' */
        int k, held = 1, most = 0;
        uint32_t s0;
        quiet();
        player_voice_count(8);
        player_play_song(0xA2, 0, 0);
        run(0.5);
        player_key_on(48, pad, 100);
        player_key_on(55, pad, 100);
        s0 = st().steals;
        for (k = 0; k < 200; k++) {
            run(0.1);
            held &= key_on_now(48) && key_on_now(55);
            most = st().voices > most ? st().voices : most;
        }
        printf("MIDNIGHT RUN at 8 voices, two keys held 20 s: %u notes taken, the most voices %d\n",
               st().steals - s0, most);
        check("the keys' held notes taken last: never, with song notes there", held && st().steals > s0);
        check("never more than 8 voices heard", most <= 8);
        player_keys_off();
        player_stop();
    }
    rules_budget(1);
    {   /* 12 keys held: VOICES 8 makes ghosts of 4, VOICES 12 brings them back */
        int k;
        player_state_t a, b;
        quiet();
        for (k = 0; k < 12; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        run(0.5);
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        player_voice_count(8);
        run(0.1);
        a = st();
        player_voice_count(12);
        run(0.1);
        b = st();
        printf("12 keys held: VOICES 8: %d heard, %d ghosts; VOICES 12: %d heard, %d ghosts\n", a.voices, a.ghosts,
               b.voices, b.ghosts);
        check("VOICES 8 with 12 keys held: 8 heard, 4 ghosts", a.voices == 8 && a.ghosts == 4);
        check("VOICES 12 again: all 12 back", b.voices == 12 && b.ghosts == 0);
#ifdef PTFM_DEBUG
        check("the figures: 4 made past a smaller count, 4 back into free engines, 4 a block at most",
              pl_gf.made[GM_COUNT] == 4 && pl_gf.made[GM_TAKEN] == 0 && pl_gf.back_free == 4 && !pl_gf.back_tail &&
              pl_gf.n_most <= PL_RETURNS && pl_gf.fwd_n[1] >= 4 && pl_gf.start_n[1] == 4 && pl_gf.wait_most == 0);
#endif
        player_keys_off();
    }
    rules_budget(0);
    {   /* 20 keys held: past the count every note a ghost (the pool's 16-note cap gone) */
        int k;
        player_state_t a, b;
        quiet();
        for (k = 0; k < 20; k++) {
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
            run(0.05);
        }
        run(0.5);
        a = st();
        player_keys_off();
        quiet();
        player_voice_count(8);
        for (k = 0; k < 20; k++) {
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
            run(0.05);
        }
        run(0.5);
        b = st();
        printf("20 keys held: VOICES 12: %d heard, %d ghosts; VOICES 8: %d heard, %d ghosts\n", a.voices, a.ghosts,
               b.voices, b.ghosts);
        check("20 keys at VOICES 12: 12 heard, 8 ghosts", a.voices == 12 && a.ghosts == 8);
        check("20 keys at VOICES 8: 8 heard, 12 ghosts", b.voices == 8 && b.ghosts == 12);
        player_keys_off();
    }
    {   /* the keys first, whatever their level: 8 keys held at VOICES 8 with a song playing, every key heard and the
         * song's notes ghosts; 2 more keys make ghosts of the 2 oldest; one let go, the key's ghost made last comes back,
         * not the other's, not the song's; all let go, the song is heard again */
        int k, song, all = 1, none = 1, most = 0, ghosts = 0, songs = 0;
        quiet();
        player_voice_count(8);
        player_play_song(0xA2, 0, 0);
        run(0.5);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 20);   /* (quiet ones: the keys win however quiet) */
        run(0.1);
        for (k = 0; k < 50; k++) {
            uint32_t g;
            run(0.1);
            all &= keys_heard(&song) == 8;
            none &= song == 0;
            most = st().voices > most ? st().voices : most;
            ghosts = st().ghosts > ghosts ? st().ghosts : ghosts;
            for (g = 0; g < pl_gn; g++)
                songs += pl_v[pl_gl[g]].use == PV_SONG;
        }
        printf("MIDNIGHT RUN at 8 voices, 8 quiet keys held 5 s: keys always heard %d, song never heard %d, the most "
               "voices %d, the most held ghosts %d, songs' ghosts seen %d\n", all, none, most, ghosts, songs);
        check("8 keys at VOICES 8: every key heard, the song's notes ghosts", all && none && most <= 8 && songs > 0);
        player_key_on(80, pad, 20);
        player_key_on(82, pad, 20);
        run(0.2);
        int k1 = keys_heard(&song), oldest = key_on_now(48) || key_on_now(50);
        check("10 keys: 8 heard, the 2 oldest ghosts", k1 == 8 && !oldest && key_voice(48) && key_voice(50) &&
              key_voice(48)->gh == GH_GHOST && key_voice(50)->gh == GH_GHOST);
        player_key_off(60);
        run(2.5);                        /* (its tail rung out: an engine free) */
        int k2 = keys_heard(&song), s2 = song;
        printf("10 keys, one let go: %d keys heard, %d of the song's notes; 48 heard %d, 50 heard %d\n", k2, s2,
               key_on_now(48), key_on_now(50));
        check("one let go: a key's ghost comes back, not the song's", k2 == 8 && s2 == 0 &&
              (key_on_now(48) || key_on_now(50)));
        check("... the one made a ghost last first (50's, not 48's)", key_on_now(50) && !key_on_now(48));
        player_keys_off();
        run(3);
        keys_heard(&song);
        check("all let go: the song heard again", song > 0);
        player_stop();
    }
    rules_budget(1);
    {   /* 4 a block: VOICES 8, 24 keys held (8 sounding, 16 ghosts); the 8 sounding cut at once: the 4 ghosts made last
         * come back in the next block, the 4 before them in the one after */
        int k, ok = 1, a, b, c;
        pl_voice_t *g[8];
        quiet();
        player_voice_count(8);
        for (k = 0; k < 24; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        a = st().ghosts;
        for (k = 0; k < 8; k++)          /* (the last 8 made, the last first) */
            g[k] = &pl_v[pl_gl[pl_gn - 1u - (uint32_t)k]];
        for (k = 0; k < (int)PL_NE; k++)
            if (pl_e[k].voice != PL_NO_VOICE)
                pl_v_cut(&pl_v[pl_e[k].voice]);
        blocks(1);
        b = st().ghosts;
        for (k = 0; k < 8; k++)
            ok &= (g[k]->place != PL_NO_ENG) == (k < 4);
        blocks(1);
        c = st().ghosts;
        for (k = 0; k < 8; k++)
            ok &= g[k]->place != PL_NO_ENG;
        printf("24 keys held at VOICES 8: %d ghosts; the 8 sounding cut: %d ghosts a block on, %d two blocks on\n", a, b,
               c);
        check("4 come back a block, the ones made last first", a == 16 && b == 12 && c == 8 && ok);
        player_keys_off();
    }
    rules_budget(0);
    {   /* the FM-1's budget (PL_BUDGET: 44 units of about 1 us, each weight its work's mean measured there): the same 24
         * keys at VOICES 8, the 8 sounding cut at once: a block begins one return at most (its checks and its start
         * overdraw the budget), the ones made last first, their work going on in the blocks after; all 8 back within
         * 60 blocks */
        int k, b, most = 0, order = 1, all = -1, was = 0;
        pl_voice_t *g[8];
        quiet();
        player_voice_count(8);
        for (k = 0; k < 24; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        for (k = 0; k < 8; k++)
            g[k] = &pl_v[pl_gl[pl_gn - 1u - (uint32_t)k]];
        for (k = 0; k < (int)PL_NE; k++)
            if (pl_e[k].voice != PL_NO_VOICE)
                pl_v_cut(&pl_v[pl_e[k].voice]);
        for (b = 1; b <= 60 && all < 0; b++) {
            int n = 0, back = 0;
            blocks(1);
            for (k = 0; k < 8; k++) {   /* (begun: under way or back; in their order, the one made last first) */
                int on = g[k]->gh == GH_BACK || g[k]->place != PL_NO_ENG;
                order &= !on || k == 0 || g[k - 1]->gh == GH_BACK || g[k - 1]->place != PL_NO_ENG;
                n += on;
                back += g[k]->place != PL_NO_ENG;
            }
            if (n - was > most)
                most = n - was;
            was = n;
            if (back == 8)
                all = b;
        }
        printf("the FM-1's budget (%d units a block): the 8 sounding cut, all 8 back in %d blocks, %d begun in a block at "
               "most\n", (int)PL_BUDGET, all, most);
        check("the FM-1's budget: a block begins one return at most, the ones made last first, all back within 60 blocks",
              most == 1 && order && all > 0);
        player_keys_off();
    }
    {   /* the load guard (pl_heavy): the same 24 keys at VOICES 8 (a Juno-106 pad: 8 notes in core 1's engines, 8 x 4 x
         * 64 = 2,048 for the half); a 25th key in a half of its own adds its start and its take's fade (115 + 64); with
         * every half heavy (the cap under any count), the 8 sounding cut at once: no ghost begins its return however long,
         * the walk held each block; the cap back, all 8 back as with the FM-1's budget */
        int k, b, all = -1, none = 1, at, over;
        uint32_t g0;
        quiet();
        player_voice_count(8);
        for (k = 0; k < 24; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        pl_load_cap = 2048;
        at = !pl_heavy();
        pl_load_cap = 2047;
        over = pl_heavy();
        pl_load_cap = -1;
        player_key_on(84, pad, 100);
        run(256.0 / 44100);
        check("the load guard: core 1's engines for the half by their kind (8 Juno-106 notes: 2,048), a note started "
              "and its take's fade (179)", at && over && pl_h_load == PL_LOAD_NOTE + PL_LOAD_FADE);
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        for (k = 0; k < (int)PL_NE; k++)
            if (pl_e[k].voice != PL_NO_VOICE)
                pl_v_cut(&pl_v[pl_e[k].voice]);
        g0 = pl_gn;
        for (b = 0; b < 60; b++) {
            blocks(1);
            for (k = 0; k < (int)pl_gn; k++)
                none &= pl_v[pl_gl[k]].gh != GH_BACK;
        }
        none &= st().voices == 0 && pl_gn == g0 && g0 == 17;
#ifdef PTFM_DEBUG
        none &= pl_gf.held == 60 && !pl_gf.blocks && !pl_gf.back_free;
#endif
        pl_load_cap = PL_LOAD_CAP;
        for (b = 1; b <= 60 && all < 0; b++) {
            int way = 0;
            blocks(1);
            for (k = 0; k < (int)pl_gn; k++)
                way += pl_v[pl_gl[k]].gh == GH_BACK;
            if (st().voices == 8 && !way && pl_gn == g0 - 8u)
                all = b;
        }
        printf("the load guard: every half heavy, 60 blocks with no return begun %d; the cap back, 8 back in %d blocks\n",
               none, all);
        check("the load guard: no return begins in a heavy half; the cap back, the ghosts come back", none && all > 0);
        player_keys_off();
    }
    {   /* the load guard's own split (specs/ui/screens/settings.md#coming-back): VOICES 12, 12 keys of the Juno-106 pad
         * (12 x 64 = 768 a block). Without core 2 all are core 1's: 4 x 768 = 3,072 for the half. While core 2 takes
         * the half's voices (pl_c2t), the top engines go to it while its share and the allowance (PL_LOAD_C2, 128) stay
         * under the rest's: 4 (256 + 128 < 512; a fifth, 320 + 128 against 448, not), so core 1 counts 4 x 512 = 2,048
         * and core 2 4 x (256 + 128) = 1,536: the busier, core 1's, decides. The debug switch's fixed split counts as
         * before it moved: at VOICES 12 every engine core 1's (3,072), core 2 or not; at VOICES 16, 16 keys, the first
         * 10 (2,560), the moving one's 6 on core 2 (2 x 448 + 128 = 1,024, not under) the same here */
        int k, alone, with, fixed, f16, m16, was = pl_c2t;
        quiet();
        player_voice_count(12);
        for (k = 0; k < 12; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        pl_c2t = 0;
        pl_load_cap = 3072;
        alone = !pl_heavy();
        pl_load_cap = 3071;
        alone &= pl_heavy();
        pl_c2t = 1;
        pl_load_cap = 2048;
        with = !pl_heavy();
        pl_load_cap = 2047;
        with &= pl_heavy();
        pl_split_how = SPLIT_FIXED;
        pl_load_cap = 3072;
        fixed = !pl_heavy();
        pl_load_cap = 3071;
        fixed &= pl_heavy();
        player_keys_off();
        pool_empty();                    /* (16 by hand: no core 2 here to take VOICES 16's halves) */
        pl_nv_apply(16);
        for (k = 0, f16 = 1; k < 16; k++)
            f16 &= note_at(pad, 40u + (uint32_t)k, (uint32_t)k) != 0;
        pl_h_load = 0;
        pl_c2t = 1;
        pl_load_cap = 2560;
        f16 &= !pl_heavy();
        pl_load_cap = 2559;
        f16 &= pl_heavy();
        pl_split_how = SPLIT_MOVES;
        m16 = pl_heavy();
        pl_load_cap = 2560;
        m16 &= !pl_heavy();
        pl_load_cap = PL_LOAD_CAP;
        pl_c2t = (uint8_t)was;
        printf("the load guard's split: 12 Juno-106 notes, core 1 alone %d, with core 2 %d, fixed %d; 16, fixed %d, "
               "moving %d\n", alone, with, fixed, f16, m16);
        check("the load guard's split: VOICES 12 with core 2 counts core 1's 8 (2,048, not 3,072 as alone or fixed), "
              "VOICES 16 its 10", alone && with && fixed && f16 && m16);
        pool_empty();
    }
    {   /* the list full: 32 keys held at VOICES 8 (8 sounding, 24 ghosts); a 33rd takes the oldest's engine and its ghost
         * takes the place of the one a ghost longest; a song's note then (the keys hold every engine, the list full of
         * the keys' held ghosts) ranks below every ghost: not kept */
        int k, gone, last, songv = 0;
        pl_voice_t *first, *oldest = 0;
        uint32_t age0, c;
        quiet();
        player_voice_count(8);
        for (k = 0; k < 32; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        printf("32 keys held at VOICES 8: %d heard, %d ghosts\n", st().voices, st().ghosts);
        check("32 keys at VOICES 8: 8 heard, 24 ghosts", st().voices == 8 && st().ghosts == 24 && pl_gn == 24);
        first = &pl_v[pl_gl[0]];
        age0 = first->age;
        for (c = 0; c < pl_nv; c++)       /* (the oldest key sounding: the 33rd's take) */
            if (pl_e[c].voice != PL_NO_VOICE && (!oldest || pl_v[pl_e[c].voice].age < oldest->age))
                oldest = &pl_v[pl_e[c].voice];
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        player_key_on(110, pad, 100);
        run(0.1);
        gone = first->use == PV_FREE || first->age != age0;
        last = pl_gn == 24 && oldest && &pl_v[pl_gl[pl_gn - 1u]] == oldest && key_on_now(110);
        check("a 33rd: the oldest sounding a ghost, made last; the one a ghost longest gone", gone && last);
#ifdef PTFM_DEBUG
        check("the figures: the 33rd's take a ghost made, the one a ghost longest dropped (the list full: a key's held)",
              pl_gf.made[GM_TAKEN] == 1 && pl_gf.gone[GO_FULL_KEY][0] == 1 && pl_gf.gn_most == 24);
        gf_zero();
#endif
        player_play_song(0xA2, 0, 0);
        for (k = 0; k < 20; k++) {
            run(0.1);
            for (c = 0; c < PL_NV; c++)
                songv += pl_v[c].use == PV_SONG;
        }
        check("a song's note under every ghost (the keys' all): not kept", songv == 0 && st().ghosts == 24);
#ifdef PTFM_DEBUG
        check("the figures: the song's notes not kept (the list full, a song's held), no ghost made or gone",
              pl_gf.full_new[GR_SONG] > 0 && !pl_gf.made[GM_BORN] && !pl_gf.gone[GO_FULL_KEY][0]);
#endif
        player_stop();
        player_keys_off();
    }
    {   /* a take: VOICES 8, 8 keys held; a 9th at a half's start: in that half's first block the taken note's last block
         * is rendered in its engine (9 engine blocks: 8 notes and the fade) and the 9th sounds there, as it does started
         * in a free engine (its first block's peak printed beside that one's) */
        int k;
        uint32_t b0, b1;
        double p0 = 0, p1 = 0;
        pl_voice_t *v;
        quiet();
        player_key_on(70, pad, 100);     /* (the 9th's first block in a free engine: the reference) */
        blocks(1);
        v = key_voice(70);
        if (v && v->place != PL_NO_ENG)
            for (k = 0; k < (int)PL_SCOPE; k++)
                p0 = fabs((double)pl_scope[v->place][k]) > p0 ? fabs((double)pl_scope[v->place][k]) : p0;
        quiet();
        player_voice_count(8);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        run(0.5);
        player_key_on(70, pad, 100);
        b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        blocks(1);
        b1 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        v = key_voice(70);
        if (v && v->place != PL_NO_ENG)
            for (k = 0; k < (int)PL_SCOPE; k++)
                p1 = fabs((double)pl_scope[v->place][k]) > p1 ? fabs((double)pl_scope[v->place][k]) : p1;
        printf("a take at VOICES 8: %u engine blocks in its block; the new note's first block peaks at %.0f (in a free "
               "engine %.0f); 48 a ghost %d\n", b1 - b0, p1, p0, key_voice(48) && key_voice(48)->gh == GH_GHOST);
        check("a take: the note taken faded out in its engine in that block", b1 - b0 == 9u && key_voice(48) &&
              key_voice(48)->gh == GH_GHOST);
        check("... the new note sounding from that block, as in a free engine", p0 > 0 && p1 > 0 && v &&
              v->place != PL_NO_ENG);
        player_keys_off();
    }
    {   /* a take's new note starts in an engine as its taken note leaves it, cut: a slow-attack Juno-106 pad, VOICES 8,
         * 8 keys held at their sustain, a 9th: its envelope after its first block an attack step or two from 0, as the
         * same note's in a free engine (the block's ticks may differ by one), never the taken note's level carried on */
        pl_inst_t slow;
        int k, found = first_of(REC_JUNO, &slow, &r, ok_slow);
        uint32_t e0, e1, old;
        quiet();
        player_key_on(70, slow, 100);
        blocks(1);
        e0 = juno_env_of(70);
        quiet();
        player_voice_count(8);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), slow, 100);
        run(3.5);
        old = juno_env_of(48);
        player_key_on(70, slow, 100);
        blocks(1);
        e1 = juno_env_of(70);
        printf("a slow-attack pad %02X:%02X taken at VOICES 8: the taken note's envelope %u; the new note's after its "
               "first block %u (in a free engine %u)\n", slow.cart, slow.slot, old, e1, e0);
        check("a take: the new note's attack from a cut engine, as in a free engine", found && e0 > 0 && e1 > 0 &&
              e1 <= 2u * e0 && e0 <= 2u * e1 && old > 8u * e1);
        player_keys_off();
    }
    rules_budget(1);
    {   /* every engine a key's note coming back (VOICES 8, 16 keys: 8 ghosts, the 8 sounding cut, all 8 back in two
         * blocks, fading in): a song's note there starts as a ghost (pl_alloc gives it a voice, to be born a ghost) */
        int k, back = 1;
        pl_voice_t *v;
        uint32_t c, born;
        quiet();
        player_voice_count(8);
        for (k = 0; k < 16; k++) {
            player_key_on((uint8_t)(36 + 2 * k), pad, 100);
            run(0.02);
        }
        run(0.3);
        for (c = 0; c < PL_NE; c++)
            if (pl_e[c].voice != PL_NO_VOICE)
                pl_v_cut(&pl_v[pl_e[c].voice]);
        blocks(2);
        for (c = 0; c < pl_nv; c++)
            back &= pl_e[c].voice != PL_NO_VOICE && pl_v[pl_e[c].voice].use == PV_KEY && pl_v[pl_e[c].voice].back > 0;
        v = pl_alloc(0, EN_DX7);
        born = pl_al_born;
        pl_al_c = PL_NO_ENG;             /* (no note started: the voice stays free) */
        pl_al_born = 0;
        check("every engine a key's note coming back: a song's note starts as a ghost", back && pl_gn == 0 && v && born);
        player_keys_off();
    }
    rules_budget(0);
    {   /* a TB-303 never a ghost: ACID TEST at VOICES 8 with 8 keys held over it (the song's notes taken or starting
         * as ghosts: its TB-303's are not played then); pool_check fails on a TB-303 ghost */
        int k, tb_before, tb_during = 0, ghosts = 0;
        quiet();
        player_voice_count(8);
        player_play_song(0xA4, 0, 0);
        run(2.0);
        tb_before = in_use(EN_TB);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        for (k = 0; k < 40; k++) {
            run(0.1);
            tb_during += in_use(EN_TB);
            ghosts = st().ghosts > ghosts ? st().ghosts : ghosts;
        }
        printf("ACID TEST at VOICES 8: TB-303 voices before %d; with 8 keys held, TB-303 voices %d over 40 looks, "
               "ghosts at most %d\n", tb_before, tb_during, ghosts);
        check("a TB-303's note never a ghost (taken: it goes; starting as one: not played)", tb_before > 0 &&
              tb_during == 0);
        player_keys_off();
        player_stop();
    }
    {   /* a song's ghost let go by its track's next note: MIDNIGHT RUN at VOICES 8, 8 keys held (every song note a
         * ghost); a held song ghost of the block before, a ghost still in this one but let go there: its let-go's block
         * kept, its end 10 s on at the latest; 10.5 s on, gone */
        int k, let = 0, ok = 0;
        uint32_t idx = 0, age = 0, prev_n = 0, g, j, prev[PL_GHOSTS], prev_age[PL_GHOSTS];
        quiet();
        player_voice_count(8);
        player_play_song(0xA2, 0, 0);
        run(1.0);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        for (k = 0; k < 4000 && !let; k++) {
            blocks(1);
            for (j = 0; j < prev_n && !let; j++) {
                const pl_voice_t *v = &pl_v[prev[j]];
                if (v->age == prev_age[j] && v->gh == GH_GHOST && !v->held) {
                    let = 1;
                    idx = prev[j];
                    age = v->age;
                    ok = v->t_off == pl_blk - 1u && v->t_end != PL_NEVER &&
                         (int32_t)(v->t_end - v->t_off) <= (int32_t)PL_GH_KEEP;
                }
            }
            for (g = prev_n = 0; g < pl_gn; g++)
                if (pl_v[pl_gl[g]].use == PV_SONG && pl_v[pl_gl[g]].held) {
                    prev[prev_n] = pl_gl[g];
                    prev_age[prev_n++] = pl_v[pl_gl[g]].age;
                }
        }
        printf("MIDNIGHT RUN, a song's held ghost let go by its track: %d (after %d blocks)\n", let, k);
        check("a song's ghost let go by its track's next note, its let-go's time kept", let && ok);
        run(10.5);
        check("... 10.5 s on, gone", pl_v[idx].age != age || pl_v[idx].use == PV_FREE);
        player_keys_off();
        player_stop();
    }
    {   /* a held sampler note looping FORWARD, its LOOP END turned under LOOP START (a parameter command): it loops no
         * more and plays on to END, its place never running away (the sanitizers' build: no overflow) */
        pl_inst_t smp;
        unsigned k;
        pl_voice_t *v = 0;
        int looped = 0, ran = 0, still = 0;
        sustained(REC_SAMPLER, &smp, &r);
        quiet();
        player_key_on(60, smp, 100);
        run(0.2);
        for (k = 0; k < PL_NV; k++)
            if (pl_v[k].use == PV_KEY && pl_v[k].who == 60 && pl_v[k].engine == EN_SAMP && pl_v[k].place != PL_NO_ENG)
                v = &pl_v[k];
        if (v) {
            samp_voice_t *sm = &PL_E(v)->u.sm;
            int32_t len = sm->s.length;
            sm->s.loop = SAMP_LOOP_FORWARD;              /* (a loop with room, whatever the instrument's) */
            sm->s.loop_start = len / 4;
            sm->s.loop_end = len / 2;
            sm->looping = 1;
            run(0.2);
            looped = sm->looping;
            pl_param_live(v, 0x43u, 250u);               /* LOOP START near the sample's end */
            pl_param_live(v, 0x44u, 1u);                 /* LOOP END at its start */
            still = sm->looping;
            for (k = 0; k < 40 && sm->on; k++) {
                run(0.1);
                ran |= sm->pos < 0 || sm->pos > len;
            }
        }
        printf("a sampler note looping, its LOOP END turned under LOOP START: looping %d, then %d; its place out of the "
               "sample %d\n", looped, still, ran);
        check("a sampler's loop with no room left: it loops no more", v && looped && !still && !ran);
        player_keys_off();
        run(0.5);
    }
    {   /* a held ghost that ends by itself: a one-shot sample (A0:00, LOOP OFF) and a CHIP note (F9:18) */
        pl_inst_t smp = {0xA0, 0x00}, chip = {0xF9, 0x18};
        own_end(smp, "a one-shot sample");
        own_end(chip, "a CHIP note");
    }
    rules_budget(1);
    {   /* a held ghost comes back into a release tail's engine, none free: VOICES 8, 10 keys of a Juno-106 heard on after
         * its let-go (48 and 50 ghosts); 52 let go: in the next block its tail fades out in its engine (8 engine
         * blocks: 7 notes and the fade), 50, the ghost made last, its return under way there (held for it, still a
         * ghost), and in the block after 50 comes back there, 48 still a ghost, nothing counted taken; then 48 let go
         * (a ghost let go) and 54: 54's tail rings on, 48 a ghost (a ghost let go takes only a free engine) */
        pl_inst_t lng;
        int k, back, still, wait;
        uint32_t c52, c54, b0, b1, s0;
        pl_voice_t *g48, *g50, *v54;
        check("a Juno-106 heard on after its let-go", first_of(REC_JUNO, &lng, &r, ok_long));
        quiet();
        player_voice_count(8);
        for (k = 0; k < 10; k++) {
            player_key_on((uint8_t)(48 + 2 * k), lng, 100);
            run(0.02);
        }
        run(0.3);
        g48 = key_voice(48);
        g50 = key_voice(50);
        c52 = key_voice(52) ? key_voice(52)->place : PL_NO_ENG;
        s0 = pl_steals;
        player_key_off(52);
        b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        blocks(1);
        b1 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        wait = g50 && c52 != PL_NO_ENG && g50->gh == GH_BACK && pl_e[c52].wait == (uint8_t)(g50 - pl_v) &&
               pl_e[c52].fade == PL_NO_FADE && !key_voice(52);
        blocks(1);
        back = g48 && g50 && c52 != PL_NO_ENG && g50->place == c52 && g50->back && g48->gh == GH_GHOST && !key_voice(52);
        printf("a tail's engine for a held ghost: 50's return there %d, back in 52's engine %d, 48 a ghost %d, "
               "%u engine blocks, %u taken\n", wait, g50 && g50->place == c52, g48 && g48->gh == GH_GHOST, b1 - b0,
               pl_steals - s0);
        check("a held ghost back into a tail's engine: the tail's fade in its block, the return in the next", wait &&
              back && b1 - b0 == 8u && pl_steals == s0);
        player_key_off(48);
        blocks(1);
        v54 = key_voice(54);
        c54 = v54 ? v54->place : PL_NO_ENG;
        player_key_off(54);
        blocks(2);
        still = g48->gh == GH_GHOST && !g48->held && v54 && v54->place == c54 && !v54->held && pl_e_sounds(&pl_e[c54]);
        check("... a ghost let go takes no tail's engine (54's rings on)", still);
        player_keys_off();
    }
    rules_budget(0);
    {   /* a note silent at its last block's end gives its engine up at once, no fade rendered: VOICES 8, 8 keys held,
         * 48's gain 0 at its last block's end; a 9th takes 48's engine: 8 engine blocks in that block, 48 a ghost */
        int k;
        uint32_t b0, b1;
        quiet();
        player_voice_count(8);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        run(0.5);
        key_voice(48)->g_last = 0;
        player_key_on(70, pad, 100);
        b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        blocks(1);
        b1 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        check("a silent note taken: no fade rendered (8 engine blocks), a ghost", b1 - b0 == 8u && key_voice(48) &&
              key_voice(48)->gh == GH_GHOST && key_on_now(70));
        player_keys_off();
    }
    {   /* DJ (pl_choose on a pool set by hand, VOICES 8, the crossfader full left: the right deck not heard): engines
         * 0-3 the left deck's notes (0 and 1 release tails, 1 the quieter), 4-7 the right's (4 a tail). A held ghost of
         * the right deck takes only the right's tails (none: none at all); one of the left takes the deck lower in the
         * mix's (the right's) first, then its own, the quietest */
        void *mem = malloc(player_dj_mem_size());
        pl_voice_t *t[8];
        int k, ok;
        quiet();
        player_voice_count(8);
        run(0.05);
        pl_d2_set(mem);
        pl_d2->dj.on = 1;
        pl_dj.xf8 = 0;
        pl_dj.g_last[0] = 4096;
        pl_dj.g_last[1] = 0;
        for (k = 0; k < 8; k++) {
            t[k] = pl_v_spare();
            memset(t[k], 0, sizeof *t[k]);
            t[k]->use = PV_SONG;
            t[k]->engine = EN_DX7;
            t[k]->who = (uint8_t)((k < 4 ? 0 : 16) + k);
            t[k]->held = k != 0 && k != 1 && k != 4;
            t[k]->peak = (int16_t)(k == 1 ? 50 : 500);
            t[k]->m_last = 4096;
            t[k]->g_last = 4096;
            t[k]->age = pl_age_seen - 10u + (uint32_t)k;
            t[k]->t_off = t[k]->held ? FWD_HELD : pl_blk;
            pl_e[k].kind = EN_DX7;
            pl_bind(t[k], (uint32_t)k);
        }
        ok = pl_choose(1, 17, 1) == t[4];
        ok &= pl_choose(0, 2, 1) == t[4];
        t[4]->held = 1;
        t[4]->t_off = FWD_HELD;
        ok &= pl_choose(1, 17, 1) == PL_DEAF;
        ok &= pl_choose(0, 2, 1) == t[1];
        ok &= pl_choose(-1, -1, 1) == t[1];
        check("DJ: a faded-out deck's ghost takes its own deck's tails alone; the heard deck's the lower's first", ok);
        {   /* the returns there: two held ghosts of a DX7, the left deck's made first, the right's last (first in the
             * order); the right's finds no tail of its own deck and is passed by, the left's return takes its deck's
             * quietest tail's engine (1), to begin there once that tail's fade is rendered (both still in the list) */
            pl_voice_t *gh[2];
            for (k = 0; k < 2; k++) {
                gh[k] = pl_v_spare();
                memset(gh[k], 0, sizeof *gh[k]);
                gh[k]->use = PV_SONG;
                gh[k]->engine = EN_DX7;
                gh[k]->inst = (pl_inst_t){0xF0, 0x00};
                gh[k]->who = (uint8_t)(k ? 16 + 3 : 3);
                gh[k]->held = 1;
                gh[k]->place = PL_NO_ENG;
                gh[k]->m_last = 4096;
                gh[k]->age = pl_age_seen - 20u;
                gh[k]->t_on = pl_blk;
                gh[k]->t_off = FWD_HELD;
                gh[k]->t_end = PL_NEVER;
                pl_gh_add(gh[k]);
            }
            pl_returns();
            ok = gh[0]->gh == GH_BACK && pl_e[1].wait == (uint8_t)(gh[0] - pl_v) && pl_e[1].fade != PL_NO_FADE &&
                 gh[1]->gh == GH_GHOST && pl_gn == 2 && t[1]->use == PV_FREE;
            check("... the faded-out deck's ghost passed by, the heard deck's back into its own tail's engine", ok);
#ifdef PTFM_DEBUG
            {   /* the figures' look at the waits: the heard deck's return stamped (it began), the faded-out deck's ghost
                 * not (no free engine, no tail of its own deck: the other deck's tails are no chance for it) */
                uint32_t vo = dbg_half[DS_VOICES];
                pl_gf_could();
                dbg_half[DS_VOICES] = vo;        /* (it takes its time out of VOICES, which this call is not in) */
                check("the figures: a faded-out deck's ghost's wait not begun by the other deck's tails",
                      (pl_gf_vcan >> (gh[0] - pl_v) & 1u) && !(pl_gf_vcan >> (gh[1] - pl_v) & 1u));
            }
#endif
            pl_v_free(gh[0]);
            pl_e[1].fade = PL_NO_FADE;
            pl_nfd = 0;
            pl_v_free(gh[1]);
        }
        for (k = 0; k < 8; k++)
            pl_v_free(t[k]);
        pl_d2->dj.on = 0;
        pl_d2_set(0);
        free(mem);
        run(0.05);
    }
    rules_budget(1);
    {   /* a mute makes ghosts: MIDNIGHT RUN at VOICES 12, every track muted: a block on no note of it in an engine (but
         * a TB-303's), its held notes ghosts; 2 s on, nothing of it rendered (its new notes ghosts, pool_check: none in
         * an engine); heard again, its held ghosts back within 3 blocks, the song heard */
        int held = 0, tb = 0, other = 0, born = 0, back;
        uint32_t c, b0, b1, a0;
        quiet();
        player_play_song(0xA2, 0, 0);
        run(2.0);
        for (c = 0; c < PL_NV; c++)
            held += pl_v[c].use == PV_SONG && pl_v[c].held && pl_v[c].place != PL_NO_ENG;
#ifdef PTFM_DEBUG
        gf_zero();
#endif
        player_mutes(0xFFFF);
        blocks(1);
        for (c = 0; c < PL_NE; c++)
            if (pl_e[c].voice != PL_NO_VOICE) {
                tb += pl_v[pl_e[c].voice].engine == EN_TB;
                other += pl_v[pl_e[c].voice].engine != EN_TB;
            }
        printf("MIDNIGHT RUN, every track muted: %d held notes then %d held ghosts a block on, %d TB-303 notes, %d "
               "others in engines\n", held, st().ghosts, tb, other);
        check("a mute: a block on its notes ghosts, no engine but a TB-303's", held > 0 && st().ghosts >= held &&
              !other);
#ifdef PTFM_DEBUG
        check("the figures: the mute's ghosts made by it, held ones at least as many as the held notes",
              pl_gf.made[GM_MUTED] >= (uint32_t)held && pl_gf.made[GM_TAKEN] == 0);
#endif
        a0 = pl_age;
        b0 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        run(2.0);
        b1 = pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3];
        for (c = 0; c < pl_gn; c++)
            born += pl_v[pl_gl[c]].age > a0;
        held = st().ghosts;
        printf("... 2 s muted: %u engine blocks, %d ghosts started since, %d held ghosts\n", b1 - b0, born, held);
        check("... its new notes ghosts: nothing rendered (but a TB-303's)", (tb || b1 == b0) && born > 0);
#ifdef PTFM_DEBUG
        check("the figures: its new notes started as ghosts, muted; none back while muted",
              pl_gf.made[GM_BORN_MUTED] > 0 && !pl_gf.back_free && !pl_gf.back_tail);
        gf_zero();
#endif
        player_mutes(0);
        blocks(3);
        back = st().ghosts;
        run(0.5);
        check("heard again: its held ghosts back within 3 blocks, the song heard", held > 0 && back == 0 &&
              st().voices > 0);
#ifdef PTFM_DEBUG
        check("the figures: heard again, its ghosts back after the mute, into free engines",
              pl_gf.back_mute > 0 && pl_gf.back_free >= pl_gf.back_mute && !pl_gf.back_tail);
#endif
        player_stop();
    }
    rules_budget(0);
    {   /* a sampler note made a ghost by a mute: the first demo song found holding a sampler note (looked at every 0.1
         * s for 10 s), every track muted: a block on it is a ghost, its place kept at the next block's (the clock never
         * behind it: pool_check); heard again, it comes back at its place walked to then */
        int song, found = 0, back = 0, k;
        uint32_t c, age = 0;
        pl_voice_t *g = 0;
        for (song = 0xA0; song <= 0xA5 && !found; song++) {
            quiet();
            g = 0;
            player_mutes(0);
            player_play_song((uint8_t)song, 0, 0);
            for (k = 0; k < 100 && !g; k++) {
                run(0.1);
                for (c = 0; c < PL_NV && !g; c++)
                    if (pl_v[c].use == PV_SONG && pl_v[c].engine == EN_SAMP && pl_v[c].held &&
                        pl_v[c].place != PL_NO_ENG)
                        g = &pl_v[c];
            }
            if (!g) {
                player_stop();
                continue;
            }
            age = g->age;
            player_mutes(0xFFFF);
            blocks(1);
            found = g->age == age && g->gh == GH_GHOST && g->held ? song : 0;
        }
        if (found) {
            blocks(8);
            player_mutes(0);
            blocks(3);
            back = g->age == age && g->held && g->place != PL_NO_ENG;
            player_stop();
        }
        printf("a sampler note made a ghost by a mute: song %02X, back once heard %d\n", found, back);
        check("a sampler note made a ghost by a mute, back at its place once heard", found && back);
    }
    {   /* a take with DUCK's source muted (a pool set by hand, VOICES 8): engines 0-7 held song notes of tracks 0-7,
         * track 0's muted (silent at its last block's end) and DUCK's source, its peak 500; track 1's peak 100, the
         * others' 1000: a key's note takes track 1's (DUCK's source's muted note ranks as heard); with no source, track
         * 0's (a muted note counts as silent) */
        pl_voice_t *t[8];
        int k, ok;
        quiet();
        player_voice_count(8);
        run(0.05);
        for (k = 0; k < 8; k++) {
            t[k] = pl_v_spare();
            memset(t[k], 0, sizeof *t[k]);
            t[k]->use = PV_SONG;
            t[k]->engine = EN_DX7;
            t[k]->who = (uint8_t)k;
            t[k]->held = 1;
            t[k]->peak = (int16_t)(k == 0 ? 500 : k == 1 ? 100 : 1000);
            t[k]->m_last = k ? 4096 : 0;
            t[k]->g_last = 4096;
            t[k]->age = pl_age_seen - 10u + (uint32_t)k;
            t[k]->t_off = FWD_HELD;
            pl_e[k].kind = EN_DX7;
            pl_bind(t[k], (uint32_t)k);
        }
        pl_ctxs[0].mute = 1;
        pl_ctxs[0].duck = 1;             /* (track 0 + 1) */
        ok = pl_victim(-1, -1, 0) == t[1];
        pl_ctxs[0].duck = 0;
        ok &= pl_victim(-1, -1, 0) == t[0];
        check("a take: DUCK's source's muted note ranks as heard; a muted one as silent", ok);
        for (k = 0; k < 8; k++)
            pl_v_free(t[k]);
        pl_ctxs[0].mute = 0;
        run(0.05);
    }
    {   /* the list full (section 6; ghosts set by hand): 24 held song ghosts of track 1 but the second made, track
         * 0's, muted: a song's held ghost more drops the muted one, not the one a ghost longest; with none muted or let
         * go, a muted one more ranks below them all, not kept; one of them let go: it goes for the muted one */
        pl_voice_t *g[24];
        int k, ok;
        quiet();
        pl_ctxs[0].mute = 1;
        pl_ctxs[0].duck = 0;
        for (k = 0; k < 24; k++) {
            g[k] = pl_v_spare();
            memset(g[k], 0, sizeof *g[k]);
            g[k]->use = PV_SONG;
            g[k]->engine = EN_DX7;
            g[k]->who = (uint8_t)(k == 1 ? 0 : 1);
            g[k]->held = 1;
            g[k]->place = PL_NO_ENG;
            g[k]->m_last = 4096;
            g[k]->age = pl_age_seen - 30u + (uint32_t)k;
            g[k]->t_off = FWD_HELD;
            g[k]->t_end = PL_NEVER;
            pl_gh_add(g[k]);
        }
        ok = pl_gh_room(GR_SONG, FWD_HELD) && g[1]->use == PV_FREE && g[0]->use == PV_SONG && pl_gn == 23;
        g[1] = pl_v_spare();             /* (24 again, none muted) */
        *g[1] = *g[2];
        pl_gh_add(g[1]);
        ok &= !pl_gh_room(GR_MUTED, FWD_HELD) && pl_gn == 24;
        g[5]->held = 0;
        g[5]->t_off = pl_blk;
        ok &= pl_gh_room(GR_MUTED, FWD_HELD) && g[5]->use == PV_FREE && pl_gn == 23;
        check("the list full: a muted ghost goes before the held ones, a ghost let go before it", ok);
        for (k = 0; k < 24; k++)
            if (g[k]->use != PV_FREE)
                pl_v_free(g[k]);
        pl_ctxs[0].mute = 0;
        run(0.05);
    }
    {   /* a muted track's new note through pl_alloc, the list full (ghosts set by hand): 24 held song ghosts of track
         * 1, track 0 muted: its note ranks below them all, not kept, none dropped; two of them track 0's (muted): the
         * one made first goes for it; one let go: it goes first */
        pl_voice_t *g[24], *v;
        int k, ok;
        quiet();
        pl_ctxs[0].mute = 1;
        pl_ctxs[0].duck = 0;
        for (k = 0; k < 24; k++) {
            g[k] = pl_v_spare();
            memset(g[k], 0, sizeof *g[k]);
            g[k]->use = PV_SONG;
            g[k]->engine = EN_DX7;
            g[k]->who = 1;
            g[k]->held = 1;
            g[k]->place = PL_NO_ENG;
            g[k]->m_last = 4096;
            g[k]->age = pl_age_seen - 30u + (uint32_t)k;
            g[k]->t_off = FWD_HELD;
            g[k]->t_end = PL_NEVER;
            pl_gh_add(g[k]);
        }
        v = pl_alloc(0, EN_DX7);
        ok = !v && pl_gn == 24 && g[0]->use == PV_SONG;
        printf("a muted track's note, the list full of held song ghosts: %s (%u ghosts, the first %s)\n",
               v ? "kept" : "not kept", pl_gn, g[0]->use == PV_SONG ? "kept" : "dropped");
        g[3]->who = 0;
        g[7]->who = 0;
        v = pl_alloc(0, EN_DX7);
        ok &= v && pl_al_born && g[3]->use == PV_FREE && g[7]->use == PV_SONG && pl_gn == 23;
        pl_al_born = 0;
        g[3] = pl_v_spare();             /* (24 again) */
        *g[3] = *g[2];
        pl_gh_add(g[3]);
        g[9]->held = 0;
        g[9]->t_off = pl_blk;
        v = pl_alloc(0, EN_DX7);
        ok &= v && pl_al_born && g[9]->use == PV_FREE && g[7]->use == PV_SONG && pl_gn == 23;
        pl_al_born = 0;
        check("a muted track's new note, the list full: below the held, the first muted goes for it, a let-go first",
              ok);
        for (k = 0; k < 24; k++)
            if (g[k]->use != PV_FREE)
                pl_v_free(g[k]);
        pl_ctxs[0].mute = 0;
        run(0.05);
    }
    {   /* a take in the block a mute comes in (a pool set by hand, VOICES 8): engines 0-7 held song notes of tracks
         * 0-7, all heard at their last block's end (m_last 4096), track 1's peak 100, the others' 1000; track 0 muted
         * now: the take is track 0's (a muted one as silent), not track 1's */
        pl_voice_t *t[8], *v;
        int k;
        quiet();
        player_voice_count(8);
        run(0.05);
        for (k = 0; k < 8; k++) {
            t[k] = pl_v_spare();
            memset(t[k], 0, sizeof *t[k]);
            t[k]->use = PV_SONG;
            t[k]->engine = EN_DX7;
            t[k]->who = (uint8_t)k;
            t[k]->held = 1;
            t[k]->peak = (int16_t)(k == 1 ? 100 : 1000);
            t[k]->m_last = 4096;
            t[k]->g_last = 4096;
            t[k]->age = pl_age_seen - 10u + (uint32_t)k;
            t[k]->t_off = FWD_HELD;
            pl_e[k].kind = EN_DX7;
            pl_bind(t[k], (uint32_t)k);
        }
        pl_ctxs[0].mute = 1;
        pl_ctxs[0].duck = 0;
        v = pl_victim(-1, -1, 0);
        printf("a take, track 0 muted in this block, heard at its last block's end: track %d's taken\n",
               v ? v->who : -1);
        check("a take in a mute's block: the note muted now counts as silent", v == t[0]);
        pl_ctxs[0].mute = 0;             /* the same with DJ's SWAP: the first memory's replay in the second context */
        pl_swapped = 1;
        pl_ctxs[1].mute = 1;
        pl_ctxs[1].duck = 0;
        v = pl_victim(-1, -1, 0);
        printf("the same, the decks swapped: track %d's taken\n", v ? v->who : -1);
        check("a take in a mute's block, the decks swapped: the note muted now counts as silent", v == t[0]);
        pl_swapped = 0;
        pl_ctxs[1].mute = 0;
        for (k = 0; k < 8; k++)
            pl_v_free(t[k]);
        pl_ctxs[0].mute = 0;
        run(0.05);
    }
    {   /* the returns' work: the budget, a return taken, the drops not counted, the commands at a return, the walk past
         * a ghost that cannot begin, the give-ups, a drop's engine */
        pl_inst_t dxs, dx2, chl, qj, ju = {0xF8, 0x01}, lp, sl;
        double unused;
        int found = sustained(REC_DX7, &dxs, &unused) && first_of(REC_CHIP, &chl, &unused, ok_long) &&
                    first_of(REC_JUNO, &qj, &unused, ok_rel_short) && first_of(REC_SAMPLER, &lp, &unused, ok_loops) &&
                    first_of(REC_JUNO, &sl, &unused, ok_slow);
        check("instruments for the returns' cases (a DX7 that sustains, a CHIP heard 1 s after its let-go, a Juno-106 "
              "silent 0.5 s after it, a sampler that loops, a Juno-106 pad with a slow attack)", found);
        dx2 = dxs;
        dx2.slot = (uint8_t)(dxs.slot ^ 1u);
        never_taken_all();
        if (found) {
            budget_order(dxs);
            tail_returns(dxs);
            taken_return(dxs);
            back_follows(ju);
            RULES(dropped_not_counted(chl, quick_in, qj));
            pars_at_return(dxs, dx2, ju, lp);
            mute_quiet_tail(dxs);
            walk_on(dxs);
            give_ups(dxs);
            quiet_drop(sl);
        }
        pool_empty();
        quiet();
        if (found) {                     /* section 2's rule, case by case, and the player's cases of section 11 */
            pl_inst_t chs, jl;
            int more = sustained(REC_CHIP, &chs, &unused) && first_of(REC_JUNO, &jl, &unused, ok_long);
            check("instruments for section 11's cases (a CHIP that sustains, a Juno-106 heard after its let-go)", more);
            drop_ranks();
            RULES(fade_limit(dxs));
            take_past(pad);
            back_kept(dxs);
            RULES(quiet_held(sl));
            if (more) {
                const pl_inst_t shot = {0xA0, 0x00};   /* (a one-shot sample: LOOP OFF) */
                RULES(release_heard(jl));
                own_tail(dxs);
                RULES(samp_long(lp));
                samp_slid(shot);
                born_unloop(lp, dxs);
                chip_rebuilt(chs, (pl_inst_t){0xF9, 0x18}, dxs);
                take_ended((pl_inst_t){0xF9, 0x18}, shot);
                samp_edge(shot);
                inst_gone(lp);
                song_stopped(pad);
                RULES(track_aba(dxs, dx2));
                born_cannot(lp);
                RULES(c2_lost(dxs, ju, lp, chs));
                d2_moving();
                dj_takes_return(dxs);
                fusion_restart(dxs);
                tap_return(dxs);
                filter_held(shot);
                dj_pass_by(ju);
                loop_end_past(lp);
                RULES(ghost_unlooped(lp));
                RULES(ghost_edit_walk(lp));
                {
                    pl_inst_t tb;
                    if (first_of(REC_TB303, &tb, &unused, ok_any)) {
                        tb_above(pad, tb);
                        tb_after_cut(tb);
                    }
                    else
                        check("an instrument for the TB-303 case (a TB-303)", 0);
                }
                leg_ended(shot);
                tune_cmd();
                leg_ghost_ended(shot);
                rules_song(dxs, dx2, ju, lp, chs);
            }
        }
    }
    printf("voices: %s\n", fails ? "FAILED" : "all as settings.md#voices says");
    return fails != 0;
}
