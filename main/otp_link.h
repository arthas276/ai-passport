#pragma once

#include "otp_core.h"

#include <stdbool.h>
#include <stdint.h>

/* USB link console line protocol (PURE, host-testable).
 *
 * The device only accepts these lines while the user has opened the 电脑联机
 * page; nothing is accepted on any other screen. Replies never contain
 * secrets - LIST answers with account names only. EXPORT is the deliberate
 * exception: it echoes every account's Base32 secret (one EXPORT <name>
 * <secret> line each, then an OK EXPORT <n> terminator) for backup or
 * migration, and only while the link page is physically open - the same gate
 * that guards ADD. Anyone with USB access can already read NVS (otp_store.h). */

#define OTP_LINK_LINE_MAX 96u

typedef enum {
    OTP_LINK_CMD_NONE = 0,
    OTP_LINK_CMD_ADD,
    OTP_LINK_CMD_TIME,
    OTP_LINK_CMD_DEL,
    OTP_LINK_CMD_LIST,
    OTP_LINK_CMD_EXPORT,
} otp_link_cmd_t;

typedef struct {
    otp_link_cmd_t cmd;
    otp_account_t account;
    uint64_t unix_sec;
} otp_link_msg_t;

/* Parse one line (without the newline). Returns true and fills *msg on
 * success. On failure returns false and points *err at a short static reason
 * suitable for an ERR reply. */
bool otp_link_parse(const char *line, otp_link_msg_t *msg, const char **err);
