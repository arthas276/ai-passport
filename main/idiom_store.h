// main/idiom_store.h - one versioned NVS blob: streak statistics, the seeded
// answer-order cursor, and the help-seen flag. NVS is ordinary Flash; nothing
// here is a secret.
#pragma once

#include "idiom_core.h"

#include "esp_err.h"
#include <stdint.h>

#define IDIOM_STORE_VERSION 1u

typedef struct {
    uint32_t version;
    idiom_stats_t stats;
    uint32_t order_seed;
    uint16_t order_idx;
    uint8_t help_seen;
} idiom_store_t;

/* Fresh defaults: empty stats, a non-zero starter seed, cursor 0, help unseen.
 * Returns ESP_OK, or an NVS error after which *out still holds the defaults. */
esp_err_t idiom_store_load(idiom_store_t *out);
esp_err_t idiom_store_save(const idiom_store_t *store);
