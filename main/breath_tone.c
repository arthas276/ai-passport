// breath_tone.c - integer wavetable glide synth for BREATHE. PURE C.
//
// Wavetable approach ported from feature/ear-buddy ear_tone.c (device
// proven 2026-09-29: the float render was 0.66x real time on the FPU-less
// ESP32-C3 and underran I2S; the integer LUT restores >10x headroom).
// Changes vs the original: one voice instead of three, a per-sample
// phase-step interpolation for linear-in-Hz glides (the breathing cue),
// and a fixed 40 ms attack/release trapezoid instead of a decaying
// envelope - a guide tone must sound steady, not dying.
#include "breath_tone.h"
#include <math.h>     // one-time sinf at LUT build; float only at arm time

// Output peak at amp=1: LUT peak (Q15, ~1.15 with the 2nd partial) x
// LEVEL. 6000 keeps the small speaker in its linear range and leaves the
// user volume (default 60) a calm starting point.
#define BREATH_TONE_LEVEL 6000
// Partial weights Q15: near-sine; the slight 2nd partial adds warmth on
// the tiny speaker without turning harsh.
#define W1_Q15 32767
#define W2_Q15 4915     // 0.15

// 40 ms attack/release at 16 kHz; clamped to a quarter of short arms so
// very short cues (countdown ticks) still reach full amplitude.
#define ENV_EDGE_SAMPLES 640u

// 1024-entry sine table in Q15, built once at first use (~10 ms at boot,
// same budget as ear_tone) - no per-sample float math on the hot path.
#define LUT_BITS 10
#define LUT_SIZE (1u << LUT_BITS)
static int16_t s_sin_lut[LUT_SIZE];
static bool s_lut_ready;

static void lut_init(void) {
    for (uint32_t i = 0; i < LUT_SIZE; i++) {
        s_sin_lut[i] = (int16_t)(sinf((float)i * 6.2831853f /
                                      (float)LUT_SIZE) * 32767.0f);
    }
    s_lut_ready = true;
}

// Q15 sine via linear interpolation; phase is a 1.31 accumulator.
static inline int32_t lut_read(uint32_t phase) {
    uint32_t idx = phase >> (32 - LUT_BITS);
    int32_t a = s_sin_lut[idx];
    int32_t b = s_sin_lut[(idx + 1) & (LUT_SIZE - 1)];
    int32_t frac = (int32_t)((phase >> 7) & 0x7FFF);   // next 15 bits, Q15
    return a + (((b - a) * frac) >> 15);
}

void breath_tone_glide(breath_tone_t *v, float hz_start, float hz_end,
                       uint32_t total_samples, float amp) {
    if (!s_lut_ready) lut_init();
    uint32_t step0 = (uint32_t)((float)((uint64_t)1 << 32) * hz_start /
                                (float)BREATH_TONE_SR);
    uint32_t step1 = (uint32_t)((float)((uint64_t)1 << 32) * hz_end /
                                (float)BREATH_TONE_SR);
    v->phase = 0;
    v->step = step0;
    v->step_delta = total_samples
        ? (int32_t)(((int64_t)step1 - (int64_t)step0) / (int64_t)total_samples)
        : 0;
    v->gain = (int32_t)(amp * (float)BREATH_TONE_LEVEL);
    v->t = 0;
    v->total = total_samples;
}

void breath_tone_fade_tail(breath_tone_t *v, uint32_t fade_samples) {
    if (v->t >= v->total) return;          // already silent
    uint32_t end = v->t + fade_samples;
    if (end < v->total) v->total = end;    // only shorten, never extend
}

void breath_tone_render(breath_tone_t *v, int16_t *out, uint16_t n) {
    const uint32_t edge =
        ENV_EDGE_SAMPLES < (v->total / 4u) ? ENV_EDGE_SAMPLES : (v->total / 4u);

    for (uint16_t i = 0; i < n; i++) {
        if (v->t >= v->total) {
            out[i] = 0;               // finished: exact silence tail
            continue;
        }
        // Trapezoid envelope: attack -> sustain -> release, all linear.
        int32_t env;
        if (v->t < edge) {
            env = (int32_t)((v->t * 32768u) / edge);
        } else if (v->total - v->t <= edge) {
            env = (int32_t)(((v->total - v->t) * 32768u) / edge);
        } else {
            env = 32768;
        }
        // Fundamental + 2nd partial share the phase accumulator (the
        // octave partial reads the same phase doubled).
        int32_t wave = (lut_read(v->phase) * W1_Q15 +
                        lut_read(v->phase << 1) * W2_Q15) >> 15;   // Q15
        out[i] = (int16_t)(((int64_t)wave * v->gain * env) >> 30);
        v->phase += v->step;
        v->step = (uint32_t)((int64_t)v->step + v->step_delta);
        v->t++;
    }
}
