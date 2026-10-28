#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RFC 6238 TOTP core.
 * The functions only operate on supplied bytes and never read a clock, Flash,
 * keypad, or display. That keeps host tests deterministic on a machine without
 * an ESP32-C3 or a battery-backed RTC. */

#define OTP_SECRET_MIN 10u
#define OTP_SECRET_MAX 32u
#define OTP_NAME_MAX 16u
#define OTP_ACCOUNT_MAX 8u
#define OTP_PERIOD_SEC 30u
#define OTP_DIGITS 6u

typedef struct {
    char name[OTP_NAME_MAX + 1u];
    uint8_t secret[OTP_SECRET_MAX];
    uint8_t secret_len;
    uint8_t digits;
    uint8_t period;
} otp_account_t;

typedef enum {
    OTP_OK = 0,
    OTP_ERR_RANGE,
    OTP_ERR_FORMAT,
    OTP_ERR_FULL,
    OTP_ERR_EMPTY,
} otp_status_t;

/* Decode RFC 4648 Base32. Spaces, hyphens, and padding are skipped; lowercase
 * is accepted. The output buffer must hold OTP_SECRET_MAX bytes. */
otp_status_t otp_base32_decode(const char *text, uint8_t *out, size_t out_cap, size_t *out_len);

/* Map bytes back to canonical Base32 for on-device confirmation. */
otp_status_t otp_base32_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);

/* Copy in to out with one space after every four characters so a long Base32
 * secret stays readable on the narrow screen. Returns the number of bytes
 * written excluding the terminating NUL; output is truncated to cap-1. */
size_t otp_base32_group(const char *in, char *out, size_t cap);

/* Six-digit SHA-1 TOTP. now_sec is caller-owned because this board has no RTC;
 * the application supplies system time calibrated against a trusted device. */
otp_status_t otp_totp(const uint8_t *secret, size_t secret_len,
                      uint64_t now_sec, uint32_t period, unsigned digits,
                      char code[OTP_DIGITS + 1u], unsigned *remaining_sec);

bool otp_code_equal(const char *left, const char *right);
unsigned otp_period_remaining(uint64_t now_sec, uint32_t period);
bool otp_name_valid(const char *name);

otp_status_t otp_account_set(otp_account_t *account, const char *name, const char *base32);
void otp_account_clear(otp_account_t *account);
bool otp_account_used(const otp_account_t *account);
