/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the page. The virtual FM-1 (panel/), the screen driver (lcd.js), the UI
 * (ui.js and the scr-*.js screens), the transport (player.js) and the sound (audio.js, dsp-*.js); the
 * page's frame (the sidebar, ZOOM, MIDI, the status line) is panel/page.js's, as the emulator's.
 * The screen is drawn about 30 times a second. The first click or key starts the sound (browsers
 * allow audio only after one). In the browser's console: window.fm1 (the device), window.lcd,
 * window.midi (the real FM-1 as the controller), PTFM. */
(function () {
    "use strict";
    const dev = PTFM.createDevice(document.getElementById("fm1"));
    PTFM.attachKeyboard(dev);
    window.fm1 = dev;
    /* the frame: MIDI is attached before the first frame, so it sees every LED */
    const page = PTFM.page(dev);
    const M = PTFM.model;
    /* ERASE DATA: the page's keys and the user area kept gone, it starts again (a new FM-1) */
    page.onErase(() => M.eraseKept().then(() => location.reload(), () => location.reload()));
    /* UPLOAD DEMO DISK: the demo disk into the user area, kept, then a power cycle; refused with changes not saved */
    const demo = document.getElementById("demo-disk"), DEMO_WORD = demo ? demo.textContent : "";
    if (demo) demo.addEventListener("click", () => {
        if (!M.demoDisk()) {
            demo.textContent = "SAVE OR REVERT FIRST";
            setTimeout(() => { demo.textContent = DEMO_WORD; }, 3000);
            return;
        }
        M.kept().then(() => location.reload());
    });
    const lcd = PTFM.createLcd(dev);
    window.lcd = lcd;
    const U = PTFM.ui;

    const bad = PTFM.help.check();
    if (bad.length) console.warn("PT/FM help:\n" + bad.join("\n"));

    /* power-on: the user area as this browser kept it, else a new one (the built-ins alone) */
    M.startUp().catch(() => false).then(on);
    function on() {
        U.attach(dev);
        U.applySettings();
        U.start();

        /* the sound starts with the first click or key on the page: a browser's rule, and a press from
         * MIDI does not count. Until then a sign says so, and blinks when PLAY or a note asks for sound */
        let starting = false;
        const startAudio = () => {
            starting = true;
            PTFM.audio.start().then(() => page.soundOn())
                .catch(e => { starting = false; console.error("PT/FM sound:", e); });
        };
        window.addEventListener("pointerdown", startAudio, { capture: true });
        window.addEventListener("keydown", startAudio, { capture: true });
        function askForSound() {
            if (starting || PTFM.audio.running) return;
            page.askForSound();
        }
        dev.on("button", ev => { if (ev.down && ev.id === U.B.PLAY) askForSound(); });
        dev.on("note", ev => { if (ev.down) askForSound(); });
        dev.on("master", ev => { U.master = ev.value; });

        /* the frames: about 30 a second; FPS and the frame's time as the FM-1 would take them (a full
         * frame at 60 MHz: ~30 ms) */
        let last = 0, n = 0, t0 = performance.now();
        function frame(t) {
            requestAnimationFrame(frame);
            if (t - last < 31) return;
            last = t;
            const b0 = lcd.stats.us;
            try { U.render(lcd, dev); } catch (e) { console.error(e); }
            U.frameMs = Math.round((lcd.stats.us - b0) / 1000);
            n++;
            if (t - t0 >= 1000) { U.fps = Math.min(n, Math.floor(1000 / Math.max(1, U.frameMs))); n = 0; t0 = t; }
        }
        requestAnimationFrame(frame);
    }
})();
