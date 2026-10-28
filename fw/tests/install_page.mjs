// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
// FakeFM1's update side follows SLOOP's web/test_web.mjs simulator (Copyright (C) 2026 Leo Kuroshita,
// Hügelton Instruments).
//
// Node check of the install page (no browser, no hardware): the page's own scripts run in a sandbox with a
// fake DOM and a simulated FM-1 on Web MIDI.
//   node fw/tests/install_page.mjs PACKAGE.fwsc        (fw/tests/run_device.sh, after a build)
// A test page is made from the package and a small disk of the test's own (and the built page with
// dist/firmware/demo.ptfmdsk is checked too when both are there):
// - the embedded package and disk check out; both buttons are offered; the disk's notes are shown
// - INSTALL FIRMWARE: the controller running -> the update loader -> FM-1_7101, every byte served == the
//   package's logical image
// - UPLOAD DEMO DISK on PT/FM, one press: the device's layout, the plan, written at once: the simulated flash
//   then holds every file of the disk as a record (specs/files/store.md: header, CRC, one save, its last
//   flagged), in the user area only; pressed again, the device already holds it; a SAVE on the FM-1 right after
//   its layout: BEGIN refused (CHANGED), planned again, written
// - UPLOAD CUSTOM DISK: a disk file picked, written as UPLOAD DEMO DISK's (the FM-1 then holds its files); again, nothing
//   to write; a file that is no disk, and a disk holding nothing, refused before MIDI
// - CLEAN INSTALL, a switch (off at first; on at first on a --clean page, the debug page's): INSTALL FIRMWARE
//   installs, then erases the user area (a damaged record too): no record left; off, the firmware alone, the user
//   area untouched
// - CLEAR USER DATA (the debug page's: seed.js session.clear): the user area erased, a damaged record too, FWS
//   untouched; refused with edits not saved; planned again after a SAVE on the FM-1
// - UPLOAD with edits not saved: the page says SAVE OR REVERT; a device that is not PT/FM: it says so
// - a damaged embedded package is refused
// - end to end, when fw/tests/run_store.sh has built build/fw/store/seed_bridge: the built page uploads
//   dist/firmware/demo.ptfmdsk into the device's own seeding code (fw/core/store on a fake flash), every record as the disk;
//   CLEAR USER DATA then leaves no record
import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { createRequire } from "node:module";
import { webcrypto } from "node:crypto";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const ROOT = new URL("../..", import.meta.url).pathname;           // ptfm/
const OUT = `${ROOT}build/fw/host`;
const PKG = process.argv[2] || `${ROOT}build/fw/ptfm.fwsc`;
const SEED = require(`${ROOT}fw/web/seed.js`);
let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(78)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const u16 = (b, o) => b[o] | b[o + 1] << 8;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const le16 = (v) => [v & 255, (v >>> 8) & 255];
const le32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, v >>> 24 & 255];

function pack7(data) {                    /* M-UPGRADE's bit packing (fm1ota.js) */
  const out = []; let acc = 0, nb = 0;
  for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; } }
  if (nb) out.push(acc & 0x7F);
  return out;
}
function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) { acc |= (b & 0x7F) << nb; nb += 7; while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; } }
  return out;
}
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const USER = [[0x093000, 0x0E0000], [0x0E5000, 0x0FF000]];
const FW_END = 0x60000, ROM_END = 0x092000;

/* a small disk of the test's own (specs/files/disk.md): a song, two pattern parts, a sampler, its audio, notes */
function makeDisk() {
  const files = [
    [0x21, 0x00, 0, 1, Array.from({ length: 150 }, (_, i) => i & 255)],
    [0x20, 0x00, 0, 1, Array.from({ length: 700 }, (_, i) => (i * 7) & 255)],
    [0x20, 0x00, 1, 1, Array.from({ length: 300 }, (_, i) => (i * 3) & 255)],
    [0x04, 0x01, 0, 1, Array.from({ length: 600 }, (_, i) => (i * 5) & 255)],
    [0x10, 0x01, 0, 1, Array.from({ length: 9000 }, (_, i) => (i * 11) & 255)],
    [0xE0, 0x00, 0, 1, [...new TextEncoder().encode("TEST DISK: made by fw/tests/install_page.mjs.")]],
  ];
  const out = [...new TextEncoder().encode("PTFMDISK"), 1, 0, 0, 0, ...le32(files.length)];
  for (const [kind, num, sub, ver, p] of files) {
    out.push(kind, num, sub, ver, ...le32(p.length), ...le32(SEED.crc32(Uint8Array.from(p))), ...p);
    while (out.length % 4) out.push(0);
  }
  return Uint8Array.from(out);
}

