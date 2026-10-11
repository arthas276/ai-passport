// tuner_pitch.h - full-range pitch detection for TUNER. PURE C.
#pragma once
#include "tuner.h"

// Detect F0 of one frame (n == TUNER_PITCH_FRAME @ TUNER_PITCH_SR).
// Returns hz (0 when unvoiced/uncertain) + confidence.
tuner_pitch_result_t tuner_pitch_detect(const int16_t *pcm, int n);
// Median of the last three raw detections (display/judgement smoothing).
float tuner_pitch_median3(float a, float b, float c);
// Cents deviation between two frequencies.
float tuner_pitch_cents(float f0, float target_hz);
// Nearest MIDI note (1..127) for a frequency.
int16_t tuner_note_from_hz(float hz);
// Cents deviation from the NEAREST semitone; *midi receives that note
// (-1 when hz <= 0).
float tuner_note_cents(float hz, int16_t *midi);
// Note name "E2".."B5" style for any MIDI 21..108 (static storage).
const char *tuner_note_name(int16_t midi);
// Equal-tempered frequency of a MIDI note.
float tuner_note_freq(int16_t midi);
