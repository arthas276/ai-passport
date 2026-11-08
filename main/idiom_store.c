// main/idiom_store.c - see idiom_store.h.
#include "idiom_store.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#define NS "idiom_v1"
#define KEY "state"

static void defaults(idiom_store_t *out) {
    memset(out, 0, sizeof(*out));
    out->version = IDIOM_STORE_VERSION;
    out->order_seed = 0x20261105u;
}

esp_err_t idiom_store_load(idiom_store_t *out) {
    defaults(out);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    idiom_store_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (len != sizeof(blob) || blob.version != IDIOM_STORE_VERSION) return ESP_ERR_INVALID_STATE;
    *out = blob;
    return ESP_OK;
}

esp_err_t idiom_store_save(const idiom_store_t *store) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, KEY, store, sizeof(*store));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
