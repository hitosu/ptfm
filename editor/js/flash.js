/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The firmware's WebAssembly in the page (specs/editor.md, The preview; The FM-1):
 *   ED.flash.image(recs, { preview }): records laid out as the FM-1's user area, as a clean seeding lays them (seed.js
 *     planClean on the user area alone), or in the preview's room (PREVIEW: an instance told ed_preview_room, its
 *     user area ~1 MB): the blocks to write into a firmware's flash, which then mounts them;
 *   ED.Firmware: one instance of the firmware (the editor's build: fw/core, the emulator's platform, editor/wasm),
 *     run on the page's thread: the virtual FM-1 (seeded through seed.js's session), the pictures, the measuring of
 *     LEVELs. The preview runs its own instance in an AudioWorklet (player.js). */
(function (ED) {
"use strict";
const R = ED.rec, U = ED.util;
const USER = [{ start: 0x093000, end: 0x0E0000, area: "user", inPlace: true }, { start: 0x0E5000, end: 0x0FF000, area: "user", inPlace: true }];
/* the preview's room (an instance told ed_preview_room: the flash from the slot's start, ~1 MB): an item bigger than
 * an FM-1 holds plays there */
const PREVIEW = [{ start: 0x004000, end: 0x0FF000, area: "user", inPlace: true }];
const FLASH = 1 << 20;

/* the user area alone: nothing of the user's is ever in FWS (specs/files/store.md); o.preview: the preview's room */
function image(recs, o) {
    const preview = !!(o && o.preview);
    const accepts = Object.keys(R.VERSION).map(k => ({ kind: +k, ver: R.VERSION[k] }));
    for (const r of recs) if (!accepts.some(a => a.kind === r.kind && a.ver === r.ver)) accepts.push({ kind: r.kind, ver: r.ver });
    const files = recs.filter(r => r.payload && r.kind < 0xE0).map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, len: r.payload.length,
        crc: r.crc, payload: r.payload, label: PTFM_seed.describe(r) }));
    const P = PTFM_seed.planClean({ regions: preview ? PREVIEW : USER, reserve: 0, accepts, lastSave: 0, records: [], builtins: [], fwText: "" }, { files, problems: [] });
    if (!P.ok) throw new Error(P.problems[0] || (preview ? "it does not fit the preview's room" : "it does not fit the FM-1's user area"));
    return P.blocks;
}

