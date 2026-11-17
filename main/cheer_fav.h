// cheer_fav.h - favorite-card list, PURE C (no IDF/LVGL). v1.3.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHEER_FAV_CAP 32

typedef struct {
    uint16_t items[CHEER_FAV_CAP];   // card indices, oldest first
    uint8_t count;
} cheer_fav_t;

bool cheer_fav_has(const cheer_fav_t *f, uint16_t idx);
int cheer_fav_index_of(const cheer_fav_t *f, uint16_t idx);   // -1 if absent

// Toggle. Returns the new state (true = now a favorite). Adding to a full
// list fails and returns false without changing anything.
bool cheer_fav_toggle(cheer_fav_t *f, uint16_t idx);

// Little-endian u16 blob for NVS; returns bytes written, 0 on short buffer.
size_t cheer_fav_serialize(const cheer_fav_t *f, uint8_t *out, size_t cap);
// Inverse; on malformed input clears the list and returns false.
bool cheer_fav_deserialize(cheer_fav_t *f, const uint8_t *in, size_t len);

// Highest celebrated multiple of 10 <= count (0 below 10). Milestone events
// fire when this value rises past the stored high-water mark.
uint32_t cheer_fav_milestone_at(uint32_t count);
