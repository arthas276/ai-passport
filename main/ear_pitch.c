// ear_pitch.c - integer-domain autocorrelation pitch detection. PURE C.
// ESP32-C3 has no FPU: the correlation scan is int64 math (M extension),
// float is used only for the final hz/cents conversion.
#include "ear_game.h"
#include <math.h>

// Voice/noise gates. TUNE ON DEVICE (PRD 9): RMS^2 * FRAME energy floor and
// minimum autocorrelation peak quality. Singing voice at near-field mic is
// typically RMS >= 1000; set the gate conservatively low.
#define EAR_PITCH_RMS_GATE   300.0f
#define EAR_PITCH_MIN_CONF   0.30f
// Octave safety: a periodic voice peaks at its true period L0 AND at 2L0,
// 3L0...; the chosen lag is the earliest local peak whose value is at least
// this ratio of the global peak, i.e. the fundamental, not a subharmonic.
#define EAR_PITCH_PEAK_RATIO 0.80f
// Divisor-refinement threshold (v1.3): shimmer/jitter can smear L0 off a
// local maximum so the earliest-peak scan still lands on 2L0/3L0. Whatever
// lag wins, its integer divisors L/2..L/4 are re-tested at this slightly
// lower ratio to recover the true fundamental.
#define EAR_PITCH_DIV_RATIO  0.72f

// Autocorrelation at one lag, mean removed. Integer accumulation.
static int64_t lag_corr(const int16_t *x, int n, int32_t m, int L) {
    int64_t acc = 0;
    for (int i = 0; i + L < n; i++) {
        int32_t a = x[i] - m;
        acc += (int64_t)a * (int64_t)(x[i + L] - m);
    }
    return acc;
}

// Detect F0 of one frame (n == EAR_PITCH_FRAME @ EAR_PITCH_SR).
// Mean -> energy gate -> octave-safe earliest-peak lag scan -> parabolic
// interpolation.
ear_pitch_result_t ear_pitch_detect(const int16_t *pcm, int n) {
    ear_pitch_result_t r = {0.0f, 0.0f};
    if (!pcm || n < EAR_PITCH_FRAME) return r;
    // 1) Mean + energy gate: rejects silence and background noise before the
    //    O(lags * frame) scan. e0 == n * rms^2, so the gate is an RMS test.
    int32_t m = 0;
    double msum = 0.0;
    for (int i = 0; i < n; i++) msum += pcm[i];
    m = (int32_t)(msum / n + (msum >= 0 ? 0.5 : -0.5));
    int64_t e0 = 0;
    for (int i = 0; i < n; i++) {
        int32_t d = pcm[i] - m;
        e0 += (int64_t)d * d;
    }
    if (e0 < (int64_t)(EAR_PITCH_RMS_GATE * EAR_PITCH_RMS_GATE) * n) return r;
    // 2) Lag scan, octave-safe. Raw argmax over the whole lag range often
    //    lands on 2L0/3L0 when the voice has natural jitter (device 2026-09-29:
    //    SING readouts stuck near -1200/-2400 cents = f0 halved/thirded).
    //    Pass A finds the global peak VALUE only; pass B picks the earliest
    //    local peak >= EAR_PITCH_PEAK_RATIO of it - the fundamental.
    const int lag_min = (int)(EAR_PITCH_SR / EAR_PITCH_FMAX);
    const int lag_max = (int)(EAR_PITCH_SR / EAR_PITCH_FMIN);
    if (lag_max >= n) return r;
    int64_t peak_val = 0;
    for (int L = lag_min; L <= lag_max; L++) {
        int64_t a = lag_corr(pcm, n, m, L);
        if (a > peak_val) peak_val = a;
    }
    if (peak_val <= 0) return r;
    const int64_t peak_thr = (int64_t)(EAR_PITCH_PEAK_RATIO * (double)peak_val);
    int best = -1;
    int64_t best_acc = 0;
    int64_t prev = 0;              // unused at L == lag_min (left edge)
    int64_t cur = lag_corr(pcm, n, m, lag_min);
    for (int L = lag_min; L < lag_max; L++) {
        int64_t next = lag_corr(pcm, n, m, L + 1);
        if ((L == lag_min || cur >= prev) && cur >= next && cur >= peak_thr) {
            best = L;              // earliest qualifying local maximum
            best_acc = cur;
            break;
        }
        prev = cur;
        cur = next;
    }
    if (best < 0) {
        // No early peak qualified: fall back to the global argmax.
        for (int L = lag_min; L <= lag_max; L++) {
            int64_t a = lag_corr(pcm, n, m, L);
            if (a > best_acc) { best_acc = a; best = L; }
        }
    }
    if (best < 0 || best_acc <= 0) return r;
    // 2b) Divisor refinement: re-test L/2..L/4 of the winner. Structural
    //     subharmonic locks (shimmer: adjacent cycles differ, every second
    //     cycle matches) defeat local-max scanning; divisors find them.
    for (int div = 2; div <= 4; div++) {
        int Ld = best / div;
        if (Ld < lag_min) break;
        int64_t ad = lag_corr(pcm, n, m, Ld);
        if (ad > 0 && (float)ad >= EAR_PITCH_DIV_RATIO * (float)best_acc) {
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
    float hz = (float)EAR_PITCH_SR / lag;
    float conf = (float)((double)best_acc / (double)e0);
    if (conf < EAR_PITCH_MIN_CONF || hz < EAR_PITCH_FMIN || hz > EAR_PITCH_FMAX) {
        return (ear_pitch_result_t){0.0f, conf};
    }
    r.hz = hz;
    r.conf = conf;
    return r;
}

float ear_pitch_cents(float f0, float target_hz) {
    if (f0 <= 0.0f || target_hz <= 0.0f) return 0.0f;
    return 1200.0f * log2f(f0 / target_hz);
}

// Equal-tempered: midi = 69 + 12*log2(hz/440), rounded to nearest semitone.
uint8_t ear_note_from_hz(float hz) {
    if (hz <= 0.0f) return 0;
    float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    float rounded = floorf(midi + 0.5f);
    if (rounded < 1.0f) rounded = 1.0f;
    if (rounded > 127.0f) rounded = 127.0f;
    return (uint8_t)rounded;
}

// Names for MIDI 60..69 (C4..A4); other values fall back to "?".
const char *ear_note_name(uint8_t midi) {
    static const char *NAMES[10] = {
        "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4",
    };
    if (midi < 60 || midi > 69) return "?";
    return NAMES[midi - 60];
}

// Equal-tempered frequency of a MIDI note.
float ear_note_freq(uint8_t midi) {
    return 440.0f * exp2f((midi - 69) / 12.0f);
}

// Median of three (PRD 3.2 smoothing): one glitchy or unvoiced frame between
// two good detections must not reset the sing hold timer or the marker.
float ear_pitch_median3(float a, float b, float c) {
    float lo = a < b ? a : b;
    float hi = a < b ? b : a;
    if (c < lo) return lo;
    if (c > hi) return hi;
    return c;
}

// Octave-equivalent deviation (v1.1): wrap cents into [-600, +600) so singing
// the right pitch class one or more octaves off still grades as on-the-note
// (men commonly land an octave under C4-A4 targets). *octaves receives the
// folded-octave count (negative = sung low) for UI hints.
float ear_pitch_octave_cents(float cents, int8_t *octaves) {
    float c = cents;
    int8_t n = 0;
    while (c >= 600.0f) { c -= 1200.0f; n++; }
    while (c < -600.0f) { c += 1200.0f; n--; }
    if (octaves) *octaves = n;
    return c;
}
