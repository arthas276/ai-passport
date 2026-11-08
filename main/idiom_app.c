// main/idiom_app.c - 成语翻牌 UI and state machine: five LVGL screens (home,
// play, result, stats, help), a button queue drained under the LVGL lock, and
// the repo-standard idle dim -> deep sleep policy. Game rules live in
// idiom_core.c; persistence in idiom_store.c.
#include "idiom_app.h"
#include "idiom_power.h"
#include "idiom_strings.h"
#include "idiom_words.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(idiom_font_32);
LV_FONT_DECLARE(idiom_font_22);

static const char *TAG = "idiom";

// Night table theme; contrast pairs audited in docs/PRD_IDIOM.md.
#define BG_INK      0x141821
#define BG_RAISE    0x1D2434
#define TILE_EMPTY  0x242E40
#define TILE_BORDER 0x3A4761
#define GREEN_HIT   0x2E9663
#define AMBER_NEAR  0xD9A93C
#define SLATE_MISS  0x3B4454
#define TEXT_MAIN   0xEDF1F6
#define TEXT_MUTED  0x93A0B5
#define GOLD        0xF2C14E

// PLAY geometry (px): header 22, board 6x(36x32 tiles, pitch 40/35), wheel
// 4x(52x52 tiles, pitch 56), footer 22. Total height 312 <= 320.
#define BOARD_X0 42
#define BOARD_Y0 26
#define BOARD_TW 36
#define BOARD_TH 32
#define BOARD_PX 40
#define BOARD_PY 35
#define WHEEL_X0 10
#define WHEEL_Y0 238
#define WHEEL_TS 52
#define WHEEL_PX 56

typedef struct {
    lv_obj_t *box;
    lv_obj_t *glyph;
} tile_t;

static idiom_app_t s_app;
static idiom_power_t s_power;
static QueueHandle_t s_keys;
static TaskHandle_t s_key_task;
static lv_timer_t *s_timer;
static volatile bool s_ready;
static int8_t s_soc = -1;
static uint8_t s_soc_tick;
static bool s_help_from_stats;

static lv_obj_t *s_home;
static lv_obj_t *s_home_streak;
static lv_obj_t *s_home_batt;
static tile_t s_home_logo[4];
static lv_obj_t *s_play;
static lv_obj_t *s_play_head;
static tile_t s_board[6][4];
static tile_t s_wheel[4];
static lv_obj_t *s_result;
static lv_obj_t *s_result_title;
static tile_t s_result_tiles[4];
static lv_obj_t *s_result_tries;
static lv_obj_t *s_result_streak;
static lv_obj_t *s_result_quip;
static lv_obj_t *s_stats;
static lv_obj_t *s_stats_rows[5];
static lv_obj_t *s_stats_batt;
static lv_obj_t *s_help;

typedef struct {
    int button;
    int event;
} idiom_key_t;

