// breath_ui.h - LVGL screens for BREATHE.
#pragma once
#include "breath_audio.h"
#include "bsp_button.h"

// Build the HOME screen and load persisted settings. Runs under
// bsp_lvgl_lock from app_main.
void breath_ui_init(void);

// All handlers run on the single input task under bsp_lvgl_lock.
void breath_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void breath_ui_handle_audio(const breath_audio_event_t *ev);

// 1 Hz housekeeping: battery refresh + idle dim. Routed through the app
// queue by main.c (no LVGL access from timer callbacks).
void breath_ui_on_second(void);
