// main/almanac_cal.c — traditional-calendar engine for the almanac ([PURE], host-testable).
#include "almanac_cal.h"

#include "almanac_tables.h"

int almanac_days_from_civil(int y, int m, int d) {
    const int a = (14 - m) / 12;
    const int yy = y + 4800 - a;
    const int mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

void almanac_civil_from_days(int jdn, almanac_ymd_t *out) {
    const long long a = (long long)jdn + 32044;
    const long long era = (a >= 0 ? a : a - 146096) / 146097;
    const unsigned doe = (unsigned)(a - era * 146097); // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    long long y = (long long)yoe + era * 400 - 4800;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153; // [0, 11]
    out->day = (int)(doy - (153 * mp + 2) / 5 + 1); // [1, 31]
    out->month = (int)(mp + (mp < 10 ? 3 : -9)); // [1, 12]
    y += out->month <= 2;
    out->year = (int)y;
}

int almanac_weekday(int jdn) {
    return ((jdn % 7) + 7) % 7;
}

bool almanac_is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int almanac_days_in_month(int y, int m) {
    static const int k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && almanac_is_leap_year(y)) return 29;
    return k_days[m - 1];
}

int almanac_supported_min_day(void) {
    return almanac_days_from_civil(ALMANAC_YEAR_MIN, 1, 31);
}

int almanac_supported_max_day(void) {
    return almanac_days_from_civil(ALMANAC_YEAR_MAX, 12, 31);
}

bool almanac_is_supported_day(int jdn) {
    return jdn >= almanac_supported_min_day() && jdn <= almanac_supported_max_day();
}

int almanac_day_index(int jdn) {
    return ((jdn + 49) % 60 + 60) % 60;
}

static int mod12(int value) {
    return ((value % 12) + 12) % 12;
}

static int month_is_big(const almanac_lunar_year_t *year, int month, bool leap) {
    return leap ? (year->big_bits >> 12) & 1 : (year->big_bits >> (month - 1)) & 1;
}

// Calendar slot of a lunar year (0..12) to month number; slots at and after a leap month shift by one.
static int slot_month(int leap_month, int slot) {
    if (leap_month == 0) return slot + 1;
    if (slot < leap_month) return slot + 1;
    if (slot == leap_month) return leap_month;
    return slot;
}

int almanac_solar_to_lunar(int jdn, almanac_lunar_t *out) {
    almanac_ymd_t ymd;
    almanac_civil_from_days(jdn, &ymd);
    if (ymd.year < ALMANAC_YEAR_MIN || ymd.year > ALMANAC_YEAR_MAX) return ALMANAC_ERR_RANGE;
    int lunar_year = ymd.year;
    const almanac_lunar_year_t *civil_year = &almanac_lunar_years[ymd.year - ALMANAC_YEAR_MIN];
    if (jdn < almanac_days_from_civil(ymd.year, civil_year->spring_month, civil_year->spring_day)) {
        lunar_year = ymd.year - 1;
    }
    if (lunar_year < ALMANAC_YEAR_MIN) return ALMANAC_ERR_RANGE;
    const almanac_lunar_year_t *year = &almanac_lunar_years[lunar_year - ALMANAC_YEAR_MIN];
    const int start = almanac_days_from_civil(lunar_year, year->spring_month, year->spring_day);
    int offset = jdn - start;
    const int slots = year->leap_month ? 13 : 12;
    for (int slot = 0; slot < slots; ++slot) {
        const bool leap = year->leap_month != 0 && slot == year->leap_month;
        const int length = 29 + month_is_big(year, slot_month(year->leap_month, slot), leap);
        if (offset < length) {
            out->year = lunar_year;
            out->month = slot_month(year->leap_month, slot);
            out->day = offset + 1;
            out->leap = leap;
            return ALMANAC_OK;
        }
        offset -= length;
    }
    return ALMANAC_ERR_RANGE;
}

