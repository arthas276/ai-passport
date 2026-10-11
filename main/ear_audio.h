// ear_audio.h - audio worker: playback + mic pitch pipeline.
#pragma once
#include "ear_game.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Events posted to the app queue by the worker.
typedef enum {
    EAR_AUDIO_EV_PLAY_DONE = 0,
    EAR_AUDIO_EV_PITCH,      // live frame: hz/conf/cents valid
    EAR_AUDIO_EV_SING_PASS,
    EAR_AUDIO_EV_SING_FAIL,
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
void ear_audio_play_question(uint8_t root_midi, ear_kind_t kind);
void ear_audio_play_target(uint8_t midi);
void ear_audio_play_cue(int cue_id);   // 0=correct 1=wrong 2=evolve
// ---- sing mode ----
void ear_audio_sing_start(float target_hz, float tol_cents);
void ear_audio_sing_cancel(void);
