# Touch-Knob Hardware Map (ESP32-S3 + SH8601 + CST820)

Этот README полностью описывает текущие пины, интерфейсы и обмен с экраном согласно исходникам проекта.

## 1. Экран и тач: что за чипы

- **LCD контроллер:** SH8601 (AMOLED панель 1.5").
- **Touch контроллер:** CST820 (capacitive touch).
- **MCU:** ESP32-S3.

## 2. Как идет обмен с экраном (LCD)

Экран подключен по **QSPI (quad mode) поверх SPI2**:

1. **SPI/QSPI шина** инициализируется через `spi_bus_initialize()`.
2. Создается **SPI IO хэндл** `esp_lcd_new_panel_io_spi(...)`.
3. Подключается драйвер SH8601: `esp_lcd_new_panel_sh8601(...)`.
4. Выполняется reset/init и включение дисплея.

В конфиге используется:
- `quad_mode = true`
- `use_qspi_interface = 1`
- команды инициализации из массива `lcd_init_cmds[]`.

## 3. Как идет обмен с тачем

Touch подключен по **I2C (I2C_NUM_0)**:

1. Настраивается I2C (`i2c_param_config`, `i2c_driver_install`).
2. Создается I2C IO для touch: `esp_lcd_new_panel_io_i2c(...)`.
3. Инициализируется драйвер CST820: `esp_lcd_touch_new_i2c_cst820(...)`.

## 4. Как это связано с LVGL

- LVGL дисплей получает `lcd_io` и `lcd_panel` через `lvgl_port_add_disp(...)`.
- Touch input передается в LVGL через `lvgl_port_add_touch(...)`.

## 5. Полная карта пинов (актуально по `main/main.c`)

### LCD (SH8601, QSPI/SPI2)

| Назначение | GPIO |
|-----------|------|
| LCD_CS | GPIO12 |
| LCD_PCLK (SCLK) | GPIO10 |
| LCD_DATA0 | GPIO13 |
| LCD_DATA1 | GPIO11 |
| LCD_DATA2 | GPIO14 |
| LCD_DATA3 | GPIO9 |
| LCD_RST | GPIO8 |
| Backlight | GPIO17 |

### Touch (CST820, I2C)

| Назначение | GPIO |
|-----------|------|
| I2C_SCL | GPIO3 |
| I2C_SDA | GPIO1 |
| TOUCH_RST | GPIO2 |
| TOUCH_INT | GPIO4 |

### Энкодер

| Назначение | GPIO |
|-----------|------|
| ENCODER_A | GPIO6 |
| ENCODER_B | GPIO5 |

### Кнопка

| Назначение | GPIO |
|-----------|------|
| BUTTON | GPIO0 |

## 6. Параметры экрана (из кода)

- **Разрешение:** 472 × 466 (в коде H_RES=472, V_RES=466).
- **Цвет:** RGB565 (16-bit, зависит от `CONFIG_LV_COLOR_DEPTH`).
- **Буферы LVGL:** двойной буфер, высота 60 строк.

## 7. Что сейчас выводит LVGL

По умолчанию запускается демо **`lv_demo_widgets()`**.

---

Если нужно расширить README (схема, pinout image, wiring и т.п.) — скажи.
