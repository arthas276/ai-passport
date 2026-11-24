// main/almanac_pick.c — the deterministic day record and the lucky-date search ([PURE], host-testable).
#include "almanac_pick.h"

#include <string.h>

// Deterministic spread over a table of size n; the slot keeps different tables and slots apart.
static int spread(int jdn, int slot, int n) {
    return (int)(((unsigned)jdn * (unsigned)(7 + slot * 4) + (unsigned)slot * 13u) % (unsigned)n);
}

static bool skipped_by_lines(const int lines[2], int item) {
    return almanac_line_skips_item(lines[0], item) || almanac_line_skips_item(lines[1], item);
}

// Copy-deck rule: the k-th 宜 comes from face (d + 2k) mod 6 and the k-th 忌 from the faces one step
// later; inside a face the item is (⌊d/2⌋ + k) mod 20. An 宜 item that a 彭祖百忌 line of the day
// keeps off the card is replaced by the next item of the same face.
static int pick_item(int jdn, int face, int k, const int lines[2], bool exclude) {
    int index = (int)(((jdn / 2) + k) % ALMANAC_ITEMS_PER_FACE);
    for (int step = 0; step < ALMANAC_ITEMS_PER_FACE; ++step) {
        const int item = face * ALMANAC_ITEMS_PER_FACE + index;
        if (!exclude || !skipped_by_lines(lines, item)) return item;
        index = (index + 1) % ALMANAC_ITEMS_PER_FACE;
    }
    return face * ALMANAC_ITEMS_PER_FACE;
}

static void pick_gods(int jdn, int count, uint32_t hidden, int out[ALMANAC_GODS_PER_SIDE]) {
    int eligible[ALMANAC_UNLUCKY_GOD_COUNT];
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (!((hidden >> i) & 1u)) eligible[n++] = i;
    }
    int a = spread(jdn, 0, n);
    int b = spread(jdn, 1, n);
    if (b == a) b = (b + 1) % n;
    out[0] = eligible[a];
    out[1] = eligible[b];
}

void almanac_day_compute(int jdn, almanac_day_t *out) {
    memset(out, 0, sizeof(*out));
    out->jdn = jdn;
    almanac_solar_to_lunar(jdn, &out->lunar);
    almanac_pillars(jdn, &out->pillars);
    out->weekday = almanac_weekday(jdn);
    out->zhixing = almanac_zhixing(jdn);
    out->chong = almanac_chong(jdn);
    out->sha = almanac_sha(jdn);
    out->tianshen = almanac_tianshen_day(jdn);
    out->huangdao = almanac_god_is_huangdao(out->tianshen);
    out->hour_luck = almanac_hour_luck_mask(jdn);
    out->dirs[ALMANAC_DIR_CAI] = almanac_dir_cai(out->pillars.day_stem);
    out->dirs[ALMANAC_DIR_XI] = almanac_dir_xi(out->pillars.day_stem);
    out->dirs[ALMANAC_DIR_FU] = almanac_dir_fu(out->pillars.day_stem);
    out->dirs[ALMANAC_DIR_YANG_GUI] = almanac_dir_yang_gui(out->pillars.day_stem);

    out->baiji[0] = out->pillars.day_stem;
    out->baiji[1] = 10 + out->pillars.day_branch;
    for (int k = 0; k < ALMANAC_PICKS_PER_SIDE; ++k) {
        out->yi[k] = pick_item(jdn, (jdn + 2 * k) % ALMANAC_FACE_COUNT, k, out->baiji, true);
        out->ji[k] = pick_item(jdn, (jdn + 1 + 2 * k) % ALMANAC_FACE_COUNT, k, out->baiji, false);
    }

    const uint32_t hidden = almanac_hidden_lucky_gods(out->baiji[0]) | almanac_hidden_lucky_gods(out->baiji[1]);
    pick_gods(jdn, ALMANAC_LUCKY_GOD_COUNT, hidden, out->lucky_gods);
    pick_gods(jdn, ALMANAC_UNLUCKY_GOD_COUNT, 0u, out->unlucky_gods);

    out->protocol = spread(jdn, 2, ALMANAC_PROTOCOL_COUNT);
    out->port = spread(jdn, 3, ALMANAC_PORT_COUNT);
    out->tool = spread(jdn, 4, ALMANAC_TOOL_COUNT);
    out->color = ((jdn % ALMANAC_COLOR_COUNT) + ALMANAC_COLOR_COUNT) % ALMANAC_COLOR_COUNT;
    out->number = spread(jdn, 5, ALMANAC_NUMBER_COUNT);
    for (int category = 0; category < ALMANAC_LUCK_CATEGORY_COUNT; ++category) {
        out->luck[category] = 1 + spread(jdn, 6 + category, 5);
    }
}

int almanac_lucky_dates(int start_jdn, int matter, int *out_jdn, int *out_god, int max_out) {
    uint32_t lucky;
    uint32_t avoid;
    uint32_t skip_lines;
    almanac_matter_rule(matter, &lucky, &avoid, &skip_lines);
    int count = 0;
    for (int jdn = start_jdn + 1; jdn <= start_jdn + ALMANAC_LUCKY_DATE_SPAN && count < max_out
                                && count < ALMANAC_LUCKY_DATE_MAX;
         ++jdn) {
        const int god = almanac_zhixing(jdn);
        if (!((lucky >> god) & 1u) || ((avoid >> god) & 1u)) continue;
        const int day = almanac_day_index(jdn);
        if (((skip_lines >> (day % 10)) & 1u) || ((skip_lines >> (10 + day % 12)) & 1u)) continue;
        out_jdn[count] = jdn;
        out_god[count] = god;
        ++count;
    }
    return count;
}
