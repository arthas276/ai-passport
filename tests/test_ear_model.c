// Host test for the quiz model: question validity, grading, level adjust.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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

    // Direction gating (PRD 3.1, v1.6): L1 UP only, L2 adds DOWN, L3 adds
    // HARM; every allowed direction occurs across 300 draws (fixed seed).
    for (int lvl = 1; lvl <= 3; lvl++) {
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

    // Level adjust boundaries (PRD 3.1).
    bool ch = false;
    assert(auto_level_adjust(1, 8, &ch) == 2 && ch);
    assert(auto_level_adjust(3, 10, &ch) == 3 && !ch);   // capped at max
    assert(auto_level_adjust(2, 4, &ch) == 1 && ch);
    assert(auto_level_adjust(1, 4, &ch) == 1 && !ch);    // floored at min
    assert(auto_level_adjust(2, 6, &ch) == 2 && !ch);    // 5..7 unchanged
    assert(auto_level_adjust(3, 4, &ch) == 2 && ch);

    // Perfect session bonus flag.
    ear_model_session_init(&s, EAR_MODE_INTERVAL, 1);
    s.score = EAR_QUESTIONS_PER_SESSION;
    assert(ear_model_perfect(&s));
    s.score = 9;
    assert(!ear_model_perfect(&s));

    printf("model: grading + level adjust ok\n");
    return 0;
}
