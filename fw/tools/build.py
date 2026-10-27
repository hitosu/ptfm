#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Vasily Kazantsev
"""Build PT/FM's firmware for the FM-1: the app (fw/core on fw/device), the update loader, the package
and its install page.

  fw/build.sh [--release | --debug] [--bringup]   (it runs this; see README.md for the toolchain and the SDK)

  dev (the default)  ../build/fw/ptfm.fwsc, ../build/fw/install.html; the version: fw/VERSION's number
                     (set by hand) with " DEV" after it
  --release          ../dist/firmware/ptfm.fwsc, ../dist/firmware/install.html; the version: fw/VERSION's
                     number alone (the build's time printed beside it)
  --debug            the debug build (../specs/debug.md): PTFM_DEBUG defined, identity FM-1_7102, the version:
                     the number, its own build time (month, day, hour, minute) and " DEBUG", so each debug
                     build is told apart, within the 23 characters its records keep; ../dist/debug/ptfm-debug.fwsc,
                     ../dist/debug/install.html (with its
                     FIGURES); intermediate files in ../build/fw-debug/; 7 KB of RAM kept spare, not 8
  --bringup          the device layer alone with fw/device/bringup.c as the app (a screen, a tone, the
                     LEDs following the panel): for trying the hardware side; never released
  Intermediate files: ../build/fw/ (objects, ptfm.bin, the listing, gen/: the generated sources)

The JieLi toolchain is Linux x86-64 only: JIELI_TOOLCHAIN points at
it; JIELI_DOCKER=1 (the default) runs each tool in an offline container, 0 runs it natively.
"""
import hashlib
import os
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]           # fw/
ROOT = SRC.parent                                   # PT/FM's repository
OUT = ROOT / "build" / "fw"
OBJ = OUT / "obj"
GEN = OUT / "gen"                                   # fw/tools/gen.py: the app's generated sources
GEN_DEV = OUT / "gen_dev"                           # the device's own screens' fonts (gfx.c)
RW = OUT / "rw"                                     # fw/core with its texts' numbers: what the app compiles
LDR = OUT / "loader"
DIST = ROOT / "dist" / "firmware"                  # the release: the package, its page, the demo disk
DIST_DEBUG = ROOT / "dist" / "debug"                # the debug build's
VERSION = SRC / "VERSION"
SPARE = 8192                                        # RAM kept spare (the debug build: 7 KB)
DEBUG = False                                       # --debug (main sets it, with the folders above)
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402
import make_installer  # noqa: E402
import stackdepth  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
HOT = ("core/eng/", "core/play/", "core/fx/")       # the sound's code: -O2
COLD = '-DPLAT_COLD=__attribute__((minsize,noinline))'   # (but what it runs seldom, marked: built for size, never
                                                          # inlined into what runs often; core/plat.h)
SMALL = ("core/ui/", "core/ctlmode.c", "core/bench.c", "core/dbg.c", "core/remote.c", "core/store/", "core/midi/")
SMALL_NOT = ("gfx.c",)  # the screens', the controller's, the debug build's, the store's and MIDI's code: -Oz, the
                        # smallest (but the drawing's own); the device layer and the rest -Os
POOL = '-DPLAT_POOL=__attribute__((section(".pool")))'
SPARE_SEC = '-DPLAT_SPARE=__attribute__((section(".spare")))'   # the spare zone's own part: the data region's last
SPARE_OWN = 24576                                               # object (spare.h's SPARE_OWN)
# core 2's room (core/plat.h, ../specs/second-core.md#ram): the code a block's job reaches and its constant tables,
# linked into RAM (device/app.ld's C2RAM) and copied there from the image at power-on
C2_SEC = ['-DPLAT_C2=__attribute__((section(".c2_text")))', '-DPLAT_C2_RO=__attribute__((section(".c2_rodata")))']
# Where core 2's room must put things, by what the chip does (measured facts, as DMA_FLOOR): the chip's ROM writes its
# start stack inside ROM_STACK at every start of core 2 (the room's jobs' stack must cover it, holding nothing then);
# from TOP_SAFE up neither a restart nor the update loader writes (its loop, park, queue and fault record lie there)
ROM_STACK = (0x01C6F800, 0x01C70000)
TOP_SAFE = 0x01C72000
FLASH_XIP = (0x02000000, 0x020FC000)                # the 1 MiB flash as the CPU reads it in place (hal/fm1_xip.h)
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")
TARGET = re.compile(r"<[^<>]* : ([0-9a-f]+) >")     # objdump's note of an address an instruction names
BRANCH = re.compile(r"\b(?:call|goto)\s+-?\d+\b")   # a direct call or jump (a register's has no number)

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}
# The update loader, byte for byte the one the installers know ("ota-FM-1_900"); it builds from device/usb.c,
# ota.c and libc.c.
LOADER_SHA256 = "ffba642c23c5d6e4ad5a708c4d15d027bdfc6153da34e05e8052c8b4238aa3b5"

