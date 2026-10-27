/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM emulator: the page (specs/emulator.md). The virtual FM-1 (panel/, as the playground's), and
 * behind it PT/FM's firmware compiled to WebAssembly, run by the audio worklet (worklet.js): the screen,
 * the LEDs and the sound are the firmware's. The first click or key switches it on (browsers allow
 * audio only after one, and the audio runs the device's time). The emulated flash is kept in this
 * browser (IndexedDB), so the library survives a reload; SEED and DEMO DISK write a disk into it
 * through the install page's seeding (fw/web/seed.js).
 *
 * The firmware: ../dist/emulator/ptfm.wasm.js (the production build), else a dev build's,
 * ../build/fw/web/ptfm.wasm.js, else a file picked; index.html?dev tries the dev build first.
 * In the browser's console: window.fm1 (the panel), window.emu (the emulator: emu.eraseFlash(),
 * emu.flash() ...), window.midi. */
(function () {
    "use strict";
    const $ = id => document.getElementById(id);
    const dev = PTFM.createDevice($("fm1"));
    PTFM.attachKeyboard(dev);
    window.fm1 = dev;
    /* the page's frame (the sidebar, ZOOM, the status line, the sign until it is switched on) and MIDI, the real
     * FM-1 as the controller: panel/page.js's, as the playground's. MIDI's key's click switches on too, as any
     * first click does */
    const page = PTFM.page(dev);

    /* ------------------------------------------------------------------ the screen --- */
    /* the panel's memory as the firmware sends it (RGB565 rows), widened as the playground's lcd.js does */
    const img = new ImageData(240, 240);
    for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255;
    const C5 = [], C6 = [];
    for (let i = 0; i < 64; i++) {
        C6[i] = Math.floor(i * 255 / 63);
        if (i < 32) C5[i] = Math.floor(i * 255 / 31);
    }
    let due = false;
    function rows(y0, y1, buf) {
        const px = new Uint16Array(buf);
        for (let i = 0, o = y0 * 240 * 4; i < px.length; i++, o += 4) {
            const c = px[i];
            img.data[o] = C5[c >> 11];
            img.data[o + 1] = C6[c >> 5 & 63];
            img.data[o + 2] = C5[c & 31];
        }
        if (!due) {
            due = true;
            requestAnimationFrame(() => { due = false; dev.glass.putImageData(img, 0, 0); dev.present(); });
        }
    }

    /* ------------------------------------------------------------- the flash, kept --- */
    function idb() {
        return new Promise((resolve, reject) => {
            const r = indexedDB.open("ptfm-emulator", 1);
            r.onupgradeneeded = () => r.result.createObjectStore("flash");
            r.onsuccess = () => resolve(r.result);
            r.onerror = () => reject(r.error);
        });
    }
    async function flashLoad() {
        try {
            const db = await idb();
            return await new Promise(resolve => {
                const q = db.transaction("flash").objectStore("flash").get("image");
                q.onsuccess = () => resolve(q.result instanceof ArrayBuffer && q.result.byteLength === 1 << 20 ? q.result : null);
                q.onerror = () => resolve(null);
            });
        } catch (_) {
            return null;                             /* storage off: a new FM-1 each time */
        }
    }
    const idbDone = tx => new Promise(resolve => { tx.oncomplete = tx.onerror = tx.onabort = () => resolve(); });
    async function flashSave(image) {
        try {
            const db = await idb(), tx = db.transaction("flash", "readwrite");
            tx.objectStore("flash").put(image, "image");
            await idbDone(tx);
        } catch (e) {
            console.warn("PT/FM emulator: the flash could not be kept:", e);
        }
    }
    async function flashErase() {
        try {
            const db = await idb(), tx = db.transaction("flash", "readwrite");
            tx.objectStore("flash").delete("image");
            await idbDone(tx);
        } catch (_) {
            /* none kept */
        }
    }

    /* ---------------------------------------------------------------- the firmware --- */
    const emu = { on: false, fw: null, demo: null, node: null, ctx: null, sysex: [], waiting: [] };
    window.emu = emu;
    const fwInfo = t => { $("fw-info").textContent = t; $("fw-tip").textContent = "the firmware: " + t; };

    function loadScript(src) {
        return new Promise(resolve => {
            const s = document.createElement("script");
            s.src = src;
            s.onload = () => resolve(true);
            s.onerror = () => { s.remove(); resolve(false); };
            document.head.appendChild(s);
        });
    }
    const b64 = s => {
        const bin = atob(s), u = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
        return u;
    };

    async function findFirmware() {
        if (window.PTFM_WASM) {                      /* a page that carries its firmware (and the demo disk) itself */
            const w = window.PTFM_WASM;
            emu.fw = { wasm: b64(w.wasm), version: w.version, from: "this page" };
            if (window.PTFM_DEMO) emu.demo = { name: window.PTFM_DEMO.name, bytes: b64(window.PTFM_DEMO.ptd) };
            return;
        }
        const dirs =location.search.includes("dev") ? ["../build/fw/web/", "../dist/emulator/"] : ["../dist/emulator/", "../build/fw/web/"];
        for (const dir of dirs) {
            delete window.PTFM_WASM;
            if (await loadScript(dir + "ptfm.wasm.js") && window.PTFM_WASM) {
                const w = window.PTFM_WASM;
                emu.fw = { wasm: b64(w.wasm), version: w.version, from: dir.replace("../", "") + "ptfm.wasm.js" };
                delete window.PTFM_DEMO;
                if (await loadScript(dir + "demo.ptfmdsk.js") && window.PTFM_DEMO)
                    emu.demo = { name: window.PTFM_DEMO.name, bytes: b64(window.PTFM_DEMO.ptd) };
                return;
            }
        }
        $("firmware").hidden = false;                /* neither: a file picked */
        await new Promise(resolve => {
            $("fw-pick-btn").onclick = () => $("fw-pick").click();
            $("fw-pick").onchange = async () => {
                const f = $("fw-pick").files[0];
                if (!f) return;
                const buf = new Uint8Array(await f.arrayBuffer());
                if (f.name.endsWith(".wasm")) {
                    emu.fw = { wasm: buf, version: "?", from: f.name };
                } else {
                    const text = new TextDecoder().decode(buf), at = text.indexOf("{");
                    try {
                        const w = JSON.parse(text.slice(at, text.lastIndexOf("}") + 1));
                        emu.fw = { wasm: b64(w.wasm), version: w.version, from: f.name };
                    } catch (_) {
                        alert(f.name + " is not a firmware built for the emulator");
                        return;
                    }
                }
                $("firmware").hidden = true;
                resolve();
            };
        });
    }

    const found = findFirmware().then(() => {
        fwInfo(`${emu.fw.from} (${emu.fw.version})`);
        $("seed-demo").disabled = !emu.demo;
        if (!emu.demo) $("seed-demo").title = "no demo disk beside the firmware (tools/pack.py makes dist/firmware/demo.ptfmdsk; the build makes demo.ptfmdsk.js)";
    });

    /* ---------------------------------------------------------- switching it on --- */
    function send(m, transfer) { if (emu.on) emu.node.port.postMessage(m, transfer || []); }

    function onWorklet(m) {
        if (m.t === "frame") rows(m.y0, m.y1, m.px);
        else if (m.t === "leds") m.leds.forEach((v, id) => dev.led(id, v / 255));
        else if (m.t === "sysex") emu.sysex.forEach(fn => fn(m.bytes));
        else if (m.t === "midi") { if (emu.midiOut) emu.midiOut(m.bytes); }
        else if (m.t === "flash") flashSave(m.image);
        else if (m.t === "restart") cycle(true, "SWITCHING OFF AND ON");   /* (FACTORY RESET: the flash erased, kept) */
        else if (m.t === "ready") {
            emu.on = true;
            power();
            for (let id = 0; id < 14; id++) if (dev.state.buttons >> id & 1) send({ t: "btn", id, down: true });
            for (let n = 0; n < 27; n++) if (dev.state.notes >> n & 1) send({ t: "note", id: n, down: true });
            emu.waiting.splice(0).forEach(fn => fn());
        } else if (m.t === "fault") {
            emu.on = false;
            page.soundText("THE FIRMWARE STOPPED: " + m.text, true);
            console.error("PT/FM firmware:", m.text);
        }
    }

    let starting = null;
    function powerOn() {
        if (starting) return starting;
        starting = (async () => {
            await found;
            let ctx;
            try { ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" }); }
            catch (_) { ctx = new AudioContext({ latencyHint: "interactive" }); }
            /* the worklet from a data: URL (a page opened from file:// takes no blob: one), else a blob: one
             * (where a page's rules refuse data:, a page shown inside another site) */
            const src = PTFM.emuWorkletSource;
            let err = null;
            for (const url of ["data:text/javascript;charset=utf-8," + encodeURIComponent(src),
                               () => URL.createObjectURL(new Blob([src], { type: "text/javascript" }))]) {
                try {
                    await ctx.audioWorklet.addModule(typeof url === "function" ? url() : url);
                    err = null;
                    break;
                } catch (e) { err = e; }
            }
            if (err) throw err;
            const node = new AudioWorkletNode(ctx, "ptfm-emulator", { numberOfInputs: 0, outputChannelCount: [2] });
            node.connect(ctx.destination);
            node.port.onmessage = e => onWorklet(e.data);
            emu.ctx = ctx;
            emu.node = node;
            ctx.onstatechange = power;
            const flash = await flashLoad();
            const wasm = emu.fw.wasm.slice().buffer;
            const ready = new Promise(resolve => emu.waiting.push(resolve));
            node.port.postMessage({ t: "load", wasm, flash, master: dev.state.master }, flash ? [wasm, flash] : [wasm]);
            if (ctx.state !== "running") ctx.resume().catch(() => {});   /* (not waited for: refused, the next tap asks) */
            await ready;
        })();
        starting.catch(e => {
            starting = null;
            console.error("PT/FM emulator:", e);
            page.soundText("COULD NOT SWITCH ON: " + e, true);
        });
        return starting;
    }
    /* POWER's word as the sound is: the firmware runs only while it does (its time is the audio's) */
    function power() {
        if (!emu.on || !emu.ctx) return;
        if (emu.ctx.state === "running") page.soundOn();
        else page.soundText(matchMedia("(pointer: coarse)").matches ? "PAUSED: TAP TO GO ON" : "PAUSED: CLICK TO GO ON");
    }
    /* switched on by a gesture the browser lets sound start from: a key, a click, a mouse's press, a finger
     * lifted (a touch screen counts its tap only when the finger lifts: an AudioContext made at the touch can stay
     * suspended); every such gesture asks again while the sound is not running */
    let cycling = false;                             /* (RESET, ERASE DATA under way: they switch it on again) */
    function gesture() {
        if (cycling) return;
        if (emu.ctx && emu.ctx.state === "suspended") emu.ctx.resume().catch(() => {});
        powerOn();
    }
    window.addEventListener("pointerup", gesture, { capture: true });
    window.addEventListener("pointerdown", ev => { if (ev.pointerType === "mouse") gesture(); }, { capture: true });
    window.addEventListener("click", gesture, { capture: true });
    window.addEventListener("keydown", gesture, { capture: true });
    document.addEventListener("visibilitychange", () => {
        if (!document.hidden && emu.ctx && emu.ctx.state === "suspended") emu.ctx.resume().catch(() => {});
    });

    /* RESET: off and on again, as the FM-1's power switch, its flash kept; ERASE DATA (page.js's): off, the flash
     * erased, on again as a new FM-1 */
    async function powerOff(keep) {
        if (starting) { try { await starting; } catch (_) { /* it never came on */ } }
        if (emu.on && keep) {
            const f = await emu.flash();
            if (f) await flashSave(f.buffer);
        }
        const node = emu.node, ctx = emu.ctx;
        emu.on = false;
        starting = null;
        emu.node = emu.ctx = null;
        if (node) { node.port.onmessage = null; node.disconnect(); }
        if (ctx) { ctx.onstatechange = null; await ctx.close().catch(() => {}); }
        for (let i = 0; i < img.data.length; i += 4) img.data[i] = img.data[i + 1] = img.data[i + 2] = 0;
        dev.glass.putImageData(img, 0, 0);
        dev.present();
        for (let id = 0; id < 42; id++) dev.led(id, 0);
    }
    async function cycle(keep, word) {
        if (cycling) return;
        cycling = true;
        try {
            page.soundText(word);
            await powerOff(keep);
            if (!keep) await flashErase();
            await powerOn();
        } catch (_) {
            /* (powerOn says why) */
        } finally {
            cycling = false;
        }
    }
    $("power-reset").addEventListener("click", () => cycle(true, "SWITCHING OFF AND ON"));
    page.onErase(() => cycle(false, "ERASED: A NEW FM-1"));

    function askForSound() {
        if (!emu.on) page.askForSound();
    }

    /* the panel's input to the firmware */
    dev.on("button", ev => { send({ t: "btn", id: ev.id, down: ev.down }); if (ev.down) askForSound(); });
    dev.on("note", ev => { send({ t: "note", id: ev.note, down: ev.down }); if (ev.down) askForSound(); });
    dev.on("encoder", ev => send({ t: "knob", id: ev.id, step: ev.step }));
    dev.on("master", ev => send({ t: "master", v: ev.value }));

    /* MIDI IN and OUT of the emulated FM-1 (specs/connections.md), as its USB MIDI: a keyboard's notes play
     * as its keys, a sequencer's clock syncs it (SETTINGS' MIDI page says how); what it sends goes to the
     * output picked. The ports picked are remembered in this browser. */
    (function ports() {
        const selIn = $("midi-in"), selOut = $("midi-out");
        if (!navigator.requestMIDIAccess) { selIn.disabled = selOut.disabled = true; return; }
        const saved = k => { try { return localStorage.getItem("ptfm-emu-" + k) || ""; } catch (_) { return ""; } };
        const save = (k, v) => { try { localStorage.setItem("ptfm-emu-" + k, v); } catch (_) { /* not kept */ } };
        let access = null, input = null, output = null;
        function onMessage(e) {                          /* complete messages, SysEx apart (the page's own) */
            const d = e.data;
            if (!d.length || d[0] === 0xF0 || d[0] < 0x80) return;
            send({ t: "midi", b: d[0] >= 0xF8 ? [d[0]] : [d[0], d[1] || 0, d[2] || 0] });
        }
        function pick() {
            if (input) input.onmidimessage = null;
            input = null;
            output = null;
            for (const p of access.inputs.values()) if (p.name === selIn.value) input = p;
            for (const p of access.outputs.values()) if (p.name === selOut.value) output = p;
            if (input) input.onmidimessage = onMessage;
        }
        function fill() {
            for (const [sel, ports, key] of [[selIn, access.inputs, "in"], [selOut, access.outputs, "out"]]) {
                const want = sel.value || saved(key);
                sel.length = 1;
                for (const p of ports.values()) sel.add(new Option(p.name, p.name));
                sel.value = [...sel.options].some(o => o.value === want) ? want : "";
            }
            pick();
        }
        selIn.addEventListener("change", () => { save("in", selIn.value); pick(); });
        selOut.addEventListener("change", () => { save("out", selOut.value); pick(); });
        /* MIDI is asked for when a list is first opened, not at load (the browser's question comes when the user
         * wants MIDI); a browser that allows it already fills the lists at once */
        let asked = false;
        function ask() {
            if (asked) return;
            asked = true;
            navigator.requestMIDIAccess({ sysex: false }).then(a => { access = a; a.onstatechange = fill; fill(); },
                () => { selIn.disabled = selOut.disabled = true; });
        }
        for (const sel of [selIn, selOut]) {
            sel.addEventListener("pointerdown", ask);
            sel.addEventListener("focus", ask);
        }
        if (navigator.permissions && navigator.permissions.query)
            navigator.permissions.query({ name: "midi", sysex: false })
                .then(s => { if (s.state === "granted") ask(); }, () => { /* (unknown: asked when a list opens) */ });
        /* MIDI OUT: the firmware's messages, one after another, a message as its status byte says */
        emu.midiOut = bytes => {
            if (!output) return;
            for (let i = 0; i < bytes.length;) {
                const s = bytes[i], n = s >= 0xF8 ? 1 : (s & 0xE0) === 0xC0 ? 2 : 3;
                try { output.send(bytes.slice(i, i + n)); } catch (_) { /* a port gone */ }
                i += n;
            }
        };
    })();

    /* the console's helpers */
    emu.flash = () => new Promise(resolve => {
        if (!emu.on) { resolve(null); return; }
        const prev = emu.node.port.onmessage;
        emu.node.port.onmessage = e => { if (e.data.t === "flash") { emu.node.port.onmessage = prev; resolve(new Uint8Array(e.data.image)); } else prev(e); };
        send({ t: "flash?" });
    });
    emu.eraseFlash = async () => {
        await flashErase();
        location.reload();
    };

    /* ------------------------------------------------------------------- seeding --- */
    /* a disk into the emulated FM-1, as the install page seeds the real one: the device reports its
     * layout, the plan is shown (what goes where, what does not fit, what will be deleted), and only a
     * WRITE writes it; the same SysEx as over USB, passed to the firmware in calls */
    const transport = {                              /* as seed.js's midiTransport: one listener */
        send(bytes) { send({ t: "sysex", bytes: new Uint8Array(bytes) }); },
        onMessage(fn) { emu.sysex = [fn]; },
    };
    emu.transport = transport;
    const box = $("seed"), lines = $("seed-lines"), bar = $("seed-bar").firstElementChild, go = $("seed-go"), cancel = $("seed-cancel");

    function show(list) {                            /* lines: text, or {text, kind: "warn" | "bad"} */
        lines.textContent = "";
        for (const l of list) {
            const s = document.createElement("span");
            if (l.kind) s.className = l.kind;
            s.textContent = (typeof l === "string" ? l : l.text) + "\n";
            lines.appendChild(s);
        }
    }

    async function seed(bytes, name) {
        if (!window.PTFM_seed) { alert("seeding is missing: ../fw/web/seed.js did not load"); return; }
        const S = window.PTFM_seed;
        box.hidden = false;
        $("seed-title").textContent = "SEED: " + name;
        bar.style.width = "0";
        go.disabled = true;
        go.hidden = false;
        cancel.textContent = "CANCEL";
        let closed = false, writing = false, ses = null;
        const done = () => { go.hidden = true; cancel.textContent = "CLOSE"; };
        cancel.onclick = () => {
            closed = true;
            box.hidden = true;
            if (writing && ses) ses.abort().catch(() => {});
        };
        try {
            const data = S.parseData(bytes);
            if (data.problems && data.problems.length) {
                show(data.problems.map(p => ({ text: "PROBLEM: " + p, kind: "bad" })));
                done();
                return;
            }
            show([`${name}: ${data.files.length} files`, "switching on ..."]);
            await powerOn();
            show([`${name}: ${data.files.length} files`, "asking the FM-1 for its layout ..."]);
            ses = S.session(transport);
            const layout = await ses.hello();
            if (closed) return;
            const P = S.plan(layout, data);
            const summary = P.summary.map(t => t.startsWith("PROBLEM") ? { text: t, kind: "bad" }
                                              : t.startsWith("DELETE") || t.startsWith("REWRITE") ? { text: t, kind: "warn" } : t);
            show(summary);
            if (!P.ok || P.upToDate) { done(); return; }
            go.disabled = false;
            await new Promise(resolve => { go.onclick = resolve; });
            go.disabled = true;
            writing = true;
            const r = await ses.write(P, p => { bar.style.width = Math.round(p.done * 100 / p.total) + "%"; });
            writing = false;
            bar.style.width = "100%";
            show(summary.concat(["", `WRITTEN: ${P.blocks.length} blocks. The FM-1 mounted ${r.records} records` +
                (r.bad ? `, ${r.bad} BAD` : "") + ` (save ${r.lastSave}).`]));
            done();
        } catch (e) {
            show([{ text: "SEEDING FAILED: " + (e && e.message || e), kind: "bad" }]);
            done();
        } finally {
            if (ses) ses.close();
        }
    }
    $("seed-file").addEventListener("click", () => $("seed-pick").click());
    $("seed-pick").addEventListener("change", async () => {
        const f = $("seed-pick").files[0];
        $("seed-pick").value = "";
        if (f) seed(new Uint8Array(await f.arrayBuffer()), f.name);
    });
    $("seed-demo").addEventListener("click", () => { if (emu.demo) seed(emu.demo.bytes, emu.demo.name); });
    emu.seed = seed;

})();
