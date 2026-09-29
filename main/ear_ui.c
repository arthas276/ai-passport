// ear_ui.c - EAR BUDDY screens. All public handlers must be called under
// bsp_lvgl_lock (from the input task in main.c).
#include "ear_ui.h"
#include "ear_assets.h"
#include "ear_store.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>


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
    SCR_HOME, SCR_QUIZ, SCR_SING, SCR_RESULT, SCR_SETUP,
} ear_screen_t;
static ear_screen_t s_scr;

// ---- quiz flow state ----
static ear_session_t s_sess;
static ear_question_t s_q;
static bool s_awaiting_answer;   // options active, waiting for OK
static bool s_advancing;         // next PLAY_DONE advances the question
static uint32_t s_sess_xp;       // XP gained this session (RESULT page)

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
static lv_obj_t *r_scr, *r_score, *r_xp;
static lv_obj_t *s1_scr, *s1_vol, *s1_reset, *s1_hint;

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
static void grade_answer(void);
static void enter_sing(void);
static void enter_setup(void);

// ---- HOME ----
// Mode card names (index = s_mode_card: 0 INTERVAL 1 CHORD 2 SING 3 SETUP).
static const char *MODE_NAMES[4] = {
    "MODE: INTERVAL", "MODE: CHORD", "MODE: SING", "MODE: SETUP",
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
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "Q %u/%u LV%u",
             (unsigned)(s_sess.index + 1),
             (unsigned)EAR_QUESTIONS_PER_SESSION, (unsigned)s_sess.level);
    lbl(q_hdr, hdr, C_MUTED);
    lbl(q_status, "LISTEN...", C_CYAN);
    ear_audio_play_question(s_q.root, s_q.answer);
}

// Enter the quiz screen for a mode (INTERVAL or CHORD).
static void enter_quiz(ear_mode_t mode) {
    uint8_t lvl = (mode == EAR_MODE_INTERVAL) ? s_save.ilvl : s_save.clvl;
    ear_model_session_init(&s_sess, mode, lvl);
    s_sess_xp = 0;
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
    uint8_t n = EAR_LEVEL_OPT_COUNT[s_sess.mode][s_sess.level - 1];
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

// Advance to the next question or the RESULT page.
static void quiz_advance(void) {
    s_sess.index++;
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
            ear_audio_play_question(s_q.root, s_q.answer);   // replay
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
    if (correct) {
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
        ear_audio_play_question(s_q.root, s_q.answer);   // teach: replay
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
// Per-level tolerance in cents (PRD 3.2). Device pass 2026-09-29: beginners
// found +/-50 too tight against detector jitter; L1 sits just under a
// semitone, L3 stays demanding.
static const float SING_TOL[3] = {65.0f, 45.0f, 30.0f};

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
    s_sing_target = 60 + (rand() % 10);
    s_sing_fails = 0;
    ear_audio_play_target(s_sing_target);
    char t[24];
    snprintf(t, sizeof(t), "SING: %s", ear_note_name(s_sing_target));
    lbl(sg_note, t, C_GOLD);
    lbl(sg_status, "LISTEN TO TARGET", C_CYAN);
}

// Pick a new target note (C4-A4), reset fails, play preview.
static void sing_next_note(void) {
    s_sing_target = 60 + (rand() % 10);
    s_sing_fails = 0;
    ear_audio_play_target(s_sing_target);
    char t[24];
    snprintf(t, sizeof(t), "SING: %s", ear_note_name(s_sing_target));
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
    if (ev->kind == EAR_AUDIO_EV_PLAY_DONE && s_showing) {
        s_showing = false;
        sing_next_note();
        return;
    }
    if (ev->kind == EAR_AUDIO_EV_PITCH) {
        if (ev->hz > 0.0f) {
            int x = 94 + (int)(ev->cents * 2.0f);
            lv_obj_set_x(sg_marker, clampi(x, 0, 188));
            char t[40];
            snprintf(t, sizeof(t), "%+.0f cents", (double)ev->cents);
            lbl(sg_status, t, C_TEXT);
        } else {
            lbl(sg_status, "SING!", C_MUTED);
        }
    }

    if (ev->kind == EAR_AUDIO_EV_SING_PASS) {
        award_xp(EAR_XP_SING_PASS);
        s_sess_xp += EAR_XP_SING_PASS;
        ear_audio_play_cue(0);
        lbl(sg_status, "PASS +2 XP", C_OK);
    }

    if (ev->kind == EAR_AUDIO_EV_SING_FAIL) {
        s_sing_fails++;
        if (s_sing_fails >= 3) {
            // Show answer: replay the target, then move to a new note.
            ear_audio_play_target(s_sing_target);
            lbl(sg_status, "ANSWER SHOWN", C_BAD);
            s_showing = true;   // after replay, PLAY_DONE picks next note
        } else {
            lbl(sg_status, "TRY AGAIN", C_BAD);
        }
    }
}

// ---- RESULT ----
static void enter_result(void) {
    // Session end: perfect bonus + auto level adjust, persisted.
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
        ear_store_save(&s_save);
    }
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

    char t[64];
    snprintf(t, sizeof(t), "SCORE %u/10", (unsigned)s_sess.score);
    lbl(r_score, t, C_TEXT);
    snprintf(t, sizeof(t), "+%lu XP", (unsigned long)s_sess_xp);
    lbl(r_xp, t, C_GOLD);
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
    s1_hint = lv_label_create(s1_scr);
    lv_obj_set_style_text_font(s1_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s1_hint, lv_color_hex(C_MUTED), 0);
    lv_obj_align(s1_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s1_hint, "UP/DOWN VOLUME  OK BACK  OKL RESET");
    lv_screen_load(s1_scr);
    setup_refresh();
}

// SETUP keys: UP/DOWN adjust volume (live +/-10), OK saves+returns, OK-long
// resets progress (volume kept; second-confirm page deferred per PRD).
static void setup_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        uint8_t vol = s_save.vol;
        memset(&s_save, 0, sizeof(s_save));
        s_save.ilvl = s_save.clvl = s_save.slvl = 1;
        s_save.vol = vol;
        ear_store_save(&s_save);
        enter_home();
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
            s_mode_card = (uint8_t)((s_mode_card + 3) % 4);
            home_refresh();
        } else if (btn == BSP_BTN_DOWN) {
            s_mode_card = (uint8_t)((s_mode_card + 1) % 4);
            home_refresh();
        } else if (btn == BSP_BTN_OK) {
            if (s_mode_card == 0) enter_quiz(EAR_MODE_INTERVAL);
            else if (s_mode_card == 1) enter_quiz(EAR_MODE_CHORD);
            else if (s_mode_card == 2) enter_sing();
            else enter_setup();
        }
        break;
    case SCR_QUIZ: quiz_key(btn, ev); break;
    case SCR_SING: sing_key(btn, ev); break;
    case SCR_RESULT: result_key(btn, ev); break;
    case SCR_SETUP: setup_key(btn, ev); break;
    }
}

// Audio events: quiz advances on PLAY_DONE; sing gets live pitch + verdict.
void ear_ui_handle_audio(ear_audio_event_t *ev) {
    note_activity();
    switch (s_scr) {
    case SCR_QUIZ: quiz_audio(ev); break;
    case SCR_SING: sing_audio(ev); break;
    default: break;   // HOME/RESULT/SETUP ignore worker events
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
