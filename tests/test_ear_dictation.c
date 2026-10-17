// test_ear_dictation.c - host tests for [PURE] melody generation and
// tolerance/subsequence grading of the dictation mode (PRD 3.4, v1.4).
// Plain asserts, exit code = verdict.
#include "ear_dictation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void phrase(ear_duet_phrase_t *p, const uint8_t *m, uint8_t n) {
    memset(p, 0, sizeof(*p));
    p->count = n;
    for (uint8_t i = 0; i < n; i++) {
        p->notes[i].midi = m[i];
        p->notes[i].ms = 400;
        p->notes[i].cents = 0;
    }
}

// Same, with a signed cents offset per note: the transcriber rounds each
// frame to the nearest semitone, so a flat C arrives as B3 with +40 cents.
static void phrase_cents(ear_duet_phrase_t *p, const uint8_t *m,
                         const int8_t *cents, uint8_t n) {
    memset(p, 0, sizeof(*p));
    p->count = n;
    for (uint8_t i = 0; i < n; i++) {
        p->notes[i].midi = m[i];
        p->notes[i].ms = 400;
        p->notes[i].cents = cents[i];
    }
}

static void melody(ear_dictation_melody_t *m, const uint8_t *t, uint8_t n) {
    memset(m, 0, sizeof(*m));
    m->count = n;
    for (uint8_t i = 0; i < n; i++) m->notes[i] = t[i];
}

static void test_session_init_clamps(void) {
    ear_dictation_session_t s;
    ear_dictation_session_init(&s, 0, 123u);
    assert(s.level == 1);
    assert(s.index == 0);
    assert(s.score == 0);
    assert(s.seed != 0);
    ear_dictation_session_init(&s, 9, 7u);
    assert(s.level == 3);
    ear_dictation_session_init(&s, 2, 0u);
    assert(s.level == 2);
    assert(s.seed != 0);   // zero seed replaced by the default
    printf("dictation: session init clamps ok\n");
}

static void test_level_shapes(void) {
    ear_dictation_session_t s;
    ear_dictation_melody_t m;
    ear_dictation_session_init(&s, 1, 42u);
    ear_dictation_next_melody(&s, &m);
    assert(m.count == 3);
    assert(m.notes[0] == 60);   // L1 always starts on C4
    ear_dictation_session_init(&s, 2, 42u);
    ear_dictation_next_melody(&s, &m);
    assert(m.count == 4);
    ear_dictation_session_init(&s, 3, 42u);
    ear_dictation_next_melody(&s, &m);
    assert(m.count == 5);
    printf("dictation: level shapes ok\n");
}

static void test_no_adjacent_repeats(void) {
    ear_dictation_session_t s;
    ear_dictation_melody_t m;
    for (uint8_t lvl = 1; lvl <= 3; lvl++) {
        ear_dictation_session_init(&s, lvl, 1000u + lvl);
        for (int i = 0; i < 200; i++) {
            ear_dictation_next_melody(&s, &m);
            for (uint8_t j = 1; j < m.count; j++) {
                assert(m.notes[j] != m.notes[j - 1]);
            }
        }
    }
    printf("dictation: no adjacent repeats ok\n");
}

static void test_l1_triad_ending(void) {
    ear_dictation_session_t s;
    ear_dictation_melody_t m;
    ear_dictation_session_init(&s, 1, 7u);
    for (int i = 0; i < 100; i++) {
        ear_dictation_next_melody(&s, &m);
        uint8_t last = m.notes[m.count - 1];
        assert(last == 60 || last == 64 || last == 67);
        for (uint8_t j = 0; j < m.count; j++) {
            assert(m.notes[j] >= 60 && m.notes[j] <= 67);
        }
    }
    printf("dictation: L1 triad ending ok\n");
}

static void test_deterministic_per_seed(void) {
    ear_dictation_session_t a, b;
    ear_dictation_melody_t ma, mb;
    ear_dictation_session_init(&a, 2, 99u);
    ear_dictation_session_init(&b, 2, 99u);
    for (int i = 0; i < 10; i++) {
        ear_dictation_next_melody(&a, &ma);
        ear_dictation_next_melody(&b, &mb);
        assert(ma.count == mb.count);
        assert(memcmp(ma.notes, mb.notes, ma.count) == 0);
    }
    printf("dictation: same seed same melody ok\n");
}

