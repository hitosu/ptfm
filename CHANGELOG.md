# Changelog

What changes in PT/FM from one version to the next: the firmware, its pages (install, emulator, editor, manual,
site) and the demo disk. The top section, **Next version**, collects the changes made since the last release; at a
release it takes that version's number and date, and a new Next version starts above it. Earlier changes are in the
git history.

Each change is one entry, its kind first:

- **FEATURE:** something new;
- **CHANGE:** something that works differently;
- **BUG:** something fixed;
- **REMOVED:** something taken out.

## Next version

- BUG: The EDITOR's WRITE refused a panel that fits the FM-1 when what the FM-1 already held (records that stay,
  blocks still taken by deleted records) left no room around it. It now writes the whole user area anew in that
  case, as the install page's REPLACE ALL does, and says so before writing; the FM-1 then holds exactly the panel.
- BUG: The site's pixel fonts (PTFM Pixel, PTFM Pixel Prop) drew each letter as far left as the blank columns at its
  left: in the screen cut an I stood 2 pixels left, so words read "MI DI" or "TI ME", and the landing's logo and
  figures sat a pixel or two off. Each letter now sits in its cell as Terminus draws it.
- FEATURE: The site's landing shows the second video, PT/FM: UNDER THE HOOD: a small card under the first video, its
  thumbnail, length and what it shows; pressed, it plays there at the first video's size.
