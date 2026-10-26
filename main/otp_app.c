#include "otp_app.h"

#include "otp_power.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(otp_font_20);

/* 全量搜索校准的下界锚点:固件构建时刻(CMake 注入;无戳时退回 2024-01-01)。 */
#ifndef OTP_BUILD_UNIX
#define OTP_BUILD_UNIX 1704067200ULL
#endif

static const char *TAG = "otp";
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789>";
static const char SECRET_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static otp_app_t s_app;
static otp_power_t s_power;
static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_detail;
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;
static QueueHandle_t s_keys;
static TaskHandle_t s_key_task;
static volatile bool s_ready;

typedef struct {
    int button;
    int event;
} otp_key_t;

static void set_status(const char *text) {
    snprintf(s_app.status, sizeof(s_app.status), "%s", text ? text : "");
}

static void font_init_once(void) {
    static bool ready = false;
    if (ready) return;
    ready = true;
}

static lv_obj_t *make_label(lv_obj_t *parent, int y, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, 210);
    lv_obj_set_style_text_font(label, &otp_font_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static int s_soc = -1;      /* 缓存电量读数:CW2017 走 I2C,不能每 250ms 读一次 */
static uint8_t s_soc_tick;

static void battery_refresh(void) {
    if (s_app.battery_ok) s_soc = bsp_battery_soc();
}

static void render_battery(char *out, size_t cap) {
    if (!s_app.battery_ok || s_soc < 0) snprintf(out, cap, "--");
    else snprintf(out, cap, "%d%%", s_soc);
}

static void render_list(void) {
    char battery[8];
    render_battery(battery, sizeof(battery));
    lv_label_set_text_fmt(s_title, "离线验证码   %s", battery);
    if (!s_app.storage_ok) {
        lv_label_set_text(s_body, "存储不可用");
        lv_label_set_text(s_detail, "本次不保存账号");
    } else if (s_app.store.count == 0) {
        lv_label_set_text(s_body, "空列表");
        lv_label_set_text(s_detail, "OK 打开设置新增");
    } else {
        lv_label_set_text(s_body, s_app.store.accounts[s_app.selected].name);
        lv_label_set_text_fmt(s_detail, "%u / %u", s_app.selected + 1u, s_app.store.count);
    }
    lv_label_set_text(s_footer, "上下选择  OK进入");
}

static void render_code(void) {
    if (s_app.selected >= s_app.store.count) {
        s_app.screen = OTP_SCREEN_LIST;
        render_list();
        return;
    }
    const otp_account_t *account = &s_app.store.accounts[s_app.selected];
    lv_label_set_text(s_title, account->name);
    if (!s_app.store.clock_ready) {
        lv_label_set_text(s_body, "------");
        lv_label_set_text(s_detail, "未校正时间");
    } else {
        char code[OTP_DIGITS + 1u];
        unsigned remain = 0;
        uint64_t now = otp_store_now(&s_app.store);
        if (otp_totp(account->secret, account->secret_len, now, account->period,
                     account->digits, code, &remain) == OTP_OK) {
            char shown[8];
            snprintf(shown, sizeof(shown), "%c%c%c %c%c%c",
                     code[0], code[1], code[2], code[3], code[4], code[5]);
            lv_label_set_text(s_body, shown);
            lv_label_set_text_fmt(s_detail, "剩余 %u 秒", remain);
        } else {
            lv_label_set_text(s_body, "错误");
            lv_label_set_text(s_detail, "无法计算");
        }
    }
    lv_label_set_text(s_footer, "长按OK返回");
}

static void render_menu(void) {
    static const char *items[] = {"新增账号", "设置时间", "删除账号"};
    lv_label_set_text(s_title, "设置");
    lv_label_set_text(s_body, items[s_app.menu_item]);
    lv_label_set_text_fmt(s_detail, "%u / %u", s_app.menu_item + 1u, OTP_MENU_COUNT);
    lv_label_set_text(s_footer, "上下选择  长按返回");
}

static void render_editor(bool secret) {
    lv_label_set_text(s_title, secret ? "输入密钥" : "输入名称");
    const char *value = secret ? s_app.secret : s_app.name;
    lv_label_set_text(s_body, value[0] ? value : "-");
    char current = secret ? SECRET_CHARS[s_app.cursor] : NAME_CHARS[s_app.cursor];
    lv_label_set_text_fmt(s_detail, "当前字符 %c", current);
    lv_label_set_text(s_footer, secret ? "OK添加  长按保存" : "OK添加  >完成");
}

static void render_clock(void) {
    lv_label_set_text(s_title, "设置时间");
    lv_label_set_text(s_body, s_app.pending_code);
    lv_label_set_text(s_detail, "输入另一设备验证码");
    lv_label_set_text(s_footer, "OK下一位  长按校验");
}

static void render_confirm(void) {
    lv_label_set_text(s_title, "确认删除");
    lv_label_set_text(s_body, s_app.store.count ? s_app.store.accounts[s_app.selected].name : "空列表");
    lv_label_set_text(s_detail, "删除后不可恢复");
    lv_label_set_text(s_footer, "OK确认  长按取消");
}

static void render_error(void) {
    lv_label_set_text(s_title, "无法继续");
    lv_label_set_text(s_body, s_app.status[0] ? s_app.status : "错误");
    lv_label_set_text(s_detail, "长按返回");
    lv_label_set_text(s_footer, "");
}

static void render(void) {
    if (!s_screen) return;
    switch (s_app.screen) {
    case OTP_SCREEN_CODE: render_code(); break;
    case OTP_SCREEN_MENU: render_menu(); break;
    case OTP_SCREEN_NAME: render_editor(false); break;
    case OTP_SCREEN_SECRET: render_editor(true); break;
    case OTP_SCREEN_CLOCK: render_clock(); break;
    case OTP_SCREEN_CONFIRM: render_confirm(); break;
    case OTP_SCREEN_ERROR: render_error(); break;
    case OTP_SCREEN_LIST:
    default: render_list(); break;
    }
}

static void build_screen(void) {
    font_init_once();
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x07111F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    s_title = make_label(s_screen, 28, lv_color_hex(0x8FB7D9));
    s_body = make_label(s_screen, 112, lv_color_hex(0xF4F7FB));
    s_detail = make_label(s_screen, 176, lv_color_hex(0xF0B429));
    s_footer = make_label(s_screen, 252, lv_color_hex(0x7E8B9A));
    render();
    lv_screen_load(s_screen);
}

static void clear_draft(void) {
    memset(s_app.name, 0, sizeof(s_app.name));
    memset(s_app.secret, 0, sizeof(s_app.secret));
    memset(s_app.pending_code, '0', OTP_DIGITS);
    s_app.pending_code[OTP_DIGITS] = '\0';
    s_app.name_len = 0;
    s_app.secret_len = 0;
    s_app.cursor = 0;
}

static void open_error(const char *text) {
    s_app.return_screen = s_app.screen == OTP_SCREEN_ERROR ? OTP_SCREEN_LIST : s_app.screen;
    s_app.screen = OTP_SCREEN_ERROR;
    set_status(text);
}

static bool finish_account(void) {
    otp_account_t account;
    otp_status_t status = otp_account_set(&account, s_app.name, s_app.secret);
    if (status != OTP_OK) {
        open_error("格式错误");
        return false;
    }
    status = otp_store_add(&s_app.store, &account);
    memset(&account, 0, sizeof(account));
    if (status != OTP_OK) {
        open_error(status == OTP_ERR_FULL ? "已满" : "名称重复");
        return false;
    }
    if (otp_store_save(&s_app.store) != ESP_OK) {
        open_error("保存失败");
        return false;
    }
    s_app.selected = (uint8_t)(s_app.store.count - 1u);
    clear_draft();
    s_app.screen = OTP_SCREEN_LIST;
    return true;
}

static bool clock_candidate_matches(const otp_account_t *account, uint64_t unix_sec) {
    char code[OTP_DIGITS + 1u];
    return otp_totp(account->secret, account->secret_len, unix_sec, account->period,
                    account->digits, code, NULL) == OTP_OK &&
           otp_code_equal(code, s_app.pending_code);
}

static void search_progress(uint64_t done, uint64_t total) {
    lv_label_set_text_fmt(s_detail, "校验 %u%%",
                          total ? (unsigned)(done * 100u / total) : 100u);
    lv_refr_now(NULL);
}

static bool accept_clock(uint64_t unix_sec) {
    otp_store_set_clock(&s_app.store, unix_sec);
    if (otp_store_save(&s_app.store) != ESP_OK) {
        open_error("保存失败");
        return false;
    }
    s_app.screen = OTP_SCREEN_CODE;
    return true;
}

/* 校准:输入另一台可信设备当前的 6 位码,找到匹配的计数窗口即写入系统时间。
 * 已有时基时只搜 ±2 分钟窗口(覆盖输入耗时);从未校准(新机或断过电池)则从
 * 锚点时间起全量搜索——锚点=上次校准时间(NVS 持久)或固件构建时间,上界放宽
 * 到数年以覆盖搁置后才重新校准的设备。全量搜索在按键任务里同步执行,期间
 * 持有 LVGL 锁,进度用 lv_refr_now 直接刷新。 */
static bool finish_clock(void) {
    if (s_app.store.count == 0) {
        open_error("请先新增账号");
        return false;
    }
    const otp_account_t *account = &s_app.store.accounts[s_app.selected];

    if (s_app.store.clock_ready) {
        uint64_t base = otp_store_now(&s_app.store);
        uint64_t anchor = base - (base % OTP_PERIOD_SEC);
        for (int delta = -4; delta <= 4; ++delta) {
            uint64_t candidate = anchor + (uint64_t)((int64_t)delta * (int64_t)OTP_PERIOD_SEC);
            if (clock_candidate_matches(account, candidate)) return accept_clock(candidate);
        }
        open_error("校验失败");
        return false;
    }

    const uint64_t from = s_app.store.last_unix > 0u ? s_app.store.last_unix - 600u
                                                     : (uint64_t)OTP_BUILD_UNIX;
    const uint64_t span = s_app.store.last_unix > 0u ? 3ull * 365ull * 86400ull
                                                     : 365ull * 86400ull;
    const uint64_t steps = span / OTP_PERIOD_SEC;
    for (uint64_t step = 0u; step < steps; ++step) {
        if ((step & 0x3fffu) == 0u) search_progress(step, steps);
        uint64_t candidate = from + step * OTP_PERIOD_SEC;
        if (clock_candidate_matches(account, candidate)) return accept_clock(candidate);
    }
    open_error("校验失败");
    return false;
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
    otp_power_advance(&s_power, 250u);
    bsp_display_backlight(otp_power_backlight(&s_power));
    if (++s_soc_tick >= 20u) {   /* 每 5 秒刷新一次电量读数(走 I2C,不能每 tick 读) */
        s_soc_tick = 0u;
        battery_refresh();
    }
    if (s_app.screen == OTP_SCREEN_LIST || s_app.screen == OTP_SCREEN_CODE) render();
    if (otp_power_should_sleep(&s_power)) enter_sleep();
}

static void handle_navigation(int button, int event, uint8_t *index, uint8_t count) {
    if (event != BSP_BTN_CLICK || count == 0) return;
    if (button == BSP_BTN_UP) *index = (*index + count - 1u) % count;
    if (button == BSP_BTN_DOWN) *index = (*index + 1u) % count;
}

static void handle_key(int button, int event) {
    otp_power_activity(&s_power);
    bsp_display_backlight(OTP_BACKLIGHT_ACTIVE);
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK) {
        if (s_app.screen == OTP_SCREEN_SECRET) finish_account();
        else if (s_app.screen == OTP_SCREEN_CLOCK) finish_clock();
        else if (s_app.screen == OTP_SCREEN_ERROR) s_app.screen = s_app.return_screen;
        else s_app.screen = OTP_SCREEN_LIST;
        render();
        return;
    }
    if (event != BSP_BTN_CLICK) return;

    switch (s_app.screen) {
    case OTP_SCREEN_LIST:
        handle_navigation(button, event, &s_app.selected, s_app.store.count);
        if (button == BSP_BTN_OK) s_app.screen = s_app.store.count ? OTP_SCREEN_CODE : OTP_SCREEN_MENU;
        break;
    case OTP_SCREEN_CODE:
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) s_app.screen = OTP_SCREEN_MENU;
        break;
    case OTP_SCREEN_MENU:
        handle_navigation(button, event, &s_app.menu_item, OTP_MENU_COUNT);
        if (button == BSP_BTN_OK) {
            if (s_app.menu_item == OTP_MENU_ADD) {
                clear_draft();
                s_app.screen = OTP_SCREEN_NAME;
            } else if (s_app.menu_item == OTP_MENU_CLOCK) {
                clear_draft();
                s_app.screen = OTP_SCREEN_CLOCK;
            } else {
                s_app.screen = OTP_SCREEN_CONFIRM;
            }
        }
        break;
    case OTP_SCREEN_NAME:
    case OTP_SCREEN_SECRET: {
        bool secret = s_app.screen == OTP_SCREEN_SECRET;
        const char *alphabet = secret ? SECRET_CHARS : NAME_CHARS;
        uint8_t alphabet_len = (uint8_t)strlen(alphabet);
        handle_navigation(button, event, &s_app.cursor, alphabet_len);
        if (button == BSP_BTN_OK) {
            char *target = secret ? s_app.secret : s_app.name;
            uint8_t *len = secret ? &s_app.secret_len : &s_app.name_len;
            uint8_t limit = secret ? 52u : OTP_NAME_MAX;
            char chosen = alphabet[s_app.cursor];
            /* '>' 只结束名称，不写进账号名。名称页先截断再切到密钥页。 */
            if (!secret && chosen == '>') {
                if (*len == 0) open_error("名称不能空");
                else {
                    s_app.cursor = 0;
                    s_app.screen = OTP_SCREEN_SECRET;
                }
                break;
            }
            if (*len < limit) {
                target[(*len)++] = chosen;
                target[*len] = '\0';
            }
            if (!secret && *len == OTP_NAME_MAX) {
                s_app.cursor = 0;
                s_app.screen = OTP_SCREEN_SECRET;
            }
        }
        break;
    }
    case OTP_SCREEN_CLOCK:
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            char *digit = &s_app.pending_code[s_app.cursor];
            *digit = button == BSP_BTN_UP ? (*digit == '9' ? '0' : (char)(*digit + 1))
                                          : (*digit == '0' ? '9' : (char)(*digit - 1));
        } else if (button == BSP_BTN_OK) {
            s_app.cursor = (uint8_t)((s_app.cursor + 1u) % OTP_DIGITS);
        }
        break;
    case OTP_SCREEN_CONFIRM:
        if (button == BSP_BTN_OK && s_app.store.count) {
            (void)otp_store_remove(&s_app.store, s_app.selected);
            (void)otp_store_save(&s_app.store);
            if (s_app.selected >= s_app.store.count && s_app.store.count) s_app.selected = (uint8_t)(s_app.store.count - 1u);
            s_app.screen = OTP_SCREEN_LIST;
        }
        break;
    case OTP_SCREEN_ERROR:
        s_app.screen = s_app.return_screen;
        break;
    }
    render();
}

