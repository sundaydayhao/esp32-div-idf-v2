#pragma once
// config.h �� merged from bleconfig.h, wificonfig.h, subconfig.h

/* �������������������������� Common includes �������������������������� */
#include "div_display.h"
#include "div_nv_store.h"
#include "div_serial.h"
#include <nrf24l01.h>
#include <rf24.h>
#include <rc_switch.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <soc/soc_caps.h>
#include <nvs_flash.h>

// Use NimBLE for all BLE functionality, via the BleCompat shim which
// exposes BLEDevice/BLEScan/etc as aliases to NimBLE types.
#include "ble_compat.h"
#include "xpt2046_touchscreen.h"

#if defined(SOC_BT_CLASSIC_SUPPORTED) && SOC_BT_CLASSIC_SUPPORTED
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#endif

#include "div_cc1101.h"
#include "pcf8574.h"
#include "utils.h"
#include "shared.h"

/* �������������������������� Common externs �������������������������� */
extern DivDisplay tft;
extern PCF8574 pcf;

/* �������������������������� BLE namespaces �������������������������� */
namespace BleJammer {
  void blejamSetup();
  void blejamLoop();
  void exit();
}
namespace BleSpoofer {
  void spooferSetup();
  void spooferLoop();
  void exit();
}
namespace SourApple {
  void sourappleSetup();
  void sourappleLoop();
  void exit();
}
namespace BleScan {
  void bleScanSetup();
  void bleScanLoop();
  void exit();
  // Background BLE scanner: runs periodically in its own task, caching results.
  void startBackgroundScanner();
  // Last known BLE device count from background/foreground scans.
  int  getLastCount();
}
namespace Scanner {
  void scannerSetup();
  void scannerLoop();
}
namespace ProtoKill {
  void prokillSetup();
  void prokillLoop();
}
namespace BleSniffer {
  void blesnifferSetup();
  void blesnifferLoop();
  void exit();
}

/* �������������������������� SubGHz namespaces �������������������������� */
namespace replayat {
  void ReplayAttackSetup();
  void ReplayAttackLoop();
}
namespace SavedProfile {
  void saveSetup();
  void saveLoop();
}
namespace subjammer {
  void subjammerSetup();
  void subjammerLoop();
}

/* �������������������������� WiFi namespaces �������������������������� */
namespace PacketMonitor {
  void ptmSetup();
  void ptmLoop();
}
namespace BeaconSpammer {
  void beaconSpamSetup();
  void beaconSpamLoop();
}
namespace DeauthDetect {
  void deauthdetectSetup();
  void deauthdetectLoop();
}
namespace WifiScan {
  void wifiscanSetup();
  void wifiscanLoop();
  // Background WiFi scanner: runs when no feature is active, caching results.
  void startBackgroundScanner();
  // Last known WiFi network count from background/foreground scans.
  int  getLastCount();
  bool wifiCacheValidForReuse();
  int  staWifiScanSync();
  bool loadApListFromWifiCache(wifi_ap_record_t** ap_list, int* network_count,
                               int (*compare_ap)(const void*, const void*));
}
namespace CaptivePortal {
  void cportalSetup();
  void cportalLoop();
}
namespace Deauther {
  void deautherSetup();
  void deautherLoop();
}
namespace ProbeRequestFlood {
  void probeRequestFloodSetup();
  void probeRequestFloodLoop();
}
namespace FirmwareUpdate {
  void updateSetup();
  void updateLoop();
}
