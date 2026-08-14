#include "div_nv_store.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char* kNamespace = "div_nv_store";
constexpr const char* kBlobKey = "blob";

std::vector<std::uint8_t> g_buffer;
std::size_t g_capacity = 0;
bool g_loaded = false;
bool g_dirty = false;

bool openNvs(nvs_handle_t* handle) {
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, handle);
  if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
    if (nvs_flash_init() == ESP_OK) {
      err = nvs_open(kNamespace, NVS_READWRITE, handle);
    }
  }
  return err == ESP_OK;
}
}  // namespace

DivNvStoreClass DivNvStore;

DivNvStoreClass::DivNvStoreClass() : size_(0), initialized_(false) {}

void DivNvStoreClass::ensureCapacity(std::size_t size) {
  if (g_capacity == size && g_buffer.size() == size) {
    return;
  }

  std::vector<std::uint8_t> next(size, 0);
  const std::size_t copyLen = std::min(g_buffer.size(), next.size());
  if (copyLen > 0) {
    std::memcpy(next.data(), g_buffer.data(), copyLen);
  }
  g_buffer.swap(next);
  g_capacity = size;
}

void DivNvStoreClass::loadFromNvs() {
  if (g_loaded) {
    return;
  }

  nvs_handle_t handle = 0;
  if (!openNvs(&handle)) {
    g_loaded = true;
    return;
  }

  std::size_t storedSize = 0;
  esp_err_t err = nvs_get_blob(handle, kBlobKey, nullptr, &storedSize);
  if (err == ESP_OK && storedSize > 0) {
    std::vector<std::uint8_t> stored(storedSize, 0);
    std::size_t readSize = storedSize;
    err = nvs_get_blob(handle, kBlobKey, stored.data(), &readSize);
    if (err == ESP_OK) {
      g_buffer = std::move(stored);
      g_capacity = g_buffer.size();
    }
  }

  nvs_close(handle);
  g_loaded = true;
}

bool DivNvStoreClass::begin(std::size_t size) {
  loadFromNvs();
  ensureCapacity(size);
  size_ = size;
  initialized_ = true;
  return true;
}

std::uint8_t DivNvStoreClass::read(std::size_t address) const {
  if (!initialized_ || address >= size_ || address >= g_buffer.size()) {
    return 0;
  }
  return g_buffer[address];
}

void DivNvStoreClass::write(std::size_t address, std::uint8_t value) {
  if (!initialized_ || address >= size_) {
    return;
  }
  if (address >= g_buffer.size()) {
    g_buffer.resize(size_, 0);
  }
  if (g_buffer[address] != value) {
    g_buffer[address] = value;
    g_dirty = true;
  }
}

void DivNvStoreClass::readBytes(std::size_t address, void* dst, std::size_t len) const {
  if (!dst || len == 0) {
    return;
  }
  std::uint8_t* out = static_cast<std::uint8_t*>(dst);
  for (std::size_t i = 0; i < len; ++i) {
    out[i] = read(address + i);
  }
}

void DivNvStoreClass::writeBytes(std::size_t address, const void* src, std::size_t len) {
  if (!src || len == 0) {
    return;
  }
  const std::uint8_t* in = static_cast<const std::uint8_t*>(src);
  for (std::size_t i = 0; i < len; ++i) {
    write(address + i, in[i]);
  }
}

bool DivNvStoreClass::commit() {
  if (!initialized_) {
    return false;
  }
  if (size_ == 0 || !g_dirty) {
    return true;
  }

  nvs_handle_t handle = 0;
  if (!openNvs(&handle)) {
    return false;
  }

  esp_err_t err = nvs_set_blob(handle, kBlobKey, g_buffer.data(), size_);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err == ESP_OK) {
    g_dirty = false;
    return true;
  }
  return false;
}
