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
#define HOME_ROWS (BREATH_PATTERN_COUNT + 1)   // patterns + SETUP row
#define SETUP_ROW (BREATH_PATTERN_COUNT)
#define SETTINGS_ROWS 4u   // DURATION / GLOW / VOLUME / YOURS (v1.3)
#define EDIT_ROWS 4u       // IN / HOLD / OUT / HOLD2 (v1.3)

#define GLOW_LEVELS 3u
static const uint8_t GLOW_PCT[GLOW_LEVELS] = { 100u, 12u, 0u };
static const char *const GLOW_NAMES[GLOW_LEVELS] =
    { "BRIGHT", "NIGHT", "DARK" };
static const char *const DUR_NAMES[4] = { "FREE", "1 MIN", "3 MIN", "5 MIN" };
static const uint8_t DUR_VALUES[4] = { 0u, 1u, 3u, 5u };

static uint8_t dur_index(uint8_t dur_min) {
    for (uint8_t i = 0; i < 4u; i++) {
        if (DUR_VALUES[i] == dur_min) return i;
    }
    return 0u;
}

static uint8_t dur_next(uint8_t dur_min) {
    return DUR_VALUES[(dur_index(dur_min) + 1u) % 4u];
}

typedef enum { SCR_HOME = 0, SCR_SETTINGS, SCR_VOLUME, SCR_EDIT,
               SCR_SESSION, SCR_SUMMARY } screen_id_t;

static screen_id_t s_scr_id;
static lv_obj_t *s_scr;
static lv_obj_t *s_batt;            // current screen's battery label
static breath_save_t s_save;
static uint8_t s_sel;               // HOME selection 0..HOME_ROWS-1
static uint8_t s_set_sel;           // SETTINGS selection 0..SETTINGS_ROWS-1
static uint8_t s_vol;               // VOLUME screen live value
static uint8_t s_pattern;           // active session pattern
static uint8_t s_dur;               // active session duration (minutes)
static uint8_t s_edit_sel;          // EDIT selection 0..EDIT_ROWS-1 (v1.3)
static uint8_t s_edit[EDIT_ROWS];   // EDIT live seconds (v1.3)
static bool s_stopping;             // STOP sent, waiting for EV_DONE
static bool s_dimmed;
static uint32_t s_idle_ms;
static uint32_t s_sum_cycles, s_sum_ms;
static bool s_milestone_hit;        // SUMMARY: a milestone was crossed
static uint32_t s_milestone_min;    // the threshold that was reached

static lv_obj_t *s_cards[HOME_ROWS], *s_subs[HOME_ROWS], *s_stats;
static lv_obj_t *s_set_cards[SETTINGS_ROWS], *s_set_subs[SETTINGS_ROWS];
static lv_obj_t *s_set_pill;
static lv_obj_t *s_edit_cards[EDIT_ROWS], *s_edit_subs[EDIT_ROWS];
static lv_obj_t *s_edit_pill;
static lv_obj_t *s_vol_val, *s_vol_ind, *s_vol_pill, *s_sum_pill;
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
static void enter_settings(void);
static void enter_volume(void);
static void enter_edit(void);
static void enter_session(void);
static void enter_summary(void);

// v1.3: live sub text for the YOURS row ("5 + 5" / "4 + 2 + 6 + 2").
// A static buffer so the engine's copied pattern keeps a stable pointer.
static char s_custom_sub[24];

static void custom_sub_rebuild(void) {
    size_t n = (size_t)snprintf(s_custom_sub, sizeof(s_custom_sub), "%u",
                                s_save.c_in);
    if (s_save.c_hold) {
        n += (size_t)snprintf(s_custom_sub + n, sizeof(s_custom_sub) - n,
                              " + %u", s_save.c_hold);
    }
    n += (size_t)snprintf(s_custom_sub + n, sizeof(s_custom_sub) - n,
                          " + %u", s_save.c_out);
    if (s_save.c_hold2) {
        n += (size_t)snprintf(s_custom_sub + n, sizeof(s_custom_sub) - n,
                              " + %u", s_save.c_hold2);
    }
    (void)n;
}