PRODUCT = "FM-1_7101"               # the package identity: the installers need FM-1_<digits>
if not re.fullmatch(r"FM-1_\d+", PRODUCT):
    raise SystemExit(f"build: identity {PRODUCT!r}: the installers need FM-1_<digits>")
# The RAM's parts come from device/app.ld's symbols (the data region _data_lo .. _data_top: the pool, then the
# variables; the stacks; the records kept across restarts), never from numbers of the build's own. One number is a
# measured fact, not a layout: from DMA_FLOOR up a restart changes nothing and an update's restart unpacks no
# loader, and every buffer a DMA writes or reads (USB's, the screen's, MIDI IN's ring, the sound's) has always
# lain there (specs/files/ram.md#the-layout).
DMA_FLOOR = 0x01C08000
DMA_BUFFERS = ("ep0buf", "ep1tx", "ep1rx", "ep4tx",         # USB (device/usb.c)
               "lcd_buf", "lcd_small",                      # the screen's line buffer, its commands (main.c, lcd.c)
               "um_ring",                                   # MIDI IN's ring (device/midi_uart.c)
               "abuf")                                      # the sound's two halves (main.c)


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see README.md)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "1") == "1" or not native


def rel(a):
    return os.path.relpath(a, SRC) if isinstance(a, Path) else a


def docker_cmd(*cmd):
    return ["docker", "run", "--rm", "--platform", "linux/amd64", "--network", "none",
            "--user", f"{os.getuid()}:{os.getgid()}", "-v", f"{ROOT}:/work",
            "-v", f"{toolchain()}:/opt/jieli:ro", "-w", f"/work/{SRC.name}", DOCKER_IMAGE, *cmd]


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    r_args = [rel(a) for a in args]
    if tool == "cc":                # the toolchain's cc wrapper is a Python script; call clang directly
        tool, r_args = "pi32v2/bin/clang", ["-target", "pi32v2", *r_args]
    if use_docker():                # PT/FM's tree at /work (the build goes to ../build), the toolchain read-only
        cmd = docker_cmd(f"/opt/jieli/{tool}", *r_args)
    else:
        cmd = [str(toolchain() / tool), *r_args]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def compile_all(jobs):
    """jobs: [(source, object, flags)]; every compile in one container (docker), in parallel"""
    OBJ.mkdir(parents=True, exist_ok=True)
    clang = "/opt/jieli/pi32v2/bin/clang" if use_docker() else str(toolchain() / "pi32v2" / "bin" / "clang")
    lines = ["#!/bin/sh", "fail=0"]
    for i, (src, obj, flags) in enumerate(jobs):
        log = OBJ / f"{Path(obj).name}.log"
        cmd = " ".join(shlex.quote(a) for a in [clang, "-target", "pi32v2", *flags, "-c", rel(src), "-o", rel(obj)])
        lines.append(f"( {cmd} ) > {shlex.quote(rel(log))} 2>&1 &")
        lines.append(f"p{i}=$!")
        if i % 8 == 7 or i == len(jobs) - 1:   # eight at a time
            for j in range(i - i % 8, i + 1):
                lines.append(f"wait $p{j} || fail=1")
    lines.append("exit $fail")
    script = OUT / "compile.sh"
    script.write_text("\n".join(lines) + "\n")
    cmd = docker_cmd("/bin/sh", rel(script)) if use_docker() else ["/bin/sh", rel(script)]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    bad = False
    for src, obj, _ in jobs:
        log = OBJ / f"{Path(obj).name}.log"
        text = log.read_text() if log.exists() else ""
        if text.strip():
            sys.stderr.write(f"--- {rel(src)}\n{text}")
        if not Path(obj).exists() or " error: " in text:
            bad = True
    if r.returncode or bad:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build: compiling failed")


def generate(bringup):
    """the app's generated sources (fw/tools/gen.py; not for the bring-up app) and the device screens' fonts"""
    GEN.mkdir(parents=True, exist_ok=True)
    GEN_DEV.mkdir(parents=True, exist_ok=True)
    cmds = [[sys.executable, "-I", str(ROOT / "tools" / "gen_fonts.py"), "--c", str(GEN_DEV / "fonts.h")]]
    if not bringup:
        cmds.insert(0, [sys.executable, "-I", str(SRC / "tools" / "gen.py"), str(GEN)])
    for cmd in cmds:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        sys.stdout.write(r.stdout + r.stderr)
        if r.returncode:
            raise SystemExit(f"build: {Path(cmd[2]).name} failed")


