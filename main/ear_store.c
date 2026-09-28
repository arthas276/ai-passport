// ear_store.c - NVS persistence. All failures degrade to defaults/log only.
#include "ear_store.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *NS = "earbuddy";

// Clamp helper for loaded values (NVS could hold stale/foreign data).
static uint8_t clamp_lvl(uint8_t v) {
    return v < 1 ? 1 : (v > 3 ? 3 : v);
}

void ear_store_load(ear_save_t *out) {
    out->xp = 0;
    out->ilvl = out->clvl = out->slvl = 1;
    out->vol = 80;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;   // first boot or partition issue: fresh pet
    }
    uint32_t xp = 0;
    uint8_t v = 0;
    if (nvs_get_u32(h, "xp", &xp) == ESP_OK) out->xp = xp;
    if (nvs_get_u8(h, "ilvl", &v) == ESP_OK) out->ilvl = clamp_lvl(v);
    if (nvs_get_u8(h, "clvl", &v) == ESP_OK) out->clvl = clamp_lvl(v);
    if (nvs_get_u8(h, "slvl", &v) == ESP_OK) out->slvl = clamp_lvl(v);
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) out->vol = v;
    nvs_close(h);
}

void ear_store_save(const ear_save_t *s) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "xp", s->xp);
    nvs_set_u8(h, "ilvl", s->ilvl);
    nvs_set_u8(h, "clvl", s->clvl);
    nvs_set_u8(h, "slvl", s->slvl);
    nvs_set_u8(h, "vol", s->vol);
    nvs_commit(h);
    nvs_close(h);
}
