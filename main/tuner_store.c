// tuner_store.c - NVS load/save of the four tuner settings scalars.
// Keys are stored one by one (family ear_store pattern): a device flashing
// from an older version simply misses keys and takes their defaults.
#include "tuner_store.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS   "tuner"
#define K_MD "md"
#define K_VL "vl"
#define K_A4 "a4"
#define K_TP "tp"

static esp_err_t ensure_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    return err;
}

esp_err_t tuner_store_load(tuner_settings_t *out) {
    tuner_settings_default(out);
    esp_err_t err = ensure_nvs();
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;   // first boot: defaults stand
    uint8_t u8;
    uint16_t u16;
    if (nvs_get_u8(h, K_MD, &u8) == ESP_OK) out->mode = u8;
    if (nvs_get_u8(h, K_VL, &u8) == ESP_OK) out->vol = u8;
    if (nvs_get_u8(h, K_TP, &u8) == ESP_OK) out->tp = u8;
    if (nvs_get_u16(h, K_A4, &u16) == ESP_OK) out->a4 = u16;
    nvs_close(h);
    tuner_settings_sanitize(out);
    return ESP_OK;
}

esp_err_t tuner_store_save(const tuner_settings_t *s) {
    esp_err_t err = ensure_nvs();
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, K_MD, s->mode);
    if (err == ESP_OK) err = nvs_set_u8(h, K_VL, s->vol);
    if (err == ESP_OK) err = nvs_set_u16(h, K_A4, s->a4);
    if (err == ESP_OK) err = nvs_set_u8(h, K_TP, s->tp);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
