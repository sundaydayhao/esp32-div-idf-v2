#include "div_display.h"
#include "pcf8574.h"
#include "app.h"
#include "settings_store.h"
#include "touchscreen.h"
#include "config.h"
#include "ducky.h"
#include "icon.h"
#include "ir.h"
#include "gps.h"
#include "rfid.h"
#include "shared.h"
#include "ui_self_test.h"
#include "utils.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include <cstdio>

static void drawTouchNavBar();
void handleButtons();

DivDisplay tft = DivDisplay();

PCF8574 pcf(PCF8574_I2C_ADDR);

static const char* TAG = "esp32_div";
static const char* TOUCH_TAG = "esp32_div_touch_hit";
static bool s_backlightPwmReady = false;

static void initBuzzerIdle() {
#if BUZZER_PIN >= 0
  gpio_set_direction((gpio_num_t)BUZZER_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)BUZZER_PIN, BUZZER_INACTIVE_LEVEL ? 1 : 0);
#endif
}

static void initBacklightPwm() {
  if (BACKLIGHT_PIN < 0 || s_backlightPwmReady) {
    return;
  }

  const ledc_timer_config_t timerConfig = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = PWM_FREQ,
      .clk_cfg = LEDC_AUTO_CLK,
      .deconfigure = false,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timerConfig));

  const ledc_channel_config_t channelConfig = {
      .gpio_num = BACKLIGHT_PIN,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_0,
      .duty = 0,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {.output_invert = 0},
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channelConfig));
  s_backlightPwmReady = true;
}

void setBrightness(uint8_t value) {
  if (BACKLIGHT_PIN < 0) {
    return;
  }
  if (!s_backlightPwmReady) {
    initBacklightPwm();
  }
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, value));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

bool feature_exit_requested = false;

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "2.4GHz",
    "More",
    "Settings",
    "Bluetooth",
    "SubGHz",
    "Tools",
    "About"};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_jammer,
    bitmap_icon_dialog,
    bitmap_icon_setting,
    bitmap_icon_spoofer,
    bitmap_icon_analyzer,
    bitmap_icon_stat,
    bitmap_icon_question};

int current_menu_index = 0;
bool is_main_menu = false;

const int NUM_SUBMENU_ITEMS = 8;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Back to Main Menu"};

const int bluetooth_NUM_SUBMENU_ITEMS = 7;
const char *bluetooth_submenu_items[bluetooth_NUM_SUBMENU_ITEMS] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "Sniffer",
    "BLE Scanner",
    "BLE Rubber Ducky",
    "Back to Main Menu"};

const int nrf_NUM_SUBMENU_ITEMS = 3;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "Back to Main Menu"};

const int subghz_NUM_SUBMENU_ITEMS = 4;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "SubGHz Jammer",
    "Saved Profile",
    "Back to Main Menu"};

const int tools_NUM_SUBMENU_ITEMS = 5;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Console",
    "Update Firmware",
    "Touch Calibrate",
    "SD File Manager",
    "Back to Main Menu"};

static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_IR   = 1;
static constexpr uint8_t OTHER_LAYER_RFID = 2;
static constexpr uint8_t OTHER_LAYER_GPS  = 3;

const int other_NUM_SUBMENU_ITEMS = 4;
static constexpr int OTHER_GRID_COLS = 2;
const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "IR Remote",
    "RFID/NFC",
    "GPS",
    "Main Menu"};

const int rfid_NUM_SUBMENU_ITEMS = 9;
const char *rfid_submenu_items[rfid_NUM_SUBMENU_ITEMS] = {
    "Card Reader",
    "Card Clone",
    "Erase",
    "Dump",
    "Decode Access",
    "Jam Reader",
    "Tag Disrupt",
    "Disrupt Emulate",
    "Back to Main Menu"};

const int gps_NUM_SUBMENU_ITEMS = 3;
const char *gps_submenu_items[gps_NUM_SUBMENU_ITEMS] = {
    "Wardriver",
    "Satellite Scanner",
    "Back to Main Menu"};

const int ir_NUM_SUBMENU_ITEMS = 4;
const char *ir_submenu_items[ir_NUM_SUBMENU_ITEMS] = {
    "Record",
    "Saved Profile",
    "Universal Controller",
    "Back to Main Menu"};

const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

const int setting_NUM_SUBMENU_ITEMS = 1;
const char *setting_submenu_items[setting_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;
uint8_t other_layer = OTHER_LAYER_HOME;
int last_other_menu_index = -1;
bool other_menu_grid_initialized = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_go_back
};

const unsigned char *bluetooth_submenu_icons[bluetooth_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_ble_jammer,
    bitmap_icon_spoofer,
    bitmap_icon_apple,
    bitmap_icon_analyzer,
    bitmap_icon_graph,
    bitmap_icon_rubber_ducky,
    bitmap_icon_go_back
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_kill,
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_no_signal,
    bitmap_icon_list,
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_undo,
    bitmap_icon_sdcard,
    bitmap_icon_go_back
};

const unsigned char *other_submenu_icons[other_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_rfid_chip,
    bitmap_icon_satellite,
    bitmap_icon_go_back
};

