// main/almanac_app.c — almanac state machine: screens, keys, clock, power and deep sleep.
//
// One input task drains a key queue under bsp_lvgl_lock; one 250 ms LVGL timer advances the
// idle-power policy and requests deep sleep. Calendar maths and text live in [PURE] modules
// (almanac_cal / almanac_pick / almanac_view); drawing lives in almanac_ui.c.
#include "almanac_app.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "almanac_cal.h"
#include "almanac_clock.h"
#include "almanac_content.h"
#include "almanac_pick.h"
#include "almanac_power.h"
#include "almanac_store.h"
#include "almanac_strings.h"
#include "almanac_view.h"
#include "almanac_ui.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "almanac";

#define TICK_MS 250u
#define BATTERY_TICKS 20u // one I2C read every 5 s

enum { MENU_CALIBRATE = 0, MENU_TODAY, MENU_PICK, MENU_HELP, MENU_ABOUT };

typedef enum {
    SCR_TODAY = 0,
    SCR_DETAIL,
    SCR_HOURS,
    SCR_DIRS,
    SCR_FORTUNE,
    SCR_MENU,
    SCR_PICK,
    SCR_RESULT,
    SCR_SETUP,
    SCR_CONFIRM,
    SCR_HELP,
    SCR_ABOUT,
} screen_id_t;

typedef struct {
    int button;
    int event;
} almanac_key_t;

static struct {
    screen_id_t screen;
    int view_day; // the day on screen
    int today; // the day the clock says it is
    bool clock_valid;
    int battery_soc;
    int menu_sel;
    int pick_sel;
    int pick_first;
    int result_count;
    int result_day[ALMANAC_LUCKY_DATE_MAX];
    int result_god[ALMANAC_LUCKY_DATE_MAX];
    int setup_step;
    int setup_year, setup_month, setup_day;
    bool setup_from_lost;
    bool stored_day; // a confirmed day was stored before this boot, so a lost clock lost a real date
    screen_id_t back; // where HELP / ABOUT return to
} s_app;

static QueueHandle_t s_keys;
static almanac_power_t s_power;
static uint32_t s_soc_tick;
static volatile bool s_sleep_requested;

static void render(void);

static void battery_refresh(void) {
    const int soc = bsp_battery_soc();
    if (soc != s_app.battery_soc) {
        s_app.battery_soc = soc;
        almanac_ui_battery(soc);
    }
}

// Wall clock in seconds since the Unix epoch; the RTC keeps it running across deep sleep.
static int64_t now_seconds(void) { return (int64_t)time(NULL); }

// __DATE__ ("Mmm dd yyyy") is the day this file was compiled: what a device that never confirmed
// a date is offered first.
static int build_day(void) {
    return almanac_clock_parse_build_date(__DATE__);
}

static void load_day(int jdn) {
    s_app.view_day = jdn;
    if (jdn < almanac_supported_min_day()) s_app.view_day = almanac_supported_min_day();
    if (jdn > almanac_supported_max_day()) s_app.view_day = almanac_supported_max_day();
}

static void pick_move(int delta) {
    s_app.pick_sel += delta;
    if (s_app.pick_sel < 0) s_app.pick_sel = 0;
    if (s_app.pick_sel > ALMANAC_MATTER_COUNT - 1) s_app.pick_sel = ALMANAC_MATTER_COUNT - 1;
    if (s_app.pick_sel < s_app.pick_first) s_app.pick_first = s_app.pick_sel;
    if (s_app.pick_sel >= s_app.pick_first + ALMANAC_PICK_ROWS_VISIBLE) {
        s_app.pick_first = s_app.pick_sel - ALMANAC_PICK_ROWS_VISIBLE + 1;
    }
}

static void menu_move(int delta) {
    s_app.menu_sel += delta;
    if (s_app.menu_sel < 0) s_app.menu_sel = 0;
    if (s_app.menu_sel > ALMANAC_MENU_ROWS - 1) s_app.menu_sel = ALMANAC_MENU_ROWS - 1;
}

static void setup_clamp(void) {
    if (s_app.setup_year < 2020) s_app.setup_year = 2020;
    if (s_app.setup_year > 2100) s_app.setup_year = 2100;
    if (s_app.setup_month < 1) s_app.setup_month = 12;
    if (s_app.setup_month > 12) s_app.setup_month = 1;
    const int dim = almanac_days_in_month(s_app.setup_year, s_app.setup_month);
    if (s_app.setup_day < 1) s_app.setup_day = dim;
    if (s_app.setup_day > dim) s_app.setup_day = 1;
}

