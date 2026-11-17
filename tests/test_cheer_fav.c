// test_cheer_fav.c - host tests for the favorite-card list. v1.3.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cheer_fav.h"

int main(void) {
    cheer_fav_t f = { 0 };

    // Toggle on/off, membership, insertion order.
    assert(!cheer_fav_has(&f, 3));
    assert(cheer_fav_toggle(&f, 3));
    assert(cheer_fav_has(&f, 3));
    assert(f.count == 1 && f.items[0] == 3);
    assert(cheer_fav_index_of(&f, 3) == 0);
    assert(!cheer_fav_toggle(&f, 3));
    assert(!cheer_fav_has(&f, 3) && f.count == 0);

    assert(cheer_fav_toggle(&f, 10));
    assert(cheer_fav_toggle(&f, 20));
    assert(cheer_fav_toggle(&f, 30));
    assert(f.count == 3);
    assert(cheer_fav_index_of(&f, 20) == 1);
    // Removal keeps order of the rest.
    assert(!cheer_fav_toggle(&f, 20));
    assert(f.count == 2 && f.items[0] == 10 && f.items[1] == 30);

    // Cap: the 33rd distinct card is refused, state untouched.
    cheer_fav_t full = { 0 };
    for (uint16_t i = 0; i < CHEER_FAV_CAP; i++) {
        assert(cheer_fav_toggle(&full, i));
    }
    assert(full.count == CHEER_FAV_CAP);
    assert(!cheer_fav_toggle(&full, 999));
    assert(full.count == CHEER_FAV_CAP);
    assert(!cheer_fav_toggle(&full, 5));   // removing still works when full
    assert(full.count == CHEER_FAV_CAP - 1);

    // Blob roundtrip.
    uint8_t blob[CHEER_FAV_CAP * 2];
    size_t len = cheer_fav_serialize(&f, blob, sizeof(blob));
    assert(len == 4);
    assert(blob[0] == 10 && blob[1] == 0 && blob[2] == 30 && blob[3] == 0);
    cheer_fav_t g = { 0 };
    g.count = 9;
    assert(cheer_fav_deserialize(&g, blob, len));
    assert(g.count == 2 && g.items[0] == 10 && g.items[1] == 30);
    // Short buffer: refuses without writing garbage.
    assert(cheer_fav_serialize(&f, blob, 1) == 0);
    // Malformed: odd length / oversize clear the list and fail.
    uint8_t odd[3] = { 1, 2, 3 };
    assert(!cheer_fav_deserialize(&g, odd, sizeof(odd)) && g.count == 0);
    uint8_t big[CHEER_FAV_CAP * 2 + 2] = { 0 };
    assert(!cheer_fav_deserialize(&g, big, sizeof(big)) && g.count == 0);

    // Milestones: 0..9 -> 0, then highest multiple of ten.
    assert(cheer_fav_milestone_at(0) == 0);
    assert(cheer_fav_milestone_at(9) == 0);
    assert(cheer_fav_milestone_at(10) == 10);
    assert(cheer_fav_milestone_at(19) == 10);
    assert(cheer_fav_milestone_at(20) == 20);
    assert(cheer_fav_milestone_at(320) == 320);

    printf("cheer fav: toggle/cap/blob/milestones ok\n");
    return 0;
}