static void test_grade_exact_perfect(void) {
    static const uint8_t t[3] = {60, 64, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    phrase(&p, t, 3);
    ear_dictation_result_t r;
    ear_dictation_grade(&m, &p, 1u, &r);
    assert(r.count == 3);
    assert(r.correct_notes == 3);
    assert(r.extras == 0);
    assert(!r.octave_folded);
    assert(ear_dictation_melody_perfect(&r));
    assert(ear_dictation_melody_correct(&r));
    printf("dictation: exact match perfect ok\n");
}

// Tolerance is what makes the level ramp a difficulty ramp: L1 is wider
// than the transcriber's own vibrato window so beginners are not punished
// for wobble, L3 is near-exact. No window reaches a semitone, so singing a
// different note is always wrong.
static void test_grade_tolerance_by_level(void) {
    static const uint8_t t[3] = {60, 64, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    ear_dictation_result_t r;
    assert(EAR_DICT_TOL_CENTS[0] == 90u);
    assert(EAR_DICT_TOL_CENTS[1] == 70u);
    assert(EAR_DICT_TOL_CENTS[2] == 50u);
    assert(EAR_DICT_TOL_CENTS[0] > EAR_DICT_TOL_CENTS[1]);
    assert(EAR_DICT_TOL_CENTS[1] > EAR_DICT_TOL_CENTS[2]);
    // Every window stays under a semitone, so a different note is always
    // wrong however easy the level.
    assert(EAR_DICT_TOL_CENTS[0] < 100u);
    assert(EAR_DICT_TOL_CENTS[0] > EAR_DUET_WOBBLE_CENTS);
    assert(EAR_DICT_TOL_NAME[0] && EAR_DICT_TOL_NAME[1] &&
           EAR_DICT_TOL_NAME[2]);
    assert(EAR_DICTATION_NOTE_MS[0] > EAR_DICTATION_NOTE_MS[1]);   // slower L1
    assert(EAR_DICTATION_NOTE_MS[1] > EAR_DICTATION_NOTE_MS[2]);

    // A semitone off, dead centre of the wrong semitone: never correct, not
    // even on L1 - the window forgives pitch drift, not note names.
    static const uint8_t semi[3] = {60, 65, 67};
    phrase(&p, semi, 3);
    ear_dictation_grade(&m, &p, 1u, &r);
    assert(r.correct_notes == 2);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[1].off == 1);
    assert(!ear_dictation_melody_correct(&r));
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.correct_notes == 2);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[1].off == 1);
    assert(!ear_dictation_melody_correct(&r));

    // 60 cents flat: E4 sung flat transcribes as D#4 with +40 cents, a
    // true error of -60 cents. L1 and L2 forgive it, L3 does not - that is
    // the difficulty ramp.
    static const uint8_t flat[3] = {60, 63, 67};
    static const int8_t flatc[3] = {0, 40, 0};
    phrase_cents(&p, flat, flatc, 3);
    ear_dictation_grade(&m, &p, 1u, &r);
    assert(r.correct_notes == 3);
    assert(ear_dictation_melody_correct(&r));
    ear_dictation_grade(&m, &p, 2u, &r);
    assert(r.correct_notes == 3);
    assert(ear_dictation_melody_correct(&r));
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.correct_notes == 2);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);

    // Out-of-range level clamps instead of reading past the table.
    phrase_cents(&p, flat, flatc, 3);
    ear_dictation_grade(&m, &p, 0u, &r);
    assert(ear_dictation_melody_correct(&r));          // treated as L1
    ear_dictation_grade(&m, &p, 9u, &r);
    assert(!ear_dictation_melody_correct(&r));         // treated as L3
    printf("dictation: tolerance per level ok\n");
}

// A note sung slightly flat arrives one semitone below with a positive cents
// remainder: the true error is (semitone * 100 + cents), so B3 +40 cents is a
// flat C4 and must pass even on L3.
static void test_grade_cents_remainder(void) {
    static const uint8_t t[1] = {60};
    static const int8_t c[1] = {60};
    static const uint8_t flat[1] = {59};
    ear_dictation_melody_t m;
    melody(&m, t, 1);
    ear_duet_phrase_t p;
    ear_dictation_result_t r;
    phrase_cents(&p, flat, c, 1);
    ear_dictation_grade(&m, &p, 3u, &r);          // -100 + 60 = -40 cents
    assert(r.notes[0].kind == EAR_DICT_MARK_OK);
    assert(ear_dictation_melody_correct(&r));

    // Sharp C4 transcribed as C#4 with a negative remainder: same story.
    static const uint8_t sharp[1] = {61};
    static const int8_t cn[1] = {-60};
    phrase_cents(&p, sharp, cn, 1);
    ear_dictation_grade(&m, &p, 3u, &r);          // +100 - 60 = +40 cents
    assert(r.notes[0].kind == EAR_DICT_MARK_OK);

    // A genuinely flat B3 (whole semitone down) still fails on L3.
    static const uint8_t low[1] = {59};
    static const int8_t cz[1] = {0};
    phrase_cents(&p, low, cz, 1);
    ear_dictation_grade(&m, &p, 3u, &r);          // -100 cents
    assert(r.notes[0].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[0].off == -1);
    printf("dictation: cents remainder ok\n");
}

static void test_grade_octave_fold(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t up[3] = {72, 76, 79};
    static const uint8_t down[3] = {48, 52, 55};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    ear_dictation_result_t r;
    phrase(&p, up, 3);
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.octave_folded);
    assert(r.octaves == 1);
    assert(r.correct_notes == 3);
    assert(ear_dictation_melody_correct(&r));
    assert(ear_dictation_melody_perfect(&r));
    phrase(&p, down, 3);
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.octave_folded);
    assert(r.octaves == -1);
    assert(ear_dictation_melody_correct(&r));
    printf("dictation: octave fold ok\n");
}

