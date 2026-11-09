// main/pw_power.h - idle-power policy, pure C, host-testable (same contract
// as the repo's otp_power / idiom_power pattern).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PW_BACKLIGHT_ACTIVE 55u
#define PW_BACKLIGHT_DIM 8u
#define PW_DIM_AFTER_MS 12000u
#define PW_SLEEP_AFTER_MS 60000u

typedef struct {
    uint32_t idle_ms;
    bool sleeping;
} pw_power_t;

void pw_power_reset(pw_power_t *power);
void pw_power_activity(pw_power_t *power);
void pw_power_advance(pw_power_t *power, uint32_t elapsed_ms);
uint8_t pw_power_backlight(const pw_power_t *power);
bool pw_power_should_sleep(const pw_power_t *power);
