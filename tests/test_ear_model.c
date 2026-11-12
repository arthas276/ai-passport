// Host test for the quiz model: question validity, grading, level adjust.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ear_game.h"

int main(void)
{
    srand(1);   // deterministic run

    ear_session_t s;
    ear_model_session_init(&s, EAR_MODE_INTERVAL, 1);
    assert(s.index == 0 && s.score == 0 && s.combo == 0 && s.best_combo == 0);

    // 200 generated questions must be internally consistent.
    ear_question_t q;
    uint8_t prev = 0xFF;
    for (int i = 0; i < 200; i++) {
        ear_model_next_question(&s, &q);
        assert(q.root >= 60 && q.root <= 69);
        uint8_t n = EAR_LEVEL_OPT_COUNT[s.mode][s.level - 1];
        int answer_in_options = 0;
        for (uint8_t k = 0; k < n; k++) {
            assert(q.options[k] < EAR_KIND_COUNT);
            for (uint8_t j = 0; j < k; j++) {
                assert(q.options[j] != q.options[k]);   // no dup options
            }
            if (q.options[k] == (uint8_t)q.answer) answer_in_options = 1;
        }
        assert(answer_in_options);
        assert((uint8_t)q.answer != prev);              // no adjacent repeat
        prev = (uint8_t)q.answer;

        if ((i + 1) % 100 == 0) s.level = 2;   // exercise L2 tables too
    }

    // v1.9: every level of both quiz modes must produce valid questions
    // (options unique and in range, answer present, no adjacent repeat),
    // and inversions only appear for CHORD L5.
    for (int mode = 0; mode <= 1; mode++) {
        for (int lvl = 1; lvl <= (int)EAR_QUIZ_LEVEL_MAX; lvl++) {
            ear_model_session_init(&s, (ear_mode_t)mode, (uint8_t)lvl);
            uint8_t prev = 0xFF;
            bool inv_seen = false;
            for (int i = 0; i < 200; i++) {
                ear_model_next_question(&s, &q);
                uint8_t n = EAR_LEVEL_OPT_COUNT[s.mode][s.level - 1];
                assert(n >= 2 && n <= EAR_OPTS_MAX);
                int answer_in_options = 0;
                for (uint8_t k = 0; k < n; k++) {
                    assert(q.options[k] < EAR_KIND_COUNT);
                    for (uint8_t j = 0; j < k; j++) {
                        assert(q.options[j] != q.options[k]);
                    }
                    if (q.options[k] == (uint8_t)q.answer) answer_in_options = 1;
                }
                assert(answer_in_options);
                assert((uint8_t)q.answer != prev);
                prev = (uint8_t)q.answer;
                if (s.mode == EAR_MODE_CHORD && s.level == EAR_QUIZ_LEVEL_MAX) {
                    assert(q.inv < EAR_KINDS[q.answer].len);
                    if (q.inv != 0) inv_seen = true;
                } else {
                    assert(q.inv == 0);
                }
                if (s.mode == EAR_MODE_CHORD) assert(q.dir == EAR_DIR_UP);
            }
            // L5 chord draws must exercise at least one inverted voicing.
            if (s.mode == EAR_MODE_CHORD) {
                assert(s.level < EAR_QUIZ_LEVEL_MAX || inv_seen);
            }
        }
    }

    // Direction gating (PRD 3.1, v1.6): L1 UP only, L2 adds DOWN, L3 adds
    // HARM; L4/L5 (v1.9) keep the full span. Every allowed direction occurs
    // across 300 draws (fixed seed).
    for (int lvl = 1; lvl <= (int)EAR_QUIZ_LEVEL_MAX; lvl++) {
        ear_model_session_init(&s, EAR_MODE_INTERVAL, (uint8_t)lvl);
        bool seen[EAR_DIR_COUNT] = { false };
        for (int i = 0; i < 300; i++) {
            ear_model_next_question(&s, &q);
            assert(q.dir < EAR_DIR_COUNT);
            if (lvl == 1) assert(q.dir == EAR_DIR_UP);
            if (lvl == 2) assert(q.dir == EAR_DIR_UP || q.dir == EAR_DIR_DOWN);
            seen[q.dir] = true;
        }
        assert(seen[EAR_DIR_UP]);
        if (lvl >= 2) assert(seen[EAR_DIR_DOWN]);
        if (lvl >= 3) assert(seen[EAR_DIR_HARM]);
    }
    // Chord questions ignore direction (always UP).
    ear_model_session_init(&s, EAR_MODE_CHORD, 3);
    for (int i = 0; i < 50; i++) {
        ear_model_next_question(&s, &q);
        assert(q.dir == EAR_DIR_UP);
    }

    // Weakness weighting (v1.8): NULL reset keeps drawing valid questions;
    // a heavily-missed kind is drawn far more often than the rest.
    ear_model_set_miss(NULL);
    ear_model_session_init(&s, EAR_MODE_INTERVAL, 2);
    for (int i = 0; i < 100; i++) {
        ear_model_next_question(&s, &q);
        assert((uint8_t)q.answer < EAR_KIND_COUNT);
    }
    uint32_t miss[EAR_KIND_COUNT] = { 0 };
    miss[EAR_KIND_MIN3] = 100;   // capped at +15 but still dominant
    miss[EAR_KIND_P4] = 2;
    ear_model_set_miss(miss);
    ear_model_session_init(&s, EAR_MODE_INTERVAL, 2);
    int n_min3 = 0, n_others = 0;
    for (int i = 0; i < 1000; i++) {
        ear_model_next_question(&s, &q);
        if ((uint8_t)q.answer == EAR_KIND_MIN3) n_min3++; else n_others++;
    }
    // weights: min3 = 16, P4 = 3, others = 1 (4 options at L2). The
    // anti-repeat rule caps min3 at ~every other question, so a decisive
    // rise over the uniform 25% (without hitting pure alternation) passes.
    assert(n_min3 > 350 && n_min3 < 560);
    assert(n_others > 400);
    ear_model_set_miss(NULL);

        // Grading: correct choice advances score/combo; wrong resets combo only.
    ear_model_session_init(&s, EAR_MODE_CHORD, 2);
    ear_model_next_question(&s, &q);
    int ans_idx = -1;
    for (uint8_t k = 0; k < EAR_LEVEL_OPT_COUNT[EAR_MODE_CHORD][1]; k++) {
        if (q.options[k] == (uint8_t)q.answer) ans_idx = k;
    }
    assert(ans_idx >= 0);
    assert(ear_model_grade(&s, &q, (uint8_t)ans_idx));
    assert(s.score == 1 && s.combo == 1 && s.best_combo == 1);
    assert(!ear_model_grade(&s, &q, (uint8_t)(ans_idx ^ 1)));
    assert(s.combo == 0 && s.score == 1);

    // Level adjust boundaries (PRD 3.1). Quiz modes cap at 5, pitch-flow
    // modes at 3 (v1.9 signature takes the ceiling explicitly).
    bool ch = false;
    assert(auto_level_adjust(1, 8, EAR_LEVEL_MAX, &ch) == 2 && ch);
    assert(auto_level_adjust(3, 10, EAR_LEVEL_MAX, &ch) == 3 && !ch);
    assert(auto_level_adjust(2, 4, EAR_LEVEL_MAX, &ch) == 1 && ch);
    assert(auto_level_adjust(1, 4, EAR_LEVEL_MAX, &ch) == 1 && !ch);
    assert(auto_level_adjust(2, 6, EAR_LEVEL_MAX, &ch) == 2 && !ch);
    assert(auto_level_adjust(3, 4, EAR_LEVEL_MAX, &ch) == 2 && ch);
    assert(auto_level_adjust(4, 9, EAR_QUIZ_LEVEL_MAX, &ch) == 5 && ch);
    assert(auto_level_adjust(5, 10, EAR_QUIZ_LEVEL_MAX, &ch) == 5 && !ch);
    assert(auto_level_adjust(5, 3, EAR_QUIZ_LEVEL_MAX, &ch) == 4 && ch);

    // v1.9 kind table: wider intervals carry the right semitone spans and
    // the minor seventh chord carries the right offsets; new labels are
    // distinct from every label offered in the same level option list.
    assert(EAR_KINDS[EAR_KIND_MIN6].semis == 8);
    assert(EAR_KINDS[EAR_KIND_MAJ6].semis == 9);
    assert(EAR_KINDS[EAR_KIND_MIN7I].semis == 10);
    assert(EAR_KINDS[EAR_KIND_MAJ7I].semis == 11);
    assert(EAR_KINDS[EAR_KIND_OCT].semis == 12);
    assert(EAR_KINDS[EAR_KIND_MIN7C].len == 4);
    assert(EAR_KINDS[EAR_KIND_MIN7C].chord[1] == 3 &&
           EAR_KINDS[EAR_KIND_MIN7C].chord[2] == 7 &&
           EAR_KINDS[EAR_KIND_MIN7C].chord[3] == 10);

    // Perfect session bonus flag.
    ear_model_session_init(&s, EAR_MODE_INTERVAL, 1);
    s.score = EAR_QUESTIONS_PER_SESSION;
    assert(ear_model_perfect(&s));
    s.score = 9;
    assert(!ear_model_perfect(&s));

    // ---- REVIEW mode (v2.0) ----
    memset(miss, 0, sizeof(miss));
    uint8_t pool[EAR_KIND_COUNT];
    assert(ear_model_review_pool(miss, pool) == 0);          // empty book
    assert(ear_model_review_total(miss) == 0);
    miss[EAR_KIND_MIN3] = 2;
    miss[EAR_KIND_MAJ] = 1;
    uint8_t pn = ear_model_review_pool(miss, pool);
    assert(pn == 2 && pool[0] == EAR_KIND_MIN3 && pool[1] == EAR_KIND_MAJ);
    assert(ear_model_review_total(miss) == 3);

    // 300 review questions: answer always in the pool, options are 4,
    // unique, all same category as the answer, answer present, no adjacent
    // repeat while the pool allows it (pool has 2 kinds here).
    uint8_t rprev = 0xFF;
    for (int i = 0; i < 300; i++) {
        ear_model_review_question(miss, pool, pn, rprev, &q);
        int in_pool = 0;
        for (uint8_t k = 0; k < pn; k++) if (pool[k] == (uint8_t)q.answer) in_pool = 1;
        assert(in_pool);
        int answer_in_options = 0;
        bool chord_q = (EAR_KINDS[q.answer].semis == 0xFF);
        for (uint8_t k = 0; k < 4; k++) {
            assert(q.options[k] < EAR_KIND_COUNT);
            assert((EAR_KINDS[q.options[k]].semis == 0xFF) == chord_q);
            for (uint8_t j = 0; j < k; j++)
                assert(q.options[j] != q.options[k]);
            if (q.options[k] == (uint8_t)q.answer) answer_in_options = 1;
        }
        assert(answer_in_options);
        if (pn > 1) assert((uint8_t)q.answer != rprev);
        rprev = (uint8_t)q.answer;
    }

    // Graduate: correct answer decrements, floors at 0.
    assert(ear_model_review_graduate(miss, EAR_KIND_MIN3) == 1);
    assert(ear_model_review_graduate(miss, EAR_KIND_MIN3) == 0);
    assert(ear_model_review_graduate(miss, EAR_KIND_MIN3) == 0);
    assert(ear_model_review_total(miss) == 1);

    printf("model: grading + level adjust + L4/L5 + review ok\n");
    return 0;
}
