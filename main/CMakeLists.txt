idf_component_register(
    SRCS "main.c"
         "tuner_ui.c"
         "tuner_audio.c"
         "tuner_pitch.c"
         "tuner_modes.c"
         "tuner_settings.c"
         "tuner_store.c"
         "tuner_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
)
