#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The sound on the PC (fw/core/play, eng, fx): built with gcc's sanitizers, then optimised by gcc and by clang.
#  - PTR: every sample of dist/firmware/demo.ptfmdsk decoded, whole and from places inside, as the packer decodes it;
#  - each engine's reference sound at -20 dB, and through the player's mix at -26 dB;
#  - every demo song's notes against the playground's replay (playground/player.js, with Node);
#  - LEG (legato_test): legato onto a held DX7, JUNO-106, CHIP and sampler note; a plain note elsewhere;
#  - the voice pool (voices_test): a note taken from its engine a ghost and back where its envelope would be, held or
#    let go, the keys' first, the one made last first, 4 a block; a take's fade in its engine in its block; the 24
#    ghosts' list full; a TB-303 never a ghost; every built-in instrument and the disk's as a ghost against the note
#    never taken (held 0.1-60 s and let go; through the block's budget and in pieces); the budget's carry-over, a
#    return taken by a new note, a dropped return not counted, the parameter commands at a return; section 2's rule
#    case by case (songs of its own and DJ's decks played with their notes in engines, then as ghosts: every voice the
#    same block by block but for its engine) and section 11's other cases (the list full's ranks, the fade limit on
#    returns, a take past the fades, core 2's fallbacks on pools set by hand, DJ's pass-by, ...); the pool's links and
#    lists (voices, engines, ghosts, fades) checked after every half there and in c2_test (pool_check.h); voices_test
#    again with the debug build's figures: every ghost made counted back, gone or still a ghost, after every half, some
#    cases' own counts, and the rest the same;
#  - the forward work (forward_test): a note started fresh and worked forward over its time (held, let go, whole and
#    in pieces) the same as its blocks (the Juno-106: its ticks) stepped one by one: the DX7's envelopes, pitch
#    envelope and LFO, the Juno-106's envelope, LFO and what each tick makes of them, the sampler's envelope, every
#    built-in patch and the disk's sampler instruments and others made at random; the steps it takes; each engine's
#    measure of what it still reaches against its rendered peaks (samp_heard the sampler's); the Juno-106's ticks to
#    silence after a let-go, by RELEASE; the sampler's place in its sample (samp_place_go) against samp_skip's frames
#    (stretch by stretch with its pitch and commands changed, and frame by frame from states at random), its end
#    (samp_end_blocks) against the block samp_skip ends it in, and a voice started at its place (samp_note_at)
#    rendering as the voice stepped there, sample for sample; the CHIP's tables, envelopes, release, sweep and
#    vibrato as chip_skip's blocks leave them, with its parameters turned between calls, its end (chip_end_blocks)
#    against the block chip_skip ends it in, chip_heard against its rendered peaks;
#  - the effects at work as another core reads them inside a block: the last whole block's (fxact_test);
#  - every demo song rendered (build/fw/sound/song_NN.wav) with its loudness, peak, most voices (and at 8
#    voices with the sanitizers: notes made ghosts and back);
#  - the second core's hand-over (c2_test): one session (DJ, every song, the punch-ins, a COUNT-IN, CUT, the keys
#    DRY, SYNC PULSE, SECOND CORE switched) at VOICES 12, made in the modes of the test platform's second core that can
#    differ there (disk_store.c: core 1 alone, core 2 at once, a block late, a half late, stopped mid-session, a thread
#    of its own), the voices' split moving each block there too: modes 1-3 and 6 the same as core 1 alone whose load
#    guard counts as with core 2 (PL_ONE_CORE_16), byte for byte, and so with the split forced (every voice core 1's,
#    the top 10 core 2's, at random each block); mode 4 the same as core 1 alone whose guard counts as mode 4's
#    (C2_GUARD); the modes losing a voice job, 5 and 7, run in the next session;
#  - 16 voices, the voice job (c2_test with C2_V16: 16 and 20 keys held, keys over a song, its mutes, every song, DJ
#    HEAVY, CUT, SECOND CORE switched, VOICES 8 and 16 again): modes 1-3 and 6, and the split forced, the same as one
#    core rendering all 16 in place (PL_ONE_CORE_16), SECOND CORE's switching among themselves; modes 4, 5 and 7 (core
#    2 stopped, at a half's start, with a voice job never begun, with one begun) run on with their checks (the notes
#    core 2 had made ghosts as each case says; RECORD LIVE's tap told);
#  - the debug build's player (PTFM_DEBUG: its figures counted on both threads) in mode 6, both sessions: the same
#    as without them, byte for byte;
#  - gcc's build and clang's give the same samples.
set -e
cd "$(dirname "$0")/../.."
GEN=build/fw/sound/gen
OUT=build/fw/sound
mkdir -p "$OUT"
python3 -I fw/tools/gen.py "$GEN" >/dev/null
DISK=dist/firmware/demo.ptfmdsk
[ -f "$DISK" ] || { echo "run_sound: no $DISK (tools/pack.py makes it)"; exit 1; }
CORE="fw/core/plat_weak.c fw/core/play/rec.c fw/core/play/ibxm/ibxm.c fw/core/fx/fx.c fw/core/eng/dx7/dx7.c fw/core/eng/juno/juno.c
      fw/core/eng/tb303/tb303.c fw/core/eng/samp/samp.c fw/core/eng/chip/chip.c fw/tests/sound/disk_store.c $GEN/builtin.c"
