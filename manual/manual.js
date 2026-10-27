/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The manual's page (specs/manual.md section 2): the keys the text names lit on the FM-1 beside it (pointed at,
 * focused, tapped: every [data-k] lists the panel's elements it lights), the contents' search and drawer, the
 * contents following the text. */
(function () {
    "use strict";
    const parts = new Map();
    document.querySelectorAll(".pcol [data-p]").forEach(e => parts.set(e.dataset.p, e));
    let cur = null;                                     /* the [data-k] lighting now */

    function light(ids) { parts.forEach((e, id) => e.classList.toggle("lit", ids.indexOf(id) >= 0)); }
    function on(el) {
        if (cur) cur.classList.remove("on");
        cur = el;
        if (!el) { light([]); return; }
        el.classList.add("on");
        light(el.dataset.k.split(" "));
    }
    /* leaving a key inside a row (or a combination's row) hands back to the one around it, if still pointed at */
    function off(el) {
        if (cur !== el) return;
        const up = el.parentElement && el.parentElement.closest("[data-k]");
        on(up && up.matches(":hover") ? up : null);
    }
    /* a key or knob of the drawing pointed at: its name and kind on the drawing's screen */
    const say = document.querySelector(".pcol .p-say");
    if (say) {
        const [sayN, sayK] = say.querySelectorAll("text");
        parts.forEach(e => {
            e.addEventListener("pointerenter", () => {
                const n = e.dataset.name;
                sayN.textContent = n;
                sayN.setAttribute("font-size", String(Math.min(64, Math.floor(210 / (0.62 * n.length)))));
                sayK.textContent = e.dataset.kind;
                say.classList.add("on");
            });
            e.addEventListener("pointerleave", () => say.classList.remove("on"));
        });
    }
    let pointerAt = 0;                                  /* a focus right after a press is the pointer's, not the keyboard's */
    document.querySelectorAll(".doc [data-k]").forEach(el => {
        el.addEventListener("pointerenter", e => { if (e.pointerType === "mouse") on(el); });
        el.addEventListener("pointerleave", e => { if (e.pointerType === "mouse") off(el); });
        el.addEventListener("pointerdown", () => { pointerAt = Date.now(); });
        el.addEventListener("pointerup", e => {
            if (e.pointerType === "mouse") return;
            e.stopPropagation();                        /* (the innermost key tapped, not the row around it) */
            on(cur === el ? null : el);
        });
        el.addEventListener("click", () => { if (document.body.classList.contains("folded")) setFolded(false); });   /* (to
                                                         * see it lit, the drawing comes back) */
        el.addEventListener("focus", () => { if (Date.now() - pointerAt > 400) on(el); });
        el.addEventListener("blur", () => { if (Date.now() - pointerAt > 400) off(el); });
    });

    /* the search: the contents keep the chapters and topics holding every word typed */
    const search = document.querySelector(".search");
    const items = [...document.querySelectorAll(".toc li:not(.h)")].map(li => {
        const target = document.getElementById(li.querySelector("a").getAttribute("href").slice(1));
        return { li, text: (target ? target.textContent : "").toLowerCase() };
    });
    search.addEventListener("input", () => {
        const words = search.value.toLowerCase().split(/\s+/).filter(Boolean);
        document.querySelector(".toc").classList.toggle("searching", words.length > 0);
        items.forEach(it => { it.li.hidden = !words.every(w => it.text.indexOf(w) >= 0); });
        document.querySelectorAll(".toc .h").forEach(h => {
            let n = h.nextElementSibling, any = false;
            for (; n && !n.classList.contains("h"); n = n.nextElementSibling) any = any || !n.hidden;
            h.hidden = !any;
        });
    });
    search.addEventListener("keydown", e => {
        if (e.key !== "Enter") return;
        const first = items.find(it => !it.li.hidden && it.li.closest("ul") !== null) || items.find(it => !it.li.hidden);
        if (first) first.li.querySelector("a").click();
    });

    /* the FM-1 drawn, folded away or shown (wide pages), remembered in this browser */
    const fold = document.querySelector(".pfold");
    function folded(f) {
        document.body.classList.toggle("folded", f);
        fold.textContent = f ? "SHOW THE FM-1" : "HIDE THE FM-1";
        fold.setAttribute("aria-expanded", String(!f));
    }
    function setFolded(f) {                             /* (a choice: remembered) */
        folded(f);
        try { localStorage.setItem("ptfm-manual-fold", f ? "1" : "0"); } catch (e) { /* (not remembered) */ }
        requestAnimationFrame(snap);
    }
    try { folded(localStorage.getItem("ptfm-manual-fold") === "1"); } catch (e) { /* (no storage: shown) */ }
    fold.addEventListener("click", () => setFolded(!document.body.classList.contains("folded")));

    /* the drawer (narrow pages) */
    document.querySelector(".mtop .menu").addEventListener("click", () => document.body.classList.toggle("drawer"));
    document.querySelectorAll(".toc a").forEach(a => a.addEventListener("click", () => document.body.classList.remove("drawer")));

    /* the screenshots under the FM-1: a strip of the chapter being read, scrolling on its own; the current one is the
     * last of the chapter above the reading line (45 % down the window), else the chapter's first; a click on one
     * takes the text to its place */
    const pshot = document.querySelector(".pcol .pshot");
    const figs = [...document.querySelectorAll(".doc figure.shot")];
    const chapters = [...document.querySelectorAll(".doc section.chapter")];
    let shown = null, stripOf = null;
    const top = el => el.getBoundingClientRect().top + scrollY;
    const wide = matchMedia("(min-width: 901px)");      /* (narrower, the screenshots stand in the text: web.css) */
    const goTo = f => scrollTo({ top: Math.max(0, top(f) - innerHeight * 0.45 + 1), behavior: "smooth" });
    /* the FM-1 drawn shows the current screenshot small on its screen, STATUS (the build's) where there is none */
    const screen = document.querySelector(".pcol .p-shot");
    if (screen) screen.dataset.home = screen.getAttribute("href");
    function onScreen(fig) {
        if (!screen) return;
        const img = fig && fig.querySelector("img");
        screen.setAttribute("href", img ? img.getAttribute("src") : screen.dataset.home);
    }
    function follow() {
        const line = scrollY + innerHeight * 0.45;
        const ch = chapters.filter(c => top(c) <= line).pop() || chapters[0];
        const mine = figs.filter(f => f.closest("section.chapter") === ch);
        if (!wide.matches) return;                      /* (narrow: no FM-1 drawn, the screenshots stand in the text) */
        if (ch !== stripOf) {
            stripOf = ch;
            shown = undefined;
            pshot.replaceChildren(...mine.map(f => {
                const c = f.cloneNode(true);
                c.dataset.for = f.dataset.shot;
                c.tabIndex = 0;
                c.addEventListener("click", () => goTo(f));
                c.addEventListener("keydown", e => { if (e.key === "Enter") goTo(f); });
                return c;
            }));
        }
        const fig = mine.filter(f => top(f) <= line).pop() || mine[0] || null;
        if (fig === shown) return;
        shown = fig;
        onScreen(fig);
        pshot.querySelectorAll("figure").forEach((c, i) => c.classList.toggle("cur", mine[i] === fig));
        snap();
    }
    function snap() {                                   /* the current one into the strip's view (the strip scrolls,
                                                         * never the window) */
        const cur = pshot.querySelector("figure.cur");
        if (!cur) return;
        const a = pshot.getBoundingClientRect(), b = cur.getBoundingClientRect();
        if (b.top < a.top || b.bottom > a.bottom) pshot.scrollTop += b.top - a.top - 6;
    }
    /* a new strip's images take their size once decoded: the current one is brought into view again then */
    new MutationObserver(() => Promise.all([...pshot.querySelectorAll("img")].map(i => i.decode().catch(() => 0)))
        .then(() => requestAnimationFrame(snap))).observe(pshot, { childList: true });
    /* the contents: only the chapter at the reading line shows its topics */
    const tocOf = new Map();
    document.querySelectorAll(".toc li.c").forEach(li => tocOf.set(li.querySelector("a").getAttribute("href").slice(1), li));
    let openCh = null;
    function openToc() {
        const line = scrollY + innerHeight * 0.45;
        const ch = chapters.filter(c => top(c) <= line).pop() || chapters[0];
        if (!ch || ch === openCh) return;
        openCh = ch;
        tocOf.forEach((li, id) => li.classList.toggle("open", id === ch.id));
    }
    let queued = false;
    const soon = () => { if (!queued) { queued = true; requestAnimationFrame(() => { queued = false; follow(); openToc(); }); } };
    addEventListener("scroll", soon, { passive: true });
    addEventListener("resize", () => { shown = undefined; soon(); });
    follow();
    openToc();

    /* the contents follow the text */
    const links = new Map();
    document.querySelectorAll(".toc a").forEach(a => links.set(a.getAttribute("href").slice(1), a));
    const seen = new IntersectionObserver(es => {
        es.forEach(e => {
            if (!e.isIntersecting) return;
            document.querySelectorAll(".toc a.cur").forEach(a => a.classList.remove("cur"));
            const a = links.get(e.target.id);
            if (a) a.classList.add("cur");
        });
    }, { rootMargin: "-10% 0px -80% 0px" });
    document.querySelectorAll(".doc section[id]").forEach(s => seen.observe(s));
})();
