// ear_model.c - quiz question generation, grading, level adjust. PURE C.
// Host-testable: no IDF/LVGL includes (AGENTS rule: testable logic decoupled).
#include "ear_game.h"
#include <stdlib.h>

// Kind table. Interval kinds carry semis; chord kinds carry offsets.
const ear_kind_info_t EAR_KINDS[EAR_KIND_COUNT] = {
    {"m2", 1, 0, {0}},  {"M2", 2, 0, {0}},
    {"m3", 3, 0, {0}},  {"M3", 4, 0, {0}},
    {"P4", 5, 0, {0}},  {"P5", 7, 0, {0}},
    {"dim", 0xFF, 3, {0, 3, 6, 0}},   {"min", 0xFF, 3, {0, 3, 7, 0}},
    {"maj", 0xFF, 3, {0, 4, 7, 0}},   {"aug", 0xFF, 3, {0, 4, 8, 0}},
    {"M7", 0xFF, 4, {0, 4, 7, 11}},   {"7", 0xFF, 4, {0, 4, 7, 10}},
};

// Level option tables: kinds presented per mode and level (PRD 3.1).
// INTERVAL L1/L2/L3, CHORD L1/L2/L3. SING row unused (kept zeroed).
const uint8_t EAR_LEVELS[EAR_MODE_COUNT][3][EAR_OPTS_MAX] = {
    [EAR_MODE_INTERVAL] = {
        {EAR_KIND_MIN3, EAR_KIND_MAJ3},
        {EAR_KIND_MIN3, EAR_KIND_MAJ3, EAR_KIND_P4, EAR_KIND_P5},
        {EAR_KIND_MIN2, EAR_KIND_MAJ2, EAR_KIND_MIN3, EAR_KIND_MAJ3,
         EAR_KIND_P4, EAR_KIND_P5},
    },
    [EAR_MODE_CHORD] = {
        {EAR_KIND_MAJ, EAR_KIND_MIN},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_DIM, EAR_KIND_AUG},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_DIM, EAR_KIND_AUG,
         EAR_KIND_MAJ7, EAR_KIND_DOM7},
    },
    [EAR_MODE_SING] = {{0}},
};

// Option count per mode/level (matches EAR_LEVELS rows above).
const uint8_t EAR_LEVEL_OPT_COUNT[EAR_MODE_COUNT][3] = {
    [EAR_MODE_INTERVAL] = {2, 4, 6},
    [EAR_MODE_CHORD]    = {2, 4, 6},
    [EAR_MODE_SING]     = {0, 0, 0},
};

// Fill a fresh session. mode must be INTERVAL or CHORD (SING has no quiz).
void ear_model_session_init(ear_session_t *s, ear_mode_t mode, uint8_t level) {
    s->mode = mode;
    s->level = level;   // caller clamps to 1..3 (see auto_level_adjust)
    s->index = 0;
    s->score = 0;
    s->combo = 0;
    s->best_combo = 0;
    s->prev_kind = 0xFFu;   // "no previous question yet"
}

// Draw one kind uniformly from the level's option list.
static ear_kind_t draw_kind(const ear_session_t *s) {
    uint8_t n = EAR_LEVEL_OPT_COUNT[s->mode][s->level - 1];
    return (ear_kind_t)EAR_LEVELS[s->mode][s->level - 1][rand() % n];
}

// Shuffle the option list (Fisher-Yates) so the answer position is random.
static void shuffle_options(uint8_t *opt, uint8_t n) {
    for (uint8_t i = n - 1; i > 0; i--) {
        uint8_t j = (uint8_t)(rand() % (i + 1));
        uint8_t tmp = opt[i]; opt[i] = opt[j]; opt[j] = tmp;
    }
}

// Generate the next question into *q.
// Root: uniform 60..69. Answer: uniform from level options, retrying up to
// 20 draws to avoid repeating the previous answer kind (always terminates;
// with 2-option levels it simply alternates, which is fine).
void ear_model_next_question(ear_session_t *s, ear_question_t *q) {
    const uint8_t n = EAR_LEVEL_OPT_COUNT[s->mode][s->level - 1];
    uint8_t drawn = 0xFFu;
    for (int tries = 0; tries < 20; tries++) {
        drawn = draw_kind(s);
        if (drawn != s->prev_kind) break;
    }
    s->prev_kind = drawn;
    q->root = 60 + (rand() % 10);   // MIDI C4..A4
    q->answer = (ear_kind_t)drawn;
    for (uint8_t i = 0; i < n; i++) {
        q->options[i] = EAR_LEVELS[s->mode][s->level - 1][i];
    }
    shuffle_options(q->options, n);
}

// Grade the user's choice (index into q->options). True when correct.
// Updates score/combo/best_combo; called once per answered question.
bool ear_model_grade(ear_session_t *s, const ear_question_t *q, uint8_t choice) {
    if (choice >= EAR_OPTS_MAX) return false;
    bool correct = (q->options[choice] == (uint8_t)q->answer);
    if (correct) {
        s->score++;
        s->combo++;
        if (s->combo > s->best_combo) s->best_combo = s->combo;
    } else {
        s->combo = 0;
    }
    return correct;
}

// Level adjust at session end (PRD 3.1): >=8 -> up (max 3), <=4 -> down
// (min 1), 5..7 -> unchanged. Returns new level; *changed flags a move.
uint8_t auto_level_adjust(uint8_t old_level, uint8_t score, bool *changed) {
    uint8_t lvl = old_level;
    if (score >= 8 && old_level < EAR_LEVEL_MAX) lvl = old_level + 1;
    else if (score <= 4 && old_level > EAR_LEVEL_MIN) lvl = old_level - 1;
    *changed = (lvl != old_level);
    return lvl;
}

// Perfect session check (10/10) for the +3 XP bonus.
bool ear_model_perfect(const ear_session_t *s) {
    return s->score == EAR_QUESTIONS_PER_SESSION;
}
