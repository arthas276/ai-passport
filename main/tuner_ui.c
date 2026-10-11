// tuner_ui.c - single-screen tuner UI with instrument modes. All handlers
// run under bsp_lvgl_lock (called from the input task in main.c).
// Keys: OK click = freeze/resume; OK long = cycle mode; UP/DOWN = step the
// target string in instrument modes.
#include "tuner_ui.h"
#include "tuner.h"
#include "tuner_audio.h"
#include "tuner_pitch.h"
#include "tuner_modes.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "lvgl.h"
#include <stdio.h>

#define C_BG    0x1B263B
#define C_CARD  0x24344D
#define C_TEXT  0xFFFFFF
#define C_MUTED 0x8D99AE
#define C_OK    0x2ECC71
#define C_BAD   0xE76F51
#define C_CYAN  0x4EA8DE

#define TRACK_X 20
#define TRACK_W 200
#define CENTER_X 100
#define PX_PER_CENT 2

static lv_obj_t *s_scr, *s_title, *s_batt;
static lv_obj_t *s_note;      // big note readout, montserrat_48
static lv_obj_t *s_aux;       // STR 2/6 (instruments) / RANGE C3-A4 (voice)
static lv_obj_t *s_hz;        // frequency, montserrat_20
static lv_obj_t *s_bar, *s_zone, *s_marker;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;

static tuner_mode_t s_mode = TUNER_MODE_CHROMATIC;
static tuner_strings_t s_strings;
static tuner_range_t s_range;
static bool s_frozen;
static uint8_t s_lock_cnt;
static bool s_lock_armed = true;

// ---- backlight idle management (repo power convention) ----
#define IDLE_DIM_MS 10000
static uint32_t s_idle_ms;
static bool s_dimmed;
static esp_timer_handle_t s_idle_timer;

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

static void refresh_mode_ui(void) {
    char t[32];
    snprintf(t, sizeof(t), "TUNER · %s", tuner_mode_name(s_mode));
    lv_label_set_text(s_title, t);
    if (tuner_mode_string_count(s_mode) > 0) {
        snprintf(t, sizeof(t), "STR %d/%d", s_strings.index + 1,
                 tuner_mode_string_count(s_mode));
        lv_label_set_text(s_aux, t);
    } else if (s_mode == TUNER_MODE_VOICE) {
        lv_label_set_text(s_aux, "RANGE --");
    } else {
        lv_label_set_text(s_aux, "");
    }
}

// Build the one and only screen.
void tuner_ui_init(QueueHandle_t app_q) {
    (void)app_q;
    srand((unsigned)esp_random());
    tuner_strings_reset(&s_strings);
    tuner_range_reset(&s_range);
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
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
    lv_obj_set_style_text_font(s_aux, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_aux, lv_color_hex(C_CYAN), 0);
    lv_obj_align(s_aux, LV_ALIGN_TOP_MID, 0, 128);
    lv_label_set_text(s_aux, "");

    s_hz = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hz, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_hz, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s_hz, LV_ALIGN_TOP_MID, 0, 152);
    lv_label_set_text(s_hz, "-- Hz");

    s_bar = lv_obj_create(s_scr);
    lv_obj_set_size(s_bar, TRACK_W, 14);
    lv_obj_set_pos(s_bar, TRACK_X, 220);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_CARD), 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_zone = lv_obj_create(s_bar);
    lv_obj_set_size(s_zone, (int)(TUNER_IN_TUNE_CENTS * 2 * PX_PER_CENT), 14);
    lv_obj_set_pos(s_zone, CENTER_X - (int)(TUNER_IN_TUNE_CENTS * PX_PER_CENT), 0);
    lv_obj_set_style_bg_color(s_zone, lv_color_hex(C_OK), 0);
    lv_obj_clear_flag(s_zone, LV_OBJ_FLAG_SCROLLABLE);

    s_marker = lv_obj_create(s_bar);
    lv_obj_set_size(s_marker, 12, 26);
    lv_obj_set_pos(s_marker, CENTER_X, -6);
    lv_obj_set_style_bg_color(s_marker, lv_color_hex(C_CYAN), 0);
    lv_obj_clear_flag(s_marker, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 252);
    lv_label_set_text(s_status, "");

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s_hint, "OK FREEZE  OKL MODE");

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

