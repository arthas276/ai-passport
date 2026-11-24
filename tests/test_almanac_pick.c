// tests/test_almanac_pick.c — host test: the deterministic day record and the lucky-date search.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "almanac_pick.h"

static int failures;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            ++failures; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool yi_text_contains_any(const almanac_day_t *day, const char *const *needles, int count) {
    for (int k = 0; k < ALMANAC_PICKS_PER_SIDE; ++k) {
        const char *text = almanac_item_text(ALMANAC_YI, day->yi[k]);
        for (int i = 0; i < count; ++i) {
            if (strstr(text, needles[i]) != NULL) return true;
        }
    }
    return false;
}

static void test_deck_example_day(void) {
    almanac_day_t day;
    almanac_day_compute(almanac_days_from_civil(2026, 11, 23), &day);
    static const char *const yi[] = {"封冷通道缺口", "给上联口贴标签", "重复问题写文档"};
    static const char *const ji[] = {"磁盘九成还写日志", "关审计日志", "快照只增不删"};
    for (int k = 0; k < ALMANAC_PICKS_PER_SIDE; ++k) {
        CHECK(strcmp(almanac_item_text(ALMANAC_YI, day.yi[k]), yi[k]) == 0, "宜 %d is \"%s\", the deck says \"%s\"",
              k, almanac_item_text(ALMANAC_YI, day.yi[k]), yi[k]);
        CHECK(strcmp(almanac_item_text(ALMANAC_JI, day.ji[k]), ji[k]) == 0, "忌 %d is \"%s\", the deck says \"%s\"",
              k, almanac_item_text(ALMANAC_JI, day.ji[k]), ji[k]);
    }
    CHECK(strcmp(almanac_zhixing_name(day.zhixing), "满") == 0, "2026-11-23 is a 满日");
    CHECK(strcmp(almanac_chong_object(day.chong), "替罪羊") == 0, "2026-11-23 clashes with 替罪羊");
    CHECK(strcmp(almanac_compass(day.sha), "正东") == 0 && strcmp(almanac_sha_place(day.sha), "东列机柜") == 0,
          "2026-11-23 煞东");
    CHECK(strcmp(almanac_tianshen_name(day.tianshen), "玉堂") == 0 && day.huangdao, "2026-11-23 值神玉堂");
    CHECK(strcmp(almanac_baiji_line(day.baiji[0]), "辛不动线，动线必乱") == 0
              && strcmp(almanac_baiji_line(day.baiji[1]), "丑不割接，接则回退") == 0,
          "2026-11-23 carries the 辛 and 丑 lines");
    CHECK(strcmp(almanac_compass(day.dirs[ALMANAC_DIR_CAI]), "正东") == 0
              && strcmp(almanac_compass(day.dirs[ALMANAC_DIR_XI]), "西南") == 0
              && strcmp(almanac_compass(day.dirs[ALMANAC_DIR_FU]), "西北") == 0
              && strcmp(almanac_compass(day.dirs[ALMANAC_DIR_YANG_GUI]), "东北") == 0,
          "2026-11-23 directions");
}

static void test_record_is_deterministic(void) {
    almanac_day_t a;
    almanac_day_t b;
    const int start = almanac_days_from_civil(2026, 11, 23);
    for (int jdn = start; jdn < start + 400; jdn += 7) {
        almanac_day_compute(jdn, &a);
        almanac_day_compute(jdn, &b);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0, "JDN %d computes two different records", jdn);
    }
}

static void test_every_day_is_well_formed(void) {
    const int start = almanac_days_from_civil(2026, 11, 23);
    int bad_days = 0;
    for (int jdn = start; jdn < start + 731; ++jdn) {
        almanac_day_t day;
        almanac_day_compute(jdn, &day);
        bool ok = day.yi[0] / ALMANAC_ITEMS_PER_FACE != day.yi[1] / ALMANAC_ITEMS_PER_FACE
                  && day.yi[1] / ALMANAC_ITEMS_PER_FACE != day.yi[2] / ALMANAC_ITEMS_PER_FACE
                  && day.yi[0] / ALMANAC_ITEMS_PER_FACE != day.yi[2] / ALMANAC_ITEMS_PER_FACE
                  && day.ji[0] / ALMANAC_ITEMS_PER_FACE != day.ji[1] / ALMANAC_ITEMS_PER_FACE
                  && day.ji[1] / ALMANAC_ITEMS_PER_FACE != day.ji[2] / ALMANAC_ITEMS_PER_FACE
                  && day.ji[0] / ALMANAC_ITEMS_PER_FACE != day.ji[2] / ALMANAC_ITEMS_PER_FACE
                  && day.lucky_gods[0] != day.lucky_gods[1] && day.unlucky_gods[0] != day.unlucky_gods[1];
        for (int k = 0; k < ALMANAC_PICKS_PER_SIDE && ok; ++k) {
            ok = !almanac_line_skips_item(day.baiji[0], day.yi[k]) && !almanac_line_skips_item(day.baiji[1], day.yi[k]);
        }
        for (int category = 0; category < ALMANAC_LUCK_CATEGORY_COUNT && ok; ++category) {
            ok = day.luck[category] >= 1 && day.luck[category] <= 5;
        }
        ok = ok && day.protocol >= 0 && day.protocol < ALMANAC_PROTOCOL_COUNT && day.port >= 0
             && day.port < ALMANAC_PORT_COUNT && day.tool >= 0 && day.tool < ALMANAC_TOOL_COUNT && day.color >= 0
             && day.color < ALMANAC_COLOR_COUNT && day.number >= 0 && day.number < ALMANAC_NUMBER_COUNT
             && day.lucky_gods[0] < ALMANAC_LUCKY_GOD_COUNT && day.lucky_gods[1] < ALMANAC_LUCKY_GOD_COUNT
             && day.unlucky_gods[0] < ALMANAC_UNLUCKY_GOD_COUNT && day.unlucky_gods[1] < ALMANAC_UNLUCKY_GOD_COUNT;
        if (!ok) {
            ++bad_days;
            if (bad_days <= 3) printf("  malformed record on JDN %d\n", jdn);
        }
    }
    CHECK(bad_days == 0, "%d malformed day records in 731 days", bad_days);
}

