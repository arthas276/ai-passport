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

// ---------------------------------------------------------------------------
// v2.1: interactions - mood state + treat bank
// ---------------------------------------------------------------------------
static const char *const STAGE_NAMES[EAR_STAGE_COUNT] = {
    "EGG", "HATCHLING", "CHICK", "FLEDGLING",
    "SONGBIRD", "VIRTUOSO", "MASTER", "LEGEND",
};

const char *ear_pet_stage_name(ear_stage_t st) {
    if ((int)st < 0 || (int)st >= (int)EAR_STAGE_COUNT) return STAGE_NAMES[0];
    return STAGE_NAMES[st];
}

// Event deltas: petting is a small nudge, feeding the big one; quiz answers
// keep the pet breathing along with practice (PRD 3.10).
static const int8_t MOOD_DELTA[] = {
    10,    // EAR_MOOD_EV_PET
    20,    // EAR_MOOD_EV_FEED
    5,     // EAR_MOOD_EV_CORRECT
    -6,    // EAR_MOOD_EV_WRONG
};

void ear_pet_mood_init(ear_pet_mood_t *m) {
    m->joy = 30;             // mid CONTENT: a fresh boot is a calm pet
    m->idle_ms = 0;
}

void ear_pet_mood_event(ear_pet_mood_t *m, ear_mood_ev_t ev) {
    int32_t j = m->joy + MOOD_DELTA[ev];
    if (j < 0) j = 0;
    if (j > 100) j = 100;
    m->joy = (int16_t)j;
    m->idle_ms = 0;          // interaction restarts the decay grace window
}

void ear_pet_mood_tick(ear_pet_mood_t *m, uint32_t dt_ms) {
    uint32_t before = m->idle_ms;
    uint32_t after = before + dt_ms;
    if (after < before) after = 0xFFFFFFFFu;   // saturated: treat as fully idle
    m->idle_ms = after;
    // Decay only the portion of dt beyond the 30 s grace window; counting
    // crossed 2 s milestones (floor difference) keeps it exact at any call
    // cadence (per second or lazily on key press).
    uint32_t old_ex = before > EAR_MOOD_GRACE_MS ? before - EAR_MOOD_GRACE_MS : 0;
    uint32_t new_ex = after > EAR_MOOD_GRACE_MS ? after - EAR_MOOD_GRACE_MS : 0;
    uint32_t step = new_ex / EAR_MOOD_DECAY_MS - old_ex / EAR_MOOD_DECAY_MS;
    if (step > 0) {
        m->joy = (int16_t)(m->joy > (int16_t)step ? m->joy - step : 0);
    }
}

ear_mood_t ear_pet_mood_get(const ear_pet_mood_t *m) {
    if (m->joy < 20) return EAR_MOOD_SLEEPY;
    if (m->joy < 45) return EAR_MOOD_CONTENT;
    if (m->joy < 75) return EAR_MOOD_HAPPY;
    return EAR_MOOD_JOY;
}

const char *ear_pet_mood_name(ear_mood_t mood) {
    switch (mood) {
    case EAR_MOOD_SLEEPY: return "SLEEPY";
    case EAR_MOOD_CONTENT: return "CONTENT";
    case EAR_MOOD_HAPPY: return "HAPPY";
    default: return "JOY";
    }
}

uint32_t ear_pet_treat_earn(uint32_t *treats) {
    if (*treats < EAR_PET_TREATS_MAX) (*treats)++;
    return *treats;
}

bool ear_pet_treat_feed(uint32_t *treats) {
    if (*treats == 0) return false;
    (*treats)--;
    return true;
}
