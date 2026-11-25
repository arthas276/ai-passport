// main/almanac_view.h — screen geometry, the text of every screen line and the layout scene of
// every screen ([PURE], host-testable).
//
// almanac_view.c lays each screen out as a scene — boxes, then every line with the window it is
// drawn into — and almanac_ui.c only draws that scene. Nothing is placed by the UI, so the host
// layout test can measure every line that can ever appear, with the real glyph widths, against
// the box it is drawn into.
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
#define ALMANAC_FONT48_LINE_H 47
#define ALMANAC_MONO48_LINE_H 52

#define ALMANAC_MARGIN_X 8
#define ALMANAC_CONTENT_W (ALMANAC_SCREEN_W - 2 * ALMANAC_MARGIN_X)
// A card is a filled box: its text sits this far inside, and stacked cards keep this gap. Cards
// are outlined — neutral ones in the track colour, the selection and the focused value in the
// accent, the seal in cinnabar — while the tinted blocks read by their fill alone.
#define ALMANAC_CARD_PAD 4
#define ALMANAC_CARD_GAP 4
#define ALMANAC_CARD_BORDER 2
#define ALMANAC_CARD_RADIUS 6
// A note line starts this far right of the "label: value" line above it.
#define ALMANAC_NOTE_INDENT 28
// The widest character of almanac_font_16 is 16 px, so 12 characters never exceed 192 px.
#define ALMANAC_BLOCK_UNITS 12
#define ALMANAC_CONTENT_UNITS 14 // 14 × 16 = 224 = ALMANAC_CONTENT_W
#define ALMANAC_LIST_MAX_LINES 3
#define ALMANAC_TEXT_MAX_LINES 6

// Today card: date and pillars, then the officer seal beside a card of three fact rows.
#define ALMANAC_TODAY_DATE_Y 36
#define ALMANAC_SEAL_ROW_Y 72
#define ALMANAC_SEAL_ROW_H 60
#define ALMANAC_SEAL_SIZE 56
#define ALMANAC_SEAL_X ALMANAC_MARGIN_X
#define ALMANAC_SEAL_Y (ALMANAC_SEAL_ROW_Y + (ALMANAC_SEAL_ROW_H - ALMANAC_SEAL_SIZE) / 2)
#define ALMANAC_FACTS_X 72
#define ALMANAC_FACTS_W (ALMANAC_SCREEN_W - ALMANAC_FACTS_X - ALMANAC_MARGIN_X)
#define ALMANAC_FACTS_PAD_Y 3
// Encouraged / avoided lists: a tinted block whose solid chip label stands left of the text.
#define ALMANAC_LISTS_Y 136
#define ALMANAC_LIST_PAD 3
#define ALMANAC_LIST_TEXT_X 36
#define ALMANAC_LIST_TEXT_W (ALMANAC_SCREEN_W - ALMANAC_MARGIN_X - ALMANAC_LIST_PAD - ALMANAC_LIST_TEXT_X)
#define ALMANAC_CHIP_SIZE 22
#define ALMANAC_CHIP_X 11
#define ALMANAC_CHIP_RADIUS 5
// Roaming tag: this far right of the date; date and tag are centred as one row.
#define ALMANAC_ROAM_GAP 6

// Detail: three two-line cards.
#define ALMANAC_DETAIL_Y ALMANAC_CONTENT_Y

// Hours grid: 4 columns × 3 rows of tinted tiles, then a strip per list.
#define ALMANAC_HOUR_CELL_W 54
#define ALMANAC_HOUR_CELL_H 48
#define ALMANAC_HOUR_GRID_X ALMANAC_MARGIN_X
#define ALMANAC_HOUR_GRID_Y 40
#define ALMANAC_HOUR_GRID_GAP 2
#define ALMANAC_HOUR_LIST_Y (ALMANAC_HOUR_GRID_Y + 3 * (ALMANAC_HOUR_CELL_H + ALMANAC_HOUR_GRID_GAP) + 6)

// Directions page: the IT remarks start past the widest direction label, never under it.
#define ALMANAC_DIR_SAY_X 88
#define ALMANAC_DIR_LABEL_GAP 6

// Fortune: one card per category, a name row with its star bar and a comment row under it.
#define ALMANAC_FORTUNE_Y ALMANAC_CONTENT_Y
#define ALMANAC_FORTUNE_ROW_H 44
#define ALMANAC_FORTUNE_CARD_H 42
#define ALMANAC_FORTUNE_PAD 3
#define ALMANAC_FORTUNE_PIP_X 112
#define ALMANAC_FORTUNE_PIP_PITCH 22
#define ALMANAC_FORTUNE_PIP_W 20
#define ALMANAC_FORTUNE_PIP_H 12
#define ALMANAC_FORTUNE_PIP_GAP 6 // between the name and its star bar

// Menu and matter list: rows of 20 px text, each row a card.
#define ALMANAC_MENU_ROWS 5
#define ALMANAC_MENU_Y 56 // first label row
#define ALMANAC_MENU_ROW_H 32
#define ALMANAC_MENU_CARD_H 30
#define ALMANAC_PICK_ROWS_VISIBLE 7
#define ALMANAC_PICK_Y 41 // first label row
#define ALMANAC_PICK_ROW_H 30
#define ALMANAC_PICK_CARD_H 28

