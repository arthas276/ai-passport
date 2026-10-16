// Host test for the glide renderer: envelope, bounded amplitude, measured
// frequency, glide direction, chunk/one-shot equivalence, silence tail.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "breath_tone.h"

static int crossings(const int16_t *buf, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++) {
        if ((buf[i - 1] < 0 && buf[i] >= 0) ||
            (buf[i - 1] >= 0 && buf[i] < 0)) c++;
    }
    return c;
}

int main(void)
{
    breath_tone_t v;
    static int16_t buf[16000];

    // constant 220 Hz at amp 0.9: silent attack start, audible bounded
    // body, frequency within 3%, release tail ends near silence
    breath_tone_glide(&v, 220.0f, 220.0f, 16000, 0.9f);
    breath_tone_render(&v, buf, 16000);
    assert(buf[0] == 0);
    int nonzero = 0, peak = 0;
    for (int i = 200; i < 15000; i++) {
        if (buf[i] != 0) nonzero++;
        int a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    assert(nonzero > 12000);
    assert(peak > 2000 && peak <= 8000);
    int last = buf[15999] < 0 ? -buf[15999] : buf[15999];
    assert(last < 60);
    float hz = crossings(buf + 640, 16000 - 1280) * 0.5f
               * 16000.0f / (16000 - 1280);
    assert(hz > 213.0f && hz < 227.0f);

    // glide 220 -> 330: last quarter crosses measurably more than first
    breath_tone_glide(&v, 220.0f, 330.0f, 16000, 0.9f);
    breath_tone_render(&v, buf, 16000);
    int c_first = crossings(buf + 640, 3200);
    int c_last = crossings(buf + 16000 - 640 - 3200, 3200);
    assert(c_last > c_first * 12 / 10);   // theory ~1.34x

    // chunked rendering is bit-identical to one-shot rendering
    static int16_t whole[4800], piece[4800];
    breath_tone_glide(&v, 220.0f, 330.0f, 4800, 0.8f);
    breath_tone_render(&v, whole, 4800);
    breath_tone_glide(&v, 220.0f, 330.0f, 4800, 0.8f);
    int off = 0;
    while (off < 4800) {
        int n = (4800 - off) < 512 ? (4800 - off) : 512;
        breath_tone_render(&v, piece + off, (uint16_t)n);
        off += n;
    }
    assert(memcmp(whole, piece, sizeof(whole)) == 0);

    // past total: exact silence
    breath_tone_render(&v, buf, 64);
    for (int i = 0; i < 64; i++) assert(buf[i] == 0);

    // fade_tail: stopping mid-sustain lands at near-silence with no step
    breath_tone_glide(&v, 220.0f, 220.0f, 16000, 0.9f);
    breath_tone_render(&v, buf, 4000);
    breath_tone_fade_tail(&v, 640);
    static int16_t tail[960];
    breath_tone_render(&v, tail, 960);
    int enda = tail[639] < 0 ? -tail[639] : tail[639];
    assert(enda < 60);
    int maxstep = 0;
    for (int i = 1; i < 640; i++) {
        int d = tail[i] - tail[i - 1];
        if (d < 0) d = -d;
        if (d > maxstep) maxstep = d;
    }
    assert(maxstep < 1500);   // a hard cut would step ~6000
    for (int i = 640; i < 960; i++) assert(tail[i] == 0);

    // short 60 ms countdown tick: edge clamp keeps it audible, tail quiet
    breath_tone_glide(&v, 440.0f, 440.0f, 960, 0.35f);
    breath_tone_render(&v, buf, 960);
    peak = 0;
    for (int i = 0; i < 960; i++) {
        int a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    assert(peak > 700);
    last = buf[959] < 0 ? -buf[959] : buf[959];
    assert(last < 60);

    printf("breath tone: glide + envelope + chunks ok\n");
    return 0;
}
