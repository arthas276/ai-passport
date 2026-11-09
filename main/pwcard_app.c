// main/pwcard_app.c - 密码卡 UI and state machine: eight LVGL screens (card
// grid, site list, password reveal, add wizard, settings, double-confirm,
// seed view, help), a button queue drained under the LVGL lock, and the
// repo-standard idle dim -> deep sleep policy. Derivation lives in pw_core.c,
// persistence in pw_store.c. Theme: cyan utility dark line, contrast pairs
// audited in docs/PRD_PWCARD.md.
#include "pwcard_app.h"
#include "pw_power.h"
#include "pwcard_strings.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(pw_font_22);

static const char *TAG = "pwcard";

// Cyan utility palette (design.md, Corporate Split Complementary #5 variant).
#define BG_INK      0x0E1518
#define BG_RAISE    0x162026
#define TILE        0x1B2830
#define TILE_BORDER 0x2A3B46
#define TEXT_MAIN   0xEDF1F6
#define TEXT_MUTED  0x93A7B1
#define ACCENT      0x35C4DC
#define ACCENT_DEEP 0x17768A
#define INK         0x0C1215
#define AMBER       0xD9A93C

// CARD geometry (px): title 22 (y2), headers 14 (y26), grid 8x(20x24 cells,
// pitch 21x26) from x25/y44 -> bottom 250, footer 22 (y292). Width 240.
#define CARD_X0 25
#define CARD_Y0 44
#define CELL_W 20
#define CELL_H 24
#define CELL_PX 21
#define CELL_PY 26
// SITES: title 22 (y2), 8 rows x 32 px from y30 -> 286, footer y292.
#define SITE_Y0 30
#define SITE_ROW_H 32

typedef struct {
    int button;
    int event;
} pw_key_t;

static pw_app_t s_app;
static pw_power_t s_power;
static QueueHandle_t s_keys;
static TaskHandle_t s_key_task;
static lv_timer_t *s_timer;
static volatile bool s_ready;
static int8_t s_soc = -1;
static uint8_t s_soc_tick;

static lv_obj_t *s_scr[PW_SCREEN_N];
static lv_obj_t *s_foot[PW_SCREEN_N];
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;
static lv_timer_t *s_flash_timer;

// CARD screen widgets
static lv_obj_t *s_card_cells[PW_ROWS_N][PW_COLS_N];
static lv_obj_t *s_card_cell_labels[PW_ROWS_N][PW_COLS_N];
static lv_obj_t *s_card_row_headers[PW_ROWS_N];
static lv_obj_t *s_card_batt;
// SITES
static lv_obj_t *s_sites_names[PW_SITES_VISIBLE];
static lv_obj_t *s_sites_cursor;
// PASS
static lv_obj_t *s_pass_name;
static lv_obj_t *s_pass_sum;
static lv_obj_t *s_pass_label;
static lv_obj_t *s_pass_bar;
static lv_obj_t *s_pass_note;
// WIZARD
static lv_obj_t *s_wiz_chip;
static lv_obj_t *s_wiz_value;
static lv_obj_t *s_wiz_unit;
static lv_obj_t *s_wiz_name_lbl;
static lv_obj_t *s_wiz_wheel[7];
static lv_obj_t *s_wiz_wheel_lbl[7];
// SETTINGS
static lv_obj_t *s_set_items[4];
static lv_obj_t *s_set_cursor;
// CONFIRM
static lv_obj_t *s_confirm_msg;
// SEED
static lv_obj_t *s_seed_line1;
static lv_obj_t *s_seed_line2;

static void show_screen(pw_screen_t next);

static lv_obj_t *make_label(lv_obj_t *parent, int y, int w, uint32_t color,
                            const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static lv_obj_t *make_footer(lv_obj_t *parent, const char *text) {
    lv_obj_t *foot = make_label(parent, 292, 232, TEXT_MUTED, &pw_font_22);
    lv_label_set_text(foot, text);
    return foot;
}

static lv_obj_t *make_screen(uint32_t bg) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    return scr;
}

static void toast_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    s_toast_timer = NULL;
}

static void toast(const char *text) {
    lv_obj_t *scr = lv_screen_active();
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    s_toast = make_label(scr, 254, 232, ACCENT, &pw_font_22);
    lv_label_set_text(s_toast, text);
    s_toast_timer = lv_timer_create(toast_cb, 1200, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

static void flash_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_foot[s_app.screen]) {
        lv_obj_set_style_text_color(s_foot[s_app.screen],
                                    lv_color_hex(TEXT_MUTED), LV_PART_MAIN);
    }
    s_flash_timer = NULL;
}

