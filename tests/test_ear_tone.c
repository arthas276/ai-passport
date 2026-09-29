// Host test for the additive synth: envelope, silence tail, mixing.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ear_game.h"

int main(void)
{
    ear_tone_voice_t v;
    int16_t buf[1600];   // 100 ms at 16000 Hz
    for (int i = 0; i < 1600; i++) buf[i] = 0;

    // A 440 Hz note renders non-silent audio: silent at sample 0 (attack
    // ramp starts at 0), non-zero in the body, and exactly 0 after total.
    ear_tone_start(&v, 440.0f, 0.8f, 1300);
    ear_tone_render(&v, buf, 1600);
    assert(buf[0] == 0);
    int nonzero = 0;
    for (int i = 100; i < 650; i++) {
        if (buf[i] != 0) nonzero++;
    }
    assert(nonzero > 450);
    for (int i = 1300; i < 1600; i++) assert(buf[i] == 0);

    // Saturating mix: 30000+30000 clamps to 32767.
    int16_t a[4] = {30000, 30000, -30000, -30000};
    int16_t b[4] = {30000, -30000, 30000, -30000};
    int16_t d[4] = {0, 0, 0, 0};
    ear_tone_mix(d, a, 4);
    ear_tone_mix(d, b, 4);
    assert(d[0] == 32767 && d[1] == 0 && d[2] == 0 && d[3] == -32768);
    printf("tone: envelope + mix ok\n");
    return 0;
}

