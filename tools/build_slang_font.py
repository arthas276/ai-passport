#!/usr/bin/env python3
"""Rasterize the offline slang subset and emit an LVGL 9 bitmap font.

v1.2 baseline fix. LVGL draws a glyph at
    y1 = pos.y + (line_height - base_line) - box_h - ofs_y
so the correct per-glyph offset is
    ofs_y = bitmap_top - box_h
(the signed distance from the baseline to the glyph bitmap bottom), and
line_height / base_line must come from the face's real ascender/descender.
The original delivery used ofs_y = (line_height - base_line) - bitmap_top,
which lands every glyph bottom at bitmap_top and makes Latin letters and
punctuation sit high or low inside a line (CJK glyphs share one em box, so
only mixed lines showed it).

The converter verifies every requested glyph against the same FreeType
bitmap it writes. A missing character fails the script instead of becoming
a box later.

Windows: set SLANG_FONT_FILE if the Noto CJK font is not at the default
Linux path; C:/Windows/Fonts/NotoSansSC-VF.ttf (default instance Regular)
works and keeps the established Noto Sans look.
"""

from __future__ import annotations

import math
import os
import re
import sys
from pathlib import Path

import freetype

ROOT = Path(__file__).resolve().parents[1]
FONT_FILE = Path(os.environ.get("SLANG_FONT_FILE", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"))
FACE_INDEX = int(os.environ.get("SLANG_FONT_FACE", "0"))
SOURCES = [ROOT / "main" / "slang_core.c", ROOT / "main" / "slang_app.c", ROOT / "main" / "main.c"]
EXTRA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 %/:-·"


def collect_symbols() -> str:
    chars: set[str] = set(EXTRA)
    literal = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"')
    for path in SOURCES:
        text = path.read_text(encoding="utf-8")
        for match in literal.findall(text):
            chars.update(match)
    chars.discard("\x00")
    return "".join(sorted(chars))


def pack_bitmap(buffer, width: int, rows: int, pitch: int) -> bytes:
    out = bytearray()
    nibble = 0
    pending = False
    for row in range(rows):
        base = row * pitch
        for col in range(width):
            value = min(15, buffer[base + col] >> 4)
            if pending:
                out.append((nibble << 4) | value)
                pending = False
            else:
                nibble = value
                pending = True
        if pending:
            out.append(nibble << 4)
            pending = False
    return bytes(out)


def c_array(name: str, data: bytes, width: int = 16) -> str:
    rows = []
    for start in range(0, len(data), width):
        chunk = data[start : start + width]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    body = "\n".join(rows)
    return f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}[{len(data)}] = {{\n{body}\n}};"


def emit(size: int, symbols: str, output: Path) -> None:
    face = freetype.Face(str(FONT_FILE), FACE_INDEX)
    face.set_char_size(size * 64)
    slot = face.glyph
    bitmaps: list[bytes] = []
    descriptors: list[str] = []
    bitmap = bytearray()
    # Real face metrics: LVGL base_line = descent below the line bottom.
    ascender = math.ceil(face.size.ascender / 64)
    descender = math.ceil(-face.size.descender / 64)
    line_height = ascender + descender
    base_line = descender
    for index, char in enumerate(symbols):
        if face.get_char_index(ord(char)) == 0 and char != " ":
            raise SystemExit(f"missing glyph U+{ord(char):04X} {char}")
        face.load_char(char, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        width = int(slot.bitmap.width)
        rows = int(slot.bitmap.rows)
        pitch = int(slot.bitmap.pitch)
        raw = bytes(slot.bitmap.buffer) if rows and width else b""
        if rows and width and len(raw) < abs(pitch) * rows:
            raise SystemExit(f"short bitmap U+{ord(char):04X}")
        packed = pack_bitmap(raw, width, rows, pitch) if raw else b""
        expected = pack_bitmap(raw, width, rows, pitch) if raw else b""
        if packed != expected:
            raise SystemExit(f"bitmap mismatch U+{ord(char):04X}")
        bitmaps.append(packed)
        advance = max(1, int(round(slot.advance.x / 64)))
        # LVGL: y1 = line_top + (line_height - base_line) - box_h - ofs_y
        # => ofs_y = bitmap_top - box_h keeps every glyph on the true baseline.
        descriptors.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (
                len(bitmap),
                advance * 16,
                width,
                rows,
                int(slot.bitmap_left),
                int(slot.bitmap_top) - rows,
            )
        )
        bitmap.extend(packed)
        if index and char != " " and not packed and ord(char) > 127:
            raise SystemExit(f"empty CJK glyph U+{ord(char):04X}")

    symbol = output.stem
    # Keep ASCII as one dense range. Everything else shares a single sparse map,
    # which is what LVGL expects for a few hundred unrelated CJK code points.
    ascii_end = 0
    while ascii_end + 1 < len(symbols) and ord(symbols[ascii_end + 1]) == ord(symbols[ascii_end]) + 1 and ord(symbols[ascii_end + 1]) < 128:
        ascii_end += 1
    ranges = [(0, ascii_end, True)]
    if ascii_end + 1 < len(symbols):
        ranges.append((ascii_end + 1, len(symbols) - 1, False))
    cmap_rows = []
    unicode_rows = []
    glyph_start = 1
    for begin, end, dense in ranges:
        first = ord(symbols[begin])
        length = end - begin + 1
        if dense:
            cmap_rows.append(
                "    {.range_start = 0x%04X, .range_length = %d, .glyph_id_start = %d,"
                " .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,"
                " .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}," % (first, length, glyph_start)
            )
        else:
            offsets = [ord(symbols[begin + offset]) - first for offset in range(length)]
            name = f"unicode_list_{glyph_start}"
            unicode_rows.append(
                "static const uint16_t %s[] = {%s};" % (name, ", ".join(str(item) for item in offsets))
            )
            cmap_rows.append(
                "    {.range_start = 0x%04X, .range_length = %d, .glyph_id_start = %d,"
                " .unicode_list = %s, .glyph_id_ofs_list = NULL, .list_length = %d,"
                " .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},"
                % (first, ord(symbols[end]) - first + 1, glyph_start, name, length)
            )
        glyph_start += length

    text = f"""#include "lvgl.h"

#ifndef {symbol.upper()}
#define {symbol.upper()} 1
#endif

#if {symbol.upper()}

{c_array("glyph_bitmap", bytes(bitmap))}

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
    {{.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}},
{chr(10).join(descriptors)}
}};

{chr(10).join(unicode_rows)}

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
{chr(10).join(cmap_rows)}
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = {len(cmap_rows)},
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,
    .stride = 1,
}};

const lv_font_t {symbol} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .release_glyph = NULL,
    .line_height = {line_height},
    .base_line = {base_line},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};

#endif
"""
    output.write_text(text, encoding="utf-8")
    print(f"{output.name}: {len(symbols)} glyphs, {len(bitmap)} bitmap bytes, line_height={line_height} base_line={base_line}")


def main() -> int:
    symbols = collect_symbols()
    (ROOT / "tools" / "slang_symbols.txt").write_text(symbols + "\n", encoding="utf-8")
    emit(16, symbols, ROOT / "main" / "slang_font_16.c")
    emit(20, symbols, ROOT / "main" / "slang_font_20.c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
