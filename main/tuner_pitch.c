// tuner_pitch.c - integer-domain autocorrelation pitch detection. PURE C.
// Ported from feature/ear-buddy main/ear_pitch.c (same repository; device
// proven 2026-09-29, octave-safety hardened 2026-10-11) with the range and
// note naming extended for a chromatic tuner: any note E2..B5.
// ESP32-C3 has no FPU: the correlation scan is int64 math (M extension),
// float is used only for the final hz/cents conversion.
#include "tuner_pitch.h"
#include <math.h>
#include <stdio.h>

// Voice/noise gates: RMS^2 * FRAME energy floor and minimum autocorrelation
// peak quality. A sustained instrument note at near-field mic is comfortably
// above these; set conservatively low.
#define TUNER_RMS_GATE   300.0f
#define TUNER_MIN_CONF   0.30f
// Octave safety: a periodic tone peaks at its true period L0 AND at 2L0,
// 3L0...; the chosen lag is the earliest local peak whose value is at least
// this ratio of the global peak, i.e. the fundamental, not a subharmonic.
#define TUNER_PEAK_RATIO 0.80f
// Divisor refinement: shimmer/jitter can smear L0 off a local maximum so the
// earliest-peak scan still lands on 2L0/3L0. Whatever lag wins, its integer
// divisors L/2..L/4 are re-tested at this slightly lower ratio.
#define TUNER_DIV_RATIO  0.72f

// Autocorrelation at one lag, mean removed. Integer accumulation.
static int64_t lag_corr(const int16_t *x, int n, int32_t m, int L) {
    int64_t acc = 0;
    for (int i = 0; i + L < n; i++) {
        int32_t a = x[i] - m;
        acc += (int64_t)a * (int64_t)(x[i + L] - m);
    }
    return acc;
}

// Detect F0 of one frame: mean -> energy gate -> octave-safe earliest-peak
// + divisor lag scan -> parabolic interpolation.
tuner_pitch_result_t tuner_pitch_detect(const int16_t *pcm, int n) {
    tuner_pitch_result_t r = {0.0f, 0.0f};
    if (!pcm || n < TUNER_PITCH_FRAME) return r;
    // 1) Mean + energy gate (e0 == n * rms^2, so the gate is an RMS test).
    int32_t m = 0;
    double msum = 0.0;
    for (int i = 0; i < n; i++) msum += pcm[i];
    m = (int32_t)(msum / n + (msum >= 0 ? 0.5 : -0.5));
    int64_t e0 = 0;
    for (int i = 0; i < n; i++) {
        int32_t d = pcm[i] - m;
        e0 += (int64_t)d * d;
    }
    if (e0 < (int64_t)(TUNER_RMS_GATE * TUNER_RMS_GATE) * n) return r;
    // 2) Pass A: global peak value. Pass B: earliest local peak >= ratio.
    const int lag_min = (int)(TUNER_PITCH_SR / TUNER_PITCH_FMAX);
    const int lag_max = (int)(TUNER_PITCH_SR / TUNER_PITCH_FMIN);
    if (lag_max >= n) return r;
    int64_t peak_val = 0;
    for (int L = lag_min; L <= lag_max; L++) {
        int64_t a = lag_corr(pcm, n, m, L);
        if (a > peak_val) peak_val = a;
    }
    if (peak_val <= 0) return r;
    const int64_t peak_thr = (int64_t)(TUNER_PEAK_RATIO * (double)peak_val);
    int best = -1;
    int64_t best_acc = 0;
    // No left-edge special case: at FMAX the raw correlation is still in its
    // small-angle near-max zone for low tones, so lag_min itself must not be
    // treated as a peak. The first TRUE local maximum of a low tone is its
    // fundamental period (corr falls to the half-period valley, then rises).
    int64_t prev = lag_corr(pcm, n, m, lag_min);
    int64_t cur = lag_corr(pcm, n, m, lag_min + 1);
    for (int L = lag_min + 1; L < lag_max; L++) {
        int64_t next = lag_corr(pcm, n, m, L + 1);
        if (cur >= prev && cur >= next && cur >= peak_thr) {
            best = L;              // earliest qualifying local maximum
            best_acc = cur;
            break;
        }
        prev = cur;
        cur = next;
    }
    if (best < 0) {                // no early peak qualified: global argmax
        for (int L = lag_min; L <= lag_max; L++) {
            int64_t a = lag_corr(pcm, n, m, L);
            if (a > best_acc) { best_acc = a; best = L; }
        }
    }
    if (best < 0 || best_acc <= 0) return r;
    // 2b) Divisor refinement: re-test L/2..L/4 of the winner.
    for (int div = 2; div <= 4; div++) {
        int Ld = best / div;
        if (Ld < lag_min) break;
        int64_t ad = lag_corr(pcm, n, m, Ld);
        if (ad > 0 && (float)ad >= TUNER_DIV_RATIO * (float)best_acc) {
            best = Ld;
            best_acc = ad;
            div = 1;   // recheck divisors against the new, shorter lag
        }
    }
    // 3) Parabolic interpolation around the best lag for sub-sample precision.
    int64_t aL = lag_corr(pcm, n, m, best - 1);
    int64_t aC = lag_corr(pcm, n, m, best);
    int64_t aR = lag_corr(pcm, n, m, best + 1);
    float denom = (float)(aL - 2 * aC + aR);
    float shift = 0.0f;
    if (denom < 0.0f) {   // concave peak only; otherwise keep integer lag
        shift = 0.5f * (float)(aL - aR) / denom;
    }
    float lag = (float)best + shift;
    float hz = (float)TUNER_PITCH_SR / lag;
    float conf = (float)((double)best_acc / (double)e0);
    if (conf < TUNER_MIN_CONF || hz < TUNER_PITCH_FMIN ||
        hz > TUNER_PITCH_FMAX) {
        return (tuner_pitch_result_t){0.0f, conf};
    }
    r.hz = hz;
    r.conf = conf;
    return r;
}

