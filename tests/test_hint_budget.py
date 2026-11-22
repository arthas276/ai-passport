#!/usr/bin/env python3
"""Hint-pill text-width contract for BREATHE screen hints.

The bottom hint pill is 232 px wide with a 216 px label area; every hint
line must stay within a 204 px budget so no text can approach the screen
edge. Since v1.4 the pill renders breath_font_20 (WenKai 20 px subset)
and the hint text is passed as STR_* macros from main/breath_strings.h.

This test resolves those macros, parses the per-glyph advance widths out
of the generated main/breath_font_20.c (lv_font_conv emits a trailing
"/*<codepoint>" comment on every glyph_dsc line), and re-measures every
hint_pill_set line in breath_ui.c. ASCII-only literals (if any ever come
back) are measured against the frozen montserrat_14 table below.

Regenerate ADV16 from
managed_components/lvgl__lvgl/src/font/lv_font_montserrat_14.c
(glyph_dsc[].adv_w, 1/16 px units) whenever the bundled font changes.
The WenKai table is parsed live from the generated font - regenerate the
font via tools/breathe/gen_assets.mjs, never hand-edit it.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
MAX_HINT_LINE_PX = 204.0

ADV16 = {
    " ": 60, "-": 86, "/": 79, ":": 51,
    "A": 164, "B": 170, "C": 162, "D": 185, "E": 150, "F": 142,
    "G": 173, "H": 182, "I": 69, "K": 161, "L": 133, "M": 214,
    "N": 182, "O": 188, "P": 162, "R": 163, "S": 139, "T": 131,
    "U": 177, "V": 159, "W": 252, "X": 151, "Y": 145,
}


def wenkai_adv16() -> dict:
    """codepoint -> adv_w (1/16 px) parsed from breath_font_20.c."""
    src = (MAIN / "breath_font_20.c").read_text(encoding="utf-8")

    def arr(name: str) -> list:
        m = re.search(name + r"\[\]\s*=\s*\{([^}]*)\}", src)
        return [int(x, 0) for x in m.group(1).replace(",", " ").split()] if m else []

    adv = [int(m.group(1)) for m in re.finditer(r"\.adv_w\s*=\s*(\d+)", src)]
    table: dict = {}
    for e in re.finditer(
        r"\{\s*\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),"
        r"\s*\.glyph_id_start\s*=\s*(\d+),\s*\.unicode_list\s*=\s*(\w+),"
        r"\s*\.glyph_id_ofs_list\s*=\s*(\w+)",
        src,
    ):
        r0, rlen, g0, uni_name, ofs_name = (
            int(e.group(1)), int(e.group(2)), int(e.group(3)), e.group(4), e.group(5)
        )
        uni = arr(uni_name) if uni_name != "NULL" else None
        ofs = arr(ofs_name) if ofs_name != "NULL" else None
        if uni is not None:  # sparse: sequential glyph ids from g0
            for i, off in enumerate(uni):
                g = g0 + i
                if g < len(adv):
                    table[r0 + off] = adv[g]
        elif ofs is not None:  # dense with per-code offsets
            for i in range(min(rlen, len(ofs))):
                g = g0 + ofs[i]
                if 0 < g < len(adv):
                    table[r0 + i] = adv[g]
        else:  # fully dense run
            for i in range(rlen):
                if g0 + i < len(adv):
                    table[r0 + i] = adv[g0 + i]
    if not table:
        raise AssertionError("no glyph advance widths parsed from breath_font_20.c")
    return table


def str_macros() -> dict:
    """STR_* defines from the generated breath_strings.h."""
    src = (MAIN / "breath_strings.h").read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'#define\s+(STR_[A-Z0-9_]+)\s+"((?:[^"\\]|\\.)*)"', src):
        out[m.group(1)] = m.group(2)
    if not out:
        raise AssertionError("no STR_ defines parsed from breath_strings.h")
    return out


def line_width_px(text: str, wk: dict) -> float:
    total = 0
    for ch in text:
        if ord(ch) < 128:
            if ch not in ADV16:
                raise AssertionError(f"ASCII hint char not in frozen table: {ch!r}")
            total += ADV16[ch]
        else:
            if ord(ch) not in wk:
                raise AssertionError(f"glyph missing from breath_font_20.c: {ch!r}")
            total += wk[ord(ch)]
    return total / 16.0


def is_ascii(text: str) -> bool:
    return all(ord(c) < 128 for c in text)


def hint_lines(source: str) -> list[str]:
    lines: list[str] = []
    macros = str_macros()
    for match in re.finditer(r"hint_pill_set\s*\(\s*\w+\s*,((?:[^;()]|\([^()]*\))*)\);", source):
        args = match.group(1)
        names = re.findall(r"\bSTR_[A-Z0-9_]+\b", args)
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', args)
        if not names and not literals:
            raise AssertionError("hint_pill_set call without text (macro or literal)")
        for n in names:
            if n not in macros:
                raise AssertionError(f"{n} not defined in breath_strings.h")
            lines.append(macros[n])
        lines.extend(literals)
    if not lines:
        raise AssertionError("no hint_pill_set calls found in breath_ui.c")
    return lines


class HintBudgetTest(unittest.TestCase):
    def test_every_hint_line_fits_the_pill_budget(self) -> None:
        source = (MAIN / "breath_ui.c").read_text(encoding="utf-8")
        wk = wenkai_adv16()
        over = [
            (line, line_width_px(line, wk))
            for line in hint_lines(source)
            if line_width_px(line, wk) > MAX_HINT_LINE_PX
        ]
        self.assertEqual(
            [],
            over,
            "hint lines over budget: %s" % over,
        )

    def test_all_referenced_macros_exist_and_covered(self) -> None:
        # Every STR_ macro referenced by a hint call resolves and every one
        # of its non-ASCII characters is a glyph in the generated font.
        source = (MAIN / "breath_ui.c").read_text(encoding="utf-8")
        wk = wenkai_adv16()
        for line in hint_lines(source):
            for ch in line:
                if ord(ch) >= 128:
                    self.assertIn(
                        ord(ch), wk,
                        "hint character %r not in breath_font_20.c" % ch,
                    )


if __name__ == "__main__":
    unittest.main()
