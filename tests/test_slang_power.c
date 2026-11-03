#include "slang_power.h"

#include <stdio.h>

int main(void) {
    slang_power_t power;
    slang_power_reset(&power);
    slang_power_advance(&power, 19999u);
    if (slang_power_backlight(&power) != SLANG_BACKLIGHT_ACTIVE) return 1;
    slang_power_advance(&power, 1u);
    if (slang_power_backlight(&power) != SLANG_BACKLIGHT_DIM || slang_power_should_sleep(&power)) return 2;
    slang_power_activity(&power);
    slang_power_advance(&power, SLANG_SLEEP_AFTER_MS);
    if (!slang_power_should_sleep(&power) || slang_power_backlight(&power) != 0u) return 3;
    puts("Slang power tests: PASS");
    return 0;
}
