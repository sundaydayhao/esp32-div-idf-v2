#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include "xpt2046_touchscreen.h"
#include "settings_store.h"
#include "shared.h"

extern XPT2046_Touchscreen ts;

#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 300
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3800
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 300
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3800
#endif
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 240
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 320
#endif

extern bool feature_active;

void setupTouchscreen();
void restoreTftSpiAfterTouchIfNeeded();

/** True when the panel is pressed. Use this instead of ts.touched() directly. */
bool isTouchDown(uint16_t zThresh = 500);
/** Lower pressure threshold for release detection and light taps. */
bool isTouchDownDismiss(uint16_t zThresh = 120);
/** Raw ADC coords for calibration (before screen mapping). */
bool readTouchRawXY(int16_t& x, int16_t& y, uint16_t zThresh = 500);

bool readTouchXY(int& x, int& y);
/** Lower pressure threshold ¡ª use for "tap anywhere to dismiss" so light taps register. */
bool readTouchXYDismiss(int& x, int& y);

void injectTouchRawXYForTest(int16_t x, int16_t y, uint16_t z = 900, uint8_t reads = 6);
void injectTouchScreenXYForTest(int x, int y, uint8_t reads = 3);
void clearInjectedTouchForTest();

#endif