// Moves the wizard one step; on the last step it opens the confirmation screen.
static void wizard_step(int delta) {
    if (delta != 0) {
        if (s_app.setup_step == 0) s_app.setup_year += delta;
        else if (s_app.setup_step == 1) s_app.setup_month += delta;
        else s_app.setup_day += delta;
        setup_clamp();
        render();
        return;
    }
    if (s_app.setup_step < 2) {
        s_app.setup_step++;
        render();
        return;
    }
    s_app.screen = SCR_CONFIRM;
    render();
}

static void setup_start(bool date_lost) {
    int last = 0;
    bool found = false;
    (void)almanac_store_load(&last, &found);
    s_app.setup_step = 0;
    s_app.setup_from_lost = date_lost;
    const int offered = almanac_clock_setup_default(found ? last : 0, build_day());
    almanac_ymd_t ymd;
    almanac_civil_from_days(offered, &ymd);
    s_app.setup_year = ymd.year;
    s_app.setup_month = ymd.month;
    s_app.setup_day = ymd.day;
    setup_clamp();
    s_app.screen = SCR_SETUP;
}

// The day the wizard shows, clamped into the supported range.
static int wizard_day(void) {
    int jdn = almanac_days_from_civil(s_app.setup_year, s_app.setup_month, s_app.setup_day);
    if (jdn < almanac_supported_min_day()) jdn = almanac_supported_min_day();
    if (jdn > almanac_supported_max_day()) jdn = almanac_supported_max_day();
    return jdn;
}

// Confirms the wizard day as today: the system clock is set to that date, noon on a first run.
static void setup_confirm(void) {
    const int jdn = wizard_day();
    int hour = 12;
    int minute = 0;
    int today = 0;
    almanac_clock_split(now_seconds(), &today, &hour, &minute);
    if (!s_app.clock_valid || hour < 0 || hour > 23) {
        hour = 12;
        minute = 0;
    }
    const int64_t target = almanac_clock_make(jdn, hour, minute);
    struct timeval tv = {(time_t)target, 0};
    (void)settimeofday(&tv, NULL);
    s_app.clock_valid = almanac_clock_is_valid(now_seconds());
    s_app.today = jdn;
    s_app.view_day = jdn;
    const esp_err_t err = almanac_store_save(jdn);
    if (err != ESP_OK) ESP_LOGW(TAG, "确认日期未存下(%s)", esp_err_to_name(err));
    ESP_LOGI(TAG, "date confirmed: %d", jdn);
    s_app.setup_from_lost = false;
    s_app.screen = SCR_TODAY;
    render();
}

static void pick_search(void) {
    s_app.result_count = almanac_lucky_dates(s_app.today, s_app.pick_sel, s_app.result_day, s_app.result_god,
                                             ALMANAC_LUCKY_DATE_MAX);
    s_app.screen = SCR_RESULT;
}

static void menu_run(void) {
    switch (s_app.menu_sel) {
    case MENU_CALIBRATE:
        s_app.setup_step = 0;
        s_app.setup_from_lost = false;
        almanac_ymd_t ymd;
        almanac_civil_from_days(s_app.view_day, &ymd);
        s_app.setup_year = ymd.year;
        s_app.setup_month = ymd.month;
        s_app.setup_day = ymd.day;
        s_app.screen = SCR_CONFIRM;
        break;
    case MENU_TODAY:
        load_day(s_app.today);
        s_app.screen = SCR_TODAY;
        break;
    case MENU_PICK:
        s_app.pick_sel = 0;
        s_app.pick_first = 0;
        s_app.screen = SCR_PICK;
        break;
    case MENU_HELP:
        s_app.back = SCR_MENU;
        s_app.screen = SCR_HELP;
        break;
    default:
        s_app.back = SCR_MENU;
        s_app.screen = SCR_ABOUT;
        break;
    }
    render();
}

static void next_page(void) {
    switch (s_app.screen) {
    case SCR_TODAY:
        s_app.screen = SCR_DETAIL;
        break;
    case SCR_DETAIL:
        s_app.screen = SCR_HOURS;
        break;
    case SCR_HOURS:
        s_app.screen = SCR_DIRS;
        break;
    case SCR_DIRS:
        s_app.screen = SCR_FORTUNE;
        break;
    default:
        s_app.screen = SCR_TODAY;
        break;
    }
    render();
}

