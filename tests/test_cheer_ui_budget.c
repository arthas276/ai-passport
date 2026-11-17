// test_cheer_ui_budget.c - pixel-width guard for CHEER's Montserrat 14
// chrome strings (v1.3). ADV_W is frozen from
// managed_components/lvgl__lvgl/src/font/lv_font_montserrat_14.c
// (adv_w in 1/16 px; glyph 1..95 = ASCII 32..126). Budgets mirror the
// anchored regions in cheer_ui.c; a failing string must be shortened or
// the layout re-anchored - never the budget widened silently.
#include <assert.h>
#include <stdio.h>

static const unsigned ADV_W[95] = {
    60, 60, 88, 157, 139, 189, 154, 47, 75, 76, 90, 130,
    51, 86, 51, 79, 149, 83, 129, 128, 150, 129, 138, 134,
    144, 138, 51, 51, 130, 130, 130, 128, 232, 164, 170, 162,
    185, 150, 142, 173, 182, 69, 115, 161, 133, 214, 182, 188,
    162, 188, 163, 139, 131, 177, 159, 252, 151, 145, 147, 75,
    79, 75, 131, 112, 134, 134, 153, 128, 153, 137, 79, 155,
    153, 62, 64, 138, 62, 237, 153, 142, 153, 153, 92, 112,
    93, 152, 125, 201, 124, 125, 117, 79, 67, 79, 130,
};

static int width_q16(const char *s) {
    int q = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        assert(*p >= 32 && *p <= 126);
        q += ADV_W[*p - 32];
    }
    return q;
}

static void check(const char *s, int budget_px) {
    int w = (width_q16(s) + 15) / 16;   // ceil to whole px
    if (w > budget_px) {
        fprintf(stderr, "over budget: \"%s\" %dpx > %dpx\n", s, w,
                budget_px);
        assert(0);
    }
}

int main(void) {
    // Bottom hint column x=96, right margin 4px: 140px.
    check("OK PRAISE", 140);
    check("OKL SCENE", 140);
    check("OK NEW", 140);
    check("OKL FAV", 140);
    check("OKL UNFAV", 140);
    // Top-center status line, centered on the 240px screen: 228px.
    check("UP/DN HISTORY", 228);
    check("-20/20 FAV", 228);
    check("-20/20", 228);
    check("F20/32", 228);
    check("NO FAVS", 228);
    check("FAV 320 SAVED!", 228);
    // Scene tag x=84: 150px before the battery corner.
    check("SCENE: DAILY", 150);
    check("SCENE: WORK", 150);
    check("SCENE: STUDY", 150);
    check("SCENE: BRAVE", 150);
    check("SCENE: NIGHT", 150);
    check("SCENE: OUT", 150);
    // Picker panel interior: 160 - 2*2 border - 12: 148px.
    check("OK PICK OKL BACK", 148);
    const char *rows[] = { "SCENE", "ALL",  "DAILY", "WORK", "STUDY",
                           "BRAVE", "NIGHT", "OUT",  "FAVS" };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        check(rows[i], 148);
    }
    printf("cheer ui budget: chrome strings within budgets\n");
    return 0;
}
