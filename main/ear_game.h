// ear_game.h - shared types/constants for EAR BUDDY (pocket ear trainer).
// Pure C header: no IDF/LVGL includes so [PURE] modules can compile on host.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Pet stages & XP economy (PRD 4.1)
// ---------------------------------------------------------------------------
// Cumulative XP thresholds to REACH each stage. XP never decreases.
#define EAR_XP_HATCHLING   10u   // Egg -> Hatchling
#define EAR_XP_CHICK       30u   // Hatchling -> Chick
#define EAR_XP_FLEDGLING   80u   // Chick -> Fledgling
#define EAR_XP_SONGBIRD   160u   // Fledgling -> Songbird (final stage)
#define EAR_STAGE_COUNT    5u

typedef enum {
    EAR_STAGE_EGG = 0,
    EAR_STAGE_HATCHLING,
    EAR_STAGE_CHICK,
    EAR_STAGE_FLEDGLING,
    EAR_STAGE_SONGBIRD,
} ear_stage_t;

// XP awards (PRD 4.1): book + persist at the moment of each correct answer.
#define EAR_XP_CORRECT      1u   // per correct quiz answer
#define EAR_XP_PERFECT      3u   // bonus when session ends 10/10
// Note: no per-day caps: no RTC, no date features (PRD 2).
#define EAR_XP_SING_PASS    2u   // per PASSed sing attempt
#define EAR_XP_DUET          3u   // per performed duet round (v1.3)
#define EAR_XP_DICT_CORRECT  1u   // per fully-correct melody, first try (v1.4)
#define EAR_XP_DICT_PERFECT  2u   // extra when the melody was exact (v1.4)
#define EAR_XP_DICT_PARTIAL  1u   // half the melody right, keeps practice going

// ---------------------------------------------------------------------------
// Quiz model
// ---------------------------------------------------------------------------
#define EAR_QUESTIONS_PER_SESSION 10u
#define EAR_LEVEL_MIN 1u
#define EAR_LEVEL_MAX 3u

typedef enum {
    EAR_MODE_INTERVAL = 0,
    EAR_MODE_CHORD,
    EAR_MODE_SING,
    EAR_MODE_COUNT,
} ear_mode_t;

// Answer kinds. First 6 = intervals (ascending semitones), then chords.
// Index order is fixed: ear_model level tables and asset gen depend on it.
typedef enum {
    EAR_KIND_MIN2 = 0, EAR_KIND_MAJ2,
    EAR_KIND_MIN3, EAR_KIND_MAJ3,
    EAR_KIND_P4, EAR_KIND_P5,
    EAR_KIND_DIM, EAR_KIND_MIN, EAR_KIND_MAJ, EAR_KIND_AUG,
    EAR_KIND_MAJ7, EAR_KIND_DOM7,
    EAR_KIND_COUNT
} ear_kind_t;

// Answer kind table. semis = interval semitones (0xFF for chords);
// chord[] = semitone offsets from root; len = chord tone count (0 = interval).
typedef struct {
    const char *label;   // option text shown on screen ("m3", "maj", ...)
    uint8_t semis;       // interval size in semitones (0xFF = chord kind)
    uint8_t len;         // chord tone count (0 = interval)
    uint8_t chord[4];    // chord semitone offsets from root
} ear_kind_info_t;

// Indexed by ear_kind_t; order matches the enum above.
extern const ear_kind_info_t EAR_KINDS[EAR_KIND_COUNT];

// Level option tables: EAR_LEVELS[mode][level-1] = list of ear_kind_t values.
#define EAR_OPTS_MAX 6
extern const uint8_t EAR_LEVELS[EAR_MODE_COUNT][3][EAR_OPTS_MAX];
extern const uint8_t EAR_LEVEL_OPT_COUNT[EAR_MODE_COUNT][3];

// Interval playback direction (PRD 3.1, v1.6). Chord questions ignore it.
// L1 draws UP only; L2 mixes UP/DOWN; L3 adds HARMONIC (both notes at
// once). The answer is the interval itself either way - direction is not
// graded, it only changes how the question sounds.
typedef enum {
    EAR_DIR_UP = 0,   // root first, then root+semis (the pre-v1.6 behavior)
    EAR_DIR_DOWN,     // higher note first, then root
    EAR_DIR_HARM,     // both notes simultaneously (chord mixer path)
    EAR_DIR_COUNT
} ear_dir_t;

// One quiz question.
typedef struct {
    uint8_t root;             // MIDI root note 60..69 (C4..A4)
    ear_kind_t answer;        // correct kind (index into EAR_KINDS)
    ear_dir_t dir;            // interval playback direction (v1.6)
    uint8_t options[EAR_OPTS_MAX];  // presented kinds (may repeat values
                              // across questions, never within one question)
} ear_question_t;

// Quiz session state (10 questions per session).
typedef struct {
    ear_mode_t mode;          // EAR_MODE_INTERVAL or EAR_MODE_CHORD
    uint8_t level;            // 1..3
    uint8_t index;            // 0-based current question
    uint8_t score;            // correct answers so far
    uint8_t combo;            // consecutive correct (display only)
    uint8_t best_combo;       // max combo this session
    uint8_t prev_kind;        // ear_kind_t of previous answer (anti-repeat)
} ear_session_t;

