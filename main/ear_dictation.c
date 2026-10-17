// ear_dictation.c - [PURE] melody generation, octave-tolerant grading and
// session flow for the EAR BUDDY dictation mode (PRD 3.4, v1.4). No
// IDF/LVGL, no allocation; host-tested by tests/test_ear_dictation.c.
#include "ear_dictation.h"
#include <stdint.h>

typedef struct {
    uint8_t count;      // notes per melody
    uint8_t lo, hi;     // MIDI range
    uint8_t max_leap;   // largest step between adjacent notes, semitones
    bool triad_ends;    // first and last note land on a C-major triad
} dict_level_def_t;

// Level shape (PRD 3.4): L1 stays on the 8-key keyboard C4..G4 and is
// triadic; L2/L3 widen the range and allow wider leaps.
static const dict_level_def_t LEVELS[EAR_LEVEL_MAX] = {
    {3u, 60u, 67u, 2u, true},
    {4u, 55u, 74u, 5u, false},
    {5u, 48u, 74u, 7u, false},
};

// Playback tempo by level: slower for beginners (PRD 3.4).
const uint16_t EAR_DICTATION_NOTE_MS[EAR_LEVEL_MAX] = {560u, 420u, 360u};

// Grading tolerance by level, in cents (PRD 3.4). The transcriber rounds
// every frame to the nearest semitone, so exact MIDI equality graded a note
// sung 60 cents flat as the semitone below it; the cents remainder recovers
// the true error. The window therefore stays just under a semitone at every
// level - a genuinely different note is always wrong - and widens for
// beginners: L1 is wider than the transcriber's own EAR_DUET_WOBBLE_CENTS
// window (70), so any note the device hears as "that note" passes.
const uint16_t EAR_DICT_TOL_CENTS[EAR_LEVEL_MAX] = {90u, 70u, 50u};

const char *const EAR_DICT_TOL_NAME[EAR_LEVEL_MAX] = {"EASY", "NORMAL", "STRICT"};

// xorshift32; the zero guard keeps the rng from locking on zero.
static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    if (x == 0u) x = 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int rng_range(uint32_t *s, int lo, int hi) {
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(rng_next(s) % span);
}

// L1 always starts on C4, the keyboard's first key and the reference pitch
// the listener hears first; higher levels start in their low octave.
static uint8_t pick_first(const dict_level_def_t *L, uint32_t *seed) {
    if (L->triad_ends) return 60u;
    return (uint8_t)rng_range(seed, L->lo, L->lo + 7);
}

static uint8_t pick_next(const dict_level_def_t *L, uint8_t prev, uint32_t *seed) {
    int lo = (int)prev - (int)L->max_leap;
    int hi = (int)prev + (int)L->max_leap;
    if (lo < (int)L->lo) lo = (int)L->lo;
    if (hi > (int)L->hi) hi = (int)L->hi;
    for (int tries = 0; tries < 32; tries++) {
        int c = rng_range(seed, lo, hi);
        if (c != (int)prev) return (uint8_t)c;
    }
    if ((int)prev + 1 <= hi) return (uint8_t)(prev + 1u);
    if ((int)prev - 1 >= lo) return (uint8_t)(prev - 1u);
    return prev;
}

static uint8_t pick_last_triad(const dict_level_def_t *L, uint8_t prev,
                               uint32_t *seed) {
    static const uint8_t TRIAD[3] = {60u, 64u, 67u};
    for (int tries = 0; tries < 32; tries++) {
        uint8_t c = TRIAD[rng_range(seed, 0, 2)];
        int d = (int)c - (int)prev;
        if (d < 0) d = -d;
        if (c >= L->lo && c <= L->hi && d <= (int)L->max_leap && c != prev) {
            return c;
        }
    }
    for (uint8_t i = 0; i < 3u; i++) {
        if (TRIAD[i] >= L->lo && TRIAD[i] <= L->hi && TRIAD[i] != prev) {
            return TRIAD[i];
        }
    }
    return prev;
}

void ear_dictation_session_init(ear_dictation_session_t *s, uint8_t level,
                                uint32_t seed) {
    if (level < EAR_LEVEL_MIN) level = EAR_LEVEL_MIN;
    if (level > EAR_LEVEL_MAX) level = EAR_LEVEL_MAX;
    s->level = level;
    s->index = 0;
    s->score = 0;
    s->seed = seed ? seed : 0x6D2B79F5u;
}

void ear_dictation_next_melody(ear_dictation_session_t *s,
                               ear_dictation_melody_t *out) {
    out->count = 0;
    uint8_t level = s->level;
    if (level < EAR_LEVEL_MIN) level = EAR_LEVEL_MIN;
    if (level > EAR_LEVEL_MAX) level = EAR_LEVEL_MAX;
    const dict_level_def_t *L = &LEVELS[level - 1u];
    uint8_t n = L->count;
    if (n > EAR_DICTATION_MAX_NOTES) n = (uint8_t)EAR_DICTATION_MAX_NOTES;
    out->notes[0] = pick_first(L, &s->seed);
    for (uint8_t i = 1; i < n; i++) {
        uint8_t prev = out->notes[i - 1u];
        if (L->triad_ends && i + 1u == n) {
            out->notes[i] = pick_last_triad(L, prev, &s->seed);
        } else {
            out->notes[i] = pick_next(L, prev, &s->seed);
        }
    }
    out->count = n;
}

