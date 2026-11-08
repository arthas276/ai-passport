// tests/test_idiom_core.c - host tests for the pure game logic and the idle
// power policy. Compiled by tools/validate.sh without ESP-IDF/LVGL.
#include "idiom_core.h"
#include "idiom_power.h"
#include "idiom_words.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

#define CP(c) ((uint32_t)(c))

static void test_score_basic(void) {
    // distinct alphabets: everything MISS
    const uint32_t target[4] = {CP('A'), CP('B'), CP('C'), CP('D')};
    const uint32_t guess[4] = {CP('E'), CP('F'), CP('G'), CP('H')};
    idiom_mark_t marks[4];
    idiom_score(target, guess, marks);
    expect(marks[0] == IDIOM_MARK_MISS && marks[1] == IDIOM_MARK_MISS &&
               marks[2] == IDIOM_MARK_MISS && marks[3] == IDIOM_MARK_MISS,
           "score: disjoint guess is all MISS");
    expect(!idiom_marks_all_hit(marks), "score: disjoint not all-hit");

    const uint32_t same[4] = {CP('A'), CP('B'), CP('C'), CP('D')};
    idiom_score(target, same, marks);
    expect(idiom_marks_all_hit(marks), "score: identical guess all-hit");

    // one swap: two NEAR
    const uint32_t swapped[4] = {CP('B'), CP('A'), CP('C'), CP('D')};
    idiom_score(target, swapped, marks);
    expect(marks[0] == IDIOM_MARK_NEAR && marks[1] == IDIOM_MARK_NEAR &&
               marks[2] == IDIOM_MARK_HIT && marks[3] == IDIOM_MARK_HIT,
           "score: swapped pair gives NEAR NEAR HIT HIT");
}

static void test_score_duplicates(void) {
    // target A B A B (duplicate characters on both sides)
    const uint32_t target[4] = {CP('A'), CP('B'), CP('A'), CP('B')};
    idiom_mark_t marks[4];

    // guess A X X X: only the first A matches; no phantom NEAR for a second A
    const uint32_t g1[4] = {CP('A'), CP('X'), CP('X'), CP('X')};
    idiom_score(target, g1, marks);
    expect(marks[0] == IDIOM_MARK_HIT && marks[1] == IDIOM_MARK_MISS &&
               marks[2] == IDIOM_MARK_MISS && marks[3] == IDIOM_MARK_MISS,
           "score dup: single A hit, no phantom near");

    // guess X A X X: A exists but not at position 1... target[1]=B so NEAR
    const uint32_t g2[4] = {CP('X'), CP('A'), CP('X'), CP('X')};
    idiom_score(target, g2, marks);
    expect(marks[1] == IDIOM_MARK_NEAR, "score dup: A elsewhere is NEAR");

    // guess A A X X: first A HIT consumes one target A, second A NEAR
    const uint32_t g3[4] = {CP('A'), CP('A'), CP('X'), CP('X')};
    idiom_score(target, g3, marks);
    expect(marks[0] == IDIOM_MARK_HIT && marks[1] == IDIOM_MARK_NEAR,
           "score dup: double A gives HIT then NEAR");

    // guess A X A X vs target A B A B: all HIT
    const uint32_t g4[4] = {CP('A'), CP('X'), CP('A'), CP('X')};
    idiom_score(target, g4, marks);
    expect(marks[0] == IDIOM_MARK_HIT && marks[2] == IDIOM_MARK_HIT &&
               marks[1] == IDIOM_MARK_MISS && marks[3] == IDIOM_MARK_MISS,
           "score dup: aligned repeats all HIT");
}

