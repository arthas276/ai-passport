// main/pw_core.h - pure derivation logic for the password card: xoshiro128**
// card stream, wrap-around site passwords, seed hex encoding, site validation.
// No ESP-IDF, no LVGL: host-testable.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PW_SEED_BYTES 16u
#define PW_ROWS_N 8u
#define PW_COLS_N 10u
#define PW_CELLS_N (PW_ROWS_N * PW_COLS_N)
#define PW_LEN_MIN 4u
#define PW_LEN_MAX 16u
#define PW_PIN_LEN_MIN 4u
#define PW_PIN_LEN_MAX 6u
#define PW_NAME_MAX 8u
#define PW_SITES_MAX 32u

/* 55 unambiguous card characters: lowercase without i/l/o, uppercase without
 * I/O, digits without 0/1. log2(55) = 5.78 bit per character. */
extern const char PW_ALPHABET[];
uint32_t pw_alphabet_len(void);

/* Site-name input wheel: space first so an untouched slot stays blank. */
extern const char PW_NAME_CHARSET[];
uint32_t pw_name_charset_len(void);

typedef struct {
    uint32_t s[4];
} pw_rng_t;

/* Little-endian word seeding; an all-zero seed is bumped to a nonzero state
 * (xoshiro's zero state is a fixed point). */
void pw_rng_seed(pw_rng_t *rng, const uint8_t seed[PW_SEED_BYTES]);
uint32_t pw_rng_next(pw_rng_t *rng);

/* Derive the 80-cell stream row-major; rows are NUL-terminated. */
void pw_card_build(const uint8_t seed[PW_SEED_BYTES],
                   char out[PW_ROWS_N][PW_COLS_N + 1u]);

/* Password = len cells starting at (row, col) on the same 80-cell stream,
 * wrapping past the end (row 8 column J continues at row 1 column A).
 * Caller contract: row < PW_ROWS_N, col < PW_COLS_N,
 * PW_LEN_MIN <= len <= PW_LEN_MAX; out is NUL-terminated. */
void pw_password(const uint8_t seed[PW_SEED_BYTES], uint8_t row, uint8_t col,
                 uint8_t len, char out[PW_LEN_MAX + 1u]);

/* 32 lowercase hex characters + NUL. */
void pw_seed_hex(const uint8_t seed[PW_SEED_BYTES],
                 char out[2u * PW_SEED_BYTES + 1u]);

/* CRC-8 (poly 0x07, init 0x00, no reflect) over the 16 seed bytes - the two
 * checksum hex digits shown next to the seed so a hand-copied migration can
 * be verified. '123456789' -> 0xF4 (standard check value). */
uint8_t pw_seed_crc8(const uint8_t seed[PW_SEED_BYTES]);

/* Site kind. STD passwords are runs of cells on the card (v1.0 semantics,
 * byte-identical for pre-v1.2 sites). PINs are 4-6 digits drawn from a
 * domain-separated stream (first seed word XOR 0x50494E31): same seed +
 * position still rebuilds them, but they never appear on the printable
 * card - a stolen wallet card leaks no bank PINs. */
typedef enum {
    PW_MODE_STD = 0,
    PW_MODE_PIN = 1,
} pw_mode_t;

/* PIN = len digits '0'..'9' from the domain-separated stream. Caller
 * contract: row < PW_ROWS_N, col < PW_COLS_N,
 * PW_PIN_LEN_MIN <= len <= PW_PIN_LEN_MAX; out is NUL-terminated. */
void pw_pin(const uint8_t seed[PW_SEED_BYTES], uint8_t row, uint8_t col,
            uint8_t len, char out[PW_PIN_LEN_MAX + 1u]);

/* One stored site. 13 bytes, no padding (v1.2: +mode; v1.0 was 12). */
typedef struct {
    uint8_t row;                  /* 0-based, < PW_ROWS_N */
    uint8_t col;                  /* 0-based, < PW_COLS_N */
    uint8_t len;                  /* STD: PW_LEN_MIN..PW_LEN_MAX,
                                    * PIN: PW_PIN_LEN_MIN..PW_PIN_LEN_MAX */
    uint8_t mode;                 /* pw_mode_t */
    char name[PW_NAME_MAX + 1u];  /* NUL-terminated, wheel charset */
} pw_site_t;

