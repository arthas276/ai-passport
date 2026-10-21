// ear_rhythm.c - [PURE] rhythm dictation: pattern generation + tap grading
// (v1.8). PURE C.
#include "ear_rhythm.h"

// Per-level beat length: L1 700 ms (slow, quarters only), L2 600,
// L3 500 with sixteenth syncopation.
const uint16_t EAR_RHY_BEAT_MS[EAR_LEVEL_MAX] = { 700u, 600u, 500u };

// xorshift32 (same generator family the dictation mode uses).
static uint32_t rhy_rand(uint32_t *seed) {
    uint32_t x = *seed ? *seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return x;
}

// Slot grid per level: which sixteenth positions may carry an onset.
// L1 quarters {0,4,8,12}; L2 adds eighth offbeats {2,6,10,14};
// L3 allows any sixteenth.
#define GRID_L1 { 0u, 4u, 8u, 12u }
#define GRID_L2 { 0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u }

void ear_rhythm_session_init(ear_rhythm_session_t *s, uint8_t level,
                             uint32_t seed) {
    if (level < EAR_LEVEL_MIN) level = EAR_LEVEL_MIN;
    if (level > EAR_LEVEL_MAX) level = EAR_LEVEL_MAX;
    s->level = level;
    s->index = 0;
    s->score = 0;
    s->seed = seed;
}

static uint8_t pick_onset_count(uint32_t *seed, uint8_t level) {
    static const uint8_t LO[EAR_LEVEL_MAX] = { 3u, 4u, 5u };
    static const uint8_t HI[EAR_LEVEL_MAX] = { 4u, 6u, 8u };
    uint8_t span = (uint8_t)(HI[level - 1] - LO[level - 1] + 1u);
    return (uint8_t)(LO[level - 1] + rhy_rand(seed) % span);
}

void ear_rhythm_next_pattern(ear_rhythm_session_t *s,
                             ear_rhythm_pattern_t *out) {
    uint16_t onsets = 0u;
    uint8_t want = pick_onset_count(&s->seed, s->level);
    if (want < 2u) want = 2u;
    if (s->level < EAR_LEVEL_MAX) {
        // Grid levels: draw distinct slots from the fixed grid (rejection
        // draws; the grid is small and want <= grid size by construction).
        static const uint8_t G1[] = GRID_L1;
        static const uint8_t G2[] = GRID_L2;
        const uint8_t *grid = (s->level == 1u) ? G1 : G2;
        uint8_t ngrid = (s->level == 1u) ? 4u : 8u;
        onsets |= 1u;   // downbeat anchor
        for (uint8_t got = 1u; got < want; ) {
            uint8_t slot = grid[rhy_rand(&s->seed) % ngrid];
            if (!(onsets & (1u << slot))) {
                onsets |= (uint16_t)(1u << slot);
                got++;
            }
        }
    } else {
        // L3: any of the 16 slots, downbeat forced.
        onsets |= 1u;
        for (uint8_t got = 1u; got < want; ) {
            uint8_t slot = (uint8_t)(rhy_rand(&s->seed) % EAR_RHY_BAR_SLOTS);
            if (!(onsets & (1u << slot))) {
                onsets |= (uint16_t)(1u << slot);
                got++;
            }
        }
    }
    out->onsets = onsets;
}

uint8_t ear_rhythm_onset_count(const ear_rhythm_pattern_t *p) {
    uint8_t n = 0;
    uint16_t bits = p->onsets;
    while (bits) {
        n = (uint8_t)(n + (bits & 1u));
        bits >>= 1;
    }
    return n;
}

uint32_t ear_rhythm_slot_ms(uint16_t beat_ms, uint8_t slot) {
    return (uint32_t)beat_ms * slot / 4u;
}

// Tolerance: a third of the beat, clamped to a sane window - fast enough
// not to swallow a sixteenth at L3, wide enough for button/queue latency.
static uint32_t tolerance_ms(uint16_t beat_ms) {
    uint32_t tol = beat_ms / 3u;
    if (tol < 120u) tol = 120u;
    if (tol > 220u) tol = 220u;
    return tol;
}

uint8_t ear_rhythm_grade(const ear_rhythm_pattern_t *p, uint16_t beat_ms,
                         const uint32_t *tap_ms, uint8_t tap_count,
                         bool *perfect) {
    uint32_t tol = tolerance_ms(beat_ms);
    uint8_t matched = 0;
    uint8_t next_tap = 0;   // taps arrive in time order; consume in order
    for (uint8_t slot = 0; slot < EAR_RHY_BAR_SLOTS; slot++) {
        if (!(p->onsets & (1u << slot))) continue;
        uint32_t target = ear_rhythm_slot_ms(beat_ms, slot);
        // Skip taps that ended before this window (extras). target > tap
        // guards the subtraction: only strictly-early taps are skipped.
        while (next_tap < tap_count && tap_ms[next_tap] < target &&
               target - tap_ms[next_tap] > tol) {
            next_tap++;
        }
        if (next_tap < tap_count) {
            uint32_t t = tap_ms[next_tap];
            uint32_t d = t >= target ? t - target : target - t;
            if (d <= tol) {
                matched++;
                next_tap++;
            }
        }
    }
    if (perfect) {
        *perfect = (matched == ear_rhythm_onset_count(p)) &&
                   (tap_count == matched);
    }
    return matched;
}

// Live verdict for one tap, mirroring ear_rhythm_grade's greedy match:
// walk onsets from the cursor; a tap more than tol before an onset is a
// dead extra (EARLY), a tap inside a window consumes that onset (HIT),
// and a tap past all remaining windows is LATE. Replay a tap stream
// through one cursor and the HIT count equals grade's matched count.
ear_rhythm_tap_verdict_t ear_rhythm_tap_verdict(const ear_rhythm_pattern_t *p,
        uint16_t beat_ms, uint32_t tap_ms, ear_rhythm_tap_cursor_t *cur,
        int32_t *delta_ms) {
    uint32_t tol = tolerance_ms(beat_ms);
    for (uint8_t slot = cur->next_slot; slot < EAR_RHY_BAR_SLOTS; slot++) {
        if (!(p->onsets & (1u << slot))) continue;
        uint32_t target = ear_rhythm_slot_ms(beat_ms, slot);
        if (tap_ms + tol < target) return EAR_RHY_TAP_EARLY;
        if (tap_ms <= target + tol) {
            cur->next_slot = (uint8_t)(slot + 1u);
            if (delta_ms) *delta_ms = (int32_t)tap_ms - (int32_t)target;
            return EAR_RHY_TAP_HIT;
        }
        // Past this onset's window: the onset is missed, keep walking.
    }
    return EAR_RHY_TAP_LATE;
}
