// ear_ui.c - EAR BUDDY screens. All public handlers must be called under
// bsp_lvgl_lock (from the input task in main.c).
#include "ear_ui.h"
#include "ear_assets.h"
#include "ear_duet.h"
#include "ear_dictation.h"
#include "ear_vs.h"
#include "ear_store.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ear_ui";   // duet diagnostics via serial_peek


// ---- palette (C_BG must match tools/gen_pet_assets.py PANEL_BG) ----
#define C_BG     0x1B263B   // dark navy background
#define C_CARD   0x24344D   // panels/cards
#define CELL_BG  0x2C3E5F   // quiz option cells
#define C_TEXT   0xFFFFFF
#define C_MUTED  0x8D99AE
#define C_OK     0x2ECC71
#define C_BAD    0xE76F51
#define C_GOLD   0xFFD166
#define C_CYAN   0x4EA8DE

// ---- persistent app state ----
static ear_save_t s_save;        // progress + settings, loaded at boot
static QueueHandle_t s_app_q;    // deferred events (advance) - set in init
static bool s_evolve_pending;    // stage changed, celebrate at HOME entry

// ---- current screen ----
typedef enum {
    SCR_HOME, SCR_QUIZ, SCR_SING, SCR_DUET, SCR_DICTATION, SCR_RESULT, SCR_SETUP,
    SCR_RESET,
} ear_screen_t;
static ear_screen_t s_scr;

// ---- quiz flow state ----
static ear_session_t s_sess;
static ear_question_t s_q;
static bool s_awaiting_answer;   // options active, waiting for OK
static bool s_advancing;         // next PLAY_DONE advances the question
static uint32_t s_sess_xp;       // XP gained this session (RESULT page)
static bool s_vs;                // quiz screen is a VS BATTLE match (v1.7)
static ear_vs_t s_vsst;          // VS turn/score state for the match

// ---- home mode card (includes SETUP as 4th entry) ----
static uint8_t s_mode_card;

// ---- LVGL objects per screen (only the active screen objects exist) ----
static lv_obj_t *h_scr, *h_title, *h_batt, *h_pet, *h_bar, *h_xp_lbl;
static lv_obj_t *h_mode_lbl, *h_hint;
static lv_obj_t *q_scr, *q_hdr, *q_status;
static lv_obj_t *q_cells[EAR_OPTS_MAX], *q_cells_lbl[EAR_OPTS_MAX];
static lv_obj_t *q_footer;
static uint8_t s_cursor;          // selected quiz option index
static lv_obj_t *sg_scr, *sg_note, *sg_scale, *sg_marker, *sg_hold;
static lv_obj_t *sg_status, *sg_hint;
static lv_obj_t *d_scr, *d_title, *d_pet, *d_notes, *d_key, *d_status, *d_hint;
static lv_obj_t *dc_scr, *dc_title, *dc_prog, *dc_pet, *dc_notes, *dc_sung, *dc_status, *dc_hint;
static lv_obj_t *r_scr, *r_score, *r_xp, *r_tot;
static lv_obj_t *s1_scr, *s1_vol, *s1_reset, *s1_hint, *s1_batt;
static lv_obj_t *rs_scr, *rs_title, *rs_warn, *rs_hint;   // RESET confirm page