static void test_consistency_and_filter(void) {
    // tiny 4-word dictionary
    static const uint32_t words[4][4] = {
        {CP('A'), CP('B'), CP('C'), CP('D')},   // 0 ABCD (target)
        {CP('A'), CP('B'), CP('X'), CP('Y')},   // 1 ABXY
        {CP('E'), CP('F'), CP('G'), CP('H')},   // 2 EFGH
        {CP('A'), CP('E'), CP('B'), CP('D')},   // 3 AEBD
    };
    // guess EFGH (word 2) against target ABCD: all MISS
    idiom_mark_t marks[4];
    idiom_score(words[0], words[2], marks);
    expect(marks[0] == IDIOM_MARK_MISS && marks[1] == IDIOM_MARK_MISS &&
               marks[2] == IDIOM_MARK_MISS && marks[3] == IDIOM_MARK_MISS,
           "filter setup: EFGH vs ABCD all miss");

    // ABXY contains no EFGH character, so as target it would also produce an
    // all-MISS row: consistent. EFGH itself would produce all HIT: not.
    expect(idiom_row_consistent(words[2], marks, words[1]),
           "consistency: ABXY compatible with all-MISS row");
    expect(!idiom_row_consistent(words[2], marks, words[2]),
           "consistency: the guess itself contradicts an all-MISS row");
    expect(idiom_row_consistent(words[2], marks, words[0]),
           "consistency: target stays compatible");

    uint16_t out[4];
    const uint32_t guesses[1][4] = {{CP('E'), CP('F'), CP('G'), CP('H')}};
    const idiom_mark_t row_marks[1][4] = {{IDIOM_MARK_MISS, IDIOM_MARK_MISS,
                                           IDIOM_MARK_MISS, IDIOM_MARK_MISS}};
    uint16_t kept = idiom_filter(words, 4, guesses, row_marks, 1, out);
    expect(kept == 2, "filter: all-MISS row drops EFGH and the E-bearing AEBD");
    expect(out[0] == 0 && out[1] == 1,
           "filter: surviving indices ordered");

    // second row: guess ABXY (word 1) vs target ABCD → HIT HIT MISS MISS
    idiom_score(words[0], words[1], marks);
    expect(marks[0] == IDIOM_MARK_HIT && marks[1] == IDIOM_MARK_HIT &&
               marks[2] == IDIOM_MARK_MISS && marks[3] == IDIOM_MARK_MISS,
           "filter setup: ABXY vs ABCD prefix hit");
    // AEBD vs guess ABXY: score(AEBD, ABXY): pos0 A==A HIT; pos1 E vs B no,
    // B in target? AEBD has B at pos2 → NEAR; X,Y not in AEBD → MISS MISS.
    // Observed HIT HIT MISS MISS ≠ → AEBD drops; only ABCD survives.
    expect(!idiom_row_consistent(words[1], marks, words[3]),
           "consistency: AEBD contradicts the prefix-hit row");
    kept = idiom_filter(words, 4, guesses, row_marks, 1, out);
    const uint32_t guesses2[2][4] = {{CP('E'), CP('F'), CP('G'), CP('H')},
                                      {CP('A'), CP('B'), CP('X'), CP('Y')}};
    const idiom_mark_t marks2[2][4] = {
        {IDIOM_MARK_MISS, IDIOM_MARK_MISS, IDIOM_MARK_MISS, IDIOM_MARK_MISS},
        {IDIOM_MARK_HIT, IDIOM_MARK_HIT, IDIOM_MARK_MISS, IDIOM_MARK_MISS},
    };
    kept = idiom_filter(words, 4, guesses2, marks2, 2, out);
    expect(kept == 1 && out[0] == 0,
           "filter: two rows converge on the target alone");
    (void)kept;
}

