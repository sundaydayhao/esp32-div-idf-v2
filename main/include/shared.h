#ifndef SHARED_H
#define SHARED_H
#include <stdint.h>
#include "board_config.h"
#include "tft_setup.h"

#ifndef PROGMEM
#define PROGMEM
#endif

/*���������������������������������������� Colors ����������������������������������������*/
const uint16_t GRAY = 0x8410, BLUE = 0x001F, RED = 0xF800,
               GREEN  = 0x07E0, BLACK = 0x0000, WHITE = 0xFFFF,
               LIGHT_GRAY = 0xC618, DARK_GRAY = 0x4208;

uint16_t uiUniversalColor();
#define ORANGE uiUniversalColor()
               
#define TFT_DARKBLUE   0x3166
#define TFT_LIGHTBLUE  0x051F
#define TFTWHITE       0xFFFF
#define TFT_GRAY       0x8410

#define BG_Dark        0x20e4
#define BG_Light       0xf7de
#define FG_Dark        0x3166
#define FG_Light       0xe73c
#define LINE_Dark      0x8410  
#define LINE_Light     0x8410  
#define ICON_Dark      0xFBE4 
#define ICON_Light     0xFBE4
#define TEXT_Dark      0xFFFF
#define TEXT_Light     0x0000   
#define UI_ACCENT      0x3166

#define L_Dark        0x4208
#define L_Light       0xC618

// Default background for feature screens (separate from main menu background).
// Currently black, but you can change this one define to restyle all features.
#ifndef FEATURE_BG
#define FEATURE_BG BLACK
#endif

#define SELECTED_ICON_COLOR UI_ICON

#if defined(BOARD_ESP32_DIV_V4)
#define UI_ICON_SIZE 32
#else
#define UI_ICON_SIZE 16
#endif

/*���������������������������������������� UI Defaults ����������������������������������������*/
#ifndef UI_BG
#define UI_BG UI.bg
#endif
#ifndef UI_FG
#define UI_FG UI.fg
#endif
#ifndef UI_ICON
#define UI_ICON UI.icon
#endif
#ifndef UI_TEXT
#define UI_TEXT UI.text
#endif
#ifndef UI_LINE
#define UI_LINE UI.line
#endif
#ifndef UI_LABLE
#define UI_LABLE UI.lable
#endif
#ifndef UI_ACCENT
#define UI_ACCENT UI.accent
#endif
#ifndef UI_WARN
#define UI_WARN UI.warn
#endif
#ifndef UI_OK
#define UI_OK GREEN
#endif

/*���������������������������������������� Project Info ����������������������������������������*/
#ifndef ESP32DIV_NAME
#define ESP32DIV_NAME "ESP32-DIV"
#endif
#ifndef ESP32DIV_VERSION
#define ESP32DIV_VERSION "v1.7.0"
#endif


static inline constexpr uint8_t k0() { return (uint8_t)('h' - '`'); }

static const uint8_t OBF_PN[]   = {77, 91, 88, 59, 58, 37, 76, 65, 94};                              
static const uint8_t OBF_DN[]   = {75, 97, 110, 109, 122, 92, 109, 107, 96};                           
static const uint8_t OBF_EM[]   = {107, 97, 110, 109, 122, 124, 109, 107, 96, 72, 111, 101, 105, 97, 100, 38, 107, 103, 101};  
static const uint8_t OBF_GH[]   = {111, 97, 124, 96, 125, 106, 38, 107, 103, 101, 39, 107, 97, 110, 109, 122, 124, 109, 107, 96}; 
static const uint8_t OBF_WB[]   = {75, 97, 110, 109, 122, 92, 109, 107, 96, 38, 102, 109, 124};       

/*���������������������������������������� Board Selection ����������������������������������������*/
// Default is selected in board_config.h. You can also pass a BOARD_* define
// from your build flags to target a board without editing source.
#if !defined(BOARD_ESP32_DIV_V4) && !defined(BOARD_ESP32_DIV_V3)
#define BOARD_ESP32_DIV_V3
#endif

#if (defined(BOARD_ESP32_DIV_V4) + defined(BOARD_ESP32_DIV_V3)) > 1
#error "Select only one board: BOARD_ESP32_DIV_V4 or BOARD_ESP32_DIV_V3"
#endif

