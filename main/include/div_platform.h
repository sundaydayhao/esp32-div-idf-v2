#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "div_string.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef DIV_SPI_MODE0
#define DIV_SPI_MODE0 0
#endif
#ifndef F
#define F(x) x
#endif
#ifndef PROGMEM
#define PROGMEM
#endif

using byte = uint8_t;
using boolean = bool;

inline uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

inline uint32_t micros() {
  return static_cast<uint32_t>(esp_timer_get_time());
}

inline void delay(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

inline void delayMicroseconds(uint32_t us) {
  esp_rom_delay_us(us);
}

inline void yield() {
  vTaskDelay(1);
}

inline bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

template <typename T>
inline T constrain(T x, T lo, T hi) {
  return std::min(std::max(x, lo), hi);
}

template <typename T, typename U, typename V>
inline auto constrain(T x, U lo, V hi) -> decltype(x + lo + hi) {
  using R = decltype(x + lo + hi);
  return std::min(std::max(static_cast<R>(x), static_cast<R>(lo)), static_cast<R>(hi));
}

template <typename T>
inline T min(T a, T b) {
  return std::min(a, b);
}

template <typename T>
inline T max(T a, T b) {
  return std::max(a, b);
}

inline void randomSeed(uint32_t seed) {
  std::srand(seed);
}

inline long random(long maxExclusive) {
  if (maxExclusive <= 0) return 0;
  return static_cast<long>(esp_random() % static_cast<uint32_t>(maxExclusive));
}

inline long random(long minInclusive, long maxExclusive) {
  if (maxExclusive <= minInclusive) return minInclusive;
  return minInclusive + random(maxExclusive - minInclusive);
}

inline void pinMode(int pin, int mode) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (mode == OUTPUT) {
    cfg.mode = GPIO_MODE_OUTPUT;
  } else {
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = (mode == INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  }
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&cfg);
}

inline void digitalWrite(int pin, int level) {
  gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0);
}

inline int digitalRead(int pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

float temperatureRead();

struct DivEspSystem {
  uint32_t getFreeHeap() const { return esp_get_free_heap_size(); }
  void restart() const { esp_restart(); }
};

extern DivEspSystem DivSystem;
