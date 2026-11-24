// main/almanac_ui.c — LVGL screens for the almanac: chrome plus one builder per screen.
//
// Night-navy family base with the almanac's cinnabar identity colour. Every builder creates its
// own screen and runs on the input task under bsp_lvgl_lock, so nothing here touches LVGL from a
// timer or another task.
#include "almanac_ui.h"

#include <stdio.h>
#include <string.h>

#include "almanac_strings.h"
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

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border == bg ? 0 : 2, 0);
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
    lv_obj_t *pill = panel(s_scr, ALMANAC_MARGIN_X, ALMANAC_HINT_Y,
                           ALMANAC_SCREEN_W - 2 * ALMANAC_MARGIN_X, ALMANAC_HINT_H, C_CARD, C_CARD);
    label(pill, &almanac_font_16, C_DIM, 8, 4, line1);
    label(pill, &almanac_font_16, C_DIM, 8, 4 + ALMANAC_ROW16_H, line2);
}

void almanac_ui_today(const almanac_view_day_t *day, const char *zhixing, bool roaming, const char *banner) {
    const int y = ALMANAC_CONTENT_Y;
    if (banner) {
        lv_obj_t *warn = label(s_scr, &almanac_font_16, C_CINNABAR, 0, y, banner);
        lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, y);
    } else {
        lv_obj_t *date = label(s_scr, &almanac_font_16, C_INK, 0, y, day->date.text);
        lv_obj_t *tag = roaming ? label(s_scr, &almanac_font_16, C_ACCENT, 0, y, STR_ROAMING) : NULL;
        lv_obj_update_layout(s_scr);
        const int date_w = lv_obj_get_width(date);
        const int pair_w = date_w + (tag ? ALMANAC_ROAM_GAP + lv_obj_get_width(tag) : 0);
        const int x = (ALMANAC_SCREEN_W - pair_w) / 2;
        lv_obj_set_x(date, x);
        if (tag) lv_obj_set_x(tag, x + date_w + ALMANAC_ROAM_GAP);
    }
    lv_obj_t *pillars = label(s_scr, &almanac_font_16, C_DIM, 0, y + ALMANAC_ROW16_H, day->pillars.text);
    lv_obj_align(pillars, LV_ALIGN_TOP_MID, 0, y + ALMANAC_ROW16_H);

    lv_obj_t *seal = panel(s_scr, ALMANAC_SEAL_X, ALMANAC_SEAL_Y, ALMANAC_SEAL_SIZE, ALMANAC_SEAL_SIZE, C_CARD,
                           C_CINNABAR);
    lv_obj_t *glyph = label(seal, &almanac_font_48, C_CINNABAR, 0, 0, zhixing);
    lv_obj_center(glyph);

    label(s_scr, &almanac_font_16, C_INK, ALMANAC_SEAL_TEXT_X, ALMANAC_SEAL_Y + 2, day->lunar.text);
    label(s_scr, &almanac_font_16, C_DIM, ALMANAC_SEAL_TEXT_X, ALMANAC_SEAL_Y + 2 + ALMANAC_ROW16_H, day->gloss.text);
    label(s_scr, &almanac_font_16, C_ACCENT, ALMANAC_SEAL_TEXT_X, ALMANAC_SEAL_Y + 2 + 2 * ALMANAC_ROW16_H,
          day->day_type.text);

    int list_y = ALMANAC_LISTS_Y;
    label(s_scr, &almanac_font_20, C_ACCENT, ALMANAC_MARGIN_X, list_y, STR_LABEL_YI);
    for (int i = 0; i < day->yi_lines; ++i) {
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_BLOCK_X, list_y + i * ALMANAC_ROW16_H, day->yi[i].text);
    }
    list_y += day->yi_lines * ALMANAC_ROW16_H + ALMANAC_BLOCK_GAP;
    label(s_scr, &almanac_font_20, C_CINNABAR, ALMANAC_MARGIN_X, list_y, STR_LABEL_JI);
    for (int i = 0; i < day->ji_lines; ++i) {
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_BLOCK_X, list_y + i * ALMANAC_ROW16_H, day->ji[i].text);
    }
}

