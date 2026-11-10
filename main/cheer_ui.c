// cheer_ui.c - single-screen UI, v3 "letter card + lantern" layout:
// the praise card is the hero (nearly half the screen, warm paper), the
// spirit is a pixel paper lantern standing bottom-left whose glow color and
// size follow the growth stages; progress and hint lines sit beside it,
// left-aligned. Own warm-cream background (per the repo rule every
// derivative app designs its own UI; palette stays in the house paper/ink/
// amber family). Entry points assume the caller holds the LVGL lock; the
// pulse and idle timers run inside the LVGL task.
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
#define BG_NIGHT    0x241A12
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
    "DAILY", "WORK", "STUDY", "BRAVE",
};

// Group chip colors, all >= 5:1 on CARD_PAPER (WCAG AA small text).
static const uint32_t GROUP_COLORS[CHEER_GRP_COUNT] = {
    0xA05A10, 0x2F6B4F, 0x4A5FA5, 0xB03A3A,
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

void cheer_ui_build(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_NIGHT), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *title = ui_pixel_label(scr, "CHEER", &lv_font_montserrat_20,
                                     CREAM);
    lv_obj_set_pos(title, 14, 8);
    // Right-aligned corner: worst-case "4.44V" measures 41.25 px in
    // Montserrat 14, so a fixed x of 204 would push it past the 240 px edge
    // (204+41>240). Anchoring both labels to the right edge keeps every
    // string inside with a constant 12 px pad.
    s_batt = ui_pixel_label(scr, "80%", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -12, 12);
    s_volt = ui_pixel_label(scr, "--.-V", &lv_font_montserrat_14, CARD_MUTED);
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

    s_chip = ui_pixel_label(card, "DAILY", &lv_font_montserrat_14,
                            CARD_MUTED);
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

    s_hist = ui_pixel_label(scr, "", &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_align(s_hist, LV_ALIGN_TOP_MID, 0, 28);
    // Bottom column: three lines vertically centered on the lantern body
    // (lantern center y=264; line centers 244/266/288).
    s_aux = ui_pixel_label(scr, "PRAISE 0", &lv_font_montserrat_14,
                           CREAM);
    lv_obj_set_pos(s_aux, 96, 236);
    lv_obj_t *hint1 = ui_pixel_label(scr, "OK PRAISE",
                                     &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_set_pos(hint1, 96, 258);
    lv_obj_t *hint2 = ui_pixel_label(scr, "UP/DN HISTORY",
                                     &lv_font_montserrat_14, CARD_MUTED);
    lv_obj_set_pos(hint2, 96, 280);

    lantern_build(scr);
    lantern_apply(0);
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
    cheer_ui_show_card(3, 0, 0);
}

void cheer_ui_show_card(int idx, int hist_pos, int hist_len) {
    const cheer_card_t *c = &CHEER_CARDS[idx];
    lv_label_set_text(s_chip, GROUP_CHIPS[c->grp]);
    lv_obj_set_style_text_color(s_chip, lv_color_hex(GROUP_COLORS[c->grp]), 0);
    lv_label_set_text(s_text, c->text);
    lv_obj_center(s_text);   // recenter vertically for 1-3 line texts
    if (hist_pos > 0 && hist_len > 0) {
        lv_label_set_text_fmt(s_hist, "-%d/%d -", hist_pos, hist_len);
    } else {
        lv_label_set_text(s_hist, "");
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
