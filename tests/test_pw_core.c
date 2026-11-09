// tests/test_pw_core.c - host tests for the pure derivation logic, the stored
// record validation, and the idle power policy. Compiled by tools/validate.sh
// without ESP-IDF/LVGL. The demo card fixture from tools/pwcard/gen_assets.mjs
// (main/pwcard_card.c) locks the C xoshiro128** implementation to the
// independent generator implementation.
#include "pw_core.h"
#include "pw_power.h"
#include "pwcard_card.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* 408-byte NVS blob with no hidden padding; a mismatch here changes flash
 * layout and would brick reads after an OTA of the struct. */
_Static_assert(sizeof(pw_site_t) == 12u, "pw_site_t must stay 12 bytes");
_Static_assert(sizeof(pw_store_t) == 408u, "pw_store_t must stay 408 bytes");

static const uint8_t DEMO_SEED[16] = {0x50, 0x57, 0x43, 0x41, 0x52, 0x44,
                                      0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00};

static void test_alphabet(void) {
    expect(pw_alphabet_len() == 55u, "alphabet length is 55");
    const char forbidden[] = {'0', '1', 'O', 'o', 'I', 'i', 'l'};
    for (uint32_t k = 0; k < sizeof(forbidden); k++) {
        bool found = false;
        for (uint32_t i = 0; i < pw_alphabet_len(); i++) {
            if (PW_ALPHABET[i] == forbidden[k]) found = true;
        }
        expect(!found, "alphabet drops confusable characters");
    }
    for (uint32_t i = 0; i < pw_alphabet_len(); i++) {
        for (uint32_t j = i + 1u; j < pw_alphabet_len(); j++) {
            expect(PW_ALPHABET[i] != PW_ALPHABET[j],
                   "alphabet characters are distinct");
        }
    }
}

static void test_name_charset(void) {
    expect(pw_name_charset_len() == 39u, "name wheel length is 39");
    bool has_az = true;
    for (char c = 'A'; c <= 'Z'; c++) {
        if (!strchr(PW_NAME_CHARSET, c)) has_az = false;
    }
    expect(has_az, "name wheel covers A-Z");
    expect(strchr(PW_NAME_CHARSET, ' ') == PW_NAME_CHARSET,
           "name wheel starts with space");
    expect(strchr(PW_NAME_CHARSET, 'a') == NULL,
           "name wheel has no lowercase");
}

static void test_rng_vectors(void) {
    pw_rng_t rng;
    pw_rng_seed(&rng, DEMO_SEED);
    expect(rng.s[0] == 0x41435750u && rng.s[1] == 0x01004452u &&
               rng.s[2] == 0u && rng.s[3] == 0u,
           "rng seed words are little-endian");
    expect(pw_rng_next(&rng) == 0x86013512u, "rng vector 1 (vs generator JS)");
    expect(pw_rng_next(&rng) == 0xe52bae20u, "rng vector 2 (vs generator JS)");

    uint8_t zero[16] = {0};
    pw_rng_t rngz;
    pw_rng_seed(&rngz, zero);
    expect((rngz.s[0] | rngz.s[1] | rngz.s[2] | rngz.s[3]) != 0u,
           "all-zero seed is bumped off the fixed point");
}

static void test_card_fixture(void) {
    char card[PW_ROWS_N][PW_COLS_N + 1u];
    pw_card_build(DEMO_SEED, card);
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        char row[PW_COLS_N + 1u];
        snprintf(row, sizeof(row), "%s", PWCARD_ROWS[r]);
        expect(strcmp(card[r], row) == 0,
               "C card equals the generator's demo card");
        expect(strlen(card[r]) == PW_COLS_N, "card rows are 10 chars");
        for (uint32_t c = 0; c < PW_COLS_N; c++) {
            expect(strchr(PW_ALPHABET, card[r][c]) != NULL,
                   "card characters are all in the alphabet");
        }
    }
}

static void card_to_linear(const char card[PW_ROWS_N][PW_COLS_N + 1u],
                            char out[PW_CELLS_N]) {
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        memcpy(out + (r * PW_COLS_N), card[r], PW_COLS_N);
    }
}

