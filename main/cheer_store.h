// cheer_store.h - NVS persistence for CHEER progress.
#pragma once

#include <stdint.h>

#include "cheer_fav.h"

typedef struct {
    uint32_t praise;    // lifetime collected praise count (NVS key "cc")
    uint8_t scene;      // v1.3 scene pick: 0 = ALL, 1..CHEER_GRP_COUNT ("sc")
    uint32_t fav_hw;    // v1.3 celebrated favorite-milestone high-water ("fm")
    cheer_fav_t fav;    // v1.3 favorite cards, u16 LE blob ("fv")
} cheer_save_t;

// Load progress; on any failure fills defaults (fresh spirit). Never fails.
void cheer_store_load(cheer_save_t *out);

// Persist. Errors are logged by the caller; non-fatal.
void cheer_store_save(const cheer_save_t *s);
