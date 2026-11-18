// Host test for pet progression: thresholds, monotonicity, helpers,
// plus v2.1 mood state and the treat bank.
#include <assert.h>
#include <stdio.h>
#include <string.h>
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

    // v2.1 mood: init CONTENT (45); events clamp at 0/100 and map the four
    // labels; idle decay only starts past the 30 s grace window.
    ear_pet_mood_t m;
    ear_pet_mood_init(&m);
    assert(m.joy == 30);
    assert(ear_pet_mood_get(&m) == EAR_MOOD_CONTENT);
    for (int i = 0; i < 20; i++) ear_pet_mood_event(&m, EAR_MOOD_EV_FEED);
    assert(m.joy == 100);
    assert(ear_pet_mood_get(&m) == EAR_MOOD_JOY);
    for (int i = 0; i < 40; i++) ear_pet_mood_event(&m, EAR_MOOD_EV_WRONG);
    assert(m.joy == 0);
    assert(ear_pet_mood_get(&m) == EAR_MOOD_SLEEPY);
    for (int i = 0; i < 5; i++) ear_pet_mood_event(&m, EAR_MOOD_EV_PET);
    assert(m.joy == 50);
    assert(ear_pet_mood_get(&m) == EAR_MOOD_HAPPY);
    assert(strcmp(ear_pet_mood_name(EAR_MOOD_SLEEPY), "SLEEPY") == 0);
    assert(strcmp(ear_pet_stage_name(EAR_STAGE_LEGEND), "LEGEND") == 0);

    // Decay: grace window first, then -1 per 2 s; the same total idle time
    // yields the same joy whether ticked per 2 s or in one lazy pump.
    ear_pet_mood_init(&m);
    ear_pet_mood_tick(&m, EAR_MOOD_GRACE_MS);
    assert(m.joy == 30);              // still inside the grace window
    ear_pet_mood_tick(&m, EAR_MOOD_DECAY_MS - 1);
    assert(m.joy == 30);              // below one decay step
    ear_pet_mood_tick(&m, 1);
    assert(m.joy == 29);              // exact step boundary
    for (int i = 0; i < 28; i++) ear_pet_mood_tick(&m, EAR_MOOD_DECAY_MS);
    assert(m.joy == 1);
    ear_pet_mood_init(&m);            // lazy pump with the same total idle
    ear_pet_mood_tick(&m,
        EAR_MOOD_GRACE_MS + EAR_MOOD_DECAY_MS * 29);
    assert(m.joy == 1);
    for (int i = 0; i < 60; i++) ear_pet_mood_tick(&m, EAR_MOOD_DECAY_MS);
    assert(m.joy == 0);               // decay floors at 0, never negative
    // Any event restarts the grace window.
    ear_pet_mood_init(&m);
    ear_pet_mood_tick(&m, 60000);     // 15 decay steps -> joy 15
    assert(m.joy == 15);
    ear_pet_mood_event(&m, EAR_MOOD_EV_PET);   // joy 25, idle reset
    ear_pet_mood_tick(&m, 20000);     // inside the new grace window
    assert(m.joy == 25);

    // v2.1 treat bank: earn caps at 9, feed drains and then refuses.
    uint32_t tr = 0;
    for (int i = 0; i < 12; i++) (void)ear_pet_treat_earn(&tr);
    assert(tr == EAR_PET_TREATS_MAX);
    assert(ear_pet_treat_feed(&tr) && tr == EAR_PET_TREATS_MAX - 1);
    while (tr > 0) assert(ear_pet_treat_feed(&tr));
    assert(tr == 0);
    assert(!ear_pet_treat_feed(&tr));   // empty bank: no change, false
    assert(tr == 0);

    printf("pet: thresholds + helpers + mood + treats ok (v2.1)\n");
    return 0;
}
