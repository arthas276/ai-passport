#pragma once

#include "otp_core.h"

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* Durable account vault and calibration state. The time base is the RTC-backed
 * system clock (survives deep sleep and reboots; lost only on battery removal);
 * last_unix is the persisted anchor for full-range recalibration. NVS is
 * ordinary Flash, not a secure element; records are recoverable by anyone with
 * USB access. */

#define OTP_STORE_MAX 8u
#define OTP_STORE_NAMESPACE "otp_v1"

typedef struct {
    uint32_t version;
    uint64_t last_unix;
    bool clock_ready;
    otp_account_t accounts[OTP_STORE_MAX];
    uint8_t count;
} otp_store_t;

esp_err_t otp_store_load(otp_store_t *store);
esp_err_t otp_store_save(const otp_store_t *store);
esp_err_t otp_store_erase(void);

otp_status_t otp_store_add(otp_store_t *store, const otp_account_t *account);
otp_status_t otp_store_remove(otp_store_t *store, uint8_t index);
void otp_store_set_clock(otp_store_t *store, uint64_t unix_sec);
uint64_t otp_store_now(const otp_store_t *store);