static void render(void) {
    almanac_day_t day;
    almanac_view_day_t view_day;
    const char *title = STR_APP_TITLE;
    const char *hint1 = STR_HINT_TODAY;
    const char *hint2 = STR_HINT_LONG_MENU;

    switch (s_app.screen) {
    case SCR_TODAY:
    case SCR_DETAIL:
    case SCR_HOURS:
    case SCR_DIRS:
    case SCR_FORTUNE:
        almanac_day_compute(s_app.view_day, &day);
        almanac_view_day(&day, &view_day);
        break;
    default:
        memset(&day, 0, sizeof(day));
        memset(&view_day, 0, sizeof(view_day));
        break;
    }

    switch (s_app.screen) {
    case SCR_DETAIL:
        title = STR_PAGE_DETAIL;
        hint1 = STR_HINT_NEXT;
        break;
    case SCR_HOURS:
        title = STR_PAGE_HOURS;
        hint1 = STR_HINT_NEXT;
        break;
    case SCR_DIRS:
        title = STR_PAGE_DIRS;
        hint1 = STR_HINT_NEXT;
        break;
    case SCR_FORTUNE:
        title = STR_PAGE_LUCK;
        hint1 = STR_HINT_NEXT;
        break;
    case SCR_MENU:
        hint1 = STR_HINT_MENU;
        hint2 = STR_HINT_LONG_BACK;
        break;
    case SCR_PICK:
        title = STR_PAGE_PICK;
        hint1 = STR_HINT_PICK;
        break;
    case SCR_RESULT:
        title = STR_PAGE_PICK;
        hint1 = STR_HINT_REPICK;
        break;
    case SCR_SETUP:
        title = STR_SETUP_TITLE;
        hint1 = STR_HINT_MENU;
        hint2 = STR_HINT_LONG_CANCEL;
        break;
    case SCR_CONFIRM:
        title = STR_SETUP_TITLE;
        hint1 = STR_HINT_OK;
        hint2 = STR_HINT_LONG_CANCEL;
        break;
    case SCR_HELP:
        title = STR_PAGE_HELP;
        hint1 = STR_HINT_BACK;
        break;
    case SCR_ABOUT:
        title = STR_PAGE_ABOUT;
        hint1 = STR_HINT_BACK;
        break;
    default:
        break;
    }

    const char *banner = NULL;
    if (!s_app.clock_valid) banner = s_app.stored_day ? STR_DATE_LOST : STR_SETUP_TITLE;

    almanac_ui_begin(title, s_app.battery_soc);
    switch (s_app.screen) {
    case SCR_TODAY:
        almanac_ui_today(&view_day, almanac_zhixing_name(day.zhixing), s_app.view_day != s_app.today,
                         banner);
        break;
    case SCR_DETAIL:
        almanac_ui_detail(&view_day, day.huangdao);
        break;
    case SCR_HOURS: {
        almanac_view_hours_t hours;
        almanac_view_hours(&day, &hours);
        almanac_ui_hours(&hours);
        break;
    }
    case SCR_DIRS: {
        almanac_view_lucky_t lucky;
        almanac_view_lucky(&day, &lucky);
        almanac_ui_lucky(&lucky);
        break;
    }
    case SCR_FORTUNE:
        almanac_ui_fortune(&day);
        break;
    case SCR_MENU:
        almanac_ui_menu(s_app.menu_sel);
        break;
    case SCR_PICK:
        almanac_ui_pick(s_app.pick_sel, s_app.pick_first);
        break;
    case SCR_RESULT: {
        almanac_line_t dates[ALMANAC_LUCKY_DATE_MAX];
        for (int i = 0; i < s_app.result_count; ++i) {
            almanac_view_lucky_date(s_app.result_day[i], s_app.result_god[i], &dates[i]);
        }
        almanac_ui_result(almanac_matter(s_app.pick_sel), dates, s_app.result_count);
        break;
    }
    case SCR_SETUP:
        almanac_ui_setup(s_app.setup_step, s_app.setup_year, s_app.setup_month, s_app.setup_day);
        break;
    case SCR_CONFIRM: {
        almanac_ymd_t ymd;
        almanac_civil_from_days(wizard_day(), &ymd);
        char text[48];
        snprintf(text, sizeof(text), "%d%s%d%s%d%s", ymd.year, STR_SETUP_YEAR, ymd.month, STR_SETUP_MONTH, ymd.day,
                 STR_SETUP_DAY);
        almanac_ui_confirm(text);
        break;
    }
    case SCR_HELP: {
        almanac_line_t lines[ALMANAC_TEXT_MAX_LINES];
        const int count = almanac_view_help(lines, ALMANAC_TEXT_MAX_LINES);
        almanac_ui_help(lines, count);
        break;
    }
    case SCR_ABOUT: {
        almanac_line_t lines[ALMANAC_TEXT_MAX_LINES];
        const int count = almanac_view_about(lines, ALMANAC_TEXT_MAX_LINES);
        almanac_ui_about(lines, count);
        break;
    }
    default:
        break;
    }
    almanac_ui_hints(hint1, hint2);
}

