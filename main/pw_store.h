// main/pw_store.h - NVS load/save for the pw_store_t blob defined in the
// pure pw_core.h header. NVS is ordinary Flash; the seed never leaves the
// device.
#pragma once

#include "pw_core.h"

#include "esp_err.h"

/* Fresh defaults: version set, everything else zero (empty card, no sites).
 * A zero seed means "not generated yet"; the app rolls one on first boot.
 * Returns ESP_OK, or an NVS error after which *out still holds the defaults. */
esp_err_t pw_store_load(pw_store_t *out);
esp_err_t pw_store_save(const pw_store_t *store);
