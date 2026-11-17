// cheer_ui.c - single-screen UI, v3 "letter card + lantern" layout:
// the praise card is the hero (nearly half the screen, warm paper), the
// spirit is a pixel paper lantern standing bottom-left whose glow color and
// size follow the growth stages; progress and hint lines sit beside it,
// left-aligned. Own warm-cream background (per the repo rule every
// derivative app designs its own UI; palette stays in the house paper/ink/
// amber family). Entry points assume the caller holds the LVGL lock; the
// pulse and idle timers run inside the LVGL task.
// v1.3 adds: scene tag beside the title, a modal scene picker overlay,
// dynamic mode hints, and a FAV mark on favorited cards.
#include "cheer_ui.h"
#include "cheer_cards.h"
#include "cheer_font.h"
#include "cheer.h"
#include "ui_pixel.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "lvgl.h"

#define IDLE_DIM_MS 10000
// 深睡阈值:空闲超过此时长由空闲定时器回调通知应用层入睡(按键唤醒)。
#define IDLE_SLEEP_MS 60000

// Night-lamp theme: deep warm-brown night background, cream card glows
// like a paper lantern, chrome text in cream. Contrast on night bg:
// cream 11.7:1, muted 7.3:1 (WCAG AA); card text on paper 10.9:1.
// Picker panel lifts the night bg one step lighter so the overlay reads
// as a layer; cream on it still exceeds 7:1.
#define BG_NIGHT    0x241A12
#define BG_PANEL    0x2E2117
#define CARD_PAPER  0xFFF3D6
#define CARD_BORDER 0xB08A5A
#define CARD_TEXT   0x3D2E1E
#define CREAM       0xF2E3C8
#define CARD_MUTED  0xC9B490
#define LANTERN_INK 0xD9B98C

static const uint32_t STAGE_COLORS[CHEER_STAGE_COUNT] = {
    0xC98A4B, 0xE8A24C, 0xF5B84A, 0xFFCE56, 0xFFDF7E, 0xFFEEA6,
};

static const char *GROUP_CHIPS[CHEER_GRP_COUNT] = {
    "DAILY", "WORK", "STUDY", "BRAVE", "NIGHT", "OUT",
};

// Group chip colors, all >= 5:1 on CARD_PAPER (WCAG AA small text).
static const uint32_t GROUP_COLORS[CHEER_GRP_COUNT] = {
    0xA05A10, 0x2F6B4F, 0x4A5FA5, 0xB03A3A, 0x53417E, 0x1F6E68,
};

// Scene picker rows: 0 ALL, 1..CHEER_GRP_COUNT scenes, then favorites.
#define PICKER_ROW_ALL    0
#define PICKER_ROW_FAVS   (CHEER_GRP_COUNT + 1)
#define PICKER_ROW_COUNT  (CHEER_GRP_COUNT + 2)
#define PICKER_ROW_H      24
#define PICKER_PANEL_W    160
#define PICKER_PANEL_H    244

static const char *PICKER_ROWS[PICKER_ROW_COUNT] = {
    "ALL", "DAILY", "WORK", "STUDY", "BRAVE", "NIGHT", "OUT", "FAVS",
};

static lv_obj_t *s_batt;
static lv_obj_t *s_volt;
static lv_obj_t *s_chip;
static lv_obj_t *s_text;
static lv_obj_t *s_hist;
static lv_obj_t *s_aux;
static lv_obj_t *s_glow;
static lv_obj_t *s_light;
static lv_obj_t *s_lantern;
static lv_obj_t *s_scene;
static lv_obj_t *s_hint1;
static lv_obj_t *s_hint2;
static lv_obj_t *s_picker;
static lv_obj_t *s_pick_hl;
static lv_obj_t *s_pick_labels[PICKER_ROW_COUNT];
static bool s_pick_open;
static int s_pick_sel;

static uint32_t s_idle_ms;
static bool s_dimmed;
static esp_timer_handle_t s_idle_timer;
static cheer_ui_idle_sleep_cb_t s_sleep_cb;

