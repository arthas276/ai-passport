#!/usr/bin/env python3
"""Hint-pill text-width contract for BREATHE screen hints.

The bottom hint pill is 232 px wide with a 216 px label area; every hint
line must stay within a 204 px budget so no text can approach the screen
edge. This test freezes the montserrat_14 advance-width table and
re-measures every hint_pill_set line in breath_ui.c (same pattern as
feature/ear-buddy tests/test_hint_budget.py).

Regenerate ADV16 from
managed_components/lvgl__lvgl/src/font/lv_font_montserrat_14.c
(glyph_dsc[].adv_w, 1/16 px units) whenever the bundled font changes.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAX_HINT_LINE_PX = 204.0

ADV16 = {
    " ": 60, "-": 86, "/": 79, ":": 51,
    "A": 164, "B": 170, "C": 162, "D": 185, "E": 150, "F": 142,
    "G": 173, "H": 182, "I": 69, "K": 161, "L": 133, "M": 214,
    "N": 182, "O": 188, "P": 162, "R": 163, "S": 139, "T": 131,
    "U": 177, "V": 159, "W": 252, "X": 151, "Y": 145,
}


def line_width_px(text: str) -> float:
    unknown = set(text) - set(ADV16)
    if unknown:
        raise AssertionError(f"hint charset not in frozen table: {sorted(unknown)}")
    return sum(ADV16[ch] for ch in text) / 16.0


def hint_lines(source: str) -> list[str]:
    lines: list[str] = []
    for match in re.finditer(r"hint_pill_set\(\s*\w+\s*,((?:[^;()]|\([^()]*\))*)\);", source):
        args = match.group(1)
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', args)
        if not strings:
            raise AssertionError("hint_pill_set call without a string literal")
        lines.extend(strings)
    if not lines:
        raise AssertionError("no hint_pill_set calls found in breath_ui.c")
    return lines


class HintBudgetTest(unittest.TestCase):
    def test_every_hint_line_fits_the_pill_budget(self) -> None:
        source = (ROOT / "main" / "breath_ui.c").read_text(encoding="utf-8")
        over = [
            (line, line_width_px(line))
            for line in hint_lines(source)
            if line_width_px(line) > MAX_HINT_LINE_PX
        ]
        self.assertEqual(
            over, [],
            f"hint lines exceed {MAX_HINT_LINE_PX} px text budget: {over}")


if __name__ == "__main__":
    unittest.main()
