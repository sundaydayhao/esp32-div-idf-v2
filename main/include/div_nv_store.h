#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

class DivNvStoreClass {
 public:
  DivNvStoreClass();

  bool begin(std::size_t size);
  std::uint8_t read(std::size_t address) const;
  void write(std::size_t address, std::uint8_t value);

  template <typename T>
  T& get(std::size_t address, T& value) const {
    static_assert(std::is_trivially_copyable<T>::value,
                  "DivNvStore get() requires trivially copyable types");
    readBytes(address, &value, sizeof(T));
    return value;
  }

  template <typename T>
  const T& put(std::size_t address, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "DivNvStore put() requires trivially copyable types");
    writeBytes(address, &value, sizeof(T));
    return value;
  }

  bool commit();

 private:
  void ensureCapacity(std::size_t size);
  void loadFromNvs();
  void readBytes(std::size_t address, void* dst, std::size_t len) const;
  void writeBytes(std::size_t address, const void* src, std::size_t len);

  std::size_t size_;
  bool initialized_;
};

extern DivNvStoreClass DivNvStore;