void almanac_ui_detail(const almanac_view_day_t *day) {
    int y = ALMANAC_CONTENT_Y;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, day->chong.text);
    y += ALMANAC_ROW16_H;
    label(s_scr, &almanac_font_16, C_DIM, ALMANAC_BLOCK_X, y, day->chong_note.text);
    y += ALMANAC_ROW16_H + 6;
    for (int i = 0; i < 2; ++i) {
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, day->baiji[i].text);
        y += ALMANAC_ROW16_H;
    }
    y += 6;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, day->day_type.text);
    y += ALMANAC_ROW16_H;
    label(s_scr, &almanac_font_16, C_DIM, ALMANAC_BLOCK_X, y, day->day_type_line.text);
}

void almanac_ui_hours(const almanac_view_hours_t *hours) {
    for (int i = 0; i < 12; ++i) {
        const int col = i % 4;
        const int row = i / 4;
        lv_obj_t *cell = panel(s_scr, ALMANAC_HOUR_GRID_X + col * (ALMANAC_HOUR_CELL_W + 2),
                               ALMANAC_HOUR_GRID_Y + row * (ALMANAC_HOUR_CELL_H + 2), ALMANAC_HOUR_CELL_W,
                               ALMANAC_HOUR_CELL_H, C_CARD, C_CARD);
        label(cell, &almanac_font_16, C_INK, 6, 4, hours->hour[i].text);
        label(cell, &almanac_font_16, hours->lucky[i] ? C_ACCENT : C_CINNABAR, 6, 4 + ALMANAC_ROW16_H,
              hours->lucky[i] ? STR_LUCKY : STR_UNLUCKY);
    }
    int y = ALMANAC_HOUR_GRID_Y + 3 * (ALMANAC_HOUR_CELL_H + 2) + 8;
    label(s_scr, &almanac_font_16, C_ACCENT, ALMANAC_MARGIN_X, y, hours->lucky_hours.text);
    label(s_scr, &almanac_font_16, C_CINNABAR, ALMANAC_MARGIN_X, y + ALMANAC_ROW16_H, hours->unlucky_hours.text);
}

void almanac_ui_lucky(const almanac_view_lucky_t *lucky) {
    int y = ALMANAC_CONTENT_Y;
    int say_x = ALMANAC_DIR_SAY_X;
    for (int k = 0; k < 4; ++k) {
        lv_obj_t *dir = label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->dir[k].text);
        lv_obj_update_layout(dir);
        const int right = ALMANAC_MARGIN_X + lv_obj_get_width(dir) + ALMANAC_DIR_SAY_GAP;
        if (right > say_x) say_x = right;
        lv_obj_delete(dir);
    }
    for (int k = 0; k < 4; ++k) {
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->dir[k].text);
        label(s_scr, &almanac_font_16, C_DIM, say_x, y, lucky->dir_say[k].text);
        y += ALMANAC_ROW16_H;
    }
    y += 4;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->protocol.text);
    label(s_scr, &almanac_font_16, C_DIM, ALMANAC_BLOCK_X, y + ALMANAC_ROW16_H, lucky->protocol_note.text);
    y += 2 * ALMANAC_ROW16_H;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->port.text);
    label(s_scr, &almanac_font_16, C_DIM, ALMANAC_BLOCK_X, y + ALMANAC_ROW16_H, lucky->port_note.text);
    y += 2 * ALMANAC_ROW16_H;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->tool.text);
    y += ALMANAC_ROW16_H;
    label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, lucky->color_number.text);
}

void almanac_ui_fortune(const almanac_day_t *day) {
    for (int i = 0; i < ALMANAC_LUCK_CATEGORY_COUNT; ++i) {
        const int y = ALMANAC_FORTUNE_Y + i * ALMANAC_FORTUNE_ROW_H;
        const int stars = day->luck[i];
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, y, almanac_luck_name(i));
        for (int k = 0; k < 5; ++k) {
            lv_obj_t *pip = panel(s_scr, 112 + k * 22, y + 3, 20, 12, k < stars ? C_ACCENT : C_TRACK,
                                  k < stars ? C_ACCENT : C_TRACK);
            lv_obj_set_style_radius(pip, 3, 0);
        }
        label(s_scr, &almanac_font_16, C_DIM, ALMANAC_MARGIN_X, y + ALMANAC_ROW16_H,
              almanac_luck_line(i, stars));
    }
}

