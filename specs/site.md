# The web site, hitosu.github.io/ptfm

Status: **settled** (2026-10-28).

PT/FM's page on the web: what PT/FM is, what it does, and the way to its four pages (INSTALL, EMULATOR, MANUAL,
EDITOR), all served from GitHub Pages. GitHub serves this repository's Pages at `hitosu.github.io/ptfm/`; the root,
`hitosu.github.io`, is a repository of its own that links here.

## 1. What is published

| URL | From |
|---|---|
| `/ptfm/` | `site/index.html` (the landing), `site/img/`, `site/screens/`, `site/fonts/`; Archivo and the fonts' licences from `assets/fonts/` |
| `/ptfm/install/` | `dist/firmware/install.html` |
| `/ptfm/emulator/` | `dist/emulator/emulator.html`, the emulator in one file; before that file, `emulator/` with `panel/`, `fw/web/seed.js` and `dist/emulator/` at the paths the page loads them from |
| `/ptfm/manual/` | `dist/manual/manual.html` and `ptfm-manual.pdf` beside it (the page links it); left out while `dist/manual/` is missing |
| `/ptfm/screens/manual/ID.png` | `site/screens/manual/`: each of the manual's screenshots as a file, for links from elsewhere (no page links them) |
| `/ptfm/editor/` | `dist/editor/editor.html` |

Nothing else (the debug build, the playground, the sources) is published.

## 2. How it is put together

- **`site/assemble.sh OUT`** builds nothing: it copies what the repository holds into `OUT` as the URLs above
  want it, puts `fw/VERSION`'s version where the landing says `@VERSION@`, and adds `.nojekyll` (the files are
  served as they are).
- **`.github/workflows/pages.yml`** runs it on every push to `main`, and by hand, and publishes `OUT` with
  GitHub's Pages actions. The repository's Settings -> Pages -> Source is "GitHub Actions". The firmware is
  never built there: what is published is what was committed in `dist/`.
- **A preview** is the same script into a folder served locally (`python3 -m http.server`), the folder named
  `ptfm` under the served root so the URLs are the site's.

## 3. The landing

One page, dark ("studio night"), for a phone first and a computer:

1. **The hero:** a picture of the FM-1 running PT/FM (section 5), `PT/FM` in the start screen's letter colours in
   the FM-1's own pixel font, POCKET TRACKER / FUSION MACHINE, one line on what it is, the four buttons (INSTALL
   first and lit as PLAY's LED), the licence and the version.
2. **What it is:** a tracker in the ProTracker tradition; a row of figures (voices, a pattern's size, the kinds of
   instrument, the effects, USB, the built-in instruments).
3. **The videos,** INTRODUCING PT/FM on YouTube (`youtu.be/95bZPtyD5rE`): a 16:9 panel of eight real screens
   (section 5), dimmed, under a play button lit as PLAY's LED and the video's title; the first press puts YouTube's
   player (`youtube-nocookie.com`, playing at once) in the panel's place. Until then nothing comes from YouTube; a
   press with a modifier key, or without scripts, opens YouTube as a link. WATCH ON YOUTUBE below it, a link.
   Below, a small card for the next video, PT/FM: UNDER THE HOOD (`youtu.be/M6I354-Xu5I`): its YouTube thumbnail
   (section 5) under a small play button, NEXT and its length, its title, one line on what it shows, WATCH ON YOUTUBE;
   pressed as the panel is, its player takes the thumbnail's place and the card widens to the panel's width (on a
   phone too, where the thumbnail is beside the title and the line below both).
4. **Features:** a real PT/FM screen each (section 5): the six kinds of instrument, 16 voices, effects and
   punch-ins, LOOPS, DJ, MIDI and sync, kept on the FM-1, music included (the demo disk links INSTALL).
5. **The four pages,** a card each with what it is for (INSTALL: Chrome or Edge, a computer or an Android phone;
   the EDITOR: a beta, a computer).
6. **The footer:** the repository, the licence, the root page; custom firmware at one's own risk; not affiliated
   with M-VAVE. The credits stay in the README.

- **Texts name no count that changes with the contents** (songs, cartridges, a document's pages): the firmware's
  fixed limits are named (16 voices, 16 tracks x 128 rows, 6 kinds, 9 effects + 16 punch-ins), a count of contents
  only rounded (`500+`). The version comes from `fw/VERSION` (section 2).
- **Support:** a small outlined pill at the top; a tap opens a card of the ways to support PT/FM, one list (now
  GitHub Sponsors), closed by a tap elsewhere or Escape. It is the same on the root page.
- **Fonts:** Archivo (the manual's, `assets/fonts/Archivo.woff2`), and for the logo and labels the FM-1's Terminus
  8x16 as outline webfonts, `site/fonts/ptfm-pixel.woff2` (every glyph 8 pixels wide, as on the screen) and
  `ptfm-pixel-prop.woff2` (letters as wide as their ink, digits 8 wide), named PTFM Pixel, both SIL OFL 1.1.
  Nothing is loaded from elsewhere until a video is played (item 3).

## 4. Links back

Each of the four pages links to the landing from its `PT/FM`: the install page's name, the EDITOR's `PT/FM` beside
`EDITOR` ([editor.md](editor.md)), the emulator's and the manual's logo. The link is absolute,
`https://hitosu.github.io/ptfm/`, as the pages also open from the file system.

## 5. Its pictures (`site/tools/`)

| Tool | Makes |
|---|---|
| `shots.sh` (`shots.c`) | `site/screens/*.png`: the screens, made as the manual's are (all of `fw/core` on the PC, the demo disk, a script a screen; core 2 taken as running, so VOICES 16 is 16); also STATUS's first page (`status-ptfm.png`, PT/FM's name), on the landing only in the video's panel, for links from elsewhere too |
| `tools/build_manual.py` | `site/screens/manual/*.png`: the manual's screenshots, made with the manual ([manual.md](manual.md)) |
| `hero.html`, `images.py hero` | `site/img/hero.webp`, `hero-phone.webp`, `og.jpg`: the FM-1 in 3D from `panel/layout.js` with the EDITOR's screen, rendered in the browser (three.js), saved as PNG, then cut to the page's sizes |
| `pixfont.py` | `site/fonts/ptfm-pixel*.woff2` from `assets/fonts/ter-u16n.bdf` |
| `images.py video` | `site/img/under-the-hood.webp`: the video's own YouTube thumbnail (1280 x 720), 640 px wide |

The screens are made again when the screens they show change, the picture when the panel or its screen does, a
video's thumbnail when YouTube's is replaced.
The FM-1's sides and thickness in the picture are not measured.