// ---- helpers ----
// Null-safe label update; objects of inactive screens do not exist.
static void lbl(lv_obj_t *obj, const char *text, uint32_t color) {
    if (!obj) return;
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Add XP, persist immediately (no-punishment: only ever positive), and flag
// an evolve celebration when the stage changes.
static void award_xp(uint32_t amount) {
    uint32_t before = s_save.xp;
    s_save.xp += amount;
    ear_store_save(&s_save);
    if (ear_pet_stage(before) != ear_pet_stage(s_save.xp)) {
        s_evolve_pending = true;
    }
}

// ---- backlight idle management ----
#define IDLE_DIM_MS 10000
static uint32_t s_idle_ms;             // ms since last input/audio activity
static bool s_dimmed;                  // backlight currently at 30%
static esp_timer_handle_t s_idle_timer;

// esp_timer runs in its own task: LEDC calls are thread-safe, LVGL is not.
// Only touch the backlight here; activity resets come from event handlers.
static void idle_timer_cb(void *arg) {
    (void)arg;
    s_idle_ms += 1000;
    if (s_idle_ms >= IDLE_DIM_MS && !s_dimmed) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
}

// Reset the idle window; restore full brightness if currently dimmed.
static void note_activity(void) {
    s_idle_ms = 0;
    if (s_dimmed) {
        bsp_display_backlight(100);
        s_dimmed = false;
    }
}

// ---- forward declarations (later screens referenced earlier) ----
static void enter_result(void);
static void enter_vs_result(void);
static void enter_result_common(uint8_t score, uint8_t total, uint32_t xp);
static void grade_answer(void);
static void enter_sing(void);
static void enter_duet(void);
static void enter_dictation(void);
static void enter_setup(void);

// ---- HOME ----
// Mode card names (index = s_mode_card: 0 INTERVAL 1 CHORD 2 SING 3 DUET
// 4 DICTATION 5 VS 6 SETUP).
static const char *MODE_NAMES[7] = {
    "MODE: INTERVAL", "MODE: CHORD", "MODE: SING", "MODE: DUET",
    "MODE: DICTATION", "MODE: VS BATTLE",
    "MODE: SETUP",
};

// Battery text: --% when CW2017 read fails (graceful degrade).
static void battery_text(char *out, size_t n) {
    int soc = bsp_battery_soc();
    if (soc < 0) {
        snprintf(out, n, "--%%");
    } else {
        snprintf(out, n, "%d%%", soc);
    }
}

// Refresh HOME dynamic parts (pet sprite, XP bar/text, mode card highlight).
static void home_refresh(void) {
    ear_stage_t st = ear_pet_stage(s_save.xp);
    if (h_pet) lv_image_set_src(h_pet, &EAR_PET_IMG[st][0]);
    uint32_t floor_v = ear_pet_stage_floor(s_save.xp);
    uint32_t ceil_v = ear_pet_stage_ceiling(s_save.xp);
    if (ceil_v > floor_v) {
        lv_bar_set_range(h_bar, 0, 100);
        lv_bar_set_value(h_bar, (int)((s_save.xp - floor_v) * 100 /
                                      (ceil_v - floor_v)), LV_ANIM_OFF);
    } else {
        lv_bar_set_value(h_bar, 100, LV_ANIM_OFF);   // final stage: full bar
    }
    char t[64];
    snprintf(t, sizeof(t), "XP %lu  next %lu", (unsigned long)s_save.xp,
             (unsigned long)ear_pet_xp_to_next(s_save.xp));
    lbl(h_xp_lbl, t, C_MUTED);
    if (h_mode_lbl) lbl(h_mode_lbl, MODE_NAMES[s_mode_card], C_GOLD);
}

// Build (or rebuild) the HOME screen and load it.
static void enter_home(void) {
    if (h_scr) { lv_obj_delete(h_scr); h_scr = NULL; }
    s_scr = SCR_HOME;
    s_vs = false;                   // any HOME entry ends a VS match (v1.7)
    h_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(h_scr, lv_color_hex(C_BG), 0);

    h_title = lv_label_create(h_scr);
    lv_label_set_text(h_title, "EAR BUDDY");
    lv_obj_set_style_text_font(h_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(h_title, lv_color_hex(C_TEXT), 0);
    lv_obj_align(h_title, LV_ALIGN_TOP_MID, 0, 8);

    h_batt = lv_label_create(h_scr);
    lv_obj_set_style_text_font(h_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_batt, lv_color_hex(C_MUTED), 0);
    lv_obj_align(h_batt, LV_ALIGN_TOP_RIGHT, -8, 12);
    char t[16];
    battery_text(t, sizeof(t));
    lv_label_set_text(h_batt, t);

    // Pet sprite: stage image, centered under the title.
    h_pet = lv_image_create(h_scr);
    lv_image_set_src(h_pet, &EAR_PET_IMG[ear_pet_stage(s_save.xp)][0]);
    lv_obj_align(h_pet, LV_ALIGN_TOP_MID, 0, 64);

    h_bar = lv_bar_create(h_scr);
    lv_obj_set_size(h_bar, 180, 10);
    lv_obj_align(h_bar, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_color(h_bar, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_color(h_bar, lv_color_hex(C_OK), LV_PART_INDICATOR);

    h_xp_lbl = lv_label_create(h_scr);
    lv_obj_set_style_text_font(h_xp_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(h_xp_lbl, LV_ALIGN_TOP_MID, 0, 148);

    h_mode_lbl = lv_label_create(h_scr);
    lv_obj_set_style_text_font(h_mode_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(h_mode_lbl, LV_ALIGN_TOP_MID, 0, 214);

    // Evolve celebration: sprite swap happens in home_refresh; fanfare once.
    if (s_evolve_pending) {
        s_evolve_pending = false;
        ear_audio_play_cue(2);
    }

    h_hint = lv_label_create(h_scr);
    lv_obj_set_style_text_font(h_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(h_hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(h_hint, "UP/DOWN MODE  OK START");
    lv_screen_load(h_scr);
    home_refresh();
}

// ---- QUIZ ----
// Update the option grid visuals: cursor border + answered-state colors.
static void quiz_grid_refresh(void) {
    uint8_t n = EAR_LEVEL_OPT_COUNT[s_sess.mode][s_sess.level - 1];
    for (uint8_t i = 0; i < n; i++) {
        if (!q_cells[i]) continue;
        lv_obj_set_style_border_color(q_cells[i], lv_color_hex(
            i == s_cursor ? C_CYAN : C_CARD), 0);
    }
}

// Show/hide option cells per level option count.
static void quiz_grid_build(void) {
    uint8_t n = EAR_LEVEL_OPT_COUNT[s_sess.mode][s_sess.level - 1];
    for (uint8_t i = 0; i < EAR_OPTS_MAX; i++) {
        if (!q_cells[i]) continue;
        if (i < n) lv_obj_clear_flag(q_cells[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(q_cells[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Quiz header: "Q n/10 LVx" solo; VS adds the answering player and the
// running score (v1.7).
static void quiz_refresh_hdr(void) {
    char hdr[48];
    if (s_vs) {
        snprintf(hdr, sizeof(hdr), "Q %u/%u P%u %u:%u",
                 (unsigned)(s_vsst.index + 1u),
                 (unsigned)EAR_VS_QUESTIONS,
                 (unsigned)(ear_vs_player(&s_vsst) + 1u),
                 (unsigned)s_vsst.p1_score, (unsigned)s_vsst.p2_score);
        lbl(q_hdr, hdr, C_GOLD);
    } else {
        snprintf(hdr, sizeof(hdr), "Q %u/%u LV%u",
                 (unsigned)(s_sess.index + 1),
                 (unsigned)EAR_QUESTIONS_PER_SESSION,
                 (unsigned)s_sess.level);
        lbl(q_hdr, hdr, C_MUTED);
    }
}

// Load the next question: state, labels, grid, and start playback.
static void quiz_load_question(void) {
    ear_model_next_question(&s_sess, &s_q);
    s_cursor = 0;
    s_awaiting_answer = true;
    s_advancing = false;
    quiz_grid_build();
    // Option labels from the kind table.
    uint8_t n = EAR_LEVEL_OPT_COUNT[s_sess.mode][s_sess.level - 1];
    for (uint8_t i = 0; i < n; i++) {
        lbl(q_cells_lbl[i], EAR_KINDS[s_q.options[i]].label, C_TEXT);
    }
    quiz_refresh_hdr();
    lbl(q_status, "LISTEN...", C_CYAN);
    ear_audio_play_question(s_q.root, s_q.answer, s_q.dir);
}

// Enter the quiz screen for a mode (INTERVAL or CHORD).
static void enter_quiz(ear_mode_t mode) {
    uint8_t lvl = (mode == EAR_MODE_INTERVAL) ? s_save.ilvl : s_save.clvl;
    ear_model_session_init(&s_sess, mode, lvl);
    s_sess_xp = 0;
    s_vs = false;                   // solo entry; enter_vs() re-arms the flag
    if (q_scr) { lv_obj_delete(q_scr); q_scr = NULL; }
    s_scr = SCR_QUIZ;
    q_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(q_scr, lv_color_hex(C_BG), 0);

    q_hdr = lv_label_create(q_scr);
    lv_obj_set_style_text_font(q_hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(q_hdr, LV_ALIGN_TOP_MID, 0, 8);

    q_status = lv_label_create(q_scr);
    lv_obj_set_style_text_font(q_status, &lv_font_montserrat_14, 0);
    lv_obj_align(q_status, LV_ALIGN_TOP_MID, 0, 32);

    // Option cell layout per level (240 wide; two 100px columns + margins).
    static const uint8_t LAYOUT[3][EAR_OPTS_MAX][2] = {
        {{36,120},{126,120}},
        {{36,96},{126,96},{36,150},{126,150}},
        {{36,84},{126,84},{36,138},{126,138},{36,192},{126,192}},
    };
    for (uint8_t i = 0; i < EAR_OPTS_MAX; i++) {
        q_cells[i] = lv_obj_create(q_scr);
        lv_obj_set_size(q_cells[i], 100, 44);
        lv_obj_set_pos(q_cells[i], LAYOUT[s_sess.level - 1][i][0],
                       LAYOUT[s_sess.level - 1][i][1]);
        lv_obj_set_style_bg_color(q_cells[i], lv_color_hex(CELL_BG), 0);
        lv_obj_set_style_border_width(q_cells[i], 2, 0);
        lv_obj_set_style_border_color(q_cells[i], lv_color_hex(C_CARD), 0);
        lv_obj_clear_flag(q_cells[i], LV_OBJ_FLAG_SCROLLABLE);

        q_cells_lbl[i] = lv_label_create(q_cells[i]);
        lv_obj_set_style_text_font(q_cells_lbl[i], &lv_font_montserrat_20,
                                   0);
        lv_obj_center(q_cells_lbl[i]);
    }
    // Cells beyond the level count start hidden; grid_build manages this.
    quiz_grid_build();

    q_footer = lv_label_create(q_scr);
    lv_obj_set_style_text_font(q_footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(q_footer, lv_color_hex(C_MUTED), 0);
    lv_obj_align(q_footer, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(q_footer, "UP/DOWN SELECT  OK CONFIRM");
    lv_screen_load(q_scr);
    quiz_load_question();
}

// ---- VS BATTLE (v1.7) ----
// One device, two players: 10 interval questions alternate P1/P2 at the
// current interval level. Every correct answer feeds the shared pet +1 XP;
// no perfect bonus and no level adjustment - levels stay solo-driven.
static void enter_vs(void) {
    enter_quiz(EAR_MODE_INTERVAL);   // builds the screen, resets s_vs, Q1
    ear_vs_begin(&s_vsst, EAR_MODE_INTERVAL, s_save.ilvl);
    s_vs = true;
    quiz_refresh_hdr();              // retitle Q1 with the P1 turn header
}

// Advance to the next question or the result page (VS: its own verdict).
static void quiz_advance(void) {
    s_sess.index++;
    if (s_vs) {
        if (ear_vs_done(&s_vsst)) {
            enter_vs_result();
            return;
        }
        quiz_load_question();
        return;
    }
    if (s_sess.index >= EAR_QUESTIONS_PER_SESSION) {
        enter_result();
        return;
    }
    quiz_load_question();
}

// Grade on OK; UP-long replays; OK-long quits (earned XP stays).
static void quiz_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            ear_audio_sing_cancel();   // abort pending playback, then exit
            enter_home();
            return;
        }
        if (btn == BSP_BTN_UP && s_awaiting_answer) {
            ear_audio_play_question(s_q.root, s_q.answer, s_q.dir);   // replay
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    uint8_t n = EAR_LEVEL_OPT_COUNT[s_sess.mode][s_sess.level - 1];
    if (btn == BSP_BTN_UP) {
        s_cursor = (uint8_t)((s_cursor + n - 1) % n);
        quiz_grid_refresh();
    } else if (btn == BSP_BTN_DOWN) {
        s_cursor = (uint8_t)((s_cursor + 1) % n);
        quiz_grid_refresh();
    } else if (btn == BSP_BTN_OK && s_awaiting_answer) {
        grade_answer();
    }
}

// Grade the selected option: award XP / highlight + replay, then advance on
// the corresponding PLAY_DONE event.
static void grade_answer(void) {
    s_awaiting_answer = false;
    bool correct = ear_model_grade(&s_sess, &s_q, s_cursor);
    if (s_vs) ear_vs_record(&s_vsst, correct);   // v1.7 VS scoring
    if (correct) {
        s_save.answered++;            // lifetime stats (v1.2)
        if (!s_vs && s_sess.combo > s_save.best_combo) {
            s_save.best_combo = s_sess.combo;   // combo stays a solo stat
        }
        award_xp(EAR_XP_CORRECT);
        s_sess_xp += EAR_XP_CORRECT;
        ear_audio_play_cue(0);
        lbl(q_status, "RIGHT! +1 XP", C_OK);
        lv_obj_set_style_border_color(q_cells[s_cursor],
                                      lv_color_hex(C_OK), 0);
        lv_obj_set_style_text_color(q_cells_lbl[s_cursor],
                                    lv_color_hex(C_OK), 0);
    } else {
        ear_audio_play_cue(1);
        ear_audio_play_question(s_q.root, s_q.answer, s_q.dir);   // teach: replay
        char t[40];
        snprintf(t, sizeof(t), "ANSWER: %s", EAR_KINDS[s_q.answer].label);
        lbl(q_status, t, C_BAD);
        lv_obj_set_style_border_color(q_cells[s_cursor],
                                      lv_color_hex(C_BAD), 0);
        lv_obj_set_style_text_color(q_cells_lbl[s_cursor],
                                    lv_color_hex(C_BAD), 0);
        // Teaching highlight: show which cell was correct (green border).
        for (uint8_t i = 0; i < EAR_OPTS_MAX; i++) {
            if (q_cells[i] && s_q.options[i] == (uint8_t)s_q.answer) {
                lv_obj_set_style_border_color(q_cells[i],
                                              lv_color_hex(C_OK), 0);
            }
        }
    }
    s_advancing = true;
}

// PLAY_DONE advances (after cue/replay) or clears the LISTEN gate.
static void quiz_audio(ear_audio_event_t *ev) {
    if (ev->kind == EAR_AUDIO_EV_PLAY_DONE) {
        if (s_advancing) {
            s_advancing = false;
            quiz_advance();
        } else {
            lbl(q_status, "", C_TEXT);
            quiz_grid_refresh();
        }
    }
}

// ---- SING ----
static uint8_t s_sing_target;     // MIDI target note 60..69
static uint8_t s_sing_fails;      // consecutive failed attempts on this note
static bool s_showing;            // replaying target after 3 fails
static uint8_t s_sing_index;      // note 1..5 of this sing session (PRD 3.2)
static uint8_t s_sing_score;      // passes this sing session
static bool s_sing_advancing;     // pass cue playing: advance when done
// Per-level tolerance in cents (PRD 3.2 v1.1). Device passes 2026-09-29 /
// 2026-10-04: even after the octave-error fix + median-3 smoothing,
// +/-65/45/30 still stalled beginners at L1; loosened one notch, L3 stays
// demanding.
static const float SING_TOL[3] = {80.0f, 55.0f, 35.0f};

// Build the sing screen and arm a fresh target note.
static void enter_sing(void) {
    if (sg_scr) { lv_obj_delete(sg_scr); sg_scr = NULL; }
    s_scr = SCR_SING;
    sg_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(sg_scr, lv_color_hex(C_BG), 0);

    sg_note = lv_label_create(sg_scr);
    lv_obj_set_style_text_font(sg_note, &lv_font_montserrat_20, 0);
    lv_obj_align(sg_note, LV_ALIGN_TOP_MID, 0, 20);

    sg_scale = lv_obj_create(sg_scr);
    lv_obj_set_size(sg_scale, 200, 14);
    lv_obj_align(sg_scale, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_color(sg_scale, lv_color_hex(C_CARD), 0);
    lv_obj_clear_flag(sg_scale, LV_OBJ_FLAG_SCROLLABLE);

    sg_marker = lv_obj_create(sg_scale);
    lv_obj_set_size(sg_marker, 12, 26);
    lv_obj_set_style_bg_color(sg_marker, lv_color_hex(C_CYAN), 0);
    lv_obj_set_y(sg_marker, -6);

    sg_hold = lv_bar_create(sg_scr);
    lv_obj_set_size(sg_hold, 180, 10);
    lv_obj_align(sg_hold, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(sg_hold, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_color(sg_hold, lv_color_hex(C_OK), LV_PART_INDICATOR);
    sg_status = lv_label_create(sg_scr);
    lv_obj_set_style_text_font(sg_status, &lv_font_montserrat_14, 0);
    lv_obj_align(sg_status, LV_ALIGN_TOP_MID, 0, 190);
    sg_hint = lv_label_create(sg_scr);
    lv_obj_set_style_text_font(sg_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sg_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(sg_hint, "OK START  UP REPLAY  OKL EXIT");
    lv_screen_load(sg_scr);
    s_sing_index = 1;
    s_sing_score = 0;
    s_sing_advancing = false;
    s_sess_xp = 0;                // sing result reports its own XP (v1.2)
    s_sing_target = 60 + (rand() % 10);
    s_sing_fails = 0;
    ear_audio_play_target(s_sing_target);
    char t[24];
    snprintf(t, sizeof(t), "SING %u/5: %s", (unsigned)s_sing_index,
             ear_note_name(s_sing_target));
    lbl(sg_note, t, C_GOLD);
    lbl(sg_status, "LISTEN TO TARGET", C_CYAN);
}

// Advance the sing session: next of 5 notes, or the shared result page.
static void sing_advance(void) {
    if (s_sing_index >= 5) {
        enter_result_common(s_sing_score, 5, s_sess_xp);
        return;
    }
    s_sing_index++;
    s_sing_target = 60 + (rand() % 10);
    s_sing_fails = 0;
    ear_audio_play_target(s_sing_target);
    char t[24];
    snprintf(t, sizeof(t), "SING %u/5: %s", (unsigned)s_sing_index,
             ear_note_name(s_sing_target));
    lbl(sg_note, t, C_GOLD);
    lbl(sg_status, "LISTEN TO TARGET", C_CYAN);
    lv_obj_set_x(sg_marker, 94);          // recenter marker on the scale
    lv_bar_set_value(sg_hold, 0, LV_ANIM_OFF);
}

// Sing screen keys: UP replays target, OK starts an attempt, OK-long exits.
static void sing_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            ear_audio_sing_cancel();
            enter_home();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) {
        ear_audio_play_target(s_sing_target);
    } else if (btn == BSP_BTN_OK) {
        float tol = SING_TOL[s_save.slvl - 1];
        ear_audio_sing_start(ear_note_freq(s_sing_target), tol);
        lbl(sg_status, "SING NOW!", C_GOLD);
    }
}

static void sing_audio(ear_audio_event_t *ev) {
    if (ev->kind == EAR_AUDIO_EV_PLAY_DONE) {
        if (s_showing) {            // answer demo done: next note / result
            s_showing = false;
            sing_advance();
            return;
        }
        if (s_sing_advancing) {     // pass cue done: next note / result
            s_sing_advancing = false;
            sing_advance();
            return;
        }
    }
    if (ev->kind == EAR_AUDIO_EV_PITCH) {
        if (ev->hz > 0.0f) {
            int x = 94 + (int)(ev->cents * 2.0f);
            lv_obj_set_x(sg_marker, clampi(x, 0, 188));
            char t[40];
            if (ev->oct != 0) {     // right note, other octave (v1.2)
                snprintf(t, sizeof(t), "%+.0fc %s", (double)ev->cents,
                         ev->oct < 0 ? "OCT-LOW" : "OCT-HIGH");
                lbl(sg_status, t, C_CYAN);
            } else {
                snprintf(t, sizeof(t), "%+.0f cents", (double)ev->cents);
                lbl(sg_status, t, C_TEXT);
            }
        } else {
            lbl(sg_status, "SING!", C_MUTED);
        }
    }

    if (ev->kind == EAR_AUDIO_EV_SING_PASS) {
        award_xp(EAR_XP_SING_PASS);
        s_sess_xp += EAR_XP_SING_PASS;
        s_sing_score++;
        s_save.sing_pass++;          // lifetime stats (v1.2)
        ear_audio_play_cue(0);
        s_sing_advancing = true;    // after the cue, move on
        lbl(sg_status, "PASS +2 XP", C_OK);
    }

    if (ev->kind == EAR_AUDIO_EV_SING_FAIL) {
        s_sing_fails++;
        if (s_sing_fails >= 3) {
            // Show answer: replay the target, then move to a new note.
            ear_audio_play_target(s_sing_target);
            lbl(sg_status, "ANSWER SHOWN", C_BAD);
            s_showing = true;   // after replay, PLAY_DONE advances
        } else {
            lbl(sg_status, "TRY AGAIN", C_BAD);
        }
    }
}

// ---- DUET (v1.3, PRD 3.3) ----
// Five states on one screen: READY -> LISTEN -> REVIEW -> PLAY -> DONE.
static uint8_t s_duet_state;
#define DUET_READY  0u
#define DUET_LISTEN 1u
#define DUET_REVIEW 2u
#define DUET_PLAY   3u
#define DUET_DONE   4u
static ear_duet_key_t s_duet_key;
static uint8_t s_duet_harmony[EAR_DUET_MAX_NOTES];
static uint8_t s_duet_pet_frame;

// Note label for any MIDI value (ear_note_name only covers C4-A4):
// "C4", "A#3", ... into out (>= 5 bytes).
static void duet_note_label(char *out, size_t n, uint8_t midi) {
    snprintf(out, n, "%s%u", ear_duet_pc_name(midi % 12),
             (unsigned)(midi / 12 - 1));
}

// Harmony tier text shown on READY/DONE, gated by pet stage (PRD 3.3).
static const char *duet_tier_text(void) {
    switch (ear_pet_stage(s_save.xp)) {
    case EAR_STAGE_EGG:
    case EAR_STAGE_HATCHLING: return "HARMONY: UNISON";
    case EAR_STAGE_CHICK:     return "HARMONY: 3RD";
    case EAR_STAGE_FLEDGLING: return "HARMONY: 6TH";
    default:                  return "HARMONY: LEAD";
    }
}

static void duet_refresh_ready(void) {
    s_duet_state = DUET_READY;
    lbl(d_title, duet_tier_text(), C_GOLD);
    lbl(d_notes, "SING A PHRASE 2-4s", C_TEXT);
    lbl(d_key, "", C_MUTED);
    lbl(d_status, "", C_TEXT);
    lbl(d_hint, "OK SING  OKL EXIT", C_MUTED);
    lv_image_set_src(d_pet,
        &EAR_PET_IMG[ear_pet_stage(s_save.xp)][0]);
    lv_obj_set_y(d_pet, 84);
}

static void enter_duet(void) {
    if (d_scr) { lv_obj_delete(d_scr); d_scr = NULL; }
    s_scr = SCR_DUET;
    d_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(d_scr, lv_color_hex(C_BG), 0);

    d_title = lv_label_create(d_scr);
    lv_obj_set_style_text_font(d_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(d_title, lv_color_hex(C_GOLD), 0);
    lv_obj_align(d_title, LV_ALIGN_TOP_MID, 0, 14);

    d_pet = lv_image_create(d_scr);
    lv_obj_align(d_pet, LV_ALIGN_TOP_MID, 0, 84);

    d_notes = lv_label_create(d_scr);
    lv_obj_set_style_text_font(d_notes, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d_notes, lv_color_hex(C_TEXT), 0);
    lv_obj_set_width(d_notes, 220);
    lv_obj_set_style_text_align(d_notes, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(d_notes, LV_ALIGN_TOP_MID, 0, 150);

    d_key = lv_label_create(d_scr);
    lv_obj_set_style_text_font(d_key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d_key, lv_color_hex(C_CYAN), 0);
    lv_obj_align(d_key, LV_ALIGN_TOP_MID, 0, 205);

    d_status = lv_label_create(d_scr);
    lv_obj_set_style_text_font(d_status, &lv_font_montserrat_20, 0);
    lv_obj_align(d_status, LV_ALIGN_TOP_MID, 0, 240);

    d_hint = lv_label_create(d_scr);
    lv_obj_set_style_text_font(d_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(d_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_screen_load(d_scr);
    s_duet_pet_frame = 0;
    duet_refresh_ready();
}

// Transcription + key into the REVIEW labels; harmony precomputed here so
// playback is a single worker job.
static void duet_review(void) {
    const ear_duet_phrase_t *p = ear_audio_duet_phrase();
    ear_duet_infer_key(p, &s_duet_key);
    ear_duet_harmony(p, &s_duet_key, ear_pet_stage(s_save.xp),
                     s_duet_harmony);
    char line[EAR_DUET_MAX_NOTES * 5];
    size_t off = 0;
    line[0] = 0;
    for (uint8_t i = 0; i < p->count && off + 5 < sizeof(line); i++) {
        char nb[5];
        duet_note_label(nb, sizeof(nb), p->notes[i].midi);
        off += (size_t)snprintf(line + off, sizeof(line) - off,
                                i ? " %s" : "%s", nb);
    }
    lbl(d_notes, line, C_TEXT);
    char ktxt[24];
    snprintf(ktxt, sizeof(ktxt), "KEY %s%s %s",
             s_duet_key.fallback ? "~" : "",
             ear_duet_pc_name(s_duet_key.tonic),
             s_duet_key.minor ? "min" : "maj");
    lbl(d_key, ktxt, C_CYAN);
    ESP_LOGI(TAG, "duet review: %u notes key=%s%u%s harmonies:",
             (unsigned)p->count, s_duet_key.fallback ? "~" : "",
             (unsigned)s_duet_key.tonic, s_duet_key.minor ? "min" : "maj");
    for (uint8_t i = 0; i < p->count; i++) {
        ESP_LOGI(TAG, "  note %u: midi=%u ms=%u harm=%u", (unsigned)i + 1,
                 (unsigned)p->notes[i].midi, (unsigned)p->notes[i].ms,
                 (unsigned)s_duet_harmony[i]);
    }
    lbl(d_status, "", C_TEXT);
    lbl(d_hint, "OK PLAY UP RESING OKL EXIT", C_MUTED);
    s_duet_state = DUET_REVIEW;
}

static void duet_key_ev(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            ear_audio_sing_cancel();
            enter_home();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP && s_duet_state == DUET_REVIEW) {
        ear_audio_duet_capture();   // re-sing the phrase
        s_duet_state = DUET_LISTEN;
        lbl(d_status, "SING AGAIN 2-4s", C_GOLD);
        lbl(d_hint, "OKL EXIT", C_MUTED);
        return;
    }
    if (btn != BSP_BTN_OK) return;
    switch (s_duet_state) {
    case DUET_READY:
        ear_audio_duet_capture();
        s_duet_state = DUET_LISTEN;
        lbl(d_status, "SING NOW 2-4s", C_GOLD);
        lbl(d_hint, "OKL EXIT", C_MUTED);
        break;
    case DUET_REVIEW:
        s_duet_state = DUET_PLAY;
        lbl(d_status, "DUET...", C_CYAN);
        lbl(d_hint, "OKL EXIT", C_MUTED);
        ear_audio_duet_play(ear_audio_duet_phrase(), s_duet_harmony);
        break;
    case DUET_DONE:
        duet_refresh_ready();   // next round without rebuilding the screen
        break;
    default:
        break;   // LISTEN/PLAY: capture and playback run to their own ends
    }
}

static void duet_audio_ev(ear_audio_event_t *ev) {
    if (ev->kind == EAR_AUDIO_EV_PITCH && s_duet_state == DUET_LISTEN) {
        if (ev->hz > 0.0f) {
            char nb[5];
            duet_note_label(nb, sizeof(nb), ear_note_from_hz(ev->hz));
            char t[32];
            snprintf(t, sizeof(t), "NOTES %u  %s", (unsigned)ev->pct, nb);
            lbl(d_status, t, C_TEXT);
        } else {
            lbl(d_status, "...", C_MUTED);
        }
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_DUET_PHRASE) {
        if (ev->pct < EAR_DUET_MIN_NOTES) {
            lbl(d_status, "TOO SHORT", C_BAD);
            lbl(d_hint, "OK RETRY  OKL EXIT", C_MUTED);
            s_duet_state = DUET_READY;   // OK restarts the capture
            return;
        }
        duet_review();
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_DUET_NOTE && s_duet_state == DUET_PLAY) {
        // Pet hop: alternate sprite frames + a small vertical bounce.
        s_duet_pet_frame ^= 1u;
        lv_image_set_src(d_pet,
            &EAR_PET_IMG[ear_pet_stage(s_save.xp)][s_duet_pet_frame]);
        lv_obj_set_y(d_pet, s_duet_pet_frame ? 72 : 84);
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_PLAY_DONE && s_duet_state == DUET_PLAY) {
        s_duet_pet_frame = 0;
        lv_image_set_src(d_pet,
            &EAR_PET_IMG[ear_pet_stage(s_save.xp)][0]);
        lv_obj_set_y(d_pet, 84);
        award_xp(EAR_XP_DUET);
        s_save.duets++;               // lifetime stat (v1.3)
        ear_store_save(&s_save);
        char t[48];
        snprintf(t, sizeof(t), "+%u XP  DUETS %lu", (unsigned)EAR_XP_DUET,
                 (unsigned long)s_save.duets);
        lbl(d_status, t, C_OK);
        lbl(d_hint, "OK NEXT ROUND", C_MUTED);
        s_duet_state = DUET_DONE;
        ear_audio_play_cue(0);
    }
}

// ---- DICTATION (v1.4, PRD 3.4) ----
// Five states on one screen: READY -> LISTEN -> ECHO -> SING -> GRADE, ten
// melodies per session. Playback ticks and the sung-back transcription
// reuse the duet worker events (EV_DUET_NOTE / EV_DUET_PHRASE): the audio
// chain is identical, only the grading differs.
static uint8_t s_dict_state;
#define DICT_READY  0u
#define DICT_LISTEN 1u
#define DICT_ECHO   2u
#define DICT_SING   3u
#define DICT_GRADE  4u
static ear_dictation_session_t s_dict;
static ear_dictation_melody_t s_dict_mel;
static uint8_t s_dict_pet_frame;

// "C4 E G" style list for a MIDI array (target or sung-back).
static void note_list_text(char *out, size_t n, const uint8_t *midis,
                           uint8_t count) {
    size_t off = 0;
    out[0] = 0;
    for (uint8_t i = 0; i < count && off + 6 < n; i++) {
        char nb[5];
        duet_note_label(nb, sizeof(nb), midis[i]);
        off += (size_t)snprintf(out + off, n - off, i ? " %s" : "%s", nb);
    }
}

// "C4 D4 --" style list for a graded phrase: midi 0 prints as "--", so a
// position nothing was sung for reads as a gap (v1.4 tuning: the player sees
// exactly which notes were heard).
static void dict_mark_text(char *out, size_t n, const uint8_t *midis,
                           uint8_t count) {
    size_t off = 0;
    out[0] = 0;
    for (uint8_t i = 0; i < count && off + 6 < n; i++) {
        char nb[5];
        if (midis[i] == 0) {
            snprintf(nb, sizeof(nb), "--");
        } else {
            duet_note_label(nb, sizeof(nb), midis[i]);
        }
        off += (size_t)snprintf(out + off, n - off, i ? " %s" : "%s", nb);
    }
}

// READY: melody shown, waiting for OK to play it.
static void dict_refresh_ready(void) {
    s_dict_state = DICT_READY;
    char p[48];
    snprintf(p, sizeof(p), "MELODY %u/%u  LV%u %s",
             (unsigned)(s_dict.index + 1),
             (unsigned)EAR_QUESTIONS_PER_SESSION, (unsigned)s_dict.level,
             EAR_DICT_TOL_NAME[s_dict.level - 1]);
    lbl(dc_prog, p, C_MUTED);
    char mbuf[EAR_DICTATION_MAX_NOTES * 6];
    note_list_text(mbuf, sizeof(mbuf), s_dict_mel.notes, s_dict_mel.count);
    lbl(dc_notes, mbuf, C_TEXT);
    lbl(dc_sung, "", C_MUTED);
    lbl(dc_status, "LISTEN", C_CYAN);
    lbl(dc_hint, "OK LISTEN  OKL EXIT", C_MUTED);
    s_dict_pet_frame = 0;
    lv_image_set_src(dc_pet, &EAR_PET_IMG[ear_pet_stage(s_save.xp)][0]);
    lv_obj_set_y(dc_pet, 70);
}

// LISTEN: melody playback; the pet hops on each EV_DUET_NOTE tick.
static void dict_start_listen(void) {
    s_dict_state = DICT_LISTEN;
    lbl(dc_status, "LISTEN...", C_CYAN);
    lbl(dc_hint, "OKL EXIT", C_MUTED);
    ear_audio_play_melody(s_dict_mel.notes, s_dict_mel.count,
                          EAR_DICTATION_NOTE_MS[s_dict.level - 1]);
}

// ECHO: playback done, melody stays on screen as the target to reproduce.
static void dict_show_echo(void) {
    s_dict_state = DICT_ECHO;
    lbl(dc_sung, "", C_MUTED);
    lbl(dc_status, "SING IT BACK", C_GOLD);
    lbl(dc_hint, "OK SING  UP REPLAY  OKL EXIT", C_MUTED);
}

// SING: capture the sung-back melody (duet chain), grade on phrase end.
// The capture knows the melody length, so a thinking pause cannot cut the
// tail off the phrase.
static void dict_start_sing(void) {
    s_dict_state = DICT_SING;
    lbl(dc_status, "SING NOW", C_GOLD);
    lbl(dc_hint, "OKL EXIT", C_MUTED);
    ear_audio_dict_capture(s_dict_mel.count);
}

// Grade the transcription: XP per correct melody (+perfect), lifetime stat,
// verdict labels. Fewer than EAR_DICTATION_MIN_NOTES notes -> retry. The
// target line and the heard line stay side by side so the player can see
// which notes were missed, and half the melody right still earns a point
// (v1.4 tuning: exact grading alone was too discouraging).
static void dict_grade(void) {
    const ear_duet_phrase_t *sung = ear_audio_dict_phrase();
    if (sung->count < EAR_DICTATION_MIN_NOTES) {
        lbl(dc_status, "TOO SHORT", C_BAD);
        lbl(dc_hint, "OK RETRY  OKL EXIT", C_MUTED);
        lbl(dc_sung, "", C_MUTED);
        s_dict_state = DICT_ECHO;
        return;
    }
    ear_dictation_result_t res;
    ear_dictation_grade(&s_dict_mel, sung, s_dict.level, &res);
    bool correct = ear_dictation_melody_correct(&res);
    bool perfect = ear_dictation_melody_perfect(&res);

    // TARGET <melody> on the notes line, YOU <heard> on the sung line.
    char tlist[EAR_DICTATION_MAX_NOTES * 6];
    char slist[EAR_DICTATION_MAX_NOTES * 6];
    uint8_t heard[EAR_DICTATION_MAX_NOTES];
    note_list_text(tlist, sizeof(tlist), s_dict_mel.notes, s_dict_mel.count);
    for (uint8_t i = 0; i < res.count; i++) heard[i] = res.notes[i].sung;
    dict_mark_text(slist, sizeof(slist), heard, res.count);
    char tbuf[EAR_DICTATION_MAX_NOTES * 6 + 12];
    char sbuf[EAR_DICTATION_MAX_NOTES * 6 + 12];
    snprintf(tbuf, sizeof(tbuf), "TARGET %s", tlist);
    snprintf(sbuf, sizeof(sbuf), "YOU    %s", slist);
    lbl(dc_notes, tbuf, C_TEXT);
    lbl(dc_sung, sbuf, correct ? C_OK : C_BAD);

    char t[40];
    if (correct) {
        s_dict.score++;
        s_save.melodies++;            // lifetime stat (v1.4)
        uint32_t gain = EAR_XP_DICT_CORRECT;
        if (perfect) gain += EAR_XP_DICT_PERFECT;
        award_xp(gain);               // also persists the lifetime stat
        s_sess_xp += gain;
        snprintf(t, sizeof(t), "%s +%lu XP", perfect ? "PERFECT!" : "RIGHT!",
                 (unsigned long)gain);
        lbl(dc_status, t, C_OK);
        ear_audio_play_cue(0);
    } else if (res.correct_notes * 2u >= s_dict_mel.count) {
        uint32_t gain = EAR_XP_DICT_PARTIAL;   // half the melody right
        award_xp(gain);
        s_sess_xp += gain;
        snprintf(t, sizeof(t), "CLOSE %u/%u +%lu XP",
                 (unsigned)res.correct_notes, (unsigned)s_dict_mel.count,
                 (unsigned long)gain);
        lbl(dc_status, t, C_GOLD);
        ear_audio_play_cue(1);
    } else {
        snprintf(t, sizeof(t), "WRONG %u/%u", (unsigned)res.correct_notes,
                 (unsigned)s_dict_mel.count);
        lbl(dc_status, t, C_BAD);
        ear_audio_play_cue(1);
    }
    lbl(dc_hint, "OK NEXT", C_MUTED);
    s_dict_state = DICT_GRADE;
}

// Next melody, or session end after EAR_QUESTIONS_PER_SESSION.
static void dict_next(void) {
    s_dict.index++;
    if (s_dict.index >= EAR_QUESTIONS_PER_SESSION) {
        bool changed = false;
        uint8_t new_lvl = auto_level_adjust(s_save.dlvl, s_dict.score,
                                           &changed);
        if (changed) s_save.dlvl = new_lvl;
        ear_store_save(&s_save);
        enter_result_common(s_dict.score, EAR_QUESTIONS_PER_SESSION, s_sess_xp);
        return;
    }
    ear_dictation_next_melody(&s_dict, &s_dict_mel);
    dict_refresh_ready();
}

static void enter_dictation(void) {
    if (dc_scr) { lv_obj_delete(dc_scr); dc_scr = NULL; }
    s_scr = SCR_DICTATION;
    dc_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dc_scr, lv_color_hex(C_BG), 0);

    dc_title = lv_label_create(dc_scr);
    lv_label_set_text(dc_title, "DICTATION");
    lv_obj_set_style_text_font(dc_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(dc_title, lv_color_hex(C_GOLD), 0);
    lv_obj_align(dc_title, LV_ALIGN_TOP_MID, 0, 14);

    dc_prog = lv_label_create(dc_scr);
    lv_obj_set_style_text_font(dc_prog, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dc_prog, lv_color_hex(C_MUTED), 0);
    lv_obj_align(dc_prog, LV_ALIGN_TOP_MID, 0, 46);

    dc_pet = lv_image_create(dc_scr);
    lv_obj_align(dc_pet, LV_ALIGN_TOP_MID, 0, 70);

    dc_notes = lv_label_create(dc_scr);
    lv_obj_set_style_text_font(dc_notes, &lv_font_montserrat_14, 0);
    lv_obj_set_width(dc_notes, 220);
    lv_obj_set_style_text_align(dc_notes, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dc_notes, LV_ALIGN_TOP_MID, 0, 150);

    // Second line, GRADE only: what was heard, under the target melody.
    dc_sung = lv_label_create(dc_scr);
    lv_obj_set_style_text_font(dc_sung, &lv_font_montserrat_14, 0);
    lv_obj_set_width(dc_sung, 220);
    lv_obj_set_style_text_align(dc_sung, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dc_sung, LV_ALIGN_TOP_MID, 0, 174);

    dc_status = lv_label_create(dc_scr);
    lv_obj_set_style_text_font(dc_status, &lv_font_montserrat_20, 0);
    lv_obj_align(dc_status, LV_ALIGN_TOP_MID, 0, 210);

    dc_hint = lv_label_create(dc_scr);
    lv_obj_set_style_text_font(dc_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dc_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(dc_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_screen_load(dc_scr);
    ear_dictation_session_init(&s_dict, s_save.dlvl, esp_random());
    ear_dictation_next_melody(&s_dict, &s_dict_mel);
    s_sess_xp = 0;                    // dictation result reports its own XP
    s_dict_pet_frame = 0;
    dict_refresh_ready();
}

// Dictation keys: OKL always exits (earned XP stays); state-dependent OK/UP.
static void dict_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            ear_audio_sing_cancel();  // abort pending playback/capture
            enter_home();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    switch (s_dict_state) {
    case DICT_READY:
        if (btn == BSP_BTN_OK) dict_start_listen();
        break;
    case DICT_ECHO:
        if (btn == BSP_BTN_OK) dict_start_sing();
        else if (btn == BSP_BTN_UP) dict_start_listen();
        break;
    case DICT_GRADE:
        if (btn == BSP_BTN_OK) dict_next();
        break;
    default:
        break;   // LISTEN/SING run to their own ends
    }
}

static void dict_audio_ev(ear_audio_event_t *ev) {
    if (ev->kind == EAR_AUDIO_EV_DUET_NOTE && s_dict_state == DICT_LISTEN) {
        // Pet hop: alternate sprite frames + a small vertical bounce.
        s_dict_pet_frame ^= 1u;
        lv_image_set_src(dc_pet,
            &EAR_PET_IMG[ear_pet_stage(s_save.xp)][s_dict_pet_frame]);
        lv_obj_set_y(dc_pet, s_dict_pet_frame ? 58 : 70);
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_PLAY_DONE && s_dict_state == DICT_LISTEN) {
        s_dict_pet_frame = 0;
        lv_image_set_src(dc_pet, &EAR_PET_IMG[ear_pet_stage(s_save.xp)][0]);
        lv_obj_set_y(dc_pet, 70);
        dict_show_echo();
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_PITCH && s_dict_state == DICT_SING) {
        if (ev->hz > 0.0f) {
            char nb[5];
            duet_note_label(nb, sizeof(nb), ear_note_from_hz(ev->hz));
            char t[32];
            snprintf(t, sizeof(t), "NOTES %u  %s", (unsigned)ev->pct, nb);
            lbl(dc_status, t, C_TEXT);
        } else {
            lbl(dc_status, "...", C_MUTED);
        }
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_DUET_PHRASE && s_dict_state == DICT_SING) {
        dict_grade();
        return;
    }
}

// ---- RESULT ----
// VS match end: verdict + final score + the XP the shared pet earned.
static void enter_vs_result(void) {
    s_vs = false;
    int8_t w = ear_vs_winner(&s_vsst);
    if (r_scr) { lv_obj_delete(r_scr); r_scr = NULL; }
    s_scr = SCR_RESULT;              // result_key: any OK returns HOME
    r_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(r_scr, lv_color_hex(C_BG), 0);

    r_score = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_score, &lv_font_montserrat_20, 0);
    lv_obj_align(r_score, LV_ALIGN_TOP_MID, 0, 80);
    r_xp = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_xp, &lv_font_montserrat_14, 0);
    lv_obj_align(r_xp, LV_ALIGN_TOP_MID, 0, 120);
    r_tot = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_tot, &lv_font_montserrat_14, 0);
    lv_obj_align(r_tot, LV_ALIGN_TOP_MID, 0, 150);

    lbl(r_score, w < 0 ? "DRAW!" : (w == 0 ? "P1 WINS!" : "P2 WINS!"),
        C_GOLD);
    char t[48];
    snprintf(t, sizeof(t), "P1 %u : %u P2",
             (unsigned)s_vsst.p1_score, (unsigned)s_vsst.p2_score);
    lbl(r_xp, t, C_TEXT);
    snprintf(t, sizeof(t), "+%lu XP", (unsigned long)s_sess_xp);
    lbl(r_tot, t, C_GOLD);
    lv_screen_load(r_scr);
}

// Quiz session end: perfect bonus + auto level adjust, then the shared
// result page. Sing sessions call enter_result_common directly (v1.2).
static void enter_result(void) {
    if (ear_model_perfect(&s_sess)) {
        award_xp(EAR_XP_PERFECT);
        s_sess_xp += EAR_XP_PERFECT;
    }
    uint8_t old_lvl = (s_sess.mode == EAR_MODE_INTERVAL)
                    ? s_save.ilvl : s_save.clvl;
    bool changed = false;
    uint8_t new_lvl = auto_level_adjust(old_lvl, s_sess.score, &changed);
    if (changed) {
        if (s_sess.mode == EAR_MODE_INTERVAL) s_save.ilvl = new_lvl;
        else s_save.clvl = new_lvl;
    }
    ear_store_save(&s_save);   // level and/or lifetime stats (v1.2)
    enter_result_common(s_sess.score, EAR_QUESTIONS_PER_SESSION, s_sess_xp);
}

// Shared RESULT screen: score / XP gained / lifetime totals line (v1.2).
static void enter_result_common(uint8_t score, uint8_t total, uint32_t xp) {
    if (r_scr) { lv_obj_delete(r_scr); r_scr = NULL; }
    s_scr = SCR_RESULT;
    r_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(r_scr, lv_color_hex(C_BG), 0);

    r_score = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_score, &lv_font_montserrat_20, 0);
    lv_obj_align(r_score, LV_ALIGN_TOP_MID, 0, 80);
    r_xp = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_xp, &lv_font_montserrat_14, 0);
    lv_obj_align(r_xp, LV_ALIGN_TOP_MID, 0, 120);
    r_tot = lv_label_create(r_scr);
    lv_obj_set_style_text_font(r_tot, &lv_font_montserrat_14, 0);
    lv_obj_align(r_tot, LV_ALIGN_TOP_MID, 0, 150);
    // The lifetime totals line is longer than the 240 px panel: pin a
    // wrap width (same as the note lists) so it never runs off-screen.
    lv_obj_set_width(r_tot, 220);
    lv_label_set_long_mode(r_tot, LV_LABEL_LONG_WRAP);

    char t[80];
    snprintf(t, sizeof(t), "SCORE %u/%u", (unsigned)score, (unsigned)total);
    lbl(r_score, t, C_TEXT);
    snprintf(t, sizeof(t), "+%lu XP", (unsigned long)xp);
    lbl(r_xp, t, C_GOLD);
    snprintf(t, sizeof(t), "TOT ok %lu sing %lu mel %lu combo %u",
             (unsigned long)s_save.answered, (unsigned long)s_save.sing_pass,
             (unsigned long)s_save.melodies,
             (unsigned)s_save.best_combo);
    lbl(r_tot, t, C_MUTED);
    lv_screen_load(r_scr);
}

// RESULT: any OK press returns to HOME (evolve fanfare fires there).
static void result_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) enter_home();
}

// ---- SETUP ----
static void setup_refresh(void) {
    char t[48];
    snprintf(t, sizeof(t), "VOLUME %u", (unsigned)s_save.vol);
    lbl(s1_vol, t, C_TEXT);
    lbl(s1_reset, "RESET PROGRESS (HOLD OK)", C_MUTED);
    // Battery gauge diagnostics (v1.1): SOC% beside cell voltage. The CW2017
    // profile is provisional; % vs V divergence is the signal to recalibrate.
    int soc = bsp_battery_soc();
    int mv = bsp_battery_mv();
    if (soc >= 0 && mv >= 0) {
        snprintf(t, sizeof(t), "BATT %d%%  %d.%02dV", soc, mv / 1000,
                 (mv % 1000) / 10);
    } else if (mv >= 0) {
        snprintf(t, sizeof(t), "BATT --%%  %d.%02dV", mv / 1000,
                 (mv % 1000) / 10);
    } else if (soc >= 0) {
        snprintf(t, sizeof(t), "BATT %d%%", soc);
    } else {
        snprintf(t, sizeof(t), "BATT --");
    }
    lbl(s1_batt, t, C_MUTED);
}

// Enter SETUP: volume row, reset hint row, keys hint.
static void enter_setup(void) {
    if (s1_scr) { lv_obj_delete(s1_scr); s1_scr = NULL; }
    s_scr = SCR_SETUP;
    s1_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s1_scr, lv_color_hex(C_BG), 0);

    s1_vol = lv_label_create(s1_scr);
    lv_obj_set_style_text_font(s1_vol, &lv_font_montserrat_20, 0);
    lv_obj_align(s1_vol, LV_ALIGN_TOP_MID, 0, 90);

    s1_reset = lv_label_create(s1_scr);
    lv_obj_set_style_text_font(s1_reset, &lv_font_montserrat_14, 0);
    lv_obj_align(s1_reset, LV_ALIGN_TOP_MID, 0, 130);
    s1_batt = lv_label_create(s1_scr);
    lv_obj_set_style_text_font(s1_batt, &lv_font_montserrat_14, 0);
    lv_obj_align(s1_batt, LV_ALIGN_TOP_MID, 0, 170);
    s1_hint = lv_label_create(s1_scr);
    lv_obj_set_style_text_font(s1_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s1_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s1_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s1_hint, "UP/DOWN VOLUME  OK BACK  OKL RESET");
    lv_screen_load(s1_scr);
    setup_refresh();
}

// ---- RESET confirm page ----
// Full-screen second-confirm page (v1.5; PRD v1.2 deferred it): the first
// OK hold on SETUP opens this prompt, a second hold within 5 s wipes
// progress, and any click - or a lapsed 5 s window - returns to SETUP.
// Volume survives the wipe; the 2026-10-11 accidental single-hold wipe is
// why both holds exist.
static uint32_t s_reset_armed_at;   // ms timestamp when the page opened

static void enter_reset_confirm(void) {
    if (rs_scr) { lv_obj_delete(rs_scr); rs_scr = NULL; }
    s_scr = SCR_RESET;
    s_reset_armed_at = (uint32_t)(esp_timer_get_time() / 1000);
    rs_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(rs_scr, lv_color_hex(C_BG), 0);

    rs_title = lv_label_create(rs_scr);
    lv_obj_set_style_text_font(rs_title, &lv_font_montserrat_20, 0);
    lv_obj_align(rs_title, LV_ALIGN_TOP_MID, 0, 90);
    lbl(rs_title, "RESET ALL PROGRESS?", C_BAD);

    rs_warn = lv_label_create(rs_scr);
    lv_obj_set_style_text_font(rs_warn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(rs_warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rs_warn, LV_ALIGN_TOP_MID, 0, 140);
    lbl(rs_warn, "XP, GROWTH AND STATS\nWILL BE ERASED", C_MUTED);

    rs_hint = lv_label_create(rs_scr);
    lv_obj_set_style_text_font(rs_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rs_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(rs_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(rs_hint, "HOLD OK = RESET  ANY KEY = CANCEL");
    lv_screen_load(rs_scr);
}

// RESET CONFIRM keys: a second OK hold within 5 s executes the wipe; any
// other press cancels back to SETUP.
static void reset_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (now - s_reset_armed_at > 5000u) {   // 5 s window lapsed: cancel
            enter_setup();
            return;
        }
        uint8_t vol = s_save.vol;
        memset(&s_save, 0, sizeof(s_save));
        s_save.ilvl = s_save.clvl = s_save.slvl = 1;
        s_save.dlvl = 1;
        s_save.vol = vol;
        ear_store_save(&s_save);
        enter_home();
        return;
    }
    if (ev == BSP_BTN_CLICK) enter_setup();   // any key cancels
}

// SETUP keys: UP/DOWN adjust volume (live +/-10), OK saves+returns, OK-long
// opens the reset confirmation page (the dedicated page PRD v1.2 deferred;
// the double-hold semantics continue on it).
static void setup_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        enter_reset_confirm();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        int v = (int)s_save.vol + (btn == BSP_BTN_UP ? 10 : -10);
        s_save.vol = (uint8_t)clampi(v, 0, 100);
        bsp_audio_set_volume(s_save.vol);
        ear_store_save(&s_save);
        setup_refresh();
    } else if (btn == BSP_BTN_OK) {
        enter_home();
    }
}

// ---- public dispatchers ----
void ear_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    note_activity();
    switch (s_scr) {
    case SCR_HOME:
        if (ev != BSP_BTN_CLICK) return;
        if (btn == BSP_BTN_UP) {
            s_mode_card = (uint8_t)((s_mode_card + 6) % 7);
            home_refresh();
        } else if (btn == BSP_BTN_DOWN) {
            s_mode_card = (uint8_t)((s_mode_card + 1) % 7);
            home_refresh();
        } else if (btn == BSP_BTN_OK) {
            if (s_mode_card == 0) enter_quiz(EAR_MODE_INTERVAL);
            else if (s_mode_card == 1) enter_quiz(EAR_MODE_CHORD);
            else if (s_mode_card == 2) enter_sing();
            else if (s_mode_card == 3) enter_duet();
            else if (s_mode_card == 4) enter_dictation();
            else if (s_mode_card == 5) enter_vs();
            else enter_setup();
        }
        break;
    case SCR_QUIZ: quiz_key(btn, ev); break;
    case SCR_SING: sing_key(btn, ev); break;
    case SCR_DUET: duet_key_ev(btn, ev); break;
    case SCR_DICTATION: dict_key(btn, ev); break;
    case SCR_RESULT: result_key(btn, ev); break;
    case SCR_SETUP: setup_key(btn, ev); break;
    case SCR_RESET: reset_key(btn, ev); break;
    }
}

// Audio events: quiz advances on PLAY_DONE; sing gets live pitch + verdict.
void ear_ui_handle_audio(ear_audio_event_t *ev) {
    note_activity();
    switch (s_scr) {
    case SCR_QUIZ: quiz_audio(ev); break;
    case SCR_SING: sing_audio(ev); break;
    case SCR_DUET: duet_audio_ev(ev); break;
    case SCR_DICTATION: dict_audio_ev(ev); break;
    default: break;   // HOME/RESULT/SETUP/RESET ignore worker events
    }
}

// ---- init ----
void ear_ui_init(QueueHandle_t app_q) {
    s_app_q = app_q;   // reserved for future deferred events
    ear_store_load(&s_save);
    srand((unsigned)esp_random());
    bsp_audio_set_volume(s_save.vol);

    esp_timer_create_args_t args = {
        .callback = idle_timer_cb,
        .name = "ear_idle",
    };
    if (esp_timer_create(&args, &s_idle_timer) == ESP_OK) {
        esp_timer_start_periodic(s_idle_timer, 1000000);   // 1 s tick
    } else {
        s_idle_timer = NULL;   // backlight idle dimming disabled
    }

    enter_home();
}
