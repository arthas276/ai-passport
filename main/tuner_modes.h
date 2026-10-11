// tuner_modes.h - instrument/string modes and voice-range tracking. PURE C.
#pragma once
#include "tuner.h"
#include <stdint.h>
#include <stdbool.h>

// Tuner modes. Order is the OK-long cycle order.
typedef enum {
    TUNER_MODE_CHROMATIC = 0,   // any note, nearest-note readout
    TUNER_MODE_GUITAR,          // E2 A2 D3 G3 B3 E4, auto-advance on lock
    TUNER_MODE_UKULELE,         // G4 C4 E4 A4 (reentrant)
    TUNER_MODE_VIOLIN,          // G3 D4 A4 E5
    TUNER_MODE_VOICE,           // humming: nearest note + session range
    TUNER_MODE_COUNT,
} tuner_mode_t;

// Per-mode metadata: display name and ordered string targets (MIDI; strings
// count is 0 for free modes). Ukulele G is the reentrant high-G (G4).
const char *tuner_mode_name(tuner_mode_t mode);
int tuner_mode_string_count(tuner_mode_t mode);          // 0 = free mode
int16_t tuner_mode_string_midi(tuner_mode_t mode, int index);  // index in [0, count)

// Step a mode index with wraparound (for OK-long cycling).
tuner_mode_t tuner_mode_next(tuner_mode_t mode);

// String stepper state for instrument modes (UI-owned).
typedef struct {
    int index;      // current string index within the mode
    bool locked;    // current string has locked in-tune once
} tuner_strings_t;

void tuner_strings_reset(tuner_strings_t *s);            // to string 0
void tuner_strings_step(tuner_strings_t *s, tuner_mode_t mode, int delta);
void tuner_strings_advance(tuner_strings_t *s, tuner_mode_t mode);  // after lock

// Voice-range tracker: remembers the lowest/highest sustained note of the
// session. update() feeds every voiced frame's nearest MIDI note.
typedef struct {
    int16_t lowest;    // -1 until the first voiced frame
    int16_t highest;
} tuner_range_t;

void tuner_range_reset(tuner_range_t *r);
void tuner_range_update(tuner_range_t *r, int16_t midi);
