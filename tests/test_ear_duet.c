// test_ear_duet.c - host tests for [PURE] duet segmentation, key inference,
// and stage-gated harmony (PRD 3.3). Plain asserts, exit code = verdict.
#include "ear_duet.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C4 261.626f
#define A3 220.0f
#define D4 293.665f
#define E4 329.628f
#define G4 392.0f
#define C5 523.251f
#define E5 659.255f
#define G5 783.991f

static void feed_hz(ear_duet_seg_t *s, ear_duet_phrase_t *p, float hz,
                    int frames) {
    for (int i = 0; i < frames; i++) {
        ear_duet_seg_feed(s, hz, p);
    }
}

static void test_seg_steady_note(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, C4, 20);
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 1);
    assert(p.notes[0].midi == 60);
    assert(p.notes[0].ms == 20 * EAR_DUET_FRAME_MS);
    printf("duet seg: steady note ok\n");
}

static void test_seg_vibrato_no_split(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    for (int i = 0; i < 24; i++) {
        // +/-~40 cents around C4: natural vibrato must stay one note
        float cents = (i % 2 == 0) ? 40.0f : -40.0f;
        float hz = C4 * powf(2.0f, cents / 1200.0f);
        ear_duet_seg_feed(&s, hz, &p);
    }
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 1);
    assert(p.notes[0].midi == 60);
    printf("duet seg: vibrato no-split ok\n");
}

static void test_seg_real_change(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, A3, 12);
    feed_hz(&s, &p, C4, 12);
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 2);
    assert(p.notes[0].midi == 57);
    assert(p.notes[1].midi == 60);
    printf("duet seg: real note change ok\n");
}

static void test_seg_excursion_absorbed(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, C4, 10);
    feed_hz(&s, &p, D4, 2);    // < CONFIRM frames: vibrato/glitch
    feed_hz(&s, &p, C4, 10);
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 1);
    assert(p.notes[0].midi == 60);
    printf("duet seg: short excursion absorbed ok\n");
}

static void test_seg_min_duration_drops(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, C4, 2);    // 128 ms < ~192 ms confirm floor
    for (int i = 0; i < 10; i++) {
        ear_duet_seg_feed(&s, 0.0f, &p);
    }
    assert(p.count == 0);
    printf("duet seg: under-minimum note dropped ok\n");
}

static void test_seg_gap_and_early_stop(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, A3, 10);
    feed_hz(&s, &p, C4, 10);
    bool stop = false;
    for (unsigned i = 0; i < EAR_DUET_STOP_FRAMES && !stop; i++) {
        stop = ear_duet_seg_feed(&s, 0.0f, &p);
    }
    assert(stop);              // 2 notes + ~0.8 s silence ends capture
    assert(p.count == 2);
    printf("duet seg: gap close + early stop ok\n");
}

static void test_seg_note_cap(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    for (int n = 0; n < 15; n++) {   // 15 alternations, cap keeps 12
        feed_hz(&s, &p, A3, 6);
        feed_hz(&s, &p, C4, 6);
    }
    ear_duet_seg_finish(&s, &p);
    assert(p.count == EAR_DUET_MAX_NOTES);
    printf("duet seg: 12-note cap ok\n");
}

static void test_seg_cents_drift(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    // 40 cents flat of C4: still the same semitone, but the drift must be
    // recorded so the grader can tell "flat C" from "B".
    float hz = C4 * powf(2.0f, -40.0f / 1200.0f);
    feed_hz(&s, &p, hz, 20);
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 1);
    assert(p.notes[0].midi == 60);
    assert(p.notes[0].cents >= -41 && p.notes[0].cents <= -39);
    printf("duet seg: cents drift recorded ok\n");
}

static void test_seg_cents_in_tune(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, C4, 20);
    ear_duet_seg_finish(&s, &p);
    assert(p.count == 1);
    assert(p.notes[0].cents >= -1 && p.notes[0].cents <= 1);
    printf("duet seg: in-tune cents ~0 ok\n");
}

static void test_seg_stop_after_expected(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, A3, 10);
    feed_hz(&s, &p, C4, 10);
    // Expecting a 3-note melody: ordinary trailing silence must not end the
    // capture, or the tail of the phrase is lost.
    for (unsigned i = 0; i < EAR_DUET_STOP_FRAMES; i++) {
        assert(!ear_duet_seg_feed_ex(&s, 0.0f, &p, 3));
    }
    bool stop = false;
    for (unsigned i = 0; i < 40u && !stop; i++) {
        stop = ear_duet_seg_feed_ex(&s, 0.0f, &p, 3);
    }
    assert(stop);              // ~1.6 s of silence ends it anyway
    assert(p.count == 2);
    printf("duet seg: expected-count stop rule ok\n");
}

