// tests/test_almanac_view.c — host test: every screen's scene stays inside the screen.
//
// Each builder's scene is checked the same way: every box inside the content band, no two boxes
// partly overlapping, and every line — measured with the real advance widths of the generated
// fonts (tests/almanac_font_metrics.h) — inside the box it is drawn into and clear of every other
// line and box. Day scenes run over every day from 2026 to 2029; the fixed screens run once for
// each state they can be left in.
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

// Montserrat 48 digits as LVGL rounds their advances (lv_font_montserrat_48.c of the pinned LVGL).
static const uint8_t k_mono48_digit_w[10] = {32, 18, 28, 27, 32, 28, 30, 29, 31, 30};

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

// The 48 px officer font holds full-width CJK glyphs only.
static int width48(const char *text) {
    int total = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; ++p) {
        if ((*p & 0xC0u) != 0x80u) total += 48;
    }
    return total;
}

// Montserrat 48 digits; anything else is wider than the screen, so the line fails its check.
static int width_mono48(const char *text) {
    int total = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; ++p) {
        if (*p < '0' || *p > '9') return ALMANAC_SCREEN_W;
        total += k_mono48_digit_w[*p - '0'];
    }
    return total;
}

static int line_width(const almanac_text_t *text) {
    switch ((almanac_font_t)text->font) {
    case ALM_FONT_20: return width(text->text, 20);
    case ALM_FONT_48: return width48(text->text);
    case ALM_FONT_MONO_48: return width_mono48(text->text);
    default: return width(text->text, 16);
    }
}

typedef struct {
    int x;
    int y;
    int w;
    int h;
} rect_t;

