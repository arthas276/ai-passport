idf_component_register(
    SRCS "main.c"
         "otp_app.c"
         "otp_core.c"
         "otp_store.c"
         "otp_power.c"
         "otp_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 全量搜索校准的下界锚点:固件构建时刻(UTC 秒)。
string(TIMESTAMP OTP_BUILD_UNIX "%s")
target_compile_definitions(${COMPONENT_LIB} PRIVATE OTP_BUILD_UNIX=${OTP_BUILD_UNIX})
