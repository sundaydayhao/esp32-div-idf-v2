#pragma once

#include "ir_remote_esp8266.h"

class IRrecv {
 public:
  explicit IRrecv(uint16_t recvpin);
  void enableIRIn();
  void disableIRIn();
  bool decode(decode_results* results);
  void resume();

 private:
  uint16_t recvpin_;
  bool enabled_ = false;
};
