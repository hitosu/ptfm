/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM emulator: the page (specs/emulator.md). The virtual FM-1 (panel/, as the playground's), and
 * behind it PT/FM's firmware compiled to WebAssembly, run by the audio worklet (worklet.js): the screen,
 * the LEDs and the sound are the firmware's. The first click or key switches it on (browsers allow
 * audio only after one, and the audio runs the device's time). The emulated flash is kept in this
 * browser (IndexedDB), so the library survives a reload; UPLOAD CUSTOM DISK and UPLOAD DEMO DISK write a disk into it
 * through the install page's seeding (fw/web/seed.js), DOWNLOAD DISK saves what it holds as one. RECORD keeps its
 * USB audio, as a computer records a real FM-1's, for SAVE WAV.
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
    const emu = { on: false, fw: null, demo: null, node: null, ctx: null, sysex: [], waiting: [], caps: {}, asked: {} };
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
        else if (m.t === "rec") recFrames(m);
        else if (m.t === "disk" || m.t === "unsaved") { const r = emu.asked[m.t]; delete emu.asked[m.t]; if (r) r(m); }
        else if (m.t === "ready") {
            emu.on = true;
            emu.caps = { rec: !!m.rec, disk: !!m.disk };
            power();
            for (let id = 0; id < 14; id++) if (dev.state.buttons >> id & 1) send({ t: "btn", id, down: true });
            for (let n = 0; n < 27; n++) if (dev.state.notes >> n & 1) send({ t: "note", id: n, down: true });
            emu.waiting.splice(0).forEach(fn => fn());
        } else if (m.t === "fault") {
            emu.on = false;
            recLost();
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
        await recEnd();                              /* (a recording ends there, kept for SAVE WAV) */
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

    async function seed(bytes, name, key = "UPLOAD CUSTOM DISK") {   /* (key: the button that asked, the box's title) */
        if (!window.PTFM_seed) { alert("seeding is missing: ../fw/web/seed.js did not load"); return; }
        const S = window.PTFM_seed;
        box.hidden = false;
        $("seed-title").textContent = key + ": " + name;
        bar.style.width = "0";
        go.disabled = true;
        go.hidden = false;
        go.textContent = "WRITE";
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
            const lines = Q => Q.summary.map(t => t.startsWith("PROBLEM") ? { text: t, kind: "bad" }
                : /^(DELETE|REWRITE|LOW SPACE|ERASE)/.test(t) ? { text: t, kind: "warn" } : t);
            /* a disk that does not fit around what the FM-1 holds, but fits it emptied: REPLACE ALL offered, the user
             * area erased and the disk written anew (seed.js planClean) */
            let P = S.plan(layout, data), summary = lines(P);
            if (!P.ok) {
                const C = S.planClean(layout, data);
                if (C.ok) {
                    summary = summary.concat(["", { text: "It fits the FM-1 emptied: REPLACE ALL erases everything it holds and " +
                        "writes the disk anew (an upload cut short then leaves a part: upload again).", kind: "warn" }], lines(C));
                    P = C;
                    go.textContent = "REPLACE ALL";
                }
            }
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
    $("seed-demo").addEventListener("click", () => { if (emu.demo) seed(emu.demo.bytes, emu.demo.name, "UPLOAD DEMO DISK"); });
    emu.seed = seed;
    /* a disk dropped anywhere on the page: UPLOAD CUSTOM DISK's, as if picked (a file that is no disk: its PROBLEM line); the
     * page framed while a file is dragged over it. Not while an upload's box is open. */
    const files = ev => ev.dataTransfer && [...ev.dataTransfer.types].includes("Files");
    window.addEventListener("dragover", ev => {
        if (!files(ev)) return;
        ev.preventDefault();
        ev.dataTransfer.dropEffect = box.hidden ? "copy" : "none";
        document.body.classList.toggle("dropping", box.hidden);
    });
    window.addEventListener("dragleave", ev => { if (!ev.relatedTarget) document.body.classList.remove("dropping"); });
    window.addEventListener("drop", async ev => {
        document.body.classList.remove("dropping");
        if (!files(ev)) return;
        ev.preventDefault();                         /* (not the browser opening the file) */
        const f = ev.dataTransfer.files[0];
        if (f && box.hidden) seed(new Uint8Array(await f.arrayBuffer()), f.name);
    });

    /* the worklet's answers (disk, unsaved): null when none came (switched off meanwhile) */
    const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
    const ask = t => new Promise(resolve => {
        const done = m => { clearTimeout(timer); resolve(m); };
        const timer = setTimeout(() => { if (emu.asked[t] === done) delete emu.asked[t]; resolve(null); }, 3000);
        emu.asked[t] = done;
        send({ t: t + "?" });
    });
    const p2 = n => String(n).padStart(2, "0");
    const stamp = (d = new Date()) => `${d.getFullYear()}-${p2(d.getMonth() + 1)}-${p2(d.getDate())}-${p2(d.getHours())}${p2(d.getMinutes())}`;
    function saveAs(blob, name) {                    /* the browser's download of a file made here */
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = name;
        document.body.appendChild(a);
        a.click();
        setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
    }

    /* ------------------------------------------------------------- DOWNLOAD DISK --- */
    /* what the emulated FM-1 holds in its user area, as a disk (specs/files/disk.md): its live records read from its
     * flash, each checked against its header's CRC, as the EDITOR's backup reads a real FM-1's. Asked of the firmware
     * itself (plat_web.c's web_disk), not through seeding, which refuses while changes are not saved: here they bring
     * a warning only, as they are not in the flash, so not on the disk. The dialog sees a SAVE made while it is open. */
    async function diskNow() {                       /* { files, bytes, unsaved }, as the flash holds them now */
        const S = window.PTFM_seed;
        let d = await ask("disk");
        for (let i = 0; d && d.saving && i < 50; i++) { await sleep(100); d = await ask("disk"); }   /* (a SAVE writing) */
        if (!d) throw new Error("THE FM-1 DID NOT ANSWER");
        if (d.saving) throw new Error("A SAVE IS BEING WRITTEN: TRY AGAIN IN A MOMENT");
        const img = new Uint8Array(d.image), L = d.list, files = [];
        for (let i = 0; i < L.length; i += 3) {
            const kind = L[i] & 255, num = L[i] >>> 8 & 255, sub = L[i] >>> 16 & 255, ver = L[i] >>> 24, at = L[i + 1], len = L[i + 2];
            const rec = img.subarray(at, at + 16 + len);
            const ok = rec.length === 16 + len && rec[0] === kind && rec[1] === num && rec[2] === sub &&
                (rec[4] | rec[5] << 8 | rec[6] << 16) === len &&
                S.crc32(rec.subarray(16), S.crc32(rec.subarray(0, 12))) === (rec[12] | rec[13] << 8 | rec[14] << 16 | rec[15] << 24) >>> 0;
            if (!ok) throw new Error(S.describe({ kind, num, sub }) + " READ BACK WRONG");
            files.push({ kind, num, sub, ver, payload: rec.slice(16) });
        }
        return { files, bytes: S.makeDisk(files), unsaved: !!d.unsaved };
    }
    /* changes not saved: the dialog says so until they are (SAVE on the FM-1: its key works with the dialog open);
     * true: DOWNLOAD (ANYWAY), false: CANCEL */
    function unsavedAsk() {
        const box = $("unsaved"), text = $("unsaved-text"), go = $("unsaved-go"), cancel = $("unsaved-cancel");
        const say = unsaved => {
            text.innerHTML = unsaved
                ? "The FM-1 has <b>changes not saved</b>. A disk holds what SAVE wrote: press <b>SAVE</b> on the FM-1 " +
                  "first (its key <b>I</b> works here), or they will not be on the disk."
                : "<b>Saved:</b> the disk holds everything now.";
            go.textContent = unsaved ? "DOWNLOAD ANYWAY" : "DOWNLOAD";
        };
        say(true);
        box.hidden = false;
        return new Promise(resolve => {
            let asking = false;
            const poll = setInterval(async () => {
                if (asking || !emu.on) return;
                asking = true;
                const m = await ask("unsaved");
                asking = false;
                if (m && !box.hidden) say(!!m.v);
            }, 400);
            const end = v => { clearInterval(poll); box.hidden = true; go.onclick = cancel.onclick = null; resolve(v); };
            go.onclick = () => end(true);
            cancel.onclick = () => end(false);
        });
    }
    function diskState(text, kind) {
        const p = $("disk-state");
        p.hidden = !text;
        p.textContent = text;
        p.className = "state" + (kind ? " " + kind : "");
    }
    async function downloadDisk() {
        const btn = $("disk-down");
        btn.disabled = true;
        try {
            await powerOn();
            if (!emu.caps.disk) throw new Error("THIS FIRMWARE BUILD CANNOT GIVE ITS DISK (BUILT BEFORE DOWNLOAD DISK)");
            let d = await diskNow();
            if (d.unsaved) {
                if (!await unsavedAsk()) { diskState(""); return; }
                d = await diskNow();                 /* (a SAVE made meanwhile is on it) */
            }
            if (!d.files.length) { diskState("NOTHING SAVED IN IT YET: NO DISK MADE", "warn"); return; }
            const name = `ptfm-emulator-${stamp()}.ptfmdsk`;
            saveAs(new Blob([d.bytes], { type: "application/octet-stream" }), name);
            diskState(`${name}: ${d.files.length} RECORDS, ${Math.ceil(d.bytes.length / 1024)} KB` +
                      (d.unsaved ? ", CHANGES NOT SAVED LEFT OUT" : ""), d.unsaved ? "warn" : "ok");
        } catch (e) {
            diskState("DOWNLOAD DISK: " + (e && e.message || e), "bad");
        } finally {
            btn.disabled = false;
        }
    }
    $("disk-down").addEventListener("click", downloadDisk);
    emu.downloadDisk = downloadDisk;

    /* -------------------------------------------------------------------- RECORD --- */
    /* the emulated FM-1's USB audio (specs/connections.md#usb-audio) as a computer records a real one's, as the debug
     * page's LISTEN does: the firmware's own frames, 16-bit stereo at its rate (44.1 kHz), kept here till SAVE WAV; one
     * recording at a time (a new one takes its place), 30 minutes at most. The firmware runs only while its sound does,
     * so a pause pauses the recording too: no gaps. RESET, ERASE DATA and a stopped firmware end it, kept. */
    const RATE = 44100, REC_MOST = 30 * 60 * RATE;   /* frames */
    const recIdle = $("rec-slot").textContent;
    let rec = null;                                  /* { chunks, frames, at, until, live, url, why, ended } */
    const mmss = s => `${p2(Math.floor(s / 60))}:${p2(Math.floor(s % 60))}`;
    function wavBlob(chunks, frames) {               /* a WAV: its header, then the frames as they came */
        const n = frames * 4, h = new DataView(new ArrayBuffer(44));
        const str = (o, s) => { for (let i = 0; i < 4; i++) h.setUint8(o + i, s.charCodeAt(i)); };
        str(0, "RIFF"); h.setUint32(4, 36 + n, true); str(8, "WAVE"); str(12, "fmt "); h.setUint32(16, 16, true);
        h.setUint16(20, 1, true); h.setUint16(22, 2, true); h.setUint32(24, RATE, true); h.setUint32(28, RATE * 4, true);
        h.setUint16(32, 4, true); h.setUint16(34, 16, true); str(36, "data"); h.setUint32(40, n, true);
        return new Blob([h, ...chunks], { type: "audio/wav" });
    }
    function recShow() {
        const on = !!rec && !rec.until, secs = rec ? rec.frames / RATE : 0;
        $("rec-on").disabled = on;
        $("rec-on").classList.toggle("recording", on);
        $("rec-on").textContent = on ? "REC " + mmss(secs) : "RECORD";
        $("rec-stop").disabled = !on;
        $("rec-chip").hidden = !on;
        $("rec-chip").textContent = on ? mmss(secs) : "";
        $("rec-slot").textContent = !rec ? recIdle
            : `${on ? "RECORDING " : ""}${mmss(secs)}, 44.1 kHz STEREO, ${((rec.frames * 4 + 44) / 1048576).toFixed(1)} MB` +
              (rec.until ? `, ${new Date(rec.at).toTimeString().slice(0, 5)}` : "") + (rec.why ? ": " + rec.why : "");
        const save = $("rec-save");
        save.hidden = !(rec && rec.until && !rec.live && rec.frames);
        if (!save.hidden && !rec.url) {
            rec.url = URL.createObjectURL(wavBlob(rec.chunks, rec.frames));
            save.href = rec.url;
            save.download = `ptfm-emulator-${stamp(new Date(rec.at))}.wav`;
        }
    }
    async function recStart() {
        if (rec && !rec.until) return;
        try { await powerOn(); } catch (_) { return; }  /* (powerOn says why) */
        if (!emu.caps.rec) { $("rec-slot").textContent = "THIS FIRMWARE BUILD CANNOT RECORD (BUILT BEFORE RECORD)"; return; }
        if (rec && rec.url) URL.revokeObjectURL(rec.url);
        rec = { chunks: [], frames: 0, at: Date.now(), until: 0, live: true, url: "", why: "", ended: null };
        send({ t: "rec", on: true });
        recShow();
    }
    function recFrames(m) {                          /* the worklet's frames, into the recording under way */
        if (!rec || !rec.live) return;
        if (m.pcm.byteLength) { rec.chunks.push(m.pcm); rec.frames += m.pcm.byteLength / 4; }
        if (m.end) { rec.live = false; if (rec.ended) rec.ended(); recShow(); }
        else if (rec.frames >= REC_MOST && !rec.until) recEnd("STOPPED AT 30:00, THE MOST");
    }
    /* STOP: the worklet's last frames (a moment at most), then SAVE WAV */
    function recEnd(why) {
        if (!rec || rec.until) return Promise.resolve();
        const r = rec;
        r.until = Date.now();
        r.why = why || "";
        const last = new Promise(resolve => { r.ended = resolve; setTimeout(resolve, 600); });
        if (emu.on) send({ t: "rec", on: false });
        else r.live = false;
        recShow();
        return last.then(() => { r.live = false; if (rec === r) recShow(); });
    }
    function recLost() {                             /* the firmware stopped: what came is kept */
        if (!rec || !rec.live) return;
        if (!rec.until) { rec.until = Date.now(); rec.why = "THE FIRMWARE STOPPED"; }
        rec.live = false;
        if (rec.ended) rec.ended();
        recShow();
    }
    $("rec-on").addEventListener("click", recStart);
    $("rec-stop").addEventListener("click", () => recEnd());
    setInterval(() => { if (rec && !rec.until) recShow(); }, 250);
    emu.recording = () => rec;

})();
