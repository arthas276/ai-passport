// main/almanac_ui.h — LVGL screens for the almanac: screen chrome and the scene renderer.
//
// Every builder starts a fresh screen (almanac_ui_begin), draws the scene that the [PURE] view
// module lays out from the coordinates of main/almanac_view.h, and ends with the two-row hint
// pill (almanac_ui_hints). No state and no keys live here: almanac_app.c owns both.
#pragma once

#include <stdbool.h>

#include "almanac_pick.h"
#include "almanac_view.h"

void almanac_ui_begin(const char *title, int battery_soc);
void almanac_ui_battery(int battery_soc);
void almanac_ui_hints(const char *line1, const char *line2);

// banner stands in for the date line while the date is not confirmed; NULL shows the date.
void almanac_ui_today(const almanac_view_day_t *day, const char *zhixing, bool roaming, const char *banner);
// huangdao picks the tint of the day-type card.
void almanac_ui_detail(const almanac_view_day_t *day, bool huangdao);
void almanac_ui_hours(const almanac_view_hours_t *hours);
void almanac_ui_lucky(const almanac_view_lucky_t *lucky);
void almanac_ui_fortune(const almanac_day_t *day);
void almanac_ui_menu(int selected);
void almanac_ui_pick(int selected, int first_visible);
void almanac_ui_result(const char *matter, const almanac_line_t *dates, int count);
void almanac_ui_setup(int step, int year, int month, int day);
void almanac_ui_confirm(const char *date_text);
void almanac_ui_help(const almanac_line_t *lines, int count);
void almanac_ui_about(const almanac_line_t *lines, int count);
