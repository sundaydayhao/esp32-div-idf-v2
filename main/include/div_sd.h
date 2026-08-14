#pragma once

#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <string>

#include "div_string.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

class DivFile {
 public:
  DivFile() = default;
  DivFile(const DivFile&) = delete;
  DivFile& operator=(const DivFile&) = delete;
  DivFile(DivFile&& other) noexcept;
  DivFile& operator=(DivFile&& other) noexcept;
  ~DivFile();

  explicit operator bool() const { return file_ != nullptr || dir_ != nullptr; }
  bool isDirectory() const { return dir_ != nullptr; }
  const char* name() const { return name_.c_str(); }
  size_t size();
  bool seek(size_t pos);
  int available();
  int read();
  int read(uint8_t* buf, size_t len);
  int read(char* buf, size_t len) { return read(reinterpret_cast<uint8_t*>(buf), len); }
  size_t readBytes(uint8_t* buf, size_t len);
  size_t readBytes(char* buf, size_t len) { return readBytes(reinterpret_cast<uint8_t*>(buf), len); }
  size_t readBytesUntil(char delim, uint8_t* buf, size_t len);
  size_t readBytesUntil(char delim, char* buf, size_t len) {
    return readBytesUntil(delim, reinterpret_cast<uint8_t*>(buf), len);
  }
  DivString readStringUntil(char delim);
  size_t write(const uint8_t* data, size_t len);
  size_t write(uint8_t data);
  size_t print(const char* s);
  size_t print(const DivString& s) { return print(s.c_str()); }
  size_t print(char c);
  size_t print(int v);
  size_t print(unsigned int v);
  size_t print(long v);
  size_t print(unsigned long v);
  size_t print(float v);
  size_t print(float v, int decimals);
  size_t print(double v);
  size_t print(double v, int decimals);
  size_t println();
  template <typename T>
  size_t println(const T& v) {
    const size_t n = print(v);
    return n + println();
  }
  size_t printf(const char* fmt, ...);
  void flush();
  void close();
  DivFile openNextFile();

 private:
  friend class DivSdClass;
  static DivFile openFile(const std::string& mappedPath, const std::string& displayName, const char* mode);
  static DivFile openDir(const std::string& mappedPath, const std::string& displayName);

  FILE* file_ = nullptr;
  DIR* dir_ = nullptr;
  std::string path_;
  std::string name_;
};

class DivSdClass {
 public:
  bool begin(int cs);
  template <typename SpiLike>
  bool begin(int cs, SpiLike&) { return begin(cs); }
  void end();
  bool exists(const char* path);
  bool exists(const DivString& path) { return exists(path.c_str()); }
  bool mkdir(const char* path);
  bool mkdir(const DivString& path) { return mkdir(path.c_str()); }
  bool remove(const char* path);
  bool remove(const DivString& path) { return remove(path.c_str()); }
  bool rmdir(const char* path);
  bool rmdir(const DivString& path) { return rmdir(path.c_str()); }
  DivFile open(const char* path, const char* mode = FILE_READ);
  DivFile open(const DivString& path, const char* mode = FILE_READ) { return open(path.c_str(), mode); }
  bool mounted() const { return mounted_; }

 private:
  bool mount(int cs);
  std::string mapPath(const char* path) const;

  bool mounted_ = false;
  int cs_ = -1;
};

extern DivSdClass DivSd;
