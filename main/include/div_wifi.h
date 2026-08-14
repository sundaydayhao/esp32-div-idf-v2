#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "div_string.h"
#include "esp_wifi.h"
#include "esp_tls.h"

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)
#define WL_CONNECTED 3

#ifndef WIFI_STA
#define WIFI_STA WIFI_MODE_STA
#endif
#ifndef WIFI_AP
#define WIFI_AP WIFI_MODE_AP
#endif
#ifndef WIFI_AP_STA
#define WIFI_AP_STA WIFI_MODE_APSTA
#endif

class DivIpAddress {
 public:
  DivIpAddress() = default;
  explicit DivIpAddress(uint32_t addr);
  DivString toString() const;
  const char* c_str() const;
  operator DivString() const { return toString(); }

 private:
  mutable std::string cache_;
  uint32_t addr_ = 0;
};

class DivWiFiClass {
 public:
  bool mode(wifi_mode_t mode);
  void disconnect(bool erase = false);
  bool softAP(const char* ssid, const char* pass = nullptr, int channel = 1);
  void softAPdisconnect(bool wifioff = false);
  DivIpAddress softAPIP();
  DivIpAddress localIP();
  void begin();
  void begin(const char* ssid, const char* pass = nullptr);
  int status();
  int scanNetworks(bool async = false, bool hidden = true, bool passive = false, uint32_t maxMsPerChan = 120);
  int scanComplete();
  void scanDelete();
  DivString SSID(int i) const;
  const uint8_t* BSSID(int i) const;
  DivString BSSIDstr(int i) const;
  int RSSI(int i) const;
  int channel(int i) const;
  wifi_auth_mode_t encryptionType(int i) const;

 private:
  bool ensure();
  void cacheResults();

  bool initialized_ = false;
  bool scanReady_ = false;
  std::vector<wifi_ap_record_t> aps_;
};

class DivTlsClient {
 public:
  DivTlsClient();
  ~DivTlsClient();
  void setInsecure();
  bool connect(const char* host, int port);
  bool connected() const { return connected_; }
  void stop();
  int available();
  int read();
  DivString readStringUntil(char delim);
  size_t write(const uint8_t* data, size_t len);
  size_t write(const char* data);
  size_t print(const char* s);
  size_t print(const DivString& s) { return print(s.c_str()); }
  size_t printf(const char* fmt, ...);

 private:
  esp_tls_t* tls_ = nullptr;
  bool connected_ = false;
  bool insecure_ = false;
  std::vector<uint8_t> rx_;
};

extern DivWiFiClass DivWiFi;
