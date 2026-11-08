// main/idiom_core.h - pure game logic: feedback scoring, candidate filtering,
// seeded answer order, streak statistics. No ESP-IDF, no LVGL: host-testable.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IDIOM_ROUNDS_MAX 6u
#define IDIOM_LEN 4u

typedef enum {
    IDIOM_MARK_MISS = 0,   // character not in the target idiom
    IDIOM_MARK_NEAR,       // in the idiom, wrong position
    IDIOM_MARK_HIT,        // right character, right position
} idiom_mark_t;

typedef struct {
    uint32_t games;
    uint32_t wins;
    uint32_t streak;
    uint32_t best;
} idiom_stats_t;

void idiom_stats_record(idiom_stats_t *stats, bool win);
/* Win rate in percent; 0 before the first finished game. */
uint32_t idiom_stats_win_rate(const idiom_stats_t *stats);

/* Deterministic xorshift32 stream. */
uint32_t idiom_prng_next(uint32_t *state);

/* Full Fisher-Yates shuffle of 0..count-1 under seed, written to order.
 * Deterministic: the same seed always yields the same permutation. */
void idiom_order_build(uint32_t seed, uint16_t *order, uint16_t count);

/* Wordle two-pass feedback with duplicate-character semantics:
 * pass 1 marks exact matches (HIT) and consumes those target characters,
 * pass 2 marks remaining guess characters that match unconsumed target
 * characters (NEAR), everything else MISS. */
void idiom_score(const uint32_t target[IDIOM_LEN], const uint32_t guess[IDIOM_LEN],
                 idiom_mark_t marks[IDIOM_LEN]);
bool idiom_marks_all_hit(const idiom_mark_t marks[IDIOM_LEN]);

/* Would scoring `candidate` as the target against `guess` reproduce exactly
 * `marks`? This is the solver constraint that keeps the flip list honest. */
bool idiom_row_consistent(const uint32_t guess[IDIOM_LEN],
                          const idiom_mark_t marks[IDIOM_LEN],
                          const uint32_t candidate[IDIOM_LEN]);

/* Keep only words consistent with every committed row; writes the surviving
 * word indices to out (which may alias in) and returns their count. */
uint16_t idiom_filter(const uint32_t (*words)[IDIOM_LEN], uint16_t count,
                      const uint32_t (*guesses)[IDIOM_LEN],
                      const idiom_mark_t (*marks)[IDIOM_LEN], uint16_t rows,
                      uint16_t *out);
