#pragma once

#include <cstdint>

#include "div_platform.h"

#define SPI_MODE0 DIV_SPI_MODE0

class DivSpiClass {
 public:
  explicit DivSpiClass(int = 0) {}
  void begin(int sck = -1, int miso = -1, int mosi = -1, int ss = -1);
  void end() {}
  void setDataMode(uint8_t mode) { mode_ = mode; }
  void setFrequency(uint32_t hz) { frequency_ = hz; }
  void setBitOrder(uint8_t order) { bitOrder_ = order; }
  uint8_t transfer(uint8_t data);

 private:
  int sck_ = -1;
  int miso_ = -1;
  int mosi_ = -1;
  int ss_ = -1;
  uint8_t mode_ = SPI_MODE0;
  uint8_t bitOrder_ = MSBFIRST;
  uint32_t frequency_ = 1000000;
};

extern DivSpiClass DivSpi;
