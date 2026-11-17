// cheer_store.c - NVS persistence for CHEER (v1.3: praise "cc", scene "sc",
// favorite blob "fv", favorite-milestone high-water "fm"). Missing keys
// (pre-v1.3 devices) fall back to defaults; the praise count always survives.
#include "cheer_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "cheer_store";

void cheer_store_load(cheer_save_t *out) {
    out->praise = 0;
    out->scene = 0;
    out->fav_hw = 0;
    out->fav.count = 0;
    nvs_handle_t h;
    if (nvs_open("cheer", NVS_READONLY, &h) != ESP_OK) {
        return;   // fresh spirit
    }
    uint32_t v = 0;
    if (nvs_get_u32(h, "cc", &v) == ESP_OK) {
        out->praise = v;
    }
    uint8_t u8 = 0;
    if (nvs_get_u8(h, "sc", &u8) == ESP_OK) {
        out->scene = u8;
    }
    if (nvs_get_u32(h, "fm", &v) == ESP_OK) {
        out->fav_hw = v;
    }
    uint8_t blob[CHEER_FAV_CAP * 2];
    size_t len = sizeof(blob);
    if (nvs_get_blob(h, "fv", blob, &len) == ESP_OK) {
        if (!cheer_fav_deserialize(&out->fav, blob, len)) {
            ESP_LOGW(TAG, "favorite blob malformed (len %u); starting empty",
                     (unsigned)len);
        }
    }
    nvs_close(h);
}

void cheer_store_save(const cheer_save_t *s) {
    nvs_handle_t h;
    if (nvs_open("cheer", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed; progress not persisted");
        return;
    }
    uint8_t blob[CHEER_FAV_CAP * 2];
    size_t flen = cheer_fav_serialize(&s->fav, blob, sizeof(blob));
    if (nvs_set_u32(h, "cc", s->praise) != ESP_OK ||
        nvs_set_u8(h, "sc", s->scene) != ESP_OK ||
        nvs_set_u32(h, "fm", s->fav_hw) != ESP_OK ||
        nvs_set_blob(h, "fv", blob, flen) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs write failed");
    }
    nvs_close(h);
}
