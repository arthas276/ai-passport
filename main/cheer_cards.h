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
    CHEER_GRP_COUNT
} cheer_grp_t;

typedef struct {
    const char *text;   // UTF-8 Simplified Chinese, <= CHEER_TEXT_MAX bytes
    uint8_t grp;
} cheer_card_t;

#define CHEER_TEXT_MAX 64

// The library (single source of truth for the font subset generator).
extern const cheer_card_t CHEER_CARDS[];
extern const size_t CHEER_CARD_COUNT;

// Static bag capacity (>= CHEER_CARD_COUNT, keeps the struct fixed-size).
#define CHEER_BAG_MAX 128

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
    int16_t last;      // last drawn card index, -1 initially
} cheer_bag_t;

void cheer_bag_init(cheer_bag_t *bag, uint32_t seed, uint8_t count);
// Draw the next card index (0..count-1). Always succeeds for count>0.
int cheer_bag_draw(cheer_bag_t *bag);

// Stage math: stage 0..CHEER_STAGE_COUNT-1 for a collected count.
int cheer_stage_of(uint32_t count);
// Threshold at which `stage` is entered (0,10,30,80,160,320).
uint32_t cheer_stage_floor(int stage);
