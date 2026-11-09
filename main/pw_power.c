// main/pw_power.c - see pw_power.h.
#include "pw_power.h"

void pw_power_reset(pw_power_t *power) {
    if (!power) return;
    power->idle_ms = 0u;
    power->sleeping = false;
}

void pw_power_activity(pw_power_t *power) {
    pw_power_reset(power);
}

void pw_power_advance(pw_power_t *power, uint32_t elapsed_ms) {
    if (!power || power->sleeping) return;
    uint32_t next = power->idle_ms + elapsed_ms;
    power->idle_ms = next < power->idle_ms ? UINT32_MAX : next;
    if (power->idle_ms >= PW_SLEEP_AFTER_MS) power->sleeping = true;
}

uint8_t pw_power_backlight(const pw_power_t *power) {
    if (!power || power->sleeping) return 0u;
    return power->idle_ms >= PW_DIM_AFTER_MS ? PW_BACKLIGHT_DIM
                                             : PW_BACKLIGHT_ACTIVE;
}

bool pw_power_should_sleep(const pw_power_t *power) {
    return power && power->sleeping;
}
