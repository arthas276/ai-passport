// tuner.h - shared types/constants for TUNER. Pure C header: no IDF/LVGL
// includes so [PURE] modules compile on host.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---- pitch capture (tuner_pitch.c, pure) ----
#define TUNER_PITCH_FRAME 1024          // samples per detection frame
#define TUNER_PITCH_SR 16000u           // capture rate
#define TUNER_PITCH_FMIN 70.0f          // below low guitar E2 (82.4 Hz)
#define TUNER_PITCH_FMAX 1000.0f        // above violin E5 (659 Hz)
#define TUNER_IN_TUNE_CENTS 5.0f        // green-zone width (+/-)
#define TUNER_LOCK_FRAMES 8             // consecutive in-tune frames for LOCK

typedef struct { float hz; float conf; } tuner_pitch_result_t;
