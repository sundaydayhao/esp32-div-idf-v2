#include "settings_store.h"
#include "touchscreen.h"
#include <algorithm>
#include "div_display.h"
#include "esp_log.h"

extern DivDisplay tft;

#ifndef XPT2046_IRQ
#define XPT2046_IRQ 255
#endif

XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool feature_active = false;

static bool s_touchInitialized = false;
static const char* TAG = "esp32_div_touch";
static uint32_t s_lastTouchDebugMs = 0;
static bool s_tftSpiDirtyFromTouch = false;
static constexpr uint8_t kTouchSampleCount = 5;
static constexpr uint8_t kTouchMinValidSamples = 3;
static constexpr int16_t kTouchMaxStableSpreadRaw = 700;
static int16_t s_injectedRawX = 0;
static int16_t s_injectedRawY = 0;
static uint16_t s_injectedRawZ = 0;
static uint8_t s_injectedRawReads = 0;
static int s_injectedScreenX = 0;
static int s_injectedScreenY = 0;
static uint8_t s_injectedScreenReads = 0;

#ifndef TOUCH_ROTATION
#if defined(BOARD_ESP32_DIV_V2)
#define TOUCH_ROTATION 0
#else
#define TOUCH_ROTATION TFT_ROTATION
#endif
#endif

static constexpr bool kTouchSharesTftSpi =
    (XPT2046_CLK == TFT_SCLK) && (XPT2046_MOSI == TFT_MOSI) && (XPT2046_MISO == TFT_MISO);

static bool touchPending() {
#if defined(XPT2046_IRQ) && (XPT2046_IRQ < 255)
  return ts.tirqTouched();
#else
  return ts.touched();
#endif
}

static void markTftSpiDirtyFromTouch() {
  if (kTouchSharesTftSpi) {
    s_tftSpiDirtyFromTouch = true;
  }
}

void restoreTftSpiAfterTouchIfNeeded() {
  if (!kTouchSharesTftSpi || !s_tftSpiDirtyFromTouch) {
    return;
  }

  // TFT and XPT2046 chip selects are owned by ESP-IDF's SPI master driver.
  // Reconfiguring those pins as GPIO after spi_bus_add_device() detaches CS
  // from the peripheral, so later TFT transactions are ignored by the panel.
  s_tftSpiDirtyFromTouch = false;
}

static bool touchPointLooksValid(const TS_Point& p, uint16_t zThresh) {
  if (p.z < (int16_t)zThresh) {
    return false;
  }
  if (p.x <= 5 || p.x >= 4090 || p.y <= 5 || p.y >= 4090) {
    return false;
  }
  return true;
}

static bool touchSampleOk(uint16_t zThresh, int16_t& rawX, int16_t& rawY) {
  if (s_injectedRawReads > 0) {
    if (s_injectedRawZ < zThresh) {
      return false;
    }
    --s_injectedRawReads;
    rawX = s_injectedRawX;
    rawY = s_injectedRawY;
    return true;
  }

  if (!s_touchInitialized) {
    return false;
  }
  if (!touchPending()) {
    return false;
  }

  int16_t xs[kTouchSampleCount] = {};
  int16_t ys[kTouchSampleCount] = {};
  int16_t zs[kTouchSampleCount] = {};
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < kTouchSampleCount; ++i) {
    if (!touchPending()) {
      break;
    }

    TS_Point p = ts.getPoint();
    markTftSpiDirtyFromTouch();
    if (!touchPointLooksValid(p, zThresh)) {
      continue;
    }

    xs[validCount] = p.x;
    ys[validCount] = p.y;
    zs[validCount] = p.z;
    ++validCount;
  }

  restoreTftSpiAfterTouchIfNeeded();

  if (validCount < kTouchMinValidSamples) {
    return false;
  }

  std::sort(xs, xs + validCount);
  std::sort(ys, ys + validCount);
  std::sort(zs, zs + validCount);

  const uint8_t spreadLo = (validCount >= 4) ? 1 : 0;
  const uint8_t spreadHi = (validCount >= 4) ? (uint8_t)(validCount - 2) : (uint8_t)(validCount - 1);
  if ((xs[spreadHi] - xs[spreadLo]) > kTouchMaxStableSpreadRaw ||
      (ys[spreadHi] - ys[spreadLo]) > kTouchMaxStableSpreadRaw) {
    return false;
  }

  rawX = xs[validCount / 2];
  rawY = ys[validCount / 2];
  const uint32_t now = millis();
  if (now - s_lastTouchDebugMs >= 250) {
    s_lastTouchDebugMs = now;
    ESP_LOGI(TAG, "raw touch x=%d y=%d z=%d samples=%u irq=%d",
             (int)rawX, (int)rawY, (int)zs[validCount / 2],
             (unsigned)validCount, (int)touchPending());
  }
  return true;
}