const unsigned char *rfid_submenu_icons[rfid_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_magnifying_glass,
    bitmap_icon_follow,
    bitmap_icon_recycle,
    bitmap_icon_dot_matrix,
    bitmap_icon_key,
    bitmap_icon_kill,
    bitmap_icon_flash,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *gps_submenu_icons[gps_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_satellite,
    bitmap_icon_satellite_dish,
    bitmap_icon_go_back
};

const unsigned char *ir_submenu_icons[ir_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_list,
    bitmap_icon_remote_control,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *setting_submenu_icons[setting_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char **active_submenu_icons = nullptr;

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0:
            active_submenu_items = submenu_items;
            active_submenu_size = NUM_SUBMENU_ITEMS;
            active_submenu_icons = wifi_submenu_icons;
            break;
        case 1:
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 2:
            if (other_layer == OTHER_LAYER_HOME) {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            } else if (other_layer == OTHER_LAYER_IR) {
                active_submenu_items = ir_submenu_items;
                active_submenu_size = ir_NUM_SUBMENU_ITEMS;
                active_submenu_icons = ir_submenu_icons;
            } else if (other_layer == OTHER_LAYER_RFID) {
                active_submenu_items = rfid_submenu_items;
                active_submenu_size = rfid_NUM_SUBMENU_ITEMS;
                active_submenu_icons = rfid_submenu_icons;
            } else if (other_layer == OTHER_LAYER_GPS) {
                active_submenu_items = gps_submenu_items;
                active_submenu_size = gps_NUM_SUBMENU_ITEMS;
                active_submenu_icons = gps_submenu_icons;
            } else {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            }
            break;
        case 3:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
        case 4:
            active_submenu_items = bluetooth_submenu_items;
            active_submenu_size = bluetooth_NUM_SUBMENU_ITEMS;
            active_submenu_icons = bluetooth_submenu_icons;
            break;
        case 5:
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 6:
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;
        case 7:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

static bool touchButtonInputEnabled = false;
static bool touchButtonCueDrawn = false;
static bool s_touchNavLabelsConfigured = false;
static bool s_touchNavHeld[5] = {false, false, false, false, false};
static bool s_touchNavSampleValid = false;
static int s_touchNavSampleSlot = -1;
static uint32_t s_touchNavSampleMs = 0;
#if HAS_PCF8574_BUTTONS
static bool s_pcfButtonLastState[8] = {true, true, true, true, true, true, true, true};
#endif
static FeatureUI::Button s_touchNavBtns[5];
static const char* s_touchNavLabels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr int16_t TOUCH_NAV_BAR_H = (FeatureUI::FOOTER_H * 4) / 5;  // 20% shorter than footer
static constexpr uint32_t TOUCH_NAV_SAMPLE_CACHE_MS = 20;

static int touchNavPinForIndex(int idx) {
  switch (idx) {
    case 0: return BTN_LEFT;
    case 1: return BTN_DOWN;
    case 2: return BTN_SELECT;
    case 3: return BTN_UP;
    case 4: return BTN_RIGHT;
    default: return -1;
  }
}

static void invalidateTouchNavSampleCache() {
  s_touchNavSampleValid = false;
  s_touchNavSampleSlot = -1;
  s_touchNavSampleMs = 0;
}

void setTouchButtonInputEnabled(bool enabled) {
  if (touchButtonInputEnabled != enabled) {
    touchButtonCueDrawn = false;
    invalidateTouchNavSampleCache();
    if (!enabled) {
      for (int i = 0; i < 5; ++i) {
        s_touchNavLabels[i] = nullptr;
      }
      s_touchNavLabelsConfigured = false;
      for (int i = 0; i < 5; ++i) {
        s_touchNavHeld[i] = false;
      }
    }
  }
  touchButtonInputEnabled = enabled;
#if TOUCH_BUTTON_CUE_ENABLED
  if (enabled && feature_active) {
    drawTouchNavBar();
    touchButtonCueDrawn = true;
  }
#endif
}

bool featureHasTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  return touchButtonInputEnabled && feature_active;
#else
  return false;
#endif
}

void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right) {
  s_touchNavLabels[0] = left;
  s_touchNavLabels[1] = down;
  s_touchNavLabels[2] = center;
  s_touchNavLabels[3] = up;
  s_touchNavLabels[4] = right;
  s_touchNavLabelsConfigured = true;
  invalidateTouchButtonCue();
}

void invalidateTouchButtonCue() {
  touchButtonCueDrawn = false;
}

void resetTouchNavHeldState() {
  for (int i = 0; i < 5; ++i) {
    s_touchNavHeld[i] = false;
  }
  invalidateTouchNavSampleCache();
}

void redrawTouchButtonBar() {
  invalidateTouchButtonCue();
  drawTouchButtonCue();
}

static void layoutTouchNavBtns() {
  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int totalW = tft.width();
  const int cellW = totalW / 5;

  for (int i = 0; i < 5; ++i) {
    const int x = i * cellW;
    const int w = (i == 4) ? (totalW - x) : cellW;
    s_touchNavBtns[i] = {
      (int16_t)x, (int16_t)barY, (int16_t)w, (int16_t)barH,
      nullptr, FeatureUI::ButtonStyle::Secondary, false};
  }
}

static DivString fitTouchNavLabel(const char* label, int maxWidth) {
  if (!label || !label[0]) {
    return DivString();
  }
  DivString out = label;
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static void drawTouchNavBar() {
  static const unsigned char* kIcons[5] = {
    bitmap_icon_LEFT,
    bitmap_icon_DOWN,
    bitmap_icon_go_back,
    bitmap_icon_UP,
    bitmap_icon_RIGHT,
  };
  constexpr int kIconSize = 16;

  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int barW = tft.width();

  layoutTouchNavBtns();

  tft.fillRect(0, barY, barW, barH, UI_FG);
  tft.drawFastHLine(0, barY, barW, UI_LINE);

  for (int i = 0; i < 5; ++i) {
    const auto& b = s_touchNavBtns[i];
    if (i > 0) {
      tft.drawFastVLine(b.x, barY + 3, barH - 6, UI_LINE);
    }

    if (s_touchNavLabels[i] && s_touchNavLabels[i][0]) {
      tft.setTextDatum(MC_DATUM);
      const uint16_t txtColor = (i == 2) ? UI_ICON : UI_TEXT;
      tft.setTextColor(txtColor, UI_FG);
      const DivString fit = fitTouchNavLabel(s_touchNavLabels[i], b.w - 8);
      tft.drawString(fit, b.x + b.w / 2, b.y + b.h / 2, 1);
    } else {
      const int ix = b.x + (b.w - kIconSize) / 2;
      const int iy = b.y + (b.h - kIconSize) / 2;
      const bool inactiveSlot = s_touchNavLabelsConfigured && !s_touchNavLabels[i];
      const unsigned char* icon = inactiveSlot ? bitmap_icon_dots : kIcons[i];
      const uint16_t iconColor = inactiveSlot ? LIGHT_GRAY : ((i == 2) ? UI_ICON : UI_TEXT);
      tft.drawBitmap(ix, iy, icon, kIconSize, kIconSize, iconColor);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

static int sampleTouchNavSlot() {
  if (!feature_active || !touchButtonInputEnabled) {
    invalidateTouchNavSampleCache();
    return -1;
  }

  const uint32_t now = millis();
  if (s_touchNavSampleValid &&
      (uint32_t)(now - s_touchNavSampleMs) <= TOUCH_NAV_SAMPLE_CACHE_MS) {
    return s_touchNavSampleSlot;
  }

  int slot = -1;
  int x = 0;
  int y = 0;
  if (readTouchXYDismiss(x, y)) {
    layoutTouchNavBtns();
    const int stripTop = tft.height() - TOUCH_NAV_BAR_H;
    if (y >= stripTop) {
      slot = FeatureUI::hit(s_touchNavBtns, 5, x, y);
    }
  }

  s_touchNavSampleSlot = slot;
  s_touchNavSampleMs = now;
  s_touchNavSampleValid = true;
  return slot;
}

void maintainTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active || touchButtonCueDrawn) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

void featureClearContent(uint16_t color) {
  restoreTftSpiAfterTouchIfNeeded();
  const int bottom = touchNavContentBottomY();
  if (bottom > 0) {
    tft.fillRect(0, 0, tft.width(), bottom, color);
  } else {
    tft.fillScreen(color);
  }
}

int16_t touchNavReservedHeight() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (touchButtonInputEnabled && feature_active) {
    return TOUCH_NAV_BAR_H;
  }
#endif
  return 0;
}

int16_t touchNavContentBottomY() {
  return (int16_t)(tft.height() - touchNavReservedHeight());
}

void drawTouchButtonCue() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

static int touchNavIndexForPin(int buttonPin) {
  for (int i = 0; i < 5; ++i) {
    if (touchNavPinForIndex(i) == buttonPin) {
      return i;
    }
  }
  return -1;
}

static bool isTouchNavSlotDown(int idx) {
  if (idx < 0 || !feature_active || !touchButtonInputEnabled) {
    return false;
  }

  return sampleTouchNavSlot() == idx;
}

bool isPhysicalButtonPressed(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    return !pcf.digitalRead(buttonPin);
  }
#endif
  return false;
}

bool isTouchNavButtonPressed(int buttonPin) {
  const int idx = touchNavIndexForPin(buttonPin);
  if (idx < 0) {
    return false;
  }
  return isTouchNavSlotDown(idx);
}

bool isButtonPressed(int buttonPin) {
  if (isPhysicalButtonPressed(buttonPin)) {
    logPcf8574ButtonSnapshot("level", buttonPin);
    return true;
  }
  return isTouchNavButtonPressed(buttonPin);
}

bool isTouchNavButtonPressedEdge(int buttonPin) {
  if (!feature_active || !touchButtonInputEnabled) {
    return false;
  }

  const int navIdx = touchNavIndexForPin(buttonPin);
  if (navIdx < 0) {
    return false;
  }

  const bool down = isTouchNavSlotDown(navIdx);
  const bool edge = down && !s_touchNavHeld[navIdx];
  s_touchNavHeld[navIdx] = down;
  return edge;
}

bool isButtonPressedEdge(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    const int idx = buttonPin % 8;
    const bool cur = pcf.digitalRead(buttonPin);
    const bool edge = !cur && s_pcfButtonLastState[idx];
    s_pcfButtonLastState[idx] = cur;
    if (edge) {
      logPcf8574ButtonSnapshot("edge", buttonPin);
      return true;
    }
  }
#endif

  return isTouchNavButtonPressedEdge(buttonPin);
}

bool featureExitButtonPressed() {
  return isPhysicalButtonPressed(BTN_SELECT) || isTouchNavButtonPressed(BTN_SELECT);
}

static void showFeatureUnavailable(const char* featureName, const char* requirement) {
  feature_active = false;
  feature_exit_requested = false;
  showNotification(featureName, requirement);
  delay(250);
}

static void runBleDuckyFeature() {
#if FEATURE_BLE_DUCKY
  current_submenu_index = 5;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  Ducky::enter();
  while (current_submenu_index == 5 && !feature_exit_requested) {
      current_submenu_index = 5;
      in_sub_menu = true;
      Ducky::loop();
  }

  Ducky::exit();
  if (feature_exit_requested) {
      in_sub_menu = true;
      is_main_menu = false;
      submenu_initialized = false;
      feature_active = false;
      feature_exit_requested = false;
      displaySubmenu();
      delay(200);
  }
#else
  showFeatureUnavailable("BLE Rubber Ducky", "This feature requires ESP32-S3.");
#endif
}

float currentBatteryVoltage = readBatteryVoltage();
unsigned long last_interaction_time = 0;

int last_submenu_index = -1;
bool submenu_initialized = false;
int last_menu_index = -1;
bool menu_initialized = false;
static inline void fastUiDelay(uint32_t ms) {
    if (ms == 250) {
        ms = 100;
    } else if (ms == 200) {
        ms = 70;
    } else if (ms == 120) {
        ms = 60;
    } else if (ms == 100) {
        ms = 50;
    } else if (ms == 80) {
        ms = 40;
    }
    ::delay(ms);
}

#define delay(ms) fastUiDelay(ms)

static constexpr int Y_START = 30;
static constexpr int Y_SPACING = 75;
static constexpr int MENU_TILE_W = 100;
static constexpr int MENU_TILE_H = 60;
static constexpr int MAIN_MENU_ROWS = 4;
const int MENU_TOUCH_PAD_X = 10;
const int MENU_TOUCH_PAD_Y = 12;

static int menuGridLeftX() {
    const int screenW = tft.width();
    const int gap = (screenW > 240) ? 40 : 20;
    int x = (screenW - MENU_TILE_W * 2 - gap) / 2;
    return (x > 4) ? x : 4;
}

static int menuGridRightX() {
    return menuGridLeftX() + MENU_TILE_W + ((tft.width() > 240) ? 40 : 20);
}

static int menuGridSpacingY(int rows) {
    if (rows <= 1) {
        return Y_SPACING;
    }
    const int contentTop = 30;
    const int contentBottom = tft.height() - 12;
    const int availableStep = (contentBottom - contentTop - MENU_TILE_H) / (rows - 1);
    if (tft.height() <= 320) {
        return Y_SPACING;
    }
    if (availableStep < Y_SPACING) {
        return Y_SPACING;
    }
    return (availableStep > 105) ? 105 : availableStep;
}

static int menuGridTopY(int rows) {
    const int contentBottom = tft.height();
    const int legacyTop = Y_START;
    const int totalH = MENU_TILE_H + menuGridSpacingY(rows) * (rows - 1);
    const int centered = 20 + ((contentBottom - 20 - totalH) / 2);
    return (tft.height() > 320 && centered > legacyTop) ? centered : legacyTop;
}

static int menuTileX(int column) {
    return (column == 0) ? menuGridLeftX() : menuGridRightX();
}

static int menuTileY(int row, int rows) {
    return menuGridTopY(rows) + row * menuGridSpacingY(rows);
}

void displayMenu();
void displaySubmenu();
void displayOtherMenuGrid();
void handleAboutPage();
void handleSettingsSubmenuButtons();

static void launchCurrentMainMenuItem() {
    ESP_LOGI(TOUCH_TAG, "launch main menu idx=%d main=%d feature=%d in_sub=%d",
             current_menu_index, (int)is_main_menu, (int)feature_active, (int)in_sub_menu);
    if (current_menu_index == 3) {
        handleSettingsSubmenuButtons();
        return;
    }
    if (current_menu_index == 7) {
        handleAboutPage();
        return;
    }

    updateActiveSubmenu();
    if (active_submenu_items && active_submenu_size > 0) {
        current_submenu_index = 0;
        if (current_menu_index == 2) {
            other_layer = OTHER_LAYER_HOME;
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        in_sub_menu = true;
        is_main_menu = false;
        submenu_initialized = false;
        displaySubmenu();
    }
}

static void waitForTouchRelease(uint32_t timeoutMs = 500) {
    const uint32_t start = millis();
    while (isTouchDown(120) && (millis() - start < timeoutMs)) {
        delay(10);
    }
}

// Wait until a physical button is released before entering a feature.
// Without this, the just-pressed SELECT is still held when the feature's
// loop starts and is read as an immediate "exit", bouncing back to the menu.
static bool waitForButtonRelease(int buttonPin, uint32_t timeoutMs = 800) {
    const uint32_t start = millis();
    while (isPhysicalButtonPressed(buttonPin) && (millis() - start < timeoutMs)) {
        delay(10);
    }
    const bool released = !isPhysicalButtonPressed(buttonPin);
#if HAS_PCF8574_BUTTONS
    if (released && buttonPin >= 0) {
        s_pcfButtonLastState[buttonPin % 8] = true;
    }
#endif
    return released;
}

static void returnToMainMenu() {
    displayMenu();
    waitForButtonRelease(BTN_SELECT, 600);
    waitForTouchRelease(600);
}

static bool navButtonPressed(int buttonPin, const char* action) {
    if (!isButtonPressedEdge(buttonPin)) {
        return false;
    }

    ESP_LOGI(TAG, "button ui action=%s main_idx=%d sub_idx=%d main=%d in_sub=%d feature=%d other_layer=%u",
             action ? action : "unknown", current_menu_index, current_submenu_index,
             (int)is_main_menu, (int)in_sub_menu, (int)feature_active,
             (unsigned)other_layer);
    return true;
}

void displaySubmenu() {
    restoreTftSpiAfterTouchIfNeeded();
    in_sub_menu = true;
    is_main_menu = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);

    if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
        displayOtherMenuGrid();
        return;
    }

    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(1);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            tft.setTextColor((i == active_submenu_size - 1) ? UI_TEXT : UI_TEXT, UI_BG);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, (i == active_submenu_size - 1) ? UI_TEXT : UI_TEXT);
            tft.setCursor(30, yPos);
            if (i < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0) {
            int prev_yPos = 30 + last_submenu_index * 30;
            if (last_submenu_index == active_submenu_size - 1) prev_yPos += 10;

            tft.setTextColor((last_submenu_index == active_submenu_size - 1) ? UI_TEXT : UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, (last_submenu_index == active_submenu_size - 1) ? UI_TEXT : UI_TEXT);
            tft.setCursor(30, prev_yPos);
            if (last_submenu_index < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[last_submenu_index]);
        }

        int new_yPos = 30 + current_submenu_index * 30;
        if (current_submenu_index == active_submenu_size - 1) new_yPos += 10;

        tft.setTextColor((current_submenu_index == active_submenu_size - 1) ? UI_ICON : UI_ICON, UI_BG);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, (current_submenu_index == active_submenu_size - 1) ? UI_ICON : UI_ICON);
        tft.setCursor(30, new_yPos);
        if (current_submenu_index < active_submenu_size - 1) {
            tft.print("| ");
        }
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