#if defined(BOARD_ESP32_DIV_V3)
#ifndef ESP32DIV_BOARD_NAME
#define ESP32DIV_BOARD_NAME "ESP32-DIV V3"
#endif
#ifndef HAS_PCF8574_BUTTONS
#define HAS_PCF8574_BUTTONS 1
#endif
#ifndef BOARD_HAS_ESP32S3
#define BOARD_HAS_ESP32S3 1
#endif
#elif defined(BOARD_ESP32_DIV_V4)
#ifndef ESP32DIV_BOARD_NAME
#define ESP32DIV_BOARD_NAME "ESP32-DIV V4"
#endif
#ifndef HAS_PCF8574_BUTTONS
#define HAS_PCF8574_BUTTONS 1
#endif
#ifndef BOARD_HAS_ESP32S3
#define BOARD_HAS_ESP32S3 1
#endif
#else
#error "Unknown board: define BOARD_ESP32_DIV_V4 or BOARD_ESP32_DIV_V3"
#endif

/*���������������������������������������� Touch calibration profiles ����������������������������������������*/
/* Factory defaults per board (raw XPT2046 range). Override in board_config.h.
 * User-saved NVS calibration overrides these defaults when the board id matches. */
#if defined(BOARD_ESP32_DIV_V3)
#ifndef TOUCH_PROFILE_ID
#define TOUCH_PROFILE_ID "ESP32_DIV_V3"
#endif
#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 280
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3850
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 320
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3750
#endif
#elif defined(BOARD_ESP32_DIV_V4)
#ifndef TOUCH_PROFILE_ID
#define TOUCH_PROFILE_ID "ESP32_DIV_V4"
#endif
#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 280
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3850
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 320
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3750
#endif
#endif

#ifndef TOUCH_PROFILE_ID
#define TOUCH_PROFILE_ID "GENERIC"
#endif
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

#ifndef TFT_ROTATION
#define TFT_ROTATION 0
#endif

#ifndef TOUCH_SHARES_TFT_SPI
#define TOUCH_SHARES_TFT_SPI 0
#endif

/*���������������������������������������� I/O & Pins ����������������������������������������*/
// PCF8574 I2C address: only enabled when board_config.h defines pcf_ADDR.
#ifndef pcf_ADDR
#define PCF8574_ENABLED 0
#define PCF8574_I2C_ADDR  0x20
#else
#define PCF8574_ENABLED 1
#define PCF8574_I2C_ADDR  pcf_ADDR
#endif
#define PCF8574_ADDR_MIN 0x20
#define PCF8574_ADDR_MAX 0x27
#ifndef PCF8574_I2C_SDA
#define PCF8574_I2C_SDA 8
#endif
#ifndef PCF8574_I2C_SCL
#define PCF8574_I2C_SCL 9
#endif
#define BTN_UP       3
#define BTN_DOWN     5
#define BTN_LEFT     6
#define BTN_RIGHT    4
#define BTN_SELECT   7

/* Buzzer */
#ifndef BUZZER_PIN
// User hardware: buzzer on IO2
#define BUZZER_PIN -1
#endif
#ifndef BUZZER_USE_PWM
// 1 = passive magnetic transducer driven by PWM, 0 = active DC buzzer.
#define BUZZER_USE_PWM 1
#endif
#ifndef BUZZER_ACTIVE_LEVEL
// Logical GPIO level that makes the buzzer sound.
#define BUZZER_ACTIVE_LEVEL 1
#endif
#define BUZZER_INACTIVE_LEVEL (BUZZER_ACTIVE_LEVEL ? 0 : 1)

/* Backlight / PWM */
#ifndef BACKLIGHT_PIN
#define BACKLIGHT_PIN   7
#endif
#define PWM_CHANNEL     0
#define PWM_FREQ        5000
#define PWM_RESOLUTION  8

/* XPT2046 (Touch) SPI */
#ifndef XPT2046_CS
#define XPT2046_CS   18
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 35
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 37
#endif
#ifndef XPT2046_CLK
#define XPT2046_CLK  36
#endif
#ifndef XPT2046_IRQ
#define XPT2046_IRQ  255
#endif

#ifndef XPT2046_HOST
#define XPT2046_HOST SPI2_HOST
#endif

/* SD Card */
#ifndef SD_CS
#define SD_CS    10
#endif
#ifndef SD_MOSI
#define SD_MOSI  11
#endif
#ifndef SD_MISO
#define SD_MISO  13
#endif
#ifndef SD_SCLK
#define SD_SCLK  12
#endif
#ifndef SD_CD
#define SD_CD    38
#endif
#ifndef SD_CS_PIN
#define SD_CS_PIN 5
#endif

