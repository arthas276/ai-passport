#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Idle-power policy independent of ESP-IDF. The device is a small wearable,
 * so a valid code must not keep the backlight on indefinitely. */

#define OTP_BACKLIGHT_ACTIVE 55u
#define OTP_BACKLIGHT_DIM 8u
#define OTP_DIM_AFTER_MS 20000u
#define OTP_SLEEP_AFTER_MS 60000u

typedef struct {
    uint32_t idle_ms;
    bool sleeping;
} otp_power_t;

void otp_power_reset(otp_power_t *power);
void otp_power_activity(otp_power_t *power);
void otp_power_advance(otp_power_t *power, uint32_t elapsed_ms);
uint8_t otp_power_backlight(const otp_power_t *power);
bool otp_power_should_sleep(const otp_power_t *power);
