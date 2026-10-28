#include "otp_core.h"

#include <string.h>

static uint32_t rotl32(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32u - bits));
}

static void sha1_block(uint32_t state[5], const uint8_t block[64]) {
    uint32_t words[80];
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4u] << 24) |
                   ((uint32_t)block[i * 4u + 1u] << 16) |
                   ((uint32_t)block[i * 4u + 2u] << 8) |
                   (uint32_t)block[i * 4u + 3u];
    }
    for (unsigned i = 16; i < 80; ++i) {
        words[i] = rotl32(words[i - 3u] ^ words[i - 8u] ^ words[i - 14u] ^ words[i - 16u], 1u);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (unsigned i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20u) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40u) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60u) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t temp = rotl32(a, 5u) + f + e + k + words[i];
        e = d;
        d = c;
        c = rotl32(b, 30u);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t block[64];
    size_t offset = 0;
    while (len - offset >= 64u) {
        sha1_block(state, data + offset);
        offset += 64u;
    }

    size_t tail = len - offset;
    memset(block, 0, sizeof(block));
    memcpy(block, data + offset, tail);
    block[tail] = 0x80u;
    if (tail >= 56u) {
        sha1_block(state, block);
        memset(block, 0, sizeof(block));
    }
    uint64_t bits = (uint64_t)len * 8u;
    for (unsigned i = 0; i < 8; ++i) block[63u - i] = (uint8_t)(bits >> (8u * i));
    sha1_block(state, block);
    for (unsigned i = 0; i < 5; ++i) {
        out[i * 4u] = (uint8_t)(state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)state[i];
    }
}

static void hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *message, size_t message_len,
                      uint8_t out[20]) {
    uint8_t normalized[64];
    memset(normalized, 0, sizeof(normalized));
    if (key_len > sizeof(normalized)) {
        sha1(key, key_len, normalized);
    } else {
        memcpy(normalized, key, key_len);
    }

    uint8_t inner[64 + 8];
    uint8_t outer[64 + 20];
    for (unsigned i = 0; i < 64; ++i) {
        inner[i] = (uint8_t)(normalized[i] ^ 0x36u);
        outer[i] = (uint8_t)(normalized[i] ^ 0x5Cu);
    }
    memcpy(inner + 64, message, message_len);
    sha1(inner, 64u + message_len, outer + 64);
    sha1(outer, sizeof(outer), out);
    memset(normalized, 0, sizeof(normalized));
    memset(inner, 0, sizeof(inner));
    memset(outer, 0, sizeof(outer));
}

static int base32_value(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '2' && ch <= '7') return ch - '2' + 26;
    return -1;
}

otp_status_t otp_base32_decode(const char *text, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!text || !out || !out_len) return OTP_ERR_RANGE;
    uint64_t buffer = 0;
    unsigned bits = 0;
    size_t written = 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor == ' ' || *cursor == '-' || *cursor == '=') continue;
        int value = base32_value(*cursor);
        if (value < 0) return OTP_ERR_FORMAT;
        buffer = (buffer << 5) | (uint64_t)value;
        bits += 5u;
        if (bits >= 8u) {
            bits -= 8u;
            if (written >= out_cap) return OTP_ERR_RANGE;
            out[written++] = (uint8_t)(buffer >> bits);
            buffer &= (bits == 0u) ? 0u : ((1ull << bits) - 1ull);
        }
    }
    /* 标准 Base32 尾组(2/4/5/7 个字符)余 0-4 位,低位丢弃;
     * 余 ≥5 位即非法尾组(1/3/6 个字符)。 */
    if (bits >= 5u) return OTP_ERR_FORMAT;
    if (written < OTP_SECRET_MIN || written > OTP_SECRET_MAX) return OTP_ERR_RANGE;
    *out_len = written;
    return OTP_OK;
}

otp_status_t otp_base32_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    if (!in || !out || in_len < OTP_SECRET_MIN || in_len > OTP_SECRET_MAX) return OTP_ERR_RANGE;
    size_t need = (in_len * 8u + 4u) / 5u + 1u;
    if (out_cap < need) return OTP_ERR_RANGE;
    uint64_t buffer = 0;
    unsigned bits = 0;
    size_t written = 0;
    for (size_t i = 0; i < in_len; ++i) {
        buffer = (buffer << 8) | in[i];
        bits += 8u;
        while (bits >= 5u) {
            bits -= 5u;
            out[written++] = alphabet[(buffer >> bits) & 31u];
        }
    }
    if (bits) out[written++] = alphabet[(buffer << (5u - bits)) & 31u];
    out[written] = '\0';
    return OTP_OK;
}