// Push the saved YOURS pattern into the engine runtime table and refresh
// the sub shown on HOME and SETTINGS. Called at boot and after each edit.
static void apply_custom(void) {
    breath_pattern_t p = {
        .label = "YOURS",
        .sub = s_custom_sub,
        .inhale_ms = (uint16_t)(s_save.c_in * 1000u),
        .hold_ms = (uint16_t)(s_save.c_hold * 1000u),
        .inhale2_ms = 0,   // classic four-phase shape
        .exhale_ms = (uint16_t)(s_save.c_out * 1000u),
        .hold2_ms = (uint16_t)(s_save.c_hold2 * 1000u),
    };
    custom_sub_rebuild();
    breath_engine_set_custom(&p);   // store bounds make this always true
}

// Bottom hint pill: 232x34 rounded bar, one centered montserrat_14 line.
// Every line must stay inside the 204 px text budget; tests/
// test_hint_budget.py freezes the font advance table and re-measures each
// hint_pill_set line against it.
static lv_obj_t *hint_pill_create(lv_obj_t *parent) {
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 232, 34);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(C_TRACK), 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(pill);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 216);
    lv_obj_center(label);
    return pill;
}

static void hint_pill_set(lv_obj_t *pill, const char *text) {
    if (!pill) return;
    lv_obj_t *label = lv_obj_get_child(pill, 0);
    if (label) lv_label_set_text(label, text);
}