static bool overlaps(const rect_t *a, const rect_t *b) {
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static bool contains(const rect_t *outer, const rect_t *inner) {
    return inner->x >= outer->x && inner->y >= outer->y && inner->x + inner->w <= outer->x + outer->w
           && inner->y + inner->h <= outer->y + outer->h;
}

static rect_t box_rect(const almanac_box_t *b) {
    const rect_t r = {b->x, b->y, b->w, b->h};
    return r;
}

// Checks one line at its final x: inside its box (or the screen), and clear of every other box.
static rect_t place(const almanac_scene_t *s, int i, int x, const char *tag) {
    const almanac_text_t *text = &s->texts[i];
    const rect_t r = {x, text->y, line_width(text), almanac_font_line_h((almanac_font_t)text->font)};
    if (text->box == ALMANAC_NO_BOX) {
        CHECK(r.x >= 0 && r.x + r.w <= ALMANAC_SCREEN_W, "%s: line %d spans x %d..%d", tag, i, r.x, r.x + r.w);
        CHECK(r.y >= ALMANAC_TITLE_Y + ALMANAC_TITLE_H && r.y + r.h <= ALMANAC_CONTENT_BOTTOM, "%s: line %d spans y %d..%d",
              tag, i, r.y, r.y + r.h);
    } else {
        CHECK(text->box < s->n_boxes, "%s: line %d names box %u, which is not in the scene", tag, i, text->box);
        if (text->box < s->n_boxes) {
            const almanac_box_t *b = &s->boxes[text->box];
            const int inset = almanac_box_border_h((almanac_box_kind_t)b->kind);
            const rect_t inner = {b->x + inset, b->y + inset, b->w - 2 * inset, b->h - 2 * inset};
            CHECK(contains(&inner, &r), "%s: line %d at (%d,%d) %dx%d leaves its box at (%d,%d) %dx%d: \"%s\"", tag, i, r.x,
                  r.y, r.w, r.h, inner.x, inner.y, inner.w, inner.h, text->text);
        }
    }
    rect_t own = {0, 0, 0, 0};
    if (text->box != ALMANAC_NO_BOX && text->box < s->n_boxes) own = box_rect(&s->boxes[text->box]);
    for (int k = 0; k < s->n_boxes; ++k) {
        if (k == text->box) continue;
        const rect_t o = box_rect(&s->boxes[k]);
        // A box drawn around the line's own box (a chip inside its block) is meant to be under it.
        const bool around_own_box = text->box != ALMANAC_NO_BOX && contains(&o, &own);
        CHECK(around_own_box || !overlaps(&r, &o), "%s: line %d at (%d,%d) %dx%d runs into box %d: \"%s\"", tag, i, r.x,
              r.y, r.w, r.h, k, text->text);
    }
    return r;
}

static void check_scene(const almanac_scene_t *s, const char *tag) {
    CHECK(s->n_boxes < ALMANAC_SCENE_BOXES_MAX, "%s: %u boxes reach the scene limit", tag, s->n_boxes);
    CHECK(s->n_texts < ALMANAC_SCENE_TEXTS_MAX, "%s: %u lines reach the scene limit", tag, s->n_texts);
    for (int i = 0; i < s->n_boxes; ++i) {
        const rect_t r = box_rect(&s->boxes[i]);
        CHECK(r.w > 0 && r.h > 0, "%s: box %d is %dx%d", tag, i, r.w, r.h);
        CHECK(r.x >= 0 && r.x + r.w <= ALMANAC_SCREEN_W, "%s: box %d spans x %d..%d", tag, i, r.x, r.x + r.w);
        CHECK(r.y >= ALMANAC_TITLE_Y + ALMANAC_TITLE_H && r.y + r.h <= ALMANAC_CONTENT_BOTTOM,
              "%s: box %d spans y %d..%d", tag, i, r.y, r.y + r.h);
        for (int k = i + 1; k < s->n_boxes; ++k) {
            const rect_t o = box_rect(&s->boxes[k]);
            CHECK(!overlaps(&r, &o) || contains(&r, &o) || contains(&o, &r), "%s: boxes %d and %d partly overlap", tag, i,
                  k);
        }
    }
    rect_t placed[ALMANAC_SCENE_TEXTS_MAX];
    int i = 0;
    while (i < s->n_texts) {
        const almanac_text_t *text = &s->texts[i];
        const int w = line_width(text);
        CHECK(w <= text->w, "%s: line %d is %d px wide in a %d px window: \"%s\"", tag, i, w, text->w, text->text);
        int x = text->x;
        if (text->center == 2) {
            int group_w = 0;
            int k = i;
            for (; k < s->n_texts && s->texts[k].center == 2; ++k) {
                group_w += line_width(&s->texts[k]) + (k > i ? ALMANAC_ROAM_GAP : 0);
            }
            x += (text->w - group_w) / 2;
            for (; i < k; ++i) {
                placed[i] = place(s, i, x, tag);
                x += line_width(&s->texts[i]) + ALMANAC_ROAM_GAP;
            }
            continue;
        }
        if (text->center == 1) x += (text->w - w) / 2;
        placed[i] = place(s, i, x, tag);
        ++i;
    }
    for (i = 0; i < s->n_texts; ++i) {
        for (int k = i + 1; k < s->n_texts; ++k) {
            CHECK(!overlaps(&placed[i], &placed[k]), "%s: lines %d and %d overlap: \"%s\" / \"%s\"", tag, i, k,
                  s->texts[i].text, s->texts[k].text);
        }
    }
}

static void check_day(const almanac_day_t *day) {
    char tag[48];
    snprintf(tag, sizeof(tag), "day %d", day->jdn);
    almanac_view_day_t v;
    almanac_view_day(day, &v);
    CHECK(v.yi_lines >= 1 && v.yi_lines <= ALMANAC_LIST_MAX_LINES, "%s: yi lines %d", tag, v.yi_lines);
    CHECK(v.ji_lines >= 1 && v.ji_lines <= ALMANAC_LIST_MAX_LINES, "%s: ji lines %d", tag, v.ji_lines);

    almanac_view_hours_t h;
    almanac_view_hours(day, &h);
    int lucky_count = 0;
    for (int i = 0; i < 12; ++i) {
        if (h.lucky[i]) ++lucky_count;
    }
    CHECK(lucky_count == 6, "%s: %d lucky hours", tag, lucky_count);

    almanac_view_lucky_t l;
    almanac_view_lucky(day, &l);
    almanac_scene_t s;
    const char *zhixing = almanac_zhixing_name(day->zhixing);
    almanac_scene_today(&v, zhixing, false, NULL, &s);
    check_scene(&s, tag);
    almanac_scene_today(&v, zhixing, true, NULL, &s);
    check_scene(&s, tag);
    almanac_scene_today(&v, zhixing, false, STR_DATE_LOST, &s);
    check_scene(&s, tag);
    almanac_scene_detail(&v, day->huangdao, &s);
    check_scene(&s, tag);
    almanac_scene_hours(&h, &s);
    check_scene(&s, tag);
    almanac_scene_lucky(&l, &s);
    check_scene(&s, tag);
    almanac_scene_fortune(day, &s);
    check_scene(&s, tag);
}

static int utf8_units(const char *s) {
    int units = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p != 0; ++p) {
        if ((*p & 0xC0u) != 0x80u) ++units;
    }
    return units;
}

