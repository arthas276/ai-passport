// tuner_settings.h - persisted user settings (mode, volume, A4 reference,
// transpose). PURE C: no IDF/LVGL includes so it compiles on host.
#pragma once
#include <stdint.h>
#include "tuner_modes.h"

#define TUNER_VOL_DEF 80u

// Transpose offsets (semitones, written pitch = sounding + offset) for the
// common transposing instruments. Bb: written C sounds Bb below; Eb alto:
// written C sounds Eb below; F horn: written C sounds F below.
enum {
    TUNER_TP_OFF = 0,
    TUNER_TP_BB  = 2,
    TUNER_TP_EB  = 9,
    TUNER_TP_F   = 7,
};

typedef struct {
    uint8_t  mode;   // tuner_mode_t restored at boot
    uint8_t  vol;    // 0..100 speaker volume
    uint16_t a4;     // A4 reference Hz, 415..466
    uint8_t  tp;     // transpose offset, one of TUNER_TP_*
} tuner_settings_t;

void tuner_settings_default(tuner_settings_t *s);
// Clamp/correct every field from possibly-stale storage into range.
void tuner_settings_sanitize(tuner_settings_t *s);
// Cycle the transpose value OFF -> Bb -> Eb -> F -> OFF.
uint8_t tuner_tp_next(uint8_t tp);
// ASCII short name for host tests ("OFF", "Bb", "Eb", "F").
const char *tuner_tp_short(uint8_t tp);
