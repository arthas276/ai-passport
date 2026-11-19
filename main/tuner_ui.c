// tuner_ui.c - tuner UI: main readout screen with instrument modes plus a
// settings screen (v1.3: A4 reference, transpose display, volume, Chinese
// labels, string-progress dots, needle smoothing). All handlers run under
// bsp_lvgl_lock (called from the input task in main.c).
// Keys (main): OK click = freeze/resume; OK long = cycle mode; UP/DOWN =
// step the target string; UP long = reference tone; DOWN long = settings.
// Keys (settings): UP/DOWN = select row; OK = adjust; OK long = save+back.
#include "tuner_ui.h"
#include "tuner.h"
#include "tuner_audio.h"
#include "tuner_pitch.h"
#include "tuner_modes.h"
#include "tuner_settings.h"
#include "tuner_store.h"
#include "tuner_strings.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>

static const char *TAG = "tuner_ui";

LV_FONT_DECLARE(tuner_font_20);

// Family night-navy palette shared with EAR/OTP (house style).
#define C_BG    0x1B263B
#define C_CARD  0x24344D
#define C_EDGE  0x3A4C6E   // borders / ticks / track edge
#define C_TEXT  0xFFFFFF
#define C_MUTED 0x8D99AE
#define C_OK    0x2ECC71
#define C_BAD   0xE76F51
#define C_ACC   0x4EA8DE   // cyan accent: needle / live elements

#define TRACK_X 20
#define TRACK_W 200
#define CENTER_X 100
#define PX_PER_CENT 2
#define HINT_ROTATE_US 2500000
#define DOT_MAX 6

static lv_obj_t *s_scr, *s_title, *s_batt;
static lv_obj_t *s_note;      // big note readout, montserrat_48
static lv_obj_t *s_aux;       // 弦 n/m (instruments) / 音域 C3-A4 (voice)
static lv_obj_t *s_hz;        // frequency, montserrat_20
static lv_obj_t *s_bar, *s_zone, *s_marker;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_dots[DOT_MAX];

static tuner_mode_t s_mode = TUNER_MODE_CHROMATIC;
static tuner_strings_t s_strings;
static tuner_range_t s_range;
static bool s_frozen;
static uint8_t s_lock_cnt;
static bool s_lock_armed = true;
static float s_cents_ema;
static bool s_ema_valid;
static uint8_t s_tp;              // transpose semitones (0/2/7/9)
static tuner_settings_t s_eff;    // effective settings (boot or last save)

// Settings screen: rebuilt at its own entry, never on exit (family rule).
static lv_obj_t *s_set_scr;
static lv_obj_t *s_set_rows[3], *s_set_labels[3], *s_set_values[3];
static tuner_settings_t s_set;    // working copy while editing
static int s_set_row;
static bool s_on_settings;

// ---- backlight idle management (repo power convention) ----
#define IDLE_DIM_MS 10000
static uint32_t s_idle_ms;
static bool s_dimmed;
static esp_timer_handle_t s_idle_timer;

static int64_t s_hint_us;
static int s_hint_page;

static void idle_timer_cb(void *arg) {
    (void)arg;
    s_idle_ms += 1000;
    if (s_idle_ms >= IDLE_DIM_MS && !s_dimmed) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
}

static void note_activity(void) {
    s_idle_ms = 0;
    if (s_dimmed) {
        bsp_display_backlight(100);
        s_dimmed = false;
    }
}

static void set_status(const char *text, uint32_t color) {
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    // The big note readout shares the state colour: red when far, green
    // when locked in, plain text otherwise.
    lv_obj_set_style_text_color(s_note, lv_color_hex(color), 0);
}

// Hint pages rotate every HINT_ROTATE_US so every gesture stays advertised
// within one 240 px line (agreement: hidden operations need on-screen text).
static const char *hint_page_text(int page) {
    bool instr = tuner_mode_string_count(s_mode) > 0;
    if (instr) {
        if (page == 0) return STR_HINT_REF;
        if (page == 1) return STR_HINT_BASIC;
        return STR_HINT_SET;
    }
    return page == 0 ? STR_HINT_BASIC : STR_HINT_SET;
}