static bool key_held(void) {
    const int mv = bsp_button_read_mv();
    return mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN;
}

static void enter_sleep(void) {
    if (key_held()) {
        ESP_LOGI(TAG, "key still held, sleep deferred");
        s_sleep_requested = false;
        return;
    }
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart();
    esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) ESP_LOGW(TAG, "GPIO wake setup failed (%s)", esp_err_to_name(err));
    (void)bsp_battery_sleep();
    (void)bsp_audio_sleep();
    (void)bsp_audio_prepare_deep_sleep();
    (void)bsp_i2c_prepare_deep_sleep();
    // Held past esp_deep_sleep_start(): no flush may touch the panel after it is suspended.
    if (!bsp_lvgl_lock(1000)) esp_restart();
    (void)bsp_display_prepare_deep_sleep();
    ESP_LOGI(TAG, "deep sleep");
    esp_deep_sleep_start();
    esp_restart();
}

static void sleep_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(50));
    if (s_sleep_requested) enter_sleep();
    vTaskDelete(NULL);
}

static void request_sleep(void) {
    if (s_sleep_requested || key_held()) return;
    if (xTaskCreate(sleep_task, "almanac_sleep", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "sleep task not created, trying again next tick");
        return;
    }
    s_sleep_requested = true;
}

// Keeps "today" right while the device stays awake across midnight.
static void roll_today(void) {
    if (!s_app.clock_valid) return;
    const int day = almanac_clock_day(now_seconds());
    if (day == s_app.today) return;
    const bool on_today = s_app.view_day == s_app.today;
    s_app.today = day;
    if (on_today) s_app.view_day = day;
    if (s_app.screen >= SCR_TODAY && s_app.screen <= SCR_FORTUNE) render();
}

static void tick(lv_timer_t *timer) {
    (void)timer;
    roll_today();
    almanac_power_advance(&s_power, TICK_MS);
    bsp_display_backlight(almanac_power_backlight(&s_power));
    if (++s_soc_tick >= BATTERY_TICKS) {
        s_soc_tick = 0;
        battery_refresh();
    }
    if (almanac_power_should_sleep(&s_power)) request_sleep();
}

