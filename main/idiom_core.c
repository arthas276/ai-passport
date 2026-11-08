// main/idiom_core.c - see idiom_core.h. Pure C, no ESP-IDF/LVGL.
#include "idiom_core.h"

#include <string.h>

void idiom_stats_record(idiom_stats_t *stats, bool win) {
    if (!stats) return;
    stats->games++;
    if (win) {
        stats->wins++;
        stats->streak++;
        if (stats->streak > stats->best) stats->best = stats->streak;
    } else {
        stats->streak = 0;
    }
}

uint32_t idiom_stats_win_rate(const idiom_stats_t *stats) {
    if (!stats || stats->games == 0u) return 0u;
    return stats->wins * 100u / stats->games;
}

uint32_t idiom_prng_next(uint32_t *state) {
    if (!state) return 0u;
    uint32_t x = *state ? *state : 0xA5A5A5A5u;  // xorshift 的全零不动点必须避开
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void idiom_order_build(uint32_t seed, uint16_t *order, uint16_t count) {
    if (!order) return;
    for (uint16_t i = 0; i < count; i++) order[i] = i;
    uint32_t state = seed;
    for (uint16_t i = count; i > 1u; i--) {
        uint32_t r = idiom_prng_next(&state);
        uint16_t j = (uint16_t)(r % i);
        uint16_t tmp = order[i - 1u];
        order[i - 1u] = order[j];
        order[j] = tmp;
    }
}

void idiom_score(const uint32_t target[IDIOM_LEN], const uint32_t guess[IDIOM_LEN],
                 idiom_mark_t marks[IDIOM_LEN]) {
    bool consumed[IDIOM_LEN] = {false, false, false, false};
    for (uint32_t i = 0; i < IDIOM_LEN; i++) {
        marks[i] = (guess[i] == target[i]) ? IDIOM_MARK_HIT : IDIOM_MARK_MISS;
        if (marks[i] == IDIOM_MARK_HIT) consumed[i] = true;
    }
    for (uint32_t i = 0; i < IDIOM_LEN; i++) {
        if (marks[i] == IDIOM_MARK_HIT) continue;
        for (uint32_t j = 0; j < IDIOM_LEN; j++) {
            if (consumed[j] || guess[i] != target[j]) continue;
            consumed[j] = true;
            marks[i] = IDIOM_MARK_NEAR;
            break;
        }
    }
}

bool idiom_marks_all_hit(const idiom_mark_t marks[IDIOM_LEN]) {
    for (uint32_t i = 0; i < IDIOM_LEN; i++) {
        if (marks[i] != IDIOM_MARK_HIT) return false;
    }
    return true;
}

bool idiom_row_consistent(const uint32_t guess[IDIOM_LEN],
                          const idiom_mark_t marks[IDIOM_LEN],
                          const uint32_t candidate[IDIOM_LEN]) {
    idiom_mark_t observed[IDIOM_LEN];
    idiom_score(candidate, guess, observed);
    return memcmp(observed, marks, sizeof(observed)) == 0;
}

uint16_t idiom_filter(const uint32_t (*words)[IDIOM_LEN], uint16_t count,
                      const uint32_t (*guesses)[IDIOM_LEN],
                      const idiom_mark_t (*marks)[IDIOM_LEN], uint16_t rows,
                      uint16_t *out) {
    if (!words || !out) return 0u;
    uint16_t kept = 0u;
    for (uint16_t w = 0; w < count; w++) {
        bool ok = true;
        for (uint16_t r = 0; r < rows && ok; r++) {
            if (!idiom_row_consistent(guesses[r], marks[r], words[w])) ok = false;
        }
        if (ok) out[kept++] = w;
    }
    return kept;
}