/* Range + name sanity for records read back from NVS. */
bool pw_site_valid(const pw_site_t *site);

/* Name typing wheel: entries 0..PW_NAME_CHARS_N-1 type their character
 * (A-Z, 0-9, '-', '.', space); the last two entries delete and finish.
 * Kept pure so host tests pin the exact typing semantics. */
#define PW_NAME_CHARS_N 39u
#define PW_NAME_IDX_DELETE PW_NAME_CHARS_N
#define PW_NAME_IDX_DONE (PW_NAME_CHARS_N + 1u)
#define PW_NAME_WHEEL_N (PW_NAME_CHARS_N + 2u)

/* Character typed by wheel entry idx, or '\0' for the delete/done entries. */
char pw_name_wheel_char(uint32_t idx);

typedef struct {
    char name[PW_NAME_MAX + 1u];
    uint8_t len;
    uint32_t cursor;  /* wheel entry the cursor sits on */
} pw_name_input_t;

typedef enum {
    PW_NAME_TYPED = 0,  /* character appended */
    PW_NAME_DELETED,    /* last character removed */
    PW_NAME_DONE,       /* done entry picked */
    PW_NAME_FULL,       /* name already PW_NAME_MAX chars; nothing appended */
} pw_name_result_t;

void pw_name_reset(pw_name_input_t *input);
pw_name_result_t pw_name_ok(pw_name_input_t *input);

/* Seed-entry typing wheel: entries 0..15 type '0'..'9' 'a'..'f'; the last
 * two entries delete and finish. 32 slots = 2 * PW_SEED_BYTES. */
#define PW_HEX_CHARS_N 16u
#define PW_HEX_IDX_DELETE PW_HEX_CHARS_N
#define PW_HEX_IDX_DONE (PW_HEX_CHARS_N + 1u)
#define PW_HEX_WHEEL_N (PW_HEX_CHARS_N + 2u)
#define PW_HEX_INPUT_MAX (2u * PW_SEED_BYTES)

char pw_hex_wheel_char(uint32_t idx);

typedef struct {
    char text[PW_HEX_INPUT_MAX + 1u];  /* lowercase hex, NUL-terminated */
    uint8_t len;
    uint32_t cursor;                   /* wheel entry the cursor sits on */
} pw_hex_input_t;

typedef enum {
    PW_HEX_TYPED = 0,
    PW_HEX_DELETED,
    PW_HEX_DONE,
    PW_HEX_FULL,
} pw_hex_result_t;

void pw_hex_reset(pw_hex_input_t *input);
pw_hex_result_t pw_hex_ok(pw_hex_input_t *input);

/* Parse 32 hex chars into bytes; false on any non-hex character. */
bool pw_seed_from_hex(const char hex[2u * PW_SEED_BYTES + 1u],
                      uint8_t out[PW_SEED_BYTES]);

#define PW_STORE_VERSION 2u

/* The whole NVS blob: 440 bytes, no hidden padding (explicit tail). Lives in
 * this pure header so host tests can static-assert the flash layout. */
typedef struct {
    uint32_t version;
    uint8_t seed[PW_SEED_BYTES];
    uint8_t site_count;   /* 0..PW_SITES_MAX, sites[0..site_count-1] valid */
    uint8_t help_seen;    /* 0 = show the help overlay on this boot */
    uint8_t reserved[2];  /* explicit tail: sizeof stays 440, no hidden pad */
    pw_site_t sites[PW_SITES_MAX];
} pw_store_t;

/* v1.0 flash layout (12-byte sites, 408 bytes total) - kept verbatim so the
 * load-time upgrade is a pure, host-testable conversion. */
typedef struct {
    uint32_t version;
    uint8_t seed[PW_SEED_BYTES];
    uint8_t site_count;
    uint8_t help_seen;
    uint8_t reserved[2];
    struct {
        uint8_t row, col, len;
        char name[PW_NAME_MAX + 1u];
    } sites[PW_SITES_MAX];
} pw_store_v1_t;

/* v1 blob -> current store. Every site becomes PW_MODE_STD with its stored
 * row/col/len untouched, so all v1.0 passwords derive bit-identically.
 * Returns false (and leaves *out on defaults) when the old blob is invalid. */
bool pw_store_migrate_v1(const pw_store_v1_t *old, pw_store_t *out);