CF="-std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-type-limits -I$GEN -Ifw/core -Ifw/core/play/ibxm"
# (negative values shifted left, as the engines do, are what both compilers make of them: two's complement)
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=undefined"
export ASAN_OPTIONS=detect_leaks=0

gcc $CF $SAN -o $OUT/ptr_test fw/tests/sound/ptr_test.c fw/core/eng/samp/samp.c fw/tests/sound/disk_store.c $GEN/builtin.c
gcc $CF $SAN -o $OUT/ref_test fw/tests/sound/ref_test.c fw/core/play/player.c $CORE -lm
gcc $CF $SAN -o $OUT/render_san fw/tests/sound/render.c $CORE -lm
gcc $CF $SAN -o $OUT/gliss_test fw/tests/sound/gliss_test.c $CORE -lm
gcc $CF $SAN -o $OUT/legato_test fw/tests/sound/legato_test.c $CORE -lm
gcc $CF $SAN -o $OUT/transport_test fw/tests/sound/transport_test.c fw/core/play/player.c $CORE -lm
gcc $CF $SAN -o $OUT/voices_test fw/tests/sound/voices_test.c $CORE -lm
# (the same with the debug build's figures: the ghosts' counts kept by every case, some cases' own counts)
gcc $CF $SAN -DPTFM_DEBUG -o $OUT/voices_test_dbg fw/tests/sound/voices_test.c fw/core/dbg.c fw/tests/sound/dbg_stubs.c \
    $CORE -lm
gcc $CF $SAN -o $OUT/forward_test fw/tests/sound/forward_test.c $CORE -lm
gcc $CF $SAN -o $OUT/fxact_test fw/tests/sound/fxact_test.c fw/core/fx/fx.c fw/core/eng/dx7/dx7.c -lm
# (c2_test with the player included: the pool's links checked after every half)
gcc $CF $SAN -pthread -DPOOL_CHECK -o $OUT/c2_test fw/tests/sound/c2_test.c fw/core/play/dj.c fw/core/spare.c $CORE -lm
gcc $CF $SAN -pthread -DPOOL_CHECK -DPL_ONE_CORE_16=1 -o $OUT/c2_test16 fw/tests/sound/c2_test.c fw/core/play/dj.c \
    fw/core/spare.c $CORE -lm
gcc $CF $SAN -pthread -DPOOL_CHECK -DPTFM_DEBUG -o $OUT/c2_test_dbg fw/tests/sound/c2_test.c fw/core/play/dj.c \
    fw/core/spare.c fw/core/dbg.c fw/tests/sound/dbg_stubs.c $CORE -lm
gcc $CF -O2 -o $OUT/render_gcc fw/tests/sound/render.c $CORE -lm

if [ -d build/fw/pack/dump ]; then
    $OUT/ptr_test $DISK build/fw/pack/dump
else
    echo "ptr: (no build/fw/pack/dump: tools/pack.py --dump $DISK build/fw/pack/dump makes it)"