static void test_line_breaking(void) {
    almanac_line_t lines[ALMANAC_TEXT_MAX_LINES + 1];
    // 15 units: a 14 + 1 break would leave one character alone, so the lines come out 8 + 7.
    const int fifteen = almanac_view_wrap_text("一二三四五六七八九十甲乙丙丁戊", 14, lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(fifteen == 2 && utf8_units(lines[0].text) == 8 && utf8_units(lines[1].text) == 7,
          "15 units break into lines of %d", fifteen);
    // 29 units at 14: 14 + 14 + 1 becomes 10 + 10 + 9.
    const int twentynine =
        almanac_view_wrap_text("一二三四五六七八九十甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申", 14, lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(twentynine == 3 && utf8_units(lines[0].text) == 10 && utf8_units(lines[1].text) == 10
              && utf8_units(lines[2].text) == 9,
          "29 units break into lines of %d", twentynine);
    // A closing mark that would open the second line pulls the character before it along.
    const int carried = almanac_view_wrap_text("一二三四五六七八、九十甲乙丙丁", 14, lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(carried == 2 && utf8_units(lines[0].text) == 7 && utf8_units(lines[1].text) == 8
              && strncmp(lines[1].text, "八、", 6) == 0,
          "15 units with a mark at 9 break into lines of %d", carried);
    // 18 units in lines of 14: 14 + 4 is fine as it is.
    const int eighteen = almanac_view_wrap_text("一二三四五六七八九十甲乙丙丁戊己庚辛", 14, lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(eighteen == 2 && utf8_units(lines[0].text) == 14 && utf8_units(lines[1].text) == 4,
          "18 units break into lines of %d", eighteen);
    // Evening the lines must not cost a line: 10 units in lines of 3 with room for 4 stay 3 + 3 + 3 + 1.
    const int narrow = almanac_view_wrap_text("一二三四五六七八九十", 3, lines, 4);
    CHECK(narrow == 4 && utf8_units(lines[0].text) == 3 && utf8_units(lines[3].text) == 1,
          "10 units in lines of 3 break into %d", narrow);
    CHECK(almanac_view_wrap_text("一二三四五六七八九十", 3, lines, 2) == 2, "max_lines caps the line count");
}

int main(void) {
    almanac_scene_t s;
    almanac_line_t lines[ALMANAC_TEXT_MAX_LINES + 1];

    const char *const hints[] = {
        STR_HINT_TODAY, STR_HINT_NEXT, STR_HINT_PICK, STR_HINT_MENU, STR_HINT_LONG_MENU, STR_HINT_LONG_BACK,
        STR_HINT_LONG_CANCEL, STR_HINT_BACK, STR_HINT_OK, STR_HINT_REPICK,
    };
    for (unsigned i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i) {
        const int w = width(hints[i], 16);
        CHECK(w <= 204, "hint is %d px wide: \"%s\"", w, hints[i]);
    }
    const char *const titles[] = {
        STR_APP_TITLE, STR_PAGE_DETAIL, STR_PAGE_HOURS, STR_PAGE_DIRS, STR_PAGE_LUCK,
        STR_PAGE_PICK, STR_PAGE_HELP, STR_PAGE_ABOUT, STR_SETUP_TITLE, STR_CALIBRATE_CONFIRM,
    };
    for (unsigned i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        const int w = width(titles[i], 20);
        CHECK(w <= ALMANAC_TITLE_MAX_W, "title is %d px wide: \"%s\"", w, titles[i]);
    }
    // The look-alike U+7141 has been typed for 煞 (U+715E) in this tree before, so the label is checked by bytes.
    CHECK(strcmp(STR_PREFIX_SHA, "\xE7\x85\x9E\xEF\xBC\x9A") == 0, "the 煞 label must be U+715E, not U+7141");

    for (int sel = 0; sel < ALMANAC_MENU_ROWS; ++sel) {
        almanac_scene_menu(sel, &s);
        check_scene(&s, "menu");
    }
    const int picks[][2] = {{0, 0}, {5, 0}, {6, 6}, {11, 5}};
    for (unsigned i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i) {
        almanac_scene_pick(picks[i][0], picks[i][1], &s);
        check_scene(&s, "matter list");
    }
    int days[ALMANAC_LUCKY_DATE_MAX];
    int gods[ALMANAC_LUCKY_DATE_MAX];
    almanac_line_t dates[ALMANAC_LUCKY_DATE_MAX];
    for (int matter = 0; matter < ALMANAC_MATTER_COUNT; ++matter) {
        for (int start = 0; start < 4; ++start) {
            const int count = almanac_lucky_dates(almanac_days_from_civil(2026, 11, 25) + 9 * start, matter, days, gods,
                                                  ALMANAC_LUCKY_DATE_MAX);
            for (int i = 0; i < count; ++i) almanac_view_lucky_date(days[i], gods[i], &dates[i]);
            almanac_scene_result(almanac_matter(matter), dates, count, &s);
            check_scene(&s, "lucky dates");
        }
    }
    // The year 2040 is the widest the wheel offers: its digits are the four widest of Montserrat 48.
    const int values[][3] = {{2020, 1, 1}, {2040, 12, 31}, {2100, 6, 15}};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (int step = 0; step < 3; ++step) {
            almanac_scene_setup(step, values[i][0], values[i][1], values[i][2], &s);
            check_scene(&s, "date setup");
        }
    }
    almanac_scene_confirm("2026" STR_SETUP_YEAR "12" STR_SETUP_MONTH "31" STR_SETUP_DAY, &s);
    check_scene(&s, "confirm");
    CHECK(almanac_view_wrap_text(STR_HELP_TEXT, ALMANAC_CONTENT_UNITS, lines, 2) == 2,
          "wrap_text returns more lines than max_lines");

    const int help_lines = almanac_view_help(lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(help_lines >= 1 && help_lines <= ALMANAC_TEXT_MAX_LINES, "help needs %d lines", help_lines);
    almanac_scene_help(lines, help_lines, &s);
    check_scene(&s, "help");
    const int about_lines = almanac_view_about(lines, ALMANAC_TEXT_MAX_LINES);
    CHECK(about_lines >= 2 && about_lines <= ALMANAC_TEXT_MAX_LINES, "about needs %d lines", about_lines);
    for (int i = 0; i < about_lines; ++i) {
        CHECK(utf8_units(lines[i].text) >= 2, "about line %d is a lone character: \"%s\"", i, lines[i].text);
    }
    almanac_scene_about(lines, about_lines, &s);
    check_scene(&s, "about");

    CHECK(ALMANAC_HINT_Y + ALMANAC_HINT_H <= ALMANAC_SCREEN_H, "hint pill overflows the screen");

    test_line_breaking();

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
