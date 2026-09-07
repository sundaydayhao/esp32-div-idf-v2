#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
enum {
    DIV_LCD_MOSI = 35, DIV_LCD_MISO = 37, DIV_LCD_SCLK = 36,
    DIV_LCD_CS = 17, DIV_LCD_DC = 16, DIV_LCD_BL = 7, DIV_TOUCH_CS = 18,
    DIV_SPI_MOSI = 11, DIV_SPI_MISO = 13, DIV_SPI_SCLK = 12, DIV_SD_CS = 10,
    DIV_I2C_SDA = 8, DIV_I2C_SCL = 9, DIV_PCF_ADDR = 0x20,
    DIV_CC_CS = 5, DIV_CC_GDO0 = 6, DIV_CC_GDO2 = 3,
    DIV_NRF1_CE = 15, DIV_NRF1_CS = 4, DIV_NRF2_CE = 47, DIV_NRF2_CS = 48,
    DIV_NRF3_CE = 14, DIV_NRF3_CS = 21, DIV_IR_TX = 14, DIV_IR_RX = 21,
    DIV_LED_GPIO = 1, DIV_LED_COUNT = 4, DIV_BATTERY_BUZZER = 2,
    DIV_BUTTON_UP = 3, DIV_BUTTON_RIGHT = 4, DIV_BUTTON_DOWN = 5,
    DIV_BUTTON_LEFT = 6, DIV_BUTTON_OK = 7, DIV_BUTTON_AUX = 2
};
#define DIV_LCD_HOST SPI2_HOST
#define DIV_RADIO_HOST SPI3_HOST
#ifdef CONFIG_DIV_BOARD_V4
#define DIV_BOARD_NAME "ESP32-DIV V4"
#define DIV_BOARD_ID 4
#else
#define DIV_BOARD_NAME "ESP32-DIV V3"
#define DIV_BOARD_ID 3
#endif
#ifdef CONFIG_DIV_PANEL_ST7796
#define DIV_LCD_WIDTH 320
#define DIV_LCD_HEIGHT 480
#else
#define DIV_LCD_WIDTH 240
#define DIV_LCD_HEIGHT 320
#endif
/* Match TFT_eSPI ILI9341 rotation 0: MADCTL = MX | BGR.
   LVGL's rotation callback must preserve MX or glyphs are mirrored. */
#ifdef CONFIG_DIV_PANEL_ILI9341
#define DIV_LCD_NATIVE_MIRROR_X true
#else
#define DIV_LCD_NATIVE_MIRROR_X false
#endif
esp_err_t div_board_init(void);
i2c_master_bus_handle_t div_board_i2c(void);
esp_err_t div_board_buttons(uint8_t *pressed);
esp_err_t div_board_brightness(uint8_t value);
esp_err_t div_board_led(uint8_t r, uint8_t g, uint8_t b);
esp_err_t div_board_battery_mv(int *millivolts);
esp_err_t div_board_beep(bool on);
bool div_board_external_gpio(int pin, bool output);