fi
$OUT/ref_test
$OUT/gliss_test
$OUT/legato_test $DISK
$OUT/transport_test $DISK
$OUT/voices_test $DISK > $OUT/voices.txt || { cat $OUT/voices.txt; exit 1; }
cat $OUT/voices.txt
$OUT/voices_test_dbg $DISK > $OUT/voices_dbg.txt || { cat $OUT/voices_dbg.txt; echo "voices_test, the debug build: WRONG"; exit 1; }
grep -v '^the figures: ' $OUT/voices_dbg.txt > $OUT/voices_dbg_rest.txt
cmp -s $OUT/voices_dbg_rest.txt $OUT/voices.txt || { diff $OUT/voices.txt $OUT/voices_dbg_rest.txt | head; \
    echo "voices_test, the debug build: its cases' figures differ from without the debug build's"; exit 1; }
echo "voices_test, the debug build: the same, the ghosts' figures kept through every case" \
     "($(grep -c '^the figures: .* ok$' $OUT/voices_dbg.txt) checks of their own)"
$OUT/forward_test $DISK
$OUT/fxact_test

fail=0
# (the plain session's modes at once: each its own process, its files in c2/MODE; the reference c2/ref one core
# rendering every voice in place, its load guard counting as with core 2 (PL_ONE_CORE_16: so it decides as modes 1-3
# and 6 do, core 2 taking voices at VOICES 12 too); mode 4's own reference c2/g4 mode 0 with the guard counting as
# mode 4's (C2_GUARD: core 2 its first 499 halves). The split forced in modes 6 and 2 (C2_SPLIT: 2 every voice core 1's,
# 3 the top 10 core 2's, 4 a place at random each block), c2/fN: the sound the same wherever it stands. Modes 5 and 7
# lose a voice job (that block unheard, then core 1 alone): they run in the 16 voices' session. Then 16 voices, in
# c2v/MODE, c2v/ref one core rendering all 16)
rm -rf $OUT/c2 $OUT/c2v
c2run() {   # $1 the session's folder, $2 its name there, then the environment and the program
    d=$1
    n=$2
    shift 2
    mkdir -p $OUT/$d/$n
    { env "$@" $DISK $OUT/$d/$n > $OUT/$d/$n.txt 2>&1; echo $? > $OUT/$d/$n.rc; } &
}
for m in 0 1 2 3 4 6; do
    c2run c2 $m C2_MODE=$m $OUT/c2_test
done
c2run c2 ref C2_MODE=0 $OUT/c2_test16
c2run c2 g4 C2_MODE=0 C2_GUARD=500 $OUT/c2_test
c2run c2 dbg C2_MODE=6 $OUT/c2_test_dbg
for f in 2 3 4; do
    c2run c2 f$f C2_MODE=6 C2_SPLIT=$f $OUT/c2_test
done
c2run c2 f4b C2_MODE=2 C2_SPLIT=4 $OUT/c2_test
wait
c2run c2v ref C2_V16=1 C2_MODE=0 $OUT/c2_test16
for m in 1 2 3 4 5 6 7; do
    c2run c2v $m C2_V16=1 C2_MODE=$m $OUT/c2_test
done
c2run c2v dbg C2_V16=1 C2_MODE=6 $OUT/c2_test_dbg
for f in 2 3 4; do
    c2run c2v f$f C2_V16=1 C2_MODE=6 C2_SPLIT=$f $OUT/c2_test
done
c2run c2v f4b C2_V16=1 C2_MODE=2 C2_SPLIT=4 $OUT/c2_test
wait
c2=0
for m in ref g4 0 1 2 3 4 6 f2 f3 f4 f4b; do
    [ "$(cat $OUT/c2/$m.rc)" = 0 ] || { cat $OUT/c2/$m.txt; echo "c2_test: mode $m WRONG"; c2=1; }
