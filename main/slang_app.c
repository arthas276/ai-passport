#include "slang_app.h"

#include "slang_core.h"
#include "slang_power.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(slang_font_16);
LV_FONT_DECLARE(slang_font_20);

static const char *TAG = "slang";
static char s_present[27];   /* 有条目的首字母(小写升序);空字母桶不进选择器 */

typedef enum {
    SLANG_LIST = 0,
    SLANG_LETTER,
} slang_screen_t;

typedef struct {
    int button;
    int event;
} slang_key_t;

static slang_screen_t s_screen;
static uint16_t s_matches[256];
static uint16_t s_match_count;
static uint16_t s_selected;
static uint8_t s_letter;
static bool s_battery_ok;
static slang_power_t s_power;

static lv_obj_t *s_root;
static lv_obj_t *s_card;
static lv_obj_t *s_top_l;   /* 顶栏左：筛选状态 */
static lv_obj_t *s_top_r;   /* 顶栏右：电量 */
static lv_obj_t *s_title;   /* 卡内词条 20px */
static lv_obj_t *s_meta;    /* 卡内「读法 · 拼音」 16px */
static lv_obj_t *s_divider; /* 卡内分隔线 */
static lv_obj_t *s_gloss;   /* 卡内释义 16px；字母页兼作提示行 */
static lv_obj_t *s_short;   /* 卡内短评 16px */
static lv_obj_t *s_big;     /* 字母选择大字 20px */
static lv_obj_t *s_footer;  /* 底栏左提示 */
static lv_obj_t *s_index;   /* 底栏右页码 */
static lv_timer_t *s_timer;
static QueueHandle_t s_keys;

static void build_present_letters(void) {
    bool seen[26] = {false};
    for (uint16_t i = 0; i < slang_count(); ++i) {
        const slang_entry_t *entry = slang_at(i);
        if (entry && entry->pinyin[0] >= 'a' && entry->pinyin[0] <= 'z') {
            seen[entry->pinyin[0] - 'a'] = true;
        }
    }
    uint8_t n = 0u;
    for (int i = 0; i < 26; ++i) {
        if (seen[i]) s_present[n++] = (char)('a' + i);
    }
    s_present[n] = '\0';
}

static void load_letter(char letter) {
    s_match_count = slang_by_initial(letter, s_matches, 256u);
    s_selected = 0u;
}

static void load_all(void) {
    s_match_count = slang_count();
    if (s_match_count > 256u) s_match_count = 256u;
    for (uint16_t i = 0; i < s_match_count; ++i) s_matches[i] = i;
    s_selected = 0u;
    s_letter = 0u;
}

static int s_soc = -1;      /* 缓存电量读数:CW2017 走 I2C,不能每 250ms 读一次 */
static uint8_t s_soc_tick;

static void battery_refresh(void) {
    if (s_battery_ok) s_soc = bsp_battery_soc();
}

static void battery_text(char *out, size_t cap) {
    if (!s_battery_ok || s_soc < 0) snprintf(out, cap, "--");
    else snprintf(out, cap, "%d%%", s_soc);
}

/* ---- 「夜读」扁平卡 v1.3 UI（ui-ux-pro-max 规范） ----------------------------
 * 240x320 像素预算：
 *   顶栏 y=10 h=20：左 x16 w150 金色左对齐；右 x224 右对齐 浅蓝灰
 *   词条卡 x=12 y=40 w=216 h=212 r=16 底#1A2E48 边框#2A4A6E（内容全展开，无详情页）
 *     词条   +16,+32  w184 20px #F7F4EC（词库最长 8 字=160px，单行）
 *     读法行 +16,+84  w184 16px #D7E2EA（「读法 · 拼音」，见字宽审计）
 *     分隔线 +16,+116 w184 h1 #2A4A6E
 *     释义   +16,+128 w184 16px #F7F4EC（最长 16 字折 2 行至 168）
 *     短评   +16,+176 w184 16px #8EA0B0（最长 6 字，单行至 196）
 *     大字母 居中 +72  20px 金（字母筛选页）
 *   底栏 y=292 h=20：左 x16 w208（两屏提示均单行：列表 118px / 字母页 202px）；右 x224 右对齐 页码，均 #8EA0B0
 * 字体：霞鹜文楷 v1.530（tools/build_slang_font.py + SLANG_FONT_FILE）
 * 对比度（卡底#1A2E48）：正文 12.5:1 次级 10.4:1 提示 5.1:1 金 8.2:1，全过 AA */
