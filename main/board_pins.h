#pragma once

#include "board_config.h"

#ifndef DIV_TFT_HOST
#define DIV_TFT_HOST SPI2_HOST
#endif

#ifndef DIV_SD_HOST
#define DIV_SD_HOST SPI3_HOST
#endif

#ifndef DIV_SD_CS
#define DIV_SD_CS 10
#endif

#ifndef DIV_SD_MOSI
#define DIV_SD_MOSI 11
#endif

#ifndef DIV_SD_MISO
#define DIV_SD_MISO 13
#endif

#ifndef DIV_SD_SCLK
#define DIV_SD_SCLK 12
#endif