void displayOtherMenuGrid() {
    restoreTftSpiAfterTouchIfNeeded();
    applyThemeToPalette(settings().theme);
    const int gridRows = (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

    submenu_initialized = false;
    last_submenu_index = -1;
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(1);

    if (!other_menu_grid_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = menuTileX(column);
            int y_position = menuTileY(row, gridRows);

            tft.fillRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_LINE);
            tft.drawBitmap(x_position + (MENU_TILE_W - 16) / 2, y_position + 10, other_submenu_icons[i], 16, 16, UI_ICON);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(other_submenu_items[i]);
            int textX = x_position + (MENU_TILE_W - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(other_submenu_items[i]);
        }

        other_menu_grid_initialized = true;
        last_other_menu_index = -1;
    }

    if (last_other_menu_index != current_submenu_index) {
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = menuTileX(column);
            int y_position = menuTileY(row, gridRows);

            if (i == last_other_menu_index) {
                tft.fillRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                tft.drawBitmap(x_position + (MENU_TILE_W - 16) / 2, y_position + 10,
                               other_submenu_icons[last_other_menu_index], 16, 16, UI_ICON);
                int textWidth = tft.textWidth(other_submenu_items[last_other_menu_index]);
                int textX = x_position + (MENU_TILE_W - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(other_submenu_items[last_other_menu_index]);
            }
        }

        int column = current_submenu_index % OTHER_GRID_COLS;
        int row = current_submenu_index / OTHER_GRID_COLS;
        int x_position = menuTileX(column);
        int y_position = menuTileY(row, gridRows);

        tft.fillRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_FG);
        tft.drawRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, UI_ICON);

        tft.setTextColor(UI_ICON, UI_FG);
        tft.drawBitmap(x_position + (MENU_TILE_W - 16) / 2, y_position + 10, other_submenu_icons[current_submenu_index],
                       16, 16, SELECTED_ICON_COLOR);
        int textWidth = tft.textWidth(other_submenu_items[current_submenu_index]);
        int textX = x_position + (MENU_TILE_W - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(other_submenu_items[current_submenu_index]);

        last_other_menu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

/** Main menu "Other" tile (index 2): triple preview icons (LED / satellite / dots). */
static constexpr int MAIN_MENU_OTHER_IDX = 2;
static constexpr int MAIN_MENU_OTHER_ICON_GAP = 4;

static void drawMainMenuOtherTripleIcons(int x_position, int y_position, uint16_t iconColor) {
    const int tripleW = 16 * 3 + MAIN_MENU_OTHER_ICON_GAP * 2;
    int ix = x_position + (MENU_TILE_W - tripleW) / 2;
    const int iy = y_position + 10;
    tft.drawBitmap(ix, iy, bitmap_icon_led, 16, 16, iconColor);
    tft.drawBitmap(ix + 16 + MAIN_MENU_OTHER_ICON_GAP, iy, bitmap_icon_satellite, 16, 16, iconColor);
    tft.drawBitmap(ix + 32 + MAIN_MENU_OTHER_ICON_GAP * 2, iy, bitmap_icon_down_dots, 16, 16, iconColor);
}

static uint16_t contrastColorFor565(uint16_t color) {
    const uint8_t r = (uint8_t)((((color >> 11) & 0x1F) * 255) / 31);
    const uint8_t g = (uint8_t)((((color >> 5) & 0x3F) * 255) / 63);
    const uint8_t b = (uint8_t)(((color & 0x1F) * 255) / 31);
    const uint16_t luma = (uint16_t)(r * 3 + g * 6 + b);
    return (luma >= 1280) ? BLACK : WHITE;
}

static void drawMainMenuSelectionBanner() {
}

static void drawMainMenuTile(int index, bool selected, const uint16_t* iconColors) {
    const int column = index / 4;
    const int row = index % 4;
    const int x_position = menuTileX(column);
    const int y_position = menuTileY(row, MAIN_MENU_ROWS);
    const uint16_t fill = selected ? UI_ICON : UI_FG;
    const uint16_t selectedFg = contrastColorFor565(fill);
    const uint16_t edge = selected ? selectedFg : UI_LINE;
    const uint16_t fg = selected ? selectedFg : UI_TEXT;
    const uint16_t icon = selected ? selectedFg : iconColors[index];

    tft.fillRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, fill);
    tft.drawRoundRect(x_position, y_position, MENU_TILE_W, MENU_TILE_H, 5, edge);

    if (index == MAIN_MENU_OTHER_IDX) {
        drawMainMenuOtherTripleIcons(x_position, y_position, icon);
    } else {
        tft.drawBitmap(x_position + (MENU_TILE_W - 16) / 2, y_position + 10, bitmap_icons[index], 16, 16, icon);
    }

    tft.setTextColor(fg, fill);
    int textWidth = tft.textWidth(menu_items[index]);
    int textX = x_position + (MENU_TILE_W - textWidth) / 2;
    int textY = y_position + 30;
    tft.setCursor(textX, textY);
    tft.print(menu_items[index]);
}

