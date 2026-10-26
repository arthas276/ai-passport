// main/main.c —— OTP 离线验证码:初始化 + 唤醒守卫 + 按键转发。
//
// 应用本体(otp_app.c)自带按键队列与 LVGL 定时器;本文件只负责深睡按键唤醒的
// 防误触守卫(见 key_guard.h),并把按键事件转发给应用。
#include "otp_app.h"

#include "bsp_button.h"
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
    // 先消费守卫再转发:release 可能晚于初始化后的电压采样到达,若被早退吞掉,
    // 守卫会一直武装并吞掉下一次真实按键。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    otp_button_event(button, event);
}

void app_main(void) {
    ESP_LOGI(TAG, "OTP 离线验证码 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // otp_app_start() 自带显示/LVGL/电量/音频/按键初始化(bsp_i2c_init 幂等,
    // 由各驱动自行索取总线)。
    const esp_err_t err = otp_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "otp_app_start 失败: %s,重启重试", esp_err_to_name(err));
        esp_restart();
    }

    if (s_key_guard.armed) {
        // 唤醒键可能在按键驱动启动前就已松手:按真实电压同步一次,已松手立即撤防。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }
}
