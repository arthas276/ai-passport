// Host test for VS BATTLE scoring: alternation, scores, verdicts, clamping.
#include <assert.h>
#include <stdio.h>
#include "ear_vs.h"

int main(void)
{
    ear_vs_t vs;

    // Fresh match: P1 opens, not done.
    ear_vs_begin(&vs, EAR_MODE_INTERVAL, 2);
    assert(vs.index == 0 && vs.p1_score == 0 && vs.p2_score == 0);
    assert(!ear_vs_done(&vs));
    assert(ear_vs_player(&vs) == 0);

    // Alternation holds across the whole match; P1 answers everything
    // correctly, P2 always misses.
    for (uint8_t i = 0; i < EAR_VS_QUESTIONS; i++) {
        assert(!ear_vs_done(&vs));
        assert(ear_vs_player(&vs) == (uint8_t)(i & 1u));
        ear_vs_record(&vs, (i & 1u) == 0u);
    }
    assert(ear_vs_done(&vs));
    assert(vs.p1_score == 5 && vs.p2_score == 0);
    assert(ear_vs_winner(&vs) == 0);

    // Recording past the end must not corrupt scores or the index.
    ear_vs_record(&vs, true);
    assert(vs.p1_score == 5 && vs.index == EAR_VS_QUESTIONS);

    // All correct on both sides: draw.
    ear_vs_begin(&vs, EAR_MODE_INTERVAL, 1);
    for (uint8_t i = 0; i < EAR_VS_QUESTIONS; i++) ear_vs_record(&vs, true);
    assert(vs.p1_score == 5 && vs.p2_score == 5);
    assert(ear_vs_winner(&vs) == -1);

    // P2 sweep: P2 wins.
    ear_vs_begin(&vs, EAR_MODE_CHORD, 3);
    for (uint8_t i = 0; i < EAR_VS_QUESTIONS; i++) {
        ear_vs_record(&vs, (i & 1u) != 0u);
    }
    assert(vs.p1_score == 0 && vs.p2_score == 5);
    assert(ear_vs_winner(&vs) == 1);

    printf("vs: alternation + scoring + winner ok\n");
    return 0;
}
