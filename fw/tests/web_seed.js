/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Seeding into the emulator, as its UPLOAD DEMO DISK button does: fw/web/seed.js (the install page's module)
 * talks to the firmware's own seeding code in the WebAssembly, through calls instead of USB-MIDI, with
 * the device's time running between messages. Checks that every block is answered, that the FM-1 mounts
 * what was written with nothing bad, and that its layout afterwards holds exactly the disk (a second
 * plan finds nothing to write); then keeps the flash for the session test, and clears the user area as the
 * debug page's CLEAR USER DATA does (session.clear): no record left, the FM-1 running on.
 *
 *   node web_seed.js WASM DISK.ptfmdsk FLASH_OUT */
"use strict";
const fs = require("fs"), path = require("path");
const S = require(path.join(__dirname, "..", "web", "seed.js"));
const [wasmPath, diskPath, flashOut] = process.argv.slice(2);

let fake = 0;
const inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)),
    { env: { web_clock_us: () => (fake += 1) } });
const ex = inst.exports;
const mem = () => ex.memory.buffer;
ex.web_flash_reset();
ex.web_init();

let cb = null, quanta = 0;
const transport = {
    send(bytes) {
        new Uint8Array(mem(), ex.web_sysex_stage(), bytes.length).set(bytes);
        if (ex.web_sysex_push(bytes.length) < 0) throw new Error("the device's SysEx queue is full");
    },
    onMessage(fn) { cb = fn; },
};
let running = true;
(async function pump() {                       /* the device's time: a quantum, its SysEx out, the next */
    while (running) {
        ex.web_render(128);
        quanta++;
        const n = ex.web_sysex_out_len();
        if (n) {
            const all = new Uint8Array(mem(), ex.web_sysex_out(), n).slice();
            ex.web_sysex_out_clear();
            let s = 0;
            for (let i = 0; i < all.length; i++)
                if (all[i] === 0xF7) { if (cb) cb(all.slice(s, i + 1)); s = i + 1; }
        }
        await new Promise(r => setImmediate(r));
    }
})();

(async () => {
    const data = S.parseData(new Uint8Array(fs.readFileSync(diskPath)));
    if (data.problems && data.problems.length) throw new Error("the disk: " + data.problems.join("; "));
    const ses = S.session(transport, { timeout: 20000 });
    const layout = await ses.hello();
    const P = S.plan(layout, data);
    if (!P.ok) throw new Error("the plan: " + P.problems.join("; "));
    let blocks = 0;
    const r = await ses.write(P, p => { blocks = p.done; });
    const secs = (quanta * 128 / 44100).toFixed(1);
    console.log(`seeded ${path.basename(diskPath)}: ${data.files.length} files, ${blocks} of ${P.blocks.length} blocks, ` +
        `the FM-1 mounted ${r.records} records, ${r.bad} bad, save ${r.lastSave}; ${secs} s of the device's time`);
    if (r.bad) throw new Error(r.bad + " bad records after the mount");
    const again = S.plan(await ses.hello(), data);
    if (!again.upToDate) throw new Error("the FM-1 does not hold the disk afterwards: " + again.summary.join(" / "));
    console.log("a second plan: nothing to write (the FM-1 holds exactly the disk)");
    const kept = new Uint8Array(mem(), ex.web_flash(), 1 << 20).slice();
    /* then the debug page's CLEAR USER DATA on the running FM-1 (the flash above is the one kept) */
    const c = await ses.clear();
    const after = await ses.hello();
    const t0 = quanta;
    while (quanta < t0 + 700) await new Promise(r => setTimeout(r, 1));       /* (a second of its time on the empty library) */
    if (c.records || c.bad || after.records.length) throw new Error(`CLEAR USER DATA left ${c.records} records, ${c.bad} bad`);
    console.log("CLEAR USER DATA: the FM-1 erased it all and runs on, 0 records");
    ses.close();
    running = false;
    fs.writeFileSync(flashOut, Buffer.from(kept));
})().catch(e => { running = false; console.error("SEEDING FAILED:", e.message); process.exit(1); });
