// tests/test_almanac_view.c — host test: every line of every ALMANAC screen fits its box.
//
// Measures the lines built by almanac_view.c with the real advance widths of the generated
// fonts (tests/almanac_font_metrics.h), over every day from 2026 to 2029, plus the fixed
// strings and the pages that do not depend on the day.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "almanac_font_metrics.h"
#include "almanac_strings.h"
#include "almanac_view.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        ++failures; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static int adv_exception(uint32_t cp, int size) {
    for (unsigned i = 0; i < ALMANAC_TEST_ADV_EXCEPTIONS; ++i) {
        if (k_ad_exception_cp[i] == cp) {
            return size == 16 ? k_ad_exception_16[i] : k_ad_exception_20[i];
        }
    }
    return -1;
}

// Width of a UTF-8 string in whole pixels for the 16 or 20 px UI font, each glyph rounded to
// whole pixels the way LVGL renders it.
static int width(const char *text, int size) {
    const uint16_t *ascii_table = size == 16 ? k_ad_16 : k_ad_20;
    int total = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p != 0) {
        uint32_t cp;
        if (p[0] < 0x80u) {
            cp = p[0];
            ++p;
        } else if ((p[0] & 0xE0u) == 0xC0u) {
            cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
            p += 2;
        } else if ((p[0] & 0xF0u) == 0xE0u) {
            cp = ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
            p += 3;
        } else {
            cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) |
                 ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
            p += 4;
        }
        if (cp >= 0x20u && cp <= 0x7Eu) {
            total += (ascii_table[cp - 0x20u] + 8) >> 4;
            continue;
        }
        const int exception = adv_exception(cp, size);
        total += exception >= 0 ? (exception + 8) >> 4 : size;
    }
    return total;
}

static int px16(const char *text) { return width(text, 16); }
static int px20(const char *text) { return width(text, 20); }

static void check_fits(const char *what, const char *text, int box, int size) {
    const int w = size == 16 ? px16(text) : px20(text);
    CHECK(w <= box, "%s: %d px > %d px: \"%s\"", what, w, box, text);
}

