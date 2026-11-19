// tuner_store.h - NVS persistence for tuner_settings_t. Thin device-side
// wrapper; all logic (defaults, clamps) lives in pure tuner_settings.
#pragma once
#include "tuner_settings.h"
#include "esp_err.h"

// Load settings. *out is always usable afterwards: on any error or missing
// key it holds defaults (stored values are sanitised on the way in).
esp_err_t tuner_store_load(tuner_settings_t *out);
esp_err_t tuner_store_save(const tuner_settings_t *s);
