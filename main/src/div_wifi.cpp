#include "div_wifi.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "lwip/inet.h"

namespace {
constexpr const char* kTag = "DivWiFi";
bool g_scanDone = false;
esp_netif_t* g_staNetif = nullptr;
esp_netif_t* g_apNetif = nullptr;

void onWifiEvent(void*, esp_event_base_t eventBase, int32_t eventId, void*) {
  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_SCAN_DONE) {
    g_scanDone = true;
  }
}

void copyStringToField(uint8_t* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  std::snprintf(reinterpret_cast<char*>(dst), dstSize, "%s", src ? src : "");
}
}  // namespace

DivWiFiClass DivWiFi;

DivIpAddress::DivIpAddress(uint32_t addr) : addr_(addr) {}

DivString DivIpAddress::toString() const {
  esp_ip4_addr_t ip{};
  ip.addr = addr_;
  char buf[16] = "0.0.0.0";
  esp_ip4addr_ntoa(&ip, buf, sizeof(buf));
  return DivString(buf);
}

const char* DivIpAddress::c_str() const {
  cache_ = static_cast<std::string>(toString());
  return cache_.c_str();
}

bool DivWiFiClass::ensure() {
  if (initialized_) {
    return true;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "esp_netif_init failed: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "event loop init failed: %s", esp_err_to_name(err));
    return false;
  }

  if (!g_staNetif) {
    g_staNetif = esp_netif_create_default_wifi_sta();
  }
  if (!g_apNetif) {
    g_apNetif = esp_netif_create_default_wifi_ap();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return false;
  }
  esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, onWifiEvent, nullptr, nullptr);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "esp_wifi_start failed: %s", esp_err_to_name(err));
  }
  initialized_ = true;
  return true;
}

bool DivWiFiClass::mode(wifi_mode_t m) {
  if (!ensure()) {
    return false;
  }
  esp_err_t err = esp_wifi_set_mode(m);
  return err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT;
}

void DivWiFiClass::disconnect(bool erase) {
  if (!ensure()) {
    return;
  }
  esp_wifi_disconnect();
  if (erase) {
    esp_wifi_restore();
    initialized_ = false;
  }
}

bool DivWiFiClass::softAP(const char* ssid, const char* pass, int channel) {
  if (!ensure()) {
    return false;
  }
  esp_wifi_set_mode(WIFI_MODE_AP);
  wifi_config_t cfg = {};
  copyStringToField(cfg.ap.ssid, sizeof(cfg.ap.ssid), ssid);
  cfg.ap.ssid_len = std::strlen(reinterpret_cast<const char*>(cfg.ap.ssid));
  copyStringToField(cfg.ap.password, sizeof(cfg.ap.password), pass);
  cfg.ap.channel = channel > 0 ? static_cast<uint8_t>(channel) : 1;
  cfg.ap.max_connection = 4;
  cfg.ap.authmode = (pass && std::strlen(pass) >= 8) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
  esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
  if (err == ESP_OK) {
    err = esp_wifi_start();
  }
  return err == ESP_OK || err == ESP_ERR_WIFI_CONN || err == ESP_ERR_INVALID_STATE;
}

void DivWiFiClass::softAPdisconnect(bool wifioff) {
  if (!ensure()) {
    return;
  }
  if (wifioff) {
    esp_wifi_stop();
  } else {
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }
}

DivIpAddress DivWiFiClass::softAPIP() {
  esp_netif_ip_info_t info = {};
  if (g_apNetif) {
    esp_netif_get_ip_info(g_apNetif, &info);
  }
  return DivIpAddress(info.ip.addr);
}

DivIpAddress DivWiFiClass::localIP() {
  esp_netif_ip_info_t info = {};
  if (g_staNetif) {
    esp_netif_get_ip_info(g_staNetif, &info);
  }
  return DivIpAddress(info.ip.addr);
}

