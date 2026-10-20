// ear_rhythm.h - [PURE] rhythm-dictation mode: pattern generation and
// tap grading (PRD 3.6, v1.8). One 4/4 bar in sixteenth-note slots; the
// user taps the bar back on the OK button after a count-in bar. Pure C:
// no IDF/LVGL includes so host tests compile it directly.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ear_game.h"

#define EAR_RHY_BAR_SLOTS 16u    // one 4/4 bar = 16 sixteenth slots
#define EAR_RHY_TAPS_MAX  16u    // recorded taps per pattern (extras kept)
#define EAR_RHY_PATTERNS  10u    // patterns per session (quiz scale)

// Pattern = onset bitmask over the bar; bit 0 (downbeat) is always set so
// the tapper has an anchor after the count-in.
typedef struct {
    uint16_t onsets;             // bit i = onset at sixteenth slot i
} ear_rhythm_pattern_t;

// Session state (10 patterns, auto level adjust like the quiz).
typedef struct {
    uint8_t level;               // 1..3 (persisted as store rlvl)
    uint8_t index;               // 0-based pattern index
    uint8_t score;               // patterns with >= 2/3 onsets hit
    uint32_t seed;               // xorshift32 state (0 -> default seed)
} ear_rhythm_session_t;

// Beat length per level: slower bar = easier tapping.
extern const uint16_t EAR_RHY_BEAT_MS[EAR_LEVEL_MAX];

// Fill a fresh session. Level clamped 1..EAR_LEVEL_MAX; seed 0 -> default.
void ear_rhythm_session_init(ear_rhythm_session_t *s, uint8_t level,
                             uint32_t seed);
// Generate the NEXT pattern into *out. Deterministic per seed; L1 uses
// quarter-note positions only, L2 adds eighth pairs, L3 any slot with
// sixteenth syncopation. The downbeat (slot 0) is always an onset.
void ear_rhythm_next_pattern(ear_rhythm_session_t *s,
                             ear_rhythm_pattern_t *out);
// Onset count of a pattern (2..8 by construction).
uint8_t ear_rhythm_onset_count(const ear_rhythm_pattern_t *p);
// ms offset of a sixteenth slot within the bar.
uint32_t ear_rhythm_slot_ms(uint16_t beat_ms, uint8_t slot);
// Grade user taps (ms offsets from the pattern-bar start, arrival order)
// against the pattern onsets. Each onset matches the earliest unused tap
// inside a per-level tolerance window; unmatched taps are extras. Returns
// matched onset count; *perfect = every onset matched and no extra taps.
uint8_t ear_rhythm_grade(const ear_rhythm_pattern_t *p, uint16_t beat_ms,
                         const uint32_t *tap_ms, uint8_t tap_count,
                         bool *perfect);
