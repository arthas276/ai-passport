// ear_duet.h - [PURE] duet phrase transcription helpers: live note
// segmentation, key inference, and stage-gated harmony lines (PRD 3.3, v1.3).
// Pure C: no IDF/LVGL includes, so host tests compile it directly.
#pragma once
#include "ear_game.h"   // ear_stage_t + shared constants (pure header)

// ---- capture timing (frames arrive every EAR_DUET_FRAME_MS) ----
#define EAR_DUET_FRAME_MS       64u   // 1024 samples @ 16 kHz
#define EAR_DUET_MAX_FRAMES     62u   // ~4 s capture window
#define EAR_DUET_CONFIRM_FRAMES 3u    // new note stable this long (~192 ms,
                                      // satisfies the >= 190 ms minimum)
#define EAR_DUET_GAP_FRAMES     6u    // unvoiced run closing a note (~384 ms
                                      // >= 350 ms rest)
#define EAR_DUET_STOP_FRAMES    13u   // trailing silence ending capture
                                      // (~832 ms ~ 0.8 s after >= 2 notes)
#define EAR_DUET_WOBBLE_CENTS   70.0f // in-note vibrato tolerance
#define EAR_DUET_MAX_NOTES      12u
#define EAR_DUET_MIN_NOTES      2u    // fewer -> "TOO SHORT", retry
#define EAR_DUET_LONG_STOP_FRAMES 25u  // ~1.6 s of silence ends a capture even
                                       // before the expected note count is
                                       // reached (dictation: a thinking pause
                                       // must not eat the whole window)

// One transcribed note: quantized MIDI pitch, how long it was held, and how far
// the sung pitch drifted from that semitone's ideal frequency. The cents
// value lets the grader tell "slightly flat C" from "B" (v1.4 tuning).
typedef struct {
    uint8_t  midi;    // 21..108
    uint16_t ms;      // confirmed duration, multiple of EAR_DUET_FRAME_MS
    int8_t   cents;   // mean deviation from midi's ideal pitch, -99..+99
} ear_duet_note_t;

// A transcribed phrase.
typedef struct {
    ear_duet_note_t notes[EAR_DUET_MAX_NOTES];
    uint8_t count;    // <= EAR_DUET_MAX_NOTES (extras dropped in order)
} ear_duet_phrase_t;

// ---- live segmentation (feed one smoothed f0 per frame) ----
typedef struct {
    uint8_t  cur_midi;       // open note (valid while active)
    uint16_t cur_ms;         // held duration of the open note
    float    cur_cents;      // summed cents deviation of the open note
    uint16_t cur_frames;     // voiced frames summed into cur_cents
    uint8_t  cand_midi;      // pending note change
    uint8_t  cand_frames;    // consecutive frames backing cand_midi
    uint8_t  gap_frames;     // running unvoiced count
    uint8_t  count;          // notes closed so far (mirrors out->count)
    bool     active;         // a note is open
} ear_duet_seg_t;

void ear_duet_seg_init(ear_duet_seg_t *s);
// Feed one frame (hz = 0 -> unvoiced). Closed notes append to *out; extra
// notes beyond EAR_DUET_MAX_NOTES are dropped. Returns true when the phrase
// should stop early: >= stop_after confirmed notes plus trailing silence, or
// >= EAR_DUET_MIN_NOTES notes plus EAR_DUET_LONG_STOP_FRAMES of silence
// (dictation passes the melody length, so a thinking pause cannot cut the
// tail off the phrase).
bool ear_duet_seg_feed_ex(ear_duet_seg_t *s, float hz, ear_duet_phrase_t *out,
                          uint8_t stop_after);
// Convenience wrapper: stop after EAR_DUET_MIN_NOTES confirmed notes.
bool ear_duet_seg_feed(ear_duet_seg_t *s, float hz, ear_duet_phrase_t *out);
// Close the trailing note at capture end (kept when >= CONFIRM frames).
void ear_duet_seg_finish(ear_duet_seg_t *s, ear_duet_phrase_t *out);

// ---- key inference ----
typedef struct {
    uint8_t tonic;      // 0..11 pitch class (C=0)
    bool    minor;      // false = major
    bool    fallback;   // true = weak margin path (heaviest class, major)
} ear_duet_key_t;

// Duration-weighted pitch-class histogram scored against 24 major/minor
// diatonic templates (see ear_duet.c). Weak margin (leader ahead of the
// runner-up by < 10% of total sung duration), top score <= 0, or fewer than
// 3 distinct classes -> fallback.
void ear_duet_infer_key(const ear_duet_phrase_t *p, ear_duet_key_t *k);

// Key label "C", "C#", "D" ... for a pitch class (static storage).
const char *ear_duet_pc_name(uint8_t pc);

// ---- harmony line ----
// Per-note harmony MIDI for the phrase under key *k, gated by pet stage:
//   EGG/HATCHLING: unison (melody, quieter twin voice)
//   CHICK:         diatonic 3rd below (above when below < C3 / MIDI 48)
//   FLEDGLING:     diatonic 6th below (same mirror rule)
//   SONGBIRD:      per-note 3rd-or-6th picked for the smoothest voice
//                  leading (smallest harmony step; ties prefer the 3rd)
// All voices clamp into MIDI 48..81 (C3..A5); > A5 folds down an octave
// (the > 880 Hz rule), < C3 folds up.
void ear_duet_harmony(const ear_duet_phrase_t *p, const ear_duet_key_t *k,
                      ear_stage_t stage, uint8_t out[EAR_DUET_MAX_NOTES]);
