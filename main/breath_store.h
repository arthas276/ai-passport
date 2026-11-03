// breath_store.h - NVS persistence for BREATHE (stats + volume).
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t  vol;        // 0..100, default 70
    uint8_t  dur_min;    // 0=FREE / 1 / 3 / 5 minutes, default 0 (v1.2)
    uint8_t  glow;       // 0=BRIGHT / 1=NIGHT / 2=DARK, default 0 (v1.2)
    uint32_t sessions;   // completed sessions
    uint32_t minutes;    // lifetime guided minutes
    uint32_t cycles;     // lifetime completed breath cycles
    uint32_t milestone;  // next lifetime-minute threshold to celebrate (v1.2)
} breath_save_t;

// Load with defaults on first boot / missing keys; failures degrade to
// defaults and log only (never blocks the app).
void breath_store_load(breath_save_t *out);

// Best-effort save; called only outside playback (guide 8.1).
void breath_store_save(const breath_save_t *s);
