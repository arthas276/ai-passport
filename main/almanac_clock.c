// main/almanac_clock.c — wall-clock helpers for a device without a battery-backed clock ([PURE]).
#include "almanac_clock.h"

#include <stdbool.h>

#include "almanac_cal.h"

#define SECONDS_PER_DAY 86400
#define JDN_OF_EPOCH 2440588 // 1970-01-01

static int64_t floor_div(int64_t value, int64_t divisor) {
    const int64_t quotient = value / divisor;
    return value % divisor != 0 && ((value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

static int64_t local_seconds(int64_t now) {
    return now + ALMANAC_TZ_OFFSET_SECONDS;
}

int almanac_clock_day(int64_t now) {
    return (int)(floor_div(local_seconds(now), SECONDS_PER_DAY) + JDN_OF_EPOCH);
}

void almanac_clock_split(int64_t now, int *jdn, int *hour, int *minute) {
    const int64_t local = local_seconds(now);
    const int64_t whole_days = floor_div(local, SECONDS_PER_DAY);
    const int64_t within_day = local - whole_days * SECONDS_PER_DAY;
    *jdn = (int)(whole_days + JDN_OF_EPOCH);
    *hour = (int)(within_day / 3600);
    *minute = (int)((within_day % 3600) / 60);
}

int64_t almanac_clock_make(int jdn, int hour, int minute) {
    return ((int64_t)(jdn - JDN_OF_EPOCH) * SECONDS_PER_DAY) + hour * 3600 + minute * 60
           - ALMANAC_TZ_OFFSET_SECONDS;
}

bool almanac_clock_is_valid(int64_t now) {
    const int day = almanac_clock_day(now);
    return day >= almanac_days_from_civil(ALMANAC_CLOCK_MIN_YEAR, 1, 1)
           && day <= almanac_days_from_civil(ALMANAC_CLOCK_MAX_YEAR, 12, 31);
}

int almanac_clock_setup_default(int last_confirmed_day) {
    if (last_confirmed_day >= almanac_supported_min_day() && last_confirmed_day <= almanac_supported_max_day()) {
        return last_confirmed_day;
    }
    return almanac_days_from_civil(2026, 1, 1);
}
