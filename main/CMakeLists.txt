idf_component_register(
    SRCS "main.c"
         "almanac_app.c"
         "almanac_ui.c"
         "almanac_view.c"
         "almanac_pick.c"
         "almanac_cal.c"
         "almanac_content.c"
         "almanac_tables.c"
         "almanac_clock.c"
         "almanac_power.c"
         "almanac_store.c"
         "almanac_font_16.c"
         "almanac_font_20.c"
         "almanac_font_48.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