static void set_hint(void) {
    int pages = (tuner_mode_string_count(s_mode) > 0) ? 3 : 2;
    const char *t = s_frozen ? STR_HINT_FROZEN : hint_page_text(s_hint_page % pages);
    lv_label_set_text(s_hint, t);
    lv_obj_set_style_text_color(s_hint,
        lv_color_hex(s_frozen ? C_ACC : C_MUTED), 0);
}

static void hint_tick(void) {
    int64_t now = esp_timer_get_time();
    if (s_hint_us == 0 || now - s_hint_us >= HINT_ROTATE_US) {
        s_hint_us = now;
        s_hint_page++;
        set_hint();
    }
}

static void set_marker(float cents) {
    int x = CENTER_X + (int)(cents * (float)PX_PER_CENT);
    if (x < 0) x = 0;
    if (x > TRACK_W - 12) x = TRACK_W - 12;
    lv_obj_set_x(s_marker, x);
}

// The reference the needle grades against: the current target string in
// instrument modes, the nearest semitone otherwise.
static float reference_cents(const tuner_event_t *ev, int16_t *ref_midi) {
    int count = tuner_mode_string_count(s_mode);
    if (count > 0) {
        int16_t target = tuner_mode_string_midi(s_mode, s_strings.index);
        *ref_midi = target;
        return tuner_pitch_cents(ev->hz, tuner_note_freq(target));
    }
    *ref_midi = ev->midi;
    return ev->cents;
}

// String-progress dots: done = green, current = accent, pending = edge.
static void refresh_dots(void) {
    int n = tuner_mode_string_count(s_mode);
    for (int i = 0; i < DOT_MAX; i++) {
        if (i >= n) {
            lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        uint32_t c = (s_strings.done_mask & (uint8_t)(1u << i)) ? C_OK
                   : (i == s_strings.index) ? C_ACC : C_EDGE;
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(c), 0);
    }
    if (n > 0) {
        int pitch = 18;
        int x0 = CENTER_X - (n * 8 + (n - 1) * (pitch - 8)) / 2;
        for (int i = 0; i < n; i++) lv_obj_set_x(s_dots[i], x0 + i * pitch);
    }
}

static void refresh_mode_ui(void) {
    char t[32];
    // Bare mode name in WenKai 20: the longest (尤克里里) is 80 px wide,
    // centred 60..140, clear of the battery readout at top-right.
    lv_label_set_text(s_title, tuner_mode_name(s_mode));
    if (tuner_mode_string_count(s_mode) > 0) {
        snprintf(t, sizeof(t), "%s %d/%d", STR_AUX_STR, s_strings.index + 1,
                 tuner_mode_string_count(s_mode));
        lv_label_set_text(s_aux, t);
    } else if (s_mode == TUNER_MODE_VOICE) {
        snprintf(t, sizeof(t), "%s --", STR_AUX_RANGE);
        lv_label_set_text(s_aux, t);
    } else {
        lv_label_set_text(s_aux, "");
    }
    set_hint();
    refresh_dots();
}

static const char *tp_label(uint8_t tp) {
    switch (tp) {
    case TUNER_TP_BB: return STR_TP_BB;
    case TUNER_TP_EB: return STR_TP_EB;
    case TUNER_TP_F:  return STR_TP_F;
    default:          return STR_TP_OFF;
    }
}

static void refresh_settings_ui(void) {
    static const char *names[3] = { STR_SET_A4, STR_SET_TP, STR_SET_VOL };
    char v[16];
    for (int r = 0; r < 3; r++) {
        bool sel = (r == s_set_row);
        lv_obj_set_style_border_color(s_set_rows[r],
            lv_color_hex(sel ? C_ACC : C_EDGE), 0);
        lv_obj_set_style_border_width(s_set_rows[r], sel ? 2 : 1, 0);
        lv_obj_set_style_text_color(s_set_labels[r],
            lv_color_hex(sel ? C_TEXT : C_MUTED), 0);
        lv_obj_set_style_text_color(s_set_values[r],
            lv_color_hex(sel ? C_ACC : C_MUTED), 0);
        if (r == 0) snprintf(v, sizeof(v), "%u Hz", (unsigned)s_set.a4);
        else if (r == 1) snprintf(v, sizeof(v), "%s", tp_label(s_set.tp));
        else snprintf(v, sizeof(v), "%u", (unsigned)s_set.vol);
        lv_label_set_text(s_set_values[r], v);
        (void)names;
    }
}

