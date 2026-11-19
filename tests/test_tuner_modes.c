// Host tests for tuner modes: tables, cycling, string stepping, range.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tuner.h"
#include "tuner_modes.h"
#include "tuner_settings.h"
#include "tuner_strings.h"

int main(void)
{
    // Mode set and cycle order (names are the generated Chinese labels).
    assert(TUNER_MODE_COUNT == 5);
    assert(strcmp(tuner_mode_name(TUNER_MODE_CHROMATIC),
                  STR_MODE_CHROMATIC) == 0);
    assert(strcmp(tuner_mode_name(TUNER_MODE_VOICE), STR_MODE_VOICE) == 0);
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

    // String progress mask: advance marks the string left behind, manual
    // stepping keeps it, reset clears, all_done needs every bit.
    tuner_strings_reset(&s);
    assert(s.done_mask == 0);
    assert(!tuner_strings_all_done(&s, TUNER_MODE_GUITAR));
    tuner_strings_advance(&s, TUNER_MODE_GUITAR);
    assert(s.done_mask == 0x01 && s.index == 1);
    tuner_strings_step(&s, TUNER_MODE_GUITAR, 1);
    assert(s.done_mask == 0x01);                  // step keeps done bits
    tuner_strings_reset(&s);
    for (int i = 0; i < 6; i++) tuner_strings_advance(&s, TUNER_MODE_GUITAR);
    assert(s.done_mask == 0x3F);
    assert(tuner_strings_all_done(&s, TUNER_MODE_GUITAR));
    tuner_strings_step(&s, TUNER_MODE_GUITAR, 1);
    assert(tuner_strings_all_done(&s, TUNER_MODE_GUITAR));
    tuner_strings_reset(&s);
    for (int i = 0; i < 3; i++) tuner_strings_advance(&s, TUNER_MODE_UKULELE);
    assert(s.done_mask == 0x07);
    assert(!tuner_strings_all_done(&s, TUNER_MODE_UKULELE));
    tuner_strings_advance(&s, TUNER_MODE_UKULELE);
    assert(tuner_strings_all_done(&s, TUNER_MODE_UKULELE));
    tuner_strings_reset(&s);
    assert(!tuner_strings_all_done(&s, TUNER_MODE_CHROMATIC));
    printf("modes: string progress ok\n");

    // Transpose display: written = sounding + offset, clamped to 21..108.
    assert(tuner_transpose_midi(60, TUNER_TP_BB) == 62);
    assert(tuner_transpose_midi(60, TUNER_TP_EB) == 69);
    assert(tuner_transpose_midi(60, TUNER_TP_F) == 67);
    assert(tuner_transpose_midi(100, TUNER_TP_EB) == 108);
    assert(tuner_transpose_midi(15, TUNER_TP_OFF) == 21);   // clamped low
    assert(tuner_transpose_midi(-1, TUNER_TP_BB) == -1);

    // Needle EMA: halves the gap at 0.5, approaches the target monotonic.
    assert(tuner_ema(0.0f, 10.0f, 0.5f) == 5.0f);
    float v = 0.0f;
    for (int i = 0; i < 8; i++) v = tuner_ema(v, 10.0f, 0.35f);
    assert(v > 5.0f && v < 10.0f);
    printf("modes: transpose + ema ok\n");

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