// Lucky dates: the matter name, then a card of the dates under it.
#define ALMANAC_RESULT_LIST_Y (ALMANAC_CONTENT_Y + ALMANAC_ROW20_H + ALMANAC_CARD_GAP)
#define ALMANAC_RESULT_ROW_H (ALMANAC_ROW20_H + 6)

// Date setup: the unit, the value under the wheel in a focused card, then the picked date.
#define ALMANAC_SETUP_UNIT_Y 78
#define ALMANAC_SETUP_FOCUS_X 44
#define ALMANAC_SETUP_FOCUS_Y 104
#define ALMANAC_SETUP_FOCUS_W 152
#define ALMANAC_SETUP_FOCUS_H 68
#define ALMANAC_SETUP_VALUE_Y (ALMANAC_SETUP_FOCUS_Y + 8)
#define ALMANAC_SETUP_DATE_Y 176

// Calibrate confirm: one card holding the date and the question.
#define ALMANAC_CONFIRM_CARD_Y 90
#define ALMANAC_CONFIRM_CARD_H 74
#define ALMANAC_CONFIRM_ROW_Y (ALMANAC_CONFIRM_CARD_Y + 6)
#define ALMANAC_CONFIRM_ROW_H 40

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
// closing punctuation and never leaving a single character alone on the last line. Returns the
// number of lines written (at most max_lines).
int almanac_view_wrap_text(const char *text, int units_per_line, almanac_line_t *lines, int max_lines);
// Lays the day's items out as "A、B、C" lines without splitting an item.
int almanac_view_wrap_items(const almanac_day_t *day, almanac_side_t side, almanac_line_t *lines, int max_lines);

// A scene is one screen's content: boxes first, then every line with the window it is drawn into.
#define ALMANAC_SCENE_BOXES_MAX 32
#define ALMANAC_SCENE_TEXTS_MAX 32
#define ALMANAC_NO_BOX 0xFF

typedef enum {
    ALM_BOX_CARD = 0, // neutral region
    ALM_BOX_CARD_SELECTED, // the menu or matter row under the cursor
    ALM_BOX_FOCUS, // the value the date wheel is editing
    ALM_BOX_GOOD, // encouraged, lucky, 黄道
    ALM_BOX_BAD, // avoided, unlucky, 黑道
    ALM_BOX_SEAL, // officer seal
    ALM_BOX_CHIP_GOOD,
    ALM_BOX_CHIP_BAD,
    ALM_BOX_PIP_ON,
    ALM_BOX_PIP_OFF,
} almanac_box_kind_t;

typedef enum { ALM_FONT_16 = 0, ALM_FONT_20, ALM_FONT_48, ALM_FONT_MONO_48 } almanac_font_t;
typedef enum { ALM_INK = 0, ALM_INK_DIM, ALM_INK_GOOD, ALM_INK_BAD, ALM_INK_NAVY } almanac_ink_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t kind;
} almanac_box_t;

// One line drawn at y inside a window x..x+w: at the left edge, centred in the window, or
// centred together with the lines that follow it (the date and its roaming tag).
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    uint8_t font;
    uint8_t ink;
    uint8_t box; // ALMANAC_NO_BOX, or the index of the box the line is drawn into
    uint8_t center; // 0 left, 1 centred, 2 centred as a group
    char text[ALMANAC_LINE_BYTES];
} almanac_text_t;

typedef struct {
    almanac_box_t boxes[ALMANAC_SCENE_BOXES_MAX];
    almanac_text_t texts[ALMANAC_SCENE_TEXTS_MAX];
    uint8_t n_boxes;
    uint8_t n_texts;
} almanac_scene_t;

int almanac_font_line_h(almanac_font_t font);
// Border the UI draws around a box kind, which the box's content must stay clear of.
int almanac_box_border_h(almanac_box_kind_t kind);
// Height of a card holding rows of 16 px text.
int almanac_card_h(int rows);

// banner stands in for the date line while the date is not confirmed; NULL shows the date.
void almanac_scene_today(const almanac_view_day_t *day, const char *zhixing, bool roaming, const char *banner,
                         almanac_scene_t *out);
void almanac_scene_detail(const almanac_view_day_t *day, bool huangdao, almanac_scene_t *out);
void almanac_scene_hours(const almanac_view_hours_t *hours, almanac_scene_t *out);
void almanac_scene_lucky(const almanac_view_lucky_t *lucky, almanac_scene_t *out);
void almanac_scene_fortune(const almanac_day_t *day, almanac_scene_t *out);
void almanac_scene_menu(int selected, almanac_scene_t *out);
void almanac_scene_pick(int selected, int first_visible, almanac_scene_t *out);
void almanac_scene_result(const char *matter, const almanac_line_t *dates, int count, almanac_scene_t *out);
// step is 0 for the year, 1 for the month, 2 for the day.
void almanac_scene_setup(int step, int year, int month, int day, almanac_scene_t *out);
void almanac_scene_confirm(const char *date_text, almanac_scene_t *out);
void almanac_scene_help(const almanac_line_t *lines, int count, almanac_scene_t *out);
void almanac_scene_about(const almanac_line_t *lines, int count, almanac_scene_t *out);