/* the FM-1 on Web MIDI: the update path (identity, reads of the image), and PT/FM's seeding */
class FakeFM1 {
  constructor(image, { running = "FM-1_924", unsaved = false, loaderIn = null, loaderOut = null, gs = false } = {}) {
    /* (at first another FM-1 firmware; loaderIn / loaderOut: the update loader's port names as Windows may give
     * them; gs: Windows' own "Microsoft GS Wavetable Synth" output beside the FM-1's) */
    this.image = image; this.served = 0; this.bad = 0; this.unsaved = unsaved;
    this.loaderIn = loaderIn; this.loaderOut = loaderOut; this.gs = gs;
    this.flash = new Uint8Array(1 << 20).fill(0xFF);
    this.lastSave = 0; this.mounts = 0; this.outside = 0;
    this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
    this.boot(running);
  }
  boot(identity) {
    const ota = identity.startsWith("ota-");
    const name = identity === "FM-1_7101" ? "FM-1 PT/FM" : ota ? this.loaderIn || "Felucca Update" : "FM-1";   /* (the update loader's USB name) */
    this.identity = identity; this.waiting = null; this.queue = []; this.blk = null;
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2), listeners = [];
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {},
      addEventListener: (t, f) => { if (t === "midimessage") listeners.push(f); }, listeners };
    this.output = { id: "o" + id, name: ota && this.loaderOut || name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
    if (this.gs) this.access.outputs.set("gs", { id: "gs", name: "Microsoft GS Wavetable Synth", state: "connected",
                                                 open: async () => {}, send: () => {} });
  }
  tx(bytes) {
    const i = this.input;
    setTimeout(() => {
      if (i.state !== "connected") return;
      const e = { data: Uint8Array.from(bytes) };
      if (i.onmidimessage) i.onmidimessage(e);
      for (const f of i.listeners) f(e);
    }, 1);
  }
  rx(d) {
    if (d.length > 5 && d[1] === 0x7D && d[2] === 0x50 && d[3] === 0x54) {
      if (this.identity === "FM-1_7101") this.seed(d[4], SEED.unpack7(d.slice(5, -1)));
    } else if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      const last = this.image.length - 512;            /* also the end of the image */
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [last, 512], [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900"), 300);
      else if (addr === 0xF0000000) setTimeout(() => {    /* installed: the slot rewritten, ROM's free part */
        for (let a = FW_END; a < ROM_END; a++) this.flash[a] = (a * 2654435761) >>> 24;   /* holds padding, not 0xFF */
        this.lastSave = this.records().reduce((m, r) => Math.max(m, r.save), this.lastSave);
        this.boot("FM-1_7101");
      }, 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
  /* ---- seeding, the device's side as specs/files/blob.md and seed.js's header say ---- */
  send(cmd, payload) { this.tx(Array.from(SEED.message(cmd, payload))); }
  ack(cmd, block, result, extra = []) { this.send(SEED.CMD.ACK, [cmd, ...le16(block), result, ...extra]); }
  records() {                             /* the store's walk (store.c), simplified: every record of the user area */
    const out = [];
    for (const [lo, hi] of USER) {
      let o = lo;                                        /* audio runs on over the next blocks; else a block's */
      while (o + 16 <= hi) {                             /* records end at the first 0xFF kind */
        if (this.flash[o] === 0xFF) { o = (o & ~0xFFF) + 0x1000; continue; }
        const h = this.flash.subarray(o, o + 16), len = h[4] | h[5] << 8 | h[6] << 16;
        const del = !!(h[7] & 2), plen = del ? 0 : len;
        const plausible = h[0] && h[0] < 0xE0 && !(h[7] & ~3) && !(del && len) && o + 16 + plen <= hi &&
          (h[0] === 0x10 || (o & 0xFFF) + 16 + plen <= 4096);
        const body = plausible ? this.flash.subarray(o + 16, o + 16 + plen) : null;
        if (!plausible || SEED.crc32(body, SEED.crc32(h.subarray(0, 12))) !== u32(h, 12)) { o += 4; continue; }
        out.push({ kind: h[0], num: h[1], sub: h[2], ver: h[3], len, flags: h[7], save: u32(h, 8), addr: o,
          ok: true, crc: SEED.crc32(body), payload: Uint8Array.from(body) });
        o += 16 + ((plen + 3) & ~3);
      }
    }
    return out;
  }
  put(addr, kind, num, sub, ver, payload, save, last, del) {   /* a record written straight into the flash (del: a
                                                                   deleted record, no payload) */
    if (del) payload = new Uint8Array(0);
    const h = Uint8Array.from([kind, num, sub, ver, payload.length & 255, payload.length >> 8 & 255, payload.length >> 16,
      (last ? 1 : 0) | (del ? 2 : 0), ...le32(save), 0, 0, 0, 0]);
    h.set(le32(SEED.crc32(payload, SEED.crc32(h.subarray(0, 12)))), 12);
    this.flash.set(h, addr);
    this.flash.set(payload, addr + 16);
    return addr + 16 + ((payload.length + 3) & ~3);
  }
  layout() {
    const recs = this.records().filter((r) => r.ok);
    const b = [..."PTLY"].map((c) => c.charCodeAt(0));
    const regions = [[0x093000, 0x0E0000, 0], [0x0E5000, 0x0FF000, 0]];
    const accepts = [[1, 1], [2, 1], [3, 1], [4, 1], [5, 1], [0x10, 1], [0x20, 1], [0x21, 1], [0x22, 3], [0x23, 1], [0x30, 1], [0x31, 1]];
    b.push(1, 1, 0, 0, ...le32(1), ...le32(FW_END), ...le16(512), ...le16(16), regions.length, accepts.length,
      ...le16(0), ...le32(this.lastSave), ...le32(recs.length), this.unsaved ? 1 : 0, 0, 0, 0);
    for (const [s, e, a] of regions) b.push(...le32(s), ...le32(e), a, a ? 0 : 1, 0, 0);
    for (const [k, v] of accepts) b.push(k, v);
    for (const r of recs) b.push(r.kind, r.num, r.sub, r.ver, ...le32(r.crc), ...le32(r.addr), ...le32(r.len), ...le32(r.save), r.flags & 2, 0, 0, 0);
    return b;
  }
  seed(cmd, d) {
    const C = SEED.CMD, R = SEED.RESULT;
    if (cmd === C.HELLO) {
      if (this.unsaved) return this.ack(C.HELLO, 0, R.UNSAVED);
      const lay = this.layout();
      for (let o = 0; o < lay.length; o += 400) this.send(C.LAYOUT, [...le32(lay.length), ...le32(o), ...lay.slice(o, o + 400)]);
      if (this.onHello) { const f = this.onHello; this.onHello = null; f(); }   /* (a SAVE on the FM-1 right after) */
      return;
    }
    if (cmd === C.READ) {                               /* the backup: flash bytes back, any area */
      const a = u32(d, 0), n = u16(d, 4);
      this.reads = (this.reads || 0) + 1;
      return this.send(C.DATA, [...le32(a), ...this.flash.subarray(a, a + n)]);
    }
    if (cmd === C.BEGIN) {
      if (u32(d, 0) !== this.lastSave + 1) return this.ack(C.BEGIN, 0, R.CHANGED);   /* saved since the HELLO */
      this.begun = { save: u32(d, 0), blocks: u16(d, 4) }; this.blk = null; return this.ack(C.BEGIN, 0, R.OK);
    }
    if (!this.begun && cmd !== C.ABORT) return this.ack(cmd, 0, R.NOT_BEGUN);
    if ((cmd === C.CHUNK || cmd === C.BLOCK) && this.stallAfter !== undefined && this.blocksAcked >= this.stallAfter)
      return;                                           /* (silent from then on: no answer at all) */
    if (cmd === C.CHUNK) {
      const blk = u16(d, 0), off = u16(d, 2), part = d.slice(4);
      if (!this.blk || this.blk.n !== blk) this.blk = { n: blk, data: new Uint8Array(4096).fill(0xFF) };
      this.blk.data.set(part, off);
      return this.ack(C.CHUNK, blk, R.OK);
    }
    if (cmd === C.BLOCK) {
      const blk = u16(d, 0), crc = u32(d, 2), a = blk * 4096;
      const data = this.blk && this.blk.n === blk ? this.blk.data : new Uint8Array(4096).fill(0xFF);
      if (a < ROM_END) this.romBlocks = (this.romBlocks || 0) + 1;      /* (a plan never writes FWS) */
      if (!USER.some(([lo, hi]) => a >= lo && a + 4096 <= hi)) { this.outside++; return this.ack(C.BLOCK, blk, R.BAD_BLOCK); }
      if (SEED.crc32(data) !== crc) return this.ack(C.BLOCK, blk, R.BAD_CRC);
      this.flash.set(data, a);
      this.blk = null;
      this.blocksAcked = (this.blocksAcked || 0) + 1;
      return this.ack(C.BLOCK, blk, R.OK);
    }
    if (cmd === C.END || cmd === C.ABORT) {
      const recs = this.records(), live = new Map();    /* the newest of each (kind, num, sub); deleted: none */
      for (const r of recs) { const k = `${r.kind}.${r.num}.${r.sub}`; if (!live.has(k) || live.get(k).save < r.save) live.set(k, r); }
      this.mounts++;
      this.lastSave = recs.reduce((m, r) => Math.max(m, r.save), this.lastSave);
      this.begun = null;
      return this.ack(cmd, 0, R.OK, [...le16([...live.values()].filter((r) => !(r.flags & 2)).length), ...le16(0), ...le32(this.lastSave)]);
    }
  }
}

/* the page's scripts in a sandbox: a fake DOM, the given MIDI access */
function runPage(html, access) {
  const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
  const els = {};
  const node = (tag) => ({ tag, textContent: "", className: "", innerHTML: "", hidden: false, value: 0, disabled: true,
    on: {}, children: [], firstChild: null, files: [], picks: 0,
    click() { this.picks++; },                          /* (a file input's picker opened: the test sets files) */
    appendChild(c) { this.children.push(c); return c; },
    addEventListener(t, f) { this.on[t] = f; }, scrollIntoView() {} });
  const el = (id) => (els[id] ||= Object.assign(node("div"), { id }));
  const ctx = {
    document: { getElementById: el, createElement: node, createTextNode: (t) => ({ textContent: t }) },
    isSecureContext: true, addEventListener() {},
    navigator: { requestMIDIAccess: async (o) => { if (!o || !o.sysex) throw new Error("no sysex"); return access; } },
    crypto: webcrypto, atob, TextEncoder, TextDecoder, setTimeout, clearTimeout, setInterval, clearInterval,
    console: process.env.VERBOSE ? console : { log() {}, warn() {}, error() {} },
    Date, Math, Uint8Array, Map, Set, Promise, Error, JSON,
  };
  ctx.window = ctx;
  ctx.globalThis = ctx;
  vm.createContext(ctx);
  for (const s of scripts) vm.runInContext(s, ctx);
  return el;
}
async function until(cond, ms) {
  const end = Date.now() + ms;
  while (Date.now() < end && !cond()) await sleep(20);
  return cond();
}
const text = (e) => e.children.map((c) => (c.children.length ? c.children.map((x) => x.textContent).join("") : c.textContent)).join("\n");

/* ---- the test page: the package and the test's disk ---- */
mkdirSync(OUT, { recursive: true });
const disk = makeDisk();
writeFileSync(`${OUT}/test.ptfmdsk`, disk);
execFileSync("python3", ["-I", `${ROOT}fw/tools/make_installer.py`, PKG, `${OUT}/install_test.html`, `${OUT}/test.ptfmdsk`]);
const html = readFileSync(`${OUT}/install_test.html`, "utf8");
const logical = execFileSync("python3", ["-c", `import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, PKG], { maxBuffer: 1 << 26 });
const parsed = SEED.parseData(disk);

{
  const dev = new FakeFM1(Uint8Array.from(logical));
  const el = runPage(html, dev.access);
  const ready = await until(() => !el("go").disabled && !el("disk").disabled, 5000);
  ok(ready && /^Ready: connect the FM-1/.test(el("status").textContent), "page: the package and the disk check out, both buttons offered");
  ok(el("ident").textContent === "FM-1_7101", "page: shows identity FM-1_7101");
  ok(!el("notesbox").hidden && /TEST DISK/.test(el("notes").textContent), "page: the disk's notes (credits) are shown");
  ok(!el("cleanrow").hidden && !el("clean").checked && /PT\/FM onto the FM-1, your work kept/.test(el("gosub").textContent),
     "page: the CLEAN INSTALL switch offered, off at first");

  el("go").on.click();
  const done = await until(() => /^Done/.test(el("status").textContent) || /err/.test(el("status").className), 60000);
  ok(done && el("status").textContent === "Done: the FM-1 runs PT/FM (FM-1_7101).", `INSTALL FIRMWARE: controller -> loader -> "${el("status").textContent}"`);
  ok(dev.bad === 0 && dev.served >= 10, `INSTALL FIRMWARE: every byte served == the package's image (${dev.served} reads, ${dev.bad} bad)`);

  await until(() => !el("disk").disabled, 5000);
  el("disk").on.click();                                 /* one press: written at once, no plan to accept */
  const wrote = await until(() => /^Done: the FM-1 holds|stopped/.test(el("status").textContent) || /err/.test(el("status").className), 60000);
  ok(wrote && el("status").textContent === `Done: the FM-1 holds ${parsed.files.length} files.`,
     `UPLOAD: one press -> "${el("status").textContent}"`);
  const recs = dev.records();
  const same = parsed.files.every((f) => recs.some((r) => r.ok && r.kind === f.kind && r.num === f.num && r.sub === f.sub &&
    r.ver === f.ver && r.len === f.len && eq(Array.from(r.payload), Array.from(f.payload))));
  ok(same && recs.length === parsed.files.length && recs.every((r) => r.ok), `UPLOAD: the flash holds every file as a record, CRCs good (${recs.length})`);
  const saves = new Set(recs.map((r) => r.save)), last = recs.filter((r) => r.flags & 1);
  ok(saves.size === 1 && [...saves][0] === 1 && last.length === 1, "UPLOAD: one save (number 1), its last record flagged");
  ok(dev.outside === 0 && dev.mounts === 1, "UPLOAD: only the user area written; the device mounted once");

  el("disk").on.click();                                 /* again: nothing to write */
  await until(() => /already holds/.test(el("status").textContent) || /err/.test(el("status").className), 10000);
  ok(/already holds/.test(el("status").textContent), "UPLOAD again: the device already holds it, nothing written");
}
{ /* UPLOAD CUSTOM DISK: a disk file picked (set on the picker, then its change), written at once as UPLOAD DEMO DISK's: the
     FM-1 then holds that disk's files, not the page's; again, nothing to write; a file that is no disk and a disk
     holding nothing are refused before MIDI */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  const el = runPage(html, dev.access);
  await until(() => !el("upload").disabled, 5000);
  const pick = async (name, bytes) => {                 /* (the line cleared: the waits below see only this pick's) */
    Object.assign(el("status"), { textContent: "", className: "status" });
    el("upload-pick").files = [{ name, arrayBuffer: async () => Uint8Array.from(bytes).buffer }];
    await el("upload-pick").on.change();
  };
  el("upload").on.click();
  ok(el("upload-pick").picks === 1, "UPLOAD CUSTOM DISK: opens the file picker");
  await pick("NOTES.TXT", new TextEncoder().encode("not a disk"));
  ok(el("status").textContent === "NOTES.TXT cannot be uploaded: not a PT/FM disk or blob." && !dev.mounts,
     `UPLOAD CUSTOM DISK, a file that is no disk: "${el("status").textContent}"`);
  await pick("EMPTY.ptfmdsk", SEED.makeDisk([]));
  ok(el("status").textContent === "EMPTY.ptfmdsk holds nothing to upload." && !dev.mounts,
     `UPLOAD CUSTOM DISK, a disk holding nothing: "${el("status").textContent}"`);
  const mine = parsed.files.filter((f) => f.kind === 0x21 || f.kind === 0x20)     /* the song and its pattern, changed */
    .map((f) => ({ kind: f.kind, num: f.num + 2, sub: f.sub, ver: f.ver, payload: f.payload.map((b) => b ^ 0x5A) }));
  await pick("MINE.ptfmdsk", SEED.makeDisk(mine));
  await until(() => /^Done: the FM-1 holds|stopped/.test(el("status").textContent) || /err/.test(el("status").className), 60000);
  const recs = dev.records();
  const same = mine.every((f) => recs.some((r) => r.ok && r.kind === f.kind && r.num === f.num && r.sub === f.sub &&
    r.ver === f.ver && eq(Array.from(r.payload), Array.from(f.payload))));
  ok(el("status").textContent === `Done: the FM-1 holds ${mine.length} files.` && same && recs.length === mine.length &&
     dev.mounts === 1, `UPLOAD CUSTOM DISK: the picked disk written, the FM-1 holds its ${mine.length} files -> "${el("status").textContent}"`);
  await pick("MINE.ptfmdsk", SEED.makeDisk(mine));
  await until(() => /already holds/.test(el("status").textContent) || /err/.test(el("status").className), 10000);
  ok(el("status").textContent === "The FM-1 already holds MINE.ptfmdsk, exactly.", "UPLOAD CUSTOM DISK again: nothing written");
}
{ /* a SAVE on the FM-1 between its layout and BEGIN: the device refuses BEGIN (CHANGED), the page plans again */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  const el = runPage(html, dev.access);
  await until(() => !el("disk").disabled, 5000);
  let saved = false;
  dev.onHello = () => {                                  /* the FM-1 saved a song */
    dev.put(0x0E5000, 0x21, 0x07, 0, 1, Uint8Array.from([1, 2, 3, 4]), 1, true);
    dev.lastSave = 1;
    saved = true;
  };
  el("disk").on.click();
  await until(() => /^Done|stopped/.test(el("status").textContent) || /err/.test(el("status").className), 30000);
  const recs = dev.records(), live = recs.filter((r) => r.save === 2);
  ok(saved && el("status").textContent === `Done: the FM-1 holds ${parsed.files.length} files.` &&
     live.some((r) => r.kind === 0x21 && r.num === 0x07 && (r.flags & 2)),
     `UPLOAD after a SAVE on the FM-1: refused (CHANGED), planned again, written (the song saved meanwhile deleted)`);
}
{ /* the debug page's INSTALL FIRMWARE: a clean install (the user area erased, damaged records too; no demo disk;
     FWS never written by a plan) */
  execFileSync("python3", ["-I", `${ROOT}fw/tools/make_installer.py`, PKG, `${OUT}/install_clean.html`, `${OUT}/test.ptfmdsk`, "--clean"]);
  const chtml = readFileSync(`${OUT}/install_clean.html`, "utf8");
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  const bytes = (n, k) => Uint8Array.from({ length: n }, (_, i) => (i * k + 7) & 255);
  const a = dev.put(0x093000, 0x21, 0x03, 0, 1, bytes(150, 3), 1, false);   /* the user's song and pattern */
  dev.put(a, 0x20, 0x09, 0, 1, bytes(700, 5), 1, true);
  dev.flash.set(Uint8Array.from([0x10, 0x02, 0x00, 1, 40, 0, 0, 0, 1, 0, 0, 0, 9, 9, 9, 9, ...bytes(40, 9)]), 0x0E6000);   /* a damaged record */
  dev.lastSave = 1;
  const el = runPage(chtml, dev.access);
  await until(() => !el("go").disabled, 5000);
  ok(el("clean").checked && /clean: PT\/FM, then an empty user area/.test(el("gosub").textContent), "clean page: CLEAN INSTALL on at first, INSTALL FIRMWARE says so");
  el("clean").checked = false;
  el("clean").on.change();
  const offLabel = /PT\/FM onto the FM-1, your work kept/.test(el("gosub").textContent);
  el("clean").checked = true;
  el("clean").on.change();
  ok(offLabel && /clean: PT\/FM, then an empty user area/.test(el("gosub").textContent), "clean page: the switch turned off and on, INSTALL FIRMWARE follows");
  el("go").on.click();
  await until(() => /^Done: a clean FM-1/.test(el("status").textContent) || /err/.test(el("status").className), 120000);
  const fwsUntouched = !dev.romBlocks && !dev.outside;
  const userClean = USER.every(([lo, hi]) => dev.flash.subarray(lo, hi).every((x) => x === 0xFF));
  ok(/^Done: a clean FM-1, PT\/FM \(FM-1_7101\), its user area empty\.$/.test(el("status").textContent) && !dev.reads &&
     dev.records().length === 0 && fwsUntouched && userClean,
     `CLEAN INSTALL: no backup, installed, the whole user area erased, no demo disk ("${el("status").textContent}")`);
}
{ /* edits not saved: the page asks for SAVE or REVERT */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101", unsaved: true });
  const el = runPage(html, dev.access);
  await until(() => !el("disk").disabled, 5000);
  el("disk").on.click();
  await until(() => /err/.test(el("status").className), 10000);
  ok(/SAVE them \(or REVERT\)/.test(el("status").textContent), "UPLOAD with edits not saved: SAVE OR REVERT first");
}
{ /* the debug page's CLEAR USER DATA (seed.js session.clear): the user area erased, a damaged record too, FWS never
     written; edits not saved: refused before anything is written; a SAVE right after the layout: planned again */
  const bytes = (n, k) => Uint8Array.from({ length: n }, (_, i) => (i * k + 7) & 255);
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  const a = dev.put(0x093000, 0x21, 0x03, 0, 1, bytes(150, 3), 1, false);
  dev.put(a, 0x30, 0x00, 0, 1, bytes(133, 5), 1, true);                 /* a song and the settings */
  dev.flash.set(Uint8Array.from([0x10, 0x02, 0x00, 1, 40, 0, 0, 0, 1, 0, 0, 0, 9, 9, 9, 9, ...bytes(40, 9)]), 0x0E6000);   /* a damaged record */
  dev.lastSave = 1;
  const s = SEED.session(SEED.midiTransport(dev.input, dev.output));
  let steps = 0;
  const r = await s.clear((p) => { steps = p.total; });
  const userClean = USER.every(([lo, hi]) => dev.flash.subarray(lo, hi).every((x) => x === 0xFF));
  const blocks = USER.reduce((n, [lo, hi]) => n + (hi - lo) / 4096, 0);
  ok(r.records === 0 && userClean && dev.records().length === 0 && !dev.romBlocks && !dev.outside && steps === blocks && dev.mounts === 1,
     `CLEAR USER DATA: every block of the user area erased (${steps} of ${blocks}), no record left, FWS untouched`);
  s.close();
  const du = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101", unsaved: true });
  const su = SEED.session(SEED.midiTransport(du.input, du.output));
  const e = await su.clear().then(() => null, (x) => x);
  ok(e && e.code === SEED.RESULT.UNSAVED && e.before && !du.mounts, "CLEAR USER DATA with edits not saved: refused before anything is written");
  su.close();
  const dc = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  dc.put(0x093000, 0x21, 0x03, 0, 1, bytes(150, 3), 1, true);
  dc.lastSave = 1;
  dc.onHello = () => { dc.put(0x0E5000, 0x21, 0x07, 0, 1, Uint8Array.from([1, 2, 3, 4]), 2, true); dc.lastSave = 2; };
  const sc = SEED.session(SEED.midiTransport(dc.input, dc.output));
  const rc = await sc.clear();
  ok(rc.records === 0 && dc.records().length === 0 && dc.mounts === 1,
     "CLEAR USER DATA after a SAVE on the FM-1: refused (CHANGED), planned again, everything erased");
  sc.close();
}
{ /* not PT/FM: no answer */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_924" });
  const el = runPage(html, dev.access);
  await until(() => !el("disk").disabled, 5000);
  el("disk").on.click();
  await until(() => /err/.test(el("status").className), 15000);
  ok(/did not answer as PT\/FM/.test(el("status").textContent), "UPLOAD on another firmware: it says PT/FM is not running");
}
{ /* the FM-1 silent part way through an upload (no answer at all): the page says to switch it off and on */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  dev.stallAfter = 1;
  const el = runPage(html, dev.access);
  await until(() => !el("disk").disabled, 5000);
  el("disk").on.click();
  await until(() => /err/.test(el("status").className), 15000);
  ok(/^The upload stopped: the FM-1 did not answer\. Switch the FM-1 off and on, then press UPLOAD DEMO DISK again\./
     .test(el("status").textContent), `UPLOAD, the FM-1 silent after a block: "${el("status").textContent}"`);
}
ok(!/FWS/.test(html.replace(/<script>[\s\S]*?<\/script>|<style>[\s\S]*?<\/style>/g, "")),
   "page: no PUT BACK INTO FWS, no word of FWS's free part (nothing of the user's goes there)");
{ /* INSTALL FIRMWARE over PT/FM (CLEAN INSTALL off): the firmware alone, nothing read or written in the user area
     (blob.md, Seeding) */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
  const bytes = (n, k) => Uint8Array.from({ length: n }, (_, i) => (i * k + 7) & 255);
  const a = dev.put(0x093000, 0x21, 0x00, 0, 1, bytes(150, 3), 1, false);
  dev.put(a, 0x20, 0x00, 0, 1, bytes(700, 5), 1, true);                 /* the user area: a song, a pattern */
  dev.lastSave = 1;
  const el = runPage(html, dev.access);
  await until(() => !el("go").disabled, 5000);
  const userBefore = Array.from(dev.flash.subarray(0x093000, 0x0FF000));
  el("go").on.click();
  await until(() => /^Done/.test(el("status").textContent) || /err/.test(el("status").className), 90000);
  ok(/^Done: the FM-1 runs PT\/FM/.test(el("status").textContent) && dev.served >= 10 && !dev.reads &&
     eq(userBefore, Array.from(dev.flash.subarray(0x093000, 0x0FF000))) && !dev.romBlocks && !dev.outside,
     "INSTALL: the firmware alone, no backup, nothing written to the user area");
}
{ /* the update loader under a name Windows gives (no FM-1 word in it), its output named otherwise, and Windows' GS
     synth beside: found by its answer to the handshake (fm1ota.js find's deep pass, probe's pairing) */
  const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101", loaderIn: "USB MIDI Device",
                                                       loaderOut: "MIDIOUT2 (USB MIDI Device)", gs: true });
  const el = runPage(html, dev.access);
  await until(() => !el("go").disabled, 5000);
  el("go").on.click();
  await until(() => /^Done/.test(el("status").textContent) || /err/.test(el("status").className), 90000);
  ok(/^Done: the FM-1 runs PT\/FM/.test(el("status").textContent) && dev.served >= 10 && dev.bad === 0,
     `INSTALL: the loader as "USB MIDI Device" / "MIDIOUT2 (USB MIDI Device)", a GS synth beside: found ("${el("status").textContent}")`);
}
{ /* a damaged embedded package */
  const at = html.indexOf('const PKG = "') + 13 + 300000;
  const flip = html[at] === "A" ? "B" : "A";
  const el = runPage(html.slice(0, at) + flip + html.slice(at + 1), new FakeFM1(Uint8Array.from(logical)).access);
  await until(() => el("status").textContent !== "Checking the package…" && el("status").textContent !== "", 5000);
  ok(el("go").disabled && /damaged/.test(el("status").textContent), "page: a damaged embedded package is refused");
}
{ /* the built pages carry the real demo disk */
  for (const page of [`${ROOT}build/fw/install.html`, `${ROOT}dist/firmware/install.html`]) {
    if (!existsSync(page) || !existsSync(`${ROOT}dist/firmware/demo.ptfmdsk`)) continue;
    const h = readFileSync(page, "utf8");
    if (!/"disk":\s*\{/.test(h)) { ok(false, `${page.slice(ROOT.length)}: carries the demo disk`); continue; }
    const el = runPage(h, new FakeFM1(Uint8Array.from(logical)).access);
    await until(() => !el("disk").disabled || /err/.test(el("status").className), 8000);
    ok(!el("disk").disabled && !el("notesbox").hidden, `${page.slice(ROOT.length)}: its demo disk checks out, notes shown`);
  }
}
{ /* end to end: the built page uploads the demo disk into the device's own seeding code (fw/core/store, built
     natively by fw/tests/run_store.sh as build/fw/store/seed_bridge, on a fake flash) */
  const BRIDGE = `${ROOT}build/fw/store/seed_bridge`, PAGE = `${ROOT}build/fw/install.html`;
  if (existsSync(BRIDGE) && existsSync(PAGE) && existsSync(`${ROOT}dist/firmware/demo.ptfmdsk`)) {
    const { spawn } = await import("node:child_process");
    const br = spawn(BRIDGE, [], { stdio: ["pipe", "pipe", "inherit"] });
    const dev = new FakeFM1(Uint8Array.from(logical), { running: "FM-1_7101" });
    let buf = "", answers = [], lines = [], sink = (b) => dev.tx(b);
    br.stdout.on("data", (d) => {
      buf += d.toString();
      let i;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i);
        buf = buf.slice(i + 1);
        if (line.startsWith("S ")) sink(line.slice(2).match(/../g).map((h) => parseInt(h, 16)));
        else if (line === ".") { const a = answers.shift(); if (a) a(lines); lines = []; }
        else lines.push(line);
      }
    });
    const cmd = (line) => new Promise((res) => { answers.push(res); br.stdin.write(line + "\n"); });
    const hex = (b) => Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16).toUpperCase()).join("");
    dev.seed = () => {};                                  /* PT/FM's messages go to the bridge instead */
    const rx = dev.rx.bind(dev);
    dev.rx = (d) => (d.length > 5 && d[1] === 0x7D && d[2] === 0x50 && d[3] === 0x54 ? cmd("M " + hex(d)) : rx(d));
    const el = runPage(readFileSync(PAGE, "utf8"), dev.access);
    await until(() => !el("disk").disabled, 8000);
    const t0 = Date.now();
    el("disk").on.click();                                /* (one press: planned and written at once) */
    await until(() => /^Done|stopped/.test(el("status").textContent) || /err/.test(el("status").className), 600000);
    const demo = SEED.parseData(readFileSync(`${ROOT}dist/firmware/demo.ptfmdsk`));
    const got = (await cmd("DUMP")).filter((l) => l.startsWith("R ")).map((l) => l.slice(2).split(" ").map((x) => parseInt(x, 16)));
    const want = new Map(demo.files.map((f) => [`${f.kind}.${f.num}.${f.sub}`, f]));
    const bad = got.filter(([k, n, s, v, len, crc]) => { const f = want.get(`${k}.${n}.${s}`); return !f || f.ver !== v || f.len !== len || f.crc !== crc >>> 0; });
    ok(el("status").textContent === `Done: the FM-1 holds ${demo.files.length} files.` && got.length === demo.files.length && !bad.length,
       `end to end: "${el("status").textContent}" (${((Date.now() - t0) / 1000).toFixed(1)} s; ${got.length} records, ${bad.length} differ)`);

    /* then an install over it (FWS rewritten with padding): the library as it was, through the device's own code */
    const to = {};                                        /* a session of the test's own, straight to the bridge */
    const bridged = () => SEED.session({ send: (b) => cmd("M " + hex(b)), onMessage: (cb) => { to.cb = cb; } }, { timeout: 4000 });
    sink = (b) => dev.tx(b);
    const boot = dev.boot.bind(dev);
    dev.boot = (id) => { if (id === "FM-1_7101" && dev.served) cmd("INSTALL"); boot(id); };
    el("go").on.click();
    await until(() => /^Done: the FM-1 runs/.test(el("status").textContent) || /err/.test(el("status").className), 120000);
    const after = (await cmd("DUMP")).filter((l) => l.startsWith("R ")).map((l) => l.slice(2).split(" ").map((x) => parseInt(x, 16)));
    const bad2 = after.filter(([k, n, s, v, len, crc]) => { const f = want.get(`${k}.${n}.${s}`); return !f || f.ver !== v || f.len !== len || f.crc !== crc >>> 0; });
    ok(/^Done: the FM-1 runs PT\/FM/.test(el("status").textContent) &&
       after.length === demo.files.length && !bad2.length,
       `end to end: INSTALL over PT/FM: installed, the library kept ("${el("status").textContent}"; ${after.length} records, ${bad2.length} differ)`);

    /* then CLEAR USER DATA (the debug page's, seed.js session.clear) over the demo disk: nothing left */
    {
      const d = bridged();
      sink = (b) => to.cb(Uint8Array.from(b));
      const r = await d.clear();
      d.close();
      sink = (b) => dev.tx(b);
      const left = (await cmd("DUMP")).filter((l) => l.startsWith("R "));
      ok(r.records === 0 && r.bad === 0 && left.length === 0,
         `end to end: CLEAR USER DATA over the demo disk: ${r.records} records, ${r.bad} damaged, ${left.length} in the flash`);
    }

    /* a save torn by a power cut (a damaged record the device counts), then the clean page's INSTALL FIRMWARE:
       everything erased, the damaged record too, exactly the demo disk; all through the device's own code */
    const counts = async () => {                          /* BEGIN, ABORT: the device mounts; its ACK counts records */
      const d = bridged();
      sink = (b) => to.cb(Uint8Array.from(b));
      const L3 = await d.hello();
      d.close();
      let got = [];
      sink = (b) => got.push(SEED.parseMessage(Uint8Array.from(b)));
      await cmd("M " + hex(SEED.message(SEED.CMD.BEGIN, [...le32(L3.lastSave + 1), ...le16(0)])));
      got = [];
      await cmd("M " + hex(SEED.message(SEED.CMD.ABORT, [])));
      const a = got.find((p) => p && p.cmd === SEED.CMD.ACK && p.data[0] === SEED.CMD.ABORT);
      return a ? { records: u16(a.data, 4), bad: u16(a.data, 6) } : { records: -1, bad: -1 };
    };
    let torn = null;
    for (let at = 1; at <= 8 && !torn; at++) {
      await cmd(`CUT ${at} 1`);
      await cmd("SAVE 21 07 00 01 " + hex(Uint8Array.from({ length: 600 }, (_, i) => (i * 13 + 1) & 255)));
      await cmd("BOOT");
      const c = await counts();
      if (c.bad > 0) torn = c;
    }
    ok(torn && torn.bad > 0, `end to end: a save cut short by a power cut: the device counts ${torn ? torn.bad : 0} damaged record(s)`);
    sink = (b) => dev.tx(b);
    const elc = runPage(readFileSync(PAGE, "utf8"), dev.access);   /* (the release page, CLEAN INSTALL switched on) */
    await until(() => !elc("go").disabled, 8000);
    elc("clean").checked = true;
    elc("clean").on.change();
    elc("go").on.click();
    await until(() => /^Done: a clean FM-1/.test(elc("status").textContent) || /err/.test(elc("status").className), 300000);
    const clean = (await cmd("DUMP")).filter((l) => l.startsWith("R ")).map((l) => l.slice(2).split(" ").map((x) => parseInt(x, 16)));
    const c3 = await counts();
    ok(/^Done: a clean FM-1, PT\/FM \(FM-1_7101\), its user area empty\.$/.test(elc("status").textContent) &&
       clean.length === 0 && c3.bad === 0 && c3.records === 0,
       `end to end: CLEAN INSTALL: "${elc("status").textContent}" (${clean.length} records, ${c3.bad} damaged)`);
    br.stdin.end("Q\n");
    br.kill();
  } else {
    console.log("SKIP  end to end: no build/fw/store/seed_bridge (fw/tests/run_store.sh makes it) or no built page");
  }
}
console.log(failed ? `${failed} failed` : "install page test passed");
process.exit(failed ? 1 : 0);