# ---- update loader (unchanged: its bytes are checked)

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = SRC / "loader"
    flags = [*CFLAGS, "-Ihal", "-Idevice"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                         ("common/bin/objdump", "-d", elf),
                         ("common/bin/objdump", "-h", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    return ota


# ---- app

def core_sources(bringup):
    if bringup:
        return [SRC / "device" / "bringup.c"]
    return sorted(SRC.glob("core/**/*.c")) + sorted(GEN.glob("*.c"))


def packed_copies():
    """fw/core's sources as the firmware compiles them, each fixed text's mark (T("..."), TX("...")) its number in
    the packed table (specs/ui/texts.md; fw/tools/gen_texts.py --rewrite): the folder they are in"""
    if RW.exists():
        shutil.rmtree(RW)
    r = subprocess.run([sys.executable, "-I", str(SRC / "tools" / "gen_texts.py"), str(GEN), "--rewrite", str(RW)],
                       cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(r.stdout + r.stderr)
    if r.returncode:
        raise SystemExit("build: gen_texts.py --rewrite failed")
    return RW


def build_app(version, bringup):
    debug = ["-DPTFM_DEBUG"] if DEBUG else []          # (the debug build: specs/debug.md)
    dflags = [*CFLAGS, "-Ihal", "-Idevice", "-Icore", "-I" + rel(GEN_DEV), f'-DFW_ID="{PRODUCT}"', *C2_SEC,
              *(["-DPTFM_BRINGUP"] if bringup else []), *debug]
    rw = None if bringup else packed_copies()      # (the app's core compiled from the packed copies)
    core_inc = rel(rw) if rw else "core"
    cflags = [*CFLAGS, "-I" + core_inc, "-Idevice/include", "-I" + rel(GEN), POOL, SPARE_SEC, *C2_SEC, COLD,
              f'-DPTFM_VERSION="{version}"', *(["-DUI_TXT_PACKED", "-Werror=int-conversion"] if rw else []), *debug]
    # (packed: a table's text number given where a text is wanted, without T(), is an error, not a warning)
    jobs = [(SRC / "device" / "crt0.S", OBJ / "crt0.o", []),
            (SRC / "hal" / "fm1_vec.S", OBJ / "fm1_vec.o", []),
            (SRC / "hal" / "fm1_isr.S", OBJ / "fm1_isr.o", []),
            (SRC / "hal" / "fm1_cpu1.S", OBJ / "fm1_cpu1.o", []),          # core 2's entry
            (SRC / "device" / "main.c", OBJ / "main.o", dflags)]
    hdirs = sorted({rel(rw / h.parent.relative_to(SRC / "core") if rw else h.parent) for h in SRC.glob("core/**/*.h")})
    for s in core_sources(bringup):
        r = rel(s)
        name = r.replace(rel(GEN) + "/", "gen/").replace("/", "_").replace(".c", ".o")
        fl = list(cflags)
        if r.startswith(HOT):
            fl[0] = "-O2"
        elif r.startswith(SMALL) and not r.endswith(SMALL_NOT):
            fl[0] = "-Oz"
        if s.parent == GEN:                       # generated sources name core's headers by file name
            fl += ["-I" + d for d in hdirs]
        if bringup:
            fl += ["-Idevice"]
        src = rw / s.relative_to(SRC / "core") if rw and r.startswith("core/") else s
        jobs.append((src, OBJ / name, fl))
    for f in OBJ.glob("*.o"):
        f.unlink()
    compile_all(jobs)
    elf = OUT / "ptfm.elf"
    objs = [o for _, o, _ in jobs]
    try:
        tc("pi32v2/bin/ld", "-T", SRC / "device" / "app.ld", *objs, "-Map", OUT / "ptfm.map", "-o", elf)
    except SystemExit:
        measure(objs)                             # what does not fit, by object, before failing
        raise
    c2_parts = ("c2text.bin", "c2ro.bin", "c2top.bin")
    for sect in ("text.bin", "data.bin", "ramtext.bin", "c2.bin", *c2_parts):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".c2_text", elf, OUT / c2_parts[0]),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".c2_rodata", elf, OUT / c2_parts[1]),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".c2_top", elf, OUT / c2_parts[2]),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "ptfm.dis").write_text(dis)
    (OUT / "ptfm.sym").write_text(syms)
    # core 2's room's code, tables, loop and park: one run in RAM and in the image (app.ld checks), so one blob
    (OUT / "c2.bin").write_bytes(b"".join((OUT / p).read_bytes() for p in c2_parts if (OUT / p).exists()))

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text, core 2's room (its code, its tables, its loop and park: one run, app.ld checks) and .data follow
    # .text at their load addresses; fm1_cstart copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("c2.bin", "_c2_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    c2_run = (OUT / "c2.bin").stat().st_size if (OUT / "c2.bin").exists() else 0
    if c2_run != symv("_c2_end") - symv("_c2_start"):
        raise SystemExit(f"build: core 2's room's run in the image is {c2_run} B, not _c2_start .. _c2_end")
    (OUT / "ptfm.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def dis_sections(dis):
    """objdump -d's text cut by section: {name: [lines]}"""
    out, cur = {}, None
    for ln in dis.splitlines():
        m = re.match(r"^Disassembly of section (\S+):\s*$", ln)
        if m:
            cur = out.setdefault(m.group(1), [])
        elif cur is not None:
            cur.append(ln)
    return out


def insns(lines):
    """[(address, text, function)] of each instruction; a target's annotation objdump wrapped onto the next line (an
    instruction inside a block's braces: `} <name : address >`) is joined to its instruction"""
    out, fn = [], None
    for ln in lines:
        m = LINE.match(ln)
        if m:
            out.append([int(m.group(1), 16), m.group(3), fn])
            continue
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_.$]*):\s*$", ln)
        if m:
            fn = m.group(1)
        elif out and TARGET.search(ln):
            out[-1][1] += " " + ln.strip()
    return out


