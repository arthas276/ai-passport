#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* BLE (Nordic UART Service) transport for the USB link console protocol.
 *
 * While the PC-link page is open the device advertises as OTP-LINK and
 * accepts the same ADD/TIME/DEL/LIST/EXPORT lines over BLE writes. The link
 * page is the physical presence gate for BOTH transports; on top of that the
 * BLE transport refuses writes until the connection is encrypted. Replies are
 * notified back and also keep flowing over USB. */

esp_err_t otp_ble_start(void);
esp_err_t otp_ble_stop(void);

/* True once a central has subscribed and can receive notifications. */
bool otp_ble_ready(void);

/* Non-blocking: pop one completed console line received over BLE. */
bool otp_ble_pop_line(char *buf, size_t cap);

/* Notify one reply line to the connected phone; no-op when not ready. */
void otp_ble_reply(const char *text);
