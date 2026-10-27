# The firmware's texts

Status: **settled** (2026-10-10). The help's tokenizer ([help.md](help.md)), extended to every fixed
text.

Every fixed text the firmware shows is stored packed, by one tokenizer: the help's pages and every
other text on the screens. One table and one decoder serve them all.

## What is packed

- **The fixed texts:** everything the firmware itself writes on the screen:
  - the screens' headers, item and property names, menu lines, questions, toasts and footers;
  - the values picked from lists (`STEREO`, `1 BAR`, the themes, the screens' names);
  - the names of the commands, effects, knobs and punch-ins, their short forms included;
  - ABOUT's lines, and the help's pages ([help.md](help.md)).

  These are the texts in every screen's Texts and Help parts ([screens/](README.md#the-files)).
- **Not packed:**
  - names that come with the data (songs, cartridges, instruments, samples): their records carry them;
  - numbers and values made as they are drawn: `500 MS`, `+3 DB`, `0B:05`, `C-4`.
- **A text with a value in it is one text with a placeholder:** `LOOP # CLEARED`, `DROP # UNSAVED
  CHANGES`, `MISSING REFERENCES: #`. The value goes in where it is drawn. The placeholder is a reserved
  token, as the help's colour tokens are, so it never clashes with a character the texts show (`#` is
  one: ARP RHYTHM's `#___`).

## Compression: a tokenizer

- **Its size:** about 24 KB as plain text, measured on the playground's texts: the help ~14 KB, the
  other texts ~10 KB in about 900 texts.
- **The method: byte-pair encoding,** as language models tokenize text, trained by the build on all
  the fixed texts together:
  - it starts from the characters (the fonts', the line break and the indent);
  - it merges the commonest pair of symbols into a new token, again and again, until the table is
    full; common words and phrases (`HOLD `, `SELECT: `, `UNSAVED`, `HOME COMES BACK HERE`) become
    single tokens;
  - the table stores each token as its two halves (2 bytes a token with one-byte tokens, 3 with 12-bit
    ones), never the spelled-out words;
  - a token is one byte (a table of 256) or 12 bits (up to 4,096). The build tries several table
    sizes and keeps the smallest total of texts, table and index.
- **One table for all:** the help and the screens' texts share their words, so one table packs them
  as small as two would, with one decoder.
- **Expected:** about 13 KB packed in all, the table included, for the 24 KB:
  - the help about 7 KB;
  - the other texts about 6 KB;
  - an index of where each text starts, 2 bytes a text (plain strings would need pointers too).

## In the firmware's source

- **Written as plain text where it is drawn,** inside a marker the build finds: `T("SAVED")`,
  `T("LOOP # CLEARED")`. The source stays readable.
- **A text kept in a table** (a menu's items, the properties, a list's values, the commands', effects'
  and screens' names) is marked `TX("REVERT")` where the table is written, and drawn through `T()`;
  in the firmware the table holds the text's number (the number 0 is no text).
- **Identifiers are not texts:** a word only compared, never shown (a screen looked up by its name, a
  menu's key), stays plain.
- **The build packs them:** a tool in `tools/`, run on every firmware build:
  - it collects every marked text from the sources, and the help's pages;
  - it stores a text used in several places once;
  - it numbers the texts, trains the tokenizer, and writes the packed texts, the index and the table
    as a C source for the firmware;
  - the compiler gets copies of the sources with each marker's text replaced by its number, so the
    firmware draws by number. The binary holds no plain copy.
- **A number always comes with its words,** so no code reads as a bare number:
  - in the copies the compiler gets, the text stays beside its number as a comment:
    `T("LOOP # CLEARED")` becomes `T(17 /* "LOOP # CLEARED" */)`;
  - in the generated C source, every text's entry has its text in a comment, and every token of the
    table its spelled-out words (`/* 0x93 "HOLD " */`);
  - code written by hand never names a text by its number; where it must, the comment beside the
    number gives the words.
- **The tool's checks:**
  - every character is in the fonts;
  - every placeholder is well formed;
  - the help's own checks, which it keeps ([help.md](help.md#compression)).

## Unpacking

- **Into a line buffer, for what is drawn:** each token expands through its halves with a small stack.
  The line buffers are a ring of 12 (a line's runs hold up to 8 texts at once); a text kept longer,
  as a panel's lines waiting to be drawn, is copied first.
- **A few µs a text:** even a full screen of texts unpacks in well under a millisecond, against a
  frame's tens of milliseconds.

## Tests

- **A host test** unpacks every text with the firmware's own code and compares it with its source,
  placeholders filled.
- **The screens' tests run twice:** built from the plain sources and from the packed copies, every
  check passes and every screen is the same, pixel for pixel; the packed build stops at a table's
  number passed as a text without `T()`.

## The playground

The playground draws the same texts as plain strings: the tokenizer belongs to the firmware's build
([../playground.md](../playground.md)).
