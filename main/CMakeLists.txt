idf_component_register(
    SRCS "main.c"
         "idiom_app.c"
         "idiom_core.c"
         "idiom_power.c"
         "idiom_store.c"
         "idiom_words.c"
         "idiom_font_32.c"
         "idiom_font_22.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