static void test_card_diverges(void) {
    char a[PW_ROWS_N][PW_COLS_N + 1u];
    char b[PW_ROWS_N][PW_COLS_N + 1u];
    uint8_t other[16];
    memcpy(other, DEMO_SEED, sizeof(other));
    other[15] ^= 0x01u;
    pw_card_build(DEMO_SEED, a);
    pw_card_build(other, b);
    char la[PW_CELLS_N];
    char lb[PW_CELLS_N];
    card_to_linear(a, la);
    card_to_linear(b, lb);
    uint32_t same = 0;
    for (uint32_t i = 0; i < PW_CELLS_N; i++) {
        if (la[i] == lb[i]) same++;
    }
    expect(same < 40u, "one-bit seed change flips most of the card");

    pw_card_build(DEMO_SEED, b);
    expect(memcmp(a, b, sizeof(a)) == 0, "same seed is deterministic");
}

static void test_password_wrap(void) {
    char pw[PW_LEN_MAX + 1u];
    char card[PW_ROWS_N][PW_COLS_N + 1u];
    pw_card_build(DEMO_SEED, card);

    pw_password(DEMO_SEED, 0u, 0u, PW_LEN_MIN, pw);
    expect(memcmp(pw, card[0], PW_LEN_MIN) == 0,
           "password starts at (row,col) on the same stream");
    expect(pw[PW_LEN_MIN] == '\0', "password is NUL-terminated");

    // 7*10 + 9 = 79; + 12 chars wraps at 80 -> crosses the end of the card
    pw_password(DEMO_SEED, 7u, 9u, PW_LEN_MAX, pw);
    char linear[PW_CELLS_N];
    card_to_linear(card, linear);
    for (uint32_t i = 0; i < PW_LEN_MAX; i++) {
        expect(pw[i] == linear[(79u + i) % PW_CELLS_N],
               "password wraps around the 80-cell stream");
    }

    // every position/length pair only ever yields alphabet characters
    for (uint32_t r = 0; r < PW_ROWS_N; r++) {
        for (uint32_t c = 0; c < PW_COLS_N; c += 3u) {
            for (uint8_t len = PW_LEN_MIN; len <= PW_LEN_MAX; len += 2u) {
                pw_password(DEMO_SEED, (uint8_t)r, (uint8_t)c, len, pw);
                expect(strlen(pw) == len, "password length is exact");
                for (uint32_t i = 0; i < len; i++) {
                    expect(strchr(PW_ALPHABET, pw[i]) != NULL,
                           "password characters are in the alphabet");
                }
            }
        }
    }
}

static void test_seed_hex(void) {
    char hex[2u * PW_SEED_BYTES + 1u];
    pw_seed_hex(DEMO_SEED, hex);
    expect(strcmp(hex, "50574341524400010000000000000000") == 0,
           "seed hex is lowercase and byte-ordered");
    expect(strlen(hex) == 32u, "seed hex is 32 chars");
}

static void test_site_valid(void) {
    pw_site_t site;
    memset(&site, 0, sizeof(site));
    site.row = 0;
    site.col = 0;
    site.len = PW_LEN_MIN;
    expect(pw_site_valid(&site), "minimal site is valid");
    site.row = (uint8_t)PW_ROWS_N;
    expect(!pw_site_valid(&site), "row 8 is rejected");
    site.row = 0;
    site.col = (uint8_t)PW_COLS_N;
    expect(!pw_site_valid(&site), "col 10 is rejected");
    site.col = 0;
    site.len = (uint8_t)(PW_LEN_MIN - 1u);
    expect(!pw_site_valid(&site), "len 3 is rejected");
    site.len = (uint8_t)(PW_LEN_MAX + 1u);
    expect(!pw_site_valid(&site), "len 13 is rejected");
    site.len = PW_LEN_MAX;

    memset(site.name, 'A', sizeof(site.name));  // no NUL anywhere
    expect(!pw_site_valid(&site), "unterminated name is rejected");
    site.name[0] = 'A';
    site.name[1] = '\0';
    expect(pw_site_valid(&site), "short name is valid");
    site.name[0] = 'a';  // lowercase is not on the wheel
    expect(!pw_site_valid(&site), "off-wheel name character is rejected");
    site.name[0] = '-';
    expect(pw_site_valid(&site), "dash name character is valid");
    expect(!pw_site_valid(NULL), "NULL site is rejected");
}

