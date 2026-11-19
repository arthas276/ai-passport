// tuner_ui.h - single-screen tuner UI.
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bsp_button.h"
#include "tuner_settings.h"

// Boot the screen. *saved carries the persisted settings (already
// sanitised by the caller); mode/transpose take effect here, A4 and volume
// are applied by main.c via the module setters. app_q reserved for future
// deferred events.
void tuner_ui_init(QueueHandle_t app_q, const tuner_settings_t *saved);
// Key handler: must be called under bsp_lvgl_lock.
void tuner_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
// Worker-event handler: must be called under bsp_lvgl_lock.
void tuner_ui_handle_audio(const void *event);
