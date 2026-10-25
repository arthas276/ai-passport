// cheer_audio.h - chime worker: serializes all bsp_audio writes.
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Start the worker (bsp_audio_init must already have succeeded).
esp_err_t cheer_audio_init(void);

// Warm 3-note draw chime; with fanfare=true a 5-note stage-up celebration.
// Non-blocking: the sequence plays on the worker task.
void cheer_audio_play_chime(bool fanfare);
