// Host test for the BREATHE session timeline: pattern totals, READY
// window, zero-length phase skipping, boundary instants, cycle counting,
// progress exactness/monotonicity, minutes bookkeeping.
#include <assert.h>
#include <stdio.h>
#include "breath.h"

static const breath_pattern_t *flow = &BREATH_PATTERNS[0];
static const breath_pattern_t *calm = &BREATH_PATTERNS[1];
static const breath_pattern_t *box = &BREATH_PATTERNS[2];

int main(void)
{
    breath_state_t st;

    // pattern table integrity: labels/subs present, totals exact
    assert(flow->label && flow->sub);
    assert(calm->label && calm->sub);
    assert(box->label && box->sub);
    assert(breath_engine_cycle_total_ms(flow) == 11000);
    assert(breath_engine_cycle_total_ms(calm) == 19000);
    assert(breath_engine_cycle_total_ms(box) == 16000);

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

    printf("breath engine: timeline ok\n");
    return 0;
}