/* PN532 RFID/NFC (SPI). Override any pin below if your wiring differs. */
#ifndef PN532_SCK
#define PN532_SCK  12
#endif
#ifndef PN532_MISO
#define PN532_MISO 11
#endif
#ifndef PN532_MOSI
#define PN532_MOSI 13
#endif
#ifndef PN532_SS
#define PN532_SS   4
#endif

/* UART (if you use hardware serial on external pins) */
#ifndef RX_PIN
#define RX_PIN 6
#endif
#ifndef TX_PIN
#define TX_PIN 3
#endif

/* Neo-6M GPS �� GPS module TX �� ESP RX, GPS RX �� ESP TX (optional). Uses UART2 by default. */
#ifndef GPS_UART_RX
#define GPS_UART_RX 47
#endif
#ifndef GPS_UART_TX
#define GPS_UART_TX 48
#endif

/* CC1101 (Sub-GHz) */
#ifndef CC1101_SCK
#define CC1101_SCK  12
#endif
#ifndef CC1101_MISO
#define CC1101_MISO 13
#endif
#ifndef CC1101_MOSI
#define CC1101_MOSI 11
#endif
#ifndef CC1101_CS
#define CC1101_CS   5
#endif

/* SubGHz (RCSwitch/Replay) data pins (wired to CC1101 GDO pins) */
// Override these in your board config if your wiring differs.
#ifndef SUBGHZ_RX_PIN
#define SUBGHZ_RX_PIN 3
#endif
#ifndef SUBGHZ_TX_PIN
#define SUBGHZ_TX_PIN 6
#endif

// CC1101 GDO mapping (used by DivCc1101Radio.setGDO).
// Default follows the legacy wiring used in the SubGHz code: setGDO(TX, RX).
#ifndef CC1101_GDO0
#define CC1101_GDO0 SUBGHZ_TX_PIN
#endif
#ifndef CC1101_GDO2
#define CC1101_GDO2 SUBGHZ_RX_PIN
#endif

/* SubGHz debug (0=off, 1=on) */
#ifndef SUBGHZ_DEBUG
#define SUBGHZ_DEBUG 0
#endif

/* NRF24 */
#ifndef CE_PIN_1
#define CE_PIN_1  15
#endif
#ifndef CSN_PIN_1
#define CSN_PIN_1 4
#endif
#ifndef CE_PIN_2
#define CE_PIN_2  47
#endif
#ifndef CSN_PIN_2
#define CSN_PIN_2 48
#endif
#ifndef CE_PIN_3
#define CE_PIN_3  14
#endif
#ifndef CSN_PIN_3
#define CSN_PIN_3 21
#endif

/* IR Remote (Record/Replay)
 * NOTE: Default pins overlap with the optional NRF24 #3 wiring above.
 * If you use NRF24 on CE_PIN_3/CSN_PIN_3, override these in your board config.
 */
#ifndef IR_RX_PIN
#define IR_RX_PIN 21
#endif
#ifndef IR_TX_PIN
#define IR_TX_PIN 14
#endif
#ifndef IR_DEFAULT_KHZ
#define IR_DEFAULT_KHZ 38
#endif

/*���������������������������������������� Display & Touch ����������������������������������������*/
#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH TFT_WIDTH
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT TFT_HEIGHT
#endif
#ifndef STATUS_BAR_Y_OFFSET
#undef STATUS_BAR_Y_OFFSET
#define STATUS_BAR_Y_OFFSET 0
#endif

/*���������������������������������������� Timing ����������������������������������������*/
#ifndef DEBOUNCE_MS
#define DEBOUNCE_MS 40
#endif
#ifndef LONG_PRESS_MS
#define LONG_PRESS_MS 800
#endif
#ifndef REPEAT_SCROLL_MS
#define REPEAT_SCROLL_MS 120
#endif

/*���������������������������������������� Backlight Levels ����������������������������������������*/
#ifndef BKL_LEVEL_MIN
#define BKL_LEVEL_MIN 10
#endif
#ifndef BKL_LEVEL_MED
#define BKL_LEVEL_MED 128
#endif
#ifndef BKL_LEVEL_MAX
#define BKL_LEVEL_MAX 255
#endif