done
for m in 1 2 3 6 f2 f3 f4 f4b 4; do
    d=""
    r=ref
    [ $m = 4 ] && r=g4
    for f in $OUT/c2/$r/*; do
        cmp -s "$f" "$OUT/c2/$m/${f##*/}" || d="$d ${f##*/}"
    done
    [ -z "$d" ] || { echo "c2_test: mode $m differs from its reference ($r) in$d"; c2=1; }
done
[ $c2 = 0 ] && echo "c2_test: modes 1-3 and 6 and the split forced (every voice core 1's, the top 10 core 2's, at" \
                    "random) the same as one core, mode 4 as one core whose load guard counted as its own" \
                    "($(tail -1 $OUT/c2/ref.txt | sed 's/^c2_test: //'))"
v=0
for m in ref 1 2 3 4 5 6 7 f2 f3 f4 f4b; do
    [ "$(cat $OUT/c2v/$m.rc)" = 0 ] || { cat $OUT/c2v/$m.txt; echo "c2_test, 16 voices: mode $m WRONG"; v=1; }
done
for m in 1 2 3 6 f2 f3 f4 f4b; do
    d=""
    for f in $OUT/c2v/ref/*; do
        case "${f##*/}" in
            v16_switch.*) g=$OUT/c2v/1/${f##*/} ;;   # (SECOND CORE off: 16 as 12, not one core's 16: mode 1's)
            *) g=$f ;;
        esac
        cmp -s "$g" "$OUT/c2v/$m/${f##*/}" || d="$d ${f##*/}"
    done
    [ -z "$d" ] || { echo "c2_test, 16 voices: mode $m differs from one core's 16 in$d"; v=1; }
done
[ $v = 0 ] && echo "c2_test, 16 voices: modes 1-3 and 6 and the split forced the same as one core's 16, modes 4, 5" \
                   "and 7 ran on ($(tail -1 $OUT/c2v/ref.txt | sed 's/^c2_test: //'))"
g=0
for s in c2 c2v; do
    [ "$(cat $OUT/$s/dbg.rc)" = 0 ] || { cat $OUT/$s/dbg.txt; echo "c2_test, the debug build ($s): WRONG"; g=1; }
    d=""
    for f in $OUT/$s/6/*; do
        cmp -s "$f" "$OUT/$s/dbg/${f##*/}" || d="$d ${f##*/}"
    done
    [ -z "$d" ] || { echo "c2_test, the debug build ($s): differs from mode 6 in$d"; g=1; }
done
[ $g = 0 ] && echo "c2_test, the debug build (its figures counted): both sessions the same as without them"
[ $c2 = 0 ] && [ $v = 0 ] && [ $g = 0 ] || fail=1
$OUT/render_gcc $DISK --list > $OUT/songs.txt
while read -r num name; do
    $OUT/render_san $DISK "$num" --seconds 40 --events > $OUT/c_$num.txt
    $OUT/render_san $DISK "$num" --seconds 40 --voices 8 > /dev/null   # (8 voices: notes made ghosts and back, sanitized)
    if command -v node >/dev/null; then
        node fw/tests/sound/pg_events.js "$name" 40 > $OUT/p_$num.txt
        r=$(python3 -I fw/tests/sound/cmp_events.py $OUT/c_$num.txt $OUT/p_$num.txt) || { fail=1; echo "$r"; }
        echo "$num $name: $(echo "$r" | tail -1)"
    fi
    lv=$($OUT/render_gcc $DISK "$num" --seconds 30 | sed -n 's/.*LUFS, \(-*[0-9.]*\) dB RMS.*/\1/p')
    want=$(grep "^$name|" fw/tests/sound/levels.txt | cut -d'|' -f2)
    if [ -n "$want" ]; then
        awk -v a="$lv" -v b="$want" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 1.0) }' ||
            { echo "$num $name: $lv dB RMS in its first 30 s, the playground's $want"; fail=1; }
    fi
    $OUT/render_gcc $DISK "$num" --seconds 240 --wav $OUT/song_$num.wav
done < $OUT/songs.txt

if command -v clang >/dev/null; then
    clang $CF -O2 -o $OUT/render_clang fw/tests/sound/render.c $CORE -lm
    for num in 00 03 05; do
        $OUT/render_clang $DISK $num --seconds 60 --wav $OUT/clang_$num.wav > /dev/null
        $OUT/render_gcc $DISK $num --seconds 60 --wav $OUT/gcc_$num.wav > /dev/null
        cmp -s $OUT/clang_$num.wav $OUT/gcc_$num.wav || { echo "song $num: gcc and clang differ"; fail=1; }
    done
    [ $fail = 0 ] && echo "gcc and clang: the same samples"
else
    echo "(no clang: the gcc / clang check skipped)"
fi
[ $fail = 0 ] || { echo "run_sound: FAILED"; exit 1; }
echo "run_sound: all passed"