static void idle_timer_cb(void *arg) {
    (void)arg;
    s_idle_ms += 1000;
    if (s_idle_ms >= IDLE_DIM_MS && !s_dimmed) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
    // 达到深睡阈值后每秒回调一次:拒绝入睡(按键被按住)时下一秒自动重试,
    // 活动重置计时后自然停止。回调运行在 esp_timer 任务,必须保持有界。
    if (s_idle_ms >= IDLE_SLEEP_MS && s_sleep_cb) s_sleep_cb();
}

void cheer_ui_set_idle_sleep_cb(cheer_ui_idle_sleep_cb_t cb) {
    s_sleep_cb = cb;
}

void cheer_ui_note_activity(void) {
    s_idle_ms = 0;
    if (s_dimmed) {
        bsp_display_backlight(100);
        s_dimmed = false;
    }
}

static void pulse_cb(lv_timer_t *t) {
    (void)t;
    static bool bright;
    bright = !bright;
    lv_obj_set_style_bg_opa(s_light, bright ? 250 : 170, 0);
}

// 深睡中电量计会挂起重启，唤醒后 SOC 要 ~1-3 秒重锚（期间会读出 0 之类的
// 窗口假值）。用 RTC 内存缓存最后有效读数：跨深睡存活、真断电失效、
// 零写入损耗（demo_low_power 同款机制）。唤醒后先显缓存值，首个 30 秒
// tick 到来时电量计早已重锚完毕；抽卡按键仍即时刷新。
#define BATT_CACHE_MAGIC 0x42415454UL  // "BATT"
static RTC_DATA_ATTR uint32_t s_batt_cache_magic;
static RTC_DATA_ATTR int32_t s_batt_cache_soc;
static RTC_DATA_ATTR int32_t s_batt_cache_mv;

// 保持最后有效值：重启窗口内单次读可能 NACK(-1)，不闪 "--"；占位符只在
// 从未读到过有效值时出现。
static void battery_apply(int soc, int mv) {
    if (soc >= 0) { s_batt_cache_soc = soc; s_batt_cache_magic = BATT_CACHE_MAGIC; }
    if (mv >= 0) { s_batt_cache_mv = mv; s_batt_cache_magic = BATT_CACHE_MAGIC; }
    if (s_batt_cache_magic != BATT_CACHE_MAGIC) {
        lv_label_set_text(s_batt, "--%");
        lv_label_set_text(s_volt, "--.-V");
        return;
    }
    lv_label_set_text_fmt(s_batt, "%d%%", (int)s_batt_cache_soc);
    lv_label_set_text_fmt(s_volt, "%d.%02dV", (int)s_batt_cache_mv / 1000,
                           ((int)s_batt_cache_mv % 1000) / 10);
}

static void battery_tick_cb(lv_timer_t *t) {
    (void)t;
    battery_apply(bsp_battery_soc(), bsp_battery_mv());
}