// Octave equivalence is per note, so jumping an octave mid-phrase still
// counts as the right pitch class.
static void test_grade_mid_phrase_slip(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t slip[3] = {72, 64, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    phrase(&p, slip, 3);
    ear_dictation_result_t r;
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.octave_folded);
    assert(r.correct_notes == 3);
    assert(ear_dictation_melody_correct(&r));
    printf("dictation: mid-phrase octave slip ok\n");
}

static void test_grade_wrong_note(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t w[3] = {60, 65, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    phrase(&p, w, 3);
    ear_dictation_result_t r;
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.notes[0].kind == EAR_DICT_MARK_OK);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[1].off == 1);
    assert(r.correct_notes == 2);
    assert(!ear_dictation_melody_correct(&r));
    assert(!ear_dictation_melody_perfect(&r));
    printf("dictation: wrong note miss+off ok\n");
}

// A note the melody does not have must not shift the later positions: the
// matcher skips it (up to EAR_DICT_MAX_SKIPS per position) and keeps going.
static void test_grade_extra_note_no_shift(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t inside[4] = {60, 64, 69, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    phrase(&p, inside, 4);
    ear_dictation_result_t r;
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.count == 3);                 // melody positions, not sung notes
    assert(r.correct_notes == 3);         // the hiccup did not derail E4
    assert(r.extras == 1);
    assert(r.notes[2].kind == EAR_DICT_MARK_OK);
    assert(ear_dictation_melody_correct(&r));
    assert(!ear_dictation_melody_perfect(&r));   // extra note sung
    printf("dictation: extra note does not shift ok\n");
}

static void test_grade_extra_and_missing(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t extra[4] = {60, 64, 67, 72};
    static const uint8_t short3[2] = {60, 64};
    static const uint8_t many[5] = {60, 69, 71, 72, 64};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    ear_dictation_result_t r;
    phrase(&p, extra, 4);
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.count == 3);
    assert(r.correct_notes == 3);
    assert(r.extras == 1);
    assert(ear_dictation_melody_correct(&r));   // every target note was sung
    assert(!ear_dictation_melody_perfect(&r));  // ...but an extra note was added
    phrase(&p, short3, 2);
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.count == 3);
    assert(r.notes[2].kind == EAR_DICT_MARK_MISS);
    assert(r.correct_notes == 2);
    assert(r.extras == 0);
    assert(!ear_dictation_melody_correct(&r));
    // Three notes off in a row exceed the skip budget: positions stop
    // matching instead of guessing, and the unmatched tail counts as extras.
    phrase(&p, many, 5);
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.correct_notes == 1);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[2].kind == EAR_DICT_MARK_MISS);
    assert(r.extras == 4);
    // No phrase at all: every position missed, nothing extra.
    ear_dictation_grade(&m, NULL, 3u, &r);
    assert(r.count == 3);
    assert(r.correct_notes == 0);
    assert(r.extras == 0);
    assert(r.notes[0].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[0].sung == 0);
    printf("dictation: extra and missing notes ok\n");
}

// A note sung in the wrong place still counts if the rest is right: a
// dropped middle note leaves the later positions matchable, which is what
// the partial-XP rule rewards.
static void test_grade_partial_phrase(void) {
    static const uint8_t t[3] = {60, 64, 67};
    static const uint8_t skip[2] = {60, 67};
    ear_dictation_melody_t m;
    melody(&m, t, 3);
    ear_duet_phrase_t p;
    phrase(&p, skip, 2);
    ear_dictation_result_t r;
    ear_dictation_grade(&m, &p, 3u, &r);
    assert(r.count == 3);
    assert(r.notes[0].kind == EAR_DICT_MARK_OK);
    assert(r.notes[1].kind == EAR_DICT_MARK_MISS);
    assert(r.notes[2].kind == EAR_DICT_MARK_OK);   // did not shift
    assert(r.correct_notes == 2);
    assert(r.extras == 0);
    assert(!ear_dictation_melody_correct(&r));
    assert(r.correct_notes * 2u >= m.count);       // half right -> partial XP
    printf("dictation: partial phrase ok\n");
}

int main(void) {
    test_session_init_clamps();
    test_level_shapes();
    test_no_adjacent_repeats();
    test_l1_triad_ending();
    test_deterministic_per_seed();
    test_grade_exact_perfect();
    test_grade_tolerance_by_level();
    test_grade_cents_remainder();
    test_grade_octave_fold();
    test_grade_mid_phrase_slip();
    test_grade_wrong_note();
    test_grade_extra_note_no_shift();
    test_grade_extra_and_missing();
    test_grade_partial_phrase();
    printf("dictation: all host tests ok\n");
    return 0;
}
