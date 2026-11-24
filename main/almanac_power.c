// main/almanac_power.c — idle-power policy, pure C, host-testable (the family's power contract).
#include "almanac_power.h"

void almanac_power_reset(almanac_power_t *power) {
    if (!power) return;
    power->idle_ms = 0u;
    power->sleeping = false;
}

void almanac_power_activity(almanac_power_t *power) {
    almanac_power_reset(power);
}

void almanac_power_advance(almanac_power_t *power, uint32_t elapsed_ms) {
    if (!power || power->sleeping) return;
    const uint32_t next = power->idle_ms + elapsed_ms;
    power->idle_ms = next < power->idle_ms ? UINT32_MAX : next;
    if (power->idle_ms >= ALMANAC_SLEEP_AFTER_MS) power->sleeping = true;
}

uint8_t almanac_power_backlight(const almanac_power_t *power) {
    if (!power || power->sleeping) return 0u;
    return power->idle_ms >= ALMANAC_DIM_AFTER_MS ? ALMANAC_BACKLIGHT_DIM : ALMANAC_BACKLIGHT_ACTIVE;
}

bool almanac_power_should_sleep(const almanac_power_t *power) {
    return power && power->sleeping;
}