static const char *const MENU_ITEMS[ALMANAC_MENU_ROWS] = {
    STR_MENU_CALIBRATE, STR_MENU_TODAY, STR_MENU_PICK_DATE, STR_MENU_HELP, STR_MENU_ABOUT,
};

void almanac_ui_menu(int selected) {
    for (int i = 0; i < ALMANAC_MENU_ROWS; ++i) {
        const int y = ALMANAC_MENU_Y + i * ALMANAC_MENU_ROW_H;
        if (i == selected) {
            panel(s_scr, ALMANAC_MARGIN_X, y - 4, ALMANAC_CONTENT_W, 28, C_CARD, C_CARD);
        }
        label(s_scr, &almanac_font_20, i == selected ? C_ACCENT : C_DIM, ALMANAC_BLOCK_X, y,
              MENU_ITEMS[i]);
    }
}

void almanac_ui_pick(int selected, int first_visible) {
    for (int row = 0; row < ALMANAC_PICK_ROWS_VISIBLE; ++row) {
        const int matter = first_visible + row;
        if (matter >= ALMANAC_MATTER_COUNT) break;
        const int y = ALMANAC_PICK_Y + row * ALMANAC_PICK_ROW_H;
        if (matter == selected) {
            panel(s_scr, ALMANAC_MARGIN_X, y - 3, ALMANAC_CONTENT_W, 26, C_CARD, C_CARD);
        }
        label(s_scr, &almanac_font_20, matter == selected ? C_ACCENT : C_DIM, ALMANAC_BLOCK_X, y,
              almanac_matter(matter));
    }
}

void almanac_ui_result(const char *matter, const almanac_line_t *dates, int count) {
    lv_obj_t *name = label(s_scr, &almanac_font_20, C_CINNABAR, 0, ALMANAC_CONTENT_Y, matter);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, ALMANAC_CONTENT_Y);
    for (int i = 0; i < count; ++i) {
        const int y = ALMANAC_RESULT_LIST_Y + i * ALMANAC_RESULT_ROW_H;
        lv_obj_t *line = label(s_scr, &almanac_font_20, C_INK, 0, y, dates[i].text);
        lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
    }
}

void almanac_ui_setup(int step, int year, int month, int day) {
    const char *const unit = step == 0 ? STR_SETUP_YEAR : step == 1 ? STR_SETUP_MONTH : STR_SETUP_DAY;
    const int value = step == 0 ? year : step == 1 ? month : day;
    lv_obj_t *u = label(s_scr, &almanac_font_20, C_DIM, 0, 84, unit);
    lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 84);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_obj_t *v = label(s_scr, &lv_font_montserrat_48, C_INK, 0, 112, buf);
    lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 112);
    char picked[40];
    snprintf(picked, sizeof(picked), "%d%s%d%s%d%s", year, STR_SETUP_YEAR, month, STR_SETUP_MONTH, day,
             STR_SETUP_DAY);
    lv_obj_t *date = label(s_scr, &almanac_font_16, C_DIM, 0, 166, picked);
    lv_obj_align(date, LV_ALIGN_TOP_MID, 0, 166);
}

void almanac_ui_confirm(const char *date_text) {
    lv_obj_t *d = label(s_scr, &almanac_font_20, C_INK, 0, 96, date_text);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *q = label(s_scr, &almanac_font_20, C_ACCENT, 0, 136, STR_CALIBRATE_CONFIRM);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 136);
}

void almanac_ui_help(const almanac_line_t *lines, int count) {
    for (int i = 0; i < count; ++i) {
        label(s_scr, &almanac_font_16, C_INK, ALMANAC_MARGIN_X, ALMANAC_CONTENT_Y + i * ALMANAC_ROW16_H,
              lines[i].text);
    }
}

void almanac_ui_about(const almanac_line_t *lines, int count) {
    for (int i = 0; i < count; ++i) {
        lv_obj_t *l = label(s_scr, &almanac_font_16, i == 0 ? C_ACCENT : C_DIM, 0, ALMANAC_CONTENT_Y + i * ALMANAC_ROW16_H,
                            lines[i].text);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, ALMANAC_CONTENT_Y + i * ALMANAC_ROW16_H);
    }
}
