// main/almanac_view.c — screen geometry and the text of every screen line ([PURE], host-testable).
#include "almanac_view.h"

#include <stdio.h>
#include <string.h>

#include "almanac_strings.h"

static int utf8_len(const char *s) {
    const unsigned char lead = (unsigned char)s[0];
    if (lead < 0x80u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    return 4;
}

static int utf8_units(const char *s) {
    int units = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p != 0; ++p) {
        if ((*p & 0xC0u) != 0x80u) ++units;
    }
    return units;
}

static uint32_t utf8_first(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80u) return p[0];
    if ((p[0] & 0xE0u) == 0xC0u) return ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if ((p[0] & 0xF0u) == 0xE0u) return ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    return ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) | ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
}

static bool is_closing_punctuation(uint32_t codepoint) {
    switch (codepoint) {
    case 0x3001: // 、
    case 0x3002: // 。
    case 0x300D: // 」
    case 0xFF01: // ！
    case 0xFF09: // ）
    case 0xFF0C: // ，
    case 0xFF1A: // ：
    case 0xFF1B: // ；
        return true;
    default:
        return false;
    }
}

static void store_line(almanac_line_t *line, const char *start, const char *end) {
    size_t length = (size_t)(end - start);
    if (length > ALMANAC_LINE_BYTES - 1) length = ALMANAC_LINE_BYTES - 1;
    memcpy(line->text, start, length);
    line->text[length] = '\0';
}

int almanac_view_wrap_text(const char *text, int units_per_line, almanac_line_t *lines, int max_lines) {
    int count = 0;
    const char *line_start = text;
    const char *previous = text;
    const char *p = text;
    int units = 0;
    while (*p != '\0') {
        if (units >= units_per_line) {
            const char *cut = p;
            if (is_closing_punctuation(utf8_first(p)) && units >= 2) cut = previous;
            if (count < max_lines) store_line(&lines[count], line_start, cut);
            ++count;
            line_start = cut;
            units = cut < p ? 1 : 0;
        }
        previous = p;
        ++units;
        p += utf8_len(p);
    }
    if (*line_start != '\0' || count == 0) {
        if (count < max_lines) store_line(&lines[count], line_start, p);
        ++count;
    }
    return count > max_lines ? max_lines : count;
}

int almanac_view_wrap_items(const almanac_day_t *day, almanac_side_t side, almanac_line_t *lines, int max_lines) {
    int count = 0;
    int units = ALMANAC_BLOCK_UNITS + 1; // the first item always opens a new line
    for (int k = 0; k < ALMANAC_PICKS_PER_SIDE; ++k) {
        const int item = side == ALMANAC_YI ? day->yi[k] : day->ji[k];
        const char *text = almanac_item_text(side, item);
        const bool last = k + 1 == ALMANAC_PICKS_PER_SIDE;
        const int item_units = utf8_units(text) + (last ? 0 : 1);
        if (units + item_units > ALMANAC_BLOCK_UNITS) {
            if (count >= max_lines) break;
            lines[count].text[0] = '\0';
            ++count;
            units = 0;
        }
        const size_t used = strlen(lines[count - 1].text);
        snprintf(lines[count - 1].text + used, ALMANAC_LINE_BYTES - used, "%s%s", text, last ? "" : STR_LIST_SEPARATOR);
        units += item_units;
    }
    return count;
}

void almanac_view_day(const almanac_day_t *day, almanac_view_day_t *out) {
    memset(out, 0, sizeof(*out));
    almanac_ymd_t ymd;
    almanac_civil_from_days(day->jdn, &ymd);
    const almanac_pillars_t *p = &day->pillars;
    snprintf(out->date.text, sizeof(out->date.text), "%d%s%d%s%d%s %s", ymd.year, STR_SETUP_YEAR, ymd.month,
             STR_SETUP_MONTH, ymd.day, STR_SETUP_DAY, almanac_weekday_name(day->weekday));
    snprintf(out->pillars.text, sizeof(out->pillars.text), "%s%s%s %s%s%s %s%s%s", almanac_stem(p->year_stem),
             almanac_branch(p->year_branch), STR_SETUP_YEAR, almanac_stem(p->month_stem), almanac_branch(p->month_branch),
             STR_SETUP_MONTH, almanac_stem(p->day_stem), almanac_branch(p->day_branch), STR_SETUP_DAY);
    snprintf(out->lunar.text, sizeof(out->lunar.text), "%s%s%s%s", STR_PREFIX_LUNAR, day->lunar.leap ? STR_PREFIX_LEAP : "",
             almanac_lunar_month(day->lunar.month), almanac_lunar_day(day->lunar.day));
    snprintf(out->gloss.text, sizeof(out->gloss.text), "%s", almanac_zhixing_gloss(day->zhixing));
    snprintf(out->day_type.text, sizeof(out->day_type.text), "%s%s%s", almanac_tianshen_name(day->tianshen), STR_SEPARATOR,
             day->huangdao ? STR_HUANGDAO_DAY : STR_HEIDAO_DAY);
    snprintf(out->day_type_line.text, sizeof(out->day_type_line.text), "%s", almanac_day_type_line(day->huangdao));
    snprintf(out->chong.text, sizeof(out->chong.text), "%s%s%s%s%s", STR_PREFIX_CHONG, almanac_chong_object(day->chong),
             STR_SEPARATOR, STR_PREFIX_SHA, almanac_sha_place(day->sha));
    snprintf(out->chong_note.text, sizeof(out->chong_note.text), "%s", almanac_chong_note(day->chong));
    for (int i = 0; i < 2; ++i) {
        snprintf(out->baiji[i].text, sizeof(out->baiji[i].text), "%s", almanac_baiji_line(day->baiji[i]));
    }
    out->yi_lines = almanac_view_wrap_items(day, ALMANAC_YI, out->yi, ALMANAC_LIST_MAX_LINES);
    out->ji_lines = almanac_view_wrap_items(day, ALMANAC_JI, out->ji, ALMANAC_LIST_MAX_LINES);
}

