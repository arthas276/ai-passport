// breath_store.c - NVS persistence for BREATHE. All failures degrade to
// defaults/log only (pattern follows feature/ear-buddy ear_store.c).
#include "breath_store.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *NS = "breathe";

void breath_store_load(breath_save_t *out) {
    out->vol = 70;
    out->sessions = 0;
    out->minutes = 0;
    out->cycles = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;   // first boot or partition issue: fresh stats
    }
    uint32_t cnt = 0;
    uint8_t v = 0;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) out->vol = v;
    if (nvs_get_u32(h, "ssn", &cnt) == ESP_OK) out->sessions = cnt;
    if (nvs_get_u32(h, "min", &cnt) == ESP_OK) out->minutes = cnt;
    if (nvs_get_u32(h, "cyc", &cnt) == ESP_OK) out->cycles = cnt;
    nvs_close(h);
}

void breath_store_save(const breath_save_t *s) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "vol", s->vol);
    nvs_set_u32(h, "ssn", s->sessions);
    nvs_set_u32(h, "min", s->minutes);
    nvs_set_u32(h, "cyc", s->cycles);
    nvs_commit(h);
    nvs_close(h);
}
