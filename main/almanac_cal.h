// main/almanac_cal.h — traditional-calendar engine for the almanac ([PURE], host-testable).
//
// Index conventions shared by the whole app:
//   stems    0..9   甲 乙 丙 丁 戊 己 庚 辛 壬 癸
//   branches 0..11  子 丑 寅 卯 辰 巳 午 未 申 酉 戌 亥
//   compass  0..7   正北 东北 正东 东南 正南 西南 正西 西北 (clockwise from north)
//   gods     0..11  青龙 明堂 天刑 朱雀 金匮 天德 白虎 玉堂 天牢 玄武 司命 勾陈
//   zhixing  0..11  建 除 满 平 定 执 破 危 成 收 开 闭
// A day is addressed by its Julian day number (integer days, noon-based).
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { ALMANAC_OK = 0, ALMANAC_ERR_RANGE = -1 };

typedef struct {
    int year;
    int month;
    int day;
} almanac_ymd_t;

// Lunar date. The year changes at the spring festival; month is 1..12 and day is 1..30.
typedef struct {
    int year;
    int month;
    int day;
    bool leap;
} almanac_lunar_t;

typedef struct {
    int year_stem;
    int year_branch;
    int month_stem;
    int month_branch;
    int day_stem;
    int day_branch;
} almanac_pillars_t;

int almanac_days_from_civil(int y, int m, int d);
void almanac_civil_from_days(int jdn, almanac_ymd_t *out);
int almanac_weekday(int jdn); // 0 = Monday .. 6 = Sunday
bool almanac_is_leap_year(int y);
int almanac_days_in_month(int y, int m);

// The supported range is 1900-01-31 .. 2100-12-31.
bool almanac_is_supported_day(int jdn);
int almanac_supported_min_day(void);
int almanac_supported_max_day(void);

// Returns ALMANAC_ERR_RANGE outside the supported range.
int almanac_solar_to_lunar(int jdn, almanac_lunar_t *out);

// The year pillar follows the spring festival (歲次). The month branch changes on the day of each
// 节 and its stem is derived from the year that began at 立春.
void almanac_pillars(int jdn, almanac_pillars_t *out);

// 0..59 in the sexagenary cycle, 甲子 = 0.
int almanac_day_index(int jdn);

// 0 (子) from 大雪 to the day before 小寒, 1 (丑) from 小寒, 2 (寅) from 立春, and so on.
int almanac_month_branch(int jdn);
int almanac_zhixing(int jdn);
int almanac_chong(int jdn); // branch clashed by the day branch
int almanac_sha(int jdn); // compass direction of 煞方
int almanac_tianshen_day(int jdn);
bool almanac_is_huangdao_day(int jdn);

// hour is 0..11 for 子时..亥时.
int almanac_tianshen_hour(int jdn, int hour);
bool almanac_is_lucky_hour(int jdn, int hour);
uint16_t almanac_hour_luck_mask(int jdn);
bool almanac_god_is_huangdao(int god);

// Directions of the four auspicious gods, by day stem. 福神 and 贵人 follow the 协纪 school.
int almanac_dir_cai(int day_stem);
int almanac_dir_xi(int day_stem);
int almanac_dir_fu(int day_stem);
int almanac_dir_yang_gui(int day_stem);
