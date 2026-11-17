// cheer_ui.h - single-screen UI for CHEER. Caller holds the LVGL lock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Build the screen and load it (must hold bsp_lvgl_lock).
void cheer_ui_build(void);

// Show card `idx` (v1.3: `fav` appends a FAV mark to the group chip).
void cheer_ui_show_card(int idx, bool fav);

// Top-center status line (history position, mode markers, hints).
void cheer_ui_set_hist(const char *txt);

// Bottom hint pair (dynamic per mode; NULL keeps the current line).
void cheer_ui_set_hint(const char *top, const char *bottom);

// v1.3 scene tag next to the title: 0 = ALL (tag hidden), 1..CHEER_GRP_COUNT.
void cheer_ui_set_scene(int scene);

// Update "PRAISE n" line + spirit palette for the stage of `count`.
void cheer_ui_set_progress(uint32_t count);

// Battery percent label (-1 shows "--%").
void cheer_ui_set_battery(int soc);

// Reset the idle timer / restore full backlight (any key).
void cheer_ui_note_activity(void);

// Deep-sleep hook: called from the idle timer (esp_timer task, keep it
// bounded) once idle passes the sleep threshold, every second until sleep
// happens or activity resets the timer. The app owns the sleep sequence.
typedef void (*cheer_ui_idle_sleep_cb_t)(void);
void cheer_ui_set_idle_sleep_cb(cheer_ui_idle_sleep_cb_t cb);

// v1.3 scene picker overlay. Rows: 0 ALL, 1..CHEER_GRP_COUNT scenes,
// CHEER_GRP_COUNT+1 favorites browser. Modal: while open the caller routes
// UP/DOWN to picker_move and OK to picker_pick.
void cheer_ui_picker_open(int selected);
void cheer_ui_picker_close(void);
void cheer_ui_picker_move(int delta);
int cheer_ui_picker_pick(void);
bool cheer_ui_picker_is_open(void);
