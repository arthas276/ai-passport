// main/main.c — almanac start-up: wake guard, key forwarding and app start (the family's entry
// structure: otp, idiom and pwcard use the same shape).
#include <stdbool.h>

#include "almanac_app.h"

#include "bsp_button.h"
#include "bsp_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "key_guard.h"

static const char *TAG = "main";

// Swallows key actions until the deep-sleep wake key is released (key_guard.h).
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;
static volatile bool s_input_ready;

void on_key(int button, int event) {
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key released, keys are active again");
        }
        return;
    }
    if (!s_input_ready) return;
    almanac_app_button_event(button, event);
}

void app_main(void) {
    ESP_LOGI(TAG, "almanac v1.0 starting");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "woke by key: key actions stay guarded until it is released");
    }

    const esp_err_t err = almanac_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "almanac_app_start failed: %s, restarting to retry", esp_err_to_name(err));
        esp_restart();
    }

    if (s_key_guard.armed) {
        // The wake key may have been released before the button driver started: sync once.
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
    }
    s_input_ready = true;
}
