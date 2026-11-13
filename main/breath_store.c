// breath_store.c - NVS persistence for BREATHE. All failures degrade to
// defaults/log only (pattern follows feature/ear-buddy ear_store.c).
#include "breath_store.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *NS = "breathe";

void breath_store_load(breath_save_t *out) {
    out->vol = 70;
    out->dur_min = 0;
    out->glow = 0;
    out->c_in = 5;
    out->c_hold = 0;
    out->c_out = 5;
    out->c_hold2 = 0;
    out->sessions = 0;
    out->minutes = 0;
    out->cycles = 0;
    out->milestone = 10;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;   // first boot or partition issue: fresh stats
    }
    uint32_t cnt = 0;
    uint8_t v = 0;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) out->vol = v;
    if (nvs_get_u8(h, "du", &v) == ESP_OK &&
        (v == 0u || v == 1u || v == 3u || v == 5u)) out->dur_min = v;
    if (nvs_get_u8(h, "gl", &v) == ESP_OK && v <= 2u) out->glow = v;
    // YOURS: read all four, keep defaults unless every one is in range
    // (a partial/corrupt set falls back to the whole-table default 5+5).
    uint8_t ci = 255, ch = 255, co = 255, c2 = 255;
    bool have = nvs_get_u8(h, "ci", &ci) == ESP_OK &&
                nvs_get_u8(h, "ch", &ch) == ESP_OK &&
                nvs_get_u8(h, "co", &co) == ESP_OK &&
                nvs_get_u8(h, "c2", &c2) == ESP_OK;
    if (have && ci >= 1u && ci <= 10u && ch <= 10u &&
        co >= 1u && co <= 10u && c2 <= 10u) {
        out->c_in = ci;
        out->c_hold = ch;
        out->c_out = co;
        out->c_hold2 = c2;
    }
    if (nvs_get_u32(h, "ssn", &cnt) == ESP_OK) out->sessions = cnt;
    if (nvs_get_u32(h, "min", &cnt) == ESP_OK) out->minutes = cnt;
    if (nvs_get_u32(h, "cyc", &cnt) == ESP_OK) out->cycles = cnt;
    if (nvs_get_u32(h, "ms", &cnt) == ESP_OK && cnt > 0) out->milestone = cnt;
    nvs_close(h);
}

void breath_store_save(const breath_save_t *s) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "vol", s->vol);
    nvs_set_u8(h, "du", s->dur_min);
    nvs_set_u8(h, "gl", s->glow);
    nvs_set_u8(h, "ci", s->c_in);
    nvs_set_u8(h, "ch", s->c_hold);
    nvs_set_u8(h, "co", s->c_out);
    nvs_set_u8(h, "c2", s->c_hold2);
    nvs_set_u32(h, "ssn", s->sessions);
    nvs_set_u32(h, "min", s->minutes);
    nvs_set_u32(h, "cyc", s->cycles);
    nvs_set_u32(h, "ms", s->milestone);
    nvs_commit(h);
    nvs_close(h);
}
