#pragma once

#include <cstdint>

class RCSwitch {
 public:
  RCSwitch() = default;

  void resetAvailable() { available_ = false; }
  void setReceiveTolerance(int tolerance) { receiveTolerance_ = tolerance; }
  void disableReceive() { receivingEnabled_ = false; }
  void disableTransmit() { transmittingEnabled_ = false; }
  void enableTransmit(int pin) { transmitPin_ = pin; transmittingEnabled_ = true; }
  void enableReceive(int pin) { receivePin_ = pin; receivingEnabled_ = true; }
  void setRepeatTransmit(uint8_t repeat) { repeat_ = repeat; }
  void setProtocol(int protocol) { receivedProtocol_ = protocol > 0 ? static_cast<unsigned int>(protocol) : 1U; }
  bool available() const { return available_; }
  unsigned long getReceivedValue() const { return receivedValue_; }
  unsigned int getReceivedBitlength() const { return receivedBits_; }
  unsigned int getReceivedProtocol() const { return receivedProtocol_; }
  int getReceiveTolerance() const { return receiveTolerance_; }
  int getReceivePin() const { return receivePin_; }
  int getTransmitPin() const { return transmitPin_; }
  bool receiveEnabled() const { return receivingEnabled_; }
  bool transmitEnabled() const { return transmittingEnabled_; }
  uint8_t repeatTransmit() const { return repeat_; }

  void send(unsigned long value, unsigned int bits) {
    receivedValue_ = value;
    receivedBits_ = bits;
    available_ = false;
  }
  void send(unsigned long value, unsigned int bits, unsigned int protocol) {
    setProtocol(static_cast<int>(protocol));
    send(value, bits);
  }

 private:
  bool available_ = false;
  bool receivingEnabled_ = false;
  bool transmittingEnabled_ = false;
  int receivePin_ = -1;
  int transmitPin_ = -1;
  int receiveTolerance_ = 0;
  uint8_t repeat_ = 1;
  unsigned long receivedValue_ = 0;
  unsigned int receivedBits_ = 0;
  unsigned int receivedProtocol_ = 1;
};
