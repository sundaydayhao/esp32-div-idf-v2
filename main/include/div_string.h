#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <type_traits>

enum : uint8_t {
  DIV_BIN = 2,
  DIV_OCT = 8,
  DIV_DEC = 10,
  DIV_HEX = 16,
};

class DivString {
 public:
  DivString() = default;
  DivString(std::nullptr_t) {}
  DivString(const char* s) : value_(s ? s : "") {}
  DivString(const char* s, size_t len) : value_(s ? std::string(s, len) : std::string()) {}
  DivString(const std::string& s) : value_(s) {}
  DivString(std::string&& s) : value_(std::move(s)) {}
  DivString(char c) : value_(1, c) {}
  DivString(unsigned char c) : value_(1, static_cast<char>(c)) {}
  DivString(int v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(unsigned int v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(long v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(unsigned long v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(long long v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(unsigned long long v, int base = DIV_DEC) { assignNumber(v, base); }
  DivString(float v, int decimals = 2) { assignFloat(v, decimals); }
  DivString(double v, int decimals = 2) { assignFloat(v, decimals); }

  const char* c_str() const { return value_.c_str(); }
  const char* data() const { return value_.data(); }
  size_t length() const { return value_.length(); }
  size_t size() const { return value_.size(); }
  bool empty() const { return value_.empty(); }
  bool isEmpty() const { return value_.empty(); }
  void clear() { value_.clear(); }
  void reserve(size_t n) { value_.reserve(n); }
  char charAt(size_t idx) const { return (*this)[idx]; }
  void setCharAt(size_t idx, char c) {
    if (idx < value_.size()) value_[idx] = c;
  }

  char operator[](size_t idx) const { return idx < value_.size() ? value_[idx] : '\0'; }
  char operator[](int idx) const { return idx >= 0 ? (*this)[static_cast<size_t>(idx)] : '\0'; }
  char& operator[](size_t idx) { return value_[idx]; }
  char& operator[](int idx) { return value_[static_cast<size_t>(idx)]; }

  DivString& operator=(const char* s) {
    value_ = s ? s : "";
    return *this;
  }
  DivString& operator=(const std::string& s) {
    value_ = s;
    return *this;
  }

  DivString& operator+=(const DivString& rhs) {
    value_ += rhs.value_;
    return *this;
  }
  DivString& operator+=(const char* rhs) {
    value_ += rhs ? rhs : "";
    return *this;
  }
  DivString& operator+=(char rhs) {
    value_ += rhs;
    return *this;
  }
  template <typename T>
  DivString& operator+=(T rhs) {
    value_ += DivString(rhs).value_;
    return *this;
  }

  friend DivString operator+(const DivString& a, const DivString& b) {
    return DivString(a.value_ + b.value_);
  }
  friend DivString operator+(const DivString& a, const char* b) {
    return DivString(a.value_ + (b ? b : ""));
  }
  friend DivString operator+(const char* a, const DivString& b) {
    return DivString(std::string(a ? a : "") + b.value_);
  }
  friend DivString operator+(const DivString& a, char b) {
    std::string out = a.value_;
    out += b;
    return DivString(std::move(out));
  }
  template <typename T>
  friend DivString operator+(const DivString& a, T b) {
    return a + DivString(b);
  }

  bool operator==(const DivString& rhs) const { return value_ == rhs.value_; }
  bool operator!=(const DivString& rhs) const { return value_ != rhs.value_; }
  bool operator<(const DivString& rhs) const { return value_ < rhs.value_; }
  bool operator==(const char* rhs) const { return value_ == (rhs ? rhs : ""); }
  bool operator!=(const char* rhs) const { return !(*this == rhs); }

  operator std::string() const { return value_; }
  operator const char*() const { return value_.c_str(); }

  bool startsWith(const char* prefix) const {
    if (!prefix) return false;
    const size_t n = std::strlen(prefix);
    return value_.size() >= n && value_.compare(0, n, prefix) == 0;
  }
  bool startsWith(const DivString& prefix) const { return startsWith(prefix.c_str()); }

  bool endsWith(const char* suffix) const {
    if (!suffix) return false;
    const size_t n = std::strlen(suffix);
    return value_.size() >= n && value_.compare(value_.size() - n, n, suffix) == 0;
  }
  bool endsWith(const DivString& suffix) const { return endsWith(suffix.c_str()); }

  bool equals(const char* rhs) const { return *this == rhs; }
  bool equals(const DivString& rhs) const { return *this == rhs; }
  bool equalsIgnoreCase(const DivString& rhs) const {
    if (value_.size() != rhs.value_.size()) return false;
    for (size_t i = 0; i < value_.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(value_[i])) !=
          std::tolower(static_cast<unsigned char>(rhs.value_[i]))) {
        return false;
      }
    }
    return true;
  }

  DivString substring(size_t start) const {
    if (start >= value_.size()) return DivString();
    return DivString(value_.substr(start));
  }
  DivString substring(size_t start, size_t end) const {
    if (start >= value_.size() || end <= start) return DivString();
    return DivString(value_.substr(start, std::min(end, value_.size()) - start));
  }

  int indexOf(char c, size_t from = 0) const {
    const size_t pos = value_.find(c, from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int indexOf(const char* s, size_t from = 0) const {
    const size_t pos = value_.find(s ? s : "", from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int indexOf(const DivString& s, size_t from = 0) const { return indexOf(s.c_str(), from); }

  int lastIndexOf(char c) const {
    const size_t pos = value_.rfind(c);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int lastIndexOf(const char* s) const {
    const size_t pos = value_.rfind(s ? s : "");
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int lastIndexOf(const DivString& s) const { return lastIndexOf(s.c_str()); }

  void remove(size_t index) {
    if (index < value_.size()) value_.erase(index);
  }
  void remove(size_t index, size_t count) {
    if (index < value_.size()) value_.erase(index, count);
  }

  void trim() {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(value_.begin(), value_.end(), not_space);
    auto end = std::find_if(value_.rbegin(), value_.rend(), not_space).base();
    if (begin >= end) {
      value_.clear();
    } else {
      value_ = std::string(begin, end);
    }
  }

  void toUpperCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  }
  void toLowerCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  }

  long toInt() const { return std::strtol(value_.c_str(), nullptr, 10); }
  float toFloat() const { return std::strtof(value_.c_str(), nullptr); }

  void replace(char from, char to) {
    std::replace(value_.begin(), value_.end(), from, to);
  }
  void replace(const char* from, const char* to) {
    if (!from || !*from) return;
    const std::string repl = to ? to : "";
    const std::string needle = from;
    size_t pos = 0;
    while ((pos = value_.find(needle, pos)) != std::string::npos) {
      value_.replace(pos, needle.size(), repl);
      pos += repl.size();
    }
  }
  void replace(const DivString& from, const DivString& to) { replace(from.c_str(), to.c_str()); }

  void toCharArray(char* out, size_t len) const {
    if (!out || len == 0) return;
    std::snprintf(out, len, "%s", value_.c_str());
  }

 private:
  template <typename T>
  void assignNumber(T v, int base) {
    char buf[48] = {};
    if (base == DIV_HEX) {
      std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
    } else if (base == DIV_OCT) {
      std::snprintf(buf, sizeof(buf), "%llo", static_cast<unsigned long long>(v));
    } else if (base == DIV_BIN) {
      unsigned long long x = static_cast<unsigned long long>(v);
      char tmp[65] = {};
      int pos = 64;
      tmp[pos] = '\0';
      do {
        tmp[--pos] = (x & 1ULL) ? '1' : '0';
        x >>= 1ULL;
      } while (x != 0 && pos > 0);
      value_ = &tmp[pos];
      return;
    } else {
      if constexpr (std::is_signed<T>::value) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
      } else {
        std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
      }
    }
    value_ = buf;
  }

  template <typename T>
  void assignFloat(T v, int decimals) {
    char fmt[12] = {};
    char buf[48] = {};
    if (decimals < 0) decimals = 2;
    if (decimals > 8) decimals = 8;
    std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(v));
    value_ = buf;
  }

  std::string value_;
};

inline std::ostream& operator<<(std::ostream& os, const DivString& s) {
  os << s.c_str();
  return os;
}