static void test_real_dictionary_filter(void) {
    // App-shaped two-row sequences on the real wordlist: the true target must
    // survive every filter and the count must never drop to zero (regression
    // for the v1.0 on-device cand=0 freeze).
    uint32_t guesses[2][4];
    idiom_mark_t rows[2][4];
    static uint16_t out[IDIOM_COUNT];
    uint32_t seed = 0x51D10u;
    for (unsigned t = 0; t < IDIOM_COUNT; t++) {
        for (int rep = 0; rep < 3; rep++) {
            int g1 = (int)(idiom_prng_next(&seed) % IDIOM_COUNT);
            int g2 = (int)(idiom_prng_next(&seed) % IDIOM_COUNT);
            idiom_score(IDIOM_WORDS_CP[t], IDIOM_WORDS_CP[g1], rows[0]);
            idiom_score(IDIOM_WORDS_CP[t], IDIOM_WORDS_CP[g2], rows[1]);
            memcpy(guesses[0], IDIOM_WORDS_CP[g1], sizeof(guesses[0]));
            memcpy(guesses[1], IDIOM_WORDS_CP[g2], sizeof(guesses[1]));
            uint16_t kept =
                idiom_filter(IDIOM_WORDS_CP, IDIOM_COUNT, guesses, rows, 2, out);
            bool has = false;
            for (uint16_t k = 0; k < kept; k++) {
                if (out[k] == (uint16_t)t) has = true;
            }
            expect(kept > 0, "real-dict filter never empties");
            expect(has, "real-dict filter keeps the target");
        }
    }
}

static void test_order(void) {
    uint16_t a[10], b[10];
    idiom_order_build(1234u, a, 10);
    idiom_order_build(1234u, b, 10);
    expect(memcmp(a, b, sizeof(a)) == 0, "order: same seed same permutation");
    bool seen[10] = {false};
    bool bijection = true;
    for (int i = 0; i < 10; i++) {
        if (a[i] >= 10 || seen[a[i]]) bijection = false;
        else seen[a[i]] = true;
    }
    expect(bijection, "order: permutation is a bijection");
    uint16_t c[10];
    idiom_order_build(1235u, c, 10);
    expect(memcmp(a, c, sizeof(a)) != 0, "order: different seed differs");
    // xorshift zero-state guard
    uint32_t state = 0u;
    expect(idiom_prng_next(&state) != 0u, "prng: zero seed escapes fixed point");
}

static void test_stats(void) {
    idiom_stats_t s = {0};
    idiom_stats_record(&s, true);
    idiom_stats_record(&s, true);
    expect(s.games == 2 && s.wins == 2 && s.streak == 2 && s.best == 2,
           "stats: two wins");
    idiom_stats_record(&s, false);
    expect(s.games == 3 && s.streak == 0 && s.best == 2,
           "stats: loss resets streak, keeps best");
    idiom_stats_record(&s, true);
    idiom_stats_record(&s, true);
    idiom_stats_record(&s, true);
    expect(s.best == 3, "stats: new best tracked");
    expect(idiom_stats_win_rate(&s) == 83u, "stats: win rate percent (5/6)");
    expect(idiom_stats_win_rate(&(idiom_stats_t){0}) == 0u,
           "stats: zero games zero rate");
}

static void test_power(void) {
    idiom_power_t p;
    idiom_power_reset(&p);
    expect(idiom_power_backlight(&p) == IDIOM_BACKLIGHT_ACTIVE,
           "power: active backlight fresh");
    for (uint32_t i = 0; i < (IDIOM_DIM_AFTER_MS / 250u) - 1u; i++) {
        idiom_power_advance(&p, 250u);
    }
    expect(idiom_power_backlight(&p) == IDIOM_BACKLIGHT_ACTIVE,
           "power: still bright before dim threshold");
    idiom_power_advance(&p, 250u);
    expect(idiom_power_backlight(&p) == IDIOM_BACKLIGHT_DIM,
           "power: dims at threshold");
    idiom_power_activity(&p);
    expect(idiom_power_backlight(&p) == IDIOM_BACKLIGHT_ACTIVE,
           "power: activity restores");
    for (uint32_t i = 0; i < IDIOM_SLEEP_AFTER_MS / 250u; i++) {
        idiom_power_advance(&p, 250u);
    }
    expect(idiom_power_should_sleep(&p), "power: sleeps at threshold");
    idiom_power_advance(&p, 1000u);
    expect(idiom_power_backlight(&p) == 0u, "power: sleeping backlight 0");
}

int main(void) {
    test_score_basic();
    test_score_duplicates();
    test_consistency_and_filter();
    test_real_dictionary_filter();
    test_order();
    test_stats();
    test_power();
    if (failures == 0) printf("test_idiom_core: all passed\n");
    return failures == 0 ? 0 : 1;
}
