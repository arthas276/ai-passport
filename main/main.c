// main.c - EAR BUDDY entry: BSP init -> UI boot -> input/audio dispatch.
//
// Button semantics (app-wide):
//   UP/DOWN click   navigate / adjust
//   OK click        confirm / start
//   OK long         back / quit to HOME
//   UP long         replay audio (quiz)
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "ear_ui.h"
#include "ear_audio.h"
#include "key_guard.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

// One app event = a key press or an audio worker event. The input task owns
// all UI mutation; audio events arrive on their own queue and are unwrapped
// here, so ear_ui handlers always run from one task (under bsp_lvgl_lock).
typedef struct {
    bool is_key;
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
    ear_audio_event_t audio;
} app_event_t;

static QueueHandle_t s_key_q;
static QueueHandle_t s_audio_q;
static volatile bool s_input_ready;

// Waking from deep sleep by key reboots the chip while that key is still held:
// the button component starts a few hundred milliseconds later, sees a level
// that is already down, and grades it a long press once it passes its
// threshold (500 ms on this board) - an unintended action fires almost
// immediately. Drop key actions until that key is released; the deadline is
// only a safety net so a stuck key cannot leave the pad dead. The state
// machine lives in key_guard.h so it can be unit-tested without the app.
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void input_task(void *arg) {
    (void)arg;
    app_event_t e;
    for (;;) {
        if (xQueueReceive(s_key_q, &e, portMAX_DELAY) != pdTRUE) continue;
        if (!e.is_key) {
            // Audio event forwarded onto the key queue by the bridge task.
            if (bsp_lvgl_lock(500)) {
                ear_ui_handle_audio(&e.audio);
                bsp_lvgl_unlock();
            }
        } else {
            if (bsp_lvgl_lock(500)) {
                ear_ui_handle_key(e.btn, e.ev);
                bsp_lvgl_unlock();
            }
        }
    }
}

// Pump audio worker events into the app queue (is_key=false wrapper).
static void audio_bridge_task(void *arg) {
    (void)arg;
    ear_audio_event_t ev;
    for (;;) {
        if (xQueueReceive(s_audio_q, &ev, portMAX_DELAY) == pdTRUE) {
            app_event_t e = { .is_key = false };
            e.audio = ev;
            xQueueSend(s_key_q, &e, pdMS_TO_TICKS(100));
        }
    }
}

// Button ISR-context callback: enqueue only, never block (BSP rule).
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check: a release can arrive while
    // the UI is still booting, and dropping it here would leave the guard armed
    // to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "wake key released, keys active again");
        }
        return;
    }
    if (!s_input_ready) return;
    app_event_t e = { .is_key = true, .btn = btn, .ev = ev };
    (void)xQueueSend(s_key_q, &e, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "EAR BUDDY boot");

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "wake causes: 0x%02x", (unsigned)wake_causes);
    }
    // A key wake leaves that key held through the whole boot (see on_key).
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "key wake: ignoring key actions until released");
    }

    // NVS first: progress store depends on it. Recover from a dirty sector.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    bsp_i2c_init();

    // Battery gauge (CW2017, I2C 0x63) is optional hardware: on init failure
    // bsp_battery_soc() keeps returning -1 and HOME shows "--%" (graceful).
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge init failed (HOME will show --%%)");
    } else {
        // Gauge diagnostics (v1.1): the CW2017 profile is provisional, so log
        // SOC beside cell voltage for serial_peek captures - divergence tells
        // us the profile needs recalibration against the real cell.
        ESP_LOGI(TAG, "battery: soc=%d%% vcell=%dmV",
                 bsp_battery_soc(), bsp_battery_mv());
    }

    // Display is mandatory: without it there is no app. Fail loudly.
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC);
        return;
    }
    bsp_display_backlight(100);

    s_key_q = xQueueCreate(8, sizeof(app_event_t));
    s_audio_q = xQueueCreate(8, sizeof(ear_audio_event_t));
    if (!s_key_q || !s_audio_q) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    // Single UI-mutation task: keys and audio events serialize here.
    if (xTaskCreate(input_task, "ear_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task create failed");
        return;
    }
    if (xTaskCreate(audio_bridge_task, "ear_bridge", 2048, NULL, 4, NULL)
        != pdPASS) {
        ESP_LOGE(TAG, "bridge task create failed");
        return;
    }

    if (ear_audio_init(s_audio_q) != ESP_OK) {
        ESP_LOGE(TAG, "audio worker init failed (quiz audio unavailable)");
        // Keep running: UI works, quiz pages will silently miss audio.
    }

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;   // three-key device without keys is unusable
    } else if (s_key_guard.armed) {
        // If the wake key was released before the button driver started
        // observing, the driver never emits a release event and the guard would
        // block until its deadline. Sync once against the sampled level: an
        // already released key disarms immediately.
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
    }

    if (bsp_lvgl_lock(1000)) {
        ear_ui_init(s_key_q);
        bsp_lvgl_unlock();
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "EAR BUDDY ready");
}
