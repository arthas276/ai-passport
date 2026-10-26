// otp_link.c - USB link console line protocol parser. PURE C.
// Host-testable: no IDF/LVGL includes. Secrets are decoded and validated via
// otp_core (same rules as on-device key entry, including short Base32 tails).
#include "otp_link.h"

#include <strings.h>
#include <stdlib.h>
#include <string.h>

// TIME bounds: 2020-01-01 .. 2100-01-01. Far wider than the RTC drift the
// store can accumulate, narrow enough to catch garbage input.
#define OTP_LINK_TIME_MIN 1577836800ull
#define OTP_LINK_TIME_MAX 4102444800ull

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

/* 名称字符集与设备按键输入一致（大写字母/数字/空格）：联机通道不得创建
 * 设备上无法再现或删除的名字。 */
static bool link_name_ok(const char *s) {
    size_t n = 0;
    if (!s || !*s) return false;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')) {
            return false;
        }
    }
    return n <= OTP_NAME_MAX;
}

bool otp_link_parse(const char *line, otp_link_msg_t *msg, const char **err) {
    *err = "未知指令";
    if (!line) return false;
    memset(msg, 0, sizeof(*msg));

    char buf[OTP_LINK_LINE_MAX];
    size_t len = strlen(line);
    if (len == 0 || len >= sizeof(buf)) {
        *err = len ? "行太长" : "空行";
        return false;
    }
    memcpy(buf, line, len + 1);
    while (len && (buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }

    char *rest = NULL;
    char *sp = strchr(buf, ' ');
    if (sp) {
        *sp = '\0';
        rest = sp + 1;
    }

    if (strcasecmp(buf, "ADD") == 0) {
        if (!rest || !*rest) {
            *err = "用法 ADD 名称 密钥";
            return false;
        }
        // The name may contain spaces; the secret is the last blank-separated
        // token (Base32 never contains spaces).
        char *sec = strrchr(rest, ' ');
        if (!sec) {
            *err = "用法 ADD 名称 密钥";
            return false;
        }
        *sec++ = '\0';
        rtrim(rest);
        if (!*rest) {
            *err = "名称不能空";
            return false;
        }
        if (!link_name_ok(rest)) {
            *err = "名称仅限大写字母数字空格";
            return false;
        }
        otp_status_t st = otp_account_set(&msg->account, rest, sec);
        if (st != OTP_OK) {
            *err = st == OTP_ERR_RANGE ? "密钥长度非法"
                 : st == OTP_ERR_FULL ? "参数超限" : "名称或密钥非法";
            return false;
        }
        msg->cmd = OTP_LINK_CMD_ADD;
        return true;
    }
    if (strcasecmp(buf, "TIME") == 0) {
        if (!rest || !*rest) {
            *err = "用法 TIME unix秒";
            return false;
        }
        char *end = NULL;
        unsigned long long v = strtoull(rest, &end, 10);
        if (!end || *end != '\0' || v < OTP_LINK_TIME_MIN || v > OTP_LINK_TIME_MAX) {
            *err = "时间超出范围";
            return false;
        }
        msg->cmd = OTP_LINK_CMD_TIME;
        msg->unix_sec = (uint64_t)v;
        return true;
    }
    if (strcasecmp(buf, "DEL") == 0) {
        if (!rest || !*rest) {
            *err = "用法 DEL 名称";
            return false;
        }
        rtrim(rest);
        if (!link_name_ok(rest)) {
            *err = "名称仅限大写字母数字空格";
            return false;
        }
        msg->cmd = OTP_LINK_CMD_DEL;
        strncpy(msg->account.name, rest, OTP_NAME_MAX);
        msg->account.name[OTP_NAME_MAX] = '\0';
        return true;
    }
    if (strcasecmp(buf, "LIST") == 0) {
        if (rest && *rest) {
            *err = "LIST 不带参数";
            return false;
        }
        msg->cmd = OTP_LINK_CMD_LIST;
        return true;
    }
    return false;
}
