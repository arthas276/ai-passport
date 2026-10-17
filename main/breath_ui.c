// breath_ui.c - LVGL screens for BREATHE: HOME / VOLUME / SESSION / SUMMARY.
//
// Redesigned application UI (mandatory-ui-redesign rule): own dark-teal
// palette, own screens and navigation - none of the baseline demo shell.
// Built only from lv_obj + labels, the widget set already proven to build
// in this repository. All handlers run on the single input task under
// bsp_lvgl_lock; the only timer is main.c's 1 Hz tick, which arrives as an
// app event so no LVGL access happens from timer callbacks.
#include "breath_ui.h"
#include "breath_store.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"
#include <stdio.h>

// Palette: deep green-black night canvas, teal accent (own scheme).
#define C_BG     0x0F1F1A
#define C_CARD   0x1C332B
#define C_INK    0xEDE8DC
#define C_DIM    0x8FA79E
#define C_ACCENT 0x53D3B5
#define C_TRACK  0x2A463C

// Session orb diameter range (px) mapped to phase progress.
#define ORB_MIN 60
#define ORB_MAX 160
#define HOME_ROWS (BREATH_PATTERN_COUNT + 1)   // patterns + VOLUME row

typedef enum { SCR_HOME = 0, SCR_VOLUME, SCR_SESSION, SCR_SUMMARY } screen_id_t;

static screen_id_t s_scr_id;
static lv_obj_t *s_scr;
static lv_obj_t *s_batt;            // current screen's battery label
static breath_save_t s_save;
static uint8_t s_sel;               // HOME selection 0..HOME_ROWS-1
static uint8_t s_vol;               // VOLUME screen live value
static uint8_t s_pattern;           // active session pattern
static bool s_stopping;             // STOP sent, waiting for EV_DONE
static bool s_dimmed;
static uint32_t s_idle_ms;
static uint32_t s_sum_cycles, s_sum_ms;

static lv_obj_t *s_cards[HOME_ROWS], *s_subs[HOME_ROWS], *s_stats;
static lv_obj_t *s_vol_val, *s_vol_ind;
static lv_obj_t *s_phase, *s_orb, *s_ring, *s_count, *s_cyc, *s_clock;

static void refresh_battery(void) {
    if (!s_batt) return;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_batt, "--%");
    } else {
        lv_label_set_text_fmt(s_batt, "%d%%", soc);
    }
}

// Tear down the active screen and return a fresh dark canvas.
static lv_obj_t *screen_create(void) {
    if (s_scr) lv_obj_delete(s_scr);
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(s_scr);
    s_batt = NULL;
    s_idle_ms = 0;
    return s_scr;
}

static void header(lv_obj_t *scr, const char *title) {
    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(C_INK), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(t, title);

    s_batt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -8, 14);
    refresh_battery();
}

static void enter_home(void);
static void enter_volume(void);
static void enter_session(void);
static void enter_summary(void);