static void build_settings_screen(void) {
    if (s_set_scr) {
        lv_obj_delete(s_set_scr);
        s_set_scr = NULL;
    }
    s_set_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_set_scr, lv_color_hex(C_BG), 0);

    lv_obj_t *title = lv_label_create(s_set_scr);
    lv_obj_set_style_text_font(title, &tuner_font_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(title, STR_SET_TITLE);

    static const char *names[3] = { STR_SET_A4, STR_SET_TP, STR_SET_VOL };
    for (int r = 0; r < 3; r++) {
        s_set_rows[r] = lv_obj_create(s_set_scr);
        lv_obj_set_size(s_set_rows[r], 208, 44);
        lv_obj_set_pos(s_set_rows[r], 16, 64 + r * 52);
        lv_obj_set_style_bg_color(s_set_rows[r], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_radius(s_set_rows[r], 10, 0);
        lv_obj_clear_flag(s_set_rows[r], LV_OBJ_FLAG_SCROLLABLE);

        s_set_labels[r] = lv_label_create(s_set_rows[r]);
        lv_obj_set_style_text_font(s_set_labels[r], &tuner_font_20, 0);
        lv_obj_align(s_set_labels[r], LV_ALIGN_LEFT_MID, 10, 0);
        lv_label_set_text(s_set_labels[r], names[r]);

        s_set_values[r] = lv_label_create(s_set_rows[r]);
        lv_obj_set_style_text_font(s_set_values[r], &tuner_font_20, 0);
        lv_obj_align(s_set_values[r], LV_ALIGN_RIGHT_MID, -10, 0);
    }

    lv_obj_t *foot = lv_label_create(s_set_scr);
    lv_obj_set_style_text_font(foot, &tuner_font_20, 0);
    lv_obj_set_style_text_color(foot, lv_color_hex(C_MUTED), 0);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(foot, STR_SET_FOOT);

    refresh_settings_ui();
}

static void enter_settings(void) {
    s_set = s_eff;
    s_set_row = 0;
    s_on_settings = true;
    build_settings_screen();
    lv_screen_load(s_set_scr);
}

static void exit_settings_save(void) {
    s_set.mode = (uint8_t)s_mode;
    tuner_settings_sanitize(&s_set);
    esp_err_t err = tuner_store_save(&s_set);
    if (err != ESP_OK) {
        // Keep running with the edited values even if persistence failed.
        ESP_LOGW(TAG, "settings save: %s", esp_err_to_name(err));
    }
    s_eff = s_set;
    tuner_pitch_set_a4(s_eff.a4);
    s_tp = s_eff.tp;
    tuner_audio_set_volume(s_eff.vol);
    s_on_settings = false;
    s_hint_page = 0;
    s_hint_us = esp_timer_get_time();
    lv_screen_load(s_scr);
    set_hint();
}

// Build the main screen.
void tuner_ui_init(QueueHandle_t app_q, const tuner_settings_t *saved) {
    (void)app_q;
    srand((unsigned)esp_random());
    s_eff = *saved;
    tuner_settings_sanitize(&s_eff);
    s_mode = (tuner_mode_t)s_eff.mode;
    s_tp = s_eff.tp;
    tuner_strings_reset(&s_strings);
    tuner_range_reset(&s_range);
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &tuner_font_20, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(C_TEXT), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 8);

    s_batt = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -8, 12);
    int soc = bsp_battery_soc();
    char t[16];
    snprintf(t, sizeof(t), soc < 0 ? "--%%" : "%d%%", soc);
    lv_label_set_text(s_batt, t);

    s_note = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_note, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_note, lv_color_hex(C_TEXT), 0);
    lv_obj_align(s_note, LV_ALIGN_TOP_MID, 0, 64);
    lv_label_set_text(s_note, "--");

    s_aux = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_aux, &tuner_font_20, 0);
    lv_obj_set_style_text_color(s_aux, lv_color_hex(C_ACC), 0);
    lv_obj_align(s_aux, LV_ALIGN_TOP_MID, 0, 122);
    lv_label_set_text(s_aux, "");

    s_hz = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hz, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_hz, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s_hz, LV_ALIGN_TOP_MID, 0, 154);
    lv_label_set_text(s_hz, "-- Hz");

    for (int i = 0; i < DOT_MAX; i++) {
        s_dots[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_pos(s_dots[i], CENTER_X - 4, 196);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(C_EDGE), 0);
        lv_obj_set_style_radius(s_dots[i], 4, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_bar = lv_obj_create(s_scr);
    lv_obj_set_size(s_bar, TRACK_W, 14);
    lv_obj_set_pos(s_bar, TRACK_X, 220);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(C_EDGE), 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_radius(s_bar, 7, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_zone = lv_obj_create(s_bar);
    lv_obj_set_size(s_zone, (int)(TUNER_IN_TUNE_CENTS * 2 * PX_PER_CENT), 14);
    lv_obj_set_pos(s_zone, CENTER_X - (int)(TUNER_IN_TUNE_CENTS * PX_PER_CENT), 0);
    lv_obj_set_style_bg_color(s_zone, lv_color_hex(C_OK), 0);
    lv_obj_set_style_border_width(s_zone, 0, 0);
    lv_obj_set_style_radius(s_zone, 7, 0);
    lv_obj_clear_flag(s_zone, LV_OBJ_FLAG_SCROLLABLE);

    // Centre tick: one-pixel reference line under the needle at rest.
    lv_obj_t *tick = lv_obj_create(s_bar);
    lv_obj_set_size(tick, 2, 14);
    lv_obj_set_pos(tick, CENTER_X - 1, 0);
    lv_obj_set_style_bg_color(tick, lv_color_hex(C_EDGE), 0);
    lv_obj_set_style_border_width(tick, 0, 0);
    lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);

    s_marker = lv_obj_create(s_bar);
    lv_obj_set_size(s_marker, 12, 26);
    lv_obj_set_pos(s_marker, CENTER_X, -6);
    lv_obj_set_style_bg_color(s_marker, lv_color_hex(C_ACC), 0);
    lv_obj_set_style_border_width(s_marker, 0, 0);
    lv_obj_set_style_radius(s_marker, 4, 0);
    lv_obj_clear_flag(s_marker, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &tuner_font_20, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 246);
    lv_label_set_text(s_status, "");

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &tuner_font_20, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_screen_load(s_scr);
    refresh_mode_ui();

    esp_timer_create_args_t args = { .callback = idle_timer_cb,
                                     .name = "tuner_idle" };
    if (esp_timer_create(&args, &s_idle_timer) == ESP_OK) {
        esp_timer_start_periodic(s_idle_timer, 1000000);
    } else {
        s_idle_timer = NULL;
    }
}

static void handle_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        exit_settings_save();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        s_set_row = (s_set_row + 2) % 3;
    } else if (btn == BSP_BTN_DOWN) {
        s_set_row = (s_set_row + 1) % 3;
    } else if (btn == BSP_BTN_OK) {
        if (s_set_row == 0) {
            s_set.a4 = (s_set.a4 >= TUNER_A4_MAX) ? TUNER_A4_MIN
                                                  : (uint16_t)(s_set.a4 + 1);
        } else if (s_set_row == 1) {
            s_set.tp = tuner_tp_next(s_set.tp);
        } else {
            s_set.vol = (s_set.vol >= 100) ? 0 : (uint8_t)(s_set.vol + 10);
        }
    }
    refresh_settings_ui();
}

