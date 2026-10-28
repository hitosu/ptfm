/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Seeding end to end: fw/web/seed.js, the uploader, against the device's seeding code built natively
 * (seed_bridge.c on a fake flash). The demo disk seeded into an empty FM-1 and read back record for
 * record; seeding again (nothing to do); the device's own records deleted; one changed record; power
 * cuts while writing (the library before stays whole); refusals; the time-out.
 *   node seed_test.js BRIDGE DISK.ptfmdsk */
"use strict";
const fs = require("fs");
const { spawn } = require("child_process");
const S = require("../web/seed.js");

const [bridgePath, diskPath] = process.argv.slice(2);
let checks = 0, fails = 0;
function check(c, msg) { checks++; if (!c) { fails++; console.log("FAIL " + msg); } }

/* ---- the bridge: a command, then its answer's lines until "." ---- */
const br = spawn(bridgePath, [], { stdio: ["pipe", "pipe", "inherit"] });
let buf = "", listeners = [], waiting = [];
const onMsg = bytes => { for (const f of listeners) f(bytes); };
br.stdout.on("data", d => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i); buf = buf.slice(i + 1);
        if (line.startsWith("S ")) {
            const bytes = Uint8Array.from(line.slice(2).match(/../g).map(h => parseInt(h, 16)));
            setImmediate(() => onMsg(bytes));
        } else if (line === ".") {
            const w = waiting.shift();
            if (w) w.resolve(w.lines);
        } else if (waiting.length) {
            if (line.startsWith("E ")) { check(false, line); }
            waiting[0].lines.push(line);
        }
    }
});
const DEBUG = !!process.env.SEED_DEBUG, T0 = Date.now();
function cmd(line) {
    if (DEBUG) console.log((Date.now() - T0) + " > " + line.slice(0, 40));
    return new Promise(resolve => { waiting.push({ lines: [], resolve }); br.stdin.write(line + "\n"); });
}
br.on("exit", (code, sig) => { if (waiting.length) console.log("FAIL the bridge stopped: " + (sig || code)); });
const hex = b => Array.from(b, x => (x < 16 ? "0" : "") + x.toString(16).toUpperCase()).join("");
let sent = 0, biggest = 0;
const transport = {
    send(bytes) { sent++; biggest = Math.max(biggest, bytes.length); cmd("M " + hex(bytes)); },
    onMessage(cb) { listeners.push(cb); }
};

async function dump() {
    const lines = await cmd("DUMP");
    return lines.filter(l => l.startsWith("R ")).map(l => {
        const [kind, num, sub, ver, len, crc] = l.slice(2).split(" ").map(x => parseInt(x, 16));
        return { kind, num, sub, ver, len, crc: crc >>> 0 };
    });
}
const key = r => r.kind << 16 | r.num << 8 | r.sub;
function same(dev, files, what) {
    const want = new Map(files.map(f => [key(f), f]));
    check(dev.length === files.length, what + ": the device holds " + dev.length + " records, the disk " + files.length);
    let bad = 0;
    for (const r of dev) {
        const f = want.get(key(r));
        if (!f || f.ver !== r.ver || f.len !== r.len || f.crc !== r.crc) bad++;
    }
    check(bad === 0, what + ": " + bad + " records differ from the disk");
}
function changed(data, n) {               /* a copy of the disk with n patterns' payloads changed */
    const files = data.files.map(f => Object.assign({}, f));
    let k = 0;
    for (const f of files) {
        if (f.kind !== 0x20 || k >= n) continue;
        f.payload = Uint8Array.from(f.payload);
        f.payload[f.payload.length - 1] ^= 0x01;
        f.crc = S.crc32(f.payload);
        k++;
    }
    return { kind: "disk", files, notes: null, problems: [] };
}

async function seed(data, sess, what) {
    const L = await sess.hello();
    const P = S.plan(L, data);
    check(P.ok, what + ": the plan: " + P.problems.join("; "));
    if (!P.ok || P.upToDate) return { L, P, res: null };
    let last = 0;
    const res = await sess.write(P, p => { last = p.done; });
    check(last === P.blocks.length, what + ": progress reached every block");
    return { L, P, res };
}

