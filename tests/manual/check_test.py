# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's completeness checks (tests/manual/check_manual.py) on small hand-made pages. From ptfm/:
    python3 -I tests/manual/check_test.py"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_manual as cm  # noqa: E402

fails = checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", what)


S = cm.Shortcut
# the help's lines as shortcuts
check(cm.line_shortcuts("{SEL} + {PRESETS}: ITS CARTRIDGE") == [S({"SEL", "PRESETS"}, False)], "a combination")
check(cm.line_shortcuts("HOLD {ARP}: RHYTHM, ORDER, LATCH") == [S({"ARP"}, True)], "a hold")
check(cm.line_shortcuts("{OP4}: A ROW, {OP5}: REAL TIME") == [S({"B:OP4"}, False), S({"B:OP5"}, False)],
      "two on one line")
check(cm.line_shortcuts("{GLO} KEY: MUTE OR SOLO") == [S({"B:GLO"}, False)], "{GLO} KEY is the black key")
check(cm.line_shortcuts("{GLO}: STEREO OR MONO VIEW") == [S({"GLO"}, False)], "{GLO} alone is the button")
check(cm.line_shortcuts("{GLO}: [STATUS]") == [S({"B:GLO"}, False)], "{GLO} opening a screen is the black key")
check(cm.line_shortcuts("HOLD {PLAY} + {KEY}: START ON THAT NOTE") == [S({"PLAY", "KEYS"}, True)], "{KEY} is a note key")
check(cm.line_shortcuts("{WHITE KEYS}, ITS NOTES DOWN THE TRACK") == [S({"WHITE KEY"}, False)], "{WHITE KEYS}")
check(cm.line_shortcuts("  ON PATTERN, {K1} OR {ENV}: ANOTHER") == [S({"K1"}, False), S({"ENV"}, False)],
      "OR: alternatives, each its own")
check(cm.line_shortcuts("  {REC} OFF: {KEYS} PLAY IT; {HOME}: BACK") ==
      [S({"REC"}, False), S({"KEYS"}, False), S({"HOME"}, False)], "; separates")
check(cm.line_shortcuts("  {K1} STEP SIZE, {K2} LENGTH,") == [S({"K1"}, False), S({"K2"}, False)],
      "a line with no colon: its commas separate")
check(cm.line_shortcuts("{HOME}, {WHITE KEY}: MUTE A TRACK") == [S({"HOME", "WHITE KEY"}, False)],
      "a comma before a colon is a combination")
check(cm.line_shortcuts("{SEQ} (SHOW ELSE, QUICK) OR {K2}:") == [S({"SEQ"}, False), S({"K2"}, False)],
      "OR after a reading")
check(cm.line_shortcuts("{OP3}, BLACK {GLO}: A DECK'S FILTER") == [S({"B:OP3", "B:GLO"}, False)], "BLACK {GLO}")
check(cm.help_page_shortcuts("PADS\n{OP1}: PLAY\n5-8: STOPS\n  STOP WHILE HELD; LET GO: BACK") ==
      [S({"B:OP1"}, False)], "a line with no key and no indent ends what goes on")
check(cm.line_shortcuts("CONTROL: THE {KEYS}") == [], "a page's title names no shortcut")
check(cm.line_shortcuts("  HELD: SOLO", parent=S({"HOME", "WHITE KEY"}, False)) == [S({"HOME", "WHITE KEY"}, True)],
      "a HELD line goes on from its parent")
check(cm.line_shortcuts("{ARP*} (AGAIN, REPEAT, PLEASE): RECORD") == [S({"ARP"}, False)], "a key's reading")
check(cm.line_shortcuts("{PLAY}: PLAY / PAUSE, HOLD: STOP", parent=S({"LFO"}, False)) ==
      [S({"PLAY"}, False), S({"PLAY"}, True)], "a HOLD part goes on from its own line's key")
check(cm.line_shortcuts("{K1}-{K4}: LEVEL, PAN, DELAY, REVERB") == [S({"K1", "K2", "K3", "K4"}, False)], "K1-K4")
check(cm.pick_template("LOOP 03", {"#", "LOOP %s"}) == "LOOP %s" and cm.pick_template("X", {"#"}) is None,
      "a template of placeholders alone is no message")
