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
    // v1.9 L4/L5 kinds (appended: indices persist in the NVS miss blob).
    {"m6", 8, 0, {0}},   {"M6", 9, 0, {0}},
    {"m7", 10, 0, {0}},  {"M7i", 11, 0, {0}},
    {"P8", 12, 0, {0}},
    {"m7c", 0xFF, 4, {0, 3, 7, 10}},
};

// Level option tables: kinds presented per mode and level (PRD 3.1).
// INTERVAL L1..L5, CHORD L1..L5 (v1.9). SING row unused (kept zeroed).
const uint8_t EAR_LEVELS[EAR_MODE_COUNT][EAR_QUIZ_LEVEL_MAX][EAR_OPTS_MAX] = {
    [EAR_MODE_INTERVAL] = {
        {EAR_KIND_MIN3, EAR_KIND_MAJ3},
        {EAR_KIND_MIN3, EAR_KIND_MAJ3, EAR_KIND_P4, EAR_KIND_P5},
        {EAR_KIND_MIN2, EAR_KIND_MAJ2, EAR_KIND_MIN3, EAR_KIND_MAJ3,
         EAR_KIND_P4, EAR_KIND_P5},
        {EAR_KIND_P4, EAR_KIND_P5, EAR_KIND_MIN6, EAR_KIND_MAJ6},
        {EAR_KIND_P5, EAR_KIND_MIN6, EAR_KIND_MAJ6, EAR_KIND_MIN7I,
         EAR_KIND_MAJ7I, EAR_KIND_OCT},
    },
    [EAR_MODE_CHORD] = {
        {EAR_KIND_MAJ, EAR_KIND_MIN},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_DIM, EAR_KIND_AUG},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_DIM, EAR_KIND_AUG,
         EAR_KIND_MAJ7, EAR_KIND_DOM7},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_MIN7C, EAR_KIND_DOM7},
        {EAR_KIND_MAJ, EAR_KIND_MIN, EAR_KIND_DIM, EAR_KIND_MIN7C,
         EAR_KIND_MAJ7, EAR_KIND_DOM7},
    },
    [EAR_MODE_SING] = {{0}},
};

// Option count per mode/level (matches EAR_LEVELS rows above).
const uint8_t EAR_LEVEL_OPT_COUNT[EAR_MODE_COUNT][EAR_QUIZ_LEVEL_MAX] = {
    [EAR_MODE_INTERVAL] = {2, 4, 6, 4, 6},
    [EAR_MODE_CHORD]    = {2, 4, 6, 4, 6},
    [EAR_MODE_SING]     = {0, 0, 0, 0, 0},
};

// Fill a fresh session. mode must be INTERVAL or CHORD (SING has no quiz).
void ear_model_session_init(ear_session_t *s, ear_mode_t mode, uint8_t level) {
    s->mode = mode;
    s->level = level;   // caller clamps to 1..EAR_QUIZ_LEVEL_MAX (store load)
    s->index = 0;
    s->score = 0;
    s->combo = 0;
    s->best_combo = 0;
    s->prev_kind = 0xFFu;   // "no previous question yet"
}

// Weakness weighting (v1.8): per-kind miss counts bias the draw. The
// contribution is capped so one badly-missed kind dominates without
// becoming the only question; NULL/zero counts draw uniformly.
static const uint32_t *s_miss;
#define MISS_CAP 15u

// Draw one kind from the level's option list, weighted by miss counts.
static ear_kind_t draw_kind(const ear_session_t *s) {
    uint8_t n = EAR_LEVEL_OPT_COUNT[s->mode][s->level - 1];
    const uint8_t *opts = EAR_LEVELS[s->mode][s->level - 1];
    uint32_t total = 0;
    uint32_t w[EAR_OPTS_MAX];
    for (uint8_t i = 0; i < n; i++) {
        uint32_t m = s_miss ? s_miss[opts[i]] : 0;
        if (m > MISS_CAP) m = MISS_CAP;
        w[i] = 1u + m;
        total += w[i];
    }
    uint32_t r = rand() % total;
    for (uint8_t i = 0; i < n; i++) {
        if (r < w[i]) return (ear_kind_t)opts[i];
        r -= w[i];
    }
    return (ear_kind_t)opts[n - 1];   // unreachable modulo rounding
}

void ear_model_set_miss(const uint32_t *miss_per_kind) {
    s_miss = miss_per_kind;
}

// Shuffle the option list (Fisher-Yates) so the answer position is random.
static void shuffle_options(uint8_t *opt, uint8_t n) {
    for (uint8_t i = n - 1; i > 0; i--) {
        uint8_t j = (uint8_t)(rand() % (i + 1));
        uint8_t tmp = opt[i]; opt[i] = opt[j]; opt[j] = tmp;
    }
}

