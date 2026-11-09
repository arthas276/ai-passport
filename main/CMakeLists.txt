idf_component_register(
    SRCS "main.c"
         "pwcard_app.c"
         "pw_core.c"
         "pw_store.c"
         "pw_power.c"
         "pw_font_22.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
