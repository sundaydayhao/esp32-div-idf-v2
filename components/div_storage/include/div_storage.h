#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define DIV_PATH_MAX 256
#define DIV_SD_ROOT "/sd"
typedef struct {
    uint32_t version;
    uint8_t brightness, dark, led_enabled, background_scan;
    uint16_t touch_x0, touch_x1, touch_y0, touch_y1;
    uint8_t touch_swap, touch_invert_x, touch_invert_y, board_id;
    uint8_t accent;
} div_settings_t;
esp_err_t div_storage_init(void);
bool div_storage_ready(void);
esp_err_t div_storage_path(const char *relative, char out[DIV_PATH_MAX]);
esp_err_t div_storage_mkdir(const char *relative);
esp_err_t div_storage_write(const char *path, const void *data, size_t len);
esp_err_t div_storage_read(const char *path, void *data, size_t capacity, size_t *len);
esp_err_t div_settings_load(div_settings_t *out);
esp_err_t div_settings_save(const div_settings_t *settings);
esp_err_t div_storage_ota(const char *path);
