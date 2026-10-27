# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The interrupts' stack's worst case, worked out from the device's code (../../specs/files/ram.md#the-stacks), and
core 2's jobs' stack's apart.

build.py runs it on the image's disassembly (objdump -d): each function's frame (what it pushes and the room
it makes: `[--sp] = {...}`, `sp += -N`) and its deepest call, for the audio interrupt (isr_alnk0), TIMER5's on
top of it (isr_timer5: it interrupts the audio's) and the fault handler on top of both (fm1_fatal_common, entered
from a stub that has pushed one word: the exception interrupts anything). Every frame is counted whole wherever
the function calls, and a tail call (`goto f`) as a call, so the result is an upper bound, never a guess low.
Core 2's jobs' stack holds its loop (fm1_c1_loop) and the jobs it runs; its exceptions' stack only a fault's frames
(the stub's word, fm1_fatal_common's, fm1_fault_c's: on core 2 it calls nothing but its park).

The code cannot show where a call through a pointer goes (`call rN`, `goto rN`): HOOKS lists every hook the
interrupts call, the functions it may hold and the functions that call through it; each call is counted at its
deepest target. A call through a pointer in a function not listed, or recursion anywhere in the tree, fails the
build: a worst case with either would only be a lower bound. A function deliberately deep (the debug build's
stack overflow) is listed in DELIBERATE: left out of the worst case, its own depth unbounded.

The table is kept by hand. When a hook is called in a function the table does not name (a new function pointer,
a new caller of one, or the compiler inlining a caller into another function), the build names that function: add
it to the hook's callers (or a new hook, with every function the pointer may hold). A function in core 2's room
calling through a hook must find every target in the room too (out of a call's reach of the image).
"""
import re

# Every hook the interrupts call: what it is, the functions it may hold, and the functions (as the compiler leaves
# them, after inlining) that call through it. A function calling through several hooks takes all their targets. A
# name stands for every function of that name (static ones may repeat); a target the image lacks is reported, not
# counted (a hook set by nothing in this build).
HOOKS = (
    ("player_out, player_usb: the output's and USB audio's taps (app_audio_out, app_audio_usb)",
     ["out_block", "usb_block"], ["player_audio", "pl_audio_c"]),
    ("pl_src: a source's own song, pattern and tracks' sounds (LOOPS; RECORD PATTERN's preview)",
     ["src_song", "src_pattern", "src_inst", "pv_song", "pv_pattern", "pv_inst"],
     ["pl_handle", "pl_start", "pl_start_tail", "pl_ib_build", "pl_ib_module", "pl_cell_get", "pl_gp_load",
      "pl_get_note", "pl_rec_song", "pl_rec_pattern", "pl_src_insts", "player_audio"]),
    ("module->get_note: IBXM's cell hook (the player's pl_get_note)", ["pl_get_note"], ["replay_row_"]),
    ("player_note_hook: MIDI OUT's notes (midi.c's track_note)", ["track_note"],
     ["pl_after_tick", "pl_note_on", "pl_take_cell", "pl_legato"]),
    ("FL_FAR: the flash driver, in RAM out of a call's reach", ["fl_read_ram"], ["plat_flash_read", "st_read"]),
    ("store_set_edits: an outside edit memory (set by nothing in this firmware)", [], ["edit_get", "store_get"]),
    ("store_watch: an edit tells the player, which reads again what it plays (the edit memory's pieces)",
     ["player_store_watch"], []),
    ("fast_fn: plat_fast's 1 kHz tick (the controller inside PT/FM: ctlmode.c's cm_fast)", ["cm_fast"],
     ["fm1_timer5_irq", "fast_tick"]),
    ("plat_c2_post's job: a block's effects and output, or its voice job, handed over (one core, or core 2 stopped: run "
     "at once)", ["pl_c2_job", "pl_v2_job"], ["plat_c2_post"]),
    ("PLAT_FAR: the voice job's time, core 2's timer read in its room (the job runs from the image)", ["plat_c2_us"],
     ["pl_v2_job"]),
    ("PLAT_FAR: core 1 runs a block's job, CUT's, the output stage or the half's effects, in core 2's room (RAM)",
     ["pl_c2_job", "pl_cut_job", "pl_audio_c", "fx_blocks"], ["player_audio"]),
    ("PLAT_FAR: a change to the effects' own state made at once (fx_ev, and its callers it is inlined into)",
     ["fx_ev_now"], ["fx_ev", "fx_click", "fx_punch", "fx_punch_stop", "fx_kill", "fx_empty", "fx_setup",
                     "fx_setup_keep", "fx_strip_noise"]),
    ("PLAT_FAR: DJ's decks' own effects and their DJ FILTERs, on core 1 with the effects' code in core 2's room",
     ["fx_strip_lr", "fx_djfilter", "fx_djfilter_rest"], ["pl_audio_a", "pl_dj_mix"]),
    ("core 2's queue: the jobs its loop runs, each handed over by fm1_cpu1_post (a block's job or its voice job,"
     " plat_c2_post's; the debug build's PROVE 4 and 6 in its place)", ["pl_c2_job", "pl_v2_job", "fm1_c1_prove_job"],
     ["fm1_c1_loop"]),
    ("core 2's park: a fault on core 2 waits there, in its room, out of a call's reach (fm1_irq.h)", ["fm1_c1_park"],
     ["fm1_fault_c"]),
)

# hook targets only the debug build has: not reported missing from the others
DEBUG_ONLY = {"fm1_c1_prove_job"}

# the one job core 2's loop runs from the image, not its room (specs/second-core.md, Core 2 and the flash): the voice
# job, which runs the engines and reads the voices' data from the flash; every flash operation waits for core 2 idle
IMAGE_JOBS = {"pl_v2_job"}

# functions deliberately deep: left out of the worst case (each named with why)
DELIBERATE = {
    "prove_dive": "the debug build's deliberate stack overflow (PROVE): it recurses until the stack check fires",
}

ROOTS = (("the audio interrupt", "isr_alnk0", 0),
         ("TIMER5, over the audio's", "isr_timer5", 0),
         ("the fault handler, over both", "fm1_fatal_common", 4))   # (its stub pushed rets first)
C2_ROOT = ("core 2's loop and its jobs", "fm1_c1_loop", 0)         # (on its jobs' stack, entered by a return)
C2_FAULT = ("fm1_fatal_common", "fm1_fault_c", "fm1_c1_park")      # a fault on core 2, on its exceptions' stack

LINE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_.$]*):$")
CALL = re.compile(r"\bcall -?\d+ <([A-Za-z_][A-Za-z0-9_.$]*)(\+0x[0-9A-Fa-f]+)? : ([0-9a-f]+) >")
TAIL = re.compile(r"^\s*goto -?\d+ <([A-Za-z_][A-Za-z0-9_.$]*) : ([0-9a-f]+) >")
INDIRECT = re.compile(r"^\s*(?:call|goto) r\d+\b|\bpc = r\d+\b")
PUSH = re.compile(r"\[--sp\] = (\{[^}]*\}|\w+)")
ALLOC = re.compile(r"\bsp \+= -(\d+)\b")


def regs(lst):
    """the registers a push names: {rets, r12-r4} -> 10"""
    n = 0
    for it in lst.strip("{}").split(","):
        it = it.strip()
        m = re.fullmatch(r"r(\d+)-r(\d+)", it)
        n += abs(int(m.group(1)) - int(m.group(2))) + 1 if m else 1 if it else 0
    return n


def parse(dis):
    """{address: function} from objdump -d's text: name, frame bytes, direct calls (addresses), indirect calls"""
    funcs, cur = {}, None
    for ln in dis.splitlines():
        m = LABEL.match(ln)
        if m:
            if m.group(1).startswith("."):          # (a jump table's label inside a function)
                continue
            cur = {"name": m.group(1), "frame": 0, "calls": set(), "indirect": 0, "addr": None}
            continue
        m = LINE.match(ln)
        if not m or cur is None:
            continue
        a, ins = int(m.group(1), 16), m.group(2)
        if cur["addr"] is None:
            cur["addr"] = a
            funcs[a] = cur
        p = PUSH.search(ins)
        if p:
            cur["frame"] += 4 * regs(p.group(1))
        p = ALLOC.search(ins)
        if p:
            cur["frame"] += int(p.group(1))
        p = CALL.search(ins)
        if p:
            cur["calls"].add(int(p.group(3), 16) - (int(p.group(2), 16) if p.group(2) else 0))
        else:
            p = TAIL.match(ins)
            if p and p.group(1) != cur["name"]:
                cur["calls"].add(int(p.group(2), 16))
        if INDIRECT.search(ins):
            cur["indirect"] += 1
    return funcs


def analyse(dis, room=None):
    """(the report's lines, errors, warnings, the interrupts' worst case, core 2's jobs' and its exceptions', in
    bytes); room: core 2's room's code (lo, hi), whose calls through a hook must stay in it"""
    funcs = parse(dis)
    by_name = {}
    for a, f in funcs.items():
        by_name.setdefault(f["name"], []).append(a)
    errors, warns, lines = [], [], []
    hooks_used = set()
    memo, stack = {}, []
    called_by = {}                              # function name -> the hooks it calls through
    for i, (_, targets, callers) in enumerate(HOOKS):
        for c in callers:
            called_by.setdefault(c, []).append(i)
        for t in targets:
            if t not in by_name and t not in DEBUG_ONLY:
                warns.append(f"stack: hook target {t} is not in the image (not counted): {HOOKS[i][0]}")

    def depth(a):
        if a in memo:
            return memo[a]
        f = funcs.get(a)
        if f is None:
            errors.append(f"stack: a call to {a:#x}, no function there")
            memo[a] = (0, [])
            return memo[a]
        if f["name"] in DELIBERATE:
            memo[a] = (0, [f["name"] + " (deliberate: left out)"])
            return memo[a]
        if a in stack:
            cyc = [funcs[x]["name"] for x in stack[stack.index(a):]] + [f["name"]]
            errors.append("stack: recursion in a stack's tree: " + " -> ".join(cyc))
            return (0, [])
        stack.append(a)
        callees = set(f["calls"])
        if f["indirect"]:
            hs = called_by.get(f["name"])
            if not hs:
                errors.append(f"stack: {f['name']} calls through a pointer ({f['indirect']}x) in a stack's tree: "
                              f"add it to its hook's callers in fw/tools/stackdepth.py (HOOKS)")
            for i in hs or ():
                hooks_used.add(i)
                for t in HOOKS[i][1]:
                    callees.update(by_name.get(t, []))
                    if room and room[0] <= a < room[1] and t not in IMAGE_JOBS and any(not room[0] <= x < room[1]
                                                                                        for x in by_name.get(t, [])):
                        errors.append(f"stack: {f['name']}, in core 2's room, calls through a pointer to {t}, "
                                      f"outside it (HOOKS: {HOOKS[i][0]})")
        best, path = 0, []
        for c in sorted(callees):
            d, p = depth(c)
            if d > best:
                best, path = d, p
        stack.pop()
        memo[a] = (f["frame"] + best, [f"{f['name']} {f['frame']}"] + path)
        return memo[a]

    total = 0
    lines.append("  the interrupts' stack, worst case (bytes; each frame whole, the deepest call; fw/tools/stackdepth.py)")
    for what, name, extra in ROOTS:
        if name not in by_name:
            errors.append(f"stack: {name} ({what}) is not in the image")
            continue
        d, path = depth(by_name[name][0])
        d += extra
        total += d
        lines.append(f"    {what:<30} {d:>6}   {' > '.join(path[:12])}{' > ...' if len(path) > 12 else ''}")
    lines.append(f"    {'all three at once':<30} {total:>6}")
    c2, c2_exc = 0, 0
    what, name, extra = C2_ROOT
    if name not in by_name:
        errors.append(f"stack: {name} ({what}) is not in the image")
    else:
        c2, path = depth(by_name[name][0])
        c2 += extra
        lines.append(f"  core 2's jobs' stack, worst case")
        lines.append(f"    {what:<30} {c2:>6}   {' > '.join(path[:12])}{' > ...' if len(path) > 12 else ''}")
    frames = [funcs[by_name[n][0]]["frame"] if n in by_name else None for n in C2_FAULT]
    if None in frames:
        errors.append(f"stack: core 2's fault path ({', '.join(C2_FAULT)}) is not all in the image")
    else:
        c2_exc = 4 + sum(frames)                # (the stub's word first)
        lines.append(f"  core 2's exceptions' stack: a fault's frames {c2_exc} "
                     f"(4 + {' + '.join(f'{n} {fr}' for n, fr in zip(C2_FAULT, frames))})")
    for i in sorted(hooks_used):
        lines.append(f"    hook: {HOOKS[i][0]} -> {', '.join(HOOKS[i][1]) or '(none)'}")
    return lines, errors, warns, total, c2, c2_exc
