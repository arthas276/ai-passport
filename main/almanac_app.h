// main/almanac_app.h — almanac application entry points used by main.c.
#pragma once

#include "esp_err.h"

// Starts the application: storage, display, LVGL, keys and the first screen.
esp_err_t almanac_app_start(void);
// Called by main.c for every key event that passed the wake guard.
void almanac_app_button_event(int button, int event);
