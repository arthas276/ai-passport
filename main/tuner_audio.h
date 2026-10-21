// tuner_audio.h - audio worker: continuous capture -> pitch -> event queue.
#pragma once
#include "tuner.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Events posted to the app queue by the worker.
typedef enum {
    TUNER_EV_PITCH = 0,    // live frame: hz/conf/midi/cents valid
    TUNER_EV_CUE_DONE,     // lock chime finished
} tuner_event_kind_t;

typedef struct {
    tuner_event_kind_t kind;
    float hz;      // EV_PITCH: median-3 smoothed f0 (0 = unvoiced)
    float conf;    // detector confidence
    int16_t midi;  // EV_PITCH: nearest note (-1 when unvoiced)
    float cents;   // EV_PITCH: deviation from the nearest note
} tuner_event_t;

// Create the worker. event_q receives tuner_event_t.
esp_err_t tuner_audio_init(QueueHandle_t event_q);

// Play the lock chime (queues; capture pauses until TUNER_EV_CUE_DONE).
void tuner_audio_cue(void);

// Play a sustained reference tone for a note (queues; capture pauses
// until the next TUNER_EV_CUE_DONE). v1.2: instrument modes hold UP to
// hear the current target string.
void tuner_audio_tone(int16_t midi, uint16_t ms);
