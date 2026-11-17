// cheer_cards.h - praise-card library + shuffled-bag engine. PURE C.
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "cheer.h"

typedef enum {
    CHEER_GRP_DAILY = 0,
    CHEER_GRP_WORK,
    CHEER_GRP_STUDY,
    CHEER_GRP_BRAVE,
    CHEER_GRP_NIGHT,   // v1.3 夜猫子
    CHEER_GRP_OUT,     // v1.3 出门在外
    CHEER_GRP_COUNT
} cheer_grp_t;

typedef struct {
    const char *text;   // UTF-8 Simplified Chinese, <= CHEER_TEXT_MAX bytes
    uint8_t grp;
} cheer_card_t;

#define CHEER_TEXT_MAX 64
#define CHEER_TEXT_COLS 6   // card text area fits 6 CJK glyphs per line

// The library (single source of truth for the font subset generator).
extern const cheer_card_t CHEER_CARDS[];
extern const size_t CHEER_CARD_COUNT;

// Static bag capacity (>= CHEER_CARD_COUNT, keeps the struct fixed-size).
// v1.3: 164 cards (4x31 + 2x20).
#define CHEER_BAG_MAX 192

// Deterministic xorshift32 PRNG (host-testable; state advances in place).
uint32_t cheer_rng_next(uint32_t *state);

// Shuffled bag: draws without replacement until empty, then reshuffles.
// Across a reshuffle boundary the first card of the new bag never equals
// the last card of the old bag (no double in a row).
typedef struct {
    uint32_t rng;
    uint8_t order[CHEER_BAG_MAX];
    uint8_t pos;
    uint8_t count;
    uint8_t base;      // v1.3: window base for scene-picked bags (0 = whole library)
    int16_t last;      // last drawn ABSOLUTE card index, -1 initially
} cheer_bag_t;

void cheer_bag_init(cheer_bag_t *bag, uint32_t seed, uint8_t count);
// v1.3 scene pick: bag over the contiguous window [base, base+count) of
// CHEER_CARDS; draws return absolute indices. Same rules as the full bag.
void cheer_bag_init_window(cheer_bag_t *bag, uint32_t seed, uint8_t base,
                           uint8_t count);
// Draw the next card index (absolute). Always succeeds for count>0.
int cheer_bag_draw(cheer_bag_t *bag);

// v1.3: contiguous extent of group `grp` in CHEER_CARDS (start index and
// card count; library is laid out grouped - test-enforced).
void cheer_grp_extent(int grp, int *start, int *count);

// Stage math: stage 0..CHEER_STAGE_COUNT-1 for a collected count.
int cheer_stage_of(uint32_t count);
// Threshold at which `stage` is entered (0,10,30,80,160,320).
uint32_t cheer_stage_floor(int stage);
