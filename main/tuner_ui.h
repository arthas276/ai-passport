// tuner_ui.h - single-screen tuner UI.
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bsp_button.h"

// Boot the screen. app_q reserved for future deferred events.
void tuner_ui_init(QueueHandle_t app_q);
// Key handler: must be called under bsp_lvgl_lock.
void tuner_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
// Worker-event handler: must be called under bsp_lvgl_lock.
void tuner_ui_handle_audio(const void *event);
