// main/almanac_ui.c — LVGL screens for the almanac: screen chrome and the scene renderer.
//
// Night-navy family base with the almanac's cinnabar identity colour. Every builder draws the
// scene that main/almanac_view.c lays out; nothing is placed here. Every builder creates its own
// screen and runs on the input task under bsp_lvgl_lock, so nothing here touches LVGL from a
// timer or another task.
#include "almanac_ui.h"

#include <stdio.h>

#include "lvgl.h"

LV_FONT_DECLARE(almanac_font_16);
LV_FONT_DECLARE(almanac_font_20);
LV_FONT_DECLARE(almanac_font_48);

#define C_BG 0x1B263B
#define C_CARD 0x24344D
#define C_INK 0xEDEFF3
#define C_DIM 0x8D99AE
#define C_TRACK 0x3A4C6E
#define C_ACCENT 0x53D3B5
#define C_CINNABAR 0xE0533F
// Fills of the boxes with a meaning: the night-navy base moved 28 % of the way toward the colour
// of that meaning, enough to read as a block and still dark enough for light text.
#define C_GOOD_BG 0x2B565D
#define C_BAD_BG 0x52333C

static almanac_scene_t s_scene; // built while rendering on the 4 KB key task stack
static lv_obj_t *s_scr;
static lv_obj_t *s_batt;

static void battery_text(char *buf, size_t size, int battery_soc) {
    if (battery_soc < 0) {
        snprintf(buf, size, "--%%");
    } else {
        snprintf(buf, size, "%d%%", battery_soc);
    }
}

// Updates the battery label of the screen in place; the rest of the screen stays untouched.
void almanac_ui_battery(int battery_soc) {
    if (!s_batt) return;
    char soc[8];
    battery_text(soc, sizeof(soc), battery_soc);
    lv_label_set_text(s_batt, soc);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t fill, uint32_t border, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border ? ALMANAC_CARD_BORDER : 0, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

void almanac_ui_begin(const char *title, int battery_soc) {
    if (s_scr) lv_obj_delete(s_scr);
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(s_scr);

    lv_obj_t *t = label(s_scr, &almanac_font_20, C_INK, 0, ALMANAC_TITLE_Y, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, ALMANAC_TITLE_Y);
    char soc[8];
    battery_text(soc, sizeof(soc), battery_soc);
    s_batt = label(s_scr, &lv_font_montserrat_14, C_DIM, 0, 14, soc);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -8, 14);
}

void almanac_ui_hints(const char *line1, const char *line2) {
    lv_obj_t *pill = box(s_scr, ALMANAC_MARGIN_X, ALMANAC_HINT_Y, ALMANAC_SCREEN_W - 2 * ALMANAC_MARGIN_X, ALMANAC_HINT_H,
                         C_CARD, C_TRACK, ALMANAC_CARD_RADIUS);
    // A bordered box hands its children coordinates from inside the border.
    const int inset_x = 8 - ALMANAC_CARD_BORDER;
    const int inset_y = 4 - ALMANAC_CARD_BORDER;
    label(pill, &almanac_font_16, C_DIM, inset_x, inset_y, line1);
    label(pill, &almanac_font_16, C_DIM, inset_x, inset_y + ALMANAC_ROW16_H, line2);
}

static const lv_font_t *scene_font(almanac_font_t font) {
    switch (font) {
    case ALM_FONT_16: return &almanac_font_16;
    case ALM_FONT_20: return &almanac_font_20;
    case ALM_FONT_48: return &almanac_font_48;
    default: return &lv_font_montserrat_48;
    }
}

static uint32_t scene_color(almanac_ink_t ink) {
    switch (ink) {
    case ALM_INK_DIM: return C_DIM;
    case ALM_INK_GOOD: return C_ACCENT;
    case ALM_INK_BAD: return C_CINNABAR;
    case ALM_INK_NAVY: return C_BG;
    default: return C_INK;
    }
}

