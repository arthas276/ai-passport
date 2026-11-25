// tests/test_almanac_clock.c — host test: wall-clock helpers and the unset-clock window.
#include <stdbool.h>
#include <stdio.h>

#include "almanac_cal.h"
#include "almanac_clock.h"

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

static void test_validity_window(void) {
    const int first = almanac_days_from_civil(2020, 1, 1);
    const int last = almanac_days_from_civil(2100, 12, 31);
    CHECK(almanac_clock_is_valid(almanac_clock_make(first, 0, 0)), "2020-01-01 00:00 is inside the window");
    CHECK(!almanac_clock_is_valid(almanac_clock_make(first, 0, 0) - 1), "one second earlier is outside");
    CHECK(almanac_clock_is_valid(almanac_clock_make(last, 23, 59)), "2100-12-31 23:59 is inside the window");
    CHECK(!almanac_clock_is_valid(almanac_clock_make(last + 1, 0, 0)), "2101-01-01 is outside");
    CHECK(!almanac_clock_is_valid(0), "the epoch itself means the clock was never set");
}

static void test_local_day_boundaries(void) {
    const int day = almanac_days_from_civil(2026, 11, 23);
    const int64_t midnight = almanac_clock_make(day, 0, 0);
    CHECK(midnight == 1795363200, "2026-11-23 00:00 CST is 1795363200, got %lld", (long long)midnight);
    CHECK(almanac_clock_day(midnight) == day, "midnight belongs to its own day");
    CHECK(almanac_clock_day(midnight - 1) == day - 1, "one second before midnight belongs to the day before");
    CHECK(almanac_clock_day(midnight + 86399) == day, "23:59:59 belongs to its own day");
    CHECK(almanac_clock_day(midnight + 86400) == day + 1, "the next midnight starts the next day");
}

static void test_split_matches_make(void) {
    const int start = almanac_days_from_civil(2026, 11, 23);
    for (int offset = 0; offset < 400; offset += 37) {
        const int hour = offset % 24;
        const int minute = (offset * 7) % 60;
        int jdn = 0;
        int h = -1;
        int m = -1;
        almanac_clock_split(almanac_clock_make(start + offset, hour, minute), &jdn, &h, &m);
        CHECK(jdn == start + offset && h == hour && m == minute, "round trip for day offset %d", offset);
    }
}

static void test_setup_default(void) {
    const int confirmed = almanac_days_from_civil(2026, 11, 23);
    const int built = almanac_days_from_civil(2026, 11, 20);
    const int fallback = almanac_days_from_civil(2026, 1, 1);
    CHECK(almanac_clock_setup_default(-1, built) == built, "nothing confirmed offers the build day");
    CHECK(almanac_clock_setup_default(confirmed, built) == confirmed,
          "a confirmed day is offered again before the build day");
    CHECK(almanac_clock_setup_default(0, built) == built, "day 0 is outside the range");
    CHECK(almanac_clock_setup_default(almanac_supported_max_day() + 1, built) == built,
          "a day past the range is not offered");
    CHECK(almanac_clock_setup_default(almanac_supported_min_day(), 0) == almanac_supported_min_day(),
          "the first supported day is offered");
    CHECK(almanac_clock_setup_default(-1, 0) == fallback, "no confirmed day and no build day gives the fallback");
    CHECK(almanac_clock_setup_default(-1, almanac_supported_max_day() + 1) == fallback,
          "a build day outside the range gives the fallback");
}

static void test_parse_build_date(void) {
    CHECK(almanac_clock_parse_build_date("Nov 25 2026") == almanac_days_from_civil(2026, 11, 25),
          "Nov 25 2026 is 2026-11-25");
    CHECK(almanac_clock_parse_build_date("Jan  5 2026") == almanac_days_from_civil(2026, 1, 5),
          "__DATE__ pads a one-digit day with a space");
    CHECK(almanac_clock_parse_build_date("Sep 1 2026") == almanac_days_from_civil(2026, 9, 1),
          "a one-digit day without padding is read too");
    CHECK(almanac_clock_parse_build_date("Feb 29 2028") == almanac_days_from_civil(2028, 2, 29), "a leap day is a day");
    CHECK(almanac_clock_parse_build_date("Feb 29 2027") == 0, "2027 has no leap day");
    CHECK(almanac_clock_parse_build_date("Dec 32 2026") == 0, "no month has 32 days");
    CHECK(almanac_clock_parse_build_date("Xyz 10 2026") == 0, "an unknown month is not a date");
    CHECK(almanac_clock_parse_build_date("Nov 25 20") == 0, "a short year is not a date");
    CHECK(almanac_clock_parse_build_date("Nov 25 2026 ") == 0, "trailing text is not a date");
    CHECK(almanac_clock_parse_build_date("Nov 2026") == 0, "a missing day is not a date");
    CHECK(almanac_clock_parse_build_date("") == 0, "an empty text is not a date");
    CHECK(almanac_clock_parse_build_date(NULL) == 0, "no text is not a date");
    CHECK(almanac_clock_parse_build_date("Jan 30 1900") == 0, "a day before the supported range is refused");
    CHECK(almanac_clock_parse_build_date("Jan 1 2101") == 0, "a day after the supported range is refused");
}

int main(void) {
    test_validity_window();
    test_local_day_boundaries();
    test_split_matches_make();
    test_parse_build_date();
    test_setup_default();
    if (failures == 0) {
        printf("PASS test_almanac_clock\n");
    } else {
        printf("FAIL test_almanac_clock: %d failing checks\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