def room_closure(lines, lo, hi):
    """core 2's room's code runs on while the flash cannot serve it, out of a direct call's reach of the image: every
    direct call or jump lands in the room [lo, hi), and nothing names a flash address (code or a constant read in
    place: objdump's annotations of a branch's target or a literal). (errors, instructions)"""
    errors, code = [], insns(lines)
    for a, text, fn in code:
        b = BRANCH.search(text)
        if b:
            t = TARGET.search(text, b.end())
            if not t:
                errors.append(f"core 2's room: {fn} at {a:#x}: a branch whose target objdump does not name: "
                              f"{text.strip()}")
            elif not lo <= int(t.group(1), 16) < hi:
                errors.append(f"core 2's room: {fn} at {a:#x} calls or jumps out of the room, to "
                              f"{t.group(0).strip('<> ')} (code there must call only code marked PLAT_C2: "
                              f"core/plat.h)")
                continue
        for t in TARGET.finditer(text):
            v = int(t.group(1), 16)
            if FLASH_XIP[0] <= v < FLASH_XIP[1]:
                errors.append(f"core 2's room: {fn} at {a:#x} names a flash address, {t.group(0).strip('<> ')} "
                              f"(a table core 2 reads is PLAT_C2_RO, a function it calls PLAT_C2: core/plat.h)")
    return errors, len(code)


def pair_writes(text):
    """the registers an instruction's text writes: its destination (`r5 = ..`, `r3_r2 += ..`), and a base register
    it steps (`[r1++=4]`, `[--r2]`)"""
    t = text.split("  #")[0].split(" <")[0].strip()
    out = set()
    m = re.match(r"^(r\d+(?:_r\d+)?)\s*(?:[-+*/&|^%]|<<|>>)?=(?!=)", t)
    if m:
        out |= set(m.group(1).split("_"))
    out |= set(re.findall(r"\[(r\d+)(?:\+\+|--)", t)) | set(re.findall(r"\[(?:\+\+|--)(r\d+)", t))
    return out


def pair_clashes(dis):
    """paired instructions (the first's text ends in `#`, the next instruction is its second half) whose two halves
    write one register: which write wins is not known (the device's compiler was seen pairing a dead load with a
    live write of the same register)"""
    out, prev = [], None
    for ln in dis.splitlines():
        m = LINE.match(ln)
        if not m:
            continue
        text = m.group(3)
        if prev is not None:
            both = pair_writes(prev[1]) & pair_writes(text)
            if both:
                out.append(f"{prev[0]}: {prev[1].strip()} | {text.strip()}")
            prev = None
            continue
        if re.search(r"\s#(\s|$)", text):
            prev = (m.group(1), text)
    return out


def by_object(map_text):
    """the linker's map: each object's bytes in RAM's variables (.data, .bss), the pool and the image
    (.text with its constants, .ram_text, core 2's room's code and tables): {object: [ram, pool, image]}"""
    out, sect, pending = {}, None, None
    kind = {".data": 0, ".bss": 0, ".pool": 1, ".text": 2, ".ram_text": 2, ".c2_text": 2, ".c2_rodata": 2,
            ".c2_top": 2}
    for ln in map_text.splitlines():
        m = re.match(r"^(\.[\w.]+)\s", ln) or re.match(r"^(\.[\w.]+)$", ln)
        if m and not ln.startswith(" "):
            sect = kind.get(m.group(1))
            pending = None
            continue
        if sect is None:
            continue
        m = re.match(r"^ (?:[\w.*]+|COMMON)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$", ln)
        if not m and re.match(r"^ ([\w.]+|COMMON)\s*$", ln):
            pending = True                        # a long input section's name: its numbers on the next line
            continue
        if m:
            size, obj = int(m.group(2), 16), Path(m.group(3)).name
            out.setdefault(obj, [0, 0, 0])[sect] += size
            pending = None
    return out


def app_ld_region(name):
    """(origin, length) of a MEMORY region as device/app.ld writes it (only where no linked image tells)"""
    m = re.search(rf"\n\s*{name}\s*\([a-z]+\)\s*:\s*ORIGIN = (0x[0-9A-Fa-f]+), LENGTH = (0x[0-9A-Fa-f]+)",
                  (SRC / "device" / "app.ld").read_text())
    return (int(m.group(1), 16), int(m.group(2), 16)) if m else (0, 0)