static void handle_key(int button, int event) {
    almanac_power_activity(&s_power);
    s_sleep_requested = false;
    bsp_display_backlight(ALMANAC_BACKLIGHT_ACTIVE);
    if (event == BSP_BTN_RELEASE) return;

    const bool up = button == BSP_BTN_UP;
    const bool down = button == BSP_BTN_DOWN;
    const bool ok = button == BSP_BTN_OK;

    if (event == BSP_BTN_CLICK && (up || down)) {
        const int delta = up ? -1 : 1;
        switch (s_app.screen) {
        case SCR_TODAY:
        case SCR_DETAIL:
        case SCR_HOURS:
        case SCR_DIRS:
        case SCR_FORTUNE:
            load_day(s_app.view_day + delta);
            render();
            return;
        case SCR_MENU:
            menu_move(delta);
            render();
            return;
        case SCR_PICK:
            pick_move(delta);
            render();
            return;
        case SCR_SETUP:
            wizard_step(delta);
            return;
        default:
            return;
        }
    }

    if (event == BSP_BTN_CLICK && ok) {
        switch (s_app.screen) {
        case SCR_TODAY:
        case SCR_DETAIL:
        case SCR_HOURS:
        case SCR_DIRS:
        case SCR_FORTUNE:
            next_page();
            return;
        case SCR_MENU:
            menu_run();
            return;
        case SCR_PICK:
            pick_search();
            render();
            return;
        case SCR_RESULT:
            s_app.screen = SCR_PICK;
            render();
            return;
        case SCR_SETUP:
            wizard_step(0);
            return;
        case SCR_CONFIRM:
            setup_confirm();
            return;
        case SCR_HELP:
        case SCR_ABOUT:
            s_app.screen = s_app.back;
            render();
            return;
        default:
            return;
        }
    }

    if (event == BSP_BTN_LONG && ok) {
        switch (s_app.screen) {
        case SCR_TODAY:
        case SCR_DETAIL:
        case SCR_HOURS:
        case SCR_DIRS:
        case SCR_FORTUNE:
        case SCR_PICK:
        case SCR_RESULT:
            s_app.menu_sel = 0;
            s_app.screen = SCR_MENU;
            render();
            return;
        case SCR_MENU:
            s_app.screen = SCR_TODAY;
            render();
            return;
        case SCR_HELP:
        case SCR_ABOUT:
            s_app.screen = SCR_MENU;
            render();
            return;
        case SCR_SETUP:
            // An unconfirmed date cannot be dropped: the card keeps the reminder in its date line.
            load_day(wizard_day());
            s_app.screen = SCR_TODAY;
            render();
            return;
        case SCR_CONFIRM:
            if (s_app.setup_from_lost) {
                s_app.screen = SCR_SETUP;
            } else {
                load_day(wizard_day());
                s_app.screen = SCR_TODAY;
            }
            render();
            return;
        default:
            return;
        }
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    extern void on_key(int button, int event);
    on_key((int)button, (int)event);
}

static void key_task(void *arg) {
    (void)arg;
    almanac_key_t key;
    while (xQueueReceive(s_keys, &key, portMAX_DELAY) == pdTRUE) {
        if (!bsp_lvgl_lock(300)) {
            ESP_LOGW(TAG, "lvgl lock timeout, key dropped b=%d e=%d", key.button, key.event);
            continue;
        }
        handle_key(key.button, key.event);
        bsp_lvgl_unlock();
    }
}

void almanac_app_button_event(int button, int event) {
    almanac_key_t key = {.button = button, .event = event};
    if (s_keys) (void)xQueueSend(s_keys, &key, 0);
}

esp_err_t almanac_app_start(void) {
    memset(&s_app, 0, sizeof(s_app));
    almanac_power_reset(&s_power);
    s_sleep_requested = false;
    s_app.battery_soc = -1;

    esp_err_t err = almanac_store_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "存储不可用(%s)，日期只能靠时钟", esp_err_to_name(err));
    int last = 0;
    bool found = false;
    if (err == ESP_OK) (void)almanac_store_load(&last, &found);
    s_app.stored_day = found;

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) return ESP_FAIL;
    bsp_display_backlight(ALMANAC_BACKLIGHT_ACTIVE);
    if (bsp_battery_init() == ESP_OK) s_app.battery_soc = bsp_battery_soc();
    (void)bsp_audio_init();
    (void)bsp_audio_sleep();

    s_keys = xQueueCreate(8, sizeof(almanac_key_t));
    if (!s_keys || xTaskCreate(key_task, "almanac_key", 4096, NULL, 6, NULL) != pdPASS) return ESP_FAIL;

    const int64_t now = now_seconds();
    s_app.clock_valid = almanac_clock_is_valid(now);
    if (s_app.clock_valid) {
        s_app.today = almanac_clock_day(now);
    } else {
        ESP_LOGW(TAG, "时钟不可信，进入设日期");
        // The card behind the date wheel shows the day the wheel offers, never day zero.
        s_app.today = almanac_clock_setup_default(s_app.stored_day ? last : 0, build_day());
    }
    load_day(s_app.today);

    if (!bsp_lvgl_lock(1000)) return ESP_FAIL;
    if (!s_app.clock_valid) {
        setup_start(found);
    } else {
        s_app.screen = SCR_TODAY;
    }
    render();
    lv_timer_create(tick, TICK_MS, NULL);
    bsp_lvgl_unlock();

    (void)bsp_button_init(button_callback, NULL);
    ESP_LOGI(TAG, "almanac ready, clock %s, today %d", s_app.clock_valid ? "valid" : "lost", s_app.today);
    return ESP_OK;
}