static void test_power(void) {
    pw_power_t power;
    pw_power_reset(&power);
    expect(pw_power_backlight(&power) == PW_BACKLIGHT_ACTIVE,
           "active backlight right after reset");
    pw_power_advance(&power, 5000u);
    expect(pw_power_backlight(&power) == PW_BACKLIGHT_ACTIVE,
           "backlight stays active before 12 s");
    pw_power_advance(&power, 7000u);
    expect(pw_power_backlight(&power) == PW_BACKLIGHT_DIM,
           "backlight dims at 12 s");
    expect(!pw_power_should_sleep(&power), "no sleep before 60 s");
    pw_power_advance(&power, 48000u);
    expect(pw_power_should_sleep(&power), "sleep at 60 s");
    expect(pw_power_backlight(&power) == 0u, "backlight off when sleeping");
    pw_power_advance(&power, 1000u);
    expect(pw_power_should_sleep(&power), "sleeping latches");

    pw_power_activity(&power);
    expect(!pw_power_should_sleep(&power) &&
               pw_power_backlight(&power) == PW_BACKLIGHT_ACTIVE,
           "activity wakes from sleep policy");

    pw_power_advance(&power, 40000u);
    expect(!pw_power_should_sleep(&power), "40 s idle does not sleep");
    pw_power_advance(&power, UINT32_MAX);  // overflow must not wrap back
    expect(pw_power_should_sleep(&power), "overflow saturates into sleep");
}

static void test_name_input(void) {
    expect(PW_NAME_CHARS_N == 39u && PW_NAME_WHEEL_N == 41u,
           "wheel has 39 typing chars plus delete and done");
    expect(pw_name_wheel_char(0u) == 'A' && pw_name_wheel_char(25u) == 'Z' &&
               pw_name_wheel_char(26u) == '0' && pw_name_wheel_char(35u) == '9' &&
               pw_name_wheel_char(36u) == '-' && pw_name_wheel_char(37u) == '.' &&
               pw_name_wheel_char(38u) == ' ',
           "wheel typing order is A-Z 0-9 dash dot space");
    expect(pw_name_wheel_char(PW_NAME_IDX_DELETE) == '\0' &&
               pw_name_wheel_char(PW_NAME_IDX_DONE) == '\0',
           "delete and done entries type nothing");

    pw_name_input_t in;
    pw_name_reset(&in);
    expect(in.len == 0u && in.cursor == 0u && in.name[0] == '\0',
           "reset starts empty at cursor A");
    in.cursor = PW_NAME_IDX_DELETE;
    expect(pw_name_ok(&in) == PW_NAME_DELETED,
           "delete on empty stays visible as a no-op");

    // type GITHUB: G=6 I=8 T=19 H=7 U=20 B=1
    static const uint32_t picks[6] = {6u, 8u, 19u, 7u, 20u, 1u};
    for (uint32_t i = 0; i < 6u; i++) {
        in.cursor = picks[i];
        expect(pw_name_ok(&in) == PW_NAME_TYPED, "typing appends");
    }
    expect(strcmp(in.name, "GITHUB") == 0 && in.len == 6u,
           "typed name is GITHUB");

    in.cursor = PW_NAME_IDX_DELETE;
    expect(pw_name_ok(&in) == PW_NAME_DELETED && in.len == 5u &&
               strcmp(in.name, "GITHU") == 0,
           "delete removes the last character");

    pw_name_reset(&in);
    in.cursor = 0u;  // A
    for (uint32_t i = 0; i < PW_NAME_MAX; i++) {
        expect(pw_name_ok(&in) == PW_NAME_TYPED, "eight chars fit");
    }
    expect(in.len == PW_NAME_MAX, "name reaches 8 chars");
    in.cursor = 1u;  // B
    expect(pw_name_ok(&in) == PW_NAME_FULL && in.len == PW_NAME_MAX,
           "ninth character is refused");

    in.cursor = PW_NAME_IDX_DONE;
    expect(pw_name_ok(&in) == PW_NAME_DONE, "done entry finishes");
}

int main(void) {
    test_alphabet();
    test_name_charset();
    test_rng_vectors();
    test_card_fixture();
    test_card_diverges();
    test_password_wrap();
    test_seed_hex();
    test_site_valid();
    test_name_input();
    test_power();
    if (failures == 0) {
        printf("test_pw_core: all pass\n");
        return 0;
    }
    printf("test_pw_core: %d failure(s)\n", failures);
    return 1;
}