def measure(objs):
    """the link failed (a region overflowed, perhaps): link again with every region far too big, only for
    the map, and print what each object takes"""
    ld = (SRC / "device" / "app.ld").read_text()
    for name, origin, length in (("XIP", "0x10000000", "0x4000000"), ("DATA", "0x20000000", "0x4000000"),
                                 ("C2RAM", "0x30000000", "0x4000000")):
        ld = re.sub(rf"(\n\s*{name}\s*\([a-z]+\)\s*:\s*ORIGIN = )0x[0-9A-Fa-f]+(, LENGTH = )[0-9A-Fa-fxK]+",
                    rf"\g<1>{origin}\g<2>{length}", ld)
    ld = re.sub(r"\n\s*ASSERT\(.*?\"\)", "", ld, flags=re.S)   # (the layout's own checks: the measuring link maps)
    script = OUT / "measure.ld"
    script.write_text(ld)
    try:
        tc("pi32v2/bin/ld", "-T", script, *objs, "-Map", OUT / "measure.map", "-o", OUT / "measure.elf")
    except SystemExit:
        print("build: (the measuring link failed too: undefined symbols?)")
        return
    print(report(by_object((OUT / "measure.map").read_text(errors="replace")), app_ld_region("DATA")[1]))


def wide_divides(dis, map_text):
    """who calls the 64-bit divide helpers (device/main.c): {function: object}"""
    where, pending = {}, None
    for ln in map_text.splitlines():              # function -> object: -ffunction-sections' .text.NAME
        m = re.match(r"^ \.text\.(\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\S+\.o)\s*$", ln)
        if m:
            where[m.group(1)] = Path(m.group(2)).name
            pending = None
            continue
        m = re.match(r"^ \.text\.(\S+)\s*$", ln)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\S+\.o)\s*$", ln)
        if m and pending:
            where[pending] = Path(m.group(1)).name
        pending = None
    callers, cur = {}, None
    for ln in dis.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:\s*$", ln)
        if m:
            cur = m.group(1)
            continue
        if cur and not cur.startswith("__") and re.search(r"call .*<__(?:u?divdi3|u?moddi3|udivmoddi4)", ln):
            callers[cur] = where.get(cur, "?")
    return callers


def report(objs, data_size):
    """the RAM by object, sorted: what each part costs of the data region its variables and the pool share"""
    rows = sorted(objs.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))
    tot = [sum(v[i] for v in objs.values()) for i in range(3)]
    lines = [f"  RAM by object (bytes)          variables      pool     image",
             f"  {'all':<30} {tot[0]:>9} {tot[1]:>9} {tot[2]:>9}   (RAM {tot[0] + tot[1]} of {data_size}; the spare "
             f"zone takes the rest: its own {SPARE_OWN} and at least {SPARE} more; image of {APP_SLOT})"]
    for name, (r, p, t) in rows:
        if r + p >= 256:
            lines.append(f"  {name.removesuffix('.o'):<30} {r:>9} {p:>9} {t:>9}")
    small = [v for v in objs.values() if v[0] + v[1] < 256]
    if small:
        lines.append(f"  {f'{len(small)} more, under 256 B each':<30} {sum(v[0] for v in small):>9} "
                     f"{sum(v[1] for v in small):>9} {sum(v[2] for v in small):>9}")
    return "\n".join(lines)


def sizes(syms):
    """the biggest symbols, by section: what to look at when something grows"""
    out = {}
    for ln in syms.splitlines():
        p = ln.split()
        if len(p) >= 5 and p[-3] in (".text", ".data", ".bss", ".pool", ".ram_text", ".c2_text"):
            try:
                out.setdefault(p[-3], []).append((int(p[-2], 16), p[-1]))
            except ValueError:
                pass
    return {k: sorted(v, reverse=True) for k, v in out.items()}


