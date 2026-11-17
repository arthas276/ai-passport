// cheer_fav.c - favorite-card list. PURE C (host-testable). v1.3.
#include "cheer_fav.h"

#include <string.h>

bool cheer_fav_has(const cheer_fav_t *f, uint16_t idx) {
    return cheer_fav_index_of(f, idx) >= 0;
}

int cheer_fav_index_of(const cheer_fav_t *f, uint16_t idx) {
    for (uint8_t i = 0; i < f->count; i++) {
        if (f->items[i] == idx) return i;
    }
    return -1;
}

bool cheer_fav_toggle(cheer_fav_t *f, uint16_t idx) {
    int at = cheer_fav_index_of(f, idx);
    if (at >= 0) {
        memmove(&f->items[at], &f->items[at + 1],
                (size_t)(f->count - at - 1) * sizeof(f->items[0]));
        f->count--;
        return false;
    }
    if (f->count >= CHEER_FAV_CAP) return false;
    f->items[f->count++] = idx;
    return true;
}

size_t cheer_fav_serialize(const cheer_fav_t *f, uint8_t *out, size_t cap) {
    size_t n = (size_t)f->count * 2u;
    if (n > cap) return 0;
    for (uint8_t i = 0; i < f->count; i++) {
        out[2u * i] = (uint8_t)(f->items[i] & 0xFFu);
        out[2u * i + 1u] = (uint8_t)(f->items[i] >> 8);
    }
    return n;
}

bool cheer_fav_deserialize(cheer_fav_t *f, const uint8_t *in, size_t len) {
    if (len % 2u != 0u || len / 2u > CHEER_FAV_CAP) {
        f->count = 0;
        return false;
    }
    f->count = (uint8_t)(len / 2u);
    for (uint8_t i = 0; i < f->count; i++) {
        f->items[i] = (uint16_t)(in[2u * i] | ((uint16_t)in[2u * i + 1u] << 8));
    }
    return true;
}

uint32_t cheer_fav_milestone_at(uint32_t count) {
    return count >= 10u ? (count / 10u) * 10u : 0u;
}
