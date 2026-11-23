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

- **Its size:** 1,923 texts (50 of them the help's pages), 46,975 B as plain text; **20,727 B packed**,
  the table and the index included (1,922 tokens of 1,854 merges, Huffman-coded in 6 to 13 bits). The FM-1's debug
  build, without the help's pages ([../debug.md](../debug.md#made-and-installed)): 1,874 texts, 11,022 B packed.
- **The method: byte-pair encoding,** as language models tokenize text, trained by the build on all
  the fixed texts together:
  - it starts from the characters (the fonts', the line break and the indent);
  - it merges the commonest pair of symbols into a new token, again and again; common words and
    phrases (`HOLD `, `SELECT: `, `UNSAVED`, `HOME COMES BACK HERE`) become single tokens;
  - a token is as many bits as the table's size needs (8 up to 256 tokens, 9 up to 512, 10 up to
    1,024, ...). The build tries every table size, one merge at a time, and keeps the smallest total
    of texts, table and index;
  - the table stores each token as its two halves, bit-packed at the tokens' width, never the
    spelled-out words;
  - **each text is then spelled with the fewest tokens of the table** (any token anywhere, found by
    dynamic programming), not in the order the merges were made: the decoder only expands tokens,
    so any spelling unpacks the same;
  - **the tokens are Huffman-coded,** each by how often the texts use it: a canonical code of 16 bits at most, the
    tokens numbered in the codes' order, so the decoder keeps only where each length's codes end (16 numbers) and
    what turns a code of that length into its token (16 more). Every token has its two halves in the table, at the
    width the tokens' count needs; a character's first half is all ones and its second the character.
- **One table for all:** the help and the screens' texts share their words, so one table packs them
  as small as two would, with one decoder.
- **The index:** each text's length in tokens in 3 bits, bit-packed (a length of 7 or more: the 3 bits all ones,
  then 8 bits), and for every 16th text the bit its codes start at (2 bytes, in bits of 4: a sixteen's codes start on
  such a bit) and the bit its length is at (2 bytes); a text is found from its sixteen's start, the tokens of the
  texts before it there read and passed over.
- **The texts are numbered by their length in tokens,** shortest first (the empty one 0): a sixteen holds texts of
  about the same length, so the short ones, most of what is drawn, pass over few tokens.
- **Packing takes a few seconds,** so the build keeps its result in its own folder, keyed by the texts
  and the tool: a build whose texts did not change reads it back.

## In the firmware's source

- **Written as plain text where it is drawn,** inside a marker the build finds: `T("SAVED")`,
  `T("LOOP # CLEARED")`. The source stays readable.
- **A text kept in a table** (a menu's items, the properties, a list's values, the commands', effects'
  and screens' names) is marked `TX("REVERT")` where the table is written, and drawn through `T()`;
  in the firmware the table holds the text's number, 2 bytes (the number 0 is no text).
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

- **Into a line buffer, for what is drawn:** each token's code is read from the next 16 bits taken as a number (its
  length the first whose codes end past it), then the token expands through its halves with a small stack.
  The line buffers are a ring of 12 (a line's runs hold up to 8 texts at once); a text kept longer,
  as a panel's lines waiting to be drawn, is copied first.
- **A few µs a text:** about 1,600 instructions on average as the screens ask for them (about 4.4 µs), the longest
  of the screens' texts about 14,000 (39 µs), a help page up to 170,000 (0.5 ms, once as it opens). The busiest main
  loop pass of the UI's tests unpacks 746 texts in about 3.4 ms, the 99th in a hundred under 0.4 ms, against a
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
