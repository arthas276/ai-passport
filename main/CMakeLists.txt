idf_component_register(
    SRCS "main.c"
         "slang_app.c"
         "slang_core.c"
         "slang_power.c"
         "slang_font_16.c"
         "slang_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer
)
