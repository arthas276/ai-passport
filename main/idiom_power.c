// main/idiom_power.c - see idiom_power.h.
#include "idiom_power.h"

void idiom_power_reset(idiom_power_t *power) {
    if (!power) return;
    power->idle_ms = 0u;
    power->sleeping = false;
}

void idiom_power_activity(idiom_power_t *power) {
    idiom_power_reset(power);
}

void idiom_power_advance(idiom_power_t *power, uint32_t elapsed_ms) {
    if (!power || power->sleeping) return;
    uint32_t next = power->idle_ms + elapsed_ms;
    power->idle_ms = next < power->idle_ms ? UINT32_MAX : next;
    if (power->idle_ms >= IDIOM_SLEEP_AFTER_MS) power->sleeping = true;
}

uint8_t idiom_power_backlight(const idiom_power_t *power) {
    if (!power || power->sleeping) return 0u;
    return power->idle_ms >= IDIOM_DIM_AFTER_MS ? IDIOM_BACKLIGHT_DIM
                                                : IDIOM_BACKLIGHT_ACTIVE;
}

bool idiom_power_should_sleep(const idiom_power_t *power) {
    return power && power->sleeping;
}
