#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y; } div_touch_point_t;
typedef struct {
    uint16_t x0, x1, y0, y1;
    bool swap, invert_x, invert_y;
} div_touch_calibration_t;
typedef struct {
    bool down, blocked, tracking, releasing;
    uint32_t since, release_since;
    div_touch_point_t last, candidate;
} div_touch_filter_t;

void div_touch_block(div_touch_filter_t *filter);
bool div_touch_filter(div_touch_filter_t *filter, bool valid, div_touch_point_t raw,
                      uint32_t now_ms, div_touch_point_t *point);
bool div_touch_map(const div_touch_calibration_t *cal, div_touch_point_t raw,
                   int width, int height, div_touch_point_t *point);
/* Samples are screen TL, TR, BR, BL. Targets are inset exactly 20 pixels. */
bool div_touch_fit(const div_touch_point_t samples[4], int width, int height,
                   unsigned rotation, div_touch_calibration_t *cal);