static lv_obj_t *part(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *text_label(lv_obj_t *parent, const char *txt,
                            const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static void lantern_build(lv_obj_t *scr) {
    // Paper lantern ~26x44 standing at (34, 242); glow disc behind it.
    s_glow = part(scr, 20, 232, 56, 56, STAGE_COLORS[0]);
    lv_obj_set_style_radius(s_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_glow, 60, 0);

    s_lantern = lv_obj_create(scr);
    lv_obj_remove_style_all(s_lantern);
    lv_obj_set_pos(s_lantern, 34, 242);
    lv_obj_set_size(s_lantern, 28, 44);
    lv_obj_set_style_bg_opa(s_lantern, LV_OPA_TRANSP, 0);

    part(s_lantern, 9, 0, 10, 4, LANTERN_INK);     // hanging ring
    part(s_lantern, 4, 4, 20, 5, LANTERN_INK);     // top cap
    lv_obj_t *body = part(s_lantern, 2, 9, 24, 26, CARD_PAPER);
    lv_obj_set_style_radius(body, 8, 0);
    part(s_lantern, 7, 11, 2, 22, 0xE8D9BC);       // ribs
    part(s_lantern, 19, 11, 2, 22, 0xE8D9BC);
    s_light = part(s_lantern, 10, 14, 8, 16, STAGE_COLORS[0]);
    part(s_lantern, 4, 35, 20, 5, LANTERN_INK);    // bottom cap
}

static void lantern_apply(int stage) {
    uint32_t c = STAGE_COLORS[stage];
    lv_obj_set_style_bg_color(s_glow, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(s_glow, (lv_opa_t)(50 + stage * 15), 0);
    int g = 44 + stage * 6;
    lv_obj_set_size(s_glow, g, g);
    lv_obj_set_pos(s_glow, 48 - g / 2, 260 - g / 2);
    lv_obj_set_style_bg_color(s_light, lv_color_hex(c), 0);
}

static void picker_apply(void) {
    lv_obj_set_pos(s_pick_hl, 6, 24 + s_pick_sel * PICKER_ROW_H);
    for (int i = 0; i < PICKER_ROW_COUNT; i++) {
        lv_obj_set_style_text_color(
            s_pick_labels[i],
            lv_color_hex(i == s_pick_sel ? BG_NIGHT : CREAM), 0);
    }
}

static void picker_build(lv_obj_t *scr) {
    s_picker = lv_obj_create(scr);
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_picker, (240 - PICKER_PANEL_W) / 2, 44);
    lv_obj_set_size(s_picker, PICKER_PANEL_W, PICKER_PANEL_H);
    lv_obj_set_style_radius(s_picker, 10, 0);
    lv_obj_set_style_bg_color(s_picker, lv_color_hex(BG_PANEL), 0);
    lv_obj_set_style_border_color(s_picker, lv_color_hex(CARD_BORDER), 0);
    lv_obj_set_style_border_width(s_picker, 2, 0);
    lv_obj_set_style_pad_all(s_picker, 0, 0);

    lv_obj_t *title = text_label(s_picker, "SCENE", &lv_font_montserrat_14,
                                 CARD_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 7);

    s_pick_hl = part(s_picker, 6, 24, PICKER_PANEL_W - 12, PICKER_ROW_H - 2,
                     CREAM);
    lv_obj_set_style_radius(s_pick_hl, 6, 0);

    for (int i = 0; i < PICKER_ROW_COUNT; i++) {
        s_pick_labels[i] = text_label(s_picker, PICKER_ROWS[i],
                                      &lv_font_montserrat_14, CREAM);
        lv_obj_align(s_pick_labels[i], LV_ALIGN_TOP_MID, 0,
                     24 + i * PICKER_ROW_H + 4);
    }

    lv_obj_t *hint = text_label(s_picker, "OK PICK OKL BACK",
                                &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    s_pick_open = false;
    s_pick_sel = PICKER_ROW_ALL;
    picker_apply();
}

void cheer_ui_build(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_NIGHT), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *title = text_label(scr, "CHEER", &lv_font_montserrat_20,
                                 CREAM);
    lv_obj_set_pos(title, 14, 8);
    // Scene tag: hidden for ALL. Worst case "SCENE: STUDY" stays left of
    // the battery corner (pixel budget pinned by test_cheer_ui_budget.c).
    s_scene = text_label(scr, "", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_set_pos(s_scene, 84, 14);
    // Right-aligned corner: worst-case "4.44V" measures 41.25 px in
    // Montserrat 14, so a fixed x of 204 would push it past the 240 px edge
    // (204+41>240). Anchoring both labels to the right edge keeps every
    // string inside with a constant 12 px pad.
    s_batt = text_label(scr, "80%", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -12, 12);
    s_volt = text_label(scr, "--.-V", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(s_volt, LV_ALIGN_TOP_RIGHT, -12, 30);

    // The card is the hero: x14 y50 w212 h172 (four 24 px CJK lines fit).
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, 14, 50);
    lv_obj_set_size(card, 212, 172);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(CARD_PAPER), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CARD_BORDER), 0);
    lv_obj_set_style_border_width(card, 4, 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    s_chip = text_label(card, "DAILY", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_set_pos(s_chip, 10, 8);
    s_text = lv_label_create(card);
    lv_label_set_text(s_text, "...");
    lv_obj_set_style_text_font(s_text, &cheer_font, 0);
    lv_obj_set_width(s_text, 186);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_text, lv_color_hex(CARD_TEXT), 0);
    lv_obj_set_style_text_line_space(s_text, 8, 0);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_text);
    lv_obj_set_y(s_text, 96);   // recenters on every set_text (see below)

    s_hist = text_label(scr, "", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(s_hist, LV_ALIGN_TOP_MID, 0, 28);
    // Bottom column: three lines vertically centered on the lantern body
    // (lantern center y=264; line centers 244/266/288).
    s_aux = text_label(scr, "PRAISE 0", &lv_font_montserrat_14, CREAM);
    lv_obj_set_pos(s_aux, 96, 236);
    s_hint1 = text_label(scr, "OK PRAISE", &lv_font_montserrat_14,
                         CARD_MUTED);
    lv_obj_set_pos(s_hint1, 96, 258);
    s_hint2 = text_label(scr, "OKL SCENE", &lv_font_montserrat_14,
                         CARD_MUTED);
    lv_obj_set_pos(s_hint2, 96, 280);

    lantern_build(scr);
    lantern_apply(0);
    picker_build(scr);
    (void)lv_timer_create(pulse_cb, 350, NULL);
    // 有缓存（深睡唤醒）先显睡前值，等首个 30 秒 tick 再取新读数；无缓存
    // （冷启动/首次上电）立即读一次。
    if (s_batt_cache_magic == BATT_CACHE_MAGIC) {
        battery_apply(-1, -1);
    } else {
        battery_tick_cb(NULL);
    }
    (void)lv_timer_create(battery_tick_cb, 30000, NULL);

    const esp_timer_create_args_t ta = {
        .callback = idle_timer_cb,
        .name = "cheer_idle",
    };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_idle_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_idle_timer, 1000000));

    lv_screen_load(scr);

    // Boot greeting: one free praise (a real library card, so every glyph is
    // guaranteed to be in the generated font subset).
    cheer_ui_show_card(3, false);
}

