// main/main.c —— SLANG 离线梗词典:初始化 + 唤醒守卫 + 按键转发。
//
// 显示与 LVGL 由本文件初始化(slang_app.h 契约:调用方先完成显示初始化);
// 应用本体自带按键队列与 LVGL 定时器,本文件只负责深睡按键唤醒的防误触守卫。
#include "slang_app.h"

#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "key_guard.h"

static const char *TAG = "main";

// 深睡按键唤醒防误触(仓库契约 test_key_guard / key_guard.h)。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

void on_key(int button, int event) {
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    slang_button_event(button, event);
}

void app_main(void) {
    ESP_LOGI(TAG, "SLANG 离线梗词典 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,重启重试");
        esp_restart();
    }
    bsp_display_backlight(100);

    const esp_err_t err = slang_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "slang_app_start 失败: %s,重启重试", esp_err_to_name(err));
        esp_restart();
    }

    if (s_key_guard.armed) {
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }
}
