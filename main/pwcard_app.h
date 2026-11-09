// main/pwcard_app.h - application-owned screens and state machine for the
// password card; not the baseline hardware test menu.
#pragma once

#include "pw_core.h"
#include "pw_store.h"

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    PW_SCREEN_CARD = 0,     // S1: the 8x10 derived card grid
    PW_SCREEN_SITES,        // S2: site list
    PW_SCREEN_PASS,         // S3: masked / revealed site password
    PW_SCREEN_WIZARD,       // S4: add-site wizard (row, col, len, name)
    PW_SCREEN_SETTINGS,     // S5: regenerate / seed / clear / help
    PW_SCREEN_CONFIRM,      // double-confirm for regen and clear
    PW_SCREEN_SEED,         // seed hex view
    PW_SCREEN_HELP,         // how-to overlay (first boot + from settings)
    PW_SCREEN_N,
} pw_screen_t;

#define PW_REVEAL_MS 8000u
#define PW_SITES_VISIBLE 8u

typedef struct {
    pw_screen_t screen;
    bool storage_ok;
    pw_store_t store;
    uint8_t cursor_row;                       // CARD
    uint8_t site_cursor;                      // SITES
    uint8_t site_top;                         // SITES scroll window
    uint32_t reveal_ms;                       // PASS: 0 = masked
    uint8_t wiz_step;                         // WIZARD: 0 row 1 col 2 len 3 name
    uint8_t wiz_row, wiz_col, wiz_len;        // WIZARD picks
    pw_name_input_t wiz_input;                // WIZARD name typing wheel
    uint8_t set_cursor;                       // SETTINGS
    uint8_t confirm_what;                     // CONFIRM: 0 regen 1 clear
    bool help_back_settings;                  // HELP return target
} pw_app_t;

esp_err_t pwcard_app_start(void);
void pwcard_app_stop(void);
void pwcard_button_event(int button, int event);
