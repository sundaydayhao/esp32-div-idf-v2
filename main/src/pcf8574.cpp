#include "pcf8574.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"

#ifndef PCF8574_I2C_PORT
#define PCF8574_I2C_PORT I2C_NUM_0
#endif

#ifndef PCF8574_I2C_SDA
#define PCF8574_I2C_SDA GPIO_NUM_8
#endif

#ifndef PCF8574_I2C_SCL
#define PCF8574_I2C_SCL GPIO_NUM_9
#endif

#ifndef PCF8574_I2C_FREQ_HZ
#define PCF8574_I2C_FREQ_HZ 400000
#endif

namespace {

struct BusState {
  i2c_master_bus_handle_t bus = nullptr;
  bool ready = false;
  SemaphoreHandle_t lock = nullptr;
};

BusState& busState() {
  static BusState state;
  return state;
}

bool ensureBus() {
  auto& state = busState();
  if (state.ready) {
    return true;
  }

  static const char* TAG = "pcf8574";
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = PCF8574_I2C_PORT,
      .sda_io_num = PCF8574_I2C_SDA,
      .scl_io_num = PCF8574_I2C_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      // 0 = synchronous master. The driver only uses blocking
      // i2c_master_transmit/receive; a non-zero depth switches the bus to
      // async mode, which hangs when mixed with the blocking API.
      .trans_queue_depth = 0,
      .flags = {
          .enable_internal_pullup = 1,
          .allow_pd = 0,
      },
  };

  ESP_LOGI(TAG, "init I2C bus port=%d sda=%d scl=%d",
           (int)PCF8574_I2C_PORT, (int)PCF8574_I2C_SDA, (int)PCF8574_I2C_SCL);
  esp_err_t err = i2c_new_master_bus(&bus_config, &state.bus);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    return false;
  }
  if (err == ESP_ERR_INVALID_STATE) {
    esp_err_t probe = i2c_master_get_bus_handle(PCF8574_I2C_PORT, &state.bus);
    if (probe != ESP_OK || state.bus == nullptr) {
      ESP_LOGE(TAG, "i2c_master_get_bus_handle failed: %s", esp_err_to_name(probe));
      return false;
    }
  }

  if (state.lock == nullptr) {
    state.lock = xSemaphoreCreateMutex();
    if (state.lock == nullptr) {
      ESP_LOGE(TAG, "failed to create PCF8574 mutex");
      return false;
    }
  }

  state.ready = true;
  return true;
}

bool lockBus(BusState& state) {
  if (state.lock == nullptr && !ensureBus()) {
    return false;
  }
  if (state.lock == nullptr) {
    return false;
  }
  return xSemaphoreTake(state.lock, pdMS_TO_TICKS(100)) == pdTRUE;
}

void unlockBus(BusState& state) {
  if (state.lock != nullptr) {
    xSemaphoreGive(state.lock);
  }
}

}  // namespace

PCF8574::PCF8574(uint8_t address)
    : address_(address), pinModes_(0x00), pinStates_(0xFF), ready_(false), device_(nullptr) {}

bool PCF8574::begin(uint8_t address) {
  auto& state = busState();
  if (!lockBus(state)) {
    return false;
  }

  address_ = address;
  pinModes_ = 0x00;
  pinStates_ = 0xFF;
  ready_ = false;
  if (device_ != nullptr) {
    (void)i2c_master_bus_rm_device(device_);
    device_ = nullptr;
    if (state.bus == nullptr) {
      state.ready = false;
    }
  }
  device_ = nullptr;

  if (!ensureBus()) {
    unlockBus(state);
    return false;
  }

  if (state.bus == nullptr) {
    unlockBus(state);
    return false;
  }

  const esp_err_t probe = i2c_master_probe(state.bus, address_, 50);
  if (probe != ESP_OK) {
    ESP_LOGW("pcf8574", "PCF8574 probe failed at 0x%02X: %s", address_, esp_err_to_name(probe));
    unlockBus(state);
    return false;
  }

  const i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address_,
      .scl_speed_hz = PCF8574_I2C_FREQ_HZ,
      .scl_wait_us = 0,
      .flags = {
          .disable_ack_check = 0,
      },
  };

  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(state.bus, &dev_config, &dev) != ESP_OK) {
    ESP_LOGE("pcf8574", "i2c_master_bus_add_device failed for 0x%02X", address_);
    unlockBus(state);
    return false;
  }

  device_ = dev;
  ready_ = true;

  if (!writePortLocked(pinStates_)) {
    ESP_LOGW("pcf8574", "PCF8574 initial write failed at 0x%02X", address_);
    (void)i2c_master_bus_rm_device(dev);
    device_ = nullptr;
    ready_ = false;
    unlockBus(state);
    return false;
  }

  uint8_t initial = 0xFF;
  if (!readPortLocked(initial)) {
    ESP_LOGW("pcf8574", "PCF8574 read failed at 0x%02X", address_);
    (void)i2c_master_bus_rm_device(dev);
    device_ = nullptr;
    ready_ = false;
    unlockBus(state);
    return false;
  }

  unlockBus(state);
  return true;
}

