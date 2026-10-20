// Host test for pet progression: thresholds, monotonicity, helpers.
#include <assert.h>
#include <stdio.h>
#include "ear_game.h"

int main(void)
{
    // Stage boundaries (PRD 4: 0/10/30/80/160 + v1.7 endgame 320/640/1280).
    assert(ear_pet_stage(0) == EAR_STAGE_EGG);
    assert(ear_pet_stage(9) == EAR_STAGE_EGG);
    assert(ear_pet_stage(10) == EAR_STAGE_HATCHLING);
    assert(ear_pet_stage(29) == EAR_STAGE_HATCHLING);
    assert(ear_pet_stage(30) == EAR_STAGE_CHICK);
    assert(ear_pet_stage(79) == EAR_STAGE_CHICK);
    assert(ear_pet_stage(80) == EAR_STAGE_FLEDGLING);
    assert(ear_pet_stage(159) == EAR_STAGE_FLEDGLING);
    assert(ear_pet_stage(160) == EAR_STAGE_SONGBIRD);
    assert(ear_pet_stage(319) == EAR_STAGE_SONGBIRD);
    assert(ear_pet_stage(320) == EAR_STAGE_VIRTUOSO);
    assert(ear_pet_stage(639) == EAR_STAGE_VIRTUOSO);
    assert(ear_pet_stage(640) == EAR_STAGE_MASTER);
    assert(ear_pet_stage(1279) == EAR_STAGE_MASTER);
    assert(ear_pet_stage(1280) == EAR_STAGE_LEGEND);
    assert(ear_pet_stage(999999) == EAR_STAGE_LEGEND);   // saturated

    // XP-to-next: 0->10, 10->20, 29->1, 80->80, final->0.
    assert(ear_pet_xp_to_next(0) == 10);
    assert(ear_pet_xp_to_next(10) == 20);
    assert(ear_pet_xp_to_next(29) == 1);
    assert(ear_pet_xp_to_next(80) == 80);
    assert(ear_pet_xp_to_next(160) == 160);   // v1.7: Songbird keeps growing
    assert(ear_pet_xp_to_next(319) == 1);
    assert(ear_pet_xp_to_next(320) == 320);
    assert(ear_pet_xp_to_next(500) == 140);
    assert(ear_pet_xp_to_next(640) == 640);
    assert(ear_pet_xp_to_next(1000) == 280);
    assert(ear_pet_xp_to_next(1280) == 0);
    assert(ear_pet_xp_to_next(999999) == 0);

    // Floor/ceiling for progress bar normalization (ui divides, so ceiling
    // must exceed floor inside a stage; final stage floor == ceiling).
    assert(ear_pet_stage_floor(15) == 10);
    assert(ear_pet_stage_ceiling(15) == 30);
    assert(ear_pet_stage_floor(160) == 160);
    assert(ear_pet_stage_ceiling(160) == 320);   // v1.7: bar rolls on
    assert(ear_pet_stage_floor(500) == 320);
    assert(ear_pet_stage_ceiling(500) == 640);
    assert(ear_pet_stage_floor(1280) == 1280);
    assert(ear_pet_stage_ceiling(1280) == 1280);

    // Monotonicity sweep: stage never decreases as XP grows.
    ear_stage_t prev = EAR_STAGE_EGG;
    for (uint32_t xp = 0; xp <= 2000; xp++) {
        ear_stage_t st = ear_pet_stage(xp);
        assert((int)st >= (int)prev);
        prev = st;
    }
    printf("pet: thresholds + helpers ok (v1.7 endgame ranks)\n");
    return 0;
}
