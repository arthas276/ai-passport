// cheer_tone.h - Q15 integer wavetable synth, ported from ear_tone.c. PURE C.
#pragma once

#include <stdint.h>

#define CHEER_TONE_SR 16000

typedef struct {
    uint32_t phase;      // 1.31 phase accumulator
    uint32_t step;       // phase increment per sample
    int32_t gain;        // Q15 target level * amp
    int32_t env;         // Q15 envelope (attack/decay/release)
    int32_t decay;       // Q15 per-sample decay factor
    int32_t atk_step;
    int32_t rel_step;
    uint32_t t;          // samples rendered
    uint32_t total;      // note length in samples
} cheer_tone_voice_t;

void cheer_tone_start(cheer_tone_voice_t *v, float hz, float amp,
                      uint32_t total_samples);
void cheer_tone_render(cheer_tone_voice_t *v, int16_t *out, int n);
void cheer_tone_mix(int16_t *dst, const int16_t *src, int n);
float cheer_tone_freq(uint8_t midi);
