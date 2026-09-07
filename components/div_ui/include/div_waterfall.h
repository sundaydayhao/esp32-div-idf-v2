#pragma once
#include "lvgl.h"
#include "div_spectrum.h"
lv_obj_t *div_waterfall_create(lv_obj_t *parent, int width, int height);
void div_waterfall_update(lv_obj_t *canvas, const uint8_t levels[DIV_SPECTRUM_BINS]);
