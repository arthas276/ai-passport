// breath_audio.h - session-clock audio worker for BREATHE.
#pragma once
#include "breath.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Events posted to the app queue by the worker (10 Hz during a session).
typedef enum {
    BREATH_AUDIO_EV_TICK = 0,   // live state snapshot for the UI
    BREATH_AUDIO_EV_DONE,       // session finished (stop + end chime played)
} breath_audio_ev_kind_t;

typedef struct {
    breath_audio_ev_kind_t kind;
    breath_state_t st;          // TICK: live; DONE: final (authoritative)
} breath_audio_event_t;

// Create the worker task. event_q receives breath_audio_event_t.
esp_err_t breath_audio_init(QueueHandle_t event_q);

// Start a session for BREATH_PATTERNS[pattern_idx]. UI contract: START only
// when idle; OK long-press is the only in-session control and sends STOP.
void breath_audio_session_start(uint8_t pattern_idx);

// Stop the running session: silence, end chime, then one EV_DONE.
void breath_audio_session_stop(void);

// Volume 0..100, applied at the next session start (never mid-playback).
void breath_audio_set_volume(uint8_t percent);
