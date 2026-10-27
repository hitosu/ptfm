/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor page in a browser (specs/editor.md, Tests: the page): each scenario of tests/editor/page/ drives
 * dist/editor/editor.html?virtual (the page's own firmware standing in for the FM-1) in a headless Chrome, through
 * its DevTools protocol with Node's own fetch and WebSocket (no packages), checks what the page shows and holds, and
 * saves screenshots to look at in build/editor/page/<scenario>/. An exception on the page fails its scenario.
 *
 *   node tests/editor/page_test.js [SCENARIO...]       (run_page.sh builds the page first)
 *   CHROME: the browser to start (else google-chrome, google-chrome-stable, chromium, chromium-browser from the PATH);
 *   PTFM_PAGE_PORT: its DevTools port (9339) */
"use strict";
const fs = require("fs"), path = require("path"), { spawn } = require("child_process");
const ROOT = path.resolve(__dirname, "..", "..");
const OUT = path.join(ROOT, "build", "editor", "page");
const PAGE = "file://" + path.join(ROOT, "dist", "editor", "editor.html") + "?virtual";
const PORT = +(process.env.PTFM_PAGE_PORT || 9339), HOST = "http://127.0.0.1:" + PORT;
const ORDER = ["files", "dialogs", "tabs", "pictures", "keys", "new", "backup", "down", "optimize", "narrow"];
const sleep = ms => new Promise(r => setTimeout(r, ms));
let checks = 0, fails = 0;
function check(ok, what) {
    checks++;
    if (!ok) { fails++; console.log("FAIL: " + what); }
    else if (process.env.VERBOSE) console.log("ok: " + what);
}

/* ---- the browser ---- */
function findChrome() {
    if (process.env.CHROME) return process.env.CHROME;
    for (const name of ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser"])
        for (const dir of (process.env.PATH || "").split(path.delimiter))
            if (dir && fs.existsSync(path.join(dir, name))) return path.join(dir, name);
    return null;
}
async function startChrome() {
    const bin = findChrome();
    if (!bin) throw new Error("no Chrome found: install Chrome or Chromium, or set CHROME to it");
    const profile = path.join(OUT, "chrome");
    fs.rmSync(profile, { recursive: true, force: true });
    const proc = spawn(bin, ["--headless=new", "--remote-debugging-port=" + PORT, "--user-data-dir=" + profile, "--window-size=1400,900",
        "--no-first-run", "--no-default-browser-check", "--autoplay-policy=no-user-gesture-required"], { stdio: "ignore" });
    for (let i = 0; i < 100; i++) {
        try { if ((await fetch(HOST + "/json/version")).ok) return proc; } catch (e) { /* (not up yet) */ }
        await sleep(100);
    }
    proc.kill();
    throw new Error("Chrome did not open its DevTools port " + PORT);
}

/* ---- one page, its DevTools session ---- */
async function openPage() {
    const t = await (await fetch(HOST + "/json/new?about:blank", { method: "PUT" })).json();
    const ws = new WebSocket(t.webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
    let id = 0;
    const wait = new Map(), errors = [];
    ws.onmessage = m => {
        const d = JSON.parse(m.data);
        if (d.id && wait.has(d.id)) { const w = wait.get(d.id); wait.delete(d.id); if (d.error) w.rej(new Error(d.error.message)); else w.res(d.result); }
        else if (d.method === "Runtime.exceptionThrown") errors.push(d.params.exceptionDetails.exception ? d.params.exceptionDetails.exception.description : d.params.exceptionDetails.text);
        else if (d.method === "Runtime.consoleAPICalled" && d.params.type === "error") errors.push(d.params.args.map(a => a.value || a.description).join(" "));
    };
    const send = (method, params) => new Promise((res, rej) => { const i = ++id; wait.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
    await send("Runtime.enable");
    await send("Page.enable");
    return { t, ws, send, errors };
}
/* (closed and gone: the editor lets one page at a time keep its panels, so the next one would wait for this one) */
async function closePage(p) {
    try { p.ws.close(); await fetch(HOST + "/json/close/" + p.t.id); } catch (e) { /* (gone already) */ }
    for (let i = 0; i < 100; i++) {
        try { if (!(await (await fetch(HOST + "/json/list")).json()).some(t => t.id === p.t.id)) break; } catch (e) { break; }
        await sleep(100);
    }
    await sleep(300);
}

/* a short sound for the drops: 0.25 s of a 440 Hz sine, 8-bit mono WAV at 22,050 Hz */
function sineWav(file) {
    const n = 5512, b = Buffer.alloc(44 + n);
    b.write("RIFF", 0); b.writeUInt32LE(36 + n, 4); b.write("WAVE", 8); b.write("fmt ", 12); b.writeUInt32LE(16, 16);
    b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(22050, 24); b.writeUInt32LE(22050, 28); b.writeUInt16LE(1, 32);
    b.writeUInt16LE(8, 34); b.write("data", 36); b.writeUInt32LE(n, 40);
    for (let i = 0; i < n; i++) b[44 + i] = 128 + Math.round(90 * Math.sin(2 * Math.PI * 440 * i / 22050));
    fs.writeFileSync(file, b);
    return file;
}

/* ---- a scenario: a fresh page at 1400 x 900, the panels emptied ---- */
async function run(name) {
    const dir = path.join(OUT, name);
    fs.rmSync(dir, { recursive: true, force: true });
    fs.mkdirSync(dir, { recursive: true });
    const p = await openPage();
    const before = checks;
    const evaluate = async expr => {
        const r = await p.send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
        if (r.exceptionDetails) throw new Error((r.exceptionDetails.exception && r.exceptionDetails.exception.description) || r.exceptionDetails.text);
        return r.result.value;
    };
    const api = {
        ROOT, sleep, check, evaluate, send: p.send,
        file: rel => path.join(ROOT, rel),
        sound: () => sineWav(path.join(OUT, "sine.wav")),
        size: (width, height, o) => p.send("Emulation.setDeviceMetricsOverride", Object.assign({ width, height, deviceScaleFactor: 1, mobile: false }, o || {})),
        async shot(n, clip) {
            const r = await p.send("Page.captureScreenshot", Object.assign({ format: "png" }, clip ? { clip: Object.assign({ scale: 1 }, clip) } : {}));
            fs.writeFileSync(path.join(dir, n + ".png"), Buffer.from(r.data, "base64"));
        },
        /* files given to the page's file input, as ADD FILES picks them */
        async drop(paths) {
            const doc = await p.send("DOM.getDocument", { depth: 1 });
            const q = await p.send("DOM.querySelector", { nodeId: doc.root.nodeId, selector: "#filepick" });
            await p.send("DOM.setFileInputFiles", { nodeId: q.nodeId, files: paths });
        },
        /* waits until expr is true (a tenth of a second at a time), its last value */
        async until(expr, secs) {
            let v;
            for (let i = 0; i < (secs || 20) * 10; i++) {
                /* (a page reloading or navigating under a poll answers "navigated or closed": not yet) */
                try { v = await evaluate(expr); } catch (e) { if (!/navigated or closed|context was destroyed|Cannot find context/i.test(e.message)) throw e; v = null; }
                if (v) return v;
                await sleep(100);
            }
            return v;
        },
        /* the page fresh: loaded, its firmware started, both panels empty, no tab */
        async fresh() {
            await p.send("Page.navigate", { url: PAGE });
            /* (started: its last step says so in the status line; a page of an earlier scenario still holding the
             * editor's one-page lock is closed, every page of this Chrome but this one) */
            const started = "!!document.querySelector('#status') && /DROP FILES ON THE TABLE|BACK AS YOU LEFT IT/.test(document.querySelector('#status').textContent)";
            if (!await api.until(started + " || !!document.querySelector('#lockwrap')", 60)) throw new Error("the page did not start");
            if (await evaluate("!!document.querySelector('#lockwrap')")) {
                for (const t of await (await fetch(HOST + "/json/list")).json())
                    if (t.type === "page" && t.id !== p.t.id) await fetch(HOST + "/json/close/" + t.id).catch(() => {});
                if (!await api.until(started, 60))
                    throw new Error("the page did not start: " + await evaluate("document.body ? document.body.innerText.slice(0, 160) : ''"));
            }
            await evaluate("ED.app.set({ L: [], R: [], base: [], fm1: ED.app.emptyFm1() }); ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.show('files'); 1");
            await evaluate("ED.pics.ready().then(() => 1)");
        },
        /* the demo disk on the TABLE */
        async demo() {
            await api.drop([api.file("dist/firmware/demo.ptfmdsk")]);
            return api.until("ED.app.st.L.length > 100 && ED.app.st.L.length", 30);
        },
        status: () => evaluate("document.querySelector('#status').textContent"),
        dialog: () => evaluate("(document.querySelector('#dlg') && !document.querySelector('#dlg').hidden ? document.querySelector('#dlg').innerText : '').replace(/\\n+/g, ' | ')"),
        button: label => evaluate(`(() => { const b = [...document.querySelectorAll("#dlg .btn")].find(x => x.textContent === ${JSON.stringify(label)}); if (b) b.click(); return !!b; })()`)
    };
    await api.size(1400, 900);
    try {
        await require(path.join(__dirname, "page", name + ".js"))(api);
    } catch (e) { check(false, name + ": " + (e.stack || e)); }
    check(!p.errors.length, name + ": no exception on the page" + (p.errors.length ? ": " + p.errors.slice(0, 3).join(" | ") : ""));
    await closePage(p);
    console.log(name + ": " + (checks - before) + " checks");
}

(async () => {
    const want = process.argv.slice(2);
    const names = want.length ? want : ORDER;
    for (const n of names) if (!fs.existsSync(path.join(__dirname, "page", n + ".js"))) throw new Error("no scenario " + n);
    fs.mkdirSync(OUT, { recursive: true });
    const chrome = await startChrome();
    try { for (const n of names) await run(n); }
    finally { chrome.kill(); }
    console.log(`page: ${checks - fails} of ${checks} checks passed`);
    process.exit(fails ? 1 : 0);
})().catch(e => { console.log("FAIL: " + (e.stack || e)); process.exit(1); });
