// ear_store.h - NVS persistence for EAR BUDDY progress.
#pragma once
#include "ear_game.h"

// Persisted progress. Levels clamp to 1..3, vol to 0..100 (PRD 7).
typedef struct {
    uint32_t xp;      // cumulative XP
    uint8_t ilvl;     // INTERVAL level 1..3
    uint8_t clvl;     // CHORD level 1..3
    uint8_t slvl;     // SING level 1..3
    uint8_t vol;      // volume 0..100
    uint32_t answered;   // lifetime correct quiz answers (v1.2)
    uint32_t sing_pass;  // lifetime sing passes (v1.2)
    uint32_t duets;      // lifetime duets performed (v1.3, key "du")
    uint8_t best_combo;  // lifetime best combo (v1.2)
} ear_save_t;

// Load progress; on any failure fills defaults (new pet). Never fails hard.
void ear_store_load(ear_save_t *out);

// Persist. Returns ESP_OK or the NVS error (logged by caller; non-fatal).
void ear_store_save(const ear_save_t *s);
