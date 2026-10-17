// ear_tone.c - integer wavetable synth for quiz tones and cues. PURE C.
// Device bring-up 2026-09-29: the original float render (3x sinf + expf per
// sample, all soft-float on the FPU-less ESP32-C3) rendered a 450 ms note in
// 677 ms -> constant I2S DMA underruns -> the reported "bad timbre". Integer
// table + multiply-shift restores >10x real-time headroom. Params centralized
// here for device tuning.
#include "ear_game.h"
#include <math.h>     // one-time sinf/expf at note start / LUT build only

// Output level: peak = LUT peak (Q15) x env x gain, shifted to Q15 twice.
// Sine at amp=1 peaks at 8500 (~-11.7 dBFS) — keeps the small speaker in its
// linear range; chords (3 voices at amp 0.55) sum to ~12k peak, no clipping.
#define EAR_TONE_LEVEL 8500
// Partial weights Q15: sine-dominant; higher partials tint fullness without
// turning harsh on the tiny speaker (v1.3: 0.32/0.12 -> 0.40/0.18 per user
// request for a fuller quiz tone).
#define W1_Q15 32767
#define W2_Q15 13107    // 0.40
#define W3_Q15 5898     // 0.18

// Envelope: linear attack, gentle exponential decay to ~64% at note end (a
// sustained tone is a steadier pitch reference than a dying one; v1.3 slows
// the decay from ~45% for a fuller feel), linear release over the last 8 ms
// so notes don't end in a click.
#define EAR_TONE_ATK (8u * (EAR_TONE_SR / 1000u))   // 8 ms attack in samples
#define EAR_TONE_REL (8u * (EAR_TONE_SR / 1000u))   // 8 ms release in samples

// 1024-entry sine table in Q15, built once at first use (1024 sinf calls at
// boot, ~10 ms) — no per-sample float math on the hot path.
#define EAR_TONE_LUT_BITS 10
#define EAR_TONE_LUT_SIZE (1u << EAR_TONE_LUT_BITS)
static int16_t s_sin_lut[EAR_TONE_LUT_SIZE];
static bool s_lut_ready;

static void lut_init(void) {
    for (uint32_t i = 0; i < EAR_TONE_LUT_SIZE; i++) {
        s_sin_lut[i] = (int16_t)(sinf((float)i * 6.2831853f /
                                      (float)EAR_TONE_LUT_SIZE) * 32767.0f);
    }
    s_lut_ready = true;
}

// Q15 sine via linear interpolation; phase is a 1.31 accumulator.
static inline int32_t lut_read(uint32_t phase) {
    uint32_t idx = phase >> (32 - EAR_TONE_LUT_BITS);
    int32_t a = s_sin_lut[idx];
    int32_t b = s_sin_lut[(idx + 1) & (EAR_TONE_LUT_SIZE - 1)];
    int32_t frac = (int32_t)((phase >> 7) & 0x7FFF);   // next 15 bits, Q15
    return a + (((b - a) * frac) >> 15);
}

// Arm one voice. total = note length in samples at EAR_TONE_SR.
void ear_tone_start(ear_tone_voice_t *v, float hz, float amp, uint32_t total) {
    if (!s_lut_ready) lut_init();
    v->phase = 0;
    v->step = (uint32_t)((uint64_t)(hz * 4294967296.0f) / EAR_TONE_SR);
    v->gain = (int32_t)(amp * (float)EAR_TONE_LEVEL);
    v->env = 0;
    v->decay = (int32_t)(expf(-0.45f / (float)(total > EAR_TONE_ATK
                                             ? total - EAR_TONE_ATK : 1)) *
                         32767.0f);
    v->atk_step = 32767 / (int32_t)EAR_TONE_ATK;
    v->rel_step = 32767 / (int32_t)EAR_TONE_REL;
    v->t = 0;
    v->total = total;
}

// Render n samples. Silence once t >= total (envelope finished).
void ear_tone_render(ear_tone_voice_t *v, int16_t *out, int n) {
    for (int s = 0; s < n; s++) {
        int16_t sample = 0;
        if (v->step > 0 && v->t < v->total) {
            // envelope: linear attack, exp decay, linear release override
            if (v->t < EAR_TONE_ATK) {
                v->env = (int32_t)v->t * v->atk_step;
            } else {
                v->env = (v->env * v->decay) >> 15;
            }
            uint32_t rt = v->total - v->t;
            if (rt <= EAR_TONE_REL) {
                v->env = (int32_t)rt * v->rel_step;
            }
            // 3 partials from one accumulator: 2f and 3f are free wraps
            int32_t mix = (W1_Q15 * lut_read(v->phase)
                           + W2_Q15 * lut_read(v->phase * 2u)
                           + W3_Q15 * lut_read(v->phase * 3u)) >> 15;
            int32_t shaped = ((mix * v->env) >> 15) * v->gain >> 15;
            sample = (int16_t)shaped;
            v->phase += v->step;
            v->t++;
        }
        out[s] = sample;
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
