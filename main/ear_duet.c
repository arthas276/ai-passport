// ear_duet.c - [PURE] duet transcription: live note segmentation, key
// inference, stage-gated harmony (PRD 3.3, v1.3). Integer-first math, no
// allocation, no IDF/LVGL -> host-testable (see tests/test_ear_duet.c).
#include "ear_duet.h"
#include <math.h>

// ---- live segmentation ----

void ear_duet_seg_init(ear_duet_seg_t *s) {
    s->cur_midi = 0;
    s->cur_ms = 0;
    s->cand_midi = 0;
    s->cand_frames = 0;
    s->gap_frames = 0;
    s->count = 0;
    s->active = false;
}

// Append one closed note; extra notes beyond the cap are dropped in order.
static void seg_close(ear_duet_seg_t *s, ear_duet_phrase_t *out) {
    if (!s->active) return;
    if (s->cur_ms >= EAR_DUET_CONFIRM_FRAMES * EAR_DUET_FRAME_MS &&
        out->count < EAR_DUET_MAX_NOTES) {
        out->notes[out->count].midi = s->cur_midi;
        out->notes[out->count].ms = s->cur_ms;
        out->count++;
        s->count = out->count;
    }
    s->active = false;
    s->cur_ms = 0;
}

// |cents| between a sung hz and the semitone it belongs to. Used to keep
// vibrato (wobble <= EAR_DUET_WOBBLE_CENTS around the open note) inside the
// same note instead of splitting it (anti-vibrato rule, PRD 3.3).
static float wobble_cents(float hz, uint8_t midi) {
    return ear_pitch_cents(hz, ear_note_freq(midi));
}

bool ear_duet_seg_feed(ear_duet_seg_t *s, float hz, ear_duet_phrase_t *out) {
    if (hz <= 0.0f) {
        s->cand_frames = 0;
        s->gap_frames++;
        if (s->gap_frames >= EAR_DUET_GAP_FRAMES) {
            seg_close(s, out);   // >= 350 ms rest closes the note
        }
        // Post-min-notes trailing silence (~0.8 s) ends the capture early.
        return out->count >= EAR_DUET_MIN_NOTES &&
               s->gap_frames >= EAR_DUET_STOP_FRAMES;
    }

    s->gap_frames = 0;
    uint8_t midi = ear_note_from_hz(hz);
    if (midi < 21 || midi > 108) return false;   // out of musical range

    if (!s->active) {
        s->active = true;
        s->cur_midi = midi;
        s->cur_ms = EAR_DUET_FRAME_MS;
        s->cand_frames = 0;
        return false;
    }

    if (midi == s->cur_midi ||
        fabsf(wobble_cents(hz, s->cur_midi)) <= EAR_DUET_WOBBLE_CENTS) {
        s->cur_ms += EAR_DUET_FRAME_MS;          // same note continues
        s->cand_frames = 0;
        return false;
    }

    // Different pitch: needs EAR_DUET_CONFIRM_FRAMES stable frames before
    // the change is believed (shorter excursions are vibrato/glitches).
    if (s->cand_frames == 0 || midi != s->cand_midi) {
        s->cand_midi = midi;
        s->cand_frames = 1;
    } else {
        s->cand_frames++;
        if (s->cand_frames >= EAR_DUET_CONFIRM_FRAMES) {
            seg_close(s, out);
            s->active = true;
            s->cur_midi = s->cand_midi;
            s->cur_ms = EAR_DUET_CONFIRM_FRAMES * EAR_DUET_FRAME_MS;
            s->cand_frames = 0;
        }
    }
    return false;
}

void ear_duet_seg_finish(ear_duet_seg_t *s, ear_duet_phrase_t *out) {
    seg_close(s, out);
}

// ---- key inference ----

// Diatonic pitch-class sets: major and natural minor, bit i = class i.
#define PC_MASK_MAJOR(tonic) \
    (1u << (tonic) | 1u << ((tonic + 2) % 12) | 1u << ((tonic + 4) % 12) | \
     1u << ((tonic + 5) % 12) | 1u << ((tonic + 7) % 12) | \
     1u << ((tonic + 9) % 12) | 1u << ((tonic + 11) % 12))
