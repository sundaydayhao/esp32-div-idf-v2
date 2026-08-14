#include "div_sd.h"

#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "board_pins.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {
constexpr const char* kTag = "DivSd";
constexpr const char* kBasePath = "/sdcard";
sdmmc_card_t* g_card = nullptr;
bool g_busReady = false;

std::string parentPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return std::string(kBasePath);
  }
  return path.substr(0, slash);
}

void ensureParentDirs(const std::string& path) {
  std::string cur;
  std::string parent = parentPath(path);
  if (parent.rfind(kBasePath, 0) != 0) {
    return;
  }
  for (size_t i = std::strlen(kBasePath) + 1; i <= parent.size(); ++i) {
    if (i == parent.size() || parent[i] == '/') {
      cur = parent.substr(0, i);
      if (!cur.empty()) {
        mkdir(cur.c_str(), 0775);
      }
    }
  }
}
}  // namespace

DivSdClass DivSd;

DivFile::DivFile(DivFile&& other) noexcept {
  *this = std::move(other);
}

DivFile& DivFile::operator=(DivFile&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  close();
  file_ = other.file_;
  dir_ = other.dir_;
  path_ = std::move(other.path_);
  name_ = std::move(other.name_);
  other.file_ = nullptr;
  other.dir_ = nullptr;
  return *this;
}

DivFile::~DivFile() {
  close();
}

DivFile DivFile::openFile(const std::string& mappedPath, const std::string& displayName, const char* mode) {
  DivFile out;
  errno = 0;
  out.file_ = std::fopen(mappedPath.c_str(), mode ? mode : FILE_READ);
  if (out.file_) {
    out.path_ = mappedPath;
    out.name_ = displayName;
  } else {
    const int e = errno;
    ESP_LOGW(kTag, "open failed path=%s mode=%s errno=%d (%s)", mappedPath.c_str(),
             mode ? mode : FILE_READ, e, std::strerror(e));
  }
  return out;
}

DivFile DivFile::openDir(const std::string& mappedPath, const std::string& displayName) {
  DivFile out;
  out.dir_ = opendir(mappedPath.c_str());
  if (out.dir_) {
    out.path_ = mappedPath;
    out.name_ = displayName;
  }
  return out;
}

size_t DivFile::size() {
  if (file_) {
    const long cur = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, cur, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }
  struct stat st = {};
  if (!path_.empty() && stat(path_.c_str(), &st) == 0) {
    return static_cast<size_t>(st.st_size);
  }
  return 0;
}

bool DivFile::seek(size_t pos) {
  return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
}

int DivFile::available() {
  if (!file_) {
    return 0;
  }
  const long cur = std::ftell(file_);
  std::fseek(file_, 0, SEEK_END);
  const long end = std::ftell(file_);
  std::fseek(file_, cur, SEEK_SET);
  return end > cur ? static_cast<int>(end - cur) : 0;
}

int DivFile::read() {
  if (!file_) {
    return -1;
  }
  return std::fgetc(file_);
}

int DivFile::read(uint8_t* buf, size_t len) {
  if (!file_ || !buf || len == 0) {
    return 0;
  }
  return static_cast<int>(std::fread(buf, 1, len, file_));
}

size_t DivFile::readBytes(uint8_t* buf, size_t len) {
  return static_cast<size_t>(read(buf, len));
}

size_t DivFile::readBytesUntil(char delim, uint8_t* buf, size_t len) {
  if (!file_ || !buf || len == 0) {
    return 0;
  }
  size_t count = 0;
  int ch = 0;
  while (count < len && (ch = std::fgetc(file_)) != EOF) {
    if (ch == delim) {
      break;
    }
    buf[count++] = static_cast<uint8_t>(ch);
  }
  return count;
}

DivString DivFile::readStringUntil(char delim) {
  DivString out;
  if (!file_) {
    return out;
  }
  int ch = 0;
  while ((ch = std::fgetc(file_)) != EOF) {
    if (ch == delim) {
      break;
    }
    out += static_cast<char>(ch);
  }
  return out;
}

size_t DivFile::write(const uint8_t* data, size_t len) {
  if (!file_ || !data || len == 0) {
    return 0;
  }
  return std::fwrite(data, 1, len, file_);
}

size_t DivFile::write(uint8_t data) {
  return write(&data, 1);
}

size_t DivFile::print(const char* s) {
  return s ? write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)) : 0;
}

size_t DivFile::print(char c) {
  return write(static_cast<uint8_t>(c));
}

size_t DivFile::print(int v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d", v);
  return print(buf);
}

size_t DivFile::print(unsigned int v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%u", v);
  return print(buf);
}

size_t DivFile::print(long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%ld", v);
  return print(buf);
}

size_t DivFile::print(unsigned long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lu", v);
  return print(buf);
}

