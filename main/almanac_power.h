// main/almanac_power.h — idle-power policy, pure C, host-testable (the family's power contract).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ALMANAC_BACKLIGHT_ACTIVE 55u
#define ALMANAC_BACKLIGHT_DIM 8u
#define ALMANAC_DIM_AFTER_MS 12000u
#define ALMANAC_SLEEP_AFTER_MS 60000u

typedef struct {
    uint32_t idle_ms;
    bool sleeping;
} almanac_power_t;

void almanac_power_reset(almanac_power_t *power);
void almanac_power_activity(almanac_power_t *power);
void almanac_power_advance(almanac_power_t *power, uint32_t elapsed_ms);
uint8_t almanac_power_backlight(const almanac_power_t *power);
bool almanac_power_should_sleep(const almanac_power_t *power);
