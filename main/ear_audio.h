// ear_audio.h - audio worker: playback + mic pitch pipeline.
#pragma once
#include "ear_game.h"
#include "ear_duet.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Events posted to the app queue by the worker.
typedef enum {
    EAR_AUDIO_EV_PLAY_DONE = 0,
    EAR_AUDIO_EV_PITCH,      // live frame: hz/conf/cents valid
    EAR_AUDIO_EV_SING_PASS,
    EAR_AUDIO_EV_SING_FAIL,
    EAR_AUDIO_EV_DUET_PHRASE, // duet capture done: pct = confirmed notes;
                              // read the phrase via ear_audio_duet_phrase()
    EAR_AUDIO_EV_DUET_NOTE,   // duet playback tick: pct = note index + 1
} ear_audio_event_kind_t;

typedef struct {
    ear_audio_event_kind_t kind;
    float hz;       // EV_PITCH: detected f0 (0 = unvoiced)
    float cents;    // EV_PITCH: octave-equivalent deviation from target
    float conf;     // ear_pitch_detect confidence
    uint8_t pct;    // sing attempt progress 0..100 (time or hold)
    int8_t oct;     // EV_PITCH: folded octaves (0 = same octave, -1 = low)
} ear_audio_event_t;

// Create the worker task. event_q receives ear_audio_event_t.
esp_err_t ear_audio_init(QueueHandle_t event_q);

// ---- playback requests (queued, non-blocking) ----
// dir selects the interval presentation (v1.6): UP/DOWN = two notes in
// turn, HARM = both together. Chords ignore it.
void ear_audio_play_question(uint8_t root_midi, ear_kind_t kind,
                             ear_dir_t dir);
void ear_audio_play_target(uint8_t midi);
void ear_audio_play_cue(int cue_id);   // 0=correct 1=wrong 2=evolve
// ---- sing mode ----
void ear_audio_sing_start(float target_hz, float tol_cents);
void ear_audio_sing_cancel(void);

// ---- duet mode (v1.3) ----
// Capture one phrase (same 16 kHz chain as sing). The transcription lands
// in worker-owned storage and is valid until the next capture starts.
void ear_audio_duet_capture(void);
// Play melody + harmony as one job; posts EAR_AUDIO_EV_DUET_NOTE per note
// (for the pet hop) and PLAY_DONE at the end.
void ear_audio_duet_play(const ear_duet_phrase_t *p,
                         const uint8_t harmony[EAR_DUET_MAX_NOTES]);
// Phrase captured by the last ear_audio_duet_capture() (worker-owned).
const ear_duet_phrase_t *ear_audio_duet_phrase(void);

// ---- dictation mode (v1.4, PRD 3.4) ----
// Play a melody sequence at uniform tempo (note_ms per note, 140 ms
// gap). Posts EAR_AUDIO_EV_DUET_NOTE per note (pct = index + 1, for the
// pet hop) and EAR_AUDIO_EV_PLAY_DONE at the end.
void ear_audio_play_melody(const uint8_t *notes, uint8_t count,
                            uint16_t note_ms);
// Capture a sung-back melody: same live-segmentation chain as duet, so
// the transcription lands in the same worker-owned storage (read it with
// ear_audio_dict_phrase()) and EAR_AUDIO_EV_DUET_PHRASE ends the capture.
// expect_notes = melody length: the capture then keeps listening through a
// thinking pause instead of stopping on the first 0.8 s of silence.
void ear_audio_dict_capture(uint8_t expect_notes);
const ear_duet_phrase_t *ear_audio_dict_phrase(void);