// OK click freezes; OK long cycles the mode; UP/DOWN step the target string
// in instrument modes; UP long plays the reference tone; DOWN long opens
// the settings screen.
void tuner_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    note_activity();
    if (s_on_settings) {
        handle_settings_key(btn, ev);
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        s_mode = tuner_mode_next(s_mode);
        tuner_strings_reset(&s_strings);
        tuner_range_reset(&s_range);
        s_frozen = false;
        s_lock_cnt = 0;
        s_lock_armed = true;
        s_ema_valid = false;
        s_hint_page = 0;
        lv_label_set_text(s_note, "--");
        lv_label_set_text(s_hz, "-- Hz");
        set_status("", C_TEXT);
        set_marker(0.0f);
        refresh_mode_ui();
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
        if (tuner_mode_string_count(s_mode) > 0) {
            tuner_audio_tone(tuner_mode_string_midi(s_mode, s_strings.index),
                             1200);
        }
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_DOWN) {
        enter_settings();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        if (tuner_mode_string_count(s_mode) > 0) {
            tuner_strings_step(&s_strings, s_mode, -1);
            s_lock_cnt = 0;
            s_lock_armed = true;
            s_ema_valid = false;
            refresh_mode_ui();
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (tuner_mode_string_count(s_mode) > 0) {
            tuner_strings_step(&s_strings, s_mode, 1);
            s_lock_cnt = 0;
            s_lock_armed = true;
            s_ema_valid = false;
            refresh_mode_ui();
        }
    } else if (btn == BSP_BTN_OK) {
        s_frozen = !s_frozen;
        set_hint();
    }
    hint_tick();
}

