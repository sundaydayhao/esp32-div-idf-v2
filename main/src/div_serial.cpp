#include "div_serial.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

DivSerialClass DivSerial;

namespace {
size_t printFormatted(const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return 0;
  }
  const size_t len = static_cast<size_t>(n);
  if (len < sizeof(buf)) {
    return std::fwrite(buf, 1, len, stdout);
  }

  std::vector<char> dyn(len + 1);
  va_start(ap, fmt);
  std::vsnprintf(dyn.data(), dyn.size(), fmt, ap);
  va_end(ap);
  return std::fwrite(dyn.data(), 1, len, stdout);
}
}  // namespace

void DivSerialClass::begin(uint32_t baud) {
  uart_config_t cfg = {};
  cfg.baud_rate = static_cast<int>(baud);
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(UART_NUM_0, &cfg);
  esp_err_t err = uart_driver_install(UART_NUM_0, 2048, 0, 0, nullptr, 0);
  if (err != ESP_ERR_INVALID_STATE) {
    (void)err;
  }
}

void DivSerialClass::end() {
  uart_driver_delete(UART_NUM_0);
}

int DivSerialClass::available() {
  size_t size = 0;
  if (uart_get_buffered_data_len(UART_NUM_0, &size) != ESP_OK) {
    return 0;
  }
  return static_cast<int>(size);
}

int DivSerialClass::read() {
  uint8_t ch = 0;
  const int n = uart_read_bytes(UART_NUM_0, &ch, 1, 0);
  return n == 1 ? ch : -1;
}

size_t DivSerialClass::print(const char* s) {
  if (!s) {
    return 0;
  }
  const size_t len = std::strlen(s);
  const size_t written = std::fwrite(s, 1, len, stdout);
  std::fflush(stdout);
  return written;
}

size_t DivSerialClass::print(char c) {
  return printFormatted("%c", c);
}

size_t DivSerialClass::print(int v) {
  return printFormatted("%d", v);
}

size_t DivSerialClass::print(unsigned int v) {
  return printFormatted("%u", v);
}

size_t DivSerialClass::print(long v) {
  return printFormatted("%ld", v);
}

size_t DivSerialClass::print(unsigned long v) {
  return printFormatted("%lu", v);
}

size_t DivSerialClass::print(float v) {
  return print(static_cast<double>(v), 2);
}

size_t DivSerialClass::print(float v, int decimals) {
  return print(static_cast<double>(v), decimals);
}

size_t DivSerialClass::print(double v) {
  return print(v, 2);
}

size_t DivSerialClass::print(double v, int decimals) {
  if (decimals < 0) decimals = 2;
  if (decimals > 8) decimals = 8;
  char fmt[12];
  std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  return printFormatted(fmt, v);
}

size_t DivSerialClass::println() {
  return print("\n");
}

size_t DivSerialClass::printf(const char* fmt, ...) {
  if (!fmt) {
    return 0;
  }
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return 0;
  }
  const size_t len = static_cast<size_t>(n);
  if (len < sizeof(buf)) {
    return print(buf);
  }
  std::vector<char> dyn(len + 1);
  va_start(ap, fmt);
  std::vsnprintf(dyn.data(), dyn.size(), fmt, ap);
  va_end(ap);
  return print(dyn.data());
}

DivUartSerial::DivUartSerial(int uartNum)
    : uart_(static_cast<uart_port_t>(uartNum)) {}

void DivUartSerial::begin(uint32_t baud, uint32_t, int rxPin, int txPin) {
  uart_config_t cfg = {};
  cfg.baud_rate = static_cast<int>(baud);
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(uart_, &cfg);
  uart_set_pin(uart_, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  esp_err_t err = uart_driver_install(uart_, 4096, 0, 0, nullptr, 0);
  installed_ = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

void DivUartSerial::end() {
  if (installed_) {
    uart_driver_delete(uart_);
    installed_ = false;
  }
}

int DivUartSerial::available() {
  size_t size = 0;
  if (!installed_ || uart_get_buffered_data_len(uart_, &size) != ESP_OK) {
    return 0;
  }
  return static_cast<int>(size);
}

int DivUartSerial::read() {
  uint8_t ch = 0;
  const int n = installed_ ? uart_read_bytes(uart_, &ch, 1, 0) : 0;
  return n == 1 ? ch : -1;
}

size_t DivUartSerial::readBytesUntil(char delim, char* buffer, size_t len) {
  if (!installed_ || !buffer || len == 0) {
    return 0;
  }
  size_t count = 0;
  while (count < len) {
    uint8_t ch = 0;
    const int n = uart_read_bytes(uart_, &ch, 1, pdMS_TO_TICKS(20));
    if (n != 1) {
      break;
    }
    if (ch == static_cast<uint8_t>(delim)) {
      break;
    }
    buffer[count++] = static_cast<char>(ch);
  }
  return count;
}