void displayMenu() {

  restoreTftSpiAfterTouchIfNeeded();
  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  is_main_menu = true;
  setTouchButtonInputEnabled(false);
  applyThemeToPalette(settings().theme);
  ESP_LOGI(TAG, "draw main menu idx=%d last=%d full=%d",
           current_menu_index, last_menu_index, (int)!menu_initialized);

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON
};

    submenu_initialized = false;
    last_submenu_index = -1;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    tft.setTextFont(1);

    if (!menu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            drawMainMenuTile(i, false, icon_colors);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        if (last_menu_index >= 0 && last_menu_index < NUM_MENU_ITEMS) {
            drawMainMenuTile(last_menu_index, false, icon_colors);
        }

        if (current_menu_index >= 0 && current_menu_index < NUM_MENU_ITEMS) {
            drawMainMenuTile(current_menu_index, true, icon_colors);
            last_menu_index = current_menu_index;
        }
    }
    drawStatusBar(currentBatteryVoltage, true);
}

static void redrawMainMenu() {
    displayMenu();
}

void handleWiFiSubmenuButtons() {
    if (navButtonPressed(BTN_UP, "wifi_up")) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_DOWN, "wifi_down")) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_SELECT, "wifi_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (current_submenu_index == 7) {
            returnToMainMenu();
            return;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeRequestFlood::probeRequestFloodSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProbeRequestFlood::probeRequestFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 7) {
                    returnToMainMenu();
                    return;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProbeRequestFlood::probeRequestFloodSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProbeRequestFlood::probeRequestFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleBluetoothSubmenuButtons() {
    if (navButtonPressed(BTN_UP, "ble_up")) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_DOWN, "ble_down")) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_SELECT, "ble_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (current_submenu_index == 6) {
            returnToMainMenu();
            return;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleJammer::blejamSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            SourApple::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSniffer::blesnifferSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            runBleDuckyFeature();
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 6) {
                    returnToMainMenu();
                    return;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    SourApple::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSniffer::blesnifferSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    runBleDuckyFeature();
                }
                break;
            }
        }
    }
}

