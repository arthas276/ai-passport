// main/pw_core.c - see pw_core.h.
#include "pw_core.h"

#include <string.h>

const char PW_ALPHABET[] =
    "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";

uint32_t pw_alphabet_len(void) {
    return (uint32_t)(sizeof(PW_ALPHABET) - 1u);
}

const char PW_NAME_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";

uint32_t pw_name_charset_len(void) {
    return (uint32_t)(sizeof(PW_NAME_CHARSET) - 1u);
}

static uint32_t pw_rotl(uint32_t x, unsigned k) {
    // xoshiro128** 参考实现以 rotl(s[3], 45) 调用（等价旋转 13）。把 k 归一到
    // 0..31，消除 C 标准下 k>=32 的未定义移位（zig cc 陷阱，RISC-V/gcc 靠硬件
    // 截断）。输出与设备端已发布固件逐位一致。
    k &= 31u;
    return (x << k) | (x >> ((32u - k) & 31u));
}

void pw_rng_seed(pw_rng_t *rng, const uint8_t seed[PW_SEED_BYTES]) {
    uint32_t w[4];
    for (unsigned i = 0; i < 4u; i++) {
        const uint8_t *p = seed + (i * 4u);
        w[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    if ((w[0] | w[1] | w[2] | w[3]) == 0u) {
        w[0] = 1u;  // all-zero is xoshiro's fixed point; bump it
    }
    memcpy(rng->s, w, sizeof(w));
}

uint32_t pw_rng_next(pw_rng_t *rng) {
    uint32_t *s = rng->s;
    const uint32_t result = pw_rotl(s[1] * 5u, 7u) * 9u;
    const uint32_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = pw_rotl(s[3], 45);
    return result;
}

static void pw_stream(const uint8_t seed[PW_SEED_BYTES], char out[PW_CELLS_N]) {
    pw_rng_t rng;
    const uint32_t n = pw_alphabet_len();
    pw_rng_seed(&rng, seed);
    for (uint32_t i = 0; i < PW_CELLS_N; i++) {
        out[i] = PW_ALPHABET[pw_rng_next(&rng) % n];
    }
}

void pw_card_build(const uint8_t seed[PW_SEED_BYTES],
                   char out[PW_ROWS_N][PW_COLS_N + 1u]) {
    char linear[PW_CELLS_N];
    pw_stream(seed, linear);
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        memcpy(out[r], linear + (r * PW_COLS_N), PW_COLS_N);
        out[r][PW_COLS_N] = '\0';
    }
}

void pw_password(const uint8_t seed[PW_SEED_BYTES], uint8_t row, uint8_t col,
                 uint8_t len, char out[PW_LEN_MAX + 1u]) {
    char linear[PW_CELLS_N];
    const uint32_t pos = ((uint32_t)row * PW_COLS_N) + col;
    pw_stream(seed, linear);
    for (uint32_t i = 0; i < len; i++) {
        out[i] = linear[(pos + i) % PW_CELLS_N];
    }
    out[len] = '\0';
}

void pw_seed_hex(const uint8_t seed[PW_SEED_BYTES],
                 char out[2u * PW_SEED_BYTES + 1u]) {
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < PW_SEED_BYTES; i++) {
        out[i * 2u] = hex[seed[i] >> 4];
        out[(i * 2u) + 1u] = hex[seed[i] & 0x0Fu];
    }
    out[2u * PW_SEED_BYTES] = '\0';
}

uint8_t pw_seed_crc8(const uint8_t seed[PW_SEED_BYTES]) {
    uint8_t crc = 0u;
    for (uint32_t i = 0; i < PW_SEED_BYTES; i++) {
        crc ^= seed[i];
        for (uint32_t b = 0; b < 8u; b++) {
            crc = (uint8_t)((crc & 0x80u) ? ((crc << 1u) ^ 0x07u)
                                          : (crc << 1u));
        }
    }
    return crc;
}

void pw_pin(const uint8_t seed[PW_SEED_BYTES], uint8_t row, uint8_t col,
            uint8_t len, char out[PW_PIN_LEN_MAX + 1u]) {
    uint8_t tagged[PW_SEED_BYTES];
    memcpy(tagged, seed, PW_SEED_BYTES);
    // domain tag "PIN1": never correlate with the card stream of the same seed
    tagged[0] ^= 0x50u;
    tagged[1] ^= 0x49u;
    tagged[2] ^= 0x4Eu;
    tagged[3] ^= 0x31u;
    pw_rng_t rng;
    pw_rng_seed(&rng, tagged);
    const uint32_t skip = ((uint32_t)row * PW_COLS_N) + col;
    for (uint32_t i = 0; i < skip; i++) (void)pw_rng_next(&rng);
    for (uint32_t i = 0; i < len; i++) {
        out[i] = (char)('0' + (pw_rng_next(&rng) % 10u));
    }
    out[len] = '\0';
}

