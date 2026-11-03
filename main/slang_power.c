#include "slang_power.h"

void slang_power_reset(slang_power_t *power) {
    if (!power) return;
    power->idle_ms = 0u;
    power->sleeping = false;
}

void slang_power_activity(slang_power_t *power) {
    slang_power_reset(power);
}

void slang_power_advance(slang_power_t *power, uint32_t elapsed_ms) {
    if (!power || power->sleeping) return;
    uint32_t next = power->idle_ms + elapsed_ms;
    power->idle_ms = next < power->idle_ms ? UINT32_MAX : next;
    if (power->idle_ms >= SLANG_SLEEP_AFTER_MS) power->sleeping = true;
}

uint8_t slang_power_backlight(const slang_power_t *power) {
    if (!power || power->sleeping) return 0u;
    return power->idle_ms >= SLANG_DIM_AFTER_MS ? SLANG_BACKLIGHT_DIM : SLANG_BACKLIGHT_ACTIVE;
}

bool slang_power_should_sleep(const slang_power_t *power) {
    return power && power->sleeping;
}
