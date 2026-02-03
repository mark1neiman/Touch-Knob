/**
 * @file main.c
 * @brief ESP-IDF LVGL移植示例程序主文件
 * @details 本文件实现了基于ESP32-S3的LVGL图形界面系统，包括：
 *          - LCD显示屏初始化（SH8601控制器）
 *          - 触摸屏初始化（CST820控制器）
 *          - LVGL图形库初始化
 *          - 旋钮编码器初始化
 *          - 按键初始化
 *          - 显示LVGL演示程序
 * @author Ayang
 * @date 2026年1月
 * @version 1.0
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "iot_knob.h"
#include "iot_button.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst820.h"

//***************** */

// extern  esp_err_t lvgl_port_indev_init(void);

//*************************************************** */




static const char *TAG = "example";

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static esp_timer_handle_t boot_timer = NULL;
static esp_timer_handle_t click_timer = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_HOST (SPI2_HOST)
#define EXAMPLE_LCD_BITS_PER_PIXEL (16)
#define EXAMPLE_LCD_DRAW_BUFF_DOUBLE (1)
#define EXAMPLE_LCD_LVGL_AVOID_TEAR (1)
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT (60)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_PCLK (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0 (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA1 (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA2 (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_DATA3 (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_RST (GPIO_NUM_8)
#define EXAMPLE_PIN_NUM_BK_LIGHT (GPIO_NUM_17)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 472 // 466
#define EXAMPLE_LCD_V_RES 466 // 466

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your touch spec ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CONFIG_EXAMPLE_LCD_TOUCH_ENABLED 1
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
#define EXAMPLE_TOUCH_HOST (I2C_NUM_0)
#define EXAMPLE_TOUCH_I2C_NUM (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ (400000)

#define EXAMPLE_PIN_NUM_TOUCH_SCL (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_SDA (GPIO_NUM_1)
#define EXAMPLE_PIN_NUM_TOUCH_RST (GPIO_NUM_2)
#define EXAMPLE_PIN_NUM_TOUCH_INT (GPIO_NUM_4)

esp_lcd_touch_handle_t tp = NULL;
#endif

typedef enum
{
    BSP_BTN_PRESS = GPIO_NUM_0,
} bsp_button_t;

#define BSP_ENCODER_A (GPIO_NUM_6)
#define BSP_ENCODER_B (GPIO_NUM_5)
#define BSP_FAN_PWM (GPIO_NUM_45)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// UI/UX configuration ///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define FAN_SPEED_MIN_PERCENT 40
#define FAN_SPEED_MAX_PERCENT 80
#define FAN_SPEED_DEFAULT_PERCENT 65
#define FAN_PWM_FREQ_HZ 25000
#define FAN_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define FAN_PWM_MAX_DUTY ((1 << 10) - 1)
#define OWNER_NAME_MAX_LEN 16
#define CLICK_WINDOW_US (800 * 1000)

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_MAIN,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_LANGUAGE,
    UI_SCREEN_OWNER,
} ui_screen_t;

typedef enum {
    LANG_EN = 0,
    LANG_RU,
    LANG_ET,
    LANG_DE,
    LANG_FI,
    LANG_LV,
    LANG_LT,
    LANG_ES,
    LANG_COUNT,
} ui_lang_t;

static ui_screen_t ui_screen = UI_SCREEN_BOOT;
static ui_lang_t current_lang = LANG_EN;
static bool ui_locked = false;
static int fan_speed_percent = FAN_SPEED_DEFAULT_PERCENT;
static int settings_index = 0;
static size_t owner_name_len = 0;
static char owner_name[OWNER_NAME_MAX_LEN + 1] = "";
static int click_count = 0;
static bool suppress_click = false;

static lv_obj_t *boot_screen = NULL;
static lv_obj_t *main_screen = NULL;
static lv_obj_t *settings_screen = NULL;
static lv_obj_t *language_screen = NULL;
static lv_obj_t *owner_screen = NULL;
static lv_obj_t *label_speed = NULL;
static lv_obj_t *label_speed_caption = NULL;
static lv_obj_t *arc_speed = NULL;
static lv_obj_t *lock_overlay = NULL;
static lv_obj_t *settings_items[2] = {0};
static lv_obj_t *label_settings_title = NULL;
static lv_obj_t *label_language_title = NULL;
static lv_obj_t *label_owner_title = NULL;
static lv_obj_t *roller_language = NULL;
static lv_obj_t *label_owner_value = NULL;
static lv_obj_t *roller_owner = NULL;

static const char *language_names[LANG_COUNT] = {
    "English",
    "Русский",
    "Eesti",
    "Deutsch",
    "Suomi",
    "Latviešu",
    "Lietuviu",
    "Español",
};

typedef struct {
    const char *fan_speed;
    const char *settings;
    const char *language;
    const char *owner_name;
    const char *locked;
    const char *unlocked;
} ui_strings_t;

static const ui_strings_t ui_strings[LANG_COUNT] = {
    [LANG_EN] = {
        .fan_speed = "Fan speed",
        .settings = "Settings",
        .language = "Language",
        .owner_name = "Owner name",
        .locked = "Locked",
        .unlocked = "Unlocked",
    },
    [LANG_RU] = {
        .fan_speed = "Скорость",
        .settings = "Настройки",
        .language = "Язык",
        .owner_name = "Имя владельца",
        .locked = "Заблокировано",
        .unlocked = "Разблокировано",
    },
    [LANG_ET] = {
        .fan_speed = "Ventilaator",
        .settings = "Seaded",
        .language = "Keel",
        .owner_name = "Omaniku nimi",
        .locked = "Lukus",
        .unlocked = "Avatud",
    },
    [LANG_DE] = {
        .fan_speed = "Luefter",
        .settings = "Einstellungen",
        .language = "Sprache",
        .owner_name = "Besitzername",
        .locked = "Gesperrt",
        .unlocked = "Entsperrt",
    },
    [LANG_FI] = {
        .fan_speed = "Tuuletin",
        .settings = "Asetukset",
        .language = "Kieli",
        .owner_name = "Omistaja",
        .locked = "Lukittu",
        .unlocked = "Avattu",
    },
    [LANG_LV] = {
        .fan_speed = "Ventilators",
        .settings = "Iestatījumi",
        .language = "Valoda",
        .owner_name = "Īpašnieks",
        .locked = "Bloķēts",
        .unlocked = "Atbloķēts",
    },
    [LANG_LT] = {
        .fan_speed = "Ventiliatorius",
        .settings = "Nustatymai",
        .language = "Kalba",
        .owner_name = "Savininko vardas",
        .locked = "Užrakinta",
        .unlocked = "Atrakinta",
    },
    [LANG_ES] = {
        .fan_speed = "Ventilador",
        .settings = "Ajustes",
        .language = "Idioma",
        .owner_name = "Propietario",
        .locked = "Bloqueado",
        .unlocked = "Desbloqueado",
    },
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to LVGL ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LVGL_BUFF_SIZE (EXAMPLE_LCD_H_RES * 40)
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 50
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE (8 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2

#define HF_ws2812 0

void my_print(lv_log_level_t level, const char *buf)
{
    esp_log_level_t lvl = ESP_LOG_NONE;

    switch (level)
    {
    case LV_LOG_LEVEL_TRACE:
        lvl = ESP_LOG_VERBOSE;
        break;
    case LV_LOG_LEVEL_INFO:
        lvl = ESP_LOG_INFO;
        break;
    case LV_LOG_LEVEL_WARN:
        lvl = ESP_LOG_WARN;
        break;
    case LV_LOG_LEVEL_ERROR:
        lvl = ESP_LOG_ERROR;
        break;
    case LV_LOG_LEVEL_USER:
        lvl = ESP_LOG_DEBUG;
        break;
    case LV_LOG_LEVEL_NONE:
        lvl = ESP_LOG_NONE;
        break;
    }
    ESP_LOG_LEVEL(lvl, TAG, "%s", buf);
}

static void init_pwm_fan(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = FAN_PWM_RESOLUTION,
        .freq_hz = FAN_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t chan_conf = {
        .gpio_num = BSP_FAN_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_conf));
}

static void apply_fan_pwm(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (uint32_t)((percent * FAN_PWM_MAX_DUTY) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void save_owner_name(void)
{
    nvs_handle_t handle;
    if (nvs_open("settings", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "owner", owner_name);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void save_language(void)
{
    nvs_handle_t handle;
    if (nvs_open("settings", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "lang", (uint8_t)current_lang);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void load_settings(void)
{
    nvs_handle_t handle;
    if (nvs_open("settings", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t lang = 0;
        if (nvs_get_u8(handle, "lang", &lang) == ESP_OK && lang < LANG_COUNT) {
            current_lang = (ui_lang_t)lang;
        }
        size_t len = sizeof(owner_name);
        if (nvs_get_str(handle, "owner", owner_name, &len) == ESP_OK) {
            owner_name_len = strnlen(owner_name, sizeof(owner_name) - 1);
        }
        nvs_close(handle);
    }
}

static void update_main_ui(void)
{
    if (!label_speed || !arc_speed) {
        return;
    }
    char speed_text[8];
    snprintf(speed_text, sizeof(speed_text), "%d%%", fan_speed_percent);
    lv_label_set_text(label_speed, speed_text);
    lv_arc_set_value(arc_speed, fan_speed_percent);
    if (label_speed_caption) {
        lv_label_set_text(label_speed_caption, ui_strings[current_lang].fan_speed);
    }
}

static void apply_language(void)
{
    if (label_speed_caption) {
        lv_label_set_text(label_speed_caption, ui_strings[current_lang].fan_speed);
    }
    if (label_settings_title) {
        lv_label_set_text(label_settings_title, ui_strings[current_lang].settings);
    }
    if (label_language_title) {
        lv_label_set_text(label_language_title, ui_strings[current_lang].language);
    }
    if (label_owner_title) {
        lv_label_set_text(label_owner_title, ui_strings[current_lang].owner_name);
    }
    if (settings_items[0]) {
        lv_label_set_text(settings_items[0], ui_strings[current_lang].language);
    }
    if (settings_items[1]) {
        lv_label_set_text(settings_items[1], ui_strings[current_lang].owner_name);
    }
}

static void set_lock_overlay(bool enabled)
{
    if (!lock_overlay) {
        return;
    }
    if (enabled) {
        lv_obj_clear_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_settings_selection(void)
{
    for (int i = 0; i < 2; ++i) {
        if (!settings_items[i]) {
            continue;
        }
        if (i == settings_index) {
            lv_obj_set_style_text_color(settings_items[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(settings_items[i], LV_OPA_40, 0);
        } else {
            lv_obj_set_style_text_color(settings_items[i], lv_color_gray(), 0);
            lv_obj_set_style_bg_opa(settings_items[i], LV_OPA_TRANSP, 0);
        }
    }
}

static void show_screen(lv_obj_t *screen)
{
    if (screen) {
        lv_scr_load(screen);
    }
}

static void boot_timer_cb(void *arg)
{
    lvgl_port_lock(0);
    ui_screen = UI_SCREEN_MAIN;
    show_screen(main_screen);
    set_lock_overlay(ui_locked);
    update_main_ui();
    lvgl_port_unlock();
}

static void click_timer_cb(void *arg)
{
    int count = click_count;
    click_count = 0;
    if (count == 1) {
        lvgl_port_lock(0);
        if (ui_screen == UI_SCREEN_MAIN) {
            ui_locked = !ui_locked;
            set_lock_overlay(ui_locked);
        } else {
            handle_single_click();
        }
        lvgl_port_unlock();
    } else if (count == 3) {
        if (ui_screen == UI_SCREEN_MAIN) {
            ui_screen = UI_SCREEN_SETTINGS;
            lvgl_port_lock(0);
            show_screen(settings_screen);
            update_settings_selection();
            lvgl_port_unlock();
        }
    }
}

static void start_click_timer(void)
{
    if (!click_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = &click_timer_cb,
            .name = "click_timer",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &click_timer));
    }
    esp_timer_stop(click_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(click_timer, CLICK_WINDOW_US));
}

esp_err_t app_touch_init(void)
{
    /* Initilize I2C */
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = EXAMPLE_TOUCH_I2C_CLK_HZ};
    ESP_RETURN_ON_ERROR(i2c_param_config(EXAMPLE_TOUCH_I2C_NUM, &i2c_conf), TAG, "I2C configuration failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(EXAMPLE_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0), TAG, "I2C initialization failed");

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            // 屏幕方向一般在这里改
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)EXAMPLE_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_cst820(tp_io_handle, &tp_cfg, &touch_handle);
}

void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_invalidated_area(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

esp_err_t app_lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 7096,       /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .timer_period_ms = 5      /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT, // EXAMPLE_LCD_DRAW_BUFF_HEIGHT
        .double_buffer = EXAMPLE_LCD_DRAW_BUFF_DOUBLE,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,  // false,  屏幕方向一般在这里改
            .mirror_x = false, // true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    lv_display_add_event_cb(lvgl_disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

static void create_boot_screen(void)
{
    boot_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_label_create(boot_screen);
    lv_label_set_text(logo, "Belom");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_28, 0);
    lv_obj_center(logo);

    if (owner_name_len > 0) {
        lv_obj_t *owner = lv_label_create(boot_screen);
        char owner_text[32];
        snprintf(owner_text, sizeof(owner_text), "Owner: %s", owner_name);
        lv_label_set_text(owner, owner_text);
        lv_obj_set_style_text_font(owner, &lv_font_montserrat_12, 0);
        lv_obj_align(owner, LV_ALIGN_BOTTOM_MID, 0, -12);
    }
}

static void create_main_screen(void)
{
    main_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    arc_speed = lv_arc_create(main_screen);
    lv_obj_set_size(arc_speed, 200, 200);
    lv_obj_center(arc_speed);
    lv_arc_set_range(arc_speed, 0, 100);
    lv_arc_set_rotation(arc_speed, 270);
    lv_arc_set_bg_angles(arc_speed, 0, 360);
    lv_obj_remove_style(arc_speed, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc_speed, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_speed, 12, LV_PART_INDICATOR);

    label_speed = lv_label_create(main_screen);
    lv_obj_set_style_text_font(label_speed, &lv_font_montserrat_36, 0);
    lv_obj_center(label_speed);

    label_speed_caption = lv_label_create(main_screen);
    lv_obj_set_style_text_font(label_speed_caption, &lv_font_montserrat_14, 0);
    lv_obj_align(label_speed_caption, LV_ALIGN_CENTER, 0, 56);

    lock_overlay = lv_label_create(main_screen);
    lv_label_set_text(lock_overlay, "🔒");
    lv_obj_set_style_text_font(lock_overlay, &lv_font_montserrat_28, 0);
    lv_obj_align(lock_overlay, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, lock_overlay);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_values(&anim, 80, 255);
    lv_anim_set_time(&anim, 1200);
    lv_anim_set_playback_time(&anim, 1200);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

static void create_settings_screen(void)
{
    settings_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    label_settings_title = lv_label_create(settings_screen);
    lv_obj_set_style_text_font(label_settings_title, &lv_font_montserrat_20, 0);
    lv_obj_align(label_settings_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *list = lv_obj_create(settings_screen);
    lv_obj_set_size(list, 200, 140);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 16);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    settings_items[0] = lv_label_create(list);
    settings_items[1] = lv_label_create(list);
    lv_obj_set_width(settings_items[0], 180);
    lv_obj_set_width(settings_items[1], 180);
    lv_obj_align(settings_items[0], LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(settings_items[1], LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_pad_all(settings_items[0], 6, 0);
    lv_obj_set_style_pad_all(settings_items[1], 6, 0);

    apply_language();
    update_settings_selection();
}

static void create_language_screen(void)
{
    language_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(language_screen, LV_OBJ_FLAG_SCROLLABLE);

    label_language_title = lv_label_create(language_screen);
    lv_obj_set_style_text_font(label_language_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(label_language_title, ui_strings[current_lang].language);
    lv_obj_align(label_language_title, LV_ALIGN_TOP_MID, 0, 12);

    roller_language = lv_roller_create(language_screen);
    lv_obj_set_width(roller_language, 220);
    lv_obj_align(roller_language, LV_ALIGN_CENTER, 0, 20);

    lv_roller_set_options(roller_language,
                          "English\nРусский\nEesti\nDeutsch\nSuomi\nLatviešu\nLietuviu\nEspañol",
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_language, 4);
    lv_roller_set_selected(roller_language, current_lang, LV_ANIM_OFF);
}

static void create_owner_screen(void)
{
    owner_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(owner_screen, LV_OBJ_FLAG_SCROLLABLE);

    label_owner_title = lv_label_create(owner_screen);
    lv_obj_set_style_text_font(label_owner_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(label_owner_title, ui_strings[current_lang].owner_name);
    lv_obj_align(label_owner_title, LV_ALIGN_TOP_MID, 0, 12);

    label_owner_value = lv_label_create(owner_screen);
    lv_label_set_text(label_owner_value, owner_name_len ? owner_name : "-");
    lv_obj_set_style_text_font(label_owner_value, &lv_font_montserrat_16, 0);
    lv_obj_align(label_owner_value, LV_ALIGN_TOP_MID, 0, 46);

    roller_owner = lv_roller_create(owner_screen);
    lv_obj_set_width(roller_owner, 200);
    lv_obj_align(roller_owner, LV_ALIGN_CENTER, 0, 20);
    lv_roller_set_visible_row_count(roller_owner, 4);
    lv_roller_set_options(roller_owner,
                          "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ\n"
                          "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n"
                          "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n"
                          "space\n<-\nSave",
                          LV_ROLLER_MODE_NORMAL);
}

static void ui_init(void)
{
    create_boot_screen();
    create_main_screen();
    create_settings_screen();
    create_language_screen();
    create_owner_screen();

    show_screen(boot_screen);
    update_main_ui();

    if (!boot_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = &boot_timer_cb,
            .name = "boot_timer",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &boot_timer));
    }
    esp_timer_start_once(boot_timer, 2000 * 1000);
}

static void handle_knob_move(int direction)
{
    if (ui_screen == UI_SCREEN_MAIN) {
        if (ui_locked) {
            return;
        }
        fan_speed_percent += direction;
        if (fan_speed_percent < FAN_SPEED_MIN_PERCENT) {
            fan_speed_percent = FAN_SPEED_MIN_PERCENT;
        }
        if (fan_speed_percent > FAN_SPEED_MAX_PERCENT) {
            fan_speed_percent = FAN_SPEED_MAX_PERCENT;
        }
        lvgl_port_lock(0);
        update_main_ui();
        lvgl_port_unlock();
        apply_fan_pwm(fan_speed_percent);
        return;
    }

    if (ui_screen == UI_SCREEN_SETTINGS) {
        settings_index += direction;
        if (settings_index < 0) {
            settings_index = 1;
        }
        if (settings_index > 1) {
            settings_index = 0;
        }
        lvgl_port_lock(0);
        update_settings_selection();
        lvgl_port_unlock();
        return;
    }

    if (ui_screen == UI_SCREEN_LANGUAGE && roller_language) {
        int selected = lv_roller_get_selected(roller_language);
        selected += direction;
        if (selected < 0) {
            selected = LANG_COUNT - 1;
        }
        if (selected >= LANG_COUNT) {
            selected = 0;
        }
        lvgl_port_lock(0);
        lv_roller_set_selected(roller_language, selected, LV_ANIM_ON);
        lvgl_port_unlock();
        return;
    }

    if (ui_screen == UI_SCREEN_OWNER && roller_owner) {
        int selected = lv_roller_get_selected(roller_owner);
        selected += direction;
        if (selected < 0) {
            selected = lv_roller_get_option_cnt(roller_owner) - 1;
        }
        if (selected >= lv_roller_get_option_cnt(roller_owner)) {
            selected = 0;
        }
        lvgl_port_lock(0);
        lv_roller_set_selected(roller_owner, selected, LV_ANIM_ON);
        lvgl_port_unlock();
    }
}

static void handle_owner_selection(void)
{
    if (!roller_owner || !label_owner_value) {
        return;
    }
    char buf[16] = {0};
    lv_roller_get_selected_str(roller_owner, buf, sizeof(buf));
    if (strcmp(buf, "<-") == 0) {
        if (owner_name_len > 0) {
            owner_name[owner_name_len - 1] = '\0';
            owner_name_len--;
        }
    } else if (strcmp(buf, "Save") == 0) {
        save_owner_name();
        ui_screen = UI_SCREEN_SETTINGS;
        show_screen(settings_screen);
        apply_language();
        update_settings_selection();
        return;
    } else {
        char ch = 0;
        if (strcmp(buf, "space") == 0) {
            ch = ' ';
        } else {
            ch = buf[0];
        }
        if (owner_name_len < OWNER_NAME_MAX_LEN) {
            owner_name[owner_name_len] = ch;
            owner_name_len++;
            owner_name[owner_name_len] = '\0';
        }
    }
    lv_label_set_text(label_owner_value, owner_name_len ? owner_name : "-");
}

static void handle_single_click(void)
{
    if (ui_screen == UI_SCREEN_SETTINGS) {
        if (settings_index == 0) {
            ui_screen = UI_SCREEN_LANGUAGE;
            if (roller_language) {
                lv_roller_set_selected(roller_language, current_lang, LV_ANIM_OFF);
            }
            show_screen(language_screen);
        } else if (settings_index == 1) {
            ui_screen = UI_SCREEN_OWNER;
            if (label_owner_value) {
                lv_label_set_text(label_owner_value, owner_name_len ? owner_name : "-");
            }
            show_screen(owner_screen);
        }
        return;
    }

    if (ui_screen == UI_SCREEN_LANGUAGE && roller_language) {
        int selected = lv_roller_get_selected(roller_language);
        if (selected >= 0 && selected < LANG_COUNT) {
            current_lang = (ui_lang_t)selected;
            apply_language();
            save_language();
        }
        ui_screen = UI_SCREEN_SETTINGS;
        show_screen(settings_screen);
        return;
    }

    if (ui_screen == UI_SCREEN_OWNER) {
        handle_owner_selection();
    }
}

static void handle_long_press(void)
{
    if (ui_screen == UI_SCREEN_LANGUAGE || ui_screen == UI_SCREEN_OWNER) {
        ui_screen = UI_SCREEN_SETTINGS;
        show_screen(settings_screen);
        apply_language();
        update_settings_selection();
        return;
    }
    if (ui_screen == UI_SCREEN_SETTINGS) {
        ui_screen = UI_SCREEN_MAIN;
        show_screen(main_screen);
        set_lock_overlay(ui_locked);
        update_main_ui();
    }
}

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 0, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 0, 10},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    //{0x2A, (uint8_t []){0x00,0x06,0x01,0xD7}, 4, 0},
    //{0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xDD}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 60},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};


//***************The following code is for standardizing the event triggering of rotary encoders and buttons.**************** */
//**************旋钮********* */
void LVGL_knob_event(void *event)
{
}

void LVGL_button_event(void *event)
{
}

static knob_handle_t knob = NULL;

const char *knob_event_table[] = {
    "KNOB_LEFT",
    "KNOB_RIGHT",
    "KNOB_H_LIM",
    "KNOB_L_LIM",
    "KNOB_ZERO",
};

static void knob_event_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "knob event %s, %d", knob_event_table[(knob_event_t)data], iot_knob_get_count_value(knob));
    if ((knob_event_t)data == KNOB_LEFT) {
        handle_knob_move(-1);
    } else if ((knob_event_t)data == KNOB_RIGHT) {
        handle_knob_move(1);
    }
    LVGL_knob_event(data);
}

void knob_init(uint32_t encoder_a, uint32_t encoder_b)
{
    knob_config_t cfg = {
        .default_direction = 0,
        .gpio_encoder_a = encoder_a,
        .gpio_encoder_b = encoder_b,
#if CONFIG_PM_ENABLE
        .enable_power_save = true,
#endif
    };

    knob = iot_knob_create(&cfg);
    assert(knob);
    esp_err_t err = iot_knob_register_cb(knob, KNOB_LEFT, knob_event_cb, (void *)KNOB_LEFT);
    err |= iot_knob_register_cb(knob, KNOB_RIGHT, knob_event_cb, (void *)KNOB_RIGHT);
    err |= iot_knob_register_cb(knob, KNOB_H_LIM, knob_event_cb, (void *)KNOB_H_LIM);
    err |= iot_knob_register_cb(knob, KNOB_L_LIM, knob_event_cb, (void *)KNOB_L_LIM);
    err |= iot_knob_register_cb(knob, KNOB_ZERO, knob_event_cb, (void *)KNOB_ZERO);
    ESP_ERROR_CHECK(err);
}

/* 按键相关函数 */
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C6
#define BOOT_BUTTON_NUM 9
#else
#define BOOT_BUTTON_NUM 0
#endif
#define BUTTON_ACTIVE_LEVEL 0
const char *button_event_table[] = {
    "BUTTON_PRESS_DOWN",
    "BUTTON_PRESS_UP",
    "BUTTON_PRESS_REPEAT",
    "BUTTON_PRESS_REPEAT_DONE",
    "BUTTON_SINGLE_CLICK",
    "BUTTON_DOUBLE_CLICK",
    "BUTTON_MULTIPLE_CLICK",
    "BUTTON_LONG_PRESS_START",
    "BUTTON_LONG_PRESS_HOLD",
    "BUTTON_LONG_PRESS_UP",
    "BUTTON_PRESS_END",
};

static void button_event_cb(void *arg, void *data)
{
    button_event_t event = (button_event_t)data;
    ESP_LOGI(TAG, "Button event %s", button_event_table[event]);
    if (event == BUTTON_PRESS_UP) {
        if (suppress_click) {
            suppress_click = false;
            return;
        }
        click_count++;
        start_click_timer();
    } else if (event == BUTTON_LONG_PRESS_START) {
        suppress_click = true;
        lvgl_port_lock(0);
        handle_long_press();
        lvgl_port_unlock();
    }
    LVGL_button_event(data);
}
void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = button_num,
            .active_level = BUTTON_ACTIVE_LEVEL,
#if CONFIG_GPIO_BUTTON_SUPPORT_POWER_SAVE
            .enable_power_save = true,
#endif
        },
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    assert(btn);
    esp_err_t err = iot_button_register_cb(btn, BUTTON_PRESS_UP, button_event_cb, (void *)BUTTON_PRESS_UP);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, button_event_cb, (void *)BUTTON_LONG_PRESS_START);

#if CONFIG_ENTER_LIGHT_SLEEP_MODE_MANUALLY
    /*!< For enter Power Save */
    button_power_save_config_t config = {
        .enter_power_save_cb = button_enter_power_save,
    };
    err |= iot_button_register_power_save_cb(&config);
#endif

    ESP_ERROR_CHECK(err);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    load_settings();
    init_pwm_fan();
    apply_fan_pwm(fan_speed_percent);

    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0)
    {
        ESP_LOGI(TAG, "Turn off LCD backlight");
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    }
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

#endif
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg =
        SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK, EXAMPLE_PIN_NUM_LCD_DATA0,
                                     EXAMPLE_PIN_NUM_LCD_DATA1, EXAMPLE_PIN_NUM_LCD_DATA2,
                                     EXAMPLE_PIN_NUM_LCD_DATA3, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);

    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = -1,                     // EXAMPLE_PIN_NUM_LCD_CS,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS, //-1,
        .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = 20,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .flags = {
            .quad_mode = true,
        },
    };

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds, // Uncomment these line if use custom initialization commands
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(sh8601_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_HOST, &io_config, &lcd_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install LCD driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel));


    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    app_touch_init();

    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0)
    {
        ESP_LOGI(TAG, "HF --Turn on LCD backlight");
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    }

    app_lvgl_init();

    knob_init(BSP_ENCODER_A, BSP_ENCODER_B);
    button_init(BSP_BTN_PRESS);

    // Lock the mutex due to the LVGL APIs are not thread-safe
    lvgl_port_lock(0);
    ui_init();
    // Release the mutex
    lvgl_port_unlock();
}