static void scene_box_style(const almanac_box_t *b, uint32_t *fill, uint32_t *border, int *radius) {
    *border = 0;
    *radius = ALMANAC_CARD_RADIUS;
    switch ((almanac_box_kind_t)b->kind) {
    case ALM_BOX_CARD: *fill = C_CARD; *border = C_TRACK; break;
    case ALM_BOX_CARD_SELECTED:
    case ALM_BOX_FOCUS: *fill = C_CARD; *border = C_ACCENT; break;
    case ALM_BOX_GOOD: *fill = C_GOOD_BG; break;
    case ALM_BOX_BAD: *fill = C_BAD_BG; break;
    case ALM_BOX_SEAL: *fill = C_CARD; *border = C_CINNABAR; break;
    case ALM_BOX_CHIP_GOOD: *fill = C_ACCENT; *radius = ALMANAC_CHIP_RADIUS; break;
    case ALM_BOX_CHIP_BAD: *fill = C_CINNABAR; *radius = ALMANAC_CHIP_RADIUS; break;
    case ALM_BOX_PIP_ON: *fill = C_ACCENT; *radius = 3; break;
    default: *fill = C_TRACK; *radius = 3; break;
    }
}

// Draws a scene: boxes first so every line sits on top, then the lines. A centred line is placed
// from its measured width, and the lines of a group are centred together.
static void draw_scene(const almanac_scene_t *scene) {
    for (int i = 0; i < scene->n_boxes; ++i) {
        const almanac_box_t *b = &scene->boxes[i];
        uint32_t fill = C_CARD;
        uint32_t border = 0;
        int radius = ALMANAC_CARD_RADIUS;
        scene_box_style(b, &fill, &border, &radius);
        box(s_scr, b->x, b->y, b->w, b->h, fill, border, radius);
    }
    lv_obj_t *labels[ALMANAC_SCENE_TEXTS_MAX];
    for (int i = 0; i < scene->n_texts; ++i) {
        const almanac_text_t *text = &scene->texts[i];
        labels[i] = label(s_scr, scene_font((almanac_font_t)text->font), scene_color((almanac_ink_t)text->ink), text->x,
                          text->y, text->text);
    }
    lv_obj_update_layout(s_scr);
    int i = 0;
    while (i < scene->n_texts) {
        const almanac_text_t *text = &scene->texts[i];
        int x = text->x;
        if (text->center == 2) {
            int group_w = 0;
            int k = i;
            for (; k < scene->n_texts && scene->texts[k].center == 2; ++k) {
                group_w += lv_obj_get_width(labels[k]) + (k > i ? ALMANAC_ROAM_GAP : 0);
            }
            x += (text->w - group_w) / 2;
            for (; i < k; ++i) {
                lv_obj_set_pos(labels[i], x, scene->texts[i].y);
                x += lv_obj_get_width(labels[i]) + ALMANAC_ROAM_GAP;
            }
            continue;
        }
        if (text->center == 1) x += (text->w - lv_obj_get_width(labels[i])) / 2;
        lv_obj_set_pos(labels[i], x, text->y);
        ++i;
    }
}

void almanac_ui_today(const almanac_view_day_t *day, const char *zhixing, bool roaming, const char *banner) {
    almanac_scene_today(day, zhixing, roaming, banner, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_detail(const almanac_view_day_t *day, bool huangdao) {
    almanac_scene_detail(day, huangdao, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_hours(const almanac_view_hours_t *hours) {
    almanac_scene_hours(hours, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_lucky(const almanac_view_lucky_t *lucky) {
    almanac_scene_lucky(lucky, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_fortune(const almanac_day_t *day) {
    almanac_scene_fortune(day, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_menu(int selected) {
    almanac_scene_menu(selected, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_pick(int selected, int first_visible) {
    almanac_scene_pick(selected, first_visible, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_result(const char *matter, const almanac_line_t *dates, int count) {
    almanac_scene_result(matter, dates, count, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_setup(int step, int year, int month, int day) {
    almanac_scene_setup(step, year, month, day, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_confirm(const char *date_text) {
    almanac_scene_confirm(date_text, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_help(const almanac_line_t *lines, int count) {
    almanac_scene_help(lines, count, &s_scene);
    draw_scene(&s_scene);
}

void almanac_ui_about(const almanac_line_t *lines, int count) {
    almanac_scene_about(lines, count, &s_scene);
    draw_scene(&s_scene);
}