// OK click freezes; OK long cycles the mode (freezing any hold); UP/DOWN
// steps the target string in instrument modes.
void tuner_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    note_activity();
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        s_mode = tuner_mode_next(s_mode);
        tuner_strings_reset(&s_strings);
        tuner_range_reset(&s_range);
        s_frozen = false;
        s_lock_cnt = 0;
        s_lock_armed = true;
        lv_label_set_text(s_hint, "OK FREEZE  OKL MODE");
        lv_obj_set_style_text_color(s_hint, lv_color_hex(C_MUTED), 0);
        lv_label_set_text(s_note, "--");
        lv_label_set_text(s_hz, "-- Hz");
        set_status("", C_TEXT);
        set_marker(0.0f);
        refresh_mode_ui();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        if (tuner_mode_string_count(s_mode) > 0) {
            tuner_strings_step(&s_strings, s_mode, -1);
            s_lock_cnt = 0;
            s_lock_armed = true;
            refresh_mode_ui();
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (tuner_mode_string_count(s_mode) > 0) {
            tuner_strings_step(&s_strings, s_mode, 1);
            s_lock_cnt = 0;
            s_lock_armed = true;
            refresh_mode_ui();
        }
    } else if (btn == BSP_BTN_OK) {
        s_frozen = !s_frozen;
        lv_label_set_text(s_hint,
                          s_frozen ? "FROZEN - OK RESUME" : "OK FREEZE  OKL MODE");
        lv_obj_set_style_text_color(s_hint,
            lv_color_hex(s_frozen ? C_CYAN : C_MUTED), 0);
    }
}

// Live frame: update the readout, track the lock state, chime on lock, and
// in string modes advance to the next string after a lock.
void tuner_ui_handle_audio(const void *event) {
    note_activity();
    const tuner_event_t *ev = event;
    if (ev->kind != TUNER_EV_PITCH || s_frozen) return;

    if (ev->hz <= 0.0f) {
        lv_label_set_text(s_note, "--");
        lv_label_set_text(s_hz, "-- Hz");
        set_status("", C_TEXT);
        s_lock_cnt = 0;
        s_lock_armed = true;
        return;
    }
    if (s_mode == TUNER_MODE_VOICE) {
        tuner_range_update(&s_range, ev->midi);
    }

    int16_t ref = -1;
    float cents = reference_cents(ev, &ref);
    int count = tuner_mode_string_count(s_mode);
    if (count > 0) {
        // String modes grade against the target string and show its name.
        lv_label_set_text(s_note, ref >= 0 ? tuner_note_name(ref) : "--");
    } else {
        lv_label_set_text(s_note, tuner_note_name(ev->midi));
    }
    if (s_mode == TUNER_MODE_VOICE && s_range.lowest >= 0) {
        char t[24];
        snprintf(t, sizeof(t), "RANGE %s-%s", tuner_note_name(s_range.lowest),
                 tuner_note_name(s_range.highest));
        lv_label_set_text(s_aux, t);
    }

    char t[24];
    snprintf(t, sizeof(t), "%.1f Hz", (double)ev->hz);
    lv_label_set_text(s_hz, t);
    set_marker(cents);

    if (cents < -TUNER_IN_TUNE_CENTS) {
        set_status("LOW", C_BAD);
        s_lock_cnt = 0;
        s_lock_armed = true;
    } else if (cents > TUNER_IN_TUNE_CENTS) {
        set_status("HIGH", C_BAD);
        s_lock_cnt = 0;
        s_lock_armed = true;
    } else if (s_lock_cnt < TUNER_LOCK_FRAMES) {
        s_lock_cnt++;
        if (s_lock_cnt == TUNER_LOCK_FRAMES) {
            set_status("IN TUNE", C_OK);
            if (s_lock_armed) {
                tuner_audio_cue();
                s_lock_armed = false;   // one chime per lock
            }
            if (count > 0) {
                // String tuned: move to the next one.
                tuner_strings_advance(&s_strings, s_mode);
                s_lock_cnt = 0;
                s_lock_armed = true;
                refresh_mode_ui();
            }
        } else {
            set_status("", C_TEXT);
        }
    }
}