// --- quiz model API (ear_model.c, pure) ---
// Fill a fresh session. mode must be INTERVAL or CHORD. level 1..3.
void ear_model_session_init(ear_session_t *s, ear_mode_t mode, uint8_t level);
// Generate the NEXT question into *q. Draws a root in 60..69 and an answer
// from the level option table; never repeats the previous answer kind, and
// shuffles the option list so the answer position is random.
void ear_model_next_question(ear_session_t *s, ear_question_t *q);
// Grade the user's choice (index into q->options). Returns true when the
// choice is the answer. Updates score/combo/best_combo in *s.
bool ear_model_grade(ear_session_t *s, const ear_question_t *q, uint8_t choice);
// Level adjust at session end: >=8 correct -> +1 (max 3); <=4 -> -1 (min 1).
// Returns the new level; *level_changed set accordingly.
uint8_t auto_level_adjust(uint8_t old_level, uint8_t score, bool *changed);
// Session perfect bonus eligibility.
bool ear_model_perfect(const ear_session_t *s);

// ---------------------------------------------------------------------------
// Dictation (melody echo) mode - v1.4 (PRD 3.4)
// ---------------------------------------------------------------------------
#define EAR_DICTATION_MAX_NOTES 5u     // notes per melody at level 3
#define EAR_DICTATION_MIN_NOTES 2u     // fewer confirmed notes -> TOO SHORT
// Playback tempo per level (ms per note); EAR_DICTATION_GAP_MS between notes.
extern const uint16_t EAR_DICTATION_NOTE_MS[EAR_LEVEL_MAX];
#define EAR_DICTATION_GAP_MS 140u

// --- pet API (ear_pet.c, pure) ---
// Stage from cumulative XP (thresholds in ear_game.h).
ear_stage_t ear_pet_stage(uint32_t xp);
// XP still needed to next stage; 0 when already final stage.
uint32_t ear_pet_xp_to_next(uint32_t xp);
// XP floor of current stage (for progress bar normalization).
uint32_t ear_pet_stage_floor(uint32_t xp);
// XP ceiling of current stage (= next threshold; final stage -> same as floor,
// bar shows full).
uint32_t ear_pet_stage_ceiling(uint32_t xp);

// --- pitch (ear_pitch.c, pure) ---
#define EAR_PITCH_FRAME 1024          // samples per detection frame
#define EAR_PITCH_SR 16000u           // capture rate for pitch detection
#define EAR_PITCH_FMIN 65.0f          // Hz, below low male F0
#define EAR_PITCH_FMAX 600.0f         // C4-A4 targets live well inside this
// Detect F0 of one frame. Returns hz (0 when unvoiced/uncertain) + confidence.
typedef struct { float hz; float conf; } ear_pitch_result_t;
ear_pitch_result_t ear_pitch_detect(const int16_t *pcm, int n);
// Median of the last three raw detections (sing smoothing, PRD 3.2).
float ear_pitch_median3(float a, float b, float c);
// Octave-equivalent cents: wraps into [-600, +600); *octaves = folded count
// (negative = sung below the target octave). v1.1 sing grading.
float ear_pitch_octave_cents(float cents, int8_t *octaves);
// Cents deviation from target. |cents| <= tol defines "in tune".
float ear_pitch_cents(float f0, float target_hz);
// Nearest MIDI note number for a frequency (rounds to nearest semitone).
uint8_t ear_note_from_hz(float hz);
// Note name "C4".."A4" for MIDI 60..69 (static storage, no allocation).
const char *ear_note_name(uint8_t midi);
// Note frequency in Hz from MIDI number.
float ear_note_freq(uint8_t midi);

// --- tone synth (ear_tone.c, pure) ---
#define EAR_TONE_SR 16000u            // quiz playback rate = board-validated
                                      // demo/xiaozhi rate (22050 distorts)
#define EAR_TONE_MAX_VOICES 3         // chord = 3 simultaneous notes
// One synth voice: integer wavetable (the FPU-less ESP32-C3 ran the float
// version at 0.66x real time on device; integer keeps >10x headroom).
// 3 partials share one phase accumulator; integer Q15 envelope.
typedef struct {
    uint32_t phase;    // 1.31 fixed-point sine phase accumulator
    uint32_t step;     // per-sample phase increment (2^32 x hz / SR)
    int32_t gain;      // output scale (amp x EAR_TONE_LEVEL)
    int32_t env;       // Q15 envelope (attack / decay / release)
    int32_t decay;     // Q15 per-sample decay multiplier
    int32_t atk_step;  // Q15 attack increment per sample
    int32_t rel_step;  // Q15 release decrement per sample
    uint32_t t;        // sample index since note start
    uint32_t total;    // note length in samples (envelope lifetime)
} ear_tone_voice_t;
// Arm a voice. amp 0..1; total = note length in samples @ EAR_TONE_SR.
void ear_tone_start(ear_tone_voice_t *v, float hz, float amp, uint32_t total);
// Render n samples into out. Silent after total reached (ends at ~45% env,
// released to zero over the final 8 ms).
void ear_tone_render(ear_tone_voice_t *v, int16_t *out, int n);
// Mix helper: out[i] += in[i] with saturation (chord voices sum).
void ear_tone_mix(int16_t *dst, const int16_t *src, int n);
// Jingle constants: short synthesized cues.
#define EAR_JINGLE_NOTE_MS 90        // per-note duration of feedback cues
#define EAR_JINGLE_GAP_MS 15         // gap between cue notes
// Feedback cue note lists (MIDI numbers, 0-terminated).
extern const uint8_t EAR_CUE_CORRECT[];
extern const uint8_t EAR_CUE_WRONG[];
extern const uint8_t EAR_CUE_EVOLVE[];