static void check_day(const almanac_day_t *day) {
    almanac_view_day_t v;
    almanac_view_day(day, &v);
    char tag[160];
    snprintf(tag, sizeof(tag), "day %d", day->jdn);

    check_fits("today date", v.date.text, ALMANAC_CONTENT_W, 16);
    const int roaming_w = px16(v.date.text) + ALMANAC_ROAM_GAP + px16(STR_ROAMING);
    CHECK(roaming_w <= ALMANAC_CONTENT_W, "%s: date and roaming tag %d px", tag, roaming_w);
    check_fits("pillars", v.pillars.text, ALMANAC_CONTENT_W, 16);
    check_fits("lunar", v.lunar.text, ALMANAC_SEAL_TEXT_W, 16);
    check_fits("gloss", v.gloss.text, ALMANAC_SEAL_TEXT_W, 16);
    check_fits("day_type", v.day_type.text, ALMANAC_SEAL_TEXT_W, 16);
    check_fits("chong", v.chong.text, ALMANAC_CONTENT_W, 16);
    check_fits("chong_note", v.chong_note.text, ALMANAC_CONTENT_W, 16);
    for (int i = 0; i < 2; ++i) {
        check_fits("baiji", v.baiji[i].text, ALMANAC_CONTENT_W, 16);
    }
    CHECK(v.yi_lines >= 1 && v.yi_lines <= ALMANAC_LIST_MAX_LINES, "%s: yi lines %d", tag, v.yi_lines);
    CHECK(v.ji_lines >= 1 && v.ji_lines <= ALMANAC_LIST_MAX_LINES, "%s: ji lines %d", tag, v.ji_lines);
    for (int i = 0; i < v.yi_lines; ++i) check_fits("yi line", v.yi[i].text, ALMANAC_BLOCK_W, 16);
    for (int i = 0; i < v.ji_lines; ++i) check_fits("ji line", v.ji[i].text, ALMANAC_BLOCK_W, 16);

    const int lists_rows = v.yi_lines + v.ji_lines;
    const int card_bottom = ALMANAC_LISTS_Y + lists_rows * ALMANAC_ROW16_H + ALMANAC_BLOCK_GAP;
    CHECK(card_bottom <= ALMANAC_CONTENT_BOTTOM, "%s: today card ends at %d", tag, card_bottom);

    almanac_view_lucky_t l;
    almanac_view_lucky(day, &l);
    int say_x = ALMANAC_DIR_SAY_X;
    for (int k = 0; k < ALMANAC_DIR_KIND_COUNT; ++k) {
        const int right = ALMANAC_MARGIN_X + px16(l.dir[k].text) + ALMANAC_DIR_SAY_GAP;
        if (right > say_x) say_x = right;
    }
    for (int k = 0; k < ALMANAC_DIR_KIND_COUNT; ++k) {
        check_fits("dir_say", l.dir_say[k].text, ALMANAC_SCREEN_W - say_x - ALMANAC_MARGIN_X, 16);
    }
    check_fits("protocol", l.protocol.text, ALMANAC_CONTENT_W, 16);
    check_fits("protocol_note", l.protocol_note.text, ALMANAC_BLOCK_W, 16);
    check_fits("port", l.port.text, ALMANAC_CONTENT_W, 16);
    check_fits("port_note", l.port_note.text, ALMANAC_BLOCK_W, 16);
    check_fits("tool", l.tool.text, ALMANAC_CONTENT_W, 16);
    check_fits("color_number", l.color_number.text, ALMANAC_CONTENT_W, 16);

    const int lucky_rows = 4 + 2 + 2 + 1 + 1;
    CHECK(ALMANAC_CONTENT_Y + lucky_rows * ALMANAC_ROW16_H <= ALMANAC_CONTENT_BOTTOM,
          "%s: lucky page ends at %d", tag, ALMANAC_CONTENT_Y + lucky_rows * ALMANAC_ROW16_H);

    almanac_view_hours_t h;
    almanac_view_hours(day, &h);
    int lucky_count = 0;
    for (int i = 0; i < 12; ++i) {
        check_fits("hour", h.hour[i].text, ALMANAC_HOUR_CELL_W - 6, 16);
        if (h.lucky[i]) ++lucky_count;
    }
    CHECK(lucky_count == 6, "%s: %d lucky hours", tag, lucky_count);
    check_fits("lucky hours", h.lucky_hours.text, ALMANAC_CONTENT_W, 16);
    check_fits("unlucky hours", h.unlucky_hours.text, ALMANAC_CONTENT_W, 16);

    almanac_line_t lucky_date;
    almanac_view_lucky_date(day->jdn, day->zhixing, &lucky_date);
    check_fits("lucky date", lucky_date.text, ALMANAC_CONTENT_W, 20);

    check_fits("luck name", almanac_luck_name(1), ALMANAC_CONTENT_W, 16);
    for (int c = 0; c < 5; ++c) {
        check_fits("luck name", almanac_luck_name(c), 100, 16);
        for (int s = 1; s <= 5; ++s) check_fits("luck line", almanac_luck_line(c, s), 192, 16);
    }
}