// Median of three: one glitchy or unvoiced frame between two good ones must
// not flicker the readout.
float tuner_pitch_median3(float a, float b, float c) {
    float lo = a < b ? a : b;
    float hi = a < b ? b : a;
    if (c < lo) return lo;
    if (c > hi) return hi;
    return c;
}

float tuner_pitch_cents(float f0, float target_hz) {
    if (f0 <= 0.0f || target_hz <= 0.0f) return 0.0f;
    return 1200.0f * log2f(f0 / target_hz);
}

// Equal-tempered: midi = 69 + 12*log2(hz/440), rounded to nearest semitone.
int16_t tuner_note_from_hz(float hz) {
    if (hz <= 0.0f) return -1;
    float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    float rounded = floorf(midi + 0.5f);
    if (rounded < 21.0f) rounded = 21.0f;    // A0
    if (rounded > 108.0f) rounded = 108.0f;  // C8
    return (int16_t)rounded;
}

// Deviation from the NEAREST semitone (what a tuner displays).
float tuner_note_cents(float hz, int16_t *midi) {
    int16_t n = tuner_note_from_hz(hz);
    if (midi) *midi = n;
    if (n < 0) return 0.0f;
    return tuner_pitch_cents(hz, tuner_note_freq(n));
}

// Full-range note names: sharps, scientific octave (C4 = middle C).
const char *tuner_note_name(int16_t midi) {
    static char buf[4];
    static const char *NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    if (midi < 21 || midi > 108) return "--";
    snprintf(buf, sizeof(buf), "%s%d", NAMES[midi % 12], midi / 12 - 1);
    return buf;
}

// Equal-tempered frequency of a MIDI note.
float tuner_note_freq(int16_t midi) {
    return 440.0f * exp2f((midi - 69) / 12.0f);
}
