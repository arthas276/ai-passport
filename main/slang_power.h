#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 和小电池配套的空闲策略。查词不需要屏幕一直全亮。 */

#define SLANG_BACKLIGHT_ACTIVE 50u
#define SLANG_BACKLIGHT_DIM 8u
#define SLANG_DIM_AFTER_MS 20000u
#define SLANG_SLEEP_AFTER_MS 60000u

typedef struct {
    uint32_t idle_ms;
    bool sleeping;
} slang_power_t;

void slang_power_reset(slang_power_t *power);
void slang_power_activity(slang_power_t *power);
void slang_power_advance(slang_power_t *power, uint32_t elapsed_ms);
uint8_t slang_power_backlight(const slang_power_t *power);
bool slang_power_should_sleep(const slang_power_t *power);