def check(img, syms, dis, rt, ota):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if v[0] and val == 0xFFC00000:
                continue                # -2^22, the sound's clamp of a 23-bit value, not an address
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot ({APP_SLOT} B)")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else None
    s = {n: sym(n) for n in ("_data_lo", "_data_top", "_pool_start", "_pool_fx_end", "_pool_end", "_data_start",
                             "_data_end", "_bss_start", "_bss_end", "_spare_lo", "_spare_end", "_guard0", "_ustack_lo",
                             "_ustack_top", "_guard1", "_sstack_lo", "_sstack_top", "_records_lo", "_records_end",
                             "_records_top", "_c2_lo", "_c2_top", "_c2_ustack_lo", "_c2_ustack_top",
                             "_c2_sstack_lo", "_c2_sstack_top", "_c2_start", "_c2_text_end", "_c2_ro_end",
                             "_c2_top_start", "_c2_end", "_c2_queue", "_c2_keep", "_c2_used", "_etext")}
    missing = [n for n, v in s.items() if v is None]
    if missing:
        errors.append(f"RAM: device/app.ld names no {', '.join(missing)}")
        return errors, notes
    lo, top = s["_data_lo"], s["_data_top"]
    size = top - lo
    # The spare zone is the region's last object (specs/files/ram.md#the-edit-memory-and-the-spare-zone): its own
    # SPARE_OWN bytes, then everything up to the region's top is the zone's too; what the code leaves past its own
    # part must be at least the spare, so the zone never has less than 24 KB and the spare
    end = s["_spare_end"]
    used = end - lo
    var = (s["_data_end"] - s["_data_start"]) + (s["_bss_end"] - s["_bss_start"])
    pool = s["_pool_end"] - s["_pool_start"]
    own = s["_spare_end"] - s["_spare_lo"]
    zone = top - s["_spare_lo"]
    fw_end = 0x4000 + 0x120 + len(img)
    grow = 0x92000 - ((fw_end + 0xFFF) & ~0xFFF)
    notes.append(f"image {len(img)} B of {APP_SLOT}; FWS's free part, the room to grow, {grow // 1024} KB "
                 f"(from {(fw_end + 0xFFF) & ~0xFFF:#x})")
    notes.append(f"RAM: the data region {lo:#010x}..{top:#010x} ({size} B): the pool {s['_pool_start']:#010x} "
                 f"({pool}, the effects' {s['_pool_fx_end'] - s['_pool_start']} at its bottom), the variables "
                 f"{s['_data_start']:#010x} ({var}), the spare zone {s['_spare_lo']:#010x}: {zone} B ({own} its own, "
                 f"{size - used} past it: {size - used - SPARE} B over the spare)")
    if own != SPARE_OWN:
        errors.append(f"RAM: the spare zone's own part is {own} B, not {SPARE_OWN} (spare.c's array in .spare)")
    if size - used < SPARE:                       # the zone's room past its own part: >= 8 KiB (the debug build: 7)
        errors.append(f"RAM: the spare zone {zone} B, {size - used} B past its own {own} ({SPARE} B wanted)")
    if s["_pool_start"] != lo or not (s["_pool_end"] <= s["_data_start"] <= s["_data_end"] <= s["_bss_start"]
                                      <= s["_bss_end"] <= s["_spare_lo"]):
        errors.append("RAM: the data region is not the pool first, then the variables, then the spare zone "
                      "(device/app.ld)")
    errors += room_check(s, sym, dis, notes)
    notes.append(f"stacks: the main loop's {s['_ustack_lo']:#010x}..{s['_ustack_top']:#010x} "
                 f"({s['_ustack_top'] - s['_ustack_lo']} B), the interrupts' {s['_sstack_lo']:#010x}.."
                 f"{s['_sstack_top']:#010x} ({s['_sstack_top'] - s['_sstack_lo']} B), guard bands at "
                 f"{s['_guard0']:#010x} and {s['_guard1']:#010x}; the records kept across restarts "
                 f"{s['_records_lo']:#010x} ({s['_records_end'] - s['_records_lo']} of "
                 f"{s['_records_top'] - s['_records_lo']} B)")
    dma = []
    for name in DMA_BUFFERS:                      # every DMA buffer where a restart and an update leave RAM alone
        a = sym(name)
        if a is None:
            continue
        if not DMA_FLOOR <= a < top:
            errors.append(f"RAM: the DMA buffer {name} at {a:#010x}, below {DMA_FLOOR:#010x} or outside the data "
                          f"region (the variables come last: device/app.ld)")
        dma.append(a)
    if dma:
        notes.append(f"DMA buffers ({len(dma)} of {len(DMA_BUFFERS)}) from {min(dma):#010x}, all >= {DMA_FLOOR:#010x}")
    h = hashlib.sha256(ota).hexdigest()
    if h != LOADER_SHA256:
        errors.append(f"update loader {h[:16]}… is not the one the installers know ({LOADER_SHA256[:16]}…)")
    else:
        notes.append(f"update loader: {len(ota)} B, byte for byte the one the installers know")
    # the interrupts' stack's worst case, core 2's jobs' stack's and its exceptions' apart
    lines, st_err, st_warn, worst, c2_worst, c2_exc = stackdepth.analyse(dis, (s["_c2_start"], s["_c2_end"]))
    print("\n".join(lines))
    errors += st_err
    notes += st_warn
    # (core 2's jobs' stack less its lowest 256 B, which its own stack limit leaves out: hal/fm1_cpu1.h's fm1_c1_loop)
    jobs_room = s["_c2_ustack_top"] - s["_c2_ustack_lo"] - 256
    for what, w, room in (("the interrupts' stack", worst, s["_sstack_top"] - s["_sstack_lo"]),
                          ("core 2's jobs' stack", c2_worst, jobs_room),
                          ("core 2's exceptions' stack", c2_exc, s["_c2_sstack_top"] - s["_c2_sstack_lo"])):
        if w > room:
            errors.append(f"stack: {what}: the worst case {w} B is more than its {room} B")
        elif not st_err:
            notes.append(f"{what}: worst case {w} B of {room} ({room - w} B to spare)")
    return errors, notes


