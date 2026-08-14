#include "div_spi.h"

#include "esp_rom_sys.h"

DivSpiClass DivSpi;

void DivSpiClass::begin(int sck, int miso, int mosi, int ss) {
  sck_ = sck;
  miso_ = miso;
  mosi_ = mosi;
  ss_ = ss;

  if (sck_ >= 0) {
    pinMode(sck_, OUTPUT);
    digitalWrite(sck_, LOW);
  }
  if (mosi_ >= 0) {
    pinMode(mosi_, OUTPUT);
    digitalWrite(mosi_, LOW);
  }
  if (miso_ >= 0) {
    pinMode(miso_, INPUT);
  }
  if (ss_ >= 0) {
    pinMode(ss_, OUTPUT);
    digitalWrite(ss_, HIGH);
  }
}

uint8_t DivSpiClass::transfer(uint8_t data) {
  if (sck_ < 0) {
    return 0;
  }

  const uint32_t halfPeriodUs = frequency_ > 0 ? std::max<uint32_t>(1, 500000UL / frequency_) : 1;
  uint8_t out = 0;
  for (int i = 0; i < 8; ++i) {
    const int bit = bitOrder_ == MSBFIRST ? (7 - i) : i;
    if (mosi_ >= 0) {
      digitalWrite(mosi_, (data >> bit) & 0x01);
    }
    esp_rom_delay_us(halfPeriodUs);
    digitalWrite(sck_, HIGH);
    esp_rom_delay_us(halfPeriodUs);
    if (miso_ >= 0 && digitalRead(miso_)) {
      out |= (1U << bit);
    }
    digitalWrite(sck_, LOW);
  }
  return out;
}