// Does a sung note (semitone + cents) satisfy a target note within tol?
// Octave-equivalent pitches pass (relative pitch, same rule as the
// whole-phrase fold); a neighbouring semitone only passes when the sung cents
// pull the pitch back inside the tolerance window, e.g. C4 sung 60 cents flat
// is transcribed as B3 +40 cents and still counts as C4.
static bool note_matches(uint8_t target, uint8_t sung_midi, int cents,
                         int tol) {
    int semi = (int)sung_midi - (int)target;
    int pc = ((semi % 12) + 12) % 12;          // pitch-class distance
    if (pc == 0) return cents >= -tol && cents <= tol;
    int shortest = pc <= 6 ? pc : pc - 12;     // signed semitone error
    int dev = shortest * 100 + cents;          // true cents error
    return dev >= -tol && dev <= tol;
}

// Grade a sung phrase against the melody, using the level's tolerance
// (EAR_DICT_TOL_CENTS[level-1]). A whole-phrase octave offset (e.g. an alto
// singing the melody an octave down, or a child up) is folded away before
// matching; only the FIRST note decides. Melody positions are then matched
// in order, octave-equivalently and within the tolerance, and up to
// EAR_DICT_MAX_SKIPS sung notes per position may be skipped as extras - a
// hiccup or a mis-segmented note no longer shifts every later position.
void ear_dictation_grade(const ear_dictation_melody_t *target,
                         const ear_duet_phrase_t *sung, uint8_t level,
                         ear_dictation_result_t *res) {
    res->count = 0;
    res->correct_notes = 0;
    res->extras = 0;
    res->octave_folded = false;
    res->octaves = 0;
    if (target->count == 0) return;

    uint8_t sung_count = sung ? sung->count : 0u;
    if (sung_count > EAR_DUET_MAX_FRAMES) sung_count = (uint8_t)EAR_DUET_MAX_FRAMES;

    int shift = 0;
    if (sung_count > 0) {
        int d = (int)sung->notes[0].midi - (int)target->notes[0];
        if (d != 0 && d % 12 == 0) {
            shift = -d;
            res->octave_folded = true;
            res->octaves = (int8_t)(d / 12);
        }
    }

    uint8_t lv = level < 1u ? 1u : (level > EAR_LEVEL_MAX ? EAR_LEVEL_MAX : level);
    int tol = (int)EAR_DICT_TOL_CENTS[lv - 1u];

    uint8_t tc = target->count;
    if (tc > EAR_DICTATION_MAX_NOTES) tc = (uint8_t)EAR_DICTATION_MAX_NOTES;
    res->count = tc;

    uint8_t si = 0;   // next unread sung note
    for (uint8_t i = 0; i < tc; i++) {
        ear_dictation_note_t *m = &res->notes[i];
        m->target = target->notes[i];
        m->sung = 0;
        m->cents = 0;
        m->kind = EAR_DICT_MARK_MISS;
        m->off = 0;

        int matched = -1;
        for (uint8_t look = si, skips = 0;
             look < sung_count && skips <= EAR_DICT_MAX_SKIPS;
             look++, skips++) {
            int sm = (int)sung->notes[look].midi + shift;
            if (sm < 1) sm = 1;
            if (sm > 127) sm = 127;
            if (note_matches(m->target, (uint8_t)sm, sung->notes[look].cents,
                             tol)) {
                matched = (int)look;
                break;
            }
        }
        if (matched >= 0) {
            res->extras += (uint8_t)(matched - (int)si);
            int sm = (int)sung->notes[matched].midi + shift;
            if (sm < 1) sm = 1;
            if (sm > 127) sm = 127;
            m->sung = (uint8_t)sm;
            m->cents = sung->notes[matched].cents;
            m->kind = EAR_DICT_MARK_OK;
            m->off = 0;
            res->correct_notes++;
            si = (uint8_t)matched + 1u;
            continue;
        }
        // MISS: report the error against the next unread note, if any.
        if (si < sung_count) {
            int sm = (int)sung->notes[si].midi + shift;
            if (sm < 1) sm = 1;
            if (sm > 127) sm = 127;
            m->sung = (uint8_t)sm;
            m->off = (int8_t)((int)m->sung - (int)m->target);
        }
    }
    // Sung notes no melody position claimed are extras.
    if (sung_count > si) res->extras += (uint8_t)(sung_count - si);
}

bool ear_dictation_melody_correct(const ear_dictation_result_t *res) {
    for (uint8_t i = 0; i < res->count; i++) {
        if (res->notes[i].kind != EAR_DICT_MARK_OK) return false;
    }
    return true;
}

bool ear_dictation_melody_perfect(const ear_dictation_result_t *res) {
    // Exact: every position right and nothing extra sung.
    return ear_dictation_melody_correct(res) && res->extras == 0u;
}