static void key_task(void *arg) {
    (void)arg;
    otp_key_t key;
    while (xQueueReceive(s_keys, &key, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(300)) continue;
        handle_key(key.button, key.event);
        bsp_lvgl_unlock();
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    /* main.c consumes the wake guard before any application action. */
    extern void on_key(int button, int event);
    on_key((int)button, (int)event);
}

void otp_button_event(int button, int event) {
    /* Queue every event. main.c owns the wake guard and may still need a
     * release that arrives before its input-ready flag is set. */
    otp_key_t key = {.button = (int)button, .event = (int)event};
    if (s_keys) (void)xQueueSend(s_keys, &key, 0);
}

esp_err_t otp_app_start(void) {
    memset(&s_app, 0, sizeof(s_app));
    otp_power_reset(&s_power);
    clear_draft();
    esp_err_t err = otp_store_load(&s_app.store);
    s_app.storage_ok = err == ESP_OK;
    if (err != ESP_OK) set_status("存储不可用");
    s_app.screen = OTP_SCREEN_LIST;

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) return ESP_FAIL;
    bsp_display_backlight(OTP_BACKLIGHT_ACTIVE);
    s_app.battery_ok = bsp_battery_init() == ESP_OK;
    battery_refresh();
    (void)bsp_audio_init();
    (void)bsp_audio_sleep();

    s_keys = xQueueCreate(8, sizeof(otp_key_t));
    if (!s_keys || xTaskCreate(key_task, "otp_keys", 4096, NULL, 5, &s_key_task) != pdPASS) return ESP_ERR_NO_MEM;
    s_app.buttons_ok = bsp_button_init(button_callback, NULL) == ESP_OK;
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    build_screen();
    s_timer = lv_timer_create(tick, 250, NULL);
    bsp_lvgl_unlock();
    s_ready = true;
    ESP_LOGI(TAG, "离线验证码就绪 accounts=%u clock=%d", s_app.store.count, s_app.store.clock_ready);
    return ESP_OK;
}

void otp_app_stop(void) {
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

uint64_t otp_app_now(const otp_app_t *app) {
    return app ? otp_store_now(&app->store) : 0u;
}

const char *otp_app_status(const otp_app_t *app) {
    return app ? app->status : "";
}
