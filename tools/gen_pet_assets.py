#!/usr/bin/env python3
"""Generate pet sprite assets for EAR BUDDY.

Draws 5 stages x 2 frames of 48x48 pixel-art birds (frame B = blink/wing
variant), converts to RGB565, and emits main/ear_assets.c/.h with LVGL
image descriptors. Pure stdlib; palette index 0 = transparent (not drawn,
background of the pet panel shows through) -> actually emits RGB565 with the
panel color baked in: PANEL_BG must match ear_ui.c pet panel color.
"""
import math, os

SIZE = 48
# RGB565 panel background baked into sprites (keep in sync with ear_ui.c).
PANEL_BG = 0x1B263B
PALETTE = {
    0: PANEL_BG,          # background (baked, opaque)
    1: 0xFFFFFF,          # white (eggshell, belly)
    2: 0xF4A261,          # beak / feet orange
    3: 0xFFD166,          # body yellow
    4: 0x1B263B,          # outline dark (same family as panel bg)
    5: 0xE76F51,          # cheek / accent red
    6: 0x8D99AE,          # wing gray-blue
    7: 0x2ECC71,          # leaf / decor green
}

def rgb565(v):
    r = (v >> 16) & 0xFF
    g = (v >> 8) & 0xFF
    b = v & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def new_canvas():
    return [[0] * SIZE for _ in range(SIZE)]

