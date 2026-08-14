#pragma once

#include "board_config.h"

#ifndef DIV_TFT_HOST
#define DIV_TFT_HOST SPI2_HOST
#endif

#ifndef DIV_SD_HOST
#if defined(BOARD_ESP32_DIV_V2) || defined(BOARD_ESP32_DIV_V3) || defined(BOARD_ESP32_DIV_V4)
#define DIV_SD_HOST SPI3_HOST
#else
#define DIV_SD_HOST SPI2_HOST
#endif
#endif

#ifndef DIV_SD_CS
#if defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1)
#define DIV_SD_CS 5
#else
#define DIV_SD_CS 10
#endif
#endif

#ifndef DIV_SD_MOSI
#if defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1)
#define DIV_SD_MOSI 23
#else
#define DIV_SD_MOSI 11
#endif
#endif

#ifndef DIV_SD_MISO
#if defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1)
#define DIV_SD_MISO 19
#else
#define DIV_SD_MISO 13
#endif
#endif

#ifndef DIV_SD_SCLK
#if defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1)
#define DIV_SD_SCLK 18
#else
#define DIV_SD_SCLK 12
#endif
#endif
