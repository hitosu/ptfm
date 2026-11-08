/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FAVORITES on the EDITOR page (specs/editor.md, Items: a FAVORITES cartridge; specs/files/instruments.md#favorites):
 * the record both ways (3 B a slot, FF an empty one, kept equal with tools/ptfm_records.py), a favourite's reference,
 * moving it to the FM-1 panel taking the cartridges it names (renumbered), DELETE of a source leaving the favourite
 * naming it, ALSO WHAT ONLY IT USES keeping what a favourite names, nothing taking a FAVORITES cartridge along.
 *   node tests/editor/favorites_test.js */
"use strict";
const { execFileSync } = require("child_process");
const path = require("path");
const { ED, S, ROOT, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model;

/* ---- the record ---- */
const b = R.packCart(K.FAVORITES, { name: "MY FAVORITES", slots: [{ cart: 0x0B, slot: 5 }, null, { cart: 0xF0, slot: 3 }, null] });
check(b.length === 10 + 3 * 3 && b[9] === 3 && b[10] === 0x0B && b[11] === 5 && b[13] === 0 && b[14] === 0xFF && b[16] === 0xF0 && b[17] === 3,
      "the record: name, slots stored up to the last used, 3 B each (cartridge, slot, spare), FF an empty one");
const u = R.unpackCart(K.FAVORITES, b);
check(u.name === "MY FAVORITES" && u.slots.length === 3 && u.slots[0].cart === 0x0B && u.slots[1] === null && u.slots[2].slot === 3, "read back");
{   /* the Python twin packs the same bytes */
    const py = execFileSync("python3", ["-I", "-c", "import sys; sys.path.insert(0, sys.argv[1]); import ptfm_records as P; " +
        "print(P.pack_cartridge(0x07, 'MY FAVORITES', [{'cart': 0x0B, 'slot': 5}, None, {'cart': 0xF0, 'slot': 3}, None]).hex())",
        path.join(ROOT, "tools")]).toString().trim();
    check(py === Buffer.from(b).toString("hex"), "tools/ptfm_records.py packs the same bytes");
}

/* ---- on the panels ---- */
(async () => {
    const disk = S.parseData(demo());
    const T = M.fromRecords(disk.files, { side: "L", src: "DEMO.PTFMDSK" });
    const L0 = T.items, carts = L0.filter(i => i.kind === K.SAMPLER || i.kind === K.FUSION);
    const a = carts[0], c2 = carts[1];
    const fav = { id: M.newId(), side: "L", kind: K.FAVORITES, num: null, orig: null, src: "NEW", name: "MY FAVORITES",
        link: { [M.lk("c", 0)]: a.id, [M.lk("c", 1)]: c2.id },
        recs: [{ sub: 0, ver: 1, payload: R.packCart(K.FAVORITES, { name: "MY FAVORITES", slots: [{ cart: 0, slot: 0 }, { cart: 1, slot: 0 }] }) }] };
    const L = L0.concat([fav]);
    const refs = M.refs(fav);
    check(refs.length === 2 && refs.every(f => f.sp === "c" && f.fav), "a favourite names its source's cartridge (a reference)");
    /* moved to the FM-1 panel: the cartridges it names go along, renumbered */
    let st = { L, R: [], base: [], fm1: {} };
    const r = M.transfer(st, "L", [fav.id], {});
    const rf = r.ok && r.st.R.find(i => i.kind === K.FAVORITES);
    const nums = rf && R.unpackCart(K.FAVORITES, rf.recs[0].payload).slots.map(s => s.cart);
    check(r.ok && rf && r.st.R.some(i => i.num === nums[0] && i.kind === a.kind) && r.st.R.some(i => i.num === nums[1] && i.kind === c2.kind),
          "moved to the FM-1 panel: the cartridges it names along, its references their numbers (" + (r.msg || "") + ")");
    /* DELETE of a source: the favourite stays, naming it */
    const ua = M.usersAll(L, [a.id]);
    check(!ua.ids.includes(fav.id), "DELETE of a cartridge a favourite names: the FAVORITES cartridge does not go along");
    const gone = M.remove({ L, R: [] }, "L", [a.id], false);
    check(gone.st.L.some(i => i.id === fav.id) && gone.st.L.find(i => i.id === fav.id).link[M.lk("c", 0)] === a.id,
          "after it: the favourite still names it (MISSING)");
    /* a song deleted with what only it uses: a cartridge a favourite names is still used */
    const song = L.find(i => i.kind === K.SONG && M.closure(L, [i.id]).has(a.id));
    if (song) {
        const ex = M.exclusive(L, [song.id]);
        check(!ex.has(a.id), "ALSO WHAT ONLY IT USES keeps a cartridge a favourite names");
    } else check(true, "(no song of the demo disk uses that cartridge: skipped)");
    /* a FAVORITES cartridge's own DELETE with ALSO WHAT ONLY IT USES: its sources stay, even one only it names */
    const lone = Object.assign({}, a, { id: M.newId(), num: 0x40, name: "LONE" });
    const fav2 = Object.assign({}, fav, { id: M.newId(), link: { [M.lk("c", 0x40)]: lone.id } });
    const L2 = L0.concat([lone, fav2]);
    check(!M.exclusive(L2, [fav2.id]).has(lone.id), "a FAVORITES cartridge's DELETE never takes a source only it names");
    /* nothing takes a FAVORITES cartridge along */
    check(!M.closure(L, L0.filter(i => !i.owner).map(i => i.id)).has(fav.id), "moving everything else never takes a FAVORITES cartridge along");
    check(M.groupOf(fav) === "CARTRIDGES" && M.ENGINE[K.FAVORITES] === "FAVORITES", "its group CARTRIDGES, its kind FAVORITES");
    done("favorites");
})();
