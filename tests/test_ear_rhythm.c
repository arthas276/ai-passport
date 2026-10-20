// Host test for rhythm dictation: generation invariants, slot math,
// grading windows, extras, perfect detection.
#include <assert.h>
#include <stdio.h>
#include "ear_rhythm.h"

int main(void)
{
    // Slot math: sixteenth i sits at i*beat/4 ms.
    assert(ear_rhythm_slot_ms(600, 0) == 0);
    assert(ear_rhythm_slot_ms(600, 4) == 600);
    assert(ear_rhythm_slot_ms(600, 8) == 1200);
    assert(ear_rhythm_slot_ms(600, 16 - 1) == 2250);

    // Generation invariants across levels: downbeat always set, count in
    // the level's span, L1/L2 onsets only on their grid.
    for (uint8_t lvl = 1; lvl <= EAR_LEVEL_MAX; lvl++) {
        ear_rhythm_session_t s;
        ear_rhythm_session_init(&s, lvl, 1234u + lvl);
        static const uint8_t LO[3] = { 3, 4, 5 };
        static const uint8_t HI[3] = { 4, 6, 8 };
        for (int i = 0; i < 50; i++) {
            ear_rhythm_pattern_t p;
            ear_rhythm_next_pattern(&s, &p);
            assert(p.onsets & 1u);
            uint8_t n = ear_rhythm_onset_count(&p);
            assert(n >= LO[lvl - 1] && n <= HI[lvl - 1]);
            for (uint8_t slot = 0; slot < EAR_RHY_BAR_SLOTS; slot++) {
                if (!(p.onsets & (1u << slot))) continue;
                if (lvl == 1) assert(slot % 4u == 0u);
                if (lvl == 2) assert(slot % 2u == 0u);
            }
        }
    }

    // Determinism: same seed -> same pattern sequence.
    ear_rhythm_session_t a, b;
    ear_rhythm_session_init(&a, 3, 42u);
    ear_rhythm_session_init(&b, 3, 42u);
    for (int i = 0; i < 20; i++) {
        ear_rhythm_pattern_t pa, pb;
        ear_rhythm_next_pattern(&a, &pa);
        ear_rhythm_next_pattern(&b, &pb);
        assert(pa.onsets == pb.onsets);
    }

    // Grading: exact taps on quarters at 600 ms beat = perfect.
    ear_rhythm_pattern_t p;
    p.onsets = (uint16_t)((1u << 0) | (1u << 4) | (1u << 8) | (1u << 12));
    uint32_t taps[EAR_RHY_TAPS_MAX];
    taps[0] = 5; taps[1] = 605; taps[2] = 1198; taps[3] = 1804;
    bool perfect = false;
    uint8_t hits = ear_rhythm_grade(&p, 600, taps, 4, &perfect);
    assert(hits == 4 && perfect);

    // Late taps inside the window still match.
    taps[0] = 180; taps[1] = 760; taps[2] = 1380; taps[3] = 1950;
    hits = ear_rhythm_grade(&p, 600, taps, 4, &perfect);
    assert(hits == 4 && perfect);

    // Way-off taps miss (300 = midpoint between quarter targets).
    taps[0] = 300; taps[1] = 300; taps[2] = 300; taps[3] = 300;
    hits = ear_rhythm_grade(&p, 600, taps, 4, &perfect);
    assert(hits == 0 && !perfect);

    // Missing one onset: 3 of 4 hit, not perfect.
    taps[0] = 5; taps[1] = 605; taps[2] = 1198;
    hits = ear_rhythm_grade(&p, 600, taps, 3, &perfect);
    assert(hits == 3 && !perfect);

    // Extra tap spoils perfect even when all onsets matched.
    taps[0] = 5; taps[1] = 300; taps[2] = 605; taps[3] = 1198; taps[4] = 1804;
    hits = ear_rhythm_grade(&p, 600, taps, 5, &perfect);
    assert(hits == 4 && !perfect);

    // Unordered safety: taps assumed ordered, but a stray early tap must
    // not shift later matches beyond the window logic (extras skipped).
    taps[0] = 0; taps[1] = 5; taps[2] = 605; taps[3] = 1198; taps[4] = 1804;
    hits = ear_rhythm_grade(&p, 600, taps, 5, &perfect);
    assert(hits == 4 && !perfect);

    printf("rhythm: generation + slot math + grading windows ok\n");
    return 0;
}
