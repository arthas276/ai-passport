// tests/test_almanac_cal.c — host test: the calendar engine against independent golden vectors.
#include <stdbool.h>
#include <stdio.h>

#include "almanac_cal.h"
#include "almanac_golden.h"

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

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static void test_golden_days(void) {
    int bad_rows = 0;
    for (size_t i = 0; i < COUNT_OF(k_golden_days); ++i) {
        const almanac_golden_day_t *g = &k_golden_days[i];
        const int jdn = almanac_days_from_civil(g->y, g->m, g->d);
        almanac_ymd_t back;
        almanac_lunar_t lunar;
        almanac_pillars_t p;
        almanac_civil_from_days(jdn, &back);
        const int rc = almanac_solar_to_lunar(jdn, &lunar);
        almanac_pillars(jdn, &p);
        const bool ok = rc == ALMANAC_OK && back.year == g->y && back.month == g->m && back.day == g->d
                        && almanac_is_supported_day(jdn) && lunar.year == g->lunar_year
                        && lunar.month == g->lunar_month && lunar.day == g->lunar_day
                        && lunar.leap == (g->lunar_leap != 0) && p.year_stem == g->year_stem
                        && p.year_branch == g->year_branch && p.month_stem == g->month_stem
                        && p.month_branch == g->month_branch && p.day_stem == g->day_stem
                        && p.day_branch == g->day_branch && almanac_zhixing(jdn) == g->zhixing
                        && almanac_chong(jdn) == g->chong && almanac_sha(jdn) == g->sha
                        && almanac_tianshen_day(jdn) == g->tianshen
                        && almanac_is_huangdao_day(jdn) == (g->huangdao != 0)
                        && almanac_dir_cai(p.day_stem) == g->dir_cai && almanac_dir_xi(p.day_stem) == g->dir_xi
                        && almanac_dir_fu(p.day_stem) == g->dir_fu
                        && (g->dir_yang < 0 || almanac_dir_yang_gui(p.day_stem) == g->dir_yang);
        if (ok) continue;
        ++bad_rows;
        if (bad_rows <= 5) {
            printf("  mismatch on %d-%02d-%02d (rc %d): civil %d-%02d-%02d, lunar %d-%02d-%02d leap %d, "
                   "pillars %d%d %d%d %d%d, zhixing %d, chong %d, sha %d, god %d huangdao %d, dirs %d %d %d %d\n",
                   g->y, g->m, g->d, rc, back.year, back.month, back.day, lunar.year, lunar.month, lunar.day,
                   (int)lunar.leap, p.year_stem, p.year_branch, p.month_stem, p.month_branch, p.day_stem,
                   p.day_branch, almanac_zhixing(jdn), almanac_chong(jdn), almanac_sha(jdn),
                   almanac_tianshen_day(jdn), (int)almanac_is_huangdao_day(jdn), almanac_dir_cai(p.day_stem),
                   almanac_dir_xi(p.day_stem), almanac_dir_fu(p.day_stem), almanac_dir_yang_gui(p.day_stem));
        }
    }
    CHECK(bad_rows == 0, "%d of %d golden day rows mismatch", bad_rows, (int)COUNT_OF(k_golden_days));
}

static void test_golden_hours(void) {
    int bad_rows = 0;
    for (size_t i = 0; i < COUNT_OF(k_golden_hours); ++i) {
        const almanac_golden_hours_t *g = &k_golden_hours[i];
        const int jdn = almanac_days_from_civil(g->y, g->m, g->d);
        const uint16_t mask = almanac_hour_luck_mask(jdn);
        if (mask != g->luck) {
            ++bad_rows;
            if (bad_rows <= 5) {
                printf("  %d-%02d-%02d: computed %04x, published %04x\n", g->y, g->m, g->d, mask, g->luck);
            }
        }
        for (int hour = 0; hour < 12; ++hour) {
            CHECK(almanac_is_lucky_hour(jdn, hour) == ((g->luck & (1 << hour)) != 0), "hour %d on %d-%02d-%02d",
                  hour, g->y, g->m, g->d);
        }
    }
    CHECK(bad_rows == 0, "%d of %d golden hour rows mismatch", bad_rows, (int)COUNT_OF(k_golden_hours));
}

