#pragma once

#include "div_string.h"
#include <cstdio>
#include "ir_remote_esp8266.h"

inline DivString uint64ToString(uint64_t value, uint8_t base = 10) {
  char buf[32];
  if (base == 16) {
    snprintf(buf, sizeof(buf), "%llX", (unsigned long long)value);
  } else {
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  }
  return DivString(buf);
}

inline DivString resultToHumanReadableBasic(const decode_results* results) {
  if (!results) {
    return DivString("UNKNOWN");
  }
  DivString out = DivString(typeToString(results->decode_type));
  out += " 0x";
  out += uint64ToString(results->value, 16);
  return out;
}