static void ui_show(lv_obj_t *obj, bool show_it) {
    if (show_it) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *add_label(lv_obj_t *parent, int w, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void show_list(void) {
    char battery[8];
    battery_text(battery, sizeof(battery));
    lv_label_set_text(s_top_r, battery);
    if (s_letter) {
        char key = s_present[s_letter - 1u];
        lv_label_set_text_fmt(s_top_l, "%c · %u条", key >= 'a' ? (char)(key - 'a' + 'A') : key, s_match_count);
    } else {
        lv_label_set_text_fmt(s_top_l, "梗词典 · %u条", s_match_count);
    }
    ui_show(s_title, true);
    ui_show(s_meta, true);
    ui_show(s_divider, true);
    ui_show(s_gloss, true);
    ui_show(s_short, true);
    ui_show(s_big, false);
    const slang_entry_t *entry = slang_at(s_matches[s_selected]);
    if (entry) {
        lv_label_set_text(s_title, entry->title);
        /* 读法与词条相同则只显拼音（194/197 条），不同才拼「读法 · 拼音」，最宽 115px ≤ 184 */
        if (strcmp(entry->reading, entry->title) == 0) lv_label_set_text(s_meta, entry->pinyin);
        else lv_label_set_text_fmt(s_meta, "%s · %s", entry->reading, entry->pinyin);
        lv_label_set_text(s_gloss, entry->gloss);
        lv_label_set_text(s_short, entry->short_gloss);
    }
    lv_obj_set_style_text_align(s_gloss, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_gloss, lv_color_hex(0xF7F4EC), 0);
    lv_obj_set_style_text_align(s_footer, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(s_footer, "上下翻·长按筛选");
    lv_label_set_text_fmt(s_index, "%u/%u", s_selected + 1u, s_match_count);
    ui_show(s_index, true);
}

static void show_letters(void) {
    char shown[8];
    char key = s_present[s_letter];
    snprintf(shown, sizeof(shown), "%c", key >= 'a' ? (char)(key - 'a' + 'A') : key);
    ui_show(s_title, false);
    ui_show(s_meta, false);
    ui_show(s_divider, false);
    ui_show(s_short, false);
    ui_show(s_index, false);
    ui_show(s_big, true);
    lv_label_set_text(s_big, shown);
    ui_show(s_gloss, true);
    lv_obj_set_style_text_align(s_gloss, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_gloss, lv_color_hex(0xD7E2EA), 0);
    lv_label_set_text(s_gloss, "只看这一字母开头");
    lv_obj_set_style_text_align(s_footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_footer, "上下选 · OK确认 · 长按全部");
}

static void render(void) {
    if (!s_root) return;
    if (s_screen == SLANG_LETTER) show_letters();
    else show_list();
}

static void sleep_warn(const char *step, esp_err_t err) {
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep 继续:%s 失败: %s", step, esp_err_to_name(err));
    }
}

/* 按 demo_low_power.c 的 BSP 契约入睡:先拒绝"按住入睡",定时唤醒源在按键
 * 交回(终结性)之前武装并检查;本函数运行于 LVGL 定时器回调,display 停止
 * 天然与 LVGL 刷屏互斥,无需再取 bsp_lvgl_lock。 */
static void enter_sleep(void) {
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) return;   /* 按住:等下一轮 tick */
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

static void tick(lv_timer_t *timer) {
    (void)timer;
    slang_power_advance(&s_power, 250u);
    bsp_display_backlight(slang_power_backlight(&s_power));
    if (++s_soc_tick >= 20u) {   /* 每 5 秒刷新一次电量读数(走 I2C,不能每 tick 读) */
        s_soc_tick = 0u;
        battery_refresh();
    }
    if (s_screen == SLANG_LIST) show_list();
    if (slang_power_should_sleep(&s_power)) enter_sleep();
}

static void handle_key(int button, int event) {
    slang_power_activity(&s_power);
    bsp_display_backlight(SLANG_BACKLIGHT_ACTIVE);
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK) {
        if (s_screen == SLANG_LETTER) {
            load_all();
            s_screen = SLANG_LIST;
        } else {
            s_letter = 0u;
            s_screen = SLANG_LETTER;
        }
        render();
        return;
    }
    if (event != BSP_BTN_CLICK) return;

    if (s_screen == SLANG_LIST) {
        if (!s_match_count) return;
        if (button == BSP_BTN_UP) s_selected = (uint16_t)((s_selected + s_match_count - 1u) % s_match_count);
        else s_selected = (uint16_t)((s_selected + 1u) % s_match_count);   /* DOWN 与 OK 都翻下一条 */
    } else {
        const uint8_t n = (uint8_t)strlen(s_present);
        if (button == BSP_BTN_UP && n) s_letter = (uint8_t)((s_letter + n - 1u) % n);
        if (button == BSP_BTN_DOWN && n) s_letter = (uint8_t)((s_letter + 1u) % n);
        if (button == BSP_BTN_OK && n) {
            load_letter(s_present[s_letter]);
            s_letter = (uint8_t)(s_letter + 1u);
            s_screen = SLANG_LIST;
        }
    }
    render();
}

static void key_task(void *arg) {
    (void)arg;
    slang_key_t key;
    while (xQueueReceive(s_keys, &key, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(300)) continue;
        handle_key(key.button, key.event);
        bsp_lvgl_unlock();
    }
}

void slang_button_event(int button, int event) {
    slang_key_t key = {.button = button, .event = event};
    if (s_keys) (void)xQueueSend(s_keys, &key, 0);
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    extern void on_key(int button, int event);
    on_key((int)button, (int)event);
}

esp_err_t slang_app_start(void) {
    slang_power_reset(&s_power);
    build_present_letters();
    load_all();
    s_battery_ok = bsp_battery_init() == ESP_OK;
    battery_refresh();
    (void)bsp_audio_init();
    (void)bsp_audio_sleep();

    s_keys = xQueueCreate(8, sizeof(slang_key_t));
    if (!s_keys || xTaskCreate(key_task, "slang_keys", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (bsp_button_init(button_callback, NULL) != ESP_OK) return ESP_FAIL;
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;

    s_root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x102033), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);

    s_top_l = add_label(s_root, 150, &slang_font_16, lv_color_hex(0xF2C14E));
    lv_obj_align(s_top_l, LV_ALIGN_TOP_LEFT, 16, 10);
    s_top_r = add_label(s_root, 130, &slang_font_16, lv_color_hex(0xD7E2EA));
    lv_obj_align(s_top_r, LV_ALIGN_TOP_RIGHT, -16, 10);
    lv_obj_set_style_text_align(s_top_r, LV_TEXT_ALIGN_RIGHT, 0);

    s_card = lv_obj_create(s_root);
    lv_obj_align(s_card, LV_ALIGN_TOP_LEFT, 12, 40);
    lv_obj_set_size(s_card, 216, 212);
    lv_obj_set_style_radius(s_card, 16, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(0x1A2E48), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(0x2A4A6E), 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    s_title = add_label(s_card, 184, &slang_font_20, lv_color_hex(0xF7F4EC));
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 16, 32);
    s_meta = add_label(s_card, 184, &slang_font_16, lv_color_hex(0xD7E2EA));
    lv_obj_align(s_meta, LV_ALIGN_TOP_LEFT, 16, 84);
    s_divider = lv_obj_create(s_card);
    lv_obj_align(s_divider, LV_ALIGN_TOP_LEFT, 16, 116);
    lv_obj_set_size(s_divider, 184, 1);
    lv_obj_set_style_radius(s_divider, 0, 0);
    lv_obj_set_style_bg_color(s_divider, lv_color_hex(0x2A4A6E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_divider, 0, 0);
    lv_obj_set_style_pad_all(s_divider, 0, 0);
    s_gloss = add_label(s_card, 184, &slang_font_16, lv_color_hex(0xF7F4EC));
    lv_obj_align(s_gloss, LV_ALIGN_TOP_LEFT, 16, 128);
    s_short = add_label(s_card, 184, &slang_font_16, lv_color_hex(0x8EA0B0));
    lv_obj_align(s_short, LV_ALIGN_TOP_LEFT, 16, 176);
    s_big = add_label(s_card, 184, &slang_font_20, lv_color_hex(0xF2C14E));
    lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);

    s_footer = add_label(s_root, 208, &slang_font_16, lv_color_hex(0x8EA0B0));
    lv_obj_align(s_footer, LV_ALIGN_TOP_LEFT, 16, 292);
    s_index = add_label(s_root, 80, &slang_font_16, lv_color_hex(0x8EA0B0));
    lv_obj_align(s_index, LV_ALIGN_TOP_RIGHT, -16, 292);
    lv_obj_set_style_text_align(s_index, LV_TEXT_ALIGN_RIGHT, 0);

    render();
    lv_screen_load(s_root);
    s_timer = lv_timer_create(tick, 250, NULL);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "离线梗词典就绪 entries=%u", slang_count());
    return ESP_OK;
}
