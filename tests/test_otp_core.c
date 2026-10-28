#include "otp_core.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_rfc_vectors(void) {
    const uint8_t secret[] = "12345678901234567890";
    const struct { uint64_t now; const char *code; } vectors[] = {
        {59ull, "287082"},
        {1111111109ull, "081804"},
        {1111111111ull, "050471"},
        {1234567890ull, "005924"},
        {2000000000ull, "279037"},
        {20000000000ull, "353130"},
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        char code[7];
        unsigned remain = 0;
        expect(otp_totp(secret, sizeof(secret) - 1u, vectors[i].now, 30u, 6u, code, &remain) == OTP_OK,
               "RFC vector status");
        expect(strcmp(code, vectors[i].code) == 0, vectors[i].code);
        expect(remain == 30u - (unsigned)(vectors[i].now % 30u), "remaining seconds");
    }
}

static void test_base32_and_account(void) {
    uint8_t decoded[32];
    size_t len = 0;
    expect(otp_base32_decode("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", decoded, sizeof(decoded), &len) == OTP_OK, "base32");
    expect(len == 20u && memcmp(decoded, "12345678901234567890", 20) == 0, "decoded ASCII");

    char encoded[40];
    expect(otp_base32_encode(decoded, len, encoded, sizeof(encoded)) == OTP_OK, "encode");
    expect(strcmp(encoded, "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ") == 0, "canonical base32");

    otp_account_t account;
    expect(otp_account_set(&account, "mail", "gezd-gnbv gy3t qojq gezd gnbv gy3t qojq") == OTP_OK, "setup");
    char code[7];
    expect(otp_totp(account.secret, account.secret_len, 1111111111ull, 30u, 6u, code, NULL) == OTP_OK,
           "account code");
    expect(strcmp(code, "050471") == 0, "official code from decoded secret");
    expect(otp_code_equal(code, "050471"), "constant-time equality");
    expect(!otp_code_equal(code, "050472"), "reject mismatch");
    expect(otp_account_set(&account, "", encoded) == OTP_ERR_FORMAT, "empty name");
    expect(otp_account_set(&account, "mail", "GEZDGNBVGY3TQOJ1") == OTP_ERR_FORMAT, "bad alphabet");
    expect(otp_base32_decode("IFBEGRCF", decoded, sizeof(decoded), &len) == OTP_ERR_RANGE, "too short");
    expect(!otp_name_valid("MAIL>"), "terminator stays out of the name");
    expect(!otp_name_valid("NAME THAT IS TOO LONG"), "name length limit");
    expect(otp_name_valid("MAIL 1"), "spaces and digits remain valid");

    /* 非整 5 字节的标准密钥长度(16/32 字节,otpauth:// 常见)必须可解码。 */
    expect(otp_base32_decode("GEZDGNBVGY3TQOJQGEZDGNBVGY", decoded, sizeof(decoded), &len) == OTP_OK, "base32 16-byte unpadded");
    expect(len == 16u && memcmp(decoded, "1234567890123456", 16) == 0, "16-byte decode");
    expect(otp_base32_decode("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZA", decoded, sizeof(decoded), &len) == OTP_OK, "base32 32-byte unpadded");
    expect(len == 32u && memcmp(decoded, "12345678901234567890123456789012", 32) == 0, "32-byte decode");
    expect(otp_base32_decode("GEZDGNBVGY3TQOJQGEZDGNBVG", decoded, sizeof(decoded), &len) == OTP_ERR_FORMAT, "1-char tail rejected");
    expect(otp_base32_decode("GEZDGNBVAAA", decoded, sizeof(decoded), &len) == OTP_ERR_FORMAT, "3-char tail rejected");
    expect(otp_base32_decode("GEZDGNBVAAAAAA", decoded, sizeof(decoded), &len) == OTP_ERR_FORMAT, "6-char tail rejected");
}

static void test_period_math(void) {
    expect(otp_period_remaining(30ull, 30u) == 30u, "boundary remaining");
}

static void test_base32_group(void) {
    char out[80];
    expect(otp_base32_group("GEZDGNBVGY3TQOJQ", out, sizeof(out)) == 19u, "group length");
    expect(strcmp(out, "GEZD GNBV GY3T QOJQ") == 0, "group spacing");
    expect(otp_base32_group("ABC", out, sizeof(out)) == 3u && strcmp(out, "ABC") == 0,
           "short secret needs no space");
    expect(otp_base32_group("ABCDE", out, sizeof(out)) == 6u && strcmp(out, "ABCD E") == 0,
           "tail group shorter than four");
    char tiny[3];
    expect(otp_base32_group("GEZDGNBV", tiny, sizeof(tiny)) == 2u && strcmp(tiny, "GE") == 0,
           "truncation respects capacity");
    expect(otp_base32_group("", out, sizeof(out)) == 0u && out[0] == '\0', "empty input");
}

int main(void) {
    test_rfc_vectors();
    test_base32_and_account();
    test_base32_group();
    test_period_math();
    if (failures) {
        fprintf(stderr, "%d OTP tests failed\n", failures);
        return 1;
    }
    puts("OTP host tests: PASS");
    return 0;
}