#define PC_MASK_MINOR(tonic) \
    (1u << (tonic) | 1u << ((tonic + 2) % 12) | 1u << ((tonic + 3) % 12) | \
     1u << ((tonic + 5) % 12) | 1u << ((tonic + 7) % 12) | \
     1u << ((tonic + 8) % 12) | 1u << ((tonic + 10) % 12))

static const char *const PC_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

const char *ear_duet_pc_name(uint8_t pc) {
    return PC_NAMES[pc % 12];
}

void ear_duet_infer_key(const ear_duet_phrase_t *p, ear_duet_key_t *k) {
    k->tonic = 0;
    k->minor = false;
    k->fallback = true;
    if (p->count == 0) return;

    // Duration-weighted pitch-class histogram (integer semitone-ms units).
    // Melodies tend to start and end on the tonic, so the first and last
    // notes each add a 50% anchor bonus to their pitch class — without it,
    // C-E-G-E-C ties C major with E minor and falls back.
    uint32_t hist[12] = {0};
    uint8_t distinct = 0;
    for (uint8_t i = 0; i < p->count; i++) {
        uint8_t pc = p->notes[i].midi % 12;
        uint32_t w = p->notes[i].ms;
        if (i == 0 || i + 1 == p->count) w += p->notes[i].ms / 2;
        if (hist[pc] == 0) distinct++;
        hist[pc] += w;
    }

    // Fallback pre-condition: too few distinct classes to rank keys at all.
    if (distinct < 3) {
        uint8_t best = 0;
        for (uint8_t pc = 1; pc < 12; pc++) {
            if (hist[pc] > hist[best]) best = pc;
        }
        k->tonic = best;
        return;
    }

    // Score 24 candidate keys: in-scale notes earn their duration, out-of-
    // scale notes cost 2x (chromatic notes are strong negative evidence),
    // tonic appearances earn a 2x bonus (tonics dominate melodies).
    // Integer units: 1 ms of duration = 1 point.
    int32_t best_score = INT32_MIN, second_score = INT32_MIN;
    uint8_t best_tonic = 0;
    bool best_minor = false;
    for (uint8_t minor = 0; minor <= 1; minor++) {
        for (uint8_t tonic = 0; tonic < 12; tonic++) {
            uint32_t mask = minor ? PC_MASK_MINOR(tonic) : PC_MASK_MAJOR(tonic);
            int32_t score = 0;
            for (uint8_t pc = 0; pc < 12; pc++) {
                if (hist[pc] == 0) continue;
                if (mask & (1u << pc)) {
                    score += (int32_t)hist[pc];
                    if (pc == tonic) score += (int32_t)hist[pc] * 2;
                } else {
                    score -= (int32_t)hist[pc] * 2;
                }
            }
            if (score > best_score) {
                second_score = best_score;
                best_score = score;
                best_tonic = tonic;
                best_minor = minor;
            } else if (score > second_score) {
                second_score = score;
            }
        }
    }

    // Weak margin: the winner leads the runner-up by less than 10% of the
    // total sung duration (integer cross-multiplication), or the winner is
    // non-positive. Then keep the fallback: heaviest pitch class as a major
    // tonic (drone-ish / genuinely ambiguous phrases).
    uint32_t total_ms = 0;
    for (uint8_t pc = 0; pc < 12; pc++) total_ms += hist[pc];
    bool weak = second_score > 0 && total_ms > 0 &&
                (uint32_t)(best_score - second_score) * 10u < total_ms;
    if (best_score <= 0 || weak) {
        uint8_t heavy = 0;
        for (uint8_t pc = 1; pc < 12; pc++) {
            if (hist[pc] > hist[heavy]) heavy = pc;
        }
        k->tonic = heavy;
        return;
    }
    k->tonic = best_tonic;
    k->minor = best_minor;
    k->fallback = false;
}

// ---- harmony ----

#define HARM_LOW  48    // C3: harmony voices stay at or above
#define HARM_HIGH 81    // A5 (~880 Hz): above this folds down an octave

static int32_t clamp_octaves(int32_t midi) {
    while (midi > HARM_HIGH) midi -= 12;
    while (midi < HARM_LOW) midi += 12;
    return midi;
}

