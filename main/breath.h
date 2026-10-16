// breath.h - shared types/constants for BREATHE (pocket breathing coach).
// Pure C header: no IDF/LVGL includes so [PURE] modules can compile on host.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Session timeline (breath_engine.c, pure)
// ---------------------------------------------------------------------------
#define BREATH_READY_MS       3000u   // GET READY countdown before first inhale
#define BREATH_TICK_MS        100u    // audio -> UI event period (10 Hz)
#define BREATH_PATTERN_COUNT  3u

// Phase durations in ms. Zero-length phases are skipped by the engine, never
// reported as phases. Engine contract: at least one phase is non-zero
// (the const pattern table below guarantees it; host tests assert it).
typedef struct {
    const char *label;       // HOME row primary text ("FLOW")
    const char *sub;         // HOME row secondary text ("5.5 + 5.5")
    uint16_t inhale_ms;
    uint16_t hold_ms;        // post-inhale hold, 0 = none
    uint16_t exhale_ms;
    uint16_t hold2_ms;       // post-exhale hold, 0 = none (box breathing)
} breath_pattern_t;

extern const breath_pattern_t BREATH_PATTERNS[BREATH_PATTERN_COUNT];

typedef enum {
    BREATH_PHASE_READY = 0,  // GET READY countdown (not a breath phase)
    BREATH_PHASE_INHALE,
    BREATH_PHASE_HOLD,
    BREATH_PHASE_EXHALE,
    BREATH_PHASE_HOLD2,
} breath_phase_t;

// Timeline snapshot at one instant. progress is Q12: 0..4096 spanning the
// current phase (0 = phase start, 4096 = phase end).
typedef struct {
    breath_phase_t phase;
    uint16_t phase_total_ms;  // duration of the current phase
    uint16_t phase_left_ms;   // remaining in the current phase
    uint16_t progress;        // 0..4096 progress within the current phase
    uint32_t cycle;           // fully completed breath cycles so far
    uint32_t elapsed_ms;      // session elapsed (echo of the query)
} breath_state_t;

// Total length of one breath cycle: in + hold + out + hold2 (ms).
uint32_t breath_engine_cycle_total_ms(const breath_pattern_t *p);

// Stateless timeline evaluation. elapsed_ms < BREATH_READY_MS reports
// BREATH_PHASE_READY; afterwards phases walk in order, skipping zero-length
// entries; elapsed_ms lands at a phase boundary belongs to the NEXT phase
// with the cycle counter already advanced.
void breath_engine_at(const breath_pattern_t *p, uint32_t elapsed_ms,
                      breath_state_t *out);

// Whole minutes in an elapsed time (stats bookkeeping).
uint32_t breath_engine_minutes(uint32_t elapsed_ms);

// ---------------------------------------------------------------------------
// Glide tone (breath_tone.c, pure) - constants shared with the worker
// ---------------------------------------------------------------------------
#define BREATH_TONE_SR    16000u   // playback rate = board-validated
#define BREATH_HZ_LOW     220.0f   // A3 - glide floor (inhale start)
#define BREATH_HZ_HIGH    330.0f   // E4 - glide ceiling (inhale end)
