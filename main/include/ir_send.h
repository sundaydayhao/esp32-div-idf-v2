#pragma once

#include "ir_remote_esp8266.h"

class IRsend {
 public:
  explicit IRsend(uint16_t sendpin);
  void begin();
  void sendRaw(const uint16_t* buf, uint16_t len, uint16_t hz);
  bool send(decode_type_t type, uint64_t data, uint16_t nbits, uint16_t repeat = 0);

 private:
  void mark(uint32_t usec);
  void space(uint32_t usec);
  void sendPulseDistance(uint64_t data, uint16_t nbits, uint32_t headerMark,
                         uint32_t headerSpace, uint32_t oneMark, uint32_t oneSpace,
                         uint32_t zeroMark, uint32_t zeroSpace, uint32_t footerMark,
                         bool msbFirst);
  void sendManchester(uint64_t data, uint16_t nbits, uint32_t halfPeriod, bool startHigh);

  uint16_t sendpin_;
  uint16_t khz_ = 38;
};
