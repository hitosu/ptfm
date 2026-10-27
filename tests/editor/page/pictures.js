/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The pictures beside their sliders: every instrument kind's tab drawn; the DX7's held in view at its last slider;
 * drawn again while a slider moves (the instruments' and the FX setup's); CHIP's TABLES following the row touched;
 * at a phone's width above the sliders. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    await api.demo();
    const open = k => evaluate(`(() => { const it = ED.app.st.L.find(i => i.kind === ${k}) || ED.files.viewItems("R").find(i => i.kind === ${k});
        ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.open(it); return !!it; })()`);
    await evaluate(`window.PP = {
        body() { return ED.tabs.current().body; },
        fig(cap) { return [...PP.body().querySelectorAll("figure")].find(f => f.querySelector("figcaption").textContent.startsWith(cap)); },
        sig(cap) { const c = PP.fig(cap) && PP.fig(cap).querySelector("canvas"); return c ? c.toDataURL() : ""; },
        input(label, v, box) { const f = [...(box || PP.body()).querySelectorAll(".field")].find(x => x.querySelector("label").textContent === label);
            const i = f.querySelector("input[type=range]"); i.value = v; i.dispatchEvent(new Event("input")); return +i.value; }
    }; 1`);
    /* every kind: its sections with their pictures drawn */
    for (const [k, name, want] of [[1, "DX7", "EDIT: OPERATOR 1,AS INSTRUMENTS SHOWS IT"], [2, "JUNO-106", "AS INSTRUMENTS SHOWS IT,EDIT"],
        [3, "TB-303", "AS INSTRUMENTS SHOWS IT,EDIT"], [6, "CHIP", "EDIT: TONE,AS INSTRUMENTS SHOWS IT,EDIT: VOLUME,EDIT: TABLES · VOL,EDIT: WAVE"],
        [5, "FUSION", "EDIT,AS INSTRUMENTS SHOWS IT"], [4, "SAMPLER", "EDIT,AS INSTRUMENTS SHOWS IT"]]) {
        await open(k);
        const r = await api.until(`(() => { const f = [...PP.body().querySelectorAll(".psec figure")];
            return f.length && f.every(x => !x.hidden && x.querySelector("canvas")) && f.map(x => x.querySelector("figcaption").textContent).join(); })()`, 10);
        check(r === want, "pictures: " + name + "'s pictures drawn beside its sliders: " + r);
        await shot("kind-" + name.toLowerCase());
    }
    /* the DX7: R1 drawn while it moves (nothing changed), the picture held in view at the last slider */
    await open(1);
    await api.until(`!!PP.sig("EDIT: OPERATOR")`, 10);
    const a = await evaluate(`PP.sig("EDIT: OPERATOR")`);
    await evaluate(`PP.input("R1", 10)`);
    check(a !== await evaluate(`PP.sig("EDIT: OPERATOR")`) && await evaluate("!ED.tabs.current().dirty"), "pictures: the DX7's drawn again while R1 moves, nothing changed yet");
    await evaluate(`[...PP.body().querySelectorAll(".field label")].find(x => x.textContent === "KEY VEL SENS").scrollIntoView({ block: "end" }); 1`);
    await sleep(300);
    const top = await evaluate(`Math.round(PP.fig("EDIT: OPERATOR").getBoundingClientRect().top - PP.body().getBoundingClientRect().top)`);
    check(top >= 0 && top <= 12, "pictures: the DX7's held in view at its last slider (" + top + " px from the top)");
    await shot("dx7-scrolled");
    /* CHIP: TABLES follows the row touched; drawn as it moves */
    await open(6);
    await api.until(`!!PP.sig("EDIT: TABLES")`, 10);
    const r = await evaluate(`(() => {
        const g = [...PP.body().querySelectorAll(".grp")].find(x => x.querySelector("h5").textContent === "ARP");
        g.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
        const cap = PP.fig("EDIT: TABLES").querySelector("figcaption").textContent, picked = [...PP.body().querySelectorAll(".grp.pick h5")].map(x => x.textContent).join();
        const s0 = PP.sig("EDIT: TABLES"); PP.input("TABLE", 3, g);
        return [cap, picked, s0 !== PP.sig("EDIT: TABLES")];
    })()`);
    check(r[0] === "EDIT: TABLES · ARP" && r[1] === "ARP" && r[2], "pictures: CHIP's TABLES follows the row touched, drawn as it moves: " + JSON.stringify(r));
    /* the FX setup's picture drawn while a knob moves */
    await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.tabs.list.slice().forEach(t => ED.tabs.close(t)); ED.tabs.open(s); return 1; })()`);
    const fx = await api.until(`(() => { const k = [...PP.body().querySelectorAll(".fxbox")].find(x => x.querySelector(".fxname") && x.querySelector("canvas")); if (!k) return null;
        const c = k.querySelector("canvas"), s0 = c.toDataURL(), i = k.querySelector("input[type=range]"); i.value = +i.value > 2 ? 1 : +i.max; i.dispatchEvent(new Event("input"));
        const c2 = k.querySelector("canvas"); return [k.querySelector(".fxname").textContent, c2 !== c && c2.toDataURL() !== s0]; })()`, 10);
    check(fx && fx[1], "pictures: the FX setup's picture drawn again while a knob moves: " + JSON.stringify(fx));
    /* a phone's width: the pictures above the sliders, not held */
    await api.size(400, 860, { mobile: true });
    await open(2);
    await sleep(500);
    const n = await evaluate(`[getComputedStyle(PP.body().querySelector(".psplit")).gridTemplateColumns.split(" ").length, getComputedStyle(PP.body().querySelector(".pcol")).position]`);
    check(n[0] === 1 && n[1] === "static", "pictures: at 400 px the pictures above the sliders: " + JSON.stringify(n));
    await shot("juno-400");
};
