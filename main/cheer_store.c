// cheer_store.c - NVS persistence for CHEER (single u32, key "cc").
#include "cheer_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "cheer_store";

void cheer_store_load(cheer_save_t *out) {
    out->praise = 0;
    nvs_handle_t h;
    if (nvs_open("cheer", NVS_READONLY, &h) != ESP_OK) {
        return;   // fresh spirit
    }
    uint32_t v = 0;
    if (nvs_get_u32(h, "cc", &v) == ESP_OK) {
        out->praise = v;
    }
    nvs_close(h);
}

void cheer_store_save(const cheer_save_t *s) {
    nvs_handle_t h;
    if (nvs_open("cheer", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed; praise count not persisted");
        return;
    }
    if (nvs_set_u32(h, "cc", s->praise) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs write failed");
    }
    nvs_close(h);
}
