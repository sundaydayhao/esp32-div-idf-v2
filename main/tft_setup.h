#pragma once

#include "board_config.h"

#define USER_SETUP_LOADED

#ifdef ILI9341_DRIVER
#undef ILI9341_DRIVER
#endif
#ifdef ILI9341_2_DRIVER
#undef ILI9341_2_DRIVER
#endif
#ifdef ST7796_DRIVER
#undef ST7796_DRIVER
#endif

#ifdef TFT_MISO
#undef TFT_MISO
#endif
#define TFT_MISO 37
#ifdef TFT_MOSI
#undef TFT_MOSI
#endif
#define TFT_MOSI 35
#ifdef TFT_SCLK
#undef TFT_SCLK
#endif
#define TFT_SCLK 36
#ifdef TFT_CS
#undef TFT_CS
#endif
#define TFT_CS 17
#ifdef TFT_DC
#undef TFT_DC
#endif
#define TFT_DC 16
#ifdef TFT_RST
#undef TFT_RST
#endif
#define TFT_RST -1
#ifdef TFT_BL
#undef TFT_BL
#endif
#define TFT_BL 7
#ifdef TFT_BACKLIGHT_ON
#undef TFT_BACKLIGHT_ON
#endif
#define TFT_BACKLIGHT_ON HIGH

#ifndef TFT_RGB
#define TFT_RGB 0x00
#endif
#ifndef TFT_BGR
#define TFT_BGR 0x08
#endif

#ifdef TFT_RGB_ORDER
#undef TFT_RGB_ORDER
#endif
#define TFT_RGB_ORDER TFT_BGR

#ifdef TFT_INVERSION_OFF
#undef TFT_INVERSION_OFF
#endif
#define TFT_INVERSION_OFF

#if defined(TFT_PANEL_ILI9341)
#define ILI9341_DRIVER
#ifdef SPI_FREQUENCY
#undef SPI_FREQUENCY
#endif
#define SPI_FREQUENCY 27000000
#ifdef SPI_READ_FREQUENCY
#undef SPI_READ_FREQUENCY
#endif
#define SPI_READ_FREQUENCY 20000000
#elif defined(TFT_PANEL_ST7796)
#define ST7796_DRIVER
#ifdef SPI_FREQUENCY
#undef SPI_FREQUENCY
#endif
#define SPI_FREQUENCY 27000000
#ifdef SPI_READ_FREQUENCY
#undef SPI_READ_FREQUENCY
#endif
#define SPI_READ_FREQUENCY 20000000
#else
#error "Unsupported TFT panel selection"
#endif

#if defined(TFT_PANEL_ILI9341)
#undef TFT_WIDTH
#define TFT_WIDTH 240
#undef TFT_HEIGHT
#define TFT_HEIGHT 320
#elif defined(TFT_PANEL_ST7796)
#undef TFT_WIDTH
#define TFT_WIDTH 320
#undef TFT_HEIGHT
#define TFT_HEIGHT 480
#endif

#ifndef TFT_COL_OFFSET
#define TFT_COL_OFFSET 0
#endif
#ifndef TFT_ROW_OFFSET
#define TFT_ROW_OFFSET 0
#endif
