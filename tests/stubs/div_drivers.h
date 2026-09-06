#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#define DIV_PULSE_MAX 1024
typedef struct {
    uint32_t carrier_hz,count;
    uint16_t duration[DIV_PULSE_MAX];
    uint8_t level[DIV_PULSE_MAX];
} div_pulses_t;
