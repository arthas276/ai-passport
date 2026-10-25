// main/main.c — CHEER 夸夸精灵:初始化 + 按键分发 + 抽卡循环。
//
// 按键语义(全局统一):
//   OK 单击     抽一张夸夸卡(+风铃;跨阶段加庆祝和弦)
//   UP/DOWN 单击 翻看抽取历史(最新在前,不发声不改计数)
#include "cheer.h"
#include "cheer_audio.h"
#include "cheer_cards.h"
#include "cheer_store.h"
#include "cheer_ui.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "cheer";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

static cheer_save_t s_save;
static cheer_bag_t s_bag;
static int16_t s_hist[CHEER_HISTORY_CAP];
static int s_hist_len;

// 深睡按键唤醒防误触（仓库契约 test_key_wake_contract）：唤醒键在整个启动期
// 间还按着，先挡住它的动作直到松手；时限仅作兜底，防粘键把整机挂死。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void hist_push(int idx) {
    if (s_hist_len < CHEER_HISTORY_CAP) {
        s_hist[s_hist_len++] = (int16_t)idx;
    } else {
        memmove(s_hist, s_hist + 1, sizeof(s_hist) - sizeof(s_hist[0]));
        s_hist[CHEER_HISTORY_CAP - 1] = (int16_t)idx;
    }
}

static void do_draw(void) {
    int idx = cheer_bag_draw(&s_bag);
    if (idx < 0) return;
    uint32_t before = s_save.praise;
    s_save.praise++;
    bool stage_up = cheer_stage_of(s_save.praise) > cheer_stage_of(before);
    hist_push(idx);
    cheer_store_save(&s_save);
    cheer_audio_play_chime(stage_up);
    cheer_ui_show_card(idx, 0, s_hist_len);
    cheer_ui_set_progress(s_save.praise);
    cheer_ui_set_battery(bsp_battery_soc());
    ESP_LOGI(TAG, "praise %lu (card %d%s)", (unsigned long)s_save.praise,
             idx, stage_up ? ", stage up" : "");
}

static void do_browse(int dir) {
    static int view = 0;
    if (s_hist_len == 0) return;
    int v = view + dir;
    if (v < 0) v = 0;
    if (v > s_hist_len - 1) v = s_hist_len - 1;
    view = v;
    cheer_ui_show_card(s_hist[s_hist_len - 1 - v], v, s_hist_len);
}

static void process_input(const input_event_t *input) {
    if (input->event != BSP_BTN_CLICK) return;
    if (!bsp_lvgl_lock(200)) return;
    cheer_ui_note_activity();
    if (input->btn == BSP_BTN_OK) {
        do_draw();
    } else if (input->btn == BSP_BTN_UP) {
        do_browse(1);
    } else if (input->btn == BSP_BTN_DOWN) {
        do_browse(-1);
    }
    bsp_lvgl_unlock();
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 先消费守卫再查就绪：release 可能晚于初始化后的电压采样到达，若被就绪
    // 早退吞掉，守卫会一直武装并吞掉下一次真实按键。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手，按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "CHEER 夸夸精灵 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒：先屏蔽按键动作，直到那颗键松手");
    }

    bsp_i2c_init();
    // NVS 必须显式初始化(BSP 无自动初始化),否则 cheer_store 全部写入失败。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC,
                 BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    const bool audio_ok = bsp_audio_init() == ESP_OK;
    const bool battery_ok = bsp_battery_init() == ESP_OK;

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue ||
        xTaskCreate(input_task, "cheer_input", 4096, NULL, 5,
                    &s_input_task) != pdPASS ||
        bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键/输入任务初始化失败");
        return;
    }
    if (s_key_guard.armed) {
        // 唤醒键若在按键驱动启动前就已松手，不会有 release 事件；按真实电压
        // 同步一次，已松手立即撤防。
        const bool released =
            bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
    }

    cheer_store_load(&s_save);
    cheer_bag_init(&s_bag, esp_random(), (uint8_t)CHEER_CARD_COUNT);

    if (audio_ok) {
        cheer_audio_init();
    }

    if (bsp_lvgl_lock(1000)) {
        cheer_ui_build();
        cheer_ui_set_progress(s_save.praise);
        cheer_ui_set_battery(battery_ok ? bsp_battery_soc() : -1);
        bsp_lvgl_unlock();
        s_input_ready = true;
    }

    ESP_LOGI(TAG, "CHEER ready (praise %lu, audio %d)",
             (unsigned long)s_save.praise, audio_ok);
}
