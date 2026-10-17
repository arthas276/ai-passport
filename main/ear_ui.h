// ear_ui.h - EAR BUDDY screens and key/audio event handling.
#pragma once
#include "ear_game.h"
#include "ear_audio.h"
#include "bsp_button.h"
#include "lvgl.h"
#include "freertos/queue.h"

// Initialize UI state and build the HOME screen. Call once after bsp display
// init, holding bsp_lvgl_lock. app_q receives advance events (see below).
void ear_ui_init(QueueHandle_t app_q);

// Key event from the three buttons (call under bsp_lvgl_lock).
void ear_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// Audio worker event (call under bsp_lvgl_lock).
void ear_ui_handle_audio(ear_audio_event_t *ev);

// Advance event: posted by UI timers as a deferred "next question" trigger.
// Reserved kind so main.c can unwrap it.
#define EAR_UI_EV_ADVANCE 0x100
