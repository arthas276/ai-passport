// Host test for the BREATHE session timeline: pattern totals, READY
// window, zero-length phase skipping, boundary instants, cycle counting,
// progress exactness/monotonicity, minutes bookkeeping.
#include <assert.h>
#include <stdio.h>
#include "breath.h"

static const breath_pattern_t *flow = &BREATH_PATTERNS[0];
static const breath_pattern_t *calm = &BREATH_PATTERNS[1];
static const breath_pattern_t *sigh = &BREATH_PATTERNS[2];
static const breath_pattern_t *box = &BREATH_PATTERNS[3];
static const breath_pattern_t *sleep = &BREATH_PATTERNS[4];
static const breath_pattern_t *wake = &BREATH_PATTERNS[5];
static const breath_pattern_t *yours = &BREATH_PATTERNS[6];

int main(void)
{
    breath_state_t st;

    // pattern table integrity: labels/subs present, totals exact
    for (uint32_t i = 0; i < BREATH_PATTERN_COUNT; i++) {
        assert(BREATH_PATTERNS[i].label && BREATH_PATTERNS[i].sub);
        assert(BREATH_PATTERNS[i].inhale_ms > 0);
        assert(BREATH_PATTERNS[i].exhale_ms > 0);
    }
    assert(breath_engine_cycle_total_ms(flow) == 11000);
    assert(breath_engine_cycle_total_ms(calm) == 19000);
    assert(breath_engine_cycle_total_ms(sigh) == 11000);
    assert(breath_engine_cycle_total_ms(box) == 16000);
    assert(breath_engine_cycle_total_ms(sleep) == 10000);
    assert(breath_engine_cycle_total_ms(wake) == 9000);
    assert(breath_engine_cycle_total_ms(yours) == 10000);   // default 5+5
    assert(BREATH_PATTERN_COUNT == 7u);
    assert(BREATH_PATTERN_CUSTOM == 6u);

    // READY window
    breath_engine_at(calm, 0, &st);
    assert(st.phase == BREATH_PHASE_READY);
    assert(st.cycle == 0 && st.phase_left_ms == 3000 && st.progress == 0);
    breath_engine_at(calm, 2999, &st);
    assert(st.phase == BREATH_PHASE_READY && st.phase_left_ms == 1);
    // boundary instant belongs to the NEXT phase
    breath_engine_at(calm, 3000, &st);
    assert(st.phase == BREATH_PHASE_INHALE);
    assert(st.progress == 0 && st.cycle == 0);

    // FLOW skips both holds: INHALE runs straight into EXHALE
    breath_engine_at(flow, 3000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.phase_total_ms == 5500);
    breath_engine_at(flow, 3000 + 5500 - 1, &st);
    assert(st.phase == BREATH_PHASE_INHALE);
    breath_engine_at(flow, 3000 + 5500, &st);
    assert(st.phase == BREATH_PHASE_EXHALE);
    breath_engine_at(flow, 3000 + 11000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.cycle == 1);

    // CALM walks INHALE -> HOLD -> EXHALE (no HOLD2)
    breath_engine_at(calm, 3000 + 4000, &st);
    assert(st.phase == BREATH_PHASE_HOLD && st.phase_left_ms == 7000);
    breath_engine_at(calm, 3000 + 4000 + 7000, &st);
    assert(st.phase == BREATH_PHASE_EXHALE && st.phase_left_ms == 8000);
    breath_engine_at(calm, 3000 + 19000 - 1, &st);
    assert(st.phase == BREATH_PHASE_EXHALE && st.cycle == 0);
    breath_engine_at(calm, 3000 + 19000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.cycle == 1);

    // BOX has HOLD2
    breath_engine_at(box, 3000 + 12000, &st);
    assert(st.phase == BREATH_PHASE_HOLD2 && st.phase_left_ms == 4000);
    breath_engine_at(box, 3000 + 16000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.cycle == 1);

    // SIGH (v1.3) walks INHALE -> INHALE2 -> EXHALE (hold skipped)
    breath_engine_at(sigh, 3000 + 2500 - 1, &st);
    assert(st.phase == BREATH_PHASE_INHALE);
    breath_engine_at(sigh, 3000 + 2500, &st);
    assert(st.phase == BREATH_PHASE_INHALE2 && st.phase_total_ms == 1500
           && st.progress == 0);
    breath_engine_at(sigh, 3000 + 2500 + 1499, &st);
    assert(st.phase == BREATH_PHASE_INHALE2 && st.phase_left_ms == 1);
    breath_engine_at(sigh, 3000 + 4000, &st);
    assert(st.phase == BREATH_PHASE_EXHALE && st.phase_total_ms == 7000);
    breath_engine_at(sigh, 3000 + 11000 - 1, &st);
    assert(st.phase == BREATH_PHASE_EXHALE && st.cycle == 0);
    breath_engine_at(sigh, 3000 + 11000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.cycle == 1);

    // ---- runtime table (v1.3): init / accessor / set_custom ----
    breath_engine_init();
    assert(breath_engine_pattern(BREATH_PATTERN_CUSTOM)->inhale_ms == 5000);
    assert(breath_engine_pattern(BREATH_PATTERN_CUSTOM)->exhale_ms == 5000);
    assert(breath_engine_pattern(0)->inhale_ms ==
           BREATH_PATTERNS[0].inhale_ms);
    // out-of-range index degrades to slot 0, never off the table
    assert(breath_engine_pattern(200) == breath_engine_pattern(0));

    // invalid customs are rejected without touching the table
    breath_pattern_t bad = *yours;
    bad.inhale_ms = 0;
    assert(!breath_engine_set_custom(&bad));
    bad = *yours;
    bad.exhale_ms = 11000;   // above the 10 s cap
    assert(!breath_engine_set_custom(&bad));
    assert(!breath_engine_set_custom(NULL));
    assert(breath_engine_pattern(BREATH_PATTERN_CUSTOM)->inhale_ms == 5000);

    // valid custom lands, walks, and survives source mutation (deep copy)
    breath_pattern_t mine = { .label = "YOURS", .sub = "4 + 2 + 6 + 2",
                              .inhale_ms = 4000, .hold_ms = 2000,
                              .inhale2_ms = 0, .exhale_ms = 6000,
                              .hold2_ms = 2000 };
    assert(breath_engine_set_custom(&mine));
    mine.inhale_ms = 9999;   // caller-side change must not leak in
    const breath_pattern_t *rt = breath_engine_pattern(BREATH_PATTERN_CUSTOM);
    assert(rt->inhale_ms == 4000 && rt->hold_ms == 2000 &&
           rt->exhale_ms == 6000 && rt->hold2_ms == 2000);
    breath_engine_at(rt, 3000 + 4000 + 2000 + 6000, &st);
    assert(st.phase == BREATH_PHASE_HOLD2 && st.phase_left_ms == 2000);
    breath_engine_at(rt, 3000 + 14000, &st);
    assert(st.phase == BREATH_PHASE_INHALE && st.cycle == 1);

    // cycle counting mid-cycle
    breath_engine_at(flow, 3000 + 11000u * 7 + 1234, &st);
    assert(st.cycle == 7);

    // progress exactness: half inhale is exactly half scale
    breath_engine_at(flow, 3000 + 2750, &st);
    assert(st.progress == 2048);

    // progress monotone within a phase, always in range
    for (uint32_t t = 3000; t < 3000 + 11000u; t += 100) {
        breath_state_t a, b;
        breath_engine_at(flow, t, &a);
        uint32_t t2 = t + 100;
        if (t2 > 3000 + 11000u) t2 = 3000 + 11000u;
        breath_engine_at(flow, t2, &b);
        assert(a.progress <= 4096 && b.progress <= 4096);
        if (a.phase == b.phase) assert(b.progress >= a.progress);
    }

    // minutes bookkeeping
    assert(breath_engine_minutes(59000) == 0);
    assert(breath_engine_minutes(60000) == 1);
    assert(breath_engine_minutes(124000) == 2);

    // timed sessions (v1.2): FREE never reports done; the READY countdown
    // does not count toward the target
    assert(!breath_engine_target_reached(0, 0));
    assert(!breath_engine_target_reached(999999, 0));
    assert(!breath_engine_target_reached(62999, 60000));
    assert(breath_engine_target_reached(63000, 60000));
    assert(breath_engine_target_reached(1000000, 60000));

    // milestone thresholds (v1.2): ladder then doubling
    assert(breath_engine_next_milestone(0) == 10);
    assert(breath_engine_next_milestone(10) == 30);
    assert(breath_engine_next_milestone(29) == 30);
    assert(breath_engine_next_milestone(600) == 1200);
    assert(breath_engine_next_milestone(1200) == 2400);

    printf("breath engine: timeline ok\n");
    return 0;
}