size_t DivFile::print(float v) {
  return print(static_cast<double>(v), 2);
}

size_t DivFile::print(float v, int decimals) {
  return print(static_cast<double>(v), decimals);
}

size_t DivFile::print(double v) {
  return print(v, 2);
}

size_t DivFile::print(double v, int decimals) {
  if (decimals < 0) decimals = 2;
  if (decimals > 8) decimals = 8;
  char fmt[12];
  char buf[48];
  std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  std::snprintf(buf, sizeof(buf), fmt, v);
  return print(buf);
}

size_t DivFile::println() {
  return print("\n");
}

size_t DivFile::printf(const char* fmt, ...) {
  if (!file_ || !fmt) {
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
    return print(buf);
  }
  std::vector<char> dyn(len + 1);
  va_start(ap, fmt);
  std::vsnprintf(dyn.data(), dyn.size(), fmt, ap);
  va_end(ap);
  return print(dyn.data());
}

void DivFile::flush() {
  if (file_) {
    std::fflush(file_);
  }
}

void DivFile::close() {
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
  if (dir_) {
    closedir(dir_);
    dir_ = nullptr;
  }
}

DivFile DivFile::openNextFile() {
  if (!dir_) {
    return DivFile();
  }
  while (dirent* ent = readdir(dir_)) {
    if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    const std::string full = path_ + "/" + ent->d_name;
    struct stat st = {};
    if (stat(full.c_str(), &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      return openDir(full, ent->d_name);
    }
    return openFile(full, ent->d_name, FILE_READ);
  }
  return DivFile();
}

bool DivSdClass::begin(int cs) {
  if (mounted_) {
    return true;
  }
  cs_ = cs >= 0 ? cs : DIV_SD_CS;
  return mount(cs_);
}

void DivSdClass::end() {
  if (mounted_) {
    esp_vfs_fat_sdcard_unmount(kBasePath, g_card);
    g_card = nullptr;
    mounted_ = false;
  }
}

bool DivSdClass::exists(const char* path) {
  struct stat st = {};
  return mounted_ && stat(mapPath(path).c_str(), &st) == 0;
}

bool DivSdClass::mkdir(const char* path) {
  if (!mounted_) {
    return false;
  }
  const std::string mapped = mapPath(path);
  return ::mkdir(mapped.c_str(), 0775) == 0 || errno == EEXIST;
}

bool DivSdClass::remove(const char* path) {
  return mounted_ && ::unlink(mapPath(path).c_str()) == 0;
}

bool DivSdClass::rmdir(const char* path) {
  return mounted_ && ::rmdir(mapPath(path).c_str()) == 0;
}

DivFile DivSdClass::open(const char* path, const char* mode) {
  if (!mounted_) {
    return DivFile();
  }
  const std::string mapped = mapPath(path);
  struct stat st = {};
  if (stat(mapped.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    return DivFile::openDir(mapped, path ? path : "/");
  }
  const char* fileMode = mode ? mode : FILE_READ;
  if (std::strchr(fileMode, 'w') || std::strchr(fileMode, 'a')) {
    ensureParentDirs(mapped);
  }
  return DivFile::openFile(mapped, path ? path : "", fileMode);
}

bool DivSdClass::mount(int cs) {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = DIV_SD_HOST;
  host.max_freq_khz = 4000;

  spi_bus_config_t busCfg = {};
  busCfg.mosi_io_num = DIV_SD_MOSI;
  busCfg.miso_io_num = DIV_SD_MISO;
  busCfg.sclk_io_num = DIV_SD_SCLK;
  busCfg.quadwp_io_num = -1;
  busCfg.quadhd_io_num = -1;
  busCfg.max_transfer_sz = 4096;

  if (!g_busReady) {
    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &busCfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(kTag, "SD SPI bus init failed: %s", esp_err_to_name(err));
      return false;
    }
    g_busReady = true;
  }

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = static_cast<gpio_num_t>(cs);
  slot.host_id = static_cast<spi_host_device_t>(host.slot);

  esp_vfs_fat_sdmmc_mount_config_t mountCfg = {};
  mountCfg.format_if_mount_failed = false;
  mountCfg.max_files = 8;
  mountCfg.allocation_unit_size = 16 * 1024;

  esp_err_t err = esp_vfs_fat_sdspi_mount(kBasePath, &host, &slot, &mountCfg, &g_card);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "mount failed on CS %d: %s", cs, esp_err_to_name(err));
    return false;
  }
  mounted_ = true;
  return true;
}

std::string DivSdClass::mapPath(const char* path) const {
  std::string p = path && *path ? path : "/";
  if (p == "/") {
    return kBasePath;
  }
  while (!p.empty() && p.front() == '/') {
    p.erase(p.begin());
  }
  return std::string(kBasePath) + "/" + p;
}
