#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The demo disk's six songs (our own, OWN_LICENSE), composed here (compose.py, theory.py): demo songs in
the way synthesizers' demos are made, each showing one part of the disk in one style, short (six patterns
of 8 bars, each played once: about 1:15 to 2:00), and ending (AT THE END = STOP) with a proper outro.

  songs.py        -> songs/<song>/song.json + patterns.json for each of catalog.SONGS (the DJ loops too)

Every song has the same shape, bent where its style wants it: (1) an intro with the song's hook within its
first two bars; (2) the groove; (3) the build, its last bar a fill into (4) the break, where the lead part
shows what its engine does alone; (5) the peak, everything and one thing new; (6) the outro, parts leaving,
a last hit and its tail. One job a track, 12 tracks at most, at most 16 voices sounding at once with their
releases (the largest pool; check.py counts them) and none taken from a note, only the disk's own
instruments and PT/FM's own effects, every song at the same loudness (GLOBAL VOLUME: TRIM_DB). Each part is
written where it sounds: the Juno-106's 16' basses with their subs sound two octaves under their notes, the
DX7's FM BASS, PLUCK BASS and SLAP BASS one; samples play near the notes they were recorded at; every riff,
bass line and arpeggio follows the chords on the key's scale (theory.py).

The disk's order (catalog.SONGS): PUNCH IN, SPICE ROAD, MIDNIGHT RUN, CITY LIGHTS, ACID TEST, ADVENTURE.

  CITY LIGHTS   the DX7 cartridges: 80s synthpop in D minor, 120 BPM: FM BASS's sequenced octaves, a
                staccato SQUARE LD hook leaping octaves over Dm Bb C A (the dominant major), BRASS SECT stabs,
                a MARIMBA ostinato, VOICE LEAD singing the verse over POLY PAD, the FM DRUM KIT (claps, FM TOM
                fills); a break of FM VOICES, ORCH HITs and a TUBE BELL; the hook doubled by SAW LEAD at the end
  MIDNIGHT RUN  the JUNO-106: outrun synthwave in F# minor, 130 BPM, F#m D E Bm: Night Bass galloping (an
                eighth and two sixteenths a beat, each pluck cut by ECx) from the first bar, its VCF opened and
                closed by commands, DUCK pumping it from the four-on-the-floor kick; sixteenth hats, the Gated
                Room snare, Syn Tom fills across the kit, Arp Saw's sixteenths from side to side, Strings 80
                and the PWM Pad (CHORUS II by command, its width swept in the break), the verse's Saw Lead
                gliding by TONE PORTA, a Super Saw
                hook of runs into held notes (the Juno's delayed vibrato), Poly Brass doubling it an octave
                down, a Glide Lead's solo in the break, gliding, then running in sixteenths
  ACID TEST     the TB-303: acid techno into acid trance in A minor, 138 BPM: the 303 alone through the
                DRIVE, relentless sixteenths with accents and slides, CUTOFF, RESONANCE, ENV MOD and DECAY
                turned by commands, a second 303 answering an octave up from side to side; then the DRIVE off
                (FX OFF) for acid trance on Am Am F G: a breakdown (Saw Pad, a sixteenth riff on a pluck,
                the 303 in a ping-pong DELAY, a snare roll, the Trance Ld) and the drop (Trance Saw chords in
                the trance gate pumped by DUCK, the riff, the Super Saw's lead, the 303 a rolling bass); the outro
                lands the lead home on A
  SPICE ROAD    the samplers: a band in D Dorian (the natural kit, steel guitar strummed on three tracks a
                tick apart by note delays, palm mutes, the muted guitar's riff, the upright, the piano's
                muted stabs, a distorted guitar), then the turn (toms, CHIME RISE, VLN SWELL, a grabbed
                crash) to D Hijaz over a drone: tablas, darbuka, congas, bongo, the wind chime, timpani,
                violins and their stabs, the flute's melody, the tenor sax's solo with SAX STAB answers
  ADVENTURE     the CHIPTUNE cartridge: one theme travelling through the eras of the sound chips, a pattern
                an era, each with its chips, key, tempo and variation: 1978 the PSG (C major, 132 BPM), 1983
                the console (D, 144), 1985 16BIT (B minor, 138), 1989 the handheld (G, 150), the 2000s all of
                them (A, 160), then home through every era to the PSG, slowing from 160 to 108; a time machine
                between eras (WHOOSH, WARP, WARP DOWN), coins (FUSION's COIN); chords as 0xy arpeggios. Its
                cells are kept as made, in chip_songs.json (as CHIP LOOP's)
  PUNCH IN      PT/FM itself: dub techno into progressive house in F minor, 124 BPM: one effect at a time,
                each two beats or more: HERE COMES thrown into the echo (ECHO THROW), dub chords into a
                ping-pong DELAY, DUCK pumping them from the kick, the FILTER opened and closed by FX commands,
                a LOOP 1/8, a REVERB SWELL on the bell in the break; a build of GATE, then a rising high-pass,
                a retriggered roll, THREE TWO ONE and a TAPE STOP; the peak with LOOP 1/16, BIT CRUSH and
                REVERSE; a cut ending: a STUTTER, then a VINYL STOP on the full beat
The DELAY's PING-PONG and the pans (a track's, and `8xx` a note's) spread every song across the stereo
picture; the kick, the bass, the snare and the lead stay in the middle.

Then the twelve DJ loops (catalog.DJ_LOOPS, after the songs: TRANCE LOOP, BREAKS LOOP, TECHNO LOOP, ACID LOOP,
DISCO LOOP, CHIP LOOP, HIP-HOP LOOP, DNB LOOP, ROCK LOOP, METAL LOOP, ELECTRO LOOP, WORLD LOOP), for DJ's decks
and LOOPS: a pattern of 8 bars that loops (DNB LOOP's, ROCK LOOP's and METAL LOOP's two: a verse and a
bridge), written on four tracks of one voice each (each note ends before its track's next: one_voice), so a
loop sounds 4 voices at most and two decks never take each other's; then each track's other instruments moved
to tracks of their own (one_sound_a_track: the notes as they were), so LOOPS loads a track as a loop of one
sound; all in A minor (WORLD LOOP's flute with B flat and C sharp); nine at 120 to 128 BPM, TRANCE LOOP at 140,
HIP-HOP LOOP at 86 and DNB LOOP at 172 (SYNC's half and double); the first four each on one engine (the
JUNO-106: TRANCE LOOP's psytrance, its bass rolling in threes after each kick; the samplers, the DX7, the
TB-303), then DISCO LOOP (the DRUM KIT and the JUNO-106: Funk Bass, Wah Clav), CHIP LOOP (CHIPTUNE's
0xy chords; a verse and a bridge), HIP-HOP LOOP (SWING and LO-FI), DNB LOOP (the BASS and VOICES samplers; its bridge the same break
and bass, the WOBBLE 140 in the pad's place), ROCK LOOP (the guitars: the DIST GTR's riff, the DX7's lead
with TONE PORTA bends; its bridge power chords ringing), METAL LOOP (the double kick, a palm-muted gallop, a
tremolo-picked lead on the recorded DIST GTR, the riff's chords on the DX7's; its bridge a breakdown), ELECTRO
LOOP (the
JUNO-106's 808 sounds, the TIMPANI, a Seq Lead), WORLD LOOP (darbuka and tablas, the upright, the flute: SPICE ROAD's turn); bar 8 a fill; track 3 the
bass in all twelve. TECHNO LOOP, ACID LOOP, DISCO LOOP, DNB LOOP, ROCK LOOP and METAL LOOP play an intro of 8
bars first, once (the drums, then the hook coming in; DNB LOOP's the hats and the pad, then the break; ROCK
LOOP's and METAL LOOP's the riff palm-muted). Each is mastered by its instruments' jobs (its mix: each
instrument's LEVEL against the kick, or the lead), all twelve at the same -26.0 LUFS (TRIM_DB).
"""
from catalog import CAT, CARTRIDGES, DJ_LOOPS, DJ_VOICES, SONGS, dx7_r4
from common import DISK, TOOLS, read_json
from compose import OFF, Pool, Song, chord_notes, midi, parse_cmd, voice
from theory import Key

B, PAT = 16, 128                         # rows a bar (4 a beat); a pattern: 8 bars

# the FX's numbers (FX ON 0xAE / OFF 0xAF name one) and the FILTER's CUT steps
FX_NO = {"GATE": 0, "FILTER": 1, "DRIVE": 2, "LO-FI": 3, "CHORUS": 4, "DELAY": 5, "REVERB": 6, "LIMITER": 7,
         "DUCK": 8, "METRONOME": 9}


def I(cart, name):
    """an instrument by its cartridge's name and its own: (cartridge, slot)"""
    kind = next(k for _, k, _, c in CARTRIDGES if c == cart)
    return CAT._find((kind,), name, cart)


def lp(n):
    return 64 - n


def hp(n):
    return 64 + n


def fxc(code, v):
    return f"#{code:02X}:{v:02X}"


def fx_on(name):
    return fxc(0xAE, FX_NO[name])


def fx_off(name):
    return fxc(0xAF, FX_NO[name])


def dx_rel(ins, rate):
    """parameter commands setting a DX7 voice's carriers' R4 (its release) to `rate`"""
    return tuple(f"*{dx7_r4(op):02X}:{rate:02X}" for op in CAT.carriers(ins))


def hats(p, track, closed, opened, steps, vol, start=0, end=None):
    """closed and open hats on one track (the next hat chokes an open one, as a drum machine's do): c closed,
    C closed accented, o open, . none"""
    end = len(p.cells) if end is None else end
    for r in range(start, end):
        ch = steps[(r - start) % len(steps)]
        if ch == "c":
            p.note(track, r, "C-4", closed, vol - 16)
        elif ch == "C":
            p.note(track, r, "C-4", closed, vol)
        elif ch == "o":
            p.note(track, r, "C-4", opened, vol)


def acid_steps(p, track, ins, riff, roots, vol, start, end, key):
    """a TB-303 line in the key's steps: riff = (step, flags) or None a row (flags 1 ACC, 2 SLD, 3 both),
    repeated; roots: a note a bar, in turn"""
    for r in range(start, end):
        s = riff[(r - start) % len(riff)]
        if s is None:
            continue
        deg, fl = s
        root = midi(roots[((r - start) // B) % len(roots)])
        flag = {1: "ACC", 2: "SLD", 3: "A+S"}.get(fl)
        p.note(track, r, key.up(root, deg), ins, vol, *((flag,) if flag else ()))


def let_go(s, tracks, after=4, only=None):
    """note offs for drums: a hit not followed by another within `after` rows lets go there (`only`: the hits
    of that instrument). A synth drum's note is held until something ends it, its voice kept though its sound
    has died away; let go, its voice is free once its release has rung out"""
    def notes(p, t):
        return [r for r in range(p.rows) if p.cells[r][t - 1] is not None and p.cells[r][t - 1][0] is not None]

    for k_p, p in enumerate(s.pats):             # (a song's patterns are its positions, in order)
        nxt_p = s.pats[k_p + 1] if k_p + 1 < len(s.pats) else None
        for t in tracks:
            rows = notes(p, t)
            for k, r in enumerate(rows):
                if p.cells[r][t - 1][0] == OFF or not (only is None or p.cells[r][t - 1][1] == only):
                    continue
                if k + 1 < len(rows):
                    if rows[k + 1] - r > after:
                        p.off(t, r + after)
                elif r + after < p.rows:
                    p.off(t, r + after)
                elif nxt_p is not None:          # past the pattern's end: in the next one, if nothing comes first
                    at = r + after - p.rows
                    first = notes(nxt_p, t)
                    if not first or first[0] > at:
                        nxt_p.off(t, at)


def cut_notes(p, track, tick, start=0, end=None):
    """NOTE CUT (ECx) at tick x of each note's row on the track (where its cell has room): the note gone
    before the next one, so no release tail sounds under it (a voice saved where the notes come every row)"""
    for r in range(start, p.rows if end is None else end):
        c = p.cells[r][track - 1]
        if c is not None and c[0] not in (None, OFF) and len(c[3]) < 4:
            p.cmd(track, r, f"EC{tick:X}")


def low_root(sym, lowest):
    """a chord's (or a note name's) root as the lowest note from `lowest` up: a bass kept in one octave"""
    k = 2 if len(sym) > 1 and sym[1] in "#b" else 1
    pc = midi(sym[:k] + "4") % 12
    return lowest + (pc - lowest) % 12


def bass_in(p, track, ins, symbols, each, figure, vol, lowest, start=0, gate=None):
    """a figure under each chord, as bassline() does, its roots in the octave from `lowest` up (an
    instrument heard two octaves under its notes: lowest A-3 sounds A1, 55 Hz, at the bottom)"""
    r = start
    for sym in symbols:
        root = low_root(sym, midi(lowest))
        for f in figure:
            rr, iv, dv = (tuple(f) + (0,))[:3]
            if rr < each:
                p.note(track, r + rr, root + iv, ins, vol + dv)
                if gate:
                    p.off(track, r + rr + gate)
        r += each


def pad_chords(p, tracks, ins, chords, each, vol, center=64, start=0, cmds=()):
    """a pad's chords, a note a track: a note the next chord keeps on its track goes on sounding (no new
    note, so no release left ringing under it); `cmds` on each track's first note (its release, say)"""
    last = [None] * len(tracks)
    r = start
    for sym in chords:
        notes = voice(chord_notes(sym), center) if isinstance(sym, str) else [midi(n) for n in sym]
        for k, t in enumerate(tracks):
            n = notes[k] if k < len(notes) else None
            if n is None:
                p.off(t, r)
            elif n != last[k]:
                p.note(t, r, n, ins, vol, *(cmds if last[k] is None else ()))
            last[k] = n
        r += each


def early_offs(p, tracks, start, end, each=16):
    """a chord's note that the next chord changes lets go a row before it (its release bridges the gap: fewer
    tails under the new chord)"""
    for t in tracks:
        for r in range(start + each, end, each):
            c = p.cells[r][t - 1]
            if c is not None and c[0] not in (None, OFF):
                p.off(t, r - 1)


def roll(p, track, ins, vol, start, end, note="C-4"):
    """a snare roll: eighths, then sixteenths, the last beat retriggered (E93), rising"""
    n = end - start
    for r in list(range(start, start + n // 2, 2)) + list(range(start + n // 2, end)):
        p.note(track, r, note, ins, vol - 30 + 30 * (r - start) // n, *(("E93",) if r >= end - 4 else ()))


# ============================================================= CITY LIGHTS ===

SP_VERSE = ["Dm", "Dm", "Bb", "C"]               # i i VI VII
SP_CHORUS = ["Dm", "Bb", "C", "A"]              # i VI VII V: the dominant major, harmonic minor's C#
SP_BREAK = ["Bb", "C", "Dm", "Dm", "Bb", "C", "A", "A"]
# the hook, a bar a chord of SP_CHORUS: staccato, syncopated (2 2 1 2 2 1 2 2 2), each bar leaping an octave
SP_HOOK_BARS = ["D5/2 D6/2 A5/1 F5/2 D5/2 F5/1 G5/2 A5/2 F5/2",
                "D5/2 D6/2 Bb5/1 F5/2 D5/2 F5/1 G5/2 F5/2 D5/2",
                "C5/2 C6/2 G5/1 E5/2 C5/2 E5/1 G5/2 C6/2 E5/2",
                "C#5/2 A5/2 E5/1 C#5/2 A4/2 C#5/1 E5/2 A5/2 ./2"]
SP_HOOK = " ".join(SP_HOOK_BARS)
# the verse's sung line, over SP_VERSE twice
SP_SING = ("A4/4 A4/2 G4/2 A4/4 D5/4 C5/4 A4/4 F4/8 F4/4 G4/2 F4/2 D4/4 F4/4 G4/12 ./4 "
           "A4/4 A4/2 G4/2 A4/4 D5/4 F5/4 D5/4 A4/8 Bb4/4 D5/2 Bb4/2 F4/4 G4/4 E4/12 ./4")
SP_BELL = "D6/8 C6/8 E6/8 C6/8 F6/16 A5/16 D6/8 C6/8 E6/8 G6/8 E6/16 C#6/16"     # the break's, over SP_BREAK


def rel_at(p, track, r, cmds):
    """parameter commands from row r on, four a cell (a voice's carriers' releases can be six)"""
    for k in range(0, len(cmds), 4):
        p.cmd(track, r + k // 4, *cmds[k:k + 4])


def city_lights(pool):
    def F(name):
        return I("FM DRUM KIT", name)
    kick, snare, clap, hat, ohat, tom, crash = (F(n) for n in ("KICK", "SNARE 2", "FM CLAP", "HI-HAT", "FM HAT OPN",
                                                               "FM TOM", "FM CRASH"))
    bass, hook_ld, saw = I("LEADS/BASSES", "FM BASS"), I("LEADS/BASSES", "SQUARE LD"), I("LEADS/BASSES", "SAW LEAD")
    sing, pad, choir = I("PADS/CHOIRS", "VOICE LEAD"), I("PADS/CHOIRS", "POLY PAD"), I("PADS/CHOIRS", "FM VOICES")
    brass, orch = I("WINDS/BRASS", "BRASS SECT"), I("PADS/CHOIRS", "ORCH HIT")
    marimba, tube = I("BELLS/PLUCKS", "MARIMBA"), I("BELLS/PLUCKS", "TUBE BELL")
    s = Song(pool, "CITY LIGHTS", "80s synthpop, D minor, 120 BPM. Shows the DX7 cartridges: FM BASS's sequenced "
             "octaves, a staccato SQUARE LD hook leaping octaves over Dm Bb C A, BRASS SECT stabs, a MARIMBA ostinato, "
             "VOICE LEAD singing the verse over POLY PAD, the FM DRUM KIT with FM TOM fills and claps; the break: FM "
             "VOICES, ORCH HITs and a TUBE BELL; the last chorus with SAW LEAD doubling the hook an octave down",
             tempo=120, tracks=12, at_end="stop")
    T_KICK, T_SNARE, T_HAT, T_TOM, T_BASS, T_C1, T_C2, T_C3, T_LEAD, T_LEAD2, T_MAR, T_FX = range(1, 13)
    CH = (T_C1, T_C2, T_C3)
    # shorter releases, so a note's tail does not hold a voice under the next
    BASSR, LEADR, SAWR, SINGR = dx_rel(bass, 80), dx_rel(hook_ld, 75), dx_rel(saw, 75), dx_rel(sing, 62)
    PADR, CHOIRR, BRASSR, MARR = dx_rel(pad, 52), dx_rel(choir, 52), dx_rel(brass, 70), dx_rel(marimba, 70)
    BELLR, ORCHR = dx_rel(tube, 68), dx_rel(orch, 62)

    def drums(p, start, end, chorus=False, fill=True):
        """the drum machine: the kick on the beats in a chorus (the verse's lighter), the snare or, in a chorus,
        the clap on 2 and 4, sixteenth hats, the open hat on the last offbeat; an FM TOM fill at the end"""
        p.hits(T_KICK, kick, "x...x...x...x..." if chorus else "x.......x.x.....", 0x6C, start, end)
        p.hits(T_SNARE, clap if chorus else snare, "....x.......x...", 0x60, start, end)
        p.hits(T_HAT, hat, "xoxoxoxoxoxoxo..", 0x46, start, end, soft=14)
        p.hits(T_HAT, ohat, "..............x.", 0x40, start, end)
        if fill:
            tom_fill(p, end - 4)

    def tom_fill(p, start, n=4):
        """FM TOM down, a sixteenth each, across the kit (high left, low right)"""
        for k in range(n):
            p.note(T_TOM, start + k, ("G-4", "E-4", "C-4", "A-3")[k % 4], tom, 0x58 + 3 * k,
                   ("850", "870", "890", "8B0")[k % 4])

    def bass_seq(p, symbols, vol, start=0):
        """FM BASS's eighths, the root and its octave in turn, staccato (heard an octave under the note sent:
        written from D-3, heard from D2)"""
        bass_in(p, T_BASS, bass, symbols, 16, [(k, 12 * (k // 2 % 2), -12 * (k // 2 % 2)) for k in range(0, 16, 2)],
                vol, lowest="D-3", start=start, gate=1)
        rel_at(p, T_BASS, start, BASSR)

    def hook(p, start, vol, double=False, text=SP_HOOK):
        p.melody(T_LEAD, hook_ld, text, vol, start, gate=1)
        rel_at(p, T_LEAD, start, LEADR)
        if double:
            p.melody(T_LEAD2, saw, text, vol - 8, start, up=-12, gate=1)
            rel_at(p, T_LEAD2, start, SAWR)

    def stabs(p, symbols, vol, start=0):
        """BRASS SECT stabs, three notes, syncopated"""
        p.stabs(T_C1, brass, symbols, 16, "x..x..x...x.x...", vol, center=70, start=start, tracks=list(CH), gate=2)
        for t in CH:
            rel_at(p, t, start, BRASSR)

    def chords(p, ins, rel, symbols, vol, start=0, center=66):
        pad_chords(p, CH, ins, symbols, 16, vol, center=center, start=start)
        early_offs(p, CH, start, start + 16 * len(symbols))
        for t in CH:
            rel_at(p, t, start, rel)

    def mallets(p, symbols, vol, start=0):
        """the MARIMBA's ostinato in eighths, the chord's tones up and back"""
        p.arp(T_MAR, marimba, symbols, 16, vol, step=2, center=74, start=start, pattern=[0, 2, 3, 2, 1, 2, 3, 2], gate=1)
        rel_at(p, T_MAR, start, MARR)

    def hit(p, r, sym, vol=0x60):
        """an ORCH HIT on the chord's root"""
        p.note(T_FX, r, voice(chord_notes(sym), 72)[0], orch, vol)
        rel_at(p, T_FX, r, ORCHR)
        p.off(T_FX, r + 6)

    # 1. intro: the hook alone from the first beat; the bass's octaves from bar 3, the drums from bar 5; a fill
    p = s.pattern(PAT, "intro: the hook alone, the FM BASS from bar 3, the drum machine from bar 5; a tom fill")
    hook(p, 0, 0x5C)
    hook(p, 64, 0x60)
    bass_seq(p, SP_CHORUS[1:] + SP_CHORUS, 0x64, start=16)
    drums(p, 64, PAT, chorus=True)
    p.note(T_TOM, 64, "C-4", crash, 0x54)
    chords(p, pad, PADR, SP_CHORUS, 0x44, start=64)
    p.ends(CH, 125)

    # 2. verse: the lighter beat, the bass, the MARIMBA's ostinato, POLY PAD held; VOICE LEAD sings
    p = s.pattern(PAT, "verse: VOICE LEAD sings over POLY PAD, the MARIMBA's ostinato, the bass's octaves")
    p.note(T_TOM, 0, "C-4", crash, 0x50)
    drums(p, 0, PAT)
    bass_seq(p, SP_VERSE * 2, 0x68)
    chords(p, pad, PADR, SP_VERSE * 2, 0x42)
    p.ends(CH, 125)
    mallets(p, SP_VERSE * 2, 0x3E)
    p.melody(T_LEAD, sing, SP_SING, 0x5C)
    rel_at(p, T_LEAD, 0, SINGR)

    # 3. chorus: the hook, BRASS SECT stabs, the claps and the kick on every beat; an ORCH HIT at its top
    p = s.pattern(PAT, "chorus: the hook, BRASS SECT stabs, claps; an ORCH HIT")
    p.note(T_TOM, 0, "C-4", crash, 0x58)
    drums(p, 0, PAT, chorus=True)
    bass_seq(p, SP_CHORUS * 2, 0x6A)
    stabs(p, SP_CHORUS * 2, 0x5C)
    hook(p, 0, 0x62)
    hook(p, 64, 0x64)
    hit(p, 0, "Dm")
    hit(p, 64, "Dm", 0x58)

    # 4. break: the drums out; FM VOICES on Bb C Dm Dm Bb C A A, an ORCH HIT on each Bb and C, the TUBE BELL; the
    # bass held; the FM TOMs rolling and the claps building into the last chorus
    p = s.pattern(PAT, "break: FM VOICES, ORCH HITs, a TUBE BELL; the toms and claps build")
    chords(p, choir, CHOIRR, SP_BREAK, 0x54, center=68)
    p.ends(CH, 120)
    bass_in(p, T_BASS, bass, SP_BREAK, 16, [(0, 0)], 0x60, lowest="D-3", gate=14)
    rel_at(p, T_BASS, 0, BASSR)
    for b in (0, 1, 4, 5):
        hit(p, 16 * b, SP_BREAK[b], 0x5A)
    p.melody(T_LEAD2, tube, SP_BELL, 0x56, 0)
    rel_at(p, T_LEAD2, 0, BELLR)
    p.ends([T_LEAD2], 120)
    tom_fill(p, 112, 16)
    roll(p, T_SNARE, clap, 0x5C, 96, PAT)

    # 5. peak: the last chorus with everything: the hook doubled an octave down on SAW LEAD, the stabs, the
    # MARIMBA, ORCH HITs
    p = s.pattern(PAT, "peak: the last chorus, the hook doubled by SAW LEAD, the stabs, the MARIMBA, ORCH HITs")
    p.note(T_TOM, 0, "C-4", crash, 0x60)
    drums(p, 0, PAT, chorus=True)
    bass_seq(p, SP_CHORUS * 2, 0x6C)
    stabs(p, SP_CHORUS * 2, 0x58)
    hook(p, 0, 0x64, double=True)
    hook(p, 64, 0x66, double=True)
    mallets(p, SP_CHORUS * 2, 0x44)
    hit(p, 64, "Dm", 0x62)

    # 6. outro: the groove goes on: the hook, then its Bb C A once more with the MARIMBA, an FM TOM fill, and the
    # last Dm on the downbeat of bar 8 (V-i): an ORCH HIT, the TUBE BELL, a crash, the chord left to ring out
    p = s.pattern(PAT, "outro: the hook, its Bb C A once more, a tom fill; the last Dm with an ORCH HIT")
    END = SP_CHORUS + ["Bb", "C", "A"]
    p.note(T_TOM, 0, "C-4", crash, 0x58)
    drums(p, 0, 112, chorus=True, fill=False)
    tom_fill(p, 104, 8)
    bass_seq(p, END, 0x68)
    stabs(p, END, 0x58)
    hook(p, 0, 0x60)
    hook(p, 64, 0x62, text=" ".join(SP_HOOK_BARS[1:]))
    mallets(p, END[4:], 0x44, start=64)
    p.chords(T_C1, pad, ["Dm"], 16, 0x58, center=66, start=112, tracks=list(CH))
    p.note(T_BASS, 112, "D-3", bass, 0x6A)
    hit(p, 112, "Dm", 0x64)
    p.note(T_LEAD2, 112, "D6", tube, 0x5C)
    rel_at(p, T_LEAD2, 112, BELLR)
    p.note(T_KICK, 112, "C-4", kick, 0x72)
    p.note(T_SNARE, 112, "C-4", clap, 0x60)
    p.note(T_TOM, 112, "C-4", crash, 0x60)

    let_go(s, [T_KICK, T_SNARE, T_HAT])
    let_go(s, [T_TOM], 8, only=crash)
    let_go(s, [T_TOM], 2, only=tom)
    s.play(*s.pats)
    # the 80s' big room: a long bright REVERB under the snare, the claps, the toms, the chords, the leads and
    # the hits (the kick and the bass stay dry)
    # the remix by the tracks' jobs (2026-10-10), against the lead: snare -1 dB, kick -2, the second lead
    # -2, bass -3, chords -5, the effects -5, toms -5, marimba -6, hats -12 (LUFS, each solo in the song)
    s.mix(T_KICK, level=85)
    s.mix(T_SNARE, level=106, reverb=74)
    s.mix(T_HAT, level=62, pan=168, reverb=18)
    s.mix(T_TOM, level=46, reverb=52)
    s.mix(T_BASS, level=81)
    for k, t in enumerate(CH):
        s.mix(t, level=42, pan=(72, 128, 184)[k], chorus=40, reverb=56)
    s.mix(T_LEAD, level=104, pan=128, delay=30, reverb=42)
    s.mix(T_LEAD2, level=101, pan=150, delay=30, reverb=50)
    s.mix(T_MAR, level=91, pan=88, delay=30, reverb=40)
    s.mix(T_FX, level=55, pan=110, reverb=62)
    s.fx(routing="sends", chorus={"on": True, "mode": "I", "mix": 50},
         delay={"on": True, "time": "1/8.", "feedback": 35, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 82, "damp": 30, "pre_delay": 30, "mix": 42})
    return s


# ============================================================ MIDNIGHT RUN ===

OUTRUN = ["F#m", "D", "E", "Bm"]                # i VI VII iv: the minor iv, dark and driving
# the gallop ("ta-da-da"): an eighth and two sixteenths a beat, the first accented; and root-octave-octave
GALLOP = [(b + r, 0, (0, -16, -10)[k]) for b in (0, 4, 8, 12) for k, r in enumerate((0, 2, 3))]
GALLOP8 = [(b + r, (0, 12, 12)[k], (0, -16, -10)[k]) for b in (0, 4, 8, 12) for k, r in enumerate((0, 2, 3))]


def midnight_run(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, snare, hat, tamb = J("909 Kick"), J("Gated Room"), J("Hat Closed"), J("Tambourine")
    crash, syn_tom = I("DRUM KIT", "CRASH"), J("Syn Tom")
    bass, arp, pwm, strings, brass = J("Night Bass"), J("Arp Saw"), J("PWM Pad"), J("Strings 80"), J("Poly Brass")
    hook, verse, glide = J("Super Saw"), J("Saw Lead"), J("Glide Lead")
    s = Song(pool, "MIDNIGHT RUN", "Outrun synthwave, F# minor, 130 BPM (F#m D E Bm: i VI VII iv). Shows the "
             "JUNO-106: Night Bass galloping (an eighth and two sixteenths a beat), its VCF opened and closed by "
             "commands, pumped by DUCK from the four-on-the-floor kick; sixteenth hats, the Gated Room snare, hard "
             "Syn Tom fills across the kit; Arp Saw's sixteenths from side to side into a ping-pong DELAY, Strings "
             "80 and the PWM Pad on CHORUS II; a Saw Lead gliding by TONE PORTA, a Super Saw hook of runs into held "
             "notes with the Juno's delayed vibrato, Poly Brass doubling it an octave down; the break a Glide Lead "
             "solo, gliding, then running in sixteenths", tempo=130, tracks=12, at_end="stop")
    T_KICK, T_SNARE, T_HAT, T_TOM, T_BASS, T_ARP, T_P1, T_P2, T_P3, T_LEAD, T_BRASS, T_FX = range(1, 13)
    PADS = (T_P1, T_P2, T_P3)
    K = Key("F#", "minor")
    # the verse: long notes gliding into each other (TONE PORTA 306 on each bar's), a short turn after some:
    # the moody lead of a night drive, not a sung line
    VERSE = ("C#5/12 E5/2 C#5/2 "
             "D5/12,306 F#5/2 D5/2 "
             "B4/8,306 E5/8 "
             "D5/16,306 "
             "C#5/12,306 E5/2 F#5/2 "
             "A5/16,306 "
             "G#5/8,306 E5/8 "
             "F#5/12,306 ./4")
    # the hook: sixteenths running up to a held note, a turn down; the same higher; a cadence on Bm (iv) back to
    # F#m: a synth lead's runs and held notes (the Juno's own vibrato on them: DCO LFO after an LFO DELAY)
    HOOK = ("C#5/1 E5/1 F#5/1 A5/1 C#6/12 "
            "B5/4 A5/4 F#5/8 "
            "E5/1 F#5/1 G#5/1 A5/1 B5/12 "
            "A5/4 F#5/4 D5/8 "
            "E5/1 F#5/1 A5/1 C#6/1 E6/12 "
            "D6/4 A5/4 F#5/8 "
            "B5/6 G#5/2 E5/4 G#5/4 "
            "D6/8 B5/8")
    # the break: a solo, four bars gliding (TONE PORTA), four running in sixteenths up and down the chords
    SOLO = ("C#5/16 A4/16,306 B4/16,306 D5/16,306 "
            "F#5/1 E5/1 C#5/1 A4/1 C#5/1 E5/1 F#5/1 A5/1 C#6/4 A5/2 F#5/2 "
            "D5/1 F#5/1 A5/1 D6/1 C#6/1 A5/1 F#5/1 D5/1 A5/8 "
            "E5/1 G#5/1 B5/1 E6/1 D6/1 B5/1 G#5/1 E5/1 B5/4 G#5/4 "
            "B4/1 D5/1 F#5/1 B5/1 D6/1 F#6/1 E6/1 D6/1 B5/8")
    VIBRATO = ("*40:3C", "*41:50", "*42:10")     # LFO RATE, LFO DELAY, DCO LFO
    # Night Bass made a pluck: ENV D short, SUSTAIN 0, RELEASE short; every gallop note a new pluck
    PLUCK = ("*4C:1C", "*4D:00", "*4E:04")

    HAT_PAN, TAMB_PAN = "8A8", "858"             # (one track: each hit pans itself, the hats right, the tambourine left)

    def drums(p, start, end, tambourine=False):
        """the four on the floor, the gated snare, sixteenth hats (the drum machine's drive), the tambourine's
        offbeats; a short Syn Tom fill closing every fourth bar"""
        p.hits(T_KICK, kick, "x...x...x...x...", 0x72, start, end)
        p.hits(T_SNARE, snare, "....x.......x...", 0x66, start, end)
        p.hits(T_HAT, hat, "xoxoxoxoxoxoxoxo", 0x44, start, end, soft=16, cmds=(HAT_PAN,))
        if tambourine:
            p.hits(T_HAT, tamb, "..x...x...x...x.", 0x48, start, end, cmds=(TAMB_PAN,))
        for b in range(start + 64, end, 128):
            tom_fill(p, b - 4, short=True)

    def tom_fill(p, start, short=False):
        """Syn Tom down the last two beats (short: the last beat), hard, across the kit (high left, low right)"""
        notes = list(zip(["E-5", "C#5", "A-4", "F#4"], ("850", "870", "890", "8B0")))
        step = 1 if short else 2
        for k, (n, pan) in enumerate(notes):
            p.note(T_TOM, start + step * k, n, syn_tom, 0x60 + 4 * k, pan, *(("*4C:14", "*4E:04") if k == 0 else ()))
            p.off(T_TOM, start + step * k + 1)

    def gallop(p, symbols, vol, figure=GALLOP, start=0):
        """each pluck cut at the end of its row (EC5): the gallop's notes never overlap"""
        bass_in(p, T_BASS, bass, symbols, 16, figure, vol, lowest="A-3", start=start, gate=1)
        p.cmd(T_BASS, start, *PLUCK)
        cut_notes(p, T_BASS, 5, start + 1)

    def arpeggio(p, start, end, vol):
        """Arp Saw's sixteenths, each cut two thirds in (EC4), each to the other side of the middle"""
        bars = (end - start) // 16
        p.arp(T_ARP, arp, (OUTRUN * 4)[:bars], 16, vol, step=1, center=64, start=start,
              pattern=[0, 1, 2, 3, 4, 5, 4, 3], gate=1)
        p.cmd(T_ARP, start, "*4E:04")
        for r in range(start, end):
            p.cmd(T_ARP, r, ("860", "8A0")[r % 2])
        cut_notes(p, T_ARP, 4, start, end)

    def pad(p, ins, start, bars, vol, rel="*4E:0E"):
        pad_chords(p, PADS, ins, (OUTRUN * 4)[:bars], 16, vol, center=66, start=start, cmds=(rel, "*53:02"))
        early_offs(p, PADS, start, start + 16 * bars)

    def lead(p, ins, text, vol):
        """the lead, legato (each note releases the one before; a TONE PORTA note glides from it), with the
        delayed vibrato; a rest lets go"""
        p.melody(T_LEAD, ins, text, vol, 0, legato=True)
        p.cmd(T_LEAD, 0, "*4E:06", *VIBRATO)
        r = 0
        for tok in text.split():
            name, _, ln = tok.split(",")[0].partition("/")
            if name == ".":
                p.off(T_LEAD, r)
            r += int(ln.partition("@")[0])

    def double(p, vol):
        """Poly Brass doubling the hook's held notes (four rows or more) an octave down: the lead made huge"""
        starts = [r for r in range(PAT) if p.cells[r][T_LEAD - 1] is not None
                  and p.cells[r][T_LEAD - 1][0] not in (None, OFF)]
        for k, r in enumerate(starts):
            nxt = starts[k + 1] if k + 1 < len(starts) else PAT
            if nxt - r >= 4:
                p.note(T_BRASS, r, p.cells[r][T_LEAD - 1][0] - 12, brass, vol, *(("*4E:06",) if k == 0 else ()))
                p.off(T_BRASS, min(nxt, PAT) - 1)

    # 1. intro: the gallop alone from the first beat, its filter opening; the pad under it; hats, then the kick
    p = s.pattern(PAT, "intro: the gallop alone, its filter opening; the pad, hats, then the kick; a Syn Tom fill")
    gallop(p, OUTRUN * 2, 0x68)
    p.sweep(T_BASS, 4, PAT, "*45", 8, 34, step=4)
    p.cmd(T_BASS, 0, "*45:08")
    pad(p, pwm, 0, 8, 0x4C)
    p.ends(PADS, 125)
    p.hits(T_HAT, hat, "xoxoxoxoxoxoxoxo", 0x3C, 32, 120, soft=14, cmds=(HAT_PAN,))
    p.hits(T_KICK, kick, "x...x...x...x...", 0x6C, 64, 120)
    tom_fill(p, 120)

    # 2. verse: the beat, the gallop under Strings 80, the verse's tune; the arpeggio from bar 5
    p = s.pattern(PAT, "verse: the beat, the gallop, Strings 80, the verse's tune; the arpeggio comes in")
    p.note(T_FX, 0, "C-4", crash, 0x58)
    drums(p, 0, PAT)
    gallop(p, OUTRUN * 2, 0x6C)
    p.cmd(T_BASS, 1, "*45:22")
    pad(p, strings, 0, 8, 0x50)
    p.ends(PADS, 125)
    lead(p, verse, VERSE, 0x5C)
    arpeggio(p, 64, PAT, 0x3C)
    p.sweep(T_ARP, 64, PAT, "*45", 24, 56, step=4)
    tom_fill(p, 120)

    # 3. chorus: the hook on the Super Saw doubled an octave down by Poly Brass, the gallop with octaves, the
    # arpeggio, the PWM Pad on CHORUS II
    p = s.pattern(PAT, "chorus: the Super Saw hook doubled by Poly Brass, the gallop in octaves, the arpeggio")
    p.note(T_FX, 0, "C-4", crash, 0x60)
    drums(p, 0, PAT, tambourine=True)
    gallop(p, OUTRUN * 2, 0x6C, GALLOP8)
    p.cmd(T_BASS, 1, "*45:26")
    pad(p, pwm, 0, 8, 0x50)
    p.ends(PADS, 125)
    arpeggio(p, 0, PAT, 0x42)
    p.cmd(T_ARP, 1, "*45:40")
    lead(p, hook, HOOK, 0x60)
    double(p, 0x4C)
    tom_fill(p, 120)

    # 4. break: the solo. Four bars without drums, the gallop's filter closing, the PWM Pad's width swept, the
    # Glide Lead gliding (TONE PORTA); then the kick and hats back, the filter opening, the solo running in
    # sixteenths, a gated snare roll into the last chorus
    p = s.pattern(PAT, "break: a Glide Lead solo, gliding, then running; the drums come back, a snare roll")
    p.off(T_ARP, 0)                              # (the chorus's last sixteenth let go)
    gallop(p, OUTRUN * 2, 0x64)
    p.sweep(T_BASS, 4, 64, "*45", 30, 6, step=4)
    p.sweep(T_BASS, 64, PAT, "*45", 6, 40, step=4)
    pad(p, pwm, 0, 8, 0x58)
    p.ends(PADS, 125)
    for t in PADS:
        p.sweep(t, 0, 96, "*43", 8, 110, step=8)
    lead(p, glide, SOLO, 0x5C)
    p.hits(T_KICK, kick, "x...x...x...x...", 0x68, 64, PAT)
    p.hits(T_HAT, hat, "xoxoxoxoxoxoxoxo", 0x40, 64, PAT, soft=14, cmds=(HAT_PAN,))
    for k, r in enumerate(range(96, 128)):
        if r < 112 and r % 2:
            continue
        p.note(T_SNARE, r, "C-4", snare, 0x34 + k, *(("E93",) if r >= 124 else ()))

    # 5. last chorus: everything; the brass under the hook's held notes
    p = s.pattern(PAT, "peak: the last chorus, the brass under the hook's held notes, everything")
    p.note(T_FX, 0, "C-4", crash, 0x66)
    drums(p, 0, PAT)
    gallop(p, OUTRUN * 2, 0x6E, GALLOP8)
    p.cmd(T_BASS, 1, "*45:28")
    pad(p, strings, 0, 8, 0x54, rel="*4E:08")     # (shorter tails: the brass is new here)
    p.ends(PADS, 125)
    arpeggio(p, 0, PAT, 0x44)
    p.cmd(T_ARP, 1, "*45:48")
    lead(p, hook, HOOK, 0x62)
    starts = [r for r in range(PAT) if p.cells[r][T_LEAD - 1] is not None and p.cells[r][T_LEAD - 1][0] is not None]
    for r in range(PAT):                         # the brass under the hook's notes of four rows or more (not its
        c = p.cells[r][T_LEAD - 1]               # runs): on a chord tone the chord's next tone down, else a
        nxt = next((q for q in starts if q > r), PAT)   # third under in F# minor's steps
        if c is not None and c[0] not in (None, OFF) and nxt - r >= 4:
            pcs = {n % 12 for n in chord_notes(OUTRUN[(r // 16) % 4])}
            under = K.up(c[0], -2)
            if c[0] % 12 in pcs or any((under - q) % 12 in (1, 11) for q in pcs):
                under = max(n for n in range(c[0] - 9, c[0] - 2) if n % 12 in pcs)
            p.note(T_BRASS, r, under, brass, 0x4C, *(("*4E:04",) if r == 0 else ()))
    p.ends([T_BRASS], 124)
    tom_fill(p, 120)

    # 6. outro: the beat thins, the gallop's filter closes and it stops; the last F#m rings with the lead's F#
    p = s.pattern(PAT, "outro: the beat thinning, the gallop closing down; the last chord rings")
    p.note(T_FX, 0, "C-4", crash, 0x5C)
    p.off(T_ARP, 0)                              # (the peak's last sixteenth let go)
    drums(p, 0, 64)
    p.hits(T_KICK, kick, "x...x...x...x...", 0x6C, 64, 96)
    gallop(p, OUTRUN * 2, 0x68)
    p.sweep(T_BASS, 4, 96, "*45", 30, 8, step=4)
    p.clear(T_BASS, 96)
    pad(p, pwm, 0, 6, 0x50)
    p.ends(PADS, 94)
    lead(p, verse, VERSE[:VERSE.index("C#5/12,306 E5/2 F#5/2")], 0x58)
    p.off(T_LEAD, 64)
    p.chords(T_P1, strings, [["F#3", "A-3", "C#4"]], 32, 0x5C, start=96, tracks=list(PADS))
    p.note(T_BASS, 96, "F#4", bass, 0x6A, "*4C:60", "*4D:40", "*4E:30")
    p.off(T_BASS, 112)
    p.note(T_KICK, 96, "C-4", kick, 0x72)
    p.note(T_FX, 96, "C-4", crash, 0x5C)
    p.note(T_LEAD, 96, "F#5", hook, 0x54)
    p.ends([T_LEAD], 124)

    let_go(s, [T_KICK, T_HAT])
    let_go(s, [T_SNARE], 2)                      # (the gated snare: its gate two rows)
    let_go(s, [T_FX], 8, only=crash)             # (the sampled crash: let go once it has rung)
    s.play(*s.pats)
    # the remix by the tracks' jobs (2026-10-10), against the kick: the gated snare -1 dB, the lead -2, the
    # gallop -3, brass -5, toms -5, the arp -6, pads -7, the effects -8, hats -12 (LUFS, each solo)
    s.mix(T_KICK, level=104)
    s.mix(T_SNARE, level=90, reverb=60)
    s.mix(T_HAT, level=50, pan=168)
    s.mix(T_TOM, level=49, reverb=35)
    s.mix(T_BASS, level=127, duck=90)
    s.mix(T_ARP, level=28, delay=35, duck=60)
    for k, t in enumerate(PADS):
        s.mix(t, level=34, pan=(72, 128, 184)[k], reverb=40, duck=80)
    s.mix(T_LEAD, level=64, delay=30, reverb=35)
    s.mix(T_BRASS, level=67, pan=84, reverb=35, duck=40)
    s.mix(T_FX, level=65, pan=144, reverb=40)
    s.fx(routing="sends", delay={"on": True, "time": "1/8.", "feedback": 35, "ping_pong": True, "mix": 28},
         reverb={"on": True, "size": 75, "damp": 35, "mix": 35},
         duck={"on": True, "source": 1, "depth": 55, "release": 120})
    return s


# =============================================================== ACID TEST ===

# the 303's lines in A minor's steps from the bar's root (0 the root, 2 its third, 4 its fifth, 7 the octave,
# 9 and 11 the octave's third and fifth, 6 and 10 the steps under them): (step, flags: 1 ACC, 2 SLD, 3 both)
TECHNO = [(0, 1), (0, 0), (7, 2), (0, 0), (9, 0), (0, 1), (0, 0), (6, 2), (0, 0), (7, 1), (0, 0), (4, 0), (0, 1),
          (0, 0), (9, 2), (10, 0)]      # acid techno: sixteenths on the root, never a rest, octaves slid
ANSWER = [None, None, (7, 1), (9, 2), None, (7, 0), None, (11, 1), None, (9, 2), (7, 0), None, (4, 1), None,
          (7, 2), (6, 0)]               # the second 303 an octave up, answering between
TRANCE = [(0, 1), (0, 0), (7, 2), (0, 0), (4, 0), (0, 0), (2, 1), (7, 0), (0, 0), (4, 2), (7, 0), (0, 0), (2, 1),
          (0, 0), (9, 2), (7, 0)]       # the breakdown's: the chord's own tones (root, third, fifth, octave)
ROLLING = [None, (0, 0), (0, 1), (0, 0), None, (0, 0), (7, 2), (0, 0), None, (0, 0), (0, 1), (0, 0), None, (7, 0),
           (7, 2), (0, 0)]              # the drop's: a rolling bass, the three sixteenths after each kick
# acid trance: i i VI VII a bar each (no major I: Am F C G is pop's axis); the riff a sixteenth arpeggio in the
# trance gate's syncopation (indexes into the chord, 3 and 4 an octave up); the lead long notes gliding (TONE
# PORTA), the melody of the drop and, its first half, of the breakdown
TR_PROG, TR_ROOTS = ["Am", "Am", "F", "G"], ["A-1", "A-1", "F-2", "G-2"]
RIFF = [3, None, 2, 1, None, 2, 0, None, 3, None, 2, 1, None, 4, None, 2]
GATE16 = "x.xx.xx.x.xx.xx."
SOAR = ("E5/16 A5/8 G5/4 E5/4 F5/8 A5/8 G5/12 B5/4 "
        "C6/16 A5/8 C6/4 A5/4 F5/4 A5/4 C6/8 B5/8 G5/4 D5/4")      # between D5 and C6, each note struck
SOAR_OUT = ("C6/16 A5/8 C6/4 A5/4 F5/4 A5/4 C6/8 B5/8 G5/4 D5/4 "
            "A5/12 F5/4 G5/8 B5/8 A5/32")                          # its second half, F G, home on A


def acid_test(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, clap, snare, ch, oh, rim = J("Tech Kick"), J("Clap"), J("Snare"), J("Hat Closed"), J("Hat Open"), J("Rim")
    ride, crash, swp = I("FM DRUM KIT", "FM RIDE"), I("DRUM KIT", "CRASH"), J("Noise Swp")
    saw_pad, supersaw, lead, pluck = J("Saw Pad"), J("Super Saw"), J("Trance Ld"), J("Prog Pluck")
    tsaw = J("Trance Saw")
    hard, wet, sq = I("TB-303", "Hard Acid"), I("TB-303", "Wet Acid"), I("TB-303", "Res Sq")
    s = Song(pool, "ACID TEST", "Acid techno into acid trance, A minor, 138 BPM. Shows the TB-303: its line alone "
             "first, relentless sixteenths with accents and slides through the DRIVE, CUTOFF, RESONANCE, ENV MOD and "
             "DECAY turned by commands, a second 303 answering an octave up from side to side; then the DRIVE off for "
             "acid trance on Am Am F G: a breakdown (Saw Pad, a sixteenth riff on a pluck, the 303 in a ping-pong "
             "DELAY, a snare roll, the Trance Ld's melody) and the drop (Trance Saw chords in the trance gate pumped "
             "by DUCK, the riff, the melody on the Super Saw, the 303 a rolling bass)", tempo=138, tracks=12,
             at_end="stop")
    T_KICK, T_CLAP, T_HAT, T_PERC, T_RIDE, T_TB1, T_TB2, T_C1, T_C2, T_C3, T_LEAD, T_FX = range(1, 13)
    CH = [T_C1, T_C2, T_C3]
    K = Key("A", "minor")
    FOUR, BACK, HATS, RIM = "x...x...x...x...", "....x.......x...", "c.o.c.o.c.o.cCo.", "...x..x....x..x."
    A, C = "A-1", "C-2"                          # (no 303 under 55 Hz)
    SOAR_A = SOAR[:SOAR.index("C6/16")]          # its first four bars

    def beat(p, start, end, ride_from=None, louder=0, techno=True):
        """the kick, the clap, the hats; techno's rim and its ride on the eighths (trance's ride: the
        offbeats only)"""
        p.hits(T_KICK, kick, FOUR, 0x74 + louder, start, end)
        p.hits(T_CLAP, clap, BACK, 0x5C + louder, start, end)
        hats(p, T_HAT, ch, oh, HATS, 0x4C + louder, start, end)
        if techno:
            p.hits(T_PERC, rim, RIM, 0x3C, start, end)
        if ride_from is not None:
            p.hits(T_RIDE, ride, "x.x.x.x.x.x.x.x." if techno else "..x...x...x...x.", 0x40, ride_from, end)

    def riff(p, symbols, vol, start=0):
        """the trance riff on the Prog Pluck (T_TB2), a bar a chord, each note cut half a row in (EC3)"""
        for b, sym in enumerate(symbols):
            notes = voice(chord_notes(sym), 69)
            for k, i in enumerate(RIFF):
                if i is not None:
                    p.note(T_TB2, start + 16 * b + k, notes[i % 3] + 12 * (i // 3), pluck, vol)
        cut_notes(p, T_TB2, 3, start, start + 16 * len(symbols))

    def gated(p, symbols, vol, start=0):
        """Trance Saw's chords chopped in the trance gate's rhythm, each cut half a row in; round C4, under the
        lead"""
        for b, sym in enumerate(symbols):
            for k, c in enumerate(GATE16):
                if c == "x":
                    for t, n in zip(CH, voice(chord_notes(sym), 60)):
                        p.note(t, start + 16 * b + k, n, tsaw, vol)
        for t in CH:
            cut_notes(p, t, 3, start, start + 16 * len(symbols))

    def soar(p, text, vol, start=0, ins=None):
        """the lead's long notes, legato, its release set: the Trance Ld in the breakdown, the Super Saw in the
        drop and the outro (the Trance Ld "sounds really bad there", an octave down or up)"""
        p.melody(T_LEAD, ins or lead, text, vol, start, legato=True)
        p.cmd(T_LEAD, start, "*4E:0C")

    def tb(p, track, ins, line, roots, vol, start=0, end=PAT, pans=()):
        """a 303 line (acid_steps); `pans`: each note to the next of them (8xx) in turn"""
        acid_steps(p, track, ins, line, roots, vol, start, end, K)
        k = 0
        for r in range(start, end):
            c = p.cells[r][track - 1]
            if pans and c is not None and c[0] not in (None, OFF):
                p.cmd(track, r, pans[k % len(pans)])
                k += 1

    # 1. intro: the 303 alone through the DRIVE for two bars, its CUTOFF opening; hats from bar 3, the kick from
    # bar 5, the clap and the rim from bar 7
    p = s.pattern(PAT, "intro: the 303 alone through the DRIVE, its CUTOFF opening; hats, the kick, the clap")
    tb(p, T_TB1, hard, TECHNO, [A], 0x64)
    p.cmd(T_TB1, 0, "*41:14", "*42:64", "*43:32")
    p.cmd(T_TB1, 1, "*44:23")
    p.sweep(T_TB1, 4, PAT, "*41", 20, 64, step=4)
    hats(p, T_HAT, ch, oh, "c.c.c.c.c.c.c.c.", 0x40, 32, 64)
    hats(p, T_HAT, ch, oh, HATS, 0x48, 64, PAT)
    p.hits(T_KICK, kick, FOUR, 0x70, 64, PAT)
    p.hits(T_CLAP, clap, BACK, 0x58, 96, PAT)
    p.hits(T_PERC, rim, RIM, 0x38, 96, PAT)

    # 2. groove: acid techno, the 303 relentless on A: ENV MOD and RESONANCE up, the DRIVE turned up; the ride
    # from bar 5
    p = s.pattern(PAT, "groove: acid techno, the 303 relentless; ENV MOD and the DRIVE turned up, the ride")
    p.note(T_FX, 0, "C-4", crash, 0x50)
    beat(p, 0, PAT, ride_from=64)
    tb(p, T_TB1, hard, TECHNO, [A], 0x66)
    p.cmd(T_TB1, 1, "*42:6E")
    p.sweep(T_TB1, 0, PAT, "*43", 50, 100, step=4)
    p.sweep(T_TB1, 2, PAT, "*41", 64, 80, step=8)
    p.sweep(T_FX, 4, PAT, "#96", 25, 40, step=8)

    # 3. peak of the techno: a second 303 (square) answers an octave up, from side to side; the line lifts to C
    # in bars 4 and 8; the beat drops out for the last bar, a Noise Swp into the break
    p = s.pattern(PAT, "peak: a second 303 answers an octave up, side to side; the beat drops out")
    p.note(T_FX, 0, "C-4", crash, 0x58)
    beat(p, 0, 112, ride_from=0, louder=2)
    tb(p, T_TB1, hard, TECHNO, [A, A, A, C], 0x58)
    p.cmd(T_TB1, 1, "*41:5A", "*43:64", "*44:30")
    p.sweep(T_TB1, 64, 120, "*41", 90, 110, step=8)
    tb(p, T_TB2, sq, ANSWER, ["A-2", "A-2", "A-2", "C-3"], 0x4E, pans=("850", "8B0"))
    p.cmd(T_TB2, 2, "*41:40", "*42:70")          # (its first note's row)
    p.cmd(T_TB2, 3, "*43:50")
    p.note(T_FX, 96, "C-4", swp, 0x58, "*4E:0C")

    # 4. break: the DRIVE off, acid trance: no kick; the Saw Pad on Am Am F G, the riff on the Prog Pluck its VCF
    # opening, the 303 (Wet Acid) low in the ping-pong DELAY; the lead's first half from bar 5, a snare roll, a
    # Noise Swp
    p = s.pattern(PAT, "break: the DRIVE off for acid trance: the Saw Pad, the riff on a pluck, the 303 in the "
                  "DELAY, a snare roll")
    p.note(T_FX, 0, "C-4", crash, 0x58, fx_off("DRIVE"))
    pad_chords(p, CH, saw_pad, TR_PROG * 2, 16, 0x5A, center=69, cmds=("*4E:24",))
    p.ends(CH, 116)                              # (its tails gone by the drop)
    riff(p, TR_PROG * 2, 0x56)
    p.cmd(T_TB2, 0, "*45:14", "*4E:08", "8A0")   # (the track's pan back from the second 303's left and right)
    p.sweep(T_TB2, 8, PAT, "*45", 22, 50, step=8)
    tb(p, T_TB1, wet, TRANCE, TR_ROOTS * 2, 0x5C)
    p.cmd(T_TB1, 1, "*41:18", "*42:76", "=82:60")
    p.sweep(T_TB1, 4, 120, "*41", 24, 56, step=8)
    p.cmd(T_TB1, 120, "=82:20")
    soar(p, SOAR_A, 0x54, 64)
    roll(p, T_CLAP, snare, 0x5C, 64, PAT)
    p.cmd(T_CLAP, 64, "*4E:08")                  # (each hit's tail short: a roll of sixteenths)
    p.note(T_FX, 96, "C-4", swp, 0x58, "*4E:0C")

    # 5. drop: the kick back; Trance Saw's chords in the trance gate, pumped by DUCK; the riff open, the melody on
    # the Super Saw over it; the 303 a rolling bass between the kicks
    p = s.pattern(PAT, "drop: the kick back, Trance Saw chords in the trance gate, the riff, the Super Saw's "
                  "melody, the 303 rolling")
    p.note(T_FX, 0, "C-4", crash, 0x64)
    beat(p, 0, PAT, ride_from=0, louder=4, techno=False)
    gated(p, TR_PROG * 2, 0x76)
    p.cmd(T_C1, 0, "*4E:06")
    riff(p, TR_PROG * 2, 0x62)
    p.cmd(T_TB2, 1, "*45:32")
    tb(p, T_TB1, wet, ROLLING, TR_ROOTS * 2, 0x72)
    p.cmd(T_TB1, 1, "*41:50", "*43:5A", "=82:20")
    p.sweep(T_TB1, 64, PAT, "*41", 80, 100, step=8)
    soar(p, SOAR, 0x70, ins=supersaw)

    # 6. outro: the drop goes on, the lead's second half lands home on A at bar 7; the gate stops after bar 4 (the
    # chords held), the riff's VCF closes, the beat stops after bar 6; the last Am, the 303 sliding down
    p = s.pattern(PAT, "outro: the lead's second half lands home on A, the riff closes, the beat stops; the last "
                  "chord, the 303 slides down")
    p.note(T_FX, 0, "C-4", crash, 0x58)
    beat(p, 0, 96, techno=False)
    gated(p, TR_PROG, 0x5C)
    pad_chords(p, CH, tsaw, ["F", "G"], 16, 0x58, center=60, start=64, cmds=("*4E:10",))
    early_offs(p, CH, 64, 96)
    riff(p, TR_PROG + ["F", "G"], 0x56)
    p.sweep(T_TB2, 0, 96, "*45", 50, 18, step=8)
    tb(p, T_TB1, wet, ROLLING, TR_ROOTS + ["F-2", "G-2"], 0x64, end=96)
    p.sweep(T_TB1, 1, 96, "*41", 90, 30, step=4)   # (from its first note: the line rests on the kick)
    soar(p, SOAR_OUT, 0x68, ins=supersaw)
    p.chords(T_C1, saw_pad, ["Am"], 32, 0x5C, center=62, start=96, tracks=CH, cmds=("*4E:40",))
    p.ends(CH, 120)
    p.ends([T_LEAD], 122)
    p.note(T_TB1, 96, "A-2", wet, 0x64, "A+S", "*41:30")
    p.note(T_TB1, 100, "A-1", wet, 0x60)
    p.off(T_TB1, 112)
    p.note(T_KICK, 96, "C-4", kick, 0x78)
    p.note(T_FX, 96, "C-4", crash, 0x60)

    let_go(s, [T_KICK, T_CLAP, T_HAT, T_PERC, T_RIDE])
    let_go(s, [T_FX], 8, only=crash)
    s.play(*s.pats)
    # the remix by the tracks' jobs (2026-10-10), against the kick: the 303 -2 dB, clap -3, the lead -3, the
    # trance chords -5, the second 303 -6, the effects -8, perc -10, hats and ride -12 (LUFS, each solo)
    s.mix(T_KICK, level=120)
    s.mix(T_CLAP, level=124, reverb=30)
    s.mix(T_HAT, level=51, pan=168)
    s.mix(T_PERC, level=127, pan=72, delay=20)
    s.mix(T_RIDE, level=40, pan=100)
    s.mix(T_TB1, level=85, delay=20)
    s.mix(T_TB2, level=70, delay=35, reverb=20)
    for k, t in enumerate(CH):
        s.mix(t, level=55, pan=(64, 128, 192)[k], reverb=40, duck=90)
    s.mix(T_LEAD, level=78, delay=30, reverb=40, duck=20)
    s.mix(T_FX, level=57, pan=144, reverb=30)
    s.fx(routing="sends", drive={"on": True, "drive": 25, "tone": 50},
         delay={"on": True, "time": "1/8.", "feedback": 45, "ping_pong": True, "mix": 32},
         reverb={"on": True, "size": 70, "damp": 35, "mix": 30},
         duck={"on": True, "source": 1, "depth": 65, "release": 120})
    return s


# ============================================================== SPICE ROAD ===

DOR = ["Dm7", "G", "Dm7", "G", "Dm7", "G", "C", "Dm7"]


def strum(p, tracks, open_ins, mute_ins, symbols, each, rhythm, vol, center=55, start=0):
    """a guitar's strums on three tracks, a string each: D down (low to high, a tick apart by EDx), U up
    (high to low), M a palm-muted strum, . nothing; each chord over `each` rows"""
    r = start
    for sym in symbols:
        notes = voice(chord_notes(sym), center)[:3]
        while len(notes) < 3:
            notes.append(notes[-1] + 12)
        for k in range(each):
            c = rhythm[k % len(rhythm)]
            if c == ".":
                continue
            ins = mute_ins if c == "M" else open_ins
            v = vol + {"D": 0, "U": -14, "M": -8}[c]
            order = range(3) if c in "DM" else range(2, -1, -1)
            for tick, i in enumerate(order):         # (R4 80: a string stopped by the next stroke stops at once)
                p.note(tracks[i], r + k, notes[i], ins, v, *((f"ED{tick}",) if tick else ()), "*4B:50")
        r += each


def spice_road(pool):
    D, P, BO = (lambda n: I("DRUM KIT", n)), (lambda n: I("PERCUSSION", n)), (lambda n: I("BAND/ORCH", n))
    kick, snare, hat, ohat, shaker = D("KICK"), D("SNARE"), D("HAT"), D("HAT OPEN"), D("SHAKER")
    crash, choke, t_lo, t_mid, t_hi = D("CRASH"), D("CRSH CHOKE"), D("TOM LO"), D("TOM MID"), D("TOM HI")
    chime, rise, darbuka = P("WIND CHIME"), P("CHIME RISE"), P("DARBUKA")
    c_hi, conga, bongo = P("CONGA HI"), P("CONGA"), P("BONGO LO")
    na, ge, tun, timp = P("TABLA NA"), P("TABLA GE"), P("TABLA TUN"), P("TIMPANI")
    p_mute, dist, steel, s_mute, g_mute = BO("PIANO MUTE"), BO("DIST GTR"), BO("STEEL GTR"), BO("STEEL MUTE"), \
        BO("GTR MUTE")
    vln, v_stab, v_swell, sax, s_stab, flute, upright = BO("VIOLINS"), BO("VLN STAB"), BO("VLN SWELL"), \
        BO("TENOR SAX"), BO("SAX STAB"), BO("FLUTE"), BO("UPRIGHT")
    s = Song(pool, "SPICE ROAD", "World fusion, D Dorian into D Hijaz, 96 BPM. Shows the samplers: the natural "
             "kit, steel guitar strummed string by string, palm mutes, the muted guitar's riff, upright bass, the "
             "piano's muted stabs, a distorted guitar; the turn (toms, CHIME RISE, VLN SWELL) to tablas, darbuka, "
             "congas, timpani, violins, the flute's melody and the TENOR SAX's solo", tempo=96, tracks=12,
             at_end="stop")
    T_KICK, T_SNARE, T_HAT, T_TOM, T_BASS, T_G1, T_G2, T_G3, T_RIFF, T_LEAD, T_PC1, T_PC2 = range(1, 13)
    GTR = (T_G1, T_G2, T_G3)
    STRUM = "D..MD.U.M.D.U.M."
    # (the muted guitar near its recorded E2, the upright near its C#2: each a dozen semitones at most away)
    RIFF = "D-2/2 ./2 D-2/2 F-2/2 D-2/2 G-2/2 ./2 A-2/2 D-2/2 ./2 C-3/2 ./2 A-2/2 G-2/2 F-2/4"   # Dm7 | G
    KD = Key("D", "dorian")
    BASS = [(0, 0, 0), (6, 0, -10), (8, -3, 0), (12, 2, -8), (14, 4, -6)]    # scale steps: root, 5th under, 3rd, 5th

    def upright_line(p, symbols, vol, start=0):
        r = start
        for sym in symbols:
            root = chord_notes(sym, 2)[0]
            for rr, st, dv in BASS:
                p.note(T_BASS, r + rr, KD.up(root, st), upright, vol + dv)
            r += 16
    D5, EB, DF, CM = ["D-4", "A-4", "D-5"], ["Eb4", "G-4", "Bb4"], ["D-4", "F#4", "A-4"], ["C-4", "Eb4", "G-4"]

    def kit(p, start, end):
        p.hits(T_KICK, kick, "x.....x...x.....", 0x6C, start, end)
        p.hits(T_SNARE, snare, "....x.......x...", 0x64, start, end)
        p.hits(T_HAT, hat, "x.xox.xox.xox.x.", 0x48, start, end)
        p.hits(T_HAT, ohat, "..............x.", 0x48, start, end)

    def tom_fill(p, start):
        for k, (r, t) in enumerate(zip(range(start, start + 8), [t_hi, t_hi, t_mid, t_mid, t_lo, t_lo, t_lo, t_lo])):
            p.note(T_TOM, r, "C-4", t, 0x58 + 3 * k)

    def flute_(p, text, vol, start=0):
        p.melody(T_LEAD, flute, text, vol, start)
        p.cmd(T_LEAD, start, "*4B:50")           # R4 80: a breath ends when the next begins

    # 1. intro: the kit alone (two bars), the steel guitar from bar 3, the upright from bar 5
    p = s.pattern(PAT, "intro: the kit, then the steel guitar's strums, then the upright")
    kit(p, 0, PAT)
    p.note(T_TOM, 0, "C-4", crash, 0x50)
    tom_fill(p, 24)
    strum(p, GTR, steel, s_mute, DOR[2:], 16, STRUM, 0x5C, start=32)
    upright_line(p, DOR[4:8], 0x68, start=64)
    tom_fill(p, 120)

    # 2. groove: the muted riff answers the strums; a shaker
    p = s.pattern(PAT, "groove: strums, the palm-muted riff, upright, shaker")
    p.note(T_TOM, 0, "C-4", crash, 0x58)
    kit(p, 0, PAT)
    p.hits(T_PC1, shaker, "xoxoxoxoxoxoxoxo", 0x40, 0, PAT)
    strum(p, GTR, steel, s_mute, DOR, 16, STRUM, 0x58)
    upright_line(p, DOR, 0x6C)
    for b in range(0, 6, 2):
        p.melody(T_RIFF, g_mute, RIFF, 0x60, 16 * b)
    p.melody(T_RIFF, g_mute, "C-2/2 ./2 C-2/2 E-2/2 G-2/2 ./2 A-2/2 G-2/2 D-2/2 ./2 D-2/2 F-2/2 A-2/2 C-3/2 D-3/4",
             0x60, 96)

    # 3. build: the piano's muted stabs, the distorted guitar; the turn in the last bar
    p = s.pattern(PAT, "build: piano stabs, distorted guitar; the turn: toms, a chime rising, a violin swell")
    p.note(T_TOM, 0, "C-4", crash, 0x60)
    kit(p, 0, 112)
    p.hits(T_PC1, shaker, "xoxoxoxoxoxoxoxo", 0x44, 0, 96)
    strum(p, GTR, steel, s_mute, DOR[:7], 16, STRUM, 0x58)
    upright_line(p, DOR[:7], 0x6C)
    for b, sym in enumerate(DOR[:7]):
        for rr in (2, 10):
            p.note(T_RIFF, 16 * b + rr, voice(chord_notes(sym), 64)[-1], p_mute, 0x5C)
    p.melody(T_LEAD, dist, "D-2/12 C-2/4 D-2/8 F-2/4 G-2/4 D-2/12 C-2/4 A-1/16 "
             "D-2/12 C-2/4 D-2/8 F-2/4 G-2/4 ./32", 0x58, 0)
    # the turn (bar 8): toms, the chime rising, the violins swelling in, a grabbed crash on the last row
    tom_fill(p, 112)
    p.note(T_PC1, 104, "C-4", rise, 0x60)
    p.chords(T_G1, v_swell, [D5], 16, 0x5C, start=112, cmds=("*4B:50",))
    p.note(T_BASS, 112, "D-2", upright, 0x64)
    p.note(T_KICK, 112, "C-4", kick, 0x60)
    p.note(T_PC2, 127, "C-4", choke, 0x60)

    # 4. the world: D Hijaz over a drone; tablas, darbuka, the wind chime; the flute's melody
    p = s.pattern(PAT, "world: tablas, darbuka, the violins' drone, the flute in D Hijaz")
    p.note(T_TOM, 0, "C-4", timp, 0x64)
    p.note(T_PC2, 0, "C-4", chime, 0x58)
    pad_chords(p, GTR, vln, [D5, D5, EB, DF, D5, D5, CM, DF], 16, 0x54, cmds=("*4B:3E",))
    p.hits(T_PC1, na, "x..x..x.x..x..x.", 0x58, 0, PAT)
    p.hits(T_PC2, ge, "x.....x.....x...", 0x60, 16, PAT)
    p.hits(T_HAT, darbuka, "x..o..x.x.o.x...", 0x5C, 0, PAT)
    p.hits(T_KICK, kick, "x...............", 0x58, 32, PAT)
    p.bassline(T_BASS, upright, ["D", "D", "Eb", "D", "D", "D", "C", "D"], 16, [(0, 0), (6, 7, -8), (8, 0),
                                                                               (14, 7, -10)], 0x64, octave=2)
    flute_(p, "A-5/4 Bb5/2 A-5/2 G-5/4 F#5/4 "
             "G-5/6 F#5/2 Eb5/4 D-5/4 "
             "D-5/2 Eb5/2 F#5/4 G-5/4 A-5/4 "
             "A-5/16 "
             "C-6/4 Bb5/2 A-5/2 Bb5/4 A-5/4 "
             "G-5/6 A-5/2 F#5/8 "
             "Eb5/4 F#5/4 G-5/4 Eb5/4 "
             "D-5/16", 0x60, 0)
    p.note(T_TOM, 120, "C-4", tun, 0x58)

    # 5. the peak: the tenor sax's solo, its stabs answering; congas and bongo; the kit back; violin stabs
    p = s.pattern(PAT, "peak: the tenor sax's solo in Hijaz, congas, tablas, the kit back, violin stabs")
    p.note(T_TOM, 0, "C-4", timp, 0x68)
    p.hits(T_KICK, kick, "x.....x...x.....", 0x68, 0, PAT)
    p.hits(T_SNARE, snare, "....x.......x...", 0x5C, 0, PAT)
    p.hits(T_HAT, darbuka, "x..o..x.x.o.x...", 0x5C, 0, PAT)
    p.hits(T_PC1, na, "x..x..x.x..x..x.", 0x54, 0, PAT)
    p.hits(T_PC2, conga, "..x...x...x.x.x.", 0x5C, 0, PAT)
    p.hits(T_PC2, c_hi, "...x.......x....", 0x58, 0, PAT)
    p.hits(T_PC2, bongo, "...............x", 0x50, 0, PAT)
    p.bassline(T_BASS, upright, ["D", "D", "D", "Eb", "D", "D", "D", "C"], 16,
               [(0, 0), (6, 7, -8), (8, 0), (11, 7, -8), (14, 12, -10)], 0x68,
               octave=2)
    p.stabs(T_G1, v_stab, [D5, D5, D5, EB, D5, D5, D5, CM], 16, "x.....x...x.....", 0x50, gate=3)
    p.melody(T_LEAD, sax, "D-4/2 F#4/2 A-4/2 C-5/2 Bb4/4 A-4/4 "
             "G-4/2 A-4/2 Bb4/2 A-4/2 G-4/2 F#4/2 Eb4/4 "
             "D-4/4 ./2 F#4/2 A-4/8 "
             "C-5/2 Bb4/2 A-4/2 G-4/2 F#4/8 "
             "A-4/2 Bb4/2 C-5/4 Bb4/2 A-4/2 C-5/4 "
             "C-5/2 Bb4/2 A-4/4 G-4/2 F#4/2 G-4/4 "
             "A-4/6 Bb4/2 A-4/4 G-4/4 "
             "F#4/4 Eb4/4 D-4/8", 0x64, 0)
    for r in (56, 60, 120, 124):
        p.note(T_RIFF, r, "D-4", s_stab, 0x58)
    p.note(T_TOM, 64, "C-4", timp, 0x60)

    # 6. outro: the flute and the sax trade phrases, the drums thin out; a last timpani, a grabbed crash, the
    # violins' last D rings
    p = s.pattern(PAT, "outro: flute and sax trade, the drums thin; the last timpani and a grabbed crash")
    p.hits(T_PC1, na, "x..x..x.x..x..x.", 0x50, 0, 96)
    p.hits(T_HAT, darbuka, "x..o..x.x.o.x...", 0x54, 0, 64)
    p.hits(T_PC2, ge, "x.....x.....x...", 0x58, 0, 96)
    p.hits(T_KICK, kick, "x.......x.......", 0x60, 0, 64)
    p.bassline(T_BASS, upright, ["D", "D", "Eb", "D", "D", "D"], 16, [(0, 0), (8, 7, -8)], 0x60, octave=2)
    pad_chords(p, GTR, vln, [D5, D5, EB, DF, D5, D5], 16, 0x50, cmds=("*4B:3E",))
    flute_(p, "A-5/4 Bb5/2 A-5/2 G-5/4 F#5/4 G-5/6 F#5/2 Eb5/4 D-5/4", 0x5C, 0)
    p.melody(T_RIFF, sax, "D-4/2 F#4/2 A-4/2 C-5/2 Bb4/4 A-4/4 G-4/6 F#4/2 Eb4/8", 0x5C, 32)
    flute_(p, "D-5/2 Eb5/2 F#5/4 G-5/4 A-5/4 Bb5/8 A-5/8", 0x5C, 64)
    p.note(T_RIFF, 80, "D-4", sax, 0x58)
    p.off(T_RIFF, 94)
    p.chords(T_G1, vln, [D5], 32, 0x58, start=96)
    p.note(T_BASS, 96, "D-2", upright, 0x64)
    p.note(T_TOM, 96, "C-4", timp, 0x6C)
    p.note(T_PC2, 96, "C-4", crash, 0x58)
    p.note(T_PC2, 104, "C-4", choke, 0x50)
    p.note(T_LEAD, 96, "D-6", flute, 0x54)
    p.ends([T_LEAD, T_G1, T_G2, T_G3], 126)

    let_go(s, [T_RIFF], 3, only=p_mute)          # (the muted piano's stabs: held, though silent, till let go)
    s.play(*s.pats)
    # the remix by the tracks' jobs (2026-10-10), against the lead: snare -2 dB, kick -3, the upright -3, the
    # guitars -4, the riff -5, toms -5, the hand drums -6, hats -12 (LUFS, each solo in the song)
    s.mix(T_KICK, level=123)
    s.mix(T_SNARE, level=91, reverb=25)
    s.mix(T_HAT, level=45, pan=168)
    s.mix(T_TOM, level=58, reverb=25)
    s.mix(T_BASS, level=93)
    for k, t in enumerate(GTR):
        s.mix(t, level=55, pan=(72, 100, 128)[k], reverb=25)
    s.mix(T_RIFF, level=108, pan=184, reverb=20)
    s.mix(T_LEAD, level=101, pan=128, reverb=40, delay=15)
    s.mix(T_PC1, level=127, pan=192)
    s.mix(T_PC2, level=77, pan=64, reverb=20)
    s.fx(routing="sends", reverb={"on": True, "size": 60, "damp": 45, "mix": 30},
         delay={"on": True, "time": "1/8.", "ping_pong": True, "mix": 25})
    return s


# =============================================================== ADVENTURE ===
# ADVENTURE and CHIP LOOP were written for the CHIP engine and listened to as they were made; their cells are kept
# as they were, in chip_songs.json: per song its tempo, its tracks ([name, level, pan])
# and its patterns, each a title, a line on it and its cells, one a line: [track, row, note (MIDI, "OFF" or null),
# instrument (a CHIPTUNE instrument's name, or the FUSION COIN), volume, commands].

def chip_cells(s, name):
    """the song's patterns from chip_songs.json, and its tracks' levels and pans"""
    data = read_json(TOOLS / "chip_songs.json")[name]
    for pd in data["patterns"]:
        p = s.pattern(pd["rows"], pd["title"] + ": " + pd["what"][0].lower() + pd["what"][1:])
        for t, r, n, ins, vol, cmds in pd["cells"]:
            ref = None if ins is None else I("FUSION", ins) if ins == "COIN" else I("CHIPTUNE", ins)
            if n == "OFF":
                p.off(t, r)
                p.cmd(t, r, *cmds)
            else:
                p.note(t, r, n, ref, vol, *cmds)
    for t, (_, level, pan) in enumerate(data["tracks"], 1):
        s.mix(t, level=level, pan=pan)
    return s.pats


def adventure(pool):
    s = Song(pool, "ADVENTURE", "Game music through time, 132 to 160 BPM. Shows the CHIPTUNE cartridge: one theme "
             "travelling through the eras of the sound chips, a pattern an era, each with its own chips, key, tempo and "
             "variation of the theme (1978 the PSG, 1983 the console, 1985 16BIT, 1989 the handheld, the 2000s all of "
             "them), a time machine between them (WHOOSH, WARP), and home again through every era, slowing down",
             tempo=132, tracks=8, at_end="stop")
    s.play(*chip_cells(s, "ADVENTURE"))
    # (its tracks' levels in chip_songs.json: the remix by the tracks' jobs (2026-10-25, its instruments' LEVELs
    # measured on the engine), against the lead: the coins and effects -2 dB, the bass -3, kick and snare -4, the
    # arpeggios -6, the echo -9, hats -12; LUFS, each solo in the song)
    return s


# ================================================================ PUNCH IN ===

def punch_in(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    V = lambda n: I("VOICES", n)  # noqa: E731
    kick, clap, ch, oh, rim = J("Tech Kick"), J("Clap"), J("Hat Closed"), J("Hat Open"), J("Rim")
    sub, chord, pad, riser = J("Sub Bass"), J("Dub Chord"), J("Night Pad"), J("Riser")
    tb = I("TB-303", "Bubble")
    bell, crash = I("BELLS/PLUCKS", "SPACE BELL"), I("FM DRUM KIT", "FM CRASH")
    s = Song(pool, "PUNCH IN", "Dub techno into progressive house, F minor, 124 BPM. Shows PT/FM's effects: dub "
             "chords into a ping-pong DELAY, DUCK pumping them from the kick, the FILTER and GATE turned by commands, "
             "the punch-ins one at a time (ECHO THROW, LOOP, REVERB SWELL, TAPE STOP, BIT CRUSH, REVERSE, STUTTER), a "
             "303 bubbling, the VOICES; a cut ending, a VINYL STOP on the full beat", tempo=124, tracks=12,
             at_end="stop")
    T_KICK, T_CLAP, T_HAT, T_PERC, T_BASS, T_TB, T_C1, T_C2, T_C3, T_BELL, T_VOX, T_FX = range(1, 13)
    CH = [T_C1, T_C2, T_C3]
    K = Key("F", "minor")
    # the chords as voiced (the sub gives the roots): Fm7, Dbmaj7, Abmaj7, Eb, moving by a step at most
    FM7, DBM7, ABM7, EB = ["Ab3", "C-4", "Eb4"], ["Ab3", "C-4", "F-4"], ["G-3", "C-4", "Eb4"], ["G-3", "Bb3", "Eb4"]
    PROG, ROOTS, TB_ROOTS = [FM7, DBM7, ABM7, EB], ["F", "Db", "Ab", "Eb"], ["F-2", "Db2", "Ab1", "Eb2"]
    BUB = [(0, 0), None, (7, 2), (0, 0), None, (4, 0), (2, 0), None, (0, 1), None, (2, 2), (4, 0), None, (7, 0),
           (4, 0), None]                         # the chord's tones only: root, third, fifth, octave
    BELL1 = "C-6/4 Ab5/4 F-5/4 Eb5/4 F-5/8 ./8 C-6/4 Bb5/4 C-6/4 Ab5/4 G-5/8 ./8"
    SUBF = [(0, 0), (6, 0, -12), (10, 0, -6), (14, 7, -10)]

    def stabs(p, chords, start=0, end=PAT, rhythm="..x.......x.....", vol=0x56):
        bars = (end - start) // B
        p.stabs(T_C1, chord, (chords * 8)[:bars], B, rhythm, vol, start=start, tracks=CH, gate=2)
        for r in range(start, end):              # ENV R 4: a stab's tail is the DELAY's, not its own
            for t in CH:
                c = p.cells[r][t - 1]
                if c is not None and c[0] not in (None, OFF):
                    p.cmd(t, r, "*4E:04")

    def groove(p, start, end, hat_vol=0x4C):
        p.hits(T_KICK, kick, "x...x...x...x...", 0x72, start, end)
        p.hits(T_CLAP, clap, "....x.......x...", 0x5C, start, end)
        hats(p, T_HAT, ch, oh, "c.o.c.o.c.o.cCo.", hat_vol, start, end)
        p.hits(T_PERC, rim, "...x......x....x", 0x40, start, end)

    def bubble(p, roots, start, end, vol=0x58):
        acid_steps(p, T_TB, tb, BUB, roots, vol, start, end, K)

    def bells(p, text, vol, start):
        p.melody(T_BELL, bell, text, vol, start)
        p.cmd(T_BELL, start, *dx_rel(bell, 62))  # the bell's release 0.25 s

    # 1. intro: a voice thrown into the echo for two bars; the dub chords behind a closed low-pass FILTER that
    # opens; the hats, then the kick (ducking the chords), then the sub; a LOOP 1/8 into the groove
    p = s.pattern(PAT, "intro: HERE COMES thrown into the echo; dub chords, the FILTER opening; a LOOP 1/8")
    p.note(T_VOX, 0, "C-4", V("HERE COMES"), 0x70, "!CD:20")
    p.cmd(T_FX, 0, fx_on("FILTER"), fxc(0x94, lp(54)), fxc(0x95, 40))
    p.sweep(T_FX, 4, PAT, "#94", lp(54), 64, step=4)
    stabs(p, [FM7])
    hats(p, T_HAT, ch, oh, "c.c.c.c.c.c.c.c.", 0x3C, 32, 64)
    hats(p, T_HAT, ch, oh, "c.o.c.o.c.o.cCo.", 0x44, 64, PAT)
    p.hits(T_KICK, kick, "x...x...x...x...", 0x6C, 64, PAT)
    bass_in(p, T_BASS, sub, ["F"] * 2, 16, SUBF, 0x64, lowest="A-3", start=96, gate=3)
    p.cmd(T_FX, 120, "!C4:08")                   # LOOP 1/8, two beats

    # 2. groove: the FILTER off, the full beat; the 303 bubbling; the bell from bar 5; R U READY; the low-pass
    # FILTER closing over the last two bars, the kick out for the last beat: into the break
    p = s.pattern(PAT, "groove: the beat, DUCK pumping, the 303 bubbling, the bell; the FILTER closing into the break")
    p.cmd(T_FX, 0, fx_off("FILTER"))
    p.note(T_FX, 0, "C-4", crash, 0x50)
    groove(p, 0, PAT)
    p.clear(T_KICK, 124)
    bass_in(p, T_BASS, sub, ["F"] * 8, 16, SUBF, 0x68, lowest="A-3", gate=3)
    stabs(p, [FM7])
    bubble(p, ["F-2"], 0, PAT)
    p.cmd(T_TB, 0, "*42:40", "*43:30")
    p.sweep(T_TB, 0, PAT, "*41", 0x28, 0x50, step=8)
    bells(p, BELL1, 0x5C, 64)
    p.note(T_VOX, 60, "C-4", V("R U READY"), 0x68)
    p.cmd(T_FX, 96, fx_on("FILTER"), fxc(0x94, 64))
    p.sweep(T_FX, 100, PAT, "#94", 64, lp(40), step=4)

    # 3. break: the FILTER off, a crash; no kick, no 303; the pad on the progression, the bell into a REVERB
    # SWELL; the hats and the sub back for the last two bars, the kick for the last one, a roll into the build
    p = s.pattern(PAT, "break: the pad on the progression, the bell into a REVERB SWELL; the kick coming back")
    p.note(T_FX, 0, "C-4", crash, 0x58, fx_off("FILTER"))
    p.off(T_TB, 0)
    pad_chords(p, CH, pad, PROG * 2, 16, 0x5C, cmds=("*4E:14",))
    bells(p, "F-6/8 Eb6/8 C-6/16", 0x54, 64)
    p.cmd(T_FX, 64, "!CE:20")                    # REVERB SWELL, two bars
    hats(p, T_HAT, ch, oh, "....o.......o...", 0x38, 96, PAT)
    bass_in(p, T_BASS, sub, ["Ab", "Eb"], 16, [(0, 0)], 0x60, lowest="A-3", start=96, gate=14)
    for k, r in enumerate(range(112, PAT, 4)):
        p.note(T_KICK, r, "C-4", kick, 0x54 + 6 * k)
    for k, r in enumerate(range(120, PAT)):
        p.note(T_CLAP, r, "C-4", clap, 0x40 + 4 * k, *(("*4E:08",) if k == 0 else ()))
    p.ends(CH, 126)

    # 4. build: the kick back; the GATE chopping the mix for four bars, then the high-pass FILTER rising for
    # four; a roll, the countdown, a TAPE STOP of two beats into the peak
    p = s.pattern(PAT, "build: GATE, then the FILTER rising; a roll, THREE TWO ONE, a TAPE STOP")
    p.cmd(T_FX, 0, fx_on("GATE"), fxc(0x90, 0x02), fxc(0x91, 1), fxc(0x92, 70))
    p.hits(T_KICK, kick, "x...x...x...x...", 0x70, 0, 96)
    p.hits(T_KICK, kick, "x.x.x.x.x.x.x.x.", 0x6C, 96, 120)
    hats(p, T_HAT, ch, oh, "c.o.c.o.c.o.cCo.", 0x48, 0, 120)
    stabs(p, PROG, 0, 96, rhythm="x.x...x...x...x.")
    pad_chords(p, CH, pad, [FM7], 28, 0x58, start=100, cmds=("*4E:14",))
    bass_in(p, T_BASS, sub, ROOTS * 2, 16, SUBF, 0x66, lowest="A-3", gate=3)
    p.clear(T_BASS, 96)
    bubble(p, TB_ROOTS * 2, 0, 112)
    p.sweep(T_TB, 0, 112, "*41", 0x40, 0x70, step=8)
    p.cmd(T_FX, 64, fx_off("GATE"), fx_on("FILTER"), fxc(0x94, 64))
    p.sweep(T_FX, 68, 116, "#94", 64, hp(44), step=4)
    p.cmd(T_FX, 116, fx_off("FILTER"))           # (the whole mix back for the TAPE STOP to wind down)
    for k, r in enumerate(range(80, 104)):
        if r < 96 and r % 2:
            continue
        p.note(T_CLAP, r, "C-4", clap, 0x44 + k, *(("E93",) if r >= 100 else ()), *(("*4E:08",) if r == 80 else ()))
    p.note(T_VOX, 80, "C-5", riser, 0x58)
    for r, w in ((104, "THREE"), (108, "TWO"), (112, "ONE")):
        p.note(T_VOX, r, "C-4", V(w), 0x70)
    p.cmd(T_FX, 120, "!D1:08")                   # TAPE STOP: a beat winding down, a beat of silence

    # 5. peak: the FILTER off, everything; the bell's motif twice, a LOOP 1/16 at bar 4, a BIT CRUSH in bar 6,
    # a REVERSE into 6
    p = s.pattern(PAT, "peak: everything; a LOOP 1/16, the bell, a BIT CRUSH, a REVERSE at the end")
    p.cmd(T_FX, 0, fx_off("FILTER"))
    p.note(T_FX, 0, "C-4", crash, 0x64)
    groove(p, 0, PAT, hat_vol=0x50)
    stabs(p, PROG, rhythm="..x.....x.x...x.", vol=0x5A)
    bass_in(p, T_BASS, sub, ROOTS * 2, 16, SUBF, 0x6A, lowest="A-3", gate=3)
    bubble(p, TB_ROOTS * 2, 0, PAT, vol=0x5C)
    p.cmd(T_TB, 0, "*41:68", "*42:50", "*43:50")
    bells(p, BELL1 + " " + BELL1, 0x60, 0)
    p.cmd(T_FX, 56, "!C5:08")                    # LOOP 1/16, two beats
    p.note(T_FX, 64, "C-4", crash, 0x58)
    p.cmd(T_FX, 88, "!C8:08")                    # BIT CRUSH, two beats
    p.cmd(T_FX, 120, "!CF:08")                   # REVERSE, two beats

    # 6. outro, cut: the full beat to the end; a STUTTER, the bell; a VINYL STOP on the full beat ends it (every
    # note cut and the DELAY and REVERB off under it once the stop has read past them: after it, nothing)
    p = s.pattern(PAT, "outro: the full beat to the end, a STUTTER, the bell; a VINYL STOP cuts it")
    p.note(T_FX, 0, "C-4", crash, 0x58)
    groove(p, 0, PAT, hat_vol=0x50)
    stabs(p, PROG, rhythm="..x.....x.x...x.", vol=0x58)
    bass_in(p, T_BASS, sub, ROOTS * 2, 16, SUBF, 0x6A, lowest="A-3", gate=3)
    bubble(p, TB_ROOTS * 2, 0, PAT, vol=0x5C)
    p.cmd(T_FX, 56, "!C7:08")                    # STUTTER, two beats
    bells(p, BELL1, 0x5C, 64)
    p.cmd(T_FX, 120, "!D2:08")                   # VINYL STOP
    for t in range(1, 13):                       # (the stop reads the mix up to 0.25 s, two rows, after it starts)
        p.clear(t, 124)
        p.cmd(t, 123, "EC1")
    p.cmd(T_FX, 124, fx_off("DELAY"), fx_off("REVERB"))

    let_go(s, [T_KICK, T_CLAP, T_HAT, T_PERC])
    let_go(s, [T_FX], 8, only=crash)
    s.play(*s.pats)
    # the remix by the tracks' jobs (2026-10-10), against the kick: the voice +1 dB (it was too low), clap
    # -3, the sub bass -5 (LUFS weighs a sub low), the dub chords -5, the bell -5, the 303 -6, the
    # effects -8, perc -10, hats -12
    s.mix(T_KICK, level=108)
    s.mix(T_CLAP, level=87, reverb=35)
    s.mix(T_HAT, level=42, pan=168, duck=40)
    s.mix(T_PERC, level=127, pan=76, delay=25)
    s.mix(T_BASS, level=127, duck=70)
    s.mix(T_TB, level=86, pan=100, delay=25, duck=60)
    for k, t in enumerate(CH):
        s.mix(t, level=36, pan=(64, 128, 192)[k], delay=45, reverb=40, duck=90)
    s.mix(T_BELL, level=77, pan=176, delay=35, reverb=50)
    s.mix(T_VOX, level=52, delay=30, reverb=30)
    s.mix(T_FX, level=48, pan=140, reverb=40)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 40, "tone": 45, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 75, "damp": 40, "mix": 35},
         duck={"on": True, "source": 1, "depth": 65, "release": 160},
         gate={"on": False, "pattern": 2, "rate": "1/16", "depth": 70, "smooth": 25},
         filter={"on": False, "cut": "OFF", "reso": 40})
    return s


# ============================================================ the DJ loops ===
# Twelve loops for DJ's decks and LOOPS: a pattern of 8 bars that loops (DNB LOOP's, ROCK LOOP's and METAL
# LOOP's two, a verse and a bridge), written on four tracks of one voice each (each note ends before its
# track's next), so two decks sound 8 voices at most; then one_sound_a_track() (each loop's end) moves a track's
# other instruments to tracks of their own, the notes as they were, as LOOPS loads a track as a loop of one sound.
# All in A minor (the bass and the hook on its pentatonic notes; WORLD LOOP's flute aside), so any two mix in
# key; nine at 120 to 128 BPM, so SYNC has something to match, TRANCE LOOP at 140, and HIP-HOP LOOP at 86 with
# DNB LOOP at 172, a pair SYNC matches at half or double. Bar 8 is a fill, so a DJ hears where the loop starts again. The same
# roles on the same tracks: 1 the kick, 2 the hats, 3 the bass, 4 the hook, then the rest (track 3 the bass in
# all twelve: the bass swapped by its mute; ACID LOOP's 303 is its bass and its hook). TECHNO LOOP, ACID LOOP,
# DISCO LOOP, DNB LOOP, ROCK LOOP and METAL LOOP have an intro of 8 bars first, played once, to mix in over
# another deck, with no bass (the drums alone for four bars, then the hook coming in: TECHNO LOOP's pluck dark,
# ACID LOOP's 303 nearly closed, DISCO LOOP's clav dark; DNB LOOP's the hats and the pad, then the break; ROCK
# LOOP's and METAL LOOP's the riff palm-muted); they loop from position 01, and their CUES have two parts
# (three with a bridge).

def dj_loop(pool, name, what, tempo, intro="", swing=50, then=""):
    """the song and its patterns: the loop (its line the song's second sentence), after the intro if `intro`
    (its line) is given, and followed by a second pattern of the loop if `then` (its line) is"""
    s = Song(pool, name, what, tempo=tempo, tracks=4, swing=swing, at_end="loop", loop_from=1 if intro else 0)
    pats = [s.pattern(PAT, intro)] if intro else []
    return s, pats + [s.pattern(PAT, what.split(". ")[1])] + ([s.pattern(PAT, then)] if then else [])


def bars(p, track, ins, figures, order, vol, cmds=(), gate=None):
    """a line of figures, a bar each in `order` (keys of `figures`); a figure: (row in the bar, note, rows,
    volume change)"""
    for b, k in enumerate(order):
        for f in figures[k]:
            r, n, ln, dv = (tuple(f) + (0,))[:4]
            p.note(track, 16 * b + r, n, ins, vol + dv, *cmds)
            p.off(track, 16 * b + r + (gate or ln))


def one_voice(p, track, tick=4, then=None):
    """each note of the track ends before its next one comes (in `then`, the pattern after it: itself, a loop,
    if not given): let go a row before it, or cut at tick `tick` of its row (ECx) when it comes on the row
    after; with releases of a row at most, the track's voice is free again for its next note (the player frees
    a released voice once it is quiet)"""
    then = then or p

    def cell(r):
        return p.cells[r][track - 1] if r < p.rows else then.cells[r - p.rows][track - 1]

    def kind(r):
        c = cell(r)
        return None if c is None or c[0] is None else "off" if c[0] == OFF else "note"

    def glides(r):
        """a note with TONE PORTA (3xx, 5xy): it bends the sounding note, in its voice"""
        return any(parse_cmd(tok)[0] in (0x03, 0x05) for tok in cell(r)[3])

    def cuts(r):
        """a NOTE CUT (ECx) in a cell with no note: it ends the sounding one"""
        c = cell(r)
        return c is not None and any(parse_cmd(tok)[0] == 0x0E and parse_cmd(tok)[1] >> 4 == 0xC for tok in c[3])

    rows = [r for r in range(p.rows) if kind(r) == "note"]
    after = [r for r in range(then.rows) if kind(p.rows + r) == "note"]
    for k, r in enumerate(rows):
        if k + 1 < len(rows):
            nxt = rows[k + 1]
        elif after:
            nxt = p.rows + after[0]
        else:
            continue                             # (nothing comes after it: it may ring)
        if any(kind(x) == "off" or cuts(x) for x in range(r + 1, nxt)) or glides(nxt):
            continue
        if nxt - r >= 2:
            (p if nxt - 1 < p.rows else then).off(track, (nxt - 1) % p.rows)
        elif len(p.cells[r][track - 1][3]) < 4:
            p.cmd(track, r, f"EC{tick:X}")
        else:
            raise SystemExit(f"songs: {p.what}: row {r:02X} track {track}: no room for a NOTE CUT")


def one_sound_a_track(s, keep=None, levels=None):
    """LOOPS loads a track as a loop of one sound: a track's notes of a second (third ...) instrument move to a
    track of their own, added after the song's, each with the cells that end it (its note off; a NOTE CUT is in
    its own cell) and the commands of the rows it sounds; the added track takes its old track's mixer, with the
    LEVEL `levels` gives its instrument (instrument -> LEVEL). The notes and their lengths stay as they were
    (one_voice() before this), so the song sounds the same. A track keeps the instrument its loop part plays
    first (`keep`: track -> instrument, where another)."""
    levels = dict(levels or {})
    def play(seq):
        return [next(p for p in s.pats if p.number == n) for n in seq]

    def is_note(c):
        return c is not None and c[0] is not None and c[0] != OFF

    order = play(s.order)
    rounds = order + play(s.order[s.loop_from:])          # (twice round: what each pattern starts after)
    ntrk = s.ntrk
    for t in range(1, ntrk + 1):
        last = {}                                         # pattern -> its last note's instrument on t
        for p in order:
            ins = [c[1] for c in (row[t - 1] for row in p.cells) if is_note(c)]
            last[p.number] = ins[-1] if ins else None
        seen = []
        for p in order[s.loop_from:] + order[:s.loop_from]:
            for row in p.cells:
                c = row[t - 1]
                if is_note(c) and c[1] not in seen:
                    seen.append(c[1])
        if len(seen) < 2:
            continue
        if keep and t in keep:
            seen.remove(tuple(keep[t]))
            seen.insert(0, tuple(keep[t]))
        to = {seen[0]: t}
        for ins in seen[1:]:
            s.ntrk += 1
            to[ins] = s.ntrk
            mix = dict(s.mixer[t - 1])
            if ins in levels:
                mix["level"] = levels.pop(ins)
            s.mixer.append(mix)
            for p in s.pats:
                p.ntrk = s.ntrk
                for row in p.cells:
                    row.append(None)
        incoming = {p.number: set() for p in order}       # the instruments each pattern starts with, sounding
        state = None
        for p in rounds:
            incoming[p.number].add(state)
            state = last[p.number] or state
        for p in order:
            cur = incoming[p.number] - {None}
            for r, row in enumerate(p.cells):
                c = row[t - 1]
                if c is None:
                    continue
                if is_note(c):
                    cur = {c[1]}
                if not cur:
                    continue
                if len(cur) > 1 and (c[3] or c[2] is not None):
                    raise SystemExit(f"songs: {s.name}: row {r:02X} track {t}: commands for one of {len(cur)} sounds")
                row[t - 1] = None
                for k, ins in enumerate(sorted(cur, key=seen.index)):
                    row[to[ins] - 1] = c if k == 0 else list(c)
    if levels:
        raise SystemExit(f"songs: {s.name}: no track added for {', '.join(CAT.name(i) for i in levels)}")
    return s


def drum_bars(p, track, kit, order, cmds=()):
    """a drum track a bar at a time: `order` a string of steps per bar (16 a bar), `kit` each step's
    character -> (instrument, volume)"""
    for b, steps in enumerate(order):
        for k, ch in enumerate(steps):
            if ch in kit:
                ins, vol = kit[ch]
                p.note(track, 16 * b + k, "C-4", ins, vol, *cmds)


def trance_loop(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, clap, ch, oh = J("Tech Kick"), J("Clap"), J("Hat Closed"), J("Hat Open")
    bass, pluck, zap = J("Seq Bass"), J("Psy Pluck"), J("Laser Zap")
    # (it took HOUSE LOOP's place: a psytrance loop, its bass rolling in triplets)
    s, (p,) = dj_loop(pool, "TRANCE LOOP", "Psytrance, A minor, 140 BPM. A DJ loop of the JUNO-106: the Tech "
                      "Kick, the Seq Bass rolling in threes after each kick (kick, bass, bass, bass), sixteenth hats "
                      "with open ones on the offbeats and claps, a Psy Pluck squelching in threes across the bar, "
                      "its VCF opening and closing by commands, a Laser Zap into the loop's start", 140)
    T_KICK, T_DRUMS, T_BASS, T_LEAD = range(1, 5)
    p.hits(T_KICK, kick, "x...x...x...x...", 0x76, 0, 124)            # (out for the fill's last beat)
    groove = "ccocxcocccocxcoc"                                       # sixteenths, open on the offbeats, claps 2 4
    kit = {"c": (ch, 0x3A), "o": (oh, 0x4C), "x": (clap, 0x5C)}
    drum_bars(p, T_DRUMS, kit, [groove] * 7 + ["ccocxcocccocxxxx"], cmds=("*4E:02",))   # (ENV R 2: chokes)
    for k, r in enumerate(range(124, PAT)):                            # the fill's claps rising
        p.note(T_DRUMS, r, None, None, 0x48 + 8 * k)
    # the rolling bass: the three sixteenths after each kick, on the bar's roots, a beat at a time
    up = 24                                                           # (heard two octaves under its notes)
    ROOTS = ["AAAA", "AAAA", "AAAA", "AAAG", "AAAA", "AAAA", "AAAA", "AACC"]
    note = {"A": 33, "C": 36, "G": 31}                                # A1 C2 G1, as heard
    for b, beats in enumerate(ROOTS):
        for k, root in enumerate(beats):
            for rr, dv in ((1, 0), (2, -10), (3, -4)):
                p.note(T_BASS, 16 * b + 4 * k + rr, note[root] + up, bass, 0x70 + dv)
    # the lead: a note every three sixteenths (0 3 6 9 12), across the four beats, two bars a motif
    LEAD = {"a": ["A-4", "A-4", "C-5", "A-4", "G-4"], "b": ["A-4", "A-4", "D-5", "C-5", "A-4"],
            "c": ["A-4", "C-5", "E-5", "D-5", "C-5"], "d": ["A-4", "A-4", "G-4", "E-4"]}
    for b, k in enumerate("ababacad"):
        for r, n in zip((0, 3, 6, 9, 12), LEAD[k]):
            p.note(T_LEAD, 16 * b + r, n, pluck, 0x64 if r == 0 else 0x58)
            p.off(T_LEAD, 16 * b + r + 2)
    p.note(T_LEAD, 124, "A-4", zap, 0x60)                             # the zap into the loop's start
    p.sweep(T_LEAD, 0, 64, "*45", 20, 52, step=4)                     # VCF FREQ: open over four bars, close over
    p.sweep(T_LEAD, 64, 124, "*45", 52, 20, step=4)                   # four (the patch's own 24)
    for t in range(1, 5):
        one_voice(p, t)
    s.play(p)
    # the master by the instruments' jobs (2026-10-11), against the kick: bass -3 dB, lead -3, clap -3, zap -8,
    # open hat -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=124)
    s.mix(T_DRUMS, level=92, pan=150, delay=10, reverb=20)
    s.mix(T_BASS, level=77)
    s.mix(T_LEAD, level=120, pan=108, delay=45, reverb=25)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 45, "tone": 50, "ping_pong": True, "mix": 35},
         reverb={"on": True, "size": 55, "damp": 45, "mix": 30})
    return one_sound_a_track(s, levels={oh: 67, clap: 127, zap: 67})


def breaks_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    kick, snare, hat, ohat = D("KICK"), D("SNARE"), D("HAT"), D("HAT OPEN")
    upright, vibes = I("BAND/ORCH", "UPRIGHT"), I("BAND/ORCH", "VIBRAPHONE")
    s, (p,) = dj_loop(pool, "BREAKS LOOP", "Breakbeat, A minor, 124 BPM. A DJ loop of the samplers: the DRUM "
                      "KIT's kick and snare in a break, its hats, the UPRIGHT bass walking, a VIBRAPHONE "
                      "riff", 124)
    T_KIT, T_HAT, T_BASS, T_VIBES = range(1, 5)
    A, B = "K.K.S..s.sK.S..s", "K.K.S..s.sK..KS."                     # the break, two bars
    drum_bars(p, T_KIT, {"K": (kick, 0x72), "S": (snare, 0x66), "s": (snare, 0x3A)}, [A, B, A, B, A, B, A,
              "K.K.S..s.sK.SSSS"])
    for k, r in enumerate(range(124, PAT)):                            # the fill's snares rising
        p.note(T_KIT, r, None, None, 0x4C + 8 * k)
    drum_bars(p, T_HAT, {"C": (hat, 0x4E), "c": (hat, 0x34), "o": (ohat, 0x46)},
              ["C.c.C.c.C.c.C.c.", "C.c.C.c.C.c.C.o."] * 4)
    BASS = {"a": [(0, "A1", 3), (3, "A1", 3), (6, "C2", 4), (10, "E2", 2), (12, "D2", 2), (14, "C2", 2)],
            "b": [(0, "A1", 3), (3, "A1", 3), (6, "E2", 2), (8, "G2", 2), (10, "E2", 2), (12, "D2", 4)],
            "c": [(0, "A1", 3), (3, "A1", 3), (6, "C2", 2), (8, "D2", 2), (10, "E2", 4), (14, "G1", 2)],
            "d": [(0, "A1", 3), (3, "A1", 3), (6, "C2", 2), (8, "D2", 2), (10, "E2", 2)]}
    bars(p, T_BASS, upright, BASS, "abacabad", 0x70)
    RIFF = ("./2 E5/2 G5/2 ./1 A5/3 G5/2 E5/4 ./2 D5/2 ./2 C5/2 E5/4 ./4 "     # four bars, played twice
            "./2 E5/2 G5/2 ./1 A5/3 C6/2 A5/4 ./2 G5/2 ./2 E5/2 D5/2 C5/2 A4/4")
    for start in (0, 64):
        p.melody(T_VIBES, vibes, RIFF, 0x6C, start)
    p.cmd(T_VIBES, 2, "*4B:52")                                       # (its R4 82: a release of 25 ms)
    for t in range(1, 5):
        one_voice(p, t)
    s.play(p)
    # the master by the instruments' jobs (2026-10-11), against the kick: snare 0 dB, upright -2, vibes -3,
    # open hat -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KIT, level=127)
    s.mix(T_HAT, level=56, pan=160, delay=15)
    s.mix(T_BASS, level=64)
    s.mix(T_VIBES, level=73, pan=96, delay=40, reverb=35)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 35, "tone": 45, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 55, "damp": 40, "mix": 30})
    return one_sound_a_track(s, levels={snare: 99, ohat: 47})


def techno_loop(pool):
    F = lambda n: I("FM DRUM KIT", n)  # noqa: E731
    kick, hat, ohat, clap = F("FM KICK"), F("FM HAT"), F("FM HAT OPN"), F("FM CLAP")
    bass, pluck = I("LEADS/BASSES", "FM BASS"), I("BELLS/PLUCKS", "PROG PLUCK")
    s, (i, p) = dj_loop(pool, "TECHNO LOOP", "Techno, A minor, 126 BPM. A DJ loop of the DX7: the FM DRUM KIT, the "
                        "FM BASS rolling after the kick, a PROG PLUCK in dotted eighths, its brightness turned by "
                        "commands. An intro first, played once: the drums, then the pluck coming in dark; no bass", 126,
                        intro="intro: the kick and closed hats, then the claps; the pluck coming in dark, brightening")
    T_KICK, T_DRUMS, T_BASS, T_PLUCK = range(1, 5)
    groove, fill = "cco.Xco.cco.Xco.", "cco.Xco.XcXcXXXX"            # closed and open hats, claps on 2 and 4
    kit = {"c": (hat, 0x40), "o": (ohat, 0x50), "X": (clap, 0x60)}

    def pluck_line(q, start, notes):
        """A, E and C (or D) in dotted eighths, a bar each from `start`"""
        for b in range(start // 16, 8):
            for k, r in enumerate(range(0, 16, 3)):
                q.note(T_PLUCK, 16 * b + r, notes(b)[k % 3], pluck, 0x5C if k % 3 == 0 else 0x50)

    # the intro: the kick and closed hats for four bars, then the claps and open hats and the pluck, coming in dark
    # and brightening to where the loop starts; no bass
    i.hits(T_KICK, kick, "x...x...x...x...", 0x76)
    drum_bars(i, T_DRUMS, kit, ["c.c.c.c.c.c.c.c."] * 4 + [groove] * 3 + [fill])
    pluck_line(i, 64, lambda b: ["A-4", "E-5", "C-5"])
    for code in ("*48", "*58"):                                       # OP2 and OP4: dark to the loop's start
        i.sweep(T_PLUCK, 64, PAT, code, 30, 62, step=4)
    i.cmd(T_PLUCK, 65, *dx_rel(pluck, 85))                            # (a release of 15 ms: the next note chokes)
    # the loop
    p.hits(T_KICK, kick, "x...x...x...x...", 0x76)
    drum_bars(p, T_DRUMS, kit, [groove] * 7 + [fill])
    for q in (i, p):
        for k, r in enumerate(range(124, PAT)):                       # the fill's claps rising
            q.note(T_DRUMS, r, None, None, 0x48 + 8 * k)
    up = 12                                                           # (heard an octave under its notes)
    roll = [(1, 0, 0), (2, 0, 8), (3, 0, -6)]                          # the three sixteenths after each kick
    for b in range(8):
        for beat in range(4):
            root = 33 + up + (3 if (b, beat) in ((3, 3), (7, 2)) else 7 if (b, beat) == (7, 3) else 0)
            for rr, iv, dv in roll:
                p.note(T_BASS, 16 * b + 4 * beat + rr, root + iv, bass, 0x6C + dv, "EC4")
    pluck_line(p, 0, lambda b: ["A-4", "E-5", "C-5" if b < 4 else "D-5"])   # D in bars 5-8
    p.cmd(T_PLUCK, 0, *dx_rel(pluck, 85))                             # (as in the intro: a cue may start here)
    for code in ("*48", "*58"):                                       # OP2 and OP4: the brightness, up and down
        p.sweep(T_PLUCK, 1, 65, code, 62, 92, step=4)
        p.sweep(T_PLUCK, 65, PAT, code, 92, 62, step=4)
    for t in range(1, 5):
        one_voice(i, t, then=p)
        one_voice(p, t)
    s.play(i, p)
    # the master by the instruments' jobs (2026-10-11), against the kick: clap -3 dB, FM bass -4, pluck -4,
    # open hat -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_DRUMS, level=127, pan=148, delay=15, reverb=25)
    s.mix(T_BASS, level=66)
    s.mix(T_PLUCK, level=75, pan=104, delay=50, reverb=35)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 45, "tone": 45, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 65, "damp": 45, "mix": 30})
    return one_sound_a_track(s, levels={ohat: 58, clap: 105})


def acid_loop(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, ch, oh, clap = J("909 Kick"), J("Hat Closed"), J("Hat Open"), J("Clap")
    tb = I("TB-303", "Classic")
    s, (i, p) = dj_loop(pool, "ACID LOOP", "Acid house, A minor, 128 BPM. A DJ loop of the TB-303: its line with "
                        "accents and slides, the CUTOFF opening over four bars and closing over four; the JUNO-106's "
                        "909 Kick, hats and claps. An intro first, played once: the drums alone for four bars, then "
                        "the 303 coming in nearly closed", 128,
                        intro="intro: the kick and closed hats, then the claps; the 303 coming in, its CUTOFF opening")
    T_KICK, T_HAT, T_TB, T_CLAP = range(1, 5)
    K = Key("A", "minor")
    LINE = [(0, 1), (0, 0), (7, 2), (0, 0), (0, 0), (4, 0), None, (0, 1), (0, 0), (7, 2), (9, 0), (0, 0), None,
            (2, 1), (0, 2), (0, 0),
            (0, 1), (0, 0), (7, 2), (0, 0), (0, 0), (4, 0), None, (0, 1), (2, 0), (4, 2), (7, 1), (4, 0), (2, 0),
            None, (9, 3), (7, 0)]                                     # two bars, in the key's steps from the root

    def drums(q, start):
        """the kick throughout; from `start` the open hats (choked by the next hat) and the claps, with the
        fill's claps on the last beat (ENV R 2: each choked by the next)"""
        q.hits(T_KICK, kick, "x...x...x...x...", 0x74)
        hats(q, T_HAT, ch, oh, "c.c.c.c.c.c.c.c.", 0x4C, 0, start)
        hats(q, T_HAT, ch, oh, "c.o.c.o.c.o.cCo.", 0x4C, start, PAT)
        for r in range(start + 2, PAT, 4):
            q.cmd(T_HAT, r, "*4E:02")
        q.hits(T_CLAP, clap, "....x.......x...", 0x5C, start, 112)
        q.hits(T_CLAP, clap, "....x.......xxxx", 0x5C, 112, PAT)
        for k, r in enumerate(range(124, PAT)):
            q.note(T_CLAP, r, None, None, 0x40 + 8 * k, "*4E:02")

    def tb_line(q, start):
        acid_steps(q, T_TB, tb, LINE, ["A-1", "A-1", "A-1", "C-2"], 0x64, start, PAT, K)
        q.cmd(T_TB, start, "*42:6E", "*43:55")                        # RESONANCE, ENV MOD; DECAY
        q.cmd(T_TB, start + 1, "*44:1E")

    # the intro: the kick and closed hats for four bars; then the open hats, the claps and the 303, its CUTOFF
    # nearly closed, opening to where the loop starts
    drums(i, 64)
    tb_line(i, 64)
    i.sweep(T_TB, 64, PAT, "*41", 12, 30, step=4)
    # the loop
    drums(p, 0)
    tb_line(p, 0)
    p.sweep(T_TB, 0, 64, "*41", 30, 84, step=4)                       # the CUTOFF up over four bars, down over four
    p.sweep(T_TB, 64, PAT, "*41", 84, 30, step=4)
    for t in (T_KICK, T_HAT, T_CLAP):                                 # (the 303 keeps its one voice)
        one_voice(i, t, then=p)
        one_voice(p, t)
    s.play(i, p)
    # the master by the instruments' jobs (2026-10-11), against the kick: the 303 -2 dB, clap -3, open hat -10,
    # hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_HAT, level=108, pan=156, delay=15)
    s.mix(T_CLAP, level=108, pan=120, reverb=30)
    s.mix(T_TB, level=80, delay=30)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 40, "tone": 50, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 60, "damp": 40, "mix": 30})
    return one_sound_a_track(s, levels={oh: 59})


def disco_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, snare, hat, ohat = D("KICK"), D("SNARE"), D("HAT"), D("HAT OPEN")
    tom, bass, clav = J("Syn Tom"), J("Funk Bass"), J("Wah Clav")
    s, (i, p) = dj_loop(pool, "DISCO LOOP", "Disco, A minor, 120 BPM. A DJ loop of the DRUM KIT and the JUNO-106: "
                        "four on the floor, open hats on the offbeats, Syn Tom fills; the Funk Bass in octaves on Am7 "
                        "and D9, a Wah Clav riff in sixteenths, its VCF opening and closing by commands. An intro "
                        "first, played once: the kick and hats, then the snares and the clav coming in dark; no bass", 120,
                        intro="intro: the kick and hats, then the snares; the clav coming in dark, opening")
    T_KICK, T_DRUMS, T_BASS, T_CLAV = range(1, 5)
    kit = {"c": (hat, 0x3C), "o": (ohat, 0x4C), "S": (snare, 0x62)}
    groove = "c.ocS.occ.ocS.oc"                                       # open hats on the offbeats, snares on 2 and 4

    def fill(q):
        """bar 8: the groove, then Syn Toms down a sixteenth each (the kick out for the last beat)"""
        for k, n in enumerate(["C-5", "A-4", "G-4", "E-4", "D-4", "C-4"]):
            q.note(T_DRUMS, 122 + k, n, tom, 0x56 + 3 * k)

    up = 12                                                           # (heard an octave under its notes)
    BASS = {k: [(r, midi(n) + up, ln) for r, n, ln in v] for k, v in {
        "a": [(0, "A1", 2), (2, "A2", 1), (3, "A1", 1), (6, "A2", 1), (8, "A1", 2), (11, "G2", 1), (12, "A2", 1),
              (14, "E2", 2)],
        "d": [(0, "D2", 2), (2, "D3", 1), (3, "D2", 1), (6, "D3", 1), (8, "D2", 2), (11, "C3", 1), (12, "D3", 1),
              (14, "A2", 2)],
        "turn": [(0, "D2", 2), (2, "D3", 1), (3, "D2", 1), (6, "D3", 1), (8, "C2", 2), (10, "D2", 2), (12, "E2", 2),
                 (14, "G2", 2)]}.items()}                             # walking up to the loop's A
    CLAV = {"": [],
            "a": [(2, "E5", 1), (4, "E5", 1), (6, "G5", 1), (7, "A5", 1, 6), (10, "E5", 1), (12, "G5", 1),
                  (14, "E5", 1)],
            "d": [(2, "D5", 1), (4, "D5", 1), (6, "E5", 1), (7, "A5", 1, 6), (10, "D5", 1), (12, "C5", 1),
                  (14, "A4", 1)],
            "a2": [(2, "E5", 1), (4, "E5", 1), (6, "G5", 1), (7, "A5", 1, 6), (10, "C6", 1, 6), (11, "A5", 1),
                   (12, "G5", 1), (14, "E5", 1)],
            "turn": [(2, "D5", 1), (4, "D5", 1), (6, "E5", 1), (8, "G5", 1), (9, "A5", 1), (10, "C6", 1, 6),
                     (12, "A5", 2, 6)]}
    VCF = "*45"                                                       # (VCF FREQ: the patch's own is 30)
    # the intro: the kick and hats for four bars; then the snares and the clav, its VCF opening to the loop's
    i.hits(T_KICK, kick, "x...x...x...x...", 0x7C, 0, 124)
    drum_bars(i, T_DRUMS, kit, ["c.occ.occ.occ.oc"] * 4 + [groove] * 3 + ["c.ocS.occ."])
    fill(i)
    bars(i, T_CLAV, clav, CLAV, ["", "", "", "", "a", "d", "a2", "turn"], 0x64)
    i.sweep(T_CLAV, 66, PAT, VCF, 8, 26, step=4)                      # (from the clav's first note)
    # the loop
    p.hits(T_KICK, kick, "x...x...x...x...", 0x7C, 0, 124)
    drum_bars(p, T_DRUMS, kit, [groove] * 7 + ["c.ocS.occ."])
    fill(p)
    bars(p, T_BASS, bass, BASS, ["a", "d", "a", "d", "a", "d", "a", "turn"], 0x70)
    bars(p, T_CLAV, clav, CLAV, ["a", "d", "a", "d", "a", "d", "a2", "turn"], 0x64)
    p.sweep(T_CLAV, 2, 66, VCF, 26, 44, step=4)                       # up over four bars, down over four
    p.sweep(T_CLAV, 66, PAT, VCF, 44, 26, step=4)
    for t in range(1, 5):
        one_voice(i, t, then=p)
        one_voice(p, t)
    s.play(i, p)
    # the master by the instruments' jobs (2026-10-11), against the kick: snare -2 dB, bass -2, clav -3, toms -5,
    # open hat -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_DRUMS, level=80, pan=150, reverb=20)
    s.mix(T_BASS, level=77)
    s.mix(T_CLAV, level=55, pan=100, chorus=40, delay=30, reverb=30)
    s.fx(routing="sends",
         chorus={"on": True, "mode": "I", "mix": 40},
         delay={"on": True, "time": "1/8.", "feedback": 30, "tone": 55, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 55, "damp": 40, "mix": 30})
    return one_sound_a_track(s, levels={ohat: 55, snare: 88, tom: 37})


def chip_loop(pool):
    """(chip_songs.json, as ADVENTURE: the verse and the bridge, a loop of both)"""
    s = Song(pool, "CHIP LOOP", "Chiptune, A minor, 125 BPM. A DJ loop of the CHIPTUNE cartridge: the triangle kick, "
             "the console's noise groove, the sync-buzzer bass bouncing, chords as 50 Hz arpeggios on one voice by 0xy "
             "in 3-3-2; a verse and a bridge between verses, each ending on its chord and a run", tempo=125, tracks=4,
             at_end="loop")
    verse, bridge = chip_cells(s, "CHIP LOOP")
    for t in range(1, 5):
        one_voice(verse, t, then=bridge)
        one_voice(bridge, t, then=verse)
    s.play(verse, bridge)
    # the master by the instruments' jobs (2026-10-25, its instruments' LEVELs measured on the engine), against
    # the kick: bass -2 dB, hook -2, snare -3, open hat -10, hats -12 (LUFS, each solo over the loop)
    return one_sound_a_track(s, levels={I("CHIPTUNE", "8BIT OHAT"): 86, I("CHIPTUNE", "8BIT SNARE"): 82})


def hip_hop_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    kick, snare, hat = D("KICK"), D("SNARE"), D("HAT")
    bass, keys = I("LEADS/BASSES", "AC BASS"), I("KEYS/ORGANS", "TINE EP")
    s, (p,) = dj_loop(pool, "HIP-HOP LOOP", "Hip-hop, A minor, 86 BPM. A DJ loop of the DRUM KIT and the DX7, "
                      "swung and through the LO-FI: a boom-bap beat, the AC BASS after the kick, a TINE EP phrase; half "
                      "DNB LOOP's tempo, so SYNC matches the two at half or double", 86, swing=58)
    T_KICK, T_DRUMS, T_BASS, T_KEYS = range(1, 5)
    K1, K2 = "x......x.x......", "x.....ox..x....."
    drum_bars(p, T_KICK, {"x": (kick, 0x74), "o": (kick, 0x5A)}, [K1, K2] * 3 + [K1, "x......x.x...x.x"])
    G1, G2 = "C.c.S.c.C.ccS.c.", "C.c.S.c.C.c.S.cs"                   # snares on 2 and 4; the 16ths swung
    drum_bars(p, T_DRUMS, {"C": (hat, 0x4C), "c": (hat, 0x36), "S": (snare, 0x66), "s": (snare, 0x34)},
              [G1, G2] * 3 + [G1, "C.c.S.c.C.csS.ss"])
    BASS = {"a": [(0, "A1", 6), (7, "A1", 2), (10, "C2", 3), (14, "D2", 2)],
            "b": [(0, "E2", 6), (6, "D2", 2), (9, "C2", 3), (12, "G1", 4)],
            "c": [(0, "A1", 6), (7, "A1", 2), (10, "G1", 3), (13, "E2", 3)]}
    bars(p, T_BASS, bass, BASS, "abababac", 0x6C)
    p.cmd(T_BASS, 0, *dx_rel(bass, 70))                               # (a release of under a row: 0.12 s)
    PHRASE = ("./4 E5/2 G5/2 A5/4 G5/2 E5/2 ./2 D5/2 E5/4 C5/4 A4/4 "    # four bars, played twice
              "./4 E5/2 G5/2 A5/2 C6/2 A5/4 G5/3 E5/3 D5/2 C5/4 A4/4")
    for start in (0, 64):
        p.melody(T_KEYS, keys, PHRASE, 0x64, start)
    p.cmd(T_KEYS, 4, *dx_rel(keys, 70))
    for t in range(1, 5):
        one_voice(p, t)
    s.play(p)
    # the master by the instruments' jobs (2026-10-11), against the kick: snare 0 dB, bass -2, keys -4, hats -12
    # (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_DRUMS, level=64, pan=140, reverb=15)
    s.mix(T_BASS, level=87)
    s.mix(T_KEYS, level=67, pan=110, chorus=40, delay=25, reverb=35)
    s.fx(routing="sends",
         lofi={"on": True, "bits": 11, "rate": 22, "dust": 30},
         chorus={"on": True, "mode": "I", "mix": 40},
         delay={"on": True, "time": "1/8.", "feedback": 30, "tone": 40, "ping_pong": True, "mix": 25},
         reverb={"on": True, "size": 50, "damp": 55, "mix": 30})
    return one_sound_a_track(s, levels={snare: 100})


def dnb_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    kick, snare, hat, ohat, shaker = D("KICK"), D("SNARE"), D("HAT"), D("HAT OPEN"), D("SHAKER")
    bass, pad, shout = I("BASS", "DNB BASS"), I("JUNO-106", "Space Pad"), I("VOICES", "R U READY")
    wobble = I("BASS", "WOBBLE 140")
    s, (i, p, q) = dj_loop(pool, "DNB LOOP", "Drum and bass, A minor, 172 BPM. A DJ loop of the samplers: the "
                           "DRUM KIT's two-step, hats and a shaker, the BASS cartridge's DNB BASS, a "
                           "JUNO-106 Space Pad. It loops over two patterns, this one and a bridge: the same break and "
                           "bass, the BASS cartridge's WOBBLE 140 as the pad for six bars, low, opening slowly, then "
                           "the Space Pad finishing; double HIP-HOP LOOP's tempo. An intro first, played once: the "
                           "hats and the pad, then the break, R U READY at its end; no bass", 172,
                           intro="intro: the hats and the pad, the break from bar 5, R U READY in bar 8",
                           then="the bridge: the same break and bass, the WOBBLE 140 as the pad for six bars, then "
                                "the Space Pad")
    T_BREAK, T_HATS, T_BASS, T_PAD = range(1, 5)
    kit = {"K": (kick, 0x74), "S": (snare, 0x68), "s": (snare, 0x38)}
    A, B = "K...S.....K.S...", "K...S..s..KKS..s"                     # the two-step, two bars
    hats = {"C": (hat, 0x48), "c": (hat, 0x34), "s": (shaker, 0x30), "o": (ohat, 0x44)}
    full = "CsCsCsCsCsCsCsCo"

    def fill(q):
        for k, r in enumerate(range(124, PAT)):                       # the fill's snares rising
            q.note(T_BREAK, r, None, None, 0x4C + 8 * k)

    def pad_line(q, notes):
        """notes held for their rows, each let go two rows early with a release of 0.12 s (ENV R 12), so its
        voice is free for the next"""
        r = 0
        for n, rows in notes:
            q.note(T_PAD, r, n, pad, 0x3A, "*4E:0C")                 # (lowered; was 0x54)
            q.off(T_PAD, r + rows - 2)
            r += rows

    # the intro: the hats and the pad for four bars, then the break and all the hats; R U READY before the loop
    drum_bars(i, T_HATS, hats, ["C.c.C.c.C.c.C.c."] * 4 + [full] * 4)
    drum_bars(i, T_BREAK, kit, [""] * 4 + [A, B, A, "K...S..s.sK.SSSS"])
    fill(i)
    pad_line(i, [("E5", 32), ("G5", 32), ("D5", 32), ("C5", 16)])
    i.note(T_PAD, 116, "C-4", shout, 0x60)
    # the loop: the pad's half, then the bridge's (the same break, hats and bass: only the lead changes)
    BASS = {"a": [(0, "A1", 4), (7, "A1", 2), (10, "C2", 4), (14, "A1", 2)],
            "b": [(0, "G1", 4), (7, "G1", 2), (10, "E2", 4), (14, "D2", 2)],
            "c": [(0, "C2", 4), (7, "C2", 2), (10, "D2", 4), (14, "E2", 2)],
            "d": [(0, "G1", 4), (7, "G1", 2), (10, "E2", 2), (12, "D2", 2), (14, "C2", 2)]}
    for q_ in (p, q):
        drum_bars(q_, T_BREAK, kit, [A, B] * 3 + [A, "K...S..s.sK.SSSS"])
        fill(q_)
        drum_bars(q_, T_HATS, hats, [full] * 8)
        bars(q_, T_BASS, bass, BASS, "abacabad", 0x70)
    pad_line(p, [("E5", 32), ("G5", 32), ("D5", 32), ("C5", 32)])
    # the bridge's pad: the WOBBLE 140 (a bar of a 140 BPM wobble, looping) as the Space Pad plays in 01, low:
    # E1 for two bars three times (on E it wobbles 1.26 x as fast as its own C1), each opening slowly (R1 40, as
    # the pad's slow attack) and let go two rows early, deep in the reverb and the chorus (Seq Lead plucks, fast
    # or slow, were tried in its place: "doesn't work")
    for k in range(3):
        q.note(T_PAD, 32 * k, "E-1", wobble, 0x7F)
        q.off(T_PAD, 32 * k + 30)
    q.cmd(T_PAD, 0, "*48:28", "=81:60", "=83:7F")                    # (R1 40; CHORUS and REVERB sends; its LEVEL
    #                                                                   the master's, below)
    # then the Space Pad itself finishes, its C5 as in 01's last two bars
    q.note(T_PAD, 96, "C5", pad, 0x3A, "*4E:0C")
    q.off(T_PAD, 126)
    for t in range(1, 5):
        one_voice(i, t, then=p)
        one_voice(p, t, then=q)
        one_voice(q, t, then=p)
    s.play(i, p, q)
    # the master by the instruments' jobs (2026-10-11), against the kick: snare +1 dB, bass -1, wobble -6, pad -8,
    # open hat -10, shaker -12, hats -12 (LUFS, each solo over the loop part); R U READY, in the intro only, at 100
    s.mix(T_BREAK, level=127)
    s.mix(T_HATS, level=42, pan=158, delay=10)
    s.mix(T_BASS, level=80)
    s.mix(T_PAD, level=25, pan=100, delay=35, reverb=55)
    s.fx(routing="sends",
         chorus={"on": True, "mode": "I+II", "mix": 50},                 # (only the bridge's wobble sends to it)
         delay={"on": True, "time": "1/4.", "feedback": 40, "tone": 40, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 75, "damp": 45, "mix": 35})
    return one_sound_a_track(s, levels={snare: 93, shaker: 77, ohat: 60, wobble: 33, shout: 100})


def rock_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    kick, snare, hat, crash = D("KICK"), D("SNARE"), D("HAT"), D("CRASH")
    tom_hi, tom_mid, tom_lo = D("TOM HI"), D("TOM MID"), D("TOM LO")
    power, mute = I("BAND/ORCH", "DIST GTR"), I("BAND/ORCH", "GTR MUTE")
    lead = I("GUITAR/STRGS", "DIST GTR")
    s, (i, p, q) = dj_loop(pool, "ROCK LOOP", "Rock, A minor, 126 BPM. A DJ loop of the DRUM KIT and the guitars: a "
                           "rock beat with tom fills, the DIST GTR's power chords and palm mutes in a riff, the DX7's "
                           "DIST GTR on a hook with bends (TONE PORTA) and vibrato, played twice. It loops over two "
                           "patterns, this verse and a bridge: power chords ringing on C G D A, a half-time beat, the "
                           "lead soaring. An intro first, played once: the beat, then the riff palm-muted; no lead",
                           126, intro="intro: the beat, then the riff palm-muted",
                           then="the bridge: power chords ringing on C G D A, a half-time beat, the lead soaring")
    T_KIT, T_CYM, T_GTR, T_LEAD = range(1, 5)
    kit = {"K": (kick, 0x76), "S": (snare, 0x6A), "s": (snare, 0x3A), "h": (tom_hi, 0x60), "m": (tom_mid, 0x64),
           "l": (tom_lo, 0x68)}
    BEAT, BEAT2, FILL = "K...S...K.K.S...", "K...S...K.K.S..K", "K...S...SShhmmll"
    cym = {"X": (crash, 0x5C), "C": (hat, 0x4C), "c": (hat, 0x36)}
    HATS, CRASH = "C.c.C.c.C.c.C.c.", "X...C.c.C.c.C.c."                # (the crash on 1, rung out a beat)

    def riff(q_, figures):
        """the rhythm guitar a bar a figure: (row, root, O an open power chord or M a palm mute, rows); the
        roots A C D G, so the chords' fifths stay in the key's pentatonic notes too"""
        for b, fig in enumerate(figures):
            for r, n, k, ln in fig:
                q_.note(T_GTR, 16 * b + r, n, power if k == "O" else mute, 0x74 if k == "O" else 0x5E)
                q_.off(T_GTR, 16 * b + r + ln)

    chug = [(r, "A2", "M", 1) for r in (2, 3, 4, 5)]
    R1 = [(0, "A2", "O", 2)] + chug + [(6, "C3", "O", 2), (8, "A2", "M", 1), (9, "A2", "M", 1), (10, "D3", "O", 2),
                                       (12, "A2", "M", 1), (13, "A2", "M", 1), (14, "C3", "O", 2)]
    R2 = [(0, "A2", "O", 2)] + chug + [(6, "C3", "O", 2), (8, "A2", "M", 1), (9, "A2", "M", 1), (10, "G2", "O", 3),
                                       (13, "D3", "O", 3)]
    R3 = [(0, "A2", "O", 2)] + chug + [(6, "C3", "O", 2), (8, "A2", "M", 1), (9, "A2", "M", 1), (10, "D3", "O", 2),
                                       (12, "C3", "O", 2), (14, "G2", "O", 2)]
    R4 = [(0, "A2", "O", 2)] + chug + [(6, "C3", "O", 2), (8, "D3", "O", 4), (12, "G2", "O", 4)]   # into the fill
    MUTED = [(r, "A2", "M", 1) for r in (0, 2, 4, 5, 6, 8, 10, 12, 13, 14)]
    INTO = [(r, "A2", "M", 1) for r in (0, 2, 4, 6)] + [(8, "C3", "O", 2), (10, "D3", "O", 2), (12, "G2", "O", 4)]

    def solo(q_, text):
        """the lead's line; a note held 8 rows or more gets vibrato (4xy) from its fifth row to its end"""
        q_.melody(T_LEAD, lead, text, 0x6C)
        r = 0
        for tok in text.split():
            name, _, ln = tok.split(",")[0].partition("/")
            if name != "." and int(ln) >= 8:
                for x in range(r + 4, r + int(ln)):
                    q_.cmd(T_LEAD, x, "445")
            r += int(ln)
        q_.cmd(T_LEAD, next(x for x in range(PAT) if q_.cells[x][T_LEAD - 1] is not None), *dx_rel(lead, 70))

    # the verse's hook, 4 bars on the A minor pentatonic, played twice; bends are TONE PORTA (306) a whole step up
    HOOK = ("A4/2 C5/2 D5/2 E5/4,306 D5/2 C5/2 A4/2 "
            "G4/2 A4/14,306 "
            "A4/2 C5/2 D5/2 E5/4,306 G5/2 E5/2 D5/2 "
            "E5/4 D5/4 C5/4 A4/4 ")
    # the bridge's lead: long notes over C G D A, bent and held, down into the verse's A
    SOAR = ("C5/4 D5/4 E5/8,306 "
            "D5/4 G5/12 "
            "A5/4 G5/4 E5/8 "
            "C5/4 A4/12 "
            "G5/4 A5/12,306 "
            "G5/4 E5/4 D5/8 "
            "E5/4 G5/4 A5/8,306 "
            "C6/4 A5/4 G5/4 E5/4")
    HALF = "K.....K.S......."                                             # the bridge's half-time beat

    def ring(root):
        """a bridge bar: the power chord struck 3-3-2 in eighths, left ringing"""
        return [(0, root, "O", 6), (6, root, "O", 6), (12, root, "O", 4)]

    # the intro: the beat (a crash on bar 5), then the riff palm-muted from bar 5; no lead
    drum_bars(i, T_KIT, kit, [BEAT, BEAT2, BEAT, BEAT2, BEAT, BEAT2, BEAT, FILL])
    drum_bars(i, T_CYM, cym, [HATS] * 4 + [CRASH] + [HATS] * 3)
    riff(i, [[]] * 4 + [MUTED] * 3 + [INTO])
    # the verse: the beat (a crash on bars 1 and 5), the riff, the hook twice
    drum_bars(p, T_KIT, kit, [BEAT, BEAT2, BEAT, BEAT2, BEAT, BEAT2, BEAT, FILL])
    drum_bars(p, T_CYM, cym, [CRASH] + [HATS] * 3 + [CRASH] + [HATS] * 3)
    riff(p, [R1, R2, R1, R3, R1, R2, R1, R4])
    solo(p, HOOK * 2)
    # the bridge: half time, a crash every two bars, the chords ringing, the lead soaring; the fill into the verse
    drum_bars(q, T_KIT, kit, [HALF] * 7 + [FILL])
    for b in range(7):
        q.off(T_KIT, 16 * b + 3)                                      # (the kick let go by beat 2, as the verse's)
    drum_bars(q, T_CYM, cym, [CRASH, HATS] * 4)
    riff(q, [ring(n) for n in ("C3", "G2", "D3", "A2", "C3", "G2", "D3")] +
         [[(0, "D3", "O", 6), (6, "D3", "O", 6), (12, "G2", "O", 4)]])
    solo(q, SOAR)
    for t in range(1, 5):
        one_voice(i, t, then=p)
        one_voice(p, t, then=q)
        one_voice(q, t, then=p)
    s.play(i, p, q)
    # the master by the instruments' jobs (2026-10-11), against the lead: snare -2 dB, kick -3, riff -3, mutes -5,
    # toms -5, crash -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KIT, level=127, reverb=20)
    s.mix(T_CYM, level=77, pan=156)
    s.mix(T_GTR, level=96, pan=112)
    s.mix(T_LEAD, level=91, pan=144, delay=30, reverb=30)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/4.", "feedback": 35, "tone": 45, "ping_pong": True, "mix": 25},
         reverb={"on": True, "size": 50, "damp": 45, "mix": 30})
    return one_sound_a_track(s, keep={T_CYM: hat},                     # (the hats on 2, the crash added)
                             levels={snare: 93, tom_hi: 57, tom_mid: 57, tom_lo: 57, crash: 65, mute: 118})


def metal_loop(pool):
    D = lambda n: I("DRUM KIT", n)  # noqa: E731
    kick, snare, hat, crash, choke = D("KICK"), D("SNARE"), D("HAT"), D("CRASH"), D("CRSH CHOKE")
    tom_hi, tom_mid, tom_lo = D("TOM HI"), D("TOM MID"), D("TOM LO")
    # (the instruments of the lead and the power chords swapped: the lead on BAND/ORCH's DIST GTR, the
    # recorded power chord; the riff's open chords on the DX7's)
    power, mute = I("GUITAR/STRGS", "DIST GTR"), I("BAND/ORCH", "GTR MUTE")
    lead = I("BAND/ORCH", "DIST GTR")
    s, (i, p, q) = dj_loop(pool, "METAL LOOP", "Metal, A minor, 128 BPM. A DJ loop of the DRUM KIT and the guitars: "
                           "the double kick in sixteenths under the snare, the GTR MUTE galloping palm-muted with the "
                           "DX7's DIST GTR between, the DIST GTR's power chord tremolo-picking a line. It loops over two "
                           "patterns, "
                           "this verse and a bridge: a half-time breakdown, the riff chugging, the lead bending long "
                           "notes. An intro first, played once: the drums, then the gallop palm-muted; no lead", 128,
                           intro="intro: the drums, then the gallop palm-muted",
                           then="the bridge: a half-time breakdown, the riff chugging, the lead bending long notes")
    T_KIT, T_CYM, T_GTR, T_LEAD = range(1, 5)
    kit = {"K": (kick, 0x70), "S": (snare, 0x74), "h": (tom_hi, 0x64), "m": (tom_mid, 0x68), "l": (tom_lo, 0x6C)}
    BLAST, FILL = "KKKKSKKKKKKKSKKK", "KKKKSKKKSShhmmll"                # the double kick, the snare on 2 and 4
    BREAK = "K..K..K.S..K.K.."                                        # the breakdown: the kick on the chugs
    cym = {"X": (crash, 0x60), "W": (choke, 0x5C), "C": (hat, 0x50), "c": (hat, 0x36)}
    HATS, CRASH, WASH = "C.c.C.c.C.c.C.c.", "X...C.c.C.c.C.c.", "W.W.W.W.W.W.W.W."   # (WASH: choked crashes)

    def riff(q_, figures):
        """the rhythm guitar a bar a figure: (row, root, O an open power chord or M a palm mute, rows); the roots
        A C D G, so the chords' fifths stay in the key's pentatonic notes too"""
        for b, fig in enumerate(figures):
            for r, n, k, ln in fig:
                if k == "O":                                          # (the DX7's, its release under a row)
                    q_.note(T_GTR, 16 * b + r, n, power, 0x46, *dx_rel(power, 70))
                    for x in range(r + 3, r + ln) if ln >= 8 else ():   # (a held one: vibrato, the user's)
                        q_.cmd(T_GTR, 16 * b + x, "445")
                else:
                    q_.note(T_GTR, 16 * b + r, n, mute, 0x70)
                q_.off(T_GTR, 16 * b + r + ln)

    def gallop(beats):
        """palm-muted gallops on A, an eighth and two sixteenths a beat, on these beats"""
        return [(4 * b + r, "A2", "M", 1) for b in beats for r in (0, 2, 3)]

    G3 = gallop((0, 1, 2, 3))
    G4 = [(0, "A2", "O", 4), (4, "C3", "O", 4), (8, "D3", "O", 8)]        # into the fill: "3 2 1"
    # the verse's bars (long notes, as a guitar player swings and then holds): two beats of gallop, then the
    # chord struck and left ringing for two
    RING = {n: gallop((0, 1)) + [(8, n, "O", 8)] for n in ("C3", "D3", "G2")}
    CHUG = [(r, "A2", "M", 1) for r in (0, 3, 6, 11, 13)] + [(8, "C3", "O", 3), (14, "G2", "O", 2)]

    def lead_line(q_, text):
        """the lead; a note held 8 rows or more gets vibrato (4xy) from its fifth row to its end. The recorded
        power chord rings 1.2 s at its own E2 and shorter higher up, so its middle loops (ping-pong, a quarter to
        a half of it) while a note is held"""
        q_.melody(T_LEAD, lead, text, 0x6C)
        r = 0
        for tok in text.split():
            name, _, ln = tok.split(",")[0].partition("/")
            if name != "." and int(ln) >= 8:
                for x in range(r + 4, r + int(ln)):
                    q_.cmd(T_LEAD, x, "445")
            r += int(ln)
        q_.cmd(T_LEAD, next(x for x in range(PAT) if q_.cells[x][T_LEAD - 1] is not None), "*42:02", "*43:40",
               "*44:80")

    def tremolo(*pairs):
        """tremolo picking: each (note, sixteenths) a run of that note in sixteenths"""
        return " ".join(f"{n}/1" for n, k in pairs for _ in range(k)) + " "

    VERSE = (tremolo(("A4", 8), ("C5", 8)) + tremolo(("D5", 8), ("C5", 4), ("A4", 4)) +
             tremolo(("E5", 8), ("D5", 8)) + tremolo(("C5", 8), ("A4", 8)))
    BEND = ("D5/4 E5/12,306 "
            "G5/4 A5/12,306 "
            "A5/4 G5/4 E5/8 "
            "D5/4 E5/12,306 "
            "A5/4 C6/12 "
            "C6/4 D6/8,306 C6/4 "
            "G5/4 A5/12,306 "
            "E5/16")
    # the intro: the drums (a crash on bar 5), then the gallop palm-muted from bar 5; no lead
    drum_bars(i, T_KIT, kit, [BLAST] * 7 + [FILL])
    drum_bars(i, T_CYM, cym, [HATS] * 4 + [CRASH] + [HATS] * 3)
    riff(i, [[]] * 4 + [G3] * 3 + [G4])
    # the verse: the double kick, the gallop riff, the tremolo line twice
    drum_bars(p, T_KIT, kit, [BLAST] * 7 + [FILL])
    drum_bars(p, T_CYM, cym, [CRASH] + [HATS] * 3 + [CRASH] + [HATS] * 3)
    riff(p, [RING[n] for n in ("C3", "D3", "C3", "G2", "C3", "D3", "G2")] + [G4])
    lead_line(p, VERSE * 2)
    # the bridge: the breakdown in half time, the crash in eighths, the chugs, the lead bending long notes
    drum_bars(q, T_KIT, kit, [BREAK] * 7 + [FILL])
    drum_bars(q, T_CYM, cym, [WASH] * 7 + [HATS])
    riff(q, [CHUG] * 7 + [G4])
    lead_line(q, BEND)
    for t in range(1, 5):
        one_voice(i, t, then=p)
        one_voice(p, t, then=q)
        one_voice(q, t, then=p)
    s.play(i, p, q)
    # the master by the instruments' jobs (2026-10-11), against the lead: snare -2 dB, kick -3, mutes -4, toms -5,
    # chords -6, crashes -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KIT, level=94, reverb=15)
    s.mix(T_CYM, level=75, pan=156)
    s.mix(T_GTR, level=127, pan=112)
    s.mix(T_LEAD, level=108, pan=144, delay=25, reverb=25)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/4.", "feedback": 30, "tone": 45, "ping_pong": True, "mix": 25},
         reverb={"on": True, "size": 45, "damp": 50, "mix": 25})
    return one_sound_a_track(s, keep={T_CYM: hat},                     # (the hats on 2, the crashes added)
                             levels={snare: 89, tom_hi: 55, tom_mid: 55, tom_lo: 55, crash: 114, choke: 114,
                                     power: 88})


def electro_loop(pool):
    J = lambda n: I("JUNO-106", n)  # noqa: E731
    kick, snare, hat, clave = J("808 Kick"), J("808 Snare"), J("Hat Closed"), J("Clave")
    bass, seq, zap, timp = J("Seq Bass"), J("Seq Lead"), J("Laser Zap"), I("PERCUSSION", "TIMPANI")
    s, (p,) = dj_loop(pool, "ELECTRO LOOP", "Electro, A minor, 126 BPM. A DJ loop of the JUNO-106: its 808 sounds, "
                      "the 808 Kick broken, the 808 Snare, hats and a clave, a Seq Bass in sixteenths, the TIMPANI "
                      "booming a figure answered by a robotic Seq Lead, a Laser Zap into the loop's start", 126)
    T_KICK, T_DRUMS, T_BASS, T_HOOK = range(1, 5)
    K1, K2 = "x.....x...x.....", "x.....x...x...x."
    p.hits(T_KICK, kick, K1 + K2 + K1 + K2 + K1 + K2 + K1 + "x.....x.x.x.x...", 0x76, cmds=("*4E:04",))
    groove, fill = "h.hhS.hkh.hhS.hk", "h.hhS.hkh.hhSSSS"                  # the snare on 2 and 4, a clave between
    drum_bars(p, T_DRUMS, {"h": (hat, 0x44), "S": (snare, 0x66), "k": (clave, 0x4C)}, [groove] * 7 + [fill],
              cmds=("*4E:02",))                                       # (ENV R short: each let go at the next)
    for k, r in enumerate(range(124, PAT)):                            # the fill's snares rising
        p.note(T_DRUMS, r, None, None, 0x50 + 8 * k)
    up = 24                                                           # (heard two octaves under its notes)
    BASS = {k: [(r, midi(n) + up, ln) for r, n, ln in v] for k, v in {
        "a": [(0, "A1", 2), (3, "A1", 1), (4, "A2", 1), (6, "A1", 2), (10, "A1", 1), (11, "G1", 1), (12, "A1", 2),
              (14, "C2", 2)],
        "b": [(0, "A1", 2), (3, "A1", 1), (4, "A2", 1), (6, "A1", 2), (10, "D2", 2), (12, "C2", 2), (14, "G1", 2)]}.items()}
    bars(p, T_BASS, bass, BASS, "abababab", 0x6C)
    # the TIMPANI's figure, 3-3-..., on A G C at its own register (its C-4 its own pitch)
    STAB = [(0, "A-3", 2, 8), (3, "A-3", 2), (6, "G-3", 2), (10, "A-3", 1), (12, "C-4", 2, 6)]
    ROBOT = {1: "E5/2 E5/2 G5/2 A5/2 G5/2 E5/2 D5/2 C5/2", 3: "E5/2 E5/2 G5/2 A5/2 C6/2 A5/2 G5/2 E5/2",
             5: "E5/2 E5/2 G5/2 A5/2 G5/2 E5/2 D5/2 C5/2", 7: "D5/2 E5/2 G5/2 A5/2 C6/4"}
    for b in range(8):
        if b % 2 == 0:
            for r, n, ln, *dv in STAB:
                p.note(T_HOOK, 16 * b + r, n, timp, 0x74 + (dv[0] if dv else 0))
                p.off(T_HOOK, 16 * b + r + ln)
        else:
            p.melody(T_HOOK, seq, ROBOT[b], 0x46, 16 * b)
    p.note(T_HOOK, 124, "A-4", zap, 0x60)                             # the zap into the loop's start
    for r in range(PAT):                                              # the Seq Lead's release short (ENV R 10)
        c = p.cells[r][T_HOOK - 1]
        if c is not None and c[1] == tuple(seq) and c[0] not in (None, OFF):
            p.cmd(T_HOOK, r, "*4E:0A")
            break
    for t in range(1, 5):
        one_voice(p, t)
    s.play(p)
    # the master by the instruments' jobs (2026-10-11), against the kick: snare -2 dB, bass -3, timpani -4,
    # Seq Lead -5, zap -8, clave -10, hats -12 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_DRUMS, level=80, pan=150, delay=15)
    s.mix(T_BASS, level=80)
    s.mix(T_HOOK, level=88, pan=104, delay=35, reverb=30)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 35, "tone": 50, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 55, "damp": 45, "mix": 30})
    return one_sound_a_track(s, levels={snare: 122, clave: 68, seq: 61, zap: 59})


def world_loop(pool):
    P, BO = (lambda n: I("PERCUSSION", n)), (lambda n: I("BAND/ORCH", n))
    kick = I("DRUM KIT", "KICK")
    darbuka, na, ge = P("DARBUKA"), P("TABLA NA"), P("TABLA GE")
    upright, flute = BO("UPRIGHT"), BO("FLUTE")
    s, (p,) = dj_loop(pool, "WORLD LOOP", "World house, A with B flat and C sharp, 120 BPM. A DJ loop of the "
                      "samplers, after SPICE ROAD's turn: the kick on every beat, the darbuka with tabla strokes "
                      "between, the UPRIGHT on A and E, the FLUTE's melody round B flat and C sharp (they leave the "
                      "other loops' pentatonic notes, so mix its drums and bass with theirs, not its flute with their "
                      "hooks)", 120)
    T_KICK, T_HAND, T_BASS, T_FLUTE = range(1, 5)
    p.hits(T_KICK, kick, "x...x...x...x...", 0x6C, 0, 124)            # (out for the fill's last beat)
    hand = {"D": (darbuka, 0x62), "t": (darbuka, 0x40), "n": (na, 0x50), "g": (ge, 0x5C)}
    GROOVE, ROLL = "D.nt.nD.D.t.Dnt.", "D.nt.nD.D.tnttnn"                # low and high darbuka, tabla between
    drum_bars(p, T_HAND, hand, [GROOVE] * 3 + [ROLL] + [GROOVE] * 3 + ["D.nt.nD.gggg.ttt"],
              cmds=("*4B:5A",))                                       # (R4 90: each let go at the next)
    p.bassline(T_BASS, upright, ["A"] * 8, 16, [(0, 0), (6, 7, -8), (8, 0), (14, 7, -10)], 0x68, octave=1)
    # the flute on A with B flat and C sharp (SPICE ROAD's turn, a fifth up): round the augmented second, down
    # to A at bar 4; bar 8 a pickup (C sharp, B flat, G, B flat) back into its first A, so it loops on
    p.melody(T_FLUTE, flute, "A5/2 Bb5/2 C#6/4 D6/4 E6/4 "
                             "F6/2 E6/2 D6/4 C#6/4 Bb5/4 "
                             "C#6/6 Bb5/2 A5/8 "
                             "./4 G5/4 Bb5/4 A5/4 "
                             "E6/4 F6/2 E6/2 D6/2 E6/2 C#6/4 "
                             "D6/6 C#6/2 Bb5/4 C#6/4 "
                             "Bb5/2 A5/2 G5/2 Bb5/2 A5/8 "
                             "A5/8 C#6/2 Bb5/2 G5/2 Bb5/2", 0x70)     # (a pickup back into its first A)
    p.cmd(T_FLUTE, 0, "*4B:50")                                       # (R4 80: let go within a row)
    for t in range(1, 5):
        one_voice(p, t)
    s.play(p)
    # the master by the instruments' jobs (2026-10-11), against the kick: flute -1 dB, darbuka -2, upright -3,
    # tablas -5 (LUFS, each solo over the loop part)
    s.mix(T_KICK, level=127)
    s.mix(T_HAND, level=93, pan=150, reverb=20)
    s.mix(T_BASS, level=79)
    s.mix(T_FLUTE, level=94, pan=104, delay=30, reverb=40)
    s.fx(routing="sends",
         delay={"on": True, "time": "1/8.", "feedback": 35, "tone": 45, "ping_pong": True, "mix": 30},
         reverb={"on": True, "size": 65, "damp": 40, "mix": 35})
    return one_sound_a_track(s, levels={na: 90, ge: 90})


MAKERS = [punch_in, spice_road, midnight_run, city_lights, acid_test, adventure]   # (catalog.SONGS' order)
LOOP_MAKERS = [trance_loop, breaks_loop, techno_loop, acid_loop,                  # (then the DJ loops)
               disco_loop, chip_loop, hip_hop_loop, dnb_loop, rock_loop, metal_loop, electro_loop, world_loop]

# each song's loudness made the same (-23.4 LUFS, BS.1770, as rendered by the engines with the disk's LEVELs):
# its GLOBAL VOLUME (0-64) brings it down by these dB
# (after the remix of 2026-10-10 PUNCH IN, MIDNIGHT RUN, SPICE ROAD and CITY LIGHTS stay under it at 0.0:
# their loudest tracks are at LEVEL 127 already). The DJ loops are mastered by the instruments' jobs
# (2026-10-11; mastering is about the instruments' jobs, not every track at the same level), as the demo
# songs' remix: each instrument's loudness against the kick, or the lead in ROCK LOOP and METAL LOOP, set by
# its LEVEL in the loop's mix (the loudest track at 127), then all at the same -26.0 LUFS over their loop part:
# the most DNB LOOP reaches, so any two match on the decks
TRIM_DB = {"CITY LIGHTS": 0.0, "MIDNIGHT RUN": 0.0, "ACID TEST": -2.0, "SPICE ROAD": 0.0, "ADVENTURE": -1.2,
           "PUNCH IN": 0.0, "TRANCE LOOP": -3.5, "BREAKS LOOP": 0.0, "TECHNO LOOP": -1.3, "ACID LOOP": -2.2,
           "DISCO LOOP": -0.9, "CHIP LOOP": -2.1, "HIP-HOP LOOP": -0.8, "DNB LOOP": 0.0, "ROCK LOOP": -2.1,
           "METAL LOOP": -1.6, "ELECTRO LOOP": -2.0, "WORLD LOOP": -0.5}


def build(pool):
    return [make(pool) for make in MAKERS + LOOP_MAKERS]


def main():
    pool = Pool()
    songs = build(pool)
    folders = {n: f for f, n in SONGS}
    for s in songs:
        if folders[s.name] in DJ_LOOPS:
            if s.loop_from not in (0, 1) or not 1 <= len(s.order) - s.loop_from <= 2 or \
                    any(len(p.cells) != PAT for p in s.pats) or s.at_end != "loop":
                raise SystemExit(f"songs: {s.name}: a DJ loop is one or two patterns of {PAT} rows that loop "
                                 f"(after an intro of {PAT} rows, if it has one)")
        elif len(s.order) != 6 or any(len(p.cells) != PAT for p in s.pats):
            raise SystemExit(f"songs: {s.name}: {len(s.order)} positions (six of {PAT} rows each)")
        s.gvol = round(64 * 10 ** (TRIM_DB[s.name] / 20))
        s.write(DISK / "songs" / folders[s.name])
        cells = sum(p.cell_count() for p in s.pats)
        print(f"songs: {s.name:14s} {len(s.pats):2d} patterns, {s.ntrk:2d} tracks, {cells:5d} cells, "
              f"{s.seconds():5.1f} s")


if __name__ == "__main__":
    main()
