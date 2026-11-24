// main/almanac_view.h — screen geometry and the text of every screen line ([PURE], host-testable).
//
// The UI places labels from these constants and shows only the lines built here, so the host
// layout test can measure every line that can ever appear with the real glyph widths.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "almanac_pick.h"

#define ALMANAC_SCREEN_W 240
#define ALMANAC_SCREEN_H 320

// Vertical bands; every screen is a title row, content, then a two-row hint pill.
#define ALMANAC_TITLE_Y 10
#define ALMANAC_TITLE_H 22
#define ALMANAC_TITLE_MAX_W 140 // keeps a centred title clear of the battery label
#define ALMANAC_CONTENT_Y 38
#define ALMANAC_CONTENT_BOTTOM 262 // exclusive: content must end above the hint pill
#define ALMANAC_HINT_Y 268
#define ALMANAC_HINT_H 44
#define ALMANAC_ROW16_H 18
#define ALMANAC_ROW20_H 22

#define ALMANAC_MARGIN_X 8
#define ALMANAC_CONTENT_W (ALMANAC_SCREEN_W - 2 * ALMANAC_MARGIN_X)
// Labelled block: a one- or two-character label at the left, text in the remaining column.
#define ALMANAC_BLOCK_LABEL_W 28
#define ALMANAC_BLOCK_X (ALMANAC_MARGIN_X + ALMANAC_BLOCK_LABEL_W)
#define ALMANAC_BLOCK_W (ALMANAC_SCREEN_W - ALMANAC_BLOCK_X - ALMANAC_MARGIN_X)
// Menu and matter list: rows of 20 px text, starting at the block column.
#define ALMANAC_MENU_ROWS 5
#define ALMANAC_MENU_Y 56
#define ALMANAC_MENU_ROW_H 32
#define ALMANAC_PICK_ROWS_VISIBLE 7
#define ALMANAC_PICK_Y 40
#define ALMANAC_PICK_ROW_H 28
// The widest character of almanac_font_16 is 16 px, so 12 characters never exceed 192 px.
#define ALMANAC_BLOCK_UNITS 12
#define ALMANAC_CONTENT_UNITS 14 // 14 × 16 = 224 = ALMANAC_CONTENT_W
#define ALMANAC_LIST_MAX_LINES 3
#define ALMANAC_TEXT_MAX_LINES 6
// Today card: seal box on the left, three rows of 16 px text on the right.
#define ALMANAC_SEAL_X 8
#define ALMANAC_SEAL_Y 76
#define ALMANAC_SEAL_SIZE 56
#define ALMANAC_SEAL_TEXT_X 76
#define ALMANAC_SEAL_TEXT_W (ALMANAC_SCREEN_W - ALMANAC_SEAL_TEXT_X - ALMANAC_MARGIN_X)
#define ALMANAC_LISTS_Y 138
#define ALMANAC_BLOCK_GAP 4
// Hours grid: 4 columns × 3 rows.
#define ALMANAC_HOUR_CELL_W 54
#define ALMANAC_HOUR_CELL_H 48
#define ALMANAC_HOUR_GRID_X 8
#define ALMANAC_HOUR_GRID_Y 40
// Roaming tag: this far right of the date; date and tag are centred as one row.
#define ALMANAC_ROAM_GAP 6
// Fortune: a name row with its star bar and a comment row under it, one pair per category. The
// 40 px pitch keeps a comment nearer its own name than the next category's name.
#define ALMANAC_FORTUNE_Y 40
#define ALMANAC_FORTUNE_ROW_H 40
// Lucky dates: the matter name, then the dates under it.
#define ALMANAC_RESULT_LIST_Y (ALMANAC_CONTENT_Y + 36)
#define ALMANAC_RESULT_ROW_H (ALMANAC_ROW20_H + 6)
// Directions page: the IT remarks start past the widest direction label, never under it.
#define ALMANAC_DIR_SAY_X 84
#define ALMANAC_DIR_SAY_GAP 6

#define ALMANAC_LINE_BYTES 64

typedef struct {
    char text[ALMANAC_LINE_BYTES];
} almanac_line_t;

typedef struct {
    almanac_line_t date; // 2026年11月23日 周一
    almanac_line_t pillars; // 丙午年 己亥月 辛丑日
    almanac_line_t lunar; // 农历十月十五
    almanac_line_t gloss; // 宜入库盘点
    almanac_line_t day_type; // 玉堂 · 黄道日
    almanac_line_t day_type_line; // 难的事往前推一步
    almanac_line_t chong; // 冲：替罪羊 · 煞：东列机柜
    almanac_line_t chong_note; // 别让他背锅
    almanac_line_t baiji[2];
    almanac_line_t yi[ALMANAC_LIST_MAX_LINES];
    almanac_line_t ji[ALMANAC_LIST_MAX_LINES];
    int yi_lines;
    int ji_lines;
} almanac_view_day_t;

typedef struct {
    almanac_line_t dir[4]; // 预算 正东
    almanac_line_t dir_say[4]; // 朝这边提钱的事
    almanac_line_t protocol; // 幸运协议 SSH
    almanac_line_t protocol_note;
    almanac_line_t port; // 幸运端口 8443
    almanac_line_t port_note;
    almanac_line_t tool; // 幸运工具 贴满贴纸的笔记本
    almanac_line_t color_number; // 幸运色 白蓝 · 幸运数字 65535
} almanac_view_lucky_t;

typedef struct {
    almanac_line_t hour[12]; // 子时 .. 亥时
    bool lucky[12];
    almanac_line_t lucky_hours; // 吉时：寅 卯 巳 申 戌 亥
    almanac_line_t unlucky_hours; // 凶时：子 丑 辰 午 未 酉
} almanac_view_hours_t;

void almanac_view_day(const almanac_day_t *day, almanac_view_day_t *out);
void almanac_view_lucky(const almanac_day_t *day, almanac_view_lucky_t *out);
void almanac_view_hours(const almanac_day_t *day, almanac_view_hours_t *out);
// 11月27日 破日
void almanac_view_lucky_date(int jdn, int god, almanac_line_t *out);
int almanac_view_help(almanac_line_t *lines, int max_lines);
int almanac_view_about(almanac_line_t *lines, int max_lines);

// Breaks text into lines of at most units_per_line characters, never starting a line with
// closing punctuation. Returns the number of lines written (at most max_lines).
int almanac_view_wrap_text(const char *text, int units_per_line, almanac_line_t *lines, int max_lines);
// Lays the day's items out as "A、B、C" lines without splitting an item.
int almanac_view_wrap_items(const almanac_day_t *day, almanac_side_t side, almanac_line_t *lines, int max_lines);
