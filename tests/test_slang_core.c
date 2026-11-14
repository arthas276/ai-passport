#include "slang_core.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_catalog(void) {
    expect(slang_count() == 197u, "curated catalog size");
    for (uint16_t i = 0; i < slang_count(); ++i) {
        const slang_entry_t *entry = slang_at(i);
        expect(entry && entry->title[0] && entry->pinyin[0] && entry->gloss[0], "complete entry");
        expect(entry->pinyin[0] >= 'a' && entry->pinyin[0] <= 'z', "lowercase initial");
    }
    expect(slang_at(slang_count()) == NULL, "reject out of range");
}

static int utf8_len(const char *s) {
    int n = 0;
    for (; *s; ++s) {
        if (((unsigned char)*s & 0xC0u) != 0x80u) ++n;
    }
    return n;
}

/* 布局契约：单行槽/两行槽的像素预算依赖这些字数上限（见 PRD 版式预算） */
static void test_field_caps(void) {
    for (uint16_t i = 0; i < slang_count(); ++i) {
        const slang_entry_t *entry = slang_at(i);
        expect(utf8_len(entry->title) <= 8, "title fits one 20px line");
        expect(utf8_len(entry->reading) <= 8, "reading fits the meta line");
        expect(utf8_len(entry->pinyin) <= 7, "pinyin fits the meta line");
        expect(utf8_len(entry->gloss) <= 16, "gloss fits two 16px lines");
        expect(utf8_len(entry->short_gloss) <= 6, "short gloss fits one 16px line");
    }
}

static void test_initials_and_search(void) {
    uint16_t found[32];
    uint16_t count = slang_by_initial('Y', found, 32u);
    expect(count > 0u, "Y bucket");
    const slang_entry_t *first = slang_at(found[0]);
    expect(first && first->pinyin[0] == 'y', "Y maps to lowercase");
    expect(slang_by_initial('1', found, 32u) == 0u, "digit is not a letter");

    count = slang_search("yyds", found, 32u);
    expect(count == 1u && strcmp(slang_at(found[0])->title, "YYDS") == 0, "pinyin lookup");
    count = slang_search("永远", found, 32u);
    expect(count >= 1u, "Chinese substring");
    expect(slang_search("", found, 32u) == 0u, "empty query");
    expect(!slang_query_ok("this query is definitely too long"), "query limit");
}

int main(void) {
    test_catalog();
    test_field_caps();
    test_initials_and_search();
    if (failures) {
        fprintf(stderr, "%d slang tests failed\n", failures);
        return 1;
    }
    puts("Slang host tests: PASS");
    return 0;
}
