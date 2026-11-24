// main/almanac_store.h — the day the user last confirmed, kept in NVS.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Prepares the NVS partition and must run before load or save.
esp_err_t almanac_store_init(void);
// Returns ESP_ERR_NVS_NOT_FOUND when no day was ever confirmed.
esp_err_t almanac_store_load(int *day, bool *found);
esp_err_t almanac_store_save(int day);
