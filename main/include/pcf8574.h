#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

class PCF8574 {
 public:
  explicit PCF8574(uint8_t address = 0x20);

  bool begin(uint8_t address);
  void pinMode(uint8_t pin, uint8_t mode);
  int digitalRead(uint8_t pin);
  void digitalWrite(uint8_t pin, uint8_t level);
  bool readPortState(uint8_t& value);

  uint8_t address() const;

 private:
  bool ensureReady();
  bool readPort(uint8_t& value);
  bool readPortLocked(uint8_t& value);
  bool writePort(uint8_t value);
  bool writePortLocked(uint8_t value);

  uint8_t address_;
  uint8_t pinModes_;
  uint8_t pinStates_;
  bool ready_;
  i2c_master_dev_handle_t device_;
};
