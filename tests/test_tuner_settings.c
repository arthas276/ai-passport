// Host tests for tuner_settings: defaults, sanitisation, transpose cycling.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tuner.h"
#include "tuner_modes.h"
#include "tuner_settings.h"

int main(void)
{
    // Defaults: chromatic, volume 80, A4 440, transpose off.
    tuner_settings_t s;
    tuner_settings_default(&s);
    assert(s.mode == TUNER_MODE_CHROMATIC);
    assert(s.vol == TUNER_VOL_DEF);
    assert(s.a4 == TUNER_A4_DEF);
    assert(s.tp == TUNER_TP_OFF);
    printf("settings: defaults ok\n");

    // Sanitise: out-of-range fields fall back to defaults, never wrap.
    s.mode = TUNER_MODE_COUNT;
    s.vol = 101;
    s.a4 = 400;
    s.tp = 5;
    tuner_settings_sanitize(&s);
    assert(s.mode == TUNER_MODE_CHROMATIC && s.vol == TUNER_VOL_DEF);
    assert(s.a4 == TUNER_A4_DEF && s.tp == TUNER_TP_OFF);
    s.a4 = 500;
    tuner_settings_sanitize(&s);
    assert(s.a4 == TUNER_A4_DEF);
    // In-range values survive untouched.
    s.mode = TUNER_MODE_GUITAR;
    s.vol = 30;
    s.a4 = 442;
    s.tp = TUNER_TP_EB;
    tuner_settings_sanitize(&s);
    assert(s.mode == TUNER_MODE_GUITAR && s.vol == 30 && s.a4 == 442);
    assert(s.tp == TUNER_TP_EB);
    printf("settings: sanitize ok\n");

    // Transpose cycle OFF -> Bb -> Eb -> F -> OFF, ASCII short names.
    uint8_t tp = TUNER_TP_OFF;
    tp = tuner_tp_next(tp);
    assert(tp == TUNER_TP_BB);
    tp = tuner_tp_next(tp);
    assert(tp == TUNER_TP_EB);
    tp = tuner_tp_next(tp);
    assert(tp == TUNER_TP_F);
    tp = tuner_tp_next(tp);
    assert(tp == TUNER_TP_OFF);
    // Unknown value cycles back to OFF.
    assert(tuner_tp_next(99) == TUNER_TP_OFF);
    assert(strcmp(tuner_tp_short(TUNER_TP_OFF), "OFF") == 0);
    assert(strcmp(tuner_tp_short(TUNER_TP_BB), "Bb") == 0);
    assert(strcmp(tuner_tp_short(TUNER_TP_EB), "Eb") == 0);
    assert(strcmp(tuner_tp_short(TUNER_TP_F), "F") == 0);
    printf("settings: transpose cycle ok\n");
    return 0;
}
