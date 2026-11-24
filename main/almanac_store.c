// main/almanac_store.c — the day the user last confirmed, kept in NVS.
#include "almanac_store.h"

#include <nvs.h>
#include <nvs_flash.h>

#include "esp_log.h"

static const char *TAG = "almanac_store";

#define STORAGE_NAMESPACE "almanac"
#define KEY_DAY "day"

esp_err_t almanac_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs an erase: %s", esp_err_to_name(err));
        const esp_err_t erased = nvs_flash_erase();
        if (erased != ESP_OK) return erased;
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t almanac_store_load(int *day, bool *found) {
    *found = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    int32_t value = 0;
    err = nvs_get_i32(handle, KEY_DAY, &value);
    nvs_close(handle);
    if (err == ESP_OK) {
        *day = (int)value;
        *found = true;
    }
    return err;
}

esp_err_t almanac_store_save(int day) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(handle, KEY_DAY, (int32_t)day);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