def room_check(s, sym, dis, notes):
    """core 2's room (../specs/second-core.md#ram): placed by what the chip does (also device/app.ld's ASSERTs), its
    code closed (room_closure); and over the whole image no instruction pair writing one register twice. Its map
    goes into notes. The errors"""
    errors = []
    lo, top, start, end = s["_c2_lo"], s["_c2_top"], s["_c2_start"], s["_c2_end"]
    jobs, exc = (s["_c2_ustack_lo"], s["_c2_ustack_top"]), (s["_c2_sstack_lo"], s["_c2_sstack_top"])
    if not s["_data_top"] <= lo < top <= s["_guard0"]:
        errors.append("core 2's room: not between the data region and the main loop's stack's guard band")
    if jobs[0] != lo or not jobs[0] <= ROM_STACK[0] < ROM_STACK[1] <= jobs[1] - 1024:
        errors.append(f"core 2's room: its jobs' stack {jobs[0]:#010x}..{jobs[1]:#010x} is not at its bottom over "
                      f"the ROM's start stack {ROM_STACK[0]:#010x}..{ROM_STACK[1]:#010x}, below its top 1 KB")
    if exc[0] != jobs[1] or start < exc[1]:
        errors.append("core 2's room: its exceptions' stack is not between its jobs' stack and its code")
    for what, name in (("its loop", "fm1_c1_loop"), ("its park", "fm1_c1_park"), ("its queue", "fm1_c1_q"),
                       ("its fault record", "fm1_c1_fault")):
        a = sym(name)
        if a is None or not TOP_SAFE <= a < top:
            errors.append(f"core 2's room: {what} ({name}) at {a or 0:#010x}, not in the room at or above "
                          f"{TOP_SAFE:#010x} (below it a restart or the update loader writes)")
    entry = sym("fm1_c1_entry")
    if entry is None or not APP_XIP <= entry < s["_etext"]:
        errors.append("core 2's room: its entry (fm1_c1_entry) is not in the image's code")
    if s["_c2_used"] > top:
        errors.append(f"core 2's room: {s['_c2_used'] - lo} B used of {top - lo}")
    sect, n, open_ = dis_sections(dis), 0, []
    for name in (".c2_text", ".c2_top"):
        e, k = room_closure(sect.get(name, []), start, end)
        open_ += e
        n += k
    errors += open_[:8] + ([f"core 2's room: {len(open_) - 8} more as these"] if len(open_) > 8 else [])
    if not open_:
        notes.append(f"core 2's room's code: {n} instructions, every direct call and jump inside the room, no flash "
                     f"address named")
    pairs = pair_clashes(dis)
    if pairs:
        errors.append(f"{len(pairs)} instruction pairs write one register twice (reshape the source so the dead "
                      f"write goes): {pairs[:3]}")
    else:
        notes.append("no instruction pair in the image writes one register twice")
    notes.append(f"core 2's room {lo:#010x}..{top:#010x} ({top - lo} B): the jobs' stack {jobs[0]:#010x}..{jobs[1]:#010x} "
                 f"({jobs[1] - jobs[0]}, the ROM's start stack {ROM_STACK[0]:#010x}..{ROM_STACK[1]:#010x} inside), "
                 f"the exceptions' {exc[0]:#010x}..{exc[1]:#010x} ({exc[1] - exc[0]})")
    notes.append(f"core 2's room: its code {start:#010x}..{s['_c2_text_end']:#010x} ({s['_c2_text_end'] - start}), "
                 f"tables ..{s['_c2_ro_end']:#010x} ({s['_c2_ro_end'] - s['_c2_text_end']}), from {TOP_SAFE:#010x} "
                 f"up: loop and park {s['_c2_top_start']:#010x}..{end:#010x} ({end - s['_c2_top_start']}, to 256 B), "
                 f"queue {s['_c2_queue']:#010x} ({s['_c2_keep'] - s['_c2_queue']}), fault record {s['_c2_keep']:#010x} "
                 f"({s['_c2_used'] - s['_c2_keep']}); {top - s['_c2_used']} B left")
    notes.append(f"core 2's room: copied from the image at power-on {start:#010x}..{end:#010x} ({end - start} B), "
                 f"core 1's write limit over it; the PC limits' RAM window {sym('_rt_start') or 0:#010x}..{end:#010x}")
    return errors