def fill_ellipse(c, cx, cy, rx, ry, color):
    for y in range(SIZE):
        for x in range(SIZE):
            dx = (x - cx) / rx
            dy = (y - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                c[y][x] = color

def rect(c, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(SIZE, y1 + 1)):
        for x in range(max(0, x0), min(SIZE, x1 + 1)):
            c[y][x] = color

def px(c, x, y, color):
    if 0 <= x < SIZE and 0 <= y < SIZE:
        c[y][x] = color

def draw_egg(c, blink):
    fill_ellipse(c, 24, 26, 13, 16, 1)      # white egg
    fill_ellipse(c, 19, 20, 3, 4, 3)        # spots
    fill_ellipse(c, 28, 30, 2, 3, 3)
    px(c, 24, 12, 4)                        # outline hint on top
    if blink:
        rect(c, 20, 22, 27, 23, 4)          # "zzz" suggestion line

def draw_hatchling(c, blink):
    fill_ellipse(c, 24, 30, 11, 9, 1)       # white shell bottom
    fill_ellipse(c, 24, 22, 10, 9, 3)       # yellow head
    fill_ellipse(c, 20, 20, 1, 1, 4)        # eye
    fill_ellipse(c, 28, 20, 1, 1, 4)
    if blink:
        rect(c, 19, 20, 21, 20, 4)
        rect(c, 27, 20, 29, 20, 4)
    rect(c, 22, 24, 25, 25, 2)              # beak
    rect(c, 15, 28, 33, 29, 5)              # crack line

def draw_chick(c, blink):
    fill_ellipse(c, 24, 28, 12, 11, 3)      # round yellow body
    fill_ellipse(c, 19, 22, 1, 1, 4)
    fill_ellipse(c, 29, 22, 1, 1, 4)
    if blink:
        rect(c, 18, 22, 20, 22, 4)
        rect(c, 28, 22, 30, 22, 4)
    rect(c, 23, 25, 26, 26, 2)              # beak
    fill_ellipse(c, 14, 30, 4, 6, 6)        # wing
    rect(c, 20, 39, 22, 41, 2)              # feet
    rect(c, 26, 39, 28, 41, 2)

def draw_fledgling(c, blink):
    fill_ellipse(c, 24, 26, 13, 12, 3)      # body
    fill_ellipse(c, 24, 16, 8, 7, 3)        # head
    fill_ellipse(c, 21, 15, 1, 1, 4)
    fill_ellipse(c, 27, 15, 1, 1, 4)
    if blink:
        rect(c, 20, 15, 22, 15, 4)
        rect(c, 26, 15, 28, 15, 4)
    rect(c, 23, 18, 25, 19, 2)              # beak
    fill_ellipse(c, 13, 28, 5, 7, 6)        # wing
    rect(c, 34, 26, 38, 28, 3)              # tail
    rect(c, 19, 38, 21, 41, 2)
    rect(c, 27, 38, 29, 41, 2)

def draw_songbird(c, blink):
    fill_ellipse(c, 25, 27, 13, 12, 3)      # body
    fill_ellipse(c, 25, 14, 8, 7, 3)        # head
    fill_ellipse(c, 22, 13, 1, 1, 4)
    fill_ellipse(c, 28, 13, 1, 1, 4)
    if blink:
        rect(c, 21, 13, 23, 13, 4)
        rect(c, 27, 13, 29, 13, 4)
    rect(c, 24, 16, 26, 17, 2)              # beak
    fill_ellipse(c, 14, 29, 5, 8, 6)        # wing
    rect(c, 36, 24, 41, 27, 6)              # tail
    fill_ellipse(c, 30, 22, 2, 2, 5)        # cheek
    rect(c, 20, 39, 22, 42, 2)
    rect(c, 28, 39, 30, 42, 2)
    px(c, 44, 10, 7)                        # music note dot
    rect(c, 43, 11, 43, 15, 7)              # note stem

def draw_virtuoso(c, blink):
    # Songbird anatomy, palette-shifted: white body keeps the gray wing/tail.
    # The v1.7 endgame ranks reuse the existing visual language - same bird,
    # same single note - and differentiate only through body/wing/tail
    # colors: white/gray -> white/gold -> gold/gold.
    fill_ellipse(c, 25, 27, 13, 12, 1)      # white body
    fill_ellipse(c, 25, 14, 8, 7, 1)        # white head
    fill_ellipse(c, 22, 13, 1, 1, 4)
    fill_ellipse(c, 28, 13, 1, 1, 4)
    if blink:
        rect(c, 21, 13, 23, 13, 4)
        rect(c, 27, 13, 29, 13, 4)
    rect(c, 24, 16, 26, 17, 2)              # beak
    fill_ellipse(c, 14, 29, 5, 8, 6)        # wing
    rect(c, 36, 24, 41, 27, 6)              # tail
    fill_ellipse(c, 30, 22, 2, 2, 5)        # cheek
    rect(c, 20, 39, 22, 42, 2)
    rect(c, 28, 39, 30, 42, 2)
    px(c, 44, 10, 7)                        # music note dot
    rect(c, 43, 11, 43, 15, 7)              # note stem

def draw_master(c, blink):
    # White body with gold wing and tail.
    fill_ellipse(c, 25, 27, 13, 12, 1)      # white body
    fill_ellipse(c, 25, 14, 8, 7, 1)        # white head
    fill_ellipse(c, 22, 13, 1, 1, 4)
    fill_ellipse(c, 28, 13, 1, 1, 4)
    if blink:
        rect(c, 21, 13, 23, 13, 4)
        rect(c, 27, 13, 29, 13, 4)
    rect(c, 24, 16, 26, 17, 2)              # beak
    fill_ellipse(c, 14, 29, 5, 8, 3)        # gold wing
    rect(c, 36, 24, 41, 27, 3)              # gold tail
    fill_ellipse(c, 30, 22, 2, 2, 5)        # cheek
    rect(c, 20, 39, 22, 42, 2)
    rect(c, 28, 39, 30, 42, 2)
    px(c, 44, 10, 7)                        # music note dot
    rect(c, 43, 11, 43, 15, 7)              # note stem

def draw_legend(c, blink):
    # Fully golden songbird: the final rank.
    fill_ellipse(c, 25, 27, 13, 12, 3)      # gold body
    fill_ellipse(c, 25, 14, 8, 7, 3)        # gold head
    fill_ellipse(c, 22, 13, 1, 1, 4)
    fill_ellipse(c, 28, 13, 1, 1, 4)
    if blink:
        rect(c, 21, 13, 23, 13, 4)
        rect(c, 27, 13, 29, 13, 4)
    rect(c, 24, 16, 26, 17, 2)              # beak
    fill_ellipse(c, 14, 29, 5, 8, 3)        # gold wing
    rect(c, 36, 24, 41, 27, 3)              # gold tail
    fill_ellipse(c, 30, 22, 2, 2, 5)        # cheek
    rect(c, 20, 39, 22, 42, 2)
    rect(c, 28, 39, 30, 42, 2)
    px(c, 44, 10, 7)                        # music note dot
    rect(c, 43, 11, 43, 15, 7)              # note stem

# (draw function, name) per stage, order matches ear_stage_t.
STAGES = [
    (draw_egg, "egg"), (draw_hatchling, "hatchling"), (draw_chick, "chick"),
    (draw_fledgling, "fledgling"), (draw_songbird, "songbird"),
    (draw_virtuoso, "virtuoso"), (draw_master, "master"),
    (draw_legend, "legend"),
]

def emit():
    os.makedirs("main", exist_ok=True)
    h = open("main/ear_assets.h", "w", encoding="utf-8", newline="\n")
    h.write("""// ear_assets.h - GENERATED by tools/gen_pet_assets.py. Do not edit by hand.
#pragma once
#include "ear_game.h"
#include "lvgl.h"

// Pet sprites per stage x 2 frames (48x48 RGB565, opaque, panel bg baked in).
extern const lv_image_dsc_t EAR_PET_IMG[EAR_STAGE_COUNT][2];
""")
    h.close()
    c = open("main/ear_assets.c", "w", encoding="utf-8", newline="\n")
    c.write("""// ear_assets.c - GENERATED by tools/gen_pet_assets.py. Do not edit by hand.
#include "ear_assets.h"
""")

    for si, (fn, name) in enumerate(STAGES):
        for fi, blink in enumerate((False, True)):
            canvas = new_canvas()
            fn(canvas, blink)
            data_name = f"pet_{name}_f{fi}"
            c.write(f"static const uint16_t {data_name}[{SIZE * SIZE}] = {{\n")
            for y in range(SIZE):
                row = ",".join("0x%04X" % rgb565(PALETTE[canvas[y][x]])
                               for x in range(SIZE))
                c.write(row + ",\n")
            c.write("};\n\n")

    c.write("""// Descriptor table: cf/stride follow LVGL 9 RGB565 image requirements.
const lv_image_dsc_t EAR_PET_IMG[EAR_STAGE_COUNT][2] = {
""")
    for si, (fn, name) in enumerate(STAGES):
        for fi, blink in enumerate((False, True)):
            c.write(f"""    [{si}][{fi}] = {{
        .header = {{
            .cf = LV_COLOR_FORMAT_RGB565, .w = {SIZE}, .h = {SIZE},
            .stride = {SIZE * 2},
        }},
        .data = (const uint8_t *)pet_{name}_f{fi},
        .data_size = sizeof(pet_{name}_f{fi}),
    }},
""")
    c.write("};\n")
    c.close()
    print("wrote main/ear_assets.h and main/ear_assets.c")

if __name__ == "__main__":
    emit()