void almanac_view_lucky(const almanac_day_t *day, almanac_view_lucky_t *out) {
    memset(out, 0, sizeof(*out));
    for (int kind = 0; kind < ALMANAC_DIR_KIND_COUNT; ++kind) {
        snprintf(out->dir[kind].text, sizeof(out->dir[kind].text), "%s %s", almanac_dir_label(kind),
                 almanac_compass(day->dirs[kind]));
        snprintf(out->dir_say[kind].text, sizeof(out->dir_say[kind].text), "%s", almanac_dir_say(kind));
    }
    snprintf(out->protocol.text, sizeof(out->protocol.text), "%s %s", STR_LABEL_PROTOCOL,
             almanac_protocol_name(day->protocol));
    snprintf(out->protocol_note.text, sizeof(out->protocol_note.text), "%s", almanac_protocol_note(day->protocol));
    snprintf(out->port.text, sizeof(out->port.text), "%s %s", STR_LABEL_PORT, almanac_port_number(day->port));
    snprintf(out->port_note.text, sizeof(out->port_note.text), "%s", almanac_port_note(day->port));
    snprintf(out->tool.text, sizeof(out->tool.text), "%s %s", STR_LABEL_TOOL, almanac_tool(day->tool));
    snprintf(out->color_number.text, sizeof(out->color_number.text), "%s %s%s%s %s", STR_LABEL_COLOR,
             almanac_color(day->color), STR_SEPARATOR, STR_LABEL_NUMBER, almanac_number(day->number));
}

void almanac_view_hours(const almanac_day_t *day, almanac_view_hours_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->lucky_hours.text, sizeof(out->lucky_hours.text), "%s", STR_LABEL_LUCKY_HOURS);
    snprintf(out->unlucky_hours.text, sizeof(out->unlucky_hours.text), "%s", STR_LABEL_UNLUCKY_HOURS);
    size_t lucky_used = strlen(out->lucky_hours.text);
    size_t unlucky_used = strlen(out->unlucky_hours.text);
    bool lucky_first = true;
    bool unlucky_first = true;
    for (int hour = 0; hour < 12; ++hour) {
        snprintf(out->hour[hour].text, sizeof(out->hour[hour].text), "%s%s", almanac_branch(hour), STR_UNIT_HOUR);
        out->lucky[hour] = ((day->hour_luck >> hour) & 1u) != 0;
        if (out->lucky[hour]) {
            lucky_used += (size_t)snprintf(out->lucky_hours.text + lucky_used, sizeof(out->lucky_hours.text) - lucky_used,
                                           "%s%s", lucky_first ? "" : " ", almanac_branch(hour));
            lucky_first = false;
        } else {
            unlucky_used += (size_t)snprintf(out->unlucky_hours.text + unlucky_used,
                                             sizeof(out->unlucky_hours.text) - unlucky_used, "%s%s",
                                             unlucky_first ? "" : " ", almanac_branch(hour));
            unlucky_first = false;
        }
    }
}

void almanac_view_lucky_date(int jdn, int god, almanac_line_t *out) {
    almanac_ymd_t ymd;
    almanac_civil_from_days(jdn, &ymd);
    snprintf(out->text, sizeof(out->text), "%d%s%d%s %s%s", ymd.month, STR_SETUP_MONTH, ymd.day, STR_SETUP_DAY,
             almanac_zhixing_name(god), STR_SETUP_DAY);
}

int almanac_view_help(almanac_line_t *lines, int max_lines) {
    return almanac_view_wrap_text(STR_HELP_TEXT, ALMANAC_CONTENT_UNITS, lines, max_lines);
}

int almanac_view_about(almanac_line_t *lines, int max_lines) {
    const char *const parts[3] = {STR_ABOUT_LINE_1, STR_ABOUT_LINE_2, STR_ABOUT_LINE_3};
    int count = 0;
    for (int i = 0; i < 3 && count < max_lines; ++i) {
        count += almanac_view_wrap_text(parts[i], ALMANAC_CONTENT_UNITS, &lines[count], max_lines - count);
    }
    return count;
}