static void test_pinned_days(void) {
    const int jdn = almanac_days_from_civil(2026, 11, 23);
    almanac_lunar_t lunar;
    almanac_pillars_t p;
    CHECK(almanac_solar_to_lunar(jdn, &lunar) == ALMANAC_OK, "2026-11-23 is in range");
    CHECK(lunar.year == 2026 && lunar.month == 10 && lunar.day == 15 && !lunar.leap, "2026-11-23 is 农历十月十五");
    almanac_pillars(jdn, &p);
    CHECK(p.year_stem == 2 && p.year_branch == 6 && p.month_stem == 5 && p.month_branch == 11 && p.day_stem == 7
              && p.day_branch == 1,
          "2026-11-23 is 丙午年 己亥月 辛丑日");
    CHECK(almanac_zhixing(jdn) == 2, "2026-11-23 is a 满日");
    CHECK(almanac_chong(jdn) == 7 && almanac_sha(jdn) == 2, "2026-11-23 冲羊煞东");
    CHECK(almanac_tianshen_day(jdn) == 7 && almanac_is_huangdao_day(jdn), "2026-11-23 值神玉堂, a 黄道日");
    CHECK(almanac_weekday(jdn) == 0, "2026-11-23 is a Monday");

    const int jiazi = almanac_days_from_civil(1949, 10, 1);
    almanac_pillars(jiazi, &p);
    CHECK(p.day_stem == 0 && p.day_branch == 0, "1949-10-01 is a 甲子 day");
    CHECK(almanac_weekday(jiazi) == 5, "1949-10-01 is a Saturday");
    CHECK(almanac_zhixing(jiazi) == 3, "1949-10-01 is a 平日");
    CHECK(almanac_tianshen_day(jiazi) == 10 && almanac_is_huangdao_day(jiazi), "1949-10-01 值神司命");

    const int wuwu = almanac_days_from_civil(2000, 1, 1);
    almanac_pillars(wuwu, &p);
    CHECK(p.day_stem == 4 && p.day_branch == 6, "2000-01-01 is a 戊午 day");
    CHECK(almanac_zhixing(wuwu) == 6, "2000-01-01 is a 破日");

    CHECK(almanac_zhixing(almanac_days_from_civil(2026, 11, 27)) == 6, "2026-11-27 is a 破日");
    CHECK(almanac_zhixing(almanac_days_from_civil(2026, 12, 10)) == 6, "2026-12-10 is a 破日");
    CHECK(almanac_zhixing(almanac_days_from_civil(2026, 12, 6)) == 3
              && almanac_zhixing(almanac_days_from_civil(2026, 12, 7)) == 3,
          "the 建除 god repeats on the day of 大雪");
    CHECK(almanac_month_branch(almanac_days_from_civil(2026, 12, 7)) == 0
              && almanac_month_branch(almanac_days_from_civil(2026, 12, 6)) == 11,
          "the month branch becomes 子 on 大雪");
}

