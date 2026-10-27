#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The sound on the PC (fw/core/play, eng, fx): built with gcc's sanitizers, then optimised by gcc and by clang.
#  - PTR: every sample of dist/firmware/demo.ptfmdsk decoded, whole and from places inside, as the packer decodes it;
#  - each engine's reference sound at -20 dB, and through the player's mix at -26 dB;
#  - every demo song's notes against the playground's replay (playground/player.js, with Node);
#  - LEG (legato_test): legato onto a held DX7, JUNO-106, CHIP and sampler note; a plain note elsewhere;
#  - the voice pool: a note taken from its voice parked and back where its envelope would be (voices_test);
#  - every demo song rendered (build/fw/sound/song_NN.wav) with its loudness, peak, most voices (and at 8
#    voices with the sanitizers: notes parked and back);
#  - the second core's hand-over (c2_test): one session (DJ, every song, the punch-ins, a COUNT-IN, CUT, the keys
#    DRY, SYNC PULSE, SECOND CORE switched) made in each mode of the test platform's second core (disk_store.c: core
#    1 alone, core 2 at once, a block late, a half late, stopped mid-session, a voice job lost, a thread of its own),
#    each the same as core 1 alone, byte for byte;
#  - 16 voices, the voice job (c2_test with C2_V16: 16 and 20 keys held, keys over a song, its mutes, every song, DJ
#    HEAVY, CUT, SECOND CORE switched): modes 1-3 and 6 the same as one core rendering all 16 in place (PL_ONE_CORE_16),
#    SECOND CORE's switching among themselves; modes 4 and 5 (core 2 stopped, at a half's start or with a voice job
#    out) run on with their checks;
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
CORE="fw/core/play/rec.c fw/core/play/ibxm/ibxm.c fw/core/fx/fx.c fw/core/eng/dx7/dx7.c fw/core/eng/juno/juno.c
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
gcc $CF $SAN -o $OUT/voices_test fw/tests/sound/voices_test.c fw/core/play/player.c $CORE -lm
gcc $CF $SAN -pthread -o $OUT/c2_test fw/tests/sound/c2_test.c fw/core/play/player.c fw/core/play/dj.c fw/core/spare.c $CORE -lm
gcc $CF $SAN -pthread -DPL_ONE_CORE_16=1 -o $OUT/c2_test16 fw/tests/sound/c2_test.c fw/core/play/player.c fw/core/play/dj.c \
    fw/core/spare.c $CORE -lm
gcc $CF $SAN -pthread -DPTFM_DEBUG -o $OUT/c2_test_dbg fw/tests/sound/c2_test.c fw/core/play/player.c fw/core/play/dj.c \
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
$OUT/voices_test $DISK

fail=0
# (the seven modes at once: each its own process, its files in c2/MODE; mode 0's the reference; then 16 voices, in
# c2v/MODE, c2v/ref one core rendering all 16)
rm -rf $OUT/c2 $OUT/c2v
for m in 0 1 2 3 4 5 6; do
    mkdir -p $OUT/c2/$m $OUT/c2v/$m
    { C2_MODE=$m $OUT/c2_test $DISK $OUT/c2/$m > $OUT/c2/$m.txt 2>&1; echo $? > $OUT/c2/$m.rc; } &
done
mkdir -p $OUT/c2/dbg
{ C2_MODE=6 $OUT/c2_test_dbg $DISK $OUT/c2/dbg > $OUT/c2/dbg.txt 2>&1; echo $? > $OUT/c2/dbg.rc; } &
wait
mkdir -p $OUT/c2v/ref $OUT/c2v/dbg
{ C2_V16=1 C2_MODE=0 $OUT/c2_test16 $DISK $OUT/c2v/ref > $OUT/c2v/ref.txt 2>&1; echo $? > $OUT/c2v/ref.rc; } &
for m in 1 2 3 4 5 6; do
    { C2_V16=1 C2_MODE=$m $OUT/c2_test $DISK $OUT/c2v/$m > $OUT/c2v/$m.txt 2>&1; echo $? > $OUT/c2v/$m.rc; } &
done
{ C2_V16=1 C2_MODE=6 $OUT/c2_test_dbg $DISK $OUT/c2v/dbg > $OUT/c2v/dbg.txt 2>&1; echo $? > $OUT/c2v/dbg.rc; } &
wait
c2=0
for m in 0 1 2 3 4 5 6; do
    [ "$(cat $OUT/c2/$m.rc)" = 0 ] || { cat $OUT/c2/$m.txt; echo "c2_test: mode $m WRONG"; c2=1; }
done
for m in 1 2 3 4 5 6; do
    d=""
    for f in $OUT/c2/0/*; do
        cmp -s "$f" "$OUT/c2/$m/${f##*/}" || d="$d ${f##*/}"
    done
    [ -z "$d" ] || { echo "c2_test: mode $m differs from mode 0 in$d"; c2=1; }
done
[ $c2 = 0 ] && echo "c2_test: modes the same ($(tail -1 $OUT/c2/0.txt | sed 's/^c2_test: //'))"
v=0
for m in ref 1 2 3 4 5 6; do
    [ "$(cat $OUT/c2v/$m.rc)" = 0 ] || { cat $OUT/c2v/$m.txt; echo "c2_test, 16 voices: mode $m WRONG"; v=1; }
done
for m in 1 2 3 6; do
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
[ $v = 0 ] && echo "c2_test, 16 voices: modes 1-3 and 6 the same as one core's 16, modes 4 and 5 ran on" \
                   "($(tail -1 $OUT/c2v/ref.txt | sed 's/^c2_test: //'))"
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
    $OUT/render_san $DISK "$num" --seconds 40 --voices 8 > /dev/null   # (8 voices: notes parked and back, sanitized)
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
