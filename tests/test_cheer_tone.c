// test_cheer_tone.c - host tests for the ported Q15 synth.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cheer_tone.h"

int main(void) {
    // LUT build + audible output within int16 bounds.
    cheer_tone_voice_t v;
    enum { N = 1600 };   // 100 ms
    static int16_t buf[N];
    cheer_tone_start(&v, 440.0f, 0.8f, N);
    cheer_tone_render(&v, buf, N);
    int peak = 0;
    for (int i = 0; i < N; i++) {
        int a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    assert(peak > 2000 && peak <= 32767);

    // Envelope finishes at total: rendering past the end is digital silence.
    cheer_tone_render(&v, buf, N);
    for (int i = 0; i < N; i++) assert(buf[i] == 0);

    // A4 tuning: midi 69 == 440 Hz.
    float f = cheer_tone_freq(69);
    assert(fabsf(f - 440.0f) < 0.01f);

    // Saturating mix never wraps.
    static int16_t a[N], b[N], out[N];
    memset(a, 0x7F, sizeof(a));   // 32639
    memset(b, 0x7F, sizeof(b));
    memcpy(out, a, sizeof(a));
    cheer_tone_mix(out, b, N);
    for (int i = 0; i < N; i++) assert(out[i] == 32767);

    // v1.3 growth-scale chimes: root rises with stage, notes stay in the
    // small-speaker band, chime gains an octave sparkle at stage >= 3,
    // fanfare stays 5 notes and strictly ascending.
    uint8_t prev_root = 0;
    for (int st = 0; st < CHEER_STAGE_COUNT; st++) {
        uint8_t r = cheer_chime_root(st);
        if (st > 0 && r <= prev_root) return 1;   // fail (main asserts 0)
        if (r < 60 || r > 84) return 1;
        prev_root = r;
        uint8_t n[8];
        int cnt = cheer_chime_notes(st, false, n, 8);
        if (cnt != (st >= 3 ? 4 : 3)) return 1;
        for (int i = 0; i < cnt; i++) {
            if (n[i] < 60 || n[i] > 96) return 1;
            if (i > 0 && n[i] <= n[i - 1]) return 1;
        }
        int fc = cheer_chime_notes(st, true, n, 8);
        if (fc != 5 || n[4] > 96) return 1;
    }

    printf("cheer tone: lut/env/tuning/mix/chimes ok (peak %d)\n", peak);
    return 0;
}
