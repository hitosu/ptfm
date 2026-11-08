// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
/* Samples as records of their own (specs/files/instruments.md#samples; specs/ui/screens/instruments.md, The samples, the
 * SEL menu's REPLACE SAMPLE, CREATE and INIT, the cartridge list's ALL SAMPLES line; specs/ui/saving.md#revert;
 * specs/ui/screens/settings.md, the tools; status.md): numbered 00-FF, named by any sampler's instruments. The
 * firmware's own checks (fw/tests/ui3_test.c: test_samples, test_samples_none, test_samples_flash,
 * test_samples_tools), step for step where the playground has the same: THE SAMPLES list three ways and its line, its
 * keys, DELETE across samplers with its warning, (DELETED) rows, K1 on SAMPLE, REVERT bringing a deletion back, DELETE
 * in edit mode, PASTE after a deletion, THE SAMPLE IS MISSING, REPLACE SAMPLE, CREATE and EDIT on an empty sampler
 * slot, INIT, CLONE and MOVE of a sampler, NO SAMPLES ON THE FM-1, an old sampler's number taken, CHECK REFERENCES,
 * CLEAN UP, the storage figures. Each part undoes its own edits. Run by the harness:
 *   node tests/playground/harness.js tests/playground/samples.js */
"use strict";
module.exports = async t => {
    const { U, M, ok } = t, D = t.PTFM.draw, A = t.PTFM.audio, Y0 = U.BODY_Y;
    const hex = v => M.hex(v);
    /* the checks count the disk's samples and samplers from 0 and 1, its numbers from A0 (specs/files/disk.md#the-demo-disk) */
    const CART = c => c >= 1 && c <= 5 ? 0x9F + c : c, SMP = n => (n + 0xA0) & 255;
    const slotSample = (c, s) => { const x = M.cart(CART(c)); return x && x.slots[s] ? (x.slots[s].p.sample - 0xA0) & 255 : -1; };
    const sampleOn = n => !!(M.state.samples && M.state.samples[SMP(n)]);
    const going = n => !sampleOn(n) && M.savedThing("sample", SMP(n)) != null;     /* (its deletion waits for SAVE) */
    const nSamples = () => (M.state.samples || []).filter(Boolean).length;
    const listOn = () => t.top() === "list";
    const samplesOn = () => listOn() && !!U.top().samples;
    const menuOn = () => t.top() === "menu";
    const editMode = () => U.screen.editMode ? U.screen.editMode() : -1;
    const asking = () => t.top() === "question" ? U.top().lines : [];
    /* a text as drawn: the frame's rows y..y+10 over the text's width from x, its ink against the text drawn alone on
     * black, pixel for pixel, some of it inked (as the firmware's text_as) */
    function textAs(x, y, s, col) {
        t.frame();
        const fb = D.fb, w = s.length * 6, frame = fb.slice();
        D.px(0, 0, col);
        const ink = fb[0];
        D.rect(x, y, w, 11, D.BLACK);
        D.text(x, y, D.F.T, s, col);
        const alone = fb.slice();
        fb.set(frame);
        let same = true, inked = 0;
        for (let j = 0; j < 11; j++) for (let i = x; i < x + w && i < 240; i++) {
            const a = alone[(y + j) * 240 + i] === ink, f = frame[(y + j) * 240 + i] === ink;
            inked += a;
            if (a !== f) same = false;
        }
        return same && inked > 8;
    }
    const countPx = (x0, y0, x1, y1, col) => {
        t.frame();
        const fb = D.fb, keep = fb[0];
        D.px(0, 0, col);
        const ink = fb[0];
        fb[0] = keep;
        let n = 0;
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) n += fb[y * 240 + x] === ink;
        return n;
    };
    /* SEL's menu, that item (in a group too); not there: the menu closed, false */
    async function pick(item) {
        await t.tap("SEL");
        const m = U.top();
        if (!m || m.kind !== "menu") return false;
        m.goto(item);
        if (m.sel < 0 || m.list()[m.sel] !== item) { await t.tap("HOME"); return false; }
        await t.tap("SEL");
        return true;
    }
    async function menuItems() {                            /* SEL's menu: its items joined by ",", then closed */
        await t.tap("SEL");
        const m = U.top(), s = m && m.kind === "menu" ? m.items.join(",") : "";
        if (m && m.kind === "menu") await t.tap("HOME");
        return s;
    }
    const smY = (row, sel, n) => Y0 + 2 + 18 + (row - Math.max(0, Math.min(sel - 6, n - 14))) * 13;
    const allLineY = () => {                                /* the ALL SAMPLES line's y, the cursor on it */
        const n = M.state.carts.length, top = Math.max(0, Math.min(n - 6, n + 1 - 13));
        return Y0 + 2 + 30 + (n - top) * 13;
    };
    const cartListTo = async line => { await t.turn("SELECT", -300); await t.turn("SELECT", line); };
    async function allSamples() {                           /* the cartridge list's ALL SAMPLES line, ENV: THE SAMPLES list */
        await t.tap("ENV"); U.top().sel = M.state.carts.length; await t.tap("ENV");
    }
    async function rowTo(r) { U.top().sel = r; await t.sleep(20); }
    /* the keys' notes heard: the reference and the instrument as it was sent */
    const heard = [], on0 = A.noteOn;
    A.noteOn = (id, time, midi, ref, ...rest) => {
        heard.push({ id, midi, ref, inst: structuredClone(M.inst(ref)) });
        return on0(id, time, midi, ref, ...rest);
    };
    const c4 = async () => { heard.length = 0; await t.key(7, 60); return heard[heard.length - 1] || null; };
    const ENV0 = { rates: [99, 99, 99, 80], levels: [99, 99, 99, 0] };
    /* a new instrument on sample n, whole (specs/files/instruments.md#sample-instruments), named name */
    const isNew = (ins, n, name) => {
        const s = M.state.samples && M.state.samples[SMP(n)];
        return !!(ins && s && ins.p.sample === SMP(n) && ins.p.start === 0 && ins.p.end === s.length && ins.p.loop === "OFF" &&
                  ins.p.loopStart === 0 && ins.p.loopEnd === s.length && ins.p.base === 60 && ins.p.tune === 0 &&
                  ins.p.volume === 64 && ins.level === 0 && JSON.stringify(ins.p.env) === JSON.stringify(ENV0) && ins.name === name);
    };
    const step = async (name, fn) => { try { await fn(); } catch (e) { ok(false, name + ": " + (e.stack || e)); } };
    M.save();

    /* ---------------------------------------------------------------- the list three ways, DELETE, REVERT ... --- */
    await step("the samples", async () => {
        ok(nSamples() === 33 && M.state.carts.every(c => !c.samples) && M.sample && M.sample(0xA0).name === "KICK" &&
           M.sample(0xBD).name === "WOBBLE 140" && slotSample(1, 0) === 0 && slotSample(2, 0) === 7 && slotSample(5, 2) === 0x1F &&
           !M.unsaved, "the samples: 33 on the demo disk, numbered 00-20 sampler by sampler, the samplers' instruments naming " +
           "them, nothing unsaved");
        U.setInst(0xA000);
        await t.homeKey(5);
        /* the cartridge list's last line, ALL SAMPLES (their size, their count of 256); SEL there opens nothing; ENV opens
         * THE SAMPLES list, titled SAMPLES (33), the keys playing the highlighted sample as a new instrument would; ENV, or
         * HOME, back to the line */
        await t.tap("ENV");
        await cartListTo(40);
        const ly = allLineY(), rows = M.sampleRows ? M.sampleRows() : [];
        const size = M.kb(rows.reduce((a, r) => a + M.bytes.sample(r.s), 0));
        ok(listOn() && textAs(26, ly, "ALL SAMPLES", D.WHITE) && textAs(110, ly, size, D.LGREY) && textAs(170, ly, "033/256", D.LGREY),
           "the cartridge list's last line: ALL SAMPLES " + size + " 033/256 (the cursor's white)");
        t.shot("instr_all_samples_line");
        await t.tap("SEL");
        ok(listOn() && !menuOn(), "SEL on the ALL SAMPLES line opens nothing");
        await t.tap("ENV");
        ok(samplesOn() && textAs(6, Y0 + 5, "SAMPLES (33)", D.CYAN) && textAs(24, smY(0, 0, 33), "KICK", D.WHITE),
           "ENV on it: THE SAMPLES list, SAMPLES (33), every sample, the first highlighted");
        await t.turn("SELECT", 0x1D);
        let h = await c4();
        ok(!!h && h.ref === 0xFEFF && h.midi === 60 && isNew(h.inst, 0x1D, "WOBBLE 140"),
           "its keys play the highlighted sample as a new instrument would (WOBBLE 140, through FE:FF: " + (h && hex(h.ref >> 8)) + ")");
        t.shot("instr_samples_all");
        await t.tap("ENV");
        ok(listOn() && !samplesOn() && textAs(26, ly, "ALL SAMPLES", D.WHITE), "ENV there: back to the line");
        await t.tap("ENV"); await t.tap("HOME");
        ok(listOn() && !samplesOn() && textAs(26, ly, "ALL SAMPLES", D.WHITE), "HOME there: back to the line");
        /* CLONE of a sampler as any cartridge: its instruments name the same samples, no sample copied */
        const cl = M.freeCart();
        await cartListTo(M.cartIndex(0xA0));
        await pick("CLONE");
        let same = !!M.cart(cl) && M.cart(cl).type === "SAMPLER";
        for (let s = 0; s < 10; s++) same = same && slotSample(cl, s) === slotSample(1, s) && slotSample(1, s) >= 0;
        ok(same && nSamples() === 33 && U.toastNow() === "A0 CLONED TO " + hex(cl) && t.top() !== "progress",
           "CLONE of a sampler: its instruments on the same samples, no sample copied (" + U.toastNow() + ")");
        await t.tap("HOME");
        /* DELETE of a sample: CRASH (04), named by 01:04 and 01:05, their two in the clone and one pasted into 02:0B: the
         * warning counts all 5; DELETE takes them with it; its row reads (DELETED), grey, SEL there opening nothing */
        U.edit(() => { M.cart(0xA1).slots[0x0B] = structuredClone(M.cart(0xA0).slots[4]); });
        await allSamples();
        await rowTo(4);
        await pick("DELETE");
        const q = asking();
        ok(q[0] === "DELETE SAMPLE?" && q[1] === "5 INSTRUMENTS USE IT AND ARE" && q[2] === "DELETED TOO",
           "DELETE of a sample: DELETE SAMPLE? 5 INSTRUMENTS USE IT AND ARE DELETED TOO (every sampler's: " + q.join(" / ") + ")");
        t.shot("instr_sample_delete_question");
        await t.tap("OCT+");
        ok(going(4) && slotSample(1, 4) < 0 && slotSample(1, 5) < 0 && slotSample(cl, 4) < 0 && slotSample(cl, 5) < 0 &&
           slotSample(2, 0x0B) < 0 && slotSample(1, 3) === 3 && slotSample(1, 6) === 5 && slotSample(cl, 3) === 3,
           "DELETE: the sample's deletion waits for SAVE, its 5 instruments gone with it, the others there");
        ok(samplesOn() && textAs(24, smY(4, 4, 33), "(DELETED)", D.DGREY) && textAs(8, smY(4, 4, 33), "A4", D.DGREY) &&
           textAs(6, Y0 + 5, "SAMPLES (33)", D.CYAN), "its row: (DELETED), all grey, until SAVE");
        ok(countPx(176, smY(4, 4, 33), 232, smY(4, 4, 33) + 10, D.BROWN) === 0 && countPx(176, smY(4, 4, 33), 232, smY(4, 4, 33) + 10, D.DGREY) === 0,
           "... no waveform on a (DELETED) row, the cursor's");
        await rowTo(3);
        const brownAt = countPx(176, smY(3, 3, 33), 232, smY(3, 3, 33) + 10, D.BROWN);
        const brownOff = countPx(176, smY(5, 3, 33), 232, smY(5, 3, 33) + 10, D.BROWN);
        ok(brownAt > 0 && brownOff === 0, "THE SAMPLES list: the waveform on the cursor's row only (" + brownAt + ", " + brownOff + ")");
        await rowTo(4);
        t.shot("instr_samples_deleted");
        await t.tap("SEL");
        ok(samplesOn() && !menuOn(), "SEL on a (DELETED) row opens nothing");
        await t.tap("HOME"); await t.tap("HOME");
        /* (a pick's list: ENV on a (DELETED) row refused, nothing picked) */
        U.setInst(0xA000);
        await pick("REPLACE SAMPLE");
        await rowTo(4);
        await t.tap("ENV");
        ok(samplesOn() && slotSample(1, 0) === 0 && !M.unsavedThing("slot", [0xA0, 0]), "REPLACE SAMPLE's ENV on a (DELETED) row: refused, nothing picked");
        await t.tap("HOME");
        /* K1 on SAMPLE passes over a deletion waiting for SAVE */
        U.setInst(0xA003);
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        await t.turn("K1", 1);
        const k1 = slotSample(1, 3);
        await t.turn("K1", -1);
        const k2 = slotSample(1, 3);
        await t.tap("HOME");
        ok(k1 === 5 && k2 === 3 && slotSample(1, 3) === 3, "K1 on SAMPLE passes over the deletion waiting (03 + 1: " + hex(k1) +
           ", back " + hex(k2) + ")");
        /* ... and at an end (01:00 on sample 00, K1 to the left) no sample further: nothing changes */
        U.setInst(0xA000);
        const at0 = JSON.stringify(M.inst(0xA000));
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        await t.turn("K1", -1);
        const end0 = JSON.stringify(M.inst(0xA000));
        await t.tap("HOME");
        ok(end0 === at0 && slotSample(1, 0) === 0 && !M.unsavedThing("slot", [0xA0, 0]), "K1 on SAMPLE at an end (01:00 on 00, to the left): nothing changes");
        /* REVERT of 01:04, back on CRASH: the sample's deletion taken back with it; the instruments deleted with it elsewhere
         * (01:05, the clone's, 02:0B) stay deleted until their own REVERT */
        U.setInst(0xA004);
        await pick("REVERT");
        await t.tap("OCT+");
        ok(slotSample(1, 4) === 4 && !going(4) && sampleOn(4) && slotSample(1, 5) < 0 && slotSample(cl, 4) < 0 && slotSample(2, 0x0B) < 0,
           "REVERT of a slot naming a sample about to go: its deletion back too, the other instruments still deleted");
        M.revertThing("cart", 0xA0); M.revertThing("cart", 0xA1);
        /* DELETE in edit mode (ENV on SAMPLE's list): edit mode left first, keeping its changes; HOME after it brings nothing
         * back. SHAKER (05): 01:06 and the clone's */
        U.setInst(0xA006);
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        await t.tap("ENV");
        ok(samplesOn() && textAs(24, smY(5, 5, 33), "SHAKER", D.WHITE), "ENV on SAMPLE: THE SAMPLES list, the instrument's sample highlighted");
        await t.turn("SELECT", 0x1D - 5);
        h = await c4();
        const sh = M.inst(0xA006);
        ok(!!h && h.ref === 0xFEFF && h.inst && h.inst.p.sample === 0xBD && h.inst.p.start === 0 && h.inst.p.end === M.sample(0xBD).length &&
           h.inst.p.loop === "OFF" && h.inst.name === "SHAKER" && JSON.stringify(h.inst.p.env) === JSON.stringify(sh.p.env) &&
           h.inst.p.base === sh.p.base, "ENV on SAMPLE's keys: the highlighted sample through the instrument (WOBBLE, whole, SHAKER's values)");
        await t.turn("SELECT", 5 - 0x1D);
        await pick("DELETE");
        const q2 = asking();
        ok(editMode() === 0 && q2[0] === "DELETE SAMPLE?" && q2[1] === "2 INSTRUMENTS USE IT AND ARE", "DELETE in edit mode: edit mode left " +
           "first, then its question (" + q2.join(" / ") + ")");
        await t.tap("OCT+");
        await t.tap("HOME");                                /* (the list closed) */
        await t.tap("HOME");                                /* (edit mode left already: the HOME overlay, closed) */
        if (t.top() === "home") await t.tap("HOME");
        ok(going(5) && slotSample(1, 6) < 0 && slotSample(cl, 6) < 0 && editMode() === 0,
           "DELETE in edit mode, then HOME: the deletion stays (the instruments gone with it)");
        M.revertThing("sample", 5); M.revertThing("cart", 0xA0);
        /* ... and the list it was asked from, edit mode left, no longer picks for the instrument: answered KEEP (nothing
         * deleted), ENV on another row changes nothing (HAT OPEN's 03 kept), no edit made outside edit mode */
        U.setInst(0xA003);
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        await t.tap("ENV");
        await t.turn("SELECT", 8 - 3);
        await pick("DELETE");
        const asked = asking()[0] === "DELETE SAMPLE?";
        await t.tap("OCT-");                                /* KEEP */
        const kept = samplesOn() && editMode() === 0 && !going(8) && slotSample(2, 2) === 8;
        await t.turn("SELECT", -1);                         /* (WIND CHIME, 07) */
        await t.tap("ENV");
        ok(asked && kept && !samplesOn() && slotSample(1, 3) === 3 && !M.unsavedThing("cart", 0xA0) && editMode() === 0,
           "DELETE asked from ENV on SAMPLE's list, KEEP (asked " + asked + ", kept " + kept + "): ENV there then picks nothing for " +
           "the instrument, edit mode left (01:03 on " + hex(slotSample(1, 3)) + ")");
        if (samplesOn()) await t.tap("HOME");
        M.revertThing("cart", 0xA0);
        /* a sample no instrument uses goes at once: DARBUKA (08), its one instrument emptied first */
        U.edit(() => { M.cart(0xA1).slots[2] = null; });
        await allSamples();
        await rowTo(8);
        await pick("DELETE");
        ok(!asking().length && going(8) && U.toastNow() === "SAMPLE A8 DELETED", "DELETE of an unused sample: at once, SAMPLE A8 DELETED");
        await t.tap("HOME"); await t.tap("HOME");
        M.revertThing("sample", 8); M.revertThing("cart", 0xA1);
        /* PASTE of an instrument whose sample is gone: put down all the same; its page says THE SAMPLE IS MISSING, grey, and
         * it plays nothing */
        U.setInst(0xA402);
        await pick("COPY");
        await allSamples();
        await rowTo(0x1F);
        await pick("DELETE");
        const q3 = asking();
        ok(q3[0] === "DELETE SAMPLE?" && q3[1] === "1 INSTRUMENT USES IT AND IS", "one instrument: 1 INSTRUMENT USES IT AND IS");
        await t.tap("OCT+");
        await t.tap("HOME"); await t.tap("HOME");
        ok(slotSample(5, 2) < 0, "GROWL (1F) deleted with its instrument");
        await pick("PASTE");
        ok(slotSample(5, 2) === 0x1F && !sampleOn(0x1F) && textAs(0, Y0 + 136, "THE SAMPLE IS MISSING", D.DGREY) &&
           !countPx(0, Y0 + 116, 240, 226, D.RED), "PASTE of an instrument whose sample is gone: put down, its page THE SAMPLE IS MISSING, grey");
        t.shot("instr_sample_missing");
        const im = A.instMsg ? A.instMsg(0xA402) : null;
        ok(!!im && im.engine === "SAMPLER" && im.p.key == null, "an instrument naming a missing sample plays nothing (no sample for its voice)");
        await t.tap("EDIT");
        ok(editMode() === 1 && textAs(0, Y0 + 83 + 30, "THE SAMPLE IS MISSING", D.DGREY), "its edit page: THE SAMPLE IS MISSING where the waveform would be");
        t.shot("instr_sample_missing_edit");
        await t.tap("HOME");
        M.revertThing("sample", 0xBF); M.revertThing("cart", 0xA4);
        /* REPLACE SAMPLE (after PASTE; not with slots marked): THE SAMPLES list on the instrument's sample; ENV: every value a
         * new instrument's, the name following when it was the old sample's (KICK -> SNARE; CRSH CHOKE kept); HOME: as it was */
        U.setInst(0xA000);
        let it = await menuItems();
        ok(it.includes("COPY,PASTE,REPLACE SAMPLE,MOVE"), "a sampler's slot menu: REPLACE SAMPLE after PASTE (" + it + ")");
        await pick("REPLACE SAMPLE");
        ok(samplesOn() && textAs(6, Y0 + 5, "SAMPLES (33)", D.CYAN) && textAs(24, smY(0, 0, 33), "KICK", D.WHITE),
           "REPLACE SAMPLE: THE SAMPLES list, the instrument's sample highlighted");
        await t.turn("SELECT", 1);
        h = await c4();
        ok(!!h && h.ref === 0xFEFF && isNew(h.inst, 1, "SNARE"), "REPLACE SAMPLE's keys: the highlighted sample as a new instrument would (SNARE)");
        t.shot("instr_replace_sample");
        await t.tap("ENV");
        ok(!samplesOn() && isNew(M.inst(0xA000), 1, "SNARE"), "REPLACE SAMPLE: SNARE, every value a new instrument's, named after it");
        U.setInst(0xA005);
        await pick("REPLACE SAMPLE");
        await t.turn("SELECT", 1);
        await t.tap("ENV");
        ok(slotSample(1, 5) === 5 && M.inst(0xA005).name === "CRSH CHOKE", "REPLACE SAMPLE on a renamed instrument: its name kept");
        U.setInst(0xA001);
        const b0 = JSON.stringify(M.inst(0xA001));
        await pick("REPLACE SAMPLE");
        await t.turn("SELECT", 3);
        await t.tap("HOME");
        ok(!samplesOn() && JSON.stringify(M.inst(0xA001)) === b0, "REPLACE SAMPLE, HOME: the instrument as it was");
        await pick("MARK");
        await t.turn("SELECT", 1);
        it = await menuItems();
        ok(!it.includes("REPLACE SAMPLE") && it.includes("INIT"), "with slots marked, no REPLACE SAMPLE (" + it + ")");
        await t.tap("HOME");
        U.setInst(0xA001);
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        it = await menuItems();
        ok(it.includes("COPY,PASTE,REPLACE SAMPLE,INIT,RENAME,AUTO LEVEL,DELETE"),
           "edit mode's menu on a sample instrument: REPLACE SAMPLE after PASTE, DELETE after AUTO LEVEL (" + it + ")");
        await t.tap("HOME");
        M.revertThing("cart", 0xA0);
        /* CREATE on an empty sampler slot (in place of INIT): THE SAMPLES list; HOME makes nothing; ENV a new instrument
         * there, whole, named after its sample. EDIT on an empty sampler slot: the same pick, then edit mode on the new
         * instrument */
        U.setInst(0xA10B);
        ok(textAs(4, Y0 + 116 + 30, "EMPTY SLOT: SEL MENU, CREATE OR PASTE", D.DGREY), "an empty sampler slot: EMPTY SLOT: SEL MENU, CREATE OR PASTE");
        it = await menuItems();
        ok(it.includes(",CREATE,") && !it.includes("INIT") && !it.includes("REPLACE SAMPLE"), "an empty sampler slot's menu: CREATE in place of INIT (" + it + ")");
        await pick("CREATE");
        ok(samplesOn() && textAs(24, smY(0, 0, 33), "KICK", D.WHITE), "CREATE: THE SAMPLES list");
        await t.tap("HOME");
        ok(!samplesOn() && slotSample(2, 0x0B) < 0, "CREATE, HOME: nothing made");
        await pick("CREATE");
        await t.turn("SELECT", 0x1D);
        await t.tap("ENV");
        ok(!samplesOn() && isNew(M.inst(0xA10B), 0x1D, "WOBBLE 140") && editMode() === 0, "CREATE, ENV: a new instrument playing WOBBLE 140 whole, named after it");
        U.setInst(0xA10C);
        await t.tap("EDIT");
        ok(samplesOn() && U.toastNow() !== "EMPTY SLOT: SEL, INIT", "EDIT on an empty sampler slot: CREATE's pick");
        await t.turn("SELECT", 7);
        await t.tap("ENV");
        ok(!samplesOn() && editMode() === 1 && isNew(M.inst(0xA10C), 7, "WIND CHIME"), "... once picked, edit mode on the new instrument");
        await t.tap("EDIT");
        ok(editMode() === 0 && isNew(M.inst(0xA10C), 7, "WIND CHIME"), "... EDIT keeps it");
        U.setInst(0xA10D);
        await t.tap("EDIT");
        await t.tap("ENV");
        await t.tap("HOME");
        ok(editMode() === 0 && slotSample(2, 0x0D) < 0, "... HOME in that edit mode: the slot as it was at EDIT, empty");
        M.revertThing("cart", 0xA1);
        /* INIT on a sampler slot keeps its sample, every other value a new instrument's, named after its sample; over marked
         * slots each used one so, the empty ones left empty */
        U.setInst(0xA201);
        await pick("INIT");
        ok(isNew(M.inst(0xA201), 0x0F, "PIANO"), "INIT on PIANO MUTE: its sample kept, a new instrument's values, named PIANO");
        U.setInst(0xA10A);
        await pick("MARK");
        await t.turn("SELECT", 1);
        await pick("INIT");
        ok(isNew(M.inst(0xA10A), 0x0E, "TIMPANI") && slotSample(2, 0x0B) < 0, "INIT over marked slots: each used one so, the empty one left empty");
        M.revertThing("cart", 0xA1); M.revertThing("cart", 0xA2);
        /* MOVE and DELETE of a sampler as any cartridge: its samples stay */
        U.setInst(cl << 8);
        await t.tap("ENV");
        await cartListTo(M.cartIndex(cl));
        await pick("MOVE");
        await t.turn("SELECT", 1);
        await t.tap("SEL");
        ok(!!M.cart(cl + 1) && M.cart(cl + 1).type === "SAMPLER" && !M.cart(cl) && nSamples() === 33 && t.top() !== "progress",
           "MOVE of a sampler: moved, every sample where it was");
        await cartListTo(M.cartIndex(cl + 1));
        await pick("DELETE");
        ok(!M.cart(cl + 1) && nSamples() === 33, "DELETE of a sampler: its instruments only, the samples stay");
        await t.tap("HOME");
        M.revertThing("cart", cl); M.revertThing("cart", cl + 1);
        ok(!M.unsaved, "the samples' tests leave the edits as they found them (" + M.unsaved + ")");
    });

    /* ---------------------------------------------------------------------------- NO SAMPLES ON THE FM-1 --- */
    /* every sample's deletion waiting (none to pick): REPLACE SAMPLE, CREATE, EDIT on an empty sampler slot and ENV on
     * SAMPLE say so and open nothing */
    await step("no samples", async () => {
        const nums = [];
        for (let n = 0; n < 256; n++) if (sampleOn(n)) nums.push(n);
        U.edit(() => { for (const n of nums) M.state.samples[SMP(n)] = null; });
        U.setInst(0xA000);
        await t.homeKey(5);
        await pick("REPLACE SAMPLE");
        const a = !samplesOn() && U.toastNow() === "NO SAMPLES ON THE FM-1";
        t.shot("instr_no_samples");
        U.setInst(0xA10B);
        await t.sleep(50);
        await pick("CREATE");
        const b = !samplesOn() && U.toastNow() === "NO SAMPLES ON THE FM-1";
        U.toast("", 0);
        await t.tap("EDIT");
        const c = !samplesOn() && editMode() === 0 && U.toastNow() === "NO SAMPLES ON THE FM-1";
        U.setInst(0xA000);
        U.toast("", 0);
        await t.tap("EDIT");
        await t.turn("SELECT", -10);
        await t.tap("ENV");
        const d = !samplesOn() && editMode() === 1 && U.toastNow() === "NO SAMPLES ON THE FM-1";
        await t.tap("HOME");
        ok(nums.length === 33 && a && b && c && d, "no sample on the FM-1: REPLACE SAMPLE " + a + ", CREATE " + b + ", EDIT on an " +
           "empty sampler slot " + c + ", ENV on SAMPLE " + d + " say NO SAMPLES ON THE FM-1, open nothing");
        for (const n of nums) M.revertThing("sample", SMP(n));
        ok(!M.unsaved, "... the deletions taken back");
    });

    /* --------------------------------------------------------------- what lies in the flash (the user area kept) --- */
    await step("the flash", async () => {
        /* an old sampler (the old form) under the next free number: not read, its number taken by NEW, CLONE and MOVE */
        const old = M.freeCart() + 1;                       /* (NEW takes the free one under it, CLONE and MOVE pass over it) */
        U.edit(() => { M.state.older.push({ id: old, bytes: 67, audio: 0 }); });
        M.save();
        ok(M.cartTaken && M.cartTaken(old) && !M.cart(old), "an old sampler in the flash: not read, its number taken");
        U.setInst(0xA000);
        await t.homeKey(5);
        await t.tap("ENV");
        await cartListTo(M.cartIndex(0xA0));
        await pick("SAMPLER");                              /* (NEW's group: the type) */
        const a = U.toastNow() === "CARTRIDGE " + hex(old - 1) + " MADE" && !!M.cart(old - 1) && M.cart(old - 1).type === "SAMPLER";
        await cartListTo(M.cartIndex(0xA0));
        await pick("CLONE");
        const b = U.toastNow() === "A0 CLONED TO " + hex(old + 1) && !!M.cart(old + 1) && M.cart(old + 1).type === "SAMPLER";
        await cartListTo(M.cartIndex(old - 1));
        await pick("MOVE");
        await t.turn("SELECT", 1);                          /* (up: past the old sampler's and the clone) */
        await t.tap("SEL");
        const c = !!M.cart(old + 2) && M.cart(old + 2).type === "SAMPLER" && !M.cart(old - 1) && !M.cart(old) && M.cartTaken(old);
        await t.tap("HOME");
        ok(a && b && c, "an old sampler's number " + hex(old) + " passed over: NEW " + a + ", CLONE " + b + ", the cartridge list's MOVE " + c);
        M.revertThing("cart", old - 1); M.revertThing("cart", old + 1); M.revertThing("cart", old + 2);
        U.edit(() => { M.state.older = []; });
        M.save();
        /* a sample's deletion saved before its instruments' changes (the FM-1's save cut between the two: deletions first):
         * GROWL's (1F) deletion saved, 05:02 still naming it: THE SAMPLE IS MISSING, grey, silent; emptied by an edit and
         * REVERTed, it names the missing sample again (nothing to take back) */
        const growl = structuredClone(M.sample(0xBF));
        U.edit(() => { M.state.samples[0xBF] = null; });
        M.save();
        U.setInst(0xA402);
        await t.homeKey(5);
        ok(!sampleOn(0x1F) && slotSample(5, 2) === 0x1F && textAs(0, Y0 + 136, "THE SAMPLE IS MISSING", D.DGREY) &&
           A.instMsg && A.instMsg(0xA402).p.key == null, "the sample's deletion saved alone: its instrument THE SAMPLE IS MISSING, grey, silent");
        U.edit(() => { M.cart(0xA4).slots[2] = null; });
        await pick("REVERT");
        await t.tap("OCT+");
        ok(slotSample(5, 2) === 0x1F && !sampleOn(0x1F) && !going(0x1F) && textAs(0, Y0 + 136, "THE SAMPLE IS MISSING", D.DGREY),
           "REVERT of an instrument whose sample's deletion is saved: back, naming a missing sample, grey");
        U.edit(() => { M.state.samples[0xBF] = growl; });
        M.save();
        /* the waveform drawn again once another sample lies under its number (WOBBLE's 1D: ONE's under 1D) */
        U.setInst(0xA400);
        const browns = () => { const n = []; for (let x = 0; x < 240; x++) n.push(countPx(x, 149, x + 1, 208, D.BROWN)); return n.join(); };
        const w0 = browns();
        U.edit(() => { M.state.samples[0xBD] = structuredClone(M.sample(0xBC)); });
        const w1 = browns();
        M.revertThing("sample", 0xBD);
        const w2 = browns();
        ok(w0 !== w1 && w0 === w2, "a sample's waveform drawn again when another lies under its number, and back");
        ok(!M.unsaved, "the flash's tests leave the edits as they found them");
    });

    /* ------------------------------------------------------------------------ SETTINGS' tools and STATUS --- */
    await step("the tools", async () => {
        const toolsAt = M.SETTINGS.findIndex(s => s.k === "TOOLS");
        const runTool = async name => {
            await t.homeKey(20); await t.turn("SELECT", -M.SETTINGS.length); await t.turn("SELECT", toolsAt);
            U.pickTool(name); await t.tap("EDIT");
        };
        /* two old samplers with some old audio, two samples no instrument names (40, 41) */
        const old = M.freeCart();
        U.edit(() => {
            M.state.older.push({ id: old, bytes: 67, audio: 200 }, { id: old + 1, bytes: 67, audio: 0 });
            M.state.samples[0xE0] = structuredClone(M.sample(0xBC));
            M.state.samples[0xE1] = structuredClone(M.sample(0xBC));
        });
        M.save();
        ok(sampleOn(0x40) && sampleOn(0x41) && !sampleOn(0x42) && !M.unsaved, "the tools: two old samplers, old audio and two unnamed samples kept");
        /* CLEAN UP: 41 is named by 02:0B (a sampler), so only 40 goes; the other lines unticked */
        U.edit(() => { M.cart(0xA1).slots[0x0B] = Object.assign(structuredClone(M.cart(0xA0).slots[4]), {}); M.cart(0xA1).slots[0x0B].p.sample = 0xE1; });
        U.cleanUp();
        ok(t.top() === "question", "CLEAN UP: its checklist opens");
        t.shot("ui3_clean_up_samples");
        await t.turn("SELECT", -5); await t.turn("K1", -1);  /* PATTERNS off */
        await t.turn("SELECT", 2); await t.turn("K1", -1);   /* INSTRUMENTS off */
        await t.tap("OCT+");
        ok(U.toastNow() === "CLEANED UP: 1 DELETED" && going(0x40) && !going(0x41) && !going(0x1C) && M.state.older.length === 2,
           "CLEAN UP: the one unnamed sample (40) goes, the named one (41), the others and the old samplers stay (" + U.toastNow() + ")");
        M.revertThing("sample", 0xE0); M.revertThing("cart", 0xA1);   /* 02:0B unnamed again: both go */
        U.cleanUp();
        await t.turn("SELECT", -5); await t.turn("K1", -1);
        await t.turn("SELECT", 2); await t.turn("K1", -1);
        await t.tap("OCT+");
        ok(U.toastNow() === "CLEANED UP: 2 DELETED" && going(0x40) && going(0x41) && !going(0x1C),
           "CLEAN UP: with 02:0B empty both unnamed samples go (" + U.toastNow() + ")");
        M.revertThing("sample", 0xE0); M.revertThing("sample", 0xE1);
        ok(!M.unsaved, "CLEAN UP's deletions taken back");
        /* STATUS: the old samplers' records among INSTRUMENTS, their old audio in OTHER (the map colours it so: the legend's
         * rows add up to every kind) */
        const st = M.storage();
        await t.homeKey(17);
        await t.turn("SELECT", -6); await t.turn("SELECT", 3);
        t.shot("ui3_status_flash_map_old_audio");
        const rows = [st.smp, st.inst, st.patt, st.song, st.loops, st.other];
        let drawn = U.screen.sub() === "USER FLASH MAP";
        rows.forEach((b, i) => { const k = M.kb(b); drawn = drawn && textAs(150 - 6 * k.length, 86 + 12 * i, k, D.WHITE); });
        const plain = (() => { const s = M.state.older; M.state.older = []; const x = M.storage(); M.state.older = s; return x; })();
        ok(drawn && st.other === plain.other + 200 && st.inst === plain.inst + 2 * 67,
           "USER FLASH MAP: the old samplers among INSTRUMENTS, the old audio in OTHER (" + M.kb(st.other) + ")");
        await t.turn("SELECT", -6);
        /* CHECK REFERENCES: GROWL's (1F) deletion saved alone, so the instrument naming it names a missing sample; a cell
         * and a favourite naming an old sampler's instrument are no references to nothing */
        const growl = structuredClone(M.sample(0xBF));
        U.edit(() => { M.state.samples[0xBF] = null; });
        M.save();
        const pt = M.state.patterns.find(p => p && p.n < 0xE0 && !p.builtin), cell0 = pt.cells[0];
        pt.cells[0] = M.newCell(60, old << 8, null);
        const FB = M.cart(0xFB), fav0 = FB.slots[0];
        FB.slots[0] = { src: old << 8 };
        await runTool("CHECK REFERENCES");
        await t.sleep(900);
        const want = ["SAMPLER A4:02: SAMPLE BF", "CARTRIDGE " + hex(old) + ": OLDER FORM", "CARTRIDGE " + hex(old + 1) + ": OLDER FORM"];
        let ls = listOn() && U.top().title === "MISSING REFERENCES: 1" && textAs(6, Y0 + 5, "MISSING REFERENCES: 1", D.CYAN);
        let bad = -1;
        for (let i = 0; i < want.length && ls; i++) if (!textAs(8, Y0 + 20 + 13 * i, want[i], D.LGREY)) { bad = i; ls = false; }
        ok(ls && (U.top().lines || []).length === 3, "CHECK REFERENCES: the instrument naming the missing sample first (SAMPLER 05:02: " +
           "SAMPLE 1F), then each old sampler once, not counted, nothing for the cell and the favourite naming one (" +
           (U.top() && U.top().title) + "; line " + bad + " wrong)");
        t.shot("ui3_check_refs_samples");
        if (t.top()) await t.tap("HOME");
        pt.cells[0] = cell0;
        FB.slots[0] = fav0;
        U.edit(() => { M.state.samples[0xBF] = growl; });
        M.save();
        /* only the old samplers: the list opens, titled MISSING REFERENCES: 0 */
        await runTool("CHECK REFERENCES");
        await t.sleep(900);
        ok(listOn() && textAs(6, Y0 + 5, "MISSING REFERENCES: 0", D.CYAN) && textAs(8, Y0 + 20, "CARTRIDGE " + hex(old) + ": OLDER FORM", D.LGREY),
           "CHECK REFERENCES with old samplers alone: the list, MISSING REFERENCES: 0");
        t.shot("ui3_check_refs_older_form");
        if (t.top()) await t.tap("HOME");
        U.edit(() => { M.state.older = []; M.state.samples[0xE0] = null; M.state.samples[0xE1] = null; });
        M.save();
        await runTool("CHECK REFERENCES");
        await t.sleep(900);
        ok(!listOn() && U.toastNow() === "NO MISSING REFERENCES", "CHECK REFERENCES with nothing missing: the toast");
        ok(!M.unsaved && nSamples() === 33, "the tools' tests leave the user area as they found it");
    });
    A.noteOn = on0;
};
