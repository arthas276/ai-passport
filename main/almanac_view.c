// main/almanac_view.c — screen text, line breaking and the layout scene of every screen ([PURE],
// host-testable).
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

// Greedy line breaking. *needed receives the number of lines the whole text takes, even when only
// the first max_lines of them fit into lines.
static int wrap_into(const char *text, int line_units, almanac_line_t *lines, int max_lines, int *needed) {
    int count = 0;
    const char *line_start = text;
    const char *previous = text;
    const char *p = text;
    int units = 0;
    while (*p != '\0') {
        if (units >= line_units) {
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
    *needed = count;
    return count > max_lines ? max_lines : count;
}

int almanac_view_wrap_text(const char *text, int units_per_line, almanac_line_t *lines, int max_lines) {
    int needed = 0;
    int count = wrap_into(text, units_per_line, lines, max_lines, &needed);
    if (needed > max_lines || count < 2 || units_per_line < 2 || utf8_units(lines[count - 1].text) > 1) return count;
    // A lone last character reads as a stray one: even the lines out with the narrowest line that
    // still takes no more than count lines.
    for (int line_units = 1; line_units < units_per_line; ++line_units) {
        const int even = wrap_into(text, line_units, lines, max_lines, &needed);
        if (needed <= count) return even;
    }
    return wrap_into(text, units_per_line, lines, max_lines, &needed);
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

int almanac_font_line_h(almanac_font_t font) {
    switch (font) {
    case ALM_FONT_20: return ALMANAC_ROW20_H;
    case ALM_FONT_48: return ALMANAC_FONT48_LINE_H;
    case ALM_FONT_MONO_48: return ALMANAC_MONO48_LINE_H;
    default: return ALMANAC_ROW16_H;
    }
}

int almanac_box_border_h(almanac_box_kind_t kind) {
    switch (kind) {
    case ALM_BOX_CARD:
    case ALM_BOX_CARD_SELECTED:
    case ALM_BOX_FOCUS:
    case ALM_BOX_SEAL: return ALMANAC_CARD_BORDER;
    default: return 0;
    }
}

int almanac_card_h(int rows) {
    return 2 * ALMANAC_CARD_PAD + rows * ALMANAC_ROW16_H;
}

// Appends a box and returns its index. A full scene answers ALMANAC_NO_BOX, and the lines meant
// for that box stay on the screen background, where the host layout test still measures them.
static int add_box(almanac_scene_t *out, int x, int y, int w, int h, almanac_box_kind_t kind) {
    if (out->n_boxes >= ALMANAC_SCENE_BOXES_MAX) return ALMANAC_NO_BOX;
    almanac_box_t *box = &out->boxes[out->n_boxes];
    box->x = (int16_t)x;
    box->y = (int16_t)y;
    box->w = (int16_t)w;
    box->h = (int16_t)h;
    box->kind = (uint8_t)kind;
    return out->n_boxes++;
}

static void add_text(almanac_scene_t *out, int box, int x, int y, int w, almanac_font_t font, almanac_ink_t ink,
                     int center, const char *text) {
    if (out->n_texts >= ALMANAC_SCENE_TEXTS_MAX) return;
    almanac_text_t *line = &out->texts[out->n_texts++];
    line->x = (int16_t)x;
    line->y = (int16_t)y;
    line->w = (int16_t)w;
    line->font = (uint8_t)font;
    line->ink = (uint8_t)ink;
    line->box = (uint8_t)box;
    line->center = (uint8_t)center;
    snprintf(line->text, sizeof(line->text), "%s", text);
}

static int card_text_x(void) {
    return ALMANAC_MARGIN_X + ALMANAC_CARD_PAD;
}

// Width of a line's window that ends at the right inset of a full-width card.
static int card_text_w(int x) {
    return ALMANAC_SCREEN_W - ALMANAC_MARGIN_X - ALMANAC_CARD_PAD - x;
}

static int add_card(almanac_scene_t *out, int y, int rows, almanac_box_kind_t kind) {
    return add_box(out, ALMANAC_MARGIN_X, y, ALMANAC_CONTENT_W, almanac_card_h(rows), kind);
}

static void add_row(almanac_scene_t *out, int box, int y, almanac_ink_t ink, const char *text) {
    add_text(out, box, card_text_x(), y, card_text_w(card_text_x()), ALM_FONT_16, ink, 0, text);
}

static void add_note(almanac_scene_t *out, int box, int y, almanac_ink_t ink, const char *text) {
    const int x = card_text_x() + ALMANAC_NOTE_INDENT;
    add_text(out, box, x, y, card_text_w(x), ALM_FONT_16, ink, 0, text);
}

static void add_centred(almanac_scene_t *out, int box, int y, almanac_font_t font, almanac_ink_t ink, int center,
                        const char *text) {
    add_text(out, box, ALMANAC_MARGIN_X, y, ALMANAC_CONTENT_W, font, ink, center, text);
}

// A tinted block of count text rows with a solid chip holding the 20 px label beside the first
// row. Returns the block's height.
static int add_list(almanac_scene_t *out, int y, almanac_box_kind_t kind, almanac_box_kind_t chip_kind,
                    const char *label, const almanac_line_t *lines, int count) {
    const int chip_y = y + ALMANAC_LIST_PAD + (ALMANAC_ROW16_H - ALMANAC_CHIP_SIZE) / 2;
    int h = 2 * ALMANAC_LIST_PAD + count * ALMANAC_ROW16_H;
    const int chip_bottom = chip_y - y + ALMANAC_CHIP_SIZE + ALMANAC_LIST_PAD;
    if (h < chip_bottom) h = chip_bottom;
    const int box = add_box(out, ALMANAC_MARGIN_X, y, ALMANAC_CONTENT_W, h, kind);
    const int chip = add_box(out, ALMANAC_CHIP_X, chip_y, ALMANAC_CHIP_SIZE, ALMANAC_CHIP_SIZE, chip_kind);
    add_text(out, chip, ALMANAC_CHIP_X, chip_y, ALMANAC_CHIP_SIZE, ALM_FONT_20, ALM_INK_NAVY, 1, label);
    for (int i = 0; i < count; ++i) {
        add_text(out, box, ALMANAC_LIST_TEXT_X, y + ALMANAC_LIST_PAD + i * ALMANAC_ROW16_H, ALMANAC_LIST_TEXT_W, ALM_FONT_16,
                 ALM_INK, 0, lines[i].text);
    }
    return h;
}

void almanac_scene_today(const almanac_view_day_t *day, const char *zhixing, bool roaming, const char *banner,
                         almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    if (banner) {
        add_centred(out, ALMANAC_NO_BOX, ALMANAC_TODAY_DATE_Y, ALM_FONT_16, ALM_INK_BAD, 1, banner);
    } else {
        add_centred(out, ALMANAC_NO_BOX, ALMANAC_TODAY_DATE_Y, ALM_FONT_16, ALM_INK, roaming ? 2 : 1, day->date.text);
        if (roaming) add_centred(out, ALMANAC_NO_BOX, ALMANAC_TODAY_DATE_Y, ALM_FONT_16, ALM_INK_GOOD, 2, STR_ROAMING);
    }
    add_centred(out, ALMANAC_NO_BOX, ALMANAC_TODAY_DATE_Y + ALMANAC_ROW16_H, ALM_FONT_16, ALM_INK_DIM, 1, day->pillars.text);

    const almanac_line_t *const facts[3] = {&day->lunar, &day->gloss, &day->day_type};
    const almanac_ink_t fact_ink[3] = {ALM_INK, ALM_INK_DIM, ALM_INK_GOOD};
    const int card = add_box(out, ALMANAC_FACTS_X, ALMANAC_SEAL_ROW_Y, ALMANAC_FACTS_W, ALMANAC_SEAL_ROW_H, ALM_BOX_CARD);
    for (int i = 0; i < 3; ++i) {
        add_text(out, card, ALMANAC_FACTS_X + ALMANAC_CARD_PAD, ALMANAC_SEAL_ROW_Y + ALMANAC_FACTS_PAD_Y + i * ALMANAC_ROW16_H,
                 ALMANAC_FACTS_W - 2 * ALMANAC_CARD_PAD, ALM_FONT_16, fact_ink[i], 0, facts[i]->text);
    }
    const int seal = add_box(out, ALMANAC_SEAL_X, ALMANAC_SEAL_Y, ALMANAC_SEAL_SIZE, ALMANAC_SEAL_SIZE, ALM_BOX_SEAL);
    add_text(out, seal, ALMANAC_SEAL_X, ALMANAC_SEAL_Y + (ALMANAC_SEAL_SIZE - ALMANAC_FONT48_LINE_H) / 2, ALMANAC_SEAL_SIZE,
             ALM_FONT_48, ALM_INK_BAD, 1, zhixing);

    const int yi_h = add_list(out, ALMANAC_LISTS_Y, ALM_BOX_GOOD, ALM_BOX_CHIP_GOOD, STR_LABEL_YI, day->yi, day->yi_lines);
    add_list(out, ALMANAC_LISTS_Y + yi_h + ALMANAC_CARD_GAP, ALM_BOX_BAD, ALM_BOX_CHIP_BAD, STR_LABEL_JI, day->ji,
             day->ji_lines);
}

void almanac_scene_detail(const almanac_view_day_t *day, bool huangdao, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    int y = ALMANAC_DETAIL_Y;
    int card = add_card(out, y, 2, ALM_BOX_CARD);
    add_row(out, card, y + ALMANAC_CARD_PAD, ALM_INK, day->chong.text);
    add_note(out, card, y + ALMANAC_CARD_PAD + ALMANAC_ROW16_H, ALM_INK_DIM, day->chong_note.text);
    y += almanac_card_h(2) + ALMANAC_CARD_GAP;
    card = add_card(out, y, 2, ALM_BOX_CARD);
    for (int i = 0; i < 2; ++i) {
        add_row(out, card, y + ALMANAC_CARD_PAD + i * ALMANAC_ROW16_H, ALM_INK, day->baiji[i].text);
    }
    y += almanac_card_h(2) + ALMANAC_CARD_GAP;
    card = add_card(out, y, 2, huangdao ? ALM_BOX_GOOD : ALM_BOX_BAD);
    add_row(out, card, y + ALMANAC_CARD_PAD, ALM_INK, day->day_type.text);
    add_note(out, card, y + ALMANAC_CARD_PAD + ALMANAC_ROW16_H, ALM_INK, day->day_type_line.text);
}

void almanac_scene_hours(const almanac_view_hours_t *hours, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    // The ink of a 16 px line stops three rows above the line's foot: one row lower reads centred.
    const int row_y = (ALMANAC_HOUR_CELL_H - 2 * ALMANAC_ROW16_H) / 2 + 1;
    for (int i = 0; i < 12; ++i) {
        const int x = ALMANAC_HOUR_GRID_X + (i % 4) * (ALMANAC_HOUR_CELL_W + ALMANAC_HOUR_GRID_GAP);
        const int y = ALMANAC_HOUR_GRID_Y + (i / 4) * (ALMANAC_HOUR_CELL_H + ALMANAC_HOUR_GRID_GAP);
        const int cell =
            add_box(out, x, y, ALMANAC_HOUR_CELL_W, ALMANAC_HOUR_CELL_H, hours->lucky[i] ? ALM_BOX_GOOD : ALM_BOX_BAD);
        add_text(out, cell, x, y + row_y, ALMANAC_HOUR_CELL_W, ALM_FONT_16, ALM_INK, 1, hours->hour[i].text);
        add_text(out, cell, x, y + row_y + ALMANAC_ROW16_H, ALMANAC_HOUR_CELL_W, ALM_FONT_16, ALM_INK, 1,
                 hours->lucky[i] ? STR_LUCKY : STR_UNLUCKY);
    }
    const int lucky_y = ALMANAC_HOUR_LIST_Y;
    int card = add_box(out, ALMANAC_MARGIN_X, lucky_y, ALMANAC_CONTENT_W, almanac_card_h(1), ALM_BOX_GOOD);
    add_row(out, card, lucky_y + ALMANAC_CARD_PAD, ALM_INK, hours->lucky_hours.text);
    const int unlucky_y = lucky_y + almanac_card_h(1) + ALMANAC_CARD_GAP;
    card = add_box(out, ALMANAC_MARGIN_X, unlucky_y, ALMANAC_CONTENT_W, almanac_card_h(1), ALM_BOX_BAD);
    add_row(out, card, unlucky_y + ALMANAC_CARD_PAD, ALM_INK, hours->unlucky_hours.text);
}

void almanac_scene_lucky(const almanac_view_lucky_t *lucky, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    const int label_x = card_text_x();
    const int top = ALMANAC_CONTENT_Y + ALMANAC_CARD_PAD;
    int card = add_card(out, ALMANAC_CONTENT_Y, ALMANAC_DIR_KIND_COUNT, ALM_BOX_GOOD);
    for (int k = 0; k < ALMANAC_DIR_KIND_COUNT; ++k) {
        add_text(out, card, label_x, top + k * ALMANAC_ROW16_H, ALMANAC_DIR_SAY_X - ALMANAC_DIR_LABEL_GAP - label_x, ALM_FONT_16,
                 ALM_INK, 0, lucky->dir[k].text);
        add_text(out, card, ALMANAC_DIR_SAY_X, top + k * ALMANAC_ROW16_H, card_text_w(ALMANAC_DIR_SAY_X), ALM_FONT_16, ALM_INK, 0,
                 lucky->dir_say[k].text);
    }
    const int items_y = ALMANAC_CONTENT_Y + almanac_card_h(ALMANAC_DIR_KIND_COUNT) + ALMANAC_CARD_GAP;
    const int items_top = items_y + ALMANAC_CARD_PAD;
    card = add_card(out, items_y, 6, ALM_BOX_CARD);
    add_row(out, card, items_top, ALM_INK, lucky->protocol.text);
    add_note(out, card, items_top + ALMANAC_ROW16_H, ALM_INK_DIM, lucky->protocol_note.text);
    add_row(out, card, items_top + 2 * ALMANAC_ROW16_H, ALM_INK, lucky->port.text);
    add_note(out, card, items_top + 3 * ALMANAC_ROW16_H, ALM_INK_DIM, lucky->port_note.text);
    add_row(out, card, items_top + 4 * ALMANAC_ROW16_H, ALM_INK, lucky->tool.text);
    add_row(out, card, items_top + 5 * ALMANAC_ROW16_H, ALM_INK, lucky->color_number.text);
}

void almanac_scene_fortune(const almanac_day_t *day, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    const int name_x = card_text_x();
    const int name_w = ALMANAC_FORTUNE_PIP_X - ALMANAC_FORTUNE_PIP_GAP - name_x;
    const int pip_y = ALMANAC_FORTUNE_PAD + (ALMANAC_ROW16_H - ALMANAC_FORTUNE_PIP_H) / 2;
    for (int i = 0; i < ALMANAC_LUCK_CATEGORY_COUNT; ++i) {
        const int y = ALMANAC_FORTUNE_Y + i * ALMANAC_FORTUNE_ROW_H;
        const int stars = day->luck[i];
        const int card = add_box(out, ALMANAC_MARGIN_X, y, ALMANAC_CONTENT_W, ALMANAC_FORTUNE_CARD_H, ALM_BOX_CARD);
        add_text(out, card, name_x, y + ALMANAC_FORTUNE_PAD, name_w, ALM_FONT_16, ALM_INK, 0, almanac_luck_name(i));
        for (int k = 0; k < 5; ++k) {
            add_box(out, ALMANAC_FORTUNE_PIP_X + k * ALMANAC_FORTUNE_PIP_PITCH, y + pip_y, ALMANAC_FORTUNE_PIP_W,
                    ALMANAC_FORTUNE_PIP_H, k < stars ? ALM_BOX_PIP_ON : ALM_BOX_PIP_OFF);
        }
        add_row(out, card, y + ALMANAC_FORTUNE_PAD + ALMANAC_ROW16_H, ALM_INK_DIM, almanac_luck_line(i, stars));
    }
}

static const char *const MENU_ITEMS[ALMANAC_MENU_ROWS] = {
    STR_MENU_CALIBRATE, STR_MENU_TODAY, STR_MENU_PICK_DATE, STR_MENU_HELP, STR_MENU_ABOUT,
};

// One selectable 20 px row: a card centred on the label row, with the label indented like the
// list text.
static void add_choice(almanac_scene_t *out, const char *text, int label_y, int card_h, bool selected) {
    const int card = add_box(out, ALMANAC_MARGIN_X, label_y - (card_h - ALMANAC_ROW20_H) / 2, ALMANAC_CONTENT_W, card_h,
                             selected ? ALM_BOX_CARD_SELECTED : ALM_BOX_CARD);
    add_text(out, card, ALMANAC_LIST_TEXT_X, label_y, card_text_w(ALMANAC_LIST_TEXT_X), ALM_FONT_20,
             selected ? ALM_INK_GOOD : ALM_INK_DIM, 0, text);
}

void almanac_scene_menu(int selected, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < ALMANAC_MENU_ROWS; ++i) {
        add_choice(out, MENU_ITEMS[i], ALMANAC_MENU_Y + i * ALMANAC_MENU_ROW_H, ALMANAC_MENU_CARD_H, i == selected);
    }
}

void almanac_scene_pick(int selected, int first_visible, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    for (int row = 0; row < ALMANAC_PICK_ROWS_VISIBLE; ++row) {
        const int matter = first_visible + row;
        if (matter >= ALMANAC_MATTER_COUNT) break;
        add_choice(out, almanac_matter(matter), ALMANAC_PICK_Y + row * ALMANAC_PICK_ROW_H, ALMANAC_PICK_CARD_H,
                   matter == selected);
    }
}

void almanac_scene_result(const char *matter, const almanac_line_t *dates, int count, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    add_centred(out, ALMANAC_NO_BOX, ALMANAC_CONTENT_Y, ALM_FONT_20, ALM_INK_BAD, 1, matter);
    if (count <= 0) return;
    const int h = 2 * ALMANAC_CARD_PAD + (count - 1) * ALMANAC_RESULT_ROW_H + ALMANAC_ROW20_H;
    const int card = add_box(out, ALMANAC_MARGIN_X, ALMANAC_RESULT_LIST_Y, ALMANAC_CONTENT_W, h, ALM_BOX_CARD);
    for (int i = 0; i < count; ++i) {
        add_centred(out, card, ALMANAC_RESULT_LIST_Y + ALMANAC_CARD_PAD + i * ALMANAC_RESULT_ROW_H, ALM_FONT_20, ALM_INK, 1,
                    dates[i].text);
    }
}

void almanac_scene_setup(int step, int year, int month, int day, almanac_scene_t *out) {
    static const char *const UNITS[3] = {STR_SETUP_YEAR, STR_SETUP_MONTH, STR_SETUP_DAY};
    const int values[3] = {year, month, day};
    memset(out, 0, sizeof(*out));
    add_centred(out, ALMANAC_NO_BOX, ALMANAC_SETUP_UNIT_Y, ALM_FONT_20, ALM_INK_DIM, 1, UNITS[step]);
    const int card = add_box(out, ALMANAC_SETUP_FOCUS_X, ALMANAC_SETUP_FOCUS_Y, ALMANAC_SETUP_FOCUS_W, ALMANAC_SETUP_FOCUS_H,
                             ALM_BOX_FOCUS);
    char value[12];
    snprintf(value, sizeof(value), "%d", values[step]);
    add_text(out, card, ALMANAC_SETUP_FOCUS_X, ALMANAC_SETUP_VALUE_Y, ALMANAC_SETUP_FOCUS_W, ALM_FONT_MONO_48, ALM_INK, 1, value);
    char picked[48];
    snprintf(picked, sizeof(picked), "%d%s%d%s%d%s", year, STR_SETUP_YEAR, month, STR_SETUP_MONTH, day, STR_SETUP_DAY);
    add_centred(out, ALMANAC_NO_BOX, ALMANAC_SETUP_DATE_Y, ALM_FONT_16, ALM_INK_DIM, 1, picked);
}

void almanac_scene_confirm(const char *date_text, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    const int card = add_box(out, ALMANAC_MARGIN_X, ALMANAC_CONFIRM_CARD_Y, ALMANAC_CONTENT_W, ALMANAC_CONFIRM_CARD_H,
                             ALM_BOX_CARD);
    add_centred(out, card, ALMANAC_CONFIRM_ROW_Y, ALM_FONT_20, ALM_INK, 1, date_text);
    add_centred(out, card, ALMANAC_CONFIRM_ROW_Y + ALMANAC_CONFIRM_ROW_H, ALM_FONT_20, ALM_INK_GOOD, 1, STR_CALIBRATE_CONFIRM);
}

void almanac_scene_help(const almanac_line_t *lines, int count, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < count; ++i) {
        add_text(out, ALMANAC_NO_BOX, ALMANAC_MARGIN_X, ALMANAC_CONTENT_Y + i * ALMANAC_ROW16_H, ALMANAC_CONTENT_W, ALM_FONT_16,
                 ALM_INK, 0, lines[i].text);
    }
}

void almanac_scene_about(const almanac_line_t *lines, int count, almanac_scene_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < count; ++i) {
        add_centred(out, ALMANAC_NO_BOX, ALMANAC_CONTENT_Y + i * ALMANAC_ROW16_H, ALM_FONT_16, i == 0 ? ALM_INK_GOOD : ALM_INK_DIM,
                    1, lines[i].text);
    }
}
