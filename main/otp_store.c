#include "otp_store.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <sys/time.h>

#include <string.h>

#define STORE_VERSION 1u
#define STORE_KEY "vault"

typedef struct {
    uint32_t version;
    uint64_t last_unix;
    uint8_t clock_ready;
    uint8_t count;
    otp_account_t accounts[OTP_STORE_MAX];
} otp_store_blob_t;

static bool blob_sane(const otp_store_blob_t *blob) {
    if (blob->version != STORE_VERSION || blob->count > OTP_STORE_MAX) return false;
    for (uint8_t i = 0; i < blob->count; ++i) {
        const otp_account_t *account = &blob->accounts[i];
        if (!otp_account_used(account) || account->digits != OTP_DIGITS ||
            account->period != OTP_PERIOD_SEC) return false;
        if (account->secret_len < OTP_SECRET_MIN || account->secret_len > OTP_SECRET_MAX) return false;
        if (memchr(account->name, '\0', OTP_NAME_MAX + 1u) == NULL) return false;
    }
    return true;
}

esp_err_t otp_store_load(otp_store_t *store) {
    if (!store) return ESP_ERR_INVALID_ARG;
    memset(store, 0, sizeof(*store));
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return err;
    }
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    err = nvs_open(OTP_STORE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    otp_store_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, STORE_KEY, &blob, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || !blob_sane(&blob)) return ESP_ERR_INVALID_VERSION;

    store->version = blob.version;
    store->last_unix = blob.last_unix;
    store->clock_ready = blob.clock_ready != 0;
    store->count = blob.count;
    memcpy(store->accounts, blob.accounts, sizeof(store->accounts));

    /* RTC 掉电检测:系统时间比上次校准还早一小时以上,说明时间已丢。 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (store->clock_ready && (uint64_t)tv.tv_sec + 3600ull < store->last_unix) {
        store->clock_ready = false;
    }
    return ESP_OK;
}

esp_err_t otp_store_save(const otp_store_t *store) {
    if (!store || store->count > OTP_STORE_MAX) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTP_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    otp_store_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = STORE_VERSION;
    blob.last_unix = store->last_unix;
    blob.clock_ready = store->clock_ready ? 1u : 0u;
    blob.count = store->count;
    memcpy(blob.accounts, store->accounts, sizeof(blob.accounts));
    err = nvs_set_blob(handle, STORE_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    memset(&blob, 0, sizeof(blob));
    return err;
}

esp_err_t otp_store_erase(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTP_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

otp_status_t otp_store_add(otp_store_t *store, const otp_account_t *account) {
    if (!store || !otp_account_used(account)) return OTP_ERR_FORMAT;
    if (store->count >= OTP_STORE_MAX) return OTP_ERR_FULL;
    for (uint8_t i = 0; i < store->count; ++i) {
        if (strncmp(store->accounts[i].name, account->name, OTP_NAME_MAX) == 0) return OTP_ERR_FORMAT;
    }
    store->accounts[store->count++] = *account;
    return OTP_OK;
}

otp_status_t otp_store_remove(otp_store_t *store, uint8_t index) {
    if (!store || index >= store->count) return OTP_ERR_RANGE;
    memmove(&store->accounts[index], &store->accounts[index + 1u],
            (store->count - index - 1u) * sizeof(store->accounts[0]));
    memset(&store->accounts[store->count - 1u], 0, sizeof(store->accounts[0]));
    --store->count;
    return OTP_OK;
}

void otp_store_set_clock(otp_store_t *store, uint64_t unix_sec) {
    if (!store) return;
    struct timeval tv = { .tv_sec = (time_t)unix_sec, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) return;
    store->last_unix = unix_sec;
    store->clock_ready = true;
}

uint64_t otp_store_now(const otp_store_t *store) {
    if (!store || !store->clock_ready) return 0u;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec > 0 ? (uint64_t)tv.tv_sec : 0u;
}