// ---------------------------------------------------------------------------
// HOME
// ---------------------------------------------------------------------------
static void refresh_home(void) {
    for (uint8_t i = 0; i < HOME_ROWS; i++) {
        bool sel = i == s_sel;
        lv_obj_set_style_border_width(s_cards[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(C_ACCENT), 0);
    }
    lv_label_set_text_fmt(s_subs[BREATH_PATTERN_COUNT], "%u%%", s_save.vol);
    lv_label_set_text_fmt(s_stats, "%lu min  -  %lu cycles",
                          (unsigned long)s_save.minutes,
                          (unsigned long)s_save.cycles);
}

static void enter_home(void) {
    s_scr_id = SCR_HOME;
    lv_obj_t *scr = screen_create();
    header(scr, "BREATHE");

    for (uint8_t i = 0; i < HOME_ROWS; i++) {
        int y = 44 + (int)i * 52;
        s_cards[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_cards[i], 16, y);
        lv_obj_set_size(s_cards[i], 208, 46);
        lv_obj_set_style_radius(s_cards[i], 10, 0);
        lv_obj_set_style_bg_color(s_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_width(s_cards[i], 0, 0);
        lv_obj_set_style_pad_all(s_cards[i], 10, 0);
        lv_obj_remove_flag(s_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *primary = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(primary, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(primary, lv_color_hex(C_INK), 0);
        lv_obj_align(primary, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(primary, i < BREATH_PATTERN_COUNT
                                   ? BREATH_PATTERNS[i].label : "VOLUME");

        s_subs[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_subs[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_subs[i], lv_color_hex(C_DIM), 0);
        lv_obj_align(s_subs[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_label_set_text(s_subs[i], i < BREATH_PATTERN_COUNT
                                     ? BREATH_PATTERNS[i].sub : "");
    }

    s_stats = lv_label_create(scr);
    lv_obj_set_style_text_font(s_stats, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_stats, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_stats, LV_ALIGN_TOP_MID, 0, 272);
    refresh_home();
}

// ---------------------------------------------------------------------------
// VOLUME
// ---------------------------------------------------------------------------
static void refresh_volume(void) {
    lv_label_set_text_fmt(s_vol_val, "%u", s_vol);
    lv_obj_set_width(s_vol_ind, 180 * s_vol / 100);
}

static void enter_volume(void) {
    s_scr_id = SCR_VOLUME;
    s_vol = s_save.vol;
    lv_obj_t *scr = screen_create();
    header(scr, "VOLUME");

    s_vol_val = lv_label_create(scr);
    lv_obj_set_style_text_font(s_vol_val, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_vol_val, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(s_vol_val, LV_ALIGN_TOP_MID, 0, 80);
    lv_label_set_text(s_vol_val, "60");

    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_set_pos(track, 30, 190);
    lv_obj_set_size(track, 180, 12);
    lv_obj_set_style_radius(track, 6, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    s_vol_ind = lv_obj_create(track);
    lv_obj_set_pos(s_vol_ind, 0, 0);
    lv_obj_set_height(s_vol_ind, 12);
    lv_obj_set_style_radius(s_vol_ind, 6, 0);
    lv_obj_set_style_bg_color(s_vol_ind, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(s_vol_ind, 0, 0);
    lv_obj_remove_flag(s_vol_ind, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(C_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(hint, "UP/DOWN: SET   OK: BACK");
    refresh_volume();
}

// ---------------------------------------------------------------------------
// SESSION
// ---------------------------------------------------------------------------
static const char *phase_word(breath_phase_t ph) {
    switch (ph) {
    case BREATH_PHASE_READY:   return "GET READY";
    case BREATH_PHASE_INHALE:  return "BREATHE IN";
    case BREATH_PHASE_HOLD:    return "HOLD";
    case BREATH_PHASE_EXHALE:  return "BREATHE OUT";
    default:                   return "HOLD";
    }
}

static void update_session(const breath_state_t *st) {
    lv_label_set_text(s_phase, phase_word(st->phase));

    int d;
    switch (st->phase) {
    case BREATH_PHASE_INHALE:  d = ORB_MIN + (ORB_MAX - ORB_MIN) * st->progress / 4096; break;
    case BREATH_PHASE_HOLD:    d = ORB_MAX; break;
    case BREATH_PHASE_EXHALE:  d = ORB_MAX - (ORB_MAX - ORB_MIN) * st->progress / 4096; break;
    default:                   d = ORB_MIN; break;   // READY / HOLD2 rest
    }
    lv_obj_set_size(s_orb, d, d);
    lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, 6);

    lv_label_set_text_fmt(s_count, "%u", (st->phase_left_ms + 999u) / 1000u);
    lv_label_set_text_fmt(s_cyc, "C %lu", (unsigned long)st->cycle);
    lv_label_set_text_fmt(s_clock, "%lu:%02lu",
                          (unsigned long)(st->elapsed_ms / 60000u),
                          (unsigned long)(st->elapsed_ms / 1000u % 60u));
}

static void enter_session(void) {
    s_scr_id = SCR_SESSION;
    s_stopping = false;
    lv_obj_t *scr = screen_create();
    bsp_display_backlight(100);   // the session screen is the whole point
    s_dimmed = false;

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(C_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 14);
    lv_label_set_text(hint, "OK LONG: END");

    s_batt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -8, 14);
    refresh_battery();

    s_phase = lv_label_create(scr);
    lv_obj_set_style_text_font(s_phase, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_phase, lv_color_hex(C_INK), 0);
    lv_obj_align(s_phase, LV_ALIGN_TOP_MID, 0, 44);
    lv_label_set_text(s_phase, "GET READY");

    // Static rest ring; the growing/shrinking orb inside carries progress.
    s_ring = lv_obj_create(scr);
    lv_obj_set_size(s_ring, 176, 176);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ring, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(C_TRACK), 0);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_SCROLLABLE);

    s_orb = lv_obj_create(scr);
    lv_obj_set_size(s_orb, ORB_MIN, ORB_MIN);
    lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_radius(s_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_orb, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(s_orb, 0, 0);
    lv_obj_remove_flag(s_orb, LV_OBJ_FLAG_SCROLLABLE);

    s_count = lv_label_create(scr);
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(C_INK), 0);
    lv_obj_align(s_count, LV_ALIGN_TOP_MID, 0, 246);
    lv_label_set_text(s_count, "3");

    s_cyc = lv_label_create(scr);
    lv_obj_set_style_text_font(s_cyc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cyc, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_cyc, LV_ALIGN_BOTTOM_LEFT, 14, -12);
    lv_label_set_text(s_cyc, "C 0");

    s_clock = lv_label_create(scr);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_clock, LV_ALIGN_BOTTOM_RIGHT, -14, -12);
    lv_label_set_text(s_clock, "0:00");

    breath_state_t st;
    breath_engine_at(&BREATH_PATTERNS[s_pattern], 0, &st);
    update_session(&st);
}

// ---------------------------------------------------------------------------
// SUMMARY
// ---------------------------------------------------------------------------
static void enter_summary(void) {
    s_scr_id = SCR_SUMMARY;
    lv_obj_t *scr = screen_create();
    header(scr, "SESSION DONE");

    lv_obj_t *big = lv_label_create(scr);
    lv_obj_set_style_text_font(big, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(big, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(big, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_text_fmt(big, "%lu", (unsigned long)s_sum_cycles);

    lv_obj_t *unit = lv_label_create(scr);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(C_DIM), 0);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 128);
    lv_label_set_text(unit, "CYCLES");

    lv_obj_t *time = lv_label_create(scr);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time, lv_color_hex(C_INK), 0);
    lv_obj_align(time, LV_ALIGN_TOP_MID, 0, 168);
    lv_label_set_text_fmt(time, "%lu:%02lu",
                          (unsigned long)(s_sum_ms / 60000u),
                          (unsigned long)(s_sum_ms / 1000u % 60u));

    lv_obj_t *tot = lv_label_create(scr);
    lv_obj_set_style_text_font(tot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tot, lv_color_hex(C_DIM), 0);
    lv_obj_align(tot, LV_ALIGN_TOP_MID, 0, 214);
    lv_label_set_text_fmt(tot, "TOTAL %lu MIN  -  %lu CYC",
                          (unsigned long)s_save.minutes,
                          (unsigned long)s_save.cycles);

    lv_obj_t *ses = lv_label_create(scr);
    lv_obj_set_style_text_font(ses, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ses, lv_color_hex(C_DIM), 0);
    lv_obj_align(ses, LV_ALIGN_TOP_MID, 0, 236);
    lv_label_set_text_fmt(ses, "%lu SESSIONS", (unsigned long)s_save.sessions);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(C_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(hint, "OK: HOME");
}

// ---------------------------------------------------------------------------
// Event handlers (single input task, under bsp_lvgl_lock)
// ---------------------------------------------------------------------------
static void wake(void) {
    s_idle_ms = 0;
    if (s_dimmed) {
        bsp_display_backlight(100);
        s_dimmed = false;
    }
}

void breath_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    wake();

    if (s_scr_id == SCR_SESSION) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK && !s_stopping) {
            s_stopping = true;
            breath_audio_session_stop();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;

    switch (s_scr_id) {
    case SCR_HOME:
        if (btn == BSP_BTN_UP) {
            s_sel = (uint8_t)((s_sel + HOME_ROWS - 1) % HOME_ROWS);
            refresh_home();
        } else if (btn == BSP_BTN_DOWN) {
            s_sel = (uint8_t)((s_sel + 1) % HOME_ROWS);
            refresh_home();
        } else if (btn == BSP_BTN_OK) {
            if (s_sel < BREATH_PATTERN_COUNT) {
                s_pattern = s_sel;
                breath_audio_session_start(s_pattern);
                enter_session();
            } else {
                enter_volume();
            }
        }
        break;

    case SCR_VOLUME:
        if (btn == BSP_BTN_UP && s_vol <= 90) {
            s_vol = (uint8_t)(s_vol + 10);
            refresh_volume();
        } else if (btn == BSP_BTN_DOWN && s_vol >= 10) {
            s_vol = (uint8_t)(s_vol - 10);
            refresh_volume();
        } else if (btn == BSP_BTN_OK) {
            s_save.vol = s_vol;
            breath_audio_set_volume(s_vol);
            breath_store_save(&s_save);   // playback is idle here (8.1)
            enter_home();
        }
        break;

    case SCR_SUMMARY:
        if (btn == BSP_BTN_OK) enter_home();
        break;

    default:
        break;
    }
}

void breath_ui_handle_audio(const breath_audio_event_t *ev) {
    if (s_scr_id != SCR_SESSION) return;   // stale tick after exit
    if (ev->kind == BREATH_AUDIO_EV_TICK) {
        update_session(&ev->st);
        return;
    }
    // EV_DONE: chime has played; persist stats once, outside playback.
    s_sum_cycles = ev->st.cycle;
    s_sum_ms = ev->st.elapsed_ms;
    s_save.sessions++;
    s_save.minutes += breath_engine_minutes(s_sum_ms);
    s_save.cycles += s_sum_cycles;
    breath_store_save(&s_save);
    enter_summary();
}

void breath_ui_on_second(void) {
    refresh_battery();
    if (s_scr_id == SCR_SESSION) return;   // never dim the guide screen
    s_idle_ms += 1000;
    if (!s_dimmed && s_idle_ms >= 10000) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
}

void breath_ui_init(void) {
    breath_store_load(&s_save);
    breath_audio_set_volume(s_save.vol);
    enter_home();
    bsp_display_backlight(100);
    s_dimmed = false;
}