// True when midi's pitch class is diatonic in the key.
static bool in_scale(uint8_t midi, const ear_duet_key_t *k) {
    uint32_t mask = k->minor ? PC_MASK_MINOR(k->tonic) : PC_MASK_MAJOR(k->tonic);
    return (mask & (1u << (midi % 12))) != 0;
}

// Diatonic interval below m with `steps_between` in-scale notes strictly
// between h and m (1 = a third, 4 = a sixth). Returns -1 when none exists
// within one octave. Walks downward so the returned h is the closest one.
static int32_t dia_below(uint8_t m, const ear_duet_key_t *k,
                         uint8_t steps_between) {
    for (int32_t h = (int32_t)m - 1; h >= (int32_t)m - 12; h--) {
        if (h < 21) break;
        if (!in_scale((uint8_t)h, k)) continue;
        uint8_t between = 0;
        for (int32_t x = h + 1; x < (int32_t)m; x++) {
            if (in_scale((uint8_t)x, k)) between++;
        }
        if (between == steps_between) return h;
    }
    return -1;
}

// Mirror of dia_below walking upward (used when below would leave the range).
static int32_t dia_above(uint8_t m, const ear_duet_key_t *k,
                         uint8_t steps_between) {
    for (int32_t h = (int32_t)m + 1; h <= (int32_t)m + 12; h++) {
        if (h > 108) break;
        if (!in_scale((uint8_t)h, k)) continue;
        uint8_t between = 0;
        for (int32_t x = (int32_t)m + 1; x < h; x++) {
            if (in_scale((uint8_t)x, k)) between++;
        }
        if (between == steps_between) return h;
    }
    return -1;
}

// One harmony candidate for melody note m: the diatonic `steps`-interval
// (1 = third, 4 = sixth) below m, mirrored above when it would fall under
// C3, then octave-clamped into [48, 81]. Falls back to m (unison) when the
// scale walk finds nothing (cannot happen for a 12-note walk, kept for
// safety so playback never receives garbage).
static uint8_t harmony_for(uint8_t m, const ear_duet_key_t *k,
                           uint8_t steps_between) {
    int32_t h = dia_below(m, k, steps_between);
    if (h >= 0 && h < HARM_LOW) {
        int32_t up = dia_above(m, k, steps_between);
        if (up >= 0) h = up;
    }
    if (h < 0) h = m;
    return (uint8_t)clamp_octaves(h);
}

void ear_duet_harmony(const ear_duet_phrase_t *p, const ear_duet_key_t *k,
                      ear_stage_t stage, uint8_t out[EAR_DUET_MAX_NOTES]) {
    for (uint8_t i = 0; i < EAR_DUET_MAX_NOTES; i++) {
        out[i] = i < p->count ? p->notes[i].midi : 0;
    }
    if (stage == EAR_STAGE_EGG || stage == EAR_STAGE_HATCHLING) {
        for (uint8_t i = 0; i < p->count; i++) {
            out[i] = (uint8_t)clamp_octaves(p->notes[i].midi);
        }
        return;   // unison: the quieter twin voice doubles the melody
    }

    bool sixth = (stage == EAR_STAGE_FLEDGLING);
    uint8_t prev = 0;
    for (uint8_t i = 0; i < p->count; i++) {
        uint8_t m = p->notes[i].midi;
        uint8_t third = harmony_for(m, k, 1);
        if (stage != EAR_STAGE_SONGBIRD) {
            out[i] = sixth ? harmony_for(m, k, 4) : third;
        } else {
            // Smooth voice leading: pick third or sixth closest to the
            // previous harmony note; ties prefer the third.
            uint8_t sixth_h = harmony_for(m, k, 4);
            if (prev == 0) {
                out[i] = third;
            } else {
                int32_t d3 = (int32_t)third - (int32_t)prev;
                int32_t d6 = (int32_t)sixth_h - (int32_t)prev;
                if (d3 < 0) d3 = -d3;
                if (d6 < 0) d6 = -d6;
                out[i] = d6 < d3 ? sixth_h : third;
            }
            prev = out[i];
        }
    }
}
