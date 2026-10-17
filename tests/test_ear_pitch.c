// Host test for pitch detection with synthetic sine PCMs.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ear_game.h"

#define SR 16000.0f

// Fill n samples with a sine of hz at amplitude amp (int16).
static void fill_sine(int16_t *buf, int n, float hz, float amp)
{
    for (int i = 0; i < n; i++) {
        buf[i] = (int16_t)(amp * sinf(6.2831853f * hz * (float)i / SR));
    }
}

// Harmonic-rich tone (voice-like): fundamental + 2f/3f partials.
static void fill_harmonic(int16_t *buf, int n, float hz, float amp)
{
    for (int i = 0; i < n; i++) {
        float t = 6.2831853f * hz * (float)i / SR;
        float v = sinf(t) + 0.5f * sinf(2.0f * t) + 0.25f * sinf(3.0f * t);
        buf[i] = (int16_t)(amp / 1.75f * v);
    }
}

// Deterministic LCG so jittered-fill tests are reproducible.
static unsigned lcg_state = 20261011u;
static float lcg_jit(float mag)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return ((float)((lcg_state >> 8) & 0xFFFFu) / 65535.0f - 0.5f)
           * 2.0f * mag;
}

// Tone with instantaneous period jitter (+- mag fraction) and harmonics:
// the case where raw autocorrelation argmax drifts onto 2L0/3L0.
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
    static int16_t pcm[EAR_PITCH_FRAME];

    // Musical pitches across the target range: detection within 1.5 %.
    const float freqs[] = { 220.0f, 261.63f, 329.63f, 440.0f, 523.25f };
    for (int f = 0; f < 5; f++) {
        fill_sine(pcm, EAR_PITCH_FRAME, freqs[f], 8000.0f);
        ear_pitch_result_t r = ear_pitch_detect(pcm, EAR_PITCH_FRAME);
        assert(r.hz > freqs[f] * 0.985f && r.hz < freqs[f] * 1.015f);
        assert(r.conf > 0.5f);
    }
    printf("pitch: sine detection ok\n");

    // Octave-error regression (device 2026-09-29: SING readouts pinned near
    // -2400 cents): harmonic and jittered tones must report f0, never f0/2.
    const float hfreqs[] = { 196.0f, 261.63f, 392.0f };
    for (int f = 0; f < 3; f++) {
        fill_harmonic(pcm, EAR_PITCH_FRAME, hfreqs[f], 12000.0f);
        ear_pitch_result_t rh = ear_pitch_detect(pcm, EAR_PITCH_FRAME);
        assert(rh.hz > hfreqs[f] * 0.98f && rh.hz < hfreqs[f] * 1.02f);
        assert(rh.conf > 0.5f);
    }
    lcg_state = 20261011u;
    fill_jittered(pcm, EAR_PITCH_FRAME, 330.0f, 12000.0f);
    ear_pitch_result_t rj = ear_pitch_detect(pcm, EAR_PITCH_FRAME);
    assert(rj.hz > 330.0f * 0.98f && rj.hz < 330.0f * 1.02f);

    // Shimmer (v1.3 divisor refinement): per-cycle amplitude alternates
    // 1.0/0.45, so adjacent cycles differ but every second cycle matches -
    // raw autocorrelation favors 2L0; divisors must recover the fundamental.
    {
        const float shz = 294.0f;
        const float n_c = SR / shz;
        for (int i = 0; i < EAR_PITCH_FRAME; i++) {
            int cyc = (int)((float)i / n_c);
            float amp = (cyc & 1) ? 0.45f : 1.0f;
            float t = 6.2831853f * shz * (float)i / SR;
            float v = sinf(t) + 0.4f * sinf(2.0f * t) + 0.2f * sinf(3.0f * t);
            pcm[i] = (int16_t)(amp * 12000.0f / 1.6f * v);
        }
        ear_pitch_result_t rs = ear_pitch_detect(pcm, EAR_PITCH_FRAME);
        assert(rs.hz > shz * 0.98f && rs.hz < shz * 1.02f);
    }
    printf("pitch: octave safety ok\n");

    // median-3 smoothing helper (PRD 3.2).
    assert(ear_pitch_median3(440.0f, 0.0f, 442.0f) == 440.0f);
    assert(ear_pitch_median3(0.0f, 0.0f, 100.0f) == 0.0f);
    assert(ear_pitch_median3(1.0f, 3.0f, 2.0f) == 2.0f);
    printf("pitch: median3 ok\n");

    // octave-equivalent folding (v1.2 sing grading).
    int8_t oc = 0;
    assert(fabsf(ear_pitch_octave_cents(150.0f, &oc) - 150.0f) < 0.01f
           && oc == 0);
    assert(fabsf(ear_pitch_octave_cents(-1200.0f, &oc)) < 0.01f && oc == -1);
    assert(fabsf(ear_pitch_octave_cents(-1300.0f, &oc) + 100.0f) < 0.01f
           && oc == -1);
    assert(fabsf(ear_pitch_octave_cents(1300.0f, &oc) - 100.0f) < 0.01f
           && oc == 1);
    assert(fabsf(ear_pitch_octave_cents(-2600.0f, &oc) + 200.0f) < 0.01f
           && oc == -2);
    printf("pitch: octave fold ok\n");

    // Silence and tiny noise must be gated out (hz == 0).
    for (int i = 0; i < EAR_PITCH_FRAME; i++) pcm[i] = 0;
    assert(ear_pitch_detect(pcm, EAR_PITCH_FRAME).hz == 0.0f);
    for (int i = 0; i < EAR_PITCH_FRAME; i++) {
        pcm[i] = (int16_t)((i % 7) - 3);   // low-amplitude jitter
    }
    assert(ear_pitch_detect(pcm, EAR_PITCH_FRAME).hz == 0.0f);
    printf("pitch: gates ok\n");

    // Cents math: 0 at unison, ~+100 at one semitone up.
    float c0 = ear_pitch_cents(440.0f, 440.0f);
    assert(c0 > -0.1f && c0 < 0.1f);
    float c1 = ear_pitch_cents(466.16f, 440.0f);
    assert(c1 > 99.0f && c1 < 101.0f);

    // Note helpers.
    assert(ear_note_from_hz(440.0f) == 69);
    assert(ear_note_from_hz(261.63f) == 60);
    assert(ear_note_name(60)[0] == 'C' && ear_note_name(60)[1] == '4');
    assert(ear_note_name(69)[0] == 'A');
    assert(ear_note_name(59)[0] == '?');
    printf("pitch: cents + helpers ok\n");
    return 0;
}
