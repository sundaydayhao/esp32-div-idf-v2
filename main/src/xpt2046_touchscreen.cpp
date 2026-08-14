#include "xpt2046_touchscreen.h"

#include <string.h>

#include "board_pins.h"
#include "shared.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {

static const char* TAG = "xpt2046";
static constexpr int kSpiClockHz = 1000000;
static constexpr uint16_t kAdcMax = 4095;

}  // namespace

XPT2046_Touchscreen::XPT2046_Touchscreen(uint8_t csPin, uint8_t irqPin)
    : csPin_(csPin),
      irqPin_(irqPin),
      rotation_(0),
      clkPin_(XPT2046_CLK),
      mosiPin_(XPT2046_MOSI),
      misoPin_(XPT2046_MISO),
      spi_(nullptr),
      spiReady_(false) {}

void XPT2046_Touchscreen::configurePins() {
  gpio_config_t io = {};

  if (irqPin_ < 255) {
    io.pin_bit_mask = 1ULL << irqPin_;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
  }

  if (spiReady_) {
    return;
  }

  spi_bus_config_t bus = {};
  bus.mosi_io_num = mosiPin_;
  bus.miso_io_num = misoPin_;
  bus.sclk_io_num = clkPin_;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 8;

  esp_err_t err = ESP_OK;
  if (XPT2046_HOST != DIV_TFT_HOST) {
    err = spi_bus_initialize(XPT2046_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
      return;
    }
  }

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = kSpiClockHz;
  dev.mode = 0;
  dev.spics_io_num = csPin_;
  dev.queue_size = 1;

  err = spi_bus_add_device(XPT2046_HOST, &dev, &spi_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SPI device add failed: %s", esp_err_to_name(err));
    spi_ = nullptr;
    return;
  }
  spiReady_ = true;
}

void XPT2046_Touchscreen::begin() {
  configurePins();
  ESP_LOGI(TAG, "begin cs=%u clk=%u mosi=%u miso=%u irq=%u",
           (unsigned)csPin_, (unsigned)clkPin_, (unsigned)mosiPin_,
           (unsigned)misoPin_, (unsigned)irqPin_);
}

void XPT2046_Touchscreen::setRotation(uint8_t rotation) {
  rotation_ = rotation;
}

bool XPT2046_Touchscreen::tirqTouched() const {
  if (irqPin_ >= 255) {
    return true;
  }
  return gpio_get_level((gpio_num_t)irqPin_) == 0;
}

bool XPT2046_Touchscreen::touched() {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t z = 0;
  return readRaw(x, y, z) && z > 0;
}

uint16_t XPT2046_Touchscreen::getTouchRawZ() {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t z = 0;
  if (!readRaw(x, y, z)) {
    return 0;
  }
  return z;
}

void XPT2046_Touchscreen::getTouchRaw(uint16_t* x, uint16_t* y) {
  uint16_t rx = 0;
  uint16_t ry = 0;
  uint16_t rz = 0;
  if (!readRaw(rx, ry, rz)) {
    if (x) *x = 0;
    if (y) *y = 0;
    return;
  }
  if (x) *x = rx;
  if (y) *y = ry;
}

TS_Point XPT2046_Touchscreen::getPoint() {
  uint16_t rx = 0;
  uint16_t ry = 0;
  uint16_t rz = 0;
  if (!readRaw(rx, ry, rz)) {
    return TS_Point();
  }
  return TS_Point((int16_t)rx, (int16_t)ry, (int16_t)rz);
}

bool XPT2046_Touchscreen::readRaw(uint16_t& x, uint16_t& y, uint16_t& z) {
  configurePins();
  if (!spiReady_) {
    return false;
  }

  uint16_t x1 = transfer12(0xD0);
  uint16_t y1 = transfer12(0x90);
  uint16_t z1 = transfer12(0xB0);
  uint16_t z2 = transfer12(0xC0);

  x = x1;
  y = y1;
  if ((z1 == 0 && z2 == 0) || (z1 == kAdcMax && z2 == kAdcMax)) {
    z = 0;
  } else {
    int32_t pressure = (int32_t)z1 + kAdcMax - (int32_t)z2;
    if (pressure < 0) pressure = 0;
    if (pressure > kAdcMax) pressure = kAdcMax;
    z = (uint16_t)pressure;
  }

  return true;
}

uint16_t XPT2046_Touchscreen::transfer12(uint8_t cmd) {
  if (!spi_) {
    return 0;
  }

  uint8_t tx[3] = {cmd, 0, 0};
  uint8_t rx[3] = {};
  spi_transaction_t t = {};
  t.length = sizeof(tx) * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  esp_err_t err = spi_device_polling_transmit(spi_, &t);
  if (err != ESP_OK) {
    return 0;
  }
  return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3) & 0x0FFF;
}