void cheer_ui_show_card(int idx, bool fav) {
    const cheer_card_t *c = &CHEER_CARDS[idx];
    if (fav) {
        lv_label_set_text_fmt(s_chip, "%s FAV", GROUP_CHIPS[c->grp]);
    } else {
        lv_label_set_text(s_chip, GROUP_CHIPS[c->grp]);
    }
    lv_obj_set_style_text_color(s_chip, lv_color_hex(GROUP_COLORS[c->grp]), 0);
    lv_label_set_text(s_text, c->text);
    lv_obj_center(s_text);   // recenter vertically for 1-3 line texts
}

void cheer_ui_set_hist(const char *txt) {
    lv_label_set_text(s_hist, txt ? txt : "");
}

void cheer_ui_set_hint(const char *top, const char *bottom) {
    if (top) lv_label_set_text(s_hint1, top);
    if (bottom) lv_label_set_text(s_hint2, bottom);
}

void cheer_ui_set_scene(int scene) {
    if (scene > 0 && scene <= CHEER_GRP_COUNT) {
        lv_label_set_text_fmt(s_scene, "SCENE: %s",
                              GROUP_CHIPS[scene - 1]);
    } else {
        lv_label_set_text(s_scene, "");
    }
}

void cheer_ui_set_progress(uint32_t count) {
    int stage = cheer_stage_of(count);
    if (stage >= CHEER_STAGE_COUNT - 1) {
        lv_label_set_text_fmt(s_aux, "PRAISE %lu · MAX",
                              (unsigned long)count);
    } else {
        lv_label_set_text_fmt(s_aux, "PRAISE %lu / %lu",
                              (unsigned long)count,
                              (unsigned long)cheer_stage_floor(stage + 1));
    }
    lantern_apply(stage);
}

void cheer_ui_set_battery(int soc) {
    battery_apply(soc, bsp_battery_mv());
}

void cheer_ui_picker_open(int selected) {
    if (s_pick_open) return;
    if (selected < 0 || selected >= PICKER_ROW_COUNT) selected = PICKER_ROW_ALL;
    s_pick_sel = selected;
    picker_apply();
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_picker);
    s_pick_open = true;
}

void cheer_ui_picker_close(void) {
    if (!s_pick_open) return;
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    s_pick_open = false;
}

void cheer_ui_picker_move(int delta) {
    if (!s_pick_open) return;
    int v = s_pick_sel + delta;
    if (v < 0) v = 0;
    if (v > PICKER_ROW_COUNT - 1) v = PICKER_ROW_COUNT - 1;
    s_pick_sel = v;
    picker_apply();
}

int cheer_ui_picker_pick(void) {
    return s_pick_open ? s_pick_sel : -1;
}

bool cheer_ui_picker_is_open(void) {
    return s_pick_open;
}
