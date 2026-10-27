/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The page at a phone's width (400 px, touch), loaded at that width: nothing wider than the screen; the TABLE / FM-1
 * switch; a pattern's tab. */
module.exports = async api => {
    const { evaluate, check, shot, send, sleep } = api;
    await api.size(400, 800, { deviceScaleFactor: 2, mobile: true });
    await send("Emulation.setTouchEmulationEnabled", { enabled: true });
    await api.fresh();
    await api.demo();
    await sleep(300);
    const wide = () => evaluate(`[...document.querySelectorAll("body *")].filter(e => e.getBoundingClientRect().right > innerWidth + 1 && getComputedStyle(e).position !== "fixed" &&
        !e.closest(".pgrid, .tabs, [style*='overflow']")).slice(0, 4).map(e => e.tagName + "." + e.className).join(" | ")`);
    /* (under a phone's emulation the page grows past the screen's 400 px when something is wider: it must not) */
    const vw = await evaluate("innerWidth");
    check(vw === 400 && await evaluate("document.documentElement.scrollWidth <= innerWidth"), "narrow: the page as wide as the screen (" + vw + " px)");
    const w1 = await wide();
    check(!w1, "narrow: nothing on the TABLE wider than the screen" + (w1 ? ": " + w1 : ""));
    await shot("01-table");
    await evaluate(`document.querySelector("#narrowsw button:nth-child(2)").click(); 1`);
    await sleep(300);
    check(await evaluate(`!!document.querySelector(".panel[data-side=R]") && document.querySelector(".panel[data-side=R]").getBoundingClientRect().width > 300`),
        "narrow: the switch shows the FM-1 panel");
    await shot("02-fm1");
    await evaluate(`(() => { const p = ED.app.st.L.find(i => i.kind === 0x20); ED.tabs.open(p); return 1; })()`);
    await sleep(500);
    check(await evaluate("innerWidth === 400 && document.documentElement.scrollWidth <= innerWidth"), "narrow: a pattern's tab as wide as the screen (its grid scrolls inside)");
    await shot("03-pattern");
};