static void test_seg_stop_when_expected_reached(void) {
    ear_duet_seg_t s;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&s);
    memset(&p, 0, sizeof(p));
    feed_hz(&s, &p, A3, 10);
    feed_hz(&s, &p, C4, 10);
    feed_hz(&s, &p, E4, 10);
    bool stop = false;
    for (unsigned i = 0; i < EAR_DUET_STOP_FRAMES && !stop; i++) {
        stop = ear_duet_seg_feed_ex(&s, 0.0f, &p, 3);
    }
    assert(stop);              // 3 notes + ~0.8 s silence ends capture
    assert(p.count == 3);
    printf("duet seg: stop when expected count reached ok\n");
}

static void test_key_c_major(void) {
    ear_duet_phrase_t p = { .notes = {
        {60, 800, 0}, {64, 600, 0}, {67, 600, 0}, {62, 400, 0}, {65, 400, 0}, {69, 400, 0},
    }, .count = 6 };
    ear_duet_key_t k;
    ear_duet_infer_key(&p, &k);
    assert(!k.fallback);
    assert(k.tonic == 0 && !k.minor);
    printf("duet key: C major ok\n");
}

static void test_key_a_minor(void) {
    ear_duet_phrase_t p = { .notes = {
        {69, 700, 0}, {72, 500, 0}, {64, 500, 0}, {67, 400, 0}, {62, 400, 0},
    }, .count = 5 };
    ear_duet_key_t k;
    ear_duet_infer_key(&p, &k);
    assert(!k.fallback);
    assert(k.tonic == 9 && k.minor);
    printf("duet key: A minor ok\n");
}

static void test_key_octave_folding(void) {
    ear_duet_phrase_t p = { .notes = {
        {60, 500, 0}, {64, 400, 0}, {67, 400, 0}, {72, 500, 0}, {76, 400, 0}, {79, 400, 0},
    }, .count = 6 };
    ear_duet_key_t k;
    ear_duet_infer_key(&p, &k);
    assert(!k.fallback);
    assert(k.tonic == 0 && !k.minor);   // octave doubling changes nothing
    printf("duet key: octave folding ok\n");
}

static void test_key_fallback_drone(void) {
    ear_duet_phrase_t p = { .notes = {
        {60, 1500, 0}, {64, 300, 0},
    }, .count = 2 };
    ear_duet_key_t k;
    ear_duet_infer_key(&p, &k);
    assert(k.fallback);
    assert(k.tonic == 0 && !k.minor);   // heaviest class as major tonic
    printf("duet key: drone fallback ok\n");
}

static void test_harmony_unison(void) {
    ear_duet_phrase_t p = { .notes = {{60, 500, 0}, {64, 500, 0}}, .count = 2 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_EGG, h);
    assert(h[0] == 60 && h[1] == 64);
    ear_duet_harmony(&p, &k, EAR_STAGE_HATCHLING, h);
    assert(h[0] == 60 && h[1] == 64);
    printf("duet harmony: unison stages ok\n");
}

static void test_harmony_third(void) {
    ear_duet_phrase_t p = { .notes = {{64, 500, 0}, {67, 500, 0}, {60, 500, 0}},
                           .count = 3 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_CHICK, h);
    assert(h[0] == 60);   // E4 -> C4
    assert(h[1] == 64);   // G4 -> E4
    assert(h[2] == 57);   // C4 -> A3 (57 >= C3, stays below)
    printf("duet harmony: diatonic thirds ok\n");
}

static void test_harmony_low_mirror(void) {
    ear_duet_phrase_t p = { .notes = {{48, 500, 0}}, .count = 1 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_CHICK, h);
    assert(h[0] == 52);   // C3 third-below A2 < C3 -> mirror above E3
    printf("duet harmony: low mirror ok\n");
}

static void test_harmony_sixth(void) {
    ear_duet_phrase_t p = { .notes = {{64, 500, 0}}, .count = 1 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_FLEDGLING, h);
    assert(h[0] == 55);   // E4 sixth-below G3
    printf("duet harmony: diatonic sixth ok\n");
}

static void test_harmony_octave_clamp(void) {
    ear_duet_phrase_t p = { .notes = {{84, 500, 0}, {45, 500, 0}}, .count = 2 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_EGG, h);
    assert(h[0] == 72);   // C6 folds down to C5 (> A5 rule)
    assert(h[1] == 57);   // A2 folds up to A3 (< C3 rule)
    printf("duet harmony: octave clamps ok\n");
}