void PCF8574::pinMode(uint8_t pin, uint8_t mode) {
  if (pin > 7) {
    return;
  }
  if (mode == 0) {
    pinModes_ |= (uint8_t)(1U << pin);
    pinStates_ |= (uint8_t)(1U << pin);
    (void)writePort(pinStates_);
  } else {
    pinModes_ &= (uint8_t)~(1U << pin);
    pinStates_ |= (uint8_t)(1U << pin);
    (void)writePort(pinStates_);
  }
}

int PCF8574::digitalRead(uint8_t pin) {
  uint8_t value = 0xFF;
  if (!readPort(value)) {
    return 1;
  }
  if (pin > 7) {
    return 1;
  }
  return (value & (uint8_t)(1U << pin)) ? 1 : 0;
}

void PCF8574::digitalWrite(uint8_t pin, uint8_t level) {
  if (pin > 7) {
    return;
  }
  if (level) {
    pinStates_ |= (uint8_t)(1U << pin);
  } else {
    pinStates_ &= (uint8_t)~(1U << pin);
  }
  (void)writePort(pinStates_);
}

uint8_t PCF8574::address() const {
  return address_;
}

bool PCF8574::ensureReady() {
  return ensureBus() && device_ != nullptr && ready_;
}

bool PCF8574::readPort(uint8_t& value) {
  auto& state = busState();
  if (!lockBus(state)) {
    return false;
  }
  const bool ok = readPortLocked(value);
  unlockBus(state);
  return ok;
}

bool PCF8574::readPortLocked(uint8_t& value) {
  if (!ensureReady()) {
    return false;
  }

  uint8_t buf = 0xFF;
  const esp_err_t err = i2c_master_receive(device_, &buf, 1, 20);
  if (err != ESP_OK) {
    ESP_LOGW("pcf8574", "PCF8574 receive failed at 0x%02X: %s", address_, esp_err_to_name(err));
    return false;
  }
  value = buf;
  return true;
}

bool PCF8574::readPortState(uint8_t& value) {
  if (readPort(value)) {
    return true;
  }

  if (device_ == nullptr) {
    return false;
  }

  auto& state = busState();
  if (!lockBus(state)) {
    return false;
  }
  (void)i2c_master_bus_rm_device(device_);
  device_ = nullptr;
  ready_ = false;
  if (state.bus == nullptr || !ensureBus()) {
    unlockBus(state);
    return false;
  }

  const i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address_,
      .scl_speed_hz = PCF8574_I2C_FREQ_HZ,
      .scl_wait_us = 0,
      .flags = {
          .disable_ack_check = 0,
      },
  };

  if (i2c_master_bus_add_device(state.bus, &dev_config, &device_) != ESP_OK) {
    device_ = nullptr;
    ready_ = false;
    unlockBus(state);
    return false;
  }
  ready_ = true;
  const bool ok = readPortLocked(value);
  unlockBus(state);
  return ok;
}

bool PCF8574::writePort(uint8_t value) {
  auto& state = busState();
  if (!lockBus(state)) {
    return false;
  }
  const bool ok = writePortLocked(value);
  unlockBus(state);
  return ok;
}

bool PCF8574::writePortLocked(uint8_t value) {
  if (!ensureReady()) {
    return false;
  }

  const uint8_t out = value;
  return i2c_master_transmit(device_, &out, 1, 20) == ESP_OK;
}