async function main() {
    const disk = new Uint8Array(fs.readFileSync(diskPath));
    const data = S.parseData(disk);
    check(data.kind === "disk" && !data.problems.length, "the demo disk reads: " + data.problems.join("; "));
    check(data.notes && data.notes.length > 1000, "its notes (the credits) are there");
    const sess = S.session(transport, { timeout: 1500 });

    /* the 7-in-8 packing both ways */
    const all = Uint8Array.from({ length: 300 }, (_, i) => (i * 37) & 0xFF);
    check(hex(S.unpack7(Uint8Array.from(S.pack7(all)))) === hex(all), "7 bytes in 8, both ways");

    /* an empty FM-1 */
    let L = await sess.hello();
    check(L.records.length === 0 && L.lastSave === 0, "an empty device: no records");
    check(L.regions.length === 2 && L.regions[0].start === 0x093000 && L.regions[1].start === 0x0E5000 &&
          L.regions.every(g => g.area !== "rom"), "two regions, the user area's (FWS is never written)");
    check(L.reserve === 16 && L.maxData === 512 && L.accepts.length === 13, "the reserve, the message size, the kinds");
    check(L.builtins.length === 5 && [0x20, 0x21].every(k => L.builtins.some(b => b.kind === k && b.num === 0xFE)),
          "the built-ins reported, the firmware's own among them (the EDITOR's pattern FE, the keys' song FE)");
    check(L.fwText === "TEST 1", "the firmware's version: " + JSON.stringify(L.fwText));
    let P = S.plan(L, data);
    check(P.ok && P.write.length === data.files.length && !P.del.length, "the plan writes every file");
    console.log("  plan for an empty device:\n    " + P.summary.join("\n    "));
    const audioBlocks = new Set();
    for (const b of P.blocks) if (b.block * 4096 < 0x0E0000) audioBlocks.add(b.block);
    check(P.space.after >= 16, "the reserve is kept: " + P.space.after + " blocks left");
    const t0 = Date.now();
    let res = await sess.write(P, () => {});
    console.log("  seeded " + P.blocks.length + " blocks in " + (Date.now() - t0) + " ms, " + sent + " messages, the biggest " + biggest + " B");
    check(biggest <= 640, "every message within 640 bytes");
    check(res.records === data.files.length && res.bad === 0, "the device mounted " + res.records + " records, " + res.bad + " bad");
    same(await dump(), data.files, "after seeding");
    await cmd("BOOT");
    same(await dump(), data.files, "after a power-cycle");

    /* again: nothing to do */
    L = await sess.hello();
    P = S.plan(L, data);
    check(P.ok && P.upToDate, "seeding the same disk again: nothing to write");

    /* the device's own work: deleted, and the plan says so */
    let r = await cmd("SAVE 30 0 0 1 0102030405");
    check(r[0] === "R 0", "a settings record saved on the device");
    L = await sess.hello();
    P = S.plan(L, data);
    check(P.ok && P.del.length === 1 && P.del[0].kind === 0x30, "the plan deletes the device's settings");
    check(P.summary.some(s => s.startsWith("DELETE:") && s.includes("SETTINGS 00")), "and names it");
    res = await sess.write(P, () => {});
    same(await dump(), data.files, "the device's settings deleted");

    /* one pattern changed: only it is written */
    const one = changed(data, 1);
    L = await sess.hello();
    P = S.plan(L, one);
    check(P.ok && P.write.length === 1 && P.keep.length === data.files.length - 1 && P.blocks.length === 1, "one change, one block");
    await sess.write(P, () => {});
    same(await dump(), one.files, "one pattern changed");
    await cmd("BOOT");
    same(await dump(), one.files, "one pattern changed, after a power-cycle");

    /* the backup: the library read back as a disk */
    L = await sess.hello();
    let bytesRead = 0;
    const backup = await sess.backup(L, p => { bytesRead = p.done; });
    const back = S.parseData(backup);
    check(!back.problems.length, "the backup reads as a disk: " + back.problems.join("; "));
    same(back.files.map(f => ({ kind: f.kind, num: f.num, sub: f.sub, ver: f.ver, len: f.len, crc: f.crc })), one.files, "the backup");
    console.log("  backup: " + back.files.length + " files, " + bytesRead + " bytes read, a disk of " + backup.length + " B");
    /* the EDITOR's own pattern, kept by SAVE on the device: in the backup, and a plan putting the backup back (a
     * restore after an install) takes it, as a changed built-in */
    r = await cmd("SAVE 20 FE 0 1 013F010100803C00");
    check(r[0] === "R 0", "the EDITOR's own pattern saved on the device");
    L = await sess.hello();
    const withFe = S.parseData(await sess.backup(L, () => {}));
    check(withFe.files.some(f => f.kind === 0x20 && f.num === 0xFE), "the backup holds it");
    P = S.plan(L, { files: withFe.files.filter(f => !(f.kind === 0x20 && f.num === 0xFE)), problems: [] });
    check(P.ok && P.del.length === 1 && P.del[0].num === 0xFE, "a disk without it: deleted (the firmware's empty one back)");
    await sess.write(P, () => {});
    L = await sess.hello();
    P = S.plan(L, withFe);
    check(P.ok && P.write.length === 1 && P.write[0].num === 0xFE, "the backup put back: it is written again (" + P.problems.join("; ") + ")");
    await sess.write(P, () => {});
    same(await dump(), withFe.files, "the backup put back, the EDITOR's pattern with it");
    L = await sess.hello();
    P = S.plan(L, back);
    await sess.write(P, () => {});
    same(await dump(), one.files, "as before it");
    /* a disk made again from the demo disk's own files: byte for byte the packer's */
    const again = S.makeDisk(data.files, data.notes);
    check(again.length === disk.length && again.every((x, i) => x === disk[i]), "makeDisk gives the packer's bytes for the same files");

    /* power cuts while seeding: the library before stays whole (the new records went into free blocks) */
    const five = changed(data, 5);
    const quick = S.session(transport, { timeout: 300, blockTimeout: 300, endTimeout: 300 });
    let cutsDone = 0;
    for (const [cut, torn] of [[0, 0], [1, 0], [2, 1], [3, 0], [5, 1], [7, 0], [9, 1], [12, 0]]) {
        L = await quick.hello();
        P = S.plan(L, five);
        if (P.upToDate) break;
        await cmd("CUT " + cut + " " + torn);
        let failed = false;
        try { await quick.write(P, () => {}); } catch (e) { failed = true; }
        await cmd("BOOT");
        const now = await dump();
        if (failed) { cutsDone++; same(now, one.files, "cut after " + cut + " flash operations" + (torn ? " (torn)" : "")); }
        else same(now, five.files, "cut after " + cut + " (the seeding had finished)");
    }
    check(cutsDone >= 5, "seedings cut: " + cutsDone);
    console.log("  " + cutsDone + " seedings cut short: the library before stayed whole");
    /* and seeding again mends it */
    L = await sess.hello();
    P = S.plan(L, five);
    if (!P.upToDate) await sess.write(P, () => {});
    same(await dump(), five.files, "seeded again after the cuts");

    /* every sample changed: too much for the free blocks alone, so blocks whose records all go are written over */
    const allAudio = { kind: "disk", problems: [], files: five.files.map(f => {
        if (f.kind !== 0x10) return f;
        const p = Uint8Array.from(f.payload); p[0] ^= 0x80;
        return Object.assign({}, f, { payload: p, crc: S.crc32(p) });
    }) };
    L = await sess.hello();
    P = S.plan(L, allAudio);
    check(P.ok && P.rewrites > 0, "a rewrite: " + P.rewrites + " blocks written over; " + P.problems.join("; "));
    console.log("  every sample changed:\n    " + P.summary.join("\n    "));
    if (P.ok) await sess.write(P, () => {});
    same(await dump(), allAudio.files, "every sample changed");
    await cmd("BOOT");
    same(await dump(), allAudio.files, "every sample changed, after a power-cycle");
    L = await sess.hello();
    P = S.plan(L, five);
    if (P.ok && !P.upToDate) await sess.write(P, () => {});
    same(await dump(), five.files, "back again");

    /* refusals */
    await cmd("UNSAVED 1");
    let code = null;
    try { await sess.hello(); } catch (e) { code = e.code; }
    check(code === S.RESULT.UNSAVED, "edits unsaved: SAVE OR REVERT FIRST (" + code + ")");
    await cmd("UNSAVED 0");
    r = await cmd("EDIT 21 5 0 1 0A0B0C");
    check(r[0] === "R 0", "an edit waits in the store's edit memory");
    code = null;
    try { await sess.hello(); } catch (e) { code = e.code; }
    check(code === S.RESULT.UNSAVED, "an edit unsaved: SAVE OR REVERT FIRST (" + code + ")");
    await cmd("REVERT");
    L = await sess.hello();
    const withOne = (kind, num, label) => ({ kind: "disk", files: five.files.concat([{ kind, num, sub: 0, ver: 1, len: 4,
        crc: S.crc32(Uint8Array.of(1, 2, 3, 4)), payload: Uint8Array.of(1, 2, 3, 4), label }]), problems: [] });
    P = S.plan(L, withOne(0x01, 0xF0, "DX7 CART F0"));
    check(!P.problems.some(p => p.includes("F0-FF")), "a built-in number taken (the built-in DX7 F0's change: the user area)");
    P = S.plan(L, withOne(0x04, 0xF0, "SAMPLER CART F0"));
    check(!P.ok && P.problems.some(p => p.includes("F0-FF")), "a built-in number of another kind (a sampler at the DX7's F0) refused");
    P = S.plan(L, withOne(0x01, 0xF3, "DX7 CART F3"));
    check(!P.ok && P.problems.some(p => p.includes("F0-FF")), "a built-in number with nothing built in there (F3 here) refused");
    const v2 = { kind: "disk", files: [Object.assign({}, five.files[0], { ver: 9 })], problems: [] };
    P = S.plan(L, v2);
    check(!P.ok && P.problems.some(p => p.includes("version 9")), "a version the device does not take refused");
    /* more than the reserve leaves: planned to the user area's last block, LOW SPACE saying what fewer free blocks
       cost the device's saves; more than the user area holds: refused */
    const big = n => {
        const d = { kind: "disk", files: five.files.slice(), problems: [] };
        for (let i = 0; i < n; i++) d.files.push({ kind: 0x10, num: 0x40, sub: i, ver: 1, len: 8000,
            crc: S.crc32(new Uint8Array(8000)), payload: new Uint8Array(8000), label: "SAMPLE" });
        return d;
    };
    let n = 1;                                          /* (more till the reserve is not kept: ~2 blocks a sample, 16 kept) */
    P = S.plan(L, big(n));
    while (P.ok && !P.low && n < 120) P = S.plan(L, big(++n));
    check(P.ok && P.low && P.space.after < L.reserve &&
          P.summary.some(t => t.startsWith("LOW SPACE: only " + P.space.after * 4 + " KB will be free")),
          n + " samples, more than the reserve leaves: planned to the last block, said (" + P.low + ")");
    P = S.plan(L, big(n + 30));
    check(!P.ok && P.problems.some(p => p.includes("does not fit")), "too much for the user area: " + P.problems.join("; "));

    /* CLEAN INSTALL's seeding: no files, every block of the user area written empty; a region not the user area's
       (an area-1 one, which a layout may list) never written */
    {
        const odd = Object.assign({}, L, { regions: L.regions.concat([{ start: 0x082000, end: 0x092000, area: "rom", inPlace: false }]) });
        const user = b => L.regions.some(g => b.block * 4096 >= g.start && b.block * 4096 < g.end);
        const nuser = L.regions.reduce((n, g) => n + (g.end - g.start) / 4096, 0);
        P = S.planClean(odd, { files: [], problems: [] });
        check(P.ok && !P.write.length && P.blocks.length === nuser && P.blocks.every(user) && P.blocks.every(b => b.image.every(x => x === 0xFF)),
              "planClean with no files: every block of the user area empty (" + P.blocks.length + "), nothing else written");
    }
    /* a SAVE on the FM-1 between the plan and the writing: BEGIN refused (CHANGED), the plan made again works */
    L = await sess.hello();
    P = S.plan(L, one);
    check(P.ok && !P.upToDate, "a plan to write");
    r = await cmd("SAVE 21 7 0 1 0102");
    code = null;
    try { await sess.write(P, () => {}); } catch (e) { code = e.code; }
    check(code === S.RESULT.CHANGED, "a save since the layout: BEGIN refused, the layout changed (" + code + ")");
    const keep = await dump();
    check(keep.some(x => x.kind === 0x21 && x.num === 7), "nothing written: the device's save stands");
    L = await sess.hello();
    P = S.plan(L, one);
    await sess.write(P, () => {});
    same(await dump(), one.files, "planned again and written");
    /* the player: told all sound off at BEGIN and at the end, and that the store changed before it runs again */
    const pl = (await cmd("PLAYER"))[0].split(" ").map(Number);
    check(pl[1] >= 2 && pl[2] >= 1 && pl[3] === pl[2], "the player stopped at BEGIN and told before the end: " + pl.slice(1).join(" "));

    /* the time-out: BEGIN, then nothing for 10 s; the device mounts what is there */
    const next = n => { const s = n + 1; return [s & 255, s >> 8 & 255, s >> 16 & 255, s >>> 24, 1, 0]; };
    L = await sess.hello();
    transport.send(S.message(S.CMD.BEGIN, next(L.lastSave)));
    await new Promise(r => setTimeout(r, 50));
    let a = await cmd("ACTIVE");
    check(a[0] === "A 1", "seeding under way after BEGIN");
    await cmd("T 9000");
    a = await cmd("ACTIVE");
    check(a[0] === "A 1", "still after 9 s");
    await cmd("T 2000");
    a = await cmd("ACTIVE");
    check(a[0] === "A 0", "ended after 10 s without a message");
    same(await dump(), one.files, "the library after the time-out");

    /* a CHUNK outside the regions */
    const chunkBad = [0x40, 0x00, 0, 0, 1, 2, 3];   /* block 0x40: the firmware */
    L = await sess.hello();
    transport.send(S.message(S.CMD.BEGIN, next(L.lastSave)));
    await new Promise(r => setTimeout(r, 50));
    let got = null;
    listeners.push(m => { const p = S.parseMessage(m); if (p.cmd === S.CMD.ACK && p.data[0] === S.CMD.CHUNK) got = p.data[3]; });
    transport.send(S.message(S.CMD.CHUNK, chunkBad));
    await new Promise(r => setTimeout(r, 100));
    check(got === S.RESULT.BAD_BLOCK, "a block outside the regions refused (" + got + ")");
    got = null;
    transport.send(S.message(S.CMD.CHUNK, [0x88, 0x00, 0, 0, 1, 2, 3]));   /* block 0x88: FWS's free part */
    await new Promise(r => setTimeout(r, 100));
    check(got === S.RESULT.BAD_BLOCK, "a block in FWS's free part refused (" + got + ")");
    transport.send(S.message(S.CMD.ABORT, []));
    await new Promise(r => setTimeout(r, 100));

    br.stdin.write("Q\n");
    console.log("seed: " + checks + " checks, " + fails + " failed");
    process.exit(fails ? 1 : 0);
}
main().catch(e => { console.log("FAIL", e); process.exit(1); });
