#pragma once
#include "div_drivers.h"
esp_err_t div_profile_save(const char *path, const div_pulses_t *pulses, uint32_t frequency);
esp_err_t div_profile_load(const char *path, div_pulses_t *pulses, uint32_t *frequency);
esp_err_t div_pulse_append(div_pulses_t *pulses, uint32_t duration, bool level);
esp_err_t div_nec_pulses(uint32_t code, div_pulses_t *pulses);
