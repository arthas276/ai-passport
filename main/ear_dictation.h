// ear_dictation.h - [PURE] melody-dictation mode: melody generation and
// octave-tolerant grading (PRD 3.4, v1.4). Pure C: no IDF/LVGL includes so
// host tests compile it directly; sung input arrives as the phrase the
// audio worker transcribes (ear_duet_phrase_t, same segmentation chain as
// the duet mode).
#pragma once
#include "ear_game.h"
#include "ear_duet.h"

// A generated melody: pitch sequence, played at the level's uniform tempo
// (EAR_DICTATION_NOTE_MS[level-1] + EAR_DICTATION_GAP_MS between notes).
typedef struct {
    uint8_t notes[EAR_DICTATION_MAX_NOTES];  // MIDI 48..74 (C3..D5)
    uint8_t count;                           // 3..EAR_DICTATION_MAX_NOTES
} ear_dictation_melody_t;

// One dictation session: EAR_QUESTIONS_PER_SESSION melodies.
typedef struct {
    uint8_t level;     // 1..EAR_LEVEL_MAX (persisted as store dlvl)
    uint8_t index;     // 0-based melody index
    uint8_t score;     // fully-correct melodies so far
    uint32_t seed;     // xorshift32 state (0 -> default seed)
} ear_dictation_session_t;

// Per-position grading mark. notes[] is indexed by melody position, so a
// note the singer added shows up in result.extras instead of a position.
typedef enum {
    EAR_DICT_MARK_OK = 0,  // right pitch (within the level's tolerance)
    EAR_DICT_MARK_MISS,    // wrong pitch, or no note sung for this position
} ear_dictation_mark_t;

// Grading tolerance per level, in cents. The tolerance is what makes the
// level ramp a difficulty ramp: L1 forgives up to a semitone of drift, L3
// is near-exact. Exact MIDI equality was unbeatable for untrained singers
// because the transcriber rounds every frame to the nearest semitone
// (v1.4 tuning).
extern const uint16_t EAR_DICT_TOL_CENTS[EAR_LEVEL_MAX];
// Short UI tag per level: "L1 EASY", "L2 NORMAL", "L3 STRICT".
extern const char *const EAR_DICT_TOL_NAME[EAR_LEVEL_MAX];

// Extra sung notes the match may skip inside the phrase: a hiccup or a
// mis-segmented note must not shift every later position.
#define EAR_DICT_MAX_SKIPS 2u

typedef struct {
    uint8_t target;   // melody MIDI at this position (0 = none)
    uint8_t sung;     // matched sung MIDI after octave fold (0 = none)
    int8_t  cents;    // matched sung cents (0 when nothing matched)
    ear_dictation_mark_t kind;
    int8_t off;       // semitone error when wrong (sung - target)
} ear_dictation_note_t;

typedef struct {
    ear_dictation_note_t notes[EAR_DICTATION_MAX_NOTES];
    uint8_t count;          // melody positions graded (= melody length)
    uint8_t correct_notes;  // EAR_DICT_MARK_OK count
    uint8_t extras;         // sung notes the melody does not have
    bool octave_folded;     // a whole-phrase octave shift was removed
    int8_t octaves;         // folded octaves (negative = sung lower)
} ear_dictation_result_t;

// --- API (ear_dictation.c, pure) ---
// Fill a fresh session. Level clamped to 1..EAR_LEVEL_MAX; seed 0 -> default.
void ear_dictation_session_init(ear_dictation_session_t *s, uint8_t level,
                                uint32_t seed);
// Generate the NEXT melody into *out. Deterministic per seed: never repeats
// the immediately previous note; L1 is triadic (starts on C4, ends on the
// C-major triad, stays inside C4..G4); L2/L3 widen range and leaps.
void ear_dictation_next_melody(ear_dictation_session_t *s,
                               ear_dictation_melody_t *out);
// Grade a sung phrase against the melody, using the level's tolerance
// (EAR_DICT_TOL_CENTS[level-1]). A whole-phrase octave offset is folded away
// (the first note decides) and each note is then matched octave-equivalently
// with that tolerance, so a slightly flat C passes and a semitone error does
// not. Melody positions are matched in order; up to EAR_DICT_MAX_SKIPS sung
// notes per position may be skipped as extras.
void ear_dictation_grade(const ear_dictation_melody_t *target,
                         const ear_duet_phrase_t *sung, uint8_t level,
                         ear_dictation_result_t *res);
// Every melody position has a matching note; extra notes are tolerated.
bool ear_dictation_melody_correct(const ear_dictation_result_t *res);
// Exact match: no MISS and no extras.
bool ear_dictation_melody_perfect(const ear_dictation_result_t *res);
