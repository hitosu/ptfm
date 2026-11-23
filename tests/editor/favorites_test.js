/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FAVORITES on the EDITOR page (specs/editor.md, Items: a FAVORITES cartridge; Moving items; specs/files/instruments.md
 * #favorites): the record both ways (3 B a slot, FF an empty one, kept equal with tools/ptfm_records.py), a favourite's
 * reference, moving it to the FM-1 panel taking the cartridges it names (renumbered) and replacing FB's slots (FB the
 * FM-1's one FAVORITES: asked first when it holds favourites; of several moved, the last in the TABLE's order, DOWNLOAD
 * AS DISK the same; an empty one, or one equal to the firmware's FB, giving the firmware's own; FB of a version the page
 * cannot read refused; a disk's UPLOAD putting FB and an earlier PT/FM's FAVORITES back as they are; DELETE of FB; FB
 * moved to the TABLE an item of its own), DELETE of a source leaving the
 * favourite naming it, ALSO WHAT ONLY IT USES keeping what a favourite names, nothing taking a FAVORITES cartridge along.
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
    /* ---- the FM-1 holds one FAVORITES, FB (specs/editor.md, Moving items) ---- */
    const favsOn = items => items.filter(i => i.kind === K.FAVORITES);
    const nameIn = it => R.unpackCart(K.FAVORITES, it.recs[0].payload).name;
    check(rf && rf.num === M.FB && rf.builtin && nameIn(rf) === "FAVORITES" && favsOn(r.st.R).length === 1 && !r.replaced.length &&
          / · FB FAVORITES IS NOW MY FAVORITES$/.test(r.msg) && /^→ FM-1: MY FAVORITES · /.test(r.msg),
          "onto an FM-1 panel whose FB is empty: it replaces FB's slots, FB keeping its name and number, a changed built-in; nothing asked: " + r.msg);
    check(M.toRecords(r.st.R).some(x => x.kind === K.FAVORITES && x.num === M.FB) && !M.toRecords(r.st.R).some(x => x.kind === K.FAVORITES && x.num !== M.FB),
          "WRITE writes it as FB's change");
    /* FB holding favourites: another moved replaces them, after asking */
    const mkFav = (name, slots, link) => ({ id: M.newId(), side: "L", kind: K.FAVORITES, num: null, orig: null, src: "NEW", name, link,
        recs: [{ sub: 0, ver: 1, payload: R.packCart(K.FAVORITES, { name, slots }) }] });
    const favB = mkFav("SECOND", [{ cart: 1, slot: 1 }], { [M.lk("c", 1)]: c2.id });
    const r2 = M.transfer({ L: L.concat([favB]), R: r.st.R, base: [], fm1: {} }, "L", [favB.id], {});
    const fb2 = r2.ok && favsOn(r2.st.R);
    check(r2.ok && fb2.length === 1 && fb2[0].num === M.FB && R.unpackCart(K.FAVORITES, fb2[0].recs[0].payload).slots.filter(Boolean).length === 1 &&
          r2.replaced.length === 1 && r2.replaced[0] === rf.id && r2.fav && r2.fav.had === 2 && r2.fav.now === 1 && r2.fav.from === "SECOND",
          "onto FB holding favourites: replaced after asking (its 2 give way to the 1 of SECOND): " + r2.msg);
    /* the same again: FB holds it already, nothing asked */
    const r3 = M.transfer({ L: L.concat([favB]), R: r2.st.R, base: [], fm1: {} }, "L", [favB.id], {});
    check(r3.ok && !r3.replaced.length && favsOn(r3.st.R).length === 1 && favsOn(r3.st.R)[0] === fb2[0] && /ITEMS? ALREADY THERE/.test(r3.msg),
          "the same moved again: FB holds it already (" + r3.msg + ")");
    /* several in one move: the last in the TABLE's order kept, the others not put on (nor what only they name) */
    const hers = Object.assign({}, a, { id: M.newId(), num: null, orig: 0x41, name: "ONLY HERS" });
    const favC = mkFav("THIRD", [{ cart: 0x41, slot: 0 }], { [M.lk("c", 0x41)]: hers.id });
    const LL = L.concat([favB, favC, hers]);
    const r4 = M.transfer({ L: LL, R: [], base: [], fm1: {} }, "L", [favC.id, fav.id, favB.id], { order: [fav.id, favC.id, favB.id] });
    const fb4 = r4.ok && favsOn(r4.st.R);
    check(r4.ok && fb4.length === 1 && fb4[0].num === M.FB && R.unpackCart(K.FAVORITES, fb4[0].recs[0].payload).slots.length === 1 &&
          / · FB FAVORITES IS NOW SECOND · 2 OTHER FAVORITES NOT PUT ON$/.test(r4.msg) && !r4.st.R.some(i => i.orig === 0x41),
          "several in one move: the last in the TABLE's order (SECOND) kept, the other two not put on, nor what only THIRD names: " + r4.msg);
    /* picked SECOND then THIRD, THIRD above SECOND on the TABLE: the TABLE's order keeps SECOND (the picking's, THIRD) */
    const r5 = M.transfer({ L: LL, R: [], base: [], fm1: {} }, "L", [favB.id, favC.id], { order: [favC.id, favB.id] });
    check(r5.ok && favsOn(r5.st.R).length === 1 && / · FB FAVORITES IS NOW SECOND · 1 OTHER FAVORITES NOT PUT ON$/.test(r5.msg) &&
          !r5.st.R.some(i => i.orig === 0x41), "... the TABLE's order, not the picking's: SECOND, THIRD's cartridge left: " + r5.msg);
    /* DOWNLOAD AS DISK is that move: the same one on FB, the status line's part on it, nothing else of the status line */
    const F = ED.fm1, day = new Date(2026, 10, 15);
    const dd = F.selectionDisk(LL, [favB.id, favC.id], { order: [favC.id, favB.id], date: day });
    check(dd.ok && favsOn(dd.items).length === 1 && favsOn(dd.items)[0].num === M.FB && dd.said === "FB FAVORITES IS NOW SECOND · 1 OTHER FAVORITES LEFT OUT" &&
          !dd.items.some(i => i.orig === 0x41), "DOWNLOAD AS DISK: of two FAVORITES the bottom-most on the TABLE as FB: " + JSON.stringify(dd.said));
    const mkCart = (name, orig) => ({ id: M.newId(), side: "L", kind: K.DX7, num: null, orig, src: "TEST", name, link: {},
        recs: [{ sub: 0, ver: R.VERSION[K.DX7], payload: R.packCart(K.DX7, { name, slots: [ED.defs.newSlot(K.DX7, "BASS")] }) }] });
    const fbBass = mkCart("FB BASS", 0x30), other = mkCart("OTHER", 0x30);
    const ds = F.selectionDisk([fbBass, other], [other.id, fbBass.id], { date: day });
    check(ds.ok && ds.said === "", "... and nothing else: a cartridge named FB BASS renumbered is not said (" + JSON.stringify(ds.said) + ")");
    /* FB of a version this page cannot read: nothing is put over it */
    const dimFb = Object.assign({}, rf, { id: M.newId(), dim: true, recs: [Object.assign({}, rf.recs[0], { ver: 2 })] });
    const r6 = M.transfer({ L, R: [dimFb], base: [], fm1: {} }, "L", [fav.id], {});
    check(!r6.ok && r6.msg === "FB IS OF A VERSION THIS PAGE CANNOT READ: NOTHING MOVED", "onto FB of a version this page cannot read: refused: " + r6.msg);
    /* the firmware's own FB known (as on the page): an empty item onto it is no change; onto FB holding favourites it
     * gives the firmware's own back after asking; an item equal to the firmware's FB is already there */
    const keepBuiltins = M.builtins;
    M.setBuiltins([{ kind: K.FAVORITES, num: M.FB, sub: 0, ver: 1, payload: R.packCart(K.FAVORITES, { name: "FAVORITES", slots: [] }) }]);
    const E = mkFav("EMPTY", [], {}), Ef = mkFav("FAVORITES", [], {});
    const e1 = M.transfer({ L: L.concat([E]), R: [], base: [], fm1: {} }, "L", [E.id], {});
    check(e1.ok && !favsOn(e1.st.R).length && !e1.replaced.length && /1 ITEM ALREADY THERE/.test(e1.msg) &&
          !M.toRecords(e1.st.R).some(x => x.kind === K.FAVORITES), "an empty item onto FB as built in: already there, no record for FB: " + e1.msg);
    const e2 = M.transfer({ L: L.concat([E]), R: r.st.R, base: [], fm1: {} }, "L", [E.id], {});
    check(e2.ok && e2.replaced.length === 1 && e2.replaced[0] === rf.id && !favsOn(e2.st.R).length && !M.toRecords(e2.st.R).some(x => x.kind === K.FAVORITES) &&
          /^→ FM-1: EMPTY · FB FAVORITES IS NOW EMPTY$/.test(e2.msg),
          "an empty item onto FB holding favourites: asked; FB the firmware's own again, no record; the status line saying so: " + e2.msg);
    const e3 = M.transfer({ L: L.concat([Ef]), R: [], base: [], fm1: {} }, "L", [Ef.id], {});
    check(e3.ok && !favsOn(e3.st.R).length && /1 ITEM ALREADY THERE/.test(e3.msg), "an item equal to the firmware's FB: already there: " + e3.msg);
    M.setBuiltins(keepBuiltins);
    /* a disk's UPLOAD puts the disk back as it is: a backup holding FB and an earlier PT/FM's 07 gives FB and 07 (07 as it
     * is read), and a WRITE then has nothing to write or delete */
    const pk = (num, name, slot) => ({ kind: K.FAVORITES, num, sub: 0, ver: 1, payload: R.packCart(K.FAVORITES, { name, slots: [{ cart: 0xF0, slot }] }) });
    const read = M.fromRecords([pk(M.FB, "FAVORITES", 1), pk(0x07, "MY FAVORITES", 2)], { side: "R", src: "FM-1" }).items;
    const up = F.diskItems(F.diskOf(read), "R", "BACKUP.PTFMDSK").items;
    const pe = M.pending({ R: up, base: read });
    check(favsOn(up).map(i => M.H2(i.num) + (i.builtin ? " CHANGED" : "")).sort().join() === "07,FB CHANGED" && pe.count === 0 && !pe.removed.length,
          "UPLOAD DISK of a backup with FB and an earlier PT/FM's 07: both back as they were; WRITE writes and deletes nothing (" + pe.count + ")");
    /* DELETE of FB on the FM-1 panel (BACK TO BUILT-IN): the firmware's own back, WRITE deleting FB's record */
    const bb = M.remove(Object.assign({}, r.st, { base: r.st.R }), "R", [rf.id], false);
    check(!favsOn(bb.st.R).length && M.pending(bb.st).removed.some(x => x.id === rf.id), "DELETE of FB: back to the firmware's own, WRITE deleting its record");
    /* FB moved to the TABLE: a FAVORITES item of its own, named FAVORITES (as any built-in copied there) */
    const back = M.transfer(r.st, "R", [rf.id], { builtins: [rf] });
    const bl = back.ok && back.st.L.filter(i => i.kind === K.FAVORITES && !L.includes(i));
    check(back.ok && bl.length === 1 && !bl[0].builtin && bl[0].num == null && M.nameOf(bl[0], back.st.L) === "FAVORITES",
          "FB moved to the TABLE: a FAVORITES item of its own, named FAVORITES: " + back.msg);
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
