// test_cheer_cards.c - host tests for the CHEER praise library + bag.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cheer_cards.h"

static int seen_all_once(void) {
    cheer_bag_t bag;
    uint8_t seen[CHEER_BAG_MAX] = { 0 };
    cheer_bag_init(&bag, 12345u, (uint8_t)CHEER_CARD_COUNT);
    for (size_t i = 0; i < CHEER_CARD_COUNT; i++) {
        int c = cheer_bag_draw(&bag);
        if (c < 0 || c >= (int)CHEER_CARD_COUNT) return 0;
        if (seen[c]) return 0;          // duplicate within one bag
        seen[c] = 1;
    }
    for (size_t i = 0; i < CHEER_CARD_COUNT; i++) {
        if (!seen[i]) return 0;         // missing card
    }
    return 1;
}

static int no_repeat_across_reshuffle(void) {
    for (uint32_t seed = 1; seed <= 64; seed++) {
        cheer_bag_t bag;
        cheer_bag_init(&bag, seed, (uint8_t)CHEER_CARD_COUNT);
        int last = -1;
        for (size_t i = 0; i < CHEER_CARD_COUNT * 3; i++) {   // 3 bags
            int c = cheer_bag_draw(&bag);
            if (c == last) return 0;    // immediate repeat across boundary
            last = c;
        }
    }
    return 1;
}

static int stages_ok(void) {
    struct { uint32_t n; int want; } cases[] = {
        { 0, 0 }, { 9, 0 }, { 10, 1 }, { 29, 1 }, { 30, 2 }, { 79, 2 },
        { 80, 3 }, { 159, 3 }, { 160, 4 }, { 319, 4 }, { 320, 5 },
        { 100000, 5 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cheer_stage_of(cases[i].n) != cases[i].want) return 0;
    }
    if (cheer_stage_floor(1) != 10 || cheer_stage_floor(5) != 320 ||
        cheer_stage_floor(0) != 0) return 0;
    return 1;
}

static int library_ok(void) {
    if (CHEER_CARD_COUNT < 60 || CHEER_CARD_COUNT > CHEER_BAG_MAX) return 0;
    uint8_t per_grp[CHEER_GRP_COUNT] = { 0 };
    for (size_t i = 0; i < CHEER_CARD_COUNT; i++) {
        const cheer_card_t *c = &CHEER_CARDS[i];
        if (!c->text || !c->text[0]) return 0;
        if (strlen(c->text) >= CHEER_TEXT_MAX) return 0;
        if (c->grp >= CHEER_GRP_COUNT) return 0;
        if (strchr(c->text, '\n')) {
            size_t line = 0;
            for (const char *p = c->text; ; p++) {
                if (*p == '\n' || *p == '\0') {
                    if (line > CHEER_TEXT_COLS * 3) return 0;   // CJK glyph = 3 bytes
                    if (*p == '\0') break;
                    line = 0;
                } else {
                    line++;
                }
            }
        }
        per_grp[c->grp]++;
    }
    for (int g = 0; g < CHEER_GRP_COUNT; g++) {
        if (per_grp[g] < 15) return 0;  // every group well stocked
    }
    return 1;
}

static int groups_and_extents_ok(void) {
    if (CHEER_GRP_COUNT != 6) return 0;
    uint8_t per_grp[CHEER_GRP_COUNT] = { 0 };
    for (size_t i = 0; i < CHEER_CARD_COUNT; i++) per_grp[CHEER_CARDS[i].grp]++;
    int prev_end = -1, total = 0;
    for (int g = 0; g < CHEER_GRP_COUNT; g++) {
        int start = -1, cnt = -1;
        cheer_grp_extent(g, &start, &cnt);
        if (cnt != per_grp[g] || cnt < 15) return 0;
        if (start != prev_end + 1) return 0;   // contiguous & ordered
        prev_end = start + cnt - 1;
        total += cnt;
    }
    return total == (int)CHEER_CARD_COUNT;
}

static int scene_bag_ok(void) {
    for (int g = 0; g < CHEER_GRP_COUNT; g++) {
        int start = 0, cnt = 0;
        cheer_grp_extent(g, &start, &cnt);
        cheer_bag_t bag;
        cheer_bag_init_window(&bag, 77u + (uint32_t)g, (uint8_t)start,
                              (uint8_t)cnt);
        uint8_t seen[CHEER_BAG_MAX] = { 0 };
        int last = -1;
        for (int i = 0; i < cnt * 2; i++) {   // a full bag + a reshuffle
            int c = cheer_bag_draw(&bag);
            if (c < start || c >= start + cnt) return 0;   // stays in scene
            if (i < cnt) {
                if (seen[c - start]) return 0;
                seen[c - start] = 1;
            }
            if (c == last) return 0;   // no immediate repeat, any boundary
            last = c;
        }
    }
    return 1;
}

static int rng_never_sticks(void) {
    uint32_t s = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t v = cheer_rng_next(&s);
        if (v == 0 && s == 0 && i < 99) return 0;
    }
    return 1;
}

int main(void) {
    assert(library_ok());
    assert(seen_all_once());
    assert(no_repeat_across_reshuffle());
    assert(stages_ok());
    assert(groups_and_extents_ok());
    assert(scene_bag_ok());
    assert(rng_never_sticks());
    printf("cheer cards: library/bag/stages/scenes/rng ok (%zu cards)\n",
           CHEER_CARD_COUNT);
    return 0;
}
