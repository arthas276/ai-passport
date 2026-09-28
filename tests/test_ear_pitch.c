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