// ---------------------------------------------------------------------------
// HOME
// ---------------------------------------------------------------------------
static void refresh_home(void) {
    for (uint8_t i = 0; i < HOME_ROWS; i++) {
        bool sel = i == s_sel;
        lv_obj_set_style_border_width(s_cards[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(C_ACCENT), 0);
    }
    char sub[24];
    snprintf(sub, sizeof(sub), "%s - %s", DUR_NAMES[dur_index(s_save.dur_min)],
             GLOW_NAMES[s_save.glow]);
    lv_label_set_text(s_subs[SETUP_ROW],
                      s_save.dur_min || s_save.glow ? sub : "FREE");
    // YOURS shows the live custom summary, not the engine default copy.
    lv_label_set_text(s_subs[BREATH_PATTERN_CUSTOM], s_custom_sub);
    lv_label_set_text_fmt(s_stats, "%lu min  -  %lu cycles",
                          (unsigned long)s_save.minutes,
                          (unsigned long)s_save.cycles);
}

static void enter_home(void) {
    s_scr_id = SCR_HOME;
    lv_obj_t *scr = screen_create();
    header(scr, "BREATHE");

    // v1.3: 8 rows (7 patterns + SETUP) at a 33 px pitch in 30 px cards;
    // montserrat_20 (line height 23) fits the 24 px inner band with pad 3.
    for (uint8_t i = 0; i < HOME_ROWS; i++) {
        int y = 38 + (int)i * 33;
        s_cards[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_cards[i], 16, y);
        lv_obj_set_size(s_cards[i], 208, 30);
        lv_obj_set_style_radius(s_cards[i], 8, 0);
        lv_obj_set_style_bg_color(s_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_width(s_cards[i], 0, 0);
        lv_obj_set_style_pad_all(s_cards[i], 3, 0);
        lv_obj_remove_flag(s_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *primary = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(primary, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(primary, lv_color_hex(C_INK), 0);
        lv_obj_align(primary, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(primary, i < BREATH_PATTERN_COUNT
                                   ? BREATH_PATTERNS[i].label : "SETUP");

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
    lv_obj_align(s_stats, LV_ALIGN_TOP_MID, 0, 302);
    refresh_home();
}

// ---------------------------------------------------------------------------
// SETTINGS (v1.2): DURATION / GLOW / VOLUME rows; v1.3 adds YOURS. UP/DOWN
// walk the rows; OK on DURATION/GLOW cycles the value directly (saved
// outside playback); OK on VOLUME opens the big adjust screen; OK on YOURS
// opens the pace editor.
// ---------------------------------------------------------------------------
static void refresh_settings(void) {
    for (uint8_t i = 0; i < SETTINGS_ROWS; i++) {
        bool sel = i == s_set_sel;
        lv_obj_set_style_border_width(s_set_cards[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_set_cards[i], lv_color_hex(C_ACCENT), 0);
    }
    lv_label_set_text(s_set_subs[0], DUR_NAMES[dur_index(s_save.dur_min)]);
    lv_label_set_text(s_set_subs[1], GLOW_NAMES[s_save.glow]);
    lv_label_set_text_fmt(s_set_subs[2], "%u%%", s_save.vol);
    lv_label_set_text(s_set_subs[3], s_custom_sub);
}

static void enter_settings(void) {
    s_scr_id = SCR_SETTINGS;
    lv_obj_t *scr = screen_create();
    header(scr, "SETUP");

    static const char *const NAMES[SETTINGS_ROWS] =
        { "DURATION", "GLOW", "VOLUME", "YOURS" };
    for (uint8_t i = 0; i < SETTINGS_ROWS; i++) {
        int y = 56 + (int)i * 50;
        s_set_cards[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_set_cards[i], 16, y);
        lv_obj_set_size(s_set_cards[i], 208, 44);
        lv_obj_set_style_radius(s_set_cards[i], 10, 0);
        lv_obj_set_style_bg_color(s_set_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_width(s_set_cards[i], 0, 0);
        lv_obj_set_style_pad_all(s_set_cards[i], 10, 0);
        lv_obj_remove_flag(s_set_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *primary = lv_label_create(s_set_cards[i]);
        lv_obj_set_style_text_font(primary, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(primary, lv_color_hex(C_INK), 0);
        lv_obj_align(primary, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(primary, NAMES[i]);

        s_set_subs[i] = lv_label_create(s_set_cards[i]);
        lv_obj_set_style_text_font(s_set_subs[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_set_subs[i], lv_color_hex(C_DIM), 0);
        lv_obj_align(s_set_subs[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_label_set_text(s_set_subs[i], "");
    }

    s_set_pill = hint_pill_create(scr);
    hint_pill_set(s_set_pill, "OK: SET   OK LONG: BACK");
    refresh_settings();
}

// ---------------------------------------------------------------------------
// EDIT (v1.3): YOURS pace editor. Four value rows (IN/HOLD/OUT/HOLD2 in
// seconds); UP/DOWN adjust the selected row with wrap (no dead keys at
// the bounds), OK advances to the next row, OK on the last row saves and
// returns to SETUP; long-press OK discards and returns.
// ---------------------------------------------------------------------------
static void refresh_edit(void) {
    for (uint8_t i = 0; i < EDIT_ROWS; i++) {
        bool sel = i == s_edit_sel;
        lv_obj_set_style_border_width(s_edit_cards[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_edit_cards[i], lv_color_hex(C_ACCENT), 0);
        lv_label_set_text_fmt(s_edit_subs[i], "%u s", s_edit[i]);
    }
    hint_pill_set(s_edit_pill, s_edit_sel < EDIT_ROWS - 1u
                                  ? "UP/DOWN: SEC  OK: NEXT"
                                  : "OK: SAVE  LONG: CANCEL");
}

static void enter_edit(void) {
    s_scr_id = SCR_EDIT;
    s_edit_sel = 0;
    s_edit[0] = s_save.c_in;
    s_edit[1] = s_save.c_hold;
    s_edit[2] = s_save.c_out;
    s_edit[3] = s_save.c_hold2;
    lv_obj_t *scr = screen_create();
    header(scr, "YOURS");

    static const char *const NAMES[EDIT_ROWS] = { "IN", "HOLD", "OUT", "HOLD2" };
    for (uint8_t i = 0; i < EDIT_ROWS; i++) {
        int y = 48 + (int)i * 56;
        s_edit_cards[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_edit_cards[i], 16, y);
        lv_obj_set_size(s_edit_cards[i], 208, 48);
        lv_obj_set_style_radius(s_edit_cards[i], 10, 0);
        lv_obj_set_style_bg_color(s_edit_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_width(s_edit_cards[i], 0, 0);
        lv_obj_set_style_pad_all(s_edit_cards[i], 10, 0);
        lv_obj_remove_flag(s_edit_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *primary = lv_label_create(s_edit_cards[i]);
        lv_obj_set_style_text_font(primary, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(primary, lv_color_hex(C_INK), 0);
        lv_obj_align(primary, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(primary, NAMES[i]);

        s_edit_subs[i] = lv_label_create(s_edit_cards[i]);
        lv_obj_set_style_text_font(s_edit_subs[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_edit_subs[i], lv_color_hex(C_DIM), 0);
        lv_obj_align(s_edit_subs[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_label_set_text(s_edit_subs[i], "");
    }

    s_edit_pill = hint_pill_create(scr);
    refresh_edit();
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

    s_vol_pill = hint_pill_create(scr);
    hint_pill_set(s_vol_pill, "UP/DOWN: SET   OK: BACK");
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
    case BREATH_PHASE_INHALE2: return "TOP UP";
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
    // Top-up inhale: resume from three quarters of the span and finish the
    // growth, so a sigh reads as one continuous rise with a second step.
    case BREATH_PHASE_INHALE2: d = ORB_MIN + (ORB_MAX - ORB_MIN) * (12288u + st->progress) / 16384u; break;
    case BREATH_PHASE_EXHALE:  d = ORB_MAX - (ORB_MAX - ORB_MIN) * st->progress / 4096; break;
    default:                   d = ORB_MIN; break;   // READY / HOLD2 rest
    }
    lv_obj_set_size(s_orb, d, d);
    lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, 6);

    lv_label_set_text_fmt(s_count, "%u", (st->phase_left_ms + 999u) / 1000u);
    lv_label_set_text_fmt(s_cyc, "C %lu", (unsigned long)st->cycle);
    if (s_dur) {
        uint32_t left = (uint32_t)s_dur * 60000u + BREATH_READY_MS;
        left = st->elapsed_ms >= left ? 0u : left - st->elapsed_ms;
        lv_label_set_text_fmt(s_clock, "-%lu:%02lu",
                              (unsigned long)(left / 60000u),
                              (unsigned long)(left / 1000u % 60u));
    } else {
        lv_label_set_text_fmt(s_clock, "%lu:%02lu",
                              (unsigned long)(st->elapsed_ms / 60000u),
                              (unsigned long)(st->elapsed_ms / 1000u % 60u));
    }
}

static void enter_session(void) {
    s_scr_id = SCR_SESSION;
    s_stopping = false;
    lv_obj_t *scr = screen_create();
    bsp_display_backlight(100);   // settle in bright; GLOW dims after idle
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
    breath_engine_at(breath_engine_pattern(s_pattern), 0, &st);
    update_session(&st);
}

// ---------------------------------------------------------------------------
// SUMMARY
// ---------------------------------------------------------------------------
static void enter_summary(void) {
    s_scr_id = SCR_SUMMARY;
    lv_obj_t *scr = screen_create();
    bsp_display_backlight(100);   // DARK sessions end on a readable page
    s_dimmed = false;
    header(scr, "SESSION DONE");

    lv_obj_t *big = lv_label_create(scr);
    lv_obj_set_style_text_font(big, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(big, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(big, LV_ALIGN_TOP_MID, 0, 56);
    lv_label_set_text_fmt(big, "%lu", (unsigned long)s_sum_cycles);

    lv_obj_t *unit = lv_label_create(scr);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(C_DIM), 0);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 112);
    lv_label_set_text(unit, "CYCLES");

    lv_obj_t *time = lv_label_create(scr);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time, lv_color_hex(C_INK), 0);
    lv_obj_align(time, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_text_fmt(time, "%lu:%02lu",
                          (unsigned long)(s_sum_ms / 60000u),
                          (unsigned long)(s_sum_ms / 1000u % 60u));

    // Breathing pace this session (v1.2): cycles per guided minute, the
    // number FLOW players try to hold near 5.5.
    if (s_sum_ms >= 60000u) {
        lv_obj_t *pace = lv_label_create(scr);
        lv_obj_set_style_text_font(pace, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pace, lv_color_hex(C_DIM), 0);
        lv_obj_align(pace, LV_ALIGN_TOP_MID, 0, 168);
        lv_label_set_text_fmt(pace, "PACE %lu.%lu / MIN",
                              (unsigned long)(s_sum_cycles * 60u /
                                              (s_sum_ms / 1000u)),
                              (unsigned long)(s_sum_cycles * 600u /
                                              (s_sum_ms / 1000u) % 10u));
    }

    lv_obj_t *tot = lv_label_create(scr);
    lv_obj_set_style_text_font(tot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tot, lv_color_hex(C_DIM), 0);
    lv_obj_align(tot, LV_ALIGN_TOP_MID, 0, 194);
    lv_label_set_text_fmt(tot, "TOTAL %lu MIN  -  %lu CYC",
                          (unsigned long)s_save.minutes,
                          (unsigned long)s_save.cycles);

    lv_obj_t *ses = lv_label_create(scr);
    lv_obj_set_style_text_font(ses, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ses, lv_color_hex(C_DIM), 0);
    lv_obj_align(ses, LV_ALIGN_TOP_MID, 0, 216);
    lv_label_set_text_fmt(ses, "%lu SESSIONS", (unsigned long)s_save.sessions);

    if (s_milestone_hit) {
        lv_obj_t *ms_label = lv_label_create(scr);
        lv_obj_set_style_text_font(ms_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ms_label, lv_color_hex(C_ACCENT), 0);
        lv_obj_align(ms_label, LV_ALIGN_TOP_MID, 0, 232);
        lv_label_set_text(ms_label, "MILESTONE");

        lv_obj_t *ms_val = lv_label_create(scr);
        lv_obj_set_style_text_font(ms_val, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ms_val, lv_color_hex(C_ACCENT), 0);
        lv_obj_align(ms_val, LV_ALIGN_TOP_MID, 0, 250);
        lv_label_set_text_fmt(ms_val, "%lu MIN", (unsigned long)s_milestone_min);
    }

    s_sum_pill = hint_pill_create(scr);
    hint_pill_set(s_sum_pill, "OK: HOME");
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
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        // VOLUME discards its live value; SETTINGS returns HOME; EDIT
        // discards the pace edits and returns to SETTINGS.
        if (s_scr_id == SCR_VOLUME) enter_settings();
        else if (s_scr_id == SCR_SETTINGS) enter_home();
        else if (s_scr_id == SCR_EDIT) enter_settings();
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
                s_dur = s_save.dur_min;
                breath_audio_session_start(s_pattern, s_dur);
                enter_session();
            } else {
                s_set_sel = 0;
                enter_settings();
            }
        }
        break;

    case SCR_SETTINGS:
        if (btn == BSP_BTN_UP) {
            s_set_sel = (uint8_t)((s_set_sel + SETTINGS_ROWS - 1) % SETTINGS_ROWS);
            refresh_settings();
        } else if (btn == BSP_BTN_DOWN) {
            s_set_sel = (uint8_t)((s_set_sel + 1) % SETTINGS_ROWS);
            refresh_settings();
        } else if (btn == BSP_BTN_OK) {
            if (s_set_sel == 0) {
                s_save.dur_min = dur_next(s_save.dur_min);
            } else if (s_set_sel == 1) {
                s_save.glow = (uint8_t)((s_save.glow + 1u) % GLOW_LEVELS);
            } else if (s_set_sel == 2) {
                enter_volume();
            } else {
                enter_edit();   // v1.3: YOURS pace editor
                break;
            }
            breath_store_save(&s_save);   // playback is idle here (8.1)
            refresh_settings();
        }
        break;

    case SCR_EDIT:
        // Bounds wrap (1..10 in/out, 0..10 holds) so no key press is a
        // silent no-op at an edge; a wrap is visible as a value change.
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            const int8_t d = btn == BSP_BTN_UP ? 1 : -1;
            const uint8_t lo = (s_edit_sel == 0 || s_edit_sel == 2) ? 1u : 0u;
            const uint8_t span = 10u - lo + 1u;
            s_edit[s_edit_sel] = (uint8_t)(lo + ((int)s_edit[s_edit_sel] - lo +
                                    d + (int)span) % (int)span);
            refresh_edit();
        } else if (btn == BSP_BTN_OK) {
            if (s_edit_sel < EDIT_ROWS - 1u) {
                s_edit_sel++;
                refresh_edit();
            } else {
                s_save.c_in = s_edit[0];
                s_save.c_hold = s_edit[1];
                s_save.c_out = s_edit[2];
                s_save.c_hold2 = s_edit[3];
                breath_store_save(&s_save);   // playback is idle here
                apply_custom();
                enter_settings();
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
            enter_settings();
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
    s_milestone_hit = s_save.minutes >= s_save.milestone;
    if (s_milestone_hit) {
        s_milestone_min = s_save.milestone;
        s_save.milestone = breath_engine_next_milestone(s_save.minutes);
    }
    breath_store_save(&s_save);
    enter_summary();
}

void breath_ui_on_second(void) {
    refresh_battery();
    s_idle_ms += 1000;
    if (s_idle_ms < 10000u || s_dimmed) return;
    if (s_scr_id == SCR_SESSION && s_save.glow != 0u) {
        // Night/dark sessions: settle to the configured glow level; any
        // key peeks back at full brightness for another idle window.
        bsp_display_backlight(GLOW_PCT[s_save.glow]);
        s_dimmed = true;
    } else if (s_scr_id != SCR_SESSION) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
}

void breath_ui_init(void) {
    breath_store_load(&s_save);
    breath_engine_init();      // runtime table defaults before any use
    apply_custom();            // store -> YOURS slot + live sub
    breath_audio_set_volume(s_save.vol);
    enter_home();
    bsp_display_backlight(100);
    s_dimmed = false;
}
