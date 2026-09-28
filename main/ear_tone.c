// ear_tone.c - additive synthesizer for quiz tones and cues. PURE C.
// Per-sample sinf/expf are soft-float on ESP32-C3 (no FPU); budget is fine
// at 22050 Hz mono (see PRD 3.1), params centralized here for device tuning.
#include "ear_game.h"
#include <math.h>

// Output scaling: full-scale sine at amp=1, env=1 peaks at 12000 (~-4.7 dBFS),
// chords sum up to 3 voices -> worst case ~36000 (clipping guard in mix).
#define EAR_TONE_SCALE 12000.0f
// Partial weights: piano-ish spectrum without sounding organ-like.
#define W1 1.0f
#define W2 0.5f
#define W3 0.2f
// Envelope: linear attack then exponential decay to ~5% at note end.
#define EAR_TONE_ATK (8u * (EAR_TONE_SR / 1000u))   // 8 ms attack in samples

// Arm one voice. total = note length in samples at EAR_TONE_SR.
void ear_tone_start(ear_tone_voice_t *v, float hz, float amp, uint32_t total) {
    v->freq = hz;
    v->amp = amp;
    v->phase[0] = v->phase[1] = v->phase[2] = 0.0f;
    v->t = 0;
    v->total = total;
}

// Render n samples. Silence once t >= total (envelope finished).
void ear_tone_render(ear_tone_voice_t *v, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int16_t s = 0;
        if (v->freq > 0.0f && v->t < v->total) {
            float env = (v->t < EAR_TONE_ATK)
                ? (float)v->t / (float)EAR_TONE_ATK
                : expf(-3.0f * (float)(v->t - EAR_TONE_ATK) /
                       (float)(v->total - EAR_TONE_ATK));
            float s1 = sinf(v->phase[0]) * W1;
            float s2 = sinf(v->phase[1]) * W2;
            float s3 = sinf(v->phase[2]) * W3;
            float mixv = s1 + s2 + s3;
            s = (int16_t)(mixv * env * v->amp * EAR_TONE_SCALE);
        }
        out[i] = s;
        if (v->freq > 0.0f) {
            float step0 = (6.2831853f * v->freq) / (float)EAR_TONE_SR;
            v->phase[0] += step0;
            float stepk = step0 * 2.0f;
            v->phase[1] += stepk;
            if (v->phase[1] > 6.2831853f) v->phase[1] -= 6.2831853f;
            v->phase[2] += step0 * 3.0f;
            if (v->phase[2] > 6.2831853f) v->phase[2] -= 6.2831853f;
            if (v->phase[0] > 6.2831853f) v->phase[0] -= 6.2831853f;
            v->t++;
        }
    }
}

// Saturating mix: dst += src (chord voices sum without wraparound).
void ear_tone_mix(int16_t *dst, const int16_t *src, int n) {
    for (int i = 0; i < n; i++) {
        int32_t sum = (int32_t)dst[i] + (int32_t)src[i];
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        dst[i] = (int16_t)sum;
    }
}

// Feedback cues (MIDI notes, 0-terminated), rendered by the audio worker.
const uint8_t EAR_CUE_CORRECT[] = {76, 79, 0};        // E5 G5 (rising, happy)
const uint8_t EAR_CUE_WRONG[] = {64, 60, 0};          // E4 C4 (falling, neutral)
const uint8_t EAR_CUE_EVOLVE[] = {72, 76, 79, 84, 0}; // C5 E5 G5 C6 fanfare
