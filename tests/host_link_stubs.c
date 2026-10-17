// host_link_stubs.c - no-op definitions for LVGL/ui_pixel symbols that GNU
// ld drops via --gc-sections but lld (llvm-mingw on Windows dev hosts)
// rejects before GC. Semantically inert: the demo runtime tests never call
// these code paths; with GNU ld on CI this object is simply redundant.
#include "demo_stubs/demo_test_stubs.h"

lv_obj_t *lv_obj_create(lv_obj_t *parent) {
    (void)parent;
    return 0;
}

void lv_obj_delete(lv_obj_t *object) { (void)object; }

void lv_screen_load(lv_obj_t *screen) { (void)screen; }

lv_obj_t *ui_pixel_screen_create(const char *title) {
    (void)title;
    return 0;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color) {
    (void)parent; (void)x; (void)y; (void)w; (void)h; (void)color;
    return 0;
}

uint32_t lv_color_hex(uint32_t color) { (void)color; return 0; }
lv_obj_t *lv_label_create(lv_obj_t *parent) { (void)parent; return 0; }
void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o; (void)w; (void)h; }
void lv_obj_set_width(lv_obj_t *o, int w) { (void)o; (void)w; }
void lv_obj_set_style_radius(lv_obj_t *o, int r, int sel) { (void)o; (void)r; (void)sel; }
void lv_obj_set_style_bg_color(lv_obj_t *o, uint32_t c, int sel) { (void)o; (void)c; (void)sel; }
void lv_obj_set_style_border_width(lv_obj_t *o, int w, int sel) { (void)o; (void)w; (void)sel; }
void lv_obj_set_style_text_color(lv_obj_t *o, uint32_t c, int sel) { (void)o; (void)c; (void)sel; }
void lv_obj_set_style_text_align(lv_obj_t *o, int a, int sel) { (void)o; (void)a; (void)sel; }
void lv_obj_center(lv_obj_t *o) { (void)o; }
void lv_obj_align(lv_obj_t *o, int align, int x, int y) { (void)o; (void)align; (void)x; (void)y; }
void lv_label_set_long_mode(lv_obj_t *o, int mode) { (void)o; (void)mode; }
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y) { (void)parent; (void)x; (void)y; return 0; }

int esp_sleep_get_wakeup_cause(void) { return 0; }
void lv_obj_set_style_text_font(lv_obj_t *o, const lv_font_t *f, int sel) { (void)o; (void)f; (void)sel; }
void lv_label_set_text_fmt(lv_obj_t *o, const char *fmt, ...) { (void)o; (void)fmt; }
lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), unsigned period, void *data) { (void)cb; (void)period; (void)data; return 0; }
void lv_timer_delete(lv_timer_t *t) { (void)t; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count) { if (count) *count = 0; return 0; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) { if (count) *count = 0; (void)records; return 0; }