# Every hardware register access lives in hal/. In device/, loader/ and core/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF): write such a number as a shift (1u << 16);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# And fw/core includes no hal/ or device/ header: it sees the hardware through plat.h only.
# No weak function outside core/plat_weak.c: the device's compiler keeps values across a call in the registers
# a weak body in the same unit leaves alone, and the function linked in its place overwrites them (plat_weak.c).
WEAK = re.compile(r"__attribute__\s*\(\(\s*(?:__)?weak|#\s*pragma\s+weak")
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+(?:volatile|__volatile__)\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")
CORE_INC = re.compile(r'#\s*include\s*[<"](?:[^>"]*/)?(fm1_\w+\.h|bootguard\.h|usb\.c|lcd\.c|gfx\.c|ota\.c)[>"]')


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((SRC / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    files = [*(SRC / "device").glob("*.[ch]"), *(SRC / "loader").glob("*.c"), *SRC.glob("core/**/*.[ch]")]
    for f in sorted(files):
        text = f.read_text(encoding="utf-8", errors="replace")
        core = "core" in f.relative_to(SRC).parts[:1]
        for no, ln in enumerate(strip(text).splitlines(), 1):
            where = f"{f.relative_to(SRC)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
            if core and CORE_INC.search(ln):
                errors.append(f"{where}: fw/core includes a device header; use plat.h")
            if WEAK.search(ln) and f.relative_to(SRC).as_posix() != "core/plat_weak.c":
                errors.append(f"{where}: a weak function outside core/plat_weak.c; put the default there")
    return errors


def version(release):
    number = VERSION.read_text().strip() if VERSION.exists() else ""
    if not number:
        raise SystemExit("build: no version number in fw/VERSION")
    if release:
        return number
    if DEBUG:                                       # its own time, so each debug build is told apart
        return number + " " + time.strftime("%m-%d %H:%M") + " DEBUG"
    return number + " DEV"


def use_debug():
    """--debug: its own folders (its objects never mix with the dev build's), identity, RAM rule"""
    global OUT, OBJ, GEN, GEN_DEV, RW, LDR, PRODUCT, SPARE, DEBUG
    OUT = ROOT / "build" / "fw-debug"
    OBJ, GEN, GEN_DEV, RW, LDR = OUT / "obj", OUT / "gen", OUT / "gen_dev", OUT / "rw", OUT / "loader"
    PRODUCT, SPARE, DEBUG = "FM-1_7102", 7168, True


def main():
    release, bringup, debug = ("--release" in sys.argv[1:], "--bringup" in sys.argv[1:], "--debug" in sys.argv[1:])
    if release and bringup:
        raise SystemExit("build: the bring-up app is never released")
    if debug and (release or bringup):
        raise SystemExit("build: --debug is a build of its own")
    if debug:
        use_debug()
    for rel_, sha in SDK_SHA256.items():          # refuse SDK files that are not the tested ones
        if hashlib.sha256(fm1pkg_make.sdk_file(rel_)).hexdigest() != sha:
            raise SystemExit(f"build: SDK {rel_} differs from AC79NN_SDK_V1.2.1 (SHA-256 mismatch)")
    OUT.mkdir(parents=True, exist_ok=True)
    ver = version(release)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate, bringup), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    img, syms, dis, rt = build_app(ver, bringup)
    map_text = (OUT / "ptfm.map").read_text(errors="replace")
    lo, top = (re.search(r"^([0-9a-f]+) .*\s" + n + r"$", syms, re.M) for n in ("_data_lo", "_data_top"))
    print(report(by_object(map_text), int(top.group(1), 16) - int(lo.group(1), 16) if lo and top else 0))
    errors, notes = check(img, syms, dis, rt, ota)
    divs = wide_divides(dis, map_text)
    hot = sorted(f"{f} ({o})" for f, o in divs.items() if o.startswith(("core_eng_", "core_play_", "core_fx_")))
    if hot:
        errors.append("64-bit divides in the sound's code (slow, a bit at a time): " + ", ".join(hot))
    if divs:
        notes.append("64-bit divides called from: " + ", ".join(sorted(f"{f} ({o.removesuffix('.o')})" for f, o in divs.items())))
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (device/, loader/, core/ clean); core/ sees plat.h only")
    big = sizes(syms)
    for sect in (".text", ".c2_text", ".bss", ".pool"):
        top = ", ".join(f"{n} {s}" for s, n in big.get(sect, [])[:6])
        if top:
            notes.append(f"largest in {sect}: {top}")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    dest = DIST if release else DIST_DEBUG if debug else OUT
    dest.mkdir(parents=True, exist_ok=True)
    name = "ptfm-bringup" if bringup else "ptfm-debug" if debug else "ptfm"
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (dest / f"{name}.fwsc").write_bytes(pkg)
    demo = DIST / "demo.ptfmdsk"
    page = make_installer.make(pkg, PRODUCT, dest / (f"{name}-install.html" if bringup else "install.html"),
                               version=ver, disk=demo if demo.exists() else None, debug=debug)
    print(f"version  {ver}  (built {time.strftime('%Y-%m-%d %H:%M')})")
    print(f"app      {OUT / 'ptfm.bin'}  {len(img)} B")
    print(f"package  {dest / (name + '.fwsc')}  {len(pkg)} B, identity {PRODUCT}, "
          f"sha256 {hashlib.sha256(pkg).hexdigest()[:16]}…")
    print(f"page     {page}" + ("" if demo.exists() else "  (no dist/firmware/demo.ptfmdsk yet: UPLOAD DEMO DISK is off)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