static void test_lunar_boundaries(void) {
    almanac_lunar_t lunar;
    const int spring = almanac_days_from_civil(2026, 2, 17);
    CHECK(almanac_solar_to_lunar(spring, &lunar) == ALMANAC_OK && lunar.year == 2026 && lunar.month == 1
              && lunar.day == 1,
          "2026-02-17 is 正月初一");
    CHECK(almanac_solar_to_lunar(spring - 1, &lunar) == ALMANAC_OK && lunar.year == 2025 && lunar.month == 12
              && lunar.day == 29,
          "2026-02-16 is 腊月廿九");
    const int leap = almanac_days_from_civil(2025, 7, 25);
    CHECK(almanac_solar_to_lunar(leap, &lunar) == ALMANAC_OK && lunar.month == 6 && lunar.day == 1 && lunar.leap,
          "2025-07-25 is 闰六月初一");
    CHECK(almanac_solar_to_lunar(leap - 1, &lunar) == ALMANAC_OK && lunar.month == 6 && lunar.day == 30
              && !lunar.leap,
          "2025-07-24 is 六月三十");
    CHECK(almanac_solar_to_lunar(almanac_days_from_civil(1900, 1, 30), &lunar) == ALMANAC_ERR_RANGE,
          "1900-01-30 is out of range");
    CHECK(almanac_solar_to_lunar(almanac_days_from_civil(2101, 1, 1), &lunar) == ALMANAC_ERR_RANGE,
          "2101-01-01 is out of range");
    CHECK(almanac_solar_to_lunar(almanac_supported_min_day(), &lunar) == ALMANAC_OK && lunar.month == 1
              && lunar.day == 1,
          "1900-01-31 is the first supported day");
    CHECK(almanac_solar_to_lunar(almanac_supported_max_day(), &lunar) == ALMANAC_OK && lunar.year == 2100
              && lunar.month == 12 && lunar.day == 1,
          "2100-12-31 is 腊月初一");
}

static void test_civil_helpers(void) {
    CHECK(almanac_days_in_month(2024, 2) == 29 && almanac_days_in_month(2025, 2) == 28, "February lengths");
    CHECK(almanac_is_leap_year(2000) && !almanac_is_leap_year(1900) && !almanac_is_leap_year(2026),
          "leap-year rule");
    int round_trips = 0;
    for (int jdn = almanac_supported_min_day(); jdn <= almanac_supported_max_day(); jdn += 97) {
        almanac_ymd_t ymd;
        almanac_ymd_t next;
        almanac_civil_from_days(jdn, &ymd);
        almanac_civil_from_days(jdn + 1, &next);
        if (almanac_days_from_civil(ymd.year, ymd.month, ymd.day) == jdn
            && ymd.day <= almanac_days_in_month(ymd.year, ymd.month)
            && almanac_days_from_civil(next.year, next.month, next.day) == jdn + 1
            && next.day == ymd.day + 1) {
            ++round_trips;
        }
    }
    CHECK(round_trips > 700, "%d civil round trips succeeded", round_trips);
}

static void test_direction_tables(void) {
    static const int cai[10] = {1, 1, 5, 5, 0, 0, 2, 2, 4, 4};
    static const int xi[10] = {1, 7, 5, 4, 3, 1, 7, 5, 4, 3};
    static const int fu[10] = {0, 5, 7, 3, 1, 0, 5, 7, 3, 1};
    static const int yang[10] = {5, 5, 6, 7, 1, 0, 1, 1, 2, 3};
    for (int stem = 0; stem < 10; ++stem) {
        CHECK(almanac_dir_cai(stem) == cai[stem] && almanac_dir_xi(stem) == xi[stem]
                  && almanac_dir_fu(stem) == fu[stem] && almanac_dir_yang_gui(stem) == yang[stem],
              "direction tables, stem %d", stem);
    }
}

static void test_hour_structure(void) {
    const int start = almanac_days_from_civil(2026, 1, 1);
    for (int jdn = start; jdn < start + 400; ++jdn) {
        unsigned seen = 0;
        int lucky = 0;
        for (int hour = 0; hour < 12; ++hour) {
            seen |= 1u << almanac_tianshen_hour(jdn, hour);
            lucky += almanac_is_lucky_hour(jdn, hour) ? 1 : 0;
        }
        CHECK(seen == 0xFFFu && lucky == 6, "hour gods on JDN %d: seen %03x, lucky %d", jdn, seen, lucky);
    }
}

int main(void) {
    test_golden_days();
    test_golden_hours();
    test_pinned_days();
    test_lunar_boundaries();
    test_civil_helpers();
    test_direction_tables();
    test_hour_structure();
    if (failures == 0) {
        printf("PASS test_almanac_cal (%d golden days, %d golden hour rows)\n", (int)COUNT_OF(k_golden_days),
               (int)COUNT_OF(k_golden_hours));
    } else {
        printf("FAIL test_almanac_cal: %d failing checks\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
