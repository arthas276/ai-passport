// ear_pet.c - XP -> stage mapping and progress helpers. PURE C.
#include "ear_game.h"

// Cumulative XP thresholds, indexed by stage (stage 0 = Egg starts at 0).
static const uint32_t STAGE_XP[EAR_STAGE_COUNT] = {
    0, EAR_XP_HATCHLING, EAR_XP_CHICK, EAR_XP_FLEDGLING, EAR_XP_SONGBIRD,
    EAR_XP_VIRTUOSO, EAR_XP_MASTER, EAR_XP_LEGEND,
};

// Stage from cumulative XP. XP never decreases, thresholds are monotonic.
ear_stage_t ear_pet_stage(uint32_t xp) {
    ear_stage_t st = EAR_STAGE_EGG;
    for (int i = 1; i < (int)EAR_STAGE_COUNT; i++) {
        if (xp >= STAGE_XP[i]) st = (ear_stage_t)i; else break;
    }
    return st;
}

// XP still needed to reach the next stage; 0 at final stage.
uint32_t ear_pet_xp_to_next(uint32_t xp) {
    ear_stage_t st = ear_pet_stage(xp);
    if ((int)st >= (int)EAR_STAGE_LEGEND) return 0;   // v1.7 final rank
    return STAGE_XP[st + 1] - xp;
}

// XP floor of the current stage (progress bar start).
uint32_t ear_pet_stage_floor(uint32_t xp) {
    return STAGE_XP[ear_pet_stage(xp)];
}

// XP ceiling of the current stage (= next threshold). At the final stage the
// bar shows full: ceiling == floor, caller renders 100% without dividing by 0
// (see ear_ui.c).
uint32_t ear_pet_stage_ceiling(uint32_t xp) {
    ear_stage_t st = ear_pet_stage(xp);
    if ((int)st >= (int)EAR_STAGE_LEGEND) return STAGE_XP[st];   // v1.7
    return STAGE_XP[st + 1];
}
