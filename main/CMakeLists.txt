idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "cheer_cards.c"
         "cheer_tone.c"
         "cheer_audio.c"
         "cheer_store.c"
         "cheer_ui.c"
         "cheer_font.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash
)