static void test_harmony_voice_leading(void) {
    ear_duet_phrase_t p = { .notes = {{60, 400, 0}, {64, 400, 0}, {67, 400, 0}},
                           .count = 3 };
    ear_duet_key_t k = { .tonic = 0, .minor = false, .fallback = false };
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_SONGBIRD, h);
    // C4: first note takes the third (A3). E4: third C4 (step 3) vs sixth
    // G3 (step 2) -> G3. G4: third E4 (step 9) vs sixth B3 (step 4) -> B3.
    assert(h[0] == 57);
    assert(h[1] == 55);
    assert(h[2] == 59);
    printf("duet harmony: voice leading ok\n");
}


// ---- end-to-end pipeline: the exact chain run_duet_capture + duet_review
// execute on device (detect -> median-3 -> segment -> key -> harmony),
// driven with synthesized "singing" instead of a microphone.
static void synth_voice(int16_t *pcm, int n, float hz) {
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)EAR_PITCH_SR;
        float v = 0.75f * sinf(6.2831853f * hz * t)
                + 0.20f * sinf(12.5663706f * hz * t);
        pcm[i] = (int16_t)(v * 11000.0f);
    }
}

static void pipeline_sing(ear_duet_seg_t *seg, ear_duet_phrase_t *p,
                          float hz, int frames) {
    static int16_t pcm[EAR_PITCH_FRAME];
    static float f1 = 0.0f, f2 = 0.0f;
    static int hist = 0;
    for (int i = 0; i < frames; i++) {
        synth_voice(pcm, EAR_PITCH_FRAME, hz);
        ear_pitch_result_t pr = ear_pitch_detect(pcm, EAR_PITCH_FRAME);
        float f0 = pr.hz;
        if (hist >= 2) f0 = ear_pitch_median3(f1, f2, pr.hz);
        f1 = f2; f2 = pr.hz;
        if (hist < 2) hist++;
        ear_duet_seg_feed(seg, f0, p);
    }
}

static void test_pipeline_duet_round(void) {
    ear_duet_seg_t seg;
    ear_duet_phrase_t p;
    ear_duet_seg_init(&seg);
    memset(&p, 0, sizeof(p));
    // "Sing" C4 E4 G4 E4 C4, ten 64 ms frames per note (~640 ms each).
    pipeline_sing(&seg, &p, C4, 10);
    pipeline_sing(&seg, &p, E4, 10);
    pipeline_sing(&seg, &p, G4, 10);
    pipeline_sing(&seg, &p, E4, 10);
    pipeline_sing(&seg, &p, C4, 10);
    ear_duet_seg_finish(&seg, &p);
    assert(p.count == 5);
    assert(p.notes[0].midi == 60 && p.notes[1].midi == 64 &&
           p.notes[2].midi == 67 && p.notes[3].midi == 64 &&
           p.notes[4].midi == 60);
    ear_duet_key_t k;
    ear_duet_infer_key(&p, &k);
    assert(!k.fallback);
    assert(k.tonic == 0 && !k.minor);   // C major over the E-minor tie
    uint8_t h[EAR_DUET_MAX_NOTES];
    ear_duet_harmony(&p, &k, EAR_STAGE_SONGBIRD, h);
    assert(h[0] == 57 && h[1] == 55 && h[2] == 59);   // A3 G3 B3
    assert(h[3] == 60 && h[4] == 57);                 // C4 A3
    // Octave-low male voice: same phrase an octave down must transcribe
    // identically up to the octave (pitch classes and key unchanged).
    ear_duet_seg_init(&seg);
    memset(&p, 0, sizeof(p));
    pipeline_sing(&seg, &p, C4 / 2, 10);
    pipeline_sing(&seg, &p, E4 / 2, 10);
    pipeline_sing(&seg, &p, G4 / 2, 10);
    pipeline_sing(&seg, &p, E4 / 2, 10);
    pipeline_sing(&seg, &p, C4 / 2, 10);
    ear_duet_seg_finish(&seg, &p);
    assert(p.count == 5);
    ear_duet_infer_key(&p, &k);
    assert(!k.fallback && k.tonic == 0 && !k.minor);
    printf("duet pipeline: synthesized round-trip ok\n");
}

int main(void) {
    test_pipeline_duet_round();
    test_seg_steady_note();
    test_seg_vibrato_no_split();
    test_seg_real_change();
    test_seg_excursion_absorbed();
    test_seg_min_duration_drops();
    test_seg_gap_and_early_stop();
    test_seg_cents_drift();
    test_seg_cents_in_tune();
    test_seg_stop_after_expected();
    test_seg_stop_when_expected_reached();
    test_seg_note_cap();
    test_key_c_major();
    test_key_a_minor();
    test_key_octave_folding();
    test_key_fallback_drone();
    test_harmony_unison();
    test_harmony_third();
    test_harmony_low_mirror();
    test_harmony_sixth();
    test_harmony_octave_clamp();
    test_harmony_voice_leading();
    printf("duet: all tests passed\n");
    return 0;
}
