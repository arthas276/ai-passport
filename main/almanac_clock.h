// main/almanac_clock.h — wall-clock helpers for a device without a battery-backed clock ([PURE]).
//
// Times are China Standard Time (UTC+8) seconds since the Unix epoch. The app never sets a time
// zone: local dates come from a fixed offset, so the host and the device agree. The system clock
// survives deep sleep but not a reset or power loss; when it is unset the app asks for the date again.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ALMANAC_TZ_OFFSET_SECONDS (8 * 3600)

// Days outside this window mean the clock was never set or has been lost.
#define ALMANAC_CLOCK_MIN_YEAR 2020
#define ALMANAC_CLOCK_MAX_YEAR 2100

// True while the clock carries a believable date.
bool almanac_clock_is_valid(int64_t now);
// Local day (Julian day number) of a wall-clock time.
int almanac_clock_day(int64_t now);
void almanac_clock_split(int64_t now, int *jdn, int *hour, int *minute);
// Wall-clock time of a local day at hour:minute.
int64_t almanac_clock_make(int jdn, int hour, int minute);
// Day the date wheel offers first: the day the user last confirmed while it is inside the
// supported range, otherwise 2026-01-01.
int almanac_clock_setup_default(int last_confirmed_day);
