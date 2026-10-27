/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* EDITOR [beta]'s start (specs/editor.md): one page at a time, the built-in instruments from the page's firmware, both
 * panels back from the browser, FILES built, the keys wired (the header's logo shows FILES; Ctrl+Z / Ctrl+Y; Space plays
 * the picked item), files picked with + ADD FILES. */
(function (ED) {
"use strict";
const U = ED.ui, app = ED.app, M = ED.model, $ = U.$;

async function start() {
    await ED.keep.lock(() => {
        document.body.append(U.h("div", { class: "lockwrap", id: "lockwrap" }, U.h("div", null, U.h("b", null, "THE EDITOR IS OPEN IN ANOTHER TAB"), U.h("br"),
            "IT KEEPS BOTH PANELS IN THIS BROWSER, SO ONLY ONE PAGE AT A TIME MAY CHANGE THEM. CLOSE THE OTHER ONE: THIS PAGE THEN GOES ON BY ITSELF.")));
    });
    const lw = $("#lockwrap");
    if (lw) lw.remove();
    ED.keep.onError = () => U.status("THIS BROWSER DID NOT KEEP THE PANELS (ITS STORAGE IS FULL OR OFF): DOWNLOAD DISK SAVES THE FM-1 PANEL AS A FILE", { err: true });
    U.busy("STARTING THE FIRMWARE", 0.2);
    try {
        const fw = await ED.pics.ready();
        M.setBuiltins(fw.builtins());
    } catch (e) { U.status("THE FIRMWARE DID NOT START IN THIS PAGE: " + String(e.message || e).toUpperCase(), { err: true }); }
    const saved = await ED.keep.load();
    if (saved && saved.v === 1 && saved.st) {
        for (const side of ["L", "R", "base"]) for (const it of saved.st[side] || []) M.seen(it.id);
        app.st = Object.assign({ L: [], R: [], base: [], fm1: app.emptyFm1() }, saved.st);
    }
    ED.files.build();
    if (saved && saved.ui) ED.files.setPrefs(saved.ui);
    ED.player.wire();
    app.changed({});
    ED.tabs.show("files");
    wire();
    const n = app.st.L.length + app.st.R.length;
    U.status(n ? "BACK AS YOU LEFT IT: " + U.plural(app.st.L.length, "ITEM") + " ON THE TABLE, " + U.plural(app.st.R.length, "ITEM") + " ON THE FM-1 PANEL"
        : "DROP FILES ON THE TABLE, OR READ WHAT THE FM-1 HOLDS");
}
function wire() {
    $("#brand").addEventListener("click", () => ED.tabs.show("files"));
    $("#undo").addEventListener("click", () => app.undo());
    $("#redo").addEventListener("click", () => app.redo());
    $("#filepick").addEventListener("change", e => { const f = [...e.target.files]; e.target.value = ""; if (f.length) ED.drops.add(f); });
    document.addEventListener("keydown", e => {
        if (U.dialogOpen()) return;
        const typing = e.target.closest("input, select, textarea, [contenteditable]");
        const t = ED.tabs.current();
        if ((e.ctrlKey || e.metaKey) && !typing && (e.key.toLowerCase() === "z" || e.key.toLowerCase() === "y")) {
            e.preventDefault();
            const redo = e.key.toLowerCase() === "y" || e.shiftKey;
            if (t) { if (redo) ED.tabs.redo(t); else ED.tabs.undo(t); }
            else if (redo) app.redo(); else app.undo();
        } else if (e.key === "Escape" && ED.player.now) ED.player.stop();
    });
    /* a file dropped anywhere outside a panel goes to the TABLE too */
    document.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("Files")) e.preventDefault(); });
    document.addEventListener("drop", e => {
        if (e.defaultPrevented || !e.dataTransfer.files.length) return;
        e.preventDefault();
        ED.tabs.show("files");
        ED.drops.add([...e.dataTransfer.files]);
    });
}
start();
})(globalThis.ED = globalThis.ED || {});