// Any key press on a screen where UP/DOWN have nothing to do still answers:
// the footer lights up briefly instead of the press vanishing silently.
static void foot_flash(void) {
    lv_obj_t *foot = s_foot[s_app.screen];
    if (!foot) return;
    lv_obj_set_style_text_color(foot, lv_color_hex(ACCENT), LV_PART_MAIN);
    if (s_flash_timer) lv_timer_delete(s_flash_timer);
    s_flash_timer = lv_timer_create(flash_cb, 250, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

static void battery_refresh(void) {
    s_soc = bsp_battery_soc();
}

static void render_battery(lv_obj_t *label) {
    if (!label) return;
    if (s_soc < 0) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(label, "%d%%", s_soc);
}

// ---------------------------------------------------------------- CARD (S1)

static void build_card(void) {
    if (s_scr[PW_SCREEN_CARD]) return;
    lv_obj_t *scr = make_screen(BG_INK);
    s_scr[PW_SCREEN_CARD] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_APP_NAME);
    s_card_batt = make_label(scr, 4, 60, TEXT_MUTED, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(s_card_batt, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_card_batt, LV_ALIGN_TOP_RIGHT, -8, 4);
    for (uint32_t c = 0; c < PW_COLS_N; c++) {
        lv_obj_t *h = make_label(scr, 27, CELL_W, TEXT_MUTED,
                                 &lv_font_montserrat_14);
        lv_obj_align(h, LV_ALIGN_TOP_LEFT, (int32_t)(CARD_X0 + c * CELL_PX), 27);
        lv_label_set_text_fmt(h, "%c", (char)('A' + c));
    }
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        s_card_row_headers[r] = make_label(scr, 0, 20, TEXT_MUTED,
                                           &lv_font_montserrat_14);
        lv_obj_align(s_card_row_headers[r], LV_ALIGN_TOP_LEFT, 5,
                     (int32_t)(CARD_Y0 + (r * CELL_PY) + 5));
        lv_label_set_text_fmt(s_card_row_headers[r], "%u", (unsigned)(r + 1u));
        for (uint32_t c = 0; c < PW_COLS_N; c++) {
            lv_obj_t *cell = lv_obj_create(scr);
            lv_obj_set_pos(cell, (int32_t)(CARD_X0 + c * CELL_PX),
                           (int32_t)(CARD_Y0 + r * CELL_PY));
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(cell, 3, LV_PART_MAIN);
            lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(cell, lv_color_hex(TILE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_t *gl = lv_label_create(cell);
            lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(gl, lv_color_hex(TEXT_MAIN),
                                        LV_PART_MAIN);
            lv_obj_center(gl);
            s_card_cells[r][c] = cell;
            s_card_cell_labels[r][c] = gl;
        }
    }
    s_foot[PW_SCREEN_CARD] = make_footer(scr, STR_CARD_FOOT);
}

static void render_card(void) {
    char card[PW_ROWS_N][PW_COLS_N + 1u];
    pw_card_build(s_app.store.seed, card);
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        const bool active = r == s_app.cursor_row;
        for (uint32_t c = 0; c < PW_COLS_N; c++) {
            lv_label_set_text_fmt(s_card_cell_labels[r][c], "%c", card[r][c]);
            lv_obj_set_style_text_color(
                s_card_cell_labels[r][c],
                lv_color_hex(active ? INK : TEXT_MAIN), LV_PART_MAIN);
            lv_obj_set_style_bg_color(
                s_card_cells[r][c], lv_color_hex(active ? ACCENT : TILE),
                LV_PART_MAIN);
            lv_obj_set_style_border_width(
                s_card_cells[r][c], active ? 1 : 0, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_card_cells[r][c],
                                          lv_color_hex(ACCENT_DEEP),
                                          LV_PART_MAIN);
        }
        lv_obj_set_style_text_color(
            s_card_row_headers[r],
            lv_color_hex(active ? ACCENT : TEXT_MUTED), LV_PART_MAIN);
    }
    render_battery(s_card_batt);
}

// ---------------------------------------------------------------- SITES (S2)

static void build_sites(void) {
    if (s_scr[PW_SCREEN_SITES]) return;
    lv_obj_t *scr = make_screen(BG_RAISE);
    s_scr[PW_SCREEN_SITES] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_SITES_TITLE);
    s_sites_cursor = lv_obj_create(scr);
    lv_obj_set_pos(s_sites_cursor, 4, SITE_Y0);
    lv_obj_set_size(s_sites_cursor, 232, SITE_ROW_H - 2);
    lv_obj_clear_flag(s_sites_cursor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_sites_cursor, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_sites_cursor, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sites_cursor, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sites_cursor, lv_color_hex(ACCENT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sites_cursor, LV_OPA_COVER, LV_PART_MAIN);
    for (uint32_t i = 0; i < PW_SITES_VISIBLE; i++) {
        const int y = (int)SITE_Y0 + (int)i * (int)SITE_ROW_H;
        s_sites_names[i] = make_label(scr, y + 4, 216, TEXT_MAIN,
                                      &lv_font_montserrat_20);
        lv_obj_set_style_text_align(s_sites_names[i], LV_TEXT_ALIGN_LEFT,
                                    LV_PART_MAIN);
        lv_obj_align(s_sites_names[i], LV_ALIGN_TOP_LEFT, 12, y + 4);
    }
    s_foot[PW_SCREEN_SITES] = make_footer(scr, STR_SITES_FOOT);
}

/* Rows on the SITES screen: the stored sites plus a trailing fixed
 * "add site" row (hidden once the 32-site cap is reached). */
static uint32_t sites_total(void) {
    return s_app.store.site_count +
           (s_app.store.site_count < PW_SITES_MAX ? 1u : 0u);
}

static void render_sites(void) {
    const uint32_t count = s_app.store.site_count;
    const uint32_t total = sites_total();
    for (uint32_t i = 0; i < PW_SITES_VISIBLE; i++) {
        const uint32_t idx = s_app.site_top + i;
        const bool active = idx == s_app.site_cursor;
        if (idx < count) {
            const pw_site_t *site = &s_app.store.sites[idx];
            char name[PW_NAME_MAX + 2u];
            if (site->name[0] == '\0') {
                snprintf(name, sizeof(name), "--");
            } else {
                snprintf(name, sizeof(name), "%s", site->name);
            }
            lv_obj_set_style_text_font(s_sites_names[i],
                                       &lv_font_montserrat_20, LV_PART_MAIN);
            lv_label_set_text(s_sites_names[i], name);
            lv_obj_set_style_text_color(
                s_sites_names[i], lv_color_hex(active ? INK : TEXT_MAIN),
                LV_PART_MAIN);
        } else if (idx == count) {  // the fixed add-site row
            lv_obj_set_style_text_font(s_sites_names[i], &pw_font_22,
                                       LV_PART_MAIN);
            lv_label_set_text(s_sites_names[i], STR_WIZ_TITLE);
            lv_obj_set_style_text_color(
                s_sites_names[i], lv_color_hex(active ? INK : ACCENT),
                LV_PART_MAIN);
        } else {
            lv_label_set_text(s_sites_names[i], "");
        }
    }
    const int32_t rel = (int32_t)s_app.site_cursor - (int32_t)s_app.site_top;
    if (total > 0 && rel >= 0 && rel < (int32_t)PW_SITES_VISIBLE) {
        lv_obj_set_pos(s_sites_cursor, 4,
                       (int32_t)SITE_Y0 + rel * (int32_t)SITE_ROW_H);
    }
}

static void sites_scroll_to_cursor(void) {
    if (s_app.site_cursor < s_app.site_top) {
        s_app.site_top = s_app.site_cursor;
    } else if (s_app.site_cursor >= s_app.site_top + PW_SITES_VISIBLE) {
        s_app.site_top = (uint8_t)(s_app.site_cursor - (PW_SITES_VISIBLE - 1u));
    }
}

// ----------------------------------------------------------------- PASS (S3)

static void build_pass(void) {
    if (s_scr[PW_SCREEN_PASS]) return;
    lv_obj_t *scr = make_screen(BG_INK);
    s_scr[PW_SCREEN_PASS] = scr;
    s_pass_name = make_label(scr, 2, 240, TEXT_MAIN, &lv_font_montserrat_20);
    s_pass_sum = make_label(scr, 28, 240, TEXT_MUTED, &pw_font_22);
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_pos(panel, 10, 64);
    lv_obj_set_size(panel, 220, 64);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(BG_RAISE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    s_pass_label = lv_label_create(panel);
    lv_obj_set_style_text_color(s_pass_label, lv_color_hex(TEXT_MAIN),
                                LV_PART_MAIN);
    lv_obj_center(s_pass_label);
    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_set_pos(track, 30, 144);
    lv_obj_set_size(track, 180, 4);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(track, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(track, lv_color_hex(TILE_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    s_pass_bar = lv_obj_create(scr);
    lv_obj_set_pos(s_pass_bar, 30, 144);
    lv_obj_set_size(s_pass_bar, 0, 4);
    lv_obj_clear_flag(s_pass_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_pass_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pass_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pass_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pass_bar, lv_color_hex(ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pass_bar, LV_OPA_COVER, LV_PART_MAIN);
    s_pass_note = make_label(scr, 156, 240, TEXT_MUTED, &pw_font_22);
    lv_label_set_text(s_pass_note, STR_PASS_NOTE);
    s_foot[PW_SCREEN_PASS] = make_footer(scr, STR_PASS_FOOT);
}

// Widest-case check: a 12-char "WWWWWWWWWWWW" at 28 px overflows the 220 px
// panel, so render_pass adapts the reveal font 28 -> 20 -> 14. Width is
// measured on the live label via public API: LVGL 9 keeps lv_text_get_width
// in a private header with attribute-pointer arguments.

static void render_pass(void) {
    const uint32_t count = s_app.store.site_count;
    if (count == 0 || s_app.site_cursor >= count) {
        show_screen(PW_SCREEN_SITES);
        return;
    }
    const pw_site_t *site = &s_app.store.sites[s_app.site_cursor];
    if (site->name[0] == '\0') {
        lv_label_set_text(s_pass_name, "--");
    } else {
        lv_label_set_text(s_pass_name, site->name);
    }
    lv_label_set_text_fmt(s_pass_sum, "%u行%c列%u位", (unsigned)(site->row + 1u),
                          (char)('A' + site->col), (unsigned)site->len);
    char buf[PW_LEN_MAX + 1u];
    if (s_app.reveal_ms == 0u) {
        memset(buf, '*', site->len);
        buf[site->len] = '\0';
        lv_obj_set_style_text_color(s_pass_label, lv_color_hex(TEXT_MUTED),
                                    LV_PART_MAIN);
    } else {
        pw_password(s_app.store.seed, site->row, site->col, site->len, buf);
        lv_obj_set_style_text_color(s_pass_label, lv_color_hex(TEXT_MAIN),
                                    LV_PART_MAIN);
    }
    lv_label_set_text(s_pass_label, buf);
    static const lv_font_t *fits[3] = {
        &lv_font_montserrat_28,
        &lv_font_montserrat_20,
        &lv_font_montserrat_14,
    };
    for (uint32_t i = 0; i < 3u; i++) {
        lv_obj_set_style_text_font(s_pass_label, fits[i], LV_PART_MAIN);
        lv_obj_update_layout(s_pass_label);
        if (lv_obj_get_width(s_pass_label) <= 200) break;
    }
    lv_obj_center(s_pass_label);
    const int32_t w = (int32_t)((180 * (uint64_t)s_app.reveal_ms) /
                                (uint64_t)PW_REVEAL_MS);
    lv_obj_set_size(s_pass_bar, w, 4);
    if (s_app.reveal_ms > 0u) {
        lv_obj_clear_flag(s_pass_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pass_note, LV_OBJ_FLAG_HIDDEN);
    }
}

// --------------------------------------------------------------- WIZARD (S4)

static void build_wizard(void) {
    if (s_scr[PW_SCREEN_WIZARD]) return;
    lv_obj_t *scr = make_screen(BG_RAISE);
    s_scr[PW_SCREEN_WIZARD] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_WIZ_TITLE);
    s_wiz_chip = make_label(scr, 34, 240, TEXT_MUTED, &pw_font_22);
    s_wiz_value = make_label(scr, 84, 240, TEXT_MAIN, &lv_font_montserrat_28);
    s_wiz_unit = make_label(scr, 126, 240, TEXT_MUTED, &pw_font_22);
    lv_label_set_text(s_wiz_unit, STR_UNIT_LEN);
    // name step: typed text with a trailing underscore cursor, then a 7-tile
    // window onto the 41-entry typing wheel (chars + delete + done)
    s_wiz_name_lbl = make_label(scr, 66, 240, TEXT_MAIN,
                                &lv_font_montserrat_20);
    for (uint32_t i = 0; i < 7u; i++) {
        lv_obj_t *tile = lv_obj_create(scr);
        lv_obj_set_pos(tile, (int32_t)(29 + i * 26), 118);
        lv_obj_set_size(tile, 24, 30);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(tile, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, lv_color_hex(TILE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_t *gl = lv_label_create(tile);
        lv_obj_set_style_text_font(gl, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(gl, lv_color_hex(TEXT_MAIN), LV_PART_MAIN);
        lv_obj_center(gl);
        s_wiz_wheel[i] = tile;
        s_wiz_wheel_lbl[i] = gl;
    }
    s_foot[PW_SCREEN_WIZARD] = make_footer(scr, STR_WIZ_FOOT);
}

static void wizard_reset(void) {
    s_app.wiz_step = 0;
    s_app.wiz_row = 0;
    s_app.wiz_col = 0;
    s_app.wiz_len = 8u;
    pw_name_reset(&s_app.wiz_input);
}

static void render_wizard(void) {
    static const char *steps[4] = {
        STR_STEP_ROW, STR_STEP_COL, STR_STEP_LEN, STR_STEP_NAME,
    };
    lv_label_set_text_fmt(s_wiz_chip, "%u/4 %s", (unsigned)(s_app.wiz_step + 1u),
                          steps[s_app.wiz_step]);
    const bool name_step = s_app.wiz_step == 3u;
    if (name_step) {
        lv_obj_add_flag(s_wiz_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wiz_unit, LV_OBJ_FLAG_HIDDEN);
        char shown[PW_NAME_MAX + 2u];
        snprintf(shown, sizeof(shown), "%s", s_app.wiz_input.name);
        if (s_app.wiz_input.len < PW_NAME_MAX) {
            shown[s_app.wiz_input.len] = '_';
            shown[s_app.wiz_input.len + 1u] = '\0';
        }
        if (s_app.wiz_input.len == 0u) {
            lv_obj_set_style_text_color(s_wiz_name_lbl,
                                        lv_color_hex(TEXT_MUTED), LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(s_wiz_name_lbl,
                                        lv_color_hex(TEXT_MAIN), LV_PART_MAIN);
        }
        lv_label_set_text(s_wiz_name_lbl, shown);
        for (uint32_t i = 0; i < 7u; i++) {
            const uint32_t entry =
                (s_app.wiz_input.cursor + PW_NAME_WHEEL_N + i - 3u) %
                PW_NAME_WHEEL_N;
            const bool center = i == 3u;
            const char c = pw_name_wheel_char(entry);
            if (c == '\0') {
                lv_obj_set_style_text_font(s_wiz_wheel_lbl[i], &pw_font_22,
                                           LV_PART_MAIN);
                lv_label_set_text(s_wiz_wheel_lbl[i],
                                  entry == PW_NAME_IDX_DELETE
                                      ? STR_WHEEL_DELETE
                                      : STR_WHEEL_DONE);
            } else {
                lv_obj_set_style_text_font(s_wiz_wheel_lbl[i],
                                           &lv_font_montserrat_20,
                                           LV_PART_MAIN);
                lv_label_set_text_fmt(s_wiz_wheel_lbl[i], "%c", c);
            }
            lv_obj_set_style_text_color(
                s_wiz_wheel_lbl[i],
                lv_color_hex(center ? INK : TEXT_MUTED), LV_PART_MAIN);
            lv_obj_set_style_bg_color(
                s_wiz_wheel[i],
                lv_color_hex(center ? ACCENT : TILE), LV_PART_MAIN);
        }
    } else {
        lv_obj_add_flag(s_wiz_name_lbl, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t i = 0; i < 7u; i++) {
            lv_obj_add_flag(s_wiz_wheel[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_wiz_value, LV_OBJ_FLAG_HIDDEN);
        if (s_app.wiz_step == 2u) {
            lv_label_set_text_fmt(s_wiz_value, "%u", (unsigned)s_app.wiz_len);
            lv_obj_clear_flag(s_wiz_unit, LV_OBJ_FLAG_HIDDEN);
        } else if (s_app.wiz_step == 1u) {
            lv_label_set_text_fmt(s_wiz_value, "%c",
                                  (char)('A' + s_app.wiz_col));
            lv_obj_add_flag(s_wiz_unit, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text_fmt(s_wiz_value, "%u",
                                  (unsigned)(s_app.wiz_row + 1u));
            lv_obj_add_flag(s_wiz_unit, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (name_step) {
        for (uint32_t i = 0; i < 7u; i++) {
            lv_obj_clear_flag(s_wiz_wheel[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_wiz_name_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_foot[PW_SCREEN_WIZARD],
                      name_step ? STR_WIZ_NAME_FOOT : STR_WIZ_FOOT);
}

// ------------------------------------------------------------- SETTINGS (S5)

static void build_settings(void) {
    if (s_scr[PW_SCREEN_SETTINGS]) return;
    lv_obj_t *scr = make_screen(BG_RAISE);
    s_scr[PW_SCREEN_SETTINGS] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_SET_TITLE);
    s_set_cursor = lv_obj_create(scr);
    lv_obj_set_pos(s_set_cursor, 4, SITE_Y0);
    lv_obj_set_size(s_set_cursor, 232, 34);
    lv_obj_clear_flag(s_set_cursor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_set_cursor, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_set_cursor, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_set_cursor, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_set_cursor, lv_color_hex(ACCENT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_set_cursor, LV_OPA_COVER, LV_PART_MAIN);
    static const char *items[4] = {
        STR_SET_REGEN, STR_SET_SEED, STR_SET_CLEAR, STR_SET_HELP,
    };
    for (uint32_t i = 0; i < 4u; i++) {
        s_set_items[i] = make_label(scr, 0, 192, TEXT_MAIN, &pw_font_22);
        lv_obj_set_style_text_align(s_set_items[i], LV_TEXT_ALIGN_LEFT,
                                    LV_PART_MAIN);
        lv_obj_align(s_set_items[i], LV_ALIGN_TOP_LEFT, 24,
                     (int32_t)(SITE_Y0 + i * 36u + 3u));
        lv_label_set_text(s_set_items[i], items[i]);
    }
    s_foot[PW_SCREEN_SETTINGS] = make_footer(scr, STR_SET_FOOT);
}

static void render_settings(void) {
    for (uint32_t i = 0; i < 4u; i++) {
        lv_obj_set_style_text_color(
            s_set_items[i],
            lv_color_hex(i == s_app.set_cursor ? INK : TEXT_MAIN),
            LV_PART_MAIN);
    }
    lv_obj_set_pos(s_set_cursor, 4,
                   (int32_t)(SITE_Y0 + s_app.set_cursor * 36u));
}

// ------------------------------------------------------ CONFIRM / SEED / HELP

static void build_confirm(void) {
    if (s_scr[PW_SCREEN_CONFIRM]) return;
    lv_obj_t *scr = make_screen(BG_INK);
    s_scr[PW_SCREEN_CONFIRM] = scr;
    s_confirm_msg = make_label(scr, 120, 232, AMBER, &pw_font_22);
    s_foot[PW_SCREEN_CONFIRM] = make_footer(scr, STR_CONFIRM_FOOT);
}

static void render_confirm(void) {
    lv_label_set_text(s_confirm_msg, s_app.confirm_what == 0u
                                        ? STR_CONFIRM_REGEN
                                        : STR_CONFIRM_CLEAR);
}

static void build_seed(void) {
    if (s_scr[PW_SCREEN_SEED]) return;
    lv_obj_t *scr = make_screen(BG_INK);
    s_scr[PW_SCREEN_SEED] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_SEED_TITLE);
    s_seed_line1 = make_label(scr, 110, 240, TEXT_MAIN,
                              &lv_font_montserrat_14);
    s_seed_line2 = make_label(scr, 132, 240, TEXT_MAIN,
                              &lv_font_montserrat_14);
    lv_obj_t *hint = make_label(scr, 170, 240, TEXT_MUTED, &pw_font_22);
    lv_label_set_text(hint, STR_SEED_HINT);
    s_foot[PW_SCREEN_SEED] = make_footer(scr, STR_BACK_FOOT);
}

static void render_seed(void) {
    char hex[2u * PW_SEED_BYTES + 1u];
    char line[17];
    pw_seed_hex(s_app.store.seed, hex);
    memcpy(line, hex, 16);
    line[16] = '\0';
    lv_label_set_text(s_seed_line1, line);
    lv_label_set_text(s_seed_line2, hex + 16);
}

static void build_help(void) {
    if (s_scr[PW_SCREEN_HELP]) return;
    lv_obj_t *scr = make_screen(BG_RAISE);
    s_scr[PW_SCREEN_HELP] = scr;
    lv_obj_t *title = make_label(scr, 2, 240, TEXT_MAIN, &pw_font_22);
    lv_label_set_text(title, STR_SET_HELP);
    static const char *lines[4] = {
        STR_HELP_L1, STR_HELP_L2, STR_HELP_L3, STR_SEED_HINT,
    };
    for (uint32_t i = 0; i < 4u; i++) {
        lv_obj_t *l = make_label(scr, (int)(84 + i * 30u), 232, TEXT_MAIN,
                                 &pw_font_22);
        lv_label_set_text(l, lines[i]);
    }
    s_foot[PW_SCREEN_HELP] = make_footer(scr, STR_BACK_FOOT);
}

static void show_screen(pw_screen_t next) {
    s_app.screen = next;
    switch (next) {
    case PW_SCREEN_SITES:
        build_sites();
        render_sites();
        lv_screen_load(s_scr[PW_SCREEN_SITES]);
        break;
    case PW_SCREEN_PASS:
        build_pass();
        lv_screen_load(s_scr[PW_SCREEN_PASS]);  // load first: font fit measures
        render_pass();                            // the live label via layout
        break;
    case PW_SCREEN_WIZARD:
        build_wizard();
        render_wizard();
        lv_screen_load(s_scr[PW_SCREEN_WIZARD]);
        break;
    case PW_SCREEN_SETTINGS:
        build_settings();
        render_settings();
        lv_screen_load(s_scr[PW_SCREEN_SETTINGS]);
        break;
    case PW_SCREEN_CONFIRM:
        build_confirm();
        render_confirm();
        lv_screen_load(s_scr[PW_SCREEN_CONFIRM]);
        break;
    case PW_SCREEN_SEED:
        build_seed();
        render_seed();
        lv_screen_load(s_scr[PW_SCREEN_SEED]);
        break;
    case PW_SCREEN_HELP:
        build_help();
        lv_screen_load(s_scr[PW_SCREEN_HELP]);
        break;
    case PW_SCREEN_CARD:
    default:
        build_card();
        render_card();
        lv_screen_load(s_scr[PW_SCREEN_CARD]);
        break;
    }
}

// ------------------------------------------------------------------ actions

static void wizard_save(void) {
    if (s_app.store.site_count >= PW_SITES_MAX) {
        toast(STR_SITES_FULL);
        return;
    }
    // trailing spaces are the wheel's blank filler, not content
    char name[PW_NAME_MAX + 1u];
    uint32_t end = s_app.wiz_input.len;
    while (end > 0u && s_app.wiz_input.name[end - 1u] == ' ') end--;
    memcpy(name, s_app.wiz_input.name, end);
    name[end] = '\0';
    pw_site_t *site = &s_app.store.sites[s_app.store.site_count];
    site->row = s_app.wiz_row;
    site->col = s_app.wiz_col;
    site->len = s_app.wiz_len;
    memcpy(site->name, name, sizeof(site->name));
    s_app.store.site_count++;
    s_app.site_cursor = (uint8_t)(s_app.store.site_count - 1u);
    sites_scroll_to_cursor();
    if (pw_store_save(&s_app.store) != ESP_OK) {
        ESP_LOGW(TAG, "保存站点失败,NVS 写入失败");
    }
    ESP_LOGI(TAG, "site added row=%u col=%u len=%u count=%u",
             (unsigned)(site->row + 1u), (unsigned)('A' + site->col),
             (unsigned)site->len, (unsigned)s_app.store.site_count);
    show_screen(PW_SCREEN_SITES);
    toast(STR_TOAST_SAVED);
}

static void wizard_step(int dir) {
    switch (s_app.wiz_step) {
    case 0:
        s_app.wiz_row = (uint8_t)((s_app.wiz_row + PW_ROWS_N +
                                   (dir > 0 ? 1u : PW_ROWS_N - 1u)) %
                                  PW_ROWS_N);
        break;
    case 1:
        s_app.wiz_col = (uint8_t)((s_app.wiz_col + PW_COLS_N +
                                   (dir > 0 ? 1u : PW_COLS_N - 1u)) %
                                  PW_COLS_N);
        break;
    case 2:
        s_app.wiz_len = (uint8_t)(PW_LEN_MIN +
                                  ((s_app.wiz_len - PW_LEN_MIN +
                                    PW_LEN_MAX - PW_LEN_MIN + 1u +
                                    (dir > 0 ? 1u
                                             : PW_LEN_MAX - PW_LEN_MIN)) %
                                   (PW_LEN_MAX - PW_LEN_MIN + 1u)));
        break;
    default: {
        const uint32_t n = PW_NAME_WHEEL_N;
        s_app.wiz_input.cursor =
            (s_app.wiz_input.cursor + n + (dir > 0 ? 1u : n - 1u)) % n;
        break;
    }
    }
    render_wizard();
}

static void wizard_ok(void) {
    if (s_app.wiz_step < 3u) {
        s_app.wiz_step++;
        render_wizard();
        return;
    }
    switch (pw_name_ok(&s_app.wiz_input)) {
    case PW_NAME_DONE:
        wizard_save();
        return;
    case PW_NAME_FULL:
        foot_flash();  // name is already 8 chars: delete or finish first
        return;
    default:
        break;
    }
    render_wizard();
}

static void confirm_execute(void) {
    if (s_app.confirm_what == 0u) {
        esp_fill_random(s_app.store.seed, PW_SEED_BYTES);
        if (pw_store_save(&s_app.store) != ESP_OK) {
            ESP_LOGW(TAG, "重生成保存失败");
        }
        show_screen(PW_SCREEN_SETTINGS);
        toast(STR_TOAST_REGEN);
        ESP_LOGI(TAG, "card regenerated");
    } else {
        s_app.store.site_count = 0;
        s_app.site_cursor = 0;
        s_app.site_top = 0;
        if (pw_store_save(&s_app.store) != ESP_OK) {
            ESP_LOGW(TAG, "清空保存失败");
        }
        show_screen(PW_SCREEN_SETTINGS);
        toast(STR_TOAST_CLEAR);
        ESP_LOGI(TAG, "sites cleared");
    }
}

// -------------------------------------------------------------- key dispatch

static void step_screen(int button) {
    const int dir = button == BSP_BTN_UP ? -1 : 1;
    switch (s_app.screen) {
    case PW_SCREEN_CARD:
        s_app.cursor_row = (uint8_t)((s_app.cursor_row + PW_ROWS_N + dir) %
                                     PW_ROWS_N);
        render_card();
        break;
    case PW_SCREEN_SITES: {
        const uint32_t total = sites_total();
        s_app.site_cursor =
            (uint8_t)((s_app.site_cursor + total + dir) % total);
        sites_scroll_to_cursor();
        render_sites();
        break;
    }
    case PW_SCREEN_WIZARD:
        wizard_step(dir);
        break;
    case PW_SCREEN_SETTINGS:
        s_app.set_cursor = (uint8_t)((s_app.set_cursor + 4u + dir) % 4u);
        render_settings();
        break;
    default:
        foot_flash();
        break;
    }
}

static void sleep_warn(const char *step, esp_err_t err) {
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep 继续:%s 失败: %s", step,
                 esp_err_to_name(err));
    }
}

// BSP terminal-shutdown order (contract of demo_low_power.c), running inside
// the LVGL timer: display stop already excludes concurrent refreshes.
static void enter_sleep(void) {
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) return;  // held: retry next tick
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart();
    sleep_warn("GPIO wake arm",
               esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO,
                                                 ESP_GPIO_WAKEUP_GPIO_LOW));
    sleep_warn("CW2017 suspend", bsp_battery_sleep());
    sleep_warn("ES8311 suspend", bsp_audio_sleep());
    sleep_warn("I2S pin release", bsp_audio_prepare_deep_sleep());
    sleep_warn("shared I2C pin release", bsp_i2c_prepare_deep_sleep());
    sleep_warn("ST7789 suspend", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    esp_restart();
}

static int s_hold_btn = -1;
static uint32_t s_hold_ms;

static void tick(lv_timer_t *timer) {
    (void)timer;
    pw_power_advance(&s_power, 250u);
    bsp_display_backlight(pw_power_backlight(&s_power));
    if (++s_soc_tick >= 20u) {  // battery is an I2C read: 5 s cadence, cached
        s_soc_tick = 0u;
        battery_refresh();
        if (s_app.screen == PW_SCREEN_CARD) render_battery(s_card_batt);
    }
    if (s_app.screen == PW_SCREEN_PASS && s_app.reveal_ms > 0u) {
        s_app.reveal_ms = s_app.reveal_ms > 250u ? s_app.reveal_ms - 250u : 0u;
        if (s_app.reveal_ms == 0u) {
            render_pass();
            ESP_LOGI(TAG, "reveal timed out, masked");
        } else {
            const int32_t w = (int32_t)((180 * (uint64_t)s_app.reveal_ms) /
                                        (uint64_t)PW_REVEAL_MS);
            lv_obj_set_size(s_pass_bar, w, 4);
        }
    }
    if (s_hold_btn >= 0) {  // hold-to-repeat: 400 ms delay, 150 ms steps
        s_hold_ms += 250u;
        if (s_hold_ms >= 400u && ((s_hold_ms - 400u) % 150u) < 250u) {
            step_screen(s_hold_btn);
        }
    }
    if (pw_power_should_sleep(&s_power)) enter_sleep();
}

static void handle_key(int button, int event) {
    pw_power_activity(&s_power);
    bsp_display_backlight(PW_BACKLIGHT_ACTIVE);
    if (event == BSP_BTN_RELEASE) {
        if ((int)button == s_hold_btn) s_hold_btn = -1;
        return;
    }
    if (event == BSP_BTN_PRESS &&
        (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        switch (s_app.screen) {
        case PW_SCREEN_CARD:
        case PW_SCREEN_SITES:
        case PW_SCREEN_WIZARD:
        case PW_SCREEN_SETTINGS:
            s_hold_btn = button;
            s_hold_ms = 0;
            step_screen(button);
            return;
        case PW_SCREEN_PASS:
            if (s_app.reveal_ms > 0u) {  // any non-OK key kills a reveal
                s_app.reveal_ms = 0u;
                render_pass();
            } else {
                foot_flash();
            }
            return;
        default:
            foot_flash();
            return;
        }
    }
    if (event == BSP_BTN_CLICK &&
        (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        if (s_app.screen == PW_SCREEN_CONFIRM) {
            show_screen(PW_SCREEN_SETTINGS);
        } else {
            foot_flash();
        }
        return;
    }
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK) {
        switch (s_app.screen) {
        case PW_SCREEN_CARD:
            s_app.set_cursor = 0;
            show_screen(PW_SCREEN_SETTINGS);
            break;
        case PW_SCREEN_SITES:
            show_screen(PW_SCREEN_CARD);
            break;
        case PW_SCREEN_PASS:
            s_app.reveal_ms = 0u;
            show_screen(PW_SCREEN_SITES);
            break;
        case PW_SCREEN_WIZARD:
            show_screen(PW_SCREEN_SITES);
            break;
        case PW_SCREEN_SETTINGS:
            show_screen(PW_SCREEN_CARD);
            break;
        case PW_SCREEN_CONFIRM:
            show_screen(PW_SCREEN_SETTINGS);
            break;
        case PW_SCREEN_SEED:
            show_screen(PW_SCREEN_SETTINGS);
            break;
        case PW_SCREEN_HELP:
        default:
            show_screen(s_app.help_back_settings ? PW_SCREEN_SETTINGS
                                                 : PW_SCREEN_CARD);
            break;
        }
        return;
    }
    if (event != BSP_BTN_CLICK) return;
    if (button == BSP_BTN_OK) {
        switch (s_app.screen) {
        case PW_SCREEN_CARD:
            show_screen(PW_SCREEN_SITES);
            break;
        case PW_SCREEN_SITES:
            if (s_app.site_cursor >= s_app.store.site_count) {
                // the fixed add-site row
                wizard_reset();
                show_screen(PW_SCREEN_WIZARD);
            } else {
                s_app.reveal_ms = 0u;
                show_screen(PW_SCREEN_PASS);
            }
            break;
        case PW_SCREEN_PASS:
            s_app.reveal_ms = s_app.reveal_ms > 0u ? 0u : PW_REVEAL_MS;
            render_pass();
            if (s_app.reveal_ms > 0u) {
                ESP_LOGI(TAG, "password revealed site=%u",
                         (unsigned)s_app.site_cursor);
            }
            break;
        case PW_SCREEN_WIZARD:
            wizard_ok();
            break;
        case PW_SCREEN_SETTINGS:
            switch (s_app.set_cursor) {
            case 0:
                s_app.confirm_what = 0;
                show_screen(PW_SCREEN_CONFIRM);
                break;
            case 1:
                show_screen(PW_SCREEN_SEED);
                break;
            case 2:
                s_app.confirm_what = 1;
                show_screen(PW_SCREEN_CONFIRM);
                break;
            default:
                s_app.help_back_settings = true;
                show_screen(PW_SCREEN_HELP);
                break;
            }
            break;
        case PW_SCREEN_CONFIRM:
            confirm_execute();
            break;
        case PW_SCREEN_SEED:
            show_screen(PW_SCREEN_SETTINGS);
            break;
        case PW_SCREEN_HELP:
        default:
            show_screen(s_app.help_back_settings ? PW_SCREEN_SETTINGS
                                                 : PW_SCREEN_CARD);
            s_app.help_back_settings = false;
            break;
        }
    }
}

static void key_task(void *arg) {
    (void)arg;
    pw_key_t key;
    while (xQueueReceive(s_keys, &key, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(300)) {
            ESP_LOGW(TAG, "lvgl lock timeout, key dropped b=%d e=%d",
                     key.button, key.event);
            continue;
        }
        handle_key(key.button, key.event);
        bsp_lvgl_unlock();
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    // main.c owns the wake guard and may still swallow this event.
    extern void on_key(int button, int event);
    on_key((int)button, (int)event);
}

void pwcard_button_event(int button, int event) {
    pw_key_t key = {.button = button, .event = event};
    if (s_keys) (void)xQueueSend(s_keys, &key, 0);
}

esp_err_t pwcard_app_start(void) {
    memset(&s_app, 0, sizeof(s_app));
    pw_power_reset(&s_power);
    s_hold_btn = -1;
    esp_err_t err = pw_store_load(&s_app.store);
    s_app.storage_ok = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "存储不可用(%s),按首启处理", esp_err_to_name(err));
    }
    bool seed_blank = true;
    for (uint32_t i = 0; i < PW_SEED_BYTES; i++) {
        if (s_app.store.seed[i] != 0u) {
            seed_blank = false;
            break;
        }
    }
    bool first_boot = seed_blank || s_app.store.help_seen == 0u;
    if (seed_blank) {
        esp_fill_random(s_app.store.seed, PW_SEED_BYTES);
        (void)pw_store_save(&s_app.store);
        ESP_LOGI(TAG, "首启:已生成新卡种子");
    }

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) return ESP_FAIL;
    bsp_display_backlight(PW_BACKLIGHT_ACTIVE);
    if (bsp_battery_init() == ESP_OK) battery_refresh();
    (void)bsp_audio_init();
    (void)bsp_audio_sleep();

    s_keys = xQueueCreate(8, sizeof(pw_key_t));
    if (!s_keys ||
        xTaskCreate(key_task, "pwcard_keys", 4096, NULL, 5, &s_key_task) !=
            pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (bsp_button_init(button_callback, NULL) != ESP_OK) return ESP_FAIL;
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    s_app.help_back_settings = false;
    if (first_boot) {
        s_app.store.help_seen = 1u;
        (void)pw_store_save(&s_app.store);
        show_screen(PW_SCREEN_HELP);
    } else {
        show_screen(PW_SCREEN_CARD);
    }
    s_timer = lv_timer_create(tick, 250, NULL);
    bsp_lvgl_unlock();
    s_ready = true;
    ESP_LOGI(TAG, "密码卡就绪 sites=%u", (unsigned)s_app.store.site_count);
    return ESP_OK;
}

void pwcard_app_stop(void) {
    s_ready = false;
    if (s_key_task) {
        vTaskDelete(s_key_task);
        s_key_task = NULL;
    }
    if (s_keys) {
        vQueueDelete(s_keys);
        s_keys = NULL;
    }
}
