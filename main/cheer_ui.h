// cheer_ui.h - single-screen UI for CHEER. Caller holds the LVGL lock.
#pragma once

#include <stdint.h>

// Build the screen and load it (must hold bsp_lvgl_lock).
void cheer_ui_build(void);

// Show card `idx`; hist_pos 0 = live draw, >0 = n-th step back in history.
void cheer_ui_show_card(int idx, int hist_pos, int hist_len);

// Update "PRAISE n" line + spirit palette for the stage of `count`.
void cheer_ui_set_progress(uint32_t count);

// Battery percent label (-1 shows "--%").
void cheer_ui_set_battery(int soc);

// Reset the idle timer / restore full backlight (any key).
void cheer_ui_note_activity(void);
