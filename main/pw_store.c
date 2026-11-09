// main/pw_store.c - see pw_store.h.
#include "pw_store.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#define NS "pwcard_v1"
#define KEY "state"

static void defaults(pw_store_t *out) {
    memset(out, 0, sizeof(*out));
    out->version = PW_STORE_VERSION;
}

esp_err_t pw_store_load(pw_store_t *out) {
    defaults(out);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    pw_store_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (len != sizeof(blob) || blob.version != PW_STORE_VERSION) {
        return ESP_ERR_INVALID_STATE;
    }
    if (blob.site_count > PW_SITES_MAX) return ESP_ERR_INVALID_STATE;
    for (uint32_t i = 0; i < blob.site_count; i++) {
        if (!pw_site_valid(&blob.sites[i])) return ESP_ERR_INVALID_STATE;
    }
    *out = blob;
    return ESP_OK;
}

esp_err_t pw_store_save(const pw_store_t *store) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, KEY, store, sizeof(*store));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
