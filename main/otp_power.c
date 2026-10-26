#include "otp_power.h"

void otp_power_reset(otp_power_t *power) {
    if (!power) return;
    power->idle_ms = 0u;
    power->sleeping = false;
}

void otp_power_activity(otp_power_t *power) {
    otp_power_reset(power);
}

void otp_power_advance(otp_power_t *power, uint32_t elapsed_ms) {
    if (!power || power->sleeping) return;
    uint32_t next = power->idle_ms + elapsed_ms;
    power->idle_ms = next < power->idle_ms ? UINT32_MAX : next;
    if (power->idle_ms >= OTP_SLEEP_AFTER_MS) power->sleeping = true;
}

uint8_t otp_power_backlight(const otp_power_t *power) {
    if (!power || power->sleeping) return 0u;
    return power->idle_ms >= OTP_DIM_AFTER_MS ? OTP_BACKLIGHT_DIM : OTP_BACKLIGHT_ACTIVE;
}

bool otp_power_should_sleep(const otp_power_t *power) {
    return power && power->sleeping;
}