unsigned otp_period_remaining(uint64_t now_sec, uint32_t period) {
    if (period == 0u) return 0u;
    unsigned used = (unsigned)(now_sec % period);
    return used == 0u ? period : period - used;
}

bool otp_name_valid(const char *name) {
    if (!name || !name[0]) return false;
    size_t len = 0;
    bool visible = false;
    for (const unsigned char *cursor = (const unsigned char *)name; *cursor; ++cursor) {
        if (++len > OTP_NAME_MAX || *cursor < 0x20u || *cursor == 0x7Fu) return false;
        /* 界面用 > 结束输入，账号名本身不允许保留这个控制符。 */
        if (*cursor == '>') return false;
        if (*cursor > 0x20u) visible = true;
    }
    return visible;
}

otp_status_t otp_totp(const uint8_t *secret, size_t secret_len,
                      uint64_t now_sec, uint32_t period, unsigned digits,
                      char code[OTP_DIGITS + 1u], unsigned *remaining_sec) {
    if (!secret || !code || secret_len < OTP_SECRET_MIN || secret_len > OTP_SECRET_MAX) return OTP_ERR_RANGE;
    if (period == 0u || period > 300u || digits != OTP_DIGITS) return OTP_ERR_RANGE;

    uint64_t counter = now_sec / period;
    uint8_t message[8];
    for (unsigned i = 0; i < 8; ++i) message[7u - i] = (uint8_t)(counter >> (8u * i));

    uint8_t mac[20];
    hmac_sha1(secret, secret_len, message, sizeof(message), mac);
    unsigned offset = mac[19] & 0x0Fu;
    uint32_t binary = ((uint32_t)(mac[offset] & 0x7Fu) << 24) |
                      ((uint32_t)mac[offset + 1u] << 16) |
                      ((uint32_t)mac[offset + 2u] << 8) |
                      (uint32_t)mac[offset + 3u];
    binary %= 1000000u;
    for (int i = 5; i >= 0; --i) {
        code[i] = (char)('0' + (binary % 10u));
        binary /= 10u;
    }
    code[6] = '\0';
    if (remaining_sec) *remaining_sec = otp_period_remaining(now_sec, period);
    memset(mac, 0, sizeof(mac));
    return OTP_OK;
}

bool otp_code_equal(const char *left, const char *right) {
    if (!left || !right) return false;
    unsigned diff = 0;
    for (unsigned i = 0; i < OTP_DIGITS; ++i) diff |= (unsigned char)(left[i] ^ right[i]);
    return diff == 0u && left[OTP_DIGITS] == '\0' && right[OTP_DIGITS] == '\0';
}

size_t otp_base32_group(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0u) return 0u;
    size_t written = 0u;
    size_t since_space = 0u;
    for (size_t i = 0u; in[i] != '\0'; ++i) {
        if (written + 1u >= cap) break;
        if (since_space == 4u) {
            out[written++] = ' ';
            since_space = 0u;
            if (written + 1u >= cap) break;
        }
        out[written++] = in[i];
        since_space++;
    }
    out[written] = '\0';
    return written;
}

otp_status_t otp_account_set(otp_account_t *account, const char *name, const char *base32) {
    if (!account || !otp_name_valid(name)) return OTP_ERR_FORMAT;
    otp_account_t next;
    memset(&next, 0, sizeof(next));
    strncpy(next.name, name, OTP_NAME_MAX);
    size_t secret_len = 0;
    otp_status_t status = otp_base32_decode(base32, next.secret, sizeof(next.secret), &secret_len);
    if (status != OTP_OK) return status;
    next.secret_len = (uint8_t)secret_len;
    next.digits = OTP_DIGITS;
    next.period = OTP_PERIOD_SEC;
    *account = next;
    return OTP_OK;
}

void otp_account_clear(otp_account_t *account) {
    if (account) memset(account, 0, sizeof(*account));
}

bool otp_account_used(const otp_account_t *account) {
    return account && account->secret_len >= OTP_SECRET_MIN && account->name[0] != '\0';
}
