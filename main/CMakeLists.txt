idf_component_register(
    SRCS "main.c"
         "ear_model.c"
         "ear_pet.c"
         "ear_pitch.c"
         "ear_tone.c"
         "ear_assets.c"
         "ear_store.c"
         "ear_audio.c"
         "ear_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash esp_timer
)