// Live frame: update the readout, track the lock state, chime on lock, and
// in string modes advance to the next string after a lock. The needle
// position is EMA-smoothed; lock judgement uses the raw cents.
void tuner_ui_handle_audio(const void *event) {
    const tuner_event_t *ev = event;
    if (s_on_settings) return;   // capture runs on; the readout is paused
    if (ev->kind != TUNER_EV_PITCH || s_frozen) return;
    note_activity();
    hint_tick();

    if (ev->hz <= 0.0f) {
        lv_label_set_text(s_note, "--");
        lv_label_set_text(s_hz, "-- Hz");
        set_status("", C_TEXT);
        s_lock_cnt = 0;
        s_lock_armed = true;
        s_ema_valid = false;
        return;
    }
    if (s_mode == TUNER_MODE_VOICE) {
        tuner_range_update(&s_range, ev->midi);
    }

    int16_t ref = -1;
    float cents = reference_cents(ev, &ref);
    int count = tuner_mode_string_count(s_mode);
    if (count > 0) {
        // String modes grade against the target string and show its name
        // (sounding pitch; transpose never applies to string targets).
        lv_label_set_text(s_note, ref >= 0 ? tuner_note_name(ref) : "--");
    } else {
        // Free modes show the written pitch for transposing instruments.
        lv_label_set_text(s_note,
            tuner_note_name(tuner_transpose_midi(ev->midi, s_tp)));
    }
    if (s_mode == TUNER_MODE_VOICE && s_range.lowest >= 0) {
        char t[24];
        snprintf(t, sizeof(t), "%s %s-%s", STR_AUX_RANGE,
                 tuner_note_name(tuner_transpose_midi(s_range.lowest, s_tp)),
                 tuner_note_name(tuner_transpose_midi(s_range.highest, s_tp)));
        lv_label_set_text(s_aux, t);
    }

    char t[24];
    snprintf(t, sizeof(t), "%.1f Hz  %+.1fct", (double)ev->hz, (double)cents);
    lv_label_set_text(s_hz, t);
    if (!s_ema_valid) {
        s_cents_ema = cents;
        s_ema_valid = true;
    } else {
        s_cents_ema = tuner_ema(s_cents_ema, cents, 0.35f);
    }
    set_marker(s_cents_ema);

    if (cents < -TUNER_IN_TUNE_CENTS) {
        set_status(STR_STATUS_LOW, C_BAD);
        s_lock_cnt = 0;
        s_lock_armed = true;
    } else if (cents > TUNER_IN_TUNE_CENTS) {
        set_status(STR_STATUS_HIGH, C_BAD);
        s_lock_cnt = 0;
        s_lock_armed = true;
    } else if (s_lock_cnt < TUNER_LOCK_FRAMES) {
        s_lock_cnt++;
        if (s_lock_cnt == TUNER_LOCK_FRAMES) {
            set_status(STR_STATUS_INTUNE, C_OK);
            if (s_lock_armed) {
                tuner_audio_cue();
                s_lock_armed = false;   // one chime per lock
            }
            if (count > 0) {
                // String tuned: mark it done and move to the next one.
                tuner_strings_advance(&s_strings, s_mode);
                s_lock_cnt = 0;
                s_lock_armed = true;
                s_ema_valid = false;
                if (tuner_strings_all_done(&s_strings, s_mode)) {
                    set_status(STR_STATUS_ALLDONE, C_OK);
                }
                refresh_mode_ui();
            }
        } else {
            set_status("", C_TEXT);
        }
    }
}
