#pragma once

#include <cstdint>
#include <cstddef>

#include "div_string.h"
#include "driver/uart.h"

#define SERIAL_8N1 0

class DivSerialClass {
 public:
  void begin(uint32_t baud);
  void end();
  int available();
  int read();
  size_t print(const char* s);
  size_t print(const DivString& s) { return print(s.c_str()); }
  size_t print(char c);
  size_t print(int v);
  size_t print(unsigned int v);
  size_t print(long v);
  size_t print(unsigned long v);
  size_t print(float v);
  size_t print(float v, int decimals);
  size_t print(double v);
  size_t print(double v, int decimals);
  size_t println();
  template <typename T>
  size_t println(const T& v) {
    const size_t n = print(v);
    return n + println();
  }
  size_t printf(const char* fmt, ...);
};

class DivUartSerial {
 public:
  explicit DivUartSerial(int uartNum);
  void begin(uint32_t baud, uint32_t config, int rxPin, int txPin);
  void end();
  int available();
  int read();
  size_t readBytesUntil(char delim, char* buffer, size_t len);

 private:
  uart_port_t uart_;
  bool installed_ = false;
};

extern DivSerialClass DivSerial;
