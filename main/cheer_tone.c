// cheer_tone.c - Q15 integer wavetable synth. PURE C.
// Ported from feature/ear-buddy ear_tone.c (device-proven 2026-09-29): the
// ESP32-C3 has no FPU, so per-sample float math starves I2S DMA; a 1024-entry
// sine LUT with multiply-shift partial mixing restores >10x real-time margin.
// Float math appears only at note start / one-time LUT build.
#include "cheer_tone.h"

#include <math.h>

#define CHEER_TONE_LEVEL 8500   // ~-11.7 dBFS peak: small-speaker linear range
#define W1_Q15 32767            // partial weights Q15: fundamental dominant
#define W2_Q15 13107            // 0.40
#define W3_Q15 5898              // 0.18

#define ATK_SAMPLES (8u * (CHEER_TONE_SR / 1000u))
#define REL_SAMPLES (8u * (CHEER_TONE_SR / 1000u))

#define LUT_BITS 10
#define LUT_SIZE (1u << LUT_BITS)
static int16_t s_sin_lut[LUT_SIZE];
static int s_lut_ready;

static void lut_init(void) {
    for (uint32_t i = 0; i < LUT_SIZE; i++) {
        s_sin_lut[i] = (int16_t)(sinf((float)i * 6.2831853f /
                                      (float)LUT_SIZE) * 32767.0f);
    }
    s_lut_ready = 1;
}

static inline int32_t lut_read(uint32_t phase) {
    uint32_t idx = phase >> (32 - LUT_BITS);
    int32_t a = s_sin_lut[idx];
    int32_t b = s_sin_lut[(idx + 1) & (LUT_SIZE - 1)];
    int32_t frac = (int32_t)((phase >> 7) & 0x7FFF);
    return a + (((b - a) * frac) >> 15);
}

void cheer_tone_start(cheer_tone_voice_t *v, float hz, float amp,
                      uint32_t total) {
    if (!s_lut_ready) lut_init();
    v->phase = 0;
    v->step = (uint32_t)((uint64_t)(hz * 4294967296.0f) / CHEER_TONE_SR);
    v->gain = (int32_t)(amp * (float)CHEER_TONE_LEVEL);
    v->env = 0;
    v->decay = (int32_t)(expf(-0.45f / (float)(total > ATK_SAMPLES
                                               ? total - ATK_SAMPLES : 1u)) *
                         32767.0f);
    v->atk_step = 32767 / (int32_t)ATK_SAMPLES;
    v->rel_step = 32767 / (int32_t)REL_SAMPLES;
    v->t = 0;
    v->total = total;
}

void cheer_tone_render(cheer_tone_voice_t *v, int16_t *out, int n) {
    for (int s = 0; s < n; s++) {
        int16_t sample = 0;
        if (v->step > 0 && v->t < v->total) {
            if (v->t < ATK_SAMPLES) {
                v->env = (int32_t)v->t * v->atk_step;
            } else {
                v->env = (v->env * v->decay) >> 15;
            }
            uint32_t rt = v->total - v->t;
            if (rt <= REL_SAMPLES) {
                v->env = (int32_t)rt * v->rel_step;
            }
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

void cheer_tone_mix(int16_t *dst, const int16_t *src, int n) {
    for (int i = 0; i < n; i++) {
        int32_t sum = (int32_t)dst[i] + (int32_t)src[i];
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        dst[i] = (int16_t)sum;
    }
}

float cheer_tone_freq(uint8_t midi) {
    return 440.0f * powf(2.0f, ((int)midi - 69) / 12.0f);
}
