// main/almanac_pick.h — the deterministic day record and the lucky-date search ([PURE], host-testable).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "almanac_cal.h"
#include "almanac_content.h"

#define ALMANAC_PICKS_PER_SIDE 3
#define ALMANAC_GODS_PER_SIDE 2

enum { ALMANAC_DIR_CAI = 0, ALMANAC_DIR_XI, ALMANAC_DIR_FU, ALMANAC_DIR_YANG_GUI, ALMANAC_DIR_COUNT };

// Lucky dates are searched in the 30 days after the start day, which is itself excluded.
#define ALMANAC_LUCKY_DATE_SPAN 30
#define ALMANAC_LUCKY_DATE_MAX 3

// Everything one day shows. The same day always yields the same record on every device.
typedef struct {
    int jdn;
    almanac_lunar_t lunar;
    almanac_pillars_t pillars;
    int weekday;
    int zhixing;
    int chong; // clashed zodiac animal
    int sha; // compass direction of 煞方
    int tianshen; // day god
    bool huangdao;
    uint16_t hour_luck; // bit i set: double-hour i (子时..亥时) is lucky
    int dirs[ALMANAC_DIR_COUNT];
    int yi[ALMANAC_PICKS_PER_SIDE]; // item ids
    int ji[ALMANAC_PICKS_PER_SIDE];
    int baiji[2]; // 彭祖百忌 lines: by day stem, by day branch
    int lucky_gods[ALMANAC_GODS_PER_SIDE];
    int unlucky_gods[ALMANAC_GODS_PER_SIDE];
    int protocol;
    int port;
    int tool;
    int color;
    int number;
    int luck[ALMANAC_LUCK_CATEGORY_COUNT]; // 1..5 stars
} almanac_day_t;

void almanac_day_compute(int jdn, almanac_day_t *out);

// Writes up to max_out lucky dates for a matter into out_jdn (with the day's 建除 god in out_god)
// and returns how many were written. A day qualifies when its god is lucky and not avoided for the
// matter and none of its 彭祖百忌 lines suppresses the matter.
int almanac_lucky_dates(int start_jdn, int matter, int *out_jdn, int *out_god, int max_out);
