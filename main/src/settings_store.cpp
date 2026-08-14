#include "settings_store.h"

#include <div_string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static AppSettings g_settings;
AppSettings& settings() { return g_settings; }

static const char* TAG = "settings";

static const AccentOption kAccentPresets[] = {
  {"Orange", 0xFBE4},
  {"Green",  0x07E0},
  {"Red",    0xF800},
  {"Cyan",   0x07FF},
  {"Purple", 0xF81F},
  {"Yellow", 0xFFE0},
  {"White",  0xFFFF},
};

uint8_t accentPresetClamp(uint8_t preset) {
  if (preset >= ACCENT_PRESET_COUNT) return 0;
  return preset;
}

uint16_t accentColor565(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].color565;
}

const char* accentPresetName(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].name;
}

const char* settingsBoardProfileId() {
  return TOUCH_PROFILE_ID;
}

void settingsApplyBoardTouchDefaults() {
  auto& s = g_settings;
  s.touchXMin = TOUCH_X_MIN;
  s.touchXMax = TOUCH_X_MAX;
  s.touchYMin = TOUCH_Y_MIN;
  s.touchYMax = TOUCH_Y_MAX;
}

static bool settingsOpen(nvs_open_mode_t mode, nvs_handle_t* out) {
  esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, mode, out);
  if (err == ESP_ERR_NVS_NOT_FOUND && mode == NVS_READONLY) {
    return false;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

static bool settingsGetU8(nvs_handle_t h, const char* key, uint8_t* value) {
  uint8_t tmp = 0;
  esp_err_t err = nvs_get_u8(h, key, &tmp);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_get_u8(%s) failed: %s", key, esp_err_to_name(err));
    return false;
  }
  *value = tmp;
  return true;
}

static bool settingsGetBool(nvs_handle_t h, const char* key, bool* value) {
  uint8_t tmp = *value ? 1 : 0;
  if (settingsGetU8(h, key, &tmp)) {
    *value = tmp != 0;
    return true;
  }
  return false;
}

static bool settingsGetU16(nvs_handle_t h, const char* key, uint16_t* value) {
  uint16_t tmp = 0;
  esp_err_t err = nvs_get_u16(h, key, &tmp);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return false;
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_get_u16(%s) failed: %s", key, esp_err_to_name(err));
    return false;
  }
  *value = tmp;
  return true;
}

bool settingsLoad() {
  settingsApplyBoardTouchDefaults();

  nvs_handle_t h = 0;
  if (!settingsOpen(NVS_READONLY, &h)) {
    return true;
  }

  auto& s = g_settings;
  uint8_t u8 = 0;
  if (settingsGetU8(h, "brightness", &u8)) {
    s.brightness = u8;
  }
  if (settingsGetU8(h, "theme", &u8)) {
    s.theme = (u8 == (uint8_t)Theme::Light) ? Theme::Light : Theme::Dark;
  }
  if (settingsGetU8(h, "accent", &u8)) {
    s.accentColor = accentPresetClamp(u8);
  }
  settingsGetBool(h, "neopixel", &s.neopixelEnabled);

  settingsGetBool(h, "auto_wifi", &s.autoWifiScan);
  settingsGetBool(h, "auto_ble", &s.autoBleScan);

  if (s.autoWifiScan != s.autoBleScan) {
    bool en = (s.autoWifiScan || s.autoBleScan);
    s.autoWifiScan = en;
    s.autoBleScan  = en;
  }

  char board[32] = {};
  size_t boardLen = sizeof(board);
  bool touchForThisBoard = false;
  esp_err_t boardErr = nvs_get_str(h, "board", board, &boardLen);
  if (boardErr == ESP_OK) {
    touchForThisBoard = (strcmp(board, TOUCH_PROFILE_ID) == 0);
  } else if (boardErr == ESP_ERR_NVS_NOT_FOUND) {
    touchForThisBoard = false;
  } else {
    ESP_LOGW(TAG, "nvs_get_str(board) failed: %s", esp_err_to_name(boardErr));
  }

  if (touchForThisBoard) {
    settingsGetU16(h, "touch_x_min", &s.touchXMin);
    settingsGetU16(h, "touch_x_max", &s.touchXMax);
    settingsGetU16(h, "touch_y_min", &s.touchYMin);
    settingsGetU16(h, "touch_y_max", &s.touchYMax);
  } else if (boardErr != ESP_ERR_NVS_NOT_FOUND) {
    settingsApplyBoardTouchDefaults();
  }

  nvs_close(h);
  return true;
}

bool settingsSave() {

  nvs_handle_t h = 0;
  if (!settingsOpen(NVS_READWRITE, &h)) {
    return false;
  }

  auto& s = g_settings;
  esp_err_t err = ESP_OK;
  if (err == ESP_OK) err = nvs_set_str(h, "board", TOUCH_PROFILE_ID);
  if (err == ESP_OK) err = nvs_set_u8(h, "brightness", s.brightness);
  if (err == ESP_OK) err = nvs_set_u8(h, "theme", (uint8_t)s.theme);
  if (err == ESP_OK) err = nvs_set_u8(h, "accent", accentPresetClamp(s.accentColor));
  if (err == ESP_OK) err = nvs_set_u8(h, "neopixel", s.neopixelEnabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(h, "auto_wifi", s.autoWifiScan ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(h, "auto_ble", s.autoBleScan ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u16(h, "touch_x_min", s.touchXMin);
  if (err == ESP_OK) err = nvs_set_u16(h, "touch_x_max", s.touchXMax);
  if (err == ESP_OK) err = nvs_set_u16(h, "touch_y_min", s.touchYMin);
  if (err == ESP_OK) err = nvs_set_u16(h, "touch_y_max", s.touchYMax);
  if (err == ESP_OK) err = nvs_commit(h);

  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "settings save failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}
