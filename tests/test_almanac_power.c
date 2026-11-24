// tests/test_almanac_power.c — host test: the idle-power policy.
#include <stdbool.h>
#include <stdio.h>

#include "almanac_power.h"

static int failures;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            ++failures; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static void test_dim_then_sleep(void) {
    almanac_power_t power;
    almanac_power_reset(&power);
    CHECK(almanac_power_backlight(&power) == ALMANAC_BACKLIGHT_ACTIVE && !almanac_power_should_sleep(&power),
          "a fresh policy is active");
    almanac_power_advance(&power, ALMANAC_DIM_AFTER_MS - 250u);
    CHECK(almanac_power_backlight(&power) == ALMANAC_BACKLIGHT_ACTIVE, "just before the dim point the backlight is active");
    almanac_power_advance(&power, 250u);
    CHECK(almanac_power_backlight(&power) == ALMANAC_BACKLIGHT_DIM && !almanac_power_should_sleep(&power),
          "at the dim point the backlight dims");
    almanac_power_activity(&power);
    CHECK(almanac_power_backlight(&power) == ALMANAC_BACKLIGHT_ACTIVE, "activity restores the backlight");
    almanac_power_advance(&power, ALMANAC_SLEEP_AFTER_MS);
    CHECK(almanac_power_should_sleep(&power) && almanac_power_backlight(&power) == 0u, "sleep at the sleep point");
    almanac_power_advance(&power, 1000u);
    almanac_power_activity(&power);
    CHECK(!almanac_power_should_sleep(&power), "activity after sleep starts over");
}

static void test_idle_counter_saturates(void) {
    almanac_power_t power;
    almanac_power_reset(&power);
    power.idle_ms = UINT32_MAX - 100u;
    almanac_power_advance(&power, 1000u);
    CHECK(power.idle_ms == UINT32_MAX && almanac_power_should_sleep(&power), "the idle counter does not wrap around");
}

int main(void) {
    test_dim_then_sleep();
    test_idle_counter_saturates();
    if (failures == 0) {
        printf("PASS test_almanac_power\n");
    } else {
        printf("FAIL test_almanac_power: %d failing checks\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