bool pw_site_valid(const pw_site_t *site) {
    if (!site) return false;
    if (site->row >= PW_ROWS_N || site->col >= PW_COLS_N) return false;
    if (site->mode != PW_MODE_STD && site->mode != PW_MODE_PIN) return false;
    if (site->mode == PW_MODE_PIN) {
        if (site->len < PW_PIN_LEN_MIN || site->len > PW_PIN_LEN_MAX)
            return false;
    } else {
        if (site->len < PW_LEN_MIN || site->len > PW_LEN_MAX) return false;
    }
    bool nul = false;
    const uint32_t wheel = pw_name_charset_len();
    for (uint32_t i = 0; i <= PW_NAME_MAX; i++) {
        const char c = site->name[i];
        if (c == '\0') {
            nul = true;
            break;
        }
        bool found = false;
        for (uint32_t k = 0; k < wheel; k++) {
            if (PW_NAME_CHARSET[k] == c) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return nul;
}

char pw_name_wheel_char(uint32_t idx) {
    if (idx < PW_NAME_CHARS_N) return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-. "[idx];
    return '\0';
}

void pw_name_reset(pw_name_input_t *input) {
    memset(input->name, 0, sizeof(input->name));
    input->len = 0;
    input->cursor = 0;
}

pw_name_result_t pw_name_ok(pw_name_input_t *input) {
    if (input->cursor == PW_NAME_IDX_DELETE) {
        if (input->len == 0) return PW_NAME_DELETED;  // no-op delete stays visible
        input->len--;
        input->name[input->len] = '\0';
        return PW_NAME_DELETED;
    }
    if (input->cursor == PW_NAME_IDX_DONE) return PW_NAME_DONE;
    if (input->len >= PW_NAME_MAX) return PW_NAME_FULL;
    input->name[input->len] = pw_name_wheel_char(input->cursor);
    input->len++;
    input->name[input->len] = '\0';
    return PW_NAME_TYPED;
}

static bool pw_v1_site_valid(const pw_store_v1_t *old, uint32_t i) {
    pw_site_t probe;
    memset(&probe, 0, sizeof(probe));
    probe.row = old->sites[i].row;
    probe.col = old->sites[i].col;
    probe.len = old->sites[i].len;
    probe.mode = PW_MODE_STD;
    memcpy(probe.name, old->sites[i].name, sizeof(probe.name));
    if (probe.len < PW_LEN_MIN || probe.len > 12u) return false;  // v1 cap
    return pw_site_valid(&probe);
}

bool pw_store_migrate_v1(const pw_store_v1_t *old, pw_store_t *out) {
    if (!old || !out) return false;
    if (old->version != 1u) return false;
    if (old->site_count > PW_SITES_MAX) return false;
    for (uint32_t i = 0; i < old->site_count; i++) {
        if (!pw_v1_site_valid(old, i)) return false;
    }
    memset(out, 0, sizeof(*out));
    out->version = PW_STORE_VERSION;
    memcpy(out->seed, old->seed, PW_SEED_BYTES);
    out->site_count = old->site_count;
    out->help_seen = old->help_seen;
    for (uint32_t i = 0; i < old->site_count; i++) {
        pw_site_t *dst = &out->sites[i];
        dst->row = old->sites[i].row;
        dst->col = old->sites[i].col;
        dst->len = old->sites[i].len;
        dst->mode = PW_MODE_STD;
        memcpy(dst->name, old->sites[i].name, sizeof(dst->name));
    }
    return true;
}

char pw_hex_wheel_char(uint32_t idx) {
    if (idx < PW_HEX_CHARS_N) return "0123456789abcdef"[idx];
    return '\0';
}

void pw_hex_reset(pw_hex_input_t *input) {
    memset(input, 0, sizeof(*input));
}

pw_hex_result_t pw_hex_ok(pw_hex_input_t *input) {
    if (input->cursor == PW_HEX_IDX_DELETE) {
        if (input->len == 0) return PW_HEX_DELETED;
        input->len--;
        input->text[input->len] = '\0';
        return PW_HEX_DELETED;
    }
    if (input->cursor == PW_HEX_IDX_DONE) return PW_HEX_DONE;
    if (input->len >= PW_HEX_INPUT_MAX) return PW_HEX_FULL;
    input->text[input->len] = pw_hex_wheel_char(input->cursor);
    input->len++;
    input->text[input->len] = '\0';
    return PW_HEX_TYPED;
}

bool pw_seed_from_hex(const char hex[2u * PW_SEED_BYTES + 1u],
                      uint8_t out[PW_SEED_BYTES]) {
    static const char *digits = "0123456789abcdef";
    if (!hex || !out) return false;
    for (uint32_t i = 0; i < PW_SEED_BYTES; i++) {
        uint8_t byte = 0u;
        for (uint32_t half = 0; half < 2u; half++) {
            const char c = hex[(i * 2u) + half];
            const char *p = strchr(digits, c);
            if (!p || p >= digits + 16u) return false;
            byte = (uint8_t)((byte << 4u) | (uint8_t)(p - digits));
        }
        out[i] = byte;
    }
    return true;
}
