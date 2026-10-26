// Host test for the USB link console parser: ADD/TIME/DEL/LIST syntax,
// validation reuse (names, Base32 incl. short tails), bounds, rejects.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "otp_link.h"

static void ok_add(const char *line, const char *name, size_t secret_len) {
    otp_link_msg_t m;
    const char *err = NULL;
    assert(otp_link_parse(line, &m, &err));
    assert(m.cmd == OTP_LINK_CMD_ADD);
    assert(strcmp(m.account.name, name) == 0);
    assert(m.account.secret_len == secret_len);
}

int main(void)
{
    otp_link_msg_t m;
    const char *err = NULL;

    // ADD: canonical 16-char secret decodes to 10 bytes (RFC 6238 vector key).
    ok_add("ADD SITE GEZDGNBVGY3TQOJQ", "SITE", 10);
    // Names may contain spaces; the secret is the last token.
    ok_add("ADD MY SITE GEZDGNBVGY3TQOJQ", "MY SITE", 10);
    // Lowercase secret text is accepted by the shared decoder; the NAME
    // charset stays device-identical (uppercase/digits/spaces only).
    ok_add("ADD X gezdgnbvgy3tqojq====", "X", 10);
    // Short Base32 tails (total length mod 8 = 2/4/5/7) still decode:
    // 16-char base (10 bytes) plus 2/4/5/7 tail chars.
    ok_add("ADD T2 JBSWY3DPEHPK3PXPO2", "T2", 11);
    ok_add("ADD T4 JBSWY3DPEHPK3PXPO44Q", "T4", 12);
    ok_add("ADD T5 JBSWY3DPEHPK3PXPO44Q2", "T5", 13);
    ok_add("ADD T7 JBSWY3DPEHPK3PXPO44Q2E2", "T7", 14);
    // CRLF and trailing blanks are trimmed.
    ok_add("ADD SITE GEZDGNBVGY3TQOJQ  \r", "SITE", 10);
    // Case-insensitive command word.
    ok_add("add SITE GEZDGNBVGY3TQOJQ", "SITE", 10);

    // Secret too short / non-Base32 character / bad name characters.
    assert(!otp_link_parse("ADD S JBSW", &m, &err));            // < 10 bytes
    assert(!otp_link_parse("ADD S GEZDGNBVGY3TQOJ1", &m, &err)); // '1' invalid
    assert(!otp_link_parse("ADD bad!name GEZDGNBVGY3TQOJQ", &m, &err));
    assert(!otp_link_parse("ADD onlyname GEZDGNBVGY3TQOJQ", &m, &err)); // lowercase
    assert(!otp_link_parse("ADD ONLYNAME", &m, &err));           // no secret

    // TIME: in-range accepted; garbage and out-of-range rejected.
    assert(otp_link_parse("TIME 1760000000", &m, &err));
    assert(m.cmd == OTP_LINK_CMD_TIME && m.unix_sec == 1760000000ull);
    assert(otp_link_parse("time 1760000000\r", &m, &err));
    assert(!otp_link_parse("TIME 1234567890", &m, &err));        // < 2020
    assert(!otp_link_parse("TIME 99999999999", &m, &err));       // > 2100
    assert(!otp_link_parse("TIME now", &m, &err));
    assert(!otp_link_parse("TIME", &m, &err));

    // DEL: valid name carried; invalid name or missing name rejected.
    assert(otp_link_parse("DEL MY SITE", &m, &err));
    assert(m.cmd == OTP_LINK_CMD_DEL && strcmp(m.account.name, "MY SITE") == 0);
    assert(!otp_link_parse("DEL", &m, &err));
    assert(!otp_link_parse("DEL bad!name", &m, &err));

    // LIST: bare only.
    assert(otp_link_parse("LIST", &m, &err) && m.cmd == OTP_LINK_CMD_LIST);
    assert(otp_link_parse("list\r", &m, &err));
    assert(!otp_link_parse("LIST EXTRA", &m, &err));

    // Unknown commands / empty / oversized lines.
    assert(!otp_link_parse("HELLO", &m, &err));
    assert(!otp_link_parse("", &m, &err));
    assert(!otp_link_parse(NULL, &m, &err));
    char big[OTP_LINK_LINE_MAX + 8];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(!otp_link_parse(big, &m, &err));

    puts("otp_link: ADD/TIME/DEL/LIST parser PASS");
    return 0;
}