void handleNRFSubmenuButtons() {
    if (navButtonPressed(BTN_UP, "nrf_up")) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_DOWN, "nrf_down")) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_SELECT, "nrf_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (current_submenu_index == 2) {
            returnToMainMenu();
            return;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProtoKill::prokillSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ProtoKill::prokillLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 2) {
                    returnToMainMenu();
                    return;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Scanner::scannerSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProtoKill::prokillSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleSubGHzSubmenuButtons() {
    if (navButtonPressed(BTN_UP, "subghz_up")) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_DOWN, "subghz_down")) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_SELECT, "subghz_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (current_submenu_index == 3) {
            returnToMainMenu();
            return;
        }

        if (current_submenu_index == 0) {

            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            replayat::ReplayAttackSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                replayat::ReplayAttackLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {

            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            subjammer::subjammerSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                subjammer::subjammerLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {

            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SavedProfile::saveSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SavedProfile::saveLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 3) {
                    returnToMainMenu();
                    return;

                } else if (current_submenu_index == 0) {

                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    replayat::ReplayAttackSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        replayat::ReplayAttackLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {

                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SavedProfile::saveSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SavedProfile::saveLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {

                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    subjammer::subjammerSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        subjammer::subjammerLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

constexpr int TOOLS_IDX_TERMINAL = 0;
constexpr int TOOLS_IDX_UPDATE   = 1;
constexpr int TOOLS_IDX_TOUCH    = 2;
constexpr int TOOLS_IDX_SD_FILES = 3;
constexpr int TOOLS_IDX_SETTINGS = -1;
constexpr int TOOLS_IDX_BACK     = 4;

static void runToolsFeatureExitCleanup() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);
    setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
    resetTouchNavHeldState();
    displaySubmenu();
    delay(200);
    while (isButtonPressed(BTN_SELECT)) {
        delay(10);
    }
}

static void runToolsFeature(int idx, void (*setupFn)(), void (*loopFn)()) {
    const bool useTouchNav = (idx != TOOLS_IDX_TOUCH);
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    ESP_LOGI(TOUCH_TAG, "enter tools feature idx=%d touchNav=%d", idx, (int)useTouchNav);
    if (useTouchNav) {
        setTouchButtonInputEnabled(true);
    }
    waitForTouchRelease(800);
    if (useTouchNav) {
        waitForButtonRelease(BTN_SELECT);
    }
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loopFn();
        if (feature_exit_requested) {
            break;
        }
        if (!useTouchNav && isButtonPressed(BTN_SELECT)) {
            break;
        }
        delay(1);
    }
    runToolsFeatureExitCleanup();
}

static void launchToolsFeature(int idx) {
    ESP_LOGI(TOUCH_TAG, "launch tools feature idx=%d", idx);
    switch (idx) {
        case TOOLS_IDX_TERMINAL:
            runToolsFeature(idx, Terminal::terminalSetup, Terminal::terminalLoop);
            break;
        case TOOLS_IDX_UPDATE:
            runToolsFeature(idx, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
            break;
        case TOOLS_IDX_TOUCH:
            runToolsFeature(idx, TouchCalib::setup, TouchCalib::loop);
            break;
        case TOOLS_IDX_SD_FILES:
            runToolsFeature(idx, SdFileManager::setup, SdFileManager::loop);
            break;
        default:
            break;
    }
}

void handleToolsSubmenuButtons() {
    if (navButtonPressed(BTN_UP, "tools_up")) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_DOWN, "tools_down")) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (navButtonPressed(BTN_SELECT, "tools_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (current_submenu_index == TOOLS_IDX_BACK) {
            returnToMainMenu();
            return;
        }

        launchToolsFeature(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) {
            return;
        }
        ESP_LOGI(TOUCH_TAG, "tools submenu touch raw=(%d,%d) idx=%d", x, y, current_submenu_index);
        int touchedSlot = -1;

        // Keep the list usable on panels where the touch map is still a bit off.
        // The observed user taps land in the lower-right area, so treat that as
        // Touch Calibrate first instead of requiring an exact row hit.
        if (x >= 140 && y >= 170) {
            touchedSlot = TOOLS_IDX_TOUCH;
        } else {
            for (int i = 0; i < active_submenu_size; i++) {
                int yPos = 30 + i * 30;
                int button_y1 = yPos - 8;
                int button_y2 = yPos + (i == active_submenu_size - 1 ? 42 : 32);
                if (y >= button_y1 && y <= button_y2) {
                    touchedSlot = i;
                    break;
                }
            }
        }

        if (touchedSlot < 0) {
            return;
        }

        current_submenu_index = touchedSlot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);

        if (current_submenu_index == TOOLS_IDX_BACK) {
            returnToMainMenu();
            return;
        } else {
            launchToolsFeature(current_submenu_index);
        }
    }
}

