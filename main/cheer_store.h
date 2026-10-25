// cheer_store.h - NVS persistence for CHEER progress.
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t praise;   // lifetime collected praise count (NVS key "cc")
} cheer_save_t;

// Load progress; on any failure fills defaults (fresh spirit). Never fails.
void cheer_store_load(cheer_save_t *out);

// Persist. Errors are logged by the caller; non-fatal.
void cheer_store_save(const cheer_save_t *s);
