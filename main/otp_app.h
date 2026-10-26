#pragma once

#include "otp_store.h"

#include "esp_err.h"
#include <stdint.h>

/* Application-owned screens and controls. This is not the baseline hardware
 * test menu: it starts on the code list and never registers a demo entry. */

typedef enum {
    OTP_SCREEN_LIST = 0,
    OTP_SCREEN_CODE,
    OTP_SCREEN_MENU,
    OTP_SCREEN_NAME,
    OTP_SCREEN_SECRET,
    OTP_SCREEN_CLOCK,
    OTP_SCREEN_CONFIRM,
    OTP_SCREEN_ERROR,
} otp_screen_t;

typedef enum {
    OTP_MENU_ADD = 0,
    OTP_MENU_CLOCK,
    OTP_MENU_DELETE,
    OTP_MENU_COUNT,
} otp_menu_item_t;

typedef struct {
    otp_store_t store;
    otp_screen_t screen;
    otp_screen_t return_screen;
    uint8_t selected;
    uint8_t menu_item;
    uint8_t name_len;
    uint8_t secret_len;
    uint8_t cursor;
    char name[OTP_NAME_MAX + 1u];
    char secret[64];
    char pending_code[OTP_DIGITS + 1u];
    char status[48];
    bool battery_ok;
    bool buttons_ok;
    bool storage_ok;
} otp_app_t;

esp_err_t otp_app_start(void);
void otp_app_stop(void);
void otp_button_event(int button, int event);
uint64_t otp_app_now(const otp_app_t *app);
const char *otp_app_status(const otp_app_t *app);
