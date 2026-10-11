idf_component_register(
    SRCS "main.c"
         "tuner_ui.c"
         "tuner_audio.c"
         "tuner_pitch.c"
         "tuner_modes.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer
)