// Month branch that began with the most recent 节.
int almanac_month_branch(int jdn) {
    static const int8_t k_branch_after_jie[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0};
    almanac_ymd_t ymd;
    almanac_civil_from_days(jdn, &ymd);
    int year = ymd.year;
    if (year < ALMANAC_YEAR_MIN) year = ALMANAC_YEAR_MIN;
    if (year > ALMANAC_YEAR_MAX) year = ALMANAC_YEAR_MAX;
    const almanac_jie_year_t *jie = &almanac_jie_years[year - ALMANAC_YEAR_MIN];
    for (int i = 11; i >= 0; --i) {
        if (jdn >= almanac_days_from_civil(year, jie->month[i], jie->day[i])) return k_branch_after_jie[i];
    }
    return 0;
}

void almanac_pillars(int jdn, almanac_pillars_t *out) {
    almanac_lunar_t lunar;
    almanac_solar_to_lunar(jdn, &lunar);
    out->year_stem = (lunar.year - 4) % 10;
    out->year_branch = (lunar.year - 4) % 12;

    almanac_ymd_t ymd;
    almanac_civil_from_days(jdn, &ymd);
    const almanac_jie_year_t *jie = &almanac_jie_years[ymd.year - ALMANAC_YEAR_MIN];
    const int lichun_year =
        jdn >= almanac_days_from_civil(ymd.year, jie->month[1], jie->day[1]) ? ymd.year : ymd.year - 1;
    const int first_month_stem = (2 * ((lichun_year - 4) % 10) + 2) % 10; // stem of that year's 寅 month
    out->month_branch = almanac_month_branch(jdn);
    out->month_stem = (first_month_stem + mod12(out->month_branch - 2)) % 10;

    out->day_stem = almanac_day_index(jdn) % 10;
    out->day_branch = almanac_day_index(jdn) % 12;
}

int almanac_zhixing(int jdn) {
    return mod12(almanac_day_index(jdn) % 12 - almanac_month_branch(jdn));
}

int almanac_chong(int jdn) {
    return (almanac_day_index(jdn) % 12 + 6) % 12;
}

// 煞方 by day branch: 申子辰 煞南, 寅午戌 煞北, 巳酉丑 煞东, 亥卯未 煞西.
int almanac_sha(int jdn) {
    static const int8_t k_sha_by_branch[12] = {4, 2, 0, 6, 4, 2, 0, 6, 4, 2, 0, 6};
    return k_sha_by_branch[almanac_day_index(jdn) % 12];
}

// The day god and the hour gods run through the same sequence; where it starts follows the
// six-fold cycle of the key branch (the month branch for the day god, the day branch for hours).
static int god_start(int key_branch) {
    return (8 + 2 * (key_branch % 6)) % 12;
}

int almanac_tianshen_day(int jdn) {
    return mod12(almanac_day_index(jdn) % 12 - god_start(almanac_month_branch(jdn)));
}

bool almanac_god_is_huangdao(int god) {
    static const bool k_huangdao[12] = {true, true, false, false, true, true, false, true, false, false, true, false};
    return k_huangdao[god];
}

bool almanac_is_huangdao_day(int jdn) {
    return almanac_god_is_huangdao(almanac_tianshen_day(jdn));
}

int almanac_tianshen_hour(int jdn, int hour) {
    return mod12(hour - god_start(almanac_day_index(jdn) % 12));
}

bool almanac_is_lucky_hour(int jdn, int hour) {
    return almanac_god_is_huangdao(almanac_tianshen_hour(jdn, hour));
}

uint16_t almanac_hour_luck_mask(int jdn) {
    uint16_t mask = 0;
    for (int hour = 0; hour < 12; ++hour) {
        if (almanac_is_lucky_hour(jdn, hour)) mask |= (uint16_t)(1u << hour);
    }
    return mask;
}

int almanac_dir_cai(int day_stem) {
    static const int8_t k_dir[10] = {1, 1, 5, 5, 0, 0, 2, 2, 4, 4};
    return k_dir[day_stem];
}

int almanac_dir_xi(int day_stem) {
    static const int8_t k_dir[10] = {1, 7, 5, 4, 3, 1, 7, 5, 4, 3};
    return k_dir[day_stem];
}

int almanac_dir_fu(int day_stem) {
    static const int8_t k_dir[10] = {0, 5, 7, 3, 1, 0, 5, 7, 3, 1};
    return k_dir[day_stem];
}

int almanac_dir_yang_gui(int day_stem) {
    static const int8_t k_dir[10] = {5, 5, 6, 7, 1, 0, 1, 1, 2, 3};
    return k_dir[day_stem];
}
