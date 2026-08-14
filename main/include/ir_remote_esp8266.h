#pragma once

#include <cstdint>

enum decode_type_t : uint8_t {
  UNKNOWN = 0,
  NEC,
  NEC_LIKE,
  SONY,
  SONY_38K,
  SAMSUNG36,
  LG,
  LG2,
  JVC,
  DENON,
  PANASONIC,
  RC5,
  RC6,
  PIONEER,
  DISH,
  GICABLE,
  EPSON
};

struct decode_results {
  decode_type_t decode_type = UNKNOWN;
  uint64_t value = 0;
  uint16_t bits = 0;
  uint16_t rawlen = 0;
  uint16_t rawbuf[512]{};
};

inline const char* typeToString(decode_type_t type) {
  switch (type) {
    case NEC: return "NEC";
    case NEC_LIKE: return "NEC_LIKE";
    case SONY: return "SONY";
    case SONY_38K: return "SONY_38K";
    case SAMSUNG36: return "SAMSUNG36";
    case LG: return "LG";
    case LG2: return "LG2";
    case JVC: return "JVC";
    case DENON: return "DENON";
    case PANASONIC: return "PANASONIC";
    case RC5: return "RC5";
    case RC6: return "RC6";
    case PIONEER: return "PIONEER";
    case DISH: return "DISH";
    case GICABLE: return "GICABLE";
    case EPSON: return "EPSON";
    default: return "UNKNOWN";
  }
}