/*���������������������������������������� Filesystem ����������������������������������������*/
#ifndef FS_MOUNT_OK_MSG
#define FS_MOUNT_OK_MSG "SD OK"
#endif
#ifndef FS_MOUNT_FAIL_MSG
#define FS_MOUNT_FAIL_MSG "SD Fail"
#endif
#ifndef LOG_DIR
#define LOG_DIR "/logs"
#endif
#ifndef CAPTURE_DIR
#define CAPTURE_DIR "/captures"
#endif

/*���������������������������������������� Battery ����������������������������������������*/
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN -1
#endif
#ifndef BATTERY_VDIV_R1
#define BATTERY_VDIV_R1 200000.0f
#endif
#ifndef BATTERY_VDIV_R2
#define BATTERY_VDIV_R2 100000.0f
#endif
#ifndef BATTERY_LOW_VOLT
#define BATTERY_LOW_VOLT 3.40f
#endif

/*���������������������������������������� Wi-Fi ����������������������������������������*/
#ifndef WIFI_SCAN_ACTIVE_MS
#define WIFI_SCAN_ACTIVE_MS  500
#endif
#ifndef WIFI_SCAN_PASSIVE_MS
#define WIFI_SCAN_PASSIVE_MS 0
#endif
#ifndef WIFI_SPECTRUM_FFT_N
#define WIFI_SPECTRUM_FFT_N  256
#endif

/*���������������������������������������� BLE ����������������������������������������*/
#ifndef BLE_ADV_INTERVAL_MS
#define BLE_ADV_INTERVAL_MS 100
#endif
#ifndef BLE_JAMMER_SWEEP_MS
#define BLE_JAMMER_SWEEP_MS 80
#endif

/*���������������������������������������� Sub-GHz / RF ����������������������������������������*/
#ifndef SUBGHZ_SAMPLE_RATE
#define SUBGHZ_SAMPLE_RATE 38000
#endif
#ifndef SUBGHZ_DEFAULT_FREQ
#define SUBGHZ_DEFAULT_FREQ 433920000UL
#endif
#ifndef NRF24_DATA_RATE
#define NRF24_DATA_RATE    2
#endif
#ifndef NRF24_PA_LEVEL
#define NRF24_PA_LEVEL     3
#endif
#ifndef CC1101_DEFAULT_MOD
#define CC1101_DEFAULT_MOD 2
#endif

/*���������������������������������������� Feature Flags ����������������������������������������*/
#ifndef FEATURE_WIFI_TOOLS
#define FEATURE_WIFI_TOOLS   1
#endif
#ifndef FEATURE_BLE_TOOLS
#define FEATURE_BLE_TOOLS    1
#endif
#ifndef FEATURE_BLE_DUCKY
#define FEATURE_BLE_DUCKY    BOARD_HAS_ESP32S3
#endif
#ifndef FEATURE_SUBGHZ_TOOLS
#define FEATURE_SUBGHZ_TOOLS 1
#endif

/*���������������������������������������� Utilities ����������������������������������������*/
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#endif
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

/*���������������������������������������� Settings / Themes ����������������������������������������*/
enum class Theme : uint8_t { Dark = 0, Light = 1 };

#ifndef NEOPIXEL_BRIGHT_MAX
#define NEOPIXEL_BRIGHT_MAX 64
#endif
#ifndef SETTINGS_NVS_NAMESPACE
#define SETTINGS_NVS_NAMESPACE "app_settings"
#endif

void displaySubmenu();

/*���������������������������������������� Runtime UI Palette ����������������������������������������*/
struct UiPalette { uint16_t bg, fg, icon, text, accent, line, lable, warn, ok; };
extern UiPalette UI;                   
void applyThemeToPalette(Theme t);
uint16_t uiUniversalColor();

/*���������������������������������������� UI Text Helpers ����������������������������������������*/
// Dim label text color (used for *text* only).
// Keep UI_LABLE unchanged because it's also used for fills (e.g., status bar bg).
static inline uint16_t uiDimTextColor() {
  // In Dark theme, UI_LABLE (L_Dark) is intentionally very dark; brighten text a bit.
  return (UI.bg == BG_Dark) ? GRAY : UI_LABLE;
}
#ifndef UI_DIM_TEXT
#define UI_DIM_TEXT uiDimTextColor()
#endif

/*���������������������������������������� Global State Flags ����������������������������������������*/
extern bool in_sub_menu;
extern bool feature_active;
extern bool submenu_initialized;
extern bool is_main_menu;
extern bool feature_exit_requested;

#endif

