// breath_store.h - NVS persistence for BREATHE (stats + volume).
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t  vol;        // 0..100, default 70
    uint32_t sessions;   // completed sessions
    uint32_t minutes;    // lifetime guided minutes
    uint32_t cycles;     // lifetime completed breath cycles
} breath_save_t;

// Load with defaults on first boot / missing keys; failures degrade to
// defaults and log only (never blocks the app).
void breath_store_load(breath_save_t *out);

// Best-effort save; called only outside playback (guide 8.1).
void breath_store_save(const breath_save_t *s);