int main(void) {
    almanac_line_t lines[ALMANAC_TEXT_MAX_LINES];

    const char *const hints[] = {
        STR_HINT_TODAY, STR_HINT_NEXT, STR_HINT_PICK, STR_HINT_MENU, STR_HINT_LONG_MENU, STR_HINT_LONG_BACK,
        STR_HINT_LONG_CANCEL, STR_HINT_BACK, STR_HINT_OK, STR_HINT_REPICK,
    };
    for (unsigned i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i) {
        check_fits("hint", hints[i], 204, 16);
    }
    const char *const titles[] = {
        STR_APP_TITLE, STR_PAGE_DETAIL, STR_PAGE_HOURS, STR_PAGE_DIRS, STR_PAGE_LUCK,
        STR_PAGE_PICK, STR_PAGE_HELP, STR_PAGE_ABOUT, STR_SETUP_TITLE, STR_CALIBRATE_CONFIRM,
        STR_ROAMING,
    };
    for (unsigned i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        check_fits("title", titles[i], ALMANAC_TITLE_MAX_W, 20);
    }
    check_fits("lost banner", STR_DATE_LOST, ALMANAC_CONTENT_W, 16);
    check_fits("setup banner", STR_SETUP_TITLE, ALMANAC_CONTENT_W, 16);
    // The look-alike U+7141 has been typed for 煞 (U+715E) in this tree before, so the label is checked by bytes.
    CHECK(strcmp(STR_PREFIX_SHA, "\xE7\x85\x9E\xEF\xBC\x9A") == 0, "the 煞 label must be U+715E, not U+7141");

    const char *const menu[] = {
        STR_MENU_CALIBRATE, STR_MENU_TODAY, STR_MENU_PICK_DATE, STR_MENU_HELP, STR_MENU_ABOUT,
    };
    const int menu_count = (int)(sizeof(menu) / sizeof(menu[0]));
    CHECK(menu_count == ALMANAC_MENU_ROWS, "the menu names %d rows", menu_count);
    for (int i = 0; i < menu_count; ++i) check_fits("menu item", menu[i], ALMANAC_BLOCK_W, 20);
    for (int m = 0; m < ALMANAC_MATTER_COUNT; ++m) check_fits("matter", almanac_matter(m), ALMANAC_BLOCK_W, 20);
    char date_line[48];
    snprintf(date_line, sizeof(date_line), "%d%s%d%s%d%s", 2026, STR_SETUP_YEAR, 12, STR_SETUP_MONTH, 31,
             STR_SETUP_DAY);
    check_fits("confirm date", date_line, ALMANAC_CONTENT_W, 20);
    check_fits("setup date", date_line, ALMANAC_CONTENT_W, 16);

    const int band_ends[] = {
        ALMANAC_MENU_Y + (ALMANAC_MENU_ROWS - 1) * ALMANAC_MENU_ROW_H + ALMANAC_ROW20_H,
        ALMANAC_PICK_Y + (ALMANAC_PICK_ROWS_VISIBLE - 1) * ALMANAC_PICK_ROW_H + ALMANAC_ROW20_H,
        ALMANAC_FORTUNE_Y + (ALMANAC_LUCK_CATEGORY_COUNT - 1) * ALMANAC_FORTUNE_ROW_H + 2 * ALMANAC_ROW16_H,
        ALMANAC_RESULT_LIST_Y + (ALMANAC_LUCKY_DATE_MAX - 1) * ALMANAC_RESULT_ROW_H + ALMANAC_ROW20_H,
        ALMANAC_HOUR_GRID_Y + 3 * (ALMANAC_HOUR_CELL_H + 2) + 8 + 2 * ALMANAC_ROW16_H,
    };
    const char *const band_names[] = {"menu", "matter list", "fortune", "lucky dates", "hours"};
    for (unsigned i = 0; i < sizeof(band_ends) / sizeof(band_ends[0]); ++i) {
        CHECK(band_ends[i] <= ALMANAC_CONTENT_BOTTOM, "%s page ends at %d", band_names[i], band_ends[i]);
    }
    almanac_line_t need[ALMANAC_TEXT_MAX_LINES + 1];
    const int help_need = almanac_view_help(need, ALMANAC_TEXT_MAX_LINES + 1);
    CHECK(help_need >= 1 && help_need <= ALMANAC_TEXT_MAX_LINES, "help needs %d lines", help_need);
    const int about_need = almanac_view_about(need, ALMANAC_TEXT_MAX_LINES + 1);
    CHECK(about_need >= 2 && about_need <= ALMANAC_TEXT_MAX_LINES, "about needs %d lines", about_need);
    CHECK(almanac_view_wrap_text(STR_HELP_TEXT, ALMANAC_CONTENT_UNITS, lines, 2) == 2,
          "wrap_text returns more lines than max_lines");
    const int help_lines = almanac_view_help(lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(help_lines >= 1 && help_lines <= ALMANAC_TEXT_MAX_LINES, "help lines %d", help_lines);
    for (int i = 0; i < help_lines; ++i) check_fits("help", lines[i].text, ALMANAC_CONTENT_W, 16);
    CHECK(ALMANAC_CONTENT_Y + help_lines * ALMANAC_ROW16_H <= ALMANAC_CONTENT_BOTTOM,
          "help page ends at %d", ALMANAC_CONTENT_Y + help_lines * ALMANAC_ROW16_H);

    const int about_lines = almanac_view_about(lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(about_lines >= 2 && about_lines <= ALMANAC_TEXT_MAX_LINES, "about lines %d", about_lines);
    for (int i = 0; i < about_lines; ++i) check_fits("about", lines[i].text, ALMANAC_CONTENT_W, 16);

    CHECK(ALMANAC_HINT_Y + ALMANAC_HINT_H <= ALMANAC_SCREEN_H, "hint pill overflows the screen");

    const int first_day = almanac_days_from_civil(2026, 1, 1);
    const int last_day = almanac_supported_max_day();
    for (int jdn = first_day; jdn <= first_day + 365 * 4 && jdn <= last_day; ++jdn) {
        almanac_day_t day;
        almanac_day_compute(jdn, &day);
        check_day(&day);
    }

    if (failures != 0) {
        printf("FAIL test_almanac_view (%d failures)\n", failures);
        return 1;
    }
    printf("PASS test_almanac_view\n");
    return 0;
}