void DivWiFiClass::begin() {
  if (!ensure()) {
    return;
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_connect();
}

void DivWiFiClass::begin(const char* ssid, const char* pass) {
  if (!ensure()) {
    return;
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  wifi_config_t cfg = {};
  copyStringToField(cfg.sta.ssid, sizeof(cfg.sta.ssid), ssid);
  copyStringToField(cfg.sta.password, sizeof(cfg.sta.password), pass);
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_wifi_connect();
}

int DivWiFiClass::status() {
  if (!ensure()) {
    return 0;
  }
  wifi_ap_record_t ap = {};
  return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? WL_CONNECTED : 0;
}

int DivWiFiClass::scanNetworks(bool async, bool hidden, bool passive, uint32_t maxMsPerChan) {
  if (!ensure()) {
    return 0;
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();

  wifi_scan_config_t cfg = {};
  cfg.show_hidden = hidden;
  cfg.scan_type = passive ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 40;
  cfg.scan_time.active.max = maxMsPerChan > 0 ? maxMsPerChan : 120;
  cfg.scan_time.passive = maxMsPerChan > 0 ? maxMsPerChan : 120;

  g_scanDone = false;
  scanReady_ = false;
  aps_.clear();
  esp_err_t err = esp_wifi_scan_start(&cfg, !async);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "scan start failed: %s", esp_err_to_name(err));
    return WIFI_SCAN_FAILED;
  }
  if (async) {
    return WIFI_SCAN_RUNNING;
  }
  g_scanDone = true;
  cacheResults();
  return static_cast<int>(aps_.size());
}

int DivWiFiClass::scanComplete() {
  if (scanReady_) {
    return static_cast<int>(aps_.size());
  }
  if (!g_scanDone) {
    return WIFI_SCAN_RUNNING;
  }
  cacheResults();
  return static_cast<int>(aps_.size());
}

void DivWiFiClass::scanDelete() {
  aps_.clear();
  scanReady_ = false;
  g_scanDone = false;
  esp_wifi_clear_ap_list();
}

DivString DivWiFiClass::SSID(int i) const {
  if (i < 0 || static_cast<size_t>(i) >= aps_.size()) {
    return DivString();
  }
  return DivString(reinterpret_cast<const char*>(aps_[i].ssid));
}

const uint8_t* DivWiFiClass::BSSID(int i) const {
  static const uint8_t empty[6] = {};
  if (i < 0 || static_cast<size_t>(i) >= aps_.size()) {
    return empty;
  }
  return aps_[i].bssid;
}

DivString DivWiFiClass::BSSIDstr(int i) const {
  const uint8_t* b = BSSID(i);
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
  return DivString(buf);
}

int DivWiFiClass::RSSI(int i) const {
  return (i >= 0 && static_cast<size_t>(i) < aps_.size()) ? aps_[i].rssi : 0;
}

int DivWiFiClass::channel(int i) const {
  return (i >= 0 && static_cast<size_t>(i) < aps_.size()) ? aps_[i].primary : 0;
}

wifi_auth_mode_t DivWiFiClass::encryptionType(int i) const {
  return (i >= 0 && static_cast<size_t>(i) < aps_.size()) ? aps_[i].authmode : WIFI_AUTH_OPEN;
}

void DivWiFiClass::cacheResults() {
  uint16_t count = 0;
  if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || count == 0) {
    aps_.clear();
    scanReady_ = true;
    return;
  }
  aps_.assign(count, wifi_ap_record_t{});
  uint16_t readCount = count;
  if (esp_wifi_scan_get_ap_records(&readCount, aps_.data()) != ESP_OK) {
    aps_.clear();
  } else {
    aps_.resize(readCount);
  }
  scanReady_ = true;
}

DivTlsClient::DivTlsClient() = default;

DivTlsClient::~DivTlsClient() {
  stop();
}

void DivTlsClient::setInsecure() {
  insecure_ = true;
}

bool DivTlsClient::connect(const char* host, int port) {
  stop();
  if (!host || port <= 0) {
    return false;
  }
  tls_ = esp_tls_init();
  if (!tls_) {
    return false;
  }
  esp_tls_cfg_t cfg = {};
  cfg.timeout_ms = 10000;
  cfg.skip_common_name = insecure_;
  const int rc = esp_tls_conn_new_sync(host, std::strlen(host), port, &cfg, tls_);
  connected_ = rc == 1;
  if (!connected_) {
    stop();
  }
  return connected_;
}

void DivTlsClient::stop() {
  if (tls_) {
    esp_tls_conn_destroy(tls_);
    tls_ = nullptr;
  }
  connected_ = false;
  rx_.clear();
}

int DivTlsClient::available() {
  return static_cast<int>(rx_.size());
}

int DivTlsClient::read() {
  if (!connected_ || !tls_) {
    return -1;
  }
  if (!rx_.empty()) {
    const int ch = rx_.front();
    rx_.erase(rx_.begin());
    return ch;
  }
  uint8_t ch = 0;
  const ssize_t n = esp_tls_conn_read(tls_, &ch, 1);
  return n == 1 ? ch : -1;
}

DivString DivTlsClient::readStringUntil(char delim) {
  DivString out;
  int ch = 0;
  while ((ch = read()) >= 0) {
    if (ch == delim) {
      break;
    }
    out += static_cast<char>(ch);
  }
  return out;
}

size_t DivTlsClient::write(const uint8_t* data, size_t len) {
  if (!connected_ || !tls_ || !data || len == 0) {
    return 0;
  }
  const ssize_t n = esp_tls_conn_write(tls_, data, len);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t DivTlsClient::write(const char* data) {
  return data ? write(reinterpret_cast<const uint8_t*>(data), std::strlen(data)) : 0;
}

size_t DivTlsClient::print(const char* s) {
  return write(s);
}

size_t DivTlsClient::printf(const char* fmt, ...) {
  if (!fmt) {
    return 0;
  }
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return 0;
  }
  const size_t len = static_cast<size_t>(n);
  if (len < sizeof(buf)) {
    return write(reinterpret_cast<const uint8_t*>(buf), len);
  }
  std::vector<char> dyn(len + 1);
  va_start(ap, fmt);
  std::vsnprintf(dyn.data(), dyn.size(), fmt, ap);
  va_end(ap);
  return write(reinterpret_cast<const uint8_t*>(dyn.data()), len);
}
