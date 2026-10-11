// Host tests for tuner modes: tables, cycling, string stepping, range.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tuner.h"
#include "tuner_modes.h"

int main(void)
{
    // Mode set and cycle order.
    assert(TUNER_MODE_COUNT == 5);
    assert(strcmp(tuner_mode_name(TUNER_MODE_CHROMATIC), "CHROMATIC") == 0);
    assert(strcmp(tuner_mode_name(TUNER_MODE_VOICE), "VOICE") == 0);
    assert(tuner_mode_next(TUNER_MODE_CHROMATIC) == TUNER_MODE_GUITAR);
    assert(tuner_mode_next(TUNER_MODE_GUITAR) == TUNER_MODE_UKULELE);
    assert(tuner_mode_next(TUNER_MODE_VOICE) == TUNER_MODE_CHROMATIC);
    printf("modes: set + cycle ok\n");

    // String counts: free modes have none.
    assert(tuner_mode_string_count(TUNER_MODE_CHROMATIC) == 0);
    assert(tuner_mode_string_count(TUNER_MODE_VOICE) == 0);
    assert(tuner_mode_string_count(TUNER_MODE_GUITAR) == 6);
    assert(tuner_mode_string_count(TUNER_MODE_UKULELE) == 4);
    assert(tuner_mode_string_count(TUNER_MODE_VIOLIN) == 4);
    printf("modes: counts ok\n");

    // Tunings: guitar E2..E4, ukulele reentrant G4 first, violin G3..E5.
    static const int16_t GUITAR[] = { 40, 45, 50, 55, 59, 64 };
    static const int16_t UKULELE[] = { 69, 60, 64, 69 };
    static const int16_t VIOLIN[] = { 55, 62, 69, 76 };
    for (int i = 0; i < 6; i++) {
        assert(tuner_mode_string_midi(TUNER_MODE_GUITAR, i) == GUITAR[i]);
    }
    for (int i = 0; i < 4; i++) {
        assert(tuner_mode_string_midi(TUNER_MODE_UKULELE, i) == UKULELE[i]);
        assert(tuner_mode_string_midi(TUNER_MODE_VIOLIN, i) == VIOLIN[i]);
    }
    assert(tuner_mode_string_midi(TUNER_MODE_GUITAR, -1) == -1);
    assert(tuner_mode_string_midi(TUNER_MODE_GUITAR, 6) == -1);
    assert(tuner_mode_string_midi(TUNER_MODE_CHROMATIC, 0) == -1);
    printf("modes: tunings ok\n");

    // String stepper: step/advance wrap and clear the locked flag.
    tuner_strings_t s;
    tuner_strings_reset(&s);
    assert(s.index == 0 && !s.locked);
    s.locked = true;
    tuner_strings_step(&s, TUNER_MODE_GUITAR, 1);
    assert(s.index == 1 && !s.locked);
    tuner_strings_step(&s, TUNER_MODE_GUITAR, -2);   // 1-2 -> wraps to 5
    assert(s.index == 5);
    tuner_strings_step(&s, TUNER_MODE_GUITAR, 1);    // 5+1 -> 0
    assert(s.index == 0);
    tuner_strings_advance(&s, TUNER_MODE_GUITAR);
    assert(s.index == 1);
    s.index = 5;
    tuner_strings_advance(&s, TUNER_MODE_GUITAR);
    assert(s.index == 0);
    tuner_strings_step(&s, TUNER_MODE_CHROMATIC, 1);
    assert(s.index == 0);
    printf("modes: strings stepper ok\n");

    // Voice-range tracker.
    tuner_range_t r;
    tuner_range_reset(&r);
    assert(r.lowest == -1 && r.highest == -1);
    tuner_range_update(&r, 60);
    assert(r.lowest == 60 && r.highest == 60);
    tuner_range_update(&r, 50);
    tuner_range_update(&r, 70);
    assert(r.lowest == 50 && r.highest == 70);
    tuner_range_update(&r, -1);                      // unvoiced: no-op
    assert(r.lowest == 50 && r.highest == 70);
    printf("modes: voice range ok\n");
    return 0;
}