static void cp_to_utf8(uint32_t cp, char out[5]) {
    if (cp < 0x80u) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
    } else if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, int y, int w, uint32_t color,
                            const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static tile_t make_tile(lv_obj_t *parent, int x, int y, int w, int h,
                        const lv_font_t *font) {
    tile_t t;
    t.box = lv_obj_create(parent);
    lv_obj_set_pos(t.box, x, y);
    lv_obj_set_size(t.box, w, h);
    lv_obj_clear_flag(t.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(t.box, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(t.box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(t.box, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t.box, lv_color_hex(TILE_EMPTY), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t.box, LV_OPA_COVER, LV_PART_MAIN);
    t.glyph = lv_label_create(t.box);
    lv_obj_set_style_text_font(t.glyph, font, LV_PART_MAIN);
    lv_obj_center(t.glyph);
    return t;
}

static void tile_set(tile_t t, uint32_t cp, int mark, uint32_t glyph_color,
                     uint32_t bg, int border_px, uint32_t border_color) {
    char utf8[5];
    cp_to_utf8(cp, utf8);
    lv_label_set_text(t.glyph, utf8);
    lv_obj_set_style_text_color(t.glyph, lv_color_hex(glyph_color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(t.box, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_border_width(t.box, border_px, LV_PART_MAIN);
    lv_obj_set_style_border_color(t.box, lv_color_hex(border_color), LV_PART_MAIN);
}

static void tile_mark(tile_t t, uint32_t cp, idiom_mark_t mark) {
    switch (mark) {
    case IDIOM_MARK_HIT:
        tile_set(t, cp, mark, TEXT_MAIN, GREEN_HIT, 0, GREEN_HIT);
        break;
    case IDIOM_MARK_NEAR:
        tile_set(t, cp, mark, BG_INK, AMBER_NEAR, 0, AMBER_NEAR);
        break;
    default:
        tile_set(t, cp, mark, TEXT_MUTED, BG_INK, 2, SLATE_MISS);
        break;
    }
}

static void battery_refresh(void) {
    s_soc = bsp_battery_soc();
}

static void render_battery(lv_obj_t *label) {
    if (!label) return;
    if (s_soc < 0) lv_label_set_text(label, "--");
    else lv_label_set_text_fmt(label, "%d%%", s_soc);
}

static void render_home(void) {
    lv_label_set_text_fmt(s_home_streak, STR_HOME_STREAK,
                          (unsigned)s_app.store.stats.streak,
                          (unsigned)s_app.store.stats.best);
    render_battery(s_home_batt);
}

static void render_play(void) {
    lv_label_set_text_fmt(s_play_head, STR_PLAY_HEAD,
                          (unsigned)s_app.rows, (unsigned)s_app.cand_count);
    for (int r = 0; r < 6; r++) {
        bool committed = r < (int)s_app.rows;
        for (int c = 0; c < 4; c++) {
            if (committed) {
                tile_mark(s_board[r][c], s_app.guesses[r][c],
                          s_app.marks[r][c]);
            } else {
                tile_set(s_board[r][c], ' ', 0, TEXT_MAIN, TILE_EMPTY, 0, TILE_EMPTY);
            }
        }
    }
    uint32_t border = s_app.cand_count <= 1u ? GOLD : TILE_BORDER;
    for (int c = 0; c < 4; c++) {
        uint32_t cp = s_app.cand_count
                          ? IDIOM_WORDS_CP[s_app.cand[s_app.wheel]][c]
                          : (uint32_t)' ';
        tile_set(s_wheel[c], cp, 0, TEXT_MAIN, TILE_EMPTY, 2, border);
    }
}

static void render_result(void) {
    lv_label_set_text(s_result_title, s_app.won ? STR_BIG_WIN : STR_BIG_LOSE);
    lv_obj_set_style_text_color(s_result_title,
                                lv_color_hex(s_app.won ? GOLD : TEXT_MAIN),
                                LV_PART_MAIN);
    for (int c = 0; c < 4; c++) {
        tile_mark(s_result_tiles[c], IDIOM_WORDS_CP[s_app.target][c],
                  IDIOM_MARK_HIT);
    }
    lv_label_set_text_fmt(s_result_tries, STR_RESULT_TRIES,
                          (unsigned)s_app.result_tries,
                          (unsigned)s_app.cand_count);
    if (s_app.new_record) {
        lv_label_set_text_fmt(s_result_streak, "连胜 %u · " STR_RESULT_REC,
                              (unsigned)s_app.store.stats.streak);
    } else {
        lv_label_set_text_fmt(s_result_streak, STR_HOME_STREAK,
                              (unsigned)s_app.store.stats.streak,
                              (unsigned)s_app.store.stats.best);
    }
    const char *quip = STR_QUIP_LOSE;
    if (s_app.won) {
        static const char *quips[6] = {
            STR_QUIP_1, STR_QUIP_2, STR_QUIP_3,
            STR_QUIP_4, STR_QUIP_5, STR_QUIP_6,
        };
        uint8_t idx = s_app.result_tries ? (uint8_t)(s_app.result_tries - 1u) : 0u;
        if (idx > 5u) idx = 5u;
        quip = quips[idx];
    }
    lv_label_set_text(s_result_quip, quip);
}

static void render_stats(void) {
    static const char *names[5] = {
        STR_STATS_TOTAL, STR_STATS_WINS, STR_STATS_RATE,
        STR_STATS_CUR, STR_STATS_BEST,
    };
    const idiom_stats_t *s = &s_app.store.stats;
    unsigned values[5] = {
        (unsigned)s->games, (unsigned)s->wins, idiom_stats_win_rate(s),
        (unsigned)s->streak, (unsigned)s->best,
    };
    for (int i = 0; i < 5; i++) {
        lv_label_set_text_fmt(s_stats_rows[i], "%s %u", names[i], values[i]);
    }
    render_battery(s_stats_batt);
}

static void show_screen(idiom_screen_t next);

static void build_home(void) {
    if (s_home) return;
    s_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home, lv_color_hex(BG_INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_home, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *name = make_label(s_home, 30, 200, TEXT_MAIN, &idiom_font_32);
    lv_label_set_text(name, STR_APP_NAME);
    // The logo is the game itself: flip card states spelling 翻牌成语.
    static const uint32_t logo_cp[4] = {0x7FFB, 0x724C, 0x6210, 0x8BED};
    static const idiom_mark_t logo_mark[4] = {
        IDIOM_MARK_NEAR, IDIOM_MARK_MISS, IDIOM_MARK_HIT, IDIOM_MARK_HIT,
    };
    static const int lx[4] = {62, 118, 62, 118};
    static const int ly[4] = {78, 78, 134, 134};
    for (int i = 0; i < 4; i++) {
        s_home_logo[i] = make_tile(s_home, lx[i], ly[i], WHEEL_TS, WHEEL_TS,
                                   &idiom_font_32);
        tile_mark(s_home_logo[i], logo_cp[i], logo_mark[i]);
    }
    s_home_streak = make_label(s_home, 200, 220, GOLD, &idiom_font_22);
    lv_obj_t *start = make_label(s_home, 252, 220, TEXT_MAIN, &idiom_font_22);
    lv_label_set_text(start, STR_HOME_START);
    lv_obj_t *foot = make_label(s_home, 292, 220, TEXT_MUTED, &idiom_font_22);
    lv_label_set_text(foot, STR_HOME_FOOT);
    s_home_batt = make_label(s_home, 4, 60, TEXT_MUTED, &idiom_font_22);
    lv_obj_set_style_text_align(s_home_batt, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_home_batt, LV_ALIGN_TOP_RIGHT, -10, 4);
}

static void build_play(void) {
    if (s_play) return;
    s_play = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_play, lv_color_hex(BG_INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_play, LV_OPA_COVER, LV_PART_MAIN);
    s_play_head = make_label(s_play, 2, 220, TEXT_MUTED, &idiom_font_22);
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 4; c++) {
            s_board[r][c] = make_tile(s_play, BOARD_X0 + c * BOARD_PX,
                                      BOARD_Y0 + r * BOARD_PY, BOARD_TW, BOARD_TH,
                                      &idiom_font_32);
        }
    }
    for (int c = 0; c < 4; c++) {
        s_wheel[c] = make_tile(s_play, WHEEL_X0 + c * WHEEL_PX, WHEEL_Y0,
                               WHEEL_TS, WHEEL_TS, &idiom_font_32);
    }
    lv_obj_t *foot = make_label(s_play, 296, 230, TEXT_MUTED, &idiom_font_22);
    lv_label_set_text(foot, STR_PLAY_FOOT);
}

static void build_result(void) {
    if (s_result) return;
    s_result = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_result, lv_color_hex(BG_RAISE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_result, LV_OPA_COVER, LV_PART_MAIN);
    s_result_title = make_label(s_result, 30, 200, GOLD, &idiom_font_32);
    for (int c = 0; c < 4; c++) {
        s_result_tiles[c] = make_tile(s_result, WHEEL_X0 + c * WHEEL_PX, 84,
                                      WHEEL_TS, WHEEL_TS, &idiom_font_32);
    }
    s_result_tries = make_label(s_result, 152, 220, TEXT_MUTED, &idiom_font_22);
    s_result_streak = make_label(s_result, 196, 220, GOLD, &idiom_font_22);
    s_result_quip = make_label(s_result, 228, 220, TEXT_MAIN, &idiom_font_22);
    lv_obj_t *foot = make_label(s_result, 292, 220, TEXT_MUTED, &idiom_font_22);
    lv_label_set_text(foot, STR_RESULT_FOOT);
}

static void build_stats(void) {
    if (s_stats) return;
    s_stats = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_stats, lv_color_hex(BG_RAISE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_stats, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *title = make_label(s_stats, 30, 200, TEXT_MAIN, &idiom_font_32);
    lv_label_set_text(title, STR_BIG_STATS);
    for (int i = 0; i < 5; i++) {
        s_stats_rows[i] = make_label(s_stats, 84 + i * 36, 220, TEXT_MAIN,
                                     &idiom_font_22);
    }
    lv_obj_t *foot = make_label(s_stats, 292, 220, TEXT_MUTED, &idiom_font_22);
    lv_label_set_text(foot, STR_STATS_FOOT);
    s_stats_batt = make_label(s_stats, 4, 60, TEXT_MUTED, &idiom_font_22);
    lv_obj_set_style_text_align(s_stats_batt, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_stats_batt, LV_ALIGN_TOP_RIGHT, -10, 4);
}

static void build_help(void) {
    if (s_help) return;
    s_help = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_help, lv_color_hex(BG_RAISE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_help, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *title = make_label(s_help, 30, 200, TEXT_MAIN, &idiom_font_32);
    lv_label_set_text(title, STR_BIG_HELP);
    // 人山水 demonstrate the three states with wordlist characters.
    static const uint32_t demo_cp[3] = {0x4EBA, 0x5C71, 0x6C34};
    static const idiom_mark_t demo_mark[3] = {
        IDIOM_MARK_HIT, IDIOM_MARK_NEAR, IDIOM_MARK_MISS,
    };
    for (int i = 0; i < 3; i++) {
        tile_t t = make_tile(s_help, BOARD_X0 + 24 + i * BOARD_PX, 84, BOARD_TW,
                             BOARD_TH, &idiom_font_22);
        tile_mark(t, demo_cp[i], demo_mark[i]);
    }
    static const char *lines[4] = {STR_HELP_L1, STR_HELP_L2, STR_HELP_L3,
                                   STR_HELP_L4};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = make_label(s_help, 136 + i * 26, 230, TEXT_MAIN,
                                 &idiom_font_22);
        lv_label_set_text(l, lines[i]);
    }
    lv_obj_t *foot = make_label(s_help, 292, 220, TEXT_MUTED, &idiom_font_22);
    lv_label_set_text(foot, STR_HELP_FOOT);
}

static void show_screen(idiom_screen_t next) {
    s_app.screen = next;
    switch (next) {
    case IDIOM_SCREEN_PLAY:
        build_play();
        render_play();
        lv_screen_load(s_play);
        break;
    case IDIOM_SCREEN_RESULT:
        build_result();
        render_result();
        lv_screen_load(s_result);
        break;
    case IDIOM_SCREEN_STATS:
        build_stats();
        render_stats();
        lv_screen_load(s_stats);
        break;
    case IDIOM_SCREEN_HELP:
        build_help();
        lv_screen_load(s_help);
        break;
    case IDIOM_SCREEN_HOME:
    default:
        build_home();
        render_home();
        lv_screen_load(s_home);
        break;
    }
}

static void start_round(void) {
    if (s_app.store.order_idx >= IDIOM_COUNT) {
        s_app.store.order_seed = idiom_prng_next(&s_app.store.order_seed);
        s_app.store.order_idx = 0;
    }
    uint16_t order[IDIOM_COUNT];
    idiom_order_build(s_app.store.order_seed, order, IDIOM_COUNT);
    s_app.target = order[s_app.store.order_idx];
    s_app.store.order_idx++;
    s_app.rows = 0;
    s_app.new_record = false;
    for (uint16_t i = 0; i < IDIOM_COUNT; i++) s_app.cand[i] = i;
    s_app.cand_count = IDIOM_COUNT;
    s_app.wheel = 0;
    (void)idiom_store_save(&s_app.store);
    show_screen(IDIOM_SCREEN_PLAY);
}

static void finish_round(bool win) {
    uint32_t prev_best = s_app.store.stats.best;
    idiom_stats_record(&s_app.store.stats, win);
    s_app.new_record = win && s_app.store.stats.streak > prev_best;
    s_app.result_tries = (uint8_t)s_app.rows;
    s_app.won = win;
    (void)idiom_store_save(&s_app.store);
    ESP_LOGI(TAG, "round done win=%d tries=%u", (int)win,
             (unsigned)s_app.result_tries);
    show_screen(IDIOM_SCREEN_RESULT);
}

static void commit_guess(void) {
    if (s_app.rows >= 6 || s_app.cand_count == 0) {
        ESP_LOGW(TAG, "commit blocked rows=%u cand=%u", (unsigned)s_app.rows,
                 (unsigned)s_app.cand_count);
        return;
    }
    uint16_t word = s_app.cand[s_app.wheel];
    memcpy(s_app.guesses[s_app.rows], IDIOM_WORDS_CP[word],
           sizeof(s_app.guesses[0]));
    idiom_mark_t marks[4];
    idiom_score(IDIOM_WORDS_CP[s_app.target], IDIOM_WORDS_CP[word], marks);
    for (int c = 0; c < 4; c++) s_app.marks[s_app.rows][c] = marks[c];
    s_app.rows++;
    if (idiom_marks_all_hit(marks)) {
        finish_round(true);
        return;
    }
    if (s_app.rows >= 6) {
        finish_round(false);
        return;
    }
    s_app.cand_count = idiom_filter(IDIOM_WORDS_CP, IDIOM_COUNT,
                                    s_app.guesses, s_app.marks,
                                    s_app.rows, s_app.cand);
    s_app.wheel = 0;
    ESP_LOGI(TAG, "commit ok rows=%u cand=%u", (unsigned)s_app.rows,
             (unsigned)s_app.cand_count);
    render_play();
}

static void wheel_step(int dir) {
    if (s_app.cand_count == 0) return;
    s_app.wheel = (uint16_t)((s_app.wheel + s_app.cand_count + dir) %
                            s_app.cand_count);
    render_play();
}

static void sleep_warn(const char *step, esp_err_t err) {
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deep sleep 继续:%s 失败: %s", step, esp_err_to_name(err));
    }
}

// BSP terminal-shutdown order (contract of demo_low_power.c), running inside
// the LVGL timer: display stop already excludes concurrent refreshes.
static void enter_sleep(void) {
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) return;  // held: retry next tick
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart();
    sleep_warn("GPIO wake arm",
               esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO,
                                                 ESP_GPIO_WAKEUP_GPIO_LOW));
    sleep_warn("CW2017 suspend", bsp_battery_sleep());
    sleep_warn("ES8311 suspend", bsp_audio_sleep());
    sleep_warn("I2S pin release", bsp_audio_prepare_deep_sleep());
    sleep_warn("shared I2C pin release", bsp_i2c_prepare_deep_sleep());
    sleep_warn("ST7789 suspend", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    esp_restart();
}

static int s_hold_btn = -1;
static uint32_t s_hold_ms;

static void tick(lv_timer_t *timer) {
    (void)timer;
    idiom_power_advance(&s_power, 250u);
    bsp_display_backlight(idiom_power_backlight(&s_power));
    if (++s_soc_tick >= 20u) {  // battery is an I2C read: 5 s cadence, cached
        s_soc_tick = 0u;
        battery_refresh();
        if (s_app.screen == IDIOM_SCREEN_HOME) render_battery(s_home_batt);
    }
    if (s_hold_btn >= 0) {  // hold-to-repeat flip: 400 ms delay, 150 ms steps
        s_hold_ms += 250u;
        if (s_hold_ms >= 400u && ((s_hold_ms - 400u) % 150u) < 250u) {
            wheel_step(s_hold_btn == BSP_BTN_UP ? -1 : 1);
        }
    }
    if (idiom_power_should_sleep(&s_power)) enter_sleep();
}

static void handle_key(int button, int event) {
    idiom_power_activity(&s_power);
    bsp_display_backlight(IDIOM_BACKLIGHT_ACTIVE);
    if (event == BSP_BTN_RELEASE) {
        if ((int)button == s_hold_btn) s_hold_btn = -1;
        return;
    }
    if (event == BSP_BTN_PRESS && s_app.screen == IDIOM_SCREEN_PLAY &&
        (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        s_hold_btn = button;
        s_hold_ms = 0;
        wheel_step(button == BSP_BTN_UP ? -1 : 1);
        return;
    }
    if (event == BSP_BTN_LONG && button == BSP_BTN_OK) {
        switch (s_app.screen) {
        case IDIOM_SCREEN_HOME:
            show_screen(IDIOM_SCREEN_STATS);
            break;
        case IDIOM_SCREEN_PLAY:
            // 长按不得弃局：500ms 长按在出牌时太易误触（v1.0 试玩实录），且帮助页从未宣传该手势。
            break;
        case IDIOM_SCREEN_RESULT:
            show_screen(IDIOM_SCREEN_HOME);
            break;
        case IDIOM_SCREEN_STATS:
            s_help_from_stats = true;
            show_screen(IDIOM_SCREEN_HELP);
            break;
        case IDIOM_SCREEN_HELP:
        default:
            show_screen(s_help_from_stats ? IDIOM_SCREEN_STATS
                                          : IDIOM_SCREEN_HOME);
            break;
        }
        return;
    }
    if (event != BSP_BTN_CLICK) return;
    if (button == BSP_BTN_OK) {
        switch (s_app.screen) {
        case IDIOM_SCREEN_HOME:
            start_round();
            break;
        case IDIOM_SCREEN_PLAY:
            commit_guess();
            break;
        case IDIOM_SCREEN_RESULT:
            start_round();
            break;
        case IDIOM_SCREEN_STATS:
            show_screen(IDIOM_SCREEN_HOME);
            break;
        case IDIOM_SCREEN_HELP:
        default:
            show_screen(s_help_from_stats ? IDIOM_SCREEN_STATS
                                          : IDIOM_SCREEN_HOME);
            s_help_from_stats = false;
            break;
        }
    }
}

static void key_task(void *arg) {
    (void)arg;
    idiom_key_t key;
    while (xQueueReceive(s_keys, &key, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(300)) {
            ESP_LOGW(TAG, "lvgl lock timeout, key dropped b=%d e=%d", key.button, key.event);
            continue;
        }
        handle_key(key.button, key.event);
        bsp_lvgl_unlock();
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    // main.c owns the wake guard and may still swallow this event.
    extern void on_key(int button, int event);
    on_key((int)button, (int)event);
}

void idiom_button_event(int button, int event) {
    idiom_key_t key = {.button = button, .event = event};
    if (s_keys) (void)xQueueSend(s_keys, &key, 0);
}

esp_err_t idiom_app_start(void) {
    memset(&s_app, 0, sizeof(s_app));
    idiom_power_reset(&s_power);
    s_hold_btn = -1;
    esp_err_t err = idiom_store_load(&s_app.store);
    s_app.storage_ok = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "存储不可用(%s),战绩不保存", esp_err_to_name(err));
    }

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) return ESP_FAIL;
    bsp_display_backlight(IDIOM_BACKLIGHT_ACTIVE);
    if (bsp_battery_init() == ESP_OK) battery_refresh();
    (void)bsp_audio_init();
    (void)bsp_audio_sleep();

    s_keys = xQueueCreate(8, sizeof(idiom_key_t));
    if (!s_keys ||
        xTaskCreate(key_task, "idiom_keys", 4096, NULL, 5, &s_key_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (bsp_button_init(button_callback, NULL) != ESP_OK) return ESP_FAIL;
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    build_home();
    bool first_boot = s_app.store.help_seen == 0u;
    if (first_boot) {
        s_app.store.help_seen = 1u;
        (void)idiom_store_save(&s_app.store);
        s_help_from_stats = false;
        show_screen(IDIOM_SCREEN_HELP);
    } else {
        show_screen(IDIOM_SCREEN_HOME);
    }
    s_timer = lv_timer_create(tick, 250, NULL);
    bsp_lvgl_unlock();
    s_ready = true;
    ESP_LOGI(TAG, "成语翻牌就绪 words=%u games=%u", (unsigned)IDIOM_COUNT,
             (unsigned)s_app.store.stats.games);
    return ESP_OK;
}

void idiom_app_stop(void) {
    s_ready = false;
    if (s_key_task) {
        vTaskDelete(s_key_task);
        s_key_task = NULL;
    }
    if (s_keys) {
        vQueueDelete(s_keys);
        s_keys = NULL;
    }
}