static void otherDismissPlaceholder() {
    delay(25);
    while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(5);
    }
    while (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT)) {
        int x = 0, y = 0;
        if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
            delay(12);
            continue;
        }
        if (isNotificationVisible()) {
            NotificationAction a = notificationHandleTouch(x, y);
            if (a != NotificationAction::None) {
                break;
            }
            hideNotification();
        }
        break;
    }
    if (in_sub_menu) {
        submenu_initialized = false;
        if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        displaySubmenu();
    }
}

static void otherRfidReturnGuard() {
    delay(120);
    for (int i = 0; i < 120; i++) {
        if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
            !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
            !isButtonPressed(BTN_DOWN) && !isTouchDownDismiss()) {
            break;
        }
        delay(5);
    }
    delay(120);
}

static void otherRfidPlaceholderAction(int idx) {
    feature_active = true;
    if (!RfidNfc::begin()) {
        showNotification("RFID/NFC", "PN532 not found. Check SPI wiring/pins.");
        otherDismissPlaceholder();
        feature_active = false;
        return;
    }
    feature_exit_requested = false;
    setTouchButtonInputEnabled(true);
    for (;;) {
        RfidNfc::clearSessionRetry();
        switch (idx) {
            case 0:
                RfidNfc::sessionCardReader();
                break;
            case 1:
                RfidNfc::sessionClone();
                break;
            case 2:
                RfidNfc::sessionErase();
                break;
            case 3:
                RfidNfc::sessionDump();
                break;
            case 4:
                RfidNfc::sessionDecodeAccess();
                break;
            case 5:
                RfidNfc::sessionJamReader();
                break;
            case 6:
                RfidNfc::sessionTagDisrupt();
                break;
            case 7:
                RfidNfc::sessionDisruptEmulate();
                break;
            default:
                feature_active = false;
                restoreSdAfterSharedSpi();
                return;
        }
        if (feature_exit_requested || !RfidNfc::consumeSessionRetry()) {
            break;
        }
        feature_exit_requested = false;
    }
    restoreSdAfterSharedSpi();
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

static void otherGpsPlaceholderAction(int idx) {
    feature_active = true;
    if (idx == 0) {
        feature_exit_requested = false;
        setTouchButtonInputEnabled(true);
        for (;;) {
            GpsWardriver::clearSessionRetry();
            GpsWardriver::session();
            if (feature_exit_requested || !GpsWardriver::consumeSessionRetry()) {
                break;
            }
            feature_exit_requested = false;
        }
        setTouchButtonInputEnabled(false);
    } else {
        switch (idx) {
            case 1:
                GpsSatelliteScanner::session();
                break;
            default:
                feature_active = false;
                return;
        }
    }
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

void handleOtherSubmenuButtons() {
    if (other_layer == OTHER_LAYER_HOME) {
        const int og_rows =
            (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

        if (navButtonPressed(BTN_UP, "other_up")) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row > 0) {
                current_submenu_index -= OTHER_GRID_COLS;
            } else {
                current_submenu_index += OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (navButtonPressed(BTN_DOWN, "other_down")) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row < og_rows - 1) {
                current_submenu_index += OTHER_GRID_COLS;
            } else {
                current_submenu_index -= OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (navButtonPressed(BTN_LEFT, "other_left")) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col > 0) {
                current_submenu_index--;
            } else {
                current_submenu_index++;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (navButtonPressed(BTN_RIGHT, "other_right")) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col < OTHER_GRID_COLS - 1) {
                current_submenu_index++;
            } else {
                current_submenu_index--;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    } else {
        if (navButtonPressed(BTN_UP, "other_list_up")) {
            current_submenu_index =
                (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (navButtonPressed(BTN_DOWN, "other_list_down")) {
            current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    }

    if (navButtonPressed(BTN_SELECT, "other_select")) {
        last_interaction_time = millis();
        if (!waitForButtonRelease(BTN_SELECT, 3000)) {
            return;
        }

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                returnToMainMenu();
                return;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        int touched_slot = -1;
        if (other_layer == OTHER_LAYER_HOME) {
            const int gridRows = (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;
            for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
                int column = i % OTHER_GRID_COLS;
                int row = i / OTHER_GRID_COLS;
                int x_position = menuTileX(column);
                int y_position = menuTileY(row, gridRows);
                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + MENU_TILE_W;
                int button_y2 = y_position + MENU_TILE_H;
                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < active_submenu_size; i++) {
                int yPos = 30 + i * 30;
                if (i == active_submenu_size - 1) yPos += 10;

                int button_x1 = 10;
                int button_y1 = yPos;
                int button_x2 = 110;
                int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        }

        if (touched_slot < 0) {
            return;
        }

        current_submenu_index = touched_slot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                returnToMainMenu();
                return;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }
}

void handleAboutPage() {

  feature_active = true;
  feature_exit_requested = false;
  ESP_LOGI(TOUCH_TAG, "enter about page");
  waitForTouchRelease(800);
  waitForButtonRelease(BTN_SELECT);
  waitForButtonRelease(BTN_LEFT);

  restoreTftSpiAfterTouchIfNeeded();
  tft.fillScreen(UI_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);

  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setTextSize(1);
  tft.setTextFont(1);

  const char* title = "[About This Project]";
  tft.setCursor(10, 42);
  tft.println(title);

  int lineHeight = 12;
  int text_x = 10;
  int text_y = 62;
  tft.setCursor(text_x, text_y);
  tft.print("- ");
  tftPrintlnObf(OBF_PN, sizeof(OBF_PN));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Developed by: ");
  tftPrintlnObf(OBF_DN, sizeof(OBF_DN));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Version: ");
  tft.println(ESP32DIV_VERSION);
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Contact: ");
  tftPrintlnObf(OBF_EM, sizeof(OBF_EM));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- GitHub: ");
  tftPrintlnObf(OBF_GH, sizeof(OBF_GH));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Website: ");
  tftPrintlnObf(OBF_WB, sizeof(OBF_WB));

  tft.setTextFont(1);
  tft.setCursor(10, 300);
  tft.println("Press SELECT / touch to go back");

  while (!feature_exit_requested) {
    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
      last_interaction_time = millis();
      ESP_LOGI(TOUCH_TAG, "about exit by button");
      feature_exit_requested = true;
      delay(200);
      break;
    }

    int x, y;
    if (readTouchXY(x, y)) {
      last_interaction_time = millis();
      ESP_LOGI(TOUCH_TAG, "about exit by touch raw=(%d,%d)", x, y);
      feature_exit_requested = true;
      delay(200);
      break;
    }

    delay(20);
  }

  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleSettingsSubmenuButtons() {

  feature_active = true;
  feature_exit_requested = false;
  ESP_LOGI(TOUCH_TAG, "enter settings page");
  waitForTouchRelease(800);
  waitForButtonRelease(BTN_SELECT);

  AppSettingsUI::setup();
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
  }

  feature_active = false;
  feature_exit_requested = false;

  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleButtons() {
    if (is_main_menu && (feature_active || in_sub_menu)) {
        ESP_LOGW(TAG, "main menu state corrected feature=%d in_sub=%d idx=%d",
                 (int)feature_active, (int)in_sub_menu, current_menu_index);
        feature_active = false;
        feature_exit_requested = false;
        in_sub_menu = false;
    }

    if (in_sub_menu) {
        switch (current_menu_index) {

            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleNRFSubmenuButtons(); break;
            case 2: handleOtherSubmenuButtons(); break;
            case 3: /* Settings: full-screen AppSettings, not list submenu */ break;
            case 4: handleBluetoothSubmenuButtons(); break;
            case 5: handleSubGHzSubmenuButtons(); break;
            case 6: handleToolsSubmenuButtons(); break;
            default: break;
        }
    } else {
        const bool on_main_menu = !feature_active && !in_sub_menu;

        if (on_main_menu && navButtonPressed(BTN_UP, "main_up")) {
            const int oldIndex = current_menu_index;
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            ESP_LOGI(TAG, "main menu action=UP index %d -> %d", oldIndex, current_menu_index);
            last_interaction_time = millis();
            redrawMainMenu();
            delay(200);
            return;
        }

        if (on_main_menu && navButtonPressed(BTN_DOWN, "main_down")) {
            const int oldIndex = current_menu_index;
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            ESP_LOGI(TAG, "main menu action=DOWN index %d -> %d", oldIndex, current_menu_index);
            last_interaction_time = millis();
            redrawMainMenu();
            delay(200);
            return;
        }

        if (on_main_menu && navButtonPressed(BTN_LEFT, "main_left")) {
            const int oldIndex = current_menu_index;
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            ESP_LOGI(TAG, "main menu action=LEFT index %d -> %d", oldIndex, current_menu_index);
            last_interaction_time = millis();
            redrawMainMenu();
            delay(200);
            return;
        }

        if (on_main_menu && navButtonPressed(BTN_RIGHT, "main_right")) {
            const int oldIndex = current_menu_index;
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            ESP_LOGI(TAG, "main menu action=RIGHT index %d -> %d", oldIndex, current_menu_index);
            last_interaction_time = millis();
            redrawMainMenu();
            delay(200);
            return;
        }

        if (on_main_menu && navButtonPressed(BTN_SELECT, "main_select")) {
            last_interaction_time = millis();
            if (!waitForButtonRelease(BTN_SELECT, 3000)) {
                return;
            }
            launchCurrentMainMenuItem();
            return;
        }

        static unsigned long lastTouchTime = 0;
        static unsigned long lastTouchPollTime = 0;
        const unsigned long touchFeedbackDelay = 100;
        const unsigned long touchPollInterval = 40;

        if (!feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            if (millis() - lastTouchPollTime < touchPollInterval) {
                return;
            }
            lastTouchPollTime = millis();
            int x, y;
            if (!readTouchXY(x, y)) { return; }
            delay(10);
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = menuTileX(column);
                int y_position = menuTileY(row, MAIN_MENU_ROWS);

                int button_x1 = x_position - MENU_TOUCH_PAD_X;
                int button_y1 = y_position - MENU_TOUCH_PAD_Y;
                int button_x2 = x_position + MENU_TILE_W + MENU_TOUCH_PAD_X;
                int button_y2 = y_position + MENU_TILE_H + MENU_TOUCH_PAD_Y;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    ESP_LOGI(TOUCH_TAG, "main menu hit idx=%d raw=(%d,%d) box=[%d..%d,%d..%d] main=%d feature=%d",
                             i, x, y, button_x1, button_x2, button_y1, button_y2,
                             (int)is_main_menu, (int)feature_active);
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();
                    waitForTouchRelease(600);
                    launchCurrentMainMenuItem();

                    lastTouchTime = millis();
                    delay(200);
                    break;
                }
            }
        }
    }
}

void esp32_div_app_init() {

  ESP_LOGI(TAG, "boot start");
  ESP_LOGI(TAG, "build marker: btn-touch-spi-fix v9");

  initBuzzerIdle();

  tft.init();
  ESP_LOGI(TAG, "tft init done");
  tft.setRotation(TFT_ROTATION);

  initBacklightPwm();
  setBrightness(100);
  ESP_LOGI(TAG, "backlight ready");

  applyThemeToPalette(settings().theme);

  tft.fillScreen(TFT_BLACK);

  loading(100, UI_ICON, 0, 0, 2, true);
  ESP_LOGI(TAG, "loading animation done");

  tft.fillScreen(TFT_BLACK);

  displayLogo(TFT_WHITE, 500);
  ESP_LOGI(TAG, "logo done");

  initSDCard();
  ESP_LOGI(TAG, "sd init done");

  settingsLoad();
  applyThemeToPalette(settings().theme);
  setBrightness(settings().brightness);
  ESP_LOGI(TAG, "settings loaded");

#if HAS_PCF8574_BUTTONS
  if (!initPcf8574Buttons()) {
    ESP_LOGW(TAG, "PCF8574 buttons unavailable");
  }
#else
  ESP_LOGI(TAG, "PCF8574 buttons disabled for this board");
#endif

  ESP_LOGI(TAG, "BLE init");
  BLEDevice::init(ESP32DIV_NAME);
  ESP_LOGI(TAG, "BLE init done");

#if FEATURE_BLE_DUCKY
  Ducky::setup();
#endif

  ESP_LOGI(TAG, "background tasks");
  WifiScan::startBackgroundScanner();
  BleScan::startBackgroundScanner();
  startStatusBarTask();
  ESP_LOGI(TAG, "background tasks done");

  ESP_LOGI(TAG, "draw menu");
  menu_initialized = false;
  currentBatteryVoltage = readBatteryVoltage();
  displayMenu();
  is_main_menu = true;
  drawStatusBar(currentBatteryVoltage, false);
  ESP_LOGI(TAG, "menu drawn");

  setupTouchscreen();
  ESP_LOGI(TAG, "touch ready");

  last_interaction_time = millis();
  ESP_LOGI(TAG, "ready");
}

namespace AppUiTestHooks {

void resetToMenu() {
  clearInjectedTouchForTest();
  feature_exit_requested = false;
  feature_active = false;
  in_sub_menu = false;
  is_main_menu = true;
  other_layer = OTHER_LAYER_HOME;
  setTouchButtonInputEnabled(false);
  setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
  resetTouchNavHeldState();
  menu_initialized = false;
  submenu_initialized = false;
  other_menu_grid_initialized = false;
  displayMenu();
  delay(80);
}

void tapMain(int mainIdx) {
  current_menu_index = mainIdx;
  displayMenu();
  const int column = mainIdx / 4;
  const int row = mainIdx % 4;
  const int x = menuTileX(column) + MENU_TILE_W / 2;
  const int y = menuTileY(row, MAIN_MENU_ROWS) + MENU_TILE_H / 2;
  ESP_LOGI(TOUCH_TAG, "ui-test tap main[%d]=%s at %d,%d",
           mainIdx, menu_items[mainIdx], x, y);
  injectTouchScreenXYForTest(x, y, 1);
  delay(60);
  clearInjectedTouchForTest();
}

void tapSubmenu(const char* groupName, int subIdx) {
  current_submenu_index = subIdx;
  submenu_initialized = false;
  displaySubmenu();
  const char* label =
      (active_submenu_items && subIdx >= 0 && subIdx < active_submenu_size)
          ? active_submenu_items[subIdx]
          : "?";
  int yPos = 30 + subIdx * 30;
  if (subIdx == active_submenu_size - 1) {
    yPos += 10;
  }
  ESP_LOGI(TOUCH_TAG, "ui-test tap %s[%d]=%s at %d,%d",
           groupName ? groupName : "submenu", subIdx, label, 60, yPos + 12);
  injectTouchScreenXYForTest(60, yPos + 12, 1);
  delay(60);
  clearInjectedTouchForTest();
}

void openTopSubmenu(int mainIdx) {
  tapMain(mainIdx);
  updateActiveSubmenu();
  current_submenu_index = 0;
  in_sub_menu = true;
  is_main_menu = false;
  submenu_initialized = false;
  displaySubmenu();
}

void openOtherLayer(uint8_t layer, int homeIdx, const char* name) {
  tapMain(2);
  other_layer = OTHER_LAYER_HOME;
  current_menu_index = 2;
  current_submenu_index = homeIdx;
  updateActiveSubmenu();
  in_sub_menu = true;
  submenu_initialized = false;
  displaySubmenu();
  const int gridRows = (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;
  const int column = homeIdx % OTHER_GRID_COLS;
  const int row = homeIdx / OTHER_GRID_COLS;
  ESP_LOGI(TOUCH_TAG, "ui-test tap More/%s at %d,%d",
           name ? name : "layer",
           menuTileX(column) + MENU_TILE_W / 2,
           menuTileY(row, gridRows) + MENU_TILE_H / 2);
  delay(60);
  other_layer = layer;
  current_submenu_index = 0;
  updateActiveSubmenu();
  submenu_initialized = false;
  displaySubmenu();
}

}  // namespace AppUiTestHooks

void esp32_div_app_run_once() {
  applyThemeToPalette(settings().theme);
  pollPcf8574ScanMode();
  pollPcf8574SelfTest();
  UiSelfTest::pollAutoRun();
  handleButtons();
  updateStatusBar();
}