void setupTouchscreen() {
  if (s_touchInitialized) {
    return;
  }

  ts.begin();
  ts.setRotation(TOUCH_ROTATION);

  s_touchInitialized = true;
  markTftSpiDirtyFromTouch();
  restoreTftSpiAfterTouchIfNeeded();
}

bool isTouchDown(uint16_t zThresh) {
  if (s_injectedScreenReads > 0) {
    return true;
  }
  int16_t x = 0;
  int16_t y = 0;
  return touchSampleOk(zThresh, x, y);
}

bool isTouchDownDismiss(uint16_t zThresh) {
  return isTouchDown(zThresh);
}

bool readTouchRawXY(int16_t& x, int16_t& y, uint16_t zThresh) {
  return touchSampleOk(zThresh, x, y);
}

static void mapTouchToScreen(int16_t rawX, int16_t rawY, int& x, int& y) {
  auto& s = settings();
  auto clampToScreen = [](long v, long maxV) -> int {
    if (v < 0) return 0;
    if (v > maxV) return (int)maxV;
    return (int)v;
  };
  int x0 = 0;
  int y0 = 0;

#if defined(BOARD_CYD)
  x0 = ::map(rawX, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
  y0 = ::map(rawY, s.touchYMin, s.touchYMax, 0, TFT_HEIGHT - 1);
#else
  x0 = ::map(rawX, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
  y0 = ::map(rawY, s.touchYMax, s.touchYMin, 0, TFT_HEIGHT - 1);
#endif

  switch (TOUCH_ROTATION & 3) {
    case 0:
      x = x0;
      y = y0;
      break;
    case 1:
      x = TFT_WIDTH - 1 - y0;
      y = x0;
      break;
    case 2:
      x = TFT_WIDTH - 1 - x0;
      y = TFT_HEIGHT - 1 - y0;
      break;
    case 3:
      x = y0;
      y = TFT_HEIGHT - 1 - x0;
      break;
  }

  x = clampToScreen(x, TFT_WIDTH - 1);
  y = clampToScreen(y, TFT_HEIGHT - 1);
}

bool readTouchXY(int& x, int& y) {
  if (s_injectedScreenReads > 0) {
    --s_injectedScreenReads;
    x = s_injectedScreenX;
    y = s_injectedScreenY;
    return true;
  }

  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(200, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}

bool readTouchXYDismiss(int& x, int& y) {
  if (s_injectedScreenReads > 0) {
    --s_injectedScreenReads;
    x = s_injectedScreenX;
    y = s_injectedScreenY;
    return true;
  }

  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(120, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}

void injectTouchRawXYForTest(int16_t x, int16_t y, uint16_t z, uint8_t reads) {
  s_injectedRawX = x;
  s_injectedRawY = y;
  s_injectedRawZ = z;
  s_injectedRawReads = reads;
}

void injectTouchScreenXYForTest(int x, int y, uint8_t reads) {
  s_injectedScreenX = x;
  s_injectedScreenY = y;
  s_injectedScreenReads = reads;
}

void clearInjectedTouchForTest() {
  s_injectedRawReads = 0;
  s_injectedScreenReads = 0;
}