# the controls specs' first cells
check(cm.cell_shortcut("SEL + PRESETS (SEL held, or tapped first)") == S({"SEL", "PRESETS"}, None), "a key map row")
check(cm.cell_shortcut("HOME + white key") == S({"HOME", "WHITE KEY"}, None), "HOME + white key")
check(cm.cell_shortcut("SAVE held") == S({"SAVE"}, True), "a held cell")
check(cm.cell_shortcut("the screen's thing") is None, "a cell naming no key")
check(cm.cell_shortcut("FX + white key").held is None, "a combination cell says nothing of a hold")
check(cm.satisfied(cm.cell_shortcut("FX + white key"), [(S({"FX", "WHITE KEY"}, True), "")]),
      "... and a held row satisfies it")

page = cm.Page("""<main><section class="chapter" id="fx"><h2>FX</h2>
<table class="keys"><thead><tr><th>Key</th><th>Does</th></tr></thead><tbody>
<tr data-k="x"><td><kbd>ARP</kbd></td><td>holds a punch-in on</td></tr>
<tr data-k="x"><td><kbd class="w">5</kbd> held</td><td>a punch-in</td></tr>
<tr data-k="x"><td><kbd class="b">OP4</kbd> <kbd class="b">OP5</kbd></td><td>quantize</td></tr>
</tbody></table>
<dl class="params"><dt>DRIVE</dt><dd>x</dd><dt>R1, R2</dt><dd>y</dd></dl>
<p><samp class="msg" data-t="# MUTED">LOOP 02 MUTED</samp> <samp class="msg">NOPE</samp></p>
</section></main>""")
rows = page.rows("fx")
check(S({"ARP"}, False) in [r for r, _ in rows], "a row's keys")
check(S({"WHITE KEY"}, True) in [r for r, _ in rows], "a numbered white key counts as WHITE KEY, held")
check(cm.satisfied(S({"B:OP5"}, False), rows), "a black key in a row of two")
check(cm.satisfied(S({"B:*"}, False), rows), "BLACK KEY by any black key")
check(not cm.satisfied(S({"ARP"}, True), rows), "a hold is not a tap")
check(not cm.satisfied(S({"SEL", "PRESETS"}, False), rows), "a shortcut missing")
check(cm.find_text(rows, {"ARP"}, "punch-in"), "the user's example: ARP with punch-in")
check(page.dts("fx") >= {"DRIVE", "R1", "R2"}, "<dt> names, a list split")
texts = {"# MUTED", "QUANTIZE: #"}
f = cm.check_quoted(page, texts)
check(len(f) == 1 and "NOPE" in f[0], "a quoted message that is not the firmware's")
check(cm.check_quoted(cm.Page('<main><samp class="msg">A  B</samp></main>'), {"A  B"}) == [],
      "a firmware text's double space matches the page's (spaces count as one)")
check(cm.template_re("# MUTED").fullmatch("LOOP 02 MUTED") and not cm.template_re("# MUTED").fullmatch("LOOP 02 ON"),
      "a template's #")
check(cm.pick_template("LOOP 03 MUTED", {"# MUTED", "LOOP %s"}) == "# MUTED", "the most literal template")
check(cm.check_combos('<p><kbd data-k="a">A</kbd> + <kbd data-k="b">B</kbd></p>') != [], "two keys + outside a combination")
check(cm.check_combos('<span class="combo" data-k="a b"><kbd>A</kbd> + <kbd>B</kbd></span>') == [], "inside one")
check(cm.check_never("<p>SCORE: 3</p>") != [] and cm.check_never("<p>the score of a song</p>") == [], "never mentioned")
check(cm.check_never("<li>FOMA flies; <kbd>K4</kbd> lifts it.</li>") != [] and
      cm.check_never("<li>FOMA flies across the screen.</li><li><kbd>K4</kbd> sets GAIN.</li>") == [],
      "never: the touch that steers FOMA, only where one place names both")
check(cm.check_chars("<body><p>a ❖ b</p></body>", "ab ") != [], "a character the font lacks")
check(cm.check_chars("<body><style>❖</style><p>a b</p></body>", "ab ") == [], "styles are not text")

print("check_test: %d of %d checks FAILED" % (fails, checks) if fails else "check_test: %d checks passed" % checks)
sys.exit(1 if fails else 0)
