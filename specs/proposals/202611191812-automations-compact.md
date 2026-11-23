# Proposal: AUTOMATIONS' COMPACT, and playing one from any screen

Status: **settled** (2026-11-19); its specs are written with the work, and lead from then on. It changes
[../ui/screens/automations.md](../ui/screens/automations.md), [../ui/controls.md](../ui/controls.md) (HOME's
combination), [../ui/dialogs.md](../ui/dialogs.md), [../ui/leds.md](../ui/leds.md), [../ui/help.md](../ui/help.md),
the playground and the manual. The file ([../files/automations.md](../files/automations.md)) stays as it is.

An automation serves two ways of working: playing music again (its pauses are the music's: kept), and doing again some
work on the screens, done at a hand's pace and wanted back as fast as it can go. The second wants the pauses shortened,
and a way to start an automation without going to AUTOMATIONS first.

## 1. COMPACT

- **SEL's menu** on AUTOMATIONS: RECORD, PLAY, **COMPACT**, CLEAR, REVERT. COMPACT opens a list of limits, `1 S`,
  `0.5 S`, `0.3 S` (as QUANTIZE's list does on LOOPS); the one picked compacts the highlighted slot.
- **Only the pauses while nothing is held are shortened:** a pause from an event after which no key or button of the
  automation's is down to the next event, longer than the limit, becomes the limit. A pause while something is held
  is part of what that control does and keeps its time: a hold (HOLD DELAY), the 2-second holds (FX held, EDIT held on
  LOOPS, HOME + REC held), a knob turned with SEL held, a note's length. Every event stays, in its order. The other
  choices were every pause (the 2-second holds and the notes would change) and only from a release to the next press
  (the pauses before a knob's turn would stay).
- **The limits:** 0.3 s is the shortest above COMBO GRACE's 250 ms, the longest it can be set to (a button let go
  within it still makes a combination with the next press). A slot whose steps wait for the music (DJ's LOAD at the
  bar, LOOPS' QUANTIZE line) takes a longer one; compacting again with a shorter one goes further.
- **An edit,** as a recording: SAVE keeps it, REVERT drops it. The file's format does not change: a compacted slot is
  events as any other, and the EDITOR reads it as it is.

## 2. Playing one from any screen: HOME, ARP, then a white key

- HOME + a white key is taken on every screen (a mute; held, a solo). **ARP joins HOME's combination** as SEQ is in it:
  it switches the white keys to the automations, and back to the mutes; **white key *n* plays slot *n* − 1** (the
  numbers AUTOMATIONS' own white keys pick), on its screen, as AUTOMATIONS' PLAY does. HOME tapped first or held, as
  every combination; nothing needs holding. The other choices were HOME + OP6 held then a white key (AUTOMATIONS
  still opens) and HOME, ARP playing the last slot played (one slot only).
- What it costs: HOME tapped, then ARP, no longer switches the arpeggiator (ARP alone still does).

## 3. Playing waits while the FM-1 takes no input

A save's progress takes no input: a playback's events given meanwhile were lost, and a compacted pause after SAVE
makes that likely. **While the FM-1 takes no input, a playback's time stands still,** and it goes on from there after.