// Draw the interval playback direction (PRD 3.1, v1.6): L1 ascending
// only, L2 mixes ascending/descending, L3 adds harmonic. Chords ignore
// direction (the caller keeps EAR_DIR_UP).
static ear_dir_t draw_dir(uint8_t level) {
    uint8_t span = (level >= 3) ? EAR_DIR_COUNT
                 : (level >= 2) ? 2 : 1;
    return (ear_dir_t)(rand() % span);
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
    q->dir = (s->mode == EAR_MODE_INTERVAL) ? draw_dir(s->level)
                                            : EAR_DIR_UP;   // chords: n/a
    // Chord inversions (v1.9): only at CHORD L5, rotated voicing; the answer
    // stays the chord quality. Intervals and L1..L4 chords: root position.
    if (s->mode == EAR_MODE_CHORD && s->level >= EAR_QUIZ_LEVEL_MAX) {
        q->inv = (uint8_t)(rand() % EAR_KINDS[drawn].len);
    } else {
        q->inv = 0;
    }
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
uint8_t auto_level_adjust(uint8_t old_level, uint8_t score, uint8_t level_max,
                          bool *changed) {
    uint8_t lvl = old_level;
    if (score >= 8 && old_level < level_max) lvl = old_level + 1;
    else if (score <= 4 && old_level > EAR_LEVEL_MIN) lvl = old_level - 1;
    *changed = (lvl != old_level);
    return lvl;
}

// Perfect session check (10/10) for the +3 XP bonus.
bool ear_model_perfect(const ear_session_t *s) {
    return s->score == EAR_QUESTIONS_PER_SESSION;
}

// ---------------------------------------------------------------------------
// REVIEW mode - mistake trainer (v2.0)
// ---------------------------------------------------------------------------
// Distractor pools for review options: all interval kinds, all chord kinds.
static const uint8_t REVIEW_POOL_INTERVAL[11] = {
    EAR_KIND_MIN2, EAR_KIND_MAJ2, EAR_KIND_MIN3, EAR_KIND_MAJ3,
    EAR_KIND_P4, EAR_KIND_P5, EAR_KIND_MIN6, EAR_KIND_MAJ6,
    EAR_KIND_MIN7I, EAR_KIND_MAJ7I, EAR_KIND_OCT,
};
static const uint8_t REVIEW_POOL_CHORD[7] = {
    EAR_KIND_DIM, EAR_KIND_MIN, EAR_KIND_MAJ, EAR_KIND_AUG,
    EAR_KIND_MAJ7, EAR_KIND_DOM7, EAR_KIND_MIN7C,
};

// Fill out with kinds whose miss count is > 0 (interval + chord mixed).
// Returns the pool size; out must hold EAR_KIND_COUNT entries.
uint8_t ear_model_review_pool(const uint32_t *miss, uint8_t *out) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < EAR_KIND_COUNT; k++) {
        if (miss && miss[k] > 0) out[n++] = k;
    }
    return n;
}

// Total misses left (review header counter).
uint32_t ear_model_review_total(const uint32_t *miss) {
    uint32_t t = 0;
    for (uint8_t k = 0; k < EAR_KIND_COUNT; k++) t += miss ? miss[k] : 0;
    return t;
}

// One review correct answer graduates the kind: miss floor 0. Returns the
// new value (pure so host tests can pin it).
uint32_t ear_model_review_graduate(uint32_t *miss, ear_kind_t k) {
    if (miss[k] > 0) miss[k]--;
    return miss[k];
}

// Generate a review question: the answer is drawn from pool (weighted by
// miss), options are the answer plus 3 same-category distractors, shuffled.
// prev_kind suppresses repeats when the pool allows it. Interval questions
// mix UP/DOWN directions; chords stay in root position.
void ear_model_review_question(const uint32_t *miss, const uint8_t *pool,
                               uint8_t pool_n, uint8_t prev_kind,
                               ear_question_t *q) {
    // Weighted draw from the pool by miss count (cap so one kind cannot
    // take over; misses are small in practice).
    uint32_t total = 0;
    for (uint8_t i = 0; i < pool_n; i++) {
        uint32_t m = miss[pool[i]];
        if (m > MISS_CAP) m = MISS_CAP;
        total += 1u + m;
    }
    uint8_t drawn = pool[0];
    for (int tries = 0; tries < 20; tries++) {
        uint32_t r = rand() % total;
        drawn = pool[pool_n - 1];
        for (uint8_t i = 0; i < pool_n; i++) {
            uint32_t m = miss[pool[i]];
            if (m > MISS_CAP) m = MISS_CAP;
            if (r < 1u + m) { drawn = pool[i]; break; }
            r -= 1u + m;
        }
        if (!(pool_n > 1 && drawn == prev_kind)) break;
    }
    bool is_chord = (EAR_KINDS[drawn].semis == 0xFF);
    const uint8_t *dist = is_chord ? REVIEW_POOL_CHORD : REVIEW_POOL_INTERVAL;
    uint8_t dist_n = is_chord ? 7 : 11;

    q->root = 60 + (rand() % 10);
    q->answer = (ear_kind_t)drawn;
    q->dir = is_chord ? EAR_DIR_UP : (ear_dir_t)(rand() % 2);
    q->inv = 0;
    q->options[0] = drawn;
    uint8_t n = 1;
    // Fill distractors: prefer semitone-adjacent kinds first (they are the
    // classic confusions), then anything else in the category.
    for (uint8_t i = 0; i < dist_n && n < 4; i++) {
        uint8_t cand = dist[i];
        if (cand == drawn) continue;
        bool dup = false;
        for (uint8_t j = 0; j < n; j++) if (q->options[j] == cand) dup = true;
        if (!dup) q->options[n++] = cand;
    }
    shuffle_options(q->options, n);
}
