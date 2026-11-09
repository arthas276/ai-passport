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
    return (x << k) | (x >> (32u - k));
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

bool pw_site_valid(const pw_site_t *site) {
    if (!site) return false;
    if (site->row >= PW_ROWS_N || site->col >= PW_COLS_N) return false;
    if (site->len < PW_LEN_MIN || site->len > PW_LEN_MAX) return false;
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
