// Host test for the tuner pitch detector: range sweep, octave safety,
// median-3, note naming, gates.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tuner.h"
#include "tuner_pitch.h"

#define SR 16000.0f

static void fill_sine(int16_t *buf, int n, float hz, float amp)
{
    for (int i = 0; i < n; i++) {
        buf[i] = (int16_t)(amp * sinf(6.2831853f * hz * (float)i / SR));
    }
}

// Harmonic-rich tone (instrument-like): fundamental + 2f/3f partials.
static void fill_harmonic(int16_t *buf, int n, float hz, float amp)
{
    for (int i = 0; i < n; i++) {
        float t = 6.2831853f * hz * (float)i / SR;
        float v = sinf(t) + 0.5f * sinf(2.0f * t) + 0.25f * sinf(3.0f * t);
        buf[i] = (int16_t)(amp / 1.75f * v);
    }
}

static unsigned lcg_state = 20261011u;
static float lcg_jit(float mag)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return ((float)((lcg_state >> 8) & 0xFFFFu) / 65535.0f - 0.5f)
           * 2.0f * mag;
}

static void fill_jittered(int16_t *buf, int n, float hz, float amp)
{
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        phase += 6.2831853f * hz * (1.0f + lcg_jit(0.015f)) / SR;
        float v = sinf(phase) + 0.5f * sinf(2.0f * phase)
                + 0.25f * sinf(3.0f * phase);
        buf[i] = (int16_t)(amp / 1.75f * v);
    }
}

int main(void)
{
    static int16_t pcm[TUNER_PITCH_FRAME];

    // Chromatic range sweep: low guitar E2 (82.4 Hz) up to B5 (987.8 Hz),
    // detection within 1.5 %.
    const float freqs[] = { 82.41f, 110.0f, 146.83f, 196.0f, 261.63f,
                            329.63f, 440.0f, 659.26f, 987.77f };
    for (int f = 0; f < 9; f++) {
        fill_sine(pcm, TUNER_PITCH_FRAME, freqs[f], 8000.0f);
        tuner_pitch_result_t r = tuner_pitch_detect(pcm, TUNER_PITCH_FRAME);
        assert(r.hz > freqs[f] * 0.985f && r.hz < freqs[f] * 1.015f);
        assert(r.conf > 0.5f);
    }
    printf("tuner pitch: range sweep ok\n");

    // Octave safety: harmonic and jittered tones report f0, never f0/2.
    const float hfreqs[] = { 98.0f, 220.0f, 466.16f };
    for (int f = 0; f < 3; f++) {
        fill_harmonic(pcm, TUNER_PITCH_FRAME, hfreqs[f], 12000.0f);
        tuner_pitch_result_t rh = tuner_pitch_detect(pcm, TUNER_PITCH_FRAME);
        assert(rh.hz > hfreqs[f] * 0.98f && rh.hz < hfreqs[f] * 1.02f);
        assert(rh.conf > 0.5f);
    }
    lcg_state = 20261011u;
    fill_jittered(pcm, TUNER_PITCH_FRAME, 330.0f, 12000.0f);
    tuner_pitch_result_t rj = tuner_pitch_detect(pcm, TUNER_PITCH_FRAME);
    assert(rj.hz > 330.0f * 0.98f && rj.hz < 330.0f * 1.02f);
    printf("tuner pitch: octave safety ok\n");

    // Silence and low noise must be gated out.
    for (int i = 0; i < TUNER_PITCH_FRAME; i++) pcm[i] = 0;
    assert(tuner_pitch_detect(pcm, TUNER_PITCH_FRAME).hz == 0.0f);
    for (int i = 0; i < TUNER_PITCH_FRAME; i++) {
        pcm[i] = (int16_t)((i % 7) - 3);
    }
    assert(tuner_pitch_detect(pcm, TUNER_PITCH_FRAME).hz == 0.0f);
    printf("tuner pitch: gates ok\n");

    // Note mapping: full range, sharps, clamping, cents to nearest.
    assert(tuner_note_from_hz(440.0f) == 69);
    assert(tuner_note_from_hz(82.41f) == 40);
    assert(tuner_note_from_hz(261.63f) == 60);
    assert(strcmp(tuner_note_name(69), "A4") == 0);
    assert(strcmp(tuner_note_name(40), "E2") == 0);
    assert(strcmp(tuner_note_name(61), "C#4") == 0);
    assert(strcmp(tuner_note_name(21), "A0") == 0);
    assert(strcmp(tuner_note_name(108), "C8") == 0);
    assert(strcmp(tuner_note_name(10), "--") == 0);

    int16_t midi = -1;
    float c0 = tuner_note_cents(440.0f, &midi);
    assert(midi == 69 && fabsf(c0) < 0.5f);
    float c450 = tuner_note_cents(450.0f, &midi);
    assert(midi == 69 && fabsf(c450 - 38.6f) < 1.0f);
    float cb = tuner_note_cents(466.16f, &midi);
    assert(midi == 70 && fabsf(cb) < 1.0f);
    assert(tuner_note_cents(0.0f, &midi) == 0.0f && midi == -1);
    printf("tuner pitch: notes + cents ok\n");

    // median-3 smoothing.
    assert(tuner_pitch_median3(440.0f, 0.0f, 442.0f) == 440.0f);
    assert(tuner_pitch_median3(0.0f, 0.0f, 100.0f) == 0.0f);
    assert(tuner_pitch_median3(1.0f, 3.0f, 2.0f) == 2.0f);
    printf("tuner pitch: median3 ok\n");
    return 0;
}
