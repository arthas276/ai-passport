idf_component_register(
    SRCS "main.c"
         "breath_engine.c"
         "breath_tone.c"
         "breath_audio.c"
         "breath_store.c"
         "breath_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash esp_timer
)
