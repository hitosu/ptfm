/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The emulator's audio worklet: the firmware (ptfm.wasm, fw/core with emulator/plat_web.c) runs here.
 * Each render quantum asks it for audio, and making the audio runs the device's time (app_audio every
 * 256 frames, app_poll every millisecond: plat_web.c). Between quanta the page's input goes in; up to
 * 60 times a second the screen's changed rows and the LEDs go out, and the SysEx the device sends at
 * once; the flash goes out a second after its last write, for the page to keep.
 *
 * A page opened from file:// can load a worklet only from a data: URL, so this file is a classic
 * script: it gives the page the worklet's source (PTFM.emuWorkletSource), the text of the function
 * below, run in the worklet's scope.
 *
 * Messages in:  {t: "load", wasm, flash, master}   the WebAssembly; the kept flash (1 MiB) or null
 *               {t: "btn", id, down} {t: "note", id, down} {t: "knob", id, step} {t: "master", v 0-1}
 *               {t: "sysex", bytes}                 a message F0 ... F7 for the device
 *               {t: "midi", b, src}                 a MIDI message in (notes, CCs, the clock): b its bytes
 *               {t: "flash?"}                       the flash now, whatever its writes
 *               {t: "rec", on}                      RECORD: USB audio's frames from now on, or the end of them
 *               {t: "disk?"} {t: "unsaved?"}        DOWNLOAD DISK: the user area's records; changes not saved?
 * Messages out: {t: "ready", fwEnd, rec, disk}      (rec, disk: the firmware can record, give its disk)
 *               {t: "frame", y0, y1, px} {t: "leds", leds} {t: "sysex", bytes}
 *               {t: "midi", bytes}                  MIDI OUT: messages one after another
 *               {t: "flash", image} {t: "fault", text}
 *               {t: "restart"}                      the firmware asks for a restart (FACTORY RESET), once
 *               {t: "rec", pcm, end}                USB audio's frames, 16-bit stereo, a quarter second or so a
 *                                                   message; end: the last of the recording
 *               {t: "disk", list, image, unsaved}   the live records (3 words each: plat_web.c's web_disk) and the
 *                                                   flash they are in; {t: "disk", saving: true} while a save writes
 *               {t: "unsaved", v} */
window.PTFM = window.PTFM || {};

PTFM.emuWorklet = function () {
    "use strict";
    const RATE = 44100;                                   /* the FM-1's */
    const QUANTUM = 128;
    /* the PC's time for the firmware's CPU figures: performance.now when the worklet has it */
    const clockUs = globalThis.performance ? () => globalThis.performance.now() * 1000 : () => Date.now() * 1000;

    class Emulator extends AudioWorkletProcessor {
        constructor() {
            super();
            this.ex = null;
            this.gain = 1 / 8388608;                      /* the codec's 24-bit words (MASTER: the C's) */
            this.step = RATE / sampleRate;                /* device frames an output frame */
            this.ring = new Float32Array(4096 * 2);       /* device frames, for resampling */
            this.written = 0;                             /* device frames in the ring, ever */
            this.pos = 0;                                 /* the next output frame, in device frames */
            this.sentAt = -1;
            this.leds = null;
            this.pending = [];                            /* SysEx the device's queue had no room for */
            this.gen = 0; this.genAt = 0; this.savedGen = 0;
            this.rec = null;                              /* RECORD's frames not yet sent: { parts, frames } */
            this.port.onmessage = e => this.onMessage(e.data);
        }

        fault(err) {
            let text = String(err);
            if (this.ex) {
                const mem = new Uint8Array(this.ex.memory.buffer, this.ex.web_fault(), 128);
                const why = String.fromCharCode(...mem.slice(0, mem.indexOf(0)));
                if (why) text = why + " (" + text + ")";
            }
            this.ex = null;
            this.port.postMessage({ t: "fault", text });
        }

        async onMessage(m) {
            if (m.t === "load") {
                try {
                    const { instance } = await WebAssembly.instantiate(m.wasm, { env: { web_clock_us: clockUs } });
                    const ex = instance.exports;
                    ex.web_flash_reset();
                    if (m.flash) new Uint8Array(ex.memory.buffer, ex.web_flash(), 1 << 20).set(new Uint8Array(m.flash));
                    ex.web_master(Math.round((m.master ?? 0.5) * 1023));
                    this.ex = ex;
                    ex.web_init();
                    this.gen = this.savedGen = ex.web_flash_gen();
                    this.port.postMessage({ t: "ready", fwEnd: ex.web_fw_end(), rec: !!ex.web_rec, disk: !!ex.web_disk });
                } catch (err) {
                    this.fault(err);
                }
                return;
            }
            const ex = this.ex;
            if (!ex) return;
            try {
                if (m.t === "btn") ex.web_button(m.id, m.down ? 1 : 0);
                else if (m.t === "note") ex.web_note(m.id, m.down ? 1 : 0);
                else if (m.t === "knob") ex.web_knob(m.id, m.step);
                else if (m.t === "master") ex.web_master(Math.round(m.v * 1023));
                else if (m.t === "sysex") { this.pending.push(m.bytes); this.flushSysex(); }
                else if (m.t === "midi") ex.web_midi_in(m.b[0], m.b[1] || 0, m.b[2] || 0, m.src || 0);
                else if (m.t === "flash?") this.sendFlash();
                else if (m.t === "rec" && ex.web_rec) {
                    if (m.on) { ex.web_rec(1); this.rec = { parts: [], frames: 0 }; }
                    else if (this.rec) { this.recTake(); ex.web_rec(0); this.recPost(true); this.rec = null; }
                }
                else if (m.t === "disk?" && ex.web_disk) this.sendDisk();
                else if (m.t === "unsaved?" && ex.web_unsaved) this.port.postMessage({ t: "unsaved", v: ex.web_unsaved() });
            } catch (err) {
                this.fault(err);
            }
        }

        recTake() {                                       /* USB audio's frames since the last take */
            const ex = this.ex, n = ex.web_rec_len();
            if (!n) return;
            this.rec.parts.push(new Int16Array(ex.memory.buffer, ex.web_rec_at(), n * 2).slice());
            this.rec.frames += n;
            ex.web_rec_take();
        }

        recPost(end) {                                    /* what was taken, in one message */
            const pcm = new Int16Array(this.rec.frames * 2);
            let at = 0;
            for (const p of this.rec.parts) { pcm.set(p, at); at += p.length; }
            this.rec.parts = [];
            this.rec.frames = 0;
            this.port.postMessage({ t: "rec", pcm: pcm.buffer, end }, [pcm.buffer]);
        }

        sendDisk() {
            const ex = this.ex, n = ex.web_disk();
            if (n < 0) { this.port.postMessage({ t: "disk", saving: true }); return; }
            const list = new Uint32Array(ex.memory.buffer, ex.web_disk_at(), n * 3).slice();
            const image = new Uint8Array(ex.memory.buffer, ex.web_flash(), 1 << 20).slice().buffer;
            this.port.postMessage({ t: "disk", list, image, unsaved: ex.web_unsaved() }, [list.buffer, image]);
        }

        flushSysex() {                                    /* the page's messages into the device's queue */
            const ex = this.ex;
            while (this.pending.length) {
                const b = this.pending[0];
                if (b.length > 2048) { this.pending.shift(); continue; }
                new Uint8Array(ex.memory.buffer, ex.web_sysex_stage(), b.length).set(b);
                if (ex.web_sysex_push(b.length) < 0) return;  /* full: again after more of the device's time */
                this.pending.shift();
            }
        }

        sendFlash() {
            const ex = this.ex;
            const image = new Uint8Array(ex.memory.buffer, ex.web_flash(), 1 << 20).slice().buffer;
            this.savedGen = ex.web_flash_gen();
            this.port.postMessage({ t: "flash", image }, [image]);
        }

        pull() {                                          /* the device's next quantum, into the ring */
            const ex = this.ex;
            ex.web_render(QUANTUM);
            const s = new Int32Array(ex.memory.buffer, ex.web_out(), QUANTUM * 2);
            for (let k = 0; k < QUANTUM; k++) {
                const i = ((this.written + k) % 4096) * 2;
                this.ring[i] = s[2 * k];
                this.ring[i + 1] = s[2 * k + 1];
            }
            this.written += QUANTUM;
            if (this.rec) {                               /* RECORD: USB audio's frames made meanwhile */
                this.recTake();
                if (this.rec.frames >= 11025) this.recPost(false);
            }
        }

        process(inputs, outputs) {
            const out = outputs[0], L = out[0], R = out[1] || out[0];
            if (!this.ex) return true;
            try {
                if (this.pending.length) this.flushSysex();
                const n = L.length, g = this.gain, ring = this.ring;
                if (this.step === 1 && n === QUANTUM && this.pos === this.written) {
                    this.pull();                          /* 44.1 kHz: the device's frames as they are */
                    const i0 = ((this.written - QUANTUM) % 4096) * 2;
                    for (let k = 0; k < n; k++) {
                        L[k] = ring[i0 + 2 * k] * g;
                        R[k] = ring[i0 + 2 * k + 1] * g;
                    }
                    this.pos = this.written;
                } else {
                    for (let k = 0; k < n; k++) {         /* any other rate: linear between frames */
                        const i = Math.floor(this.pos), f = this.pos - i;
                        while (this.written < i + 2) this.pull();
                        const a = (i % 4096) * 2, b = ((i + 1) % 4096) * 2;
                        L[k] = (ring[a] + (ring[b] - ring[a]) * f) * g;
                        R[k] = (ring[a + 1] + (ring[b + 1] - ring[a + 1]) * f) * g;
                        this.pos += this.step;
                    }
                }
                this.output();
            } catch (err) {
                this.fault(err);
            }
            return true;
        }

        output() {                                        /* the device's screen, LEDs, SysEx, flash to the page */
            const ex = this.ex, mem = ex.memory.buffer;
            const sx = ex.web_sysex_out_len();
            if (sx) {
                const all = new Uint8Array(mem, ex.web_sysex_out(), sx).slice();
                ex.web_sysex_out_clear();
                let s = 0;
                for (let i = 0; i < all.length; i++)
                    if (all[i] === 0xF7) { this.port.postMessage({ t: "sysex", bytes: all.slice(s, i + 1) }); s = i + 1; }
            }
            const mo = ex.web_midi_out_len();           /* MIDI OUT: at once, as it came */
            if (mo) {
                this.port.postMessage({ t: "midi", bytes: new Uint8Array(mem, ex.web_midi_out(), mo).slice() });
                ex.web_midi_out_clear();
            }
            const gen = ex.web_flash_gen();
            if (gen !== this.gen) { this.gen = gen; this.genAt = currentTime; }
            else if (gen !== this.savedGen && currentTime - this.genAt >= 1) this.sendFlash();
            if (!this.restarting && ex.web_restart && ex.web_restart()) {   /* FACTORY RESET's restart: the page switches it off and on */
                this.restarting = true;
                this.port.postMessage({ t: "restart" });
            }
            if (currentTime - this.sentAt < 1 / 60) return;
            this.sentAt = currentTime;
            const d = ex.web_fb_dirty();
            if (d) {
                const y0 = d >>> 16, y1 = d & 0xFFFF;
                const px = new Uint16Array(mem, ex.web_fb() + y0 * 480, (y1 - y0) * 240).slice().buffer;
                this.port.postMessage({ t: "frame", y0, y1, px }, [px]);
            }
            const leds = new Uint8Array(mem, ex.web_leds(), 42);
            if (!this.leds || leds.some((v, i) => v !== this.leds[i])) {
                this.leds = leds.slice();
                this.port.postMessage({ t: "leds", leds: this.leds.slice() });
            }
        }
    }

    registerProcessor("ptfm-emulator", Emulator);
};

PTFM.emuWorkletSource = "(" + PTFM.emuWorklet.toString() + ")();\n";
