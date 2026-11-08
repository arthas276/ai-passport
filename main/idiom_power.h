// main/idiom_power.h - idle-power policy, pure C, host-testable (mirrors the
// repo's otp_power pattern; constants tuned for a game: shorter dim delay).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IDIOM_BACKLIGHT_ACTIVE 55u
#define IDIOM_BACKLIGHT_DIM 8u
#define IDIOM_DIM_AFTER_MS 12000u
#define IDIOM_SLEEP_AFTER_MS 60000u

typedef struct {
    uint32_t idle_ms;
    bool sleeping;
} idiom_power_t;

void idiom_power_reset(idiom_power_t *power);
void idiom_power_activity(idiom_power_t *power);
void idiom_power_advance(idiom_power_t *power, uint32_t elapsed_ms);
uint8_t idiom_power_backlight(const idiom_power_t *power);
bool idiom_power_should_sleep(const idiom_power_t *power);
