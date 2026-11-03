// breath_engine.c - session timeline for BREATHE. PURE C (host-testable).
//
// Stateless by design: every call is a pure function of (pattern, elapsed
// ms), so the audio worker can drive it from its sample counter without a
// second clock, and host tests can probe any instant directly. Integer
// math only; the one division per call is 16 kHz-cheap on the FPU-less
// ESP32-C3 (called once per 512-sample chunk in practice).
#include "breath.h"

const breath_pattern_t BREATH_PATTERNS[BREATH_PATTERN_COUNT] = {
    { "FLOW",  "5.5 + 5.5",     5500, 0,    5500, 0    },
    { "CALM",  "4 + 7 + 8",     4000, 7000, 8000, 0    },
    { "BOX",   "4 + 4 + 4 + 4", 4000, 4000, 4000, 4000 },
    { "SLEEP", "4 + 6",         4000, 0,    6000, 0    },
    { "WAKE",  "6 + 3",         6000, 0,    3000, 0    },
};

uint32_t breath_engine_cycle_total_ms(const breath_pattern_t *p) {
    return (uint32_t)p->inhale_ms + p->hold_ms + p->exhale_ms + p->hold2_ms;
}

uint32_t breath_engine_minutes(uint32_t elapsed_ms) {
    return elapsed_ms / 60000u;
}

bool breath_engine_target_reached(uint32_t elapsed_ms, uint32_t target_ms) {
    if (target_ms == 0u) return false;
    return elapsed_ms >= BREATH_READY_MS + target_ms;
}

uint32_t breath_engine_next_milestone(uint32_t reached_minutes) {
    static const uint32_t STEPS[] = { 10u, 30u, 60u, 120u, 300u, 600u };
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); i++) {
        if (STEPS[i] > reached_minutes) return STEPS[i];
    }
    uint32_t m = 600u;
    while (m <= reached_minutes) m *= 2u;
    return m;
}

void breath_engine_at(const breath_pattern_t *p, uint32_t elapsed_ms,
                      breath_state_t *out) {
    out->elapsed_ms = elapsed_ms;

    if (elapsed_ms < BREATH_READY_MS) {
        out->phase = BREATH_PHASE_READY;
        out->phase_total_ms = BREATH_READY_MS;
        out->phase_left_ms = (uint16_t)(BREATH_READY_MS - elapsed_ms);
        out->progress = (uint16_t)((elapsed_ms * 4096u) / BREATH_READY_MS);
        out->cycle = 0;
        return;
    }

    uint32_t t = elapsed_ms - BREATH_READY_MS;
    const uint32_t total = breath_engine_cycle_total_ms(p);
    out->cycle = t / total;
    uint32_t rem = t % total;

    // Walk the four phases in order; zero-length phases are skipped, and a
    // boundary instant belongs to the next non-zero phase (progress 0).
    for (int i = 0; i < 4; i++) {
        breath_phase_t ph;
        uint16_t dur;
        switch (i) {
        case 0:  ph = BREATH_PHASE_INHALE; dur = p->inhale_ms; break;
        case 1:  ph = BREATH_PHASE_HOLD;   dur = p->hold_ms;   break;
        case 2:  ph = BREATH_PHASE_EXHALE; dur = p->exhale_ms; break;
        default: ph = BREATH_PHASE_HOLD2;  dur = p->hold2_ms;  break;
        }
        if (dur == 0) continue;
        if (rem < dur) {
            out->phase = ph;
            out->phase_total_ms = dur;
            out->phase_left_ms = (uint16_t)(dur - rem);
            out->progress = (uint16_t)((rem * 4096u) / dur);
            return;
        }
        rem -= dur;
    }
    // Unreachable for valid tables: rem < total = sum of non-zero phases,
    // so some phase always claims it. Keep a defined fallback anyway.
    out->phase = BREATH_PHASE_EXHALE;
    out->phase_total_ms = p->exhale_ms;
    out->phase_left_ms = 0;
    out->progress = 4096;
}