/* one instance of the firmware on the page's thread */
class Firmware {
    constructor(module) { this.module = module; this.ex = null; this.cb = null; this.busyUntil = 0; this.timer = 0; this.savedGen = 0; }
    static compile(bytes) { return WebAssembly.compile(bytes); }
    async start(flash) {
        let fake = 0;
        const clock = globalThis.performance ? () => performance.now() * 1000 : () => (fake += 1);
        const inst = await WebAssembly.instantiate(this.module, { env: { web_clock_us: clock } });
        this.ex = inst.exports || inst.instance.exports;
        this.ex.web_flash_reset();
        if (flash) this.flash().set(flash);
        this.ex.web_master(1023);
        this.ex.web_init();
        this.savedGen = this.ex.web_flash_gen();
        return this;
    }
    mem() { return new Uint8Array(this.ex.memory.buffer); }
    flash() { return new Uint8Array(this.ex.memory.buffer, this.ex.web_flash(), FLASH); }
    /* this instance the preview's: its user area the preview's room (pictures, AUTO LEVEL of anything on the TABLE) */
    preview() {
        this.ex.ed_preview_room(1);
        this.room = true;
        return this;
    }
    /* blocks laid out by image() into the flash (its user area erased first: nothing of an earlier image stays), then
     * mounted */
    mount(blocks) {
        const f = this.flash();
        f.fill(0xFF, this.room ? PREVIEW[0].start : USER[0].start, USER[1].end);
        for (const b of blocks) f.set(b.image, b.block << 12);
        this.ex.ed_remount();
    }
    /* records as the FM-1 would hold them: laid out and mounted, the built-in things' changed copies (F0-FF, which no
     * layout takes) put as edits on top, as the FM-1's own edits are */
    load(recs) {
        const isB = r => r.num >= 0xF0 && (R.isCart(r.kind) || r.kind === R.KIND.PATTERN);
        this.mount(image(recs.filter(r => !isB(r)), { preview: this.room }));
        for (const r of recs.filter(isB)) {
            this.mem().set(r.payload, this.ex.ed_edit_buf());
            this.ex.ed_edit_put(r.kind, r.num, r.sub, r.ver, r.payload.length);
        }
    }
    /* the device's time: n frames */
    run(frames) {
        const ex = this.ex;
        for (let done = 0; done < frames; done += 128) {
            ex.web_render(128);
            const n = ex.web_sysex_out_len();
            if (n) {
                const all = this.mem().slice(ex.web_sysex_out(), ex.web_sysex_out() + n);
                ex.web_sysex_out_clear();
                let s = 0;
                for (let i = 0; i < all.length; i++) if (all[i] === 0xF7) { if (this.cb) this.cb(all.slice(s, i + 1)); s = i + 1; }
            }
            const mo = ex.web_midi_out_len();           /* its MIDI OUT: messages one after another */
            if (mo) {
                const b = this.mem().slice(ex.web_midi_out(), ex.web_midi_out() + mo);
                ex.web_midi_out_clear();
                if (this.onMidi) for (let i = 0; i < b.length; ) {
                    const n = b[i] >= 0xF8 ? 1 : (b[i] & 0xE0) === 0xC0 ? 2 : 3;
                    this.onMidi(b.slice(i, i + n));
                    i += n;
                }
            }
        }
    }
    /* a transport for seed.js's session: SysEx in through the device's queue, out as it sends */
    transport() {
        const self = this;
        return {
            send(bytes) {
                const ex = self.ex;
                for (let tries = 0; ; tries++) {
                    self.mem().set(bytes, ex.web_sysex_stage());
                    if (ex.web_sysex_push(bytes.length) >= 0) break;
                    if (tries > 50) throw new Error("the virtual FM-1's SysEx queue is full");
                    self.run(256);
                }
                self.busyUntil = Date.now() + 3000;
                self.pump();
            },
            onMessage(fn) { self.cb = fn; }
        };
    }
    /* while a session waits for answers the device's time runs fast; else it rests */
    pump() {
        if (this.timer) return;
        const tick = () => {
            this.timer = 0;
            const t0 = Date.now();
            while (Date.now() - t0 < 12) this.run(1024);
            if (this.onFlash && this.ex.web_flash_gen() !== this.savedGen) { this.savedGen = this.ex.web_flash_gen(); this.onFlash(); }
            if (Date.now() < this.busyUntil) this.timer = setTimeout(tick, 0);
        };
        this.timer = setTimeout(tick, 0);
    }
    /* LEVEL as AUTO LEVEL measures it, half dB; null silent */
    level(cart, slot) {
        /* (the player keeps the cartridges it read: told the store changed, and a half run, it reads them again) */
        this.ex.ed_store_changed();
        this.run(512);
        const v = this.ex.ed_level(cart, slot);
        return v ? v - 1000 : null;
    }
    /* the built-in instruments' records */
    builtins() {
        const ex = this.ex, out = [];
        for (let i = 0; i < ex.ed_builtin_count(); i++) {
            const at = ex.ed_builtin(i), h = this.mem().slice(at, at + 12), dv = new DataView(h.buffer);
            const len = dv.getUint32(4, true), p = dv.getUint32(8, true);
            const payload = this.mem().slice(p, p + len);
            out.push({ kind: h[0], num: h[1], sub: h[2], ver: h[3], len, crc: U.crc32(payload), payload, builtin: true });
        }
        return out;
    }
}

ED.flash = { image, USER, PREVIEW };
ED.Firmware = Firmware;
})(globalThis.ED = globalThis.ED || {});
