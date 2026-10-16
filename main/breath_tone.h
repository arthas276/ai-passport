// breath_tone.h - integer wavetable glide renderer for BREATHE. PURE C.
#pragma once
#include "breath.h"

// One glide voice. The frequency glide is linear in Hz: the per-sample
// phase increment interpolates from hz_start to hz_end across the arm.
// Integer wavetable (the FPU-less ESP32-C3 ran a float render at 0.66x
// real time on device in the ear-buddy bring-up; this approach keeps
// >10x headroom - lineage: feature/ear-buddy ear_tone.c, device-proven).
typedef struct {
    uint32_t phase;       // 1.31 fixed-point fundamental phase accumulator
    uint32_t step;        // current per-sample phase increment
    int32_t  step_delta;  // per-sample increment change (the glide itself)
    int32_t  gain;        // output scale (amp x BREATH_TONE_LEVEL)
    uint32_t t;           // sample index within this arm
    uint32_t total;       // arm length in samples
} breath_tone_t;

// Arm a glide from hz_start to hz_end over total_samples at amplitude
// 0..1. A constant tone is hz_start == hz_end (used for countdown ticks).
// Envelope: 40 ms linear attack, flat sustain, 40 ms linear release - so a
// finished glide already ends at silence (holds need no extra fade).
void breath_tone_glide(breath_tone_t *v, float hz_start, float hz_end,
                       uint32_t total_samples, float amp);

// Cut a running arm short with a linear release fade of fade_samples
// (used when the user ends a session mid-glide: cutting a sustained tone
// at full amplitude is an audible pop). Moves `total` to t+fade_samples
// so the existing release slope takes over; never extends a finished arm.
void breath_tone_fade_tail(breath_tone_t *v, uint32_t fade_samples);

// Render the next n samples into out. Exactly zero after total; rendering
// in chunks is bit-identical to rendering in one call (state carries).
void breath_tone_render(breath_tone_t *v, int16_t *out, uint16_t n);
