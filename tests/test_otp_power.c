#include "otp_power.h"

#include <stdio.h>

int main(void) {
    otp_power_t power;
    otp_power_reset(&power);
    otp_power_advance(&power, 19999u);
    if (otp_power_backlight(&power) != OTP_BACKLIGHT_ACTIVE) return 1;
    otp_power_advance(&power, 1u);
    if (otp_power_backlight(&power) != OTP_BACKLIGHT_DIM || otp_power_should_sleep(&power)) return 2;
    otp_power_activity(&power);
    if (otp_power_backlight(&power) != OTP_BACKLIGHT_ACTIVE) return 3;
    otp_power_advance(&power, OTP_SLEEP_AFTER_MS);
    if (!otp_power_should_sleep(&power) || otp_power_backlight(&power) != 0u) return 4;
    puts("OTP power tests: PASS");
    return 0;
}
