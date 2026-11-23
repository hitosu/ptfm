# Proposal: recording an automation from any screen

Status: **settled** (2026-11-19); its specs are written with the work, and lead from then on. It changes
[../ui/screens/automations.md](../ui/screens/automations.md), [../ui/controls.md](../ui/controls.md) (HOME's
combination), [../ui/dialogs.md](../ui/dialogs.md), [../ui/leds.md](../ui/leds.md), [../ui/help.md](../ui/help.md),
the playground and the manual. The file ([../files/automations.md](../files/automations.md)) stays as it is.

HOME, ARP, then a white key plays an automation from any screen ([its proposal](202611191812-automations-compact.md));
recording one still went through AUTOMATIONS: HOME + OP6, a slot, REC, and HOME + OP6 again at the end, which opens
AUTOMATIONS. Recording is wanted from where the work is, the same way.

## 1. The start: HOME, ARP, REC, then a white key

- In HOME's combination, after ARP (the white keys the automations), **REC switches the white keys from playing to
  recording**, and back, again and again while they are members, as SEQ and ARP switch them; it is no longer CUT
  there. **White key *n* then records into slot *n* − 1**, as AUTOMATIONS' REC does: on the screen HOME's combination
  is over (on AUTOMATIONS itself, the screen AUTOMATIONS was opened from), from the first press after it. Taps, HOME
  tapped or held: nothing else needs holding.
- **A slot holding one asks first,** as AUTOMATIONS' REC does (`AUTOMATION 03 HOLDS 0:42` / `REPLACE IT?`), over the
  screen; KEEP leaves everything as it was.
- **While an automation records or plays,** none starts: `ONE AUTOMATION AT A TIME`, the combination staying open.
- None of HOME, ARP, REC and the white key is in the recording: it starts after them.

## 2. The end: HOME, ARP, REC

- **While a recording runs, HOME, ARP, REC ends it where it is,** whichever way it started: the screen stays as it
  is, `AUTOMATION 03 RECORDED: 0:42`. None of HOME, ARP and REC is in it: taken out as the end's HOME and OP6 are
  (when only their own events followed HOME's press). **HOME + OP6 ends it as before,** opening AUTOMATIONS.
- The other choices were HOME + OP6 alone (it lands on AUTOMATIONS every time) and HOME + OP6 ending every recording
  where it is (it would no longer open AUTOMATIONS while one records).

## 3. What shows

- The overlay after ARP: `WHITE KEY: PLAY ITS AUTOMATION`, after REC `WHITE KEY: RECORD ITS AUTOMATION`; the REC line
  `REC: RECORD INSTEAD`, `REC: PLAY INSTEAD` while the white keys record, `REC: END THE RECORDING` while one records
  (the white keys then playing, as ever, `ONE AUTOMATION AT A TIME`).
- The LEDs: REC full while the white keys record; the white keys then all dim (each records; one holding an automation
  asks first).
- The help: AUTOMATIONS' page and THE HOME KEY page name it ([../ui/help.md](../ui/help.md)).

## Room

About 270 B of the release's slot (620 B left): the switch and the start through AUTOMATIONS' own REC, the end through
the end HOME + OP6 already takes out of the stream.
