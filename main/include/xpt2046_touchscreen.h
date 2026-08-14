#pragma once

#include <stdint.h>

#include "driver/spi_master.h"

struct TS_Point {
  int16_t x;
  int16_t y;
  int16_t z;
  TS_Point(int16_t x_ = 0, int16_t y_ = 0, int16_t z_ = 0) : x(x_), y(y_), z(z_) {}
};

class XPT2046_Touchscreen {
 public:
  XPT2046_Touchscreen(uint8_t csPin, uint8_t irqPin = 255);

  void begin();
  void setRotation(uint8_t rotation);
  bool touched();
  bool tirqTouched() const;
  TS_Point getPoint();
  uint16_t getTouchRawZ();
  void getTouchRaw(uint16_t* x, uint16_t* y);

 private:
  void configurePins();
  bool readRaw(uint16_t& x, uint16_t& y, uint16_t& z);
  uint16_t transfer12(uint8_t cmd);

  uint8_t csPin_;
  uint8_t irqPin_;
  uint8_t rotation_;
  uint8_t clkPin_;
  uint8_t mosiPin_;
  uint8_t misoPin_;
  spi_device_handle_t spi_;
  bool spiReady_;
};
