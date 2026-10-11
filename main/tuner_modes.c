// tuner_modes.c - mode tables and small steppers. PURE C.
#include "tuner_modes.h"

// String tunings, low to high in playing order; ukulele is reentrant
// (4th string G4 above C4). Bass is intentionally absent: its low E1
// (41.2 Hz) sits under the detector's 70 Hz floor and needs a longer frame.
static const int16_t STRINGS[TUNER_MODE_COUNT][6] = {
    [TUNER_MODE_GUITAR]   = { 40, 45, 50, 55, 59, 64 },   // E2 A2 D3 G3 B3 E4
    [TUNER_MODE_UKULELE]  = { 69, 60, 64, 69, 0, 0 },    // G4 C4 E4 A4
    [TUNER_MODE_VIOLIN]   = { 55, 62, 69, 76, 0, 0 },    // G3 D4 A4 E5
};

static const char *MODE_NAMES[TUNER_MODE_COUNT] = {
    "CHROMATIC", "GUITAR", "UKULELE", "VIOLIN", "VOICE",
};

const char *tuner_mode_name(tuner_mode_t mode) {
    if (mode < 0 || mode >= TUNER_MODE_COUNT) return "?";
    return MODE_NAMES[mode];
}

int tuner_mode_string_count(tuner_mode_t mode) {
    switch (mode) {
    case TUNER_MODE_GUITAR: return 6;
    case TUNER_MODE_UKULELE: return 4;
    case TUNER_MODE_VIOLIN: return 4;
    default: return 0;
    }
}

int16_t tuner_mode_string_midi(tuner_mode_t mode, int index) {
    int count = tuner_mode_string_count(mode);
    if (count == 0 || index < 0 || index >= count) return -1;
    return STRINGS[mode][index];
}

tuner_mode_t tuner_mode_next(tuner_mode_t mode) {
    return (tuner_mode_t)(((int)mode + 1) % TUNER_MODE_COUNT);
}

void tuner_strings_reset(tuner_strings_t *s) {
    s->index = 0;
    s->locked = false;
}

void tuner_strings_step(tuner_strings_t *s, tuner_mode_t mode, int delta) {
    int count = tuner_mode_string_count(mode);
    if (count == 0) return;
    s->index = ((s->index + delta) % count + count) % count;
    s->locked = false;
}

void tuner_strings_advance(tuner_strings_t *s, tuner_mode_t mode) {
    int count = tuner_mode_string_count(mode);
    if (count == 0) return;
    s->index = (s->index + 1) % count;
    s->locked = false;
}

void tuner_range_reset(tuner_range_t *r) {
    r->lowest = -1;
    r->highest = -1;
}

void tuner_range_update(tuner_range_t *r, int16_t midi) {
    if (midi < 0) return;
    if (r->lowest < 0 || midi < r->lowest) r->lowest = midi;
    if (r->highest < 0 || midi > r->highest) r->highest = midi;
}
