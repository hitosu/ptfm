/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The forward work every engine gives the same way (specs/proposals/202611121830-ghost-notes.md, section 5): a note
 * taken from its engine (a ghost) comes back into a fresh engine, its note started again, and is worked on there,
 * without its sound, to where its own steps would have left it by now.
 *
 *   int xx_forward(xx_voice_t *v, uint32_t *on, uint32_t *off, int32_t *steps)
 *
 * - v: a note just started (its note on done in a zeroed state, its parameters set, its pitch, level, key as they
 *   stand). Worked on: *on units still to go held from its start, then (*off != FWD_HELD) its note off and *off units
 *   more. What it moves comes out as those units of the engine's own skip step leave it, exactly; what the engine makes
 *   again each unit (the operators' phases, an oscillator's place) may differ, each engine says what.
 * - The units, what the caller gives:
 *   - the DX7, the sampler, the CHIP: blocks of 64 frames, counted on the player's block clock from the note's start
 *     (and from its let-go); their engines' own clocks start at their note on;
 *   - the Juno-106: its firmware loop's ticks, as the player counts them (pl_tick_acc: juno_tick_q16 a block, Q16,
 *     a tick at each overflow). The ticks in blocks [a, b) of the block clock are (b * q >> 16) - (a * q >> 16), q
 *     the increment a block: 64-bit products and shifts, no division; true as long as the block clock and pl_tick_acc
 *     count the same blocks from the same start.
 * - The budget: each unit of work spends its cost from *steps, in the engine's own steps, each the weight of a
 *   stretch read and moved (a stretch: the units a part moves through by one change a unit, or a hold), the LFO's
 *   move, or a note off counted at its real weight (as the stretches it starts). A call with *steps > 0 always gets
 *   on (a stretch at least), so its last unit may take *steps below 0: each engine's header states the lowest a call
 *   leaves (measured by fw/tests/sound/forward_test.c).
 * - Returns 1 when done (*on == 0 and *off is 0 or FWD_HELD), 0 when the budget ran out first (*steps <= 0). Called
 *   again later, it goes on from where it is: what is left is in *on, *off and v, the note off done once (the engine's
 *   own state tells it was). Between calls the time goes on and the caller adds the units gone by: to *on while the
 *   note is held; to *off once let go. A note let go while its return is under way: the caller switches *off from
 *   FWD_HELD to 0, then adds to it. Never add to *off while it is FWD_HELD. An engine may set *on and *off to 0 itself
 *   for a note that does not sound or has ended (the sampler's, the CHIP's): the caller tells a note ended by xx_heard
 *   or the engine's own "active", never by the counts.
 *
 *   int32_t xx_heard(const xx_voice_t *v)
 *
 * - The most the note still reaches without a new stage rising, on the 16-bit output scale of the engine's render, before
 *   the gain the player applies to that render: a stage rising now counts its target. Each engine's header says which
 *   gain is already in its xx_heard (the DX7's: none; the Juno-106's: the note's level, which its engine renders with),
 *   so that the player multiplies it by the rest alone, and how its rendered peaks compare. A ghost let go whose
 *   xx_heard times that gain is under PL_QUIET (-60 dB) at its return is dropped; a held one never is. */
#ifndef FWD_H
#define FWD_H
#include <stdint.h>

#define FWD_HELD 0xFFFFFFFFu             /* *off: the note still held */
#endif
