# Colours and themes

Status: **settled** (2026-10-09), the palette's values *(proposed)* ([open-questions.md](../open-questions.md#the-ui-ui)).
The record: [the UI proposal](../proposals/202610072229-ui.md), section 4.

The colour coding every screen follows: 16 colours on a ground (black, or a light theme's), each with a deep shade for fills; what
each colour means; and the themes, which change the palette and nothing else. The screens' files say
in words only what this coding does not already say ([screens/](README.md#the-files)).

## The default palette: soft EGA

**What changes from the exact EGA colours:**
- The 16 hues stay. The dark ones are lifted so they read on black, and the bright ones are softened a
  little.
- **Black stays black.**
- **Each colour has a deep shade** (about 30 %) to fill a band behind text: the playing row, a
  selection, a bar. It lies 30 % of the way from the ground (colour 0) to the colour: each of red, green and
  blue is the ground's plus 3/10 of the difference, the fraction dropped (toward the ground). On black that is
  the colour with each of red, green and blue at 30 %, rounded down; on a light ground, a pale tint.
- *(proposed)* These are first values, tuned in the playground; to look at on the FM-1's screen.

| # | EGA | EGA RGB | PT/FM default | Main roles |
|---|---|---|---|---|
| 0 | black | `000000` | `000000` | background |
| 1 | blue | `0000AA` | `4C5CD0` | (deep shade) the playing row's band |
| 2 | green | `00AA00` | `4AAA4A` | on / OK states, playing, the PLAY group |
| 3 | cyan | `00AAAA` | `3AA8A8` | labels, beat-row numbers |
| 4 | red | `AA0000` | `C84C44` | recording, warnings, missing references |
| 5 | magenta | `AA00AA` | `B054B0` | (deep shade) a marked block; the SYSTEM group |
| 6 | brown | `AA5500` | `C87C34` | volumes, the HEAR group, a built-in thing's number in a list, as the firmware has it ([saving.md](saving.md#fws)) |
| 7 | light grey | `AAAAAA` | `B4B4AC` | normal text, note offs |
| 8 | dark grey | `555555` | `5C5C5C` | grid lines, empty cells, muted tracks |
| 9 | light blue | `5555FF` | `7C8CF8` | command columns, **CHIP**, the MAKE group |
| 10 | light green | `55FF55` | `84E884` | **TB-303** |
| 11 | light cyan | `55FFFF` | `84E8E8` | **Juno-106** |
| 12 | light red | `FF5555` | `F87C74` | **DX7** |
| 13 | light magenta | `FF55FF` | `E884E8` | **FUSION** |
| 14 | yellow | `FFFF55` | `F0E074` | **samples**, unsaved |
| 15 | white | `FFFFFF` | `F4F4EC` | bright text, the cursor |

## What carries meaning

- **Colour tells the kind:**
  - the engine (DX7, Juno-106, TB-303, CHIP, samples, FUSION); CHIP shares light blue with the command
    columns: a cell's note and instrument stand at its left, its commands at its right, so the place tells them
    apart (on a CHIP track the EDITOR's `*40` sign is light blue as its commands; the sign itself says "a
    parameter");
  - the field (volume, command);
  - the state (playing, recording, missing, built in).
- **Brightness tells importance:** bright for the cursor and active things, normal for values, faint
  for empty and muted.
- **Shapes mark positions, never colour alone:**
  - the cursor is a box;
  - the playing row is a band;
  - the focus row is larger, with lines above and below;
  - a marked block is a filled area.

  This keeps every theme usable, the one-colour themes included.
- **Deep shades never carry text on black;** they only fill.
- **Text on a solid colour** (a held punch-in's chip, red) is white, or black where the theme's colour is nearer
  white than black by its luma (0.2126 R + 0.7152 G + 0.0722 B), black being the ground: in the one-colour themes
  red is nearly as bright as white, and in the light ones a mid red is nearer the light ground than the dark
  white, so their text takes the ground's colour.
- **A count of voices** (the voices sounding, or the most since a reset) is coloured against the limit in force
  ([screens/settings.md](screens/settings.md#voices)): red at the limit and above (every voice in use), yellow from
  4 under it, light grey below; at 12 light grey under 8, yellow from 8, red from 12; at 16 yellow from 12, red from
  16; at 8 yellow from 4, red from 8. On every screen; a limit written after it (`06/12`) takes the count's colour.
- **The screens' groups** colour their names in the header ([frame.md](frame.md#the-header)) and in
  the help ([help.md](help.md)): MAKE light blue, PLAY green, HEAR brown, SYSTEM magenta; LOOPS' and
  DJ's names are in the PLAY group's colour ([navigation.md](navigation.md)).
- **In cells:**

  | Part | Colour |
  |---|---|
  | the note and the instrument | the instrument's engine colour |
  | volume | brown |
  | command | light blue |
  | `---` and dots | dark grey |
  | `===` (note off) | light grey |
  | a missing instrument | red on its deep shade |

## Themes (SETTINGS → THEME)

**How themes work:**
- Every screen draws by role (label, value, DX7, cursor...), never by a fixed colour, so a theme is
  just a palette.
- Switching a theme changes no layout.

**The themes.** Ten are on black, six on a light ground or a blue one. A theme is 16 colours (32 bytes), so there
can be many.

| Theme | Kind | What |
|---|---|---|
| **DEFAULT** | colour | the soft EGA above |
| **EGA** | colour | the exact EGA colours |
| **PASTEL** | colour | the same hues, lighter and softer: chalk on a blackboard |
| **NEON** | colour | saturated magenta, cyan, violet and lime: a night-time arcade |
| **SUNSET** | colour | warm: reds, oranges, golds, with teal for contrast |
| **ICE** | colour | cool: blues, teals, violet, white, with orange for contrast |
| **CLEAR** | colour | for colour-blind eyes: the engines and states use the Okabe-Ito colours (orange, sky blue, bluish green, yellow, blue, vermilion, reddish purple), which stay distinct with the common kinds of colour blindness |
| **GREEN** | one colour | green phosphor: every role is one of about 6 intensities of green |
| **AMBER** | one colour | the same in amber |
| **WHITE** | one colour | the same in grey |
| **DAYLIGHT** | light, colour | DEFAULT's colours on a light warm grey, each darkened to read on it |
| **PAPER** | light, colour | black on white like a document, the colours as printed |
| **MANUAL** | light, colour | an old manual's off-white page, the ink colours a little muted |
| **SILVER** | light, one colour | a light grey ground, every role a shade of dark ink: GREEN's idea, light |
| **LCD** | light, one colour | a reflective LCD: a pale olive ground, dark green ink |
| **COMMANDER** | blue | a DOS file manager's panels: a blue ground, cyan text, yellow headers, white for what is picked |

**The light themes** (and COMMANDER) keep every role's meaning with brightness turned round: what stands out is
what differs most from the ground, so on a light ground the darkest colour is the brightest role (bright text, the
cursor), normal text is dark, dark grey (empty cells, grid lines) a mid grey lighter than it, and each colour is
deep enough to read as text on the ground. Black (role 0) is the ground everywhere: what is drawn in it, as a
digit on a white box or the lines over DJ's overview, takes the ground's colour. FOMA flies by day in them
([screens/visual.md](screens/visual.md#foma)).

**In the colour themes:**
- Every theme keeps the 5 engines' colours, the states (playing, recording, missing) and the fields (volume,
  command) apart: 10 roles (CHIP takes the command's, as above).
  - Most themes give each its own hue. In DEFAULT, recording and a missing reference share red: a
    missing one is always drawn on red's deep shade, so the two never look alike.
  - CLEAR has only 7 Okabe-Ito colours. The 5 engines get 5 of them, and the states and fields share
    the other 2 and white, told apart by brightness.
  - CLEAR's groups: MAKE keeps blue (`3A92D8`), PLAY gets a green of its own (`6CC070`, outside the
    Okabe-Ito set, used by no engine: blue against green stays apart in the common colour blindness),
    HEAR tan (`A87848`), SYSTEM purple, so no two groups' names look alike, nor a name and its white
    subpage ([frame.md](frame.md#the-header)).
- The playground shows every theme on every screen.

**In the one-colour themes:**
- The engines are told apart by intensity and by the instrument ID in the cell, not by hue.
- [What carries meaning](#what-carries-meaning) keeps the cursor, the playing row and selections
  visible.

### The themes' colours

Each theme's 16 colours, by role (the default palette's numbers); every one's deep shade is made as
DEFAULT's is.

| # | Role | DEFAULT | EGA | PASTEL | NEON | SUNSET | ICE | CLEAR |
|---|---|---|---|---|---|---|---|---|
| 0 | black | `000000` | `000000` | `000000` | `000000` | `000000` | `000000` | `000000` |
| 1 | blue | `4C5CD0` | `0000AA` | `8890D8` | `3A2AE0` | `2A6A80` | `3050C0` | `0072B2` |
| 2 | green | `4AAA4A` | `00AA00` | `8CC88C` | `30E060` | `8AB04A` | `40B0A0` | `6CC070` |
| 3 | cyan | `3AA8A8` | `00AAAA` | `8CC4C4` | `00D0E0` | `3AA0A0` | `58A8D8` | `A0A0A0` |
| 4 | red | `C84C44` | `AA0000` | `D88C8C` | `FF2A6A` | `E0442E` | `F07840` | `D55E00` |
| 5 | magenta | `B054B0` | `AA00AA` | `C890C8` | `B030FF` | `A84870` | `8A60D0` | `8A5A84` |
| 6 | brown | `C87C34` | `AA5500` | `D8AC84` | `FF8A00` | `D0803A` | `D89858` | `A87848` |
| 7 | light grey | `B4B4AC` | `AAAAAA` | `C8C8C0` | `C8C8E0` | `D8C4AC` | `B8C8D8` | `C0C0C0` |
| 8 | dark grey | `5C5C5C` | `555555` | `6C6C6C` | `545474` | `6A5A50` | `506070` | `606060` |
| 9 | light blue | `7C8CF8` | `5555FF` | `A8B0F0` | `8A6AFF` | `50C0BC` | `88ACF8` | `3A92D8` |
| 10 | light green | `84E884` | `55FF55` | `B8F0B8` | `A8FF30` | `F0C048` | `70E0D0` | `009E73` |
| 11 | light cyan | `84E8E8` | `55FFFF` | `B8F0F0` | `30F0FF` | `60D4CC` | `98DCF8` | `56B4E9` |
| 12 | light red | `F87C74` | `FF5555` | `F8B4AC` | `FF4AA0` | `FF7A48` | `C4A4F8` | `E69F00` |
| 13 | light magenta | `E884E8` | `FF55FF` | `F0B8F0` | `FF50FF` | `F08AA8` | `E8B4F8` | `CC79A7` |
| 14 | yellow | `F0E074` | `FFFF55` | `F4ECAC` | `FFF030` | `FFD870` | `F8C080` | `F0E442` |
| 15 | white | `F4F4EC` | `FFFFFF` | `F8F8F0` | `FFFFFF` | `FFF2E4` | `F0F8FF` | `FFFFFF` |

The light themes and COMMANDER:

| # | Role | DAYLIGHT | PAPER | MANUAL | SILVER | LCD | COMMANDER |
|---|---|---|---|---|---|---|---|
| 0 | black: the ground | `D0D0C8` | `FFFFFF` | `F4EEDC` | `D4D4D0` | `C8D2AA` | `0000A8` |
| 1 | blue | `5060D0` | `3C78E0` | `4A68B0` | `949492` | `8F9978` | `000000` |
| 2 | green | `2F762F` | `29953E` | `48862F` | `51514F` | `525C43` | `54D454` |
| 3 | cyan | `227171` | `2F7F8A` | `3A7070` | `5F5F5E` | `5F694F` | `C8C854` |
| 4 | red | `B73A33` | `D73A30` | `C04030` | `0B0B0A` | `141E0C` | `E46464` |
| 5 | magenta | `A040A0` | `B03A90` | `9A4A8A` | `757572` | `737D5F` | `C570C5` |
| 6 | brown | `A64B1A` | `B0481A` | `A04A1C` | `595957` | `5A644A` | `E0A050` |
| 7 | light grey: normal text | `2C2C28` | `1C1C1C` | `2A2620` | `373736` | `3B452F` | `00C0C0` |
| 8 | dark grey: dim | `767671` | `999999` | `958E7F` | `7B7B79` | `788264` | `6464EB` |
| 9 | light blue | `3742BE` | `0A54C8` | `2A4AA0` | `595957` | `5A644A` | `A0A0FF` |
| 10 | light green | `175D17` | `177837` | `386B1F` | `2A2A2A` | `303A25` | `80FF80` |
| 11 | light cyan | `085A61` | `0B6E7A` | `1A6A70` | `40403E` | `434D36` | `A8FFFF` |
| 12 | light red | `9E221A` | `C8201E` | `B02A20` | `111111` | `192311` | `FF8070` |
| 13 | light magenta | `832592` | `8040D0` | `7A3A8A` | `484847` | `4B553C` | `FF80FF` |
| 14 | yellow | `5C5300` | `766A00` | `695E00` | `1E1E1D` | `252F1B` | `FFFF54` |
| 15 | white: bright text | `000000` | `000000` | `0A0806` | `000000` | `0A1404` | `FFFFFF` |

COMMANDER's blue (role 1) is black: the playing row's band, its deep shade, a darker blue.

**Made to read** (contrast against the ground, WCAG's measure): in DAYLIGHT, PAPER and MANUAL the base colours
(green, cyan, red, magenta, brown) at least 3.6:1, the light ones (light blue, the engines, yellow) at least 5:1,
above the base as on black, so a label and a Juno-106 note stay apart; normal text at least 9:1; dim about 2.8:1,
fainter than everything but still there over the deep shades. Brown leans to rust and yellow to olive gold, so
volumes and samples stay apart on a light ground. COMMANDER keeps its cyan text (5.9:1), its other colours at
least 4:1, dim 2.9:1.

**The one-colour themes** scale one hue, each of its red, green and blue by the role's intensity,
rounded: GREEN `3CFF6C`, AMBER `FFB030`, WHITE `E8E8E8`. The light ones go the same way from their ground to an
ink, each of red, green and blue the ground's plus the intensity times the difference, rounded, with dark grey at
0.42 (0.34 leaves it too faint on a light ground): SILVER from `D4D4D0` to `000000`, LCD from `C8D2AA` to `0A1404`.

| # | Role | Intensity | # | Role | Intensity |
|---|---|---|---|---|---|
| 0 | black | 0 | 8 | dark grey | 0.34 |
| 1 | blue | 0.30 | 9 | light blue | 0.58 |
| 2 | green | 0.62 | 10 | light green | 0.80 |
| 3 | cyan | 0.55 | 11 | light cyan | 0.70 |
| 4 | red | 0.95 | 12 | light red | 0.92 |
| 5 | magenta | 0.45 | 13 | light magenta | 0.66 |
| 6 | brown | 0.58 | 14 | yellow | 0.86 |
| 7 | light grey | 0.74 | 15 | white | 1 |