// Words a 彭祖百忌 line must keep off the same card's 宜 list, one list per line (NULL = none).
static const char *const k_stem_words[10][7] = {
    {"上新", "上架", "新机", NULL}, {"升", NULL}, {NULL}, {"刷", "固件", NULL}, {"搬迁", "搬机", NULL},
    {"扩", NULL}, {"删库", NULL}, {"理线", "线缆", "水晶头", "跳线", "电源线", "清理闲置链路", NULL}, {"换址", "改地址", NULL},
    {NULL},
};
static const char *const k_branch_words[12][4] = {
    {"重启", NULL}, {"割接", NULL}, {"巡机房", "巡检", NULL}, {"跳线", "理线", "水晶头", NULL}, {"弱口令", "出厂口令", NULL},
    {"报预算", "复盘", "汇报", NULL}, {"断电", NULL}, {"过策略", "放通", NULL}, {"答应", NULL}, {"发版", "发包", NULL},
    {"看群", "盯群", NULL}, {"补丁", NULL},
};

static int word_count(const char *const *words) {
    int n = 0;
    while (words[n] != NULL) ++n;
    return n;
}

static void test_baiji_keeps_its_action_off_the_card(void) {
    const int start = almanac_days_from_civil(2026, 11, 23);
    int clashes = 0;
    for (int jdn = start; jdn < start + 731; ++jdn) {
        almanac_day_t day;
        almanac_day_compute(jdn, &day);
        const char *const *stem_words = k_stem_words[day.pillars.day_stem];
        const char *const *branch_words = k_branch_words[day.pillars.day_branch];
        const bool stem_clash =
            stem_words[0] != NULL && yi_text_contains_any(&day, stem_words, word_count(stem_words));
        const bool branch_clash =
            branch_words[0] != NULL && yi_text_contains_any(&day, branch_words, word_count(branch_words));
        if (stem_clash || branch_clash) {
            ++clashes;
            if (clashes <= 6) {
                almanac_ymd_t ymd;
                almanac_civil_from_days(jdn, &ymd);
                printf("  %d-%02d-%02d %s%s日: 宜 %s、%s、%s (line \"%s\")\n", ymd.year, ymd.month, ymd.day,
                       almanac_stem(day.pillars.day_stem), almanac_branch(day.pillars.day_branch),
                       almanac_item_text(ALMANAC_YI, day.yi[0]), almanac_item_text(ALMANAC_YI, day.yi[1]),
                       almanac_item_text(ALMANAC_YI, day.yi[2]),
                       almanac_baiji_line(day.baiji[stem_clash ? 0 : 1]));
            }
        }
    }
    CHECK(clashes == 0, "%d days show a 宜 item that the day's 彭祖百忌 forbids", clashes);
}

static void test_no_item_starves(void) {
    const int start = almanac_days_from_civil(2026, 11, 23);
    int counts[ALMANAC_ITEM_COUNT * 2] = {0};
    for (int jdn = start; jdn < start + 8 * 365; ++jdn) {
        almanac_day_t day;
        almanac_day_compute(jdn, &day);
        for (int k = 0; k < ALMANAC_PICKS_PER_SIDE; ++k) {
            ++counts[day.yi[k]];
            ++counts[ALMANAC_ITEM_COUNT + day.ji[k]];
        }
    }
    for (int item = 0; item < ALMANAC_ITEM_COUNT * 2; ++item) {
        CHECK(counts[item] >= 10, "item %d (%s) appears only %d times in 8 years", item,
              almanac_item_text(item < ALMANAC_ITEM_COUNT ? ALMANAC_YI : ALMANAC_JI, item % ALMANAC_ITEM_COUNT),
              counts[item]);
    }
}

