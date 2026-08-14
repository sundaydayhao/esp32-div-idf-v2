#include "div_platform.h"

#include "driver/temperature_sensor.h"
#include "esp_err.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

namespace {
constexpr const char* kTag = "DivPlatform";

#if SOC_TEMPERATURE_SENSOR_SUPPORTED
temperature_sensor_handle_t g_temperatureSensor = nullptr;

bool ensureTemperatureSensor() {
  if (g_temperatureSensor) {
    return true;
  }
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  esp_err_t err = temperature_sensor_install(&cfg, &g_temperatureSensor);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "temperature sensor install failed: %s", esp_err_to_name(err));
    return false;
  }
  err = temperature_sensor_enable(g_temperatureSensor);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "temperature sensor enable failed: %s", esp_err_to_name(err));
    temperature_sensor_uninstall(g_temperatureSensor);
    g_temperatureSensor = nullptr;
    return false;
  }
  return true;
}
#endif
}  // namespace

DivEspSystem DivSystem;

float temperatureRead() {
#if SOC_TEMPERATURE_SENSOR_SUPPORTED
  if (!ensureTemperatureSensor()) {
    return 0.0f;
  }
  float value = 0.0f;
  esp_err_t err = temperature_sensor_get_celsius(g_temperatureSensor, &value);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "temperature sensor read failed: %s", esp_err_to_name(err));
    return 0.0f;
  }
  return value;
#else
  return 0.0f;
#endif
}
