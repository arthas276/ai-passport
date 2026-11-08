// main/idiom_app.h - application-owned screens and state machine; not the
// baseline hardware test menu.
#pragma once

#include "idiom_core.h"
#include "idiom_store.h"
#include "idiom_words.h"

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    IDIOM_SCREEN_HOME = 0,
    IDIOM_SCREEN_PLAY,
    IDIOM_SCREEN_RESULT,
    IDIOM_SCREEN_STATS,
    IDIOM_SCREEN_HELP,
} idiom_screen_t;

typedef struct {
    idiom_screen_t screen;
    bool storage_ok;
    idiom_store_t store;
    uint16_t target;
    uint16_t rows;
    uint32_t guesses[6][4];
    idiom_mark_t marks[6][4];
    uint16_t cand[IDIOM_COUNT];
    uint16_t cand_count;
    uint16_t wheel;
    bool won;
    uint8_t result_tries;
    bool new_record;
} idiom_app_t;

esp_err_t idiom_app_start(void);
void idiom_app_stop(void);
void idiom_button_event(int button, int event);