static void test_hidden_lucky_god_stays_hidden(void) {
    int change_god = -1;
    for (int i = 0; i < ALMANAC_LUCKY_GOD_COUNT; ++i) {
        if (strcmp(almanac_god_lucky(i), "变更一次过") == 0) change_god = i;
    }
    CHECK(change_god >= 0, "变更一次过 is a 吉神");
    if (change_god < 0) return;
    const int start = almanac_days_from_civil(2026, 11, 23);
    int shown = 0;
    for (int jdn = start; jdn < start + 731; ++jdn) {
        almanac_day_t day;
        almanac_day_compute(jdn, &day);
        const bool restricted = day.pillars.day_stem == 1 || day.pillars.day_branch == 1 || day.pillars.day_branch == 9;
        if (restricted && (day.lucky_gods[0] == change_god || day.lucky_gods[1] == change_god)) ++shown;
    }
    CHECK(shown == 0, "变更一次过 is shown on %d 乙, 丑 or 酉 days", shown);
}

static void test_lucky_dates(void) {
    const int start = almanac_days_from_civil(2026, 11, 23);
    int cutover = -1;
    for (int matter = 0; matter < ALMANAC_MATTER_COUNT; ++matter) {
        if (strcmp(almanac_matter(matter), "割接") == 0) cutover = matter;
    }
    CHECK(cutover >= 0, "割接 is a 择吉日 matter");
    if (cutover < 0) return;

    int days[ALMANAC_LUCKY_DATE_MAX];
    int gods[COUNT_OF(days)];
    const int n = almanac_lucky_dates(start, cutover, days, gods, COUNT_OF(days));
    CHECK(n == 3, "割接 from 2026-11-23 yields %d lucky dates, the deck lists three", n);
    if (n == 3) {
        almanac_ymd_t first;
        almanac_ymd_t second;
        almanac_ymd_t third;
        almanac_civil_from_days(days[0], &first);
        almanac_civil_from_days(days[1], &second);
        almanac_civil_from_days(days[2], &third);
        CHECK(first.month == 11 && first.day == 27 && gods[0] == 6, "first 割接 date is 11-27 破日");
        CHECK(second.month == 11 && second.day == 29 && gods[1] == 8, "second 割接 date is 11-29 成日");
        CHECK(third.month == 12 && third.day == 10 && gods[2] == 6, "third 割接 date is 12-10 破日");
    }

    int empty_windows = 0;
    int wrong_days = 0;
    for (int matter = 0; matter < ALMANAC_MATTER_COUNT; ++matter) {
        uint32_t lucky;
        uint32_t avoid;
        uint32_t skip;
        almanac_matter_rule(matter, &lucky, &avoid, &skip);
        for (int s = start; s < start + 731; ++s) {
            const int found = almanac_lucky_dates(s, matter, days, gods, 1);
            if (found < 1) ++empty_windows;
            for (int i = 0; i < found; ++i) {
                const int day = almanac_day_index(days[i]);
                if (days[i] <= s || days[i] > s + ALMANAC_LUCKY_DATE_SPAN || !((lucky >> gods[i]) & 1u)
                    || ((avoid >> gods[i]) & 1u) || ((skip >> (day % 10)) & 1u)
                    || ((skip >> (10 + day % 12)) & 1u) || gods[i] != almanac_zhixing(days[i])) {
                    ++wrong_days;
                }
            }
        }
    }
    CHECK(empty_windows == 0, "%d start days leave a matter without a lucky date in 30 days", empty_windows);
    CHECK(wrong_days == 0, "%d returned lucky dates break their own rule", wrong_days);

    // The app lets any supported day be "today", so the 30-day window must never come up empty
    // anywhere in the supported range; a zero would leave the result screen with no lines.
    int empty_anywhere = 0;
    for (int s = almanac_supported_min_day(); s <= almanac_supported_max_day(); s += 7) {
        for (int matter = 0; matter < ALMANAC_MATTER_COUNT; ++matter) {
            if (almanac_lucky_dates(s, matter, days, gods, 1) < 1) ++empty_anywhere;
        }
    }
    CHECK(empty_anywhere == 0, "%d start days in 1900-2100 leave a matter without a lucky date",
          empty_anywhere);
}

int main(void) {
    test_deck_example_day();
    test_record_is_deterministic();
    test_every_day_is_well_formed();
    test_baiji_keeps_its_action_off_the_card();
    test_no_item_starves();
    test_hidden_lucky_god_stays_hidden();
    test_lucky_dates();
    if (failures == 0) {
        printf("PASS test_almanac_pick\n");
    } else {
        printf("FAIL test_almanac_pick: %d failing checks\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
