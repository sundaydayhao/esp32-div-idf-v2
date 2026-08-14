#include "ui_self_test.h"

#include "config.h"
#include "ducky.h"
#include "gps.h"
#include "ir.h"
#include "rfid.h"
#include "shared.h"
#include "touchscreen.h"
#include "utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* UI_TEST_TAG = "esp32_div_ui_test";

extern int current_submenu_index;
extern void handleAboutPage();

namespace {

static constexpr uint8_t OTHER_LAYER_IR = 1;
static constexpr uint8_t OTHER_LAYER_RFID = 2;
static constexpr uint8_t OTHER_LAYER_GPS = 3;
static constexpr int TOOLS_IDX_TOUCH = 2;
static volatile bool s_uiSelfTestExitArmed = false;
static volatile uint32_t s_uiSelfTestExitDeadline = 0;

static void uiSelfTestExitTask(void*) {
  for (;;) {
    if (s_uiSelfTestExitArmed &&
        (int32_t)(millis() - s_uiSelfTestExitDeadline) >= 0) {
      feature_exit_requested = true;
      s_uiSelfTestExitArmed = false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void uiSelfTestArmExit(uint32_t ms) {
  feature_exit_requested = false;
  s_uiSelfTestExitDeadline = millis() + ms;
  s_uiSelfTestExitArmed = true;
}

static void uiSelfTestDisarmExit() {
  s_uiSelfTestExitArmed = false;
}

static void uiSelfTestRunLoopFeature(const char* path, int mainIdx, int subIdx,
                                     void (*setupFn)(), void (*loopFn)(),
                                     void (*exitFn)() = nullptr,
                                     uint32_t runMs = 1400) {
  ESP_LOGI(UI_TEST_TAG, "START %s", path);
  AppUiTestHooks::openTopSubmenu(mainIdx);
  AppUiTestHooks::tapSubmenu(path, subIdx);
  current_submenu_index = subIdx;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  setTouchButtonInputEnabled(true);
  setupFn();
  uiSelfTestArmExit(runMs);
  const uint32_t start = millis();
  while (!feature_exit_requested && (uint32_t)(millis() - start) < runMs + 600u) {
    current_submenu_index = subIdx;
    in_sub_menu = true;
    feature_active = true;
    loopFn();
    delay(3);
  }
  uiSelfTestDisarmExit();
  feature_exit_requested = true;
  if (exitFn) {
    exitFn();
  }
  ESP_LOGI(UI_TEST_TAG, "END %s elapsed=%lu", path, (unsigned long)(millis() - start));
  AppUiTestHooks::resetToMenu();
}

static void uiSelfTestRunOtherLoopFeature(const char* path, uint8_t layer, int homeIdx,
                                          int subIdx, void (*setupFn)(), void (*loopFn)(),
                                          uint32_t runMs = 1400) {
  ESP_LOGI(UI_TEST_TAG, "START %s", path);
  AppUiTestHooks::openOtherLayer(layer, homeIdx, path);
  AppUiTestHooks::tapSubmenu(path, subIdx);
  current_submenu_index = subIdx;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  setTouchButtonInputEnabled(true);
  setupFn();
  uiSelfTestArmExit(runMs);
  const uint32_t start = millis();
  while (!feature_exit_requested && (uint32_t)(millis() - start) < runMs + 600u) {
    current_submenu_index = subIdx;
    in_sub_menu = true;
    feature_active = true;
    loopFn();
    delay(3);
  }
  uiSelfTestDisarmExit();
  ESP_LOGI(UI_TEST_TAG, "END %s elapsed=%lu", path, (unsigned long)(millis() - start));
  AppUiTestHooks::resetToMenu();
}

static void uiSelfTestRunBlockingSession(const char* path, uint8_t layer, int homeIdx,
                                         int subIdx, void (*sessionFn)(),
                                         uint32_t timeoutMs = 2200) {
  ESP_LOGI(UI_TEST_TAG, "START %s", path);
  AppUiTestHooks::openOtherLayer(layer, homeIdx, path);
  AppUiTestHooks::tapSubmenu(path, subIdx);
  current_submenu_index = subIdx;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  setTouchButtonInputEnabled(true);
  uiSelfTestArmExit(timeoutMs);
  const uint32_t start = millis();
  sessionFn();
  uiSelfTestDisarmExit();
  ESP_LOGI(UI_TEST_TAG, "END %s elapsed=%lu", path, (unsigned long)(millis() - start));
  AppUiTestHooks::resetToMenu();
}

static void uiSelfTestRunRfidSession(const char* path, int subIdx, void (*sessionFn)()) {
  ESP_LOGI(UI_TEST_TAG, "START %s", path);
  AppUiTestHooks::openOtherLayer(OTHER_LAYER_RFID, 1, "RFID/NFC");
  AppUiTestHooks::tapSubmenu(path, subIdx);
  current_submenu_index = subIdx;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  setTouchButtonInputEnabled(true);
  if (!RfidNfc::begin()) {
    ESP_LOGW(UI_TEST_TAG, "SKIP %s: PN532 not found", path);
    restoreSdAfterSharedSpi();
    AppUiTestHooks::resetToMenu();
    return;
  }
  uiSelfTestArmExit(2200);
  const uint32_t start = millis();
  sessionFn();
  uiSelfTestDisarmExit();
  restoreSdAfterSharedSpi();
  ESP_LOGI(UI_TEST_TAG, "END %s elapsed=%lu", path, (unsigned long)(millis() - start));
  AppUiTestHooks::resetToMenu();
}

static void uiSelfTestRunTouchCalibration() {
  ESP_LOGI(UI_TEST_TAG, "START Tools/Touch Calibrate");
  AppUiTestHooks::openTopSubmenu(6);
  AppUiTestHooks::tapSubmenu("Tools/Touch Calibrate", TOOLS_IDX_TOUCH);
  current_submenu_index = TOOLS_IDX_TOUCH;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  TouchCalib::setup();
  const int16_t xs[4] = {300, 3800, 3800, 300};
  const int16_t ys[4] = {3800, 3800, 300, 300};
  for (int i = 0; i < 4 && !feature_exit_requested; ++i) {
    injectTouchRawXYForTest(xs[i], ys[i], 900, 3);
    TouchCalib::loop();
    clearInjectedTouchForTest();
    for (int r = 0; r < 4; ++r) {
      TouchCalib::loop();
    }
  }
  for (int i = 0; i < 4 && !feature_exit_requested; ++i) {
    TouchCalib::loop();
  }
  ESP_LOGI(UI_TEST_TAG, "END Tools/Touch Calibrate");
  AppUiTestHooks::resetToMenu();
}


}  // namespace

void UiSelfTest::runOnce() {
  static bool s_exitTaskStarted = false;
  if (!s_exitTaskStarted) {
    xTaskCreate(uiSelfTestExitTask, "ui_test_exit", 2048, nullptr, 5, nullptr);
    s_exitTaskStarted = true;
  }

  ESP_LOGI(UI_TEST_TAG, "BEGIN full UI traversal");

  uiSelfTestRunLoopFeature("WiFi/Packet Monitor", 0, 0, PacketMonitor::ptmSetup, PacketMonitor::ptmLoop);
  uiSelfTestRunLoopFeature("WiFi/Beacon Spammer", 0, 1, BeaconSpammer::beaconSpamSetup, BeaconSpammer::beaconSpamLoop);
  uiSelfTestRunLoopFeature("WiFi/WiFi Deauther", 0, 2, Deauther::deautherSetup, Deauther::deautherLoop);
  uiSelfTestRunLoopFeature("WiFi/Probe Request Flood", 0, 3, ProbeRequestFlood::probeRequestFloodSetup, ProbeRequestFlood::probeRequestFloodLoop);
  uiSelfTestRunLoopFeature("WiFi/Deauth Detector", 0, 4, DeauthDetect::deauthdetectSetup, DeauthDetect::deauthdetectLoop);
  uiSelfTestRunLoopFeature("WiFi/WiFi Scanner", 0, 5, WifiScan::wifiscanSetup, WifiScan::wifiscanLoop);
  uiSelfTestRunLoopFeature("WiFi/Captive Portal", 0, 6, CaptivePortal::cportalSetup, CaptivePortal::cportalLoop);

  uiSelfTestRunLoopFeature("2.4GHz/Scanner", 1, 0, Scanner::scannerSetup, Scanner::scannerLoop);
  uiSelfTestRunLoopFeature("2.4GHz/Proto Kill", 1, 1, ProtoKill::prokillSetup, ProtoKill::prokillLoop);

  uiSelfTestRunOtherLoopFeature("More/IR Remote/Record", OTHER_LAYER_IR, 0, 0, IRRemoteFeature::setup, IRRemoteFeature::loop);
  uiSelfTestRunOtherLoopFeature("More/IR Remote/Saved Profile", OTHER_LAYER_IR, 0, 1, IRSavedProfile::setup, IRSavedProfile::loop);
  uiSelfTestRunOtherLoopFeature("More/IR Remote/Universal Controller", OTHER_LAYER_IR, 0, 2, IRUniversalController::setup, IRUniversalController::loop);

  uiSelfTestRunRfidSession("More/RFID/Card Reader", 0, RfidNfc::sessionCardReader);
  uiSelfTestRunRfidSession("More/RFID/Card Clone", 1, RfidNfc::sessionClone);
  uiSelfTestRunRfidSession("More/RFID/Erase", 2, RfidNfc::sessionErase);
  uiSelfTestRunRfidSession("More/RFID/Dump", 3, RfidNfc::sessionDump);
  uiSelfTestRunRfidSession("More/RFID/Decode Access", 4, RfidNfc::sessionDecodeAccess);
  uiSelfTestRunRfidSession("More/RFID/Jam Reader", 5, RfidNfc::sessionJamReader);
  uiSelfTestRunRfidSession("More/RFID/Tag Disrupt", 6, RfidNfc::sessionTagDisrupt);
  uiSelfTestRunRfidSession("More/RFID/Disrupt Emulate", 7, RfidNfc::sessionDisruptEmulate);

  uiSelfTestRunBlockingSession("More/GPS/Wardriver", OTHER_LAYER_GPS, 2, 0, GpsWardriver::session, 3200);
  uiSelfTestRunBlockingSession("More/GPS/Satellite Scanner", OTHER_LAYER_GPS, 2, 1, GpsSatelliteScanner::session, 2600);

  uiSelfTestRunLoopFeature("Bluetooth/BLE Jammer", 4, 0, BleJammer::blejamSetup, BleJammer::blejamLoop, BleJammer::exit);
  uiSelfTestRunLoopFeature("Bluetooth/BLE Spoofer", 4, 1, BleSpoofer::spooferSetup, BleSpoofer::spooferLoop, BleSpoofer::exit);
  uiSelfTestRunLoopFeature("Bluetooth/Sour Apple", 4, 2, SourApple::sourappleSetup, SourApple::sourappleLoop, SourApple::exit);
  uiSelfTestRunLoopFeature("Bluetooth/Sniffer", 4, 3, BleSniffer::blesnifferSetup, BleSniffer::blesnifferLoop, BleSniffer::exit);
  uiSelfTestRunLoopFeature("Bluetooth/BLE Scanner", 4, 4, BleScan::bleScanSetup, BleScan::bleScanLoop, BleScan::exit);
  uiSelfTestRunLoopFeature("Bluetooth/BLE Rubber Ducky", 4, 5, Ducky::enter, Ducky::loop, Ducky::exit);

  uiSelfTestRunLoopFeature("SubGHz/Replay Attack", 5, 0, replayat::ReplayAttackSetup, replayat::ReplayAttackLoop);
  uiSelfTestRunLoopFeature("SubGHz/SubGHz Jammer", 5, 1, subjammer::subjammerSetup, subjammer::subjammerLoop);
  uiSelfTestRunLoopFeature("SubGHz/Saved Profile", 5, 2, SavedProfile::saveSetup, SavedProfile::saveLoop);

  uiSelfTestRunLoopFeature("Tools/Serial Console", 6, 0, Terminal::terminalSetup, Terminal::terminalLoop);
  uiSelfTestRunLoopFeature("Tools/Update Firmware", 6, 1, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
  uiSelfTestRunTouchCalibration();
  uiSelfTestRunLoopFeature("Tools/SD File Manager", 6, 3, SdFileManager::setup, SdFileManager::loop);

  ESP_LOGI(UI_TEST_TAG, "START Settings");
  AppUiTestHooks::tapMain(3);
  feature_active = true;
  feature_exit_requested = false;
  AppSettingsUI::setup();
  uiSelfTestArmExit(1400);
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
    delay(3);
  }
  uiSelfTestDisarmExit();
  AppUiTestHooks::resetToMenu();
  ESP_LOGI(UI_TEST_TAG, "END Settings");

  ESP_LOGI(UI_TEST_TAG, "START About");
  AppUiTestHooks::tapMain(7);
  uiSelfTestArmExit(900);
  handleAboutPage();
  uiSelfTestDisarmExit();
  AppUiTestHooks::resetToMenu();
  ESP_LOGI(UI_TEST_TAG, "END About");

  ESP_LOGI(UI_TEST_TAG, "DONE full UI traversal");
}

void UiSelfTest::armExit(uint32_t ms) {
  uiSelfTestArmExit(ms);
}

void UiSelfTest::disarmExit() {
  uiSelfTestDisarmExit();
}

void UiSelfTest::pollAutoRun() {
#if CONFIG_ESP32_DIV_UI_SELF_TEST_AUTO_RUN
  static bool s_uiSelfTestDone = false;
  if (!s_uiSelfTestDone && millis() > 4000) {
    s_uiSelfTestDone = true;
    UiSelfTest::runOnce();
  }
#endif
}