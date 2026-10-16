// main.c - BREATHE entry: BSP init -> UI boot -> input/audio dispatch.
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "breath_ui.h"
#include "breath_audio.h"
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

// One app event = a key press, an audio worker event, or the 1 Hz tick.
// The input task owns all UI mutation under bsp_lvgl_lock; nothing else
// touches LVGL objects.
typedef enum { APP_EV_KEY = 0, APP_EV_AUDIO, APP_EV_SEC } app_ev_kind_t;

typedef struct {
    app_ev_kind_t kind;
    bsp_btn_t btn;               // APP_EV_KEY
    bsp_btn_ev_t ev;             // APP_EV_KEY
    breath_audio_event_t audio;  // APP_EV_AUDIO
} app_event_t;

static QueueHandle_t s_app_q;
static QueueHandle_t s_audio_q;
static volatile bool s_input_ready;

// Deep-sleep key-wake guard (see key_guard.h; contract-tested upstream).
// BREATHE never enters deep sleep in v1, but the wiring is mandatory so a
// held wake key can never leak a phantom long-press into the app if a
// future version adds sleep.
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void input_task(void *arg) {
    (void)arg;
    app_event_t e;
    for (;;) {
        if (xQueueReceive(s_app_q, &e, portMAX_DELAY) != pdTRUE) continue;
        if (!bsp_lvgl_lock(500)) continue;
        switch (e.kind) {
        case APP_EV_KEY:   breath_ui_handle_key(e.btn, e.ev); break;
        case APP_EV_AUDIO: breath_ui_handle_audio(&e.audio); break;
        case APP_EV_SEC:   breath_ui_on_second(); break;
        default: break;
        }
        bsp_lvgl_unlock();
    }
}

// Pump audio worker events into the app queue.
static void audio_bridge_task(void *arg) {
    (void)arg;
    breath_audio_event_t ev;
    for (;;) {
        if (xQueueReceive(s_audio_q, &ev, portMAX_DELAY) == pdTRUE) {
            app_event_t e = { .kind = APP_EV_AUDIO, .audio = ev };
            xQueueSend(s_app_q, &e, pdMS_TO_TICKS(100));
        }
    }
}

// 1 Hz housekeeping tick (battery refresh + idle dim) via the app queue,
// so the callback itself only enqueues (esp_timer task, no LVGL access).
static void sec_timer_cb(void *arg) {
    (void)arg;
    app_event_t e = { .kind = APP_EV_SEC };
    (void)xQueueSend(s_app_q, &e, 0);
}

// Button callbacks run on the shared esp_timer task; enqueue only.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check: a release can arrive
    // after the post-init voltage sample but before input dispatch is
    // ready, and dropping it here would leave the guard armed to swallow
    // the next real press.
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        return;
    }
    if (!s_input_ready || !s_app_q) return;
    app_event_t e = { .kind = APP_EV_KEY, .btn = btn, .ev = ev };
    (void)xQueueSend(s_app_q, &e, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "BREATHE boot");

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "key wake: swallowing key actions until release");
    }

    bsp_i2c_init();

    // Battery gauge (CW2017, I2C 0x63) is optional hardware: on init
    // failure bsp_battery_soc() keeps returning -1 and the header shows
    // "--%".
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "battery gauge init failed (header will show --%%)");
    } else {
        ESP_LOGI(TAG, "battery: soc=%d%% vcell=%dmV",
                 bsp_battery_soc(), bsp_battery_mv());
    }

    // Stats/volume persistence; store calls degrade gracefully on failure.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: stats/volume won't persist");
    }

    // Display is mandatory: without it there is no app. Fail loudly.
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC);
        return;
    }
    bsp_display_backlight(100);

    s_app_q = xQueueCreate(8, sizeof(app_event_t));
    s_audio_q = xQueueCreate(8, sizeof(breath_audio_event_t));
    if (!s_app_q || !s_audio_q) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    // Single UI-mutation task: keys, audio events and the second tick all
    // serialize here.
    if (xTaskCreate(input_task, "breath_input", 4096, NULL, 5, NULL) != pdPASS
        ||
        xTaskCreate(audio_bridge_task, "breath_bridge", 2048, NULL, 4, NULL)
            != pdPASS) {
        ESP_LOGE(TAG, "dispatch task create failed");
        return;
    }

    if (breath_audio_init(s_audio_q) != ESP_OK) {
        ESP_LOGE(TAG, "audio worker init failed (sessions unavailable)");
        return;
    }

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;   // three-key device without keys is unusable
    }
    if (s_key_guard.armed) {
        // The wake key may have been released before the button driver
        // came up, in which case no release event will ever arrive: sync
        // once on the sampled physical level.
        key_guard_sync_released(
            &s_key_guard,
            bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }

    esp_timer_create_args_t sec_args = {
        .callback = sec_timer_cb, .name = "breath_sec",
    };
    esp_timer_handle_t sec_timer;
    if (esp_timer_create(&sec_args, &sec_timer) == ESP_OK) {
        esp_timer_start_periodic(sec_timer, 1000000);
    }

    if (bsp_lvgl_lock(1000)) {
        breath_ui_init();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }
    ESP_LOGI(TAG, "BREATHE ready");
}
